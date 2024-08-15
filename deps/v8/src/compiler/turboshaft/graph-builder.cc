// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/graph-builder.h"

#include <limits>
#include <numeric>
#include <string_view>

#include "src/base/container-utils.h"
#include "src/base/logging.h"
#include "src/base/optional.h"
#include "src/base/safe_conversions.h"
#include "src/base/small-vector.h"
#include "src/base/vector.h"
#include "src/codegen/bailout-reason.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/fast-api-calls.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-aux-data.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/schedule.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/state-values-utils.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/deopt-data.h"
#include "src/compiler/turboshaft/explicit-truncation-reducer.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/flags/flags.h"
#include "src/heap/factory-inl.h"
#include "src/objects/map.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

#include "src/compiler/turboshaft/define-assembler-macros.inc"

namespace {

struct GraphBuilder {
  Zone* phase_zone;
  Schedule& schedule;
  Linkage* linkage;

  Isolate* isolate = PipelineData::Get().isolate();
  JSHeapBroker* broker = PipelineData::Get().broker();
  Zone* graph_zone = PipelineData::Get().graph_zone();
  using AssemblerT = TSAssembler<ExplicitTruncationReducer>;
  AssemblerT assembler{PipelineData::Get().graph(), PipelineData::Get().graph(),
                       phase_zone};
  SourcePositionTable* source_positions =
      PipelineData::Get().source_positions();
  NodeOriginTable* origins = PipelineData::Get().node_origins();

  struct BlockData {
    Block* block;
    OpIndex final_frame_state;
  };
  NodeAuxData<OpIndex> op_mapping{phase_zone};
  ZoneVector<BlockData> block_mapping{schedule.RpoBlockCount(), phase_zone};
  bool inside_region = false;

  base::Optional<BailoutReason> Run();
  AssemblerT& Asm() { return assembler; }

 private:
  OpIndex Map(Node* old_node) {
    OpIndex result = op_mapping.Get(old_node);
    DCHECK(__ output_graph().IsValid(result));
    return result;
  }
  Block* Map(BasicBlock* block) {
    Block* result = block_mapping[block->rpo_number()].block;
    DCHECK_NOT_NULL(result);
    return result;
  }

  void FixLoopPhis(BasicBlock* loop) {
    DCHECK(loop->IsLoopHeader());
    for (Node* node : *loop->nodes()) {
      if (node->opcode() != IrOpcode::kPhi) {
        continue;
      }
      OpIndex phi_index = Map(node);
      PendingLoopPhiOp& pending_phi =
          __ output_graph().Get(phi_index).Cast<PendingLoopPhiOp>();
      __ output_graph().Replace<PhiOp>(
          phi_index,
          base::VectorOf({pending_phi.first(), Map(node->InputAt(1))}),
          pending_phi.rep);
    }
  }

  void ProcessDeoptInput(FrameStateData::Builder* builder, Node* input,
                         MachineType type) {
    DCHECK_NE(input->opcode(), IrOpcode::kObjectState);
    DCHECK_NE(input->opcode(), IrOpcode::kStateValues);
    DCHECK_NE(input->opcode(), IrOpcode::kTypedStateValues);
    if (input->opcode() == IrOpcode::kObjectId) {
      builder->AddDematerializedObjectReference(ObjectIdOf(input->op()));
    } else if (input->opcode() == IrOpcode::kTypedObjectState) {
      const TypedObjectStateInfo& info =
          OpParameter<TypedObjectStateInfo>(input->op());
      int field_count = input->op()->ValueInputCount();
      builder->AddDematerializedObject(info.object_id(),
                                       static_cast<uint32_t>(field_count));
      for (int i = 0; i < field_count; ++i) {
        ProcessDeoptInput(builder, input->InputAt(i),
                          (*info.machine_types())[i]);
      }
    } else if (input->opcode() == IrOpcode::kArgumentsElementsState) {
      builder->AddArgumentsElements(ArgumentsStateTypeOf(input->op()));
    } else if (input->opcode() == IrOpcode::kArgumentsLengthState) {
      builder->AddArgumentsLength();
    } else {
      builder->AddInput(type, Map(input));
    }
  }

  void ProcessStateValues(FrameStateData::Builder* builder,
                          Node* state_values) {
    for (auto it = StateValuesAccess(state_values).begin(); !it.done(); ++it) {
      if (Node* node = it.node()) {
        ProcessDeoptInput(builder, node, (*it).type);
      } else {
        builder->AddUnusedRegister();
      }
    }
  }

  void BuildFrameStateData(FrameStateData::Builder* builder,
                           FrameState frame_state) {
    if (frame_state.outer_frame_state()->opcode() != IrOpcode::kStart) {
      builder->AddParentFrameState(Map(frame_state.outer_frame_state()));
    }
    ProcessDeoptInput(builder, frame_state.function(),
                      MachineType::AnyTagged());
    ProcessStateValues(builder, frame_state.parameters());
    ProcessDeoptInput(builder, frame_state.context(), MachineType::AnyTagged());
    ProcessStateValues(builder, frame_state.locals());
    Node* stack = frame_state.stack();
    if (v8_flags.turboshaft_frontend) {
      // If we run graph building before Turbofan's SimplifiedLowering, the
      // `stack` input of frame states is still a single deopt input, rather
      // than a StateValues node.
      if (stack->opcode() == IrOpcode::kHeapConstant &&
          *HeapConstantOf(stack->op()) ==
              ReadOnlyRoots(isolate->heap()).optimized_out()) {
        // Nothing to do in this case.
      } else {
        const Operation& accumulator_op = __ output_graph().Get(Map(stack));
        const RegisterRepresentation accumulator_rep =
            accumulator_op.outputs_rep()[0];
        MachineType type;
        switch (accumulator_rep.value()) {
          case RegisterRepresentation::Tagged():
            type = MachineType::AnyTagged();
            break;
          default:
            UNIMPLEMENTED();
        }
        ProcessDeoptInput(builder, stack, type);
      }
    } else {
      ProcessStateValues(builder, stack);
    }
  }

