// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef V8_COMPILER_BACKEND_RISCV_INSTRUCTION_SELECTOR_RISCV_H_
#define V8_COMPILER_BACKEND_RISCV_INSTRUCTION_SELECTOR_RISCV_H_

#include <optional>

#include "src/base/bits.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/turboshaft/operations.h"

namespace v8 {
namespace internal {
namespace compiler {

#define TRACE_UNIMPL() \
  PrintF("UNIMPLEMENTED instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)

#define TRACE() PrintF("instr_sel: %s at line %d\n", __FUNCTION__, __LINE__)

// Adds RISC-V-specific methods for generating InstructionOperands.
template <typename Adapter>
class RiscvOperandGeneratorT final : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit RiscvOperandGeneratorT<Adapter>(
      InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  InstructionOperand UseOperand(typename Adapter::node_t node,
                                InstructionCode opcode) {
    if (CanBeImmediate(node, opcode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  // Use the zero register if the node has the immediate value zero, otherwise
  // assign a register.
  InstructionOperand UseRegisterOrImmediateZero(typename Adapter::node_t node) {
    if (this->is_constant(node)) {
      auto constant = selector()->constant_view(node);
      if ((IsIntegerConstant(constant) &&
           GetIntegerConstantValue(constant) == 0) ||
          constant.is_float_zero()) {
        return UseImmediate(node);
      }
    }
    return UseRegister(node);
  }

  bool IsIntegerConstant(typename Adapter::node_t node) const {
    return selector()->is_integer_constant(node);
  }

  int64_t GetIntegerConstantValue(Node* node);

  bool IsIntegerConstant(typename Adapter::ConstantView constant) {
    return constant.is_int32() || constant.is_int64();
  }

  int64_t GetIntegerConstantValue(typename Adapter::ConstantView constant) {
    if (constant.is_int32()) {
      return constant.int32_value();
    }
    DCHECK(constant.is_int64());
    return constant.int64_value();
  }

  std::optional<int64_t> GetOptionalIntegerConstant(
      InstructionSelectorT<TurboshaftAdapter>* selector,
      turboshaft::OpIndex operation) {
    if (!this->is_constant(operation)) return {};
    auto constant = selector->constant_view(operation);
    if (!this->IsIntegerConstant(constant)) return {};
    return this->GetIntegerConstantValue(constant);
  }

  bool IsFloatConstant(Node* node) {
    return (node->opcode() == IrOpcode::kFloat32Constant) ||
           (node->opcode() == IrOpcode::kFloat64Constant);
  }

  double GetFloatConstantValue(Node* node) {
    if (node->opcode() == IrOpcode::kFloat32Constant) {
      return OpParameter<float>(node->op());
    }
    DCHECK_EQ(IrOpcode::kFloat64Constant, node->opcode());
    return OpParameter<double>(node->op());
  }
  bool CanBeZero(typename Adapter::node_t node) {
    if (this->is_constant(node)) {
      auto constant = selector()->constant_view(node);
      if ((IsIntegerConstant(constant) &&
           GetIntegerConstantValue(constant) == 0) ||
          constant.is_float_zero()) {
        return true;
      }
    }
    return false;
  }

  bool CanBeImmediate(node_t node, InstructionCode mode) {
    if (!this->is_constant(node)) return false;
    auto constant = this->constant_view(node);
    if (constant.is_compressed_heap_object()) {
      if (!COMPRESS_POINTERS_BOOL) return false;
      // For builtin code we need static roots
      if (selector()->isolate()->bootstrapper() && !V8_STATIC_ROOTS_BOOL) {
        return false;
      }
      const RootsTable& roots_table = selector()->isolate()->roots_table();
      RootIndex root_index;
      Handle<HeapObject> value = constant.heap_object_value();
      if (roots_table.IsRootHandle(value, &root_index)) {
        if (!RootsTable::IsReadOnly(root_index)) return false;
        return CanBeImmediate(MacroAssemblerBase::ReadOnlyRootPtr(
                                  root_index, selector()->isolate()),
                              mode);
      }
      return false;
    }

    return IsIntegerConstant(constant) &&
           CanBeImmediate(GetIntegerConstantValue(constant), mode);
  }

  bool CanBeImmediate(int64_t value, InstructionCode opcode);

 private:
  bool ImmediateFitsAddrMode1Instruction(int32_t imm) const {
    TRACE_UNIMPL();
    return false;
  }
};

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
             typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
             typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
static void VisitRRI(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
                     typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = selector->Get(node);
    int imm = op.template Cast<Simd128ExtractLaneOp>().lane;
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)),
                   g.UseImmediate(imm));
  } else {
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseImmediate(imm));
  }
}

template <typename Adapter>
static void VisitSimdShift(InstructionSelectorT<Adapter>* selector,
                           ArchOpcode opcode, typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  if (g.IsIntegerConstant(selector->input_at(node, 1))) {
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseImmediate(selector->input_at(node, 1)));
  } else {
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));
  }
}

template <typename Adapter>
static void VisitRRIR(InstructionSelectorT<Adapter>* selector,
                      ArchOpcode opcode, typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ReplaceLaneOp& op =
        selector->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)),
                   g.UseImmediate(op.lane), g.UseUniqueRegister(op.input(1)));
  } else {
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseImmediate(imm),
                   g.UseRegister(selector->input_at(node, 1)));
  }
}

template <typename Adapter>
void VisitRRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node,
              typename OperandGeneratorT<Adapter>::RegisterUseKind kind =
                  OperandGeneratorT<Adapter>::RegisterUseKind::kUseRegister) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1), kind));
}

void VisitRRR(InstructionSelectorT<TurbofanAdapter>* selector,
              InstructionCode opcode, Node* node) {
  RiscvOperandGeneratorT<TurbofanAdapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)));
}

template <typename Adapter>
static void VisitUniqueRRR(InstructionSelectorT<Adapter>* selector,
                           ArchOpcode opcode, Node* node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseUniqueRegister(selector->input_at(node, 0)),
                 g.UseUniqueRegister(selector->input_at(node, 1)));
}

template <typename Adapter>
void VisitRRRR(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
               Node* node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineSameAsFirst(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)),
                 g.UseRegister(node->InputAt(2)));
}

template <typename Adapter>
static void VisitRRO(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
                     typename Adapter::node_t node) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseOperand(selector->input_at(node, 1), opcode));
}

template <typename Adapter>
bool TryMatchImmediate(InstructionSelectorT<Adapter>* selector,
                       InstructionCode* opcode_return,
                       typename Adapter::node_t node,
                       size_t* input_count_return, InstructionOperand* inputs) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  if (g.CanBeImmediate(node, *opcode_return)) {
    *opcode_return |= AddressingModeField::encode(kMode_MRI);
    inputs[0] = g.UseImmediate(node);
    *input_count_return = 1;
    return true;
  }
  return false;
}

// Shared routine for multiple binary operations.
template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<TurboshaftAdapter>* selector,
                       typename TurboshaftAdapter::node_t node,
                       InstructionCode opcode, bool has_reverse_opcode,
                       InstructionCode reverse_opcode,
                       FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  RiscvOperandGeneratorT<TurboshaftAdapter> g(selector);
  InstructionOperand inputs[2];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  const Operation& binop = selector->Get(node);
  OpIndex left_node = binop.input(0);
  OpIndex right_node = binop.input(1);

  if (TryMatchImmediate(selector, &opcode, right_node, &input_count,
                        &inputs[1])) {
    inputs[0] = g.UseRegisterOrImmediateZero(left_node);
    input_count++;
  } else if (has_reverse_opcode &&
             TryMatchImmediate(selector, &reverse_opcode, left_node,
                               &input_count, &inputs[1])) {
    inputs[0] = g.UseRegisterOrImmediateZero(right_node);
    opcode = reverse_opcode;
    input_count++;
  } else {
    inputs[input_count++] = g.UseRegister(left_node);
    inputs[input_count++] = g.UseOperand(right_node, opcode);
  }

  if (cont->IsDeoptimize()) {
    // If we can deoptimize as a result of the binop, we need to make sure that
    // the deopt inputs are not overwritten by the binop result. One way
    // to achieve that is to declare the output register as same-as-first.
    outputs[output_count++] = g.DefineSameAsFirst(node);
  } else {
    outputs[output_count++] = g.DefineAsRegister(node);
  }

  DCHECK_NE(0u, input_count);
  DCHECK_EQ(1u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<TurboshaftAdapter>* selector,
                       TurboshaftAdapter::node_t node, InstructionCode opcode,
                       bool has_reverse_opcode,
                       InstructionCode reverse_opcode) {
  FlagsContinuationT<TurboshaftAdapter> cont;
  VisitBinop<Adapter, Matcher>(selector, node, opcode, has_reverse_opcode,
                               reverse_opcode, &cont);
}

