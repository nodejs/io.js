// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/effect-control-linearizer.h"

#include <cstdint>
#include <type_traits>

#include "include/v8-fast-api-calls.h"
#include "src/base/bits.h"
#include "src/base/small-vector.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/common/ptr-compr-inl.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/graph-assembler.h"
#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/schedule.h"
#include "src/compiler/string-builder-optimizer.h"
#include "src/heap/factory-inl.h"
#include "src/objects/heap-number.h"
#include "src/objects/instance-type-inl.h"
#include "src/objects/js-objects.h"
#include "src/objects/object-list-macros.h"
#include "src/objects/oddball.h"
#include "src/objects/ordered-hash-table.h"
#include "src/objects/string.h"
#include "src/objects/turbofan-types.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class MaintainSchedule { kMaintain, kDiscard };

class EffectControlLinearizer {
 public:
  EffectControlLinearizer(JSGraph* js_graph, Schedule* schedule,
                          JSGraphAssembler* graph_assembler, Zone* temp_zone,
                          SourcePositionTable* source_positions,
                          NodeOriginTable* node_origins,
                          MaintainSchedule maintain_schedule,
                          JSHeapBroker* broker,
                          StringBuilderOptimizer* string_builder_optimizer)
      : js_graph_(js_graph),
        schedule_(schedule),
        temp_zone_(temp_zone),
        maintain_schedule_(maintain_schedule),
        source_positions_(source_positions),
        node_origins_(node_origins),
        broker_(broker),
        graph_assembler_(graph_assembler),
        string_builder_optimizer_(string_builder_optimizer),
        frame_state_zapper_(nullptr) {}

  void Run();

 private:
  void UpdateEffectControlForNode(Node* node);
  void ProcessNode(Node* node, Node** frame_state);