  Block::Kind BlockKind(BasicBlock* block) {
    switch (block->front()->opcode()) {
      case IrOpcode::kStart:
      case IrOpcode::kEnd:
      case IrOpcode::kMerge:
        return Block::Kind::kMerge;
      case IrOpcode::kIfTrue:
      case IrOpcode::kIfFalse:
      case IrOpcode::kIfValue:
      case IrOpcode::kIfDefault:
      case IrOpcode::kIfSuccess:
      case IrOpcode::kIfException:
        return Block::Kind::kBranchTarget;
      case IrOpcode::kLoop:
        return Block::Kind::kLoopHeader;
      default:
        block->front()->Print();
        UNIMPLEMENTED();
    }
  }
  OpIndex Process(Node* node, BasicBlock* block,
                  const base::SmallVector<int, 16>& predecessor_permutation,
                  OpIndex& dominating_frame_state,
                  base::Optional<BailoutReason>* bailout,
                  bool is_final_control = false);
};

base::Optional<BailoutReason> GraphBuilder::Run() {
  for (BasicBlock* block : *schedule.rpo_order()) {
    block_mapping[block->rpo_number()].block =
        block->IsLoopHeader() ? __ NewLoopHeader() : __ NewBlock();
  }

  for (BasicBlock* block : *schedule.rpo_order()) {
    Block* target_block = Map(block);
    if (!__ Bind(target_block)) continue;

    // Since we visit blocks in rpo-order, the new block predecessors are sorted
    // in rpo order too. However, the input schedule does not order
    // predecessors, so we have to apply a corresponding permutation to phi
    // inputs.
    const BasicBlockVector& predecessors = block->predecessors();
    base::SmallVector<int, 16> predecessor_permutation(predecessors.size());
    std::iota(predecessor_permutation.begin(), predecessor_permutation.end(),
              0);
    std::sort(predecessor_permutation.begin(), predecessor_permutation.end(),
              [&](size_t i, size_t j) {
                return predecessors[i]->rpo_number() <
                       predecessors[j]->rpo_number();
              });

    OpIndex dominating_frame_state = OpIndex::Invalid();
    if (!predecessors.empty()) {
      dominating_frame_state =
          block_mapping[predecessors[0]->rpo_number()].final_frame_state;
      for (size_t i = 1; i < predecessors.size(); ++i) {
        if (block_mapping[predecessors[i]->rpo_number()].final_frame_state !=
            dominating_frame_state) {
          dominating_frame_state = OpIndex::Invalid();
          break;
        }
      }
    }
    base::Optional<BailoutReason> bailout = base::nullopt;
    for (Node* node : *block->nodes()) {
      if (V8_UNLIKELY(node->InputCount() >=
                      int{std::numeric_limits<
                          decltype(Operation::input_count)>::max()})) {
        return BailoutReason::kTooManyArguments;
      }
      OpIndex i = Process(node, block, predecessor_permutation,
                          dominating_frame_state, &bailout);
      if (V8_UNLIKELY(bailout)) return bailout;
      if (!__ current_block()) break;
      op_mapping.Set(node, i);
    }
    // We have terminated this block with `Unreachable`, so we stop generation
    // here and continue with the next block.
    if (!__ current_block()) continue;

    if (Node* node = block->control_input()) {
      if (V8_UNLIKELY(node->InputCount() >=
                      int{std::numeric_limits<
                          decltype(Operation::input_count)>::max()})) {
        return BailoutReason::kTooManyArguments;
      }
      OpIndex i = Process(node, block, predecessor_permutation,
                          dominating_frame_state, &bailout, true);
      if (V8_UNLIKELY(bailout)) return bailout;
      op_mapping.Set(node, i);
    }
    switch (block->control()) {
      case BasicBlock::kGoto: {
        DCHECK_EQ(block->SuccessorCount(), 1);
        Block* destination = Map(block->SuccessorAt(0));
        __ Goto(destination);
        if (destination->IsBound()) {
          DCHECK(destination->IsLoop());
          FixLoopPhis(block->SuccessorAt(0));
        }
        break;
      }
      case BasicBlock::kBranch:
      case BasicBlock::kSwitch:
      case BasicBlock::kReturn:
      case BasicBlock::kDeoptimize:
      case BasicBlock::kThrow:
      case BasicBlock::kCall:
      case BasicBlock::kTailCall:
        break;
      case BasicBlock::kNone:
        UNREACHABLE();
    }
    DCHECK_NULL(__ current_block());

    block_mapping[block->rpo_number()].final_frame_state =
        dominating_frame_state;
  }

  if (source_positions->IsEnabled()) {
    for (OpIndex index : __ output_graph().AllOperationIndices()) {
      compiler::NodeId origin =
          __ output_graph().operation_origins()[index].DecodeTurbofanNodeId();
      __ output_graph().source_positions()[index] =
          source_positions->GetSourcePosition(origin);
    }
  }

  if (origins) {
    for (OpIndex index : __ output_graph().AllOperationIndices()) {
      OpIndex origin = __ output_graph().operation_origins()[index];
      origins->SetNodeOrigin(index.id(), origin.DecodeTurbofanNodeId());
    }
  }

  return base::nullopt;
}

OpIndex GraphBuilder::Process(
    Node* node, BasicBlock* block,
    const base::SmallVector<int, 16>& predecessor_permutation,
    OpIndex& dominating_frame_state, base::Optional<BailoutReason>* bailout,
    bool is_final_control) {
  if (Asm().current_block() == nullptr) {
    return OpIndex::Invalid();
  }
  __ SetCurrentOrigin(OpIndex::EncodeTurbofanNodeId(node->id()));
  const Operator* op = node->op();
  Operator::Opcode opcode = op->opcode();
  switch (opcode) {
    case IrOpcode::kStart:
    case IrOpcode::kMerge:
    case IrOpcode::kLoop:
    case IrOpcode::kIfTrue:
    case IrOpcode::kIfFalse:
    case IrOpcode::kIfDefault:
    case IrOpcode::kIfValue:
    case IrOpcode::kStateValues:
    case IrOpcode::kTypedStateValues:
    case IrOpcode::kObjectId:
    case IrOpcode::kTypedObjectState:
    case IrOpcode::kArgumentsElementsState:
    case IrOpcode::kArgumentsLengthState:
    case IrOpcode::kEffectPhi:
    case IrOpcode::kTerminate:
      return OpIndex::Invalid();

    case IrOpcode::kCheckpoint: {
      // Preserve the frame state from this checkpoint for following nodes.
      dominating_frame_state = Map(NodeProperties::GetFrameStateInput(node));
      return OpIndex::Invalid();
    }

    case IrOpcode::kIfException: {
      return __ CatchBlockBegin();
    }

    case IrOpcode::kIfSuccess: {
      return OpIndex::Invalid();
    }

    case IrOpcode::kParameter: {
      const ParameterInfo& info = ParameterInfoOf(op);
      RegisterRepresentation rep =
          RegisterRepresentation::FromMachineRepresentation(
              linkage->GetParameterType(ParameterIndexOf(node->op()))
                  .representation());
      return __ Parameter(info.index(), rep, info.debug_name());
    }

    case IrOpcode::kOsrValue: {
      return __ OsrValue(OsrValueIndexOf(op));
    }

    case IrOpcode::kPhi: {
      int input_count = op->ValueInputCount();
      RegisterRepresentation rep =
          RegisterRepresentation::FromMachineRepresentation(
              PhiRepresentationOf(op));
      if (__ current_block()->IsLoop()) {
        DCHECK_EQ(input_count, 2);
        return __ PendingLoopPhi(Map(node->InputAt(0)), rep);
      } else {
        base::SmallVector<OpIndex, 16> inputs;
        for (int i = 0; i < input_count; ++i) {
          // If this predecessor end with an unreachable (and doesn't jump to
          // this merge block), we skip its Phi input.
          Block* pred = Map(block->PredecessorAt(predecessor_permutation[i]));
          if (!pred->IsBound() ||
              pred->LastOperation(__ output_graph()).Is<UnreachableOp>()) {
            continue;
          }
          inputs.push_back(Map(node->InputAt(predecessor_permutation[i])));
        }
        return __ Phi(base::VectorOf(inputs), rep);
      }
    }

    case IrOpcode::kInt64Constant:
      return __ Word64Constant(static_cast<uint64_t>(OpParameter<int64_t>(op)));
    case IrOpcode::kInt32Constant:
      return __ Word32Constant(static_cast<uint32_t>(OpParameter<int32_t>(op)));
    case IrOpcode::kFloat64Constant:
      return __ Float64Constant(OpParameter<double>(op));
    case IrOpcode::kFloat32Constant:
      return __ Float32Constant(OpParameter<float>(op));
    case IrOpcode::kNumberConstant:
      return __ NumberConstant(OpParameter<double>(op));
    case IrOpcode::kTaggedIndexConstant:
      return __ TaggedIndexConstant(OpParameter<int32_t>(op));
    case IrOpcode::kHeapConstant:
      return __ HeapConstant(HeapConstantOf(op));
    case IrOpcode::kCompressedHeapConstant:
      return __ CompressedHeapConstant(HeapConstantOf(op));
    case IrOpcode::kExternalConstant:
      return __ ExternalConstant(OpParameter<ExternalReference>(op));
    case IrOpcode::kRelocatableInt64Constant:
      return __ RelocatableConstant(
          OpParameter<RelocatablePtrConstantInfo>(op).value(),
          OpParameter<RelocatablePtrConstantInfo>(op).rmode());
#define BINOP_CASE(opcode, assembler_op) \
  case IrOpcode::k##opcode:              \
    return __ assembler_op(Map(node->InputAt(0)), Map(node->InputAt(1)));

      BINOP_CASE(Int32Add, Word32Add)
      BINOP_CASE(Int64Add, Word64Add)
      BINOP_CASE(Int32Mul, Word32Mul)
      BINOP_CASE(Int64Mul, Word64Mul)
      BINOP_CASE(Word32And, Word32BitwiseAnd)
      BINOP_CASE(Word64And, Word64BitwiseAnd)
      BINOP_CASE(Word32Or, Word32BitwiseOr)
      BINOP_CASE(Word64Or, Word64BitwiseOr)
      BINOP_CASE(Word32Xor, Word32BitwiseXor)
      BINOP_CASE(Word64Xor, Word64BitwiseXor)
      BINOP_CASE(Int32Sub, Word32Sub)
      BINOP_CASE(Int64Sub, Word64Sub)
      BINOP_CASE(Int32Div, Int32Div)
      BINOP_CASE(Uint32Div, Uint32Div)
      BINOP_CASE(Int64Div, Int64Div)
      BINOP_CASE(Uint64Div, Uint64Div)
      BINOP_CASE(Int32Mod, Int32Mod)
      BINOP_CASE(Uint32Mod, Uint32Mod)
      BINOP_CASE(Int64Mod, Int64Mod)
      BINOP_CASE(Uint64Mod, Uint64Mod)
      BINOP_CASE(Int32MulHigh, Int32MulOverflownBits)
      BINOP_CASE(Int64MulHigh, Int64MulOverflownBits)
      BINOP_CASE(Uint32MulHigh, Uint32MulOverflownBits)
      BINOP_CASE(Uint64MulHigh, Uint64MulOverflownBits)

      BINOP_CASE(Float32Add, Float32Add)
      BINOP_CASE(Float64Add, Float64Add)
      BINOP_CASE(Float32Sub, Float32Sub)
      BINOP_CASE(Float64Sub, Float64Sub)
      BINOP_CASE(Float64Mul, Float64Mul)
      BINOP_CASE(Float32Mul, Float32Mul)
      BINOP_CASE(Float32Div, Float32Div)
      BINOP_CASE(Float64Div, Float64Div)
      BINOP_CASE(Float32Min, Float32Min)
      BINOP_CASE(Float64Min, Float64Min)
      BINOP_CASE(Float32Max, Float32Max)
      BINOP_CASE(Float64Max, Float64Max)
      BINOP_CASE(Float64Mod, Float64Mod)
      BINOP_CASE(Float64Pow, Float64Power)
      BINOP_CASE(Float64Atan2, Float64Atan2)

      BINOP_CASE(Word32Shr, Word32ShiftRightLogical)
      BINOP_CASE(Word64Shr, Word64ShiftRightLogical)

      BINOP_CASE(Word32Shl, Word32ShiftLeft)
      BINOP_CASE(Word64Shl, Word64ShiftLeft)

      BINOP_CASE(Word32Rol, Word32RotateLeft)
      BINOP_CASE(Word64Rol, Word64RotateLeft)

      BINOP_CASE(Word32Ror, Word32RotateRight)
      BINOP_CASE(Word64Ror, Word64RotateRight)

      BINOP_CASE(Word32Equal, Word32Equal)
      BINOP_CASE(Word64Equal, Word64Equal)
      BINOP_CASE(Float32Equal, Float32Equal)
      BINOP_CASE(Float64Equal, Float64Equal)

      BINOP_CASE(Int32LessThan, Int32LessThan)
      BINOP_CASE(Int64LessThan, Int64LessThan)
      BINOP_CASE(Uint32LessThan, Uint32LessThan)
      BINOP_CASE(Uint64LessThan, Uint64LessThan)
      BINOP_CASE(Float32LessThan, Float32LessThan)
      BINOP_CASE(Float64LessThan, Float64LessThan)

      BINOP_CASE(Int32LessThanOrEqual, Int32LessThanOrEqual)
      BINOP_CASE(Int64LessThanOrEqual, Int64LessThanOrEqual)
      BINOP_CASE(Uint32LessThanOrEqual, Uint32LessThanOrEqual)
      BINOP_CASE(Uint64LessThanOrEqual, Uint64LessThanOrEqual)
      BINOP_CASE(Float32LessThanOrEqual, Float32LessThanOrEqual)
      BINOP_CASE(Float64LessThanOrEqual, Float64LessThanOrEqual)

      BINOP_CASE(Int32AddWithOverflow, Int32AddCheckOverflow)
      BINOP_CASE(Int64AddWithOverflow, Int64AddCheckOverflow)
      BINOP_CASE(Int32MulWithOverflow, Int32MulCheckOverflow)
      BINOP_CASE(Int64MulWithOverflow, Int64MulCheckOverflow)
      BINOP_CASE(Int32SubWithOverflow, Int32SubCheckOverflow)
      BINOP_CASE(Int64SubWithOverflow, Int64SubCheckOverflow)
#undef BINOP_CASE

    case IrOpcode::kWord64Sar:
    case IrOpcode::kWord32Sar: {
      WordRepresentation rep = opcode == IrOpcode::kWord64Sar
                                   ? WordRepresentation::Word64()
                                   : WordRepresentation::Word32();
      ShiftOp::Kind kind;
      switch (ShiftKindOf(op)) {
        case ShiftKind::kShiftOutZeros:
          kind = ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros;
          break;
        case ShiftKind::kNormal:
          kind = ShiftOp::Kind::kShiftRightArithmetic;
          break;
      }
      return __ Shift(Map(node->InputAt(0)), Map(node->InputAt(1)), kind, rep);
    }

#define UNARY_CASE(opcode, assembler_op) \
  case IrOpcode::k##opcode:              \
    return __ assembler_op(Map(node->InputAt(0)));

      UNARY_CASE(Word32ReverseBytes, Word32ReverseBytes)
      UNARY_CASE(Word64ReverseBytes, Word64ReverseBytes)
      UNARY_CASE(Word32Clz, Word32CountLeadingZeros)
      UNARY_CASE(Word64Clz, Word64CountLeadingZeros)
      UNARY_CASE(Word32Ctz, Word32CountTrailingZeros)
      UNARY_CASE(Word64Ctz, Word64CountTrailingZeros)
      UNARY_CASE(Word32Popcnt, Word32PopCount)
      UNARY_CASE(Word64Popcnt, Word64PopCount)
      UNARY_CASE(SignExtendWord8ToInt32, Word32SignExtend8)
      UNARY_CASE(SignExtendWord16ToInt32, Word32SignExtend16)
      UNARY_CASE(SignExtendWord8ToInt64, Word64SignExtend8)
      UNARY_CASE(SignExtendWord16ToInt64, Word64SignExtend16)

      UNARY_CASE(Float32Abs, Float32Abs)
      UNARY_CASE(Float64Abs, Float64Abs)
      UNARY_CASE(Float32Neg, Float32Negate)
      UNARY_CASE(Float64Neg, Float64Negate)
      UNARY_CASE(Float64SilenceNaN, Float64SilenceNaN)
      UNARY_CASE(Float32RoundDown, Float32RoundDown)
      UNARY_CASE(Float64RoundDown, Float64RoundDown)
      UNARY_CASE(Float32RoundUp, Float32RoundUp)
      UNARY_CASE(Float64RoundUp, Float64RoundUp)
      UNARY_CASE(Float32RoundTruncate, Float32RoundToZero)
      UNARY_CASE(Float64RoundTruncate, Float64RoundToZero)
      UNARY_CASE(Float32RoundTiesEven, Float32RoundTiesEven)
      UNARY_CASE(Float64RoundTiesEven, Float64RoundTiesEven)
      UNARY_CASE(Float64Log, Float64Log)
      UNARY_CASE(Float32Sqrt, Float32Sqrt)
      UNARY_CASE(Float64Sqrt, Float64Sqrt)
      UNARY_CASE(Float64Exp, Float64Exp)
      UNARY_CASE(Float64Expm1, Float64Expm1)
      UNARY_CASE(Float64Sin, Float64Sin)
      UNARY_CASE(Float64Cos, Float64Cos)
      UNARY_CASE(Float64Sinh, Float64Sinh)
      UNARY_CASE(Float64Cosh, Float64Cosh)
      UNARY_CASE(Float64Asin, Float64Asin)
      UNARY_CASE(Float64Acos, Float64Acos)
      UNARY_CASE(Float64Asinh, Float64Asinh)
      UNARY_CASE(Float64Acosh, Float64Acosh)
      UNARY_CASE(Float64Tan, Float64Tan)
      UNARY_CASE(Float64Tanh, Float64Tanh)
      UNARY_CASE(Float64Log2, Float64Log2)
      UNARY_CASE(Float64Log10, Float64Log10)
      UNARY_CASE(Float64Log1p, Float64Log1p)
      UNARY_CASE(Float64Atan, Float64Atan)
      UNARY_CASE(Float64Atanh, Float64Atanh)
      UNARY_CASE(Float64Cbrt, Float64Cbrt)

      UNARY_CASE(BitcastWord32ToWord64, BitcastWord32ToWord64)
      UNARY_CASE(BitcastFloat32ToInt32, BitcastFloat32ToWord32)
      UNARY_CASE(BitcastInt32ToFloat32, BitcastWord32ToFloat32)
      UNARY_CASE(BitcastFloat64ToInt64, BitcastFloat64ToWord64)
      UNARY_CASE(BitcastInt64ToFloat64, BitcastWord64ToFloat64)
      UNARY_CASE(ChangeUint32ToUint64, ChangeUint32ToUint64)
      UNARY_CASE(ChangeInt32ToInt64, ChangeInt32ToInt64)
      UNARY_CASE(SignExtendWord32ToInt64, ChangeInt32ToInt64)

      UNARY_CASE(ChangeFloat32ToFloat64, ChangeFloat32ToFloat64)

      UNARY_CASE(ChangeFloat64ToInt32, ReversibleFloat64ToInt32)
      UNARY_CASE(ChangeFloat64ToInt64, ReversibleFloat64ToInt64)
      UNARY_CASE(ChangeFloat64ToUint32, ReversibleFloat64ToUint32)
      UNARY_CASE(ChangeFloat64ToUint64, ReversibleFloat64ToUint64)

      UNARY_CASE(ChangeInt32ToFloat64, ChangeInt32ToFloat64)
      UNARY_CASE(ChangeInt64ToFloat64, ReversibleInt64ToFloat64)
      UNARY_CASE(ChangeUint32ToFloat64, ChangeUint32ToFloat64)

      UNARY_CASE(RoundFloat64ToInt32, TruncateFloat64ToInt32OverflowUndefined)
      UNARY_CASE(RoundInt32ToFloat32, ChangeInt32ToFloat32)
      UNARY_CASE(RoundInt64ToFloat32, ChangeInt64ToFloat32)
      UNARY_CASE(RoundInt64ToFloat64, ChangeInt64ToFloat64)
      UNARY_CASE(RoundUint32ToFloat32, ChangeUint32ToFloat32)
      UNARY_CASE(RoundUint64ToFloat32, ChangeUint64ToFloat32)
      UNARY_CASE(RoundUint64ToFloat64, ChangeUint64ToFloat64)
      UNARY_CASE(TruncateFloat64ToFloat32, TruncateFloat64ToFloat32)
      UNARY_CASE(TruncateFloat64ToUint32,
                 TruncateFloat64ToUint32OverflowUndefined)
      UNARY_CASE(TruncateFloat64ToWord32, JSTruncateFloat64ToWord32)

      UNARY_CASE(TryTruncateFloat32ToInt64, TryTruncateFloat32ToInt64)
      UNARY_CASE(TryTruncateFloat32ToUint64, TryTruncateFloat32ToUint64)
      UNARY_CASE(TryTruncateFloat64ToInt32, TryTruncateFloat64ToInt32)
      UNARY_CASE(TryTruncateFloat64ToInt64, TryTruncateFloat64ToInt64)
      UNARY_CASE(TryTruncateFloat64ToUint32, TryTruncateFloat64ToUint32)
      UNARY_CASE(TryTruncateFloat64ToUint64, TryTruncateFloat64ToUint64)

      UNARY_CASE(Float64ExtractLowWord32, Float64ExtractLowWord32)
      UNARY_CASE(Float64ExtractHighWord32, Float64ExtractHighWord32)
#undef UNARY_CASE
    case IrOpcode::kTruncateInt64ToInt32:
      return __ TruncateWord64ToWord32(Map(node->InputAt(0)));
    case IrOpcode::kTruncateFloat32ToInt32:
      switch (OpParameter<TruncateKind>(node->op())) {
        case TruncateKind::kArchitectureDefault:
          return __ TruncateFloat32ToInt32OverflowUndefined(
              Map(node->InputAt(0)));
        case TruncateKind::kSetOverflowToMin:
          return __ TruncateFloat32ToInt32OverflowToMin(Map(node->InputAt(0)));
      }
    case IrOpcode::kTruncateFloat32ToUint32:
      switch (OpParameter<TruncateKind>(node->op())) {
        case TruncateKind::kArchitectureDefault:
          return __ TruncateFloat32ToUint32OverflowUndefined(
              Map(node->InputAt(0)));
        case TruncateKind::kSetOverflowToMin:
          return __ TruncateFloat32ToUint32OverflowToMin(Map(node->InputAt(0)));
      }
    case IrOpcode::kTruncateFloat64ToInt64:
      switch (OpParameter<TruncateKind>(node->op())) {
        case TruncateKind::kArchitectureDefault:
          return __ TruncateFloat64ToInt64OverflowUndefined(
              Map(node->InputAt(0)));
        case TruncateKind::kSetOverflowToMin:
          return __ TruncateFloat64ToInt64OverflowToMin(Map(node->InputAt(0)));
      }
    case IrOpcode::kFloat64InsertLowWord32: {
      OpIndex high;
      OpIndex low = Map(node->InputAt(1));
      if (node->InputAt(0)->opcode() == IrOpcode::kFloat64InsertHighWord32) {
        // We can turn this into a single operation.
        high = Map(node->InputAt(0)->InputAt(1));
      } else {
        // We need to extract the high word to combine it.
        high = __ Float64ExtractHighWord32(Map(node->InputAt(0)));
      }
      return __ BitcastWord32PairToFloat64(high, low);
    }
    case IrOpcode::kFloat64InsertHighWord32: {
      OpIndex high = Map(node->InputAt(1));
      OpIndex low;
      if (node->InputAt(0)->opcode() == IrOpcode::kFloat64InsertLowWord32) {
        // We can turn this into a single operation.
        low = Map(node->InputAt(0)->InputAt(1));
      } else {
        // We need to extract the low word to combine it.
        low = __ Float64ExtractLowWord32(Map(node->InputAt(0)));
      }
      return __ BitcastWord32PairToFloat64(high, low);
    }
    case IrOpcode::kBitcastTaggedToWord:
      return __ BitcastTaggedToWordPtr(Map(node->InputAt(0)));
    case IrOpcode::kBitcastWordToTagged:
      return __ BitcastWordPtrToTagged(Map(node->InputAt(0)));
    case IrOpcode::kNumberIsFinite:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kFinite,
                        FloatRepresentation::Float64());
    case IrOpcode::kNumberIsInteger:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kInteger,
                        FloatRepresentation::Float64());
    case IrOpcode::kNumberIsSafeInteger:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kSafeInteger,
                        FloatRepresentation::Float64());
    case IrOpcode::kNumberIsFloat64Hole:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kFloat64Hole,
                        FloatRepresentation::Float64());
    case IrOpcode::kNumberIsMinusZero:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kMinusZero,
                        FloatRepresentation::Float64());
    case IrOpcode::kNumberIsNaN:
      return __ FloatIs(Map(node->InputAt(0)), NumericKind::kNaN,
                        FloatRepresentation::Float64());
    case IrOpcode::kObjectIsMinusZero:
      return __ ObjectIsNumericValue(Map(node->InputAt(0)),
                                     NumericKind::kMinusZero,
                                     FloatRepresentation::Float64());
    case IrOpcode::kObjectIsNaN:
      return __ ObjectIsNumericValue(Map(node->InputAt(0)), NumericKind::kNaN,
                                     FloatRepresentation::Float64());
    case IrOpcode::kObjectIsFiniteNumber:
      return __ ObjectIsNumericValue(Map(node->InputAt(0)),
                                     NumericKind::kFinite,
                                     FloatRepresentation::Float64());
    case IrOpcode::kObjectIsInteger:
      return __ ObjectIsNumericValue(Map(node->InputAt(0)),
                                     NumericKind::kInteger,
                                     FloatRepresentation::Float64());
    case IrOpcode::kObjectIsSafeInteger:
      return __ ObjectIsNumericValue(Map(node->InputAt(0)),
                                     NumericKind::kSafeInteger,
                                     FloatRepresentation::Float64());