template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<Adapter>* selector, Node* node,
                       InstructionCode opcode, bool has_reverse_opcode,
                       InstructionCode reverse_opcode,
                       FlagsContinuationT<Adapter>* cont) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand inputs[2];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  if (TryMatchImmediate(selector, &opcode, m.right().node(), &input_count,
                        &inputs[1])) {
    inputs[0] = g.UseRegisterOrImmediateZero(m.left().node());
    input_count++;
  } else if (has_reverse_opcode &&
             TryMatchImmediate(selector, &reverse_opcode, m.left().node(),
                               &input_count, &inputs[1])) {
    inputs[0] = g.UseRegisterOrImmediateZero(m.right().node());
    opcode = reverse_opcode;
    input_count++;
  } else {
    inputs[input_count++] = g.UseRegister(m.left().node());
    inputs[input_count++] = g.UseOperand(m.right().node(), opcode);
  }

  if (cont->IsDeoptimize()) {
    // If we can deoptimize as a result of the binop, we need to make sure that
    // the deopt inputs are not overwritten by the binop result. One way
    // to achieve that is to declare the output register as same-as-first.
    outputs[output_count++] = g.DefineSameAsFirst(node);
  } else {
    outputs[output_count++] = g.DefineAsRegister(node);
  }

  DCHECK_NE(0u, input_count);
  DCHECK_EQ(1u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<Adapter>* selector, Node* node,
                       InstructionCode opcode, bool has_reverse_opcode,
                       InstructionCode reverse_opcode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop<Adapter, Matcher>(selector, node, opcode, has_reverse_opcode,
                               reverse_opcode, &cont);
}

template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, InstructionCode opcode,
                       FlagsContinuationT<Adapter>* cont) {
  VisitBinop<Adapter, Matcher>(selector, node, opcode, false, kArchNop, cont);
}

template <typename Adapter, typename Matcher>
static void VisitBinop(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, InstructionCode opcode) {
  VisitBinop<Adapter, Matcher>(selector, node, opcode, false, kArchNop);
}

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
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kArchAbortCSADcheck, g.NoOutput(),
         g.UseFixed(this->input_at(node, 0), a0));
  }
}

template <typename Adapter>
void EmitS128Load(InstructionSelectorT<Adapter>* selector, Node* node,
                  InstructionCode opcode, VSew sew, Vlmul lmul);

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadTransform(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    LoadTransformParameters params = LoadTransformParametersOf(node->op());
    bool is_protected = (params.kind == MemoryAccessKind::kProtected);
    InstructionCode opcode = kArchNop;
    switch (params.transformation) {
      case LoadTransformation::kS128Load8Splat:
        opcode = kRiscvS128LoadSplat;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E8, m1);
        break;
      case LoadTransformation::kS128Load16Splat:
        opcode = kRiscvS128LoadSplat;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E16, m1);
        break;
      case LoadTransformation::kS128Load32Splat:
        opcode = kRiscvS128LoadSplat;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E32, m1);
        break;
      case LoadTransformation::kS128Load64Splat:
        opcode = kRiscvS128LoadSplat;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E64, m1);
        break;
      case LoadTransformation::kS128Load8x8S:
        opcode = kRiscvS128Load64ExtendS;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E16, m1);
        break;
      case LoadTransformation::kS128Load8x8U:
        opcode = kRiscvS128Load64ExtendU;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E16, m1);
        break;
      case LoadTransformation::kS128Load16x4S:
        opcode = kRiscvS128Load64ExtendS;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E32, m1);
        break;
      case LoadTransformation::kS128Load16x4U:
        opcode = kRiscvS128Load64ExtendU;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E32, m1);
        break;
      case LoadTransformation::kS128Load32x2S:
        opcode = kRiscvS128Load64ExtendS;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E64, m1);
        break;
      case LoadTransformation::kS128Load32x2U:
        opcode = kRiscvS128Load64ExtendU;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E64, m1);
        break;
      case LoadTransformation::kS128Load32Zero:
        opcode = kRiscvS128Load32Zero;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E32, m1);
        break;
      case LoadTransformation::kS128Load64Zero:
        opcode = kRiscvS128Load64Zero;
        if (is_protected) {
          opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
        }
        EmitS128Load(this, node, opcode, E64, m1);
        break;
      default:
        UNIMPLEMENTED();
    }
  }
}

// Shared routine for multiple compare operations.
template <typename Adapter>
static void VisitCompare(InstructionSelectorT<Adapter>* selector,
                         InstructionCode opcode, InstructionOperand left,
                         InstructionOperand right,
                         FlagsContinuationT<Adapter>* cont) {
#ifdef V8_COMPRESS_POINTERS
  if (opcode == kRiscvCmp32) {
    RiscvOperandGeneratorT<Adapter> g(selector);
    InstructionOperand inputs[] = {left, right};
    if (right.IsImmediate()) {
      InstructionOperand temps[1] = {g.TempRegister()};
      selector->EmitWithContinuation(opcode, 0, nullptr, arraysize(inputs),
                                     inputs, arraysize(temps), temps, cont);
    } else {
      InstructionOperand temps[2] = {g.TempRegister(), g.TempRegister()};
      selector->EmitWithContinuation(opcode, 0, nullptr, arraysize(inputs),
                                     inputs, arraysize(temps), temps, cont);
    }
    return;
  }
#endif
  selector->EmitWithContinuation(opcode, left, right, cont);
}

// Shared routine for multiple compare operations.
template <typename Adapter>
static void VisitWordCompareZero(InstructionSelectorT<Adapter>* selector,
                                 InstructionOperand value,
                                 FlagsContinuationT<Adapter>* cont) {
  selector->EmitWithContinuation(kRiscvCmpZero, value, cont);
}

// Shared routine for multiple float32 compare operations.
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& op = selector->Get(node).template Cast<ComparisonOp>();
    OpIndex left = op.left();
    OpIndex right = op.right();
    if (selector->MatchZero(right)) {
      VisitCompare(selector, kRiscvCmpS, g.UseRegister(left),
                   g.UseImmediate(right), cont);
    } else if (selector->MatchZero(left)) {
      cont->Commute();
      VisitCompare(selector, kRiscvCmpS, g.UseRegister(right),
                   g.UseImmediate(left), cont);
    } else {
      VisitCompare(selector, kRiscvCmpS, g.UseRegister(left),
                   g.UseRegister(right), cont);
    }
  } else {
    Float32BinopMatcher m(node);
    InstructionOperand lhs, rhs;

    lhs = m.left().IsZero() ? g.UseImmediate(m.left().node())
                            : g.UseRegister(m.left().node());
    rhs = m.right().IsZero() ? g.UseImmediate(m.right().node())
                             : g.UseRegister(m.right().node());
    VisitCompare(selector, kRiscvCmpS, lhs, rhs, cont);
  }
}