  bool TryWireInStateEffect(Node* node, Node* frame_state);
  Node* LowerChangeBitToTagged(Node* node);
  Node* LowerChangeInt31ToTaggedSigned(Node* node);
  Node* LowerChangeInt32ToTagged(Node* node);
  Node* LowerChangeInt64ToTagged(Node* node);
  Node* LowerChangeUint32ToTagged(Node* node);
  Node* LowerChangeUint64ToTagged(Node* node);
  Node* LowerChangeFloat64ToTagged(Node* node);
  Node* LowerChangeFloat64ToTaggedPointer(Node* node);
  Node* LowerChangeFloat64HoleToTagged(Node* node);
  Node* LowerChangeTaggedSignedToInt32(Node* node);
  Node* LowerChangeTaggedSignedToInt64(Node* node);
  Node* LowerChangeTaggedToBit(Node* node);
  Node* LowerChangeTaggedToInt32(Node* node);
  Node* LowerChangeTaggedToUint32(Node* node);
  Node* LowerChangeTaggedToInt64(Node* node);
  Node* LowerChangeTaggedToTaggedSigned(Node* node);
  Node* LowerCheckInternalizedString(Node* node, Node* frame_state);
  void LowerCheckMaps(Node* node, Node* frame_state);
  Node* LowerCompareMaps(Node* node);
  Node* LowerCheckNumber(Node* node, Node* frame_state);
  Node* LowerCheckClosure(Node* node, Node* frame_state);
  Node* LowerCheckReceiver(Node* node, Node* frame_state);
  Node* LowerCheckReceiverOrNullOrUndefined(Node* node, Node* frame_state);
  Node* LowerCheckString(Node* node, Node* frame_state);
  Node* LowerCheckedBigIntToBigInt64(Node* node, Node* frame_state);
  Node* LowerCheckSymbol(Node* node, Node* frame_state);
  void LowerCheckIf(Node* node, Node* frame_state);
  Node* LowerCheckedInt32Add(Node* node, Node* frame_state);
  Node* LowerCheckedInt32Sub(Node* node, Node* frame_state);
  Node* LowerCheckedInt32Div(Node* node, Node* frame_state);
  Node* LowerCheckedInt32Mod(Node* node, Node* frame_state);
  Node* LowerCheckedUint32Div(Node* node, Node* frame_state);
  Node* LowerCheckedUint32Mod(Node* node, Node* frame_state);
  Node* LowerCheckedInt32Mul(Node* node, Node* frame_state);
  Node* LowerCheckedInt64Add(Node* node, Node* frame_state);
  Node* LowerCheckedInt64Sub(Node* node, Node* frame_state);
  Node* LowerCheckedInt64Mul(Node* node, Node* frame_state);
  Node* LowerCheckedInt64Div(Node* node, Node* frame_state);
  Node* LowerCheckedInt64Mod(Node* node, Node* frame_state);
  Node* LowerCheckedInt32ToTaggedSigned(Node* node, Node* frame_state);
  Node* LowerCheckedInt64ToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedInt64ToTaggedSigned(Node* node, Node* frame_state);
  Node* LowerCheckedUint32Bounds(Node* node, Node* frame_state);
  Node* LowerCheckedUint32ToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedUint32ToTaggedSigned(Node* node, Node* frame_state);
  Node* LowerCheckedUint64Bounds(Node* node, Node* frame_state);
  Node* LowerCheckedUint64ToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedUint64ToInt64(Node* node, Node* frame_state);
  Node* LowerCheckedUint64ToTaggedSigned(Node* node, Node* frame_state);
  Node* LowerCheckedFloat64ToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedFloat64ToInt64(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedSignedToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToArrayIndex(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToInt32(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToInt64(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToFloat64(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToTaggedSigned(Node* node, Node* frame_state);
  Node* LowerCheckedTaggedToTaggedPointer(Node* node, Node* frame_state);
  Node* LowerCheckBigInt(Node* node, Node* frame_state);
  Node* LowerChangeInt64ToBigInt(Node* node);
  Node* LowerChangeUint64ToBigInt(Node* node);
  Node* LowerTruncateBigIntToWord64(Node* node);
  Node* LowerChangeTaggedToFloat64(Node* node);
  void TruncateTaggedPointerToBit(Node* node, GraphAssemblerLabel<1>* done);
  Node* LowerTruncateTaggedToBit(Node* node);
  Node* LowerTruncateTaggedPointerToBit(Node* node);
  Node* LowerTruncateTaggedToFloat64(Node* node);
  Node* LowerTruncateTaggedToWord32(Node* node);
  Node* LowerCheckedTruncateTaggedToWord32(Node* node, Node* frame_state);
  Node* LowerAllocate(Node* node);
  Node* LowerNumberToString(Node* node);
  Node* LowerObjectIsArrayBufferView(Node* node);
  Node* LowerObjectIsBigInt(Node* node);
  Node* LowerObjectIsCallable(Node* node);
  Node* LowerObjectIsConstructor(Node* node);
  Node* LowerObjectIsDetectableCallable(Node* node);
  Node* LowerObjectIsMinusZero(Node* node);
  Node* LowerNumberIsMinusZero(Node* node);
  Node* LowerObjectIsNaN(Node* node);
  Node* LowerNumberIsNaN(Node* node);
  Node* LowerObjectIsNonCallable(Node* node);
  Node* LowerObjectIsNumber(Node* node);
  Node* LowerObjectIsReceiver(Node* node);
  Node* LowerObjectIsSmi(Node* node);
  Node* LowerObjectIsString(Node* node);
  Node* LowerObjectIsSymbol(Node* node);
  Node* LowerObjectIsUndetectable(Node* node);
  Node* LowerNumberIsFloat64Hole(Node* node);
  Node* LowerNumberIsFinite(Node* node);
  Node* LowerObjectIsFiniteNumber(Node* node);
  Node* LowerNumberIsInteger(Node* node);
  Node* LowerObjectIsInteger(Node* node);
  Node* LowerNumberIsSafeInteger(Node* node);
  Node* LowerObjectIsSafeInteger(Node* node);
  Node* LowerArgumentsLength(Node* node);
  Node* LowerRestLength(Node* node);
  Node* LowerNewDoubleElements(Node* node);
  Node* LowerNewSmiOrObjectElements(Node* node);
  Node* LowerNewArgumentsElements(Node* node);
  Node* LowerNewConsString(Node* node);
  Node* LowerSameValue(Node* node);
  Node* LowerSameValueNumbersOnly(Node* node);
  Node* LowerNumberSameValue(Node* node);
  Node* LowerDeadValue(Node* node);
  Node* LowerStringConcat(Node* node);
  Node* LowerStringToNumber(Node* node);
  Node* LowerStringCharCodeAt(Node* node);
  Node* StringCharCodeAt(Node* receiver, Node* position);
  Node* LowerStringCodePointAt(Node* node);
  Node* LowerStringToLowerCaseIntl(Node* node);
  Node* LowerStringToUpperCaseIntl(Node* node);
  Node* LowerStringFromSingleCharCode(Node* node);
  Node* LowerStringFromSingleCodePoint(Node* node);
  Node* LowerStringIndexOf(Node* node);
  Node* LowerStringSubstring(Node* node);
  Node* LowerStringFromCodePointAt(Node* node);
  Node* LowerStringLength(Node* node);
  Node* LowerStringEqual(Node* node);
  Node* LowerStringLessThan(Node* node);
  Node* LowerStringLessThanOrEqual(Node* node);
  Node* LowerBigIntAdd(Node* node, Node* frame_state);
  Node* LowerBigIntSubtract(Node* node, Node* frame_state);
  Node* LowerBigIntMultiply(Node* node, Node* frame_state);
  Node* LowerBigIntDivide(Node* node, Node* frame_state);
  Node* LowerBigIntModulus(Node* node, Node* frame_state);
  Node* LowerBigIntBitwiseAnd(Node* node, Node* frame_state);
  Node* LowerBigIntBitwiseOr(Node* node);
  Node* LowerBigIntBitwiseXor(Node* node, Node* frame_state);
  Node* LowerBigIntShiftLeft(Node* node, Node* frame_state);
  Node* LowerBigIntShiftRight(Node* node, Node* frame_state);
  Node* LowerBigIntEqual(Node* node);
  Node* LowerBigIntLessThan(Node* node);
  Node* LowerBigIntLessThanOrEqual(Node* node);
  Node* LowerBigIntNegate(Node* node);
  Node* LowerCheckFloat64Hole(Node* node, Node* frame_state);
  Node* LowerCheckNotTaggedHole(Node* node, Node* frame_state);
  Node* LowerConvertTaggedHoleToUndefined(Node* node);
  void LowerCheckEqualsInternalizedString(Node* node, Node* frame_state);
  void LowerCheckEqualsSymbol(Node* node, Node* frame_state);
  Node* LowerTypeOf(Node* node);
  Node* LowerToBoolean(Node* node);
  Node* LowerPlainPrimitiveToNumber(Node* node);
  Node* LowerPlainPrimitiveToWord32(Node* node);
  Node* LowerPlainPrimitiveToFloat64(Node* node);
  Node* LowerEnsureWritableFastElements(Node* node);
  Node* LowerMaybeGrowFastElements(Node* node, Node* frame_state);
  void LowerTransitionElementsKind(Node* node);
  Node* LowerLoadFieldByIndex(Node* node);
  Node* LowerLoadMessage(Node* node);
  Node* AdaptFastCallTypedArrayArgument(Node* node,
                                        ElementsKind expected_elements_kind,
                                        GraphAssemblerLabel<0>* bailout);
  Node* AdaptFastCallArgument(Node* node, CTypeInfo arg_type,
                              GraphAssemblerLabel<0>* if_error);
  Node* ClampFastCallArgument(Node* input, CTypeInfo::Type scalar_type);

  struct AdaptOverloadedFastCallResult {
    Node* target_address;
    Node* argument;
  };
  AdaptOverloadedFastCallResult AdaptOverloadedFastCallArgument(
      Node* node, const FastApiCallFunctionVector& c_functions,
      const fast_api_call::OverloadsResolutionResult&
          overloads_resolution_result,
      GraphAssemblerLabel<0>* if_error);

  Node* WrapFastCall(const CallDescriptor* call_descriptor, int inputs_size,
                     Node** inputs, Node* target,
                     const CFunctionInfo* c_signature, int c_arg_count,
                     Node* stack_slot);
  Node* GenerateSlowApiCall(Node* node);
  Node* LowerFastApiCall(Node* node);
  Node* LowerLoadTypedElement(Node* node);
  Node* LowerLoadDataViewElement(Node* node);
  Node* LowerLoadStackArgument(Node* node);
  void LowerStoreMessage(Node* node);
  void LowerStoreTypedElement(Node* node);
  void LowerStoreDataViewElement(Node* node);
  void LowerStoreSignedSmallElement(Node* node);
  Node* LowerFindOrderedHashMapEntry(Node* node);
  Node* LowerFindOrderedHashMapEntryForInt32Key(Node* node);
  Node* LowerFindOrderedHashSetEntry(Node* node);
  void LowerTransitionAndStoreElement(Node* node);
  void LowerTransitionAndStoreNumberElement(Node* node);
  void LowerTransitionAndStoreNonNumberElement(Node* node);
  void LowerRuntimeAbort(Node* node);
  void LowerAssertType(Node* node);
  Node* LowerConvertReceiver(Node* node);
  Node* LowerDateNow(Node* node);
  Node* LowerDoubleArrayMinMax(Node* node);

  // Lowering of optional operators.
  Maybe<Node*> LowerFloat64RoundUp(Node* node);
  Maybe<Node*> LowerFloat64RoundDown(Node* node);
  Maybe<Node*> LowerFloat64RoundTiesEven(Node* node);
  Maybe<Node*> LowerFloat64RoundTruncate(Node* node);

  // Lowering of Wasm-specific operators.
#if V8_ENABLE_WEBASSEMBLY
  void LowerWasmTrap(Node* node, Node* frame_state);
#endif

  Node* AllocateHeapNumberWithValue(Node* node);
  Node* BuildCheckedFloat64ToInt32(CheckForMinusZeroMode mode,
                                   const FeedbackSource& feedback, Node* value,
                                   Node* frame_state);
  Node* BuildCheckedFloat64ToInt64(CheckForMinusZeroMode mode,
                                   const FeedbackSource& feedback, Node* value,
                                   Node* frame_state);
  Node* BuildCheckedFloat64ToIndex(const FeedbackSource& feedback, Node* value,
                                   Node* frame_state);
  Node* BuildCheckedHeapNumberOrOddballToFloat64(CheckTaggedInputMode mode,
                                                 const FeedbackSource& feedback,
                                                 Node* value,
                                                 Node* frame_state);
  Node* BuildReverseBytes(ExternalArrayType type, Node* value);
  Node* BuildFloat64RoundDown(Node* value);
  Node* BuildFloat64RoundTruncate(Node* input);
  template <size_t VarCount, size_t VarCount2>
  void SmiTagOrOverflow(Node* value, GraphAssemblerLabel<VarCount>* if_overflow,
                        GraphAssemblerLabel<VarCount2>* done);
  Node* SmiTagOrDeopt(Node* value, const CheckParameters& params,
                      Node* frame_state);
  Node* BuildUint32Mod(Node* lhs, Node* rhs);
  Node* ComputeUnseededHash(Node* value);
  Node* LowerStringComparison(Callable const& callable, Node* node);
  Node* IsElementsKindGreaterThan(Node* kind, ElementsKind reference_kind);

  Node* BuildTypedArrayDataPointer(Node* base, Node* external);

  template <typename... Args>
  Node* CallBuiltin(Builtin builtin, Operator::Properties properties, Args...);

  Node* ChangeBitToTagged(Node* value);
  Node* ChangeFloat64ToTagged(Node* value, CheckForMinusZeroMode mode);
  Node* ChangeInt32ToSmi(Node* value);
  // In pointer compression, we smi-corrupt. This means the upper bits of a Smi
  // are not important. ChangeTaggedInt32ToSmi has a known tagged int32 as input
  // and takes advantage of the smi corruption by emitting a Bitcast node
  // instead of a Change node in order to save instructions.
  // In non pointer compression, it behaves like ChangeInt32ToSmi.
  Node* ChangeTaggedInt32ToSmi(Node* value);
  Node* ChangeInt32ToIntPtr(Node* value);
  Node* ChangeInt32ToTagged(Node* value);
  Node* ChangeInt64ToBigInt(Node* value);
  Node* ChangeInt64ToSmi(Node* value);
  Node* ChangeInt64ToTagged(Node* value);
  Node* ChangeIntPtrToInt32(Node* value);
  Node* ChangeIntPtrToSmi(Node* value);
  Node* ChangeUint64ToBigInt(Node* value);
  Node* ChangeUint64ToTagged(Node* value);
  Node* ChangeUint32ToUintPtr(Node* value);
  Node* ChangeUint32ToSmi(Node* value);
  Node* ChangeUint32ToTagged(Node* value);
  Node* ChangeSmiToIntPtr(Node* value);
  Node* ChangeSmiToInt32(Node* value);
  Node* ChangeSmiToInt64(Node* value);
  Node* ObjectIsSmi(Node* value);
  Node* JSAnyIsNotPrimitiveHeapObject(Node* value, Node* map = nullptr);
  Node* LoadFromSeqString(Node* receiver, Node* position, Node* is_one_byte);
  Node* TruncateWordToInt32(Node* value);
  Node* SmiMaxValueConstant();
  Node* SmiShiftBitsConstant();

  void IfThenElse(Node* condition, std::function<void(void)> then_body,
                  std::function<void(void)> else_body);
  Node* SizeForString(Node* length, Node* is_two_byte);
  Node* AllocateSeqString(Node* size, Node* one_byte);
  Node* AllocateSeqString(Node* size, bool one_byte);
  Node* AllocateOneByteSlicedString();
  Node* AllocateTwoByteSlicedString();
  void CopyString(Node* src, Node* dst, Node* len, Node* is_one_byte);
  Node* StringIsOneByte(Node* node);
  Node* StringIsTwoByte(Node* node);
  Node* ConstStringIsOneByte(Node* node);
  void StoreLiteralStringToBuffer(Node* buffer, Node* offset, Node* node,
                                  Node* is_one_byte);
  Node* ConvertOneByteStringToTwoByte(Node* orig, Node* total_len,
                                      Node* initialized_len);
  template <typename Char>
  void StoreConstantLiteralStringToBuffer(Node* buffer, Node* offset,
                                          Node* node, Node* is_one_byte);
  void EndStringBuilderConcatForLoopPhi(Node* node, BasicBlock* block);
  Node* EndStringBuilderConcat(Node* node);

  // Pass {bitfield} = {digit} = nullptr to construct the canoncial 0n BigInt.
  Node* BuildAllocateBigInt(Node* bitfield, Node* digit);

  Node* BuildAllocateJSExternalObject(Node* pointer);

  void TransitionElementsTo(Node* node, Node* array, ElementsKind from,
                            ElementsKind to);

  // This function tries to migrate |value| if its map |value_map| is
  // deprecated. It deopts, if either |value_map| isn't deprecated or migration
  // fails.
  void MigrateInstanceOrDeopt(Node* value, Node* value_map, Node* frame_state,
                              FeedbackSource const& feedback_source,
                              DeoptimizeReason reason);
  // Tries to migrate |value| if its map |value_map| is deprecated, but doesn't
  // deopt on failure.
  void TryMigrateInstance(Node* value, Node* value_map);

  Node* CallBuiltinForBigIntBinop(Node* left, Node* right, Builtin builtin);

  bool should_maintain_schedule() const {
    return maintain_schedule_ == MaintainSchedule::kMaintain;
  }

  Factory* factory() const { return isolate()->factory(); }
  Isolate* isolate() const { return jsgraph()->isolate(); }
  JSGraph* jsgraph() const { return js_graph_; }
  Graph* graph() const { return js_graph_->graph(); }
  Schedule* schedule() const { return schedule_; }
  Zone* temp_zone() const { return temp_zone_; }
  CommonOperatorBuilder* common() const { return js_graph_->common(); }
  SimplifiedOperatorBuilder* simplified() const {
    return js_graph_->simplified();
  }
  MachineOperatorBuilder* machine() const { return js_graph_->machine(); }
  JSGraphAssembler* gasm() const { return graph_assembler_; }
  JSHeapBroker* broker() const { return broker_; }

  JSGraph* js_graph_;
  Schedule* schedule_;
  Zone* temp_zone_;
  MaintainSchedule maintain_schedule_;
  RegionObservability region_observability_ = RegionObservability::kObservable;
  bool inside_region_ = false;
  SourcePositionTable* source_positions_;
  NodeOriginTable* node_origins_;
  JSHeapBroker* broker_;
  JSGraphAssembler* graph_assembler_;
  StringBuilderOptimizer* string_builder_optimizer_;
  Node* frame_state_zapper_;  // For tracking down compiler::Node::New crashes.
};

namespace {

struct BlockEffectControlData {
  Node* current_effect = nullptr;       // New effect.
  Node* current_control = nullptr;      // New control.
  Node* current_frame_state = nullptr;  // New frame state.
};

class BlockEffectControlMap {
 public:
  explicit BlockEffectControlMap(Zone* temp_zone) : map_(temp_zone) {}

  BlockEffectControlData& For(BasicBlock* from, BasicBlock* to) {
    return map_[std::make_pair(from->id().ToInt(), to->id().ToInt())];
  }

  const BlockEffectControlData& For(BasicBlock* from, BasicBlock* to) const {
    return map_.at(std::make_pair(from->id().ToInt(), to->id().ToInt()));
  }

 private:
  using Key = std::pair<int32_t, int32_t>;
  using Map = ZoneMap<Key, BlockEffectControlData>;

  Map map_;
};

// Effect phis that need to be updated after the first pass.
struct PendingEffectPhi {
  Node* effect_phi;
  BasicBlock* block;

  PendingEffectPhi(Node* effect_phi, BasicBlock* block)
      : effect_phi(effect_phi), block(block) {}
};

void UpdateEffectPhi(Node* node, BasicBlock* block,
                     BlockEffectControlMap* block_effects) {
  // Update all inputs to an effect phi with the effects from the given
  // block->effect map.
  DCHECK_EQ(IrOpcode::kEffectPhi, node->opcode());
  DCHECK_EQ(static_cast<size_t>(node->op()->EffectInputCount()),
            block->PredecessorCount());
  for (int i = 0; i < node->op()->EffectInputCount(); i++) {
    Node* input = node->InputAt(i);
    BasicBlock* predecessor = block->PredecessorAt(static_cast<size_t>(i));
    const BlockEffectControlData& block_effect =
        block_effects->For(predecessor, block);
    Node* effect = block_effect.current_effect;
    if (input != effect) {
      node->ReplaceInput(i, effect);
    }
  }
}

void UpdateBlockControl(BasicBlock* block,
                        BlockEffectControlMap* block_effects) {
  Node* control = block->NodeAt(0);
  DCHECK(NodeProperties::IsControl(control));

  // Do not rewire the end node.
  if (control->opcode() == IrOpcode::kEnd) return;

  // Update all inputs to the given control node with the correct control.
  DCHECK(control->opcode() == IrOpcode::kMerge ||
         static_cast<size_t>(control->op()->ControlInputCount()) ==
             block->PredecessorCount());
  if (static_cast<size_t>(control->op()->ControlInputCount()) !=
      block->PredecessorCount()) {
    return;  // We already re-wired the control inputs of this node.
  }
  for (int i = 0; i < control->op()->ControlInputCount(); i++) {
    Node* input = NodeProperties::GetControlInput(control, i);
    BasicBlock* predecessor = block->PredecessorAt(static_cast<size_t>(i));
    const BlockEffectControlData& block_effect =
        block_effects->For(predecessor, block);
    if (input != block_effect.current_control) {
      NodeProperties::ReplaceControlInput(control, block_effect.current_control,
                                          i);
    }
  }
}

void RemoveRenameNode(Node* node) {
  DCHECK(IrOpcode::kFinishRegion == node->opcode() ||
         IrOpcode::kBeginRegion == node->opcode() ||
         IrOpcode::kTypeGuard == node->opcode());
  // Update the value/context uses to the value input of the finish node and
  // the effect uses to the effect input.
  for (Edge edge : node->use_edges()) {
    DCHECK(!edge.from()->IsDead());
    if (NodeProperties::IsEffectEdge(edge)) {
      edge.UpdateTo(NodeProperties::GetEffectInput(node));
    } else {
      DCHECK(!NodeProperties::IsControlEdge(edge));
      DCHECK(!NodeProperties::IsFrameStateEdge(edge));
      edge.UpdateTo(node->InputAt(0));
    }
  }
  node->Kill();
}

void TryCloneBranch(Node* node, BasicBlock* block, Zone* temp_zone,
                    Graph* graph, CommonOperatorBuilder* common,
                    BlockEffectControlMap* block_effects,
                    SourcePositionTable* source_positions,
                    NodeOriginTable* node_origins) {
  DCHECK_EQ(IrOpcode::kBranch, node->opcode());

  // This optimization is a special case of (super)block cloning. It takes an
  // input graph as shown below and clones the Branch node for every predecessor
  // to the Merge, essentially removing the Merge completely. This avoids
  // materializing the bit for the Phi and may offer potential for further
  // branch folding optimizations (i.e. because one or more inputs to the Phi is
  // a constant). Note that there may be more Phi nodes hanging off the Merge,
  // but we can only a certain subset of them currently (actually only Phi and
  // EffectPhi nodes whose uses have either the IfTrue or IfFalse as control
  // input).

  //   Control1 ... ControlN
  //      ^            ^
  //      |            |   Cond1 ... CondN
  //      +----+  +----+     ^         ^
  //           |  |          |         |
  //           |  |     +----+         |
  //          Merge<--+ | +------------+
  //            ^      \|/
  //            |      Phi
  //            |       |
  //          Branch----+
  //            ^
  //            |
  //      +-----+-----+
  //      |           |
  //    IfTrue     IfFalse
  //      ^           ^
  //      |           |

  // The resulting graph (modulo the Phi and EffectPhi nodes) looks like this:

  // Control1 Cond1 ... ControlN CondN
  //    ^      ^           ^      ^
  //    \      /           \      /
  //     Branch     ...     Branch
  //       ^                  ^
  //       |                  |
  //   +---+---+          +---+----+
  //   |       |          |        |
  // IfTrue IfFalse ... IfTrue  IfFalse
  //   ^       ^          ^        ^
  //   |       |          |        |
  //   +--+ +-------------+        |
  //      | |  +--------------+ +--+
  //      | |                 | |
  //     Merge               Merge
  //       ^                   ^
  //       |                   |

  SourcePositionTable::Scope scope(source_positions,
                                   source_positions->GetSourcePosition(node));
  NodeOriginTable::Scope origin_scope(node_origins, "clone branch", node);
  Node* branch = node;
  Node* cond = NodeProperties::GetValueInput(branch, 0);
  if (!cond->OwnedBy(branch) || cond->opcode() != IrOpcode::kPhi) return;
  Node* merge = NodeProperties::GetControlInput(branch);
  if (merge->opcode() != IrOpcode::kMerge ||
      NodeProperties::GetControlInput(cond) != merge) {
    return;
  }
  // Grab the IfTrue/IfFalse projections of the Branch.
  BranchMatcher matcher(branch);
  // Check/collect other Phi/EffectPhi nodes hanging off the Merge.
  NodeVector phis(temp_zone);
  for (Node* const use : merge->uses()) {
    if (use == branch || use == cond) continue;
    // We cannot currently deal with non-Phi/EffectPhi nodes hanging off the
    // Merge. Ideally, we would just clone the nodes (and everything that
    // depends on it to some distant join point), but that requires knowledge
    // about dominance/post-dominance.
    if (!NodeProperties::IsPhi(use)) return;
    for (Edge edge : use->use_edges()) {
      // Right now we can only handle Phi/EffectPhi nodes whose uses are
      // directly control-dependend on either the IfTrue or the IfFalse
      // successor, because we know exactly how to update those uses.
      if (edge.from()->op()->ControlInputCount() != 1) return;
      Node* control = NodeProperties::GetControlInput(edge.from());
      if (NodeProperties::IsPhi(edge.from())) {
        control = NodeProperties::GetControlInput(control, edge.index());
      }
      if (control != matcher.IfTrue() && control != matcher.IfFalse()) return;
    }
    phis.push_back(use);
  }
  BranchHint const hint = BranchHintOf(branch->op());
  int const input_count = merge->op()->ControlInputCount();
  DCHECK_LE(1, input_count);
  Node** const inputs = graph->zone()->AllocateArray<Node*>(2 * input_count);
  Node** const merge_true_inputs = &inputs[0];
  Node** const merge_false_inputs = &inputs[input_count];
  for (int index = 0; index < input_count; ++index) {
    Node* cond1 = NodeProperties::GetValueInput(cond, index);
    Node* control1 = NodeProperties::GetControlInput(merge, index);
    Node* branch1 = graph->NewNode(common->Branch(hint), cond1, control1);
    merge_true_inputs[index] = graph->NewNode(common->IfTrue(), branch1);
    merge_false_inputs[index] = graph->NewNode(common->IfFalse(), branch1);
  }
  Node* const merge_true = matcher.IfTrue();
  Node* const merge_false = matcher.IfFalse();
  merge_true->TrimInputCount(0);
  merge_false->TrimInputCount(0);
  for (int i = 0; i < input_count; ++i) {
    merge_true->AppendInput(graph->zone(), merge_true_inputs[i]);
    merge_false->AppendInput(graph->zone(), merge_false_inputs[i]);
  }
  DCHECK_EQ(2u, block->SuccessorCount());
  NodeProperties::ChangeOp(matcher.IfTrue(), common->Merge(input_count));
  NodeProperties::ChangeOp(matcher.IfFalse(), common->Merge(input_count));
  int const true_index =
      block->SuccessorAt(0)->NodeAt(0) == matcher.IfTrue() ? 0 : 1;
  BlockEffectControlData* true_block_data =
      &block_effects->For(block, block->SuccessorAt(true_index));
  BlockEffectControlData* false_block_data =
      &block_effects->For(block, block->SuccessorAt(true_index ^ 1));
  for (Node* const phi : phis) {
    for (int index = 0; index < input_count; ++index) {
      inputs[index] = phi->InputAt(index);
    }
    inputs[input_count] = merge_true;
    Node* phi_true = graph->NewNode(phi->op(), input_count + 1, inputs);
    inputs[input_count] = merge_false;
    Node* phi_false = graph->NewNode(phi->op(), input_count + 1, inputs);
    if (phi->UseCount() == 0) {
      DCHECK_EQ(phi->opcode(), IrOpcode::kEffectPhi);
    } else {
      for (Edge edge : phi->use_edges()) {
        Node* control = NodeProperties::GetControlInput(edge.from());
        if (NodeProperties::IsPhi(edge.from())) {
          control = NodeProperties::GetControlInput(control, edge.index());
        }
        DCHECK(control == matcher.IfTrue() || control == matcher.IfFalse());
        edge.UpdateTo((control == matcher.IfTrue()) ? phi_true : phi_false);
      }
    }
    if (phi->opcode() == IrOpcode::kEffectPhi) {
      true_block_data->current_effect = phi_true;
      false_block_data->current_effect = phi_false;
    }
    phi->Kill();
  }
  // Fix up IfTrue and IfFalse and kill all dead nodes.
  if (branch == block->control_input()) {
    true_block_data->current_control = merge_true;
    false_block_data->current_control = merge_false;
  }
  branch->Kill();
  cond->Kill();
  merge->Kill();
}

}  // namespace

void EffectControlLinearizer::Run() {
  BlockEffectControlMap block_effects(temp_zone());
  ZoneVector<PendingEffectPhi> pending_effect_phis(temp_zone());
  ZoneVector<BasicBlock*> pending_block_controls(temp_zone());
  NodeVector inputs_buffer(temp_zone());

  // TODO(rmcilroy) We should not depend on having rpo_order on schedule, and
  // instead just do our own RPO walk here.
  for (BasicBlock* block : *(schedule()->rpo_order())) {
    if (block != schedule()->start() && block->PredecessorCount() == 0) {
      // Block has been removed from the schedule by a preceeding unreachable
      // node, just skip it.
      continue;
    }

    gasm()->Reset();

    BasicBlock::iterator instr = block->begin();
    BasicBlock::iterator end_instr = block->end();

    // The control node should be the first.
    Node* control = *instr;
    gasm()->AddNode(control);

    DCHECK(NodeProperties::IsControl(control));
    bool has_incoming_backedge = IrOpcode::kLoop == control->opcode();
    // Update the control inputs.
    if (has_incoming_backedge) {
      // If there are back edges, we need to update later because we have not
      // computed the control yet.
      pending_block_controls.push_back(block);
    } else {
      // If there are no back edges, we can update now.
      UpdateBlockControl(block, &block_effects);
    }
    instr++;

    // We collect in {string_builder_end_phis} the non-loop-Phis that end String
    // Builders: before processing regular nodes of the current block, we'll
    // insert truncations of those Phis. (note that when loop Phis end String
    // Builders, the truncation is inserted in the next block rather than in the
    // current block, see where BlockShouldFinalizeStringBuilders is used in
    // this file)
    base::SmallVector<Node*, 8> string_builder_end_phis;
    // Iterate over the phis and update the effect phis.
    Node* effect_phi = nullptr;
    Node* terminate = nullptr;
    for (; instr != end_instr; instr++) {
      Node* node = *instr;
      // Only go through the phis and effect phis.
      if (node->opcode() == IrOpcode::kEffectPhi) {
        // There should be at most one effect phi in a block.
        DCHECK_NULL(effect_phi);
        // IfException blocks should not have effect phis.
        DCHECK_NE(IrOpcode::kIfException, control->opcode());
        effect_phi = node;
      } else if (node->opcode() == IrOpcode::kPhi) {
        if (string_builder_optimizer_->IsNonLoopPhiStringBuilderEnd(node)) {
          string_builder_end_phis.push_back(node);
        }
        // Just skip phis.
      } else if (node->opcode() == IrOpcode::kTerminate) {
        DCHECK_NULL(terminate);
        terminate = node;
      } else {
        break;
      }
      gasm()->AddNode(node);
    }

    if (effect_phi) {
      // Make sure we update the inputs to the incoming blocks' effects.
      if (has_incoming_backedge) {
        // In case of loops, we do not update the effect phi immediately
        // because the back predecessor has not been handled yet. We just
        // record the effect phi for later processing.
        pending_effect_phis.push_back(PendingEffectPhi(effect_phi, block));
      } else {
        UpdateEffectPhi(effect_phi, block, &block_effects);
      }
    }

    Node* effect = effect_phi;
    if (effect == nullptr) {
      // There was no effect phi.
      if (block == schedule()->start()) {
        // Start block => effect is start.
        DCHECK_EQ(graph()->start(), control);
        effect = graph()->start();
      } else if (control->opcode() == IrOpcode::kEnd) {
        // End block is just a dummy, no effect needed.
        DCHECK_EQ(BasicBlock::kNone, block->control());
        DCHECK_EQ(1u, block->size());
        effect = nullptr;
      } else {
        // If all the predecessors have the same effect, we can use it as our
        // current effect.
        for (size_t i = 0; i < block->PredecessorCount(); ++i) {
          const BlockEffectControlData& data =
              block_effects.For(block->PredecessorAt(i), block);
          if (!effect) effect = data.current_effect;
          if (data.current_effect != effect) {
            effect = nullptr;
            break;
          }
        }
        if (effect == nullptr) {
          DCHECK_NE(IrOpcode::kIfException, control->opcode());
          // The input blocks do not have the same effect. We have
          // to create an effect phi node.
          inputs_buffer.clear();
          inputs_buffer.resize(block->PredecessorCount(), jsgraph()->Dead());
          inputs_buffer.push_back(control);
          effect = graph()->NewNode(
              common()->EffectPhi(static_cast<int>(block->PredecessorCount())),
              static_cast<int>(inputs_buffer.size()), &(inputs_buffer.front()));
          gasm()->AddNode(effect);
          // For loops, we update the effect phi node later to break cycles.
          if (control->opcode() == IrOpcode::kLoop) {
            pending_effect_phis.push_back(PendingEffectPhi(effect, block));
          } else {
            UpdateEffectPhi(effect, block, &block_effects);
          }
        } else if (control->opcode() == IrOpcode::kIfException) {
          // The IfException is connected into the effect chain, so we need
          // to update the effect here.
          NodeProperties::ReplaceEffectInput(control, effect);
          effect = control;
        }
      }
    }

    // Fixup the Terminate node.
    if (terminate != nullptr) {
      NodeProperties::ReplaceEffectInput(terminate, effect);
    }

    // The frame state at block entry is determined by the frame states leaving
    // all predecessors. In case there is no frame state dominating this block,
    // we can rely on a checkpoint being present before the next deoptimization.
    Node* frame_state = nullptr;
    if (block != schedule()->start()) {
      // If all the predecessors have the same effect, we can use it
      // as our current effect.
      frame_state =
          block_effects.For(block->PredecessorAt(0), block).current_frame_state;
      for (size_t i = 1; i < block->PredecessorCount(); i++) {
        if (block_effects.For(block->PredecessorAt(i), block)
                .current_frame_state != frame_state) {
          frame_state = nullptr;
          frame_state_zapper_ = graph()->end();
          break;
        }
      }
    }

    gasm()->InitializeEffectControl(effect, control);

    // Inserting trimmings for finished string builders that ended with a loop
    // Phi.
    if (string_builder_optimizer_->BlockShouldFinalizeStringBuilders(block)) {
      for (Node* node :
           string_builder_optimizer_->GetStringBuildersToFinalize(block)) {
        EndStringBuilderConcatForLoopPhi(node, block);
      }
    }

    // Finishing string builders that end with non-loop Phi nodes.
    for (Node* phi : string_builder_end_phis) {
      size_t node_count = graph()->NodeCount();

      Node* trimmed = EndStringBuilderConcat(phi);

      // Replacing uses of {phi} by {trimmed}.
      for (Edge edge : phi->use_edges()) {
        if (edge.from()->id() >= node_count) {
          // This is an edge to a new node that was introduced to do the
          // trimming; we certainly don't want to replace it.
          continue;
        }
        DCHECK(!NodeProperties::IsControlEdge(edge) &&
               !NodeProperties::IsEffectEdge(edge));
        edge.UpdateTo(trimmed);
      }
    }

    // Process the ordinary instructions.
    for (; instr != end_instr; instr++) {
      Node* node = *instr;
      ProcessNode(node, &frame_state);
    }

    switch (block->control()) {
      case BasicBlock::kGoto:
      case BasicBlock::kNone:
        break;
      case BasicBlock::kCall:
      case BasicBlock::kTailCall:
      case BasicBlock::kSwitch:
      case BasicBlock::kReturn:
      case BasicBlock::kDeoptimize:
      case BasicBlock::kThrow:
      case BasicBlock::kBranch:
        UpdateEffectControlForNode(block->control_input());
        gasm()->UpdateEffectControlWith(block->control_input());
        break;
    }

    if (!should_maintain_schedule() &&
        block->control() == BasicBlock::kBranch) {
      TryCloneBranch(block->control_input(), block, temp_zone(), graph(),
                     common(), &block_effects, source_positions_,
                     node_origins_);
    }

    // Store the effect, control and frame state for later use.
    for (BasicBlock* successor : block->successors()) {
      BlockEffectControlData* data = &block_effects.For(block, successor);
      if (data->current_effect == nullptr) {
        data->current_effect = gasm()->effect();
      }
      if (data->current_control == nullptr) {
        data->current_control = gasm()->control();
      }
      data->current_frame_state = frame_state;
    }
  }

  for (BasicBlock* pending_block_control : pending_block_controls) {
    UpdateBlockControl(pending_block_control, &block_effects);
  }
  // Update the incoming edges of the effect phis that could not be processed
  // during the first pass (because they could have incoming back edges).
  for (const PendingEffectPhi& pending_effect_phi : pending_effect_phis) {
    UpdateEffectPhi(pending_effect_phi.effect_phi, pending_effect_phi.block,
                    &block_effects);
  }

  schedule_->rpo_order()->clear();
}

void EffectControlLinearizer::UpdateEffectControlForNode(Node* node) {
  // If the node takes an effect, replace with the current one.
  if (node->op()->EffectInputCount() > 0) {
    DCHECK_EQ(1, node->op()->EffectInputCount());
    NodeProperties::ReplaceEffectInput(node, gasm()->effect());
  } else {
    // New effect chain is only started with a Start or ValueEffect node.
    DCHECK(node->op()->EffectOutputCount() == 0 ||
           node->opcode() == IrOpcode::kStart);
  }

  // Rewire control inputs.
  for (int i = 0; i < node->op()->ControlInputCount(); i++) {
    NodeProperties::ReplaceControlInput(node, gasm()->control(), i);
  }
}

void EffectControlLinearizer::ProcessNode(Node* node, Node** frame_state) {
  SourcePositionTable::Scope scope(source_positions_,
                                   source_positions_->GetSourcePosition(node));
  NodeOriginTable::Scope origin_scope(node_origins_, "process node", node);

  // If basic block is unreachable after this point, update the node's effect
  // and control inputs to mark it as dead, but don't process further.
  if (gasm()->effect() == jsgraph()->Dead()) {
    UpdateEffectControlForNode(node);
    return;
  }

  // If the node needs to be wired into the effect/control chain, do this
  // here. Pass current frame state for lowering to eager deoptimization.
  if (TryWireInStateEffect(node, *frame_state)) {
    return;
  }

  // If the node has a visible effect, then there must be a checkpoint in the
  // effect chain before we are allowed to place another eager deoptimization
  // point. We zap the frame state to ensure this invariant is maintained.
  if (region_observability_ == RegionObservability::kObservable &&
      !node->op()->HasProperty(Operator::kNoWrite)) {
    if (V8_UNLIKELY(node->opcode() == IrOpcode::kCheckMaps ||
                    node->opcode() == IrOpcode::kTransitionElementsKind)) {
      // These side effects are not JS-observable so we can keep the
      // FrameState, when we don't lower those nodes here, but in the Turboshaft
      // pipeline.
      DCHECK(v8_flags.turboshaft);
    } else {
      *frame_state = nullptr;
      frame_state_zapper_ = node;
    }
  }

  // Remove the end markers of 'atomic' allocation region because the
  // region should be wired-in now.
  if (node->opcode() == IrOpcode::kFinishRegion) {
    // Reset the current region observability.
    region_observability_ = RegionObservability::kObservable;
    inside_region_ = false;
    // Update the value uses to the value input of the finish node and
    // the effect uses to the effect input.
    if (!v8_flags.turboshaft) {
      RemoveRenameNode(node);
      return;
    }
  }
  if (node->opcode() == IrOpcode::kBeginRegion) {
    // Determine the observability for this region and use that for all
    // nodes inside the region (i.e. ignore the absence of kNoWrite on
    // StoreField and other operators).
    DCHECK_NE(RegionObservability::kNotObservable, region_observability_);
    region_observability_ = RegionObservabilityOf(node->op());
    inside_region_ = true;
    // Update the value uses to the value input of the finish node and
    // the effect uses to the effect input.
    if (!v8_flags.turboshaft) {
      RemoveRenameNode(node);
      return;
    }
  }
  if (node->opcode() == IrOpcode::kTypeGuard) {
    return RemoveRenameNode(node);
  }

  // Special treatment for checkpoint nodes.
  if (node->opcode() == IrOpcode::kCheckpoint) {
    // Unlink the check point; effect uses will be updated to the incoming
    // effect that is passed. The frame state is preserved for lowering.
    DCHECK_EQ(RegionObservability::kObservable, region_observability_);
    *frame_state = NodeProperties::GetFrameStateInput(node);
    if (!v8_flags.turboshaft) return;
    // We keep checkpoints to allow Turboshaft's graph builder to recompute the
    // correct FrameStates for nodes.
  }

  if (node->opcode() == IrOpcode::kStoreField) {
    // Mark stores outside a region as non-initializing and non-transitioning.
    if (!inside_region_) {
      const FieldAccess access = FieldAccessOf(node->op());
      NodeProperties::ChangeOp(node, simplified()->StoreField(access, false));
    }
  }

  // The IfSuccess nodes should always start a basic block (and basic block
  // start nodes are not handled in the ProcessNode method).
  DCHECK_NE(IrOpcode::kIfSuccess, node->opcode());

  UpdateEffectControlForNode(node);

  gasm()->AddNode(node);

  if (node->opcode() == IrOpcode::kUnreachable) {
    // Break the effect chain on {Unreachable} and reconnect to the graph end.
    // Mark the following code for deletion by connecting to the {Dead} node.
    gasm()->ConnectUnreachableToEnd();
  }
}

bool EffectControlLinearizer::TryWireInStateEffect(Node* node,
                                                   Node* frame_state) {
  Node* result = nullptr;
  switch (node->opcode()) {
    case IrOpcode::kChangeBitToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeBitToTagged(node);
      break;
    case IrOpcode::kChangeInt31ToTaggedSigned:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeInt31ToTaggedSigned(node);
      break;
    case IrOpcode::kChangeInt32ToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeInt32ToTagged(node);
      break;
    case IrOpcode::kChangeInt64ToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeInt64ToTagged(node);
      break;
    case IrOpcode::kChangeUint32ToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeUint32ToTagged(node);
      break;
    case IrOpcode::kChangeUint64ToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeUint64ToTagged(node);
      break;
    case IrOpcode::kChangeFloat64ToTagged:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeFloat64ToTagged(node);
      break;
    case IrOpcode::kChangeFloat64ToTaggedPointer:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeFloat64ToTaggedPointer(node);
      break;
    case IrOpcode::kChangeFloat64HoleToTagged:
      result = LowerChangeFloat64HoleToTagged(node);
      break;
    case IrOpcode::kChangeTaggedSignedToInt32:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedSignedToInt32(node);
      break;
    case IrOpcode::kChangeTaggedSignedToInt64:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedSignedToInt64(node);
      break;
    case IrOpcode::kChangeTaggedToBit:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToBit(node);
      break;
    case IrOpcode::kChangeTaggedToInt32:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToInt32(node);
      break;
    case IrOpcode::kChangeTaggedToUint32:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToUint32(node);
      break;
    case IrOpcode::kChangeTaggedToInt64:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToInt64(node);
      break;
    case IrOpcode::kChangeTaggedToFloat64:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToFloat64(node);
      break;
    case IrOpcode::kChangeTaggedToTaggedSigned:
      if (v8_flags.turboshaft) return false;
      result = LowerChangeTaggedToTaggedSigned(node);
      break;
    case IrOpcode::kTruncateTaggedToBit:
      if (v8_flags.turboshaft) return false;
      result = LowerTruncateTaggedToBit(node);
      break;
    case IrOpcode::kTruncateTaggedPointerToBit:
      if (v8_flags.turboshaft) return false;
      result = LowerTruncateTaggedPointerToBit(node);
      break;
    case IrOpcode::kTruncateTaggedToFloat64:
      if (v8_flags.turboshaft) return false;
      result = LowerTruncateTaggedToFloat64(node);
      break;
    case IrOpcode::kCheckClosure:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckClosure(node, frame_state);
      break;
    case IrOpcode::kCheckMaps:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      LowerCheckMaps(node, frame_state);
      break;
    case IrOpcode::kCompareMaps:
      if (v8_flags.turboshaft) return false;
      result = LowerCompareMaps(node);
      break;
    case IrOpcode::kCheckNumber:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckNumber(node, frame_state);
      break;
    case IrOpcode::kCheckReceiver:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckReceiver(node, frame_state);
      break;
    case IrOpcode::kCheckReceiverOrNullOrUndefined:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckReceiverOrNullOrUndefined(node, frame_state);
      break;
    case IrOpcode::kCheckSymbol:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckSymbol(node, frame_state);
      break;
    case IrOpcode::kCheckString:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckString(node, frame_state);
      break;
    case IrOpcode::kCheckedUint64ToInt64:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint64ToInt64(node, frame_state);
      break;
    case IrOpcode::kCheckedBigIntToBigInt64:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedBigIntToBigInt64(node, frame_state);
      break;
    case IrOpcode::kCheckInternalizedString:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckInternalizedString(node, frame_state);
      break;
    case IrOpcode::kCheckIf:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      LowerCheckIf(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32Add:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32Add(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32Sub:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32Sub(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32Div:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32Div(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32Mod:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32Mod(node, frame_state);
      break;
    case IrOpcode::kCheckedUint32Div:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint32Div(node, frame_state);
      break;
    case IrOpcode::kCheckedUint32Mod:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint32Mod(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32Mul:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32Mul(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64Add:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64Add(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64Sub:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64Sub(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64Mul:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64Mul(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64Div:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64Div(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64Mod:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64Mod(node, frame_state);
      break;
    case IrOpcode::kCheckedInt32ToTaggedSigned:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt32ToTaggedSigned(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64ToInt32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64ToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedInt64ToTaggedSigned:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedInt64ToTaggedSigned(node, frame_state);
      break;
    case IrOpcode::kCheckedUint32Bounds:
      if (v8_flags.turboshaft) {
        // We do have a {frame_state} iff we do actually need it for a deopt
        // (`kAbortOnOutOfBounds` is not set).
        DCHECK_IMPLIES(!(CheckBoundsParametersOf(node->op()).flags() &
                         CheckBoundsFlag::kAbortOnOutOfBounds),
                       frame_state != nullptr);
        if (frame_state) gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint32Bounds(node, frame_state);
      break;
    case IrOpcode::kCheckedUint32ToInt32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint32ToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedUint32ToTaggedSigned:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint32ToTaggedSigned(node, frame_state);
      break;
    case IrOpcode::kCheckedUint64Bounds:
      if (v8_flags.turboshaft) {
        // We do have a {frame_state} iff we do actually need it for a deopt
        // (`kAbortOnOutOfBounds` is not set).
        DCHECK_IMPLIES(!(CheckBoundsParametersOf(node->op()).flags() &
                         CheckBoundsFlag::kAbortOnOutOfBounds),
                       frame_state != nullptr);
        if (frame_state) gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint64Bounds(node, frame_state);
      break;
    case IrOpcode::kCheckedUint64ToInt32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint64ToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedUint64ToTaggedSigned:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedUint64ToTaggedSigned(node, frame_state);
      break;
    case IrOpcode::kCheckedFloat64ToInt32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedFloat64ToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedFloat64ToInt64:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedFloat64ToInt64(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedSignedToInt32:
      if (frame_state == nullptr) {
        FATAL("No frame state (zapped by #%d: %s)", frame_state_zapper_->id(),
              frame_state_zapper_->op()->mnemonic());
      }
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedSignedToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToArrayIndex:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToArrayIndex(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToInt32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToInt32(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToInt64:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToInt64(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToFloat64:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToFloat64(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToTaggedSigned:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToTaggedSigned(node, frame_state);
      break;
    case IrOpcode::kCheckedTaggedToTaggedPointer:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTaggedToTaggedPointer(node, frame_state);
      break;
    case IrOpcode::kCheckBigInt:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckBigInt(node, frame_state);
      break;
    case IrOpcode::kChangeInt64ToBigInt:
      if (v8_flags.turboshaft) {
        DCHECK(machine()->Is64());
        // ChangeInt64ToBigInt is allocting when lowered, so we must fix its
        // position in the effect chain such that it is non-floating after ECL
        // and cannot mess up when rescheduling (e.g. in Turboshaft's graph
        // builder).
        result = gasm()->Chained(node->op(), node->InputAt(0));
      } else {
        result = LowerChangeInt64ToBigInt(node);
      }
      break;
    case IrOpcode::kChangeUint64ToBigInt:
      if (v8_flags.turboshaft) {
        DCHECK(machine()->Is64());
        // ChangeUint64ToBigInt is allocting when lowered, so we must fix its
        // position in the effect chain such that it is non-floating after ECL
        // and cannot mess up when rescheduling (e.g. in Turboshaft's graph
        // builder).
        result = gasm()->Chained(node->op(), node->InputAt(0));
      } else {
        result = LowerChangeUint64ToBigInt(node);
      }
      break;
    case IrOpcode::kTruncateBigIntToWord64:
      if (v8_flags.turboshaft) return false;
      result = LowerTruncateBigIntToWord64(node);
      break;
    case IrOpcode::kTruncateTaggedToWord32:
      if (v8_flags.turboshaft) return false;
      result = LowerTruncateTaggedToWord32(node);
      break;
    case IrOpcode::kCheckedTruncateTaggedToWord32:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckedTruncateTaggedToWord32(node, frame_state);
      break;
    case IrOpcode::kNumberToString:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberToString(node);
      break;
    case IrOpcode::kObjectIsArrayBufferView:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsArrayBufferView(node);
      break;
    case IrOpcode::kObjectIsBigInt:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsBigInt(node);
      break;
    case IrOpcode::kObjectIsCallable:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsCallable(node);
      break;
    case IrOpcode::kObjectIsConstructor:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsConstructor(node);
      break;
    case IrOpcode::kObjectIsDetectableCallable:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsDetectableCallable(node);
      break;
    case IrOpcode::kObjectIsMinusZero:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsMinusZero(node);
      break;
    case IrOpcode::kNumberIsMinusZero:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsMinusZero(node);
      break;
    case IrOpcode::kObjectIsNaN:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsNaN(node);
      break;
    case IrOpcode::kNumberIsNaN:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsNaN(node);
      break;
    case IrOpcode::kObjectIsNonCallable:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsNonCallable(node);
      break;
    case IrOpcode::kObjectIsNumber:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsNumber(node);
      break;
    case IrOpcode::kObjectIsReceiver:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsReceiver(node);
      break;
    case IrOpcode::kObjectIsSmi:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsSmi(node);
      break;
    case IrOpcode::kObjectIsString:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsString(node);
      break;
    case IrOpcode::kObjectIsSymbol:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsSymbol(node);
      break;
    case IrOpcode::kObjectIsUndetectable:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsUndetectable(node);
      break;
    case IrOpcode::kArgumentsLength:
      if (v8_flags.turboshaft) return false;
      result = LowerArgumentsLength(node);
      break;
    case IrOpcode::kRestLength:
      if (v8_flags.turboshaft) return false;
      result = LowerRestLength(node);
      break;
    case IrOpcode::kToBoolean:
      if (v8_flags.turboshaft) return false;
      result = LowerToBoolean(node);
      break;
    case IrOpcode::kTypeOf:
      if (v8_flags.turboshaft) return false;
      result = LowerTypeOf(node);
      break;
    case IrOpcode::kNewDoubleElements:
      if (v8_flags.turboshaft) return false;
      result = LowerNewDoubleElements(node);
      break;
    case IrOpcode::kNewSmiOrObjectElements:
      if (v8_flags.turboshaft) return false;
      result = LowerNewSmiOrObjectElements(node);
      break;
    case IrOpcode::kNewArgumentsElements:
      if (v8_flags.turboshaft) return false;
      result = LowerNewArgumentsElements(node);
      break;
    case IrOpcode::kNewConsString:
      if (v8_flags.turboshaft) return false;
      result = LowerNewConsString(node);
      break;
    case IrOpcode::kSameValue:
      if (v8_flags.turboshaft) return false;
      result = LowerSameValue(node);
      break;
    case IrOpcode::kSameValueNumbersOnly:
      if (v8_flags.turboshaft) return false;
      result = LowerSameValueNumbersOnly(node);
      break;
    case IrOpcode::kNumberSameValue:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberSameValue(node);
      break;
    case IrOpcode::kDeadValue:
      if (v8_flags.turboshaft) return false;
      result = LowerDeadValue(node);
      break;
    case IrOpcode::kStringConcat:
      if (v8_flags.turboshaft) return false;
      result = LowerStringConcat(node);
      break;
    case IrOpcode::kStringFromSingleCharCode:
      if (v8_flags.turboshaft) return false;
      result = LowerStringFromSingleCharCode(node);
      break;
    case IrOpcode::kStringFromSingleCodePoint:
      if (v8_flags.turboshaft) return false;
      result = LowerStringFromSingleCodePoint(node);
      break;
    case IrOpcode::kStringIndexOf:
      if (v8_flags.turboshaft) return false;
      result = LowerStringIndexOf(node);
      break;
    case IrOpcode::kStringFromCodePointAt:
      if (v8_flags.turboshaft) return false;
      result = LowerStringFromCodePointAt(node);
      break;
    case IrOpcode::kStringLength:
      if (v8_flags.turboshaft) return false;
      result = LowerStringLength(node);
      break;
    case IrOpcode::kStringToNumber:
      if (v8_flags.turboshaft) return false;
      result = LowerStringToNumber(node);
      break;
    case IrOpcode::kStringCharCodeAt:
      if (v8_flags.turboshaft) return false;
      result = LowerStringCharCodeAt(node);
      break;
    case IrOpcode::kStringCodePointAt:
      if (v8_flags.turboshaft) return false;
      result = LowerStringCodePointAt(node);
      break;
    case IrOpcode::kStringToLowerCaseIntl:
      if (v8_flags.turboshaft) return false;
      result = LowerStringToLowerCaseIntl(node);
      break;
    case IrOpcode::kStringToUpperCaseIntl:
      if (v8_flags.turboshaft) return false;
      result = LowerStringToUpperCaseIntl(node);
      break;
    case IrOpcode::kStringSubstring:
      if (v8_flags.turboshaft) return false;
      result = LowerStringSubstring(node);
      break;
    case IrOpcode::kStringEqual:
      if (v8_flags.turboshaft) return false;
      result = LowerStringEqual(node);
      break;
    case IrOpcode::kStringLessThan:
      if (v8_flags.turboshaft) return false;
      result = LowerStringLessThan(node);
      break;
    case IrOpcode::kStringLessThanOrEqual:
      if (v8_flags.turboshaft) return false;
      result = LowerStringLessThanOrEqual(node);
      break;
    case IrOpcode::kBigIntAdd:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntAdd(node, frame_state);
      break;
    case IrOpcode::kBigIntSubtract:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntSubtract(node, frame_state);
      break;
    case IrOpcode::kBigIntMultiply:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntMultiply(node, frame_state);
      break;
    case IrOpcode::kBigIntDivide:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntDivide(node, frame_state);
      break;
    case IrOpcode::kBigIntModulus:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntModulus(node, frame_state);
      break;
    case IrOpcode::kBigIntBitwiseAnd:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntBitwiseAnd(node, frame_state);
      break;
    case IrOpcode::kBigIntBitwiseOr:
      if (v8_flags.turboshaft) {
        // Although bitwise or doesn't need a FrameState (because it cannot
        // deopt), we keep it here for Turboshaft, because this allows us to
        // handle `or` uniformly with all other binary operations.
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntBitwiseOr(node);
      break;
    case IrOpcode::kBigIntBitwiseXor:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntBitwiseXor(node, frame_state);
      break;
    case IrOpcode::kBigIntShiftLeft:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntShiftLeft(node, frame_state);
      break;
    case IrOpcode::kBigIntShiftRight:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerBigIntShiftRight(node, frame_state);
      break;
    case IrOpcode::kBigIntEqual:
      if (v8_flags.turboshaft) return false;
      result = LowerBigIntEqual(node);
      break;
    case IrOpcode::kBigIntLessThan:
      if (v8_flags.turboshaft) return false;
      result = LowerBigIntLessThan(node);
      break;
    case IrOpcode::kBigIntLessThanOrEqual:
      if (v8_flags.turboshaft) return false;
      result = LowerBigIntLessThanOrEqual(node);
      break;
    case IrOpcode::kBigIntNegate:
      if (v8_flags.turboshaft) return false;
      result = LowerBigIntNegate(node);
      break;
    case IrOpcode::kNumberIsFloat64Hole:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsFloat64Hole(node);
      break;
    case IrOpcode::kNumberIsFinite:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsFinite(node);
      break;
    case IrOpcode::kObjectIsFiniteNumber:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsFiniteNumber(node);
      break;
    case IrOpcode::kNumberIsInteger:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsInteger(node);
      break;
    case IrOpcode::kObjectIsInteger:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsInteger(node);
      break;
    case IrOpcode::kNumberIsSafeInteger:
      if (v8_flags.turboshaft) return false;
      result = LowerNumberIsSafeInteger(node);
      break;
    case IrOpcode::kObjectIsSafeInteger:
      if (v8_flags.turboshaft) return false;
      result = LowerObjectIsSafeInteger(node);
      break;
    case IrOpcode::kCheckFloat64Hole:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckFloat64Hole(node, frame_state);
      break;
    case IrOpcode::kCheckNotTaggedHole:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerCheckNotTaggedHole(node, frame_state);
      break;
    case IrOpcode::kConvertTaggedHoleToUndefined:
      if (v8_flags.turboshaft) return false;
      result = LowerConvertTaggedHoleToUndefined(node);
      break;
    case IrOpcode::kCheckEqualsInternalizedString:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      LowerCheckEqualsInternalizedString(node, frame_state);
      break;
    case IrOpcode::kAllocate:
      if (v8_flags.turboshaft) return false;
      result = LowerAllocate(node);
      break;
    case IrOpcode::kCheckEqualsSymbol:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      LowerCheckEqualsSymbol(node, frame_state);
      break;
    case IrOpcode::kPlainPrimitiveToNumber:
      if (v8_flags.turboshaft) return false;
      result = LowerPlainPrimitiveToNumber(node);
      break;
    case IrOpcode::kPlainPrimitiveToWord32:
      if (v8_flags.turboshaft) return false;
      result = LowerPlainPrimitiveToWord32(node);
      break;
    case IrOpcode::kPlainPrimitiveToFloat64:
      if (v8_flags.turboshaft) return false;
      result = LowerPlainPrimitiveToFloat64(node);
      break;
    case IrOpcode::kEnsureWritableFastElements:
      if (v8_flags.turboshaft) return false;
      result = LowerEnsureWritableFastElements(node);
      break;
    case IrOpcode::kMaybeGrowFastElements:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerMaybeGrowFastElements(node, frame_state);
      break;
    case IrOpcode::kTransitionElementsKind:
      if (v8_flags.turboshaft) return false;
      LowerTransitionElementsKind(node);
      break;
    case IrOpcode::kLoadMessage:
      if (v8_flags.turboshaft) return false;
      result = LowerLoadMessage(node);
      break;
    case IrOpcode::kStoreMessage:
      if (v8_flags.turboshaft) return false;
      LowerStoreMessage(node);
      break;
    case IrOpcode::kFastApiCall:
      if (v8_flags.turboshaft) {
        gasm()->Checkpoint(FrameState{frame_state});
        return false;
      }
      result = LowerFastApiCall(node);
      break;
    case IrOpcode::kLoadFieldByIndex:
      if (v8_flags.turboshaft) return false;
      result = LowerLoadFieldByIndex(node);
      break;
    case IrOpcode::kLoadTypedElement:
      if (v8_flags.turboshaft) return false;
      result = LowerLoadTypedElement(node);
      break;
    case IrOpcode::kLoadDataViewElement:
      if (v8_flags.turboshaft) return false;
      result = LowerLoadDataViewElement(node);
      break;
    case IrOpcode::kLoadStackArgument:
      if (v8_flags.turboshaft) return false;
      result = LowerLoadStackArgument(node);
      break;
    case IrOpcode::kStoreTypedElement:
      if (v8_flags.turboshaft) return false;
      LowerStoreTypedElement(node);
      break;
    case IrOpcode::kStoreDataViewElement:
      if (v8_flags.turboshaft) return false;
      LowerStoreDataViewElement(node);
      break;
    case IrOpcode::kStoreSignedSmallElement:
      if (v8_flags.turboshaft) return false;
      LowerStoreSignedSmallElement(node);
      break;
    case IrOpcode::kFindOrderedHashMapEntry:
      if (v8_flags.turboshaft) return false;
      result = LowerFindOrderedHashMapEntry(node);
      break;
    case IrOpcode::kFindOrderedHashMapEntryForInt32Key:
      if (v8_flags.turboshaft) return false;
      result = LowerFindOrderedHashMapEntryForInt32Key(node);
      break;
    case IrOpcode::kFindOrderedHashSetEntry:
      if (v8_flags.turboshaft) return false;
      result = LowerFindOrderedHashSetEntry(node);
      break;
    case IrOpcode::kTransitionAndStoreNumberElement:
      if (v8_flags.turboshaft) return false;
      LowerTransitionAndStoreNumberElement(node);
      break;
    case IrOpcode::kTransitionAndStoreNonNumberElement:
      if (v8_flags.turboshaft) return false;
      LowerTransitionAndStoreNonNumberElement(node);
      break;
    case IrOpcode::kTransitionAndStoreElement:
      if (v8_flags.turboshaft) return false;
      LowerTransitionAndStoreElement(node);
      break;
    case IrOpcode::kRuntimeAbort:
      if (v8_flags.turboshaft) return false;
      LowerRuntimeAbort(node);
      break;
    case IrOpcode::kAssertType:
      if (v8_flags.turboshaft) return false;
      LowerAssertType(node);
      break;
    case IrOpcode::kConvertReceiver:
      if (v8_flags.turboshaft) return false;
      result = LowerConvertReceiver(node);
      break;
    case IrOpcode::kFloat64RoundUp:
      if (v8_flags.turboshaft) return false;
      if (!LowerFloat64RoundUp(node).To(&result)) {
        return false;
      }
      break;
    case IrOpcode::kFloat64RoundDown:
      if (v8_flags.turboshaft) return false;
      if (!LowerFloat64RoundDown(node).To(&result)) {
        return false;
      }
      break;
    case IrOpcode::kFloat64RoundTruncate:
      if (v8_flags.turboshaft) return false;
      if (!LowerFloat64RoundTruncate(node).To(&result)) {
        return false;
      }
      break;
    case IrOpcode::kFloat64RoundTiesEven:
      if (v8_flags.turboshaft) return false;
      if (!LowerFloat64RoundTiesEven(node).To(&result)) {
        return false;
      }
      break;
    case IrOpcode::kDateNow:
      if (v8_flags.turboshaft) return false;
      result = LowerDateNow(node);
      break;
    case IrOpcode::kDoubleArrayMax:
    case IrOpcode::kDoubleArrayMin:
      if (v8_flags.turboshaft) return false;
      result = LowerDoubleArrayMinMax(node);
      break;
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kTrapIf:
    case IrOpcode::kTrapUnless:
      LowerWasmTrap(node, frame_state);
      return false;
    case IrOpcode::kLoadTrapOnNull:
    case IrOpcode::kStoreTrapOnNull:
      FATAL("Inlined wasm code may not use trap handler instructions");
#endif
    default:
      return false;
  }

  if ((result ? 1 : 0) != node->op()->ValueOutputCount()) {
    FATAL(
        "Effect control linearizer lowering of '%s':"
        " value output count does not agree.",
        node->op()->mnemonic());
  }

  NodeProperties::ReplaceUses(node, result, gasm()->effect(),
                              gasm()->control());
  return true;
}

#define __ gasm()->

Node* EffectControlLinearizer::LowerChangeFloat64ToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  CheckForMinusZeroMode mode = CheckMinusZeroModeOf(node->op());
  Node* value = node->InputAt(0);
  return ChangeFloat64ToTagged(value, mode);
}

Node* EffectControlLinearizer::ChangeFloat64ToTagged(
    Node* value, CheckForMinusZeroMode mode) {
  auto done = __ MakeLabel(MachineRepresentation::kTagged);
  auto if_heapnumber = __ MakeDeferredLabel();
  auto if_int32 = __ MakeLabel();

  Node* value32 = __ RoundFloat64ToInt32(value);
  __ GotoIf(__ Float64Equal(value, __ ChangeInt32ToFloat64(value32)),
            &if_int32);
  __ Goto(&if_heapnumber);

  __ Bind(&if_int32);
  {
    if (mode == CheckForMinusZeroMode::kCheckForMinusZero) {
      Node* zero = __ Int32Constant(0);
      auto if_zero = __ MakeDeferredLabel();
      auto if_smi = __ MakeLabel();

      __ GotoIf(__ Word32Equal(value32, zero), &if_zero);
      __ Goto(&if_smi);

      __ Bind(&if_zero);
      {
        // In case of 0, we need to check the high bits for the IEEE -0 pattern.
        __ GotoIf(__ Int32LessThan(__ Float64ExtractHighWord32(value), zero),
                  &if_heapnumber);
        __ Goto(&if_smi);
      }

      __ Bind(&if_smi);
    }

    if (SmiValuesAre32Bits()) {
      Node* value_smi = ChangeInt32ToSmi(value32);
      __ Goto(&done, value_smi);
    } else {
      SmiTagOrOverflow(value32, &if_heapnumber, &done);
    }
  }

  __ Bind(&if_heapnumber);
  {
    Node* value_number = AllocateHeapNumberWithValue(value);
    __ Goto(&done, value_number);
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeFloat64ToTaggedPointer(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return AllocateHeapNumberWithValue(value);
}

Node* EffectControlLinearizer::LowerChangeBitToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeBitToTagged(value);
}

Node* EffectControlLinearizer::ChangeBitToTagged(Node* value) {
  auto if_true = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  __ GotoIf(value, &if_true);
  __ Goto(&done, __ FalseConstant());

  __ Bind(&if_true);
  __ Goto(&done, __ TrueConstant());

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeInt31ToTaggedSigned(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeInt32ToSmi(value);
}

Node* EffectControlLinearizer::LowerChangeInt32ToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeInt32ToTagged(value);
}

Node* EffectControlLinearizer::ChangeInt32ToTagged(Node* value) {
  if (SmiValuesAre32Bits()) {
    return ChangeInt32ToSmi(value);
  }
  DCHECK(SmiValuesAre31Bits());

  auto if_overflow = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  SmiTagOrOverflow(value, &if_overflow, &done);

  __ Bind(&if_overflow);
  Node* number = AllocateHeapNumberWithValue(__ ChangeInt32ToFloat64(value));
  __ Goto(&done, number);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeInt64ToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  return ChangeInt64ToTagged(value);
}

Node* EffectControlLinearizer::LowerChangeUint32ToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeUint32ToTagged(value);
}

Node* EffectControlLinearizer::ChangeUint32ToTagged(Node* value) {
  auto if_not_in_smi_range = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  Node* check = __ Uint32LessThanOrEqual(value, SmiMaxValueConstant());
  __ GotoIfNot(check, &if_not_in_smi_range);
  __ Goto(&done, ChangeUint32ToSmi(value));

  __ Bind(&if_not_in_smi_range);
  Node* number = AllocateHeapNumberWithValue(__ ChangeUint32ToFloat64(value));

  __ Goto(&done, number);
  __ Bind(&done);

  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeUint64ToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  return ChangeUint64ToTagged(value);
}

Node* EffectControlLinearizer::LowerChangeTaggedSignedToInt32(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeSmiToInt32(value);
}

Node* EffectControlLinearizer::LowerChangeTaggedSignedToInt64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ChangeSmiToInt64(value);
}

Node* EffectControlLinearizer::LowerChangeTaggedToBit(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return __ TaggedEqual(value, __ TrueConstant());
}

void EffectControlLinearizer::TruncateTaggedPointerToBit(
    Node* node, GraphAssemblerLabel<1>* done) {
  Node* value = node->InputAt(0);

  auto if_heapnumber = __ MakeDeferredLabel();
  auto if_bigint = __ MakeDeferredLabel();

  Node* zero = __ Int32Constant(0);
  Node* fzero = __ Float64Constant(0.0);

  // Check if {value} is false.
  __ GotoIf(__ TaggedEqual(value, __ FalseConstant()), done, zero);

  // Check if {value} is the empty string.
  __ GotoIf(__ TaggedEqual(value, __ EmptyStringConstant()), done, zero);

  // Load the map of {value}.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);

  // Check if the {value} is undetectable and immediately return false.
  // This includes undefined and null.
  Node* value_map_bitfield =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  __ GotoIfNot(
      __ Word32Equal(
          __ Word32And(value_map_bitfield,
                       __ Int32Constant(Map::Bits1::IsUndetectableBit::kMask)),
          zero),
      done, zero);

  // Check if {value} is a HeapNumber.
  __ GotoIf(__ TaggedEqual(value_map, __ HeapNumberMapConstant()),
            &if_heapnumber);

  // Check if {value} is a BigInt.
  __ GotoIf(__ TaggedEqual(value_map, __ BigIntMapConstant()), &if_bigint);

  // All other values that reach here are true.
  __ Goto(done, __ Int32Constant(1));

  __ Bind(&if_heapnumber);
  {
    // For HeapNumber {value}, just check that its value is not 0.0, -0.0 or
    // NaN.
    Node* value_value =
        __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
    __ Goto(done, __ Float64LessThan(fzero, __ Float64Abs(value_value)));
  }

  __ Bind(&if_bigint);
  {
    Node* bitfield = __ LoadField(AccessBuilder::ForBigIntBitfield(), value);
    Node* length_is_zero = __ Word32Equal(
        __ Word32And(bitfield, __ Int32Constant(BigInt::LengthBits::kMask)),
        __ Int32Constant(0));
    __ Goto(done, __ Word32Equal(length_is_zero, zero));
  }
}

Node* EffectControlLinearizer::LowerTruncateTaggedToBit(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  auto done = __ MakeLabel(MachineRepresentation::kBit);
  auto if_smi = __ MakeDeferredLabel();

  Node* value = node->InputAt(0);
  __ GotoIf(ObjectIsSmi(value), &if_smi);

  TruncateTaggedPointerToBit(node, &done);

  __ Bind(&if_smi);
  {
    // If {value} is a Smi, then we only need to check that it's not zero.
    __ Goto(&done, __ Word32Equal(__ TaggedEqual(value, __ SmiConstant(0)),
                                  __ Int32Constant(0)));
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerTruncateTaggedPointerToBit(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  TruncateTaggedPointerToBit(node, &done);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeTaggedToInt32(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  __ Goto(&done, ChangeSmiToInt32(value));

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  vfalse = __ ChangeFloat64ToInt32(vfalse);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeTaggedToUint32(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  __ Goto(&done, ChangeSmiToInt32(value));

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  vfalse = __ ChangeFloat64ToUint32(vfalse);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeTaggedToInt64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord64);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  __ Goto(&done, ChangeSmiToInt64(value));

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  vfalse = __ ChangeFloat64ToInt64(vfalse);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerChangeTaggedToFloat64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return LowerTruncateTaggedToFloat64(node);
}

Node* EffectControlLinearizer::LowerChangeTaggedToTaggedSigned(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTaggedSigned);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  __ Goto(&done, value);

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  vfalse = __ ChangeFloat64ToInt32(vfalse);
  vfalse = ChangeInt32ToSmi(vfalse);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerTruncateTaggedToFloat64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  Node* vtrue = ChangeSmiToInt32(value);
  vtrue = __ ChangeInt32ToFloat64(vtrue);
  __ Goto(&done, vtrue);

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckClosure(Node* node,
                                                 Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Handle<FeedbackCell> feedback_cell = FeedbackCellOf(node->op());
  Node* value = node->InputAt(0);

  // Check that {value} is actually a JSFunction.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* check_instance_type = __ Uint32LessThanOrEqual(
      __ Int32Sub(value_instance_type,
                  __ Int32Constant(FIRST_JS_FUNCTION_TYPE)),
      __ Int32Constant(LAST_JS_FUNCTION_TYPE - FIRST_JS_FUNCTION_TYPE));
  __ DeoptimizeIfNot(DeoptimizeReason::kWrongCallTarget, FeedbackSource(),
                     check_instance_type, frame_state);

  // Check that the {value}s feedback vector cell matches the one
  // we recorded before.
  Node* value_cell =
      __ LoadField(AccessBuilder::ForJSFunctionFeedbackCell(), value);
  Node* check_cell = __ TaggedEqual(value_cell, __ HeapConstant(feedback_cell));
  __ DeoptimizeIfNot(DeoptimizeReason::kWrongFeedbackCell, FeedbackSource(),
                     check_cell, frame_state);
  return value;
}

void EffectControlLinearizer::MigrateInstanceOrDeopt(
    Node* value, Node* value_map, Node* frame_state,
    FeedbackSource const& feedback_source, DeoptimizeReason reason) {
  // If map is not deprecated the migration attempt does not make sense.
  Node* bitfield3 = __ LoadField(AccessBuilder::ForMapBitField3(), value_map);
  Node* is_not_deprecated = __ Word32Equal(
      __ Word32And(bitfield3,
                   __ Int32Constant(Map::Bits3::IsDeprecatedBit::kMask)),
      __ Int32Constant(0));
  __ DeoptimizeIf(reason, feedback_source, is_not_deprecated, frame_state);
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  Runtime::FunctionId id = Runtime::kTryMigrateInstance;
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      graph()->zone(), id, 1, properties, CallDescriptor::kNoFlags);
  Node* result = __ Call(call_descriptor, __ CEntryStubConstant(1), value,
                         __ ExternalConstant(ExternalReference::Create(id)),
                         __ Int32Constant(1), __ NoContextConstant());
  Node* check = ObjectIsSmi(result);
  __ DeoptimizeIf(DeoptimizeReason::kInstanceMigrationFailed, feedback_source,
                  check, frame_state);
}

void EffectControlLinearizer::LowerCheckMaps(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  CheckMapsParameters const& p = CheckMapsParametersOf(node->op());
  Node* value = node->InputAt(0);

  ZoneRefSet<Map> const& maps = p.maps();
  size_t const map_count = maps.size();

  if (p.flags() & CheckMapsFlag::kTryMigrateInstance) {
    auto done = __ MakeLabel();
    auto migrate = __ MakeDeferredLabel();

    // Load the current map of the {value}.
    Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);

    // Perform the map checks.
    for (size_t i = 0; i < map_count; ++i) {
      Node* map = __ HeapConstant(maps[i].object());
      Node* check = __ TaggedEqual(value_map, map);
      if (i == map_count - 1) {
        __ BranchWithCriticalSafetyCheck(check, &done, &migrate);
      } else {
        auto next_map = __ MakeLabel();
        __ BranchWithCriticalSafetyCheck(check, &done, &next_map);
        __ Bind(&next_map);
      }
    }

    // Perform the (deferred) instance migration.
    __ Bind(&migrate);
    MigrateInstanceOrDeopt(value, value_map, frame_state, p.feedback(),
                           DeoptimizeReason::kWrongMap);

    // Reload the current map of the {value}.
    value_map = __ LoadField(AccessBuilder::ForMap(), value);

    // Perform the map checks again.
    for (size_t i = 0; i < map_count; ++i) {
      Node* map = __ HeapConstant(maps[i].object());
      Node* check = __ TaggedEqual(value_map, map);
      if (i == map_count - 1) {
        __ DeoptimizeIfNot(DeoptimizeReason::kWrongMap, p.feedback(), check,
                           frame_state);
      } else {
        auto next_map = __ MakeLabel();
        __ BranchWithCriticalSafetyCheck(check, &done, &next_map);
        __ Bind(&next_map);
      }
    }

    __ Goto(&done);
    __ Bind(&done);
  } else {
    auto done = __ MakeLabel();

    // Load the current map of the {value}.
    Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);

    for (size_t i = 0; i < map_count; ++i) {
      Node* map = __ HeapConstant(maps[i].object());
      Node* check = __ TaggedEqual(value_map, map);

      if (i == map_count - 1) {
        __ DeoptimizeIfNot(DeoptimizeReason::kWrongMap, p.feedback(), check,
                           frame_state);
      } else {
        auto next_map = __ MakeLabel();
        __ BranchWithCriticalSafetyCheck(check, &done, &next_map);
        __ Bind(&next_map);
      }
    }
    __ Goto(&done);
    __ Bind(&done);
  }
}

void EffectControlLinearizer::TryMigrateInstance(Node* value, Node* value_map) {
  auto done = __ MakeLabel();
  // If map is not deprecated the migration attempt does not make sense.
  Node* bitfield3 = __ LoadField(AccessBuilder::ForMapBitField3(), value_map);
  Node* is_not_deprecated = __ Word32Equal(
      __ Word32And(bitfield3,
                   __ Int32Constant(Map::Bits3::IsDeprecatedBit::kMask)),
      __ Int32Constant(0));
  __ GotoIf(is_not_deprecated, &done);
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  Runtime::FunctionId id = Runtime::kTryMigrateInstance;
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      graph()->zone(), id, 1, properties, CallDescriptor::kNoFlags);
  __ Call(call_descriptor, __ CEntryStubConstant(1), value,
          __ ExternalConstant(ExternalReference::Create(id)),
          __ Int32Constant(1), __ NoContextConstant());
  __ Goto(&done);
  __ Bind(&done);
}

Node* EffectControlLinearizer::CallBuiltinForBigIntBinop(Node* left,
                                                         Node* right,
                                                         Builtin builtin) {
  Callable const callable = Builtins::CallableFor(isolate(), builtin);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), CallDescriptor::kNoFlags,
      Operator::kFoldable | Operator::kNoThrow);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), left, right,
                 __ NoContextConstant());
}

Node* EffectControlLinearizer::LowerCompareMaps(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ZoneRefSet<Map> const& maps = CompareMapsParametersOf(node->op());
  size_t const map_count = maps.size();
  Node* value = node->InputAt(0);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Load the current map of the {value}.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);

  for (size_t i = 0; i < map_count; ++i) {
    Node* map = __ HeapConstant(maps[i].object());
    Node* check = __ TaggedEqual(value_map, map);

    auto next_map = __ MakeLabel();
    auto passed = __ MakeLabel();
    __ BranchWithCriticalSafetyCheck(check, &passed, &next_map);

    __ Bind(&passed);
    __ Goto(&done, __ Int32Constant(1));

    __ Bind(&next_map);
  }
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckNumber(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  Node* check0 = ObjectIsSmi(value);
  __ GotoIfNot(check0, &if_not_smi);
  __ Goto(&done);

  __ Bind(&if_not_smi);
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* check1 = __ TaggedEqual(value_map, __ HeapNumberMapConstant());
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAHeapNumber, params.feedback(),
                     check1, frame_state);
  __ Goto(&done);

  __ Bind(&done);
  return value;
}

Node* EffectControlLinearizer::LowerCheckReceiver(Node* node,
                                                  Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* check = JSAnyIsNotPrimitiveHeapObject(value);
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAJavaScriptObject, FeedbackSource(),
                     check, frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckReceiverOrNullOrUndefined(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

#if V8_STATIC_ROOTS_BOOL
  Node* check0 = JSAnyIsNotPrimitiveHeapObject(value);
  Node* check1 = __ TaggedEqual(value, __ UndefinedConstant());
  Node* check2 = __ TaggedEqual(value, __ NullConstant());

  __ DeoptimizeIfNot(DeoptimizeReason::kNotAJavaScriptObjectOrNullOrUndefined,
                     FeedbackSource(),
                     __ Word32Or(check0, __ Word32Or(check1, check2)),
                     frame_state);

#else
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);

  // Rule out all primitives except oddballs (true, false, undefined, null).
  static_assert(LAST_PRIMITIVE_HEAP_OBJECT_TYPE == ODDBALL_TYPE);
  static_assert(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
  Node* check0 = __ Uint32LessThanOrEqual(__ Uint32Constant(ODDBALL_TYPE),
                                          value_instance_type);
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAJavaScriptObjectOrNullOrUndefined,
                     FeedbackSource(), check0, frame_state);

  // Rule out booleans.
  Node* check1 = __ TaggedEqual(value_map, __ BooleanMapConstant());
  __ DeoptimizeIf(DeoptimizeReason::kNotAJavaScriptObjectOrNullOrUndefined,
                  FeedbackSource(), check1, frame_state);
#endif
  return value;
}

Node* EffectControlLinearizer::LowerCheckSymbol(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);

  Node* check =
      __ TaggedEqual(value_map, __ HeapConstant(factory()->symbol_map()));
  __ DeoptimizeIfNot(DeoptimizeReason::kNotASymbol, FeedbackSource(), check,
                     frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckString(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);

  Node* check = __ Uint32LessThan(value_instance_type,
                                  __ Uint32Constant(FIRST_NONSTRING_TYPE));
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAString, params.feedback(), check,
                     frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckInternalizedString(Node* node,
                                                            Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);

  Node* check = __ Word32Equal(
      __ Word32And(value_instance_type,
                   __ Int32Constant(kIsNotStringMask | kIsNotInternalizedMask)),
      __ Int32Constant(kInternalizedTag));
  __ DeoptimizeIfNot(DeoptimizeReason::kWrongInstanceType, FeedbackSource(),
                     check, frame_state);

  return value;
}

void EffectControlLinearizer::LowerCheckIf(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckIfParameters& p = CheckIfParametersOf(node->op());
  __ DeoptimizeIfNot(p.reason(), p.feedback(), value, frame_state);
}

namespace {

int GetLiteralStringLen(Node* node, JSHeapBroker* broker) {
  if (node->opcode() == IrOpcode::kStringFromSingleCharCode) {
    return 1;
  }
  HeapObjectMatcher m(node);
  DCHECK(m.HasResolvedValue() && m.Ref(broker).IsString());
  StringRef string = m.Ref(broker).AsString();
  return string.length();
}

int IsTwoByteString(Node* node, JSHeapBroker* broker) {
  HeapObjectMatcher m(node);
  DCHECK(m.HasResolvedValue() && m.Ref(broker).IsString());
  StringRef string = m.Ref(broker).AsString();
  return string.object()->IsTwoByteRepresentation();
}

template <typename Char>
const Char* GetLiteralString(Node* node, JSHeapBroker* broker) {
  DCHECK_EQ(node->opcode(), IrOpcode::kHeapConstant);
  HeapObjectMatcher m(node);
  DCHECK(m.HasResolvedValue() && m.Ref(broker).IsString());
  StringRef string = m.Ref(broker).AsString();
  DisallowGarbageCollection no_gc;
  SharedStringAccessGuardIfNeeded access_guard(broker->isolate());
  const Char* str =
      string.object()->template GetDirectStringChars<Char>(no_gc, access_guard);
  return str;
}

}  // namespace

// Generated-code equivalent of:
//   if (condition) { then_body } else { else_body }
void EffectControlLinearizer::IfThenElse(Node* condition,
                                         std::function<void(void)> then_body,
                                         std::function<void(void)> else_body) {
  auto if_true = __ MakeLabel(), if_false = __ MakeLabel(),
       done = __ MakeLabel();
  __ Branch(condition, &if_true, &if_false);
  __ Bind(&if_true);
  then_body();
  __ Goto(&done);
  __ Bind(&if_false);
  else_body();
  __ Goto(&done);
  __ Bind(&done);
}

// This function is the generated-code equivalent of SeqOneByteString::SizeFor
// and SeqTwoByteString::SizeFor: it calculates how many bytes should be
// allocated if we want to allocate a string of length {length} (in particular,
// it takes headers and alignment into account).
Node* EffectControlLinearizer::SizeForString(Node* length, Node* is_two_byte) {
  Node* size = __ Int32Constant(sizeof(SeqString));
  size = __ Int32Add(size, __ Word32Shl(length, is_two_byte));

  auto object_pointer_align = [&](Node* value) -> Node* {
    // Generated-code equivalent of the OBJECT_POINTER_ALIGN macro.
    return __ Word32And(
        __ Int32Add(value, __ Int32Constant(kObjectAlignmentMask)),
        __ Int32Constant(~kObjectAlignmentMask));
  };
  size = object_pointer_align(size);
  size = ChangeInt32ToIntPtr(size);
  return size;
}

// Allocates a new 1/2-byte (depending on {is_one_byte}) SeqString of length
// {length}.
Node* EffectControlLinearizer::AllocateSeqString(Node* length, bool one_byte) {
  Node* size = SizeForString(length, __ Int32Constant(!one_byte));

  Node* seq_string = __ Allocate(AllocationType::kYoung, size);

  __ StoreField(
      AccessBuilder::ForMap(), seq_string,
      __ HeapConstant(one_byte ? factory()->seq_one_byte_string_map()
                               : factory()->seq_two_byte_string_map()));
  __ StoreField(AccessBuilder::ForNameRawHashField(), seq_string,
                __ Int32Constant(Name::kEmptyHashField));
  __ StoreField(AccessBuilder::ForStringLength(), seq_string, length);
  // zero-ing the padding bytes.
  __ Store(
      StoreRepresentation(MachineRepresentation::kTaggedSigned,
                          kNoWriteBarrier),
      seq_string,
      __ IntPtrAdd(size, __ IntPtrConstant(-kObjectAlignment - kHeapObjectTag)),
      __ SmiConstant(0));

  return seq_string;
}

// Allocates a new 1/2-byte (depending on {is_one_byte}) SeqString of length
// {length}. This function is a dynamic version of the previous one for cases
// where {is_one_byte} is not known at compile time.
Node* EffectControlLinearizer::AllocateSeqString(Node* length,
                                                 Node* is_one_byte) {
  Node* is_two_byte = __ Word32Xor(is_one_byte, __ Int32Constant(1));
  Node* size = SizeForString(length, is_two_byte);

  Node* seq_string = __ Allocate(AllocationType::kYoung, size);

  __ StoreField(AccessBuilder::ForNameRawHashField(), seq_string,
                __ Int32Constant(Name::kEmptyHashField));
  __ StoreField(AccessBuilder::ForStringLength(), seq_string, length);
  // zero-ing the padding bytes.
  __ Store(
      StoreRepresentation(MachineRepresentation::kTaggedSigned,
                          kNoWriteBarrier),
      seq_string,
      __ IntPtrAdd(size, __ IntPtrConstant(-kObjectAlignment - kHeapObjectTag)),
      __ SmiConstant(0));

  IfThenElse(
      is_one_byte,
      [&]() {
        __ StoreField(AccessBuilder::ForMap(), seq_string,
                      __ HeapConstant(factory()->seq_one_byte_string_map()));
      },
      [&]() {
        __ StoreField(AccessBuilder::ForMap(), seq_string,
                      __ HeapConstant(factory()->seq_two_byte_string_map()));
      });
  return seq_string;
}

Node* EffectControlLinearizer::AllocateOneByteSlicedString() {
  Node* sliced_string = __ Allocate(
      AllocationType::kYoung,
      __ IntPtrConstant(
          jsgraph()->factory()->sliced_one_byte_string_map()->instance_size()));
  __ StoreField(AccessBuilder::ForMap(), sliced_string,
                __ HeapConstant(factory()->sliced_one_byte_string_map()));
  return sliced_string;
}

Node* EffectControlLinearizer::AllocateTwoByteSlicedString() {
  Node* sliced_string = __ Allocate(
      AllocationType::kYoung,
      __ IntPtrConstant(
          jsgraph()->factory()->sliced_two_byte_string_map()->instance_size()));
  __ StoreField(AccessBuilder::ForMap(), sliced_string,
                __ HeapConstant(factory()->sliced_two_byte_string_map()));
  return sliced_string;
}

// Copies the first {length} characters of {src} into {dst}, assuming that they
// both have the same 1/2-byte representation described by {is_one_byte}.
void EffectControlLinearizer::CopyString(Node* src, Node* dst, Node* length,
                                         Node* is_one_byte) {
  auto one_byte_lbl = __ MakeLabel(), two_byte_lbl = __ MakeLabel(),
       done = __ MakeLabel();

  auto copy_string_fun = [&](auto label, auto access) {
    __ Bind(label);
    auto loop = __ MakeLoopLabel(MachineRepresentation::kWord32);
    __ Goto(&loop, __ Int32Constant(0));

    __ Bind(&loop);
    Node* index = loop.PhiAt(0);
    __ GotoIf(__ Word32Equal(index, length), &done);
    __ StoreElement(access, dst, index, __ LoadElement(access, src, index));
    index = __ Int32Add(index, __ Int32Constant(1));
    __ Goto(&loop, index);
  };

  __ Branch(is_one_byte, &one_byte_lbl, &two_byte_lbl);
  copy_string_fun(&one_byte_lbl, AccessBuilder::ForSeqOneByteStringCharacter());
  copy_string_fun(&two_byte_lbl, AccessBuilder::ForSeqTwoByteStringCharacter());

  __ Bind(&done);
}

// For any string, returns true if it's representation is 1-byte.
Node* EffectControlLinearizer::StringIsOneByte(Node* node) {
  Node* map = __ LoadField(AccessBuilder::ForMap(), node);
  Node* instance_type = __ LoadField(AccessBuilder::ForMapInstanceType(), map);
  Node* is_one_byte = __ Word32Equal(
      __ Word32And(instance_type, __ Int32Constant(kStringEncodingMask)),
      __ Int32Constant(kOneByteStringTag));
  return is_one_byte;
}

// For any string, returns true if its representation is 2-byte. The result
// should be equivalent to `!StringIsOneByte(node)`, but we avoid the negation
// to make things a bit faster.
Node* EffectControlLinearizer::StringIsTwoByte(Node* node) {
  Node* map = __ LoadField(AccessBuilder::ForMap(), node);
  Node* instance_type = __ LoadField(AccessBuilder::ForMapInstanceType(), map);
  Node* is_one_byte = __ Word32Equal(
      __ Word32And(instance_type, __ Int32Constant(kStringEncodingMask)),
      __ Int32Constant(kTwoByteStringTag));
  return is_one_byte;
}

// For a literal-ish string (a HeapConstant or a StringFromSingleCharCode),
// computes whether it's 1-byte, or 2-byte.
Node* EffectControlLinearizer::ConstStringIsOneByte(Node* node) {
  if (node->opcode() == IrOpcode::kHeapConstant) {
    HeapObjectMatcher m(node);
    DCHECK(m.HasResolvedValue() && m.Ref(broker()).IsString());
    StringRef string = m.Ref(broker()).AsString();
    return __ Int32Constant(string.object()->IsOneByteRepresentation());
  } else {
    DCHECK_EQ(node->opcode(), IrOpcode::kStringFromSingleCharCode);
    Node* c = __ Word32And(node->InputAt(0), __ Uint32Constant(0xFFFF));
    return __ Int32LessThan(c, __ Int32Constant(0xFF + 1));
  }
}

// Assuming that {orig} is a 1-byte backing store for a string builder of total
// length of {total_length}, including {initialized_length} initialized
// characters (ie, the SlicedString has length {initialized_length}), this
// function allocates a 2-byte backing store of {total_length} length, and
// copies the first {initialized_length} characters of {orig} to this new
// backing store.
Node* EffectControlLinearizer::ConvertOneByteStringToTwoByte(
    Node* orig, Node* total_length, Node* initialized_length) {
  Node* new_string = AllocateSeqString(total_length, false);
  auto loop = __ MakeLoopLabel(MachineRepresentation::kWord32);
  auto done = __ MakeLabel();
  __ Goto(&loop, __ Int32Constant(0));
  __ Bind(&loop);

  Node* index = loop.PhiAt(0);
  __ GotoIf(__ Word32Equal(index, initialized_length), &done);
  __ StoreElement(AccessBuilder::ForSeqTwoByteStringCharacter(), new_string,
                  index,
                  __ LoadElement(AccessBuilder::ForSeqOneByteStringCharacter(),
                                 orig, index));
  index = __ Int32Add(index, __ Int32Constant(1));
  __ Goto(&loop, index);

  __ Bind(&done);
  return new_string;
}

// Stores {node} (which should be a kHeapConstant containing a SeqString) into
// {buffer} at offset {offset}. {Char} should be {uint8_t} if {node} is a 1-byte
// string, and {uint16_t} if {node} is a 2-byte string.
// If {node}'s length is more than {kMaxUnrollSize}, we generate a loop that
// inserts each character of {node} in {buffer} one by one. Otherwise, we unroll
// the loop and generate one StoreElement per character.
template <typename Char>
void EffectControlLinearizer::StoreConstantLiteralStringToBuffer(
    Node* buffer, Node* offset, Node* node, Node* is_one_byte) {
  const int kMaxUnrollSize = 6;
  int len = GetLiteralStringLen(node, broker());
  if (len < kMaxUnrollSize) {
    // For {len} below 6, we unroll the loop and generate one Store per
    // character.
    auto copy_constant = [&](auto access) {
      const Char* chars = GetLiteralString<Char>(node, broker());
      for (int i = 0; i < len; i++) {
        __ StoreElement(access, buffer,
                        __ Int32Add(offset, __ Int32Constant(i)),
                        __ Int32Constant(chars[i]));
      }
    };
    if (std::is_same<Char, uint16_t>()) {
      DCHECK(IsTwoByteString(node, broker()));
      // If {node} is a literal 2-byte string, then we can just generate the
      // 2-byte case (because it's guaranteed that {is_one_byte} is false).
      copy_constant(AccessBuilder::ForSeqTwoByteStringCharacter());
    } else {
      // On the other hand, if {node} is 1-byte, then we can't infer anything,
      // because {node} can be a 1-byte string that we need to add into a 2-byte
      // string. Thus, we generate both 1-byte and 2-byte cases, and we'll
      // decide dynamically which one to execute based on the value of
      // {is_one_byte}.
      IfThenElse(
          is_one_byte,
          [&]() {
            copy_constant(AccessBuilder::ForSeqOneByteStringCharacter());
          },
          [&]() {
            copy_constant(AccessBuilder::ForSeqTwoByteStringCharacter());
          });
    }
  } else {
    // For {len} above 6, we generate a proper loop, in order to keep code size
    // not too high.
    auto copy_constant = [&](auto buffer_access, auto constant_access) {
      auto loop = __ MakeLoopLabel(MachineRepresentation::kWord32);
      auto done = __ MakeLabel();
      __ Goto(&loop, __ Int32Constant(0));

      __ Bind(&loop);
      Node* index = loop.PhiAt(0);
      __ GotoIf(__ Word32Equal(index, __ Int32Constant(len)), &done);
      __ StoreElement(buffer_access, buffer, __ Int32Add(offset, index),
                      __ LoadElement(constant_access, node, index));
      __ Goto(&loop, __ Int32Add(index, __ Int32Constant(1)));
      __ Bind(&done);
    };
    auto constant_access = IsTwoByteString(node, broker())
                               ? AccessBuilder::ForSeqTwoByteStringCharacter()
                               : AccessBuilder::ForSeqOneByteStringCharacter();
    IfThenElse(
        is_one_byte,
        [&]() {
          copy_constant(AccessBuilder::ForSeqOneByteStringCharacter(),
                        constant_access);
        },
        [&]() {
          copy_constant(AccessBuilder::ForSeqTwoByteStringCharacter(),
                        constant_access);
        });
  }
}

// Stores {node} (which should be a kHeapConstant or a StringFromSingleCharCode)
// into {buffer} at offset {offset}.
void EffectControlLinearizer::StoreLiteralStringToBuffer(Node* buffer,
                                                         Node* offset,
                                                         Node* node,
                                                         Node* is_one_byte) {
  DCHECK(node->opcode() == IrOpcode::kHeapConstant ||
         node->opcode() == IrOpcode::kStringFromSingleCharCode);

  if (node->opcode() == IrOpcode::kHeapConstant) {
    if (IsTwoByteString(node, broker())) {
      StoreConstantLiteralStringToBuffer<uint16_t>(buffer, offset, node,
                                                   is_one_byte);
    } else {
      StoreConstantLiteralStringToBuffer<uint8_t>(buffer, offset, node,
                                                  is_one_byte);
    }
  } else {
    IfThenElse(
        is_one_byte,
        [&]() {
          __ StoreElement(
              AccessBuilder::ForSeqOneByteStringCharacter(), buffer, offset,
              __ Word32And(node->InputAt(0), __ Uint32Constant(0xFFFF)));
        },
        [&]() {
          __ StoreElement(
              AccessBuilder::ForSeqTwoByteStringCharacter(), buffer, offset,
              __ Word32And(node->InputAt(0), __ Uint32Constant(0xFFFF)));
        });
  }
}

void EffectControlLinearizer::EndStringBuilderConcatForLoopPhi(
    Node* node, BasicBlock* block) {
  Node* backing_store = EndStringBuilderConcat(node);

  // Updating the uses of {node} to use {backing_store} instead.
  BasicBlock* loop_header = schedule()->block(node);
  DCHECK(loop_header->IsLoopHeader());
  for (Edge edge : node->use_edges()) {
    BasicBlock* user_block = schedule()->block(edge.from());
    if (!user_block) {
      // If {schedule_} doesn't have a block |edge.from()|, then this is a new
      // node, which means that it is within the Phi's loop (because
      // EffectControlLinearize iterates the graph in RPO order).
      continue;
    }
    if (loop_header->LoopContains(user_block)) {
      // |edge.from()| is within the loop, so it's part of the string builder,
      // and should use the regular Phi node.
      continue;
    }
    if ((*user_block->nodes())[0]->opcode() == IrOpcode::kMerge) {
      if (std::find(user_block->predecessors().begin(),
                    user_block->predecessors().end(),
                    block) != user_block->predecessors().end()) {
        // {user_block} merges {block} (+ some other block), so we update the
        // edge, even though {block} will not dominate {user_block}.
        edge.UpdateTo(backing_store);
        continue;
      }
    }

    // If we reach this point:
    //   - {user_block} cannot be before {block}, otherwise it couldn't use
    //     {node} which is defined in {block} (that was true at the beginning of
    //     the loop already). (unless used as the backedge of a loop phi)
    //   - {user_block} cannot be inside the loop (we've checked that).
    // So {user_block} has to be after the loop. Then, since {user_block} is
    // after {block} and uses {node}:
    //   - either it's not dominated by {block}, in which case it has to be a
    //     merge so that is can receive another value for {node} from its other
    //     predecessor(s) (otherwise it couldn't use {node}). We've checked that
    //     right above, so we're not in this case.
    //   - it is dominated by {block}. This is the only case that remains; we
    //     DCHECK it just to be safe.
    DCHECK((BasicBlock::GetCommonDominator(block, user_block) == block) ||
           (user_block->IsLoopHeader() &&
            edge.from()->opcode() == IrOpcode::kPhi));
    // {user_block} is dominated by {block}, or it's a loop header using {node}
    // as the backedge, which mean that we can safely update the edge.
    DCHECK(!NodeProperties::IsControlEdge(edge) &&
           !NodeProperties::IsEffectEdge(edge));
    edge.UpdateTo(backing_store);
  }
}

// Once the string builder is done building the string, we get rid of the
// SlicedString and right-trim the backing store SeqString, to keep only the
// SeqString.
Node* EffectControlLinearizer::EndStringBuilderConcat(Node* node) {
  Node* new_length = __ LoadField(AccessBuilder::ForStringLength(), node);
  Node* backing_store =
      __ LoadField(AccessBuilder::ForSlicedStringParent(), node);
  Node* backing_store_length =
      __ LoadField(AccessBuilder::ForStringLength(), backing_store);

  Node* is_two_byte = StringIsTwoByte(backing_store);
  Node* backing_store_real_size =
      SizeForString(backing_store_length, is_two_byte);
  Node* new_backing_store_real_size = SizeForString(new_length, is_two_byte);

  Node* freed_size =
      __ Int32Sub(backing_store_real_size, new_backing_store_real_size);

  // Right-trimming code inspired by heap.cc:CreateFillerObjectAtImpl
  IfThenElse(
      __ Word32Equal(freed_size, __ Int32Constant(0)),
      []() {
        // If the filler has size 0, do nothing.
      },
      [&]() {
        Node* filler_map_location =
            __ IntPtrAdd(backing_store, new_backing_store_real_size);
        IfThenElse(
            __ Word32Equal(freed_size, __ Int32Constant(kTaggedSize)),
            [&]() {
              // If the filler has size kTaggedSize, insert
              // one_pointer_filler_map()
              __ StoreField(
                  AccessBuilder::ForMap(kNoWriteBarrier), filler_map_location,
                  __ HeapConstant(factory()->one_pointer_filler_map()));
            },
            [&]() {
              IfThenElse(
                  __ Word32Equal(freed_size, __ Int32Constant(2 * kTaggedSize)),
                  [&]() {
                    // If the filler has size kTaggedSize*2, insert
                    // two_pointer_filler_map()
                    __ StoreField(
                        AccessBuilder::ForMap(kNoWriteBarrier),
                        filler_map_location,
                        __ HeapConstant(factory()->two_pointer_filler_map()));
                  },
                  [&]() {
                    // Otherwise, insert free_space_map(), with the proper size.
                    __ StoreField(AccessBuilder::ForMap(kNoWriteBarrier),
                                  filler_map_location,
                                  __ HeapConstant(factory()->free_space_map()));
                    __ StoreField(AccessBuilder::ForFreeSpaceSize(),
                                  filler_map_location,
                                  ChangeIntPtrToSmi(freed_size));
                  });
            });
      });

  // Updating backing store length after trimming
  __ StoreField(AccessBuilder::ForStringLength(), backing_store, new_length);

  // Setting the padding bytes to 0
  {
    Node* end =
        __ IntPtrSub(__ IntPtrAdd(backing_store, new_backing_store_real_size),
                     __ IntPtrConstant(kHeapObjectTag));
    Node* start = __ IntPtrSub(
        end, __ IntPtrSub(new_backing_store_real_size,
                          __ IntPtrAdd(__ IntPtrConstant(sizeof(SeqString)),
                                       ChangeInt32ToIntPtr(__ Word32Shl(
                                           new_length, is_two_byte)))));
    auto loop = __ MakeLoopLabel(MachineType::PointerRepresentation());
    auto done = __ MakeLabel();
    __ Goto(&loop, start);
    __ Bind(&loop);
    Node* addr = loop.PhiAt(0);
    Node* check = __ UintLessThan(addr, end);
    __ GotoIfNot(check, &done);
    __ Store(
        StoreRepresentation(MachineRepresentation::kWord8, kNoWriteBarrier),
        addr, 0, __ Int32Constant(0));
    __ Goto(&loop, __ IntPtrAdd(addr, __ IntPtrConstant(1)));

    __ Bind(&done);
  }

  // Overwriting {node} with a filler object, so that we don't leave around a
  // potentially-too-small SlicedString. Note that:
  //   - the GC can still see too-small SlicedString, but it shouldn't care
  //     except in debug builds, and even then, the isolate has a
  //     has_turbofan_string_builders method that tells the GC to ignore
  //     too-small SlicedString when checking verifying heap integrity.
  //   - overwriting {node} with a filler is not strictly needed, but is
  //     relatively cheap, and useful to catch bugs (eg, if after a string
  //     builder, we get an exception saying "Expected String, got FreeSpace",
  //     we'll have an idea of what went wrong!).
  __ StoreField(AccessBuilder::ForMap(kNoWriteBarrier), node,
                __ HeapConstant(factory()->free_space_map()));
  __ StoreField(AccessBuilder::ForFreeSpaceSize(), node,
                ChangeInt32ToSmi(__ Int32Constant(sizeof(SlicedString))));

  return backing_store;
}

Node* EffectControlLinearizer::LowerStringConcat(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  if (string_builder_optimizer_->IsFirstConcatInStringBuilder(node)) {
    // This is the 1st node of a string builder. We thus need to create and
    // initialize the string builder's backing store and SlicedString.
    OneOrTwoByteAnalysis::State one_or_two_byte =
        string_builder_optimizer_->GetOneOrTwoByte(node);

    int left_length = GetLiteralStringLen(node->InputAt(1), broker());
    int right_length = GetLiteralStringLen(node->InputAt(2), broker());

    int initial_length = left_length + right_length;
    int backing_store_initial_length = initial_length * 4;

    // Creating the backing store.
    Node* is_one_byte =
        one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte
            ? __ Int32Constant(1)
        : one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte
            ? __ Int32Constant(0)
            : __ Word32And(ConstStringIsOneByte(node->InputAt(1)),
                           ConstStringIsOneByte(node->InputAt(2)));

    Node* length = __ Int32Constant(initial_length);
    Node* backing_store_length = __ Int32Constant(backing_store_initial_length);
    Node* backing_store = AllocateSeqString(backing_store_length, is_one_byte);

    // Storing first two strings into the backing store
    if (left_length != 0) {
      StoreLiteralStringToBuffer(backing_store, __ Int32Constant(0),
                                 node->InputAt(1), is_one_byte);
    }
    if (right_length != 0) {
      StoreLiteralStringToBuffer(backing_store, __ Int32Constant(left_length),
                                 node->InputAt(2), is_one_byte);
    }

    // Creating the SlicedString view into the backing store
    Node* sliced_string = nullptr;
    if (one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte) {
      // Allocating 1-byte sliced string
      sliced_string = AllocateOneByteSlicedString();
    } else if (one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte) {
      // Allocating 2-byte sliced string
      sliced_string = AllocateTwoByteSlicedString();
    } else {
      // Dynamically choosing to allocate a 1-byte or 2-byte sliced string
      auto if_true = __ MakeLabel(), if_false = __ MakeLabel();
      auto done = __ MakeLabel(MachineRepresentation::kTaggedPointer);
      __ Branch(is_one_byte, &if_true, &if_false);
      __ Bind(&if_true);
      __ Goto(&done, AllocateOneByteSlicedString());
      __ Bind(&if_false);
      __ Goto(&done, AllocateTwoByteSlicedString());
      __ Bind(&done);
      sliced_string = done.PhiAt(0);
    }
    // The following StoreFields initialize the SlicedString (except for its map
    // field, which was already initialized by AllocateOneByteSlicedString or
    // AllocateTwoByteSlicedString).
    __ StoreField(AccessBuilder::ForNameRawHashField(), sliced_string,
                  __ Int32Constant(Name::kEmptyHashField));
    __ StoreField(AccessBuilder::ForSlicedStringParent(), sliced_string,
                  backing_store);
    __ StoreField(AccessBuilder::ForSlicedStringOffset(), sliced_string,
                  __ Int32Constant(0));
    __ StoreField(AccessBuilder::ForStringLength(), sliced_string, length);

    return sliced_string;
  } else if (string_builder_optimizer_->ConcatIsInStringBuilder(node)) {
    OneOrTwoByteAnalysis::State one_or_two_byte =
        string_builder_optimizer_->GetOneOrTwoByte(node);

    int literal_length = GetLiteralStringLen(node->InputAt(2), broker());

    Node* sliced_string = node->InputAt(1);
    Node* current_length =
        __ LoadField(AccessBuilder::ForStringLength(), sliced_string);
    Node* init_backing_store =
        __ LoadField(AccessBuilder::ForSlicedStringParent(), sliced_string);
    Node* max_length =
        __ LoadField(AccessBuilder::ForStringLength(), init_backing_store);

    // Checking if we need to convert from 1-byte to 2-byte
    Node* backing_store_is_onebyte =
        one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte
            ? __ Int32Constant(1)
        : one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte
            ? __ Int32Constant(0)
            : StringIsOneByte(init_backing_store);
    Node* rhs_is_onebyte =
        one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte
            ? __ Int32Constant(1)
        : one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte
            ? __ Int32Constant(0)
            : ConstStringIsOneByte(node->InputAt(2));
    auto has_correct_representation =
        __ MakeLabel(MachineRepresentation::kTaggedPointer);
    if (one_or_two_byte != OneOrTwoByteAnalysis::State::kOneByte &&
        one_or_two_byte != OneOrTwoByteAnalysis::State::kTwoByte) {
      Node* need_to_move_backing_store_to_2_bytes =
          __ Word32And(backing_store_is_onebyte,
                       __ Word32Equal(rhs_is_onebyte, __ Int32Constant(0)));
      auto move_backing_store_to_2_bytes = __ MakeDeferredLabel();
      __ GotoIf(need_to_move_backing_store_to_2_bytes,
                &move_backing_store_to_2_bytes);
      __ Goto(&has_correct_representation, init_backing_store);

      // Converting from 1-byte to 2-byte string.
      __ Bind(&move_backing_store_to_2_bytes);
      {
        Node* new_backing_store = ConvertOneByteStringToTwoByte(
            init_backing_store, max_length, current_length);
        __ StoreField(AccessBuilder::ForMap(), sliced_string,
                      __ HeapConstant(factory()->sliced_two_byte_string_map()));
        __ StoreField(AccessBuilder::ForSlicedStringParent(), sliced_string,
                      new_backing_store);
        __ Goto(&has_correct_representation, new_backing_store);
      }
    } else {
      // We statically know that the string is 1 (or 2) byte, so we know that we
      // don't need to change the string from 1 to 2-byte.
      __ Goto(&has_correct_representation, init_backing_store);
    }

    // At this point, the backing store has the correct 1-byte or 2-byte
    // representation.
    __ Bind(&has_correct_representation);
    Node* backing_store = has_correct_representation.PhiAt(0);
    Node* is_one_byte =
        one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte
            ? __ Int32Constant(1)
        : one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte
            ? __ Int32Constant(0)
            : __ Word32And(rhs_is_onebyte, backing_store_is_onebyte);

    // Checking if reallocation is needed (if there is not enough free space
    // left in the backing store).
    Node* new_length =
        __ Int32Add(current_length, __ Int32Constant(literal_length));
    auto needs_realloc = __ MakeLabel();
    auto add_to_backing_store =
        __ MakeLabel(MachineRepresentation::kTaggedPointer);
    Node* realloc_cond = __ Int32LessThan(max_length, new_length);

    __ GotoIf(realloc_cond, &needs_realloc);
    __ Goto(&add_to_backing_store, backing_store);

    // Reallocating backing store.
    __ Bind(&needs_realloc);
    {
      // The new backing store will have a length of min(2*new_length,
      // String::kMaxLength).
      // Note that since early in the pipeline, StringConcat is preceeded by a
      // check that the size of the resulting string does not exceed
      // String::kMaxLength (which the String Builder optimizer does not
      // remove). So, we can safely assume that `new_length*2` is less or equal
      // to `String::kMaxLength*2`. Thus, since `String::kMaxLength` is less
      // than `max_uint/2` (checked by the static_assert right here),
      // `new_length*2` never overflows, and we can safely check if it is more
      // or less than `String::kMaxLength`: if it's more, then we use
      // String::kMaxLength instead.
      static_assert(String::kMaxLength <=
                    std::numeric_limits<unsigned int>::max() / 2);
      Node* new_backing_store_size_maybe_too_large =
          __ Word32Shl(new_length, __ Int32Constant(1));
      auto size_computed_lbl = __ MakeLabel(MachineRepresentation::kWord32);
      __ GotoIf(__ Int32LessThan(new_backing_store_size_maybe_too_large,
                                 __ Int32Constant(String::kMaxLength)),
                &size_computed_lbl, new_backing_store_size_maybe_too_large);
      __ Goto(&size_computed_lbl, __ Int32Constant(String::kMaxLength));

      __ Bind(&size_computed_lbl);
      Node* new_backing_store_size = size_computed_lbl.PhiAt(0);
      // We allocate a new SeqString, and copy the content of the old backing
      // store into the new one.
      Node* new_backing_store =
          one_or_two_byte == OneOrTwoByteAnalysis::State::kOneByte
              ? AllocateSeqString(new_backing_store_size, /*one_byte*/ true)
          : one_or_two_byte == OneOrTwoByteAnalysis::State::kTwoByte
              ? AllocateSeqString(new_backing_store_size, /*one_byte*/ false)
              : AllocateSeqString(new_backing_store_size, is_one_byte);
      CopyString(backing_store, new_backing_store, current_length, is_one_byte);
      __ StoreField(AccessBuilder::ForSlicedStringParent(), sliced_string,
                    new_backing_store);
      __ Goto(&add_to_backing_store, new_backing_store);
    }

    // After reallocation, simply adding the rhs to the backing store.
    __ Bind(&add_to_backing_store);
    {
      Node* real_backing_store = add_to_backing_store.PhiAt(0);
      StoreLiteralStringToBuffer(real_backing_store, current_length,
                                 node->InputAt(2), is_one_byte);
      __ StoreField(AccessBuilder::ForStringLength(), sliced_string,
                    new_length);
    }

    if (string_builder_optimizer_->IsStringBuilderEnd(node)) {
      // If the string builder ends on {node}, then we add the trimming code
      // right now.
      return EndStringBuilderConcat(sliced_string);
    } else {
      return sliced_string;
    }
  } else {
    Node* lhs = node->InputAt(1);
    Node* rhs = node->InputAt(2);

    Callable const callable =
        CodeFactory::StringAdd(isolate(), STRING_ADD_CHECK_NONE);
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(),
        CallDescriptor::kNoFlags,
        Operator::kNoDeopt | Operator::kNoWrite | Operator::kNoThrow);

    Node* value = __ Call(call_descriptor, __ HeapConstant(callable.code()),
                          lhs, rhs, __ NoContextConstant());

    return value;
  }
}

Node* EffectControlLinearizer::LowerCheckedInt32Add(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* value = __ Int32AddWithOverflow(lhs, rhs);
  Node* check = __ Projection(1, value);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);
  return __ Projection(0, value);
}

Node* EffectControlLinearizer::LowerCheckedInt32Sub(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* value = __ Int32SubWithOverflow(lhs, rhs);
  Node* check = __ Projection(1, value);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);
  return __ Projection(0, value);
}

Node* EffectControlLinearizer::LowerCheckedInt32Div(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);
  Node* zero = __ Int32Constant(0);

  // Check if the {rhs} is a known power of two.
  Int32Matcher m(rhs);
  if (m.IsPowerOf2()) {
    // Since we know that {rhs} is a power of two, we can perform a fast
    // check to see if the relevant least significant bits of the {lhs}
    // are all zero, and if so we know that we can perform a division
    // safely (and fast by doing an arithmetic - aka sign preserving -
    // right shift on {lhs}).
    int32_t divisor = m.ResolvedValue();
    Node* mask = __ Int32Constant(divisor - 1);
    Node* shift = __ Int32Constant(base::bits::WhichPowerOfTwo(divisor));
    Node* check = __ Word32Equal(__ Word32And(lhs, mask), zero);
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, FeedbackSource(),
                       check, frame_state);
    return __ Word32Sar(lhs, shift);
  } else {
    auto if_rhs_positive = __ MakeLabel();
    auto if_rhs_negative = __ MakeDeferredLabel();
    auto done = __ MakeLabel(MachineRepresentation::kWord32);

    // Check if {rhs} is positive (and not zero).
    Node* check_rhs_positive = __ Int32LessThan(zero, rhs);
    __ Branch(check_rhs_positive, &if_rhs_positive, &if_rhs_negative);

    __ Bind(&if_rhs_positive);
    {
      // Fast case, no additional checking required.
      __ Goto(&done, __ Int32Div(lhs, rhs));
    }

    __ Bind(&if_rhs_negative);
    {
      auto if_lhs_minint = __ MakeDeferredLabel();
      auto if_lhs_notminint = __ MakeLabel();

      // Check if {rhs} is zero.
      Node* check_rhs_zero = __ Word32Equal(rhs, zero);
      __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(),
                      check_rhs_zero, frame_state);

      // Check if {lhs} is zero, as that would produce minus zero.
      Node* check_lhs_zero = __ Word32Equal(lhs, zero);
      __ DeoptimizeIf(DeoptimizeReason::kMinusZero, FeedbackSource(),
                      check_lhs_zero, frame_state);

      // Check if {lhs} is kMinInt and {rhs} is -1, in which case we'd have
      // to return -kMinInt, which is not representable as Word32.
      Node* check_lhs_minint = __ Word32Equal(lhs, __ Int32Constant(kMinInt));
      __ Branch(check_lhs_minint, &if_lhs_minint, &if_lhs_notminint);

      __ Bind(&if_lhs_minint);
      {
        // Check that {rhs} is not -1, otherwise result would be -kMinInt.
        Node* check_rhs_minusone = __ Word32Equal(rhs, __ Int32Constant(-1));
        __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(),
                        check_rhs_minusone, frame_state);

        // Perform the actual integer division.
        __ Goto(&done, __ Int32Div(lhs, rhs));
      }

      __ Bind(&if_lhs_notminint);
      {
        // Perform the actual integer division.
        __ Goto(&done, __ Int32Div(lhs, rhs));
      }
    }

    __ Bind(&done);
    Node* value = done.PhiAt(0);

    // Check if the remainder is non-zero.
    Node* check = __ Word32Equal(lhs, __ Int32Mul(value, rhs));
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, FeedbackSource(),
                       check, frame_state);

    return value;
  }
}

template <size_t VarCount, size_t VarCount2>
void EffectControlLinearizer::SmiTagOrOverflow(
    Node* value, GraphAssemblerLabel<VarCount>* if_overflow,
    GraphAssemblerLabel<VarCount2>* done) {
  DCHECK(SmiValuesAre31Bits());
  // Check for overflow at the same time that we are smi tagging.
  // Since smi tagging shifts left by one, it's the same as adding value twice.
  Node* add = __ Int32AddWithOverflow(value, value);
  Node* ovf = __ Projection(1, add);
  __ GotoIf(ovf, if_overflow);
  Node* value_smi = __ Projection(0, add);
  value_smi = ChangeTaggedInt32ToSmi(value_smi);
  __ Goto(done, value_smi);
}

Node* EffectControlLinearizer::SmiTagOrDeopt(Node* value,
                                             const CheckParameters& params,
                                             Node* frame_state) {
  DCHECK(SmiValuesAre31Bits());
  // Check for the lost precision at the same time that we are smi tagging.
  // Since smi tagging shifts left by one, it's the same as adding value twice.
  Node* add = __ Int32AddWithOverflow(value, value);
  Node* check = __ Projection(1, add);
  __ DeoptimizeIf(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                  frame_state);
  Node* result = __ Projection(0, add);
  return ChangeTaggedInt32ToSmi(result);
}

Node* EffectControlLinearizer::BuildUint32Mod(Node* lhs, Node* rhs) {
  auto if_rhs_power_of_two = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  // Compute the mask for the {rhs}.
  Node* one = __ Int32Constant(1);
  Node* msk = __ Int32Sub(rhs, one);

  // Check if the {rhs} is a power of two.
  __ GotoIf(__ Word32Equal(__ Word32And(rhs, msk), __ Int32Constant(0)),
            &if_rhs_power_of_two);
  {
    // The {rhs} is not a power of two, do a generic Uint32Mod.
    __ Goto(&done, __ Uint32Mod(lhs, rhs));
  }

  __ Bind(&if_rhs_power_of_two);
  {
    // The {rhs} is a power of two, just do a fast bit masking.
    __ Goto(&done, __ Word32And(lhs, msk));
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedInt32Mod(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  // General case for signed integer modulus, with optimization for (unknown)
  // power of 2 right hand side.
  //
  //   if rhs <= 0 then
  //     rhs = -rhs
  //     deopt if rhs == 0
  //   if lhs < 0 then
  //     let lhs_abs = -lhs in
  //     let res = lhs_abs % rhs in
  //     deopt if res == 0
  //     -res
  //   else
  //     let msk = rhs - 1 in
  //     if rhs & msk == 0 then
  //       lhs & msk
  //     else
  //       lhs % rhs
  //
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  auto if_rhs_not_positive = __ MakeDeferredLabel();
  auto if_lhs_negative = __ MakeDeferredLabel();
  auto if_rhs_power_of_two = __ MakeLabel();
  auto rhs_checked = __ MakeLabel(MachineRepresentation::kWord32);
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* zero = __ Int32Constant(0);

  // Check if {rhs} is not strictly positive.
  Node* check0 = __ Int32LessThanOrEqual(rhs, zero);
  __ GotoIf(check0, &if_rhs_not_positive);
  __ Goto(&rhs_checked, rhs);

  __ Bind(&if_rhs_not_positive);
  {
    // Negate {rhs}, might still produce a negative result in case of
    // -2^31, but that is handled safely below.
    Node* vtrue0 = __ Int32Sub(zero, rhs);

    // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
    __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(),
                    __ Word32Equal(vtrue0, zero), frame_state);
    __ Goto(&rhs_checked, vtrue0);
  }

  __ Bind(&rhs_checked);
  rhs = rhs_checked.PhiAt(0);

  __ GotoIf(__ Int32LessThan(lhs, zero), &if_lhs_negative);
  {
    // The {lhs} is a non-negative integer.
    __ Goto(&done, BuildUint32Mod(lhs, rhs));
  }

  __ Bind(&if_lhs_negative);
  {
    // The {lhs} is a negative integer. This is very unlikely and
    // we intentionally don't use the BuildUint32Mod() here, which
    // would try to figure out whether {rhs} is a power of two,
    // since this is intended to be a slow-path.
    Node* res = __ Uint32Mod(__ Int32Sub(zero, lhs), rhs);

    // Check if we would have to return -0.
    __ DeoptimizeIf(DeoptimizeReason::kMinusZero, FeedbackSource(),
                    __ Word32Equal(res, zero), frame_state);
    __ Goto(&done, __ Int32Sub(zero, res));
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedUint32Div(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);
  Node* zero = __ Int32Constant(0);

  // Check if the {rhs} is a known power of two.
  Uint32Matcher m(rhs);
  if (m.IsPowerOf2()) {
    // Since we know that {rhs} is a power of two, we can perform a fast
    // check to see if the relevant least significant bits of the {lhs}
    // are all zero, and if so we know that we can perform a division
    // safely (and fast by doing a logical - aka zero extending - right
    // shift on {lhs}).
    uint32_t divisor = m.ResolvedValue();
    Node* mask = __ Uint32Constant(divisor - 1);
    Node* shift = __ Uint32Constant(base::bits::WhichPowerOfTwo(divisor));
    Node* check = __ Word32Equal(__ Word32And(lhs, mask), zero);
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, FeedbackSource(),
                       check, frame_state);
    return __ Word32Shr(lhs, shift);
  } else {
    // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
    Node* check = __ Word32Equal(rhs, zero);
    __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(), check,
                    frame_state);

    // Perform the actual unsigned integer division.
    Node* value = __ Uint32Div(lhs, rhs);

    // Check if the remainder is non-zero.
    check = __ Word32Equal(lhs, __ Int32Mul(rhs, value));
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, FeedbackSource(),
                       check, frame_state);
    return value;
  }
}

Node* EffectControlLinearizer::LowerCheckedUint32Mod(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* zero = __ Int32Constant(0);

  // Ensure that {rhs} is not zero, otherwise we'd have to return NaN.
  Node* check = __ Word32Equal(rhs, zero);
  __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(), check,
                  frame_state);

  // Perform the actual unsigned integer modulus.
  return BuildUint32Mod(lhs, rhs);
}

Node* EffectControlLinearizer::LowerCheckedInt32Mul(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  CheckForMinusZeroMode mode = CheckMinusZeroModeOf(node->op());
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* projection = __ Int32MulWithOverflow(lhs, rhs);
  Node* check = __ Projection(1, projection);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);

  Node* value = __ Projection(0, projection);

  if (mode == CheckForMinusZeroMode::kCheckForMinusZero) {
    auto if_zero = __ MakeDeferredLabel();
    auto check_done = __ MakeLabel();
    Node* zero = __ Int32Constant(0);
    Node* check_zero = __ Word32Equal(value, zero);
    __ GotoIf(check_zero, &if_zero);
    __ Goto(&check_done);

    __ Bind(&if_zero);
    // We may need to return negative zero.
    Node* check_or = __ Int32LessThan(__ Word32Or(lhs, rhs), zero);
    __ DeoptimizeIf(DeoptimizeReason::kMinusZero, FeedbackSource(), check_or,
                    frame_state);
    __ Goto(&check_done);

    __ Bind(&check_done);
  }

  return value;
}

Node* EffectControlLinearizer::LowerCheckedInt32ToTaggedSigned(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(SmiValuesAre31Bits());
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());
  return SmiTagOrDeopt(value, params, frame_state);
}

Node* EffectControlLinearizer::LowerCheckedInt64ToInt32(Node* node,
                                                        Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* value32 = __ TruncateInt64ToInt32(value);
  Node* check = __ Word64Equal(__ ChangeInt32ToInt64(value32), value);
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);
  return value32;
}

Node* EffectControlLinearizer::LowerCheckedInt64ToTaggedSigned(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* value32 = __ TruncateInt64ToInt32(value);
  Node* check = __ Word64Equal(__ ChangeInt32ToInt64(value32), value);
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);

  if (SmiValuesAre32Bits()) {
    return ChangeInt64ToSmi(value);
  } else {
    return SmiTagOrDeopt(value32, params, frame_state);
  }
}

Node* EffectControlLinearizer::LowerCheckedUint32Bounds(Node* node,
                                                        Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* index = node->InputAt(0);
  Node* limit = node->InputAt(1);
  const CheckBoundsParameters& params = CheckBoundsParametersOf(node->op());

  Node* check = __ Uint32LessThan(index, limit);
  if (!(params.flags() & CheckBoundsFlag::kAbortOnOutOfBounds)) {
    __ DeoptimizeIfNot(DeoptimizeReason::kOutOfBounds,
                       params.check_parameters().feedback(), check,
                       frame_state);
  } else {
    auto if_abort = __ MakeDeferredLabel();
    auto done = __ MakeLabel();

    __ Branch(check, &done, &if_abort);

    __ Bind(&if_abort);
    __ Unreachable();

    __ Bind(&done);
  }

  return index;
}

Node* EffectControlLinearizer::LowerCheckedUint32ToInt32(Node* node,
                                                         Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());
  Node* unsafe = __ Int32LessThan(value, __ Int32Constant(0));
  __ DeoptimizeIf(DeoptimizeReason::kLostPrecision, params.feedback(), unsafe,
                  frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckedUint32ToTaggedSigned(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());
  Node* check = __ Uint32LessThanOrEqual(value, SmiMaxValueConstant());
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);
  return ChangeUint32ToSmi(value);
}