#define OBJECT_IS_CASE(kind)                                             \
  case IrOpcode::kObjectIs##kind: {                                      \
    return __ ObjectIs(Map(node->InputAt(0)), ObjectIsOp::Kind::k##kind, \
                       ObjectIsOp::InputAssumptions::kNone);             \
  }
      OBJECT_IS_CASE(ArrayBufferView)
      OBJECT_IS_CASE(BigInt)
      OBJECT_IS_CASE(Callable)
      OBJECT_IS_CASE(Constructor)
      OBJECT_IS_CASE(DetectableCallable)
      OBJECT_IS_CASE(NonCallable)
      OBJECT_IS_CASE(Number)
      OBJECT_IS_CASE(Receiver)
      OBJECT_IS_CASE(Smi)
      OBJECT_IS_CASE(String)
      OBJECT_IS_CASE(Symbol)
      OBJECT_IS_CASE(Undetectable)
#undef OBJECT_IS_CASE

#define CHECK_OBJECT_IS_CASE(code, kind, input_assumptions, reason, feedback) \
  case IrOpcode::k##code: {                                                   \
    DCHECK(dominating_frame_state.valid());                                   \
    V<Object> input = Map(node->InputAt(0));                                  \
    V<Word32> check =                                                         \
        __ ObjectIs(input, ObjectIsOp::Kind::k##kind,                         \
                    ObjectIsOp::InputAssumptions::k##input_assumptions);      \
    __ DeoptimizeIfNot(check, dominating_frame_state,                         \
                       DeoptimizeReason::k##reason, feedback);                \
    return input;                                                             \
  }
      CHECK_OBJECT_IS_CASE(CheckInternalizedString, InternalizedString,
                           HeapObject, WrongInstanceType, {})
      CHECK_OBJECT_IS_CASE(CheckNumber, Number, None, NotANumber,
                           CheckParametersOf(op).feedback())
      CHECK_OBJECT_IS_CASE(CheckReceiver, Receiver, HeapObject,
                           NotAJavaScriptObject, {})
      CHECK_OBJECT_IS_CASE(CheckReceiverOrNullOrUndefined,
                           ReceiverOrNullOrUndefined, HeapObject,
                           NotAJavaScriptObjectOrNullOrUndefined, {})
      CHECK_OBJECT_IS_CASE(CheckString, String, HeapObject, NotAString,
                           CheckParametersOf(op).feedback())
      CHECK_OBJECT_IS_CASE(CheckStringOrStringWrapper, StringOrStringWrapper,
                           HeapObject, NotAStringOrStringWrapper,
                           CheckParametersOf(op).feedback())
      CHECK_OBJECT_IS_CASE(CheckSymbol, Symbol, HeapObject, NotASymbol, {})
      CHECK_OBJECT_IS_CASE(CheckBigInt, BigInt, None, NotABigInt,
                           CheckParametersOf(op).feedback())
      CHECK_OBJECT_IS_CASE(CheckedBigIntToBigInt64, BigInt64, BigInt,
                           NotABigInt64, CheckParametersOf(op).feedback())
#undef CHECK_OBJECT_IS_CASE

    case IrOpcode::kPlainPrimitiveToNumber:
      return __ ConvertPlainPrimitiveToNumber(Map(node->InputAt(0)));
    case IrOpcode::kPlainPrimitiveToWord32:
      return __ ConvertJSPrimitiveToUntagged(
          Map(node->InputAt(0)),
          ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kInt32,
          ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kPlainPrimitive);
    case IrOpcode::kPlainPrimitiveToFloat64:
      return __ ConvertJSPrimitiveToUntagged(
          Map(node->InputAt(0)),
          ConvertJSPrimitiveToUntaggedOp::UntaggedKind::kFloat64,
          ConvertJSPrimitiveToUntaggedOp::InputAssumptions::kPlainPrimitive);

    case IrOpcode::kConvertTaggedHoleToUndefined: {
      V<Object> input = Map(node->InputAt(0));
      V<Word32> is_the_hole = __ TaggedEqual(
          input, __ HeapConstant(isolate->factory()->the_hole_value()));
      return __ Conditional(
          is_the_hole, __ HeapConstant(isolate->factory()->undefined_value()),
          input, BranchHint::kFalse);
    }

    case IrOpcode::kConvertReceiver:
      return __ ConvertJSPrimitiveToObject(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          ConvertReceiverModeOf(node->op()));

    case IrOpcode::kToBoolean:
      return __ ConvertToBoolean(Map(node->InputAt(0)));
    case IrOpcode::kNumberToString:
      return __ ConvertNumberToString(Map(node->InputAt(0)));
    case IrOpcode::kStringToNumber:
      return __ ConvertStringToNumber(Map(node->InputAt(0)));
    case IrOpcode::kChangeTaggedToTaggedSigned:
      return __ Convert(Map(node->InputAt(0)),
                        ConvertOp::Kind::kNumberOrOddball,
                        ConvertOp::Kind::kSmi);

    case IrOpcode::kCheckedTaggedToTaggedSigned: {
      DCHECK(dominating_frame_state.valid());
      V<Object> input = Map(node->InputAt(0));
      __ DeoptimizeIfNot(__ ObjectIsSmi(input), dominating_frame_state,
                         DeoptimizeReason::kNotASmi,
                         CheckParametersOf(node->op()).feedback());
      return input;
    }

    case IrOpcode::kCheckedTaggedToTaggedPointer: {
      DCHECK(dominating_frame_state.valid());
      V<Object> input = Map(node->InputAt(0));
      __ DeoptimizeIf(__ ObjectIsSmi(input), dominating_frame_state,
                      DeoptimizeReason::kSmi,
                      CheckParametersOf(node->op()).feedback());
      return input;
    }

#define CONVERT_PRIMITIVE_TO_OBJECT_CASE(name, kind, input_type,  \
                                         input_interpretation)    \
  case IrOpcode::k##name:                                         \
    return __ ConvertUntaggedToJSPrimitive(                       \
        Map(node->InputAt(0)),                                    \
        ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::k##kind, \
        V<input_type>::rep,                                       \
        ConvertUntaggedToJSPrimitiveOp::InputInterpretation::     \
            k##input_interpretation,                              \
        CheckForMinusZeroMode::kDontCheckForMinusZero);
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeInt32ToTagged, Number, Word32,
                                       Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeUint32ToTagged, Number, Word32,
                                       Unsigned)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeInt64ToTagged, Number, Word64,
                                       Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeUint64ToTagged, Number, Word64,
                                       Unsigned)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeFloat64ToTaggedPointer, HeapNumber,
                                       Float64, Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeInt64ToBigInt, BigInt, Word64,
                                       Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeUint64ToBigInt, BigInt, Word64,
                                       Unsigned)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeInt31ToTaggedSigned, Smi, Word32,
                                       Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeBitToTagged, Boolean, Word32,
                                       Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(StringFromSingleCharCode, String, Word32,
                                       CharCode)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(StringFromSingleCodePoint, String,
                                       Word32, CodePoint)
      CONVERT_PRIMITIVE_TO_OBJECT_CASE(ChangeFloat64HoleToTagged,
                                       HeapNumberOrUndefined, Float64, Signed)

    case IrOpcode::kChangeFloat64ToTagged:
      return __ ConvertUntaggedToJSPrimitive(
          Map(node->InputAt(0)),
          ConvertUntaggedToJSPrimitiveOp::JSPrimitiveKind::kNumber,
          RegisterRepresentation::Float64(),
          ConvertUntaggedToJSPrimitiveOp::InputInterpretation::kSigned,
          CheckMinusZeroModeOf(node->op()));
