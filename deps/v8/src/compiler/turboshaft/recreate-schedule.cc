// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/recreate-schedule.h"

#include "src/base/logging.h"
#include "src/base/safe_conversions.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/codegen/callable.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/compiler-source-position-table.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-origin-table.h"
#include "src/compiler/phase.h"
#include "src/compiler/pipeline-data-inl.h"
#include "src/compiler/schedule.h"
#include "src/compiler/scheduler.h"
#include "src/compiler/turboshaft/deopt-data.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

namespace v8::internal::compiler::turboshaft {

namespace {

struct ScheduleBuilder {
  PipelineData* data;
  CallDescriptor* call_descriptor;
  Zone* phase_zone;
  compiler::TFPipelineData* turbofan_data;

  const Graph& input_graph = data->graph();
  JSHeapBroker* broker = data->broker();
  Zone* graph_zone = turbofan_data->graph_zone();
  SourcePositionTable* source_positions = turbofan_data->source_positions();
  NodeOriginTable* origins = turbofan_data->node_origins();

  Schedule* const schedule = turbofan_data->schedule();
  compiler::Graph* const tf_graph = turbofan_data->graph();
  compiler::MachineOperatorBuilder& machine = *turbofan_data->machine();
  compiler::CommonOperatorBuilder& common = *turbofan_data->common();

  compiler::BasicBlock* current_block = schedule->start();
  const Block* current_input_block = nullptr;
  ZoneAbslFlatHashMap<int, Node*> parameters{phase_zone};
  ZoneAbslFlatHashMap<int, Node*> osr_values{phase_zone};
  std::vector<BasicBlock*> blocks = {};
  std::vector<Node*> nodes{input_graph.op_id_count()};
  std::vector<std::pair<Node*, OpIndex>> loop_phis = {};

  RecreateScheduleResult Run();
  Node* MakeNode(const Operator* op, base::Vector<Node* const> inputs);
  Node* MakeNode(const Operator* op, std::initializer_list<Node*> inputs) {
    return MakeNode(op, base::VectorOf(inputs));
  }
  Node* AddNode(const Operator* op, base::Vector<Node* const> inputs);
  Node* AddNode(const Operator* op, std::initializer_list<Node*> inputs) {
    return AddNode(op, base::VectorOf(inputs));
  }
  Node* GetNode(OpIndex i) { return nodes[i.id()]; }
  BasicBlock* GetBlock(const Block& block) {
    return blocks[block.index().id()];
  }
  Node* IntPtrConstant(intptr_t value) {
    return AddNode(machine.Is64() ? common.Int64Constant(value)
                                  : common.Int32Constant(
                                        base::checked_cast<int32_t>(value)),
                   {});
  }
  Node* IntPtrAdd(Node* a, Node* b) {
    return AddNode(machine.Is64() ? machine.Int64Add() : machine.Int32Add(),
                   {a, b});
  }
  Node* IntPtrShl(Node* a, Node* b) {
    return AddNode(machine.Is64() ? machine.Word64Shl() : machine.Word32Shl(),
                   {a, b});
  }
  Node* RelocatableIntPtrConstant(intptr_t value, RelocInfo::Mode mode) {
    return AddNode(machine.Is64()
                       ? common.RelocatableInt64Constant(value, mode)
                       : common.RelocatableInt32Constant(
                             base::checked_cast<int32_t>(value), mode),
                   {});
  }
  void ProcessOperation(const Operation& op);
#define DECL_PROCESS_OPERATION(Name) Node* ProcessOperation(const Name##Op& op);
  TURBOSHAFT_OPERATION_LIST(DECL_PROCESS_OPERATION)
#undef DECL_PROCESS_OPERATION

  std::pair<Node*, MachineType> BuildDeoptInput(FrameStateData::Iterator* it);
  Node* BuildStateValues(FrameStateData::Iterator* it, int32_t size);
  Node* BuildTaggedInput(FrameStateData::Iterator* it);
};

Node* ScheduleBuilder::MakeNode(const Operator* op,
                                base::Vector<Node* const> inputs) {
  Node* node = tf_graph->NewNodeUnchecked(op, static_cast<int>(inputs.size()),
                                          inputs.data());
  return node;
}
Node* ScheduleBuilder::AddNode(const Operator* op,
                               base::Vector<Node* const> inputs) {
  DCHECK_NOT_NULL(current_block);
  Node* node = MakeNode(op, inputs);
  schedule->AddNode(current_block, node);
  return node;
}

RecreateScheduleResult ScheduleBuilder::Run() {
  DCHECK_GE(input_graph.block_count(), 1);
  blocks.reserve(input_graph.block_count());
  blocks.push_back(current_block);
  for (size_t i = 1; i < input_graph.block_count(); ++i) {
    blocks.push_back(schedule->NewBasicBlock());
  }
  // The value output count of the start node does not actually matter.
  tf_graph->SetStart(tf_graph->NewNode(common.Start(0)));
  tf_graph->SetEnd(tf_graph->NewNode(common.End(0)));

  for (const Block& block : input_graph.blocks()) {
    current_input_block = &block;
    current_block = GetBlock(block);
    for (OpIndex op : input_graph.OperationIndices(block)) {
      DCHECK_NOT_NULL(current_block);
      ProcessOperation(input_graph.Get(op));
    }
  }

  for (auto& p : loop_phis) {
    p.first->ReplaceInput(1, GetNode(p.second));
  }

  DCHECK(schedule->rpo_order()->empty());
  Scheduler::ComputeSpecialRPO(phase_zone, schedule);
  // Note that Scheduler::GenerateDominatorTree also infers which blocks are
  // deferred, so we only need to set branch targets as deferred based on the
  // hints, and we let Scheduler::GenerateDominatorTree propagate this
  // information to other blocks.
  Scheduler::GenerateDominatorTree(schedule);
  return {tf_graph, schedule};
}

void ScheduleBuilder::ProcessOperation(const Operation& op) {
  if (!turboshaft::ShouldSkipOptimizationStep() && ShouldSkipOperation(op)) {
    return;
  }
  Node* node;
  switch (op.opcode) {
#define SWITCH_CASE(Name)                         \
  case Opcode::k##Name:                           \
    node = ProcessOperation(op.Cast<Name##Op>()); \
    break;
    TURBOSHAFT_OPERATION_LIST(SWITCH_CASE)
#undef SWITCH_CASE
  }
  OpIndex index = input_graph.Index(op);
  DCHECK_LT(index.id(), nodes.size());
  nodes[index.id()] = node;
  if (source_positions && source_positions->IsEnabled() && node) {
    source_positions->SetSourcePosition(node,
                                        input_graph.source_positions()[index]);
  }
  if (origins && node) {
    origins->SetNodeOrigin(node->id(), index.id());
  }
}

#define SHOULD_HAVE_BEEN_LOWERED(op) \
  Node* ScheduleBuilder::ProcessOperation(const op##Op&) { UNREACHABLE(); }
// These operations should have been lowered in previous reducers already.
TURBOSHAFT_JS_OPERATION_LIST(SHOULD_HAVE_BEEN_LOWERED)
TURBOSHAFT_SIMPLIFIED_OPERATION_LIST(SHOULD_HAVE_BEEN_LOWERED)
TURBOSHAFT_OTHER_OPERATION_LIST(SHOULD_HAVE_BEEN_LOWERED)
TURBOSHAFT_WASM_OPERATION_LIST(SHOULD_HAVE_BEEN_LOWERED)
SHOULD_HAVE_BEEN_LOWERED(Dead)
// {AbortCSADcheck} is not emitted in pipelines that still use
// {RecreateSchedule}.
SHOULD_HAVE_BEEN_LOWERED(AbortCSADcheck)
#undef SHOULD_HAVE_BEEN_LOWERED

Node* ScheduleBuilder::ProcessOperation(const WordBinopOp& op) {
  using Kind = WordBinopOp::Kind;
  const Operator* o;
  switch (op.rep.value()) {
    case WordRepresentation::Word32():
      switch (op.kind) {
        case Kind::kAdd:
          o = machine.Int32Add();
          break;
        case Kind::kSub:
          o = machine.Int32Sub();
          break;
        case Kind::kMul:
          o = machine.Int32Mul();
          break;
        case Kind::kSignedMulOverflownBits:
          o = machine.Int32MulHigh();
          break;
        case Kind::kUnsignedMulOverflownBits:
          o = machine.Uint32MulHigh();
          break;
        case Kind::kSignedDiv:
          o = machine.Int32Div();
          break;
        case Kind::kUnsignedDiv:
          o = machine.Uint32Div();
          break;
        case Kind::kSignedMod:
          o = machine.Int32Mod();
          break;
        case Kind::kUnsignedMod:
          o = machine.Uint32Mod();
          break;
        case Kind::kBitwiseAnd:
          o = machine.Word32And();
          break;
        case Kind::kBitwiseOr:
          o = machine.Word32Or();
          break;
        case Kind::kBitwiseXor:
          o = machine.Word32Xor();
          break;
      }
      break;
    case WordRepresentation::Word64():
      switch (op.kind) {
        case Kind::kAdd:
          o = machine.Int64Add();
          break;
        case Kind::kSub:
          o = machine.Int64Sub();
          break;
        case Kind::kMul:
          o = machine.Int64Mul();
          break;
        case Kind::kSignedDiv:
          o = machine.Int64Div();
          break;
        case Kind::kUnsignedDiv:
          o = machine.Uint64Div();
          break;
        case Kind::kSignedMod:
          o = machine.Int64Mod();
          break;
        case Kind::kUnsignedMod:
          o = machine.Uint64Mod();
          break;
        case Kind::kBitwiseAnd:
          o = machine.Word64And();
          break;
        case Kind::kBitwiseOr:
          o = machine.Word64Or();
          break;
        case Kind::kBitwiseXor:
          o = machine.Word64Xor();
          break;
        case Kind::kSignedMulOverflownBits:
          o = machine.Int64MulHigh();
          break;
        case Kind::kUnsignedMulOverflownBits:
          o = machine.Uint64MulHigh();
          break;
      }
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const FloatBinopOp& op) {
  using Kind = FloatBinopOp::Kind;
  const Operator* o;
  switch (op.rep.value()) {
    case FloatRepresentation::Float32():
      switch (op.kind) {
        case Kind::kAdd:
          o = machine.Float32Add();
          break;
        case Kind::kSub:
          o = machine.Float32Sub();
          break;
        case Kind::kMul:
          o = machine.Float32Mul();
          break;
        case Kind::kDiv:
          o = machine.Float32Div();
          break;
        case Kind::kMin:
          o = machine.Float32Min();
          break;
        case Kind::kMax:
          o = machine.Float32Max();
          break;
        case Kind::kPower:
        case Kind::kAtan2:
        case Kind::kMod:
          UNREACHABLE();
      }
      break;
    case FloatRepresentation::Float64():
      switch (op.kind) {
        case Kind::kAdd:
          o = machine.Float64Add();
          break;
        case Kind::kSub:
          o = machine.Float64Sub();
          break;
        case Kind::kMul:
          o = machine.Float64Mul();
          break;
        case Kind::kDiv:
          o = machine.Float64Div();
          break;
        case Kind::kMod:
          o = machine.Float64Mod();
          break;
        case Kind::kMin:
          o = machine.Float64Min();
          break;
        case Kind::kMax:
          o = machine.Float64Max();
          break;
        case Kind::kPower:
          o = machine.Float64Pow();
          break;
        case Kind::kAtan2:
          o = machine.Float64Atan2();
          break;
      }
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}

Node* ScheduleBuilder::ProcessOperation(const OverflowCheckedBinopOp& op) {
  const Operator* o;
  switch (op.rep.value()) {
    case WordRepresentation::Word32():
      switch (op.kind) {
        case OverflowCheckedBinopOp::Kind::kSignedAdd:
          o = machine.Int32AddWithOverflow();
          break;
        case OverflowCheckedBinopOp::Kind::kSignedSub:
          o = machine.Int32SubWithOverflow();
          break;
        case OverflowCheckedBinopOp::Kind::kSignedMul:
          o = machine.Int32MulWithOverflow();
          break;
      }
      break;
    case WordRepresentation::Word64():
      switch (op.kind) {
        case OverflowCheckedBinopOp::Kind::kSignedAdd:
          o = machine.Int64AddWithOverflow();
          break;
        case OverflowCheckedBinopOp::Kind::kSignedSub:
          o = machine.Int64SubWithOverflow();
          break;
        case OverflowCheckedBinopOp::Kind::kSignedMul:
          o = machine.Int64MulWithOverflow();
          break;
      }
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const WordUnaryOp& op) {
  bool word64 = op.rep == WordRepresentation::Word64();
  const Operator* o;
  switch (op.kind) {
    case WordUnaryOp::Kind::kReverseBytes:
      o = word64 ? machine.Word64ReverseBytes() : machine.Word32ReverseBytes();
      break;
    case WordUnaryOp::Kind::kCountLeadingZeros:
      o = word64 ? machine.Word64Clz() : machine.Word32Clz();
      break;
    case WordUnaryOp::Kind::kCountTrailingZeros:
      o = word64 ? machine.Word64Ctz().op() : machine.Word32Ctz().op();
      break;
    case WordUnaryOp::Kind::kPopCount:
      o = word64 ? machine.Word64Popcnt().op() : machine.Word32Popcnt().op();
      break;
    case WordUnaryOp::Kind::kSignExtend8:
      o = word64 ? machine.SignExtendWord8ToInt64()
                 : machine.SignExtendWord8ToInt32();
      break;
    case WordUnaryOp::Kind::kSignExtend16:
      o = word64 ? machine.SignExtendWord16ToInt64()
                 : machine.SignExtendWord16ToInt32();
      break;
  }
  return AddNode(o, {GetNode(op.input())});
}

Node* ScheduleBuilder::ProcessOperation(const OverflowCheckedUnaryOp& op) {
  bool word64 = op.rep == WordRepresentation::Word64();
  const Operator* o;
  switch (op.kind) {
    case OverflowCheckedUnaryOp::Kind::kAbs:
      o = word64 ? machine.Int64AbsWithOverflow().op()
                 : machine.Int32AbsWithOverflow().op();
  }
  return AddNode(o, {GetNode(op.input())});
}

Node* ScheduleBuilder::ProcessOperation(const FloatUnaryOp& op) {
  DCHECK(FloatUnaryOp::IsSupported(op.kind, op.rep));
  bool float64 = op.rep == FloatRepresentation::Float64();
  const Operator* o;
  switch (op.kind) {
    case FloatUnaryOp::Kind::kAbs:
      o = float64 ? machine.Float64Abs() : machine.Float32Abs();
      break;
    case FloatUnaryOp::Kind::kNegate:
      o = float64 ? machine.Float64Neg() : machine.Float32Neg();
      break;
    case FloatUnaryOp::Kind::kRoundDown:
      o = float64 ? machine.Float64RoundDown().op()
                  : machine.Float32RoundDown().op();
      break;
    case FloatUnaryOp::Kind::kRoundUp:
      o = float64 ? machine.Float64RoundUp().op()
                  : machine.Float32RoundUp().op();
      break;
    case FloatUnaryOp::Kind::kRoundToZero:
      o = float64 ? machine.Float64RoundTruncate().op()
                  : machine.Float32RoundTruncate().op();
      break;
    case FloatUnaryOp::Kind::kRoundTiesEven:
      o = float64 ? machine.Float64RoundTiesEven().op()
                  : machine.Float32RoundTiesEven().op();
      break;
    case FloatUnaryOp::Kind::kSqrt:
      o = float64 ? machine.Float64Sqrt() : machine.Float32Sqrt();
      break;
    case FloatUnaryOp::Kind::kSilenceNaN:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64SilenceNaN();
      break;
    case FloatUnaryOp::Kind::kLog:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Log();
      break;
    case FloatUnaryOp::Kind::kExp:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Exp();
      break;
    case FloatUnaryOp::Kind::kExpm1:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Expm1();
      break;
    case FloatUnaryOp::Kind::kSin:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Sin();
      break;
    case FloatUnaryOp::Kind::kCos:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Cos();
      break;
    case FloatUnaryOp::Kind::kAsin:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Asin();
      break;
    case FloatUnaryOp::Kind::kAcos:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Acos();
      break;
    case FloatUnaryOp::Kind::kSinh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Sinh();
      break;
    case FloatUnaryOp::Kind::kCosh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Cosh();
      break;
    case FloatUnaryOp::Kind::kAsinh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Asinh();
      break;
    case FloatUnaryOp::Kind::kAcosh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Acosh();
      break;
    case FloatUnaryOp::Kind::kTan:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Tan();
      break;
    case FloatUnaryOp::Kind::kTanh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Tanh();
      break;
    case FloatUnaryOp::Kind::kLog2:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Log2();
      break;
    case FloatUnaryOp::Kind::kLog10:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Log10();
      break;
    case FloatUnaryOp::Kind::kLog1p:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Log1p();
      break;
    case FloatUnaryOp::Kind::kAtan:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Atan();
      break;
    case FloatUnaryOp::Kind::kAtanh:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Atanh();
      break;
    case FloatUnaryOp::Kind::kCbrt:
      DCHECK_EQ(op.rep, FloatRepresentation::Float64());
      o = machine.Float64Cbrt();
      break;
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const ShiftOp& op) {
  DCHECK(op.rep == WordRepresentation::Word32() ||
         op.rep == WordRepresentation::Word64());
  bool word64 = op.rep == WordRepresentation::Word64();
  Node* right = GetNode(op.right());
  if (word64) {
    // In Turboshaft's ShiftOp, the right hand side always has Word32
    // representation, so for 64 bit shifts, we have to zero-extend when
    // constructing Turbofan.
    if (const ConstantOp* constant =
            input_graph.Get(op.right()).TryCast<Opmask::kWord32Constant>()) {
      int64_t value = static_cast<int64_t>(constant->word32());
      right = AddNode(common.Int64Constant(value), {});
    } else {
      right = AddNode(machine.ChangeUint32ToUint64(), {right});
    }
  }
  const Operator* o;
  switch (op.kind) {
    case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
      o = word64 ? machine.Word64SarShiftOutZeros()
                 : machine.Word32SarShiftOutZeros();
      break;
    case ShiftOp::Kind::kShiftRightArithmetic:
      o = word64 ? machine.Word64Sar() : machine.Word32Sar();
      break;
    case ShiftOp::Kind::kShiftRightLogical:
      o = word64 ? machine.Word64Shr() : machine.Word32Shr();
      break;
    case ShiftOp::Kind::kShiftLeft:
      o = word64 ? machine.Word64Shl() : machine.Word32Shl();
      break;
    case ShiftOp::Kind::kRotateLeft:
      o = word64 ? machine.Word64Rol().op() : machine.Word32Rol().op();
      break;
    case ShiftOp::Kind::kRotateRight:
      o = word64 ? machine.Word64Ror() : machine.Word32Ror();
      break;
  }
  return AddNode(o, {GetNode(op.left()), right});
}
Node* ScheduleBuilder::ProcessOperation(const ComparisonOp& op) {
  const Operator* o;
  switch (op.rep.value()) {
    case RegisterRepresentation::Word32():
      switch (op.kind) {
        case ComparisonOp::Kind::kEqual:
          o = machine.Word32Equal();
          break;
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Int32LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Int32LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
          o = machine.Uint32LessThan();
          break;
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          o = machine.Uint32LessThanOrEqual();
          break;
      }
      break;
    case RegisterRepresentation::Word64():
      switch (op.kind) {
        case ComparisonOp::Kind::kEqual:
          o = machine.Word64Equal();
          break;
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Int64LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Int64LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
          o = machine.Uint64LessThan();
          break;
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          o = machine.Uint64LessThanOrEqual();
          break;
      }
      break;
    case RegisterRepresentation::Float32():
      switch (op.kind) {
        case ComparisonOp::Kind::kEqual:
          o = machine.Float32Equal();
          break;
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Float32LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Float32LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          UNREACHABLE();
      }
      break;
    case RegisterRepresentation::Float64():
      switch (op.kind) {
        case ComparisonOp::Kind::kEqual:
          o = machine.Float64Equal();
          break;
        case ComparisonOp::Kind::kSignedLessThan:
          o = machine.Float64LessThan();
          break;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          o = machine.Float64LessThanOrEqual();
          break;
        case ComparisonOp::Kind::kUnsignedLessThan:
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          UNREACHABLE();
      }
      break;
    case RegisterRepresentation::Tagged():
      switch (op.kind) {
        case ComparisonOp::Kind::kEqual:
          o = machine.TaggedEqual();
          break;
        case ComparisonOp::Kind::kSignedLessThan:
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
        case ComparisonOp::Kind::kUnsignedLessThan:
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          UNREACHABLE();
      }
      break;
    default:
      UNREACHABLE();
  }
  return AddNode(o, {GetNode(op.left()), GetNode(op.right())});
}
Node* ScheduleBuilder::ProcessOperation(const ChangeOp& op) {
  const Operator* o;
  switch (op.kind) {
    using Kind = ChangeOp::Kind;
    using Assumption = ChangeOp::Assumption;
    case Kind::kFloatConversion:
      if (op.from == FloatRepresentation::Float64() &&
          op.to == FloatRepresentation::Float32()) {
        o = machine.TruncateFloat64ToFloat32();
      } else if (op.from == FloatRepresentation::Float32() &&
                 op.to == FloatRepresentation::Float64()) {
        o = machine.ChangeFloat32ToFloat64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignedFloatTruncateOverflowToMin:
    case Kind::kUnsignedFloatTruncateOverflowToMin: {
      bool is_signed = op.kind == Kind::kSignedFloatTruncateOverflowToMin;
      if (op.assumption == Assumption::kReversible) {
        if (op.from == FloatRepresentation::Float64() &&
            op.to == WordRepresentation::Word64()) {
          o = is_signed ? machine.ChangeFloat64ToInt64()
                        : machine.ChangeFloat64ToUint64();
        } else if (op.from == FloatRepresentation::Float64() &&
                   op.to == WordRepresentation::Word32()) {
          o = is_signed ? machine.ChangeFloat64ToInt32()
                        : machine.ChangeFloat64ToUint32();
        } else {
          UNIMPLEMENTED();
        }
        break;
      }
      TruncateKind truncate_kind;
      switch (op.assumption) {
        case ChangeOp::Assumption::kReversible:
          UNREACHABLE();
        case ChangeOp::Assumption::kNoAssumption:
          truncate_kind = TruncateKind::kSetOverflowToMin;
          break;
        case ChangeOp::Assumption::kNoOverflow:
          truncate_kind = TruncateKind::kArchitectureDefault;
          break;
      }
      if (op.from == FloatRepresentation::Float64() &&
          op.to == WordRepresentation::Word64()) {
        DCHECK(is_signed);
        o = machine.TruncateFloat64ToInt64(truncate_kind);
      } else if (op.from == FloatRepresentation::Float64() &&
                 op.to == WordRepresentation::Word32()) {
        if (is_signed) {
          DCHECK_EQ(truncate_kind, TruncateKind::kArchitectureDefault);
          o = machine.RoundFloat64ToInt32();
        } else {
          o = machine.TruncateFloat64ToUint32();
        }
      } else if (op.from == FloatRepresentation::Float32() &&
                 op.to == WordRepresentation::Word32()) {
        o = is_signed ? machine.TruncateFloat32ToInt32(truncate_kind)
                      : machine.TruncateFloat32ToUint32(truncate_kind);
      } else {
        UNIMPLEMENTED();
      }
      break;
    }
    case Kind::kJSFloatTruncate:
      if (op.from == FloatRepresentation::Float64() &&
          op.to == WordRepresentation::Word32()) {
        o = machine.TruncateFloat64ToWord32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignedToFloat:
      if (op.from == WordRepresentation::Word32() &&
          op.to == FloatRepresentation::Float64()) {
        DCHECK_EQ(op.assumption, Assumption::kNoAssumption);
        o = machine.ChangeInt32ToFloat64();
      } else if (op.from == WordRepresentation::Word64() &&
                 op.to == FloatRepresentation::Float64()) {
        o = op.assumption == Assumption::kReversible
                ? machine.ChangeInt64ToFloat64()
                : machine.RoundInt64ToFloat64();
      } else if (op.from == WordRepresentation::Word32() &&
                 op.to == FloatRepresentation::Float32()) {
        o = machine.RoundInt32ToFloat32();
      } else if (op.from == WordRepresentation::Word64() &&
                 op.to == FloatRepresentation::Float32()) {
        o = machine.RoundInt64ToFloat32();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kUnsignedToFloat:
      if (op.from == WordRepresentation::Word32() &&
          op.to == FloatRepresentation::Float64()) {
        o = machine.ChangeUint32ToFloat64();
      } else if (op.from == WordRepresentation::Word32() &&
                 op.to == FloatRepresentation::Float32()) {
        o = machine.RoundUint32ToFloat32();
      } else if (op.from == WordRepresentation::Word64() &&
                 op.to == FloatRepresentation::Float32()) {
        o = machine.RoundUint64ToFloat32();
      } else if (op.from == WordRepresentation::Word64() &&
                 op.to == FloatRepresentation::Float64()) {
        o = machine.RoundUint64ToFloat64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kExtractHighHalf:
      DCHECK_EQ(op.from, FloatRepresentation::Float64());
      DCHECK_EQ(op.to, WordRepresentation::Word32());
      o = machine.Float64ExtractHighWord32();
      break;
    case Kind::kExtractLowHalf:
      DCHECK_EQ(op.from, FloatRepresentation::Float64());
      DCHECK_EQ(op.to, WordRepresentation::Word32());
      o = machine.Float64ExtractLowWord32();
      break;
    case Kind::kBitcast:
      if (op.from == WordRepresentation::Word32() &&
          op.to == WordRepresentation::Word64()) {
        o = machine.BitcastWord32ToWord64();
      } else if (op.from == FloatRepresentation::Float32() &&
                 op.to == WordRepresentation::Word32()) {
        o = machine.BitcastFloat32ToInt32();
      } else if (op.from == WordRepresentation::Word32() &&
                 op.to == FloatRepresentation::Float32()) {
        o = machine.BitcastInt32ToFloat32();
      } else if (op.from == FloatRepresentation::Float64() &&
                 op.to == WordRepresentation::Word64()) {
        o = machine.BitcastFloat64ToInt64();
      } else if (op.from == WordRepresentation::Word64() &&
                 op.to == FloatRepresentation::Float64()) {
        o = machine.BitcastInt64ToFloat64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kSignExtend:
      if (op.from == WordRepresentation::Word32() &&
          op.to == WordRepresentation::Word64()) {
        o = machine.ChangeInt32ToInt64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kZeroExtend:
      if (op.from == WordRepresentation::Word32() &&
          op.to == WordRepresentation::Word64()) {
        o = machine.ChangeUint32ToUint64();
      } else {
        UNIMPLEMENTED();
      }
      break;
    case Kind::kTruncate:
      if (op.from == WordRepresentation::Word64() &&
          op.to == WordRepresentation::Word32()) {
        o = machine.TruncateInt64ToInt32();
      } else {
        UNIMPLEMENTED();
      }
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const TryChangeOp& op) {
  const Operator* o;
  switch (op.kind) {
    using Kind = TryChangeOp::Kind;
    case Kind::kSignedFloatTruncateOverflowUndefined:
      if (op.from == FloatRepresentation::Float64() &&
          op.to == WordRepresentation::Word64()) {
        o = machine.TryTruncateFloat64ToInt64();
      } else if (op.from == FloatRepresentation::Float64() &&
                 op.to == WordRepresentation::Word32()) {
        o = machine.TryTruncateFloat64ToInt32();
      } else if (op.from == FloatRepresentation::Float32() &&
                 op.to == WordRepresentation::Word64()) {
        o = machine.TryTruncateFloat32ToInt64();
      } else {
        UNREACHABLE();
      }
      break;
    case Kind::kUnsignedFloatTruncateOverflowUndefined:
      if (op.from == FloatRepresentation::Float64() &&
          op.to == WordRepresentation::Word64()) {
        o = machine.TryTruncateFloat64ToUint64();
      } else if (op.from == FloatRepresentation::Float64() &&
                 op.to == WordRepresentation::Word32()) {
        o = machine.TryTruncateFloat64ToUint32();
      } else if (op.from == FloatRepresentation::Float32() &&
                 op.to == WordRepresentation::Word64()) {
        o = machine.TryTruncateFloat32ToUint64();
      } else {
        UNREACHABLE();
      }
      break;
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(
    const BitcastWord32PairToFloat64Op& op) {
  Node* temp = AddNode(
      machine.Float64InsertHighWord32(),
      {AddNode(common.Float64Constant(0), {}), GetNode(op.high_word32())});
  return AddNode(machine.Float64InsertLowWord32(),
                 {temp, GetNode(op.low_word32())});
}
Node* ScheduleBuilder::ProcessOperation(const TaggedBitcastOp& op) {
  using Rep = RegisterRepresentation;
  const Operator* o;
  switch (multi(op.from, op.to)) {
    case multi(Rep::Tagged(), Rep::Word32()):
      if constexpr (Is64()) {
        DCHECK_EQ(op.kind, TaggedBitcastOp::Kind::kSmi);
        DCHECK(SmiValuesAre31Bits());
        o = machine.TruncateInt64ToInt32();
      } else {
        o = machine.BitcastTaggedToWord();
      }
      break;
    case multi(Rep::Tagged(), Rep::Word64()):
      o = machine.BitcastTaggedToWord();
      break;
    case multi(Rep::Word32(), Rep::Tagged()):
    case multi(Rep::Word64(), Rep::Tagged()):
      if (op.kind == TaggedBitcastOp::Kind::kSmi) {
        o = machine.BitcastWordToTaggedSigned();
      } else {
        o = machine.BitcastWordToTagged();
      }
      break;
    case multi(Rep::Compressed(), Rep::Word32()):
      o = machine.BitcastTaggedToWord();
      break;
    default:
      UNIMPLEMENTED();
  }
  return AddNode(o, {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const SelectOp& op) {
  // If there is a Select, then it should only be one that is supported by the
  // machine, and it should be meant to be implementation with cmove.
  DCHECK_EQ(op.implem, SelectOp::Implementation::kCMove);
  DCHECK((op.rep == RegisterRepresentation::Word32() &&
          SupportedOperations::word32_select()) ||
         (op.rep == RegisterRepresentation::Word64() &&
          SupportedOperations::word64_select()) ||
         (op.rep == RegisterRepresentation::Float32() &&
          SupportedOperations::float32_select()) ||
         (op.rep == RegisterRepresentation::Float64() &&
          SupportedOperations::float64_select()));
  const Operator* o = nullptr;
  switch (op.rep.value()) {
    case RegisterRepresentation::Enum::kWord32:
      o = machine.Word32Select().op();
      break;
    case RegisterRepresentation::Enum::kWord64:
      o = machine.Word64Select().op();
      break;
    case RegisterRepresentation::Enum::kFloat32:
      o = machine.Float32Select().op();
      break;
    case RegisterRepresentation::Enum::kFloat64:
      o = machine.Float64Select().op();
      break;
    case RegisterRepresentation::Enum::kTagged:
    case RegisterRepresentation::Enum::kCompressed:
    case RegisterRepresentation::Enum::kSimd128:
    case RegisterRepresentation::Enum::kSimd256:
      UNREACHABLE();
  }

  return AddNode(
      o, {GetNode(op.cond()), GetNode(op.vtrue()), GetNode(op.vfalse())});
}
Node* ScheduleBuilder::ProcessOperation(const PendingLoopPhiOp& op) {
  UNREACHABLE();
}

Node* ScheduleBuilder::ProcessOperation(const AtomicWord32PairOp& op) {
  DCHECK(!Is64());
  Node* index;
  if (op.index().valid() && op.offset) {
    index = AddNode(machine.Int32Add(),
                    {GetNode(op.index().value()), IntPtrConstant(op.offset)});
  } else if (op.index().valid()) {
    index = GetNode(op.index().value());
  } else {
    index = IntPtrConstant(op.offset);
  }
#define BINOP_CASE(OP)                                               \
  if (op.kind == AtomicWord32PairOp::Kind::k##OP) {                  \
    return AddNode(                                                  \
        machine.Word32AtomicPair##OP(),                              \
        {GetNode(op.base()), index, GetNode(op.value_low().value()), \
         GetNode(op.value_high().value())});                         \
  }
#define ATOMIC_BINOPS(V) \
  V(Add)                 \
  V(Sub)                 \
  V(And)                 \
  V(Or)                  \
  V(Xor)                 \
  V(Exchange)
  ATOMIC_BINOPS(BINOP_CASE)
#undef ATOMIC_BINOPS
#undef BINOP_CASE

  if (op.kind == AtomicWord32PairOp::Kind::kLoad) {
    return AddNode(machine.Word32AtomicPairLoad(AtomicMemoryOrder::kSeqCst),
                   {GetNode(op.base()), index});
  }
  if (op.kind == AtomicWord32PairOp::Kind::kStore) {
    return AddNode(machine.Word32AtomicPairStore(AtomicMemoryOrder::kSeqCst),
                   {GetNode(op.base()), index, GetNode(op.value_low().value()),
                    GetNode(op.value_high().value())});
  }
  DCHECK_EQ(op.kind, AtomicWord32PairOp::Kind::kCompareExchange);
  return AddNode(
      machine.Word32AtomicPairCompareExchange(),
      {GetNode(op.base()), index, GetNode(op.expected_low().value()),
       GetNode(op.expected_high().value()), GetNode(op.value_low().value()),
       GetNode(op.value_high().value())});
}

Node* ScheduleBuilder::ProcessOperation(const AtomicRMWOp& op) {
#define ATOMIC_BINOPS(V) \
  V(Add)                 \
  V(Sub)                 \
  V(And)                 \
  V(Or)                  \
  V(Xor)                 \
  V(Exchange)            \
  V(CompareExchange)

  AtomicOpParameters param(op.memory_rep.ToMachineType(),
                           op.memory_access_kind);
  const Operator* node_op;
  if (op.in_out_rep == RegisterRepresentation::Word32()) {
    switch (op.bin_op) {
#define CASE(Name)                               \
  case AtomicRMWOp::BinOp::k##Name:              \
    node_op = machine.Word32Atomic##Name(param); \
    break;
      ATOMIC_BINOPS(CASE)
#undef CASE
    }
  } else {
    DCHECK_EQ(op.in_out_rep, RegisterRepresentation::Word64());
    switch (op.bin_op) {
#define CASE(Name)                               \
  case AtomicRMWOp::BinOp::k##Name:              \
    node_op = machine.Word64Atomic##Name(param); \
    break;
      ATOMIC_BINOPS(CASE)
#undef CASE
    }
  }
#undef ATOMIC_BINOPS
  Node* base = GetNode(op.base());
  Node* index = GetNode(op.index());
  Node* value = GetNode(op.value());
  if (op.bin_op == AtomicRMWOp::BinOp::kCompareExchange) {
    Node* expected = GetNode(op.expected().value());
    return AddNode(node_op, {base, index, expected, value});
  } else {
    return AddNode(node_op, {base, index, value});
  }
}

Node* ScheduleBuilder::ProcessOperation(const MemoryBarrierOp& op) {
  return AddNode(machine.MemoryBarrier(op.memory_order), {});
}

Node* ScheduleBuilder::ProcessOperation(const TupleOp& op) {
  // Tuples are only used for lowerings during reduction. Therefore, we can
  // assume that it is unused if it occurs at this point.
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const ConstantOp& op) {
  switch (op.kind) {
    case ConstantOp::Kind::kWord32:
      return AddNode(common.Int32Constant(static_cast<int32_t>(op.word32())),
                     {});
    case ConstantOp::Kind::kWord64:
      return AddNode(common.Int64Constant(static_cast<int64_t>(op.word64())),
                     {});
    case ConstantOp::Kind::kSmi:
      if constexpr (Is64()) {
        return AddNode(
            machine.BitcastWordToTaggedSigned(),
            {AddNode(common.Int64Constant(static_cast<int64_t>(op.smi().ptr())),
                     {})});
      } else {
        return AddNode(
            machine.BitcastWordToTaggedSigned(),
            {AddNode(common.Int32Constant(static_cast<int32_t>(op.smi().ptr())),
                     {})});
      }
    case ConstantOp::Kind::kExternal:
      return AddNode(common.ExternalConstant(op.external_reference()), {});
    case ConstantOp::Kind::kHeapObject:
      return AddNode(common.HeapConstant(op.handle()), {});
    case ConstantOp::Kind::kCompressedHeapObject:
      return AddNode(common.CompressedHeapConstant(op.handle()), {});
    case ConstantOp::Kind::kTrustedHeapObject:
      return AddNode(common.TrustedHeapConstant(op.handle()), {});
    case ConstantOp::Kind::kNumber:
      return AddNode(common.NumberConstant(op.number().get_scalar()), {});
    case ConstantOp::Kind::kTaggedIndex:
      return AddNode(common.TaggedIndexConstant(op.tagged_index()), {});
    case ConstantOp::Kind::kFloat64:
      return AddNode(common.Float64Constant(op.float64().get_scalar()), {});
    case ConstantOp::Kind::kFloat32:
      return AddNode(common.Float32Constant(op.float32().get_scalar()), {});
    case ConstantOp::Kind::kRelocatableWasmCall:
      return RelocatableIntPtrConstant(op.integral(), RelocInfo::WASM_CALL);
    case ConstantOp::Kind::kRelocatableWasmStubCall:
      return RelocatableIntPtrConstant(op.integral(),
                                       RelocInfo::WASM_STUB_CALL);
    case ConstantOp::Kind::kRelocatableWasmCanonicalSignatureId:
      return AddNode(common.RelocatableInt32Constant(
                         base::checked_cast<int32_t>(op.integral()),
                         RelocInfo::WASM_CANONICAL_SIG_ID),
                     {});
  }
}

Node* ScheduleBuilder::ProcessOperation(const LoadOp& op) {
  intptr_t offset = op.offset;
  if (op.kind.tagged_base) {
    CHECK_GE(offset, std::numeric_limits<int32_t>::min() + kHeapObjectTag);
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  Node* index;
  if (op.index().valid()) {
    index = GetNode(op.index().value());
    if (op.element_size_log2 != 0) {
      index = IntPtrShl(index, IntPtrConstant(op.element_size_log2));
    }
    if (offset != 0) {
      index = IntPtrAdd(index, IntPtrConstant(offset));
    }
  } else {
    index = IntPtrConstant(offset);
  }

  MachineType loaded_rep = op.machine_type();
  const Operator* o;
  if (op.kind.maybe_unaligned) {
    DCHECK(!op.kind.with_trap_handler);
    if (loaded_rep.representation() == MachineRepresentation::kWord8 ||
        machine.UnalignedLoadSupported(loaded_rep.representation())) {
      o = machine.Load(loaded_rep);
    } else {
      o = machine.UnalignedLoad(loaded_rep);
    }
  } else if (op.kind.is_atomic) {
    DCHECK(!op.kind.maybe_unaligned);
    AtomicLoadParameters params(loaded_rep, AtomicMemoryOrder::kSeqCst,
                                op.kind.with_trap_handler
                                    ? MemoryAccessKind::kProtected
                                    : MemoryAccessKind::kNormal);
    if (op.result_rep == RegisterRepresentation::Word32()) {
      o = machine.Word32AtomicLoad(params);
    } else {
      DCHECK_EQ(op.result_rep, RegisterRepresentation::Word64());
      o = machine.Word64AtomicLoad(params);
    }
  } else if (op.kind.with_trap_handler) {
    DCHECK(!op.kind.maybe_unaligned);
    if (op.kind.tagged_base) {
      o = machine.LoadTrapOnNull(loaded_rep);
    } else {
      o = machine.ProtectedLoad(loaded_rep);
    }
  } else {
    o = machine.Load(loaded_rep);
  }
  return AddNode(o, {base, index});
}

Node* ScheduleBuilder::ProcessOperation(const StoreOp& op) {
  intptr_t offset = op.offset;
  if (op.kind.tagged_base) {
    CHECK(offset >= std::numeric_limits<int32_t>::min() + kHeapObjectTag);
    offset -= kHeapObjectTag;
  }
  Node* base = GetNode(op.base());
  Node* index;
  if (op.index().valid()) {
    index = GetNode(op.index().value());
    if (op.element_size_log2 != 0) {
      index = IntPtrShl(index, IntPtrConstant(op.element_size_log2));
    }
    if (offset != 0) {
      index = IntPtrAdd(index, IntPtrConstant(offset));
    }
  } else {
    index = IntPtrConstant(offset);
  }
  Node* value = GetNode(op.value());

  const Operator* o;
  if (op.kind.maybe_unaligned) {
    DCHECK(!op.kind.with_trap_handler);
    DCHECK_EQ(op.write_barrier, WriteBarrierKind::kNoWriteBarrier);
    if (op.stored_rep.ToMachineType().representation() ==
            MachineRepresentation::kWord8 ||
        machine.UnalignedStoreSupported(
            op.stored_rep.ToMachineType().representation())) {
      o = machine.Store(StoreRepresentation(
          op.stored_rep.ToMachineType().representation(), op.write_barrier));
    } else {
      o = machine.UnalignedStore(
          op.stored_rep.ToMachineType().representation());
    }
  } else if (op.kind.is_atomic) {
    AtomicStoreParameters params(op.stored_rep.ToMachineType().representation(),
                                 op.write_barrier, AtomicMemoryOrder::kSeqCst,
                                 op.kind.with_trap_handler
                                     ? MemoryAccessKind::kProtected
                                     : MemoryAccessKind::kNormal);
    if (op.stored_rep == MemoryRepresentation::Int64() ||
        op.stored_rep == MemoryRepresentation::Uint64()) {
      o = machine.Word64AtomicStore(params);
    } else {
      o = machine.Word32AtomicStore(params);
    }
  } else if (op.kind.with_trap_handler) {
    DCHECK(!op.kind.maybe_unaligned);
    if (op.kind.tagged_base) {
      o = machine.StoreTrapOnNull(StoreRepresentation(
          op.stored_rep.ToMachineType().representation(), op.write_barrier));
    } else {
      DCHECK_EQ(op.write_barrier, WriteBarrierKind::kNoWriteBarrier);
      o = machine.ProtectedStore(
          op.stored_rep.ToMachineType().representation());
    }
  } else if (op.stored_rep == MemoryRepresentation::IndirectPointer()) {
    o = machine.StoreIndirectPointer(op.write_barrier);
    // In this case we need a fourth input: the indirect pointer tag.
    Node* tag = IntPtrConstant(op.indirect_pointer_tag());
    return AddNode(o, {base, index, value, tag});
  } else {
    o = machine.Store(StoreRepresentation(
        op.stored_rep.ToMachineType().representation(), op.write_barrier));
  }
  return AddNode(o, {base, index, value});
}

Node* ScheduleBuilder::ProcessOperation(const RetainOp& op) {
  return AddNode(common.Retain(), {GetNode(op.retained())});
}
Node* ScheduleBuilder::ProcessOperation(const ParameterOp& op) {
  // Parameters need to be cached because the register allocator assumes that
  // there are no duplicate nodes for the same parameter.
  if (parameters.count(op.parameter_index)) {
    return parameters[op.parameter_index];
  }
  Node* parameter = MakeNode(
      common.Parameter(static_cast<int>(op.parameter_index), op.debug_name),
      {tf_graph->start()});
  schedule->AddNode(schedule->start(), parameter);
  parameters[op.parameter_index] = parameter;
  return parameter;
}
Node* ScheduleBuilder::ProcessOperation(const OsrValueOp& op) {
  // OSR values behave like parameters, so they also need to be cached.
  if (osr_values.count(op.index)) {
    return osr_values[op.index];
  }
  Node* osr_value = MakeNode(common.OsrValue(static_cast<int>(op.index)),
                             {tf_graph->start()});
  schedule->AddNode(schedule->start(), osr_value);
  osr_values[op.index] = osr_value;
  return osr_value;
}
Node* ScheduleBuilder::ProcessOperation(const GotoOp& op) {
  schedule->AddGoto(current_block, blocks[op.destination->index().id()]);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const StackPointerGreaterThanOp& op) {
  return AddNode(machine.StackPointerGreaterThan(op.kind),
                 {GetNode(op.stack_limit())});
}
Node* ScheduleBuilder::ProcessOperation(const StackSlotOp& op) {
  return AddNode(machine.StackSlot(op.size, op.alignment, op.is_tagged), {});
}
Node* ScheduleBuilder::ProcessOperation(const FrameConstantOp& op) {
  switch (op.kind) {
    case FrameConstantOp::Kind::kStackCheckOffset:
      return AddNode(machine.LoadStackCheckOffset(), {});
    case FrameConstantOp::Kind::kFramePointer:
      return AddNode(machine.LoadFramePointer(), {});
    case FrameConstantOp::Kind::kParentFramePointer:
      return AddNode(machine.LoadParentFramePointer(), {});
  }
}
Node* ScheduleBuilder::ProcessOperation(const DeoptimizeIfOp& op) {
  Node* condition = GetNode(op.condition());
  Node* frame_state = GetNode(op.frame_state());
  const Operator* o = op.negated
                          ? common.DeoptimizeUnless(op.parameters->reason(),
                                                    op.parameters->feedback())
                          : common.DeoptimizeIf(op.parameters->reason(),
                                                op.parameters->feedback());
  return AddNode(o, {condition, frame_state});
}

#if V8_ENABLE_WEBASSEMBLY
Node* ScheduleBuilder::ProcessOperation(const TrapIfOp& op) {
  Node* condition = GetNode(op.condition());
  bool has_frame_state = op.frame_state().valid();
  Node* frame_state =
      has_frame_state ? GetNode(op.frame_state().value()) : nullptr;
  const Operator* o = op.negated
                          ? common.TrapUnless(op.trap_id, has_frame_state)
                          : common.TrapIf(op.trap_id, has_frame_state);
  return has_frame_state ? AddNode(o, {condition, frame_state})
                         : AddNode(o, {condition});
}
#endif  // V8_ENABLE_WEBASSEMBLY

Node* ScheduleBuilder::ProcessOperation(const DeoptimizeOp& op) {
  Node* frame_state = GetNode(op.frame_state());
  const Operator* o =
      common.Deoptimize(op.parameters->reason(), op.parameters->feedback());
  Node* node = MakeNode(o, {frame_state});
  schedule->AddDeoptimize(current_block, node);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const PhiOp& op) {
  if (current_input_block->IsLoop()) {
    DCHECK_EQ(op.input_count, 2);
    Node* input = GetNode(op.input(0));
    // The second `input` is a placeholder that is patched when we process the
    // backedge.
    Node* node =
        AddNode(common.Phi(op.rep.machine_representation(), 2), {input, input});
    loop_phis.emplace_back(node, op.input(1));
    return node;
  } else {
    // Predecessors of {current_input_block} and the TF's matching block might
    // not be in the same order, so Phi inputs might need to be reordered to
    // match the new order.
    // This is similar to what AssembleOutputGraphPhi in CopyingPhase does,
    // except that CopyingPhase has a new->old block mapping, which we
    // don't have here in RecreateSchedule, so the implementation is slightly
    // different (relying on std::lower_bound rather than looking up the
    // old->new mapping).
    ZoneVector<BasicBlock*> new_predecessors = current_block->predecessors();
    // Since RecreateSchedule visits the blocks in increasing ID order,
    // predecessors should be sorted (we rely on this property to binary search
    // new predecessors corresponding to old ones).
    auto cmp_basic_block = [](BasicBlock* a, BasicBlock* b) {
      return a->id().ToInt() < b->id().ToInt();
    };
    DCHECK(std::is_sorted(new_predecessors.begin(), new_predecessors.end(),
                          cmp_basic_block));
    size_t predecessor_count = new_predecessors.size();
    base::SmallVector<Node*, 8> inputs(predecessor_count);
#ifdef DEBUG
    std::fill(inputs.begin(), inputs.end(), nullptr);
#endif

    int current_index = 0;
    for (const Block* pred : current_input_block->PredecessorsIterable()) {
      size_t pred_index = predecessor_count - current_index - 1;
      auto lower =
          std::lower_bound(new_predecessors.begin(), new_predecessors.end(),
                           GetBlock(*pred), cmp_basic_block);
      DCHECK_NE(lower, new_predecessors.end());
      size_t new_pred_index = std::distance(new_predecessors.begin(), lower);
      // Block {pred_index} became predecessor {new_pred_index} in the TF graph.
      // We thus put the input {pred_index} in position {new_pred_index}.
      inputs[new_pred_index] = GetNode(op.input(pred_index));
      ++current_index;
    }
    DCHECK(!base::contains(inputs, nullptr));

    return AddNode(common.Phi(op.rep.machine_representation(), op.input_count),
                   base::VectorOf(inputs));
  }
}
Node* ScheduleBuilder::ProcessOperation(const ProjectionOp& op) {
  return AddNode(common.Projection(op.index), {GetNode(op.input())});
}
Node* ScheduleBuilder::ProcessOperation(const AssumeMapOp&) {
  // AssumeMapOp is just a hint that optimization phases can use, but has no
  // Turbofan equivalent and is thus not used past this point.
  return nullptr;
}

std::pair<Node*, MachineType> ScheduleBuilder::BuildDeoptInput(
    FrameStateData::Iterator* it) {
  switch (it->current_instr()) {
    using Instr = FrameStateData::Instr;
    case Instr::kInput: {
      MachineType type;
      OpIndex input;
      it->ConsumeInput(&type, &input);
      const Operation& op = input_graph.Get(input);
      if (op.outputs_rep()[0] == RegisterRepresentation::Word64() &&
          type.representation() == MachineRepresentation::kWord32) {
        // 64 to 32-bit conversion is implicit in turboshaft, but explicit in
        // turbofan, so we insert this conversion.
        Node* conversion =
            AddNode(machine.TruncateInt64ToInt32(), {GetNode(input)});
        return {conversion, type};
      }
      return {GetNode(input), type};
    }
    case Instr::kDematerializedObject: {
      uint32_t obj_id;
      uint32_t field_count;
      it->ConsumeDematerializedObject(&obj_id, &field_count);
      base::SmallVector<Node*, 16> fields;
      ZoneVector<MachineType>& field_types =
          *tf_graph->zone()->New<ZoneVector<MachineType>>(field_count,
                                                          tf_graph->zone());
      for (uint32_t i = 0; i < field_count; ++i) {
        std::pair<Node*, MachineType> p = BuildDeoptInput(it);
        fields.push_back(p.first);
        field_types[i] = p.second;
      }
      return {AddNode(common.TypedObjectState(obj_id, &field_types),
                      base::VectorOf(fields)),
              MachineType::AnyTagged()};
    }
    case Instr::kDematerializedObjectReference: {
      uint32_t obj_id;
      it->ConsumeDematerializedObjectReference(&obj_id);
      return {AddNode(common.ObjectId(obj_id), {}), MachineType::AnyTagged()};
    }
    case Instr::kArgumentsElements: {
      CreateArgumentsType type;
      it->ConsumeArgumentsElements(&type);
      return {AddNode(common.ArgumentsElementsState(type), {}),
              MachineType::AnyTagged()};
    }
    case Instr::kArgumentsLength: {
      it->ConsumeArgumentsLength();
      return {AddNode(common.ArgumentsLengthState(), {}),
              MachineType::AnyTagged()};
    }
    case Instr::kRestLength:
      // For now, kRestLength is only generated when using the Maglev frontend,
      // which doesn't use recreate-schedule.
      [[fallthrough]];
    case Instr::kUnusedRegister:
      UNREACHABLE();
  }
}

// Create a mostly balanced tree of `StateValues` nodes.
Node* ScheduleBuilder::BuildStateValues(FrameStateData::Iterator* it,
                                        int32_t size) {
  constexpr int32_t kMaxStateValueInputCount = 8;

  base::SmallVector<Node*, kMaxStateValueInputCount> inputs;
  base::SmallVector<MachineType, kMaxStateValueInputCount> types;
  SparseInputMask::BitMaskType input_mask = 0;
  int32_t child_size =
      (size + kMaxStateValueInputCount - 1) / kMaxStateValueInputCount;
  // `state_value_inputs` counts the number of inputs used for the current
  // `StateValues` node. It is gradually adjusted as nodes are shifted to lower
  // levels in the tree.
  int32_t state_value_inputs = size;
  int32_t mask_size = 0;
  for (int32_t i = 0; i < state_value_inputs; ++i) {
    DCHECK_LT(i, kMaxStateValueInputCount);
    ++mask_size;
    if (state_value_inputs <= kMaxStateValueInputCount) {
      // All the remaining inputs fit at the current level.
      if (it->current_instr() == FrameStateData::Instr::kUnusedRegister) {
        it->ConsumeUnusedRegister();
      } else {
        std::pair<Node*, MachineType> p = BuildDeoptInput(it);
        input_mask |= SparseInputMask::BitMaskType{1} << i;
        inputs.push_back(p.first);
        types.push_back(p.second);
      }
    } else {
      // We have too many inputs, so recursively create another `StateValues`
      // node.
      input_mask |= SparseInputMask::BitMaskType{1} << i;
      int32_t actual_child_size = std::min(child_size, state_value_inputs - i);
      inputs.push_back(BuildStateValues(it, actual_child_size));
      // This is a dummy type that shouldn't matter.
      types.push_back(MachineType::AnyTagged());
      // `child_size`-many inputs were shifted to the next level, being replaced
      // with 1 `StateValues` node.
      state_value_inputs = state_value_inputs - actual_child_size + 1;
    }
  }
  input_mask |= SparseInputMask::kEndMarker << mask_size;
  return AddNode(
      common.TypedStateValues(graph_zone->New<ZoneVector<MachineType>>(
                                  types.begin(), types.end(), graph_zone),
                              SparseInputMask(input_mask)),
      base::VectorOf(inputs));
}

Node* ScheduleBuilder::BuildTaggedInput(FrameStateData::Iterator* it) {
  std::pair<Node*, MachineType> p = BuildDeoptInput(it);
  DCHECK(p.second.IsTagged());
  return p.first;
}

Node* ScheduleBuilder::ProcessOperation(const FrameStateOp& op) {
  const FrameStateInfo& info = op.data->frame_state_info;
  auto it = op.data->iterator(op.state_values());

  Node* closure = BuildTaggedInput(&it);
  Node* parameter_state_values = BuildStateValues(&it, info.parameter_count());
  Node* context = BuildTaggedInput(&it);
  Node* register_state_values = BuildStateValues(&it, info.local_count());
  Node* accumulator_state_values = BuildStateValues(&it, info.stack_count());
  Node* parent =
      op.inlined ? GetNode(op.parent_frame_state()) : tf_graph->start();

  return AddNode(common.FrameState(info.bailout_id(), info.state_combine(),
                                   info.function_info()),
                 {parameter_state_values, register_state_values,
                  accumulator_state_values, context, closure, parent});
}
Node* ScheduleBuilder::ProcessOperation(const CallOp& op) {
  base::SmallVector<Node*, 16> inputs;
  inputs.push_back(GetNode(op.callee()));
  for (OpIndex i : op.arguments()) {
    inputs.push_back(GetNode(i));
  }
  if (op.HasFrameState()) {
    DCHECK(op.frame_state().valid());
    inputs.push_back(GetNode(op.frame_state().value()));
  }
  return AddNode(common.Call(op.descriptor->descriptor),
                 base::VectorOf(inputs));
}
Node* ScheduleBuilder::ProcessOperation(const CheckExceptionOp& op) {
  Node* call_node = GetNode(op.throwing_operation());
  DCHECK_EQ(call_node->opcode(), IrOpcode::kCall);

  // Re-building the IfSuccess/IfException mechanism.
  BasicBlock* success_block = GetBlock(*op.didnt_throw_block);
  BasicBlock* exception_block = GetBlock(*op.catch_block);
  exception_block->set_deferred(true);
  schedule->AddCall(current_block, call_node, success_block, exception_block);
  // Pass `call` as the control input of `IfSuccess` and as both the effect and
  // control input of `IfException`.
  Node* if_success = MakeNode(common.IfSuccess(), {call_node});
  Node* if_exception = MakeNode(common.IfException(), {call_node, call_node});
  schedule->AddNode(success_block, if_success);
  schedule->AddNode(exception_block, if_exception);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const CatchBlockBeginOp& op) {
  Node* if_exception = current_block->NodeAt(0);
  DCHECK(if_exception != nullptr &&
         if_exception->opcode() == IrOpcode::kIfException);
  return if_exception;
}
Node* ScheduleBuilder::ProcessOperation(const DidntThrowOp& op) {
  return GetNode(op.throwing_operation());
}
Node* ScheduleBuilder::ProcessOperation(const TailCallOp& op) {
  base::SmallVector<Node*, 16> inputs;
  inputs.push_back(GetNode(op.callee()));
  for (OpIndex i : op.arguments()) {
    inputs.push_back(GetNode(i));
  }
  Node* call = MakeNode(common.TailCall(op.descriptor->descriptor),
                        base::VectorOf(inputs));
  schedule->AddTailCall(current_block, call);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const UnreachableOp& op) {
  Node* node = MakeNode(common.Throw(), {});
  schedule->AddNode(current_block, MakeNode(common.Unreachable(), {}));
  schedule->AddThrow(current_block, node);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const ReturnOp& op) {
  base::SmallVector<Node*, 8> inputs = {GetNode(op.pop_count())};
  for (OpIndex i : op.return_values()) {
    inputs.push_back(GetNode(i));
  }
  Node* node =
      MakeNode(common.Return(static_cast<int>(op.return_values().size())),
               base::VectorOf(inputs));
  schedule->AddReturn(current_block, node);
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const BranchOp& op) {
  Node* branch = MakeNode(common.Branch(op.hint), {GetNode(op.condition())});
  BasicBlock* true_block = GetBlock(*op.if_true);
  BasicBlock* false_block = GetBlock(*op.if_false);
  schedule->AddBranch(current_block, branch, true_block, false_block);
  schedule->AddNode(true_block, MakeNode(common.IfTrue(), {branch}));
  schedule->AddNode(false_block, MakeNode(common.IfFalse(), {branch}));
  switch (op.hint) {
    case BranchHint::kNone:
      break;
    case BranchHint::kTrue:
      false_block->set_deferred(true);
      break;
    case BranchHint::kFalse:
      true_block->set_deferred(true);
      break;
  }
  current_block = nullptr;
  return nullptr;
}
Node* ScheduleBuilder::ProcessOperation(const SwitchOp& op) {
  size_t succ_count = op.cases.size() + 1;
  Node* switch_node =
      MakeNode(common.Switch(succ_count), {GetNode(op.input())});

  base::SmallVector<BasicBlock*, 16> successors;
  for (SwitchOp::Case c : op.cases) {
    BasicBlock* case_block = GetBlock(*c.destination);
    successors.push_back(case_block);
    Node* case_node =
        MakeNode(common.IfValue(c.value, 0, c.hint), {switch_node});
    schedule->AddNode(case_block, case_node);
    if (c.hint == BranchHint::kFalse) {
      case_block->set_deferred(true);
    }
  }
  BasicBlock* default_block = GetBlock(*op.default_case);
  successors.push_back(default_block);
  schedule->AddNode(default_block,
                    MakeNode(common.IfDefault(op.default_hint), {switch_node}));
  if (op.default_hint == BranchHint::kFalse) {
    default_block->set_deferred(true);
  }

  schedule->AddSwitch(current_block, switch_node, successors.data(),
                      successors.size());
  current_block = nullptr;
  return nullptr;
}

Node* ScheduleBuilder::ProcessOperation(const DebugBreakOp& op) {
  return AddNode(machine.DebugBreak(), {});
}

Node* ScheduleBuilder::ProcessOperation(const LoadRootRegisterOp& op) {
  return AddNode(machine.LoadRootRegister(), {});
}

Node* ScheduleBuilder::ProcessOperation(const Word32PairBinopOp& op) {
  using Kind = Word32PairBinopOp::Kind;
  const Operator* pair_operator = nullptr;
  switch (op.kind) {
    case Kind::kAdd:
      pair_operator = machine.Int32PairAdd();
      break;
    case Kind::kSub:
      pair_operator = machine.Int32PairSub();
      break;
    case Kind::kMul:
      pair_operator = machine.Int32PairMul();
      break;
    case Kind::kShiftLeft:
      pair_operator = machine.Word32PairShl();
      break;
    case Kind::kShiftRightArithmetic:
      pair_operator = machine.Word32PairSar();
      break;
    case Kind::kShiftRightLogical:
      pair_operator = machine.Word32PairShr();
      break;
  }
  return AddNode(pair_operator,
                 {GetNode(op.left_low()), GetNode(op.left_high()),
                  GetNode(op.right_low()), GetNode(op.right_high())});
}

Node* ScheduleBuilder::ProcessOperation(const CommentOp& op) {
  return AddNode(machine.Comment(op.message), {});
}

#ifdef V8_ENABLE_WEBASSEMBLY
Node* ScheduleBuilder::ProcessOperation(const Simd128ConstantOp& op) {
  return AddNode(machine.S128Const(op.value), {});
}

Node* ScheduleBuilder::ProcessOperation(const Simd128BinopOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd128BinopOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.left()), GetNode(op.right())});
    FOREACH_SIMD_128_BINARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128UnaryOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd128UnaryOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.input())});
    FOREACH_SIMD_128_UNARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128ReduceOp& op) {
  UNIMPLEMENTED();
}

Node* ScheduleBuilder::ProcessOperation(const Simd128ShiftOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd128ShiftOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.input()), GetNode(op.shift())});
    FOREACH_SIMD_128_SHIFT_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128TestOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)            \
  case Simd128TestOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.input())});
    FOREACH_SIMD_128_TEST_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128SplatOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd128SplatOp::Kind::k##kind: \
    return AddNode(machine.kind##Splat(), {GetNode(op.input())});
    FOREACH_SIMD_128_SPLAT_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128TernaryOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)                                                      \
  case Simd128TernaryOp::Kind::k##kind:                                        \
    return AddNode(machine.kind(), {GetNode(op.first()), GetNode(op.second()), \
                                    GetNode(op.third())});
    FOREACH_SIMD_128_TERNARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd128ExtractLaneOp& op) {
  const Operator* o = nullptr;
  switch (op.kind) {
    case Simd128ExtractLaneOp::Kind::kI8x16S:
      o = machine.I8x16ExtractLaneS(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kI8x16U:
      o = machine.I8x16ExtractLaneU(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kI16x8S:
      o = machine.I16x8ExtractLaneS(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kI16x8U:
      o = machine.I16x8ExtractLaneU(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kI32x4:
      o = machine.I32x4ExtractLane(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kI64x2:
      o = machine.I64x2ExtractLane(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kF16x8:
      o = machine.F16x8ExtractLane(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kF32x4:
      o = machine.F32x4ExtractLane(op.lane);
      break;
    case Simd128ExtractLaneOp::Kind::kF64x2:
      o = machine.F64x2ExtractLane(op.lane);
      break;
  }

  return AddNode(o, {GetNode(op.input())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd128ReplaceLaneOp& op) {
  const Operator* o = nullptr;
  switch (op.kind) {
    case Simd128ReplaceLaneOp::Kind::kI8x16:
      o = machine.I8x16ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kI16x8:
      o = machine.I16x8ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kI32x4:
      o = machine.I32x4ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kI64x2:
      o = machine.I64x2ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kF16x8:
      o = machine.F16x8ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kF32x4:
      o = machine.F32x4ReplaceLane(op.lane);
      break;
    case Simd128ReplaceLaneOp::Kind::kF64x2:
      o = machine.F64x2ReplaceLane(op.lane);
      break;
  }

  return AddNode(o, {GetNode(op.into()), GetNode(op.new_lane())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd128LaneMemoryOp& op) {
  DCHECK_EQ(op.offset, 0);
  MemoryAccessKind access =
      op.kind.with_trap_handler ? MemoryAccessKind::kProtected
      : op.kind.maybe_unaligned ? MemoryAccessKind::kUnaligned
                                : MemoryAccessKind::kNormal;

  MachineType type;
  switch (op.lane_kind) {
    case Simd128LaneMemoryOp::LaneKind::k8:
      type = MachineType::Int8();
      break;
    case Simd128LaneMemoryOp::LaneKind::k16:
      type = MachineType::Int16();
      break;
    case Simd128LaneMemoryOp::LaneKind::k32:
      type = MachineType::Int32();
      break;
    case Simd128LaneMemoryOp::LaneKind::k64:
      type = MachineType::Int64();
      break;
  }

  const Operator* o = nullptr;
  if (op.mode == Simd128LaneMemoryOp::Mode::kLoad) {
    o = machine.LoadLane(access, type, op.lane);
  } else {
    o = machine.StoreLane(access, type.representation(), op.lane);
  }

  return AddNode(
      o, {GetNode(op.base()), GetNode(op.index()), GetNode(op.value())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd128LoadTransformOp& op) {
  DCHECK_EQ(op.offset, 0);
  MemoryAccessKind access =
      op.load_kind.with_trap_handler ? MemoryAccessKind::kProtected
      : op.load_kind.maybe_unaligned ? MemoryAccessKind::kUnaligned
                                     : MemoryAccessKind::kNormal;
  LoadTransformation transformation;
  switch (op.transform_kind) {
#define HANDLE_KIND(kind)                                 \
  case Simd128LoadTransformOp::TransformKind::k##kind:    \
    transformation = LoadTransformation::kS128Load##kind; \
    break;
    FOREACH_SIMD_128_LOAD_TRANSFORM_OPCODE(HANDLE_KIND)
#undef HANDLE_KIND
  }

  const Operator* o = machine.LoadTransform(access, transformation);

  return AddNode(o, {GetNode(op.base()), GetNode(op.index())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd128ShuffleOp& op) {
  return AddNode(machine.I8x16Shuffle(op.shuffle),
                 {GetNode(op.left()), GetNode(op.right())});
}

#if V8_ENABLE_WASM_SIMD256_REVEC
Node* ScheduleBuilder::ProcessOperation(const Simd256ConstantOp& op) {
  return AddNode(machine.S256Const(op.value), {});
}

Node* ScheduleBuilder::ProcessOperation(const Simd256Extract128LaneOp& op) {
  const Operator* o = machine.ExtractF128(op.lane);
  return AddNode(o, {GetNode(op.input())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd256LoadTransformOp& op) {
  DCHECK_EQ(op.offset, 0);
  MemoryAccessKind access =
      op.load_kind.with_trap_handler ? MemoryAccessKind::kProtected
      : op.load_kind.maybe_unaligned ? MemoryAccessKind::kUnaligned
                                     : MemoryAccessKind::kNormal;
  LoadTransformation transformation;
  switch (op.transform_kind) {
#define HANDLE_KIND(kind)                                 \
  case Simd256LoadTransformOp::TransformKind::k##kind:    \
    transformation = LoadTransformation::kS256Load##kind; \
    break;
    FOREACH_SIMD_256_LOAD_TRANSFORM_OPCODE(HANDLE_KIND)
#undef HANDLE_KIND
  }

  const Operator* o = machine.LoadTransform(access, transformation);

  return AddNode(o, {GetNode(op.base()), GetNode(op.index())});
}

Node* ScheduleBuilder::ProcessOperation(const Simd256UnaryOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd256UnaryOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.input())});
    FOREACH_SIMD_256_UNARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd256BinopOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd256BinopOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.left()), GetNode(op.right())});
    FOREACH_SIMD_256_BINARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd256ShiftOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd256ShiftOp::Kind::k##kind: \
    return AddNode(machine.kind(), {GetNode(op.input()), GetNode(op.shift())});
    FOREACH_SIMD_256_SHIFT_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd256TernaryOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)                                                      \
  case Simd256TernaryOp::Kind::k##kind:                                        \
    return AddNode(machine.kind(), {GetNode(op.first()), GetNode(op.second()), \
                                    GetNode(op.third())});
    FOREACH_SIMD_256_TERNARY_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const Simd256SplatOp& op) {
  switch (op.kind) {
#define HANDLE_KIND(kind)             \
  case Simd256SplatOp::Kind::k##kind: \
    return AddNode(machine.kind##Splat(), {GetNode(op.input())});
    FOREACH_SIMD_256_SPLAT_OPCODE(HANDLE_KIND);
#undef HANDLE_KIND
  }
}

Node* ScheduleBuilder::ProcessOperation(const SimdPack128To256Op& op) {
  UNREACHABLE();
}

#ifdef V8_TARGET_ARCH_X64
Node* ScheduleBuilder::ProcessOperation(const Simd256ShufdOp& op) {
  UNIMPLEMENTED();
}
Node* ScheduleBuilder::ProcessOperation(const Simd256ShufpsOp& op) {
  UNIMPLEMENTED();
}
Node* ScheduleBuilder::ProcessOperation(const Simd256UnpackOp& op) {
  UNIMPLEMENTED();
}
#endif  // V8_TARGET_ARCH_X64
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

Node* ScheduleBuilder::ProcessOperation(const LoadStackPointerOp& op) {
  return AddNode(machine.LoadStackPointer(), {});
}

Node* ScheduleBuilder::ProcessOperation(const SetStackPointerOp& op) {
  return AddNode(machine.SetStackPointer(), {GetNode(op.value())});
}

#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace

RecreateScheduleResult RecreateSchedule(PipelineData* data,
                                        compiler::TFPipelineData* turbofan_data,
                                        CallDescriptor* call_descriptor,
                                        Zone* phase_zone) {
  ScheduleBuilder builder{data, call_descriptor, phase_zone, turbofan_data};
  return builder.Run();
}

}  // namespace v8::internal::compiler::turboshaft