Node* EffectControlLinearizer::LowerCheckedUint64Bounds(Node* node,
                                                        Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* const index = node->InputAt(0);
  Node* const limit = node->InputAt(1);
  const CheckBoundsParameters& params = CheckBoundsParametersOf(node->op());

  Node* check = __ Uint64LessThan(index, limit);
  if (!(params.flags() & CheckBoundsFlag::kAbortOnOutOfBounds)) {
    __ DeoptimizeIfNot(DeoptimizeReason::kOutOfBounds,
                       params.check_parameters().feedback(), check,
                       frame_state);
  } else {
    auto if_abort = __ MakeDeferredLabel();
    auto done = __ MakeLabel();

    __ Branch(check, &done, &if_abort);

    __ Bind(&if_abort);
    __ Unreachable();

    __ Bind(&done);
  }
  return index;
}

Node* EffectControlLinearizer::LowerCheckedUint64ToInt32(Node* node,
                                                         Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* check = __ Uint64LessThanOrEqual(value, __ Int64Constant(kMaxInt));
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);
  return __ TruncateInt64ToInt32(value);
}

Node* EffectControlLinearizer::LowerCheckedUint64ToInt64(Node* node,
                                                         Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* check = __ Uint64LessThanOrEqual(
      value, __ Uint64Constant(std::numeric_limits<int64_t>::max()));
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckedUint64ToTaggedSigned(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* check =
      __ Uint64LessThanOrEqual(value, __ Int64Constant(Smi::kMaxValue));
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecision, params.feedback(), check,
                     frame_state);
  return ChangeInt64ToSmi(value);
}