// Shared routine for multiple float64 compare operations.
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  if constexpr (Adapter::IsTurboshaft) {
    RiscvOperandGeneratorT<Adapter> g(selector);
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& compare = selector->Get(node);
    DCHECK(compare.Is<ComparisonOp>());
    OpIndex lhs = compare.input(0);
    OpIndex rhs = compare.input(1);
    if (selector->MatchZero(rhs)) {
      VisitCompare(selector, kRiscvCmpD, g.UseRegister(lhs),
                   g.UseImmediate(rhs), cont);
    } else if (selector->MatchZero(lhs)) {
      VisitCompare(selector, kRiscvCmpD, g.UseImmediate(lhs),
                   g.UseRegister(rhs), cont);
    } else {
      VisitCompare(selector, kRiscvCmpD, g.UseRegister(lhs), g.UseRegister(rhs),
                   cont);
    }
  } else {
    RiscvOperandGeneratorT<Adapter> g(selector);
    Float64BinopMatcher m(node);
    InstructionOperand lhs, rhs;

    lhs = m.left().IsZero() ? g.UseImmediate(m.left().node())
                            : g.UseRegister(m.left().node());
    rhs = m.right().IsZero() ? g.UseImmediate(m.right().node())
                             : g.UseRegister(m.right().node());
    VisitCompare(selector, kRiscvCmpD, lhs, rhs, cont);
  }
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont, bool commutative) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  // If one of the two inputs is an immediate, make sure it's on the right.
  if (!g.CanBeImmediate(right, opcode) && g.CanBeImmediate(left, opcode)) {
    cont->Commute();
    std::swap(left, right);
  }
  // Match immediates on right side of comparison.
  if (g.CanBeImmediate(right, opcode)) {
#if V8_TARGET_ARCH_RISCV64
    if (opcode == kRiscvTst64 || opcode == kRiscvTst32) {
#elif V8_TARGET_ARCH_RISCV32
    if (opcode == kRiscvTst32) {
#endif
      if constexpr (Adapter::IsTurbofan) {
        if (selector->opcode(left) ==
            Adapter::opcode_t::kTruncateInt64ToInt32) {
          VisitCompare(selector, opcode,
                       g.UseRegister(selector->input_at(left, 0)),
                       g.UseImmediate(right), cont);
        } else {
          VisitCompare(selector, opcode, g.UseRegister(left),
                       g.UseImmediate(right), cont);
        }
      } else {
        VisitCompare(selector, opcode, g.UseRegister(left),
                     g.UseImmediate(right), cont);
      }
    } else {
      switch (cont->condition()) {
        case kEqual:
        case kNotEqual:
          if (cont->IsSet()) {
            VisitCompare(selector, opcode, g.UseRegister(left),
                         g.UseImmediate(right), cont);
          } else {
            if (g.CanBeZero(right)) {
              VisitWordCompareZero(selector, g.UseRegisterOrImmediateZero(left),
                                   cont);
            } else {
              VisitCompare(selector, opcode, g.UseRegister(left),
                           g.UseRegister(right), cont);
            }
          }
          break;
        case kSignedLessThan:
        case kSignedGreaterThanOrEqual:
        case kUnsignedLessThan:
        case kUnsignedGreaterThanOrEqual: {
          if (g.CanBeZero(right)) {
            VisitWordCompareZero(selector, g.UseRegisterOrImmediateZero(left),
                                 cont);
          } else {
            VisitCompare(selector, opcode, g.UseRegister(left),
                         g.UseImmediate(right), cont);
          }
        } break;
        default:
          if (g.CanBeZero(right)) {
            VisitWordCompareZero(selector, g.UseRegisterOrImmediateZero(left),
                                 cont);
          } else {
            VisitCompare(selector, opcode, g.UseRegister(left),
                         g.UseRegister(right), cont);
          }
      }
    }
  } else {
    VisitCompare(selector, opcode, g.UseRegister(left), g.UseRegister(right),
                 cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand value_operand = g.UseRegister(this->input_at(node, 0));

    // Emit either ArchTableSwitch or ArchBinarySearchSwitch.
    if (enable_switch_jump_table_ ==
        InstructionSelector::kEnableSwitchJumpTable) {
      static const size_t kMaxTableSwitchValueRange = 2 << 16;
      size_t table_space_cost = 10 + 2 * sw.value_range();
      size_t table_time_cost = 3;
      size_t lookup_space_cost = 2 + 2 * sw.case_count();
      size_t lookup_time_cost = sw.case_count();
      if (sw.case_count() > 0 &&
          table_space_cost + 3 * table_time_cost <=
              lookup_space_cost + 3 * lookup_time_cost &&
          sw.min_value() > std::numeric_limits<int32_t>::min() &&
          sw.value_range() <= kMaxTableSwitchValueRange) {
        InstructionOperand index_operand = value_operand;
        if (sw.min_value()) {
          index_operand = g.TempRegister();
          Emit(kRiscvSub32, index_operand, value_operand,
               g.TempImmediate(sw.min_value()));
        }
        // Generate a table lookup.
        return EmitTableSwitch(sw, index_operand);
      }
    }

    // Generate a tree of conditional jumps.
    return EmitBinarySearchSwitch(sw, value_operand);
}

template <typename Adapter>
void EmitWordCompareZero(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t value,
                         FlagsContinuationT<Adapter>* cont) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  selector->EmitWithContinuation(kRiscvCmpZero,
                                 g.UseRegisterOrImmediateZero(value), cont);
}

#ifdef V8_TARGET_ARCH_RISCV64
template <typename Adapter>
void EmitWord32CompareZero(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t value,
                         FlagsContinuationT<Adapter>* cont) {
  RiscvOperandGeneratorT<Adapter> g(selector);
  InstructionOperand inputs[] = {g.UseRegisterOrImmediateZero(value)};
  InstructionOperand temps[] = {g.TempRegister()};
  selector->EmitWithContinuation(kRiscvCmpZero32, 0, nullptr, arraysize(inputs),
                                 inputs, arraysize(temps), temps, cont);
}
#endif


template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Equal(node_t node) {
  FlagsContinuationT<Adapter> cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThan(node_t node) {
  FlagsContinuationT<Adapter> cont =
      FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThanOrEqual(node_t node) {
  FlagsContinuationT<Adapter> cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Equal(node_t node) {
  FlagsContinuationT<Adapter> cont = FlagsContinuation::ForSet(kEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThan(node_t node) {
  FlagsContinuationT<Adapter> cont =
      FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThanOrEqual(node_t node) {
  FlagsContinuationT<Adapter> cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64ExtractLowWord32(node_t node) {
    VisitRR(this, kRiscvFloat64ExtractLowWord32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64ExtractHighWord32(node_t node) {
    VisitRR(this, kRiscvFloat64ExtractHighWord32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64SilenceNaN(node_t node) {
    VisitRR(this, kRiscvFloat64SilenceNaN, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  RiscvOperandGeneratorT<TurboshaftAdapter> g(this);
  const auto& bitcast =
      this->Cast<turboshaft::BitcastWord32PairToFloat64Op>(node);
  node_t hi = bitcast.high_word32();
  node_t lo = bitcast.low_word32();
  // TODO(nicohartmann@): We could try to emit a better sequence here.
  InstructionOperand zero = sequence()->AddImmediate(Constant(0.0));
  InstructionOperand temp = g.TempDoubleRegister();
  Emit(kRiscvFloat64InsertHighWord32, temp, zero, g.Use(hi));
  Emit(kRiscvFloat64InsertLowWord32, g.DefineSameAsFirst(node), temp,
       g.Use(lo));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertLowWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* left = this->input_at(node, 0);
    Node* right = this->input_at(node, 1);
    Emit(kRiscvFloat64InsertLowWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertHighWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Node* left = this->input_at(node, 0);
    Node* right = this->input_at(node, 1);
    Emit(kRiscvFloat64InsertHighWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvSync, g.NoOutput());
  }
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsTailCallAddressImmediate() {
  return false;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
  RiscvOperandGeneratorT<Adapter> g(this);

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
      Emit(kRiscvPeek, g.DefineAsRegister(output.node),
           g.UseImmediate(reverse_slot));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveParamToFPR(node_t node, int index) {
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitMoveFPRToParam(
    InstructionOperand* op, LinkageLocation location) {}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Abs(node_t node) {
    VisitRR(this, kRiscvAbsS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Abs(node_t node) {
    VisitRR(this, kRiscvAbsD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sqrt(node_t node) {
  VisitRR(this, kRiscvSqrtS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sqrt(node_t node) {
  VisitRR(this, kRiscvSqrtD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32RoundDown(node_t node) {
  VisitRR(this, kRiscvFloat32RoundDown, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Add(node_t node) {
    VisitRRR(this, kRiscvAddS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Add(node_t node) {
    VisitRRR(this, kRiscvAddD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sub(node_t node) {
    VisitRRR(this, kRiscvSubS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sub(node_t node) {
  VisitRRR(this, kRiscvSubD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Mul(node_t node) {
    VisitRRR(this, kRiscvMulS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mul(node_t node) {
    VisitRRR(this, kRiscvMulD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Div(node_t node) {
    VisitRRR(this, kRiscvDivS, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Div(node_t node) {
  VisitRRR(this, kRiscvDivD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvModD, g.DefineAsFixed(node, fa0),
         g.UseFixed(this->input_at(node, 0), fa0),
         g.UseFixed(this->input_at(node, 1), fa1))
        ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Max(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvFloat32Max, node);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvFloat32Max, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Max(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvFloat64Max, node);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvFloat64Max, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Min(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvFloat32Min, node);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvFloat32Min, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Min(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    VisitRRR(this, kRiscvFloat64Min, node);
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvFloat64Min, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToWord32(node_t node) {
  VisitRR(this, kArchTruncateDoubleToI, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundFloat64ToInt32(node_t node) {
  VisitRR(this, kRiscvTruncWD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToFloat32(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    node_t value = this->input_at(node, 0);
    // Match TruncateFloat64ToFloat32(ChangeInt32ToFloat64) to corresponding
    // instruction.
    if constexpr (Adapter::IsTurboshaft) {
      using Rep = turboshaft::RegisterRepresentation;
      if (CanCover(node, value)) {
        const turboshaft::Operation& op = this->Get(value);
        if (op.Is<turboshaft::ChangeOp>()) {
          const turboshaft::ChangeOp& change = op.Cast<turboshaft::ChangeOp>();
          if (change.kind == turboshaft::ChangeOp::Kind::kSignedToFloat) {
            if (change.from == Rep::Word32() && change.to == Rep::Float64()) {
              Emit(kRiscvCvtSW, g.DefineAsRegister(node),
                   g.UseRegister(this->input_at(value, 0)));
              return;
            }
          }
        }
      }
    } else {
      if (CanCover(node, value) &&
          this->opcode(value) == IrOpcode::kChangeInt32ToFloat64) {
        Emit(kRiscvCvtSW, g.DefineAsRegister(node),
             g.UseRegister(this->input_at(value, 0)));
        return;
      }
    }
    VisitRR(this, kRiscvCvtSD, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shl(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // todo(RISCV): Optimize it
    VisitRRO(this, kRiscvShl32, node);
  } else {
    Int32BinopMatcher m(node);
    if (m.left().IsWord32And() && CanCover(node, m.left().node()) &&
        m.right().IsInRange(1, 31)) {
      RiscvOperandGeneratorT<Adapter> g(this);
      Int32BinopMatcher mleft(m.left().node());
      // Match Word32Shl(Word32And(x, mask), imm) to Shl where the mask is
      // contiguous, and the shift immediate non-zero.
      if (mleft.right().HasResolvedValue()) {
        uint32_t mask = mleft.right().ResolvedValue();
        uint32_t mask_width = base::bits::CountPopulation(mask);
        uint32_t mask_msb = base::bits::CountLeadingZeros32(mask);
        if ((mask_width != 0) && (mask_msb + mask_width == 32)) {
          uint32_t shift = m.right().ResolvedValue();
          DCHECK_EQ(0u, base::bits::CountTrailingZeros32(mask));
          DCHECK_NE(0u, shift);
          if ((shift + mask_width) >= 32) {
            // If the mask is contiguous and reaches or extends beyond the top
            // bit, only the shift is needed.
            Emit(kRiscvShl32, g.DefineAsRegister(node),
                 g.UseRegister(mleft.left().node()),
                 g.UseImmediate(m.right().node()));
            return;
          }
        }
      }
    }
    VisitRRO(this, kRiscvShl32, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shr(node_t node) {
  VisitRRO(this, kRiscvShr32, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Sar(
    turboshaft::OpIndex node) {
  // todo(RISCV): Optimize it
  VisitRRO(this, kRiscvSar32, node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Sar(Node* node) {
  Int32BinopMatcher m(node);
  if (CanCover(node, m.left().node())) {
    RiscvOperandGeneratorT<TurbofanAdapter> g(this);
    if (m.left().IsWord32Shl()) {
      Int32BinopMatcher mleft(m.left().node());
      if (m.right().HasResolvedValue() && mleft.right().HasResolvedValue()) {
        uint32_t sar = m.right().ResolvedValue();
        uint32_t shl = mleft.right().ResolvedValue();
        if ((sar == shl) && (sar == 16)) {
          Emit(kRiscvSignExtendShort, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()));
          return;
        } else if ((sar == shl) && (sar == 24)) {
          Emit(kRiscvSignExtendByte, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()));
          return;
        } else if ((sar == shl) && (sar == 32)) {
          Emit(kRiscvShl32, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()), g.TempImmediate(0));
          return;
        }
      }
    }
  }
  VisitRRO(this, kRiscvSar32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8S(
    node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand src1 = g.TempSimd128Register();
    InstructionOperand src2 = g.TempSimd128Register();
    InstructionOperand src = g.UseUniqueRegister(this->input_at(node, 0));
    Emit(kRiscvVrgather, src1, src, g.UseImmediate64(0x0006000400020000),
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVrgather, src2, src, g.UseImmediate64(0x0007000500030001),
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVwaddVv, g.DefineAsRegister(node), src1, src2,
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(mf2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8U(
    node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand src1 = g.TempSimd128Register();
    InstructionOperand src2 = g.TempSimd128Register();
    InstructionOperand src = g.UseUniqueRegister(this->input_at(node, 0));
    Emit(kRiscvVrgather, src1, src, g.UseImmediate64(0x0006000400020000),
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVrgather, src2, src, g.UseImmediate64(0x0007000500030001),
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVwadduVv, g.DefineAsRegister(node), src1, src2,
         g.UseImmediate(int8_t(E16)), g.UseImmediate(int8_t(mf2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16S(
    node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand src1 = g.TempSimd128Register();
    InstructionOperand src2 = g.TempSimd128Register();
    InstructionOperand src = g.UseUniqueRegister(this->input_at(node, 0));
    Emit(kRiscvVrgather, src1, src, g.UseImmediate64(0x0E0C0A0806040200),
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVrgather, src2, src, g.UseImmediate64(0x0F0D0B0907050301),
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVwaddVv, g.DefineAsRegister(node), src1, src2,
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(mf2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16U(
    node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand src1 = g.TempSimd128Register();
    InstructionOperand src2 = g.TempSimd128Register();
    InstructionOperand src = g.UseUniqueRegister(this->input_at(node, 0));
    Emit(kRiscvVrgather, src1, src, g.UseImmediate64(0x0E0C0A0806040200),
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVrgather, src2, src, g.UseImmediate64(0x0F0D0B0907050301),
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(m1)));
    Emit(kRiscvVwadduVv, g.DefineAsRegister(node), src1, src2,
         g.UseImmediate(int8_t(E8)), g.UseImmediate(int8_t(mf2)));
  }
}

#define SIMD_INT_TYPE_LIST(V) \
  V(I64x2, E64, m1)           \
  V(I32x4, E32, m1)           \
  V(I16x8, E16, m1)           \
  V(I8x16, E8, m1)

#define SIMD_TYPE_LIST(V) \
  V(F32x4)                \
  V(I64x2)                \
  V(I32x4)                \
  V(I16x8)                \
  V(I8x16)

#define SIMD_UNOP_LIST2(V)                 \
  V(F32x4Splat, kRiscvVfmvVf, E32, m1)     \
  V(I8x16Neg, kRiscvVnegVv, E8, m1)        \
  V(I16x8Neg, kRiscvVnegVv, E16, m1)       \
  V(I32x4Neg, kRiscvVnegVv, E32, m1)       \
  V(I64x2Neg, kRiscvVnegVv, E64, m1)       \
  V(I8x16Splat, kRiscvVmv, E8, m1)         \
  V(I16x8Splat, kRiscvVmv, E16, m1)        \
  V(I32x4Splat, kRiscvVmv, E32, m1)        \
  V(I64x2Splat, kRiscvVmv, E64, m1)        \
  V(F32x4Neg, kRiscvVfnegVv, E32, m1)      \
  V(F64x2Neg, kRiscvVfnegVv, E64, m1)      \
  V(F64x2Splat, kRiscvVfmvVf, E64, m1)     \
  V(I32x4AllTrue, kRiscvVAllTrue, E32, m1) \
  V(I16x8AllTrue, kRiscvVAllTrue, E16, m1) \
  V(I8x16AllTrue, kRiscvVAllTrue, E8, m1)  \
  V(I64x2AllTrue, kRiscvVAllTrue, E64, m1) \
  V(I64x2Abs, kRiscvVAbs, E64, m1)         \
  V(I32x4Abs, kRiscvVAbs, E32, m1)         \
  V(I16x8Abs, kRiscvVAbs, E16, m1)         \
  V(I8x16Abs, kRiscvVAbs, E8, m1)

#define SIMD_UNOP_LIST(V)                                       \
  V(F64x2Abs, kRiscvF64x2Abs)                                   \
  V(F64x2Sqrt, kRiscvF64x2Sqrt)                                 \
  V(F64x2ConvertLowI32x4S, kRiscvF64x2ConvertLowI32x4S)         \
  V(F64x2ConvertLowI32x4U, kRiscvF64x2ConvertLowI32x4U)         \
  V(F64x2PromoteLowF32x4, kRiscvF64x2PromoteLowF32x4)           \
  V(F64x2Ceil, kRiscvF64x2Ceil)                                 \
  V(F64x2Floor, kRiscvF64x2Floor)                               \
  V(F64x2Trunc, kRiscvF64x2Trunc)                               \
  V(F64x2NearestInt, kRiscvF64x2NearestInt)                     \
  V(F32x4SConvertI32x4, kRiscvF32x4SConvertI32x4)               \
  V(F32x4UConvertI32x4, kRiscvF32x4UConvertI32x4)               \
  V(F32x4Abs, kRiscvF32x4Abs)                                   \
  V(F32x4Sqrt, kRiscvF32x4Sqrt)                                 \
  V(F32x4DemoteF64x2Zero, kRiscvF32x4DemoteF64x2Zero)           \
  V(F32x4Ceil, kRiscvF32x4Ceil)                                 \
  V(F32x4Floor, kRiscvF32x4Floor)                               \
  V(F32x4Trunc, kRiscvF32x4Trunc)                               \
  V(F32x4NearestInt, kRiscvF32x4NearestInt)                     \
  V(I32x4RelaxedTruncF32x4S, kRiscvI32x4SConvertF32x4)          \
  V(I32x4RelaxedTruncF32x4U, kRiscvI32x4UConvertF32x4)          \
  V(I32x4RelaxedTruncF64x2SZero, kRiscvI32x4TruncSatF64x2SZero) \
  V(I32x4RelaxedTruncF64x2UZero, kRiscvI32x4TruncSatF64x2UZero) \
  V(I64x2SConvertI32x4Low, kRiscvI64x2SConvertI32x4Low)         \
  V(I64x2SConvertI32x4High, kRiscvI64x2SConvertI32x4High)       \
  V(I64x2UConvertI32x4Low, kRiscvI64x2UConvertI32x4Low)         \
  V(I64x2UConvertI32x4High, kRiscvI64x2UConvertI32x4High)       \
  V(I32x4SConvertF32x4, kRiscvI32x4SConvertF32x4)               \
  V(I32x4UConvertF32x4, kRiscvI32x4UConvertF32x4)               \
  V(I32x4TruncSatF64x2SZero, kRiscvI32x4TruncSatF64x2SZero)     \
  V(I32x4TruncSatF64x2UZero, kRiscvI32x4TruncSatF64x2UZero)     \
  V(I8x16Popcnt, kRiscvI8x16Popcnt)                             \
  V(S128Not, kRiscvVnot)                                        \
  V(V128AnyTrue, kRiscvV128AnyTrue)

#define SIMD_SHIFT_OP_LIST(V) \
  V(I64x2Shl)                 \
  V(I64x2ShrS)                \
  V(I64x2ShrU)                \
  V(I32x4Shl)                 \
  V(I32x4ShrS)                \
  V(I32x4ShrU)                \
  V(I16x8Shl)                 \
  V(I16x8ShrS)                \
  V(I16x8ShrU)                \
  V(I8x16Shl)                 \
  V(I8x16ShrS)                \
  V(I8x16ShrU)

#define SIMD_BINOP_LIST(V)                    \
  V(I64x2Add, kRiscvVaddVv, E64, m1)          \
  V(I32x4Add, kRiscvVaddVv, E32, m1)          \
  V(I16x8Add, kRiscvVaddVv, E16, m1)          \
  V(I8x16Add, kRiscvVaddVv, E8, m1)           \
  V(I64x2Sub, kRiscvVsubVv, E64, m1)          \
  V(I32x4Sub, kRiscvVsubVv, E32, m1)          \
  V(I16x8Sub, kRiscvVsubVv, E16, m1)          \
  V(I8x16Sub, kRiscvVsubVv, E8, m1)           \
  V(I32x4MaxU, kRiscvVmaxuVv, E32, m1)        \
  V(I16x8MaxU, kRiscvVmaxuVv, E16, m1)        \
  V(I8x16MaxU, kRiscvVmaxuVv, E8, m1)         \
  V(I32x4MaxS, kRiscvVmax, E32, m1)           \
  V(I16x8MaxS, kRiscvVmax, E16, m1)           \
  V(I8x16MaxS, kRiscvVmax, E8, m1)            \
  V(I32x4MinS, kRiscvVminsVv, E32, m1)        \
  V(I16x8MinS, kRiscvVminsVv, E16, m1)        \
  V(I8x16MinS, kRiscvVminsVv, E8, m1)         \
  V(I32x4MinU, kRiscvVminuVv, E32, m1)        \
  V(I16x8MinU, kRiscvVminuVv, E16, m1)        \
  V(I8x16MinU, kRiscvVminuVv, E8, m1)         \
  V(I64x2Mul, kRiscvVmulVv, E64, m1)          \
  V(I32x4Mul, kRiscvVmulVv, E32, m1)          \
  V(I16x8Mul, kRiscvVmulVv, E16, m1)          \
  V(I64x2GtS, kRiscvVgtsVv, E64, m1)          \
  V(I32x4GtS, kRiscvVgtsVv, E32, m1)          \
  V(I16x8GtS, kRiscvVgtsVv, E16, m1)          \
  V(I8x16GtS, kRiscvVgtsVv, E8, m1)           \
  V(I64x2GeS, kRiscvVgesVv, E64, m1)          \
  V(I32x4GeS, kRiscvVgesVv, E32, m1)          \
  V(I16x8GeS, kRiscvVgesVv, E16, m1)          \
  V(I8x16GeS, kRiscvVgesVv, E8, m1)           \
  V(I32x4GeU, kRiscvVgeuVv, E32, m1)          \
  V(I16x8GeU, kRiscvVgeuVv, E16, m1)          \
  V(I8x16GeU, kRiscvVgeuVv, E8, m1)           \
  V(I32x4GtU, kRiscvVgtuVv, E32, m1)          \
  V(I16x8GtU, kRiscvVgtuVv, E16, m1)          \
  V(I8x16GtU, kRiscvVgtuVv, E8, m1)           \
  V(I64x2Eq, kRiscvVeqVv, E64, m1)            \
  V(I32x4Eq, kRiscvVeqVv, E32, m1)            \
  V(I16x8Eq, kRiscvVeqVv, E16, m1)            \
  V(I8x16Eq, kRiscvVeqVv, E8, m1)             \
  V(I64x2Ne, kRiscvVneVv, E64, m1)            \
  V(I32x4Ne, kRiscvVneVv, E32, m1)            \
  V(I16x8Ne, kRiscvVneVv, E16, m1)            \
  V(I8x16Ne, kRiscvVneVv, E8, m1)             \
  V(I16x8AddSatS, kRiscvVaddSatSVv, E16, m1)  \
  V(I8x16AddSatS, kRiscvVaddSatSVv, E8, m1)   \
  V(I16x8AddSatU, kRiscvVaddSatUVv, E16, m1)  \
  V(I8x16AddSatU, kRiscvVaddSatUVv, E8, m1)   \
  V(I16x8SubSatS, kRiscvVsubSatSVv, E16, m1)  \
  V(I8x16SubSatS, kRiscvVsubSatSVv, E8, m1)   \
  V(I16x8SubSatU, kRiscvVsubSatUVv, E16, m1)  \
  V(I8x16SubSatU, kRiscvVsubSatUVv, E8, m1)   \
  V(F64x2Add, kRiscvVfaddVv, E64, m1)         \
  V(F32x4Add, kRiscvVfaddVv, E32, m1)         \
  V(F64x2Sub, kRiscvVfsubVv, E64, m1)         \
  V(F32x4Sub, kRiscvVfsubVv, E32, m1)         \
  V(F64x2Mul, kRiscvVfmulVv, E64, m1)         \
  V(F32x4Mul, kRiscvVfmulVv, E32, m1)         \
  V(F64x2Div, kRiscvVfdivVv, E64, m1)         \
  V(F32x4Div, kRiscvVfdivVv, E32, m1)         \
  V(S128And, kRiscvVandVv, E8, m1)            \
  V(S128Or, kRiscvVorVv, E8, m1)              \
  V(S128Xor, kRiscvVxorVv, E8, m1)            \
  V(I16x8Q15MulRSatS, kRiscvVsmulVv, E16, m1) \
  V(I16x8RelaxedQ15MulRS, kRiscvVsmulVv, E16, m1)

#define UNIMPLEMENTED_SIMD_FP16_OP_LIST(V) \
  V(F16x8Splat)                            \
  V(F16x8ExtractLane)                      \
  V(F16x8ReplaceLane)                      \
  V(F16x8Abs)                              \
  V(F16x8Neg)                              \
  V(F16x8Sqrt)                             \
  V(F16x8Floor)                            \
  V(F16x8Ceil)                             \
  V(F16x8Trunc)                            \
  V(F16x8NearestInt)                       \
  V(F16x8Add)                              \
  V(F16x8Sub)                              \
  V(F16x8Mul)                              \
  V(F16x8Div)                              \
  V(F16x8Min)                              \
  V(F16x8Max)                              \
  V(F16x8Pmin)                             \
  V(F16x8Pmax)                             \
  V(F16x8Eq)                               \
  V(F16x8Ne)                               \
  V(F16x8Lt)                               \
  V(F16x8Le)                               \
  V(F16x8SConvertI16x8)                    \
  V(F16x8UConvertI16x8)                    \
  V(I16x8SConvertF16x8)                    \
  V(I16x8UConvertF16x8)                    \
  V(F16x8DemoteF32x4Zero)                  \
  V(F16x8DemoteF64x2Zero)                  \
  V(F32x4PromoteLowF16x8)                  \
  V(F16x8Qfma)                             \
  V(F16x8Qfms)

#define SIMD_VISIT_UNIMPL_FP16_OP(Name)                          \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    UNIMPLEMENTED();                                             \
  }
UNIMPLEMENTED_SIMD_FP16_OP_LIST(SIMD_VISIT_UNIMPL_FP16_OP)
#undef SIMD_VISIT_UNIMPL_FP16_OP
#undef UNIMPLEMENTED_SIMD_FP16_OP_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128AndNot(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVnotVv, temp1, g.UseRegister(this->input_at(node, 1)),
               g.UseImmediate(E8), g.UseImmediate(m1));
    this->Emit(kRiscvVandVv, g.DefineAsRegister(node),
               g.UseRegister(this->input_at(node, 0)), temp1,
               g.UseImmediate(E8), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    static const int kUint32Immediates = kSimd128Size / sizeof(uint32_t);
    uint32_t val[kUint32Immediates];
    memcpy(val, S128ImmediateParameterOf(node->op()).data(), kSimd128Size);
    // If all bytes are zeros or ones, avoid emitting code for generic constants
    bool all_zeros = !(val[0] || val[1] || val[2] || val[3]);
    bool all_ones = val[0] == UINT32_MAX && val[1] == UINT32_MAX &&
                    val[2] == UINT32_MAX && val[3] == UINT32_MAX;
    InstructionOperand dst = g.DefineAsRegister(node);
    if (all_zeros) {
      Emit(kRiscvS128Zero, dst);
    } else if (all_ones) {
      Emit(kRiscvS128AllOnes, dst);
    } else {
      Emit(kRiscvS128Const, dst, g.UseImmediate(val[0]), g.UseImmediate(val[1]),
           g.UseImmediate(val[2]), g.UseImmediate(val[3]));
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvS128Zero, g.DefineAsRegister(node));
  }
}

#define SIMD_VISIT_EXTRACT_LANE(Type, Sign)                           \
  template <typename Adapter>                                         \
  void InstructionSelectorT<Adapter>::Visit##Type##ExtractLane##Sign( \
      node_t node) {                                                  \
      VisitRRI(this, kRiscv##Type##ExtractLane##Sign, node);          \
  }
SIMD_VISIT_EXTRACT_LANE(F64x2, )
SIMD_VISIT_EXTRACT_LANE(F32x4, )
SIMD_VISIT_EXTRACT_LANE(I32x4, )
SIMD_VISIT_EXTRACT_LANE(I64x2, )
SIMD_VISIT_EXTRACT_LANE(I16x8, U)
SIMD_VISIT_EXTRACT_LANE(I16x8, S)
SIMD_VISIT_EXTRACT_LANE(I8x16, U)
SIMD_VISIT_EXTRACT_LANE(I8x16, S)
#undef SIMD_VISIT_EXTRACT_LANE

#define SIMD_VISIT_REPLACE_LANE(Type)                                         \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##Type##ReplaceLane(node_t node) { \
      VisitRRIR(this, kRiscv##Type##ReplaceLane, node);                       \
  }
SIMD_TYPE_LIST(SIMD_VISIT_REPLACE_LANE)
SIMD_VISIT_REPLACE_LANE(F64x2)
#undef SIMD_VISIT_REPLACE_LANE

#define SIMD_VISIT_UNOP(Name, instruction)                       \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
      VisitRR(this, instruction, node);                          \
  }
SIMD_UNOP_LIST(SIMD_VISIT_UNOP)
#undef SIMD_VISIT_UNOP

#define SIMD_VISIT_SHIFT_OP(Name)                                \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
      VisitSimdShift(this, kRiscv##Name, node);                  \
  }
SIMD_SHIFT_OP_LIST(SIMD_VISIT_SHIFT_OP)
#undef SIMD_VISIT_SHIFT_OP

#define SIMD_VISIT_BINOP_RVV(Name, instruction, VSEW, LMUL)                    \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {               \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      this->Emit(instruction, g.DefineAsRegister(node),                        \
                 g.UseRegister(this->input_at(node, 0)),                       \
                 g.UseRegister(this->input_at(node, 1)), g.UseImmediate(VSEW), \
                 g.UseImmediate(LMUL));                                        \
  }
SIMD_BINOP_LIST(SIMD_VISIT_BINOP_RVV)
#undef SIMD_VISIT_BINOP_RVV

#define SIMD_VISIT_UNOP2(Name, instruction, VSEW, LMUL)                        \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {               \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      this->Emit(instruction, g.DefineAsRegister(node),                        \
                 g.UseRegister(this->input_at(node, 0)), g.UseImmediate(VSEW), \
                 g.UseImmediate(LMUL));                                        \
  }
SIMD_UNOP_LIST2(SIMD_VISIT_UNOP2)
#undef SIMD_VISIT_UNOP2

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitRRRR(this, kRiscvS128Select, node);
  }
}

#define SIMD_VISIT_SELECT_LANE(Name)                             \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    if constexpr (Adapter::IsTurboshaft) {                       \
      UNIMPLEMENTED();                                           \
    } else {                                                     \
      VisitRRRR(this, kRiscvS128Select, node);                   \
    }                                                            \
  }
SIMD_VISIT_SELECT_LANE(I8x16RelaxedLaneSelect)
SIMD_VISIT_SELECT_LANE(I16x8RelaxedLaneSelect)
SIMD_VISIT_SELECT_LANE(I32x4RelaxedLaneSelect)
SIMD_VISIT_SELECT_LANE(I64x2RelaxedLaneSelect)
#undef SIMD_VISIT_SELECT_LANE

#define VISIT_SIMD_QFMOP(Name, instruction)                      \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    if constexpr (Adapter::IsTurboshaft) {                       \
      UNIMPLEMENTED();                                           \
    } else {                                                     \
      VisitRRRR(this, instruction, node);                        \
    }                                                            \
  }
VISIT_SIMD_QFMOP(F64x2Qfma, kRiscvF64x2Qfma)
VISIT_SIMD_QFMOP(F64x2Qfms, kRiscvF64x2Qfms)
VISIT_SIMD_QFMOP(F32x4Qfma, kRiscvF32x4Qfma)
VISIT_SIMD_QFMOP(F32x4Qfms, kRiscvF32x4Qfms)
#undef VISIT_SIMD_QFMOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Min(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    InstructionOperand mask_reg = g.TempFpRegister(v0);
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);

    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmfeqVv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVandVv, mask_reg, temp2, temp1, g.UseImmediate(E32),
               g.UseImmediate(m1));

    InstructionOperand NaN = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand result = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, NaN, g.UseImmediate(0x7FC00000), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVfminVv, result, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1), g.UseImmediate(MaskType::Mask));
    this->Emit(kRiscvVmv, g.DefineAsRegister(node), result, g.UseImmediate(E32),
               g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Max(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    InstructionOperand mask_reg = g.TempFpRegister(v0);
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);

    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmfeqVv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVandVv, mask_reg, temp2, temp1, g.UseImmediate(E32),
               g.UseImmediate(m1));

    InstructionOperand NaN = g.TempFpRegister(kSimd128ScratchReg);
    InstructionOperand result = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, NaN, g.UseImmediate(0x7FC00000), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVfmaxVv, result, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1), g.UseImmediate(MaskType::Mask));
    this->Emit(kRiscvVmv, g.DefineAsRegister(node), result, g.UseImmediate(E32),
               g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMin(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitF32x4Min(node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMin(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitF64x2Min(node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMax(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitF64x2Max(node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMax(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitF32x4Max(node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Eq(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E64), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Ne(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfneVv, temp1, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E64), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Lt(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfltVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E64), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Le(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfleVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E64),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E64),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E64), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Eq(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfeqVv, temp1, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Ne(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfneVv, temp1, g.UseRegister(this->input_at(node, 1)),
               g.UseRegister(this->input_at(node, 0)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Lt(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfltVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Le(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    this->Emit(kRiscvVmfleVv, temp1, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E32),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp2, g.UseImmediate(0), g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVmergeVx, g.DefineAsRegister(node), g.UseImmediate(-1),
               temp2, g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertI16x8Low(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVsextVf2, g.DefineAsRegister(node), temp,
               g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertI16x8Low(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVzextVf2, g.DefineAsRegister(node), temp,
               g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8SConvertI8x16High(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp1 = g.TempFpRegister(v0);
    Emit(kRiscvVslidedown, temp1, g.UseRegister(this->input_at(node, 0)),
         g.UseImmediate(8), g.UseImmediate(E8), g.UseImmediate(m1));
    Emit(kRiscvVsextVf2, g.DefineAsRegister(node), temp1, g.UseImmediate(E16),
         g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8SConvertI32x4(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v26);
    InstructionOperand temp2 = g.TempFpRegister(v27);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVmv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVnclip, g.DefineAsRegister(node), temp, g.UseImmediate(0),
               g.UseImmediate(E16), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8UConvertI32x4(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v26);
    InstructionOperand temp2 = g.TempFpRegister(v27);
    InstructionOperand temp3 = g.TempFpRegister(v26);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVmv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseImmediate(E32), g.UseImmediate(m1));
    this->Emit(kRiscvVmax, temp3, temp, g.UseImmediate(0), g.UseImmediate(E32),
               g.UseImmediate(m2));
    this->Emit(kRiscvVnclipu, g.DefineAsRegister(node), temp3,
               g.UseImmediate(0), g.UseImmediate(E16), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16RoundingAverageU(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVwadduVv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E8),
               g.UseImmediate(m1));
    InstructionOperand temp2 = g.TempFpRegister(kSimd128ScratchReg3);
    this->Emit(kRiscvVwadduWx, temp2, temp, g.UseImmediate(1),
               g.UseImmediate(E8), g.UseImmediate(m1));
    InstructionOperand temp3 = g.TempFpRegister(kSimd128ScratchReg3);
    this->Emit(kRiscvVdivu, temp3, temp2, g.UseImmediate(2),
               g.UseImmediate(E16), g.UseImmediate(m2));
    this->Emit(kRiscvVnclipu, g.DefineAsRegister(node), temp3,
               g.UseImmediate(0), g.UseImmediate(E8), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16SConvertI16x8(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v26);
    InstructionOperand temp2 = g.TempFpRegister(v27);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVmv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVnclip, g.DefineAsRegister(node), temp, g.UseImmediate(0),
               g.UseImmediate(E8), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16UConvertI16x8(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v26);
    InstructionOperand temp2 = g.TempFpRegister(v27);
    InstructionOperand temp3 = g.TempFpRegister(v26);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVmv, temp2, g.UseRegister(this->input_at(node, 1)),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVmax, temp3, temp, g.UseImmediate(0), g.UseImmediate(E16),
               g.UseImmediate(m2));
    this->Emit(kRiscvVnclipu, g.DefineAsRegister(node), temp3,
               g.UseImmediate(0), g.UseImmediate(E8), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8RoundingAverageU(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v16);
    InstructionOperand temp2 = g.TempFpRegister(v16);
    InstructionOperand temp3 = g.TempFpRegister(v16);
    this->Emit(kRiscvVwadduVv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E16),
               g.UseImmediate(m1));
    this->Emit(kRiscvVwadduWx, temp2, temp, g.UseImmediate(1),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVdivu, temp3, temp2, g.UseImmediate(2),
               g.UseImmediate(E32), g.UseImmediate(m2));
    this->Emit(kRiscvVnclipu, g.DefineAsRegister(node), temp3,
               g.UseImmediate(0), g.UseImmediate(E16), g.UseImmediate(m1),
               g.UseImmediate(FPURoundingMode::RNE));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI16x8S(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    constexpr int32_t FIRST_INDEX = 0b01010101;
    constexpr int32_t SECOND_INDEX = 0b10101010;
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v16);
    InstructionOperand temp1 = g.TempFpRegister(v14);
    InstructionOperand temp2 = g.TempFpRegister(v30);
    InstructionOperand dst = g.DefineAsRegister(node);
    this->Emit(kRiscvVwmul, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E16),
               g.UseImmediate(m1));
    this->Emit(kRiscvVcompress, temp2, temp, g.UseImmediate(FIRST_INDEX),
               g.UseImmediate(E32), g.UseImmediate(m2));
    this->Emit(kRiscvVcompress, temp1, temp, g.UseImmediate(SECOND_INDEX),
               g.UseImmediate(E32), g.UseImmediate(m2));
    this->Emit(kRiscvVaddVv, dst, temp1, temp2, g.UseImmediate(E32),
               g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8DotI8x16I7x16S(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    constexpr int32_t FIRST_INDEX = 0b0101010101010101;
    constexpr int32_t SECOND_INDEX = 0b1010101010101010;
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(v16);
    InstructionOperand temp1 = g.TempFpRegister(v14);
    InstructionOperand temp2 = g.TempFpRegister(v30);
    InstructionOperand dst = g.DefineAsRegister(node);
    this->Emit(kRiscvVwmul, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E8),
               g.UseImmediate(m1));
    this->Emit(kRiscvVcompress, temp2, temp, g.UseImmediate(FIRST_INDEX),
               g.UseImmediate(E16), g.UseImmediate(m2));
    this->Emit(kRiscvVcompress, temp1, temp, g.UseImmediate(SECOND_INDEX),
               g.UseImmediate(E16), g.UseImmediate(m2));
    this->Emit(kRiscvVaddVv, dst, temp1, temp2, g.UseImmediate(E16),
               g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    constexpr int32_t FIRST_INDEX = 0b0001000100010001;
    constexpr int32_t SECOND_INDEX = 0b0010001000100010;
    constexpr int32_t THIRD_INDEX = 0b0100010001000100;
    constexpr int32_t FOURTH_INDEX = 0b1000100010001000;
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand intermediate = g.TempFpRegister(v12);
    this->Emit(kRiscvVwmul, intermediate,
               g.UseRegister(this->input_at(node, 0)),
               g.UseRegister(this->input_at(node, 1)), g.UseImmediate(E8),
               g.UseImmediate(m1));

    InstructionOperand compressedPart1 = g.TempFpRegister(v14);
    InstructionOperand compressedPart2 = g.TempFpRegister(v16);
    this->Emit(kRiscvVcompress, compressedPart2, intermediate,
               g.UseImmediate(FIRST_INDEX), g.UseImmediate(E16),
               g.UseImmediate(m2));
    this->Emit(kRiscvVcompress, compressedPart1, intermediate,
               g.UseImmediate(SECOND_INDEX), g.UseImmediate(E16),
               g.UseImmediate(m2));

    InstructionOperand compressedPart3 = g.TempFpRegister(v20);
    InstructionOperand compressedPart4 = g.TempFpRegister(v26);
    this->Emit(kRiscvVcompress, compressedPart3, intermediate,
               g.UseImmediate(THIRD_INDEX), g.UseImmediate(E16),
               g.UseImmediate(m2));
    this->Emit(kRiscvVcompress, compressedPart4, intermediate,
               g.UseImmediate(FOURTH_INDEX), g.UseImmediate(E16),
               g.UseImmediate(m2));

    InstructionOperand temp2 = g.TempFpRegister(v18);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVwaddVv, temp2, compressedPart1, compressedPart2,
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVwaddVv, temp, compressedPart3, compressedPart4,
               g.UseImmediate(E16), g.UseImmediate(m1));

    InstructionOperand mul_result = g.TempFpRegister(v16);
    InstructionOperand dst = g.DefineAsRegister(node);
    this->Emit(kRiscvVaddVv, mul_result, temp2, temp, g.UseImmediate(E32),
               g.UseImmediate(m1));
    this->Emit(kRiscvVaddVv, dst, mul_result, g.UseRegister(node->InputAt(2)),
               g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
    uint8_t shuffle[kSimd128Size];
    bool is_swizzle;
    // TODO(riscv): Properly use view here once Turboshaft support is
    // implemented.
    auto view = this->simd_shuffle_view(node);
    CanonicalizeShuffle(view, shuffle, &is_swizzle);
    node_t input0 = view.input(0);
    node_t input1 = view.input(1);
    RiscvOperandGeneratorT<Adapter> g(this);
    // uint8_t shuffle32x4[4];
    // ArchOpcode opcode;
    // if (TryMatchArchShuffle(shuffle, arch_shuffles, arraysize(arch_shuffles),
    //                         is_swizzle, &opcode)) {
    //   VisitRRR(this, opcode, node);
    //   return;
    // }
    // uint8_t offset;
    // if (wasm::SimdShuffle::TryMatchConcat(shuffle, &offset)) {
    //   Emit(kRiscvS8x16Concat, g.DefineSameAsFirst(node),
    //   g.UseRegister(input1),
    //        g.UseRegister(input0), g.UseImmediate(offset));
    //   return;
    // }
    // if (wasm::SimdShuffle::TryMatch32x4Shuffle(shuffle, shuffle32x4)) {
    //   Emit(kRiscvS32x4Shuffle, g.DefineAsRegister(node),
    //   g.UseRegister(input0),
    //        g.UseRegister(input1),
    //        g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle32x4)));
    //   return;
    // }
    Emit(kRiscvI8x16Shuffle, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseRegister(input1),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 4)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 8)),
         g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 12)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Swizzle(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temps[] = {g.TempSimd128Register()};
    // We don't want input 0 or input 1 to be the same as output, since we will
    // modify output before do the calculation.
    Emit(kRiscvVrgather, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)), g.UseImmediate(E8),
         g.UseImmediate(m1), arraysize(temps), temps);
}

#define VISIT_BIMASK(TYPE, VSEW, LMUL)                                      \
  template <typename Adapter>                                               \
  void InstructionSelectorT<Adapter>::Visit##TYPE##BitMask(node_t node) {   \
    RiscvOperandGeneratorT<Adapter> g(this);                                \
    InstructionOperand temp = g.TempFpRegister(v16);                        \
    this->Emit(kRiscvVmslt, temp, g.UseRegister(this->input_at(node, 0)),   \
               g.UseImmediate(0), g.UseImmediate(VSEW), g.UseImmediate(m1), \
               g.UseImmediate(true));                                       \
    this->Emit(kRiscvVmvXs, g.DefineAsRegister(node), temp,                 \
               g.UseImmediate(E32), g.UseImmediate(m1));                    \
  }

SIMD_INT_TYPE_LIST(VISIT_BIMASK)
#undef VISIT_BIMASK

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertI16x8High(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVslidedown, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(4), g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVsextVf2, g.DefineAsRegister(node), temp,
               g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertI16x8High(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVslidedown, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(4), g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVzextVf2, g.DefineAsRegister(node), temp,
               g.UseImmediate(E32), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8SConvertI8x16Low(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    this->Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
               g.UseImmediate(E16), g.UseImmediate(m1));
    this->Emit(kRiscvVsextVf2, g.DefineAsRegister(node), temp,
               g.UseImmediate(E16), g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8UConvertI8x16High(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    Emit(kRiscvVslidedown, temp, g.UseRegister(this->input_at(node, 0)),
         g.UseImmediate(8), g.UseImmediate(E8), g.UseImmediate(m1));
    Emit(kRiscvVzextVf2, g.DefineAsRegister(node), temp, g.UseImmediate(E16),
         g.UseImmediate(m1));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8UConvertI8x16Low(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    InstructionOperand temp = g.TempFpRegister(kSimd128ScratchReg);
    Emit(kRiscvVmv, temp, g.UseRegister(this->input_at(node, 0)),
         g.UseImmediate(E16), g.UseImmediate(m1));
    Emit(kRiscvVzextVf2, g.DefineAsRegister(node), temp, g.UseImmediate(E16),
         g.UseImmediate(m1));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt32(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvSignExtendByte, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt32(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvSignExtendShort, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Clz(node_t node) {
  VisitRR(this, kRiscvClz32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ctz(node_t node) {
    RiscvOperandGeneratorT<Adapter> g(this);
    Emit(kRiscvCtz32, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
}

#define VISIT_EXT_MUL(OPCODE1, OPCODE2, TYPE)                                  \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##OPCODE1##ExtMulLow##OPCODE2##S(   \
      node_t node) {                                                           \
    if constexpr (Adapter::IsTurboshaft) {                                     \
      UNIMPLEMENTED();                                                         \
    } else {                                                                   \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      Emit(kRiscvVwmul, g.DefineAsRegister(node),                              \
           g.UseUniqueRegister(this->input_at(node, 0)),                       \
           g.UseUniqueRegister(this->input_at(node, 1)),                       \
           g.UseImmediate(E##TYPE), g.UseImmediate(mf2));                      \
    }                                                                          \
  }                                                                            \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##OPCODE1##ExtMulHigh##OPCODE2##S(  \
      node_t node) {                                                           \
    if constexpr (Adapter::IsTurboshaft) {                                     \
      UNIMPLEMENTED();                                                         \
    } else {                                                                   \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      InstructionOperand t1 = g.TempFpRegister(v16);                           \
      Emit(kRiscvVslidedown, t1, g.UseUniqueRegister(this->input_at(node, 0)), \
           g.UseImmediate(kRvvVLEN / TYPE / 2), g.UseImmediate(E##TYPE),       \
           g.UseImmediate(m1));                                                \
      InstructionOperand t2 = g.TempFpRegister(v17);                           \
      Emit(kRiscvVslidedown, t2, g.UseUniqueRegister(this->input_at(node, 1)), \
           g.UseImmediate(kRvvVLEN / TYPE / 2), g.UseImmediate(E##TYPE),       \
           g.UseImmediate(m1));                                                \
      Emit(kRiscvVwmul, g.DefineAsRegister(node), t1, t2,                      \
           g.UseImmediate(E##TYPE), g.UseImmediate(mf2));                      \
    }                                                                          \
  }                                                                            \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##OPCODE1##ExtMulLow##OPCODE2##U(   \
      node_t node) {                                                           \
    if constexpr (Adapter::IsTurboshaft) {                                     \
      UNIMPLEMENTED();                                                         \
    } else {                                                                   \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      Emit(kRiscvVwmulu, g.DefineAsRegister(node),                             \
           g.UseUniqueRegister(this->input_at(node, 0)),                       \
           g.UseUniqueRegister(this->input_at(node, 1)),                       \
           g.UseImmediate(E##TYPE), g.UseImmediate(mf2));                      \
    }                                                                          \
  }                                                                            \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::Visit##OPCODE1##ExtMulHigh##OPCODE2##U(  \
      node_t node) {                                                           \
    if constexpr (Adapter::IsTurboshaft) {                                     \
      UNIMPLEMENTED();                                                         \
    } else {                                                                   \
      RiscvOperandGeneratorT<Adapter> g(this);                                 \
      InstructionOperand t1 = g.TempFpRegister(v16);                           \
      Emit(kRiscvVslidedown, t1, g.UseUniqueRegister(this->input_at(node, 0)), \
           g.UseImmediate(kRvvVLEN / TYPE / 2), g.UseImmediate(E##TYPE),       \
           g.UseImmediate(m1));                                                \
      InstructionOperand t2 = g.TempFpRegister(v17);                           \
      Emit(kRiscvVslidedown, t2, g.UseUniqueRegister(this->input_at(node, 1)), \
           g.UseImmediate(kRvvVLEN / TYPE / 2), g.UseImmediate(E##TYPE),       \
           g.UseImmediate(m1));                                                \
      Emit(kRiscvVwmulu, g.DefineAsRegister(node), t1, t2,                     \
           g.UseImmediate(E##TYPE), g.UseImmediate(mf2));                      \
    }                                                                          \
  }

VISIT_EXT_MUL(I64x2, I32x4, 32)
VISIT_EXT_MUL(I32x4, I16x8, 16)
VISIT_EXT_MUL(I16x8, I8x16, 8)
#undef VISIT_EXT_MUL

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmin(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitUniqueRRR(this, kRiscvF32x4Pmin, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmax(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitUniqueRRR(this, kRiscvF32x4Pmax, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmin(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitUniqueRRR(this, kRiscvF64x2Pmin, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmax(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    VisitUniqueRRR(this, kRiscvF64x2Pmax, node);
  }
}

// static
MachineOperatorBuilder::AlignmentRequirements
InstructionSelector::AlignmentRequirements() {
#ifdef RISCV_HAS_NO_UNALIGNED
  return MachineOperatorBuilder::AlignmentRequirements::
      NoUnalignedAccessSupport();
#else
  return MachineOperatorBuilder::AlignmentRequirements::
      FullUnalignedAccessSupport();
#endif
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGenerator* g, int first_input_index, node_t node) {
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSetStackPointer(node_t node) {
  OperandGenerator g(this);
  auto input = g.UseRegister(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}
#endif
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#undef SIMD_BINOP_LIST
#undef SIMD_SHIFT_OP_LIST
#undef SIMD_UNOP_LIST
#undef SIMD_TYPE_LIST
#endif  // V8_COMPILER_BACKEND_RISCV_INSTRUCTION_SELECTOR_RISCV_H_