#undef CONVERT_PRIMITIVE_TO_OBJECT_CASE

#define CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE(name, kind, input_type, \
                                                  input_interpretation)   \
  case IrOpcode::k##name: {                                               \
    DCHECK(dominating_frame_state.valid());                               \
    const CheckParameters& params = CheckParametersOf(node->op());        \
    return __ ConvertUntaggedToJSPrimitiveOrDeopt(                        \
        Map(node->InputAt(0)), dominating_frame_state,                    \
        ConvertUntaggedToJSPrimitiveOrDeoptOp::JSPrimitiveKind::k##kind,  \
        V<input_type>::rep,                                               \
        ConvertUntaggedToJSPrimitiveOrDeoptOp::InputInterpretation::      \
            k##input_interpretation,                                      \
        params.feedback());                                               \
  }
      CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE(CheckedInt32ToTaggedSigned, Smi,
                                                Word32, Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE(CheckedUint32ToTaggedSigned,
                                                Smi, Word32, Unsigned)
      CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE(CheckedInt64ToTaggedSigned, Smi,
                                                Word64, Signed)
      CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE(CheckedUint64ToTaggedSigned,
                                                Smi, Word64, Unsigned)
#undef CONVERT_PRIMITIVE_TO_OBJECT_OR_DEOPT_CASE

#define CONVERT_OBJECT_TO_PRIMITIVE_CASE(name, kind, input_assumptions) \
  case IrOpcode::k##name:                                               \
    return __ ConvertJSPrimitiveToUntagged(                             \
        Map(node->InputAt(0)),                                          \
        ConvertJSPrimitiveToUntaggedOp::UntaggedKind::k##kind,          \
        ConvertJSPrimitiveToUntaggedOp::InputAssumptions::              \
            k##input_assumptions);
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedSignedToInt32, Int32, Smi)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedSignedToInt64, Int64, Smi)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedToBit, Bit, Boolean)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedToInt32, Int32,
                                       NumberOrOddball)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedToUint32, Uint32,
                                       NumberOrOddball)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedToInt64, Int64,
                                       NumberOrOddball)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(ChangeTaggedToFloat64, Float64,
                                       NumberOrOddball)
      CONVERT_OBJECT_TO_PRIMITIVE_CASE(TruncateTaggedToFloat64, Float64,
                                       NumberOrOddball)
#undef CONVERT_OBJECT_TO_PRIMITIVE_CASE

#define TRUNCATE_OBJECT_TO_PRIMITIVE_CASE(name, kind, input_assumptions) \
  case IrOpcode::k##name:                                                \
    return __ TruncateJSPrimitiveToUntagged(                             \
        Map(node->InputAt(0)),                                           \
        TruncateJSPrimitiveToUntaggedOp::UntaggedKind::k##kind,          \
        TruncateJSPrimitiveToUntaggedOp::InputAssumptions::              \
            k##input_assumptions);
      TRUNCATE_OBJECT_TO_PRIMITIVE_CASE(TruncateTaggedToWord32, Int32,
                                        NumberOrOddball)
      TRUNCATE_OBJECT_TO_PRIMITIVE_CASE(TruncateBigIntToWord64, Int64, BigInt)
      TRUNCATE_OBJECT_TO_PRIMITIVE_CASE(TruncateTaggedToBit, Bit, Object)
      TRUNCATE_OBJECT_TO_PRIMITIVE_CASE(TruncateTaggedPointerToBit, Bit,
                                        HeapObject)
#undef TRUNCATE_OBJECT_TO_PRIMITIVE_CASE

    case IrOpcode::kCheckedTruncateTaggedToWord32:
      DCHECK(dominating_frame_state.valid());
      using IR = TruncateJSPrimitiveToUntaggedOrDeoptOp::InputRequirement;
      IR input_requirement;
      switch (CheckTaggedInputParametersOf(node->op()).mode()) {
        case CheckTaggedInputMode::kNumber:
          input_requirement = IR::kNumber;
          break;
        case CheckTaggedInputMode::kNumberOrBoolean:
          input_requirement = IR::kNumberOrBoolean;
          break;
        case CheckTaggedInputMode::kNumberOrOddball:
          input_requirement = IR::kNumberOrOddball;
          break;
      }
      return __ TruncateJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state,
          TruncateJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
          input_requirement,
          CheckTaggedInputParametersOf(node->op()).feedback());

#define CHANGE_OR_DEOPT_INT_CASE(kind)                                     \
  case IrOpcode::kChecked##kind: {                                         \
    DCHECK(dominating_frame_state.valid());                                \
    const CheckParameters& params = CheckParametersOf(node->op());         \
    return __ ChangeOrDeopt(Map(node->InputAt(0)), dominating_frame_state, \
                            ChangeOrDeoptOp::Kind::k##kind,                \
                            CheckForMinusZeroMode::kDontCheckForMinusZero, \
                            params.feedback());                            \
  }
      CHANGE_OR_DEOPT_INT_CASE(Uint32ToInt32)
      CHANGE_OR_DEOPT_INT_CASE(Int64ToInt32)
      CHANGE_OR_DEOPT_INT_CASE(Uint64ToInt32)
      CHANGE_OR_DEOPT_INT_CASE(Uint64ToInt64)