Node* EffectControlLinearizer::BuildCheckedFloat64ToInt32(
    CheckForMinusZeroMode mode, const FeedbackSource& feedback, Node* value,
    Node* frame_state) {
  Node* value32 = __ RoundFloat64ToInt32(value);
  Node* check_same = __ Float64Equal(value, __ ChangeInt32ToFloat64(value32));
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecisionOrNaN, feedback,
                     check_same, frame_state);

  if (mode == CheckForMinusZeroMode::kCheckForMinusZero) {
    // Check if {value} is -0.
    auto if_zero = __ MakeDeferredLabel();
    auto check_done = __ MakeLabel();

    Node* check_zero = __ Word32Equal(value32, __ Int32Constant(0));
    __ GotoIf(check_zero, &if_zero);
    __ Goto(&check_done);

    __ Bind(&if_zero);
    // In case of 0, we need to check the high bits for the IEEE -0 pattern.
    Node* check_negative = __ Int32LessThan(__ Float64ExtractHighWord32(value),
                                            __ Int32Constant(0));
    __ DeoptimizeIf(DeoptimizeReason::kMinusZero, feedback, check_negative,
                    frame_state);
    __ Goto(&check_done);

    __ Bind(&check_done);
  }
  return value32;
}

Node* EffectControlLinearizer::BuildCheckedFloat64ToIndex(
    const FeedbackSource& feedback, Node* value, Node* frame_state) {
  if (machine()->Is64()) {
    Node* value64 =
        __ TruncateFloat64ToInt64(value, TruncateKind::kArchitectureDefault);
    // The TruncateKind above means there will be a precision loss in case
    // INT64_MAX input is passed, but that precision loss would not be
    // detected and would not lead to a deoptimization from the first check.
    // But in this case, we'll deopt anyway because of the following checks.
    Node* check_same = __ Float64Equal(value, __ ChangeInt64ToFloat64(value64));
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecisionOrNaN, feedback,
                       check_same, frame_state);
    Node* check_max =
        __ IntLessThan(value64, __ Int64Constant(kMaxSafeInteger));
    __ DeoptimizeIfNot(DeoptimizeReason::kNotAnArrayIndex, feedback, check_max,
                       frame_state);
    Node* check_min =
        __ IntLessThan(__ Int64Constant(-kMaxSafeInteger), value64);
    __ DeoptimizeIfNot(DeoptimizeReason::kNotAnArrayIndex, feedback, check_min,
                       frame_state);
    return value64;
  } else {
    Node* value32 = __ RoundFloat64ToInt32(value);
    Node* check_same = __ Float64Equal(value, __ ChangeInt32ToFloat64(value32));
    __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecisionOrNaN, feedback,
                       check_same, frame_state);
    return value32;
  }
}

Node* EffectControlLinearizer::LowerCheckedFloat64ToInt32(Node* node,
                                                          Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  const CheckMinusZeroParameters& params =
      CheckMinusZeroParametersOf(node->op());
  Node* value = node->InputAt(0);
  return BuildCheckedFloat64ToInt32(params.mode(), params.feedback(), value,
                                    frame_state);
}

Node* EffectControlLinearizer::BuildCheckedFloat64ToInt64(
    CheckForMinusZeroMode mode, const FeedbackSource& feedback, Node* value,
    Node* frame_state) {
  Node* value64 =
      __ TruncateFloat64ToInt64(value, TruncateKind::kSetOverflowToMin);
  Node* check_same = __ Float64Equal(value, __ ChangeInt64ToFloat64(value64));
  __ DeoptimizeIfNot(DeoptimizeReason::kLostPrecisionOrNaN, feedback,
                     check_same, frame_state);

  if (mode == CheckForMinusZeroMode::kCheckForMinusZero) {
    // Check if {value} is -0.
    auto if_zero = __ MakeDeferredLabel();
    auto check_done = __ MakeLabel();

    Node* check_zero = __ Word64Equal(value64, __ Int64Constant(0));
    __ GotoIf(check_zero, &if_zero);
    __ Goto(&check_done);

    __ Bind(&if_zero);
    // In case of 0, we need to check the high bits for the IEEE -0 pattern.
    Node* check_negative = __ Int32LessThan(__ Float64ExtractHighWord32(value),
                                            __ Int32Constant(0));
    __ DeoptimizeIf(DeoptimizeReason::kMinusZero, feedback, check_negative,
                    frame_state);
    __ Goto(&check_done);

    __ Bind(&check_done);
  }
  return value64;
}

Node* EffectControlLinearizer::LowerCheckedFloat64ToInt64(Node* node,
                                                          Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  const CheckMinusZeroParameters& params =
      CheckMinusZeroParametersOf(node->op());
  Node* value = node->InputAt(0);
  return BuildCheckedFloat64ToInt64(params.mode(), params.feedback(), value,
                                    frame_state);
}

Node* EffectControlLinearizer::LowerCheckedTaggedSignedToInt32(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());
  Node* check = ObjectIsSmi(value);
  __ DeoptimizeIfNot(DeoptimizeReason::kNotASmi, params.feedback(), check,
                     frame_state);
  return ChangeSmiToInt32(value);
}

Node* EffectControlLinearizer::LowerCheckedTaggedToArrayIndex(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  CheckParameters const& params = CheckParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineType::PointerRepresentation());

  __ GotoIfNot(ObjectIsSmi(value), &if_not_smi);
  // In the Smi case, just convert to intptr_t.
  __ Goto(&done, ChangeSmiToIntPtr(value));

  // In the non-Smi case, check the heap numberness, load the number and convert
  // to integer.
  __ Bind(&if_not_smi);
  auto if_not_heap_number = __ MakeDeferredLabel();
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* is_heap_number = __ TaggedEqual(value_map, __ HeapNumberMapConstant());
  __ GotoIfNot(is_heap_number, &if_not_heap_number);

  Node* number = __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
  number = BuildCheckedFloat64ToIndex(params.feedback(), number, frame_state);
  __ Goto(&done, number);

  __ Bind(&if_not_heap_number);
  auto calculate_index = __ MakeDeferredLabel();
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* is_string = __ Uint32LessThan(value_instance_type,
                                      __ Uint32Constant(FIRST_NONSTRING_TYPE));
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAString, params.feedback(),
                     is_string, frame_state);

  MachineSignature::Builder builder(graph()->zone(), 1, 1);
  builder.AddReturn(MachineType::IntPtr());
  builder.AddParam(MachineType::TaggedPointer());
  Node* string_to_array_index_function =
      __ ExternalConstant(ExternalReference::string_to_array_index_function());
  auto call_descriptor =
      Linkage::GetSimplifiedCDescriptor(graph()->zone(), builder.Build());
  Node* index = __ Call(common()->Call(call_descriptor),
                        string_to_array_index_function, value);

  __ DeoptimizeIf(DeoptimizeReason::kNotAnArrayIndex, params.feedback(),
                  __ Word32Equal(index, __ Int32Constant(-1)), frame_state);

  __ Goto(&done, index);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedTaggedToInt32(Node* node,
                                                         Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  const CheckMinusZeroParameters& params =
      CheckMinusZeroParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);
  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  // In the Smi case, just convert to int32.
  __ Goto(&done, ChangeSmiToInt32(value));

  // In the non-Smi case, check the heap numberness, load the number and convert
  // to int32.
  __ Bind(&if_not_smi);
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* check_map = __ TaggedEqual(value_map, __ HeapNumberMapConstant());
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAHeapNumber, params.feedback(),
                     check_map, frame_state);
  Node* vfalse = __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
  vfalse = BuildCheckedFloat64ToInt32(params.mode(), params.feedback(), vfalse,
                                      frame_state);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedTaggedToInt64(Node* node,
                                                         Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  const CheckMinusZeroParameters& params =
      CheckMinusZeroParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord64);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  // In the Smi case, just convert to int64.
  __ Goto(&done, ChangeSmiToInt64(value));

  // In the non-Smi case, check the heap numberness, load the number and convert
  // to int64.
  __ Bind(&if_not_smi);
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* check_map = __ TaggedEqual(value_map, __ HeapNumberMapConstant());
  __ DeoptimizeIfNot(DeoptimizeReason::kNotAHeapNumber, params.feedback(),
                     check_map, frame_state);
  Node* vfalse = __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
  vfalse = BuildCheckedFloat64ToInt64(params.mode(), params.feedback(), vfalse,
                                      frame_state);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::BuildCheckedHeapNumberOrOddballToFloat64(
    CheckTaggedInputMode mode, const FeedbackSource& feedback, Node* value,
    Node* frame_state) {
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* check_number = __ TaggedEqual(value_map, __ HeapNumberMapConstant());
  switch (mode) {
    case CheckTaggedInputMode::kNumber: {
      __ DeoptimizeIfNot(DeoptimizeReason::kNotAHeapNumber, feedback,
                         check_number, frame_state);
      break;
    }
    case CheckTaggedInputMode::kNumberOrBoolean: {
      auto check_done = __ MakeLabel();

      __ GotoIf(check_number, &check_done);
      __ DeoptimizeIfNot(DeoptimizeReason::kNotANumberOrBoolean, feedback,
                         __ TaggedEqual(value_map, __ BooleanMapConstant()),
                         frame_state);
      __ Goto(&check_done);

      __ Bind(&check_done);
      break;
    }
    case CheckTaggedInputMode::kNumberOrOddball: {
      auto check_done = __ MakeLabel();

      __ GotoIf(check_number, &check_done);
      // For oddballs also contain the numeric value, let us just check that
      // we have an oddball here.
      Node* instance_type =
          __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
      Node* check_oddball =
          __ Word32Equal(instance_type, __ Int32Constant(ODDBALL_TYPE));
      __ DeoptimizeIfNot(DeoptimizeReason::kNotANumberOrOddball, feedback,
                         check_oddball, frame_state);
      __ Goto(&check_done);

      __ Bind(&check_done);
      break;
    }
  }
  return __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(),
                      value);
}

Node* EffectControlLinearizer::LowerCheckedTaggedToFloat64(Node* node,
                                                           Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  CheckTaggedInputParameters const& p =
      CheckTaggedInputParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  // In the Smi case, just convert to int32 and then float64.
  // Otherwise, check heap numberness and load the number.
  Node* number = BuildCheckedHeapNumberOrOddballToFloat64(
      p.mode(), p.feedback(), value, frame_state);
  __ Goto(&done, number);

  __ Bind(&if_smi);
  Node* from_smi = ChangeSmiToInt32(value);
  from_smi = __ ChangeInt32ToFloat64(from_smi);
  __ Goto(&done, from_smi);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedTaggedToTaggedSigned(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* check = ObjectIsSmi(value);
  __ DeoptimizeIfNot(DeoptimizeReason::kNotASmi, params.feedback(), check,
                     frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerCheckedTaggedToTaggedPointer(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* check = ObjectIsSmi(value);
  __ DeoptimizeIf(DeoptimizeReason::kSmi, params.feedback(), check,
                  frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerCheckBigInt(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  // Check for Smi.
  Node* smi_check = ObjectIsSmi(value);
  __ DeoptimizeIf(DeoptimizeReason::kSmi, params.feedback(), smi_check,
                  frame_state);

  // Check for BigInt.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* bi_check = __ TaggedEqual(value_map, __ BigIntMapConstant());
  __ DeoptimizeIfNot(DeoptimizeReason::kWrongInstanceType, params.feedback(),
                     bi_check, frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerCheckedBigIntToBigInt64(Node* node,
                                                            Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  auto done = __ MakeLabel();
  auto if_not_zero = __ MakeLabel();
  auto if_may_be_out_of_range = __ MakeDeferredLabel();

  Node* value = node->InputAt(0);
  const CheckParameters& params = CheckParametersOf(node->op());

  Node* bitfield = __ LoadField(AccessBuilder::ForBigIntBitfield(), value);
  __ GotoIfNot(__ Word32Equal(bitfield, __ Int32Constant(0)), &if_not_zero);
  __ Goto(&done);

  __ Bind(&if_not_zero);
  {
    // Length must be 1. Compare it with 2 to avoid a right shift.
    Node* length =
        __ Word32And(bitfield, __ Int32Constant(BigInt::LengthBits::kMask));
    __ DeoptimizeIfNot(
        DeoptimizeReason::kNotABigInt64, params.feedback(),
        __ Word32Equal(length, __ Int32Constant(uint32_t{1}
                                                << BigInt::LengthBits::kShift)),
        frame_state);

    Node* lsd =
        __ LoadField(AccessBuilder::ForBigIntLeastSignificantDigit64(), value);

    Node* magnitude_check = __ Uint64LessThanOrEqual(
        lsd, __ Int64Constant(std::numeric_limits<int64_t>::max()));
    __ Branch(magnitude_check, &done, &if_may_be_out_of_range);

    __ Bind(&if_may_be_out_of_range);
    Node* sign =
        __ Word32And(bitfield, __ Int32Constant(BigInt::SignBits::kMask));

    Node* sign_check =
        __ Word32Equal(sign, __ Int32Constant(BigInt::SignBits::kMask));
    __ DeoptimizeIfNot(DeoptimizeReason::kNotABigInt64, params.feedback(),
                       sign_check, frame_state);

    Node* min_check = __ Word64Equal(
        lsd, __ Int64Constant(std::numeric_limits<int64_t>::min()));
    __ DeoptimizeIfNot(DeoptimizeReason::kNotABigInt64, params.feedback(),
                       min_check, frame_state);

    __ Goto(&done);
  }

  __ Bind(&done);
  return value;
}

Node* EffectControlLinearizer::LowerCheckedInt64Add(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* value = __ Int64AddWithOverflow(lhs, rhs);

  Node* check = __ Projection(1, value);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);
  return __ Projection(0, value);
}

Node* EffectControlLinearizer::LowerCheckedInt64Sub(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* value = __ Int64SubWithOverflow(lhs, rhs);

  Node* check = __ Projection(1, value);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);
  return __ Projection(0, value);
}

Node* EffectControlLinearizer::LowerCheckedInt64Mul(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* value = __ Int64MulWithOverflow(lhs, rhs);

  Node* check = __ Projection(1, value);
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check,
                  frame_state);
  return __ Projection(0, value);
}

Node* EffectControlLinearizer::LowerCheckedInt64Div(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  auto division = __ MakeLabel();

  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* check_rhs_zero = __ Word64Equal(rhs, __ Int64Constant(0));
  __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(),
                  check_rhs_zero, frame_state);

  __ GotoIfNot(__ Word64Equal(
                   lhs, __ Int64Constant(std::numeric_limits<int64_t>::min())),
               &division);
  Node* check_overflow = __ Word64Equal(rhs, __ Int64Constant(-1));
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check_overflow,
                  frame_state);
  __ Goto(&division);

  __ Bind(&division);
  Node* value = __ Int64Div(lhs, rhs);
  return value;
}

Node* EffectControlLinearizer::LowerCheckedInt64Mod(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  auto modulo_op = __ MakeLabel();

  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Node* check_rhs_zero = __ Word64Equal(rhs, __ Int64Constant(0));
  __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource(),
                  check_rhs_zero, frame_state);

  // While the mod-result cannot overflow, the underlying instruction is
  // `idiv` and will trap when the accompanying div-result overflows.
  __ GotoIfNot(__ Word64Equal(
                   lhs, __ Int64Constant(std::numeric_limits<int64_t>::min())),
               &modulo_op);
  Node* check_overflow = __ Word64Equal(rhs, __ Int64Constant(-1));
  __ DeoptimizeIf(DeoptimizeReason::kOverflow, FeedbackSource(), check_overflow,
                  frame_state);
  __ Goto(&modulo_op);

  __ Bind(&modulo_op);
  Node* value = __ Int64Mod(lhs, rhs);
  return value;
}

Node* EffectControlLinearizer::LowerChangeInt64ToBigInt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  Node* value = node->InputAt(0);

  return ChangeInt64ToBigInt(value);
}

