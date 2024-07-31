// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/maglev-graph-building-phase.h"

#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/access-builder.h"
#include "src/compiler/globals.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/machine-optimization-reducer.h"
#include "src/compiler/turboshaft/maglev-early-lowering-reducer-inl.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/turboshaft/required-optimization-reducer.h"
#include "src/compiler/turboshaft/value-numbering-reducer.h"
#include "src/compiler/turboshaft/variable-reducer.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/deoptimizer/deoptimize-reason.h"
#include "src/handles/global-handles-inl.h"
#include "src/handles/handles.h"
#include "src/interpreter/bytecode-register.h"
#include "src/maglev/maglev-compilation-info.h"
#include "src/maglev/maglev-graph-builder.h"
#include "src/maglev/maglev-graph-labeller.h"
#include "src/maglev/maglev-graph-processor.h"
#include "src/maglev/maglev-ir.h"
#include "src/objects/heap-object.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace {

MachineType MachineTypeFor(maglev::ValueRepresentation repr) {
  switch (repr) {
    case maglev::ValueRepresentation::kTagged:
      return MachineType::AnyTagged();
    case maglev::ValueRepresentation::kInt32:
      return MachineType::Int32();
    case maglev::ValueRepresentation::kUint32:
      return MachineType::Uint32();
    case maglev::ValueRepresentation::kIntPtr:
      return MachineType::IntPtr();
    case maglev::ValueRepresentation::kFloat64:
      return MachineType::Float64();
    case maglev::ValueRepresentation::kHoleyFloat64:
      // TODO(dmercadier): is this correct?
      return MachineType::Float64();
  }
}

}  // namespace

class GraphBuilder {
 public:
  using AssemblerT =
      TSAssembler<MaglevEarlyLoweringReducer, MachineOptimizationReducer,
                  VariableReducer, RequiredOptimizationReducer,
                  ValueNumberingReducer>;

  GraphBuilder(Graph& graph, Zone* temp_zone,
               maglev::MaglevCompilationUnit* maglev_compilation_unit)
      : temp_zone_(temp_zone),
        assembler_(graph, graph, temp_zone),
        maglev_compilation_unit_(maglev_compilation_unit),
        node_mapping_(temp_zone),
        block_mapping_(temp_zone),
        regs_to_vars_(temp_zone) {}

  void PreProcessGraph(maglev::Graph* graph) {
    for (maglev::BasicBlock* block : *graph) {
      block_mapping_[block] =
          block->is_loop() ? __ NewLoopHeader() : __ NewBlock();
    }
    // Constants are not in a block in Maglev but are in Turboshaft. We bind a
    // block now, so that Constants can then be emitted.
    __ Bind(__ NewBlock());
  }

  void PostProcessGraph(maglev::Graph* graph) {}

  void PreProcessBasicBlock(maglev::BasicBlock* block) {
    if (__ current_block() != nullptr) {
      // The first block for Constants doesn't end with a Jump, so we add one
      // now.
      __ Goto(Map(block));
    }
    __ Bind(Map(block));

    // Because of edge splitting in Maglev, the order of predecessors in the
    // Turboshaft graph is not always the same as in the Maglev graph, which
    // means that Phi inputs will have to be reordered. We thus compute in
    // {predecessor_permutation_} the Turboshaft predecessors position of each
    // Maglev predecessor, and we'll use this later when emitting Phis to
    // reorder their inputs.
    predecessor_permutation_.clear();
    if (block->has_phi()) {
      for (int i = 0; i < block->predecessor_count(); ++i) {
        Block* pred = Map(block->predecessor_at(i));
        int pred_index = __ current_block() -> GetPredecessorIndex(pred);
        DCHECK_IMPLIES(pred_index == -1,
                       block->is_loop() && i == block->predecessor_count() - 1);
        predecessor_permutation_.push_back(pred_index);
      }
    }
  }