#undef CHANGE_OR_DEOPT_INT_CASE

    case IrOpcode::kCheckedFloat64ToInt32: {
      DCHECK(dominating_frame_state.valid());
      const CheckMinusZeroParameters& params =
          CheckMinusZeroParametersOf(node->op());
      return __ ChangeOrDeopt(Map(node->InputAt(0)), dominating_frame_state,
                              ChangeOrDeoptOp::Kind::kFloat64ToInt32,
                              params.mode(), params.feedback());
    }

    case IrOpcode::kCheckedFloat64ToInt64: {
      DCHECK(dominating_frame_state.valid());
      const CheckMinusZeroParameters& params =
          CheckMinusZeroParametersOf(node->op());
      return __ ChangeOrDeopt(Map(node->InputAt(0)), dominating_frame_state,
                              ChangeOrDeoptOp::Kind::kFloat64ToInt64,
                              params.mode(), params.feedback());
    }

    case IrOpcode::kCheckedTaggedToInt32: {
      DCHECK(dominating_frame_state.valid());
      const CheckMinusZeroParameters& params =
          CheckMinusZeroParametersOf(node->op());
      return __ ConvertJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
          params.mode(), params.feedback());
    }

    case IrOpcode::kCheckedTaggedToInt64: {
      DCHECK(dominating_frame_state.valid());
      const CheckMinusZeroParameters& params =
          CheckMinusZeroParametersOf(node->op());
      return __ ConvertJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kNumber,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt64,
          params.mode(), params.feedback());
    }

    case IrOpcode::kCheckedTaggedToFloat64: {
      DCHECK(dominating_frame_state.valid());
      const CheckTaggedInputParameters& params =
          CheckTaggedInputParametersOf(node->op());
      ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind from_kind;
      switch (params.mode()) {
#define CASE(mode)                                                       \
  case CheckTaggedInputMode::k##mode:                                    \
    from_kind =                                                          \
        ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::k##mode; \
    break;
        CASE(Number)
        CASE(NumberOrBoolean)
        CASE(NumberOrOddball)
#undef CASE
      }
      return __ ConvertJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state, from_kind,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kFloat64,
          CheckForMinusZeroMode::kDontCheckForMinusZero, params.feedback());
    }

    case IrOpcode::kCheckedTaggedToArrayIndex: {
      DCHECK(dominating_frame_state.valid());
      const CheckParameters& params = CheckParametersOf(node->op());
      return __ ConvertJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::
              kNumberOrString,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kArrayIndex,
          CheckForMinusZeroMode::kCheckForMinusZero, params.feedback());
    }

    case IrOpcode::kCheckedTaggedSignedToInt32: {
      DCHECK(dominating_frame_state.valid());
      const CheckParameters& params = CheckParametersOf(node->op());
      return __ ConvertJSPrimitiveToUntaggedOrDeopt(
          Map(node->InputAt(0)), dominating_frame_state,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::JSPrimitiveKind::kSmi,
          ConvertJSPrimitiveToUntaggedOrDeoptOp::UntaggedKind::kInt32,
          CheckForMinusZeroMode::kDontCheckForMinusZero, params.feedback());
    }

    case IrOpcode::kSelect: {
      OpIndex cond = Map(node->InputAt(0));
      OpIndex vtrue = Map(node->InputAt(1));
      OpIndex vfalse = Map(node->InputAt(2));
      const SelectParameters& params = SelectParametersOf(op);
      return __ Select(cond, vtrue, vfalse,
                       RegisterRepresentation::FromMachineRepresentation(
                           params.representation()),
                       params.hint(), SelectOp::Implementation::kBranch);
    }
    case IrOpcode::kWord32Select:
      return __ Select(Map(node->InputAt(0)), Map(node->InputAt(1)),
                       Map(node->InputAt(2)), RegisterRepresentation::Word32(),
                       BranchHint::kNone, SelectOp::Implementation::kCMove);
    case IrOpcode::kWord64Select:
      return __ Select(Map(node->InputAt(0)), Map(node->InputAt(1)),
                       Map(node->InputAt(2)), RegisterRepresentation::Word64(),
                       BranchHint::kNone, SelectOp::Implementation::kCMove);

    case IrOpcode::kLoad:
    case IrOpcode::kLoadImmutable:
    case IrOpcode::kUnalignedLoad: {
      MemoryRepresentation loaded_rep =
          MemoryRepresentation::FromMachineType(LoadRepresentationOf(op));
      Node* base = node->InputAt(0);
      Node* index = node->InputAt(1);
      // It's ok to merge LoadImmutable into Load after scheduling.
      LoadOp::Kind kind = opcode == IrOpcode::kUnalignedLoad
                              ? LoadOp::Kind::RawUnaligned()
                              : LoadOp::Kind::RawAligned();
      if (__ output_graph().Get(Map(base)).outputs_rep().at(0) ==
          RegisterRepresentation::Tagged()) {
        kind = LoadOp::Kind::TaggedBase();
      }
      if (index->opcode() == IrOpcode::kInt32Constant) {
        int32_t offset = OpParameter<int32_t>(index->op());
        if (kind.tagged_base) offset += kHeapObjectTag;
        return __ Load(Map(base), kind, loaded_rep, offset);
      }
      if (index->opcode() == IrOpcode::kInt64Constant) {
        int64_t offset = OpParameter<int64_t>(index->op());
        if (kind.tagged_base) offset += kHeapObjectTag;
        if (base::IsValueInRangeForNumericType<int32_t>(offset)) {
          return __ Load(Map(base), kind, loaded_rep,
                         static_cast<int32_t>(offset));
        }
      }
      int32_t offset = kind.tagged_base ? kHeapObjectTag : 0;
      uint8_t element_size_log2 = 0;
      return __ Load(Map(base), Map(index), kind, loaded_rep, offset,
                     element_size_log2);
    }
    case IrOpcode::kProtectedLoad: {
      MemoryRepresentation loaded_rep =
          MemoryRepresentation::FromMachineType(LoadRepresentationOf(op));
      return __ Load(Map(node->InputAt(0)), Map(node->InputAt(1)),
                     LoadOp::Kind::Protected(), loaded_rep);
    }

    case IrOpcode::kStore:
    case IrOpcode::kUnalignedStore: {
      OpIndex base = Map(node->InputAt(0));
      if (PipelineData::Get().pipeline_kind() == TurboshaftPipelineKind::kCSA) {
        // TODO(nicohartmann@): This is currently required to properly compile
        // builtins. We should fix them and remove this.
        if (__ output_graph().Get(base).outputs_rep()[0] ==
            RegisterRepresentation::Tagged()) {
          base = __ BitcastTaggedToWordPtr(base);
        }
      }
      bool aligned = opcode != IrOpcode::kUnalignedStore;
      StoreRepresentation store_rep =
          aligned ? StoreRepresentationOf(op)
                  : StoreRepresentation(UnalignedStoreRepresentationOf(op),
                                        WriteBarrierKind::kNoWriteBarrier);
      StoreOp::Kind kind = opcode == IrOpcode::kStore
                               ? StoreOp::Kind::RawAligned()
                               : StoreOp::Kind::RawUnaligned();
      bool initializing_transitioning = inside_region;

      Node* index = node->InputAt(1);
      Node* value = node->InputAt(2);
      if (index->opcode() == IrOpcode::kInt32Constant) {
        int32_t offset = OpParameter<int32_t>(index->op());
        __ Store(base, Map(value), kind,
                 MemoryRepresentation::FromMachineRepresentation(
                     store_rep.representation()),
                 store_rep.write_barrier_kind(), offset,
                 initializing_transitioning);
        return OpIndex::Invalid();
      }
      if (index->opcode() == IrOpcode::kInt64Constant) {
        int64_t offset = OpParameter<int64_t>(index->op());
        if (base::IsValueInRangeForNumericType<int32_t>(offset)) {
          __ Store(base, Map(value), kind,
                   MemoryRepresentation::FromMachineRepresentation(
                       store_rep.representation()),
                   store_rep.write_barrier_kind(), static_cast<int32_t>(offset),
                   initializing_transitioning);
          return OpIndex::Invalid();
        }
      }
      int32_t offset = 0;
      uint8_t element_size_log2 = 0;
      __ Store(base, Map(index), Map(value), kind,
               MemoryRepresentation::FromMachineRepresentation(
                   store_rep.representation()),
               store_rep.write_barrier_kind(), offset, element_size_log2,
               initializing_transitioning);
      return OpIndex::Invalid();
    }
    case IrOpcode::kProtectedStore:
      // We don't mark ProtectedStores as initialzing even when inside regions,
      // since we don't store-store eliminate them because they have a raw base.
      __ Store(Map(node->InputAt(0)), Map(node->InputAt(1)),
               Map(node->InputAt(2)), StoreOp::Kind::Protected(),
               MemoryRepresentation::FromMachineRepresentation(
                   OpParameter<MachineRepresentation>(node->op())),
               WriteBarrierKind::kNoWriteBarrier);
      return OpIndex::Invalid();

    case IrOpcode::kRetain:
      __ Retain(Map(node->InputAt(0)));
      return OpIndex::Invalid();
    case IrOpcode::kStackPointerGreaterThan:
      return __ StackPointerGreaterThan(Map(node->InputAt(0)),
                                        StackCheckKindOf(op));
    case IrOpcode::kLoadStackCheckOffset:
      return __ StackCheckOffset();
    case IrOpcode::kLoadFramePointer:
      return __ FramePointer();
    case IrOpcode::kLoadParentFramePointer:
      return __ ParentFramePointer();

    case IrOpcode::kStackSlot: {
      StackSlotRepresentation rep = StackSlotRepresentationOf(op);
      return __ StackSlot(rep.size(), rep.alignment(), rep.is_tagged());
    }
    case IrOpcode::kBranch:
      DCHECK_EQ(block->SuccessorCount(), 2);
      __ Branch(Map(node->InputAt(0)), Map(block->SuccessorAt(0)),
                Map(block->SuccessorAt(1)), BranchHintOf(node->op()));
      return OpIndex::Invalid();

    case IrOpcode::kSwitch: {
      BasicBlock* default_branch = block->successors().back();
      DCHECK_EQ(IrOpcode::kIfDefault, default_branch->front()->opcode());
      size_t case_count = block->SuccessorCount() - 1;
      base::SmallVector<SwitchOp::Case, 16> cases;
      for (size_t i = 0; i < case_count; ++i) {
        BasicBlock* branch = block->SuccessorAt(i);
        const IfValueParameters& p = IfValueParametersOf(branch->front()->op());
        cases.emplace_back(p.value(), Map(branch), p.hint());
      }
      __ Switch(
          Map(node->InputAt(0)), graph_zone->CloneVector(base::VectorOf(cases)),
          Map(default_branch), BranchHintOf(default_branch->front()->op()));
      return OpIndex::Invalid();
    }

    case IrOpcode::kCall: {
      auto call_descriptor = CallDescriptorOf(op);
      base::SmallVector<OpIndex, 16> arguments;
      // The input `0` is the callee, the following value inputs are the
      // arguments. `CallDescriptor::InputCount()` counts the callee and
      // arguments, but excludes a possible `FrameState` input.
      OpIndex callee = Map(node->InputAt(0));
      for (int i = 1; i < static_cast<int>(call_descriptor->InputCount());
           ++i) {
        arguments.emplace_back(Map(node->InputAt(i)));
      }
      CanThrow can_throw =
          op->HasProperty(Operator::kNoThrow) ? CanThrow::kNo : CanThrow::kYes;
      const TSCallDescriptor* ts_descriptor =
          TSCallDescriptor::Create(call_descriptor, can_throw, graph_zone);

      OpIndex frame_state_idx = OpIndex::Invalid();
      if (call_descriptor->NeedsFrameState()) {
        FrameState frame_state{
            node->InputAt(static_cast<int>(call_descriptor->InputCount()))};
        frame_state_idx = Map(frame_state);
      }
      base::Optional<decltype(assembler)::CatchScope> catch_scope;
      if (is_final_control) {
        Block* catch_block = Map(block->SuccessorAt(1));
        catch_scope.emplace(assembler, catch_block);
      }
      OpEffects effects =
          OpEffects().CanDependOnChecks().CanChangeControlFlow().CanDeopt();
      if ((call_descriptor->flags() & CallDescriptor::kNoAllocate) == 0) {
        effects = effects.CanAllocate();
      }
      if (!op->HasProperty(Operator::kNoWrite)) {
        effects = effects.CanWriteMemory();
      }
      if (!op->HasProperty(Operator::kNoRead)) {
        effects = effects.CanReadMemory();
      }
      OpIndex result =
          __ Call(callee, frame_state_idx, base::VectorOf(arguments),
                  ts_descriptor, effects);
      if (is_final_control) {
        // The `__ Call()` before has already created exceptional control flow
        // and bound a new block for the success case. So we can just `Goto` the
        // block that Turbofan designated as the `IfSuccess` successor.
        __ Goto(Map(block->SuccessorAt(0)));
      }
      return result;
    }

    case IrOpcode::kTailCall: {
      auto call_descriptor = CallDescriptorOf(op);
      base::SmallVector<OpIndex, 16> arguments;
      // The input `0` is the callee, the following value inputs are the
      // arguments. `CallDescriptor::InputCount()` counts the callee and
      // arguments.
      OpIndex callee = Map(node->InputAt(0));
      for (int i = 1; i < static_cast<int>(call_descriptor->InputCount());
           ++i) {
        arguments.emplace_back(Map(node->InputAt(i)));
      }

      CanThrow can_throw =
          op->HasProperty(Operator::kNoThrow) ? CanThrow::kNo : CanThrow::kYes;
      const TSCallDescriptor* ts_descriptor =
          TSCallDescriptor::Create(call_descriptor, can_throw, graph_zone);

      __ TailCall(callee, base::VectorOf(arguments), ts_descriptor);
      return OpIndex::Invalid();
    }

    case IrOpcode::kFrameState: {
      FrameState frame_state{node};
      FrameStateData::Builder builder;
      BuildFrameStateData(&builder, frame_state);
      if (builder.Inputs().size() >
          std::numeric_limits<decltype(Operation::input_count)>::max() - 1) {
        *bailout = BailoutReason::kTooManyArguments;
        return OpIndex::Invalid();
      }
      return __ FrameState(builder.Inputs(), builder.inlined(),
                           builder.AllocateFrameStateData(
                               frame_state.frame_state_info(), graph_zone));
    }

    case IrOpcode::kDeoptimizeIf:
      __ DeoptimizeIf(Map(node->InputAt(0)), Map(node->InputAt(1)),
                      &DeoptimizeParametersOf(op));
      return OpIndex::Invalid();
    case IrOpcode::kDeoptimizeUnless:
      __ DeoptimizeIfNot(Map(node->InputAt(0)), Map(node->InputAt(1)),
                         &DeoptimizeParametersOf(op));
      return OpIndex::Invalid();

#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kTrapIf:
      // For wasm the dominating_frame_state is invalid and will not be used.
      // For traps inlined into JS the dominating_frame_state is valid and is
      // needed for the trap.
      __ TrapIf(Map(node->InputAt(0)), dominating_frame_state, TrapIdOf(op));
      return OpIndex::Invalid();

    case IrOpcode::kTrapUnless:
      // For wasm the dominating_frame_state is invalid and will not be used.
      // For traps inlined into JS the dominating_frame_state is valid and is
      // needed for the trap.
      __ TrapIfNot(Map(node->InputAt(0)), dominating_frame_state, TrapIdOf(op));
      return OpIndex::Invalid();