Node* EffectControlLinearizer::LowerChangeUint64ToBigInt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  Node* value = node->InputAt(0);

  return ChangeUint64ToBigInt(value);
}

Node* EffectControlLinearizer::LowerTruncateBigIntToWord64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(machine()->Is64());

  auto done = __ MakeLabel(MachineRepresentation::kWord64);
  auto if_neg = __ MakeLabel();
  auto if_not_zero = __ MakeLabel();

  Node* value = node->InputAt(0);

  Node* bitfield = __ LoadField(AccessBuilder::ForBigIntBitfield(), value);
  __ GotoIfNot(__ Word32Equal(bitfield, __ Int32Constant(0)), &if_not_zero);
  __ Goto(&done, __ Int64Constant(0));

  __ Bind(&if_not_zero);
  {
    Node* lsd =
        __ LoadField(AccessBuilder::ForBigIntLeastSignificantDigit64(), value);
    Node* sign =
        __ Word32And(bitfield, __ Int32Constant(BigInt::SignBits::kMask));
    __ GotoIf(__ Word32Equal(sign, __ Int32Constant(1)), &if_neg);
    __ Goto(&done, lsd);

    __ Bind(&if_neg);
    __ Goto(&done, __ Int64Sub(__ Int64Constant(0), lsd));
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerTruncateTaggedToWord32(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  __ Goto(&done, ChangeSmiToInt32(value));

  __ Bind(&if_not_smi);
  Node* vfalse =
      __ LoadField(AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), value);
  vfalse = __ TruncateFloat64ToWord32(vfalse);
  __ Goto(&done, vfalse);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckedTruncateTaggedToWord32(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  const CheckTaggedInputParameters& params =
      CheckTaggedInputParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* check = ObjectIsSmi(value);
  __ GotoIfNot(check, &if_not_smi);
  // In the Smi case, just convert to int32.
  __ Goto(&done, ChangeSmiToInt32(value));

  // Otherwise, check that it's a heap number or oddball and truncate the value
  // to int32.
  __ Bind(&if_not_smi);
  Node* number = BuildCheckedHeapNumberOrOddballToFloat64(
      params.mode(), params.feedback(), value, frame_state);
  number = __ TruncateFloat64ToWord32(number);
  __ Goto(&done, number);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerAllocate(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* size = node->InputAt(0);
  AllocationType allocation = AllocationTypeOf(node->op());
  Node* new_node = __ Allocate(allocation, size);
  return new_node;
}

Node* EffectControlLinearizer::LowerNumberToString(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* argument = node->InputAt(0);

  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kNumberToString);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), argument);
}

Node* EffectControlLinearizer::LowerObjectIsArrayBufferView(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* vfalse = __ Uint32LessThan(
      __ Int32Sub(value_instance_type,
                  __ Int32Constant(FIRST_JS_ARRAY_BUFFER_VIEW_TYPE)),
      __ Int32Constant(LAST_JS_ARRAY_BUFFER_VIEW_TYPE -
                       FIRST_JS_ARRAY_BUFFER_VIEW_TYPE + 1));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsBigInt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* vfalse = __ TaggedEqual(value_map, __ BigIntMapConstant());
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsCallable(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_bit_field =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  Node* vfalse = __ Word32Equal(
      __ Int32Constant(Map::Bits1::IsCallableBit::kMask),
      __ Word32And(value_bit_field,
                   __ Int32Constant(Map::Bits1::IsCallableBit::kMask)));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsConstructor(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_bit_field =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  Node* vfalse = __ Word32Equal(
      __ Int32Constant(Map::Bits1::IsConstructorBit::kMask),
      __ Word32And(value_bit_field,
                   __ Int32Constant(Map::Bits1::IsConstructorBit::kMask)));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsDetectableCallable(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_bit_field =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  Node* vfalse = __ Word32Equal(
      __ Int32Constant(Map::Bits1::IsCallableBit::kMask),
      __ Word32And(value_bit_field,
                   __ Int32Constant((Map::Bits1::IsCallableBit::kMask) |
                                    (Map::Bits1::IsUndetectableBit::kMask))));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNumberIsFloat64Hole(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* check = __ Word32Equal(__ Float64ExtractHighWord32(value),
                               __ Int32Constant(kHoleNanUpper32));
  return check;
}

Node* EffectControlLinearizer::LowerNumberIsFinite(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* number = node->InputAt(0);
  Node* diff = __ Float64Sub(number, number);
  Node* check = __ Float64Equal(diff, diff);
  return check;
}

Node* EffectControlLinearizer::LowerObjectIsFiniteNumber(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* object = node->InputAt(0);
  Node* zero = __ Int32Constant(0);
  Node* one = __ Int32Constant(1);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Check if {object} is a Smi.
  __ GotoIf(ObjectIsSmi(object), &done, one);

  // Check if {object} is a HeapNumber.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), object);
  __ GotoIfNot(__ TaggedEqual(value_map, __ HeapNumberMapConstant()), &done,
               zero);

  // {object} is a HeapNumber.
  Node* value = __ LoadField(AccessBuilder::ForHeapNumberValue(), object);
  Node* diff = __ Float64Sub(value, value);
  Node* check = __ Float64Equal(diff, diff);
  __ Goto(&done, check);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNumberIsInteger(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* number = node->InputAt(0);
  Node* trunc = BuildFloat64RoundTruncate(number);
  Node* diff = __ Float64Sub(number, trunc);
  Node* check = __ Float64Equal(diff, __ Float64Constant(0));
  return check;
}

Node* EffectControlLinearizer::LowerObjectIsInteger(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* object = node->InputAt(0);
  Node* zero = __ Int32Constant(0);
  Node* one = __ Int32Constant(1);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Check if {object} is a Smi.
  __ GotoIf(ObjectIsSmi(object), &done, one);

  // Check if {object} is a HeapNumber.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), object);
  __ GotoIfNot(__ TaggedEqual(value_map, __ HeapNumberMapConstant()), &done,
               zero);

  // {object} is a HeapNumber.
  Node* value = __ LoadField(AccessBuilder::ForHeapNumberValue(), object);
  Node* trunc = BuildFloat64RoundTruncate(value);
  Node* diff = __ Float64Sub(value, trunc);
  Node* check = __ Float64Equal(diff, __ Float64Constant(0));
  __ Goto(&done, check);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNumberIsSafeInteger(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* number = node->InputAt(0);
  Node* zero = __ Int32Constant(0);
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* trunc = BuildFloat64RoundTruncate(number);
  Node* diff = __ Float64Sub(number, trunc);
  Node* check = __ Float64Equal(diff, __ Float64Constant(0));
  __ GotoIfNot(check, &done, zero);
  Node* in_range = __ Float64LessThanOrEqual(
      __ Float64Abs(trunc), __ Float64Constant(kMaxSafeInteger));
  __ Goto(&done, in_range);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsSafeInteger(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* object = node->InputAt(0);
  Node* zero = __ Int32Constant(0);
  Node* one = __ Int32Constant(1);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Check if {object} is a Smi.
  __ GotoIf(ObjectIsSmi(object), &done, one);

  // Check if {object} is a HeapNumber.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), object);
  __ GotoIfNot(__ TaggedEqual(value_map, __ HeapNumberMapConstant()), &done,
               zero);

  // {object} is a HeapNumber.
  Node* value = __ LoadField(AccessBuilder::ForHeapNumberValue(), object);
  Node* trunc = BuildFloat64RoundTruncate(value);
  Node* diff = __ Float64Sub(value, trunc);
  Node* check = __ Float64Equal(diff, __ Float64Constant(0));
  __ GotoIfNot(check, &done, zero);
  Node* in_range = __ Float64LessThanOrEqual(
      __ Float64Abs(trunc), __ Float64Constant(kMaxSafeInteger));
  __ Goto(&done, in_range);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsMinusZero(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* zero = __ Int32Constant(0);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Check if {value} is a Smi.
  __ GotoIf(ObjectIsSmi(value), &done, zero);

  // Check if {value} is a HeapNumber.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  __ GotoIfNot(__ TaggedEqual(value_map, __ HeapNumberMapConstant()), &done,
               zero);

  // Check if {value} contains -0.
  Node* value_value = __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
  if (machine()->Is64()) {
    Node* value64 = __ BitcastFloat64ToInt64(value_value);
    __ Goto(&done, __ Word64Equal(value64, __ Int64Constant(kMinusZeroBits)));
  } else {
    Node* value_lo = __ Float64ExtractLowWord32(value_value);
    __ GotoIfNot(__ Word32Equal(value_lo, __ Int32Constant(kMinusZeroLoBits)),
                 &done, zero);
    Node* value_hi = __ Float64ExtractHighWord32(value_value);
    __ Goto(&done,
            __ Word32Equal(value_hi, __ Int32Constant(kMinusZeroHiBits)));
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNumberIsMinusZero(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  if (machine()->Is64()) {
    Node* value64 = __ BitcastFloat64ToInt64(value);
    return __ Word64Equal(value64, __ Int64Constant(kMinusZeroBits));
  } else {
    auto done = __ MakeLabel(MachineRepresentation::kBit);

    Node* value_lo = __ Float64ExtractLowWord32(value);
    __ GotoIfNot(__ Word32Equal(value_lo, __ Int32Constant(kMinusZeroLoBits)),
                 &done, __ Int32Constant(0));
    Node* value_hi = __ Float64ExtractHighWord32(value);
    __ Goto(&done,
            __ Word32Equal(value_hi, __ Int32Constant(kMinusZeroHiBits)));

    __ Bind(&done);
    return done.PhiAt(0);
  }
}

Node* EffectControlLinearizer::LowerObjectIsNaN(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* zero = __ Int32Constant(0);

  auto done = __ MakeLabel(MachineRepresentation::kBit);

  // Check if {value} is a Smi.
  __ GotoIf(ObjectIsSmi(value), &done, zero);

  // Check if {value} is a HeapNumber.
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  __ GotoIfNot(__ TaggedEqual(value_map, __ HeapNumberMapConstant()), &done,
               zero);

  // Check if {value} contains a NaN.
  Node* value_value = __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
  __ Goto(&done,
          __ Word32Equal(__ Float64Equal(value_value, value_value), zero));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNumberIsNaN(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* number = node->InputAt(0);
  Node* diff = __ Float64Equal(number, number);
  Node* check = __ Word32Equal(diff, __ Int32Constant(0));
  return check;
}

Node* EffectControlLinearizer::LowerObjectIsNonCallable(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_primitive = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check0 = ObjectIsSmi(value);
  __ GotoIf(check0, &if_primitive);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* check1 = JSAnyIsNotPrimitiveHeapObject(value, value_map);
  __ GotoIfNot(check1, &if_primitive);

  Node* value_bit_field =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  Node* check2 = __ Word32Equal(
      __ Int32Constant(0),
      __ Word32And(value_bit_field,
                   __ Int32Constant(Map::Bits1::IsCallableBit::kMask)));
  __ Goto(&done, check2);

  __ Bind(&if_primitive);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsNumber(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  __ GotoIf(ObjectIsSmi(value), &if_smi);
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  __ Goto(&done, __ TaggedEqual(value_map, __ HeapNumberMapConstant()));

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(1));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsReceiver(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  __ GotoIf(ObjectIsSmi(value), &if_smi);

  __ Goto(&done, JSAnyIsNotPrimitiveHeapObject(value));

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsSmi(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return ObjectIsSmi(value);
}

Node* EffectControlLinearizer::LowerObjectIsString(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* vfalse = __ Uint32LessThan(value_instance_type,
                                   __ Uint32Constant(FIRST_NONSTRING_TYPE));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsSymbol(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* vfalse =
      __ Word32Equal(value_instance_type, __ Uint32Constant(SYMBOL_TYPE));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerObjectIsUndetectable(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_smi = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  Node* check = ObjectIsSmi(value);
  __ GotoIf(check, &if_smi);

  Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
  Node* value_bit_field =
      __ LoadField(AccessBuilder::ForMapBitField(), value_map);
  Node* vfalse = __ Word32Equal(
      __ Int32Constant(Map::Bits1::IsUndetectableBit::kMask),
      __ Word32And(value_bit_field,
                   __ Int32Constant(Map::Bits1::IsUndetectableBit::kMask)));
  __ Goto(&done, vfalse);

  __ Bind(&if_smi);
  __ Goto(&done, __ Int32Constant(0));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerTypeOf(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* obj = node->InputAt(0);
  Callable const callable = Builtins::CallableFor(isolate(), Builtin::kTypeof);
  Operator::Properties const properties = Operator::kEliminatable;
  CallDescriptor::Flags const flags = CallDescriptor::kNoAllocate;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), obj);
}

Node* EffectControlLinearizer::LowerToBoolean(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* obj = node->InputAt(0);
  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kToBoolean);
  Operator::Properties const properties = Operator::kEliminatable;
  CallDescriptor::Flags const flags = CallDescriptor::kNoAllocate;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), obj);
}

Node* EffectControlLinearizer::LowerArgumentsLength(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* arguments_length = ChangeIntPtrToSmi(
      __ Load(MachineType::Pointer(), __ LoadFramePointer(),
              __ IntPtrConstant(StandardFrameConstants::kArgCOffset)));
  arguments_length =
      __ SmiSub(arguments_length, __ SmiConstant(kJSArgcReceiverSlots));
  return arguments_length;
}

Node* EffectControlLinearizer::LowerRestLength(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  int formal_parameter_count = FormalParameterCountOf(node->op());
  DCHECK_LE(0, formal_parameter_count);

  auto done = __ MakeLabel(MachineRepresentation::kTaggedSigned);
  Node* frame = __ LoadFramePointer();

  Node* arguments_length = ChangeIntPtrToSmi(
      __ Load(MachineType::Pointer(), frame,
              __ IntPtrConstant(StandardFrameConstants::kArgCOffset)));
  arguments_length =
      __ SmiSub(arguments_length, __ SmiConstant(kJSArgcReceiverSlots));
  Node* rest_length =
      __ SmiSub(arguments_length, __ SmiConstant(formal_parameter_count));
  __ GotoIf(__ SmiLessThan(rest_length, __ SmiConstant(0)), &done,
            __ SmiConstant(0));
  __ Goto(&done, rest_length);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNewDoubleElements(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  AllocationType const allocation = AllocationTypeOf(node->op());
  Node* length = node->InputAt(0);

  auto done = __ MakeLabel(MachineRepresentation::kTaggedPointer);
  Node* zero_length = __ IntPtrEqual(length, __ IntPtrConstant(0));
  __ GotoIf(zero_length, &done,
            __ HeapConstant(factory()->empty_fixed_array()));

  // Compute the effective size of the backing store.
  Node* size = __ IntAdd(__ WordShl(length, __ IntPtrConstant(kDoubleSizeLog2)),
                         __ IntPtrConstant(FixedDoubleArray::kHeaderSize));

  // Allocate the result and initialize the header.
  Node* result = __ Allocate(allocation, size);
  __ StoreField(AccessBuilder::ForMap(), result,
                __ FixedDoubleArrayMapConstant());
  __ StoreField(AccessBuilder::ForFixedArrayLength(), result,
                ChangeIntPtrToSmi(length));

  // Initialize the backing store with holes.
  Node* the_hole = __ LoadField(
      AccessBuilder::ForHeapNumberOrOddballOrHoleValue(), __ TheHoleConstant());
  auto loop = __ MakeLoopLabel(MachineType::PointerRepresentation());
  __ Goto(&loop, __ IntPtrConstant(0));
  __ Bind(&loop);
  {
    // Check if we've initialized everything.
    Node* index = loop.PhiAt(0);
    Node* check = __ UintLessThan(index, length);
    __ GotoIfNot(check, &done, result);

    ElementAccess const access = {kTaggedBase, FixedDoubleArray::kHeaderSize,
                                  Type::NumberOrHole(), MachineType::Float64(),
                                  kNoWriteBarrier};
    __ StoreElement(access, result, index, the_hole);

    // Advance the {index}.
    index = __ IntAdd(index, __ IntPtrConstant(1));
    __ Goto(&loop, index);
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNewSmiOrObjectElements(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  AllocationType const allocation = AllocationTypeOf(node->op());
  Node* length = node->InputAt(0);

  auto done = __ MakeLabel(MachineRepresentation::kTaggedPointer);
  Node* zero_length = __ IntPtrEqual(length, __ IntPtrConstant(0));
  __ GotoIf(zero_length, &done,
            __ HeapConstant(factory()->empty_fixed_array()));

  // Compute the effective size of the backing store.
  Node* size = __ IntAdd(__ WordShl(length, __ IntPtrConstant(kTaggedSizeLog2)),
                         __ IntPtrConstant(FixedArray::kHeaderSize));

  // Allocate the result and initialize the header.
  Node* result = __ Allocate(allocation, size);
  __ StoreField(AccessBuilder::ForMap(), result, __ FixedArrayMapConstant());
  __ StoreField(AccessBuilder::ForFixedArrayLength(), result,
                ChangeIntPtrToSmi(length));

  // Initialize the backing store with holes.
  Node* the_hole = __ TheHoleConstant();
  auto loop = __ MakeLoopLabel(MachineType::PointerRepresentation());
  __ Goto(&loop, __ IntPtrConstant(0));
  __ Bind(&loop);
  {
    // Check if we've initialized everything.
    Node* index = loop.PhiAt(0);
    Node* check = __ UintLessThan(index, length);
    __ GotoIfNot(check, &done, result);

    // Storing "the_hole" doesn't need a write barrier.
    ElementAccess const access = {kTaggedBase, FixedArray::kHeaderSize,
                                  Type::Any(), MachineType::AnyTagged(),
                                  kNoWriteBarrier};
    __ StoreElement(access, result, index, the_hole);

    // Advance the {index}.
    index = __ IntAdd(index, __ IntPtrConstant(1));
    __ Goto(&loop, index);
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerNewArgumentsElements(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  const NewArgumentsElementsParameters& parameters =
      NewArgumentsElementsParametersOf(node->op());
  CreateArgumentsType type = parameters.arguments_type();
  Operator::Properties const properties = node->op()->properties();
  CallDescriptor::Flags const flags = CallDescriptor::kNoFlags;
  Node* frame = __ LoadFramePointer();
  Node* arguments_count = NodeProperties::GetValueInput(node, 0);
  Builtin builtin_name;
  switch (type) {
    case CreateArgumentsType::kMappedArguments:
      builtin_name = Builtin::kNewSloppyArgumentsElements;
      break;
    case CreateArgumentsType::kUnmappedArguments:
      builtin_name = Builtin::kNewStrictArgumentsElements;
      break;
    case CreateArgumentsType::kRestParameter:
      builtin_name = Builtin::kNewRestArgumentsElements;
      break;
  }
  Callable const callable = Builtins::CallableFor(isolate(), builtin_name);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), frame,
                 __ IntPtrConstant(parameters.formal_parameter_count()),
                 arguments_count);
}

Node* EffectControlLinearizer::LowerNewConsString(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  if (string_builder_optimizer_->ConcatIsInStringBuilder(node)) {
    return LowerStringConcat(node);
  }
  Node* length = node->InputAt(0);
  Node* first = node->InputAt(1);
  Node* second = node->InputAt(2);

  // Determine the instance types of {first} and {second}.
  Node* first_map = __ LoadField(AccessBuilder::ForMap(), first);
  Node* first_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), first_map);
  Node* second_map = __ LoadField(AccessBuilder::ForMap(), second);
  Node* second_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), second_map);

  // Determine the proper map for the resulting ConsString.
  // If both {first} and {second} are one-byte strings, we
  // create a new ConsOneByteString, otherwise we create a
  // new ConsString instead.
  auto if_onebyte = __ MakeLabel();
  auto if_twobyte = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTaggedPointer);
  static_assert(kOneByteStringTag != 0);
  static_assert(kTwoByteStringTag == 0);
  Node* instance_type = __ Word32And(first_instance_type, second_instance_type);
  Node* encoding =
      __ Word32And(instance_type, __ Int32Constant(kStringEncodingMask));
  __ Branch(__ Word32Equal(encoding, __ Int32Constant(kTwoByteStringTag)),
            &if_twobyte, &if_onebyte);
  __ Bind(&if_onebyte);
  __ Goto(&done, __ HeapConstant(factory()->cons_one_byte_string_map()));
  __ Bind(&if_twobyte);
  __ Goto(&done, __ HeapConstant(factory()->cons_two_byte_string_map()));
  __ Bind(&done);
  Node* result_map = done.PhiAt(0);

  // Allocate the resulting ConsString.
  Node* result = __ Allocate(AllocationType::kYoung,
                             __ IntPtrConstant(sizeof(ConsString)));
  __ StoreField(AccessBuilder::ForMap(), result, result_map);
  __ StoreField(AccessBuilder::ForNameRawHashField(), result,
                __ Int32Constant(Name::kEmptyHashField));
  __ StoreField(AccessBuilder::ForStringLength(), result, length);
  __ StoreField(AccessBuilder::ForConsStringFirst(), result, first);
  __ StoreField(AccessBuilder::ForConsStringSecond(), result, second);
  return result;
}

Node* EffectControlLinearizer::LowerSameValue(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kSameValue);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), lhs, rhs);
}

Node* EffectControlLinearizer::LowerSameValueNumbersOnly(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kSameValueNumbersOnly);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), lhs, rhs);
}

Node* EffectControlLinearizer::LowerNumberSameValue(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  auto is_float64_equal = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kBit);

  __ GotoIf(__ Float64Equal(lhs, rhs), &is_float64_equal);

  // Return true iff both {lhs} and {rhs} are NaN.
  __ GotoIf(__ Float64Equal(lhs, lhs), &done, __ Int32Constant(0));
  __ GotoIf(__ Float64Equal(rhs, rhs), &done, __ Int32Constant(0));
  __ Goto(&done, __ Int32Constant(1));

  __ Bind(&is_float64_equal);
  // Even if the values are float64-equal, we still need to distinguish
  // zero and minus zero.
  Node* lhs_hi = __ Float64ExtractHighWord32(lhs);
  Node* rhs_hi = __ Float64ExtractHighWord32(rhs);
  __ Goto(&done, __ Word32Equal(lhs_hi, rhs_hi));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerDeadValue(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* input = NodeProperties::GetValueInput(node, 0);
  if (input->opcode() != IrOpcode::kUnreachable) {
    // There is no fundamental reason not to connect to end here, except it
    // integrates into the way the graph is constructed in a simpler way at
    // this point.
    // TODO(jgruber): Connect to end here as well.
    Node* unreachable = __ UnreachableWithoutConnectToEnd();
    NodeProperties::ReplaceValueInput(node, unreachable, 0);
  }
  return gasm()->AddNode(node);
}

Node* EffectControlLinearizer::LowerStringToNumber(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* string = node->InputAt(0);

  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kStringToNumber);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), string);
}

Node* EffectControlLinearizer::StringCharCodeAt(Node* receiver,
                                                Node* position) {
  // We need a loop here to properly deal with indirect strings
  // (SlicedString, ConsString and ThinString).
  auto loop = __ MakeLoopLabel(MachineRepresentation::kTagged,
                               MachineType::PointerRepresentation());
  auto loop_next = __ MakeLabel(MachineRepresentation::kTagged,
                                MachineType::PointerRepresentation());
  auto loop_done = __ MakeLabel(MachineRepresentation::kWord32);
  __ Goto(&loop, receiver, position);
  __ Bind(&loop);
  {
    receiver = loop.PhiAt(0);
    position = loop.PhiAt(1);
    Node* receiver_map = __ LoadField(AccessBuilder::ForMap(), receiver);
    Node* receiver_instance_type =
        __ LoadField(AccessBuilder::ForMapInstanceType(), receiver_map);
    Node* receiver_representation = __ Word32And(
        receiver_instance_type, __ Int32Constant(kStringRepresentationMask));

    // Dispatch on the current {receiver}s string representation.
    auto if_lessthanoreq_cons = __ MakeLabel();
    auto if_greaterthan_cons = __ MakeLabel();
    auto if_seqstring = __ MakeLabel();
    auto if_consstring = __ MakeLabel();
    auto if_thinstring = __ MakeLabel();
    auto if_externalstring = __ MakeLabel();
    auto if_slicedstring = __ MakeLabel();
    auto if_runtime = __ MakeDeferredLabel();

    __ Branch(__ Int32LessThanOrEqual(receiver_representation,
                                      __ Int32Constant(kConsStringTag)),
              &if_lessthanoreq_cons, &if_greaterthan_cons);

    __ Bind(&if_lessthanoreq_cons);
    {
      __ Branch(__ Word32Equal(receiver_representation,
                               __ Int32Constant(kConsStringTag)),
                &if_consstring, &if_seqstring);
    }

    __ Bind(&if_greaterthan_cons);
    {
      __ GotoIf(__ Word32Equal(receiver_representation,
                               __ Int32Constant(kThinStringTag)),
                &if_thinstring);
      __ GotoIf(__ Word32Equal(receiver_representation,
                               __ Int32Constant(kExternalStringTag)),
                &if_externalstring);
      __ Branch(__ Word32Equal(receiver_representation,
                               __ Int32Constant(kSlicedStringTag)),
                &if_slicedstring, &if_runtime);
    }

    __ Bind(&if_seqstring);
    {
      Node* receiver_is_onebyte =
          __ Word32Equal(__ Word32And(receiver_instance_type,
                                      __ Int32Constant(kStringEncodingMask)),
                         __ Int32Constant(kOneByteStringTag));
      Node* result = LoadFromSeqString(receiver, position, receiver_is_onebyte);
      __ Goto(&loop_done, result);
    }

    __ Bind(&if_consstring);
    {
      Node* receiver_second =
          __ LoadField(AccessBuilder::ForConsStringSecond(), receiver);
      __ GotoIfNot(__ TaggedEqual(receiver_second, __ EmptyStringConstant()),
                   &if_runtime);
      Node* receiver_first =
          __ LoadField(AccessBuilder::ForConsStringFirst(), receiver);
      __ Goto(&loop_next, receiver_first, position);
    }

    __ Bind(&if_thinstring);
    {
      Node* receiver_actual =
          __ LoadField(AccessBuilder::ForThinStringActual(), receiver);
      __ Goto(&loop_next, receiver_actual, position);
    }

    __ Bind(&if_externalstring);
    {
      // We need to bailout to the runtime for uncached external strings.
      __ GotoIf(__ Word32Equal(
                    __ Word32And(receiver_instance_type,
                                 __ Int32Constant(kUncachedExternalStringMask)),
                    __ Int32Constant(kUncachedExternalStringTag)),
                &if_runtime);

      Node* receiver_data = __ LoadField(
          AccessBuilder::ForExternalStringResourceData(), receiver);

      auto if_onebyte = __ MakeLabel();
      auto if_twobyte = __ MakeLabel();
      __ Branch(
          __ Word32Equal(__ Word32And(receiver_instance_type,
                                      __ Int32Constant(kStringEncodingMask)),
                         __ Int32Constant(kTwoByteStringTag)),
          &if_twobyte, &if_onebyte);

      __ Bind(&if_onebyte);
      {
        Node* result = __ Load(MachineType::Uint8(), receiver_data, position);
        __ Goto(&loop_done, result);
      }

      __ Bind(&if_twobyte);
      {
        Node* result = __ Load(MachineType::Uint16(), receiver_data,
                               __ WordShl(position, __ IntPtrConstant(1)));
        __ Goto(&loop_done, result);
      }
    }

    __ Bind(&if_slicedstring);
    {
      Node* receiver_offset =
          __ LoadField(AccessBuilder::ForSlicedStringOffset(), receiver);
      Node* receiver_parent =
          __ LoadField(AccessBuilder::ForSlicedStringParent(), receiver);
      __ Goto(&loop_next, receiver_parent,
              __ IntAdd(position, ChangeSmiToIntPtr(receiver_offset)));
    }

    __ Bind(&if_runtime);
    {
      Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
      Runtime::FunctionId id = Runtime::kStringCharCodeAt;
      auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
          graph()->zone(), id, 2, properties, CallDescriptor::kNoFlags);
      Node* result = __ Call(call_descriptor, __ CEntryStubConstant(1),
                             receiver, ChangeIntPtrToSmi(position),
                             __ ExternalConstant(ExternalReference::Create(id)),
                             __ Int32Constant(2), __ NoContextConstant());
      __ Goto(&loop_done, ChangeSmiToInt32(result));
    }

    __ Bind(&loop_next);
    __ Goto(&loop, loop_next.PhiAt(0), loop_next.PhiAt(1));
  }
  __ Bind(&loop_done);
  return loop_done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerStringCharCodeAt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* receiver = node->InputAt(0);
  Node* position = node->InputAt(1);
  return StringCharCodeAt(receiver, position);
}

Node* EffectControlLinearizer::LowerStringCodePointAt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* receiver = node->InputAt(0);
  Node* position = node->InputAt(1);

  auto return_result = __ MakeLabel(MachineRepresentation::kWord32);
  Node* first_code_unit = StringCharCodeAt(receiver, position);

  __ GotoIfNot(
      __ Word32Equal(__ Word32And(first_code_unit, __ Int32Constant(0xFC00)),
                     __ Int32Constant(0xD800)),
      &return_result, BranchHint::kFalse, first_code_unit);

  auto length = __ ChangeUint32ToUintPtr(
      __ LoadField(AccessBuilder::ForStringLength(), receiver));
  auto next_index = __ IntAdd(position, __ IntPtrConstant(1));
  __ GotoIfNot(__ IntLessThan(next_index, length), &return_result,
               first_code_unit);
  Node* second_code_unit = StringCharCodeAt(receiver, next_index);
  __ GotoIfNot(
      __ Word32Equal(__ Word32And(second_code_unit, __ Int32Constant(0xFC00)),
                     __ Int32Constant(0xDC00)),
      &return_result, first_code_unit);

  auto surrogate_offset = __ Int32Constant(0x10000 - (0xD800 << 10) - 0xDC00);
  auto result = __ Int32Add(__ Word32Shl(first_code_unit, __ Int32Constant(10)),
                            __ Int32Add(second_code_unit, surrogate_offset));
  __ Goto(&return_result, result);

  __ Bind(&return_result);
  return return_result.PhiAt(0);
}

Node* EffectControlLinearizer::LoadFromSeqString(Node* receiver, Node* position,
                                                 Node* is_one_byte) {
  auto one_byte_load = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);
  __ GotoIf(is_one_byte, &one_byte_load);
  Node* two_byte_result = __ LoadElement(
      AccessBuilder::ForSeqTwoByteStringCharacter(), receiver, position);
  __ Goto(&done, two_byte_result);

  __ Bind(&one_byte_load);
  Node* one_byte_element = __ LoadElement(
      AccessBuilder::ForSeqOneByteStringCharacter(), receiver, position);
  __ Goto(&done, one_byte_element);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerStringFromSingleCharCode(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  if (string_builder_optimizer_->IsStringBuilderConcatInput(node)) {
    return node;
  }
  Node* value = node->InputAt(0);
  Node* code = __ Word32And(value, __ Uint32Constant(0xFFFF));

  auto if_not_one_byte = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // Check if the {code} is a one byte character
  Node* check1 = __ Uint32LessThanOrEqual(
      code, __ Uint32Constant(String::kMaxOneByteCharCode));
  __ GotoIfNot(check1, &if_not_one_byte);
  {
    // Load the isolate wide single character string table.
    Node* table = __ HeapConstant(factory()->single_character_string_table());

    // Compute the {table} index for {code}.
    Node* index = machine()->Is32() ? code : __ ChangeUint32ToUint64(code);

    // Load the string for the {code} from the single character string
    // table.
    Node* entry =
        __ LoadElement(AccessBuilder::ForFixedArrayElement(), table, index);

    // Use the {entry} from the {table}.
    __ Goto(&done, entry);
  }

  __ Bind(&if_not_one_byte);
  {
    // Allocate a new SeqTwoByteString for {code}.
    Node* vfalse1 =
        __ Allocate(AllocationType::kYoung,
                    __ IntPtrConstant(SeqTwoByteString::SizeFor(1)));
    __ Store(StoreRepresentation(MachineRepresentation::kTaggedSigned,
                                 kNoWriteBarrier),
             vfalse1,
             SeqTwoByteString::SizeFor(1) - kObjectAlignment - kHeapObjectTag,
             __ SmiConstant(0));
    __ StoreField(AccessBuilder::ForMap(), vfalse1,
                  __ HeapConstant(factory()->seq_two_byte_string_map()));
    __ StoreField(AccessBuilder::ForNameRawHashField(), vfalse1,
                  __ Int32Constant(Name::kEmptyHashField));
    __ StoreField(AccessBuilder::ForStringLength(), vfalse1,
                  __ Int32Constant(1));
    __ Store(
        StoreRepresentation(MachineRepresentation::kWord16, kNoWriteBarrier),
        vfalse1,
        __ IntPtrConstant(
            AccessBuilder::ForSeqTwoByteStringCharacter().header_size -
            kHeapObjectTag),
        code);
    __ Goto(&done, vfalse1);
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

#ifdef V8_INTL_SUPPORT

Node* EffectControlLinearizer::LowerStringToLowerCaseIntl(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* receiver = node->InputAt(0);

  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kStringToLowerCaseIntl);
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), receiver,
                 __ NoContextConstant());
}

Node* EffectControlLinearizer::LowerStringToUpperCaseIntl(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* receiver = node->InputAt(0);
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  Runtime::FunctionId id = Runtime::kStringToUpperCaseIntl;
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      graph()->zone(), id, 1, properties, CallDescriptor::kNoFlags);
  return __ Call(call_descriptor, __ CEntryStubConstant(1), receiver,
                 __ ExternalConstant(ExternalReference::Create(id)),
                 __ Int32Constant(1), __ NoContextConstant());
}

#else

Node* EffectControlLinearizer::LowerStringToLowerCaseIntl(Node* node) {
  UNREACHABLE();
}

Node* EffectControlLinearizer::LowerStringToUpperCaseIntl(Node* node) {
  UNREACHABLE();
}

#endif  // V8_INTL_SUPPORT