  maglev::ProcessResult Process(maglev::Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ HeapConstant(node->object().object()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::RootConstant* node,
                                const maglev::ProcessingState& state) {
    SetMap(
        node,
        __ HeapConstant(
            MakeRef(broker_, node->DoReify(isolate_)).AsHeapObject().object()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Word32Constant(node->value()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Constant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64Constant(
                     base::bit_cast<double>(node->value().get_bits())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::SmiConstant* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ SmiConstant(node->value()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::InitialValue* node,
                                const maglev::ProcessingState& state) {
#ifdef DEBUG
    char* debug_name = strdup(node->source().ToString().c_str());
#else
    char* debug_name = nullptr;
#endif
    interpreter::Register source = node->source();
    int index = source.ToParameterIndex();
    if (source.is_function_closure()) {
      index = Linkage::kJSCallClosureParamIndex;
    } else if (source.is_current_context()) {
      index = Linkage::GetJSCallContextParamIndex(
          maglev_compilation_unit_->parameter_count());
    }
    SetMap(node,
           __ Parameter(index, RegisterRepresentation::Tagged(), debug_name));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::FunctionEntryStackCheck* node,
                                const maglev::ProcessingState& state) {
    __ StackCheck(StackCheckOp::CheckOrigin::kFromJS,
                  StackCheckOp::CheckKind::kFunctionHeaderCheck);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Phi* node,
                                const maglev::ProcessingState& state) {
    int input_count = node->input_count();
    RegisterRepresentation rep =
        RegisterRepresentationFor(node->value_representation());
    if (node->is_exception_phi()) {
      SetMap(node, __ GetVariable(regs_to_vars_[node->owner().index()]));
      return maglev::ProcessResult::kContinue;
    }
    if (__ current_block()->IsLoop()) {
      DCHECK_EQ(input_count, 2);
      SetMap(node, __ PendingLoopPhi(Map(node->input(0)), rep));
    } else {
      DCHECK(!predecessor_permutation_.empty());
      base::SmallVector<OpIndex, 16> inputs;
      for (int i = 0; i < input_count; ++i) {
        inputs.push_back(Map(node->input(predecessor_permutation_[i])));
      }
      SetMap(node, __ Phi(base::VectorOf(inputs), rep));
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CallKnownJSFunction* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    OpIndex frame_state = BuildFrameState(node->lazy_deopt_info());
    V<Object> callee = Map(node->closure());
    base::SmallVector<OpIndex, 16> arguments;
    arguments.push_back(Map(node->receiver()));
    for (int i = 0; i < node->num_args(); i++) {
      arguments.push_back(Map(node->arg(i)));
    }

    arguments.push_back(Map(node->new_target()));
    arguments.push_back(__ Word32Constant(JSParameterCount(node->num_args())));

    // Load the context from {callee}.
    OpIndex context =
        __ LoadField(callee, AccessBuilder::ForJSFunctionContext());
    arguments.push_back(context);

    const CallDescriptor* descriptor = Linkage::GetJSCallDescriptor(
        graph_zone(), false, 1 + node->num_args(),
        CallDescriptor::kNeedsFrameState | CallDescriptor::kCanUseRoots);
    SetMap(node, __ Call(callee, frame_state, base::VectorOf(arguments),
                         TSCallDescriptor::Create(descriptor, CanThrow::kYes,
                                                  graph_zone())));

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CallBuiltin* node,
                                const maglev::ProcessingState& state) {
    ThrowingScope throwing_scope(this, node);

    OpIndex frame_state = BuildFrameState(node->lazy_deopt_info());
    Callable callable = Builtins::CallableFor(
        isolate_->GetMainThreadIsolateUnsafe(), node->builtin());
    const CallInterfaceDescriptor& descriptor = callable.descriptor();
    CallDescriptor* call_descriptor = Linkage::GetStubCallDescriptor(
        graph_zone(), descriptor, descriptor.GetStackParameterCount(),
        CallDescriptor::kNeedsFrameState);
    V<Code> stub_code = __ HeapConstant(callable.code());
    base::SmallVector<OpIndex, 16> arguments;

    for (int i = 0; i < node->InputCountWithoutContext(); i++) {
      arguments.push_back(Map(node->input(i)));
    }

    if (node->has_feedback()) {
      arguments.push_back(__ TaggedIndexConstant(node->feedback().index()));
      arguments.push_back(__ HeapConstant(node->feedback().vector));
    }

    if (Builtins::CallInterfaceDescriptorFor(node->builtin())
            .HasContextParameter()) {
      arguments.push_back(Map(node->context_input()));
    }

    SetMap(node, __ Call(stub_code, frame_state, base::VectorOf(arguments),
                         TSCallDescriptor::Create(
                             call_descriptor, CanThrow::kYes, graph_zone())));

    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckMaps* node,
                                const maglev::ProcessingState& state) {
    Label<> done(this);
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    if (node->check_type() == maglev::CheckType::kCheckHeapObject) {
      OpIndex is_smi = __ IsSmi(Map(node->receiver_input()));
      if (AnyMapIsHeapNumber(node->maps())) {
        // Smis count as matching the HeapNumber map, so we're done.
        GOTO_IF(is_smi, done);
      } else {
        __ DeoptimizeIf(is_smi, frame_state, DeoptimizeReason::kWrongMap,
                        node->eager_deopt_info()->feedback_to_update());
      }
    }
    __ CheckMaps(Map(node->receiver_input()), frame_state, node->maps(),
                 CheckMapsFlag::kNone,
                 node->eager_deopt_info()->feedback_to_update());

    if (done.has_incoming_jump()) {
      GOTO(done);
      BIND(done);
    }
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckValue* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    __ DeoptimizeIfNot(__ TaggedEqual(Map(node->target_input()),
                                      __ HeapConstant(node->value().object())),
                       frame_state, DeoptimizeReason::kWrongValue,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckString* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    ObjectIsOp::InputAssumptions input_assumptions =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? ObjectIsOp::InputAssumptions::kNone
            : ObjectIsOp::InputAssumptions::kHeapObject;
    V<Word32> check = __ ObjectIs(Map(node->receiver_input()),
                                  ObjectIsOp::Kind::kString, input_assumptions);
    __ DeoptimizeIfNot(check, frame_state, DeoptimizeReason::kNotAString,
                       node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckInstanceType* node,
                                const maglev::ProcessingState& state) {
    __ CheckInstanceType(
        Map(node->receiver_input()), BuildFrameState(node->eager_deopt_info()),
        node->eager_deopt_info()->feedback_to_update(),
        node->first_instance_type(), node->last_instance_type(),
        node->check_type() != maglev::CheckType::kOmitHeapObjectCheck);

    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckDynamicValue* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    __ DeoptimizeIfNot(
        __ TaggedEqual(Map(node->first_input()), Map(node->second_input())),
        frame_state, DeoptimizeReason::kWrongValue,
        node->eager_deopt_info()->feedback_to_update());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckInt32Condition* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    bool negate_result = false;
    V<Word32> cmp = ConvertInt32Compare(node->left_input(), node->right_input(),
                                        node->condition(), &negate_result);
    if (negate_result) {
      __ DeoptimizeIf(cmp, frame_state, node->reason(),
                      node->eager_deopt_info()->feedback_to_update());
    } else {
      __ DeoptimizeIfNot(cmp, frame_state, node->reason(),
                         node->eager_deopt_info()->feedback_to_update());
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::AllocationBlock* node,
                                const maglev::ProcessingState& state) {
    if (!node->is_used()) return maglev::ProcessResult::kRemove;
    int size = 0;
    for (auto alloc : node->allocation_list()) {
      if (alloc->HasEscaped()) {
        alloc->set_offset(size);
        size += alloc->size();
      }
    }
    node->set_size(size);
    SetMap(node, __ FinishInitialization(
                     __ Allocate<HeapObject>(size, node->allocation_type())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::InlinedAllocation* node,
                                const maglev::ProcessingState& state) {
    if (!node->HasEscaped()) return maglev::ProcessResult::kRemove;
    V<HeapObject> alloc = Map(node->allocation_block());
    SetMap(node, __ BitcastWordPtrToHeapObject(__ WordPtrAdd(
                     __ BitcastHeapObjectToWordPtr(alloc), node->offset())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StringConcat* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ StringConcat(Map(node->lhs()), Map(node->rhs())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringEqual* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ StringEqual(Map(node->lhs()), Map(node->rhs())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringLength* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ StringLength(Map(node->object_input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StringAt* node,
                                const maglev::ProcessingState& state) {
    V<Word32> char_code =
        __ StringCharCodeAt(Map(node->string_input()),
                            __ ChangeUint32ToUintPtr(Map(node->index_input())));
    SetMap(node, __ ConvertCharCodeToString(char_code));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedInternalizedString* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    SetMap(node, __ CheckedInternalizedString(
                     Map(node->object_input()), frame_state,
                     node->check_type() == maglev::CheckType::kCheckHeapObject,
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LoadTaggedField* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadTaggedField(Map(node->object_input()), node->offset()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadDoubleField* node,
                                const maglev::ProcessingState& state) {
    V<HeapNumber> field = __ LoadTaggedField<HeapNumber>(
        Map(node->object_input()), node->offset());
    SetMap(node, __ LoadField(field, AccessBuilder::ForHeapNumberValue()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadFixedArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedDoubleArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadHoleyFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFixedDoubleArrayElement(
                     Map(node->elements_input()),
                     __ ChangeInt32ToIntPtr(Map(node->index_input()))));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::LoadHoleyFixedDoubleArrayElementCheckedNotHole* node,
      const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());
    V<Float64> result = __ LoadFixedDoubleArrayElement(
        Map(node->elements_input()),
        __ ChangeInt32ToIntPtr(Map(node->index_input())));
    __ DeoptimizeIf(__ Float64IsHole(result), frame_state,
                    DeoptimizeReason::kHole,
                    node->eager_deopt_info()->feedback_to_update());
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::StoreTaggedFieldNoWriteBarrier* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kNoWriteBarrier, node->offset());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreTaggedFieldWithWriteBarrier* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::AnyTagged(),
             WriteBarrierKind::kFullWriteBarrier, node->offset());
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreDoubleField* node,
                                const maglev::ProcessingState& state) {
    V<HeapNumber> field = __ LoadTaggedField<HeapNumber>(
        Map(node->object_input()), node->offset());
    __ StoreField(field, AccessBuilder::ForHeapNumberValue(),
                  Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::StoreFixedArrayElementNoWriteBarrier* node,
      const maglev::ProcessingState& state) {
    __ StoreFixedArrayElement(Map(node->elements_input()),
                              __ ChangeInt32ToIntPtr(Map(node->index_input())),
                              Map(node->value_input()),
                              WriteBarrierKind::kNoWriteBarrier);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(
      maglev::StoreFixedArrayElementWithWriteBarrier* node,
      const maglev::ProcessingState& state) {
    __ StoreFixedArrayElement(Map(node->elements_input()),
                              __ ChangeInt32ToIntPtr(Map(node->index_input())),
                              Map(node->value_input()),
                              WriteBarrierKind::kFullWriteBarrier);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreFixedDoubleArrayElement* node,
                                const maglev::ProcessingState& state) {
    __ StoreFixedDoubleArrayElement(
        Map(node->elements_input()),
        __ ChangeInt32ToIntPtr(Map(node->index_input())),
        Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreMap* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), __ HeapConstant(node->map().object()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::TaggedPointer(),
             WriteBarrierKind::kMapWriteBarrier, HeapObject::kMapOffset);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::StoreFloat64* node,
                                const maglev::ProcessingState& state) {
    __ Store(Map(node->object_input()), Map(node->value_input()),
             StoreOp::Kind::TaggedBase(), MemoryRepresentation::Float64(),
             WriteBarrierKind::kNoWriteBarrier, node->offset());
    return maglev::ProcessResult::kContinue;
  }

  // For-in specific operations.
  maglev::ProcessResult Process(maglev::LoadEnumCacheLength* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bitfield3 =
        __ LoadField<Word32>(V<i::Map>::Cast(Map(node->map_input())),
                             AccessBuilder::ForMapBitField3());
    V<Word32> length = __ Word32ShiftRightLogical(
        __ Word32BitwiseAnd(bitfield3, Map::Bits3::EnumLengthBits::kMask),
        Map::Bits3::EnumLengthBits::kShift);
    SetMap(node, length);
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckCacheIndicesNotCleared* node,
                                const maglev::ProcessingState& state) {
    // If the cache length is zero, we don't have any indices, so we know this
    // is ok even though the indices are the empty array.
    IF_NOT (__ Word32Equal(Map(node->length_input()), 0)) {
      // Otherwise, an empty array with non-zero required length is not valid.
      V<Word32> condition =
          RootEqual(node->indices_input(), RootIndex::kEmptyFixedArray);
      __ DeoptimizeIfNot(condition, BuildFrameState(node->eager_deopt_info()),
                         DeoptimizeReason::kWrongEnumIndices,
                         node->eager_deopt_info()->feedback_to_update());
    }
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::LoadTaggedFieldByFieldIndex* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ LoadFieldByIndex(Map(node->object_input()),
                                     Map(node->index_input())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Jump* node,
                                const maglev::ProcessingState& state) {
    __ Goto(Map(node->target()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::JumpLoop* node,
                                const maglev::ProcessingState& state) {
    __ Goto(Map(node->target()));
    FixLoopPhis(node->target());
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Int32Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bool_res = ConvertCompare<Word32>(
        node->left_input(), node->right_input(), node->operation());
    SetMap(node, ConvertWord32ToJSBool(bool_res));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> bool_res = ConvertCompare<Float64>(
        node->left_input(), node->right_input(), node->operation());
    SetMap(node, ConvertWord32ToJSBool(bool_res));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TaggedEqual* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(
                     __ TaggedEqual(Map(node->lhs()), Map(node->rhs()))));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::BranchIfToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions assumption =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject
            : TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject;
    V<Word32> condition = __ TruncateJSPrimitiveToUntagged(
        Map(node->condition_input()),
        TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kBit, assumption);
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfInt32Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = ConvertCompare<Word32>(
        node->left_input(), node->right_input(), node->operation());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfFloat64Compare* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = ConvertCompare<Float64>(
        node->left_input(), node->right_input(), node->operation());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfInt32ToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = Map(node->condition_input());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfFloat64ToBooleanTrue* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = __ Float64Equal(Map(node->condition_input()), 0.0);
    // Swapping if_true and if_false because we the real condition is "!= 0"
    // rather than "== 0" (but Turboshaft doesn't have Float64NotEqual).
    __ Branch(condition, Map(node->if_false()), Map(node->if_true()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfReferenceEqual* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        __ TaggedEqual(Map(node->left_input()), Map(node->right_input()));
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfRootConstant* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition =
        RootEqual(node->condition_input(), node->root_index());
    __ Branch(condition, Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::BranchIfUndefinedOrNull* node,
                                const maglev::ProcessingState& state) {
    __ GotoIf(RootEqual(node->condition_input(), RootIndex::kUndefinedValue),
              Map(node->if_true()));
    __ Branch(RootEqual(node->condition_input(), RootIndex::kNullValue),
              Map(node->if_true()), Map(node->if_false()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckedSmiUntag* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           __ CheckedSmiUntag(Map(node->input()),
                              BuildFrameState(node->eager_deopt_info()),
                              node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UnsafeSmiUntag* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ UntagSmi(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }

#define PROCESS_BINOP_WITH_OVERFLOW(MaglevName, TurboshaftName,                \
                                    minus_zero_mode)                           \
  maglev::ProcessResult Process(maglev::Int32##MaglevName##WithOverflow* node, \
                                const maglev::ProcessingState& state) {        \
    OpIndex frame_state = BuildFrameState(node->eager_deopt_info());           \
    SetMap(node,                                                               \
           __ Word32##TurboshaftName##DeoptOnOverflow(                         \
               Map(node->left_input()), Map(node->right_input()), frame_state, \
               node->eager_deopt_info()->feedback_to_update(),                 \
               CheckForMinusZeroMode::k##minus_zero_mode));                    \
    return maglev::ProcessResult::kContinue;                                   \
  }
  PROCESS_BINOP_WITH_OVERFLOW(Add, SignedAdd, DontCheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Subtract, SignedSub, DontCheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Multiply, SignedMul, CheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Divide, SignedDiv, CheckForMinusZero)
  PROCESS_BINOP_WITH_OVERFLOW(Modulus, SignedMod, CheckForMinusZero)
#undef PROCESS_BINOP_WITH_OVERFLOW
  maglev::ProcessResult Process(maglev::Int32IncrementWithOverflow* node,
                                const maglev::ProcessingState& state) {
    // Turboshaft doesn't have a dedicated Increment operation; we use a regular
    // addition instead.
    SetMap(node, __ Word32SignedAddDeoptOnOverflow(
                     Map(node->value_input()), 1,
                     BuildFrameState(node->eager_deopt_info()),
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32DecrementWithOverflow* node,
                                const maglev::ProcessingState& state) {
    // Turboshaft doesn't have a dedicated Decrement operation; we use a regular
    // addition instead.
    SetMap(node, __ Word32SignedSubDeoptOnOverflow(
                     Map(node->value_input()), 1,
                     BuildFrameState(node->eager_deopt_info()),
                     node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }

#define PROCESS_FLOAT64_BINOP(MaglevName, TurboshaftName)               \
  maglev::ProcessResult Process(maglev::Float64##MaglevName* node,      \
                                const maglev::ProcessingState& state) { \
    SetMap(node, __ Float64##TurboshaftName(Map(node->left_input()),    \
                                            Map(node->right_input()))); \
    return maglev::ProcessResult::kContinue;                            \
  }
  PROCESS_FLOAT64_BINOP(Add, Add)
  PROCESS_FLOAT64_BINOP(Subtract, Sub)
  PROCESS_FLOAT64_BINOP(Multiply, Mul)
  PROCESS_FLOAT64_BINOP(Divide, Div)
  PROCESS_FLOAT64_BINOP(Modulus, Mod)
  PROCESS_FLOAT64_BINOP(Exponentiate, Power)
#undef PROCESS_FLOAT64_BINOP

#define PROCESS_INT32_BITWISE_BINOP(Name)                               \
  maglev::ProcessResult Process(maglev::Int32Bitwise##Name* node,       \
                                const maglev::ProcessingState& state) { \
    SetMap(node, __ Word32Bitwise##Name(Map(node->left_input()),        \
                                        Map(node->right_input())));     \
    return maglev::ProcessResult::kContinue;                            \
  }
  PROCESS_INT32_BITWISE_BINOP(And)
  PROCESS_INT32_BITWISE_BINOP(Or)
  PROCESS_INT32_BITWISE_BINOP(Xor)
#undef PROCESS_INT32_BITWISE_BINOP

#define PROCESS_INT32_SHIFT(MaglevName, TurboshaftName)                 \
  maglev::ProcessResult Process(maglev::Int32##MaglevName* node,        \
                                const maglev::ProcessingState& state) { \
    SetMap(node, __ Word32##TurboshaftName(Map(node->left_input()),     \
                                           Map(node->right_input())));  \
    return maglev::ProcessResult::kContinue;                            \
  }
  PROCESS_INT32_SHIFT(ShiftLeft, ShiftLeft)
  PROCESS_INT32_SHIFT(ShiftRight, ShiftRightArithmetic)
  PROCESS_INT32_SHIFT(ShiftRightLogical, ShiftRightLogical)
#undef PROCESS_INT32_SHIFT

  maglev::ProcessResult Process(maglev::Int32BitwiseNot* node,
                                const maglev::ProcessingState& state) {
    // Turboshaft doesn't have a bitwise Not operator; we instead use "^ -1".
    SetMap(node, __ Word32BitwiseXor(Map(node->value_input()),
                                     __ Word32Constant(-1)));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Float64Negate* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64Negate(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64Round* node,
                                const maglev::ProcessingState& state) {
    if (node->kind() == maglev::Float64Round::Kind::kFloor) {
      SetMap(node, __ Float64RoundDown(Map(node->input())));
    } else if (node->kind() == maglev::Float64Round::Kind::kCeil) {
      SetMap(node, __ Float64RoundUp(Map(node->input())));
    } else {
      DCHECK_EQ(node->kind(), maglev::Float64Round::Kind::kNearest);
      // Nearest rounds to +infinity on ties. We emulate this by rounding up and
      // adjusting if the difference exceeds 0.5 (like SimplifiedLowering does
      // for lower Float64Round).
      OpIndex input = Map(node->input());
      ScopedVariable<Float64, AssemblerT> result(this,
                                                 __ Float64RoundUp(input));
      IF_NOT (__ Float64LessThanOrEqual(__ Float64Sub(result, 0.5), input)) {
        result = __ Float64Sub(result, 1.0);
      }

      SetMap(node, result);
    }
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Float64Ieee754Unary* node,
                                const maglev::ProcessingState& state) {
    FloatUnaryOp::Kind kind;
    switch (node->ieee_function()) {
#define CASE(MathName, ExpName, EnumName)                         \
  case maglev::Float64Ieee754Unary::Ieee754Function::k##EnumName: \
    kind = FloatUnaryOp::Kind::k##EnumName;                       \
    break;
      IEEE_754_UNARY_LIST(CASE)
#undef CASE
    }
    SetMap(node, __ Float64Unary(Map(node->input()), kind));
    return maglev::ProcessResult::kContinue;
  }

// Note that Maglev collects feedback in the generic binops and unops, so that
// Turbofan has chance to get better feedback. However, once we reach Turbofan,
// we stop collecting feedback, since we've tried multiple times to keep
// collecting feedback in Turbofan, but it never seemed worth it. The latest
// occurence of this was ended by this CL: https://crrev.com/c/4110858.
#define PROCESS_GENERIC_BINOP(Name)                                            \
  maglev::ProcessResult Process(maglev::Generic##Name* node,                   \
                                const maglev::ProcessingState& state) {        \
    OpIndex frame_state = BuildFrameState(node->lazy_deopt_info());            \
    SetMap(node,                                                               \
           __ Generic##Name(Map(node->left_input()), Map(node->right_input()), \
                            frame_state, native_context()));                   \
    return maglev::ProcessResult::kContinue;                                   \
  }
  GENERIC_BINOP_LIST(PROCESS_GENERIC_BINOP)
#undef PROCESS_GENERIC_BINOP

#define PROCESS_GENERIC_UNOP(Name)                                         \
  maglev::ProcessResult Process(maglev::Generic##Name* node,               \
                                const maglev::ProcessingState& state) {    \
    OpIndex frame_state = BuildFrameState(node->lazy_deopt_info());        \
    SetMap(node, __ Generic##Name(Map(node->operand_input()), frame_state, \
                                  native_context()));                      \
    return maglev::ProcessResult::kContinue;                               \
  }
  GENERIC_UNOP_LIST(PROCESS_GENERIC_UNOP)
#undef PROCESS_GENERIC_UNOP

  maglev::ProcessResult Process(maglev::ToNumberOrNumeric* node,
                                const maglev::ProcessingState& state) {
    OpIndex frame_state = BuildFrameState(node->lazy_deopt_info());
    SetMap(node, __ ToNumberOrNumeric(Map(node->value_input()), frame_state,
                                      native_context(), node->mode()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::LogicalNot* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = __ TaggedEqual(
        Map(node->value()), __ HeapConstant(factory_->true_value()));
    SetMap(node, ConvertWord32ToJSBool(condition, /*flip*/ true));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToBooleanLogicalNot* node,
                                const maglev::ProcessingState& state) {
    TruncateJSPrimitiveToUntaggedOp::InputAssumptions assumption =
        node->check_type() == maglev::CheckType::kCheckHeapObject
            ? TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kObject
            : TruncateJSPrimitiveToUntaggedOp::InputAssumptions::kHeapObject;
    V<Word32> condition = __ TruncateJSPrimitiveToUntagged(
        Map(node->value()), TruncateJSPrimitiveToUntaggedOp::UntaggedKind::kBit,
        assumption);
    SetMap(node, ConvertWord32ToJSBool(condition, /*flip*/ true));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Int32ToBoolean* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, ConvertWord32ToJSBool(Map(node->value()), node->flip()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToBoolean* node,
                                const maglev::ProcessingState& state) {
    V<Word32> condition = __ Float64Equal(Map(node->value()), 0.0);
    // {condition} is 0 if the input is truthy, and false otherwise (because we
    // compared "== 0" rather than "!= 0"), so we need to negate `flip` in the
    // call to ConvertWord32ToJSBool.
    SetMap(node, ConvertWord32ToJSBool(condition, !node->flip()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Int32ToNumber* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertInt32ToNumber(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::Float64ToTagged* node,
                                const maglev::ProcessingState& state) {
    // Float64ToTagged's conversion mode is used to control whether integer
    // floats should be converted to Smis or to HeapNumbers: kCanonicalizeSmi
    // means that they can be converted to Smis, and otherwise they should
    // remain HeapNumbers.
    ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind kind =
        node->conversion_mode() ==
                maglev::Float64ToTagged::ConversionMode::kCanonicalizeSmi
            ? ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber
            : ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kHeapNumber;
    SetMap(node,
           __ ConvertUntaggedToJSPrimitive(
               Map(node->input()), kind, RegisterRepresentation::Float64(),
               ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
               CheckForMinusZeroMode::kCheckForMinusZero));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::HoleyFloat64ToTagged* node,
                                const maglev::ProcessingState& state) {
    Label<Object> done(this);
    V<Float64> input = Map(node->input());
    if (node->conversion_mode() ==
        maglev::HoleyFloat64ToTagged::ConversionMode::kCanonicalizeSmi) {
      // ConvertUntaggedToJSPrimitive cannot at the same time canonicalize smis
      // and handle holes. We thus manually insert a smi check when the
      // conversion_mode is CanonicalizeSmi.
      IF (__ Float64IsSmi(input)) {
        GOTO(done,
             __ ConvertUntaggedToJSPrimitive(
                 __ TruncateFloat64ToInt32OverflowUndefined(input),
                 ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kSmi,
                 RegisterRepresentation::Word32(),
                 ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
                 CheckForMinusZeroMode::kDontCheckForMinusZero));
      }
    }
    GOTO(done, __ ConvertUntaggedToJSPrimitive(
                   Map(node->input()),
                   ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::
                       kHeapNumberOrUndefined,
                   RegisterRepresentation::Float64(),
                   ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
                   CheckForMinusZeroMode::kCheckForMinusZero));
    BIND(done, result);
    SetMap(node, result);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::CheckedNumberOrOddballToFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           __ ConvertJSPrimitiveToUntaggedOrDeopt(
               Map(node->input()), BuildFrameState(node->eager_deopt_info()),
               ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
                   kNumberOrOddball,
               ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kFloat64,
               CheckForMinusZeroMode::kCheckForMinusZero,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::UncheckedNumberOrOddballToFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertJSPrimitiveToUntagged(
                     Map(node->input()),
                     ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kFloat64,
                     ConvertJSPrimitiveToUntaggedOp::InputAssumptions::
                         kNumberOrOddball));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::TruncateUint32ToInt32* node,
                                const maglev::ProcessingState& state) {
    // This doesn't matter in Turboshaft: both Uint32 and Int32 are Word32.
    SetMap(node, Map(node->input()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedObjectToIndex* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           __ ConvertJSPrimitiveToUntaggedOrDeopt(
               Map(node->object_input()),
               BuildFrameState(node->eager_deopt_info()),
               ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
                   kNumberOrString,
               ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kArrayIndex,
               CheckForMinusZeroMode::kCheckForMinusZero,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ChangeInt32ToFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ChangeInt32ToFloat64(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::CheckedTruncateFloat64ToInt32* node,
                                const maglev::ProcessingState& state) {
    SetMap(node,
           __ ChangeFloat64ToInt32OrDeopt(
               Map(node->input()), BuildFrameState(node->eager_deopt_info()),
               CheckForMinusZeroMode::kCheckForMinusZero,
               node->eager_deopt_info()->feedback_to_update()));
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::HoleyFloat64ToMaybeNanFloat64* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ Float64SilenceNaN(Map(node->input())));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ToObject* node,
                                const maglev::ProcessingState& state) {
    SetMap(node, __ ConvertJSPrimitiveToObject(
                     Map(node->value_input()), Map(node->context()),
                     ConvertReceiverMode::kNotNullOrUndefined));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::Return* node,
                                const maglev::ProcessingState& state) {
    __ Return(Map(node->value_input()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::SetPendingMessage* node,
                                const maglev::ProcessingState& state) {
    __ StoreMessage(
        __ ExternalConstant(
            ExternalReference::address_of_pending_message(isolate_)),
        Map(node->value()));
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(maglev::ReduceInterruptBudgetForReturn*,
                                const maglev::ProcessingState&) {
    // No need to update the interrupt budget once we reach Turboshaft.
    return maglev::ProcessResult::kContinue;
  }
  maglev::ProcessResult Process(maglev::ReduceInterruptBudgetForLoop*,
                                const maglev::ProcessingState&) {
    // No need to update the interrupt budget once we reach Turboshaft.
    return maglev::ProcessResult::kContinue;
  }

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
  maglev::ProcessResult Process(
      maglev::GetContinuationPreservedEmbedderData* node,
      const maglev::ProcessingState&) {
    V<Object> data = __ GetContinuationPreservedEmbedderData();
    SetMap(node, data);
    return maglev::ProcessResult::kContinue;
  }

  maglev::ProcessResult Process(
      maglev::SetContinuationPreservedEmbedderData* node,
      const maglev::ProcessingState&) {
    V<Object> data = Map(node->input(0));
    __ SetContinuationPreservedEmbedderData(data);
    return maglev::ProcessResult::kContinue;
  }
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

  template <typename NodeT>
  maglev::ProcessResult Process(NodeT* node,
                                const maglev::ProcessingState& state) {
    UNIMPLEMENTED();
  }

  AssemblerT& Asm() { return assembler_; }
  Zone* temp_zone() { return temp_zone_; }
  Zone* graph_zone() { return Asm().output_graph().graph_zone(); }

 private:
  OpIndex BuildFrameState(maglev::EagerDeoptInfo* eager_deopt_info) {
    // TODO(dmercadier): handle other FrameTypes.
    DCHECK_EQ(eager_deopt_info->top_frame().type(),
              maglev::DeoptFrame::FrameType::kInterpretedFrame);
    maglev::InterpretedDeoptFrame& frame =
        eager_deopt_info->top_frame().as_interpreted();
    return BuildFrameState(frame, OutputFrameStateCombine::Ignore(), true);
  }

  OpIndex BuildFrameState(maglev::LazyDeoptInfo* lazy_deopt_info) {
    DCHECK_EQ(lazy_deopt_info->top_frame().type(),
              maglev::DeoptFrame::FrameType::kInterpretedFrame);
    maglev::InterpretedDeoptFrame& frame =
        lazy_deopt_info->top_frame().as_interpreted();
    // TODO(dmercadier): handle cases where the following DCHECK doesn't hold
    // (ie, cases where the return value is not the accumulator, and where there
    // are multiple return values).
    DCHECK(lazy_deopt_info->result_location() ==
               interpreter::Register::virtual_accumulator() &&
           lazy_deopt_info->result_size() == 1);
    return BuildFrameState(frame, OutputFrameStateCombine::PokeAt(0), true);
  }

  OpIndex BuildFrameState(maglev::InterpretedDeoptFrame& frame,
                          OutputFrameStateCombine combine, bool is_topmost) {
    FrameStateData::Builder builder;

    if (frame.parent() != nullptr) {
      DCHECK_EQ(frame.parent()->type(),
                maglev::DeoptFrame::FrameType::kInterpretedFrame);
      OpIndex parent_frame =
          BuildFrameState(frame.parent()->as_interpreted(), combine, false);
      builder.AddParentFrameState(parent_frame);
    }

    // Closure
    AddDeoptInput(builder, frame.closure());

    // Parameters
    frame.frame_state()->ForEachParameter(
        frame.unit(), [&](maglev::ValueNode* value, interpreter::Register reg) {
          AddDeoptInput(builder, value);
        });

    // Context
    AddDeoptInput(builder, frame.frame_state()->context(frame.unit()));

    // Locals
    // ForEachLocal in Maglev skips over dead registers, but we still need to
    // call AddUnusedRegister on the Turboshaft FrameStateData Builder.
    // {local_index} is used to keep track of such unused registers.
    // Among the variables not included in ForEachLocal is the Accumulator (but
    // this is fine since there is an entry in the state specifically for the
    // accumulator later).
    int local_index = 0;
    frame.frame_state()->ForEachLocal(
        frame.unit(), [&](maglev::ValueNode* value, interpreter::Register reg) {
          while (local_index < reg.index()) {
            builder.AddUnusedRegister();
            local_index++;
          }
          AddDeoptInput(builder, value);
          local_index++;
        });
    for (; local_index < frame.unit().register_count(); local_index++) {
      builder.AddUnusedRegister();
    }

    // Accumulator
    // When `combine` is PokeAt(0), the value of the accumulator is produced by
    // the function that lazy deopts, and thus it shouldn't be in the
    // FrameState (since the FrameState is defined before the function is
    // called), so in that case we set the Accumulator to UnusedRegister, even
    // if it is alive.
    // TODO(dmercadier): the "combine != PokeAt(0)" is fine for now since we
    // don't have PokeAt with other values than 0 for now, but once we do, we
    // should use instead something similar to InReturnValues in Maglev.
    if (is_topmost && frame.frame_state()->liveness()->AccumulatorIsLive() &&
        combine != OutputFrameStateCombine::PokeAt(0)) {
      AddDeoptInput(builder, frame.frame_state()->accumulator(frame.unit()));
    } else {
      builder.AddUnusedRegister();
    }

    const FrameStateInfo* frame_state_info = MakeFrameStateInfo(frame, combine);
    return __ FrameState(
        builder.Inputs(), builder.inlined(),
        builder.AllocateFrameStateData(*frame_state_info, graph_zone()));
  }

  void AddDeoptInput(FrameStateData::Builder& builder,
                     maglev::ValueNode* node) {
    builder.AddInput(MachineTypeFor(node->value_representation()), Map(node));
  }

  const FrameStateInfo* MakeFrameStateInfo(
      maglev::InterpretedDeoptFrame& maglev_frame,
      OutputFrameStateCombine combine) {
    FrameStateType type = FrameStateType::kUnoptimizedFunction;
    int parameter_count = maglev_frame.unit().parameter_count();
    int local_count = maglev_frame.unit().register_count();
    Handle<SharedFunctionInfo> shared_info =
        maglev_frame.unit().shared_function_info().object();
    FrameStateFunctionInfo* info = graph_zone()->New<FrameStateFunctionInfo>(
        type, parameter_count, local_count, shared_info);

    return graph_zone()->New<FrameStateInfo>(maglev_frame.bytecode_position(),
                                             combine, info);
  }

  template <typename rep>
  V<Word32> ConvertCompare(maglev::Input left_input, maglev::Input right_input,
                           ::Operation operation) {
    ComparisonOp::Kind kind;
    bool swap_inputs = false;
    switch (operation) {
      case ::Operation::kEqual:
        kind = ComparisonOp::Kind::kEqual;
        break;
      case ::Operation::kLessThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        break;
      case ::Operation::kLessThanOrEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        break;
      case ::Operation::kGreaterThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        swap_inputs = true;
        break;
      case ::Operation::kGreaterThanOrEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        swap_inputs = true;
        break;
      default:
        UNREACHABLE();
    }
    V<rep> left = Map(left_input);
    V<rep> right = Map(right_input);
    if (swap_inputs) std::swap(left, right);
    return __ Comparison(left, right, kind, V<rep>::rep);
  }

  V<Word32> ConvertInt32Compare(maglev::Input left_input,
                                maglev::Input right_input,
                                maglev::AssertCondition condition,
                                bool* negate_result) {
    ComparisonOp::Kind kind;
    bool swap_inputs = false;
    switch (condition) {
      case maglev::AssertCondition::kEqual:
        kind = ComparisonOp::Kind::kEqual;
        break;
      case maglev::AssertCondition::kNotEqual:
        kind = ComparisonOp::Kind::kEqual;
        *negate_result = true;
        break;
      case maglev::AssertCondition::kLessThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        break;
      case maglev::AssertCondition::kLessThanEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        break;
      case maglev::AssertCondition::kGreaterThan:
        kind = ComparisonOp::Kind::kSignedLessThan;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kGreaterThanEqual:
        kind = ComparisonOp::Kind::kSignedLessThanOrEqual;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kUnsignedLessThan:
        kind = ComparisonOp::Kind::kUnsignedLessThan;
        break;
      case maglev::AssertCondition::kUnsignedLessThanEqual:
        kind = ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        break;
      case maglev::AssertCondition::kUnsignedGreaterThan:
        kind = ComparisonOp::Kind::kUnsignedLessThan;
        swap_inputs = true;
        break;
      case maglev::AssertCondition::kUnsignedGreaterThanEqual:
        kind = ComparisonOp::Kind::kUnsignedLessThanOrEqual;
        swap_inputs = true;
        break;
    }
    V<Word32> left = Map(left_input);
    V<Word32> right = Map(right_input);
    if (swap_inputs) std::swap(left, right);
    return __ Comparison(left, right, kind, WordRepresentation::Word32());
  }

  V<Word32> RootEqual(maglev::Input input, RootIndex root) {
    return __ TaggedEqual(
        Map(input),
        __ HeapConstant(Handle<HeapObject>::cast(isolate_->root_handle(root))));
  }

  void FixLoopPhis(maglev::BasicBlock* loop) {
    DCHECK(loop->is_loop());
    for (maglev::Phi* maglev_phi : *loop->phis()) {
      OpIndex phi_index = Map(maglev_phi);
      PendingLoopPhiOp& pending_phi =
          __ output_graph().Get(phi_index).Cast<PendingLoopPhiOp>();
      __ output_graph().Replace<PhiOp>(
          phi_index,
          base::VectorOf({pending_phi.first(), Map(maglev_phi -> input(1))}),
          pending_phi.rep);
    }
  }

  RegisterRepresentation RegisterRepresentationFor(
      maglev::ValueRepresentation value_rep) {
    switch (value_rep) {
      case maglev::ValueRepresentation::kTagged:
        return RegisterRepresentation::Tagged();
      case maglev::ValueRepresentation::kInt32:
      case maglev::ValueRepresentation::kUint32:
        return RegisterRepresentation::Word32();
      case maglev::ValueRepresentation::kFloat64:
      case maglev::ValueRepresentation::kHoleyFloat64:
        return RegisterRepresentation::Float64();
      case maglev::ValueRepresentation::kIntPtr:
        return RegisterRepresentation::WordPtr();
    }
  }

  // TODO(dmercadier): Using a Branch would open more optimization opportunities
  // for BranchElimination compared to using a Select. However, in most cases,
  // Maglev should avoid materializing JS booleans, so there is a good chance
  // that it we actually need to do it, it's because we have to, and
  // BranchElimination probably cannot help. Thus, using a Select rather than a
  // Branch leads to smaller graphs, which is generally beneficial. Still, once
  // the graph builder is finished, we should evaluate whether Select or Branch
  // is the best choice here.
  V<Object> ConvertWord32ToJSBool(V<Word32> b, bool flip = false) {
    V<Boolean> true_idx = __ HeapConstant(factory_->true_value());
    V<Boolean> false_idx = __ HeapConstant(factory_->false_value());
    if (flip) std::swap(true_idx, false_idx);
    return __ Select(b, true_idx, false_idx, RegisterRepresentation::Tagged(),
                     BranchHint::kNone, SelectOp::Implementation::kBranch);
  }

  class ThrowingScope {
    // In Maglev, exception handlers have no predecessors, and their Phis are a
    // bit special: they all correspond to interpreter registers, and get
    // eventually initialized with the value that their predecessors have for
    // the corresponding interpreter registers.

    // In Turboshaft, exception handlers have predecessors and contain regular
    // phis. Creating a ThrowingScope takes care of recording in Variables
    // the current value of interpreter registers (right before emitting a node
    // that can throw), and sets the current_catch_block of the Assembler.
    // Throwing operations that are emitted while the scope is active will
    // automatically be wired to the catch handler. Then, when calling
    // Process(Phi) on exception phis (= when processing the catch handler),
    // these Phis will be mapped to the Variable corresponding to their owning
    // intepreter register.

   public:
    ThrowingScope(GraphBuilder* builder, maglev::NodeBase* throwing_node)
        : builder_(*builder) {
      DCHECK(throwing_node->properties().can_throw());
      const maglev::ExceptionHandlerInfo* info =
          throwing_node->exception_handler_info();
      if (!info->HasExceptionHandler()) return;

      maglev::BasicBlock* block = info->catch_block.block_ptr();
      auto* liveness = block->state()->frame_state().liveness();

      maglev::LazyDeoptInfo* deopt_info = throwing_node->lazy_deopt_info();
      const maglev::InterpretedDeoptFrame* lazy_frame;
      switch (deopt_info->top_frame().type()) {
        case maglev::DeoptFrame::FrameType::kInterpretedFrame:
          lazy_frame = &deopt_info->top_frame().as_interpreted();
          break;
        case maglev::DeoptFrame::FrameType::kInlinedArgumentsFrame:
          UNREACHABLE();
        case maglev::DeoptFrame::FrameType::kConstructInvokeStubFrame:
        case maglev::DeoptFrame::FrameType::kBuiltinContinuationFrame:
          lazy_frame = &deopt_info->top_frame().parent()->as_interpreted();
          break;
      }

      lazy_frame->frame_state()->ForEachValue(
          lazy_frame->unit(), [this, liveness](maglev::ValueNode* value,
                                               interpreter::Register reg) {
            if (!reg.is_parameter() && !liveness->RegisterIsLive(reg.index())) {
              // Skip, since not live at the handler offset.
              return;
            }
            auto it = builder_.regs_to_vars_.find(reg.index());
            Variable var;
            if (it == builder_.regs_to_vars_.end()) {
              var = __ NewVariable(RegisterRepresentation::Tagged());
              builder_.regs_to_vars_.insert({reg.index(), var});
            } else {
              var = it->second;
            }
            __ SetVariable(var, builder_.Map(value));
          });

      DCHECK_EQ(__ current_catch_block(), nullptr);
      __ set_current_catch_block(builder_.Map(block));
    }

    ~ThrowingScope() {
      // Resetting the catch handler. It is always set on a case-by-case basis
      // before emitting a throwing node, so there is no need to "reset the
      // previous catch handler" or something like that, since there is no
      // previous handler (there is a DCHECK in the ThrowingScope constructor
      // checking that the current_catch_block is indeed nullptr when the scope
      // is created).
      __ set_current_catch_block(nullptr);
    }

   private:
    GraphBuilder::AssemblerT& Asm() { return builder_.Asm(); }
    GraphBuilder& builder_;
  };

  OpIndex Map(const maglev::Input input) { return Map(input.node()); }
  OpIndex Map(const maglev::NodeBase* node) {
    DCHECK(node_mapping_[node].valid());
    return node_mapping_[node];
  }
  Block* Map(const maglev::BasicBlock* block) { return block_mapping_[block]; }

  OpIndex SetMap(maglev::NodeBase* node, OpIndex idx) {
    DCHECK(idx.valid());
    node_mapping_[node] = idx;
    return idx;
  }

  V<Context> native_context() {
    if (!native_context_.valid()) {
      native_context_ =
          __ HeapConstant(broker_->target_native_context().object());
    }
    return native_context_;
  }

  Zone* temp_zone_;
  LocalIsolate* isolate_ = PipelineData::Get().isolate()->AsLocalIsolate();
  JSHeapBroker* broker_ = PipelineData::Get().broker();
  LocalFactory* factory_ = isolate_->factory();
  AssemblerT assembler_;
  maglev::MaglevCompilationUnit* maglev_compilation_unit_;
  ZoneUnorderedMap<const maglev::NodeBase*, OpIndex> node_mapping_;
  ZoneUnorderedMap<const maglev::BasicBlock*, Block*> block_mapping_;
  ZoneUnorderedMap<int, Variable> regs_to_vars_;
  V<Context> native_context_ = OpIndex::Invalid();
  base::SmallVector<int, 16> predecessor_permutation_;
};

void MaglevGraphBuildingPhase::Run(Zone* temp_zone) {
  PipelineData& data = PipelineData::Get();
  JSHeapBroker* broker = data.broker();
  UnparkedScopeIfNeeded unparked_scope(broker);

  auto compilation_info = maglev::MaglevCompilationInfo::New(
      data.isolate(), broker, data.info()->closure(),
      data.info()->osr_offset());

  LocalIsolate* local_isolate = broker->local_isolate()
                                    ? broker->local_isolate()
                                    : broker->isolate()->AsLocalIsolate();
  maglev::Graph* maglev_graph =
      maglev::Graph::New(temp_zone, data.info()->is_osr());
  if (V8_UNLIKELY(data.info()->trace_turbo_graph())) {
    compilation_info->set_graph_labeller(new maglev::MaglevGraphLabeller());
  }
  maglev::MaglevGraphBuilder maglev_graph_builder(
      local_isolate, compilation_info->toplevel_compilation_unit(),
      maglev_graph);
  maglev_graph_builder.Build();

  if (V8_UNLIKELY(data.info()->trace_turbo_graph())) {
    CodeTracer* code_tracer = data.GetCodeTracer();
    CodeTracer::StreamScope tracing_scope(code_tracer);
    tracing_scope.stream()
        << "\n----- Maglev graph after MaglevGraphBuilding -----" << std::endl;
    maglev::PrintGraph(tracing_scope.stream(), compilation_info.get(),
                       maglev_graph);
  }

  maglev::GraphProcessor<GraphBuilder, true> builder(
      data.graph(), temp_zone, compilation_info->toplevel_compilation_unit());
  builder.ProcessGraph(maglev_graph);
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