#endif  // V8_ENABLE_WEBASSEMBLY

    case IrOpcode::kDeoptimize: {
      OpIndex frame_state = Map(node->InputAt(0));
      __ Deoptimize(frame_state, &DeoptimizeParametersOf(op));
      return OpIndex::Invalid();
    }

    case IrOpcode::kReturn: {
      Node* pop_count = node->InputAt(0);
      base::SmallVector<OpIndex, 4> return_values;
      for (int i = 1; i < node->op()->ValueInputCount(); ++i) {
        return_values.push_back(Map(node->InputAt(i)));
      }
      __ Return(Map(pop_count), base::VectorOf(return_values));
      return OpIndex::Invalid();
    }
    case IrOpcode::kUnreachable:
    case IrOpcode::kThrow:
      __ Unreachable();
      return OpIndex::Invalid();

    case IrOpcode::kDeadValue:
      // Typically, DeadValue nodes have Unreachable as their input. In this
      // case, we would not get here because Unreachable already terminated the
      // block and we stopped generating additional operations.
      DCHECK_NE(node->InputAt(0)->opcode(), IrOpcode::kUnreachable);
      // If we find a DeadValue without an Unreachable input, we just generate
      // one here and stop.
      __ Unreachable();
      return OpIndex::Invalid();

    case IrOpcode::kProjection: {
      Node* input = node->InputAt(0);
      size_t index = ProjectionIndexOf(op);
      RegisterRepresentation rep =
          RegisterRepresentation::FromMachineRepresentation(
              NodeProperties::GetProjectionType(node));
      return __ Projection(Map(input), index, rep);
    }

    case IrOpcode::kStaticAssert: {
      if (V8_UNLIKELY(PipelineData::Get().pipeline_kind() ==
                      TurboshaftPipelineKind::kCSA)) {
        // TODO(nicohartmann@): CSA code contains some static asserts (even in
        // release builds) that we cannot prove currently, so we skip them here
        // for now.
        return OpIndex::Invalid();
      }
      __ StaticAssert(Map(node->InputAt(0)), StaticAssertSourceOf(node->op()));
      return OpIndex::Invalid();
    }

    case IrOpcode::kAllocate: {
      AllocationType allocation = AllocationTypeOf(node->op());
      return __ FinishInitialization(
          __ Allocate(Map(node->InputAt(0)), allocation));
    }
    // TODO(nicohartmann@): We might not see AllocateRaw here anymore.
    case IrOpcode::kAllocateRaw: {
      Node* size = node->InputAt(0);
      const AllocateParameters& params = AllocateParametersOf(node->op());
      return __ FinishInitialization(
          __ Allocate(Map(size), params.allocation_type()));
    }
    case IrOpcode::kStoreToObject: {
      Node* object = node->InputAt(0);
      Node* offset = node->InputAt(1);
      Node* value = node->InputAt(2);
      ObjectAccess const& access = ObjectAccessOf(node->op());
      bool initializing_transitioning = inside_region;
      __ Store(Map(object), Map(offset), Map(value),
               StoreOp::Kind::TaggedBase(),
               MemoryRepresentation::FromMachineType(access.machine_type),
               access.write_barrier_kind, kHeapObjectTag,
               initializing_transitioning);
      return OpIndex::Invalid();
    }
    case IrOpcode::kStoreElement: {
      Node* object = node->InputAt(0);
      Node* index = node->InputAt(1);
      Node* value = node->InputAt(2);
      ElementAccess const& access = ElementAccessOf(node->op());
      DCHECK(!access.machine_type.IsMapWord());
      StoreOp::Kind kind = StoreOp::Kind::Aligned(access.base_is_tagged);
      MemoryRepresentation rep =
          MemoryRepresentation::FromMachineType(access.machine_type);
      bool initializing_transitioning = inside_region;
      __ Store(Map(object), Map(index), Map(value), kind, rep,
               access.write_barrier_kind, access.header_size,
               rep.SizeInBytesLog2(), initializing_transitioning);
      return OpIndex::Invalid();
    }
    case IrOpcode::kStoreField: {
      OpIndex object = Map(node->InputAt(0));
      OpIndex value = Map(node->InputAt(1));
      FieldAccess const& access = FieldAccessOf(node->op());
      // External pointer must never be stored by optimized code.
      DCHECK(!access.type.Is(compiler::Type::ExternalPointer()) ||
             !V8_ENABLE_SANDBOX_BOOL);
      // SandboxedPointers are not currently stored by optimized code.
      DCHECK(!access.type.Is(compiler::Type::SandboxedPointer()));

#ifdef V8_ENABLE_SANDBOX
      if (access.is_bounded_size_access) {
        value = __ ShiftLeft(value, kBoundedSizeShift,
                             WordRepresentation::WordPtr());
      }
#endif  // V8_ENABLE_SANDBOX

      StoreOp::Kind kind = StoreOp::Kind::Aligned(access.base_is_tagged);
      MachineType machine_type = access.machine_type;
      if (machine_type.IsMapWord()) {
        machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
        UNIMPLEMENTED();
#endif
      }

      bool initializing_transitioning =
          access.maybe_initializing_or_transitioning_store;
      if (!inside_region) {
        // Mark stores outside a region as non-initializing and
        // non-transitioning.
        initializing_transitioning = false;
      }

      MemoryRepresentation rep =
          MemoryRepresentation::FromMachineType(machine_type);

      __ Store(object, value, kind, rep, access.write_barrier_kind,
               access.offset, initializing_transitioning,
               access.indirect_pointer_tag);
      return OpIndex::Invalid();
    }
    case IrOpcode::kLoadFromObject:
    case IrOpcode::kLoadImmutableFromObject: {
      Node* object = node->InputAt(0);
      Node* offset = node->InputAt(1);
      ObjectAccess const& access = ObjectAccessOf(node->op());
      MemoryRepresentation rep =
          MemoryRepresentation::FromMachineType(access.machine_type);
      return __ Load(Map(object), Map(offset), LoadOp::Kind::TaggedBase(), rep,
                     kHeapObjectTag);
    }
    case IrOpcode::kLoadField: {
      Node* object = node->InputAt(0);
      FieldAccess const& access = FieldAccessOf(node->op());
      StoreOp::Kind kind = StoreOp::Kind::Aligned(access.base_is_tagged);
      MachineType machine_type = access.machine_type;
      if (machine_type.IsMapWord()) {
        machine_type = MachineType::TaggedPointer();
#ifdef V8_MAP_PACKING
        UNIMPLEMENTED();
#endif
      }
      MemoryRepresentation rep =
          MemoryRepresentation::FromMachineType(machine_type);
#ifdef V8_ENABLE_SANDBOX
      bool is_sandboxed_external =
          access.type.Is(compiler::Type::ExternalPointer());
      if (is_sandboxed_external) {
        // Fields for sandboxed external pointer contain a 32-bit handle, not a
        // 64-bit raw pointer.
        rep = MemoryRepresentation::Uint32();
      }
#endif  // V8_ENABLE_SANDBOX
      OpIndex value = __ Load(Map(object), kind, rep, access.offset);
#ifdef V8_ENABLE_SANDBOX
      if (is_sandboxed_external) {
        value = __ DecodeExternalPointer(value, access.external_pointer_tag);
      }
      if (access.is_bounded_size_access) {
        DCHECK(!is_sandboxed_external);
        value = __ ShiftRightLogical(value, kBoundedSizeShift,
                                     WordRepresentation::WordPtr());
      }
#endif  // V8_ENABLE_SANDBOX
      return value;
    }
    case IrOpcode::kLoadElement: {
      Node* object = node->InputAt(0);
      Node* index = node->InputAt(1);
      ElementAccess const& access = ElementAccessOf(node->op());
      LoadOp::Kind kind = LoadOp::Kind::Aligned(access.base_is_tagged);
      MemoryRepresentation rep =
          MemoryRepresentation::FromMachineType(access.machine_type);
      return __ Load(Map(object), Map(index), kind, rep, access.header_size,
                     rep.SizeInBytesLog2());
    }
    case IrOpcode::kCheckTurboshaftTypeOf: {
      Node* input = node->InputAt(0);
      Node* type_description = node->InputAt(1);

      HeapObjectMatcher m(type_description);
      CHECK(m.HasResolvedValue() && m.Ref(broker).IsString() &&
            m.Ref(broker).AsString().IsContentAccessible());
      StringRef type_string = m.Ref(broker).AsString();
      Handle<String> pattern_string =
          *type_string.ObjectIfContentAccessible(broker);
      std::unique_ptr<char[]> pattern = pattern_string->ToCString();

      auto type_opt =
          Type::ParseFromString(std::string_view{pattern.get()}, graph_zone);
      if (type_opt == base::nullopt) {
        FATAL(
            "String '%s' (of %d:CheckTurboshaftTypeOf) is not a valid type "
            "description!",
            pattern.get(), node->id());
      }

      OpIndex input_index = Map(input);
      RegisterRepresentation rep =
          __ output_graph().Get(input_index).outputs_rep()[0];
      return __ CheckTurboshaftTypeOf(input_index, rep, *type_opt, false);
    }

    case IrOpcode::kNewConsString:
      return __ NewConsString(Map(node->InputAt(0)), Map(node->InputAt(1)),
                              Map(node->InputAt(2)));
    case IrOpcode::kNewDoubleElements:
      return __ NewArray(Map(node->InputAt(0)), NewArrayOp::Kind::kDouble,
                         AllocationTypeOf(node->op()));
    case IrOpcode::kNewSmiOrObjectElements:
      return __ NewArray(Map(node->InputAt(0)), NewArrayOp::Kind::kObject,
                         AllocationTypeOf(node->op()));

    case IrOpcode::kDoubleArrayMin:
      return __ DoubleArrayMinMax(Map(node->InputAt(0)),
                                  DoubleArrayMinMaxOp::Kind::kMin);
    case IrOpcode::kDoubleArrayMax:
      return __ DoubleArrayMinMax(Map(node->InputAt(0)),
                                  DoubleArrayMinMaxOp::Kind::kMax);

    case IrOpcode::kLoadFieldByIndex:
      return __ LoadFieldByIndex(Map(node->InputAt(0)), Map(node->InputAt(1)));

    case IrOpcode::kCheckedInt64Add:
      DCHECK(Is64());
      DCHECK(dominating_frame_state.valid());
      return __ Word64SignedAddDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt64Sub:
      DCHECK(Is64());
      DCHECK(dominating_frame_state.valid());
      return __ Word64SignedSubDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt32Add:
      DCHECK(dominating_frame_state.valid());
      return __ Word32SignedAddDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt32Sub:
      DCHECK(dominating_frame_state.valid());
      return __ Word32SignedSubDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt32Mul: {
      DCHECK(dominating_frame_state.valid());
      CheckForMinusZeroMode mode = CheckMinusZeroModeOf(node->op());
      return __ Word32SignedMulDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{}, mode);
    }

    case IrOpcode::kCheckedInt64Mul:
      DCHECK(Is64());
      DCHECK(dominating_frame_state.valid());
      return __ Word64SignedMulDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt32Div:
      DCHECK(dominating_frame_state.valid());
      return __ Word32SignedDivDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt64Div:
      DCHECK(Is64());
      DCHECK(dominating_frame_state.valid());
      return __ Word64SignedDivDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedUint32Div:
      DCHECK(dominating_frame_state.valid());
      return __ Word32UnsignedDivDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt32Mod:
      DCHECK(dominating_frame_state.valid());
      return __ Word32SignedModDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedInt64Mod:
      DCHECK(Is64());
      DCHECK(dominating_frame_state.valid());
      return __ Word64SignedModDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

    case IrOpcode::kCheckedUint32Mod:
      DCHECK(dominating_frame_state.valid());
      return __ Word32UnsignedModDeoptOnOverflow(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          FeedbackSource{});

