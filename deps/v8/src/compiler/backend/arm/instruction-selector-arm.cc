// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/base/bits.h"
#include "src/base/enum-set.h"
#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction-selector-adapter.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"

namespace v8 {
namespace internal {
namespace compiler {

// Adds Arm-specific methods for generating InstructionOperands.
template <typename Adapter>
class ArmOperandGeneratorT : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit ArmOperandGeneratorT(InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  bool CanBeImmediate(int32_t value) const {
    return Assembler::ImmediateFitsAddrMode1Instruction(value);
  }

  bool CanBeImmediate(uint32_t value) const {
    return CanBeImmediate(base::bit_cast<int32_t>(value));
  }

  bool CanBeImmediate(node_t node, InstructionCode opcode) {
    if (!selector()->is_integer_constant(node)) return false;
    int64_t value64 = selector()->integer_constant(node);
    DCHECK(base::IsInRange(value64, std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max()));
    int32_t value = static_cast<int32_t>(value64);
    switch (ArchOpcodeField::decode(opcode)) {
      case kArmAnd:
      case kArmMov:
      case kArmMvn:
      case kArmBic:
        return CanBeImmediate(value) || CanBeImmediate(~value);

      case kArmAdd:
      case kArmSub:
      case kArmCmp:
      case kArmCmn:
        return CanBeImmediate(value) || CanBeImmediate(-value);

      case kArmTst:
      case kArmTeq:
      case kArmOrr:
      case kArmEor:
      case kArmRsb:
        return CanBeImmediate(value);

      case kArmVldrF32:
      case kArmVstrF32:
      case kArmVldrF64:
      case kArmVstrF64:
        return value >= -1020 && value <= 1020 && (value % 4) == 0;

      case kArmLdrb:
      case kArmLdrsb:
      case kArmStrb:
      case kArmLdr:
      case kArmStr:
        return value >= -4095 && value <= 4095;

      case kArmLdrh:
      case kArmLdrsh:
      case kArmStrh:
        return value >= -255 && value <= 255;

      default:
        break;
    }
    return false;
  }
};

namespace {

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
             typename Adapter::node_t node) {
  ArmOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node) {
  ArmOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)));
}

#if V8_ENABLE_WEBASSEMBLY
void VisitSimdShiftRRR(InstructionSelectorT<TurboshaftAdapter>* selector,
                       ArchOpcode opcode, turboshaft::OpIndex node, int width) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(selector);
  const Simd128ShiftOp& op = selector->Get(node).Cast<Simd128ShiftOp>();
  int32_t shift_by;
  if (selector->MatchIntegralWord32Constant(op.shift(), &shift_by)) {
    if (shift_by % width == 0) {
      selector->EmitIdentity(node);
    } else {
      selector->Emit(opcode, g.DefineAsRegister(node),
                     g.UseRegister(op.input()), g.UseImmediate(op.shift()));
    }
  } else {
    VisitRRR(selector, opcode, node);
  }
}

void VisitSimdShiftRRR(InstructionSelectorT<TurbofanAdapter>* selector,
                       ArchOpcode opcode, Node* node, int width) {
  ArmOperandGeneratorT<TurbofanAdapter> g(selector);
  Int32Matcher m(node->InputAt(1));
  if (m.HasResolvedValue()) {
    if (m.IsMultipleOf(width)) {
      selector->EmitIdentity(node);
    } else {
      selector->Emit(opcode, g.DefineAsRegister(node),
                     g.UseRegister(node->InputAt(0)),
                     g.UseImmediate(node->InputAt(1)));
    }
  } else {
    VisitRRR(selector, opcode, node);
  }
}

template <typename Adapter>
void VisitRRRShuffle(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
                     typename Adapter::node_t node,
                     typename Adapter::node_t input0,
                     typename Adapter::node_t input1) {
  ArmOperandGeneratorT<Adapter> g(selector);
  // Swap inputs to save an instruction in the CodeGenerator for High ops.
  if (opcode == kArmS32x4ZipRight || opcode == kArmS32x4UnzipRight ||
      opcode == kArmS32x4TransposeRight || opcode == kArmS16x8ZipRight ||
      opcode == kArmS16x8UnzipRight || opcode == kArmS16x8TransposeRight ||
      opcode == kArmS8x16ZipRight || opcode == kArmS8x16UnzipRight ||
      opcode == kArmS8x16TransposeRight) {
    std::swap(input0, input1);
  }
  // Use DefineSameAsFirst for binary ops that clobber their inputs, e.g. the
  // NEON vzip, vuzp, and vtrn instructions.
  selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(input0),
                 g.UseRegister(input1));
}

template <typename Adapter>
void VisitRRI(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
              typename Adapter::node_t node) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = selector->Get(node);
    int imm = op.template Cast<Simd128ExtractLaneOp>().lane;
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)),
                   g.UseImmediate(imm));
  } else {
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(node->InputAt(0)), g.UseImmediate(imm));
  }
}

template <typename Adapter>
void VisitRRIR(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
               typename Adapter::node_t node) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const turboshaft::Simd128ReplaceLaneOp& op =
        selector->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.into()),
                   g.UseImmediate(op.lane), g.UseUniqueRegister(op.new_lane()));
  } else {
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(node->InputAt(0)), g.UseImmediate(imm),
                   g.UseUniqueRegister(node->InputAt(1)));
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <IrOpcode::Value kOpcode, int kImmMin, int kImmMax,
          AddressingMode kImmMode, AddressingMode kRegMode>
bool TryMatchShift(InstructionSelectorT<TurbofanAdapter>* selector,
                   InstructionCode* opcode_return, Node* node,
                   InstructionOperand* value_return,
                   InstructionOperand* shift_return) {
  ArmOperandGeneratorT<TurbofanAdapter> g(selector);
  if (node->opcode() == kOpcode) {
    Int32BinopMatcher m(node);
    *value_return = g.UseRegister(m.left().node());
    if (m.right().IsInRange(kImmMin, kImmMax)) {
      *opcode_return |= AddressingModeField::encode(kImmMode);
      *shift_return = g.UseImmediate(m.right().node());
    } else {
      *opcode_return |= AddressingModeField::encode(kRegMode);
      *shift_return = g.UseRegister(m.right().node());
    }
    return true;
  }
  return false;
}

template <typename OpmaskT, int kImmMin, int kImmMax, AddressingMode kImmMode,
          AddressingMode kRegMode>
bool TryMatchShift(InstructionSelectorT<TurboshaftAdapter>* selector,
                   InstructionCode* opcode_return, turboshaft::OpIndex node,
                   InstructionOperand* value_return,
                   InstructionOperand* shift_return) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(selector);
  const Operation& op = selector->Get(node);
  if (op.Is<OpmaskT>()) {
    const ShiftOp& shift = op.Cast<ShiftOp>();
    *value_return = g.UseRegister(shift.left());
    int32_t shift_by;
    if (selector->MatchIntegralWord32Constant(shift.right(), &shift_by) &&
        base::IsInRange(shift_by, kImmMin, kImmMax)) {
      *opcode_return |= AddressingModeField::encode(kImmMode);
      *shift_return = g.UseImmediate(shift.right());
    } else {
      *opcode_return |= AddressingModeField::encode(kRegMode);
      *shift_return = g.UseRegister(shift.right());
    }
    return true;
  }
  return false;
}

template <IrOpcode::Value kOpcode, int kImmMin, int kImmMax,
          AddressingMode kImmMode>
bool TryMatchShiftImmediate(InstructionSelectorT<TurbofanAdapter>* selector,
                            InstructionCode* opcode_return, Node* node,
                            InstructionOperand* value_return,
                            InstructionOperand* shift_return) {
  ArmOperandGeneratorT<TurbofanAdapter> g(selector);
  if (node->opcode() == kOpcode) {
    Int32BinopMatcher m(node);
    if (m.right().IsInRange(kImmMin, kImmMax)) {
      *opcode_return |= AddressingModeField::encode(kImmMode);
      *value_return = g.UseRegister(m.left().node());
      *shift_return = g.UseImmediate(m.right().node());
      return true;
    }
  }
  return false;
}

template <typename OpmaskT, int kImmMin, int kImmMax, AddressingMode kImmMode>
bool TryMatchShiftImmediate(InstructionSelectorT<TurboshaftAdapter>* selector,
                            InstructionCode* opcode_return,
                            turboshaft::OpIndex node,
                            InstructionOperand* value_return,
                            InstructionOperand* shift_return) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(selector);
  const Operation& op = selector->Get(node);
  if (op.Is<OpmaskT>()) {
    const ShiftOp& shift = op.Cast<ShiftOp>();
    int32_t shift_by;
    if (selector->MatchIntegralWord32Constant(shift.right(), &shift_by) &&
        base::IsInRange(shift_by, kImmMin, kImmMax)) {
      *opcode_return |= AddressingModeField::encode(kImmMode);
      *value_return = g.UseRegister(shift.left());
      *shift_return = g.UseImmediate(shift.right());
      return true;
    }
  }
  return false;
}

template <typename Adapter>
bool TryMatchROR(InstructionSelectorT<Adapter>* selector,
                 InstructionCode* opcode_return, typename Adapter::node_t node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<IrOpcode::kWord32Ror, 1, 31, kMode_Operand2_R_ROR_I,
                       kMode_Operand2_R_ROR_R>(selector, opcode_return, node,
                                               value_return, shift_return);
}

template <>
bool TryMatchROR(InstructionSelectorT<TurboshaftAdapter>* selector,
                 InstructionCode* opcode_return, turboshaft::OpIndex node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<turboshaft::Opmask::kWord32RotateRight, 1, 31,
                       kMode_Operand2_R_ROR_I, kMode_Operand2_R_ROR_R>(
      selector, opcode_return, node, value_return, shift_return);
}

template <typename Adapter>
bool TryMatchASR(InstructionSelectorT<Adapter>* selector,
                 InstructionCode* opcode_return, typename Adapter::node_t node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<IrOpcode::kWord32Sar, 1, 32, kMode_Operand2_R_ASR_I,
                       kMode_Operand2_R_ASR_R>(selector, opcode_return, node,
                                               value_return, shift_return);
}

template <>
bool TryMatchASR(InstructionSelectorT<TurboshaftAdapter>* selector,
                 InstructionCode* opcode_return, turboshaft::OpIndex node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<turboshaft::Opmask::kWord32ShiftRightArithmetic, 1, 32,
                       kMode_Operand2_R_ASR_I, kMode_Operand2_R_ASR_R>(
             selector, opcode_return, node, value_return, shift_return) ||
         TryMatchShift<
             turboshaft::Opmask::kWord32ShiftRightArithmeticShiftOutZeros, 1,
             32, kMode_Operand2_R_ASR_I, kMode_Operand2_R_ASR_R>(
             selector, opcode_return, node, value_return, shift_return);
}

template <typename Adapter>
bool TryMatchLSL(InstructionSelectorT<Adapter>* selector,
                 InstructionCode* opcode_return, typename Adapter::node_t node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<IrOpcode::kWord32Shl, 0, 31, kMode_Operand2_R_LSL_I,
                       kMode_Operand2_R_LSL_R>(selector, opcode_return, node,
                                               value_return, shift_return);
}

template <>
bool TryMatchLSL(InstructionSelectorT<TurboshaftAdapter>* selector,
                 InstructionCode* opcode_return, turboshaft::OpIndex node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<turboshaft::Opmask::kWord32ShiftLeft, 0, 31,
                       kMode_Operand2_R_LSL_I, kMode_Operand2_R_LSL_R>(
      selector, opcode_return, node, value_return, shift_return);
}

template <typename Adapter>
bool TryMatchLSLImmediate(InstructionSelectorT<Adapter>* selector,
                          InstructionCode* opcode_return,
                          typename Adapter::node_t node,
                          InstructionOperand* value_return,
                          InstructionOperand* shift_return) {
  return TryMatchShiftImmediate<IrOpcode::kWord32Shl, 0, 31,
                                kMode_Operand2_R_LSL_I>(
      selector, opcode_return, node, value_return, shift_return);
}

template <>
bool TryMatchLSLImmediate(InstructionSelectorT<TurboshaftAdapter>* selector,
                          InstructionCode* opcode_return,
                          turboshaft::OpIndex node,
                          InstructionOperand* value_return,
                          InstructionOperand* shift_return) {
  return TryMatchShiftImmediate<turboshaft::Opmask::kWord32ShiftLeft, 0, 31,
                                kMode_Operand2_R_LSL_I>(
      selector, opcode_return, node, value_return, shift_return);
}

template <typename Adapter>
bool TryMatchLSR(InstructionSelectorT<Adapter>* selector,
                 InstructionCode* opcode_return, typename Adapter::node_t node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<IrOpcode::kWord32Shr, 1, 32, kMode_Operand2_R_LSR_I,
                       kMode_Operand2_R_LSR_R>(selector, opcode_return, node,
                                               value_return, shift_return);
}

template <>
bool TryMatchLSR(InstructionSelectorT<TurboshaftAdapter>* selector,
                 InstructionCode* opcode_return, turboshaft::OpIndex node,
                 InstructionOperand* value_return,
                 InstructionOperand* shift_return) {
  return TryMatchShift<turboshaft::Opmask::kWord32ShiftRightLogical, 1, 32,
                       kMode_Operand2_R_LSR_I, kMode_Operand2_R_LSR_R>(
      selector, opcode_return, node, value_return, shift_return);
}

template <typename Adapter>
bool TryMatchShift(InstructionSelectorT<Adapter>* selector,
                   InstructionCode* opcode_return,
                   typename Adapter::node_t node,
                   InstructionOperand* value_return,
                   InstructionOperand* shift_return) {
  return (
      TryMatchASR(selector, opcode_return, node, value_return, shift_return) ||
      TryMatchLSL(selector, opcode_return, node, value_return, shift_return) ||
      TryMatchLSR(selector, opcode_return, node, value_return, shift_return) ||
      TryMatchROR(selector, opcode_return, node, value_return, shift_return));
}

template <typename Adapter>
bool TryMatchImmediateOrShift(InstructionSelectorT<Adapter>* selector,
                              InstructionCode* opcode_return,
                              typename Adapter::node_t node,
                              size_t* input_count_return,
                              InstructionOperand* inputs) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if (g.CanBeImmediate(node, *opcode_return)) {
    *opcode_return |= AddressingModeField::encode(kMode_Operand2_I);
    inputs[0] = g.UseImmediate(node);
    *input_count_return = 1;
    return true;
  }
  if (TryMatchShift(selector, opcode_return, node, &inputs[0], &inputs[1])) {
    *input_count_return = 2;
    return true;
  }
  return false;
}

template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                InstructionCode reverse_opcode,
                FlagsContinuationT<Adapter>* cont) {
  using node_t = typename Adapter::node_t;
  ArmOperandGeneratorT<Adapter> g(selector);
  node_t lhs = selector->input_at(node, 0);
  node_t rhs = selector->input_at(node, 1);
  InstructionOperand inputs[3];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  if (lhs == rhs) {
    // If both inputs refer to the same operand, enforce allocating a register
    // for both of them to ensure that we don't end up generating code like
    // this:
    //
    //   mov r0, r1, asr #16
    //   adds r0, r0, r1, asr #16
    //   bvs label
    InstructionOperand const input = g.UseRegister(lhs);
    opcode |= AddressingModeField::encode(kMode_Operand2_R);
    inputs[input_count++] = input;
    inputs[input_count++] = input;
  } else if (TryMatchImmediateOrShift(selector, &opcode, rhs, &input_count,
                                      &inputs[1])) {
    inputs[0] = g.UseRegister(lhs);
    input_count++;
  } else if (TryMatchImmediateOrShift(selector, &reverse_opcode, lhs,
                                      &input_count, &inputs[1])) {
    inputs[0] = g.UseRegister(rhs);
    opcode = reverse_opcode;
    input_count++;
  } else {
    opcode |= AddressingModeField::encode(kMode_Operand2_R);
    inputs[input_count++] = g.UseRegister(lhs);
    inputs[input_count++] = g.UseRegister(rhs);
  }

  outputs[output_count++] = g.DefineAsRegister(node);

  DCHECK_NE(0u, input_count);
  DCHECK_EQ(1u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);
  DCHECK_NE(kMode_None, AddressingModeField::decode(opcode));

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                InstructionCode reverse_opcode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop(selector, node, opcode, reverse_opcode, &cont);
}

template <typename Adapter>
void EmitDiv(InstructionSelectorT<Adapter>* selector, ArchOpcode div_opcode,
             ArchOpcode f64i32_opcode, ArchOpcode i32f64_opcode,
             InstructionOperand result_operand, InstructionOperand left_operand,
             InstructionOperand right_operand) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if (selector->IsSupported(SUDIV)) {
    selector->Emit(div_opcode, result_operand, left_operand, right_operand);
    return;
  }
  InstructionOperand left_double_operand = g.TempDoubleRegister();
  InstructionOperand right_double_operand = g.TempDoubleRegister();
  InstructionOperand result_double_operand = g.TempDoubleRegister();
  selector->Emit(f64i32_opcode, left_double_operand, left_operand);
  selector->Emit(f64i32_opcode, right_double_operand, right_operand);
  selector->Emit(kArmVdivF64, result_double_operand, left_double_operand,
                 right_double_operand);
  selector->Emit(i32f64_opcode, result_operand, result_double_operand);
}

template <typename Adapter>
void VisitDiv(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode div_opcode,
              ArchOpcode f64i32_opcode, ArchOpcode i32f64_opcode) {
    ArmOperandGeneratorT<Adapter> g(selector);
    EmitDiv(selector, div_opcode, f64i32_opcode, i32f64_opcode,
            g.DefineAsRegister(node),
            g.UseRegister(selector->input_at(node, 0)),
            g.UseRegister(selector->input_at(node, 1)));
}

template <typename Adapter>
void VisitMod(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode div_opcode,
              ArchOpcode f64i32_opcode, ArchOpcode i32f64_opcode) {
  ArmOperandGeneratorT<Adapter> g(selector);
  InstructionOperand div_operand = g.TempRegister();
  InstructionOperand result_operand = g.DefineAsRegister(node);
  InstructionOperand left_operand = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand right_operand = g.UseRegister(selector->input_at(node, 1));
  EmitDiv(selector, div_opcode, f64i32_opcode, i32f64_opcode, div_operand,
          left_operand, right_operand);
  if (selector->IsSupported(ARMv7)) {
    selector->Emit(kArmMls, result_operand, div_operand, right_operand,
                   left_operand);
  } else {
    InstructionOperand mul_operand = g.TempRegister();
    selector->Emit(kArmMul, mul_operand, div_operand, right_operand);
    selector->Emit(kArmSub | AddressingModeField::encode(kMode_Operand2_R),
                   result_operand, left_operand, mul_operand);
  }
}

// Adds the base and offset into a register, then change the addressing
// mode of opcode_return to use this register. Certain instructions, e.g.
// vld1 and vst1, when given two registers, will post-increment the offset, i.e.
// perform the operation at base, then add offset to base. What we intend is to
// access at (base+offset).
template <typename Adapter>
void EmitAddBeforeS128LoadStore(InstructionSelectorT<Adapter>* selector,
                                InstructionCode* opcode_return,
                                size_t* input_count_return,
                                InstructionOperand* inputs) {
  ArmOperandGeneratorT<Adapter> g(selector);
  InstructionOperand addr = g.TempRegister();
  InstructionCode op = kArmAdd;
  op |= AddressingModeField::encode(kMode_Operand2_R);
  selector->Emit(op, 1, &addr, 2, inputs);
  *opcode_return |= AddressingModeField::encode(kMode_Operand2_R);
  *input_count_return -= 1;
  inputs[0] = addr;
}