Node* EffectControlLinearizer::LowerStringFromSingleCodePoint(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* code = value;

  auto if_not_single_code = __ MakeDeferredLabel();
  auto if_not_one_byte = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // Check if the {code} is a single code unit
  Node* check0 = __ Uint32LessThanOrEqual(code, __ Uint32Constant(0xFFFF));
  __ GotoIfNot(check0, &if_not_single_code);

  {
    // Check if the {code} is a one byte character
    Node* check1 = __ Uint32LessThanOrEqual(
        code, __ Uint32Constant(String::kMaxOneByteCharCode));
    __ GotoIfNot(check1, &if_not_one_byte);
    {
      // Load the isolate wide single character string table.
      Node* table = __ HeapConstant(factory()->single_character_string_table());

      // Compute the {table} index for {code}.
      Node* index = machine()->Is32() ? code : __ ChangeUint32ToUint64(code);

      // Load the string for the {code} from the single character string
      // table.
      Node* entry =
          __ LoadElement(AccessBuilder::ForFixedArrayElement(), table, index);

      // Use the {entry} from the {table}.
      __ Goto(&done, entry);
    }

    __ Bind(&if_not_one_byte);
    {
      // Allocate a new SeqTwoByteString for {code}.
      Node* vfalse1 =
          __ Allocate(AllocationType::kYoung,
                      __ IntPtrConstant(SeqTwoByteString::SizeFor(1)));
      __ Store(StoreRepresentation(MachineRepresentation::kTaggedSigned,
                                   kNoWriteBarrier),
               vfalse1,
               SeqTwoByteString::SizeFor(1) - kObjectAlignment - kHeapObjectTag,
               __ SmiConstant(0));
      __ StoreField(AccessBuilder::ForMap(), vfalse1,
                    __ HeapConstant(factory()->seq_two_byte_string_map()));
      __ StoreField(AccessBuilder::ForNameRawHashField(), vfalse1,
                    __ IntPtrConstant(Name::kEmptyHashField));
      __ StoreField(AccessBuilder::ForStringLength(), vfalse1,
                    __ Int32Constant(1));
      __ Store(
          StoreRepresentation(MachineRepresentation::kWord16, kNoWriteBarrier),
          vfalse1,
          __ IntPtrConstant(
              AccessBuilder::ForSeqTwoByteStringCharacter().header_size -
              kHeapObjectTag),
          code);
      __ Goto(&done, vfalse1);
    }
  }

  __ Bind(&if_not_single_code);
  // Generate surrogate pair string
  {
    // Convert UTF32 to UTF16 code units, and store as a 32 bit word.
    Node* lead_offset = __ Int32Constant(0xD800 - (0x10000 >> 10));

    // lead = (codepoint >> 10) + LEAD_OFFSET
    Node* lead =
        __ Int32Add(__ Word32Shr(code, __ Int32Constant(10)), lead_offset);

    // trail = (codepoint & 0x3FF) + 0xDC00;
    Node* trail = __ Int32Add(__ Word32And(code, __ Int32Constant(0x3FF)),
                              __ Int32Constant(0xDC00));

    // codpoint = (trail << 16) | lead;
#if V8_TARGET_BIG_ENDIAN
    code = __ Word32Or(__ Word32Shl(lead, __ Int32Constant(16)), trail);
#else
    code = __ Word32Or(__ Word32Shl(trail, __ Int32Constant(16)), lead);
#endif

    // Allocate a new SeqTwoByteString for {code}.
    Node* vfalse0 =
        __ Allocate(AllocationType::kYoung,
                    __ IntPtrConstant(SeqTwoByteString::SizeFor(2)));
    __ Store(StoreRepresentation(MachineRepresentation::kTaggedSigned,
                                 kNoWriteBarrier),
             vfalse0,
             SeqTwoByteString::SizeFor(2) - kObjectAlignment - kHeapObjectTag,
             __ SmiConstant(0));
    __ StoreField(AccessBuilder::ForMap(), vfalse0,
                  __ HeapConstant(factory()->seq_two_byte_string_map()));
    __ StoreField(AccessBuilder::ForNameRawHashField(), vfalse0,
                  __ Int32Constant(Name::kEmptyHashField));
    __ StoreField(AccessBuilder::ForStringLength(), vfalse0,
                  __ Int32Constant(2));
    __ Store(
        StoreRepresentation(MachineRepresentation::kWord32, kNoWriteBarrier),
        vfalse0,
        __ IntPtrConstant(
            AccessBuilder::ForSeqTwoByteStringCharacter().header_size -
            kHeapObjectTag),
        code);
    __ Goto(&done, vfalse0);
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerStringIndexOf(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* subject = node->InputAt(0);
  Node* search_string = node->InputAt(1);
  Node* position = node->InputAt(2);

  Callable callable = Builtins::CallableFor(isolate(), Builtin::kStringIndexOf);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), subject,
                 search_string, position);
}

Node* EffectControlLinearizer::LowerStringFromCodePointAt(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* string = node->InputAt(0);
  Node* index = node->InputAt(1);

  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kStringFromCodePointAt);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), string,
                 index);
}

Node* EffectControlLinearizer::LowerStringLength(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* subject = node->InputAt(0);

  return __ LoadField(AccessBuilder::ForStringLength(), subject);
}

Node* EffectControlLinearizer::LowerStringEqual(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Callable callable = Builtins::CallableFor(isolate(), Builtin::kStringEqual);
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);
  Node* lhs_length = __ LoadField(AccessBuilder::ForStringLength(), lhs);
  Node* rhs_length = __ LoadField(AccessBuilder::ForStringLength(), rhs);

  auto if_length_equal = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  __ GotoIf(__ TaggedEqual(lhs, rhs), &done, __ TrueConstant());
  __ GotoIf(__ Word32Equal(lhs_length, rhs_length), &if_length_equal);
  __ Goto(&done, __ FalseConstant());

  __ Bind(&if_length_equal);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  Node* result = __ Call(call_descriptor, __ HeapConstant(callable.code()), lhs,
                         rhs, lhs_length);
  __ Goto(&done, result);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerStringComparison(Callable const& callable,
                                                     Node* node) {
  Node* lhs = node->InputAt(0);
  Node* rhs = node->InputAt(1);

  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), lhs, rhs);
}

Node* EffectControlLinearizer::LowerStringSubstring(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* receiver = node->InputAt(0);
  Node* start = ChangeInt32ToIntPtr(node->InputAt(1));
  Node* end = ChangeInt32ToIntPtr(node->InputAt(2));

  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kStringSubstring);
  Operator::Properties properties = Operator::kEliminatable;
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), receiver,
                 start, end);
}

Node* EffectControlLinearizer::LowerStringLessThan(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return LowerStringComparison(
      Builtins::CallableFor(isolate(), Builtin::kStringLessThan), node);
}

Node* EffectControlLinearizer::LowerStringLessThanOrEqual(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return LowerStringComparison(
      Builtins::CallableFor(isolate(), Builtin::kStringLessThanOrEqual), node);
}

Node* EffectControlLinearizer::LowerBigIntAdd(Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntAddNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntSubtract(Node* node,
                                                   Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntSubtractNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntMultiply(Node* node,
                                                   Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntMultiplyNoThrow);

  auto if_termreq = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  // Check for exception sentinel: Smi 1 is returned to signal
  // TerminationRequested.
  __ GotoIf(__ TaggedEqual(value, __ SmiConstant(1)), &if_termreq);

  // Check for exception sentinel: Smi 0 is returned to signal
  // BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);
  __ Goto(&done);

  __ Bind(&if_termreq);
  {
    Runtime::FunctionId id = Runtime::kTerminateExecution;
    auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
        graph()->zone(), id, 0, Operator::kNoDeopt,
        CallDescriptor::kNeedsFrameState);
    __ Call(call_descriptor, __ CEntryStubConstant(1),
            __ ExternalConstant(ExternalReference::Create(id)),
            __ Int32Constant(0), __ NoContextConstant(), frame_state);
    __ Goto(&done);
  }

  __ Bind(&done);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntDivide(Node* node,
                                                 Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntDivideNoThrow);

  auto if_termreq = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  // Check for exception sentinel
  // - Smi 0 is returned to signal BigIntDivZero
  // - Smi 1 is returned to signal TerminationRequested
  __ GotoIf(__ TaggedEqual(value, __ SmiConstant(1)), &if_termreq);

  __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  __ Goto(&done);

  __ Bind(&if_termreq);
  {
    Runtime::FunctionId id = Runtime::kTerminateExecution;
    auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
        graph()->zone(), id, 0, Operator::kNoDeopt,
        CallDescriptor::kNeedsFrameState);
    __ Call(call_descriptor, __ CEntryStubConstant(1),
            __ ExternalConstant(ExternalReference::Create(id)),
            __ Int32Constant(0), __ NoContextConstant(), frame_state);
    __ Goto(&done);
  }

  __ Bind(&done);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntModulus(Node* node,
                                                  Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntModulusNoThrow);

  auto if_termreq = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  // Check for exception sentinel
  // - Smi 0 is returned to signal BigIntDivZero
  // - Smi 1 is returned to signal TerminationRequested
  __ GotoIf(__ TaggedEqual(value, __ SmiConstant(1)), &if_termreq);

  __ DeoptimizeIf(DeoptimizeReason::kDivisionByZero, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  __ Goto(&done);

  __ Bind(&if_termreq);
  {
    Runtime::FunctionId id = Runtime::kTerminateExecution;
    auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
        graph()->zone(), id, 0, Operator::kNoDeopt,
        CallDescriptor::kNeedsFrameState);
    __ Call(call_descriptor, __ CEntryStubConstant(1),
            __ ExternalConstant(ExternalReference::Create(id)),
            __ Int32Constant(0), __ NoContextConstant(), frame_state);
    __ Goto(&done);
  }

  __ Bind(&done);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntBitwiseAnd(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntBitwiseAndNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntBitwiseOr(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                   Builtin::kBigIntBitwiseOrNoThrow);
}

Node* EffectControlLinearizer::LowerBigIntBitwiseXor(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntBitwiseXorNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntShiftLeft(Node* node,
                                                    Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntShiftLeftNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntShiftRight(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                          Builtin::kBigIntShiftRightNoThrow);

  // Check for exception sentinel: Smi is returned to signal BigIntTooBig.
  __ DeoptimizeIf(DeoptimizeReason::kBigIntTooBig, FeedbackSource{},
                  ObjectIsSmi(value), frame_state);

  return value;
}

Node* EffectControlLinearizer::LowerBigIntEqual(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                   Builtin::kBigIntEqual);
}

Node* EffectControlLinearizer::LowerBigIntLessThan(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                   Builtin::kBigIntLessThan);
}

Node* EffectControlLinearizer::LowerBigIntLessThanOrEqual(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  return CallBuiltinForBigIntBinop(node->InputAt(0), node->InputAt(1),
                                   Builtin::kBigIntLessThanOrEqual);
}

Node* EffectControlLinearizer::LowerBigIntNegate(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Callable const callable =
      Builtins::CallableFor(isolate(), Builtin::kBigIntUnaryMinus);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), CallDescriptor::kNoFlags,
      Operator::kFoldable | Operator::kNoThrow);
  Node* value = __ Call(call_descriptor, __ HeapConstant(callable.code()),
                        node->InputAt(0), __ NoContextConstant());

  return value;
}

Node* EffectControlLinearizer::LowerCheckFloat64Hole(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  // If we reach this point w/o eliminating the {node} that's marked
  // with allow-return-hole, we cannot do anything, so just deoptimize
  // in case of the hole NaN.
  CheckFloat64HoleParameters const& params =
      CheckFloat64HoleParametersOf(node->op());
  Node* value = node->InputAt(0);

  auto if_nan = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  // First check whether {value} is a NaN at all...
  __ Branch(__ Float64Equal(value, value), &done, &if_nan);

  __ Bind(&if_nan);
  {
    // ...and only if {value} is a NaN, perform the expensive bit
    // check. See http://crbug.com/v8/8264 for details.
    Node* check = __ Word32Equal(__ Float64ExtractHighWord32(value),
                                 __ Int32Constant(kHoleNanUpper32));
    __ DeoptimizeIf(DeoptimizeReason::kHole, params.feedback(), check,
                    frame_state);
    __ Goto(&done);
  }

  __ Bind(&done);
  return value;
}

Node* EffectControlLinearizer::LowerChangeFloat64HoleToTagged(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_nan = __ MakeDeferredLabel();
  auto allocate_heap_number = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // First check whether {value} is a NaN at all...
  __ Branch(__ Float64Equal(value, value), &allocate_heap_number, &if_nan);

  __ Bind(&if_nan);
  {
    // ...and only if {value} is a NaN, perform the expensive bit
    // check. See http://crbug.com/v8/8264 for details.
    __ GotoIfNot(__ Word32Equal(__ Float64ExtractHighWord32(value),
                                __ Int32Constant(kHoleNanUpper32)),
                 &allocate_heap_number);
    __ Goto(&done, __ UndefinedConstant());
  }

  __ Bind(&allocate_heap_number);
  __ Goto(&done, AllocateHeapNumberWithValue(value));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerCheckNotTaggedHole(Node* node,
                                                       Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  Node* check = __ TaggedEqual(value, __ TheHoleConstant());
  __ DeoptimizeIf(DeoptimizeReason::kHole, FeedbackSource(), check,
                  frame_state);
  return value;
}

Node* EffectControlLinearizer::LowerConvertTaggedHoleToUndefined(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_is_hole = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  Node* check = __ TaggedEqual(value, __ TheHoleConstant());
  __ GotoIf(check, &if_is_hole);
  __ Goto(&done, value);

  __ Bind(&if_is_hole);
  __ Goto(&done, __ UndefinedConstant());

  __ Bind(&done);
  return done.PhiAt(0);
}

void EffectControlLinearizer::LowerCheckEqualsInternalizedString(
    Node* node, Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* exp = node->InputAt(0);
  Node* val = node->InputAt(1);

  auto if_same = __ MakeLabel();
  auto if_notsame = __ MakeDeferredLabel();
  auto if_thinstring = __ MakeLabel();
  auto if_notthinstring = __ MakeLabel();

  // Check if {exp} and {val} are the same, which is the likely case.
  __ Branch(__ TaggedEqual(exp, val), &if_same, &if_notsame);

  __ Bind(&if_notsame);
  {
    // Now {val} could still be a non-internalized String that matches {exp}.
    __ DeoptimizeIf(DeoptimizeReason::kWrongName, FeedbackSource(),
                    ObjectIsSmi(val), frame_state);
    Node* val_map = __ LoadField(AccessBuilder::ForMap(), val);
    Node* val_instance_type =
        __ LoadField(AccessBuilder::ForMapInstanceType(), val_map);

    // ThinString.
    __ Branch(__ Word32Equal(
                  __ Word32And(val_instance_type,
                               __ Int32Constant(kStringRepresentationMask)),
                  __ Int32Constant(kThinStringTag)),
              &if_thinstring, &if_notthinstring);

    __ Bind(&if_notthinstring);
    {
      // Check that the {val} is a non-internalized String, if it's anything
      // else it cannot match the recorded feedback {exp} anyways.
      __ DeoptimizeIfNot(
          DeoptimizeReason::kWrongName, FeedbackSource(),
          __ Word32Equal(__ Word32And(val_instance_type,
                                      __ Int32Constant(kIsNotStringMask |
                                                       kIsNotInternalizedMask)),
                         __ Int32Constant(kStringTag | kNotInternalizedTag)),
          frame_state);

      // Try to find the {val} in the string table.
      MachineSignature::Builder builder(graph()->zone(), 1, 2);
      builder.AddReturn(MachineType::AnyTagged());
      builder.AddParam(MachineType::Pointer());
      builder.AddParam(MachineType::AnyTagged());
      Node* try_string_to_index_or_lookup_existing = __ ExternalConstant(
          ExternalReference::try_string_to_index_or_lookup_existing());
      Node* const isolate_ptr =
          __ ExternalConstant(ExternalReference::isolate_address(isolate()));
      auto call_descriptor =
          Linkage::GetSimplifiedCDescriptor(graph()->zone(), builder.Build());
      Node* val_internalized =
          __ Call(common()->Call(call_descriptor),
                  try_string_to_index_or_lookup_existing, isolate_ptr, val);

      // Now see if the results match.
      __ DeoptimizeIfNot(DeoptimizeReason::kWrongName, FeedbackSource(),
                         __ TaggedEqual(exp, val_internalized), frame_state);
      __ Goto(&if_same);
    }

    __ Bind(&if_thinstring);
    {
      // The {val} is a ThinString, let's check the actual value.
      Node* val_actual =
          __ LoadField(AccessBuilder::ForThinStringActual(), val);
      __ DeoptimizeIfNot(DeoptimizeReason::kWrongName, FeedbackSource(),
                         __ TaggedEqual(exp, val_actual), frame_state);
      __ Goto(&if_same);
    }
  }

  __ Bind(&if_same);
}

void EffectControlLinearizer::LowerCheckEqualsSymbol(Node* node,
                                                     Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  Node* exp = node->InputAt(0);
  Node* val = node->InputAt(1);
  Node* check = __ TaggedEqual(exp, val);
  __ DeoptimizeIfNot(DeoptimizeReason::kWrongName, FeedbackSource(), check,
                     frame_state);
}

Node* EffectControlLinearizer::AllocateHeapNumberWithValue(Node* value) {
  Node* result = __ Allocate(AllocationType::kYoung,
                             __ IntPtrConstant(sizeof(HeapNumber)));
  __ StoreField(AccessBuilder::ForMap(), result, __ HeapNumberMapConstant());
  __ StoreField(AccessBuilder::ForHeapNumberValue(), result, value);
  return result;
}

Node* EffectControlLinearizer::ChangeIntPtrToSmi(Node* value) {
  // Do shift on 32bit values if Smis are stored in the lower word.
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    return ChangeTaggedInt32ToSmi(__ Word32Shl(value, SmiShiftBitsConstant()));
  }
  return __ WordShl(value, SmiShiftBitsConstant());
}

Node* EffectControlLinearizer::ChangeTaggedInt32ToSmi(Node* value) {
  DCHECK(SmiValuesAre31Bits());
  // In pointer compression, we smi-corrupt. Then, the upper bits are not
  // important.
  return COMPRESS_POINTERS_BOOL ? __ BitcastWord32ToWord64(value)
                                : ChangeInt32ToIntPtr(value);
}

Node* EffectControlLinearizer::ChangeInt32ToIntPtr(Node* value) {
  if (machine()->Is64()) {
    value = __ ChangeInt32ToInt64(value);
  }
  return value;
}

Node* EffectControlLinearizer::ChangeIntPtrToInt32(Node* value) {
  if (machine()->Is64()) {
    value = __ TruncateInt64ToInt32(value);
  }
  return value;
}

Node* EffectControlLinearizer::ChangeInt32ToSmi(Node* value) {
  // Do shift on 32bit values if Smis are stored in the lower word.
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    return ChangeIntPtrToSmi(value);
  }
  return ChangeIntPtrToSmi(ChangeInt32ToIntPtr(value));
}

Node* EffectControlLinearizer::ChangeInt64ToBigInt(Node* value) {
  DCHECK(machine()->Is64());

  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // BigInts with value 0 must be of size 0 (canonical form).
  __ GotoIf(__ Word64Equal(value, __ IntPtrConstant(0)), &done,
            BuildAllocateBigInt(nullptr, nullptr));

  // Shift sign bit into BigInt's sign bit position.
  Node* sign =
      __ Word64Shr(value, __ IntPtrConstant(63 - BigInt::SignBits::kShift));
  Node* bitfield =
      __ Word32Or(__ Int32Constant(BigInt::LengthBits::encode(1)), sign);

  // We use (value XOR (value >> 63)) - (value >> 63) to compute the
  // absolute value, in a branchless fashion.
  Node* sign_mask = __ Word64Sar(value, __ Int64Constant(63));
  Node* absolute_value = __ Int64Sub(__ Word64Xor(value, sign_mask), sign_mask);
  __ Goto(&done, BuildAllocateBigInt(bitfield, absolute_value));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::ChangeInt64ToSmi(Node* value) {
  DCHECK(machine()->Is64());
  return ChangeIntPtrToSmi(value);
}

Node* EffectControlLinearizer::ChangeUint32ToUintPtr(Node* value) {
  if (machine()->Is64()) {
    value = __ ChangeUint32ToUint64(value);
  }
  return value;
}

Node* EffectControlLinearizer::ChangeInt64ToTagged(Node* value) {
  auto if_not_in_smi_range = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  Node* value32 = __ TruncateInt64ToInt32(value);
  __ GotoIfNot(__ Word64Equal(__ ChangeInt32ToInt64(value32), value),
               &if_not_in_smi_range);

  if (SmiValuesAre32Bits()) {
    Node* value_smi = ChangeInt64ToSmi(value);
    __ Goto(&done, value_smi);
  } else {
    SmiTagOrOverflow(value32, &if_not_in_smi_range, &done);
  }

  __ Bind(&if_not_in_smi_range);
  Node* number = AllocateHeapNumberWithValue(__ ChangeInt64ToFloat64(value));
  __ Goto(&done, number);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::ChangeUint64ToBigInt(Node* value) {
  DCHECK(machine()->Is64());

  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // BigInts with value 0 must be of size 0 (canonical form).
  __ GotoIf(__ Word64Equal(value, __ IntPtrConstant(0)), &done,
            BuildAllocateBigInt(nullptr, nullptr));

  const auto bitfield = BigInt::LengthBits::encode(1);
  __ Goto(&done, BuildAllocateBigInt(__ Int32Constant(bitfield), value));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::ChangeUint64ToTagged(Node* value) {
  auto if_not_in_smi_range = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  Node* check =
      __ Uint64LessThanOrEqual(value, __ Int64Constant(Smi::kMaxValue));
  __ GotoIfNot(check, &if_not_in_smi_range);
  __ Goto(&done, ChangeInt64ToSmi(value));

  __ Bind(&if_not_in_smi_range);
  Node* number = AllocateHeapNumberWithValue(__ ChangeInt64ToFloat64(value));

  __ Goto(&done, number);
  __ Bind(&done);

  return done.PhiAt(0);
}

Node* EffectControlLinearizer::ChangeUint32ToSmi(Node* value) {
  // Do shift on 32bit values if Smis are stored in the lower word.
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    Node* smi_value = __ Word32Shl(value, SmiShiftBitsConstant());
    // In pointer compression, we smi-corrupt. Then, the upper bits are not
    // important.
    return COMPRESS_POINTERS_BOOL ? __ BitcastWord32ToWord64(smi_value)
                                  : __ ChangeUint32ToUint64(smi_value);
  } else {
    return __ WordShl(ChangeUint32ToUintPtr(value), SmiShiftBitsConstant());
  }
}

Node* EffectControlLinearizer::ChangeSmiToIntPtr(Node* value) {
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    // First sign-extend the upper half, then shift away the Smi tag.
    return __ WordSarShiftOutZeros(
        __ ChangeInt32ToInt64(__ TruncateInt64ToInt32(value)),
        SmiShiftBitsConstant());
  }
  return __ WordSarShiftOutZeros(value, SmiShiftBitsConstant());
}

Node* EffectControlLinearizer::ChangeSmiToInt32(Node* value) {
  // Do shift on 32bit values if Smis are stored in the lower word.
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    return __ Word32SarShiftOutZeros(__ TruncateInt64ToInt32(value),
                                     SmiShiftBitsConstant());
  }
  if (machine()->Is64()) {
    return __ TruncateInt64ToInt32(ChangeSmiToIntPtr(value));
  }
  return ChangeSmiToIntPtr(value);
}

Node* EffectControlLinearizer::ChangeSmiToInt64(Node* value) {
  CHECK(machine()->Is64());
  return ChangeSmiToIntPtr(value);
}

Node* EffectControlLinearizer::ObjectIsSmi(Node* value) {
  return __ Word32Equal(__ Word32And(value, __ Int32Constant(kSmiTagMask)),
                        __ Int32Constant(kSmiTag));
}

Node* EffectControlLinearizer::JSAnyIsNotPrimitiveHeapObject(Node* value,
                                                             Node* value_map) {
  if (value_map == nullptr) {
    value_map = __ LoadField(AccessBuilder::ForMap(), value);
  }
#if V8_STATIC_ROOTS_BOOL
  // Assumes only primitive objects and JS_RECEIVER's are passed here.
  // All primitive object's maps are allocated at the start of the read only
  // heap. Thus JS_RECEIVER's must have maps with larger (compressed) addresses.
  return __ Uint32LessThan(
      __ Int32Constant(InstanceTypeChecker::kNonJsReceiverMapLimit), value_map);
#else
  static_assert(LAST_TYPE == LAST_JS_RECEIVER_TYPE);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  return __ Uint32LessThanOrEqual(__ Uint32Constant(FIRST_JS_RECEIVER_TYPE),
                                  value_instance_type);
#endif
}

Node* EffectControlLinearizer::SmiMaxValueConstant() {
  return __ Int32Constant(Smi::kMaxValue);
}

Node* EffectControlLinearizer::SmiShiftBitsConstant() {
  if (machine()->Is64() && SmiValuesAre31Bits()) {
    return __ Int32Constant(kSmiShiftSize + kSmiTagSize);
  }
  return __ IntPtrConstant(kSmiShiftSize + kSmiTagSize);
}

Node* EffectControlLinearizer::LowerPlainPrimitiveToNumber(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);
  return __ PlainPrimitiveToNumber(TNode<Object>::UncheckedCast(value));
}

Node* EffectControlLinearizer::LowerPlainPrimitiveToWord32(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto if_to_number_smi = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord32);

  Node* check0 = ObjectIsSmi(value);
  __ GotoIfNot(check0, &if_not_smi);
  __ Goto(&done, ChangeSmiToInt32(value));

  __ Bind(&if_not_smi);
  Node* to_number =
      __ PlainPrimitiveToNumber(TNode<Object>::UncheckedCast(value));

  Node* check1 = ObjectIsSmi(to_number);
  __ GotoIf(check1, &if_to_number_smi);
  Node* number = __ LoadField(AccessBuilder::ForHeapNumberValue(), to_number);
  __ Goto(&done, __ TruncateFloat64ToWord32(number));

  __ Bind(&if_to_number_smi);
  __ Goto(&done, ChangeSmiToInt32(to_number));

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerPlainPrimitiveToFloat64(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* value = node->InputAt(0);

  auto if_not_smi = __ MakeDeferredLabel();
  auto if_to_number_smi = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* check0 = ObjectIsSmi(value);
  __ GotoIfNot(check0, &if_not_smi);
  Node* from_smi = ChangeSmiToInt32(value);
  __ Goto(&done, __ ChangeInt32ToFloat64(from_smi));

  __ Bind(&if_not_smi);
  Node* to_number =
      __ PlainPrimitiveToNumber(TNode<Object>::UncheckedCast(value));
  Node* check1 = ObjectIsSmi(to_number);
  __ GotoIf(check1, &if_to_number_smi);

  Node* number = __ LoadField(AccessBuilder::ForHeapNumberValue(), to_number);
  __ Goto(&done, number);

  __ Bind(&if_to_number_smi);
  Node* number_from_smi = ChangeSmiToInt32(to_number);
  number_from_smi = __ ChangeInt32ToFloat64(number_from_smi);
  __ Goto(&done, number_from_smi);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerEnsureWritableFastElements(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* object = node->InputAt(0);
  Node* elements = node->InputAt(1);

  auto if_not_fixed_array = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // Load the current map of {elements}.
  Node* elements_map = __ LoadField(AccessBuilder::ForMap(), elements);

  // Check if {elements} is not a copy-on-write FixedArray.
  Node* check = __ TaggedEqual(elements_map, __ FixedArrayMapConstant());
  __ GotoIfNot(check, &if_not_fixed_array);
  // Nothing to do if the {elements} are not copy-on-write.
  __ Goto(&done, elements);

  __ Bind(&if_not_fixed_array);
  // We need to take a copy of the {elements} and set them up for {object}.
  Operator::Properties properties = Operator::kEliminatable;
  Callable callable =
      Builtins::CallableFor(isolate(), Builtin::kCopyFastSmiOrObjectElements);
  CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), flags, properties);
  Node* result =
      __ Call(call_descriptor, __ HeapConstant(callable.code()), object);
  __ Goto(&done, result);

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerMaybeGrowFastElements(Node* node,
                                                          Node* frame_state) {
  DCHECK(!v8_flags.turboshaft);
  GrowFastElementsParameters params = GrowFastElementsParametersOf(node->op());
  Node* object = node->InputAt(0);
  Node* elements = node->InputAt(1);
  Node* index = node->InputAt(2);
  Node* elements_length = node->InputAt(3);

  auto done = __ MakeLabel(MachineRepresentation::kTagged);
  auto if_grow = __ MakeDeferredLabel();
  auto if_not_grow = __ MakeLabel();

  // Check if we need to grow the {elements} backing store.
  Node* check = __ Uint32LessThan(index, elements_length);
  __ GotoIfNot(check, &if_grow);
  __ Goto(&done, elements);

  __ Bind(&if_grow);
  // We need to grow the {elements} for {object}.
  Operator::Properties properties = Operator::kEliminatable;
  Callable callable =
      (params.mode() == GrowFastElementsMode::kDoubleElements)
          ? Builtins::CallableFor(isolate(), Builtin::kGrowFastDoubleElements)
          : Builtins::CallableFor(isolate(),
                                  Builtin::kGrowFastSmiOrObjectElements);
  CallDescriptor::Flags call_flags = CallDescriptor::kNoFlags;
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), call_flags, properties);
  Node* new_elements =
      __ Call(call_descriptor, __ HeapConstant(callable.code()), object,
              ChangeInt32ToSmi(index));

  // Ensure that we were able to grow the {elements}.
  __ DeoptimizeIf(DeoptimizeReason::kCouldNotGrowElements, params.feedback(),
                  ObjectIsSmi(new_elements), frame_state);
  __ Goto(&done, new_elements);

  __ Bind(&done);
  return done.PhiAt(0);
}

void EffectControlLinearizer::LowerTransitionElementsKind(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ElementsTransition const transition = ElementsTransitionOf(node->op());
  Node* object = node->InputAt(0);

  auto if_map_same = __ MakeDeferredLabel();
  auto done = __ MakeLabel();

  Node* source_map = __ HeapConstant(transition.source().object());
  Node* target_map = __ HeapConstant(transition.target().object());

  // Load the current map of {object}.
  Node* object_map = __ LoadField(AccessBuilder::ForMap(), object);

  // Check if {object_map} is the same as {source_map}.
  Node* check = __ TaggedEqual(object_map, source_map);
  __ GotoIf(check, &if_map_same);
  __ Goto(&done);

  __ Bind(&if_map_same);
  switch (transition.mode()) {
    case ElementsTransition::kFastTransition:
      // In-place migration of {object}, just store the {target_map}.
      __ StoreField(AccessBuilder::ForMap(), object, target_map);
      break;
    case ElementsTransition::kSlowTransition: {
      // Instance migration, call out to the runtime for {object}.
      Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
      Runtime::FunctionId id = Runtime::kTransitionElementsKind;
      auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
          graph()->zone(), id, 2, properties, CallDescriptor::kNoFlags);
      __ Call(call_descriptor, __ CEntryStubConstant(1), object, target_map,
              __ ExternalConstant(ExternalReference::Create(id)),
              __ Int32Constant(2), __ NoContextConstant());
      break;
    }
  }
  __ Goto(&done);

  __ Bind(&done);
}

Node* EffectControlLinearizer::LowerLoadMessage(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* offset = node->InputAt(0);
  Node* object_pattern =
      __ LoadField(AccessBuilder::ForExternalIntPtr(), offset);
  return __ BitcastWordToTagged(object_pattern);
}

void EffectControlLinearizer::LowerStoreMessage(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* offset = node->InputAt(0);
  Node* object = node->InputAt(1);
  Node* object_pattern = __ BitcastTaggedToWord(object);
  __ StoreField(AccessBuilder::ForExternalIntPtr(), offset, object_pattern);
}

Node* EffectControlLinearizer::AdaptFastCallTypedArrayArgument(
    Node* node, ElementsKind expected_elements_kind,
    GraphAssemblerLabel<0>* bailout) {
  Node* value_map = __ LoadField(AccessBuilder::ForMap(), node);
  Node* value_instance_type =
      __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
  Node* value_is_typed_array = __ Word32Equal(
      value_instance_type, __ Int32Constant(JS_TYPED_ARRAY_TYPE));
  __ GotoIfNot(value_is_typed_array, bailout);

  Node* bit_field2 = __ LoadField(AccessBuilder::ForMapBitField2(), value_map);
  Node* mask = __ Int32Constant(Map::Bits2::ElementsKindBits::kMask);
  Node* andit = __ Word32And(bit_field2, mask);
  Node* shift = __ Int32Constant(Map::Bits2::ElementsKindBits::kShift);
  Node* kind = __ Word32Shr(andit, shift);

  Node* value_is_expected_elements_kind =
      __ Word32Equal(kind, __ Int32Constant(expected_elements_kind));
  __ GotoIfNot(value_is_expected_elements_kind, bailout);

  Node* buffer =
      __ LoadField(AccessBuilder::ForJSArrayBufferViewBuffer(), node);
  Node* buffer_bit_field =
      __ LoadField(AccessBuilder::ForJSArrayBufferBitField(), buffer);

  // Go to the slow path if the {buffer} was detached.
  Node* buffer_is_not_detached = __ Word32Equal(
      __ Word32And(buffer_bit_field,
                   __ Int32Constant(JSArrayBuffer::WasDetachedBit::kMask)),
      __ Int32Constant(0));
  __ GotoIfNot(buffer_is_not_detached, bailout);

  // Go to the slow path if the {buffer} is shared.
  Node* buffer_is_not_shared = __ Word32Equal(
      __ Word32And(buffer_bit_field,
                   __ Int32Constant(JSArrayBuffer::IsSharedBit::kMask)),
      __ Int32Constant(0));
  __ GotoIfNot(buffer_is_not_shared, bailout);

  // Unpack the store and length, and store them to a struct
  // FastApiTypedArray.
  Node* external_pointer =
      __ LoadField(AccessBuilder::ForJSTypedArrayExternalPointer(), node);

  // Load the base pointer for the buffer. This will always be Smi
  // zero unless we allow on-heap TypedArrays, which is only the case
  // for Chrome. Node and Electron both set this limit to 0. Setting
  // the base to Smi zero here allows the BuildTypedArrayDataPointer
  // to optimize away the tricky part of the access later.
  Node* base_pointer =
      __ LoadField(AccessBuilder::ForJSTypedArrayBasePointer(), node);
  if (JSTypedArray::kMaxSizeInHeap == 0) {
    base_pointer = jsgraph()->ZeroConstant();
  }
  Node* data_ptr = BuildTypedArrayDataPointer(base_pointer, external_pointer);
  Node* length_in_bytes =
      __ LoadField(AccessBuilder::ForJSTypedArrayLength(), node);

  // We hard-code int32_t here, because all specializations of
  // FastApiTypedArray have the same size.
  constexpr int kAlign = alignof(FastApiTypedArray<int32_t>);
  constexpr int kSize = sizeof(FastApiTypedArray<int32_t>);
  static_assert(kAlign == alignof(FastApiTypedArray<double>),
                "Alignment mismatch between different specializations of "
                "FastApiTypedArray");
  static_assert(kSize == sizeof(FastApiTypedArray<double>),
                "Size mismatch between different specializations of "
                "FastApiTypedArray");
  static_assert(
      kSize == sizeof(uintptr_t) + sizeof(size_t),
      "The size of "
      "FastApiTypedArray isn't equal to the sum of its expected members.");
  Node* stack_slot = __ StackSlot(kSize, kAlign);

  __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                               kNoWriteBarrier),
           stack_slot, 0, length_in_bytes);
  __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                               kNoWriteBarrier),
           stack_slot, sizeof(size_t), data_ptr);
  static_assert(sizeof(uintptr_t) == sizeof(size_t),
                "The buffer length can't "
                "fit the PointerRepresentation used to store it.");

  return stack_slot;
}