#define BIGINT_BINOP_CASE(op, kind)                                     \
  case IrOpcode::kBigInt##op:                                           \
    DCHECK(dominating_frame_state.valid());                             \
    return __ BigIntBinop(Map(node->InputAt(0)), Map(node->InputAt(1)), \
                          dominating_frame_state,                       \
                          BigIntBinopOp::Kind::k##kind);
      BIGINT_BINOP_CASE(Add, Add)
      BIGINT_BINOP_CASE(Subtract, Sub)
      BIGINT_BINOP_CASE(Multiply, Mul)
      BIGINT_BINOP_CASE(Divide, Div)
      BIGINT_BINOP_CASE(Modulus, Mod)
      BIGINT_BINOP_CASE(BitwiseAnd, BitwiseAnd)
      BIGINT_BINOP_CASE(BitwiseOr, BitwiseOr)
      BIGINT_BINOP_CASE(BitwiseXor, BitwiseXor)
      BIGINT_BINOP_CASE(ShiftLeft, ShiftLeft)
      BIGINT_BINOP_CASE(ShiftRight, ShiftRightArithmetic)
#undef BIGINT_BINOP_CASE

    case IrOpcode::kBigIntEqual:
      return __ BigIntEqual(Map(node->InputAt(0)), Map(node->InputAt(1)));

    case IrOpcode::kBigIntLessThan:
      return __ BigIntLessThan(Map(node->InputAt(0)), Map(node->InputAt(1)));
    case IrOpcode::kBigIntLessThanOrEqual:
      return __ BigIntLessThanOrEqual(Map(node->InputAt(0)),
                                      Map(node->InputAt(1)));

    case IrOpcode::kBigIntNegate:
      return __ BigIntNegate(Map(node->InputAt(0)));

    case IrOpcode::kLoadRootRegister:
      // Inlined usage of wasm root register operation in JS.
      return assembler.ReduceLoadRootRegister();

    case IrOpcode::kStringCharCodeAt:
      return __ StringCharCodeAt(Map(node->InputAt(0)), Map(node->InputAt(1)));
    case IrOpcode::kStringCodePointAt:
      return __ StringCodePointAt(Map(node->InputAt(0)), Map(node->InputAt(1)));

#ifdef V8_INTL_SUPPORT
    case IrOpcode::kStringToLowerCaseIntl:
      return __ StringToLowerCaseIntl(Map(node->InputAt(0)));
    case IrOpcode::kStringToUpperCaseIntl:
      return __ StringToUpperCaseIntl(Map(node->InputAt(0)));
#else
    case IrOpcode::kStringToLowerCaseIntl:
    case IrOpcode::kStringToUpperCaseIntl:
      UNREACHABLE();
#endif  // V8_INTL_SUPPORT

    case IrOpcode::kStringLength:
      return __ StringLength(Map(node->InputAt(0)));

    case IrOpcode::kStringIndexOf:
      return __ StringIndexOf(Map(node->InputAt(0)), Map(node->InputAt(1)),
                              Map(node->InputAt(2)));

    case IrOpcode::kStringFromCodePointAt:
      return __ StringFromCodePointAt(Map(node->InputAt(0)),
                                      Map(node->InputAt(1)));

    case IrOpcode::kStringSubstring:
      return __ StringSubstring(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                Map(node->InputAt(2)));

    case IrOpcode::kStringConcat:
      // We don't need node->InputAt(0) here.
      return __ StringConcat(Map(node->InputAt(1)), Map(node->InputAt(2)));

    case IrOpcode::kStringEqual:
      return __ StringEqual(Map(node->InputAt(0)), Map(node->InputAt(1)));
    case IrOpcode::kStringLessThan:
      return __ StringLessThan(Map(node->InputAt(0)), Map(node->InputAt(1)));
    case IrOpcode::kStringLessThanOrEqual:
      return __ StringLessThanOrEqual(Map(node->InputAt(0)),
                                      Map(node->InputAt(1)));

    case IrOpcode::kArgumentsLength:
      return __ ArgumentsLength();
    case IrOpcode::kRestLength:
      return __ RestLength(FormalParameterCountOf(node->op()));

    case IrOpcode::kNewArgumentsElements: {
      const auto& p = NewArgumentsElementsParametersOf(node->op());
      // EffectControlLinearizer used to use `node->op()->properties()` to
      // construct the builtin call descriptor for this operation. However, this
      // always seemed to be `kEliminatable` so the Turboshaft
      // BuiltinCallDescriptor's for those builtins have this property
      // hard-coded.
      DCHECK_EQ(node->op()->properties(), Operator::kEliminatable);
      return __ NewArgumentsElements(Map(node->InputAt(0)), p.arguments_type(),
                                     p.formal_parameter_count());
    }

    case IrOpcode::kLoadTypedElement:
      return __ LoadTypedElement(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                 Map(node->InputAt(2)), Map(node->InputAt(3)),
                                 ExternalArrayTypeOf(node->op()));
    case IrOpcode::kLoadDataViewElement:
      return __ LoadDataViewElement(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          Map(node->InputAt(3)), ExternalArrayTypeOf(node->op()));
    case IrOpcode::kLoadStackArgument:
      return __ LoadStackArgument(Map(node->InputAt(0)), Map(node->InputAt(1)));

    case IrOpcode::kStoreTypedElement:
      __ StoreTypedElement(Map(node->InputAt(0)), Map(node->InputAt(1)),
                           Map(node->InputAt(2)), Map(node->InputAt(3)),
                           Map(node->InputAt(4)),
                           ExternalArrayTypeOf(node->op()));
      return OpIndex::Invalid();
    case IrOpcode::kStoreDataViewElement:
      __ StoreDataViewElement(Map(node->InputAt(0)), Map(node->InputAt(1)),
                              Map(node->InputAt(2)), Map(node->InputAt(3)),
                              Map(node->InputAt(4)),
                              ExternalArrayTypeOf(node->op()));
      return OpIndex::Invalid();
    case IrOpcode::kTransitionAndStoreElement:
      __ TransitionAndStoreArrayElement(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          TransitionAndStoreArrayElementOp::Kind::kElement,
          FastMapParameterOf(node->op()).object(),
          DoubleMapParameterOf(node->op()).object());
      return OpIndex::Invalid();
    case IrOpcode::kTransitionAndStoreNumberElement:
      __ TransitionAndStoreArrayElement(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          TransitionAndStoreArrayElementOp::Kind::kNumberElement, {},
          DoubleMapParameterOf(node->op()).object());
      return OpIndex::Invalid();
    case IrOpcode::kTransitionAndStoreNonNumberElement: {
      auto kind =
          ValueTypeParameterOf(node->op())
                  .Is(compiler::Type::BooleanOrNullOrUndefined())
              ? TransitionAndStoreArrayElementOp::Kind::kOddballElement
              : TransitionAndStoreArrayElementOp::Kind::kNonNumberElement;
      __ TransitionAndStoreArrayElement(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          kind, FastMapParameterOf(node->op()).object(), {});
      return OpIndex::Invalid();
    }
    case IrOpcode::kStoreSignedSmallElement:
      __ StoreSignedSmallElement(Map(node->InputAt(0)), Map(node->InputAt(1)),
                                 Map(node->InputAt(2)));
      return OpIndex::Invalid();

    case IrOpcode::kCompareMaps: {
      const ZoneRefSet<v8::internal::Map>& maps =
          CompareMapsParametersOf(node->op());
      return __ CompareMaps(Map(node->InputAt(0)), maps);
    }

    case IrOpcode::kCheckMaps: {
      DCHECK(dominating_frame_state.valid());
      const auto& p = CheckMapsParametersOf(node->op());
      __ CheckMaps(Map(node->InputAt(0)), dominating_frame_state, p.maps(),
                   p.flags(), p.feedback());
      return OpIndex{};
    }

    case IrOpcode::kCheckedUint32Bounds:
    case IrOpcode::kCheckedUint64Bounds: {
      WordRepresentation rep = node->opcode() == IrOpcode::kCheckedUint32Bounds
                                   ? WordRepresentation::Word32()
                                   : WordRepresentation::Word64();
      const CheckBoundsParameters& params = CheckBoundsParametersOf(node->op());
      OpIndex index = Map(node->InputAt(0));
      OpIndex limit = Map(node->InputAt(1));
      V<Word32> check = __ UintLessThan(index, limit, rep);
      if ((params.flags() & CheckBoundsFlag::kAbortOnOutOfBounds) != 0) {
        IF_NOT(LIKELY(check)) { __ Unreachable(); }

      } else {
        DCHECK(dominating_frame_state.valid());
        __ DeoptimizeIfNot(check, dominating_frame_state,
                           DeoptimizeReason::kOutOfBounds,
                           params.check_parameters().feedback());
      }
      return index;
    }

    case IrOpcode::kCheckIf: {
      DCHECK(dominating_frame_state.valid());
      const CheckIfParameters& params = CheckIfParametersOf(node->op());
      __ DeoptimizeIfNot(Map(node->InputAt(0)), dominating_frame_state,
                         params.reason(), params.feedback());
      return OpIndex::Invalid();
    }

    case IrOpcode::kCheckClosure:
      DCHECK(dominating_frame_state.valid());
      return __ CheckedClosure(Map(node->InputAt(0)), dominating_frame_state,
                               FeedbackCellOf(node->op()));

    case IrOpcode::kCheckEqualsSymbol:
      DCHECK(dominating_frame_state.valid());
      __ DeoptimizeIfNot(
          __ TaggedEqual(Map(node->InputAt(0)), Map(node->InputAt(1))),
          dominating_frame_state, DeoptimizeReason::kWrongName,
          FeedbackSource{});
      return OpIndex::Invalid();

    case IrOpcode::kCheckEqualsInternalizedString:
      DCHECK(dominating_frame_state.valid());
      __ CheckEqualsInternalizedString(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state);
      return OpIndex::Invalid();

    case IrOpcode::kCheckFloat64Hole: {
      DCHECK(dominating_frame_state.valid());
      V<Float64> value = Map(node->InputAt(0));
      // TODO(tebbi): If we did partial block cloning, we could emit a
      // `DeoptimizeIf` operation here. Alternatively, we could use a branch and
      // a separate block with an unconditional `Deoptimize`.
      return __ ChangeOrDeopt(
          value, dominating_frame_state, ChangeOrDeoptOp::Kind::kFloat64NotHole,
          CheckForMinusZeroMode::kDontCheckForMinusZero,
          CheckFloat64HoleParametersOf(node->op()).feedback());
    }

    case IrOpcode::kCheckNotTaggedHole: {
      DCHECK(dominating_frame_state.valid());
      V<Object> value = Map(node->InputAt(0));
      __ DeoptimizeIf(
          __ TaggedEqual(value,
                         __ HeapConstant(isolate->factory()->the_hole_value())),
          dominating_frame_state, DeoptimizeReason::kHole, FeedbackSource{});
      return value;
    }

    case IrOpcode::kLoadMessage:
      return __ LoadMessage(Map(node->InputAt(0)));
    case IrOpcode::kStoreMessage:
      __ StoreMessage(Map(node->InputAt(0)), Map(node->InputAt(1)));
      return OpIndex::Invalid();

    case IrOpcode::kSameValue:
      return __ SameValue(Map(node->InputAt(0)), Map(node->InputAt(1)),
                          SameValueOp::Mode::kSameValue);
    case IrOpcode::kSameValueNumbersOnly:
      return __ SameValue(Map(node->InputAt(0)), Map(node->InputAt(1)),
                          SameValueOp::Mode::kSameValueNumbersOnly);
    case IrOpcode::kNumberSameValue:
      return __ Float64SameValue(Map(node->InputAt(0)), Map(node->InputAt(1)));

    case IrOpcode::kTypeOf:
      return __ CallBuiltin_Typeof(isolate, Map(node->InputAt(0)));

    case IrOpcode::kFastApiCall: {
      DCHECK(dominating_frame_state.valid());
      FastApiCallNode n(node);
      const auto& params = n.Parameters();
      const FastApiCallFunctionVector& c_functions = params.c_functions();
      const int c_arg_count = params.argument_count();

      base::SmallVector<OpIndex, 16> slow_call_arguments;
      DCHECK_EQ(node->op()->ValueInputCount() - c_arg_count,
                n.SlowCallArgumentCount());
      OpIndex slow_call_callee = Map(n.SlowCallArgument(0));
      for (int i = 1; i < n.SlowCallArgumentCount(); ++i) {
        slow_call_arguments.push_back(Map(n.SlowCallArgument(i)));
      }

      // Overload resolution.
      auto resolution_result =
          fast_api_call::OverloadsResolutionResult::Invalid();
      if (c_functions.size() != 1) {
        DCHECK_EQ(c_functions.size(), 2);
        resolution_result =
            fast_api_call::ResolveOverloads(c_functions, c_arg_count);
        if (!resolution_result.is_valid()) {
          return __ Call(
              slow_call_callee, dominating_frame_state,
              base::VectorOf(slow_call_arguments),
              TSCallDescriptor::Create(params.descriptor(), CanThrow::kYes,
                                       __ graph_zone()));
        }
      }

      // Prepare FastCallApiOp parameters.
      base::SmallVector<OpIndex, 16> arguments;
      for (int i = 0; i < c_arg_count; ++i) {
        arguments.push_back(Map(NodeProperties::GetValueInput(node, i)));
      }
      OpIndex data_argument =
          Map(n.SlowCallArgument(FastApiCallNode::kSlowCallDataArgumentIndex));
      const FastApiCallParameters* parameters = FastApiCallParameters::Create(
          c_functions, resolution_result, __ graph_zone());

      Label<Object> done(this);

      OpIndex fast_call_result =
          __ FastApiCall(data_argument, base::VectorOf(arguments), parameters);
      V<Word32> result_state =
          __ template Projection<Word32>(fast_call_result, 0);

      IF (LIKELY(__ Word32Equal(result_state, FastApiCallOp::kSuccessValue))) {
        GOTO(done, __ template Projection<Object>(fast_call_result, 1));
      } ELSE {
        // We need to generate a fallback (both fast and slow call) in case:
        // 1) the generated code might fail, in case e.g. a Smi was passed where
        // a JSObject was expected and an error must be thrown or
        // 2) the embedder requested fallback possibility via providing options
        // arg. None of the above usually holds true for Wasm functions with
        // primitive types only, so we avoid generating an extra branch here.
        V<Object> slow_call_result =
            __ Call(slow_call_callee, dominating_frame_state,
                    base::VectorOf(slow_call_arguments),
                    TSCallDescriptor::Create(params.descriptor(),
                                             CanThrow::kYes, __ graph_zone()));
        GOTO(done, slow_call_result);
      }

      BIND(done, result);
      return result;
    }

    case IrOpcode::kRuntimeAbort:
      __ RuntimeAbort(AbortReasonOf(node->op()));
      return OpIndex::Invalid();

    case IrOpcode::kDateNow:
      return __ CallRuntime_DateCurrentTime(isolate, __ NoContextConstant());

    case IrOpcode::kEnsureWritableFastElements:
      return __ EnsureWritableFastElements(Map(node->InputAt(0)),
                                           Map(node->InputAt(1)));

    case IrOpcode::kMaybeGrowFastElements: {
      DCHECK(dominating_frame_state.valid());
      const GrowFastElementsParameters& params =
          GrowFastElementsParametersOf(node->op());
      return __ MaybeGrowFastElements(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          Map(node->InputAt(3)), dominating_frame_state, params.mode(),
          params.feedback());
    }

    case IrOpcode::kTransitionElementsKind:
      __ TransitionElementsKind(Map(node->InputAt(0)),
                                ElementsTransitionOf(node->op()));
      return OpIndex::Invalid();

    case IrOpcode::kAssertType: {
      compiler::Type type = OpParameter<compiler::Type>(node->op());
      CHECK(type.CanBeAsserted());
      V<TurbofanType> allocated_type;
      {
        DCHECK(isolate->CurrentLocalHeap()->is_main_thread());
        base::Optional<UnparkedScope> unparked_scope;
        if (isolate->CurrentLocalHeap()->IsParked()) {
          unparked_scope.emplace(isolate->main_thread_local_isolate());
        }
        allocated_type =
            __ HeapConstant(type.AllocateOnHeap(isolate->factory()));
      }
      __ CallBuiltin_CheckTurbofanType(isolate, __ NoContextConstant(),
                                       Map(node->InputAt(0)), allocated_type,
                                       __ TagSmi(node->id()));
      return OpIndex::Invalid();
    }

    case IrOpcode::kFindOrderedHashMapEntry:
      return __ FindOrderedHashMapEntry(Map(node->InputAt(0)),
                                        Map(node->InputAt(1)));
    case IrOpcode::kFindOrderedHashSetEntry:
      return __ FindOrderedHashSetEntry(Map(node->InputAt(0)),
                                        Map(node->InputAt(1)));
    case IrOpcode::kFindOrderedHashMapEntryForInt32Key:
      return __ FindOrderedHashMapEntryForInt32Key(Map(node->InputAt(0)),
                                                   Map(node->InputAt(1)));

    case IrOpcode::kSpeculativeSafeIntegerAdd:
      DCHECK(dominating_frame_state.valid());
      return __ SpeculativeNumberBinop(
          Map(node->InputAt(0)), Map(node->InputAt(1)), dominating_frame_state,
          SpeculativeNumberBinopOp::Kind::kSafeIntegerAdd);

    case IrOpcode::kBeginRegion:
      inside_region = true;
      return OpIndex::Invalid();
    case IrOpcode::kFinishRegion:
      inside_region = false;
      return Map(node->InputAt(0));

    case IrOpcode::kTypeGuard:
      return Map(node->InputAt(0));

    case IrOpcode::kAbortCSADcheck:
      // TODO(nicohartmann@):
      return OpIndex::Invalid();

    case IrOpcode::kDebugBreak:
      __ DebugBreak();
      return OpIndex::Invalid();

    case IrOpcode::kComment:
      __ Comment(OpParameter<const char*>(node->op()));
      return OpIndex::Invalid();

    case IrOpcode::kAssert: {
      const AssertParameters& p = AssertParametersOf(node->op());
      __ AssertImpl(Map(node->InputAt(0)), p.condition_string(), p.file(),
                    p.line());
      return OpIndex::Invalid();
    }

    case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits:
      // Currently this is only used by the CSA pipeline.
      DCHECK_EQ(PipelineData::Get().pipeline_kind(),
                TurboshaftPipelineKind::kCSA);
      return __ BitcastTaggedToWordPtrForTagAndSmiBits(Map(node->InputAt(0)));
    case IrOpcode::kBitcastWordToTaggedSigned:
      return __ BitcastWordPtrToSmi(Map(node->InputAt(0)));

    case IrOpcode::kWord32AtomicLoad:
    case IrOpcode::kWord64AtomicLoad: {
      OpIndex base = Map(node->InputAt(0));
      OpIndex offset = Map(node->InputAt(1));
      const AtomicLoadParameters& p = AtomicLoadParametersOf(node->op());
      DCHECK_EQ(__ output_graph().Get(base).outputs_rep()[0],
                RegisterRepresentation::WordPtr());
      LoadOp::Kind kind;
      switch (p.kind()) {
        case MemoryAccessKind::kNormal:
          kind = LoadOp::Kind::RawAligned().Atomic();
          break;
        case MemoryAccessKind::kUnaligned:
          UNREACHABLE();
        case MemoryAccessKind::kProtected:
          kind = LoadOp::Kind::RawAligned().Atomic().Protected();
          break;
      }
      return __ Load(base, offset, kind,
                     MemoryRepresentation::FromMachineType(p.representation()),
                     node->opcode() == IrOpcode::kWord32AtomicLoad
                         ? RegisterRepresentation::Word32()
                         : RegisterRepresentation::Word64(),
                     0, 0);
    }

    case IrOpcode::kWord32AtomicStore:
    case IrOpcode::kWord64AtomicStore: {
      OpIndex base = Map(node->InputAt(0));
      OpIndex offset = Map(node->InputAt(1));
      OpIndex value = Map(node->InputAt(2));
      const AtomicStoreParameters& p = AtomicStoreParametersOf(node->op());
      DCHECK_EQ(__ output_graph().Get(base).outputs_rep()[0],
                RegisterRepresentation::WordPtr());
      StoreOp::Kind kind;
      switch (p.kind()) {
        case MemoryAccessKind::kNormal:
          kind = StoreOp::Kind::RawAligned().Atomic();
          break;
        case MemoryAccessKind::kUnaligned:
          UNREACHABLE();
        case MemoryAccessKind::kProtected:
          kind = StoreOp::Kind::RawAligned().Atomic().Protected();
          break;
      }
      __ Store(
          base, offset, value, kind,
          MemoryRepresentation::FromMachineRepresentation(p.representation()),
          p.write_barrier_kind(), 0, 0, true);
      return OpIndex::Invalid();
    }

    case IrOpcode::kWord32AtomicAdd:
    case IrOpcode::kWord32AtomicSub:
    case IrOpcode::kWord32AtomicAnd:
    case IrOpcode::kWord32AtomicOr:
    case IrOpcode::kWord32AtomicXor:
    case IrOpcode::kWord32AtomicExchange:
    case IrOpcode::kWord32AtomicCompareExchange:
    case IrOpcode::kWord64AtomicAdd:
    case IrOpcode::kWord64AtomicSub:
    case IrOpcode::kWord64AtomicAnd:
    case IrOpcode::kWord64AtomicOr:
    case IrOpcode::kWord64AtomicXor:
    case IrOpcode::kWord64AtomicExchange:
    case IrOpcode::kWord64AtomicCompareExchange: {
      int input_index = 0;
      OpIndex base = Map(node->InputAt(input_index++));
      OpIndex offset = Map(node->InputAt(input_index++));
      OpIndex expected;
      if (node->opcode() == IrOpcode::kWord32AtomicCompareExchange ||
          node->opcode() == IrOpcode::kWord64AtomicCompareExchange) {
        expected = Map(node->InputAt(input_index++));
      }
      OpIndex value = Map(node->InputAt(input_index++));
      const AtomicOpParameters& p = AtomicOpParametersOf(node->op());
      switch (node->opcode()) {
#define BINOP(binop, size)                                                 \
  case IrOpcode::kWord##size##Atomic##binop:                               \
    return __ AtomicRMW(base, offset, value, AtomicRMWOp::BinOp::k##binop, \
                        RegisterRepresentation::Word##size(),              \
                        MemoryRepresentation::FromMachineType(p.type()),   \
                        p.kind());
        BINOP(Add, 32)
        BINOP(Sub, 32)
        BINOP(And, 32)
        BINOP(Or, 32)
        BINOP(Xor, 32)
        BINOP(Exchange, 32)
        BINOP(Add, 64)
        BINOP(Sub, 64)
        BINOP(And, 64)
        BINOP(Or, 64)
        BINOP(Xor, 64)
        BINOP(Exchange, 64)
#undef BINOP
        case IrOpcode::kWord32AtomicCompareExchange:
          return __ AtomicCompareExchange(
              base, offset, expected, value, RegisterRepresentation::Word32(),
              MemoryRepresentation::FromMachineType(p.type()), p.kind());
        case IrOpcode::kWord64AtomicCompareExchange:
          return __ AtomicCompareExchange(
              base, offset, expected, value, RegisterRepresentation::Word64(),
              MemoryRepresentation::FromMachineType(p.type()), p.kind());
        default:
          UNREACHABLE();
      }
    }

    case IrOpcode::kWord32AtomicPairLoad:
      return __ AtomicWord32PairLoad(Map(node->InputAt(0)),
                                     Map(node->InputAt(1)), 0);
    case IrOpcode::kWord32AtomicPairStore:
      return __ AtomicWord32PairStore(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)),
          Map(node->InputAt(3)), 0);