void EmitLoad(InstructionSelectorT<TurboshaftAdapter>* selector,
              InstructionCode opcode, InstructionOperand* output,
              turboshaft::OpIndex base, turboshaft::OpIndex index) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(selector);
  InstructionOperand inputs[3];
  size_t input_count = 2;

  const Operation& base_op = selector->Get(base);
  if (base_op.Is<Opmask::kExternalConstant>() &&
      selector->is_integer_constant(index)) {
    const ConstantOp& constant_base = base_op.Cast<ConstantOp>();
    if (selector->CanAddressRelativeToRootsRegister(
            constant_base.external_reference())) {
      ptrdiff_t const delta =
          selector->integer_constant(index) +
          MacroAssemblerBase::RootRegisterOffsetForExternalReference(
              selector->isolate(), constant_base.external_reference());
      input_count = 1;
      inputs[0] = g.UseImmediate(static_cast<int32_t>(delta));
      opcode |= AddressingModeField::encode(kMode_Root);
      selector->Emit(opcode, 1, output, input_count, inputs);
      return;
    }
  }

  if (base_op.Is<LoadRootRegisterOp>()) {
    input_count = 1;
    // This will only work if {index} is a constant.
    inputs[0] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Root);
    selector->Emit(opcode, 1, output, input_count, inputs);
    return;
  }

  inputs[0] = g.UseRegister(base);
  if (g.CanBeImmediate(index, opcode)) {
    inputs[1] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Offset_RI);
  } else if ((opcode == kArmLdr) &&
             TryMatchLSLImmediate(selector, &opcode, index, &inputs[1],
                                  &inputs[2])) {
    input_count = 3;
  } else {
    inputs[1] = g.UseRegister(index);
    if (opcode == kArmVld1S128) {
      EmitAddBeforeS128LoadStore(selector, &opcode, &input_count, &inputs[0]);
    } else {
      opcode |= AddressingModeField::encode(kMode_Offset_RR);
    }
  }
  selector->Emit(opcode, 1, output, input_count, inputs);
}

void EmitLoad(InstructionSelectorT<TurbofanAdapter>* selector,
              InstructionCode opcode, InstructionOperand* output, Node* base,
              Node* index) {
  ArmOperandGeneratorT<TurbofanAdapter> g(selector);
  InstructionOperand inputs[3];
  size_t input_count = 2;

  ExternalReferenceMatcher m(base);
  if (m.HasResolvedValue() &&
      selector->CanAddressRelativeToRootsRegister(m.ResolvedValue())) {
    Int32Matcher int_matcher(index);
    if (int_matcher.HasResolvedValue()) {
      ptrdiff_t const delta =
          int_matcher.ResolvedValue() +
          MacroAssemblerBase::RootRegisterOffsetForExternalReference(
              selector->isolate(), m.ResolvedValue());
      input_count = 1;
      inputs[0] = g.UseImmediate(static_cast<int32_t>(delta));
      opcode |= AddressingModeField::encode(kMode_Root);
      selector->Emit(opcode, 1, output, input_count, inputs);
      return;
    }
  }

  if (base->opcode() == IrOpcode::kLoadRootRegister) {
    input_count = 1;
    // This will only work if {index} is a constant.
    inputs[0] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Root);
    selector->Emit(opcode, 1, output, input_count, inputs);
    return;
  }

  inputs[0] = g.UseRegister(base);
  if (g.CanBeImmediate(index, opcode)) {
    inputs[1] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Offset_RI);
  } else if ((opcode == kArmLdr) &&
             TryMatchLSLImmediate(selector, &opcode, index, &inputs[1],
                                  &inputs[2])) {
    input_count = 3;
  } else {
    inputs[1] = g.UseRegister(index);
    if (opcode == kArmVld1S128) {
      EmitAddBeforeS128LoadStore(selector, &opcode, &input_count, &inputs[0]);
    } else {
      opcode |= AddressingModeField::encode(kMode_Offset_RR);
    }
  }
  selector->Emit(opcode, 1, output, input_count, inputs);
}

template <typename Adapter>
void EmitStore(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
               size_t input_count, InstructionOperand* inputs,
               typename Adapter::node_t index) {
  ArmOperandGeneratorT<Adapter> g(selector);
  ArchOpcode arch_opcode = ArchOpcodeField::decode(opcode);

  if (g.CanBeImmediate(index, opcode)) {
    inputs[input_count++] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Offset_RI);
  } else if ((arch_opcode == kArmStr || arch_opcode == kAtomicStoreWord32) &&
             TryMatchLSLImmediate(selector, &opcode, index, &inputs[2],
                                  &inputs[3])) {
    input_count = 4;
  } else {
    inputs[input_count++] = g.UseRegister(index);
    if (arch_opcode == kArmVst1S128) {
      // Inputs are value, base, index, only care about base and index.
      EmitAddBeforeS128LoadStore(selector, &opcode, &input_count, &inputs[1]);
    } else {
      opcode |= AddressingModeField::encode(kMode_Offset_RR);
    }
  }
  selector->Emit(opcode, 0, nullptr, input_count, inputs);
}

template <typename Adapter>
void VisitPairAtomicBinOp(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node, ArchOpcode opcode) {
  ArmOperandGeneratorT<Adapter> g(selector);
  using node_t = typename Adapter::node_t;
  node_t base = selector->input_at(node, 0);
  node_t index = selector->input_at(node, 1);
  node_t value = selector->input_at(node, 2);
  node_t value_high = selector->input_at(node, 3);
  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  InstructionOperand inputs[] = {
      g.UseUniqueRegister(value), g.UseUniqueRegister(value_high),
      g.UseUniqueRegister(base), g.UseUniqueRegister(index)};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  InstructionOperand temps[6];
  size_t temp_count = 0;
  temps[temp_count++] = g.TempRegister();
  temps[temp_count++] = g.TempRegister(r6);
  temps[temp_count++] = g.TempRegister(r7);
  temps[temp_count++] = g.TempRegister();
  node_t projection0 = selector->FindProjection(node, 0);
  node_t projection1 = selector->FindProjection(node, 1);
  if (selector->valid(projection0)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, r2);
  } else {
    temps[temp_count++] = g.TempRegister(r2);
  }
  if (selector->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, r3);
  } else {
    temps[temp_count++] = g.TempRegister(r3);
  }
  selector->Emit(code, output_count, outputs, arraysize(inputs), inputs,
                 temp_count, temps);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackSlot(node_t node) {
  StackSlotRepresentation rep = this->stack_slot_representation_of(node);
  int slot =
      frame_->AllocateSpillSlot(rep.size(), rep.alignment(), rep.is_tagged());
  OperandGenerator g(this);

  Emit(kArchStackSlot, g.DefineAsRegister(node),
       sequence()->AddImmediate(Constant(slot)), 0, nullptr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitAbortCSADcheck(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // This is currently not used by Turboshaft.
    UNIMPLEMENTED();
  } else {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArchAbortCSADcheck, g.NoOutput(), g.UseFixed(node->InputAt(0), r1));
  }
}

#if V8_ENABLE_WEBASSEMBLY
namespace {
MachineRepresentation MachineRepresentationOf(
    turboshaft::Simd128LaneMemoryOp::LaneKind lane_kind) {
  using turboshaft::Simd128LaneMemoryOp;
  switch (lane_kind) {
    case Simd128LaneMemoryOp::LaneKind::k8:
      return MachineRepresentation::kWord8;
    case Simd128LaneMemoryOp::LaneKind::k16:
      return MachineRepresentation::kWord16;
    case Simd128LaneMemoryOp::LaneKind::k32:
      return MachineRepresentation::kWord32;
    case Simd128LaneMemoryOp::LaneKind::k64:
      return MachineRepresentation::kWord64;
  }
}
}  // namespace

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitStoreLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LaneMemoryOp& store = Get(node).Cast<Simd128LaneMemoryOp>();

  LoadStoreLaneParams f(MachineRepresentationOf(store.lane_kind), store.lane);
  InstructionCode opcode =
      f.low_op ? kArmS128StoreLaneLow : kArmS128StoreLaneHigh;
  opcode |= MiscField::encode(f.sz);

  ArmOperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand inputs[4];
  size_t input_count = 4;
  inputs[0] = g.UseRegister(store.value());
  inputs[1] = g.UseImmediate(f.laneidx);
  inputs[2] = g.UseRegister(store.base());
  inputs[3] = g.UseRegister(store.index());
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[2]);
  Emit(opcode, 0, nullptr, input_count, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitStoreLane(Node* node) {
  StoreLaneParameters params = StoreLaneParametersOf(node->op());
  LoadStoreLaneParams f(params.rep, params.laneidx);
  InstructionCode opcode =
      f.low_op ? kArmS128StoreLaneLow : kArmS128StoreLaneHigh;
  opcode |= MiscField::encode(f.sz);

  ArmOperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand inputs[4];
  size_t input_count = 4;
  inputs[0] = g.UseRegister(node->InputAt(2));
  inputs[1] = g.UseImmediate(f.laneidx);
  inputs[2] = g.UseRegister(node->InputAt(0));
  inputs[3] = g.UseRegister(node->InputAt(1));
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[2]);
  Emit(opcode, 0, nullptr, input_count, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LaneMemoryOp& load = this->Get(node).Cast<Simd128LaneMemoryOp>();
  LoadStoreLaneParams f(MachineRepresentationOf(load.lane_kind), load.lane);
  InstructionCode opcode =
      f.low_op ? kArmS128LoadLaneLow : kArmS128LoadLaneHigh;
  opcode |= MiscField::encode(f.sz);

  ArmOperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand output = g.DefineSameAsFirst(node);
  InstructionOperand inputs[4];
  size_t input_count = 4;
  inputs[0] = g.UseRegister(load.value());
  inputs[1] = g.UseImmediate(f.laneidx);
  inputs[2] = g.UseRegister(load.base());
  inputs[3] = g.UseRegister(load.index());
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[2]);
  Emit(opcode, 1, &output, input_count, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadLane(Node* node) {
  LoadLaneParameters params = LoadLaneParametersOf(node->op());
  LoadStoreLaneParams f(params.rep.representation(), params.laneidx);
  InstructionCode opcode =
      f.low_op ? kArmS128LoadLaneLow : kArmS128LoadLaneHigh;
  opcode |= MiscField::encode(f.sz);

  ArmOperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand output = g.DefineSameAsFirst(node);
  InstructionOperand inputs[4];
  size_t input_count = 4;
  inputs[0] = g.UseRegister(node->InputAt(2));
  inputs[1] = g.UseImmediate(f.laneidx);
  inputs[2] = g.UseRegister(node->InputAt(0));
  inputs[3] = g.UseRegister(node->InputAt(1));
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[2]);
  Emit(opcode, 1, &output, input_count, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadTransform(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LoadTransformOp& op =
      this->Get(node).Cast<Simd128LoadTransformOp>();
  InstructionCode opcode = kArchNop;
  switch (op.transform_kind) {
    case Simd128LoadTransformOp::TransformKind::k8Splat:
      opcode = kArmS128Load8Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k16Splat:
      opcode = kArmS128Load16Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Splat:
      opcode = kArmS128Load32Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Splat:
      opcode = kArmS128Load64Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8S:
      opcode = kArmS128Load8x8S;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8U:
      opcode = kArmS128Load8x8U;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4S:
      opcode = kArmS128Load16x4S;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4U:
      opcode = kArmS128Load16x4U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2S:
      opcode = kArmS128Load32x2S;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2U:
      opcode = kArmS128Load32x2U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Zero:
      opcode = kArmS128Load32Zero;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Zero:
      opcode = kArmS128Load64Zero;
      break;
    default:
      UNIMPLEMENTED();
  }

  ArmOperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand output = g.DefineAsRegister(node);
  InstructionOperand inputs[2];
  size_t input_count = 2;
  inputs[0] = g.UseRegister(op.base());
  inputs[1] = g.UseRegister(op.index());
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[0]);
  Emit(opcode, 1, &output, input_count, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadTransform(Node* node) {
  LoadTransformParameters params = LoadTransformParametersOf(node->op());
  InstructionCode opcode = kArchNop;
  switch (params.transformation) {
    case LoadTransformation::kS128Load8Splat:
      opcode = kArmS128Load8Splat;
      break;
    case LoadTransformation::kS128Load16Splat:
      opcode = kArmS128Load16Splat;
      break;
    case LoadTransformation::kS128Load32Splat:
      opcode = kArmS128Load32Splat;
      break;
    case LoadTransformation::kS128Load64Splat:
      opcode = kArmS128Load64Splat;
      break;
    case LoadTransformation::kS128Load8x8S:
      opcode = kArmS128Load8x8S;
      break;
    case LoadTransformation::kS128Load8x8U:
      opcode = kArmS128Load8x8U;
      break;
    case LoadTransformation::kS128Load16x4S:
      opcode = kArmS128Load16x4S;
      break;
    case LoadTransformation::kS128Load16x4U:
      opcode = kArmS128Load16x4U;
      break;
    case LoadTransformation::kS128Load32x2S:
      opcode = kArmS128Load32x2S;
      break;
    case LoadTransformation::kS128Load32x2U:
      opcode = kArmS128Load32x2U;
      break;
    case LoadTransformation::kS128Load32Zero:
      opcode = kArmS128Load32Zero;
      break;
    case LoadTransformation::kS128Load64Zero:
      opcode = kArmS128Load64Zero;
      break;
    default:
      UNIMPLEMENTED();
  }

  ArmOperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand output = g.DefineAsRegister(node);
  InstructionOperand inputs[2];
  size_t input_count = 2;
  inputs[0] = g.UseRegister(node->InputAt(0));
  inputs[1] = g.UseRegister(node->InputAt(1));
  EmitAddBeforeS128LoadStore(this, &opcode, &input_count, &inputs[0]);
  Emit(opcode, 1, &output, input_count, inputs);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node) {
  typename Adapter::LoadView load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();
  ArmOperandGeneratorT<Adapter> g(this);
  node_t base = load.base();
  node_t index = load.index();

  InstructionCode opcode = kArchNop;
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat32:
      opcode = kArmVldrF32;
      break;
    case MachineRepresentation::kFloat64:
      opcode = kArmVldrF64;
      break;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsUnsigned() ? kArmLdrb : kArmLdrsb;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsUnsigned() ? kArmLdrh : kArmLdrsh;
      break;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      opcode = kArmLdr;
      break;
    case MachineRepresentation::kSimd128:
      opcode = kArmVld1S128;
      break;
    case MachineRepresentation::kFloat16:
      UNIMPLEMENTED();
    case MachineRepresentation::kSimd256:            // Fall through.
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:         // Fall through.
    case MachineRepresentation::kProtectedPointer:   // Fall through.
    case MachineRepresentation::kIndirectPointer:    // Fall through.
    case MachineRepresentation::kSandboxedPointer:   // Fall through.
    case MachineRepresentation::kWord64:             // Fall through.
    case MachineRepresentation::kMapWord:            // Fall through.
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }

  InstructionOperand output = g.DefineAsRegister(node);
  EmitLoad(this, opcode, &output, base, index);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  // TODO(eholk)
  UNIMPLEMENTED();
}

namespace {

ArchOpcode GetStoreOpcode(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kFloat32:
      return kArmVstrF32;
    case MachineRepresentation::kFloat64:
      return kArmVstrF64;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      return kArmStrb;
    case MachineRepresentation::kWord16:
      return kArmStrh;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      return kArmStr;
    case MachineRepresentation::kSimd128:
      return kArmVst1S128;
    case MachineRepresentation::kFloat16:
      UNIMPLEMENTED();
    case MachineRepresentation::kSimd256:            // Fall through.
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:         // Fall through.
    case MachineRepresentation::kProtectedPointer:   // Fall through.
    case MachineRepresentation::kIndirectPointer:    // Fall through.
    case MachineRepresentation::kSandboxedPointer:   // Fall through.
    case MachineRepresentation::kWord64:             // Fall through.
    case MachineRepresentation::kMapWord:            // Fall through.
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

ArchOpcode GetAtomicStoreOpcode(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kWord8:
      return kAtomicStoreWord8;
    case MachineRepresentation::kWord16:
      return kAtomicStoreWord16;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      return kAtomicStoreWord32;
    default:
      UNREACHABLE();
  }
}

template <typename Adapter>
void VisitStoreCommon(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node,
                      StoreRepresentation store_rep,
                      std::optional<AtomicMemoryOrder> atomic_order) {
  using node_t = typename Adapter::node_t;
  ArmOperandGeneratorT<Adapter> g(selector);
  auto store_view = selector->store_view(node);
  node_t base = store_view.base();
  node_t index = selector->value(store_view.index());
  node_t value = store_view.value();

  WriteBarrierKind write_barrier_kind = store_rep.write_barrier_kind();
  MachineRepresentation rep = store_rep.representation();

  if (v8_flags.enable_unconditional_write_barriers && CanBeTaggedPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedPointer(rep));
    AddressingMode addressing_mode;
    InstructionOperand inputs[3];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(base);
    // OutOfLineRecordWrite uses the index in an 'add' instruction as well as
    // for the store itself, so we must check compatibility with both.
    if (g.CanBeImmediate(index, kArmAdd) && g.CanBeImmediate(index, kArmStr)) {
      inputs[input_count++] = g.UseImmediate(index);
      addressing_mode = kMode_Offset_RI;
    } else {
      inputs[input_count++] = g.UseUniqueRegister(index);
      addressing_mode = kMode_Offset_RR;
    }
    inputs[input_count++] = g.UseUniqueRegister(value);
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionCode code;
    if (!atomic_order) {
      code = kArchStoreWithWriteBarrier;
      code |= RecordWriteModeField::encode(record_write_mode);
    } else {
      code = kArchAtomicStoreWithWriteBarrier;
      code |= AtomicMemoryOrderField::encode(*atomic_order);
      code |= AtomicStoreRecordWriteModeField::encode(record_write_mode);
    }
    code |= AddressingModeField::encode(addressing_mode);
    selector->Emit(code, 0, nullptr, input_count, inputs);
  } else {
    InstructionCode opcode = kArchNop;
    if (!atomic_order) {
      opcode = GetStoreOpcode(rep);
    } else {
      // Release stores emit DMB ISH; STR while sequentially consistent stores
      // emit DMB ISH; STR; DMB ISH.
      // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
      opcode = GetAtomicStoreOpcode(rep);
      opcode |= AtomicMemoryOrderField::encode(*atomic_order);
    }

    std::optional<ExternalReference> external_base;
    if constexpr (Adapter::IsTurboshaft) {
      ExternalReference value;
      if (selector->MatchExternalConstant(store_view.base(), &value)) {
        external_base = value;
      }
    } else {
      ExternalReferenceMatcher m(store_view.base());
      if (m.HasResolvedValue()) {
        external_base = m.ResolvedValue();
      }
    }

    if (external_base &&
        selector->CanAddressRelativeToRootsRegister(*external_base)) {
      if (selector->is_integer_constant(index)) {
        ptrdiff_t const delta =
            selector->integer_constant(index) +
            MacroAssemblerBase::RootRegisterOffsetForExternalReference(
                selector->isolate(), *external_base);
        int input_count = 2;
        InstructionOperand inputs[2];
        inputs[0] = g.UseRegister(value);
        inputs[1] = g.UseImmediate(static_cast<int32_t>(delta));
        opcode |= AddressingModeField::encode(kMode_Root);
        selector->Emit(opcode, 0, nullptr, input_count, inputs);
        return;
      }
    }

    if (selector->is_load_root_register(base)) {
      int input_count = 2;
      InstructionOperand inputs[2];
      inputs[0] = g.UseRegister(value);
      inputs[1] = g.UseImmediate(index);
      opcode |= AddressingModeField::encode(kMode_Root);
      selector->Emit(opcode, 0, nullptr, input_count, inputs);
      return;
    }

    InstructionOperand inputs[4];
    size_t input_count = 0;
    inputs[input_count++] = g.UseRegister(value);
    inputs[input_count++] = g.UseRegister(base);
    EmitStore(selector, opcode, input_count, inputs, index);
  }
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(node_t node) {
  VisitStoreCommon(this, node, this->store_view(node).stored_rep(),
                   std::nullopt);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  // TODO(eholk)
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedLoad(node_t node) {
  auto load = this->load_view(node);
  MachineRepresentation load_rep = load.loaded_rep().representation();
  ArmOperandGeneratorT<Adapter> g(this);
  node_t base = this->input_at(node, 0);
  node_t index = this->input_at(node, 1);

  InstructionCode opcode = kArmLdr;
  // Only floating point loads need to be specially handled; integer loads
  // support unaligned access. We support unaligned FP loads by loading to
  // integer registers first, then moving to the destination FP register. If
  // NEON is supported, we use the vld1.8 instruction.
  switch (load_rep) {
    case MachineRepresentation::kFloat32: {
      InstructionOperand temp = g.TempRegister();
      EmitLoad(this, opcode, &temp, base, index);
      Emit(kArmVmovF32U32, g.DefineAsRegister(node), temp);
      return;
    }
    case MachineRepresentation::kFloat64: {
      // Compute the address of the least-significant byte of the FP value.
      // We assume that the base node is unlikely to be an encodable immediate
      // or the result of a shift operation, so only consider the addressing
      // mode that should be used for the index node.
      InstructionCode add_opcode = kArmAdd;
      InstructionOperand inputs[3];
      inputs[0] = g.UseRegister(base);

      size_t input_count;
      if (TryMatchImmediateOrShift(this, &add_opcode, index, &input_count,
                                   &inputs[1])) {
        // input_count has been set by TryMatchImmediateOrShift(), so
        // increment it to account for the base register in inputs[0].
        input_count++;
      } else {
        add_opcode |= AddressingModeField::encode(kMode_Operand2_R);
        inputs[1] = g.UseRegister(index);
        input_count = 2;  // Base register and index.
      }

      InstructionOperand addr = g.TempRegister();
      Emit(add_opcode, 1, &addr, input_count, inputs);

      if (CpuFeatures::IsSupported(NEON)) {
        // With NEON we can load directly from the calculated address.
        InstructionCode op = kArmVld1F64;
        op |= AddressingModeField::encode(kMode_Operand2_R);
        Emit(op, g.DefineAsRegister(node), addr);
      } else {
        // Load both halves and move to an FP register.
        InstructionOperand fp_lo = g.TempRegister();
        InstructionOperand fp_hi = g.TempRegister();
        opcode |= AddressingModeField::encode(kMode_Offset_RI);
        Emit(opcode, fp_lo, addr, g.TempImmediate(0));
        Emit(opcode, fp_hi, addr, g.TempImmediate(4));
        Emit(kArmVmovF64U32U32, g.DefineAsRegister(node), fp_lo, fp_hi);
      }
      return;
    }
    default:
      // All other cases should support unaligned accesses.
      UNREACHABLE();
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedStore(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  auto store_view = this->store_view(node);
  node_t base = store_view.base();
  node_t index = this->value(store_view.index());
  node_t value = store_view.value();

  InstructionOperand inputs[4];
  size_t input_count = 0;

  UnalignedStoreRepresentation store_rep =
      store_view.stored_rep().representation();

  // Only floating point stores need to be specially handled; integer stores
  // support unaligned access. We support unaligned FP stores by moving the
  // value to integer registers first, then storing to the destination address.
  // If NEON is supported, we use the vst1.8 instruction.
  switch (store_rep) {
    case MachineRepresentation::kFloat32: {
      inputs[input_count++] = g.TempRegister();
      Emit(kArmVmovU32F32, inputs[0], g.UseRegister(value));
      inputs[input_count++] = g.UseRegister(base);
      EmitStore(this, kArmStr, input_count, inputs, index);
      return;
    }
    case MachineRepresentation::kFloat64: {
      if (CpuFeatures::IsSupported(NEON)) {
        InstructionOperand address = g.TempRegister();
        {
          // First we have to calculate the actual address.
          InstructionCode add_opcode = kArmAdd;
          InstructionOperand inputs[3];
          inputs[0] = g.UseRegister(base);

          size_t input_count;
          if (TryMatchImmediateOrShift(this, &add_opcode, index, &input_count,
                                       &inputs[1])) {
            // input_count has been set by TryMatchImmediateOrShift(), so
            // increment it to account for the base register in inputs[0].
            input_count++;
          } else {
            add_opcode |= AddressingModeField::encode(kMode_Operand2_R);
            inputs[1] = g.UseRegister(index);
            input_count = 2;  // Base register and index.
          }

          Emit(add_opcode, 1, &address, input_count, inputs);
        }

        inputs[input_count++] = g.UseRegister(value);
        inputs[input_count++] = address;
        InstructionCode op = kArmVst1F64;
        op |= AddressingModeField::encode(kMode_Operand2_R);
        Emit(op, 0, nullptr, input_count, inputs);
      } else {
        // Store a 64-bit floating point value using two 32-bit integer stores.
        // Computing the store address here would require three live temporary
        // registers (fp<63:32>, fp<31:0>, address), so compute base + 4 after
        // storing the least-significant half of the value.

        // First, move the 64-bit FP value into two temporary integer registers.
        InstructionOperand fp[] = {g.TempRegister(), g.TempRegister()};
        inputs[input_count++] = g.UseRegister(value);
        Emit(kArmVmovU32U32F64, arraysize(fp), fp, input_count, inputs);

        // Store the least-significant half.
        inputs[0] = fp[0];  // Low 32-bits of FP value.
        inputs[input_count++] =
            g.UseRegister(base);  // First store base address.
        EmitStore(this, kArmStr, input_count, inputs, index);

        // Store the most-significant half.
        InstructionOperand base4 = g.TempRegister();
        Emit(kArmAdd | AddressingModeField::encode(kMode_Operand2_I), base4,
             g.UseRegister(base), g.TempImmediate(4));  // Compute base + 4.
        inputs[0] = fp[1];  // High 32-bits of FP value.
        inputs[1] = base4;  // Second store base + 4 address.
        EmitStore(this, kArmStr, input_count, inputs, index);
      }
      return;
    }
    default:
      // All other cases should support unaligned accesses.
      UNREACHABLE();
  }
}

namespace {

template <typename Adapter>
void EmitBic(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t node, typename Adapter::node_t left,
             typename Adapter::node_t right) {
  ArmOperandGeneratorT<Adapter> g(selector);
  InstructionCode opcode = kArmBic;
  InstructionOperand value_operand;
  InstructionOperand shift_operand;
  if (TryMatchShift(selector, &opcode, right, &value_operand, &shift_operand)) {
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(left),
                   value_operand, shift_operand);
    return;
  }
  selector->Emit(opcode | AddressingModeField::encode(kMode_Operand2_R),
                 g.DefineAsRegister(node), g.UseRegister(left),
                 g.UseRegister(right));
}

template <typename Adapter>
void EmitUbfx(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, typename Adapter::node_t left,
              uint32_t lsb, uint32_t width) {
  DCHECK_LE(lsb, 31u);
  DCHECK_LE(1u, width);
  DCHECK_LE(width, 32u - lsb);
  ArmOperandGeneratorT<Adapter> g(selector);
  selector->Emit(kArmUbfx, g.DefineAsRegister(node), g.UseRegister(left),
                 g.TempImmediate(lsb), g.TempImmediate(width));
}

}  // namespace

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32And(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(this);

  const WordBinopOp& bitwise_and = Get(node).Cast<WordBinopOp>();
  const Operation& lhs = Get(bitwise_and.left());

  if (lhs.Is<Opmask::kWord32BitwiseXor>() &&
      CanCover(node, bitwise_and.left())) {
    const WordBinopOp& bitwise_xor = lhs.Cast<WordBinopOp>();
    int32_t bitmask;
    if (MatchIntegralWord32Constant(bitwise_xor.right(), &bitmask) &&
        bitmask == -1) {
      EmitBic(this, node, bitwise_and.right(), bitwise_xor.left());
      return;
    }
  }

  const Operation& rhs = Get(bitwise_and.right());
  if (rhs.Is<Opmask::kWord32BitwiseXor>() &&
      CanCover(node, bitwise_and.right())) {
    const WordBinopOp& bitwise_xor = rhs.Cast<WordBinopOp>();
    int32_t bitmask;
    if (MatchIntegralWord32Constant(bitwise_xor.right(), &bitmask) &&
        bitmask == -1) {
      EmitBic(this, node, bitwise_and.left(), bitwise_xor.left());
      return;
    }
  }

  if (is_integer_constant(bitwise_and.right())) {
    uint32_t const value = integer_constant(bitwise_and.right());
    uint32_t width = base::bits::CountPopulation(value);
    uint32_t leading_zeros = base::bits::CountLeadingZeros32(value);

    // Try to merge SHR operations on the left hand input into this AND.
    if (lhs.Is<Opmask::kWord32ShiftRightLogical>()) {
      const ShiftOp& shr = lhs.Cast<ShiftOp>();
      if (is_integer_constant(shr.right())) {
        uint32_t const shift = integer_constant(shr.right());

        if (((shift == 8) || (shift == 16) || (shift == 24)) &&
            (value == 0xFF)) {
          // Merge SHR into AND by emitting a UXTB instruction with a
          // bytewise rotation.
          Emit(kArmUxtb, g.DefineAsRegister(node), g.UseRegister(shr.left()),
               g.TempImmediate(shift));
          return;
        } else if (((shift == 8) || (shift == 16)) && (value == 0xFFFF)) {
          // Merge SHR into AND by emitting a UXTH instruction with a
          // bytewise rotation.
          Emit(kArmUxth, g.DefineAsRegister(node), g.UseRegister(shr.left()),
               g.TempImmediate(shift));
          return;
        } else if (IsSupported(ARMv7) && (width != 0) &&
                   ((leading_zeros + width) == 32)) {
          // Merge Shr into And by emitting a UBFX instruction.
          DCHECK_EQ(0u, base::bits::CountTrailingZeros32(value));
          if ((1 <= shift) && (shift <= 31)) {
            // UBFX cannot extract bits past the register size, however since
            // shifting the original value would have introduced some zeros we
            // can still use UBFX with a smaller mask and the remaining bits
            // will be zeros.
            EmitUbfx(this, node, shr.left(), shift,
                     std::min(width, 32 - shift));
            return;
          }
        }
      }
    } else if (value == 0xFFFF) {
      // Emit UXTH for this AND. We don't bother testing for UXTB, as it's no
      // better than AND 0xFF for this operation.
      Emit(kArmUxth, g.DefineAsRegister(node),
           g.UseRegister(bitwise_and.left()), g.TempImmediate(0));
      return;
    }
    if (g.CanBeImmediate(~value)) {
      // Emit BIC for this AND by inverting the immediate value first.
      Emit(kArmBic | AddressingModeField::encode(kMode_Operand2_I),
           g.DefineAsRegister(node), g.UseRegister(bitwise_and.left()),
           g.TempImmediate(~value));
      return;
    }
    if (!g.CanBeImmediate(value) && IsSupported(ARMv7)) {
      // If value has 9 to 23 contiguous set bits, and has the lsb set, we can
      // replace this AND with UBFX. Other contiguous bit patterns have
      // already been handled by BIC or will be handled by AND.
      if ((width != 0) && ((leading_zeros + width) == 32) &&
          (9 <= leading_zeros) && (leading_zeros <= 23)) {
        DCHECK_EQ(0u, base::bits::CountTrailingZeros32(value));
        EmitUbfx(this, node, bitwise_and.left(), 0, width);
        return;
      }

      width = 32 - width;
      leading_zeros = base::bits::CountLeadingZeros32(~value);
      uint32_t lsb = base::bits::CountTrailingZeros32(~value);
      if ((leading_zeros + width + lsb) == 32) {
        // This AND can be replaced with BFC.
        Emit(kArmBfc, g.DefineSameAsFirst(node),
             g.UseRegister(bitwise_and.left()), g.TempImmediate(lsb),
             g.TempImmediate(width));
        return;
      }
    }
  }
  VisitBinop(this, node, kArmAnd, kArmAnd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32And(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Int32BinopMatcher m(node);
    if (m.left().IsWord32Xor() && CanCover(node, m.left().node())) {
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().Is(-1)) {
        EmitBic(this, node, m.right().node(), mleft.left().node());
        return;
      }
    }
    if (m.right().IsWord32Xor() && CanCover(node, m.right().node())) {
      Int32BinopMatcher mright(m.right().node());
      if (mright.right().Is(-1)) {
        EmitBic(this, node, m.left().node(), mright.left().node());
        return;
      }
    }
    if (m.right().HasResolvedValue()) {
      uint32_t const value = m.right().ResolvedValue();
      uint32_t width = base::bits::CountPopulation(value);
      uint32_t leading_zeros = base::bits::CountLeadingZeros32(value);

      // Try to merge SHR operations on the left hand input into this AND.
      if (m.left().IsWord32Shr()) {
        Int32BinopMatcher mshr(m.left().node());
        if (mshr.right().HasResolvedValue()) {
          uint32_t const shift = mshr.right().ResolvedValue();

          if (((shift == 8) || (shift == 16) || (shift == 24)) &&
              (value == 0xFF)) {
            // Merge SHR into AND by emitting a UXTB instruction with a
            // bytewise rotation.
            Emit(kArmUxtb, g.DefineAsRegister(m.node()),
                 g.UseRegister(mshr.left().node()),
                 g.TempImmediate(mshr.right().ResolvedValue()));
            return;
          } else if (((shift == 8) || (shift == 16)) && (value == 0xFFFF)) {
            // Merge SHR into AND by emitting a UXTH instruction with a
            // bytewise rotation.
            Emit(kArmUxth, g.DefineAsRegister(m.node()),
                 g.UseRegister(mshr.left().node()),
                 g.TempImmediate(mshr.right().ResolvedValue()));
            return;
          } else if (IsSupported(ARMv7) && (width != 0) &&
                     ((leading_zeros + width) == 32)) {
            // Merge Shr into And by emitting a UBFX instruction.
            DCHECK_EQ(0u, base::bits::CountTrailingZeros32(value));
            if ((1 <= shift) && (shift <= 31)) {
              // UBFX cannot extract bits past the register size, however since
              // shifting the original value would have introduced some zeros we
              // can still use UBFX with a smaller mask and the remaining bits
              // will be zeros.
              EmitUbfx(this, node, mshr.left().node(), shift,
                       std::min(width, 32 - shift));
              return;
            }
          }
        }
      } else if (value == 0xFFFF) {
        // Emit UXTH for this AND. We don't bother testing for UXTB, as it's no
        // better than AND 0xFF for this operation.
        Emit(kArmUxth, g.DefineAsRegister(m.node()),
             g.UseRegister(m.left().node()), g.TempImmediate(0));
        return;
      }
      if (g.CanBeImmediate(~value)) {
        // Emit BIC for this AND by inverting the immediate value first.
        Emit(kArmBic | AddressingModeField::encode(kMode_Operand2_I),
             g.DefineAsRegister(node), g.UseRegister(m.left().node()),
             g.TempImmediate(~value));
        return;
      }
      if (!g.CanBeImmediate(value) && IsSupported(ARMv7)) {
        // If value has 9 to 23 contiguous set bits, and has the lsb set, we can
        // replace this AND with UBFX. Other contiguous bit patterns have
        // already been handled by BIC or will be handled by AND.
        if ((width != 0) && ((leading_zeros + width) == 32) &&
            (9 <= leading_zeros) && (leading_zeros <= 23)) {
          DCHECK_EQ(0u, base::bits::CountTrailingZeros32(value));
          EmitUbfx(this, node, m.left().node(), 0, width);
          return;
        }

        width = 32 - width;
        leading_zeros = base::bits::CountLeadingZeros32(~value);
        uint32_t lsb = base::bits::CountTrailingZeros32(~value);
        if ((leading_zeros + width + lsb) == 32) {
          // This AND can be replaced with BFC.
          Emit(kArmBfc, g.DefineSameAsFirst(node),
               g.UseRegister(m.left().node()), g.TempImmediate(lsb),
               g.TempImmediate(width));
          return;
        }
      }
    }
    VisitBinop(this, node, kArmAnd, kArmAnd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
  VisitBinop(this, node, kArmOrr, kArmOrr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& bitwise_xor =
        this->Get(node).template Cast<WordBinopOp>();
    int32_t mask;
    if (this->MatchIntegralWord32Constant(bitwise_xor.right(), &mask) &&
        mask == -1) {
      InstructionCode opcode = kArmMvn;
      InstructionOperand value_operand;
      InstructionOperand shift_operand;
      if (TryMatchShift(this, &opcode, bitwise_xor.left(), &value_operand,
                        &shift_operand)) {
        Emit(opcode, g.DefineAsRegister(node), value_operand, shift_operand);
        return;
      }
      Emit(opcode | AddressingModeField::encode(kMode_Operand2_R),
           g.DefineAsRegister(node), g.UseRegister(bitwise_xor.left()));
      return;
    }
    VisitBinop(this, node, kArmEor, kArmEor);
  } else {
    Int32BinopMatcher m(node);
    if (m.right().Is(-1)) {
      InstructionCode opcode = kArmMvn;
      InstructionOperand value_operand;
      InstructionOperand shift_operand;
      if (TryMatchShift(this, &opcode, m.left().node(), &value_operand,
                        &shift_operand)) {
        Emit(opcode, g.DefineAsRegister(node), value_operand, shift_operand);
        return;
      }
      Emit(opcode | AddressingModeField::encode(kMode_Operand2_R),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()));
      return;
    }
    VisitBinop(this, node, kArmEor, kArmEor);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(
    node_t node, FlagsContinuation* cont) {
  StackCheckKind kind;
  node_t value;
  if constexpr (Adapter::IsTurboshaft) {
    const auto& op =
        this->turboshaft_graph()
            ->Get(node)
            .template Cast<turboshaft::StackPointerGreaterThanOp>();
    kind = op.kind;
    value = op.stack_limit();
  } else {
    kind = StackCheckKindOf(node->op());
    value = node->InputAt(0);
  }
  InstructionCode opcode =
      kArchStackPointerGreaterThan | MiscField::encode(static_cast<int>(kind));

  ArmOperandGeneratorT<Adapter> g(this);

  // No outputs.
  InstructionOperand* const outputs = nullptr;
  const int output_count = 0;

  // Applying an offset to this stack check requires a temp register. Offsets
  // are only applied to the first stack check. If applying an offset, we must
  // ensure the input and temp registers do not alias, thus kUniqueRegister.
  InstructionOperand temps[] = {g.TempRegister()};
  const int temp_count = (kind == StackCheckKind::kJSFunctionEntry) ? 1 : 0;
  const auto register_mode = (kind == StackCheckKind::kJSFunctionEntry)
                                 ? OperandGenerator::kUniqueRegister
                                 : OperandGenerator::kRegister;

  InstructionOperand inputs[] = {g.UseRegisterWithMode(value, register_mode)};
  static constexpr int input_count = arraysize(inputs);

  EmitWithContinuation(opcode, output_count, outputs, input_count, inputs,
                       temp_count, temps, cont);
}

namespace {

template <typename TryMatchShift, typename Adapter>
void VisitShift(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, TryMatchShift try_match_shift,
                FlagsContinuationT<Adapter>* cont) {
  ArmOperandGeneratorT<Adapter> g(selector);
  InstructionCode opcode = kArmMov;
  InstructionOperand inputs[2];
  size_t input_count = 2;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  CHECK(try_match_shift(selector, &opcode, node, &inputs[0], &inputs[1]));

  outputs[output_count++] = g.DefineAsRegister(node);

  DCHECK_NE(0u, input_count);
  DCHECK_NE(0u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);
  DCHECK_NE(kMode_None, AddressingModeField::decode(opcode));

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

template <typename TryMatchShift, typename Adapter>
void VisitShift(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, TryMatchShift try_match_shift) {
  FlagsContinuationT<Adapter> cont;
  VisitShift(selector, node, try_match_shift, &cont);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shl(node_t node) {
  VisitShift(this, node, TryMatchLSL<Adapter>);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shr(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shr = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(shr.left());
    if (IsSupported(ARMv7) && lhs.Is<Opmask::kWord32BitwiseAnd>() &&
        this->is_integer_constant(shr.right()) &&
        base::IsInRange(this->integer_constant(shr.right()), 0, 31)) {
      uint32_t lsb = this->integer_constant(shr.right());
      const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
      if (this->is_integer_constant(bitwise_and.right())) {
        uint32_t value = static_cast<uint32_t>(
                             this->integer_constant(bitwise_and.right())) >>
                         lsb << lsb;
        uint32_t width = base::bits::CountPopulation(value);
        uint32_t msb = base::bits::CountLeadingZeros32(value);
        if ((width != 0) && (msb + width + lsb == 32)) {
          DCHECK_EQ(lsb, base::bits::CountTrailingZeros32(value));
          return EmitUbfx(this, node, bitwise_and.left(), lsb, width);
        }
      }
    }
    VisitShift(this, node, TryMatchLSR<Adapter>);
  } else {
    Int32BinopMatcher m(node);
    if (IsSupported(ARMv7) && m.left().IsWord32And() &&
        m.right().IsInRange(0, 31)) {
      uint32_t lsb = m.right().ResolvedValue();
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue()) {
        uint32_t value =
            static_cast<uint32_t>(mleft.right().ResolvedValue() >> lsb) << lsb;
        uint32_t width = base::bits::CountPopulation(value);
        uint32_t msb = base::bits::CountLeadingZeros32(value);
        if ((width != 0) && (msb + width + lsb == 32)) {
          DCHECK_EQ(lsb, base::bits::CountTrailingZeros32(value));
          return EmitUbfx(this, node, mleft.left().node(), lsb, width);
        }
      }
    }
    VisitShift(this, node, TryMatchLSR<Adapter>);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Sar(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& sar = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(sar.left());
    if (CanCover(node, sar.left()) && lhs.Is<Opmask::kWord32ShiftLeft>()) {
      const ShiftOp& shl = lhs.Cast<ShiftOp>();
      if (this->is_integer_constant(sar.right()) &&
          this->is_integer_constant(shl.right())) {
        uint32_t sar_by = this->integer_constant(sar.right());
        uint32_t shl_by = this->integer_constant(shl.right());
        if ((sar_by == shl_by) && (sar_by == 16)) {
          Emit(kArmSxth, g.DefineAsRegister(node), g.UseRegister(shl.left()),
               g.TempImmediate(0));
          return;
        } else if ((sar_by == shl_by) && (sar_by == 24)) {
          Emit(kArmSxtb, g.DefineAsRegister(node), g.UseRegister(shl.left()),
               g.TempImmediate(0));
          return;
        } else if (IsSupported(ARMv7) && (sar_by >= shl_by)) {
          Emit(kArmSbfx, g.DefineAsRegister(node), g.UseRegister(shl.left()),
               g.TempImmediate(sar_by - shl_by), g.TempImmediate(32 - sar_by));
          return;
        }
      }
    }
    VisitShift(this, node, TryMatchASR<Adapter>);
  } else {
    Int32BinopMatcher m(node);
    if (CanCover(m.node(), m.left().node()) && m.left().IsWord32Shl()) {
      Int32BinopMatcher mleft(m.left().node());
      if (m.right().HasResolvedValue() && mleft.right().HasResolvedValue()) {
        uint32_t sar = m.right().ResolvedValue();
        uint32_t shl = mleft.right().ResolvedValue();
        if ((sar == shl) && (sar == 16)) {
          Emit(kArmSxth, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(0));
          return;
        } else if ((sar == shl) && (sar == 24)) {
          Emit(kArmSxtb, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(0));
          return;
        } else if (IsSupported(ARMv7) && (sar >= shl)) {
          Emit(kArmSbfx, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(sar - shl),
               g.TempImmediate(32 - sar));
          return;
        }
      }
    }
    VisitShift(this, node, TryMatchASR<Adapter>);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairAdd(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);

  node_t projection1 = this->FindProjection(node, 1);
  if (this->valid(projection1)) {
    // We use UseUniqueRegister here to avoid register sharing with the output
    // registers.
    InstructionOperand inputs[] = {
        g.UseRegister(this->input_at(node, 0)),
        g.UseUniqueRegister(this->input_at(node, 1)),
        g.UseRegister(this->input_at(node, 2)),
        g.UseUniqueRegister(this->input_at(node, 3))};

    InstructionOperand outputs[] = {g.DefineAsRegister(node),
                                    g.DefineAsRegister(projection1)};

    Emit(kArmAddPair, 2, outputs, 4, inputs);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kArmAdd | AddressingModeField::encode(kMode_Operand2_R),
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairSub(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);

  node_t projection1 = this->FindProjection(node, 1);
  if (this->valid(projection1)) {
    // We use UseUniqueRegister here to avoid register sharing with the output
    // register.
    InstructionOperand inputs[] = {
        g.UseRegister(this->input_at(node, 0)),
        g.UseUniqueRegister(this->input_at(node, 1)),
        g.UseRegister(this->input_at(node, 2)),
        g.UseUniqueRegister(this->input_at(node, 3))};

    InstructionOperand outputs[] = {g.DefineAsRegister(node),
                                    g.DefineAsRegister(projection1)};

    Emit(kArmSubPair, 2, outputs, 4, inputs);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kArmSub | AddressingModeField::encode(kMode_Operand2_R),
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairMul(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection1)) {
    InstructionOperand inputs[] = {
        g.UseUniqueRegister(this->input_at(node, 0)),
        g.UseUniqueRegister(this->input_at(node, 1)),
        g.UseUniqueRegister(this->input_at(node, 2)),
        g.UseUniqueRegister(this->input_at(node, 3))};

    InstructionOperand outputs[] = {g.DefineAsRegister(node),
                                    g.DefineAsRegister(projection1)};

    Emit(kArmMulPair, 2, outputs, 4, inputs);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kArmMul | AddressingModeField::encode(kMode_Operand2_R),
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 2)));
  }
}

namespace {
// Shared routine for multiple shift operations.
template <typename Adapter>
void VisitWord32PairShift(InstructionSelectorT<Adapter>* selector,
                          InstructionCode opcode,
                          typename Adapter::node_t node) {
  ArmOperandGeneratorT<Adapter> g(selector);
  // We use g.UseUniqueRegister here to guarantee that there is
  // no register aliasing of input registers with output registers.
  InstructionOperand shift_operand;
  typename Adapter::node_t shift_by = selector->input_at(node, 2);
  if (selector->is_integer_constant(shift_by)) {
    shift_operand = g.UseImmediate(shift_by);
  } else {
    shift_operand = g.UseUniqueRegister(shift_by);
  }

  InstructionOperand inputs[] = {
      g.UseUniqueRegister(selector->input_at(node, 0)),
      g.UseUniqueRegister(selector->input_at(node, 1)), shift_operand};

  typename Adapter::node_t projection1 = selector->FindProjection(node, 1);

  InstructionOperand outputs[2];
  InstructionOperand temps[1];
  int32_t output_count = 0;
  int32_t temp_count = 0;

  outputs[output_count++] = g.DefineAsRegister(node);
  if (selector->valid(projection1)) {
    outputs[output_count++] = g.DefineAsRegister(projection1);
  } else {
    temps[temp_count++] = g.TempRegister();
  }

  selector->Emit(opcode, output_count, outputs, 3, inputs, temp_count, temps);
}
}  // namespace
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairShl(node_t node) {
  VisitWord32PairShift(this, kArmLslPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairShr(node_t node) {
  VisitWord32PairShift(this, kArmLsrPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairSar(node_t node) {
  VisitWord32PairShift(this, kArmAsrPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
  VisitShift(this, node, TryMatchROR<Adapter>);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ctz(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBits(node_t node) {
  DCHECK(IsSupported(ARMv7));
  VisitRR(this, kArmRbit, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBytes(node_t node) {
  VisitRR(this, kArmRev, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Popcnt(node_t node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Add(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  ArmOperandGeneratorT<TurboshaftAdapter> g(this);
  const WordBinopOp& add = this->Get(node).Cast<WordBinopOp>();
  DCHECK(add.Is<Opmask::kWord32Add>());
  const Operation& left = this->Get(add.left());

  if (CanCover(node, add.left())) {
    if (left.Is<Opmask::kWord32Mul>()) {
      const WordBinopOp& mul = left.Cast<WordBinopOp>();
      Emit(kArmMla, g.DefineAsRegister(node), g.UseRegister(mul.left()),
           g.UseRegister(mul.right()), g.UseRegister(add.right()));
      return;
    }
    if (left.Is<Opmask::kWord32SignedMulOverflownBits>()) {
      const WordBinopOp& mul = left.Cast<WordBinopOp>();
      Emit(kArmSmmla, g.DefineAsRegister(node), g.UseRegister(mul.left()),
           g.UseRegister(mul.right()), g.UseRegister(add.right()));
      return;
    }
    if (left.Is<Opmask::kWord32BitwiseAnd>()) {
      const WordBinopOp& bitwise_and = left.Cast<WordBinopOp>();
      uint32_t mask;
      if (MatchIntegralWord32Constant(bitwise_and.right(), &mask)) {
        if (mask == 0xFF) {
          Emit(kArmUxtab, g.DefineAsRegister(node), g.UseRegister(add.right()),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(0));
          return;
        } else if (mask == 0xFFFF) {
          Emit(kArmUxtah, g.DefineAsRegister(node), g.UseRegister(add.right()),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(0));
          return;
        }
      }
    } else if (left.Is<Opmask::kWord32ShiftRightArithmetic>()) {
      const ShiftOp& lhs_shift = left.Cast<ShiftOp>();
      if (CanCover(add.left(), lhs_shift.left()) &&
          Get(lhs_shift.left()).Is<Opmask::kWord32ShiftLeft>()) {
        const ShiftOp& lhs_shift_lhs_shift =
            Get(lhs_shift.left()).Cast<ShiftOp>();
        uint32_t sar_by, shl_by;
        if (MatchIntegralWord32Constant(lhs_shift.right(), &sar_by) &&
            MatchIntegralWord32Constant(lhs_shift_lhs_shift.right(), &shl_by)) {
          if (sar_by == 24 && shl_by == 24) {
            Emit(kArmSxtab, g.DefineAsRegister(node),
                 g.UseRegister(add.right()),
                 g.UseRegister(lhs_shift_lhs_shift.left()), g.TempImmediate(0));
            return;
          }
          if (sar_by == 16 && shl_by == 16) {
            Emit(kArmSxtah, g.DefineAsRegister(node),
                 g.UseRegister(add.right()),
                 g.UseRegister(lhs_shift_lhs_shift.left()), g.TempImmediate(0));
            return;
          }
        }
      }
    }
  }

  const Operation& right = this->Get(add.right());
  if (CanCover(node, add.right())) {
    if (right.Is<Opmask::kWord32Mul>()) {
      const WordBinopOp& mul = right.Cast<WordBinopOp>();
      Emit(kArmMla, g.DefineAsRegister(node), g.UseRegister(mul.left()),
           g.UseRegister(mul.right()), g.UseRegister(add.left()));
      return;
    }
    if (right.Is<Opmask::kWord32SignedMulOverflownBits>()) {
      const WordBinopOp& mul = right.Cast<WordBinopOp>();
      Emit(kArmSmmla, g.DefineAsRegister(node), g.UseRegister(mul.left()),
           g.UseRegister(mul.right()), g.UseRegister(add.left()));
      return;
    }
    if (right.Is<Opmask::kWord32BitwiseAnd>()) {
      const WordBinopOp& bitwise_and = right.Cast<WordBinopOp>();
      uint32_t mask;
      if (MatchIntegralWord32Constant(bitwise_and.right(), &mask)) {
        if (mask == 0xFF) {
          Emit(kArmUxtab, g.DefineAsRegister(node), g.UseRegister(add.left()),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(0));
          return;
        } else if (mask == 0xFFFF) {
          Emit(kArmUxtah, g.DefineAsRegister(node), g.UseRegister(add.left()),
               g.UseRegister(bitwise_and.left()), g.TempImmediate(0));
          return;
        }
      }
    } else if (right.Is<Opmask::kWord32ShiftRightArithmetic>()) {
      const ShiftOp& rhs_shift = right.Cast<ShiftOp>();
      if (CanCover(add.right(), rhs_shift.left()) &&
          Get(rhs_shift.left()).Is<Opmask::kWord32ShiftLeft>()) {
        const ShiftOp& rhs_shift_left = Get(rhs_shift.left()).Cast<ShiftOp>();
        uint32_t sar_by, shl_by;
        if (MatchIntegralWord32Constant(rhs_shift.right(), &sar_by) &&
            MatchIntegralWord32Constant(rhs_shift_left.right(), &shl_by)) {
          if (sar_by == 24 && shl_by == 24) {
            Emit(kArmSxtab, g.DefineAsRegister(node), g.UseRegister(add.left()),
                 g.UseRegister(rhs_shift_left.left()), g.TempImmediate(0));
            return;
          } else if (sar_by == 16 && shl_by == 16) {
            Emit(kArmSxtah, g.DefineAsRegister(node), g.UseRegister(add.left()),
                 g.UseRegister(rhs_shift_left.left()), g.TempImmediate(0));
            return;
          }
        }
      }
    }
  }
  VisitBinop(this, node, kArmAdd, kArmAdd);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Add(Node* node) {
  ArmOperandGeneratorT<TurbofanAdapter> g(this);
  Int32BinopMatcher m(node);
  if (CanCover(node, m.left().node())) {
    switch (m.left().opcode()) {
      case IrOpcode::kInt32Mul: {
        Int32BinopMatcher mleft(m.left().node());
        Emit(kArmMla, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseRegister(mleft.right().node()),
             g.UseRegister(m.right().node()));
        return;
      }
      case IrOpcode::kInt32MulHigh: {
        Int32BinopMatcher mleft(m.left().node());
        Emit(kArmSmmla, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseRegister(mleft.right().node()),
             g.UseRegister(m.right().node()));
        return;
      }
      case IrOpcode::kWord32And: {
        Int32BinopMatcher mleft(m.left().node());
        if (mleft.right().Is(0xFF)) {
          Emit(kArmUxtab, g.DefineAsRegister(node),
               g.UseRegister(m.right().node()),
               g.UseRegister(mleft.left().node()), g.TempImmediate(0));
          return;
        } else if (mleft.right().Is(0xFFFF)) {
          Emit(kArmUxtah, g.DefineAsRegister(node),
               g.UseRegister(m.right().node()),
               g.UseRegister(mleft.left().node()), g.TempImmediate(0));
          return;
        }
        break;
      }
      case IrOpcode::kWord32Sar: {
        Int32BinopMatcher mleft(m.left().node());
        if (CanCover(mleft.node(), mleft.left().node()) &&
            mleft.left().IsWord32Shl()) {
          Int32BinopMatcher mleftleft(mleft.left().node());
          if (mleft.right().Is(24) && mleftleft.right().Is(24)) {
            Emit(kArmSxtab, g.DefineAsRegister(node),
                 g.UseRegister(m.right().node()),
                 g.UseRegister(mleftleft.left().node()), g.TempImmediate(0));
            return;
          } else if (mleft.right().Is(16) && mleftleft.right().Is(16)) {
            Emit(kArmSxtah, g.DefineAsRegister(node),
                 g.UseRegister(m.right().node()),
                 g.UseRegister(mleftleft.left().node()), g.TempImmediate(0));
            return;
          }
        }
        break;
      }
      default:
        break;
    }
  }
  if (CanCover(node, m.right().node())) {
    switch (m.right().opcode()) {
      case IrOpcode::kInt32Mul: {
        Int32BinopMatcher mright(m.right().node());
        Emit(kArmMla, g.DefineAsRegister(node),
             g.UseRegister(mright.left().node()),
             g.UseRegister(mright.right().node()),
             g.UseRegister(m.left().node()));
        return;
      }
      case IrOpcode::kInt32MulHigh: {
        Int32BinopMatcher mright(m.right().node());
        Emit(kArmSmmla, g.DefineAsRegister(node),
             g.UseRegister(mright.left().node()),
             g.UseRegister(mright.right().node()),
             g.UseRegister(m.left().node()));
        return;
      }
      case IrOpcode::kWord32And: {
        Int32BinopMatcher mright(m.right().node());
        if (mright.right().Is(0xFF)) {
          Emit(kArmUxtab, g.DefineAsRegister(node),
               g.UseRegister(m.left().node()),
               g.UseRegister(mright.left().node()), g.TempImmediate(0));
          return;
        } else if (mright.right().Is(0xFFFF)) {
          Emit(kArmUxtah, g.DefineAsRegister(node),
               g.UseRegister(m.left().node()),
               g.UseRegister(mright.left().node()), g.TempImmediate(0));
          return;
        }
        break;
      }
      case IrOpcode::kWord32Sar: {
        Int32BinopMatcher mright(m.right().node());
        if (CanCover(mright.node(), mright.left().node()) &&
            mright.left().IsWord32Shl()) {
          Int32BinopMatcher mrightleft(mright.left().node());
          if (mright.right().Is(24) && mrightleft.right().Is(24)) {
            Emit(kArmSxtab, g.DefineAsRegister(node),
                 g.UseRegister(m.left().node()),
                 g.UseRegister(mrightleft.left().node()), g.TempImmediate(0));
            return;
          } else if (mright.right().Is(16) && mrightleft.right().Is(16)) {
            Emit(kArmSxtah, g.DefineAsRegister(node),
                 g.UseRegister(m.left().node()),
                 g.UseRegister(mrightleft.left().node()), g.TempImmediate(0));
            return;
          }
        }
        break;
      }
      default:
        break;
    }
  }
  VisitBinop(this, node, kArmAdd, kArmAdd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Sub(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& sub = this->Get(node).template Cast<WordBinopOp>();
    const Operation& rhs = this->Get(sub.right());
    if (IsSupported(ARMv7) && rhs.Is<Opmask::kWord32Mul>() &&
        CanCover(node, sub.right())) {
      const WordBinopOp& mul = rhs.Cast<WordBinopOp>();
      Emit(kArmMls, g.DefineAsRegister(node), g.UseRegister(mul.left()),
           g.UseRegister(mul.right()), g.UseRegister(sub.left()));
      return;
    }
    VisitBinop(this, node, kArmSub, kArmRsb);
  } else {
    Int32BinopMatcher m(node);
    if (IsSupported(ARMv7) && m.right().IsInt32Mul() &&
        CanCover(node, m.right().node())) {
      Int32BinopMatcher mright(m.right().node());
      Emit(kArmMls, g.DefineAsRegister(node),
           g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()),
           g.UseRegister(m.left().node()));
      return;
    }
    VisitBinop(this, node, kArmSub, kArmRsb);
  }
}

namespace {

template <typename Adapter>
void EmitInt32MulWithOverflow(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              FlagsContinuationT<Adapter>* cont) {
  ArmOperandGeneratorT<Adapter> g(selector);
  typename Adapter::node_t lhs = selector->input_at(node, 0);
  typename Adapter::node_t rhs = selector->input_at(node, 1);
  InstructionOperand result_operand = g.DefineAsRegister(node);
  InstructionOperand temp_operand = g.TempRegister();
  InstructionOperand outputs[] = {result_operand, temp_operand};
  InstructionOperand inputs[] = {g.UseRegister(lhs), g.UseRegister(rhs)};
  selector->Emit(kArmSmull, 2, outputs, 2, inputs);

  // result operand needs shift operator.
  InstructionOperand shift_31 = g.UseImmediate(31);
  InstructionCode opcode =
      kArmCmp | AddressingModeField::encode(kMode_Operand2_R_ASR_I);
  selector->EmitWithContinuation(opcode, temp_operand, result_operand, shift_31,
                                 cont);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mul(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& mul = this->Get(node).template Cast<WordBinopOp>();
    int32_t constant_rhs;
    if (this->MatchIntegralWord32Constant(mul.right(), &constant_rhs) &&
        constant_rhs > 0) {
      if (base::bits::IsPowerOfTwo(constant_rhs - 1)) {
        Emit(kArmAdd | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
             g.DefineAsRegister(node), g.UseRegister(mul.left()),
             g.UseRegister(mul.left()),
             g.TempImmediate(base::bits::WhichPowerOfTwo(constant_rhs - 1)));
        return;
      }
      if (constant_rhs < kMaxInt &&
          base::bits::IsPowerOfTwo(constant_rhs + 1)) {
        Emit(kArmRsb | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
             g.DefineAsRegister(node), g.UseRegister(mul.left()),
             g.UseRegister(mul.left()),
             g.TempImmediate(base::bits::WhichPowerOfTwo(constant_rhs + 1)));
        return;
      }
    }
    VisitRRR(this, kArmMul, node);
  } else {
    Int32BinopMatcher m(node);
    if (m.right().HasResolvedValue() && m.right().ResolvedValue() > 0) {
      int32_t value = m.right().ResolvedValue();
      if (base::bits::IsPowerOfTwo(value - 1)) {
        Emit(kArmAdd | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
             g.DefineAsRegister(node), g.UseRegister(m.left().node()),
             g.UseRegister(m.left().node()),
             g.TempImmediate(base::bits::WhichPowerOfTwo(value - 1)));
        return;
      }
      if (value < kMaxInt && base::bits::IsPowerOfTwo(value + 1)) {
        Emit(kArmRsb | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
             g.DefineAsRegister(node), g.UseRegister(m.left().node()),
             g.UseRegister(m.left().node()),
             g.TempImmediate(base::bits::WhichPowerOfTwo(value + 1)));
        return;
      }
    }
    VisitRRR(this, kArmMul, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
  auto binop = this->word_binop_view(node);
  ArmOperandGeneratorT<Adapter> g(this);
  InstructionOperand outputs[] = {g.TempRegister(), g.DefineAsRegister(node)};
  InstructionOperand inputs[] = {g.UseRegister(binop.left()),
                                 g.UseRegister(binop.right())};
  Emit(kArmUmull, arraysize(outputs), outputs, arraysize(inputs), inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Div(node_t node) {
  VisitDiv(this, node, kArmSdiv, kArmVcvtF64S32, kArmVcvtS32F64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Div(node_t node) {
  VisitDiv(this, node, kArmUdiv, kArmVcvtF64U32, kArmVcvtU32F64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mod(node_t node) {
  VisitMod(this, node, kArmSdiv, kArmVcvtF64S32, kArmVcvtS32F64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Mod(node_t node) {
  VisitMod(this, node, kArmUdiv, kArmVcvtF64U32, kArmVcvtU32F64);
}

#define RR_OP_T_LIST(V)                              \
  V(ChangeInt32ToFloat64, kArmVcvtF64S32)            \
  V(ChangeUint32ToFloat64, kArmVcvtF64U32)           \
  V(ChangeFloat32ToFloat64, kArmVcvtF64F32)          \
  V(ChangeFloat64ToInt32, kArmVcvtS32F64)            \
  V(ChangeFloat64ToUint32, kArmVcvtU32F64)           \
  V(RoundInt32ToFloat32, kArmVcvtF32S32)             \
  V(RoundUint32ToFloat32, kArmVcvtF32U32)            \
  V(Float64ExtractLowWord32, kArmVmovLowU32F64)      \
  V(Float64ExtractHighWord32, kArmVmovHighU32F64)    \
  V(TruncateFloat64ToFloat32, kArmVcvtF32F64)        \
  V(TruncateFloat64ToWord32, kArchTruncateDoubleToI) \
  V(TruncateFloat64ToUint32, kArmVcvtU32F64)         \
  V(BitcastFloat32ToInt32, kArmVmovU32F32)           \
  V(BitcastInt32ToFloat32, kArmVmovF32U32)           \
  V(RoundFloat64ToInt32, kArmVcvtS32F64)             \
  V(Float64SilenceNaN, kArmFloat64SilenceNaN)        \
  V(Float32Abs, kArmVabsF32)                         \
  V(Float64Abs, kArmVabsF64)                         \
  V(Float32Neg, kArmVnegF32)                         \
  V(Float64Neg, kArmVnegF64)                         \
  V(Float32Sqrt, kArmVsqrtF32)                       \
  V(Float64Sqrt, kArmVsqrtF64)                       \
  V(Word32Clz, kArmClz)

#define RR_OP_T_LIST_V8(V)                         \
  V(Float32RoundDown, kArmVrintmF32)               \
  V(Float64RoundDown, kArmVrintmF64)               \
  V(Float32RoundUp, kArmVrintpF32)                 \
  V(Float64RoundUp, kArmVrintpF64)                 \
  V(Float32RoundTruncate, kArmVrintzF32)           \
  V(Float64RoundTruncate, kArmVrintzF64)           \
  V(Float64RoundTiesAway, kArmVrintaF64)           \
  V(Float32RoundTiesEven, kArmVrintnF32)           \
  V(Float64RoundTiesEven, kArmVrintnF64)           \
  IF_WASM(V, F64x2Ceil, kArmF64x2Ceil)             \
  IF_WASM(V, F64x2Floor, kArmF64x2Floor)           \
  IF_WASM(V, F64x2Trunc, kArmF64x2Trunc)           \
  IF_WASM(V, F64x2NearestInt, kArmF64x2NearestInt) \
  IF_WASM(V, F32x4Ceil, kArmVrintpF32)             \
  IF_WASM(V, F32x4Floor, kArmVrintmF32)            \
  IF_WASM(V, F32x4Trunc, kArmVrintzF32)            \
  IF_WASM(V, F32x4NearestInt, kArmVrintnF32)

#define RRR_OP_T_LIST(V)        \
  V(Float64Div, kArmVdivF64)    \
  V(Float32Mul, kArmVmulF32)    \
  V(Float64Mul, kArmVmulF64)    \
  V(Float32Div, kArmVdivF32)    \
  V(Float32Max, kArmFloat32Max) \
  V(Float64Max, kArmFloat64Max) \
  V(Float32Min, kArmFloat32Min) \
  V(Float64Min, kArmFloat64Min) \
  V(Int32MulHigh, kArmSmmul)

#define RR_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, opcode, node);                                 \
  }
RR_OP_T_LIST(RR_VISITOR)
#undef RR_VISITOR
#undef RR_OP_T_LIST

#define RR_VISITOR_V8(Name, opcode)                              \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    DCHECK(CpuFeatures::IsSupported(ARMv8));                     \
    VisitRR(this, opcode, node);                                 \
  }
RR_OP_T_LIST_V8(RR_VISITOR_V8)
#undef RR_VISITOR_V8
#undef RR_OP_T_LIST_V8

#define RRR_VISITOR(Name, opcode)                                \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRRR(this, opcode, node);                                \
  }
RRR_OP_T_LIST(RRR_VISITOR)
#undef RRR_VISITOR
#undef RRR_OP_T_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Add(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& add = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& lhs = this->Get(add.left());
    if (lhs.Is<Opmask::kFloat32Mul>() && CanCover(node, add.left())) {
      const FloatBinopOp& mul = lhs.Cast<FloatBinopOp>();
      Emit(kArmVmlaF32, g.DefineSameAsFirst(node), g.UseRegister(add.right()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    const Operation& rhs = this->Get(add.right());
    if (rhs.Is<Opmask::kFloat32Mul>() && CanCover(node, add.right())) {
      const FloatBinopOp& mul = rhs.Cast<FloatBinopOp>();
      Emit(kArmVmlaF32, g.DefineSameAsFirst(node), g.UseRegister(add.left()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRRR(this, kArmVaddF32, node);
  } else {
    Float32BinopMatcher m(node);
    if (m.left().IsFloat32Mul() && CanCover(node, m.left().node())) {
      Float32BinopMatcher mleft(m.left().node());
      Emit(kArmVmlaF32, g.DefineSameAsFirst(node),
           g.UseRegister(m.right().node()), g.UseRegister(mleft.left().node()),
           g.UseRegister(mleft.right().node()));
      return;
    }
    if (m.right().IsFloat32Mul() && CanCover(node, m.right().node())) {
      Float32BinopMatcher mright(m.right().node());
      Emit(kArmVmlaF32, g.DefineSameAsFirst(node),
           g.UseRegister(m.left().node()), g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
    VisitRRR(this, kArmVaddF32, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Add(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& add = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& lhs = this->Get(add.left());
    if (lhs.Is<Opmask::kFloat64Mul>() && CanCover(node, add.left())) {
      const FloatBinopOp& mul = lhs.Cast<FloatBinopOp>();
      Emit(kArmVmlaF64, g.DefineSameAsFirst(node), g.UseRegister(add.right()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    const Operation& rhs = this->Get(add.right());
    if (rhs.Is<Opmask::kFloat64Mul>() && CanCover(node, add.right())) {
      const FloatBinopOp& mul = rhs.Cast<FloatBinopOp>();
      Emit(kArmVmlaF64, g.DefineSameAsFirst(node), g.UseRegister(add.left()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRRR(this, kArmVaddF64, node);
  } else {
    Float64BinopMatcher m(node);
    if (m.left().IsFloat64Mul() && CanCover(node, m.left().node())) {
      Float64BinopMatcher mleft(m.left().node());
      Emit(kArmVmlaF64, g.DefineSameAsFirst(node),
           g.UseRegister(m.right().node()), g.UseRegister(mleft.left().node()),
           g.UseRegister(mleft.right().node()));
      return;
    }
    if (m.right().IsFloat64Mul() && CanCover(node, m.right().node())) {
      Float64BinopMatcher mright(m.right().node());
      Emit(kArmVmlaF64, g.DefineSameAsFirst(node),
           g.UseRegister(m.left().node()), g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
    VisitRRR(this, kArmVaddF64, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sub(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& sub = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& rhs = this->Get(sub.right());
    if (rhs.Is<Opmask::kFloat32Mul>() && CanCover(node, sub.right())) {
      const FloatBinopOp& mul = rhs.Cast<FloatBinopOp>();
      Emit(kArmVmlsF32, g.DefineSameAsFirst(node), g.UseRegister(sub.left()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRRR(this, kArmVsubF32, node);
  } else {
    Float32BinopMatcher m(node);
    if (m.right().IsFloat32Mul() && CanCover(node, m.right().node())) {
      Float32BinopMatcher mright(m.right().node());
      Emit(kArmVmlsF32, g.DefineSameAsFirst(node),
           g.UseRegister(m.left().node()), g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
    VisitRRR(this, kArmVsubF32, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sub(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& sub = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& rhs = this->Get(sub.right());
    if (rhs.Is<Opmask::kFloat64Mul>() && CanCover(node, sub.right())) {
      const FloatBinopOp& mul = rhs.Cast<FloatBinopOp>();
      Emit(kArmVmlsF64, g.DefineSameAsFirst(node), g.UseRegister(sub.left()),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRRR(this, kArmVsubF64, node);
  } else {
    Float64BinopMatcher m(node);
    if (m.right().IsFloat64Mul() && CanCover(node, m.right().node())) {
      Float64BinopMatcher mright(m.right().node());
      Emit(kArmVmlsF64, g.DefineSameAsFirst(node),
           g.UseRegister(m.left().node()), g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
    VisitRRR(this, kArmVsubF64, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmVmodF64, g.DefineAsFixed(node, d0),
       g.UseFixed(this->input_at(node, 0), d0),
       g.UseFixed(this->input_at(node, 1), d1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, d0),
       g.UseFixed(this->input_at(node, 0), d0),
       g.UseFixed(this->input_at(node, 1), d1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, d0),
       g.UseFixed(this->input_at(node, 0), d0))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveParamToFPR(node_t node, int index) {
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveFPRToParam(
    InstructionOperand* op, LinkageLocation location) {}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareArguments(
    ZoneVector<PushParameter>* arguments, const CallDescriptor* call_descriptor,
    node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);

    // Prepare for C function call.
    if (call_descriptor->IsCFunctionCall()) {
      Emit(kArchPrepareCallCFunction | MiscField::encode(static_cast<int>(
                                           call_descriptor->ParameterCount())),
           0, nullptr, 0, nullptr);

      // Poke any stack arguments.
      for (size_t n = 0; n < arguments->size(); ++n) {
        PushParameter input = (*arguments)[n];
        if (this->valid(input.node)) {
          int slot = static_cast<int>(n);
          Emit(kArmPoke | MiscField::encode(slot), g.NoOutput(),
               g.UseRegister(input.node));
        }
      }
    } else {
      // Push any stack arguments.
      int stack_decrement = 0;
      for (PushParameter input : base::Reversed(*arguments)) {
        stack_decrement += kSystemPointerSize;
        // Skip any alignment holes in pushed nodes.
        if (!this->valid(input.node)) continue;
        InstructionOperand decrement = g.UseImmediate(stack_decrement);
        stack_decrement = 0;
        Emit(kArmPush, g.NoOutput(), decrement, g.UseRegister(input.node));
      }
    }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);

    for (PushParameter output : *results) {
      if (!output.location.IsCallerFrameSlot()) continue;
      // Skip any alignment holes in nodes.
      if (this->valid(output.node)) {
        DCHECK(!call_descriptor->IsCFunctionCall());
        if (output.location.GetType() == MachineType::Float32()) {
          MarkAsFloat32(output.node);
        } else if (output.location.GetType() == MachineType::Float64()) {
          MarkAsFloat64(output.node);
        } else if (output.location.GetType() == MachineType::Simd128()) {
          MarkAsSimd128(output.node);
        }
        int offset = call_descriptor->GetOffsetToReturns();
        int reverse_slot = -output.location.GetLocation() - offset;
        Emit(kArmPeek, g.DefineAsRegister(output.node),
             g.UseImmediate(reverse_slot));
      }
    }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsTailCallAddressImmediate() {
  return false;
}

namespace {

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, InstructionOperand left,
                  InstructionOperand right, FlagsContinuationT<Adapter>* cont) {
  selector->EmitWithContinuation(opcode, left, right, cont);
}

// Shared routine for multiple float32 compare operations.
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& cmp = selector->Get(node).template Cast<ComparisonOp>();
    if (selector->MatchZero(cmp.right())) {
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(cmp.left()),
                   g.UseImmediate(cmp.right()), cont);
    } else if (selector->MatchZero(cmp.left())) {
      cont->Commute();
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(cmp.right()),
                   g.UseImmediate(cmp.left()), cont);
    } else {
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(cmp.left()),
                   g.UseRegister(cmp.right()), cont);
    }
  } else {
    Float32BinopMatcher m(node);
    if (m.right().Is(0.0f)) {
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(m.left().node()),
                   g.UseImmediate(m.right().node()), cont);
    } else if (m.left().Is(0.0f)) {
      cont->Commute();
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(m.right().node()),
                   g.UseImmediate(m.left().node()), cont);
    } else {
      VisitCompare(selector, kArmVcmpF32, g.UseRegister(m.left().node()),
                   g.UseRegister(m.right().node()), cont);
    }
  }
}

// Shared routine for multiple float64 compare operations.
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  ArmOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& op = selector->Get(node).template Cast<ComparisonOp>();
    if (selector->MatchZero(op.right())) {
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(op.left()),
                   g.UseImmediate(op.right()), cont);
    } else if (selector->MatchZero(op.left())) {
      cont->Commute();
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(op.right()),
                   g.UseImmediate(op.left()), cont);
    } else {
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(op.left()),
                   g.UseRegister(op.right()), cont);
    }
  } else {
    Float64BinopMatcher m(node);
    if (m.right().Is(0.0)) {
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(m.left().node()),
                   g.UseImmediate(m.right().node()), cont);
    } else if (m.left().Is(0.0)) {
      cont->Commute();
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(m.right().node()),
                   g.UseImmediate(m.left().node()), cont);
    } else {
      VisitCompare(selector, kArmVcmpF64, g.UseRegister(m.left().node()),
                   g.UseRegister(m.right().node()), cont);
    }
  }
}

// Check whether we can convert:
// ((a <op> b) cmp 0), b.<cond>
// to:
// (a <ops> b), b.<cond'>
// where <ops> is the flag setting version of <op>.
// We only generate conditions <cond'> that are a combination of the N
// and Z flags. This avoids the need to make this function dependent on
// the flag-setting operation.
bool CanUseFlagSettingBinop(FlagsCondition cond) {
  switch (cond) {
    case kEqual:
    case kNotEqual:
    case kSignedLessThan:
    case kSignedGreaterThanOrEqual:
    case kUnsignedLessThanOrEqual:  // x <= 0 -> x == 0
    case kUnsignedGreaterThan:      // x > 0 -> x != 0
      return true;
    default:
      return false;
  }
}

// Map <cond> to <cond'> so that the following transformation is possible:
// ((a <op> b) cmp 0), b.<cond>
// to:
// (a <ops> b), b.<cond'>
// where <ops> is the flag setting version of <op>.
FlagsCondition MapForFlagSettingBinop(FlagsCondition cond) {
  DCHECK(CanUseFlagSettingBinop(cond));
  switch (cond) {
    case kEqual:
    case kNotEqual:
      return cond;
    case kSignedLessThan:
      return kNegative;
    case kSignedGreaterThanOrEqual:
      return kPositiveOrZero;
    case kUnsignedLessThanOrEqual:  // x <= 0 -> x == 0
      return kEqual;
    case kUnsignedGreaterThan:  // x > 0 -> x != 0
      return kNotEqual;
    default:
      UNREACHABLE();
  }
}

// Check if we can perform the transformation:
// ((a <op> b) cmp 0), b.<cond>
// to:
// (a <ops> b), b.<cond'>
// where <ops> is the flag setting version of <op>, and if so,
// updates {node}, {opcode} and {cont} accordingly.
template <typename Adapter>
void MaybeReplaceCmpZeroWithFlagSettingBinop(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t* node,
    typename Adapter::node_t binop, InstructionCode* opcode,
    FlagsCondition cond, FlagsContinuationT<Adapter>* cont) {
  InstructionCode binop_opcode;
  InstructionCode no_output_opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = selector->Get(binop);
    if (op.Is<Opmask::kWord32Add>()) {
      binop_opcode = kArmAdd;
      no_output_opcode = kArmCmn;
    } else if (op.Is<Opmask::kWord32BitwiseAnd>()) {
      binop_opcode = kArmAnd;
      no_output_opcode = kArmTst;
    } else if (op.Is<Opmask::kWord32BitwiseOr>()) {
      binop_opcode = kArmOrr;
      no_output_opcode = kArmOrr;
    } else if (op.Is<Opmask::kWord32BitwiseXor>()) {
      binop_opcode = kArmEor;
      no_output_opcode = kArmTeq;
    }
  } else {
    switch (binop->opcode()) {
      case IrOpcode::kInt32Add:
        binop_opcode = kArmAdd;
        no_output_opcode = kArmCmn;
        break;
      case IrOpcode::kWord32And:
        binop_opcode = kArmAnd;
        no_output_opcode = kArmTst;
        break;
      case IrOpcode::kWord32Or:
        binop_opcode = kArmOrr;
        no_output_opcode = kArmOrr;
        break;
      case IrOpcode::kWord32Xor:
        binop_opcode = kArmEor;
        no_output_opcode = kArmTeq;
        break;
      default:
        UNREACHABLE();
    }
  }

  if (selector->CanCover(*node, binop)) {
    // The comparison is the only user of {node}.
    cont->Overwrite(MapForFlagSettingBinop(cond));
    *opcode = no_output_opcode;
    *node = binop;
  } else if (selector->IsOnlyUserOfNodeInSameBlock(*node, binop)) {
    // We can also handle the case where the {node} and the comparison are in
    // the same basic block, and the comparison is the only user of {node} in
    // this basic block ({node} has users in other basic blocks).
    cont->Overwrite(MapForFlagSettingBinop(cond));
    *opcode = binop_opcode;
    *node = binop;
  }
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont) {
    ArmOperandGeneratorT<Adapter> g(selector);
    typename Adapter::node_t lhs = selector->input_at(node, 0);
    typename Adapter::node_t rhs = selector->input_at(node, 1);
    InstructionOperand inputs[3];
    size_t input_count = 0;
    InstructionOperand outputs[2];
    size_t output_count = 0;
    bool has_result = (opcode != kArmCmp) && (opcode != kArmCmn) &&
                      (opcode != kArmTst) && (opcode != kArmTeq);

    if (TryMatchImmediateOrShift(selector, &opcode, rhs, &input_count,
                                 &inputs[1])) {
      inputs[0] = g.UseRegister(lhs);
      input_count++;
    } else if (TryMatchImmediateOrShift(selector, &opcode, lhs, &input_count,
                                        &inputs[1])) {
      if constexpr (Adapter::IsTurboshaft) {
        using namespace turboshaft;  // NOLINT(build/namespaces)
        const Operation& op = selector->Get(node);
        if (const ComparisonOp* cmp = op.TryCast<ComparisonOp>()) {
          if (!ComparisonOp::IsCommutative(cmp->kind)) cont->Commute();
        } else if (const WordBinopOp* binop = op.TryCast<WordBinopOp>()) {
          if (!WordBinopOp::IsCommutative(binop->kind)) cont->Commute();
        } else {
          UNREACHABLE();
        }
      } else {
        if (!node->op()->HasProperty(Operator::kCommutative)) cont->Commute();
      }
      inputs[0] = g.UseRegister(rhs);
      input_count++;
    } else {
      opcode |= AddressingModeField::encode(kMode_Operand2_R);
      inputs[input_count++] = g.UseRegister(lhs);
      inputs[input_count++] = g.UseRegister(rhs);
    }

    if (has_result) {
      if (cont->IsDeoptimize()) {
        // If we can deoptimize as a result of the binop, we need to make sure
        // that the deopt inputs are not overwritten by the binop result. One
        // way to achieve that is to declare the output register as
        // same-as-first.
        outputs[output_count++] = g.DefineSameAsFirst(node);
      } else {
        outputs[output_count++] = g.DefineAsRegister(node);
      }
    }

    DCHECK_NE(0u, input_count);
    DCHECK_GE(arraysize(inputs), input_count);
    DCHECK_GE(arraysize(outputs), output_count);

    selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                   inputs, cont);
}

template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node,
                      FlagsContinuationT<Adapter>* cont) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    InstructionCode opcode = kArmCmp;
    const ComparisonOp& comparison =
        selector->Get(node).template Cast<ComparisonOp>();
    const Operation& lhs = selector->Get(comparison.left());
    const Operation& rhs = selector->Get(comparison.right());

    FlagsCondition cond = cont->condition();
    if (selector->MatchIntegralZero(comparison.right()) &&
        (lhs.Is<Opmask::kWord32Add>() || lhs.Is<Opmask::kWord32BitwiseOr>() ||
         lhs.Is<Opmask::kWord32BitwiseAnd>() ||
         lhs.Is<Opmask::kWord32BitwiseXor>())) {
      // Emit flag setting instructions for comparisons against zero.
      if (CanUseFlagSettingBinop(cond)) {
        MaybeReplaceCmpZeroWithFlagSettingBinop(
            selector, &node, comparison.left(), &opcode, cond, cont);
      }
    } else if (selector->MatchIntegralZero(comparison.left()) &&
               (rhs.Is<Opmask::kWord32Add>() ||
                rhs.Is<Opmask::kWord32BitwiseOr>() ||
                rhs.Is<Opmask::kWord32BitwiseAnd>() ||
                rhs.Is<Opmask::kWord32BitwiseXor>())) {
      // Same as above, but we need to commute the condition before we
      // continue with the rest of the checks.
      cond = CommuteFlagsCondition(cond);
      if (CanUseFlagSettingBinop(cond)) {
        MaybeReplaceCmpZeroWithFlagSettingBinop(
            selector, &node, comparison.right(), &opcode, cond, cont);
      }
    }

    VisitWordCompare(selector, node, opcode, cont);
  } else {
    InstructionCode opcode = kArmCmp;
    Int32BinopMatcher m(node);

    FlagsCondition cond = cont->condition();
    if (m.right().Is(0) && (m.left().IsInt32Add() || m.left().IsWord32Or() ||
                            m.left().IsWord32And() || m.left().IsWord32Xor())) {
      // Emit flag setting instructions for comparisons against zero.
      if (CanUseFlagSettingBinop(cond)) {
        Node* binop = m.left().node();
        MaybeReplaceCmpZeroWithFlagSettingBinop(selector, &node, binop, &opcode,
                                                cond, cont);
      }
    } else if (m.left().Is(0) &&
               (m.right().IsInt32Add() || m.right().IsWord32Or() ||
                m.right().IsWord32And() || m.right().IsWord32Xor())) {
      // Same as above, but we need to commute the condition before we
      // continue with the rest of the checks.
      cond = CommuteFlagsCondition(cond);
      if (CanUseFlagSettingBinop(cond)) {
        Node* binop = m.right().node();
        MaybeReplaceCmpZeroWithFlagSettingBinop(selector, &node, binop, &opcode,
                                                cond, cont);
      }
    }

    VisitWordCompare(selector, node, opcode, cont);
  }
}

}  // namespace

// Shared routine for word comparisons against zero.
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
    // Try to combine with comparisons against 0 by simply inverting the branch.
    while (value->opcode() == IrOpcode::kWord32Equal && CanCover(user, value)) {
      Int32BinopMatcher m(value);
      if (!m.right().Is(0)) break;

      user = value;
      value = m.left().node();
      cont->Negate();
    }

    if (CanCover(user, value)) {
      switch (value->opcode()) {
        case IrOpcode::kWord32Equal:
          cont->OverwriteAndNegateIfEqual(kEqual);
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kInt32LessThan:
          cont->OverwriteAndNegateIfEqual(kSignedLessThan);
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kInt32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kUint32LessThan:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kUint32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kFloat32Equal:
          cont->OverwriteAndNegateIfEqual(kEqual);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat32LessThan:
          cont->OverwriteAndNegateIfEqual(kFloatLessThan);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat64Equal:
          cont->OverwriteAndNegateIfEqual(kEqual);
          return VisitFloat64Compare(this, value, cont);
        case IrOpcode::kFloat64LessThan:
          cont->OverwriteAndNegateIfEqual(kFloatLessThan);
          return VisitFloat64Compare(this, value, cont);
        case IrOpcode::kFloat64LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
          return VisitFloat64Compare(this, value, cont);
        case IrOpcode::kProjection:
          // Check if this is the overflow output projection of an
          // <Operation>WithOverflow node.
          if (ProjectionIndexOf(value->op()) == 1u) {
            // We cannot combine the <Operation>WithOverflow with this branch
            // unless the 0th projection (the use of the actual value of the
            // <Operation> is either nullptr, which means there's no use of the
            // actual value, or was already defined, which means it is scheduled
            // *AFTER* this branch).
            Node* const node = value->InputAt(0);
            Node* const result = NodeProperties::FindProjection(node, 0);
            if (!result || IsDefined(result)) {
              switch (node->opcode()) {
                case IrOpcode::kInt32AddWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop(this, node, kArmAdd, kArmAdd, cont);
                case IrOpcode::kInt32SubWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop(this, node, kArmSub, kArmRsb, cont);
                case IrOpcode::kInt32MulWithOverflow:
                  // ARM doesn't set the overflow flag for multiplication, so we
                  // need to test on kNotEqual. Here is the code sequence used:
                  //   smull resultlow, resulthigh, left, right
                  //   cmp resulthigh, Operand(resultlow, ASR, 31)
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt32MulWithOverflow(this, node, cont);
                default:
                  break;
              }
            }
          }
          break;
        case IrOpcode::kInt32Add:
          return VisitWordCompare(this, value, kArmCmn, cont);
        case IrOpcode::kInt32Sub:
          return VisitWordCompare(this, value, kArmCmp, cont);
        case IrOpcode::kWord32And:
          return VisitWordCompare(this, value, kArmTst, cont);
        case IrOpcode::kWord32Or:
          return VisitBinop(this, value, kArmOrr, kArmOrr, cont);
        case IrOpcode::kWord32Xor:
          return VisitWordCompare(this, value, kArmTeq, cont);
        case IrOpcode::kWord32Sar:
          return VisitShift(this, value, TryMatchASR<Adapter>, cont);
        case IrOpcode::kWord32Shl:
          return VisitShift(this, value, TryMatchLSL<Adapter>, cont);
        case IrOpcode::kWord32Shr:
          return VisitShift(this, value, TryMatchLSR<Adapter>, cont);
        case IrOpcode::kWord32Ror:
          return VisitShift(this, value, TryMatchROR<Adapter>, cont);
        case IrOpcode::kStackPointerGreaterThan:
          cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
          return VisitStackPointerGreaterThan(value, cont);
        default:
          break;
      }
    }

    if (user->opcode() == IrOpcode::kWord32Equal) {
      return VisitWordCompare(this, user, cont);
    }

    // Continuation could not be combined with a compare, emit compare against
    // 0.
    ArmOperandGeneratorT<Adapter> g(this);
    InstructionCode const opcode =
        kArmTst | AddressingModeField::encode(kMode_Operand2_R);
    InstructionOperand const value_operand = g.UseRegister(value);
    EmitWithContinuation(opcode, value_operand, value_operand, cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  // Try to combine with comparisons against 0 by simply inverting the branch.
  ConsumeEqualZero(&user, &value, cont);

  if (CanCover(user, value)) {
    const Operation& value_op = Get(value);
    if (const ComparisonOp* comparison = value_op.TryCast<ComparisonOp>()) {
      switch (comparison->rep.MapTaggedToWord().value()) {
        case RegisterRepresentation::Word32():
          cont->OverwriteAndNegateIfEqual(
              GetComparisonFlagCondition(*comparison));
          return VisitWordCompare(this, value, cont);
        case RegisterRepresentation::Float32():
          switch (comparison->kind) {
            case ComparisonOp::Kind::kEqual:
              cont->OverwriteAndNegateIfEqual(kEqual);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kFloatLessThan);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
              return VisitFloat32Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        case RegisterRepresentation::Float64():
          switch (comparison->kind) {
            case ComparisonOp::Kind::kEqual:
              cont->OverwriteAndNegateIfEqual(kEqual);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kFloatLessThan);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kFloatLessThanOrEqual);
              return VisitFloat64Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        default:
          break;
      }
    } else if (const ProjectionOp* projection =
                   value_op.TryCast<ProjectionOp>()) {
      // Check if this is the overflow output projection of an
      // <Operation>WithOverflow node.
      if (projection->index == 1u) {
        // We cannot combine the <Operation>WithOverflow with this branch
        // unless the 0th projection (the use of the actual value of the
        // <Operation> is either nullptr, which means there's no use of the
        // actual value, or was already defined, which means it is scheduled
        // *AFTER* this branch).
        OpIndex node = projection->input();
        OpIndex result = FindProjection(node, 0);
        if (!result.valid() || IsDefined(result)) {
          if (const OverflowCheckedBinopOp* binop =
                  TryCast<OverflowCheckedBinopOp>(node)) {
            DCHECK_EQ(binop->rep, WordRepresentation::Word32());
            switch (binop->kind) {
              case OverflowCheckedBinopOp::Kind::kSignedAdd:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kArmAdd, kArmAdd, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kArmSub, kArmRsb, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                // ARM doesn't set the overflow flag for multiplication, so we
                // need to test on kNotEqual. Here is the code sequence used:
                //   smull resultlow, resulthigh, left, right
                //   cmp resulthigh, Operand(resultlow, ASR, 31)
                cont->OverwriteAndNegateIfEqual(kNotEqual);
                return EmitInt32MulWithOverflow(this, node, cont);
            }
          }
        }
      }
    } else if (value_op.Is<Opmask::kWord32Add>()) {
      return VisitWordCompare(this, value, kArmCmn, cont);
    } else if (value_op.Is<Opmask::kWord32Sub>()) {
      return VisitWordCompare(this, value, kArmCmp, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseAnd>()) {
      return VisitWordCompare(this, value, kArmTst, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseOr>()) {
      return VisitBinop(this, value, kArmOrr, kArmOrr, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseXor>()) {
      return VisitWordCompare(this, value, kArmTeq, cont);
    } else if (value_op.Is<Opmask::kWord32ShiftRightArithmetic>()) {
      return VisitShift(this, value, TryMatchASR<TurboshaftAdapter>, cont);
    } else if (value_op.Is<Opmask::kWord32ShiftLeft>()) {
      return VisitShift(this, value, TryMatchLSL<TurboshaftAdapter>, cont);
    } else if (value_op.Is<Opmask::kWord32ShiftRightLogical>()) {
      return VisitShift(this, value, TryMatchLSR<TurboshaftAdapter>, cont);
    } else if (value_op.Is<Opmask::kWord32RotateRight>()) {
      return VisitShift(this, value, TryMatchROR<TurboshaftAdapter>, cont);
    } else if (value_op.Is<StackPointerGreaterThanOp>()) {
      cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
      return VisitStackPointerGreaterThan(value, cont);
    }
  }

  if (Get(user).Is<Opmask::kWord32Equal>()) {
    return VisitWordCompare(this, user, cont);
  }

  // Continuation could not be combined with a compare, emit compare against
  // 0.
  ArmOperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionCode const opcode =
      kArmTst | AddressingModeField::encode(kMode_Operand2_R);
  InstructionOperand const value_operand = g.UseRegister(value);
  EmitWithContinuation(opcode, value_operand, value_operand, cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
  ArmOperandGeneratorT<Adapter> g(this);
  InstructionOperand value_operand = g.UseRegister(this->input_at(node, 0));

  // Emit either ArchTableSwitch or ArchBinarySearchSwitch.
  if (enable_switch_jump_table_ ==
      InstructionSelector::kEnableSwitchJumpTable) {
    static const size_t kMaxTableSwitchValueRange = 2 << 16;
    size_t table_space_cost = 4 + sw.value_range();
    size_t table_time_cost = 3;
    size_t lookup_space_cost = 3 + 2 * sw.case_count();
    size_t lookup_time_cost = sw.case_count();
    if (sw.case_count() > 0 &&
        table_space_cost + 3 * table_time_cost <=
            lookup_space_cost + 3 * lookup_time_cost &&
        sw.min_value() > std::numeric_limits<int32_t>::min() &&
        sw.value_range() <= kMaxTableSwitchValueRange) {
      InstructionOperand index_operand = value_operand;
      if (sw.min_value()) {
        index_operand = g.TempRegister();
        Emit(kArmSub | AddressingModeField::encode(kMode_Operand2_I),
             index_operand, value_operand, g.TempImmediate(sw.min_value()));
      }
      // Generate a table lookup.
      return EmitTableSwitch(sw, index_operand);
    }
  }

  // Generate a tree of conditional jumps.
  return EmitBinarySearchSwitch(sw, value_operand);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& equal = this->Get(node).template Cast<ComparisonOp>();
    if (this->MatchIntegralZero(equal.right())) {
      return VisitWordCompareZero(node, equal.left(), &cont);
    }
  } else {
    Int32BinopMatcher m(node);
    if (m.right().Is(0)) {
      return VisitWordCompareZero(m.node(), m.left().node(), &cont);
    }
  }
  VisitWordCompare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWordCompare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWordCompare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWordCompare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWordCompare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kArmAdd, kArmAdd, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kArmAdd, kArmAdd, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kArmSub, kArmRsb, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kArmSub, kArmRsb, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    // ARM doesn't set the overflow flag for multiplication, so we need to
    // test on kNotEqual. Here is the code sequence used:
    //   smull resultlow, resulthigh, left, right
    //   cmp resulthigh, Operand(resultlow, ASR, 31)
    FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
    return EmitInt32MulWithOverflow(this, node, &cont);
  }
  FlagsContinuation cont;
  EmitInt32MulWithOverflow(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kFloatLessThan, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kFloatLessThanOrEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kFloatLessThan, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kFloatLessThanOrEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertLowWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    ArmOperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    if (left->opcode() == IrOpcode::kFloat64InsertHighWord32 &&
        CanCover(node, left)) {
      left = left->InputAt(1);
      Emit(kArmVmovF64U32U32, g.DefineAsRegister(node), g.UseRegister(right),
           g.UseRegister(left));
      return;
    }
    Emit(kArmVmovLowF64U32, g.DefineSameAsFirst(node), g.UseRegister(left),
         g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertHighWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    ArmOperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    if (left->opcode() == IrOpcode::kFloat64InsertLowWord32 &&
        CanCover(node, left)) {
      left = left->InputAt(1);
      Emit(kArmVmovF64U32U32, g.DefineAsRegister(node), g.UseRegister(left),
           g.UseRegister(right));
      return;
    }
    Emit(kArmVmovHighF64U32, g.DefineSameAsFirst(node), g.UseRegister(left),
         g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  if constexpr (Adapter::IsTurbofan) {
    // The Turbofan implementation is split across VisitFloat64InsertLowWord32
    // and VisitFloat64InsertHighWord32.
    UNREACHABLE();
  } else {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    ArmOperandGeneratorT<TurboshaftAdapter> g(this);
    const BitcastWord32PairToFloat64Op& cast_op =
        this->Get(node).template Cast<BitcastWord32PairToFloat64Op>();
    Emit(kArmVmovF64U32U32, g.DefineAsRegister(node),
         g.UseRegister(cast_op.low_word32()),
         g.UseRegister(cast_op.high_word32()));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  // Use DMB ISH for both acquire-release and sequentially consistent barriers.
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmDmbIsh, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit LDR; DMB ISH.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  ArmOperandGeneratorT<Adapter> g(this);
  auto load = this->load_view(node);
  node_t base = load.base();
  node_t index = load.index();
  ArchOpcode opcode;
  LoadRepresentation load_rep = load.loaded_rep();
  switch (load_rep.representation()) {
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsSigned() ? kAtomicLoadInt8 : kAtomicLoadUint8;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsSigned() ? kAtomicLoadInt16 : kAtomicLoadUint16;
      break;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      opcode = kAtomicLoadWord32;
      break;
    default:
      UNREACHABLE();
  }
  Emit(opcode | AddressingModeField::encode(kMode_Offset_RR),
       g.DefineAsRegister(node), g.UseRegister(base), g.UseRegister(index));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
  auto store = this->store_view(node);
  AtomicStoreParameters store_params(store.stored_rep().representation(),
                                     store.stored_rep().write_barrier_kind(),
                                     store.memory_order().value(),
                                     store.access_kind());
  VisitStoreCommon(this, node, store_params.store_representation(),
                   store_params.order());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicExchange(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  auto atomic_op = this->atomic_rmw_view(node);
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = kAtomicExchangeInt8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kAtomicExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = kAtomicExchangeInt16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kAtomicExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kAtomicExchangeWord32;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = kAtomicExchangeInt8;
    } else if (type == MachineType::Uint8()) {
      opcode = kAtomicExchangeUint8;
    } else if (type == MachineType::Int16()) {
      opcode = kAtomicExchangeInt16;
    } else if (type == MachineType::Uint16()) {
      opcode = kAtomicExchangeUint16;
    } else if (type == MachineType::Int32() || type == MachineType::Uint32()) {
      opcode = kAtomicExchangeWord32;
    } else {
      UNREACHABLE();
    }
  }

  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseRegister(atomic_op.base());
  inputs[input_count++] = g.UseRegister(atomic_op.index());
  inputs[input_count++] = g.UseUniqueRegister(atomic_op.value());
  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(node);
  InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  Emit(code, 1, outputs, input_count, inputs, arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicCompareExchange(
    node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  auto atomic_op = this->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t old_value = atomic_op.expected();
  node_t new_value = atomic_op.value();
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = kAtomicCompareExchangeInt8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = kAtomicCompareExchangeUint8;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = kAtomicCompareExchangeInt16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = kAtomicCompareExchangeUint16;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = kAtomicCompareExchangeWord32;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = kAtomicCompareExchangeInt8;
    } else if (type == MachineType::Uint8()) {
      opcode = kAtomicCompareExchangeUint8;
    } else if (type == MachineType::Int16()) {
      opcode = kAtomicCompareExchangeInt16;
    } else if (type == MachineType::Uint16()) {
      opcode = kAtomicCompareExchangeUint16;
    } else if (type == MachineType::Int32() || type == MachineType::Uint32()) {
      opcode = kAtomicCompareExchangeWord32;
    } else {
      UNREACHABLE();
    }
  }

  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionOperand inputs[4];
  size_t input_count = 0;
  inputs[input_count++] = g.UseRegister(base);
  inputs[input_count++] = g.UseRegister(index);
  inputs[input_count++] = g.UseUniqueRegister(old_value);
  inputs[input_count++] = g.UseUniqueRegister(new_value);
  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(node);
  InstructionOperand temps[] = {g.TempRegister(), g.TempRegister(),
                                g.TempRegister()};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  Emit(code, 1, outputs, input_count, inputs, arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicBinaryOperation(
    node_t node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  ArmOperandGeneratorT<Adapter> g(this);
  auto atomic_op = this->atomic_rmw_view(node);
  ArchOpcode opcode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    if (atomic_op.memory_rep == MemoryRepresentation::Int8()) {
      opcode = int8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = uint8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int16()) {
      opcode = int16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = uint16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Int32() ||
               atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = word32_op;
    } else {
      UNREACHABLE();
    }
  } else {
    MachineType type = AtomicOpType(node->op());
    if (type == MachineType::Int8()) {
      opcode = int8_op;
    } else if (type == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (type == MachineType::Int16()) {
      opcode = int16_op;
    } else if (type == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (type == MachineType::Int32() || type == MachineType::Uint32()) {
      opcode = word32_op;
    } else {
      UNREACHABLE();
    }
  }

  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseRegister(atomic_op.base());
  inputs[input_count++] = g.UseRegister(atomic_op.index());
  inputs[input_count++] = g.UseUniqueRegister(atomic_op.value());
  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(node);
  InstructionOperand temps[] = {g.TempRegister(), g.TempRegister(),
                                g.TempRegister()};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  Emit(code, 1, outputs, input_count, inputs, arraysize(temps), temps);
}

#define VISIT_ATOMIC_BINOP(op)                                             \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::VisitWord32Atomic##op(node_t node) { \
    VisitWord32AtomicBinaryOperation(                                      \
        node, kAtomic##op##Int8, kAtomic##op##Uint8, kAtomic##op##Int16,   \
        kAtomic##op##Uint16, kAtomic##op##Word32);                         \
  }
VISIT_ATOMIC_BINOP(Add)
VISIT_ATOMIC_BINOP(Sub)
VISIT_ATOMIC_BINOP(And)
VISIT_ATOMIC_BINOP(Or)
VISIT_ATOMIC_BINOP(Xor)
#undef VISIT_ATOMIC_BINOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairLoad(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  node_t base = this->input_at(node, 0);
  node_t index = this->input_at(node, 1);
  InstructionOperand inputs[3];
  size_t input_count = 0;
  inputs[input_count++] = g.UseUniqueRegister(base);
  inputs[input_count++] = g.UseUniqueRegister(index);
  InstructionOperand temps[1];
  size_t temp_count = 0;
  InstructionOperand outputs[2];
  size_t output_count = 0;

  node_t projection0 = FindProjection(node, 0);
  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection0) && this->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, r0);
    outputs[output_count++] = g.DefineAsFixed(projection1, r1);
    temps[temp_count++] = g.TempRegister();
  } else if (this->valid(projection0)) {
    inputs[input_count++] = g.UseImmediate(0);
    outputs[output_count++] = g.DefineAsRegister(projection0);
  } else if (this->valid(projection1)) {
    inputs[input_count++] = g.UseImmediate(4);
    temps[temp_count++] = g.TempRegister();
    outputs[output_count++] = g.DefineAsRegister(projection1);
  } else {
    // There is no use of the loaded value, we don't need to generate code.
    return;
  }
  Emit(kArmWord32AtomicPairLoad, output_count, outputs, input_count, inputs,
       temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairStore(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  auto store = this->word32_atomic_pair_store_view(node);
  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionOperand inputs[] = {
      g.UseUniqueRegister(store.base()), g.UseUniqueRegister(store.index()),
      g.UseFixed(store.value_low(), r2), g.UseFixed(store.value_high(), r3)};
  InstructionOperand temps[] = {g.TempRegister(), g.TempRegister(r0),
                                g.TempRegister(r1)};
  InstructionCode code =
      kArmWord32AtomicPairStore | AddressingModeField::encode(addressing_mode);
  Emit(code, 0, nullptr, arraysize(inputs), inputs, arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAdd(node_t node) {
  VisitPairAtomicBinOp(this, node, kArmWord32AtomicPairAdd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairSub(node_t node) {
  VisitPairAtomicBinOp(this, node, kArmWord32AtomicPairSub);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAnd(node_t node) {
  VisitPairAtomicBinOp(this, node, kArmWord32AtomicPairAnd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairOr(node_t node) {
  VisitPairAtomicBinOp(this, node, kArmWord32AtomicPairOr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairXor(node_t node) {
  VisitPairAtomicBinOp(this, node, kArmWord32AtomicPairXor);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairExchange(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  node_t base = this->input_at(node, 0);
  node_t index = this->input_at(node, 1);
  node_t value = this->input_at(node, 2);
  node_t value_high = this->input_at(node, 3);
  AddressingMode addressing_mode = kMode_Offset_RR;
  InstructionOperand inputs[] = {
      g.UseFixed(value, r0), g.UseFixed(value_high, r1),
      g.UseUniqueRegister(base), g.UseUniqueRegister(index)};
  InstructionCode code = kArmWord32AtomicPairExchange |
                         AddressingModeField::encode(addressing_mode);
  node_t projection0 = FindProjection(node, 0);
  node_t projection1 = FindProjection(node, 1);
  InstructionOperand outputs[2];
  size_t output_count = 0;
  InstructionOperand temps[4];
  size_t temp_count = 0;
  temps[temp_count++] = g.TempRegister();
  temps[temp_count++] = g.TempRegister();
  if (this->valid(projection0)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, r6);
  } else {
    temps[temp_count++] = g.TempRegister(r6);
  }
  if (this->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, r7);
  } else {
    temps[temp_count++] = g.TempRegister(r7);
  }
  Emit(code, output_count, outputs, arraysize(inputs), inputs, temp_count,
       temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairCompareExchange(
    node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  AddressingMode addressing_mode = kMode_Offset_RR;

  // In the Turbofan and the Turboshaft graph the order of expected and value is
  // swapped.
  const size_t expected_offset = Adapter::IsTurboshaft ? 4 : 2;
  const size_t value_offset = Adapter::IsTurboshaft ? 2 : 4;
  InstructionOperand inputs[] = {
      g.UseFixed(this->input_at(node, expected_offset), r4),
      g.UseFixed(this->input_at(node, expected_offset + 1), r5),
      g.UseFixed(this->input_at(node, value_offset), r8),
      g.UseFixed(this->input_at(node, value_offset + 1), r9),
      g.UseUniqueRegister(this->input_at(node, 0)),
      g.UseUniqueRegister(this->input_at(node, 1))};
  InstructionCode code = kArmWord32AtomicPairCompareExchange |
                         AddressingModeField::encode(addressing_mode);
  node_t projection0 = FindProjection(node, 0);
  node_t projection1 = FindProjection(node, 1);
  InstructionOperand outputs[2];
  size_t output_count = 0;
  InstructionOperand temps[4];
  size_t temp_count = 0;
  temps[temp_count++] = g.TempRegister();
  temps[temp_count++] = g.TempRegister();
  if (this->valid(projection0)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, r2);
  } else {
    temps[temp_count++] = g.TempRegister(r2);
  }
  if (this->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, r3);
  } else {
    temps[temp_count++] = g.TempRegister(r3);
  }
  Emit(code, output_count, outputs, arraysize(inputs), inputs, temp_count,
       temps);
}

#define SIMD_UNOP_LIST(V)                               \
  V(F64x2Abs, kArmF64x2Abs)                             \
  V(F64x2Neg, kArmF64x2Neg)                             \
  V(F64x2Sqrt, kArmF64x2Sqrt)                           \
  V(F32x4SConvertI32x4, kArmF32x4SConvertI32x4)         \
  V(F32x4UConvertI32x4, kArmF32x4UConvertI32x4)         \
  V(F32x4Abs, kArmF32x4Abs)                             \
  V(F32x4Neg, kArmF32x4Neg)                             \
  V(I64x2Abs, kArmI64x2Abs)                             \
  V(I64x2SConvertI32x4Low, kArmI64x2SConvertI32x4Low)   \
  V(I64x2SConvertI32x4High, kArmI64x2SConvertI32x4High) \
  V(I64x2UConvertI32x4Low, kArmI64x2UConvertI32x4Low)   \
  V(I64x2UConvertI32x4High, kArmI64x2UConvertI32x4High) \
  V(I32x4SConvertF32x4, kArmI32x4SConvertF32x4)         \
  V(I32x4RelaxedTruncF32x4S, kArmI32x4SConvertF32x4)    \
  V(I32x4SConvertI16x8Low, kArmI32x4SConvertI16x8Low)   \
  V(I32x4SConvertI16x8High, kArmI32x4SConvertI16x8High) \
  V(I32x4Neg, kArmI32x4Neg)                             \
  V(I32x4UConvertF32x4, kArmI32x4UConvertF32x4)         \
  V(I32x4RelaxedTruncF32x4U, kArmI32x4UConvertF32x4)    \
  V(I32x4UConvertI16x8Low, kArmI32x4UConvertI16x8Low)   \
  V(I32x4UConvertI16x8High, kArmI32x4UConvertI16x8High) \
  V(I32x4Abs, kArmI32x4Abs)                             \
  V(I16x8SConvertI8x16Low, kArmI16x8SConvertI8x16Low)   \
  V(I16x8SConvertI8x16High, kArmI16x8SConvertI8x16High) \
  V(I16x8Neg, kArmI16x8Neg)                             \
  V(I16x8UConvertI8x16Low, kArmI16x8UConvertI8x16Low)   \
  V(I16x8UConvertI8x16High, kArmI16x8UConvertI8x16High) \
  V(I16x8Abs, kArmI16x8Abs)                             \
  V(I8x16Neg, kArmI8x16Neg)                             \
  V(I8x16Abs, kArmI8x16Abs)                             \
  V(I8x16Popcnt, kArmVcnt)                              \
  V(S128Not, kArmS128Not)                               \
  V(I64x2AllTrue, kArmI64x2AllTrue)                     \
  V(I32x4AllTrue, kArmI32x4AllTrue)                     \
  V(I16x8AllTrue, kArmI16x8AllTrue)                     \
  V(V128AnyTrue, kArmV128AnyTrue)                       \
  V(I8x16AllTrue, kArmI8x16AllTrue)

#define SIMD_SHIFT_OP_LIST(V) \
  V(I64x2Shl, 64)             \
  V(I64x2ShrS, 64)            \
  V(I64x2ShrU, 64)            \
  V(I32x4Shl, 32)             \
  V(I32x4ShrS, 32)            \
  V(I32x4ShrU, 32)            \
  V(I16x8Shl, 16)             \
  V(I16x8ShrS, 16)            \
  V(I16x8ShrU, 16)            \
  V(I8x16Shl, 8)              \
  V(I8x16ShrS, 8)             \
  V(I8x16ShrU, 8)

#define SIMD_BINOP_LIST(V)                            \
  V(F64x2Add, kArmF64x2Add)                           \
  V(F64x2Sub, kArmF64x2Sub)                           \
  V(F64x2Mul, kArmF64x2Mul)                           \
  V(F64x2Div, kArmF64x2Div)                           \
  V(F64x2Min, kArmF64x2Min)                           \
  V(F64x2Max, kArmF64x2Max)                           \
  V(F64x2Eq, kArmF64x2Eq)                             \
  V(F64x2Ne, kArmF64x2Ne)                             \
  V(F64x2Lt, kArmF64x2Lt)                             \
  V(F64x2Le, kArmF64x2Le)                             \
  V(F32x4Add, kArmF32x4Add)                           \
  V(F32x4Sub, kArmF32x4Sub)                           \
  V(F32x4Mul, kArmF32x4Mul)                           \
  V(F32x4Min, kArmF32x4Min)                           \
  V(F32x4RelaxedMin, kArmF32x4Min)                    \
  V(F32x4Max, kArmF32x4Max)                           \
  V(F32x4RelaxedMax, kArmF32x4Max)                    \
  V(F32x4Eq, kArmF32x4Eq)                             \
  V(F32x4Ne, kArmF32x4Ne)                             \
  V(F32x4Lt, kArmF32x4Lt)                             \
  V(F32x4Le, kArmF32x4Le)                             \
  V(I64x2Add, kArmI64x2Add)                           \
  V(I64x2Sub, kArmI64x2Sub)                           \
  V(I32x4Sub, kArmI32x4Sub)                           \
  V(I32x4Mul, kArmI32x4Mul)                           \
  V(I32x4MinS, kArmI32x4MinS)                         \
  V(I32x4MaxS, kArmI32x4MaxS)                         \
  V(I32x4Eq, kArmI32x4Eq)                             \
  V(I64x2Eq, kArmI64x2Eq)                             \
  V(I64x2Ne, kArmI64x2Ne)                             \
  V(I64x2GtS, kArmI64x2GtS)                           \
  V(I64x2GeS, kArmI64x2GeS)                           \
  V(I32x4Ne, kArmI32x4Ne)                             \
  V(I32x4GtS, kArmI32x4GtS)                           \
  V(I32x4GeS, kArmI32x4GeS)                           \
  V(I32x4MinU, kArmI32x4MinU)                         \
  V(I32x4MaxU, kArmI32x4MaxU)                         \
  V(I32x4GtU, kArmI32x4GtU)                           \
  V(I32x4GeU, kArmI32x4GeU)                           \
  V(I16x8SConvertI32x4, kArmI16x8SConvertI32x4)       \
  V(I16x8AddSatS, kArmI16x8AddSatS)                   \
  V(I16x8Sub, kArmI16x8Sub)                           \
  V(I16x8SubSatS, kArmI16x8SubSatS)                   \
  V(I16x8Mul, kArmI16x8Mul)                           \
  V(I16x8MinS, kArmI16x8MinS)                         \
  V(I16x8MaxS, kArmI16x8MaxS)                         \
  V(I16x8Eq, kArmI16x8Eq)                             \
  V(I16x8Ne, kArmI16x8Ne)                             \
  V(I16x8GtS, kArmI16x8GtS)                           \
  V(I16x8GeS, kArmI16x8GeS)                           \
  V(I16x8UConvertI32x4, kArmI16x8UConvertI32x4)       \
  V(I16x8AddSatU, kArmI16x8AddSatU)                   \
  V(I16x8SubSatU, kArmI16x8SubSatU)                   \
  V(I16x8MinU, kArmI16x8MinU)                         \
  V(I16x8MaxU, kArmI16x8MaxU)                         \
  V(I16x8GtU, kArmI16x8GtU)                           \
  V(I16x8GeU, kArmI16x8GeU)                           \
  V(I16x8RoundingAverageU, kArmI16x8RoundingAverageU) \
  V(I16x8Q15MulRSatS, kArmI16x8Q15MulRSatS)           \
  V(I16x8RelaxedQ15MulRS, kArmI16x8Q15MulRSatS)       \
  V(I8x16SConvertI16x8, kArmI8x16SConvertI16x8)       \
  V(I8x16Add, kArmI8x16Add)                           \
  V(I8x16AddSatS, kArmI8x16AddSatS)                   \
  V(I8x16Sub, kArmI8x16Sub)                           \
  V(I8x16SubSatS, kArmI8x16SubSatS)                   \
  V(I8x16MinS, kArmI8x16MinS)                         \
  V(I8x16MaxS, kArmI8x16MaxS)                         \
  V(I8x16Eq, kArmI8x16Eq)                             \
  V(I8x16Ne, kArmI8x16Ne)                             \
  V(I8x16GtS, kArmI8x16GtS)                           \
  V(I8x16GeS, kArmI8x16GeS)                           \
  V(I8x16UConvertI16x8, kArmI8x16UConvertI16x8)       \
  V(I8x16AddSatU, kArmI8x16AddSatU)                   \
  V(I8x16SubSatU, kArmI8x16SubSatU)                   \
  V(I8x16MinU, kArmI8x16MinU)                         \
  V(I8x16MaxU, kArmI8x16MaxU)                         \
  V(I8x16GtU, kArmI8x16GtU)                           \
  V(I8x16GeU, kArmI8x16GeU)                           \
  V(I8x16RoundingAverageU, kArmI8x16RoundingAverageU) \
  V(S128And, kArmS128And)                             \
  V(S128Or, kArmS128Or)                               \
  V(S128Xor, kArmS128Xor)                             \
  V(S128AndNot, kArmS128AndNot)

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI16x8S(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmI32x4DotI16x8S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseUniqueRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8DotI8x16I7x16S(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmI16x8DotI8x16S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseUniqueRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(kArmI32x4DotI8x16AddS, g.DefineSameAsInput(node, 2),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseUniqueRegister(this->input_at(node, 1)),
       g.UseUniqueRegister(this->input_at(node, 2)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  uint32_t val[kSimd128Size / sizeof(uint32_t)];
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ConstantOp& constant =
        this->Get(node).template Cast<turboshaft::Simd128ConstantOp>();
    memcpy(val, constant.value, kSimd128Size);
  } else {
    memcpy(val, S128ImmediateParameterOf(node->op()).data(), kSimd128Size);
  }
  // If all bytes are zeros, avoid emitting code for generic constants.
  bool all_zeros = !(val[0] || val[1] || val[2] || val[3]);
  bool all_ones = val[0] == UINT32_MAX && val[1] == UINT32_MAX &&
                  val[2] == UINT32_MAX && val[3] == UINT32_MAX;
  InstructionOperand dst = g.DefineAsRegister(node);
  if (all_zeros) {
    Emit(kArmS128Zero, dst);
  } else if (all_ones) {
    Emit(kArmS128AllOnes, dst);
  } else {
    Emit(kArmS128Const, dst, g.UseImmediate(val[0]), g.UseImmediate(val[1]),
         g.UseImmediate(val[2]), g.UseImmediate(val[3]));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmS128Zero, g.DefineAsRegister(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Splat(node_t node) {
  VisitRR(this, kArmF64x2Splat, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Splat(node_t node) {
  VisitRR(this, kArmF32x4Splat, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Splat(node_t node) {
  UNIMPLEMENTED();
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4Splat(node_t node) {
  VisitRR(this, kArmI32x4Splat, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8Splat(node_t node) {
  VisitRR(this, kArmI16x8Splat, node);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Splat(node_t node) {
  VisitRR(this, kArmI8x16Splat, node);
}

#if V8_ENABLE_WEBASSEMBLY
#define SIMD_VISIT_EXTRACT_LANE(Type, Sign)                           \
  template <typename Adapter>                                         \
  void InstructionSelectorT<Adapter>::Visit##Type##ExtractLane##Sign( \
      node_t node) {                                                  \
    VisitRRI(this, kArm##Type##ExtractLane##Sign, node);              \
  }
SIMD_VISIT_EXTRACT_LANE(F64x2, )
SIMD_VISIT_EXTRACT_LANE(F32x4, )
SIMD_VISIT_EXTRACT_LANE(I32x4, )
SIMD_VISIT_EXTRACT_LANE(I16x8, U)
SIMD_VISIT_EXTRACT_LANE(I16x8, S)
SIMD_VISIT_EXTRACT_LANE(I8x16, U)
SIMD_VISIT_EXTRACT_LANE(I8x16, S)
#undef SIMD_VISIT_EXTRACT_LANE

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8ExtractLane(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ReplaceLane(node_t node) {
  VisitRRIR(this, kArmF64x2ReplaceLane, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4ReplaceLane(node_t node) {
  VisitRRIR(this, kArmF32x4ReplaceLane, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8ReplaceLane(node_t node) {
  UNIMPLEMENTED();
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ReplaceLane(node_t node) {
  VisitRRIR(this, kArmI32x4ReplaceLane, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ReplaceLane(node_t node) {
  VisitRRIR(this, kArmI16x8ReplaceLane, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ReplaceLane(node_t node) {
  VisitRRIR(this, kArmI8x16ReplaceLane, node);
}

#define SIMD_VISIT_UNOP(Name, instruction)                       \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, instruction, node);                            \
  }
SIMD_UNOP_LIST(SIMD_VISIT_UNOP)
#undef SIMD_VISIT_UNOP
#undef SIMD_UNOP_LIST

#define UNIMPLEMENTED_SIMD_UNOP_LIST(V) \
  V(F16x8Abs)                           \
  V(F16x8Neg)                           \
  V(F16x8Sqrt)                          \
  V(F16x8Floor)                         \
  V(F16x8Ceil)                          \
  V(F16x8Trunc)                         \
  V(F16x8NearestInt)

#define SIMD_VISIT_UNIMPL_UNOP(Name)                             \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    UNIMPLEMENTED();                                             \
  }

UNIMPLEMENTED_SIMD_UNOP_LIST(SIMD_VISIT_UNIMPL_UNOP)
#undef SIMD_VISIT_UNIMPL_UNOP
#undef UNIMPLEMENTED_SIMD_UNOP_LIST

#define UNIMPLEMENTED_SIMD_CVTOP_LIST(V) \
  V(F16x8SConvertI16x8)                  \
  V(F16x8UConvertI16x8)                  \
  V(I16x8SConvertF16x8)                  \
  V(I16x8UConvertF16x8)                  \
  V(F32x4PromoteLowF16x8)                \
  V(F16x8DemoteF32x4Zero)                \
  V(F16x8DemoteF64x2Zero)

#define SIMD_VISIT_UNIMPL_CVTOP(Name)                            \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    UNIMPLEMENTED();                                             \
  }

UNIMPLEMENTED_SIMD_CVTOP_LIST(SIMD_VISIT_UNIMPL_CVTOP)
#undef SIMD_VISIT_UNIMPL_CVTOP
#undef UNIMPLEMENTED_SIMD_CVTOP_LIST

#define SIMD_VISIT_SHIFT_OP(Name, width)                         \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitSimdShiftRRR(this, kArm##Name, node, width);            \
  }
SIMD_SHIFT_OP_LIST(SIMD_VISIT_SHIFT_OP)
#undef SIMD_VISIT_SHIFT_OP
#undef SIMD_SHIFT_OP_LIST

#define SIMD_VISIT_BINOP(Name, instruction)                      \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRRR(this, instruction, node);                           \
  }
SIMD_BINOP_LIST(SIMD_VISIT_BINOP)
#undef SIMD_VISIT_BINOP
#undef SIMD_BINOP_LIST

#define UNIMPLEMENTED_SIMD_BINOP_LIST(V) \
  V(F16x8Add)                            \
  V(F16x8Sub)                            \
  V(F16x8Mul)                            \
  V(F16x8Div)                            \
  V(F16x8Min)                            \
  V(F16x8Max)                            \
  V(F16x8Pmin)                           \
  V(F16x8Pmax)                           \
  V(F16x8Eq)                             \
  V(F16x8Ne)                             \
  V(F16x8Lt)                             \
  V(F16x8Le)

#define SIMD_VISIT_UNIMPL_BINOP(Name)                            \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    UNIMPLEMENTED();                                             \
  }

UNIMPLEMENTED_SIMD_BINOP_LIST(SIMD_VISIT_UNIMPL_BINOP)
#undef SIMD_VISIT_UNIMPL_BINOP
#undef UNIMPLEMENTED_SIMD_BINOP_LIST

// TODO(mliedtke): This macro has only two uses. Maybe this could be refactored
// into some helpers instead of the huge macro.
#define VISIT_SIMD_ADD(Type, PairwiseType, NeonWidth)                          \
  template <>                                                                  \
  void InstructionSelectorT<TurboshaftAdapter>::Visit##Type##Add(              \
      node_t node) {                                                           \
    using namespace turboshaft; /*NOLINT(build/namespaces)*/                   \
    ArmOperandGeneratorT<TurboshaftAdapter> g(this);                           \
    const Simd128BinopOp& add_op = Get(node).Cast<Simd128BinopOp>();           \
    const Operation& left = Get(add_op.left());                                \
    const Operation& right = Get(add_op.right());                              \
    if (left.Is<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##S>() &&  \
        CanCover(node, add_op.left())) {                                       \
      Emit(kArmVpadal | MiscField::encode(NeonS##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(add_op.right()),           \
           g.UseRegister(left.input(0)));                                      \
      return;                                                                  \
    }                                                                          \
    if (left.Is<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##U>() &&  \
        CanCover(node, add_op.left())) {                                       \
      Emit(kArmVpadal | MiscField::encode(NeonU##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(add_op.right()),           \
           g.UseRegister(left.input(0)));                                      \
      return;                                                                  \
    }                                                                          \
    if (right.Is<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##S>() && \
        CanCover(node, add_op.right())) {                                      \
      Emit(kArmVpadal | MiscField::encode(NeonS##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(add_op.left()),            \
           g.UseRegister(right.input(0)));                                     \
      return;                                                                  \
    }                                                                          \
    if (right.Is<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##U>() && \
        CanCover(node, add_op.right())) {                                      \
      Emit(kArmVpadal | MiscField::encode(NeonU##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(add_op.left()),            \
           g.UseRegister(right.input(0)));                                     \
      return;                                                                  \
    }                                                                          \
    VisitRRR(this, kArm##Type##Add, node);                                     \
  }                                                                            \
  template <>                                                                  \
  void InstructionSelectorT<TurbofanAdapter>::Visit##Type##Add(Node* node) {   \
    ArmOperandGeneratorT<TurbofanAdapter> g(this);                             \
    Node* left = node->InputAt(0);                                             \
    Node* right = node->InputAt(1);                                            \
    if (left->opcode() ==                                                      \
            IrOpcode::k##Type##ExtAddPairwise##PairwiseType##S &&              \
        CanCover(node, left)) {                                                \
      Emit(kArmVpadal | MiscField::encode(NeonS##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(right),                    \
           g.UseRegister(left->InputAt(0)));                                   \
      return;                                                                  \
    }                                                                          \
    if (left->opcode() ==                                                      \
            IrOpcode::k##Type##ExtAddPairwise##PairwiseType##U &&              \
        CanCover(node, left)) {                                                \
      Emit(kArmVpadal | MiscField::encode(NeonU##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(right),                    \
           g.UseRegister(left->InputAt(0)));                                   \
      return;                                                                  \
    }                                                                          \
    if (right->opcode() ==                                                     \
            IrOpcode::k##Type##ExtAddPairwise##PairwiseType##S &&              \
        CanCover(node, right)) {                                               \
      Emit(kArmVpadal | MiscField::encode(NeonS##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(left),                     \
           g.UseRegister(right->InputAt(0)));                                  \
      return;                                                                  \
    }                                                                          \
    if (right->opcode() ==                                                     \
            IrOpcode::k##Type##ExtAddPairwise##PairwiseType##U &&              \
        CanCover(node, right)) {                                               \
      Emit(kArmVpadal | MiscField::encode(NeonU##NeonWidth),                   \
           g.DefineSameAsFirst(node), g.UseRegister(left),                     \
           g.UseRegister(right->InputAt(0)));                                  \
      return;                                                                  \
    }                                                                          \
    VisitRRR(this, kArm##Type##Add, node);                                     \
  }

VISIT_SIMD_ADD(I16x8, I8x16, 8)
VISIT_SIMD_ADD(I32x4, I16x8, 16)
#undef VISIT_SIMD_ADD

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2SplatI32Pair(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // In turboshaft it gets lowered to an I32x4Splat.
    UNREACHABLE();
  } else {
    ArmOperandGeneratorT<Adapter> g(this);
    InstructionOperand operand0 = g.UseRegister(node->InputAt(0));
    InstructionOperand operand1 = g.UseRegister(node->InputAt(1));
    Emit(kArmI64x2SplatI32Pair, g.DefineAsRegister(node), operand0, operand1);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ReplaceLaneI32Pair(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // In turboshaft it gets lowered to an I32x4ReplaceLane.
    UNREACHABLE();
  } else {
    ArmOperandGeneratorT<Adapter> g(this);
    InstructionOperand operand = g.UseRegister(node->InputAt(0));
    InstructionOperand lane = g.UseImmediate(OpParameter<int32_t>(node->op()));
    InstructionOperand low = g.UseRegister(node->InputAt(1));
    InstructionOperand high = g.UseRegister(node->InputAt(2));
    Emit(kArmI64x2ReplaceLaneI32Pair, g.DefineSameAsFirst(node), operand, lane,
         low, high);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Neg(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmI64x2Neg, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Mul(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    InstructionOperand temps[] = {g.TempSimd128Register()};
    Emit(kArmI64x2Mul, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Sqrt(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  // Use fixed registers in the lower 8 Q-registers so we can directly access
  // mapped registers S0-S31.
  Emit(kArmF32x4Sqrt, g.DefineAsFixed(node, q0),
       g.UseFixed(this->input_at(node, 0), q0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Div(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  // Use fixed registers in the lower 8 Q-registers so we can directly access
  // mapped registers S0-S31.
  Emit(kArmF32x4Div, g.DefineAsFixed(node, q0),
       g.UseFixed(this->input_at(node, 0), q0),
       g.UseFixed(this->input_at(node, 1), q1));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmS128Select, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 2)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16RelaxedLaneSelect(node_t node) {
  VisitS128Select(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8RelaxedLaneSelect(node_t node) {
  VisitS128Select(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedLaneSelect(node_t node) {
  VisitS128Select(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2RelaxedLaneSelect(node_t node) {
  VisitS128Select(node);
}

#define VISIT_SIMD_QFMOP(op)                                   \
  template <typename Adapter>                                  \
  void InstructionSelectorT<Adapter>::Visit##op(node_t node) { \
    ArmOperandGeneratorT<Adapter> g(this);                     \
    Emit(kArm##op, g.DefineAsRegister(node),                   \
         g.UseUniqueRegister(this->input_at(node, 0)),         \
         g.UseUniqueRegister(this->input_at(node, 1)),         \
         g.UseUniqueRegister(this->input_at(node, 2)));        \
  }
VISIT_SIMD_QFMOP(F64x2Qfma)
VISIT_SIMD_QFMOP(F64x2Qfms)
VISIT_SIMD_QFMOP(F32x4Qfma)
VISIT_SIMD_QFMOP(F32x4Qfms)
#undef VISIT_SIMD_QFMOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Qfma(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Qfms(node_t node) {
  UNIMPLEMENTED();
}
namespace {

struct ShuffleEntry {
  uint8_t shuffle[kSimd128Size];
  ArchOpcode opcode;
};

static const ShuffleEntry arch_shuffles[] = {
    {{0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23},
     kArmS32x4ZipLeft},
    {{8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31},
     kArmS32x4ZipRight},
    {{0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27},
     kArmS32x4UnzipLeft},
    {{4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31},
     kArmS32x4UnzipRight},
    {{0, 1, 2, 3, 16, 17, 18, 19, 8, 9, 10, 11, 24, 25, 26, 27},
     kArmS32x4TransposeLeft},
    {{4, 5, 6, 7, 20, 21, 22, 23, 12, 13, 14, 15, 28, 29, 30, 31},
     kArmS32x4TransposeRight},
    {{4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11}, kArmS32x2Reverse},

    {{0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23},
     kArmS16x8ZipLeft},
    {{8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31},
     kArmS16x8ZipRight},
    {{0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29},
     kArmS16x8UnzipLeft},
    {{2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31},
     kArmS16x8UnzipRight},
    {{0, 1, 16, 17, 4, 5, 20, 21, 8, 9, 24, 25, 12, 13, 28, 29},
     kArmS16x8TransposeLeft},
    {{2, 3, 18, 19, 6, 7, 22, 23, 10, 11, 26, 27, 14, 15, 30, 31},
     kArmS16x8TransposeRight},
    {{6, 7, 4, 5, 2, 3, 0, 1, 14, 15, 12, 13, 10, 11, 8, 9}, kArmS16x4Reverse},
    {{2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13}, kArmS16x2Reverse},

    {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23},
     kArmS8x16ZipLeft},
    {{8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31},
     kArmS8x16ZipRight},
    {{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30},
     kArmS8x16UnzipLeft},
    {{1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31},
     kArmS8x16UnzipRight},
    {{0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30},
     kArmS8x16TransposeLeft},
    {{1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31},
     kArmS8x16TransposeRight},
    {{7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8}, kArmS8x8Reverse},
    {{3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12}, kArmS8x4Reverse},
    {{1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14}, kArmS8x2Reverse}};

bool TryMatchArchShuffle(const uint8_t* shuffle, const ShuffleEntry* table,
                         size_t num_entries, bool is_swizzle,
                         ArchOpcode* opcode) {
  uint8_t mask = is_swizzle ? kSimd128Size - 1 : 2 * kSimd128Size - 1;
  for (size_t i = 0; i < num_entries; ++i) {
    const ShuffleEntry& entry = table[i];
    int j = 0;
    for (; j < kSimd128Size; ++j) {
      if ((entry.shuffle[j] & mask) != (shuffle[j] & mask)) {
        break;
      }
    }
    if (j == kSimd128Size) {
      *opcode = entry.opcode;
      return true;
    }
  }
  return false;
}

template <typename Adapter>
void ArrangeShuffleTable(ArmOperandGeneratorT<Adapter>* g,
                         typename Adapter::node_t input0,
                         typename Adapter::node_t input1,
                         InstructionOperand* src0, InstructionOperand* src1) {
  if (input0 == input1) {
    // Unary, any q-register can be the table.
    *src0 = *src1 = g->UseRegister(input0);
  } else {
    // Binary, table registers must be consecutive.
    *src0 = g->UseFixed(input0, q0);
    *src1 = g->UseFixed(input1, q1);
  }
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
  uint8_t shuffle[kSimd128Size];
  bool is_swizzle;
  // TODO(nicohartmann@): Properly use view here once Turboshaft support is
  // implemented.
  auto view = this->simd_shuffle_view(node);
  CanonicalizeShuffle(view, shuffle, &is_swizzle);
  node_t input0 = view.input(0);
  node_t input1 = view.input(1);
  uint8_t shuffle32x4[4];
  ArmOperandGeneratorT<Adapter> g(this);
  int index = 0;
  if (wasm::SimdShuffle::TryMatch32x4Shuffle(shuffle, shuffle32x4)) {
    if (wasm::SimdShuffle::TryMatchSplat<4>(shuffle, &index)) {
      DCHECK_GT(4, index);
      Emit(kArmS128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
           g.UseImmediate(Neon32), g.UseImmediate(index % 4));
    } else if (wasm::SimdShuffle::TryMatchIdentity(shuffle)) {
      // Bypass normal shuffle code generation in this case.
      // EmitIdentity
      MarkAsUsed(input0);
      MarkAsDefined(node);
      SetRename(node, input0);
    } else {
      // 32x4 shuffles are implemented as s-register moves. To simplify these,
      // make sure the destination is distinct from both sources.
      InstructionOperand src0 = g.UseUniqueRegister(input0);
      InstructionOperand src1 = is_swizzle ? src0 : g.UseUniqueRegister(input1);
      Emit(kArmS32x4Shuffle, g.DefineAsRegister(node), src0, src1,
           g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle32x4)));
    }
    return;
  }
  if (wasm::SimdShuffle::TryMatchSplat<8>(shuffle, &index)) {
    DCHECK_GT(8, index);
    Emit(kArmS128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseImmediate(Neon16), g.UseImmediate(index % 8));
    return;
  }
  if (wasm::SimdShuffle::TryMatchSplat<16>(shuffle, &index)) {
    DCHECK_GT(16, index);
    Emit(kArmS128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseImmediate(Neon8), g.UseImmediate(index % 16));
    return;
  }
  ArchOpcode opcode;
  if (TryMatchArchShuffle(shuffle, arch_shuffles, arraysize(arch_shuffles),
                          is_swizzle, &opcode)) {
    VisitRRRShuffle(this, opcode, node, input0, input1);
    return;
  }
  uint8_t offset;
  if (wasm::SimdShuffle::TryMatchConcat(shuffle, &offset)) {
    Emit(kArmS8x16Concat, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseRegister(input1), g.UseImmediate(offset));
    return;
  }
  // Code generator uses vtbl, arrange sources to form a valid lookup table.
  InstructionOperand src0, src1;
  ArrangeShuffleTable(&g, input0, input1, &src0, &src1);
  Emit(kArmI8x16Shuffle, g.DefineAsRegister(node), src0, src1,
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 4)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 8)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 12)));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSetStackPointer(Node* node) {
  OperandGenerator g(this);
  auto input = g.UseRegister(node->InputAt(0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSetStackPointer(
    node_t node) {
  OperandGenerator g(this);
  auto input = g.UseRegister(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Swizzle(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    // We don't want input 0 (the table) to be the same as output, since we will
    // modify output twice (low and high), and need to keep the table the same.
    Emit(kArmI8x16Swizzle, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
}

#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt32(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmSxtb, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)), g.TempImmediate(0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt32(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmSxth, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)), g.TempImmediate(0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

namespace {
template <typename Adapter, ArchOpcode opcode>
void VisitBitMask(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node) {
    ArmOperandGeneratorT<Adapter> g(selector);
    InstructionOperand temps[] = {g.TempSimd128Register()};
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)), arraysize(temps),
                   temps);
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16BitMask(node_t node) {
  VisitBitMask<Adapter, kArmI8x16BitMask>(this, node);
}

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8BitMask(node_t node) {
  VisitBitMask<Adapter, kArmI16x8BitMask>(this, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4BitMask(node_t node) {
  VisitBitMask<Adapter, kArmI32x4BitMask>(this, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2BitMask(node_t node) {
  VisitBitMask<Adapter, kArmI64x2BitMask>(this, node);
}

namespace {
template <typename Adapter>
void VisitF32x4PminOrPmax(InstructionSelectorT<Adapter>* selector,
                          ArchOpcode opcode, typename Adapter::node_t node) {
    ArmOperandGeneratorT<Adapter> g(selector);
    // Need all unique registers because we first compare the two inputs, then
    // we need the inputs to remain unchanged for the bitselect later.
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseUniqueRegister(selector->input_at(node, 0)),
                   g.UseUniqueRegister(selector->input_at(node, 1)));
}

template <typename Adapter>
void VisitF64x2PminOrPMax(InstructionSelectorT<Adapter>* selector,
                          ArchOpcode opcode, typename Adapter::node_t node) {
    ArmOperandGeneratorT<Adapter> g(selector);
    selector->Emit(opcode, g.DefineSameAsFirst(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmin(node_t node) {
  VisitF32x4PminOrPmax(this, kArmF32x4Pmin, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmax(node_t node) {
  VisitF32x4PminOrPmax(this, kArmF32x4Pmax, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmin(node_t node) {
  VisitF64x2PminOrPMax(this, kArmF64x2Pmin, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmax(node_t node) {
  VisitF64x2PminOrPMax(this, kArmF64x2Pmax, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMin(node_t node) {
  VisitF64x2Pmin(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMax(node_t node) {
  VisitF64x2Pmax(node);
}

#define EXT_MUL_LIST(V)                            \
  V(I16x8ExtMulLowI8x16S, kArmVmullLow, NeonS8)    \
  V(I16x8ExtMulHighI8x16S, kArmVmullHigh, NeonS8)  \
  V(I16x8ExtMulLowI8x16U, kArmVmullLow, NeonU8)    \
  V(I16x8ExtMulHighI8x16U, kArmVmullHigh, NeonU8)  \
  V(I32x4ExtMulLowI16x8S, kArmVmullLow, NeonS16)   \
  V(I32x4ExtMulHighI16x8S, kArmVmullHigh, NeonS16) \
  V(I32x4ExtMulLowI16x8U, kArmVmullLow, NeonU16)   \
  V(I32x4ExtMulHighI16x8U, kArmVmullHigh, NeonU16) \
  V(I64x2ExtMulLowI32x4S, kArmVmullLow, NeonS32)   \
  V(I64x2ExtMulHighI32x4S, kArmVmullHigh, NeonS32) \
  V(I64x2ExtMulLowI32x4U, kArmVmullLow, NeonU32)   \
  V(I64x2ExtMulHighI32x4U, kArmVmullHigh, NeonU32)

#define VISIT_EXT_MUL(OPCODE, VMULL, NEONSIZE)                     \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##OPCODE(node_t node) { \
    VisitRRR(this, VMULL | MiscField::encode(NEONSIZE), node);     \
  }

EXT_MUL_LIST(VISIT_EXT_MUL)

#undef VISIT_EXT_MUL
#undef EXT_MUL_LIST

#define VISIT_EXTADD_PAIRWISE(OPCODE, NEONSIZE)                    \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##OPCODE(node_t node) { \
    VisitRR(this, kArmVpaddl | MiscField::encode(NEONSIZE), node); \
  }
VISIT_EXTADD_PAIRWISE(I16x8ExtAddPairwiseI8x16S, NeonS8)
VISIT_EXTADD_PAIRWISE(I16x8ExtAddPairwiseI8x16U, NeonU8)
VISIT_EXTADD_PAIRWISE(I32x4ExtAddPairwiseI16x8S, NeonS16)
VISIT_EXTADD_PAIRWISE(I32x4ExtAddPairwiseI16x8U, NeonU16)
#undef VISIT_EXTADD_PAIRWISE

// TODO(v8:9780)
// These double precision conversion instructions need a low Q register (q0-q7)
// because the codegen accesses the S registers they overlap with.
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ConvertLowI32x4S(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmF64x2ConvertLowI32x4S, g.DefineAsRegister(node),
         g.UseFixed(this->input_at(node, 0), q0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ConvertLowI32x4U(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmF64x2ConvertLowI32x4U, g.DefineAsRegister(node),
         g.UseFixed(this->input_at(node, 0), q0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2SZero(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmI32x4TruncSatF64x2SZero, g.DefineAsFixed(node, q0),
         g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2UZero(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  Emit(kArmI32x4TruncSatF64x2UZero, g.DefineAsFixed(node, q0),
       g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4DemoteF64x2Zero(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmF32x4DemoteF64x2Zero, g.DefineAsFixed(node, q0),
         g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2PromoteLowF32x4(node_t node) {
    ArmOperandGeneratorT<Adapter> g(this);
    Emit(kArmF64x2PromoteLowF32x4, g.DefineAsRegister(node),
         g.UseFixed(this->input_at(node, 0), q0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2SZero(
    node_t node) {
  VisitI32x4TruncSatF64x2SZero(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2UZero(
    node_t node) {
  VisitI32x4TruncSatF64x2UZero(node);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToInt32(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kArmVcvtS32F32;
    if (op.Is<Opmask::kTruncateFloat32ToInt32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kArmVcvtS32F32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToUint32(node_t node) {
  ArmOperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kArmVcvtU32F32;
    if (op.Is<Opmask::kTruncateFloat32ToUint32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)));
  } else {
    InstructionCode opcode = kArmVcvtU32F32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGenerator* g, int first_input_index, node_t node) {
  UNREACHABLE();
}

// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  MachineOperatorBuilder::Flags flags = MachineOperatorBuilder::kNoFlags;
  if (CpuFeatures::IsSupported(SUDIV)) {
    // The sdiv and udiv instructions correctly return 0 if the divisor is 0,
    // but the fall-back implementation does not.
    flags |= MachineOperatorBuilder::kInt32DivIsSafe |
             MachineOperatorBuilder::kUint32DivIsSafe;
  }
  if (CpuFeatures::IsSupported(ARMv7)) {
    flags |= MachineOperatorBuilder::kWord32ReverseBits;
  }
  if (CpuFeatures::IsSupported(ARMv8)) {
    flags |= MachineOperatorBuilder::kFloat32RoundDown |
             MachineOperatorBuilder::kFloat64RoundDown |
             MachineOperatorBuilder::kFloat32RoundUp |
             MachineOperatorBuilder::kFloat64RoundUp |
             MachineOperatorBuilder::kFloat32RoundTruncate |
             MachineOperatorBuilder::kFloat64RoundTruncate |
             MachineOperatorBuilder::kFloat64RoundTiesAway |
             MachineOperatorBuilder::kFloat32RoundTiesEven |
             MachineOperatorBuilder::kFloat64RoundTiesEven;
  }
  flags |= MachineOperatorBuilder::kSatConversionIsSafe;
  return flags;
}

// static
MachineOperatorBuilder::AlignmentRequirements
InstructionSelector::AlignmentRequirements() {
  base::EnumSet<MachineRepresentation> req_aligned;
  req_aligned.Add(MachineRepresentation::kFloat32);
  req_aligned.Add(MachineRepresentation::kFloat64);
  return MachineOperatorBuilder::AlignmentRequirements::
      SomeUnalignedAccessUnsupported(req_aligned, req_aligned);
}

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurbofanAdapter>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurboshaftAdapter>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8