Node* EffectControlLinearizer::ClampFastCallArgument(
    Node* input, CTypeInfo::Type scalar_type) {
  Node* min = nullptr;
  Node* max = nullptr;
  switch (scalar_type) {
    case CTypeInfo::Type::kInt32:
      min = __ Float64Constant(std::numeric_limits<int32_t>::min());
      max = __ Float64Constant(std::numeric_limits<int32_t>::max());
      break;
    case CTypeInfo::Type::kUint32:
      min = __ Float64Constant(0);
      max = __ Float64Constant(std::numeric_limits<uint32_t>::max());
      break;
    case CTypeInfo::Type::kInt64:
      min = __ Float64Constant(kMinSafeInteger);
      max = __ Float64Constant(kMaxSafeInteger);
      break;
    case CTypeInfo::Type::kUint64:
      min = __ Float64Constant(0);
      max = __ Float64Constant(kMaxSafeInteger);
      break;
    default:
      UNREACHABLE();
  }
  CHECK_NOT_NULL(min);
  CHECK_NOT_NULL(max);

  Node* clamped = graph()->NewNode(
      common()->Select(MachineRepresentation::kFloat64),
      graph()->NewNode(machine()->Float64LessThan(), min, input),
      graph()->NewNode(
          common()->Select(MachineRepresentation::kFloat64),
          graph()->NewNode(machine()->Float64LessThan(), input, max), input,
          max),
      min);

  Node* rounded = graph()->NewNode(
      machine()->Float64RoundTiesEven().placeholder(), clamped);

  auto if_zero = __ MakeDeferredLabel();
  auto if_zero_or_nan = __ MakeDeferredLabel();
  auto check_done = __ MakeLabel();
  auto check_for_nan = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kWord64);

  Node* check_is_zero = __ Float64Equal(rounded, __ Float64Constant(0));
  __ Branch(check_is_zero, &if_zero_or_nan, &check_for_nan);

  // Check if {rounded} is NaN.
  __ Bind(&check_for_nan);
  Node* diff = __ Float64Equal(rounded, rounded);
  Node* check_is_nan = __ Word32Equal(diff, __ Int32Constant(0));
  __ Branch(check_is_nan, &if_zero_or_nan, &check_done);

  __ Bind(&if_zero_or_nan);
  {
    switch (scalar_type) {
      case CTypeInfo::Type::kInt32:
        __ Goto(&done, __ Int32Constant(0));
        break;
      case CTypeInfo::Type::kUint32:
        __ Goto(&done, __ Uint32Constant(0));
        break;
      case CTypeInfo::Type::kInt64:
        __ Goto(&done, __ Int64Constant(0));
        break;
      case CTypeInfo::Type::kUint64:
        __ Goto(&done, __ Uint64Constant(0));
        break;
      default:
        UNREACHABLE();
    }
  }

  __ Bind(&check_done);
  {
    switch (scalar_type) {
      case CTypeInfo::Type::kInt32:
        __ Goto(&done, __ ChangeFloat64ToInt32(rounded));
        break;
      case CTypeInfo::Type::kUint32:
        __ Goto(&done, __ ChangeFloat64ToUint32(rounded));
        break;
      case CTypeInfo::Type::kInt64:
        __ Goto(&done, __ ChangeFloat64ToInt64(rounded));
        break;
      case CTypeInfo::Type::kUint64:
        __ Goto(&done, __ ChangeFloat64ToUint64(rounded));
        break;
      default:
        UNREACHABLE();
    }
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::AdaptFastCallArgument(
    Node* node, CTypeInfo arg_type, GraphAssemblerLabel<0>* if_error) {
  switch (arg_type.GetSequenceType()) {
    case CTypeInfo::SequenceType::kScalar: {
      uint8_t flags = uint8_t(arg_type.GetFlags());
      if (flags & uint8_t(CTypeInfo::Flags::kEnforceRangeBit)) {
        Node* truncation;
        switch (arg_type.GetType()) {
          case CTypeInfo::Type::kInt32:
            truncation = __ TryTruncateFloat64ToInt32(node);
            __ GotoIfNot(__ Projection(1, truncation, __ start()), if_error);
            return __ Projection(0, truncation, __ start());
          case CTypeInfo::Type::kUint32:
            truncation = __ TryTruncateFloat64ToUint32(node);
            __ GotoIfNot(__ Projection(1, truncation, __ start()), if_error);
            return __ Projection(0, truncation, __ start());
          case CTypeInfo::Type::kInt64:
            truncation = __ TryTruncateFloat64ToInt64(node);
            __ GotoIfNot(__ Projection(1, truncation, __ start()), if_error);
            return __ Projection(0, truncation, __ start());
          case CTypeInfo::Type::kUint64:
            truncation = __ TryTruncateFloat64ToUint64(node);
            __ GotoIfNot(__ Projection(1, truncation, __ start()), if_error);
            return __ Projection(0, truncation, __ start());
          default: {
            __ Goto(if_error);
            return node;
          }
        }
      } else if (flags & uint8_t(CTypeInfo::Flags::kClampBit)) {
        return ClampFastCallArgument(node, arg_type.GetType());
      } else {
        switch (arg_type.GetType()) {
          case CTypeInfo::Type::kV8Value: {
            return __ AdaptLocalArgument(node);
          }
          case CTypeInfo::Type::kFloat32: {
            return __ TruncateFloat64ToFloat32(node);
          }
          case CTypeInfo::Type::kPointer: {
            // Check that the value is a HeapObject.
            Node* const value_is_smi = ObjectIsSmi(node);
            __ GotoIf(value_is_smi, if_error);

            auto if_null = __ MakeDeferredLabel();
            auto done = __ MakeLabel(MachineType::PointerRepresentation());

            // Check if the value is null
            __ GotoIf(__ TaggedEqual(node, __ NullConstant()), &if_null);

            {
              // Check that the value is a JSExternalObject.
              Node* const is_external =
                  __ TaggedEqual(__ LoadField(AccessBuilder::ForMap(), node),
                                 __ ExternalObjectMapConstant());

              __ GotoIfNot(is_external, if_error);

              Node* external_pointer =
                  __ LoadField(AccessBuilder::ForJSExternalObjectValue(), node);

              __ Goto(&done, external_pointer);
            }

            // Value is null, signifying a null pointer.
            __ Bind(&if_null);
            { __ Goto(&done, __ IntPtrConstant(0)); }

            __ Bind(&done);
            return done.PhiAt(0);
          }
          case CTypeInfo::Type::kSeqOneByteString: {
            // Check that the value is a HeapObject.
            Node* value_is_smi = ObjectIsSmi(node);
            __ GotoIf(value_is_smi, if_error);

            Node* map = __ LoadField(AccessBuilder::ForMap(), node);
            Node* instance_type =
                __ LoadField(AccessBuilder::ForMapInstanceType(), map);

            Node* encoding = __ Word32And(
                instance_type,
                __ Int32Constant(kStringRepresentationAndEncodingMask));

            Node* is_onebytestring = __ Word32Equal(
                encoding, __ Int32Constant(kSeqOneByteStringTag));
            __ GotoIfNot(is_onebytestring, if_error);

            Node* length_in_bytes =
                __ LoadField(AccessBuilder::ForStringLength(), node);
            Node* data_ptr = __ IntPtrAdd(
                __ BitcastTaggedToWord(node),
                __ IntPtrConstant(
                    AccessBuilder::ForSeqOneByteStringCharacter().header_size -
                    kHeapObjectTag));

            constexpr int kAlign = alignof(FastOneByteString);
            constexpr int kSize = sizeof(FastOneByteString);
            static_assert(kSize == sizeof(uintptr_t) + sizeof(size_t),
                          "The size of "
                          "FastOneByteString isn't equal to the sum of its "
                          "expected members.");
            Node* stack_slot = __ StackSlot(kSize, kAlign);

            __ Store(StoreRepresentation(MachineType::PointerRepresentation(),
                                         kNoWriteBarrier),
                     stack_slot, 0, data_ptr);
            __ Store(StoreRepresentation(MachineRepresentation::kWord32,
                                         kNoWriteBarrier),
                     stack_slot, sizeof(size_t), length_in_bytes);

            static_assert(sizeof(uintptr_t) == sizeof(size_t),
                          "The string length can't "
                          "fit the PointerRepresentation used to store it.");

            return stack_slot;
          }
          default: {
            return node;
          }
        }
      }
    }
    case CTypeInfo::SequenceType::kIsSequence: {
      CHECK_EQ(arg_type.GetType(), CTypeInfo::Type::kVoid);

      // Check that the value is a HeapObject.
      Node* value_is_smi = ObjectIsSmi(node);
      __ GotoIf(value_is_smi, if_error);

      Node* node_to_pass = __ AdaptLocalArgument(node);

      // Check that the value is a JSArray.
      Node* value_map = __ LoadField(AccessBuilder::ForMap(), node);
      Node* value_instance_type =
          __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
      Node* value_is_js_array =
          __ Word32Equal(value_instance_type, __ Int32Constant(JS_ARRAY_TYPE));
      __ GotoIfNot(value_is_js_array, if_error);

      return node_to_pass;
    }
    case CTypeInfo::SequenceType::kIsTypedArray: {
      // Check that the value is a HeapObject.
      Node* value_is_smi = ObjectIsSmi(node);
      __ GotoIf(value_is_smi, if_error);

      return AdaptFastCallTypedArrayArgument(
          node, fast_api_call::GetTypedArrayElementsKind(arg_type.GetType()),
          if_error);
    }
    default: {
      UNREACHABLE();
    }
  }
}

EffectControlLinearizer::AdaptOverloadedFastCallResult
EffectControlLinearizer::AdaptOverloadedFastCallArgument(
    Node* node, const FastApiCallFunctionVector& c_functions,
    const fast_api_call::OverloadsResolutionResult& overloads_resolution_result,
    GraphAssemblerLabel<0>* if_error) {
  auto merge = __ MakeLabel(MachineRepresentation::kTagged,
                            MachineRepresentation::kTagged);

  for (size_t func_index = 0; func_index < c_functions.size(); func_index++) {
    const CFunctionInfo* c_signature = c_functions[func_index].signature;
    CTypeInfo arg_type = c_signature->ArgumentInfo(
        overloads_resolution_result.distinguishable_arg_index);

    auto next = __ MakeLabel();

    // Check that the value is a HeapObject.
    Node* value_is_smi = ObjectIsSmi(node);
    __ GotoIf(value_is_smi, if_error);

    ExternalReference::Type ref_type = ExternalReference::FAST_C_CALL;

    switch (arg_type.GetSequenceType()) {
      case CTypeInfo::SequenceType::kIsSequence: {
        CHECK_EQ(arg_type.GetType(), CTypeInfo::Type::kVoid);

        // Check that the value is a JSArray.
        Node* value_map = __ LoadField(AccessBuilder::ForMap(), node);
        Node* value_instance_type =
            __ LoadField(AccessBuilder::ForMapInstanceType(), value_map);
        Node* value_is_js_array = __ Word32Equal(
            value_instance_type, __ Int32Constant(JS_ARRAY_TYPE));
        __ GotoIfNot(value_is_js_array, &next);

        Node* node_to_pass = __ AdaptLocalArgument(node);

        Node* target_address = __ ExternalConstant(ExternalReference::Create(
            c_functions[func_index].address, ref_type));
        __ Goto(&merge, target_address, node_to_pass);
        break;
      }

      case CTypeInfo::SequenceType::kIsTypedArray: {
        // Check that the value is a TypedArray with a type that matches the
        // type declared in the c-function.
        Node* stack_slot = AdaptFastCallTypedArrayArgument(
            node,
            fast_api_call::GetTypedArrayElementsKind(
                overloads_resolution_result.element_type),
            &next);
        Node* target_address = __ ExternalConstant(ExternalReference::Create(
            c_functions[func_index].address, ref_type));
        __ Goto(&merge, target_address, stack_slot);
        break;
      }

      default: {
        UNREACHABLE();
      }
    }

    __ Bind(&next);
  }
  __ Goto(if_error);

  __ Bind(&merge);
  return {merge.PhiAt(0), merge.PhiAt(1)};
}

Node* EffectControlLinearizer::GenerateSlowApiCall(Node* node) {
  FastApiCallNode n(node);
  FastApiCallParameters const& params = n.Parameters();
  const CFunctionInfo* c_signature = params.c_functions()[0].signature;
  const int c_arg_count = c_signature->ArgumentCount();

  Node** const slow_inputs = graph()->zone()->AllocateArray<Node*>(
      n.SlowCallArgumentCount() + FastApiCallNode::kEffectAndControlInputCount);

  int fast_call_params = c_arg_count;
  CHECK_EQ(node->op()->ValueInputCount() - fast_call_params,
           n.SlowCallArgumentCount());
  int index = 0;
  for (; index < n.SlowCallArgumentCount(); ++index) {
    slow_inputs[index] = n.SlowCallArgument(index);
  }

  slow_inputs[index] = __ effect();
  slow_inputs[index + 1] = __ control();
  Node* slow_call_result = __ Call(
      params.descriptor(), index + FastApiCallNode::kEffectAndControlInputCount,
      slow_inputs);
  return slow_call_result;
}

Node* EffectControlLinearizer::LowerFastApiCall(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  FastApiCallNode n(node);
  FastApiCallParameters const& params = n.Parameters();

  const FastApiCallFunctionVector& c_functions = params.c_functions();
  const CFunctionInfo* c_signature = params.c_functions()[0].signature;
  const int c_arg_count = c_signature->ArgumentCount();
  CallDescriptor* js_call_descriptor = params.descriptor();
  int js_arg_count = static_cast<int>(js_call_descriptor->ParameterCount());
  const int value_input_count = node->op()->ValueInputCount();
  CHECK_EQ(FastApiCallNode::ArityForArgc(c_arg_count, js_arg_count),
           value_input_count);

  return fast_api_call::BuildFastApiCall(
      isolate(), graph(), gasm(), c_functions, c_signature,
      n.SlowCallArgument(FastApiCallNode::kSlowCallDataArgumentIndex),
      // Load and convert parameters to be passed to C function
      [this, node, c_signature, c_functions](
          int param_index,
          fast_api_call::OverloadsResolutionResult& overloads_resolution_result,
          GraphAssemblerLabel<0>* if_error) {
        Node* value = NodeProperties::GetValueInput(node, param_index);
        if (param_index ==
            overloads_resolution_result.distinguishable_arg_index) {
          // This only happens when the FastApiCall node represents multiple
          // overloaded functions and {i} is the index of the distinguishable
          // argument.
          AdaptOverloadedFastCallResult nodes = AdaptOverloadedFastCallArgument(
              value, c_functions, overloads_resolution_result, if_error);

          // Replace the target address node with a Phi node that represents the
          // choice between the target addreseses of overloaded functions.
          overloads_resolution_result.target_address = nodes.target_address;

          return nodes.argument;
        } else {
          CTypeInfo type = c_signature->ArgumentInfo(param_index);
          return AdaptFastCallArgument(value, type, if_error);
        }
      },
      // Convert return value from C function to JS value
      [this](const CFunctionInfo* c_signature, Node* c_call_result) -> Node* {
        // TODO(mslekova, aapoalas): Implement these conversions in SL.
        switch (c_signature->ReturnInfo().GetType()) {
          case CTypeInfo::Type::kVoid:
            return __ UndefinedConstant();
          case CTypeInfo::Type::kBool:
            static_assert(sizeof(bool) == 1, "unsupported bool size");
            return ChangeBitToTagged(
                __ Word32And(c_call_result, __ Int32Constant(0xFF)));
          case CTypeInfo::Type::kInt32:
            return ChangeInt32ToTagged(c_call_result);
          case CTypeInfo::Type::kUint32:
            return ChangeUint32ToTagged(c_call_result);
          case CTypeInfo::Type::kInt64: {
            CFunctionInfo::Int64Representation repr =
                c_signature->GetInt64Representation();
            if (repr == CFunctionInfo::Int64Representation::kBigInt) {
              return ChangeInt64ToBigInt(c_call_result);
            } else if (repr == CFunctionInfo::Int64Representation::kNumber) {
              return ChangeInt64ToTagged(c_call_result);
            } else {
              UNREACHABLE();
            }
          }
          case CTypeInfo::Type::kUint64: {
            CFunctionInfo::Int64Representation repr =
                c_signature->GetInt64Representation();
            if (repr == CFunctionInfo::Int64Representation::kBigInt) {
              return ChangeUint64ToBigInt(c_call_result);
            } else if (repr == CFunctionInfo::Int64Representation::kNumber) {
              return ChangeUint64ToTagged(c_call_result);
            } else {
              UNREACHABLE();
            }
          }
          case CTypeInfo::Type::kFloat32:
            return ChangeFloat64ToTagged(
                __ ChangeFloat32ToFloat64(c_call_result),
                CheckForMinusZeroMode::kCheckForMinusZero);
          case CTypeInfo::Type::kFloat64:
            return ChangeFloat64ToTagged(
                c_call_result, CheckForMinusZeroMode::kCheckForMinusZero);
          case CTypeInfo::Type::kPointer:
            return BuildAllocateJSExternalObject(c_call_result);
          case CTypeInfo::Type::kSeqOneByteString:
          case CTypeInfo::Type::kV8Value:
          case CTypeInfo::Type::kApiObject:
          case CTypeInfo::Type::kUint8:
            UNREACHABLE();
          case CTypeInfo::Type::kAny:
            return ChangeFloat64ToTagged(
                __ ChangeInt64ToFloat64(c_call_result),
                CheckForMinusZeroMode::kCheckForMinusZero);
        }
      },
      // Initialize js-specific callback options.
      [this](Node* options_stack_slot) {
        __ Store(
            StoreRepresentation(MachineType::PointerRepresentation(),
                                kNoWriteBarrier),
            options_stack_slot,
            static_cast<int>(offsetof(v8::FastApiCallbackOptions, wasm_memory)),
            __ IntPtrConstant(0));
      },
      // Generate slow fallback if fast call fails
      [this, node]() -> Node* { return GenerateSlowApiCall(node); });
}

Node* EffectControlLinearizer::LowerLoadFieldByIndex(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* object = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* zero = __ IntPtrConstant(0);
  Node* one = __ IntPtrConstant(1);

  // Sign-extend the {index} on 64-bit architectures.
  if (machine()->Is64()) {
    index = __ ChangeInt32ToInt64(index);
  }

  auto if_double = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // Check if field is a mutable double field.
  __ GotoIfNot(__ IntPtrEqual(__ WordAnd(index, one), zero), &if_double);

  // The field is a proper Tagged field on {object}. The {index} is shifted
  // to the left by one in the code below.
  {
    // Check if field is in-object or out-of-object.
    auto if_outofobject = __ MakeLabel();
    __ GotoIf(__ IntLessThan(index, zero), &if_outofobject);

    // The field is located in the {object} itself.
    {
      Node* offset =
          __ IntAdd(__ WordShl(index, __ IntPtrConstant(kTaggedSizeLog2 - 1)),
                    __ IntPtrConstant(JSObject::kHeaderSize - kHeapObjectTag));
      Node* result = __ Load(MachineType::AnyTagged(), object, offset);
      __ Goto(&done, result);
    }

    // The field is located in the properties backing store of {object}.
    // The {index} is equal to the negated out of property index plus 1.
    __ Bind(&if_outofobject);
    {
      Node* properties = __ LoadField(
          AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer(), object);
      Node* offset =
          __ IntAdd(__ WordShl(__ IntSub(zero, index),
                               __ IntPtrConstant(kTaggedSizeLog2 - 1)),
                    __ IntPtrConstant((FixedArray::kHeaderSize - kTaggedSize) -
                                      kHeapObjectTag));
      Node* result = __ Load(MachineType::AnyTagged(), properties, offset);
      __ Goto(&done, result);
    }
  }

  // The field is a Double field, either unboxed in the object on 64-bit
  // architectures, or a mutable HeapNumber.
  __ Bind(&if_double);
  {
    auto loaded_field = __ MakeLabel(MachineRepresentation::kTagged);
    auto done_double = __ MakeLabel(MachineRepresentation::kFloat64);

    index = __ WordSar(index, one);

    // Check if field is in-object or out-of-object.
    auto if_outofobject = __ MakeLabel();
    __ GotoIf(__ IntLessThan(index, zero), &if_outofobject);

    // The field is located in the {object} itself.
    {
      Node* offset =
          __ IntAdd(__ WordShl(index, __ IntPtrConstant(kTaggedSizeLog2)),
                    __ IntPtrConstant(JSObject::kHeaderSize - kHeapObjectTag));
      Node* field = __ Load(MachineType::AnyTagged(), object, offset);
      __ Goto(&loaded_field, field);
    }

    __ Bind(&if_outofobject);
    {
      Node* properties = __ LoadField(
          AccessBuilder::ForJSObjectPropertiesOrHashKnownPointer(), object);
      Node* offset =
          __ IntAdd(__ WordShl(__ IntSub(zero, index),
                               __ IntPtrConstant(kTaggedSizeLog2)),
                    __ IntPtrConstant((FixedArray::kHeaderSize - kTaggedSize) -
                                      kHeapObjectTag));
      Node* field = __ Load(MachineType::AnyTagged(), properties, offset);
      __ Goto(&loaded_field, field);
    }

    __ Bind(&loaded_field);
    {
      Node* field = loaded_field.PhiAt(0);
      // We may have transitioned in-place away from double, so check that
      // this is a HeapNumber -- otherwise the load is fine and we don't need
      // to copy anything anyway.
      __ GotoIf(ObjectIsSmi(field), &done, field);
      Node* field_map = __ LoadField(AccessBuilder::ForMap(), field);
      __ GotoIfNot(__ TaggedEqual(field_map, __ HeapNumberMapConstant()), &done,
                   field);

      Node* value = __ LoadField(AccessBuilder::ForHeapNumberValue(), field);
      __ Goto(&done_double, value);
    }

    __ Bind(&done_double);
    {
      Node* result = AllocateHeapNumberWithValue(done_double.PhiAt(0));
      __ Goto(&done, result);
    }
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::BuildReverseBytes(ExternalArrayType type,
                                                 Node* value) {
  switch (type) {
    case kExternalInt8Array:
    case kExternalUint8Array:
    case kExternalUint8ClampedArray:
      return value;

    case kExternalInt16Array: {
      Node* result = __ Word32ReverseBytes(value);
      result = __ Word32Sar(result, __ Int32Constant(16));
      return result;
    }

    case kExternalUint16Array: {
      Node* result = __ Word32ReverseBytes(value);
      result = __ Word32Shr(result, __ Int32Constant(16));
      return result;
    }

    case kExternalInt32Array:  // Fall through.
    case kExternalUint32Array:
      return __ Word32ReverseBytes(value);

    case kExternalFloat32Array: {
      Node* result = __ BitcastFloat32ToInt32(value);
      result = __ Word32ReverseBytes(result);
      result = __ BitcastInt32ToFloat32(result);
      return result;
    }

    case kExternalFloat64Array: {
      if (machine()->Is64()) {
        Node* result = __ BitcastFloat64ToInt64(value);
        result = __ Word64ReverseBytes(result);
        result = __ BitcastInt64ToFloat64(result);
        return result;
      } else {
        Node* lo = __ Word32ReverseBytes(__ Float64ExtractLowWord32(value));
        Node* hi = __ Word32ReverseBytes(__ Float64ExtractHighWord32(value));
        Node* result = __ Float64Constant(0.0);
        result = __ Float64InsertLowWord32(result, hi);
        result = __ Float64InsertHighWord32(result, lo);
        return result;
      }
    }
    case kExternalFloat16Array:
    case kExternalBigInt64Array:
    case kExternalBigUint64Array:
      UNREACHABLE();
  }
}

Node* EffectControlLinearizer::LowerLoadDataViewElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ExternalArrayType element_type = ExternalArrayTypeOf(node->op());
  Node* object = node->InputAt(0);
  Node* storage = node->InputAt(1);
  Node* index = node->InputAt(2);
  Node* is_little_endian = node->InputAt(3);

  // We need to keep the {object} (either the JSArrayBuffer or the JSDataView)
  // alive so that the GC will not release the JSArrayBuffer (if there's any)
  // as long as we are still operating on it.
  __ Retain(object);

  MachineType const machine_type =
      AccessBuilder::ForTypedArrayElement(element_type, true).machine_type;

  Node* value = __ LoadUnaligned(machine_type, storage, index);
  auto big_endian = __ MakeLabel();
  auto done = __ MakeLabel(machine_type.representation());

  __ GotoIfNot(is_little_endian, &big_endian);
  {  // Little-endian load.
#if V8_TARGET_LITTLE_ENDIAN
    __ Goto(&done, value);
#else
    __ Goto(&done, BuildReverseBytes(element_type, value));
#endif  // V8_TARGET_LITTLE_ENDIAN
  }

  __ Bind(&big_endian);
  {  // Big-endian load.
#if V8_TARGET_LITTLE_ENDIAN
    __ Goto(&done, BuildReverseBytes(element_type, value));
#else
    __ Goto(&done, value);
#endif  // V8_TARGET_LITTLE_ENDIAN
  }

  // We're done, return {result}.
  __ Bind(&done);
  return done.PhiAt(0);
}

void EffectControlLinearizer::LowerStoreDataViewElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ExternalArrayType element_type = ExternalArrayTypeOf(node->op());
  Node* object = node->InputAt(0);
  Node* storage = node->InputAt(1);
  Node* index = node->InputAt(2);
  Node* value = node->InputAt(3);
  Node* is_little_endian = node->InputAt(4);

  // We need to keep the {object} (either the JSArrayBuffer or the JSDataView)
  // alive so that the GC will not release the JSArrayBuffer (if there's any)
  // as long as we are still operating on it.
  __ Retain(object);

  MachineType const machine_type =
      AccessBuilder::ForTypedArrayElement(element_type, true).machine_type;

  auto big_endian = __ MakeLabel();
  auto done = __ MakeLabel(machine_type.representation());

  __ GotoIfNot(is_little_endian, &big_endian);
  {  // Little-endian store.
#if V8_TARGET_LITTLE_ENDIAN
    __ Goto(&done, value);
#else
    __ Goto(&done, BuildReverseBytes(element_type, value));
#endif  // V8_TARGET_LITTLE_ENDIAN
  }

  __ Bind(&big_endian);
  {  // Big-endian store.
#if V8_TARGET_LITTLE_ENDIAN
    __ Goto(&done, BuildReverseBytes(element_type, value));
#else
    __ Goto(&done, value);
#endif  // V8_TARGET_LITTLE_ENDIAN
  }

  __ Bind(&done);
  __ StoreUnaligned(machine_type.representation(), storage, index,
                    done.PhiAt(0));
}

// Compute the data pointer, handling the case where the {external} pointer
// is the effective data pointer (i.e. the {base} is Smi zero).
Node* EffectControlLinearizer::BuildTypedArrayDataPointer(Node* base,
                                                          Node* external) {
  if (IntPtrMatcher(base).Is(0)) {
    return external;
  } else {
    base = __ BitcastTaggedToWord(base);
    if (COMPRESS_POINTERS_BOOL) {
      // Zero-extend Tagged_t to UintPtr according to current compression
      // scheme so that the addition with |external_pointer| (which already
      // contains compensated offset value) will decompress the tagged value.
      // See JSTypedArray::ExternalPointerCompensationForOnHeapArray() for
      // details.
      base = ChangeUint32ToUintPtr(base);
    }
    return __ IntPtrAdd(base, external);
  }
}

Node* EffectControlLinearizer::LowerLoadTypedElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ExternalArrayType array_type = ExternalArrayTypeOf(node->op());
  Node* buffer = node->InputAt(0);
  Node* base = node->InputAt(1);
  Node* external = node->InputAt(2);
  Node* index = node->InputAt(3);

  // We need to keep the {buffer} alive so that the GC will not release the
  // ArrayBuffer (if there's any) as long as we are still operating on it.
  __ Retain(buffer);

  Node* data_ptr = BuildTypedArrayDataPointer(base, external);

  // Perform the actual typed element access.
  return __ LoadElement(AccessBuilder::ForTypedArrayElement(array_type, true),
                        data_ptr, index);
}

Node* EffectControlLinearizer::LowerLoadStackArgument(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);

  Node* argument =
      __ LoadElement(AccessBuilder::ForStackArgument(), base, index);

  return __ BitcastWordToTagged(argument);
}

void EffectControlLinearizer::LowerStoreTypedElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ExternalArrayType array_type = ExternalArrayTypeOf(node->op());
  Node* buffer = node->InputAt(0);
  Node* base = node->InputAt(1);
  Node* external = node->InputAt(2);
  Node* index = node->InputAt(3);
  Node* value = node->InputAt(4);

  // We need to keep the {buffer} alive so that the GC will not release the
  // ArrayBuffer (if there's any) as long as we are still operating on it.
  __ Retain(buffer);

  Node* data_ptr = BuildTypedArrayDataPointer(base, external);

  // Perform the actual typed element access.
  __ StoreElement(AccessBuilder::ForTypedArrayElement(array_type, true),
                  data_ptr, index, value);
}

void EffectControlLinearizer::TransitionElementsTo(Node* node, Node* array,
                                                   ElementsKind from,
                                                   ElementsKind to) {
  DCHECK(IsMoreGeneralElementsKindTransition(from, to));
  DCHECK(to == HOLEY_ELEMENTS || to == HOLEY_DOUBLE_ELEMENTS);

  MapRef target(to == HOLEY_ELEMENTS ? FastMapParameterOf(node->op())
                                     : DoubleMapParameterOf(node->op()));
  Node* target_map = __ HeapConstant(target.object());

  if (IsSimpleMapChangeTransition(from, to)) {
    __ StoreField(AccessBuilder::ForMap(), array, target_map);
  } else {
    // Instance migration, call out to the runtime for {array}.
    Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
    Runtime::FunctionId id = Runtime::kTransitionElementsKind;
    auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
        graph()->zone(), id, 2, properties, CallDescriptor::kNoFlags);
    __ Call(call_descriptor, __ CEntryStubConstant(1), array, target_map,
            __ ExternalConstant(ExternalReference::Create(id)),
            __ Int32Constant(2), __ NoContextConstant());
  }
}

Node* EffectControlLinearizer::IsElementsKindGreaterThan(
    Node* kind, ElementsKind reference_kind) {
  Node* ref_kind = __ Int32Constant(reference_kind);
  Node* ret = __ Int32LessThan(ref_kind, kind);
  return ret;
}

void EffectControlLinearizer::LowerTransitionAndStoreElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* array = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  // Possibly transition array based on input and store.
  //
  //   -- TRANSITION PHASE -----------------
  //   kind = ElementsKind(array)
  //   if value is not smi {
  //     if kind == HOLEY_SMI_ELEMENTS {
  //       if value is heap number {
  //         Transition array to HOLEY_DOUBLE_ELEMENTS
  //         kind = HOLEY_DOUBLE_ELEMENTS
  //       } else {
  //         Transition array to HOLEY_ELEMENTS
  //         kind = HOLEY_ELEMENTS
  //       }
  //     } else if kind == HOLEY_DOUBLE_ELEMENTS {
  //       if value is not heap number {
  //         Transition array to HOLEY_ELEMENTS
  //         kind = HOLEY_ELEMENTS
  //       }
  //     }
  //   }
  //
  //   -- STORE PHASE ----------------------
  //   [make sure {kind} is up-to-date]
  //   if kind == HOLEY_DOUBLE_ELEMENTS {
  //     if value is smi {
  //       float_value = convert smi to float
  //       Store array[index] = float_value
  //     } else {
  //       float_value = value
  //       Store array[index] = float_value
  //     }
  //   } else {
  //     // kind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS
  //     Store array[index] = value
  //   }
  //
  Node* map = __ LoadField(AccessBuilder::ForMap(), array);
  Node* kind;
  {
    Node* bit_field2 = __ LoadField(AccessBuilder::ForMapBitField2(), map);
    Node* mask = __ Int32Constant(Map::Bits2::ElementsKindBits::kMask);
    Node* andit = __ Word32And(bit_field2, mask);
    Node* shift = __ Int32Constant(Map::Bits2::ElementsKindBits::kShift);
    kind = __ Word32Shr(andit, shift);
  }

  auto do_store = __ MakeLabel(MachineRepresentation::kWord32);
  // We can store a smi anywhere.
  __ GotoIf(ObjectIsSmi(value), &do_store, kind);

  // {value} is a HeapObject.
  auto transition_smi_array = __ MakeDeferredLabel();
  auto transition_double_to_fast = __ MakeDeferredLabel();
  {
    __ GotoIfNot(IsElementsKindGreaterThan(kind, HOLEY_SMI_ELEMENTS),
                 &transition_smi_array);
    __ GotoIfNot(IsElementsKindGreaterThan(kind, HOLEY_ELEMENTS), &do_store,
                 kind);

    // We have double elements kind. Only a HeapNumber can be stored
    // without effecting a transition.
    Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
    Node* heap_number_map = __ HeapNumberMapConstant();
    Node* check = __ TaggedEqual(value_map, heap_number_map);
    __ GotoIfNot(check, &transition_double_to_fast);
    __ Goto(&do_store, kind);
  }

  __ Bind(&transition_smi_array);  // deferred code.
  {
    // Transition {array} from HOLEY_SMI_ELEMENTS to HOLEY_DOUBLE_ELEMENTS or
    // to HOLEY_ELEMENTS.
    auto if_value_not_heap_number = __ MakeLabel();
    Node* value_map = __ LoadField(AccessBuilder::ForMap(), value);
    Node* heap_number_map = __ HeapNumberMapConstant();
    Node* check = __ TaggedEqual(value_map, heap_number_map);
    __ GotoIfNot(check, &if_value_not_heap_number);
    {
      // {value} is a HeapNumber.
      TransitionElementsTo(node, array, HOLEY_SMI_ELEMENTS,
                           HOLEY_DOUBLE_ELEMENTS);
      __ Goto(&do_store, __ Int32Constant(HOLEY_DOUBLE_ELEMENTS));
    }
    __ Bind(&if_value_not_heap_number);
    {
      TransitionElementsTo(node, array, HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS);
      __ Goto(&do_store, __ Int32Constant(HOLEY_ELEMENTS));
    }
  }

  __ Bind(&transition_double_to_fast);  // deferred code.
  {
    TransitionElementsTo(node, array, HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS);
    __ Goto(&do_store, __ Int32Constant(HOLEY_ELEMENTS));
  }

  // Make sure kind is up-to-date.
  __ Bind(&do_store);
  kind = do_store.PhiAt(0);

  Node* elements = __ LoadField(AccessBuilder::ForJSObjectElements(), array);
  auto if_kind_is_double = __ MakeLabel();
  auto done = __ MakeLabel();
  __ GotoIf(IsElementsKindGreaterThan(kind, HOLEY_ELEMENTS),
            &if_kind_is_double);
  {
    // Our ElementsKind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS.
    __ StoreElement(AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS),
                    elements, index, value);
    __ Goto(&done);
  }
  __ Bind(&if_kind_is_double);
  {
    // Our ElementsKind is HOLEY_DOUBLE_ELEMENTS.
    auto do_double_store = __ MakeLabel();
    __ GotoIfNot(ObjectIsSmi(value), &do_double_store);
    {
      Node* int_value = ChangeSmiToInt32(value);
      Node* float_value = __ ChangeInt32ToFloat64(int_value);
      __ StoreElement(AccessBuilder::ForFixedDoubleArrayElement(), elements,
                      index, float_value);
      __ Goto(&done);
    }
    __ Bind(&do_double_store);
    {
      Node* float_value =
          __ LoadField(AccessBuilder::ForHeapNumberValue(), value);
      __ StoreElement(AccessBuilder::ForFixedDoubleArrayElement(), elements,
                      index, __ Float64SilenceNaN(float_value));
      __ Goto(&done);
    }
  }

  __ Bind(&done);
}

void EffectControlLinearizer::LowerTransitionAndStoreNumberElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* array = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);  // This is a Float64, not tagged.

  // Possibly transition array based on input and store.
  //
  //   -- TRANSITION PHASE -----------------
  //   kind = ElementsKind(array)
  //   if kind == HOLEY_SMI_ELEMENTS {
  //     Transition array to HOLEY_DOUBLE_ELEMENTS
  //   } else if kind != HOLEY_DOUBLE_ELEMENTS {
  //     if kind == HOLEY_ELEMENTS {
  //       Store value as a HeapNumber in array[index].
  //     } else {
  //       This is UNREACHABLE, execute a debug break.
  //     }
  //   }
  //
  //   -- STORE PHASE ----------------------
  //   Store array[index] = value (it's a float)
  //
  Node* map = __ LoadField(AccessBuilder::ForMap(), array);
  Node* kind;
  {
    Node* bit_field2 = __ LoadField(AccessBuilder::ForMapBitField2(), map);
    Node* mask = __ Int32Constant(Map::Bits2::ElementsKindBits::kMask);
    Node* andit = __ Word32And(bit_field2, mask);
    Node* shift = __ Int32Constant(Map::Bits2::ElementsKindBits::kShift);
    kind = __ Word32Shr(andit, shift);
  }

  auto do_store = __ MakeLabel();
  auto done = __ MakeLabel();

  // {value} is a float64.
  auto transition_smi_array = __ MakeDeferredLabel();
  {
    auto do_store_holey_elements = __ MakeDeferredLabel();

    __ GotoIfNot(IsElementsKindGreaterThan(kind, HOLEY_SMI_ELEMENTS),
                 &transition_smi_array);
    // We expect that our input array started at HOLEY_SMI_ELEMENTS, and
    // climbs the lattice up to HOLEY_DOUBLE_ELEMENTS. However, loop peeling can
    // break this assumption, because in the peeled iteration, the array might
    // have transitioned to HOLEY_ELEMENTS kind, so we handle this as well.
    __ GotoIf(__ Word32Equal(kind, __ Int32Constant(HOLEY_DOUBLE_ELEMENTS)),
              &do_store);
    __ GotoIf(__ Word32Equal(kind, __ Int32Constant(HOLEY_ELEMENTS)),
              &do_store_holey_elements);
    __ Unreachable();

    __ Bind(&do_store_holey_elements);  // deferred code.
    {
      Node* elements =
          __ LoadField(AccessBuilder::ForJSObjectElements(), array);
      // Our ElementsKind is HOLEY_ELEMENTS.
      ElementAccess access =
          AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS);
      __ StoreElement(access, elements, index,
                      AllocateHeapNumberWithValue(value));
      __ Goto(&done);
    }
  }

  __ Bind(&transition_smi_array);  // deferred code.
  {
    // Transition {array} from HOLEY_SMI_ELEMENTS to HOLEY_DOUBLE_ELEMENTS.
    TransitionElementsTo(node, array, HOLEY_SMI_ELEMENTS,
                         HOLEY_DOUBLE_ELEMENTS);
    __ Goto(&do_store);
  }

  __ Bind(&do_store);
  {
    Node* elements = __ LoadField(AccessBuilder::ForJSObjectElements(), array);
    __ StoreElement(AccessBuilder::ForFixedDoubleArrayElement(), elements,
                    index, __ Float64SilenceNaN(value));
    __ Goto(&done);
  }

  __ Bind(&done);
}

void EffectControlLinearizer::LowerTransitionAndStoreNonNumberElement(
    Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* array = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);

  // Possibly transition array based on input and store.
  //
  //   -- TRANSITION PHASE -----------------
  //   kind = ElementsKind(array)
  //   if kind == HOLEY_SMI_ELEMENTS {
  //     Transition array to HOLEY_ELEMENTS
  //   } else if kind == HOLEY_DOUBLE_ELEMENTS {
  //     Transition array to HOLEY_ELEMENTS
  //   }
  //
  //   -- STORE PHASE ----------------------
  //   // kind is HOLEY_ELEMENTS
  //   Store array[index] = value
  //
  Node* map = __ LoadField(AccessBuilder::ForMap(), array);
  Node* kind;
  {
    Node* bit_field2 = __ LoadField(AccessBuilder::ForMapBitField2(), map);
    Node* mask = __ Int32Constant(Map::Bits2::ElementsKindBits::kMask);
    Node* andit = __ Word32And(bit_field2, mask);
    Node* shift = __ Int32Constant(Map::Bits2::ElementsKindBits::kShift);
    kind = __ Word32Shr(andit, shift);
  }

  auto do_store = __ MakeLabel();

  auto transition_smi_array = __ MakeDeferredLabel();
  auto transition_double_to_fast = __ MakeDeferredLabel();
  {
    __ GotoIfNot(IsElementsKindGreaterThan(kind, HOLEY_SMI_ELEMENTS),
                 &transition_smi_array);
    __ GotoIf(IsElementsKindGreaterThan(kind, HOLEY_ELEMENTS),
              &transition_double_to_fast);
    __ Goto(&do_store);
  }

  __ Bind(&transition_smi_array);  // deferred code.
  {
    // Transition {array} from HOLEY_SMI_ELEMENTS to HOLEY_ELEMENTS.
    TransitionElementsTo(node, array, HOLEY_SMI_ELEMENTS, HOLEY_ELEMENTS);
    __ Goto(&do_store);
  }

  __ Bind(&transition_double_to_fast);  // deferred code.
  {
    TransitionElementsTo(node, array, HOLEY_DOUBLE_ELEMENTS, HOLEY_ELEMENTS);
    __ Goto(&do_store);
  }

  __ Bind(&do_store);

  Node* elements = __ LoadField(AccessBuilder::ForJSObjectElements(), array);
  // Our ElementsKind is HOLEY_ELEMENTS.
  ElementAccess access = AccessBuilder::ForFixedArrayElement(HOLEY_ELEMENTS);
  Type value_type = ValueTypeParameterOf(node->op());
  if (value_type.Is(Type::BooleanOrNullOrUndefined())) {
    access.type = value_type;
    access.write_barrier_kind = kNoWriteBarrier;
  }
  __ StoreElement(access, elements, index, value);
}

void EffectControlLinearizer::LowerStoreSignedSmallElement(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* array = node->InputAt(0);
  Node* index = node->InputAt(1);
  Node* value = node->InputAt(2);  // int32

  // Store a signed small in an output array.
  //
  //   kind = ElementsKind(array)
  //
  //   -- STORE PHASE ----------------------
  //   if kind == HOLEY_DOUBLE_ELEMENTS {
  //     float_value = convert int32 to float
  //     Store array[index] = float_value
  //   } else {
  //     // kind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS
  //     smi_value = convert int32 to smi
  //     Store array[index] = smi_value
  //   }
  //
  Node* map = __ LoadField(AccessBuilder::ForMap(), array);
  Node* kind;
  {
    Node* bit_field2 = __ LoadField(AccessBuilder::ForMapBitField2(), map);
    Node* mask = __ Int32Constant(Map::Bits2::ElementsKindBits::kMask);
    Node* andit = __ Word32And(bit_field2, mask);
    Node* shift = __ Int32Constant(Map::Bits2::ElementsKindBits::kShift);
    kind = __ Word32Shr(andit, shift);
  }

  Node* elements = __ LoadField(AccessBuilder::ForJSObjectElements(), array);
  auto if_kind_is_double = __ MakeLabel();
  auto done = __ MakeLabel();
  __ GotoIf(IsElementsKindGreaterThan(kind, HOLEY_ELEMENTS),
            &if_kind_is_double);
  {
    // Our ElementsKind is HOLEY_SMI_ELEMENTS or HOLEY_ELEMENTS.
    // In this case, we know our value is a signed small, and we can optimize
    // the ElementAccess information.
    ElementAccess access = AccessBuilder::ForFixedArrayElement();
    access.type = Type::SignedSmall();
    access.machine_type = MachineType::TaggedSigned();
    access.write_barrier_kind = kNoWriteBarrier;
    Node* smi_value = ChangeInt32ToSmi(value);
    __ StoreElement(access, elements, index, smi_value);
    __ Goto(&done);
  }
  __ Bind(&if_kind_is_double);
  {
    // Our ElementsKind is HOLEY_DOUBLE_ELEMENTS.
    Node* float_value = __ ChangeInt32ToFloat64(value);
    __ StoreElement(AccessBuilder::ForFixedDoubleArrayElement(), elements,
                    index, float_value);
    __ Goto(&done);
  }

  __ Bind(&done);
}

void EffectControlLinearizer::LowerRuntimeAbort(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  AbortReason reason = AbortReasonOf(node->op());
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  Runtime::FunctionId id = Runtime::kAbort;
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      graph()->zone(), id, 1, properties, CallDescriptor::kNoFlags);
  __ Call(call_descriptor, __ CEntryStubConstant(1),
          __ SmiConstant(static_cast<int>(reason)),
          __ ExternalConstant(ExternalReference::Create(id)),
          __ Int32Constant(1), __ NoContextConstant());
}

template <typename... Args>
Node* EffectControlLinearizer::CallBuiltin(Builtin builtin,
                                           Operator::Properties properties,
                                           Args... args) {
  Callable const callable = Builtins::CallableFor(isolate(), builtin);
  auto call_descriptor = Linkage::GetStubCallDescriptor(
      graph()->zone(), callable.descriptor(),
      callable.descriptor().GetStackParameterCount(), CallDescriptor::kNoFlags,
      properties);
  return __ Call(call_descriptor, __ HeapConstant(callable.code()), args...,
                 __ NoContextConstant());
}

void EffectControlLinearizer::LowerAssertType(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK_EQ(node->opcode(), IrOpcode::kAssertType);
  Type type = OpParameter<Type>(node->op());
  CHECK(type.CanBeAsserted());
  Node* const input = node->InputAt(0);
  Node* allocated_type;
  {
    DCHECK(isolate()->CurrentLocalHeap()->is_main_thread());
    base::Optional<UnparkedScope> unparked_scope;
    if (isolate()->CurrentLocalHeap()->IsParked()) {
      unparked_scope.emplace(isolate()->main_thread_local_isolate());
    }
    allocated_type = __ HeapConstant(type.AllocateOnHeap(factory()));
  }
  CallBuiltin(Builtin::kCheckTurbofanType, node->op()->properties(), input,
              allocated_type, __ SmiConstant(node->id()));
}

Node* EffectControlLinearizer::LowerDoubleArrayMinMax(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  DCHECK(node->opcode() == IrOpcode::kDoubleArrayMin ||
         node->opcode() == IrOpcode::kDoubleArrayMax);

  bool is_max = node->opcode() == IrOpcode::kDoubleArrayMax;
  Node* arguments_list = node->InputAt(0);

  // Iterate the elements and find the result.
  Node* empty_value = is_max ? __ Float64Constant(-V8_INFINITY)
                             : __ Float64Constant(V8_INFINITY);
  Node* array_length = __ LoadField(
      AccessBuilder::ForJSArrayLength(ElementsKind::PACKED_DOUBLE_ELEMENTS),
      arguments_list);
  array_length = ChangeSmiToIntPtr(array_length);
  Node* elements =
      __ LoadField(AccessBuilder::ForJSObjectElements(), arguments_list);

  auto loop = __ MakeLoopLabel(MachineType::PointerRepresentation(),
                               MachineRepresentation::kFloat64);
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  __ Goto(&loop, __ IntPtrConstant(0), empty_value);
  __ Bind(&loop);
  {
    Node* index = loop.PhiAt(0);
    Node* accumulator = loop.PhiAt(1);

    Node* check = __ UintLessThan(index, array_length);
    __ GotoIfNot(check, &done, accumulator);

    Node* element = __ LoadElement(AccessBuilder::ForFixedDoubleArrayElement(),
                                   elements, index);
    __ Goto(&loop, __ IntAdd(index, __ IntPtrConstant(1)),
            is_max ? __ Float64Max(accumulator, element)
                   : __ Float64Min(accumulator, element));
  }

  __ Bind(&done);
  return ChangeFloat64ToTagged(done.PhiAt(0),
                               CheckForMinusZeroMode::kCheckForMinusZero);
}

Node* EffectControlLinearizer::LowerConvertReceiver(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  ConvertReceiverMode const mode = ConvertReceiverModeOf(node->op());
  Node* value = node->InputAt(0);
  Node* native_context = node->InputAt(1);
  Node* global_proxy = node->InputAt(2);

  switch (mode) {
    case ConvertReceiverMode::kNullOrUndefined: {
      return global_proxy;
    }
    case ConvertReceiverMode::kNotNullOrUndefined: {
      auto convert_to_object = __ MakeDeferredLabel();
      auto done_convert = __ MakeLabel(MachineRepresentation::kTagged);

      // Check if {value} is already a JSReceiver.
      __ GotoIf(ObjectIsSmi(value), &convert_to_object);
      __ GotoIfNot(JSAnyIsNotPrimitiveHeapObject(value), &convert_to_object);
      __ Goto(&done_convert, value);

      // Wrap the primitive {value} into a JSPrimitiveWrapper.
      __ Bind(&convert_to_object);
      Operator::Properties properties = Operator::kEliminatable;
      Callable callable = Builtins::CallableFor(isolate(), Builtin::kToObject);
      CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          graph()->zone(), callable.descriptor(),
          callable.descriptor().GetStackParameterCount(), flags, properties);
      Node* result = __ Call(call_descriptor, __ HeapConstant(callable.code()),
                             value, native_context);
      __ Goto(&done_convert, result);

      __ Bind(&done_convert);
      return done_convert.PhiAt(0);
    }
    case ConvertReceiverMode::kAny: {
      auto convert_to_object = __ MakeDeferredLabel();
      auto convert_global_proxy = __ MakeDeferredLabel();
      auto done_convert = __ MakeLabel(MachineRepresentation::kTagged);

      // Check if {value} is already a JSReceiver, or null/undefined.
      __ GotoIf(ObjectIsSmi(value), &convert_to_object);
      __ GotoIfNot(JSAnyIsNotPrimitiveHeapObject(value), &convert_to_object);
      __ Goto(&done_convert, value);

      // Wrap the primitive {value} into a JSPrimitiveWrapper.
      __ Bind(&convert_to_object);
      __ GotoIf(__ TaggedEqual(value, __ UndefinedConstant()),
                &convert_global_proxy);
      __ GotoIf(__ TaggedEqual(value, __ NullConstant()),
                &convert_global_proxy);
      Operator::Properties properties = Operator::kEliminatable;
      Callable callable = Builtins::CallableFor(isolate(), Builtin::kToObject);
      CallDescriptor::Flags flags = CallDescriptor::kNoFlags;
      auto call_descriptor = Linkage::GetStubCallDescriptor(
          graph()->zone(), callable.descriptor(),
          callable.descriptor().GetStackParameterCount(), flags, properties);
      Node* result = __ Call(call_descriptor, __ HeapConstant(callable.code()),
                             value, native_context);
      __ Goto(&done_convert, result);

      // Replace the {value} with the {global_proxy}.
      __ Bind(&convert_global_proxy);
      __ Goto(&done_convert, global_proxy);

      __ Bind(&done_convert);
      return done_convert.PhiAt(0);
    }
  }

  UNREACHABLE();
}

Maybe<Node*> EffectControlLinearizer::LowerFloat64RoundUp(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  // Nothing to be done if a fast hardware instruction is available.
  if (machine()->Float64RoundUp().IsSupported()) {
    return Nothing<Node*>();
  }

  Node* const input = node->InputAt(0);

  // General case for ceil.
  //
  //   if 0.0 < input then
  //     if 2^52 <= input then
  //       input
  //     else
  //       let temp1 = (2^52 + input) - 2^52 in
  //       if temp1 < input then
  //         temp1 + 1
  //       else
  //         temp1
  //   else
  //     if input == 0 then
  //       input
  //     else
  //       if input <= -2^52 then
  //         input
  //       else
  //         let temp1 = -0 - input in
  //         let temp2 = (2^52 + temp1) - 2^52 in
  //         let temp3 = (if temp1 < temp2 then temp2 - 1 else temp2) in
  //         -0 - temp3

  auto if_not_positive = __ MakeDeferredLabel();
  auto if_greater_than_two_52 = __ MakeDeferredLabel();
  auto if_less_than_minus_two_52 = __ MakeDeferredLabel();
  auto if_zero = __ MakeDeferredLabel();
  auto done_temp3 = __ MakeLabel(MachineRepresentation::kFloat64);
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* const zero = __ Float64Constant(0.0);
  Node* const two_52 = __ Float64Constant(4503599627370496.0E0);
  Node* const one = __ Float64Constant(1.0);

  Node* check0 = __ Float64LessThan(zero, input);
  __ GotoIfNot(check0, &if_not_positive);
  {
    Node* check1 = __ Float64LessThanOrEqual(two_52, input);
    __ GotoIf(check1, &if_greater_than_two_52);
    {
      Node* temp1 = __ Float64Sub(__ Float64Add(two_52, input), two_52);
      __ GotoIfNot(__ Float64LessThan(temp1, input), &done, temp1);
      __ Goto(&done, __ Float64Add(temp1, one));
    }

    __ Bind(&if_greater_than_two_52);
    __ Goto(&done, input);
  }

  __ Bind(&if_not_positive);
  {
    Node* check1 = __ Float64Equal(input, zero);
    __ GotoIf(check1, &if_zero);

    Node* const minus_two_52 = __ Float64Constant(-4503599627370496.0E0);
    Node* check2 = __ Float64LessThanOrEqual(input, minus_two_52);
    __ GotoIf(check2, &if_less_than_minus_two_52);

    {
      Node* const minus_zero = __ Float64Constant(-0.0);
      Node* temp1 = __ Float64Sub(minus_zero, input);
      Node* temp2 = __ Float64Sub(__ Float64Add(two_52, temp1), two_52);
      Node* check3 = __ Float64LessThan(temp1, temp2);
      __ GotoIfNot(check3, &done_temp3, temp2);
      __ Goto(&done_temp3, __ Float64Sub(temp2, one));

      __ Bind(&done_temp3);
      Node* temp3 = done_temp3.PhiAt(0);
      __ Goto(&done, __ Float64Sub(minus_zero, temp3));
    }
    __ Bind(&if_less_than_minus_two_52);
    __ Goto(&done, input);

    __ Bind(&if_zero);
    __ Goto(&done, input);
  }
  __ Bind(&done);
  return Just(done.PhiAt(0));
}

Node* EffectControlLinearizer::BuildFloat64RoundDown(Node* value) {
  if (machine()->Float64RoundDown().IsSupported()) {
    return __ Float64RoundDown(value);
  }

  Node* const input = value;

  // General case for floor.
  //
  //   if 0.0 < input then
  //     if 2^52 <= input then
  //       input
  //     else
  //       let temp1 = (2^52 + input) - 2^52 in
  //       if input < temp1 then
  //         temp1 - 1
  //       else
  //         temp1
  //   else
  //     if input == 0 then
  //       input
  //     else
  //       if input <= -2^52 then
  //         input
  //       else
  //         let temp1 = -0 - input in
  //         let temp2 = (2^52 + temp1) - 2^52 in
  //         if temp2 < temp1 then
  //           -1 - temp2
  //         else
  //           -0 - temp2

  auto if_not_positive = __ MakeDeferredLabel();
  auto if_greater_than_two_52 = __ MakeDeferredLabel();
  auto if_less_than_minus_two_52 = __ MakeDeferredLabel();
  auto if_temp2_lt_temp1 = __ MakeLabel();
  auto if_zero = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* const zero = __ Float64Constant(0.0);
  Node* const two_52 = __ Float64Constant(4503599627370496.0E0);

  Node* check0 = __ Float64LessThan(zero, input);
  __ GotoIfNot(check0, &if_not_positive);
  {
    Node* check1 = __ Float64LessThanOrEqual(two_52, input);
    __ GotoIf(check1, &if_greater_than_two_52);
    {
      Node* const one = __ Float64Constant(1.0);
      Node* temp1 = __ Float64Sub(__ Float64Add(two_52, input), two_52);
      __ GotoIfNot(__ Float64LessThan(input, temp1), &done, temp1);
      __ Goto(&done, __ Float64Sub(temp1, one));
    }

    __ Bind(&if_greater_than_two_52);
    __ Goto(&done, input);
  }

  __ Bind(&if_not_positive);
  {
    Node* check1 = __ Float64Equal(input, zero);
    __ GotoIf(check1, &if_zero);

    Node* const minus_two_52 = __ Float64Constant(-4503599627370496.0E0);
    Node* check2 = __ Float64LessThanOrEqual(input, minus_two_52);
    __ GotoIf(check2, &if_less_than_minus_two_52);

    {
      Node* const minus_zero = __ Float64Constant(-0.0);
      Node* temp1 = __ Float64Sub(minus_zero, input);
      Node* temp2 = __ Float64Sub(__ Float64Add(two_52, temp1), two_52);
      Node* check3 = __ Float64LessThan(temp2, temp1);
      __ GotoIf(check3, &if_temp2_lt_temp1);
      __ Goto(&done, __ Float64Sub(minus_zero, temp2));

      __ Bind(&if_temp2_lt_temp1);
      __ Goto(&done, __ Float64Sub(__ Float64Constant(-1.0), temp2));
    }
    __ Bind(&if_less_than_minus_two_52);
    __ Goto(&done, input);

    __ Bind(&if_zero);
    __ Goto(&done, input);
  }
  __ Bind(&done);
  return done.PhiAt(0);
}

Maybe<Node*> EffectControlLinearizer::LowerFloat64RoundDown(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  // Nothing to be done if a fast hardware instruction is available.
  if (machine()->Float64RoundDown().IsSupported()) {
    return Nothing<Node*>();
  }

  Node* const input = node->InputAt(0);
  return Just(BuildFloat64RoundDown(input));
}

Maybe<Node*> EffectControlLinearizer::LowerFloat64RoundTiesEven(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  // Nothing to be done if a fast hardware instruction is available.
  if (machine()->Float64RoundTiesEven().IsSupported()) {
    return Nothing<Node*>();
  }

  Node* const input = node->InputAt(0);

  // Generate case for round ties to even:
  //
  //   let value = floor(input) in
  //   let temp1 = input - value in
  //   if temp1 < 0.5 then
  //     value
  //   else if 0.5 < temp1 then
  //     value + 1.0
  //   else
  //     let temp2 = value % 2.0 in
  //     if temp2 == 0.0 then
  //       value
  //     else
  //       value + 1.0

  auto if_is_half = __ MakeLabel();
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* value = BuildFloat64RoundDown(input);
  Node* temp1 = __ Float64Sub(input, value);

  Node* const half = __ Float64Constant(0.5);
  Node* check0 = __ Float64LessThan(temp1, half);
  __ GotoIf(check0, &done, value);

  Node* const one = __ Float64Constant(1.0);
  Node* check1 = __ Float64LessThan(half, temp1);
  __ GotoIfNot(check1, &if_is_half);
  __ Goto(&done, __ Float64Add(value, one));

  __ Bind(&if_is_half);
  Node* temp2 = __ Float64Mod(value, __ Float64Constant(2.0));
  Node* check2 = __ Float64Equal(temp2, __ Float64Constant(0.0));
  __ GotoIf(check2, &done, value);
  __ Goto(&done, __ Float64Add(value, one));

  __ Bind(&done);
  return Just(done.PhiAt(0));
}

Node* EffectControlLinearizer::BuildFloat64RoundTruncate(Node* input) {
  if (machine()->Float64RoundTruncate().IsSupported()) {
    return __ Float64RoundTruncate(input);
  }
  // General case for trunc.
  //
  //   if 0.0 < input then
  //     if 2^52 <= input then
  //       input
  //     else
  //       let temp1 = (2^52 + input) - 2^52 in
  //       if input < temp1 then
  //         temp1 - 1
  //       else
  //         temp1
  //   else
  //     if input == 0 then
  //       input
  //     else
  //       if input <= -2^52 then
  //         input
  //       else
  //         let temp1 = -0 - input in
  //         let temp2 = (2^52 + temp1) - 2^52 in
  //         let temp3 = (if temp1 < temp2 then temp2 - 1 else temp2) in
  //         -0 - temp3
  //
  // Note: We do not use the Diamond helper class here, because it really hurts
  // readability with nested diamonds.

  auto if_not_positive = __ MakeDeferredLabel();
  auto if_greater_than_two_52 = __ MakeDeferredLabel();
  auto if_less_than_minus_two_52 = __ MakeDeferredLabel();
  auto if_zero = __ MakeDeferredLabel();
  auto done_temp3 = __ MakeLabel(MachineRepresentation::kFloat64);
  auto done = __ MakeLabel(MachineRepresentation::kFloat64);

  Node* const zero = __ Float64Constant(0.0);
  Node* const two_52 = __ Float64Constant(4503599627370496.0E0);
  Node* const one = __ Float64Constant(1.0);

  Node* check0 = __ Float64LessThan(zero, input);
  __ GotoIfNot(check0, &if_not_positive);
  {
    Node* check1 = __ Float64LessThanOrEqual(two_52, input);
    __ GotoIf(check1, &if_greater_than_two_52);
    {
      Node* temp1 = __ Float64Sub(__ Float64Add(two_52, input), two_52);
      __ GotoIfNot(__ Float64LessThan(input, temp1), &done, temp1);
      __ Goto(&done, __ Float64Sub(temp1, one));
    }

    __ Bind(&if_greater_than_two_52);
    __ Goto(&done, input);
  }

  __ Bind(&if_not_positive);
  {
    Node* check1 = __ Float64Equal(input, zero);
    __ GotoIf(check1, &if_zero);

    Node* const minus_two_52 = __ Float64Constant(-4503599627370496.0E0);
    Node* check2 = __ Float64LessThanOrEqual(input, minus_two_52);
    __ GotoIf(check2, &if_less_than_minus_two_52);

    {
      Node* const minus_zero = __ Float64Constant(-0.0);
      Node* temp1 = __ Float64Sub(minus_zero, input);
      Node* temp2 = __ Float64Sub(__ Float64Add(two_52, temp1), two_52);
      Node* check3 = __ Float64LessThan(temp1, temp2);
      __ GotoIfNot(check3, &done_temp3, temp2);
      __ Goto(&done_temp3, __ Float64Sub(temp2, one));

      __ Bind(&done_temp3);
      Node* temp3 = done_temp3.PhiAt(0);
      __ Goto(&done, __ Float64Sub(minus_zero, temp3));
    }
    __ Bind(&if_less_than_minus_two_52);
    __ Goto(&done, input);

    __ Bind(&if_zero);
    __ Goto(&done, input);
  }
  __ Bind(&done);
  return done.PhiAt(0);
}

Maybe<Node*> EffectControlLinearizer::LowerFloat64RoundTruncate(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  // Nothing to be done if a fast hardware instruction is available.
  if (machine()->Float64RoundTruncate().IsSupported()) {
    return Nothing<Node*>();
  }

  Node* const input = node->InputAt(0);
  return Just(BuildFloat64RoundTruncate(input));
}

Node* EffectControlLinearizer::LowerFindOrderedHashMapEntry(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* table = NodeProperties::GetValueInput(node, 0);
  Node* key = NodeProperties::GetValueInput(node, 1);

  {
    Callable const callable =
        Builtins::CallableFor(isolate(), Builtin::kFindOrderedHashMapEntry);
    Operator::Properties const properties = node->op()->properties();
    CallDescriptor::Flags const flags = CallDescriptor::kNoFlags;
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(), flags, properties);
    return __ Call(call_descriptor, __ HeapConstant(callable.code()), table,
                   key, __ NoContextConstant());
  }
}

Node* EffectControlLinearizer::LowerFindOrderedHashSetEntry(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* table = NodeProperties::GetValueInput(node, 0);
  Node* key = NodeProperties::GetValueInput(node, 1);

  {
    Callable const callable =
        Builtins::CallableFor(isolate(), Builtin::kFindOrderedHashSetEntry);
    Operator::Properties const properties = node->op()->properties();
    CallDescriptor::Flags const flags = CallDescriptor::kNoFlags;
    auto call_descriptor = Linkage::GetStubCallDescriptor(
        graph()->zone(), callable.descriptor(),
        callable.descriptor().GetStackParameterCount(), flags, properties);
    return __ Call(call_descriptor, __ HeapConstant(callable.code()), table,
                   key, __ NoContextConstant());
  }
}

Node* EffectControlLinearizer::ComputeUnseededHash(Node* value) {
  // See v8::internal::ComputeUnseededHash()
  value = __ Int32Add(__ Word32Xor(value, __ Int32Constant(0xFFFFFFFF)),
                      __ Word32Shl(value, __ Int32Constant(15)));
  value = __ Word32Xor(value, __ Word32Shr(value, __ Int32Constant(12)));
  value = __ Int32Add(value, __ Word32Shl(value, __ Int32Constant(2)));
  value = __ Word32Xor(value, __ Word32Shr(value, __ Int32Constant(4)));
  value = __ Int32Mul(value, __ Int32Constant(2057));
  value = __ Word32Xor(value, __ Word32Shr(value, __ Int32Constant(16)));
  value = __ Word32And(value, __ Int32Constant(0x3FFFFFFF));
  return value;
}

Node* EffectControlLinearizer::LowerFindOrderedHashMapEntryForInt32Key(
    Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Node* table = NodeProperties::GetValueInput(node, 0);
  Node* key = NodeProperties::GetValueInput(node, 1);

  // Compute the integer hash code.
  Node* hash = ChangeUint32ToUintPtr(ComputeUnseededHash(key));

  Node* number_of_buckets = ChangeSmiToIntPtr(__ LoadField(
      AccessBuilder::ForOrderedHashMapOrSetNumberOfBuckets(), table));
  hash = __ WordAnd(hash, __ IntSub(number_of_buckets, __ IntPtrConstant(1)));
  Node* first_entry = ChangeSmiToIntPtr(__ Load(
      MachineType::TaggedSigned(), table,
      __ IntAdd(__ WordShl(hash, __ IntPtrConstant(kTaggedSizeLog2)),
                __ IntPtrConstant(OrderedHashMap::HashTableStartOffset() -
                                  kHeapObjectTag))));

  auto loop = __ MakeLoopLabel(MachineType::PointerRepresentation());
  auto done = __ MakeLabel(MachineType::PointerRepresentation());
  __ Goto(&loop, first_entry);
  __ Bind(&loop);
  {
    Node* entry = loop.PhiAt(0);
    Node* check =
        __ IntPtrEqual(entry, __ IntPtrConstant(OrderedHashMap::kNotFound));
    __ GotoIf(check, &done, entry);
    entry = __ IntAdd(
        __ IntMul(entry, __ IntPtrConstant(OrderedHashMap::kEntrySize)),
        number_of_buckets);

    Node* candidate_key = __ Load(
        MachineType::AnyTagged(), table,
        __ IntAdd(__ WordShl(entry, __ IntPtrConstant(kTaggedSizeLog2)),
                  __ IntPtrConstant(OrderedHashMap::HashTableStartOffset() -
                                    kHeapObjectTag)));

    auto if_match = __ MakeLabel();
    auto if_notmatch = __ MakeLabel();
    auto if_notsmi = __ MakeDeferredLabel();
    __ GotoIfNot(ObjectIsSmi(candidate_key), &if_notsmi);
    __ Branch(__ Word32Equal(ChangeSmiToInt32(candidate_key), key), &if_match,
              &if_notmatch);

    __ Bind(&if_notsmi);
    __ GotoIfNot(
        __ TaggedEqual(__ LoadField(AccessBuilder::ForMap(), candidate_key),
                       __ HeapNumberMapConstant()),
        &if_notmatch);
    __ Branch(__ Float64Equal(__ LoadField(AccessBuilder::ForHeapNumberValue(),
                                           candidate_key),
                              __ ChangeInt32ToFloat64(key)),
              &if_match, &if_notmatch);

    __ Bind(&if_match);
    __ Goto(&done, entry);

    __ Bind(&if_notmatch);
    {
      Node* next_entry = ChangeSmiToIntPtr(__ Load(
          MachineType::TaggedSigned(), table,
          __ IntAdd(
              __ WordShl(entry, __ IntPtrConstant(kTaggedSizeLog2)),
              __ IntPtrConstant(OrderedHashMap::HashTableStartOffset() +
                                OrderedHashMap::kChainOffset * kTaggedSize -
                                kHeapObjectTag))));
      __ Goto(&loop, next_entry);
    }
  }

  __ Bind(&done);
  return done.PhiAt(0);
}

Node* EffectControlLinearizer::LowerDateNow(Node* node) {
  DCHECK(!v8_flags.turboshaft);
  Operator::Properties properties = Operator::kNoDeopt | Operator::kNoThrow;
  Runtime::FunctionId id = Runtime::kDateCurrentTime;
  auto call_descriptor = Linkage::GetRuntimeCallDescriptor(
      graph()->zone(), id, 0, properties, CallDescriptor::kNoFlags);
  return __ Call(call_descriptor, __ CEntryStubConstant(1),
                 __ ExternalConstant(ExternalReference::Create(id)),
                 __ Int32Constant(0), __ NoContextConstant());
}

Node* EffectControlLinearizer::TruncateWordToInt32(Node* value) {
  if (machine()->Is64()) {
    return __ TruncateInt64ToInt32(value);
  }
  return value;
}

// Pass {bitfield} = {digit} = nullptr to construct the canoncial 0n BigInt.
Node* EffectControlLinearizer::BuildAllocateBigInt(Node* bitfield,
                                                   Node* digit) {
  DCHECK(machine()->Is64());
  DCHECK_EQ(bitfield == nullptr, digit == nullptr);
  static constexpr auto zero_bitfield =
      BigInt::SignBits::update(BigInt::LengthBits::encode(0), false);

  Node* map = __ HeapConstant(factory()->bigint_map());

  Node* result = __ Allocate(AllocationType::kYoung,
                             __ IntPtrConstant(BigInt::SizeFor(digit ? 1 : 0)));
  __ StoreField(AccessBuilder::ForMap(), result, map);
  __ StoreField(AccessBuilder::ForBigIntBitfield(), result,
                bitfield ? bitfield : __ Int32Constant(zero_bitfield));

#ifdef BIGINT_NEEDS_PADDING
  // BigInts have no padding on 64 bit architectures with pointer compression.
  __ StoreField(AccessBuilder::ForBigIntOptionalPadding(), result,
                __ IntPtrConstant(0));
#endif
  if (digit) {
    __ StoreField(AccessBuilder::ForBigIntLeastSignificantDigit64(), result,
                  digit);
  }
  return result;
}

Node* EffectControlLinearizer::BuildAllocateJSExternalObject(Node* pointer) {
  auto if_null = __ MakeDeferredLabel();
  auto done = __ MakeLabel(MachineRepresentation::kTagged);

  // Check if the pointer is a null pointer
  __ GotoIf(__ WordEqual(pointer, __ IntPtrConstant(0)), &if_null);

  {
    Node* external =
        __ Allocate(AllocationType::kYoung,
                    __ IntPtrConstant(JSExternalObject::kHeaderSize));
    __ StoreField(AccessBuilder::ForMap(), external,
                  __ ExternalObjectMapConstant());
    Node* empty_fixed_array = __ HeapConstant(factory()->empty_fixed_array());
    __ StoreField(AccessBuilder::ForJSObjectPropertiesOrHash(), external,
                  empty_fixed_array);
    __ StoreField(AccessBuilder::ForJSObjectElements(), external,
                  empty_fixed_array);

#ifdef V8_ENABLE_SANDBOX
    Node* const isolate_ptr =
        __ ExternalConstant(ExternalReference::isolate_address(isolate()));
    MachineSignature::Builder builder(graph()->zone(), 1, 2);
    builder.AddReturn(MachineType::Uint32());
    builder.AddParam(MachineType::Pointer());
    builder.AddParam(MachineType::Pointer());
    Node* allocate_and_initialize_external_pointer_table_entry =
        __ ExternalConstant(
            ExternalReference::
                allocate_and_initialize_external_pointer_table_entry());
    auto call_descriptor =
        Linkage::GetSimplifiedCDescriptor(graph()->zone(), builder.Build());
    Node* handle = __ Call(common()->Call(call_descriptor),
                           allocate_and_initialize_external_pointer_table_entry,
                           isolate_ptr, pointer);

    __ StoreField(AccessBuilder::ForJSExternalObjectPointerHandle(), external,
                  handle);
#else
    __ StoreField(AccessBuilder::ForJSExternalObjectValue(), external, pointer);
#endif  // V8_ENABLE_SANDBOX
    __ Goto(&done, external);
  }

  // Pointer is null, convert to a null
  __ Bind(&if_null);
  { __ Goto(&done, __ NullConstant()); }

  __ Bind(&done);
  return done.PhiAt(0);
}

#if V8_ENABLE_WEBASSEMBLY
void EffectControlLinearizer::LowerWasmTrap(Node* node, Node* frame_state) {
  // Store frame_state from CheckPoint.
  node->InsertInput(graph()->zone(), 1, frame_state);
  bool has_frame_state = true;
  const Operator* op =
      node->opcode() == IrOpcode::kTrapIf
          ? common()->TrapIf(TrapIdOf(node->op()), has_frame_state)
          : common()->TrapUnless(TrapIdOf(node->op()), has_frame_state);
  NodeProperties::ChangeOp(node, op);
}
#endif

#undef __

void LinearizeEffectControl(JSGraph* graph, Schedule* schedule, Zone* temp_zone,
                            SourcePositionTable* source_positions,
                            NodeOriginTable* node_origins,
                            JSHeapBroker* broker) {
  StringBuilderOptimizer string_builder_optimizer(graph, schedule, temp_zone,
                                                  broker);
  if (v8_flags.turbo_string_builder && !v8_flags.turboshaft) {
    string_builder_optimizer.Run();
  }
  JSGraphAssembler graph_assembler_(broker, graph, temp_zone,
                                    BranchSemantics::kMachine);
  EffectControlLinearizer linearizer(graph, schedule, &graph_assembler_,
                                     temp_zone, source_positions, node_origins,
                                     MaintainSchedule::kDiscard, broker,
                                     &string_builder_optimizer);
  linearizer.Run();
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