#define ATOMIC_WORD32_PAIR_BINOP(kind)                                       \
  case IrOpcode::kWord32AtomicPair##kind:                                    \
    return __ AtomicWord32PairBinop(                                         \
        Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(2)), \
        Map(node->InputAt(3)), AtomicRMWOp::BinOp::k##kind, 0);
      ATOMIC_WORD32_PAIR_BINOP(Add)
      ATOMIC_WORD32_PAIR_BINOP(Sub)
      ATOMIC_WORD32_PAIR_BINOP(And)
      ATOMIC_WORD32_PAIR_BINOP(Or)
      ATOMIC_WORD32_PAIR_BINOP(Xor)
      ATOMIC_WORD32_PAIR_BINOP(Exchange)
    case IrOpcode::kWord32AtomicPairCompareExchange:
      return __ AtomicWord32PairCompareExchange(
          Map(node->InputAt(0)), Map(node->InputAt(1)), Map(node->InputAt(4)),
          Map(node->InputAt(5)), Map(node->InputAt(2)), Map(node->InputAt(3)),
          0);

#ifdef V8_ENABLE_WEBASSEMBLY
#define SIMD128_BINOP(name)                                              \
  case IrOpcode::k##name:                                                \
    return __ Simd128Binop(Map(node->InputAt(0)), Map(node->InputAt(1)), \
                           Simd128BinopOp::Kind::k##name);
      FOREACH_SIMD_128_BINARY_BASIC_OPCODE(SIMD128_BINOP)
#undef SIMD128_BINOP
    case IrOpcode::kI8x16Swizzle: {
      bool relaxed = OpParameter<bool>(node->op());
      return __ Simd128Binop(Map(node->InputAt(0)), Map(node->InputAt(1)),
                             relaxed
                                 ? Simd128BinopOp::Kind::kI8x16RelaxedSwizzle
                                 : Simd128BinopOp::Kind::kI8x16Swizzle);
    }

#define SIMD128_UNOP(name)                        \
  case IrOpcode::k##name:                         \
    return __ Simd128Unary(Map(node->InputAt(0)), \
                           Simd128UnaryOp::Kind::k##name);
      FOREACH_SIMD_128_UNARY_OPCODE(SIMD128_UNOP)
#undef SIMD128_UNOP

#define SIMD128_SHIFT(name)                                              \
  case IrOpcode::k##name:                                                \
    return __ Simd128Shift(Map(node->InputAt(0)), Map(node->InputAt(1)), \
                           Simd128ShiftOp::Kind::k##name);
      FOREACH_SIMD_128_SHIFT_OPCODE(SIMD128_SHIFT)
#undef SIMD128_UNOP

#define SIMD128_TEST(name) \
  case IrOpcode::k##name:  \
    return __ Simd128Test(Map(node->InputAt(0)), Simd128TestOp::Kind::k##name);
      FOREACH_SIMD_128_TEST_OPCODE(SIMD128_TEST)
#undef SIMD128_UNOP

#define SIMD128_SPLAT(name)                       \
  case IrOpcode::k##name##Splat:                  \
    return __ Simd128Splat(Map(node->InputAt(0)), \
                           Simd128SplatOp::Kind::k##name);
      FOREACH_SIMD_128_SPLAT_OPCODE(SIMD128_SPLAT)
#undef SIMD128_SPLAT

#define SIMD128_TERNARY(name)                                              \
  case IrOpcode::k##name:                                                  \
    return __ Simd128Ternary(Map(node->InputAt(0)), Map(node->InputAt(1)), \
                             Map(node->InputAt(2)),                        \
                             Simd128TernaryOp::Kind::k##name);
      FOREACH_SIMD_128_TERNARY_OPCODE(SIMD128_TERNARY)
#undef SIMD128_TERNARY

#define SIMD128_EXTRACT_LANE(name, suffix)                                    \
  case IrOpcode::k##name##ExtractLane##suffix:                                \
    return __ Simd128ExtractLane(Map(node->InputAt(0)),                       \
                                 Simd128ExtractLaneOp::Kind::k##name##suffix, \
                                 OpParameter<int32_t>(node->op()));
      SIMD128_EXTRACT_LANE(I8x16, S)
      SIMD128_EXTRACT_LANE(I8x16, U)
      SIMD128_EXTRACT_LANE(I16x8, S)
      SIMD128_EXTRACT_LANE(I16x8, U)
      SIMD128_EXTRACT_LANE(I32x4, )
      SIMD128_EXTRACT_LANE(I64x2, )
      SIMD128_EXTRACT_LANE(F32x4, )
      SIMD128_EXTRACT_LANE(F64x2, )
#undef SIMD128_LANE

#define SIMD128_REPLACE_LANE(name)                                             \
  case IrOpcode::k##name##ReplaceLane:                                         \
    return __ Simd128ReplaceLane(Map(node->InputAt(0)), Map(node->InputAt(1)), \
                                 Simd128ReplaceLaneOp::Kind::k##name,          \
                                 OpParameter<int32_t>(node->op()));
      SIMD128_REPLACE_LANE(I8x16)
      SIMD128_REPLACE_LANE(I16x8)
      SIMD128_REPLACE_LANE(I32x4)
      SIMD128_REPLACE_LANE(I64x2)
      SIMD128_REPLACE_LANE(F32x4)
      SIMD128_REPLACE_LANE(F64x2)
#undef SIMD128_REPLACE_LANE

    case IrOpcode::kLoadStackPointer:
      return __ LoadStackPointer();

    case IrOpcode::kSetStackPointer:
      __ SetStackPointer(Map(node->InputAt(0)),
                         OpParameter<wasm::FPRelativeScope>(node->op()));
      return OpIndex::Invalid();

#endif  // V8_ENABLE_WEBASSEMBLY

    case IrOpcode::kJSStackCheck: {
      DCHECK_EQ(OpParameter<StackCheckKind>(node->op()),
                StackCheckKind::kJSFunctionEntry);
      __ StackCheck(StackCheckOp::CheckOrigin::kFromJS,
                    StackCheckOp::CheckKind::kFunctionHeaderCheck);
      return OpIndex::Invalid();
    }

#ifdef V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA
    case IrOpcode::kGetContinuationPreservedEmbedderData:
      return __ GetContinuationPreservedEmbedderData();
    case IrOpcode::kSetContinuationPreservedEmbedderData:
      __ SetContinuationPreservedEmbedderData(Map(node->InputAt(0)));
      return OpIndex::Invalid();
#endif  // V8_ENABLE_CONTINUATION_PRESERVED_EMBEDDER_DATA

    default:
      std::cerr << "unsupported node type: " << *node->op() << "\n";
      node->Print(std::cerr);
      UNIMPLEMENTED();
  }
}

}  // namespace

base::Optional<BailoutReason> BuildGraph(Schedule* schedule, Zone* phase_zone,
                                         Linkage* linkage) {
  return GraphBuilder{phase_zone, *schedule, linkage}.Run();
}

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft
