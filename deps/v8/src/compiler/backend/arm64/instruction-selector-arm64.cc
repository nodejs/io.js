// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/base/bits.h"
#include "src/base/logging.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/machine-type.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/turboshaft/operation-matcher.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {
namespace compiler {

enum ImmediateMode {
  kArithmeticImm,  // 12 bit unsigned immediate shifted left 0 or 12 bits
  kShift32Imm,     // 0 - 31
  kShift64Imm,     // 0 - 63
  kLogical32Imm,
  kLogical64Imm,
  kLoadStoreImm8,  // signed 8 bit or 12 bit unsigned scaled by access size
  kLoadStoreImm16,
  kLoadStoreImm32,
  kLoadStoreImm64,
  kConditionalCompareImm,
  kNoImmediate
};

// Adds Arm64-specific methods for generating operands.
template <typename Adapter>
class Arm64OperandGeneratorT final : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit Arm64OperandGeneratorT(InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  InstructionOperand UseOperand(node_t node, ImmediateMode mode) {
    if (CanBeImmediate(node, mode)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  bool IsImmediateZero(typename Adapter::node_t node) {
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

  // Use the zero register if the node has the immediate value zero, otherwise
  // assign a register.
  InstructionOperand UseRegisterOrImmediateZero(typename Adapter::node_t node) {
    if (IsImmediateZero(node)) {
      return UseImmediate(node);
    }
    return UseRegister(node);
  }

  // Use the zero register if the node has the immediate value zero, otherwise
  // assign a register, keeping it alive for the whole sequence of continuation
  // instructions.
  InstructionOperand UseRegisterAtEndOrImmediateZero(
      typename Adapter::node_t node) {
    if (IsImmediateZero(node)) {
      return UseImmediate(node);
    }
    return this->UseRegisterAtEnd(node);
  }

  // Use the provided node if it has the required value, or create a
  // TempImmediate otherwise.
  InstructionOperand UseImmediateOrTemp(node_t node, int32_t value) {
    if (selector()->integer_constant(node) == value) {
      return UseImmediate(node);
    }
    return TempImmediate(value);
  }

  int64_t GetIntegerConstantValue(Node* node) {
    if (node->opcode() == IrOpcode::kInt32Constant) {
      return OpParameter<int32_t>(node->op());
    }
    DCHECK_EQ(IrOpcode::kInt64Constant, node->opcode());
    return OpParameter<int64_t>(node->op());
  }

  bool IsIntegerConstant(node_t node) const {
    return selector()->is_integer_constant(node);
  }

  int64_t GetIntegerConstantValue(typename Adapter::ConstantView constant) {
    if (constant.is_int32()) {
      return constant.int32_value();
    }
    DCHECK(constant.is_int64());
    return constant.int64_value();
  }

  std::optional<int64_t> GetOptionalIntegerConstant(node_t operation) {
    if (!this->IsIntegerConstant(operation)) return {};
    return this->GetIntegerConstantValue(selector()->constant_view(operation));
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

  bool CanBeImmediate(node_t node, ImmediateMode mode) {
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

  bool CanBeImmediate(int64_t value, ImmediateMode mode) {
    unsigned ignored;
    switch (mode) {
      case kLogical32Imm:
        // TODO(dcarney): some unencodable values can be handled by
        // switching instructions.
        return Assembler::IsImmLogical(static_cast<uint32_t>(value), 32,
                                       &ignored, &ignored, &ignored);
      case kLogical64Imm:
        return Assembler::IsImmLogical(static_cast<uint64_t>(value), 64,
                                       &ignored, &ignored, &ignored);
      case kArithmeticImm:
        return Assembler::IsImmAddSub(value);
      case kLoadStoreImm8:
        return IsLoadStoreImmediate(value, 0);
      case kLoadStoreImm16:
        return IsLoadStoreImmediate(value, 1);
      case kLoadStoreImm32:
        return IsLoadStoreImmediate(value, 2);
      case kLoadStoreImm64:
        return IsLoadStoreImmediate(value, 3);
      case kNoImmediate:
        return false;
      case kConditionalCompareImm:
        return Assembler::IsImmConditionalCompare(value);
      case kShift32Imm:  // Fall through.
      case kShift64Imm:
        // Shift operations only observe the bottom 5 or 6 bits of the value.
        // All possible shifts can be encoded by discarding bits which have no
        // effect.
        return true;
    }
    return false;
  }

  bool CanBeLoadStoreShiftImmediate(node_t node, MachineRepresentation rep) {
    // TODO(arm64): Load and Store on 128 bit Q registers is not supported yet.
    DCHECK_GT(MachineRepresentation::kSimd128, rep);
    if (!selector()->is_constant(node)) return false;
    auto constant = selector()->constant_view(node);
    return IsIntegerConstant(constant) &&
           (GetIntegerConstantValue(constant) == ElementSizeLog2Of(rep));
  }

 private:
  bool IsLoadStoreImmediate(int64_t value, unsigned size) {
    return Assembler::IsImmLSScaled(value, size) ||
           Assembler::IsImmLSUnscaled(value);
  }
};

namespace {

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
             typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
             typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRRR(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
              Node* node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(node->InputAt(0)),
                 g.UseRegister(node->InputAt(1)));
}

template <typename Adapter>
void VisitRRR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)));
}

#if V8_ENABLE_WEBASSEMBLY
template <typename Adapter>
void VisitSimdShiftRRR(InstructionSelectorT<Adapter>* selector,
                       ArchOpcode opcode, typename Adapter::node_t node,
                       int width) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  if (selector->is_integer_constant(selector->input_at(node, 1))) {
    if (selector->integer_constant(selector->input_at(node, 1)) % width == 0) {
      selector->EmitIdentity(node);
    } else {
      selector->Emit(opcode, g.DefineAsRegister(node),
                     g.UseRegister(selector->input_at(node, 0)),
                     g.UseImmediate(selector->input_at(node, 1)));
    }
  } else {
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));
  }
}

template <typename Adapter>
void VisitRRI(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
              typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = selector->Get(node);
    int imm = op.template Cast<Simd128ExtractLaneOp>().lane;
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)),
                   g.UseImmediate(imm));
  } else {
    Arm64OperandGeneratorT<Adapter> g(selector);
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(node->InputAt(0)), g.UseImmediate(imm));
  }
}

template <typename Adapter>
void VisitRRIR(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
               typename Adapter::node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ReplaceLaneOp& op =
        selector->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    Arm64OperandGeneratorT<Adapter> g(selector);
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)),
                   g.UseImmediate(op.lane), g.UseUniqueRegister(op.input(1)));
  } else {
    Arm64OperandGeneratorT<Adapter> g(selector);
    int32_t imm = OpParameter<int32_t>(node->op());
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(node->InputAt(0)), g.UseImmediate(imm),
                   g.UseUniqueRegister(node->InputAt(1)));
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void VisitRRO(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
              typename Adapter::node_t node, ImmediateMode operand_mode) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseOperand(selector->input_at(node, 1), operand_mode));
}

template <typename Adapter>
struct ExtendingLoadMatcher {
  ExtendingLoadMatcher(typename Adapter::node_t node,
                       InstructionSelectorT<Adapter>* selector)
      : matches_(false), selector_(selector), immediate_(0) {
    Initialize(node);
  }

  bool Matches() const { return matches_; }

  typename Adapter::node_t base() const {
    DCHECK(Matches());
    return base_;
  }
  int64_t immediate() const {
    DCHECK(Matches());
    return immediate_;
  }
  ArchOpcode opcode() const {
    DCHECK(Matches());
    return opcode_;
  }

 private:
  bool matches_;
  InstructionSelectorT<Adapter>* selector_;
  typename Adapter::node_t base_{};
  int64_t immediate_;
  ArchOpcode opcode_;

  void Initialize(Node* node) {
    Int64BinopMatcher m(node);
    // When loading a 64-bit value and shifting by 32, we should
    // just load and sign-extend the interesting 4 bytes instead.
    // This happens, for example, when we're loading and untagging SMIs.
    DCHECK(m.IsWord64Sar());
    if (m.left().IsLoad() && m.right().Is(32) &&
        selector_->CanCover(m.node(), m.left().node())) {
      Arm64OperandGeneratorT<Adapter> g(selector_);
      Node* load = m.left().node();
      Node* offset = load->InputAt(1);
      base_ = load->InputAt(0);
      opcode_ = kArm64Ldrsw;
      if (g.IsIntegerConstant(offset)) {
        immediate_ = g.GetIntegerConstantValue(offset) + 4;
        matches_ = g.CanBeImmediate(immediate_, kLoadStoreImm32);
      }
    }
  }

  void Initialize(turboshaft::OpIndex node) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shift = selector_->Get(node).template Cast<ShiftOp>();
    DCHECK(shift.kind == ShiftOp::Kind::kShiftRightArithmetic ||
           shift.kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros);
    // When loading a 64-bit value and shifting by 32, we should
    // just load and sign-extend the interesting 4 bytes instead.
    // This happens, for example, when we're loading and untagging SMIs.
    const Operation& lhs = selector_->Get(shift.left());
    int64_t constant_rhs;

    if (lhs.Is<LoadOp>() &&
        selector_->MatchIntegralWord64Constant(shift.right(), &constant_rhs) &&
        constant_rhs == 32 && selector_->CanCover(node, shift.left())) {
      Arm64OperandGeneratorT<Adapter> g(selector_);
      const LoadOp& load = lhs.Cast<LoadOp>();
      base_ = load.base();
      opcode_ = kArm64Ldrsw;
      if (load.index().has_value()) {
        int64_t index_constant;
        if (selector_->MatchIntegralWord64Constant(load.index().value(),
                                                   &index_constant)) {
          DCHECK_EQ(load.element_size_log2, 0);
          immediate_ = index_constant + 4;
          matches_ = g.CanBeImmediate(immediate_, kLoadStoreImm32);
        }
      } else {
        immediate_ = load.offset + 4;
        matches_ = g.CanBeImmediate(immediate_, kLoadStoreImm32);
      }
    }
  }
};

template <typename Adapter>
bool TryMatchExtendingLoad(InstructionSelectorT<Adapter>* selector,
                           typename Adapter::node_t node) {
  ExtendingLoadMatcher<Adapter> m(node, selector);
  return m.Matches();
}

template <typename Adapter>
bool TryEmitExtendingLoad(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node) {
  ExtendingLoadMatcher<Adapter> m(node, selector);
  Arm64OperandGeneratorT<Adapter> g(selector);
  if (m.Matches()) {
    InstructionOperand inputs[2];
    inputs[0] = g.UseRegister(m.base());
    InstructionCode opcode =
        m.opcode() | AddressingModeField::encode(kMode_MRI);
    DCHECK(is_int32(m.immediate()));
    inputs[1] = g.TempImmediate(static_cast<int32_t>(m.immediate()));
    InstructionOperand outputs[] = {g.DefineAsRegister(node)};
    selector->Emit(opcode, arraysize(outputs), outputs, arraysize(inputs),
                   inputs);
    return true;
  }
  return false;
}

template <typename Adapter>
bool TryMatchAnyShift(InstructionSelectorT<Adapter>* selector, Node* node,
                      Node* input_node, InstructionCode* opcode, bool try_ror,
                      MachineRepresentation rep) {
  Arm64OperandGeneratorT<Adapter> g(selector);

  if (!selector->CanCover(node, input_node)) return false;
  if (input_node->InputCount() != 2) return false;
  if (!g.IsIntegerConstant(input_node->InputAt(1))) return false;

  switch (input_node->opcode()) {
    case IrOpcode::kWord32Shl:
    case IrOpcode::kWord32Shr:
    case IrOpcode::kWord32Sar:
    case IrOpcode::kWord32Ror:
      if (rep != MachineRepresentation::kWord32) return false;
      break;
    case IrOpcode::kWord64Shl:
    case IrOpcode::kWord64Shr:
    case IrOpcode::kWord64Sar:
    case IrOpcode::kWord64Ror:
      if (rep != MachineRepresentation::kWord64) return false;
      break;
    default:
      return false;
  }

  switch (input_node->opcode()) {
    case IrOpcode::kWord32Shl:
    case IrOpcode::kWord64Shl:
      *opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
      return true;
    case IrOpcode::kWord32Shr:
    case IrOpcode::kWord64Shr:
      *opcode |= AddressingModeField::encode(kMode_Operand2_R_LSR_I);
      return true;
    case IrOpcode::kWord32Sar:
      *opcode |= AddressingModeField::encode(kMode_Operand2_R_ASR_I);
      return true;
    case IrOpcode::kWord64Sar:
      if (TryMatchExtendingLoad(selector, input_node)) return false;
      *opcode |= AddressingModeField::encode(kMode_Operand2_R_ASR_I);
      return true;
    case IrOpcode::kWord32Ror:
    case IrOpcode::kWord64Ror:
      if (try_ror) {
        *opcode |= AddressingModeField::encode(kMode_Operand2_R_ROR_I);
        return true;
      }
      return false;
    default:
      UNREACHABLE();
  }
}

bool TryMatchAnyShift(InstructionSelectorT<TurboshaftAdapter>* selector,
                      turboshaft::OpIndex node, turboshaft::OpIndex input_node,
                      InstructionCode* opcode, bool try_ror,
                      turboshaft::RegisterRepresentation rep) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);

  if (!selector->CanCover(node, input_node)) return false;
  if (const ShiftOp* shift = selector->Get(input_node).TryCast<ShiftOp>()) {
    // Differently to Turbofan, the representation should always match.
    DCHECK_EQ(shift->rep, rep);
    if (shift->rep != rep) return false;
    if (!g.IsIntegerConstant(shift->right())) return false;

    switch (shift->kind) {
      case ShiftOp::Kind::kShiftLeft:
        *opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
        return true;
      case ShiftOp::Kind::kShiftRightLogical:
        *opcode |= AddressingModeField::encode(kMode_Operand2_R_LSR_I);
        return true;
      case ShiftOp::Kind::kShiftRightArithmetic:
      case ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros:
        if (rep == WordRepresentation::Word64() &&
            TryMatchExtendingLoad(selector, input_node)) {
          return false;
        }
        *opcode |= AddressingModeField::encode(kMode_Operand2_R_ASR_I);
        return true;
      case ShiftOp::Kind::kRotateRight:
        if (try_ror) {
          *opcode |= AddressingModeField::encode(kMode_Operand2_R_ROR_I);
          return true;
        }
        return false;
      case ShiftOp::Kind::kRotateLeft:
        return false;
    }
  }
  return false;
}

bool TryMatchAnyExtend(Arm64OperandGeneratorT<TurbofanAdapter>* g,
                       InstructionSelectorT<TurbofanAdapter>* selector,
                       Node* node, Node* left_node, Node* right_node,
                       InstructionOperand* left_op,
                       InstructionOperand* right_op, InstructionCode* opcode) {
  if (!selector->CanCover(node, right_node)) return false;

  NodeMatcher nm(right_node);

  if (nm.IsWord32And()) {
    Int32BinopMatcher mright(right_node);
    if (mright.right().Is(0xFF) || mright.right().Is(0xFFFF)) {
      int32_t mask = mright.right().ResolvedValue();
      *left_op = g->UseRegister(left_node);
      *right_op = g->UseRegister(mright.left().node());
      *opcode |= AddressingModeField::encode(
          (mask == 0xFF) ? kMode_Operand2_R_UXTB : kMode_Operand2_R_UXTH);
      return true;
    }
  } else if (nm.IsWord32Sar()) {
    Int32BinopMatcher mright(right_node);
    if (selector->CanCover(mright.node(), mright.left().node()) &&
        mright.left().IsWord32Shl()) {
      Int32BinopMatcher mleft_of_right(mright.left().node());
      if ((mright.right().Is(16) && mleft_of_right.right().Is(16)) ||
          (mright.right().Is(24) && mleft_of_right.right().Is(24))) {
        int32_t shift = mright.right().ResolvedValue();
        *left_op = g->UseRegister(left_node);
        *right_op = g->UseRegister(mleft_of_right.left().node());
        *opcode |= AddressingModeField::encode(
            (shift == 24) ? kMode_Operand2_R_SXTB : kMode_Operand2_R_SXTH);
        return true;
      }
    }
  } else if (nm.IsChangeInt32ToInt64()) {
    // Use extended register form.
    *opcode |= AddressingModeField::encode(kMode_Operand2_R_SXTW);
    *left_op = g->UseRegister(left_node);
    *right_op = g->UseRegister(right_node->InputAt(0));
    return true;
  }
  return false;
}

bool TryMatchBitwiseAndSmallMask(turboshaft::OperationMatcher& matcher,
                                 turboshaft::OpIndex op,
                                 turboshaft::OpIndex* left, int32_t* mask) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (const ChangeOp* change_op =
          matcher.TryCast<Opmask::kChangeInt32ToInt64>(op)) {
    return TryMatchBitwiseAndSmallMask(matcher, change_op->input(), left, mask);
  }
  if (const WordBinopOp* bitwise_and =
          matcher.TryCast<Opmask::kWord32BitwiseAnd>(op)) {
    if (matcher.MatchIntegralWord32Constant(bitwise_and->right(), mask) &&
        (*mask == 0xFF || *mask == 0xFFFF)) {
      *left = bitwise_and->left();
      return true;
    }
    if (matcher.MatchIntegralWord32Constant(bitwise_and->left(), mask) &&
        (*mask == 0xFF || *mask == 0xFFFF)) {
      *left = bitwise_and->right();
      return true;
    }
  }
  return false;
}

bool TryMatchSignExtendShift(InstructionSelectorT<TurboshaftAdapter>* selector,
                             turboshaft::OpIndex op, turboshaft::OpIndex* left,
                             int32_t* shift_by) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (const ChangeOp* change_op =
          selector->TryCast<Opmask::kChangeInt32ToInt64>(op)) {
    return TryMatchSignExtendShift(selector, change_op->input(), left,
                                   shift_by);
  }

  if (const ShiftOp* sar =
          selector->TryCast<Opmask::kWord32ShiftRightArithmetic>(op)) {
    const Operation& sar_lhs = selector->Get(sar->left());
    if (sar_lhs.Is<Opmask::kWord32ShiftLeft>() &&
        selector->CanCover(op, sar->left())) {
      const ShiftOp& shl = sar_lhs.Cast<ShiftOp>();
      int32_t sar_by, shl_by;
      if (selector->MatchIntegralWord32Constant(sar->right(), &sar_by) &&
          selector->MatchIntegralWord32Constant(shl.right(), &shl_by) &&
          sar_by == shl_by && (sar_by == 16 || sar_by == 24)) {
        *left = shl.left();
        *shift_by = sar_by;
        return true;
      }
    }
  }
  return false;
}

bool TryMatchAnyExtend(Arm64OperandGeneratorT<TurboshaftAdapter>* g,
                       InstructionSelectorT<TurboshaftAdapter>* selector,
                       turboshaft::OpIndex node, turboshaft::OpIndex left_node,
                       turboshaft::OpIndex right_node,
                       InstructionOperand* left_op,
                       InstructionOperand* right_op, InstructionCode* opcode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (!selector->CanCover(node, right_node)) return false;

  const Operation& right = selector->Get(right_node);
  OpIndex bitwise_and_left;
  int32_t mask;
  if (TryMatchBitwiseAndSmallMask(*selector, right_node, &bitwise_and_left,
                                  &mask)) {
    *left_op = g->UseRegister(left_node);
    *right_op = g->UseRegister(bitwise_and_left);
    *opcode |= AddressingModeField::encode(
        (mask == 0xFF) ? kMode_Operand2_R_UXTB : kMode_Operand2_R_UXTH);
    return true;
  }

  OpIndex shift_input_left;
  int32_t shift_by;
  if (TryMatchSignExtendShift(selector, right_node, &shift_input_left,
                              &shift_by)) {
    *left_op = g->UseRegister(left_node);
    *right_op = g->UseRegister(shift_input_left);
    *opcode |= AddressingModeField::encode(
        (shift_by == 24) ? kMode_Operand2_R_SXTB : kMode_Operand2_R_SXTH);
    return true;
  }

  if (const ChangeOp* change_op =
          right.TryCast<Opmask::kChangeInt32ToInt64>()) {
    // Use extended register form.
    *opcode |= AddressingModeField::encode(kMode_Operand2_R_SXTW);
    *left_op = g->UseRegister(left_node);
    *right_op = g->UseRegister(change_op->input());
    return true;
  }
  return false;
}

template <typename Adapter>
bool TryMatchLoadStoreShift(Arm64OperandGeneratorT<Adapter>* g,
                            InstructionSelectorT<Adapter>* selector,
                            MachineRepresentation rep,
                            typename Adapter::node_t node,
                            typename Adapter::node_t index,
                            InstructionOperand* index_op,
                            InstructionOperand* shift_immediate_op) {
  if (!selector->CanCover(node, index)) return false;
  if (index->InputCount() != 2) return false;
  switch (index->opcode()) {
    case IrOpcode::kWord32Shl:
    case IrOpcode::kWord64Shl: {
      Node* left = index->InputAt(0);
      Node* right = index->InputAt(1);
      if (!g->CanBeLoadStoreShiftImmediate(right, rep)) {
        return false;
      }
      *index_op = g->UseRegister(left);
      *shift_immediate_op = g->UseImmediate(right);
      return true;
    }
    default:
      return false;
  }
}

template <>
bool TryMatchLoadStoreShift(Arm64OperandGeneratorT<TurboshaftAdapter>* g,
                            InstructionSelectorT<TurboshaftAdapter>* selector,
                            MachineRepresentation rep, turboshaft::OpIndex node,
                            turboshaft::OpIndex index,
                            InstructionOperand* index_op,
                            InstructionOperand* shift_immediate_op) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (!selector->CanCover(node, index)) return false;
  if (const ChangeOp* change =
          selector->Get(index).TryCast<Opmask::kChangeUint32ToUint64>();
      change && selector->CanCover(index, change->input())) {
    index = change->input();
  }
  const ShiftOp* shift = selector->Get(index).TryCast<Opmask::kShiftLeft>();
  if (shift == nullptr) return false;
  if (!g->CanBeLoadStoreShiftImmediate(shift->right(), rep)) return false;
  *index_op = g->UseRegister(shift->left());
  *shift_immediate_op = g->UseImmediate(shift->right());
  return true;
}

// Bitfields describing binary operator properties:
// CanCommuteField is true if we can switch the two operands, potentially
// requiring commuting the flags continuation condition.
using CanCommuteField = base::BitField8<bool, 1, 1>;
// MustCommuteCondField is true when we need to commute the flags continuation
// condition in order to switch the operands.
using MustCommuteCondField = base::BitField8<bool, 2, 1>;
// IsComparisonField is true when the operation is a comparison and has no other
// result other than the condition.
using IsComparisonField = base::BitField8<bool, 3, 1>;
// IsAddSubField is true when an instruction is encoded as ADD or SUB.
using IsAddSubField = base::BitField8<bool, 4, 1>;

// Get properties of a binary operator.
uint8_t GetBinopProperties(InstructionCode opcode) {
  uint8_t result = 0;
  switch (opcode) {
    case kArm64Cmp32:
    case kArm64Cmp:
      // We can commute CMP by switching the inputs and commuting
      // the flags continuation.
      result = CanCommuteField::update(result, true);
      result = MustCommuteCondField::update(result, true);
      result = IsComparisonField::update(result, true);
      // The CMP and CMN instructions are encoded as SUB or ADD
      // with zero output register, and therefore support the same
      // operand modes.
      result = IsAddSubField::update(result, true);
      break;
    case kArm64Cmn32:
    case kArm64Cmn:
      result = CanCommuteField::update(result, true);
      result = IsComparisonField::update(result, true);
      result = IsAddSubField::update(result, true);
      break;
    case kArm64Add32:
    case kArm64Add:
      result = CanCommuteField::update(result, true);
      result = IsAddSubField::update(result, true);
      break;
    case kArm64Sub32:
    case kArm64Sub:
      result = IsAddSubField::update(result, true);
      break;
    case kArm64Tst32:
    case kArm64Tst:
      result = CanCommuteField::update(result, true);
      result = IsComparisonField::update(result, true);
      break;
    case kArm64And32:
    case kArm64And:
    case kArm64Or32:
    case kArm64Or:
    case kArm64Eor32:
    case kArm64Eor:
      result = CanCommuteField::update(result, true);
      break;
    default:
      UNREACHABLE();
  }
  DCHECK_IMPLIES(MustCommuteCondField::decode(result),
                 CanCommuteField::decode(result));
  return result;
}

// Shared routine for multiple binary operations.
template <typename Adapter, typename Matcher>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                ImmediateMode operand_mode, FlagsContinuationT<Adapter>* cont) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  InstructionOperand inputs[5];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  Node* left_node = node->InputAt(0);
  Node* right_node = node->InputAt(1);

  uint8_t properties = GetBinopProperties(opcode);
  bool can_commute = CanCommuteField::decode(properties);
  bool must_commute_cond = MustCommuteCondField::decode(properties);
  bool is_add_sub = IsAddSubField::decode(properties);

  if (g.CanBeImmediate(right_node, operand_mode)) {
    inputs[input_count++] = g.UseRegister(left_node);
    inputs[input_count++] = g.UseImmediate(right_node);
  } else if (can_commute && g.CanBeImmediate(left_node, operand_mode)) {
    if (must_commute_cond) cont->Commute();
    inputs[input_count++] = g.UseRegister(right_node);
    inputs[input_count++] = g.UseImmediate(left_node);
  } else if (is_add_sub &&
             TryMatchAnyExtend(&g, selector, node, left_node, right_node,
                               &inputs[0], &inputs[1], &opcode)) {
    input_count += 2;
  } else if (is_add_sub && can_commute &&
             TryMatchAnyExtend(&g, selector, node, right_node, left_node,
                               &inputs[0], &inputs[1], &opcode)) {
    if (must_commute_cond) cont->Commute();
    input_count += 2;
  } else if (TryMatchAnyShift(selector, node, right_node, &opcode, !is_add_sub,
                              Matcher::representation)) {
    Matcher m_shift(right_node);
    inputs[input_count++] = g.UseRegisterOrImmediateZero(left_node);
    inputs[input_count++] = g.UseRegister(m_shift.left().node());
    // We only need at most the last 6 bits of the shift.
    inputs[input_count++] = g.UseImmediate(static_cast<int>(
        g.GetIntegerConstantValue(m_shift.right().node()) & 0x3F));
  } else if (can_commute &&
             TryMatchAnyShift(selector, node, left_node, &opcode, !is_add_sub,
                              Matcher::representation)) {
    if (must_commute_cond) cont->Commute();
    Matcher m_shift(left_node);
    inputs[input_count++] = g.UseRegisterOrImmediateZero(right_node);
    inputs[input_count++] = g.UseRegister(m_shift.left().node());
    // We only need at most the last 6 bits of the shift.
    inputs[input_count++] = g.UseImmediate(static_cast<int>(
        g.GetIntegerConstantValue(m_shift.right().node()) & 0x3F));
  } else {
    inputs[input_count++] = g.UseRegisterOrImmediateZero(left_node);
    inputs[input_count++] = g.UseRegister(right_node);
  }

  if (!IsComparisonField::decode(properties)) {
    outputs[output_count++] = g.DefineAsRegister(node);
  }

  if (cont->IsSelect()) {
    // Keep the values live until the end so that we can use operations that
    // write registers to generate the condition, without accidently
    // overwriting the inputs.
    inputs[input_count++] =
        g.UseRegisterAtEndOrImmediateZero(cont->true_value());
    inputs[input_count++] =
        g.UseRegisterAtEndOrImmediateZero(cont->false_value());
  }

  DCHECK_NE(0u, input_count);
  DCHECK((output_count != 0) || IsComparisonField::decode(properties));
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

// Shared routine for multiple binary operations.
template <typename Adapter, typename Matcher>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, ArchOpcode opcode,
                ImmediateMode operand_mode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop<Adapter, Matcher>(selector, node, opcode, operand_mode, &cont);
}

void VisitBinopImpl(InstructionSelectorT<TurboshaftAdapter>* selector,
                    turboshaft::OpIndex binop_idx,
                    turboshaft::OpIndex left_node,
                    turboshaft::OpIndex right_node,
                    turboshaft::RegisterRepresentation rep,
                    InstructionCode opcode, ImmediateMode operand_mode,
                    FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  constexpr uint32_t kMaxFlagSetInputs = 3;
  constexpr uint32_t kMaxCcmpOperands =
      FlagsContinuationT<TurboshaftAdapter>::kMaxCompareChainSize *
      kNumCcmpOperands;
  constexpr uint32_t kExtraCcmpInputs = 2;
  constexpr uint32_t kMaxInputs =
      kMaxFlagSetInputs + kMaxCcmpOperands + kExtraCcmpInputs;
  InstructionOperand inputs[kMaxInputs];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  uint8_t properties = GetBinopProperties(opcode);
  bool can_commute = CanCommuteField::decode(properties);
  bool must_commute_cond = MustCommuteCondField::decode(properties);
  bool is_add_sub = IsAddSubField::decode(properties);

  // We've already commuted the flags while searching for the pattern.
  if (cont->IsConditionalSet() || cont->IsConditionalBranch()) {
    can_commute = false;
  }
  if (g.CanBeImmediate(right_node, operand_mode)) {
    inputs[input_count++] = g.UseRegister(left_node);
    inputs[input_count++] = g.UseImmediate(right_node);
  } else if (can_commute && g.CanBeImmediate(left_node, operand_mode)) {
    if (must_commute_cond) cont->Commute();
    inputs[input_count++] = g.UseRegister(right_node);
    inputs[input_count++] = g.UseImmediate(left_node);
  } else if (is_add_sub &&
             TryMatchAnyExtend(&g, selector, binop_idx, left_node, right_node,
                               &inputs[0], &inputs[1], &opcode)) {
    input_count += 2;
  } else if (is_add_sub && can_commute &&
             TryMatchAnyExtend(&g, selector, binop_idx, right_node, left_node,
                               &inputs[0], &inputs[1], &opcode)) {
    if (must_commute_cond) cont->Commute();
    input_count += 2;
  } else if (TryMatchAnyShift(selector, binop_idx, right_node, &opcode,
                              !is_add_sub, rep)) {
    const ShiftOp& shift = selector->Get(right_node).Cast<ShiftOp>();
    inputs[input_count++] = g.UseRegisterOrImmediateZero(left_node);
    inputs[input_count++] = g.UseRegister(shift.left());
    // We only need at most the last 6 bits of the shift.
    inputs[input_count++] = g.UseImmediate(
        static_cast<int>(selector->integer_constant(shift.right()) & 0x3F));
  } else if (can_commute && TryMatchAnyShift(selector, binop_idx, left_node,
                                             &opcode, !is_add_sub, rep)) {
    if (must_commute_cond) cont->Commute();
    const ShiftOp& shift = selector->Get(left_node).Cast<ShiftOp>();
    inputs[input_count++] = g.UseRegisterOrImmediateZero(right_node);
    inputs[input_count++] = g.UseRegister(shift.left());
    // We only need at most the last 6 bits of the shift.
    inputs[input_count++] = g.UseImmediate(
        static_cast<int>(selector->integer_constant(shift.right()) & 0x3F));
  } else {
    inputs[input_count++] = g.UseRegisterOrImmediateZero(left_node);
    inputs[input_count++] = g.UseRegister(right_node);
  }

  if (!IsComparisonField::decode(properties)) {
    outputs[output_count++] = g.DefineAsRegister(binop_idx);
  }

  if (cont->IsSelect()) {
    // Keep the values live until the end so that we can use operations that
    // write registers to generate the condition, without accidently
    // overwriting the inputs.
    inputs[input_count++] = g.UseRegisterAtEnd(cont->true_value());
    inputs[input_count++] = g.UseRegisterAtEnd(cont->false_value());
  } else if (cont->IsConditionalSet() || cont->IsConditionalBranch()) {
    DCHECK_LE(input_count, kMaxInputs);
    auto& compares = cont->compares();
    for (unsigned i = 0; i < cont->num_conditional_compares(); ++i) {
      auto compare = compares[i];
      inputs[input_count + kCcmpOffsetOfOpcode] = g.TempImmediate(compare.code);
      inputs[input_count + kCcmpOffsetOfLhs] = g.UseRegisterAtEnd(compare.lhs);
      if (g.CanBeImmediate(compare.rhs, kConditionalCompareImm)) {
        inputs[input_count + kCcmpOffsetOfRhs] = g.UseImmediate(compare.rhs);
      } else {
        inputs[input_count + kCcmpOffsetOfRhs] =
            g.UseRegisterAtEnd(compare.rhs);
      }
      inputs[input_count + kCcmpOffsetOfDefaultFlags] =
          g.TempImmediate(compare.default_flags);
      inputs[input_count + kCcmpOffsetOfCompareCondition] =
          g.TempImmediate(compare.compare_condition);
      input_count += kNumCcmpOperands;
    }
    inputs[input_count++] = g.TempImmediate(cont->final_condition());
    inputs[input_count++] =
        g.TempImmediate(static_cast<int32_t>(cont->num_conditional_compares()));
  }

  DCHECK_NE(0u, input_count);
  DCHECK((output_count != 0) || IsComparisonField::decode(properties));
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

// Shared routine for multiple binary operations.
void VisitBinop(InstructionSelectorT<TurboshaftAdapter>* selector,
                turboshaft::OpIndex binop_idx,
                turboshaft::RegisterRepresentation rep, InstructionCode opcode,
                ImmediateMode operand_mode,
                FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& binop = selector->Get(binop_idx);
  OpIndex left_node = binop.input(0);
  OpIndex right_node = binop.input(1);
  return VisitBinopImpl(selector, binop_idx, left_node, right_node, rep, opcode,
                        operand_mode, cont);
}

void VisitBinop(InstructionSelectorT<TurboshaftAdapter>* selector,
                turboshaft::OpIndex node,
                turboshaft::RegisterRepresentation rep, ArchOpcode opcode,
                ImmediateMode operand_mode) {
  FlagsContinuationT<TurboshaftAdapter> cont;
  VisitBinop(selector, node, rep, opcode, operand_mode, &cont);
}

template <typename Adapter, typename Matcher>
void VisitAddSub(InstructionSelectorT<Adapter>* selector, Node* node,
                 ArchOpcode opcode, ArchOpcode negate_opcode) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  Matcher m(node);
  if (m.right().HasResolvedValue() && (m.right().ResolvedValue() < 0) &&
      (m.right().ResolvedValue() > std::numeric_limits<int>::min()) &&
      g.CanBeImmediate(-m.right().ResolvedValue(), kArithmeticImm)) {
    selector->Emit(
        negate_opcode, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
        g.TempImmediate(static_cast<int32_t>(-m.right().ResolvedValue())));
  } else {
    VisitBinop<Adapter, Matcher>(selector, node, opcode, kArithmeticImm);
  }
}

std::tuple<turboshaft::OpIndex, turboshaft::OpIndex>
GetBinopLeftRightCstOnTheRight(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    const turboshaft::WordBinopOp& binop) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  OpIndex left = binop.left();
  OpIndex right = binop.right();
  if (!selector->Is<ConstantOp>(right) &&
      WordBinopOp::IsCommutative(binop.kind) &&
      selector->Is<ConstantOp>(left)) {
    std::swap(left, right);
  }
  return {left, right};
}

void VisitAddSub(InstructionSelectorT<TurboshaftAdapter>* selector,
                 turboshaft::OpIndex node, ArchOpcode opcode,
                 ArchOpcode negate_opcode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const WordBinopOp& add_sub = selector->Get(node).Cast<WordBinopOp>();
  auto [left, right] = GetBinopLeftRightCstOnTheRight(selector, add_sub);

  if (std::optional<int64_t> constant_rhs =
          g.GetOptionalIntegerConstant(right)) {
    if (constant_rhs < 0 && constant_rhs > std::numeric_limits<int>::min() &&
        g.CanBeImmediate(-*constant_rhs, kArithmeticImm)) {
      selector->Emit(negate_opcode, g.DefineAsRegister(node),
                     g.UseRegister(left),
                     g.TempImmediate(static_cast<int32_t>(-*constant_rhs)));
      return;
    }
  }
  VisitBinop(selector, node, add_sub.rep, opcode, kArithmeticImm);
}

// For multiplications by immediate of the form x * (2^k + 1), where k > 0,
// return the value of k, otherwise return zero. This is used to reduce the
// multiplication to addition with left shift: x + (x << k).
template <typename Matcher>
int32_t LeftShiftForReducedMultiply(Matcher* m) {
  DCHECK(m->IsInt32Mul() || m->IsInt64Mul());
  if (m->right().HasResolvedValue() && m->right().ResolvedValue() >= 3) {
    uint64_t value_minus_one = m->right().ResolvedValue() - 1;
    if (base::bits::IsPowerOfTwo(value_minus_one)) {
      return base::bits::WhichPowerOfTwo(value_minus_one);
    }
  }
  return 0;
}

// For multiplications by immediate of the form x * (2^k + 1), where k > 0,
// return the value of k, otherwise return zero. This is used to reduce the
// multiplication to addition with left shift: x + (x << k).
int32_t LeftShiftForReducedMultiply(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    turboshaft::OpIndex rhs) {
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  if (auto constant = g.GetOptionalIntegerConstant(rhs)) {
    int64_t value_minus_one = constant.value() - 1;
    if (base::bits::IsPowerOfTwo(value_minus_one)) {
      return base::bits::WhichPowerOfTwo(value_minus_one);
    }
  }
  return 0;
}

// Try to match Add(Mul(x, y), z) and emit Madd(x, y, z) for it.
template <typename MultiplyOpmaskT>
bool TryEmitMulitplyAdd(InstructionSelectorT<TurboshaftAdapter>* selector,
                        turboshaft::OpIndex add, turboshaft::OpIndex lhs,
                        turboshaft::OpIndex rhs, InstructionCode madd_opcode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& add_lhs = selector->Get(lhs);
  if (!add_lhs.Is<MultiplyOpmaskT>() || !selector->CanCover(add, lhs)) {
    return false;
  }
  // Check that multiply can't be reduced to an addition with shift later on.
  const WordBinopOp& mul = add_lhs.Cast<WordBinopOp>();
  if (LeftShiftForReducedMultiply(selector, mul.right()) != 0) return false;

  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  selector->Emit(madd_opcode, g.DefineAsRegister(add),
                 g.UseRegister(mul.left()), g.UseRegister(mul.right()),
                 g.UseRegister(rhs));
  return true;
}

bool TryEmitMultiplyAddInt32(InstructionSelectorT<TurboshaftAdapter>* selector,
                             turboshaft::OpIndex add, turboshaft::OpIndex lhs,
                             turboshaft::OpIndex rhs) {
  return TryEmitMulitplyAdd<turboshaft::Opmask::kWord32Mul>(selector, add, lhs,
                                                            rhs, kArm64Madd32);
}

bool TryEmitMultiplyAddInt64(InstructionSelectorT<TurboshaftAdapter>* selector,
                             turboshaft::OpIndex add, turboshaft::OpIndex lhs,
                             turboshaft::OpIndex rhs) {
  return TryEmitMulitplyAdd<turboshaft::Opmask::kWord64Mul>(selector, add, lhs,
                                                            rhs, kArm64Madd);
}

// Try to match Mul(Sub(0, x), y) and emit Mneg(x, y) for it.
template <typename SubtractOpmaskT>
bool TryEmitMultiplyNegate(InstructionSelectorT<TurboshaftAdapter>* selector,
                           turboshaft::OpIndex mul, turboshaft::OpIndex lhs,
                           turboshaft::OpIndex rhs,
                           InstructionCode mneg_opcode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& mul_lhs = selector->Get(lhs);
  if (!mul_lhs.Is<SubtractOpmaskT>() || !selector->CanCover(mul, lhs)) {
    return false;
  }
  const WordBinopOp& sub = mul_lhs.Cast<WordBinopOp>();
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  std::optional<int64_t> sub_lhs_constant =
      g.GetOptionalIntegerConstant(sub.left());
  if (!sub_lhs_constant.has_value() || sub_lhs_constant != 0) return false;
  selector->Emit(mneg_opcode, g.DefineAsRegister(mul),
                 g.UseRegister(sub.right()), g.UseRegister(rhs));
  return true;
}

bool TryEmitMultiplyNegateInt32(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex mul,
    turboshaft::OpIndex lhs, turboshaft::OpIndex rhs) {
  return TryEmitMultiplyNegate<turboshaft::Opmask::kWord32Sub>(
      selector, mul, lhs, rhs, kArm64Mneg32);
}

bool TryEmitMultiplyNegateInt64(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex mul,
    turboshaft::OpIndex lhs, turboshaft::OpIndex rhs) {
  return TryEmitMultiplyNegate<turboshaft::Opmask::kWord64Sub>(
      selector, mul, lhs, rhs, kArm64Mneg);
}

// Try to match Sub(a, Mul(x, y)) and emit Msub(x, y, a) for it.
template <typename MultiplyOpmaskT>
bool TryEmitMultiplySub(InstructionSelectorT<TurboshaftAdapter>* selector,
                        turboshaft::OpIndex node,
                        InstructionCode msub_opbocde) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const WordBinopOp& sub = selector->Get(node).Cast<WordBinopOp>();
  DCHECK_EQ(sub.kind, WordBinopOp::Kind::kSub);

  // Select Msub(x, y, a) for Sub(a, Mul(x, y)).
  const Operation& sub_rhs = selector->Get(sub.right());
  if (sub_rhs.Is<MultiplyOpmaskT>() && selector->CanCover(node, sub.right())) {
    const WordBinopOp& mul = sub_rhs.Cast<WordBinopOp>();
    if (LeftShiftForReducedMultiply(selector, mul.right()) == 0) {
      Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
      selector->Emit(msub_opbocde, g.DefineAsRegister(node),
                     g.UseRegister(mul.left()), g.UseRegister(mul.right()),
                     g.UseRegister(sub.left()));
      return true;
    }
  }
  return false;
}

std::tuple<InstructionCode, ImmediateMode> GetStoreOpcodeAndImmediate(
    turboshaft::MemoryRepresentation stored_rep, bool paired) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (stored_rep) {
    case MemoryRepresentation::Int8():
    case MemoryRepresentation::Uint8():
      CHECK(!paired);
      return {kArm64Strb, kLoadStoreImm8};
    case MemoryRepresentation::Int16():
    case MemoryRepresentation::Uint16():
      CHECK(!paired);
      return {kArm64Strh, kLoadStoreImm16};
    case MemoryRepresentation::Int32():
    case MemoryRepresentation::Uint32():
      return {paired ? kArm64StrWPair : kArm64StrW, kLoadStoreImm32};
    case MemoryRepresentation::Int64():
    case MemoryRepresentation::Uint64():
      return {paired ? kArm64StrPair : kArm64Str, kLoadStoreImm64};
    case MemoryRepresentation::Float16():
      CHECK(!paired);
      return {kArm64StrH, kLoadStoreImm16};
    case MemoryRepresentation::Float32():
      CHECK(!paired);
      return {kArm64StrS, kLoadStoreImm32};
    case MemoryRepresentation::Float64():
      CHECK(!paired);
      return {kArm64StrD, kLoadStoreImm64};
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
    case MemoryRepresentation::TaggedSigned():
      if (paired) {
        // There is an inconsistency here on how we treat stores vs. paired
        // stores. In the normal store case we have special opcodes for
        // compressed fields and the backend decides whether to write 32 or 64
        // bits. However, for pairs this does not make sense, since the
        // paired values could have different representations (e.g.,
        // compressed paired with word32). Therefore, we decide on the actual
        // machine representation already in instruction selection.
#ifdef V8_COMPRESS_POINTERS
        static_assert(ElementSizeLog2Of(MachineRepresentation::kTagged) == 2);
        return {kArm64StrWPair, kLoadStoreImm32};
#else
        static_assert(ElementSizeLog2Of(MachineRepresentation::kTagged) == 3);
        return {kArm64StrPair, kLoadStoreImm64};
#endif
      }
      return {kArm64StrCompressTagged,
              COMPRESS_POINTERS_BOOL ? kLoadStoreImm32 : kLoadStoreImm64};
    case MemoryRepresentation::AnyUncompressedTagged():
    case MemoryRepresentation::UncompressedTaggedPointer():
    case MemoryRepresentation::UncompressedTaggedSigned():
      CHECK(!paired);
      return {kArm64Str, kLoadStoreImm64};
    case MemoryRepresentation::ProtectedPointer():
      // We never store directly to protected pointers from generated code.
      UNREACHABLE();
    case MemoryRepresentation::IndirectPointer():
      return {kArm64StrIndirectPointer, kLoadStoreImm32};
    case MemoryRepresentation::SandboxedPointer():
      CHECK(!paired);
      return {kArm64StrEncodeSandboxedPointer, kLoadStoreImm64};
    case MemoryRepresentation::Simd128():
      CHECK(!paired);
      return {kArm64StrQ, kNoImmediate};
    case MemoryRepresentation::Simd256():
      UNREACHABLE();
  }
}

std::tuple<InstructionCode, ImmediateMode> GetStoreOpcodeAndImmediate(
    MachineRepresentation rep, bool paired) {
  InstructionCode opcode = kArchNop;
  ImmediateMode immediate_mode = kNoImmediate;
  switch (rep) {
    case MachineRepresentation::kFloat16:
      CHECK(!paired);
      opcode = kArm64StrH;
      immediate_mode = kLoadStoreImm16;
      break;
    case MachineRepresentation::kFloat32:
      CHECK(!paired);
      opcode = kArm64StrS;
      immediate_mode = kLoadStoreImm32;
      break;
    case MachineRepresentation::kFloat64:
      CHECK(!paired);
      opcode = kArm64StrD;
      immediate_mode = kLoadStoreImm64;
      break;
    case MachineRepresentation::kBit:
    case MachineRepresentation::kWord8:
      CHECK(!paired);
      opcode = kArm64Strb;
      immediate_mode = kLoadStoreImm8;
      break;
    case MachineRepresentation::kWord16:
      CHECK(!paired);
      opcode = kArm64Strh;
      immediate_mode = kLoadStoreImm16;
      break;
    case MachineRepresentation::kWord32:
      opcode = paired ? kArm64StrWPair : kArm64StrW;
      immediate_mode = kLoadStoreImm32;
      break;
    case MachineRepresentation::kCompressedPointer:
    case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
      opcode = paired ? kArm64StrWPair : kArm64StrCompressTagged;
      immediate_mode = kLoadStoreImm32;
      break;
#else
      UNREACHABLE();
#endif
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      if (paired) {
        // There is an inconsistency here on how we treat stores vs. paired
        // stores. In the normal store case we have special opcodes for
        // compressed fields and the backend decides whether to write 32 or 64
        // bits. However, for pairs this does not make sense, since the
        // paired values could have different representations (e.g.,
        // compressed paired with word32). Therefore, we decide on the actual
        // machine representation already in instruction selection.
#ifdef V8_COMPRESS_POINTERS
        static_assert(ElementSizeLog2Of(MachineRepresentation::kTagged) == 2);
        opcode = kArm64StrWPair;
#else
        static_assert(ElementSizeLog2Of(MachineRepresentation::kTagged) == 3);
        opcode = kArm64StrPair;
#endif
      } else {
        opcode = kArm64StrCompressTagged;
      }
      immediate_mode =
          COMPRESS_POINTERS_BOOL ? kLoadStoreImm32 : kLoadStoreImm64;
      break;
    case MachineRepresentation::kIndirectPointer:
      opcode = kArm64StrIndirectPointer;
      immediate_mode = kLoadStoreImm32;
      break;
    case MachineRepresentation::kSandboxedPointer:
      CHECK(!paired);
      opcode = kArm64StrEncodeSandboxedPointer;
      immediate_mode = kLoadStoreImm64;
      break;
    case MachineRepresentation::kWord64:
      opcode = paired ? kArm64StrPair : kArm64Str;
      immediate_mode = kLoadStoreImm64;
      break;
    case MachineRepresentation::kSimd128:
      CHECK(!paired);
      opcode = kArm64StrQ;
      immediate_mode = kNoImmediate;
      break;
    case MachineRepresentation::kSimd256:
    case MachineRepresentation::kMapWord:
    case MachineRepresentation::kProtectedPointer:
      // We never store directly to protected pointers from generated code.
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
  return std::tuple{opcode, immediate_mode};
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTraceInstruction(node_t node) {}

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
    Arm64OperandGeneratorT<Adapter> g(this);
    Emit(kArchAbortCSADcheck, g.NoOutput(), g.UseFixed(node->InputAt(0), x1));
  }
}

template <typename Adapter>
void EmitLoad(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, InstructionCode opcode,
              ImmediateMode immediate_mode, MachineRepresentation rep,
              typename Adapter::node_t output = typename Adapter::node_t{}) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  InstructionOperand inputs[3];
  size_t input_count = 0;
  InstructionOperand outputs[1];

  // If output is not nullptr, use that as the output register. This
  // is used when we merge a conversion into the load.
  outputs[0] = g.DefineAsRegister(output == nullptr ? node : output);

  ExternalReferenceMatcher m(base);
  if (m.HasResolvedValue() && g.IsIntegerConstant(index) &&
      selector->CanAddressRelativeToRootsRegister(m.ResolvedValue())) {
    ptrdiff_t const delta =
        g.GetIntegerConstantValue(index) +
        MacroAssemblerBase::RootRegisterOffsetForExternalReference(
            selector->isolate(), m.ResolvedValue());
    input_count = 1;
    // Check that the delta is a 32-bit integer due to the limitations of
    // immediate operands.
    if (is_int32(delta)) {
      inputs[0] = g.UseImmediate(static_cast<int32_t>(delta));
      opcode |= AddressingModeField::encode(kMode_Root);
      selector->Emit(opcode, arraysize(outputs), outputs, input_count, inputs);
      return;
    }
  }

  if (base->opcode() == IrOpcode::kLoadRootRegister) {
    input_count = 1;
    inputs[0] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Root);
    selector->Emit(opcode, arraysize(outputs), outputs, input_count, inputs);
    return;
  }

  inputs[0] = g.UseRegister(base);

  if (g.CanBeImmediate(index, immediate_mode)) {
    input_count = 2;
    inputs[1] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_MRI);
  } else if (TryMatchLoadStoreShift(&g, selector, rep, node, index, &inputs[1],
                                    &inputs[2])) {
    input_count = 3;
    opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
  } else {
    input_count = 2;
    inputs[1] = g.UseRegister(index);
    opcode |= AddressingModeField::encode(kMode_MRR);
  }

  selector->Emit(opcode, arraysize(outputs), outputs, input_count, inputs);
}

template <>
void EmitLoad(InstructionSelectorT<TurboshaftAdapter>* selector,
              typename TurboshaftAdapter::node_t node, InstructionCode opcode,
              ImmediateMode immediate_mode, MachineRepresentation rep,
              typename TurboshaftAdapter::node_t output) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const LoadOp& load = selector->Get(node).Cast<LoadOp>();

  // The LoadStoreSimplificationReducer transforms all loads into
  // *(base + index).
  OpIndex base = load.base();
  OpIndex index = load.index().value();
  DCHECK_EQ(load.offset, 0);
  DCHECK_EQ(load.element_size_log2, 0);

  InstructionOperand inputs[3];
  size_t input_count = 0;
  InstructionOperand output_op;

  // If output is valid, use that as the output register. This is used when we
  // merge a conversion into the load.
  output_op = g.DefineAsRegister(output.valid() ? output : node);

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
      // Check that the delta is a 32-bit integer due to the limitations of
      // immediate operands.
      if (is_int32(delta)) {
        inputs[0] = g.UseImmediate(static_cast<int32_t>(delta));
        opcode |= AddressingModeField::encode(kMode_Root);
        selector->Emit(opcode, 1, &output_op, input_count, inputs);
        return;
      }
    }
  }

  if (base_op.Is<LoadRootRegisterOp>()) {
    DCHECK(selector->is_integer_constant(index));
    input_count = 1;
    inputs[0] = g.UseImmediate64(selector->integer_constant(index));
    opcode |= AddressingModeField::encode(kMode_Root);
    selector->Emit(opcode, 1, &output_op, input_count, inputs);
    return;
  }

  inputs[0] = g.UseRegister(base);

  if (selector->is_integer_constant(index)) {
    int64_t offset = selector->integer_constant(index);
    if (g.CanBeImmediate(offset, immediate_mode)) {
      input_count = 2;
      inputs[1] = g.UseImmediate64(offset);
      opcode |= AddressingModeField::encode(kMode_MRI);
    } else {
      input_count = 2;
      inputs[1] = g.UseRegister(index);
      opcode |= AddressingModeField::encode(kMode_MRR);
    }
  } else {
    if (TryMatchLoadStoreShift(&g, selector, rep, node, index, &inputs[1],
                               &inputs[2])) {
      input_count = 3;
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
    } else {
      input_count = 2;
      inputs[1] = g.UseRegister(index);
      opcode |= AddressingModeField::encode(kMode_MRR);
    }
  }
  selector->Emit(opcode, 1, &output_op, input_count, inputs);
}

namespace {
// Manually add base and index into a register to get the actual address.
// This should be used prior to instructions that only support
// immediate/post-index addressing, like ld1 and st1.
template <typename Adapter>
InstructionOperand EmitAddBeforeLoadOrStore(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t node,
    InstructionCode* opcode) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  InstructionOperand addr = g.TempRegister();
  selector->Emit(kArm64Add, addr, g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)));
  *opcode |= AddressingModeField::encode(kMode_MRI);
  return addr;
}
}  // namespace

#if V8_ENABLE_WEBASSEMBLY
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LaneMemoryOp& load = this->Get(node).Cast<Simd128LaneMemoryOp>();
  InstructionCode opcode = kArm64LoadLane;
  opcode |= LaneSizeField::encode(load.lane_size() * kBitsPerByte);
  if (load.kind.with_trap_handler) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand addr = EmitAddBeforeLoadOrStore(this, node, &opcode);
  Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(input_at(node, 2)),
       g.UseImmediate(load.lane), addr, g.TempImmediate(0));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadLane(Node* node) {
  LoadLaneParameters params = LoadLaneParametersOf(node->op());
  DCHECK(
      params.rep == MachineType::Int8() || params.rep == MachineType::Int16() ||
      params.rep == MachineType::Int32() || params.rep == MachineType::Int64());

  InstructionCode opcode = kArm64LoadLane;
  opcode |= LaneSizeField::encode(params.rep.MemSize() * kBitsPerByte);
  if (params.kind == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand addr = EmitAddBeforeLoadOrStore(this, node, &opcode);
  Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(node->InputAt(2)),
       g.UseImmediate(params.laneidx), addr, g.TempImmediate(0));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitStoreLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LaneMemoryOp& store = Get(node).Cast<Simd128LaneMemoryOp>();
  InstructionCode opcode = kArm64StoreLane;
  opcode |= LaneSizeField::encode(store.lane_size() * kBitsPerByte);
  if (store.kind.with_trap_handler) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand addr = EmitAddBeforeLoadOrStore(this, node, &opcode);
  InstructionOperand inputs[4] = {
      g.UseRegister(input_at(node, 2)),
      g.UseImmediate(store.lane),
      addr,
      g.TempImmediate(0),
  };

  Emit(opcode, 0, nullptr, 4, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitStoreLane(Node* node) {
  StoreLaneParameters params = StoreLaneParametersOf(node->op());
  DCHECK_LE(MachineRepresentation::kWord8, params.rep);
  DCHECK_GE(MachineRepresentation::kWord64, params.rep);

  InstructionCode opcode = kArm64StoreLane;
  opcode |=
      LaneSizeField::encode(ElementSizeInBytes(params.rep) * kBitsPerByte);
  if (params.kind == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand addr = EmitAddBeforeLoadOrStore(this, node, &opcode);
  InstructionOperand inputs[4] = {
      g.UseRegister(node->InputAt(2)),
      g.UseImmediate(params.laneidx),
      addr,
      g.TempImmediate(0),
  };

  Emit(opcode, 0, nullptr, 4, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadTransform(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LoadTransformOp& op =
      this->Get(node).Cast<Simd128LoadTransformOp>();
  InstructionCode opcode = kArchNop;
  bool require_add = false;
  switch (op.transform_kind) {
    case Simd128LoadTransformOp::TransformKind::k8Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(8);
      require_add = true;
      break;
    case Simd128LoadTransformOp::TransformKind::k16Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(16);
      require_add = true;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(32);
      require_add = true;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(64);
      require_add = true;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8S:
      opcode = kArm64S128Load8x8S;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8U:
      opcode = kArm64S128Load8x8U;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4S:
      opcode = kArm64S128Load16x4S;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4U:
      opcode = kArm64S128Load16x4U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2S:
      opcode = kArm64S128Load32x2S;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2U:
      opcode = kArm64S128Load32x2U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Zero:
      opcode = kArm64LdrS;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Zero:
      opcode = kArm64LdrD;
      break;
    default:
      UNIMPLEMENTED();
  }
  // ARM64 supports unaligned loads
  DCHECK(!op.load_kind.maybe_unaligned);

  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  node_t base = input_at(node, 0);
  node_t index = input_at(node, 1);
  InstructionOperand inputs[2];
  InstructionOperand outputs[1];

  inputs[0] = g.UseRegister(base);
  inputs[1] = g.UseRegister(index);
  outputs[0] = g.DefineAsRegister(node);

  if (require_add) {
    // ld1r uses post-index, so construct address first.
    // TODO(v8:9886) If index can be immediate, use vldr without this add.
    inputs[0] = EmitAddBeforeLoadOrStore(this, node, &opcode);
    inputs[1] = g.TempImmediate(0);
    opcode |= AddressingModeField::encode(kMode_MRI);
  } else {
    opcode |= AddressingModeField::encode(kMode_MRR);
  }
  if (op.load_kind.with_trap_handler) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  Emit(opcode, 1, outputs, 2, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadTransform(Node* node) {
  LoadTransformParameters params = LoadTransformParametersOf(node->op());
  InstructionCode opcode = kArchNop;
  bool require_add = false;
  switch (params.transformation) {
    case LoadTransformation::kS128Load8Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(8);
      require_add = true;
      break;
    case LoadTransformation::kS128Load16Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(16);
      require_add = true;
      break;
    case LoadTransformation::kS128Load32Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(32);
      require_add = true;
      break;
    case LoadTransformation::kS128Load64Splat:
      opcode = kArm64LoadSplat;
      opcode |= LaneSizeField::encode(64);
      require_add = true;
      break;
    case LoadTransformation::kS128Load8x8S:
      opcode = kArm64S128Load8x8S;
      break;
    case LoadTransformation::kS128Load8x8U:
      opcode = kArm64S128Load8x8U;
      break;
    case LoadTransformation::kS128Load16x4S:
      opcode = kArm64S128Load16x4S;
      break;
    case LoadTransformation::kS128Load16x4U:
      opcode = kArm64S128Load16x4U;
      break;
    case LoadTransformation::kS128Load32x2S:
      opcode = kArm64S128Load32x2S;
      break;
    case LoadTransformation::kS128Load32x2U:
      opcode = kArm64S128Load32x2U;
      break;
    case LoadTransformation::kS128Load32Zero:
      opcode = kArm64LdrS;
      break;
    case LoadTransformation::kS128Load64Zero:
      opcode = kArm64LdrD;
      break;
    default:
      UNIMPLEMENTED();
  }
  // ARM64 supports unaligned loads
  DCHECK_NE(params.kind, MemoryAccessKind::kUnaligned);

  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Node* base = node->InputAt(0);
  Node* index = node->InputAt(1);
  InstructionOperand inputs[2];
  InstructionOperand outputs[1];

  inputs[0] = g.UseRegister(base);
  inputs[1] = g.UseRegister(index);
  outputs[0] = g.DefineAsRegister(node);

  if (require_add) {
    // ld1r uses post-index, so construct address first.
    // TODO(v8:9886) If index can be immediate, use vldr without this add.
    inputs[0] = EmitAddBeforeLoadOrStore(this, node, &opcode);
    inputs[1] = g.TempImmediate(0);
    opcode |= AddressingModeField::encode(kMode_MRI);
  } else {
    opcode |= AddressingModeField::encode(kMode_MRR);
  }
  if (params.kind == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  Emit(opcode, 1, outputs, 2, inputs);
}
#endif  // V8_ENABLE_WEBASSEMBLY

std::tuple<InstructionCode, ImmediateMode> GetLoadOpcodeAndImmediate(
    turboshaft::MemoryRepresentation loaded_rep,
    turboshaft::RegisterRepresentation result_rep) {
  // NOTE: The meaning of `loaded_rep` = `MemoryRepresentation::AnyTagged()` is
  // we are loading a compressed tagged field, while `result_rep` =
  // `RegisterRepresentation::Tagged()` refers to an uncompressed tagged value.
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (loaded_rep) {
    case MemoryRepresentation::Int8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return {kArm64LdrsbW, kLoadStoreImm8};
    case MemoryRepresentation::Uint8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return {kArm64Ldrb, kLoadStoreImm8};
    case MemoryRepresentation::Int16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return {kArm64LdrshW, kLoadStoreImm16};
    case MemoryRepresentation::Uint16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return {kArm64Ldrh, kLoadStoreImm16};
    case MemoryRepresentation::Int32():
    case MemoryRepresentation::Uint32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return {kArm64LdrW, kLoadStoreImm32};
    case MemoryRepresentation::Int64():
    case MemoryRepresentation::Uint64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word64());
      return {kArm64Ldr, kLoadStoreImm64};
    case MemoryRepresentation::Float16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float32());
      return {kArm64LdrH, kLoadStoreImm16};
    case MemoryRepresentation::Float32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float32());
      return {kArm64LdrS, kLoadStoreImm32};
    case MemoryRepresentation::Float64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float64());
      return {kArm64LdrD, kLoadStoreImm64};
#ifdef V8_COMPRESS_POINTERS
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
      if (result_rep == RegisterRepresentation::Compressed()) {
        return {kArm64LdrW, kLoadStoreImm32};
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return {kArm64LdrDecompressTagged, kLoadStoreImm32};
    case MemoryRepresentation::TaggedSigned():
      if (result_rep == RegisterRepresentation::Compressed()) {
        return {kArm64LdrW, kLoadStoreImm32};
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return {kArm64LdrDecompressTaggedSigned, kLoadStoreImm32};
#else
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
    case MemoryRepresentation::TaggedSigned():
      return {kArm64Ldr, kLoadStoreImm64};
#endif
    case MemoryRepresentation::AnyUncompressedTagged():
    case MemoryRepresentation::UncompressedTaggedPointer():
    case MemoryRepresentation::UncompressedTaggedSigned():
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return {kArm64Ldr, kLoadStoreImm64};
    case MemoryRepresentation::ProtectedPointer():
      CHECK(V8_ENABLE_SANDBOX_BOOL);
      return {kArm64LdrDecompressProtected, kNoImmediate};
    case MemoryRepresentation::IndirectPointer():
      UNREACHABLE();
    case MemoryRepresentation::SandboxedPointer():
      return {kArm64LdrDecodeSandboxedPointer, kLoadStoreImm64};
    case MemoryRepresentation::Simd128():
      return {kArm64LdrQ, kNoImmediate};
    case MemoryRepresentation::Simd256():
      UNREACHABLE();
  }
}

std::tuple<InstructionCode, ImmediateMode> GetLoadOpcodeAndImmediate(
    LoadRepresentation load_rep) {
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat16:
      return {kArm64LdrH, kLoadStoreImm16};
    case MachineRepresentation::kFloat32:
      return {kArm64LdrS, kLoadStoreImm32};
    case MachineRepresentation::kFloat64:
      return {kArm64LdrD, kLoadStoreImm64};
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      return {load_rep.IsUnsigned()                            ? kArm64Ldrb
              : load_rep.semantic() == MachineSemantic::kInt32 ? kArm64LdrsbW
                                                               : kArm64Ldrsb,
              kLoadStoreImm8};
    case MachineRepresentation::kWord16:
      return {load_rep.IsUnsigned()                            ? kArm64Ldrh
              : load_rep.semantic() == MachineSemantic::kInt32 ? kArm64LdrshW
                                                               : kArm64Ldrsh,
              kLoadStoreImm16};
    case MachineRepresentation::kWord32:
      return {kArm64LdrW, kLoadStoreImm32};
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
      return {kArm64LdrW, kLoadStoreImm32};
#else
      UNREACHABLE();
#endif
#ifdef V8_COMPRESS_POINTERS
    case MachineRepresentation::kTaggedSigned:
      return {kArm64LdrDecompressTaggedSigned, kLoadStoreImm32};
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      return {kArm64LdrDecompressTagged, kLoadStoreImm32};
#else
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
#endif
    case MachineRepresentation::kWord64:
      return {kArm64Ldr, kLoadStoreImm64};
    case MachineRepresentation::kProtectedPointer:
      CHECK(V8_ENABLE_SANDBOX_BOOL);
      return {kArm64LdrDecompressProtected, kNoImmediate};
    case MachineRepresentation::kSandboxedPointer:
      return {kArm64LdrDecodeSandboxedPointer, kLoadStoreImm64};
    case MachineRepresentation::kSimd128:
      return {kArm64LdrQ, kNoImmediate};
    case MachineRepresentation::kSimd256:  // Fall through.
    case MachineRepresentation::kMapWord:  // Fall through.
    case MachineRepresentation::kIndirectPointer:  // Fall through.
    case MachineRepresentation::kNone:
      UNREACHABLE();
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node) {
  InstructionCode opcode = kArchNop;
  ImmediateMode immediate_mode = kNoImmediate;
  auto load = this->load_view(node);
  LoadRepresentation load_rep = load.loaded_rep();
  MachineRepresentation rep = load_rep.representation();
  if constexpr (Adapter::IsTurboshaft) {
    std::tie(opcode, immediate_mode) =
        GetLoadOpcodeAndImmediate(load.ts_loaded_rep(), load.ts_result_rep());
  } else {
    std::tie(opcode, immediate_mode) = GetLoadOpcodeAndImmediate(load_rep);
  }
  bool traps_on_null;
  if (load.is_protected(&traps_on_null)) {
    if (traps_on_null) {
      opcode |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
    } else {
      opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
    }
  }
  EmitLoad(this, node, opcode, immediate_mode, rep);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  VisitLoad(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    auto rep_pair = StorePairRepresentationOf(node->op());
    CHECK_EQ(rep_pair.first.write_barrier_kind(), kNoWriteBarrier);
    CHECK_EQ(rep_pair.second.write_barrier_kind(),
             rep_pair.first.write_barrier_kind());
    DCHECK(!v8_flags.enable_unconditional_write_barriers);

    InstructionOperand inputs[4];
    size_t input_count = 0;

    MachineRepresentation approx_rep;
    auto info1 =
        GetStoreOpcodeAndImmediate(rep_pair.first.representation(), true);
    auto info2 =
        GetStoreOpcodeAndImmediate(rep_pair.second.representation(), true);
    CHECK_EQ(ElementSizeLog2Of(rep_pair.first.representation()),
             ElementSizeLog2Of(rep_pair.second.representation()));
    switch (ElementSizeLog2Of(rep_pair.first.representation())) {
      case 2:
        approx_rep = MachineRepresentation::kWord32;
        break;
      case 3:
        approx_rep = MachineRepresentation::kWord64;
        break;
      default:
        UNREACHABLE();
    }
    InstructionCode opcode = std::get<InstructionCode>(info1);
    ImmediateMode immediate_mode = std::get<ImmediateMode>(info1);
    CHECK_EQ(opcode, std::get<InstructionCode>(info2));
    CHECK_EQ(immediate_mode, std::get<ImmediateMode>(info2));

    node_t base = this->input_at(node, 0);
    node_t index = this->input_at(node, 1);
    node_t value = this->input_at(node, 2);

    inputs[input_count++] = g.UseRegisterOrImmediateZero(value);
    inputs[input_count++] =
        g.UseRegisterOrImmediateZero(this->input_at(node, 3));

    if (this->is_load_root_register(base)) {
      inputs[input_count++] = g.UseImmediate(index);
      opcode |= AddressingModeField::encode(kMode_Root);
      Emit(opcode, 0, nullptr, input_count, inputs);
      return;
    }

    inputs[input_count++] = g.UseRegister(base);

    if (g.CanBeImmediate(index, immediate_mode)) {
      inputs[input_count++] = g.UseImmediate(index);
      opcode |= AddressingModeField::encode(kMode_MRI);
    } else if (TryMatchLoadStoreShift(&g, this, approx_rep, node, index,
                                      &inputs[input_count],
                                      &inputs[input_count + 1])) {
      input_count += 2;
      opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
    } else {
      inputs[input_count++] = g.UseRegister(index);
      opcode |= AddressingModeField::encode(kMode_MRR);
    }

    Emit(opcode, 0, nullptr, input_count, inputs);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(typename Adapter::node_t node) {
  typename Adapter::StoreView store_view = this->store_view(node);
  DCHECK_EQ(store_view.displacement(), 0);
  WriteBarrierKind write_barrier_kind =
      store_view.stored_rep().write_barrier_kind();
  const MachineRepresentation representation =
      store_view.stored_rep().representation();

  Arm64OperandGeneratorT<Adapter> g(this);

  // TODO(arm64): I guess this could be done in a better way.
  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedOrCompressedOrIndirectPointer(representation));
    AddressingMode addressing_mode;
    InstructionOperand inputs[4];
    size_t input_count = 0;
    inputs[input_count++] = g.UseUniqueRegister(store_view.base());
    // OutOfLineRecordWrite uses the index in an add or sub instruction, but we
    // can trust the assembler to generate extra instructions if the index does
    // not fit into add or sub. So here only check the immediate for a store.
    node_t index = this->value(store_view.index());
    if (g.CanBeImmediate(index, COMPRESS_POINTERS_BOOL ? kLoadStoreImm32
                                                       : kLoadStoreImm64)) {
      inputs[input_count++] = g.UseImmediate(index);
      addressing_mode = kMode_MRI;
    } else {
      inputs[input_count++] = g.UseUniqueRegister(index);
      addressing_mode = kMode_MRR;
    }
    inputs[input_count++] = g.UseUniqueRegister(store_view.value());
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionCode code;
    if (representation == MachineRepresentation::kIndirectPointer) {
      DCHECK_EQ(write_barrier_kind, kIndirectPointerWriteBarrier);
      // In this case we need to add the IndirectPointerTag as additional input.
      code = kArchStoreIndirectWithWriteBarrier;
      IndirectPointerTag tag = store_view.indirect_pointer_tag();
      inputs[input_count++] = g.UseImmediate64(static_cast<int64_t>(tag));
    } else {
      code = kArchStoreWithWriteBarrier;
    }
    code |= AddressingModeField::encode(addressing_mode);
    code |= RecordWriteModeField::encode(record_write_mode);
    if (store_view.is_store_trap_on_null()) {
      code |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
    }
    Emit(code, 0, nullptr, input_count, inputs);
    return;
  }

  InstructionOperand inputs[4];
  size_t input_count = 0;

  MachineRepresentation approx_rep = representation;
  InstructionCode opcode;
  ImmediateMode immediate_mode;
  if constexpr (Adapter::IsTurboshaft) {
    std::tie(opcode, immediate_mode) =
        GetStoreOpcodeAndImmediate(store_view.ts_stored_rep(), false);
  } else {
    std::tie(opcode, immediate_mode) =
        GetStoreOpcodeAndImmediate(approx_rep, false);
  }

  if (v8_flags.enable_unconditional_write_barriers) {
    if (CanBeTaggedOrCompressedPointer(representation)) {
      write_barrier_kind = kFullWriteBarrier;
    }
  }

  std::optional<ExternalReference> external_base;
  if constexpr (Adapter::IsTurboshaft) {
    ExternalReference value;
    if (this->MatchExternalConstant(store_view.base(), &value)) {
      external_base = value;
    }
  } else {
    ExternalReferenceMatcher m(store_view.base());
    if (m.HasResolvedValue()) {
      external_base = m.ResolvedValue();
    }
  }

  std::optional<int64_t> constant_index;
  if (this->valid(store_view.index())) {
    node_t index = this->value(store_view.index());
    constant_index = g.GetOptionalIntegerConstant(index);
  }
  if (external_base.has_value() && constant_index.has_value() &&
      CanAddressRelativeToRootsRegister(*external_base)) {
    ptrdiff_t const delta =
        *constant_index +
        MacroAssemblerBase::RootRegisterOffsetForExternalReference(
            isolate(), *external_base);
    if (is_int32(delta)) {
      input_count = 2;
      InstructionOperand inputs[2];
      inputs[0] = g.UseRegister(store_view.value());
      inputs[1] = g.UseImmediate(static_cast<int32_t>(delta));
      opcode |= AddressingModeField::encode(kMode_Root);
      Emit(opcode, 0, nullptr, input_count, inputs);
      return;
    }
  }

  node_t base = store_view.base();
  node_t index = this->value(store_view.index());

  inputs[input_count++] = g.UseRegisterOrImmediateZero(store_view.value());

  if (this->is_load_root_register(base)) {
    inputs[input_count++] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_Root);
    Emit(opcode, 0, nullptr, input_count, inputs);
    return;
  }

  inputs[input_count++] = g.UseRegister(base);

  if (g.CanBeImmediate(index, immediate_mode)) {
    inputs[input_count++] = g.UseImmediate(index);
    opcode |= AddressingModeField::encode(kMode_MRI);
  } else if (TryMatchLoadStoreShift(&g, this, approx_rep, node, index,
                                    &inputs[input_count],
                                    &inputs[input_count + 1])) {
    input_count += 2;
    opcode |= AddressingModeField::encode(kMode_Operand2_R_LSL_I);
  } else {
    inputs[input_count++] = g.UseRegister(index);
    opcode |= AddressingModeField::encode(kMode_MRR);
  }

  if (store_view.is_store_trap_on_null()) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
  } else if (store_view.access_kind() == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  Emit(opcode, 0, nullptr, input_count, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  VisitStore(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  UNREACHABLE();
}

// Architecture supports unaligned access, therefore VisitLoad is used instead
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedLoad(node_t node) {
  UNREACHABLE();
}

// Architecture supports unaligned access, therefore VisitStore is used instead
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUnalignedStore(node_t node) {
  UNREACHABLE();
}

namespace turboshaft {

class CompareSequence {
 public:
  void InitialCompare(OpIndex op, OpIndex l, OpIndex r,
                      RegisterRepresentation rep) {
    DCHECK(!HasCompare());
    cmp_ = op;
    left_ = l;
    right_ = r;
    opcode_ = GetOpcode(rep);
  }
  bool HasCompare() const { return cmp_.valid(); }
  OpIndex cmp() const { return cmp_; }
  OpIndex left() const { return left_; }
  OpIndex right() const { return right_; }
  InstructionCode opcode() const { return opcode_; }
  uint32_t num_ccmps() const { return num_ccmps_; }
  FlagsContinuationT<TurboshaftAdapter>::compare_chain_t& ccmps() {
    return ccmps_;
  }
  void AddConditionalCompare(RegisterRepresentation rep,
                             FlagsCondition ccmp_condition,
                             FlagsCondition default_flags, OpIndex ccmp_lhs,
                             OpIndex ccmp_rhs) {
    InstructionCode code = GetOpcode(rep);
    ccmps_.at(num_ccmps_) =
        FlagsContinuationT<TurboshaftAdapter>::ConditionalCompare{
            code, ccmp_condition, default_flags, ccmp_lhs, ccmp_rhs};
    ++num_ccmps_;
  }

 private:
  InstructionCode GetOpcode(RegisterRepresentation rep) const {
    if (rep == RegisterRepresentation::Word32()) {
      return kArm64Cmp32;
    } else {
      DCHECK_EQ(rep, RegisterRepresentation::Word64());
      return kArm64Cmp;
    }
  }

  OpIndex cmp_;
  OpIndex left_;
  OpIndex right_;
  InstructionCode opcode_;
  FlagsContinuationT<TurboshaftAdapter>::compare_chain_t ccmps_;
  uint32_t num_ccmps_ = 0;
};

class CompareChainNode final : public ZoneObject {
 public:
  enum class NodeKind : uint8_t { kFlagSetting, kLogicalCombine };

  explicit CompareChainNode(OpIndex n, FlagsCondition condition)
      : node_kind_(NodeKind::kFlagSetting),
        user_condition_(condition),
        node_(n) {}

  explicit CompareChainNode(OpIndex n, CompareChainNode* l, CompareChainNode* r)
      : node_kind_(NodeKind::kLogicalCombine), node_(n), lhs_(l), rhs_(r) {
    // Canonicalise the chain with cmps on the right.
    if (lhs_->IsFlagSetting() && !rhs_->IsFlagSetting()) {
      std::swap(lhs_, rhs_);
    }
  }
  void SetCondition(FlagsCondition condition) {
    DCHECK(IsLogicalCombine());
    user_condition_ = condition;
    if (requires_negation_) {
      NegateFlags();
    }
  }
  void MarkRequiresNegation() {
    if (IsFlagSetting()) {
      NegateFlags();
    } else {
      requires_negation_ = !requires_negation_;
    }
  }
  void NegateFlags() {
    user_condition_ = NegateFlagsCondition(user_condition_);
    requires_negation_ = false;
  }
  void CommuteFlags() {
    user_condition_ = CommuteFlagsCondition(user_condition_);
  }
  bool IsLegalFirstCombine() const {
    DCHECK(IsLogicalCombine());
    // We need two cmps feeding the first logic op.
    return lhs_->IsFlagSetting() && rhs_->IsFlagSetting();
  }
  bool IsFlagSetting() const { return node_kind_ == NodeKind::kFlagSetting; }
  bool IsLogicalCombine() const {
    return node_kind_ == NodeKind::kLogicalCombine;
  }
  OpIndex node() const { return node_; }
  FlagsCondition user_condition() const { return user_condition_; }
  CompareChainNode* lhs() const {
    DCHECK(IsLogicalCombine());
    return lhs_;
  }
  CompareChainNode* rhs() const {
    DCHECK(IsLogicalCombine());
    return rhs_;
  }

 private:
  NodeKind node_kind_;
  FlagsCondition user_condition_;
  bool requires_negation_ = false;
  OpIndex node_;
  CompareChainNode* lhs_ = nullptr;
  CompareChainNode* rhs_ = nullptr;
};

static std::optional<FlagsCondition> GetFlagsCondition(
    OpIndex node, InstructionSelectorT<TurboshaftAdapter>* selector) {
  if (const ComparisonOp* comparison =
          selector->Get(node).TryCast<ComparisonOp>()) {
    if (comparison->rep == RegisterRepresentation::Word32() ||
        comparison->rep == RegisterRepresentation::Word64()) {
      switch (comparison->kind) {
        case ComparisonOp::Kind::kEqual:
          return FlagsCondition::kEqual;
        case ComparisonOp::Kind::kSignedLessThan:
          return FlagsCondition::kSignedLessThan;
        case ComparisonOp::Kind::kSignedLessThanOrEqual:
          return FlagsCondition::kSignedLessThanOrEqual;
        case ComparisonOp::Kind::kUnsignedLessThan:
          return FlagsCondition::kUnsignedLessThan;
        case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
          return FlagsCondition::kUnsignedLessThanOrEqual;
        default:
          UNREACHABLE();
      }
    }
  }
  return std::nullopt;
}

// Search through AND, OR and comparisons.
// To make life a little easier, we currently don't handle combining two logic
// operations. There are restrictions on what logical combinations can be
// performed with ccmp, so this implementation builds a ccmp chain from the LHS
// of the tree while combining one more compare from the RHS at each step. So,
// currently, if we discover a pattern like this:
//   logic(logic(cmp, cmp), logic(cmp, cmp))
// The search will fail from the outermost logic operation, but it will succeed
// for the two inner operations. This will result in, suboptimal, codegen:
//   cmp
//   ccmp
//   cset x
//   cmp
//   ccmp
//   cset y
//   logic x, y
static std::optional<CompareChainNode*> FindCompareChain(
    OpIndex user, OpIndex node,
    InstructionSelectorT<TurboshaftAdapter>* selector, Zone* zone,
    ZoneVector<CompareChainNode*>& nodes) {
  if (selector->Get(node).Is<Opmask::kWord32BitwiseAnd>() ||
      selector->Get(node).Is<Opmask::kWord32BitwiseOr>()) {
    auto maybe_lhs = FindCompareChain(node, selector->input_at(node, 0),
                                      selector, zone, nodes);
    auto maybe_rhs = FindCompareChain(node, selector->input_at(node, 1),
                                      selector, zone, nodes);
    if (maybe_lhs.has_value() && maybe_rhs.has_value()) {
      CompareChainNode* lhs = maybe_lhs.value();
      CompareChainNode* rhs = maybe_rhs.value();
      // Ensure we don't try to combine a logic operation with two logic inputs.
      if (lhs->IsFlagSetting() || rhs->IsFlagSetting()) {
        nodes.push_back(std::move(zone->New<CompareChainNode>(node, lhs, rhs)));
        return nodes.back();
      }
    }
    // Ensure we remove any valid sub-trees that now cannot be used.
    nodes.clear();
    return std::nullopt;
  } else if (selector->valid(user) && selector->CanCover(user, node)) {
    std::optional<FlagsCondition> user_condition =
        GetFlagsCondition(node, selector);
    if (!user_condition.has_value()) {
      return std::nullopt;
    }
    const ComparisonOp& comparison = selector->Get(node).Cast<ComparisonOp>();
    if (comparison.kind == ComparisonOp::Kind::kEqual &&
        selector->MatchIntegralZero(comparison.right())) {
      auto maybe_negated = FindCompareChain(node, selector->input_at(node, 0),
                                            selector, zone, nodes);
      if (maybe_negated.has_value()) {
        CompareChainNode* negated = maybe_negated.value();
        negated->MarkRequiresNegation();
        return negated;
      }
    }
    return zone->New<CompareChainNode>(node, user_condition.value());
  }
  return std::nullopt;
}

// Overview -------------------------------------------------------------------
//
// A compare operation will generate a 'user condition', which is the
// FlagCondition of the opcode. For this algorithm, we generate the default
// flags from the LHS of the logic op, while the RHS is used to predicate the
// new ccmp. Depending on the logical user, those conditions are either used
// as-is or negated:
// > For OR, the generated ccmp will negate the LHS condition for its predicate
//   while the default flags are taken from the RHS.
// > For AND, the generated ccmp will take the LHS condition for its predicate
//   while the default flags are a negation of the RHS.
//
// The new ccmp will now generate a user condition of its own, and this is
// always forwarded from the RHS.
//
// Chaining compares, including with OR, needs to be equivalent to combining
// all the results with AND, and NOT.
//
// AND Example ----------------------------------------------------------------
//
//  cmpA      cmpB
//   |         |
// condA     condB
//   |         |
//   --- AND ---
//
// As the AND becomes the ccmp, it is predicated on condA and the cset is
// predicated on condB. The user of the ccmp is always predicated on the
// condition from the RHS of the logic operation. The default flags are
// not(condB) so cset only produces one when both condA and condB are true:
//   cmpA
//   ccmpB not(condB), condA
//   cset condB
//
// OR Example -----------------------------------------------------------------
//
//  cmpA      cmpB
//   |         |
// condA     condB
//   |         |
//   --- OR  ---
//
//                    cmpA          cmpB
//   equivalent ->     |             |
//                    not(condA)  not(condB)
//                     |             |
//                     ----- AND -----
//                            |
//                           NOT
//
// In this case, the input conditions to the AND (the ccmp) have been negated
// so the user condition and default flags have been negated compared to the
// previous example. The cset still uses condB because it is negated twice:
//   cmpA
//   ccmpB condB, not(condA)
//   cset condB
//
// Combining AND and OR -------------------------------------------------------
//
//  cmpA      cmpB    cmpC
//   |         |       |
// condA     condB    condC
//   |         |       |
//   --- AND ---       |
//        |            |
//       OR -----------
//
//  equivalent -> cmpA      cmpB      cmpC
//                 |         |         |
//               condA     condB  not(condC)
//                 |         |         |
//                 --- AND ---         |
//                      |              |
//                     NOT             |
//                      |              |
//                     AND -------------
//                      |
//                     NOT
//
// For this example the 'user condition', coming out, of the first ccmp is
// condB but it is negated as the input predicate for the next ccmp as that
// one is performing an OR:
//   cmpA
//   ccmpB not(condB), condA
//   ccmpC condC, not(condB)
//   cset condC
//
void CombineFlagSettingOps(CompareChainNode* logic_node,
                           InstructionSelectorT<TurboshaftAdapter>* selector,
                           CompareSequence* sequence) {
  CompareChainNode* lhs = logic_node->lhs();
  CompareChainNode* rhs = logic_node->rhs();

  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  if (!sequence->HasCompare()) {
    // This is the beginning of the conditional compare chain.
    DCHECK(lhs->IsFlagSetting());
    DCHECK(rhs->IsFlagSetting());
    OpIndex cmp = lhs->node();
    OpIndex ccmp = rhs->node();
    // ccmp has a much smaller immediate range than cmp, so swap the
    // operations if possible.
    if ((g.CanBeImmediate(selector->input_at(cmp, 0), kConditionalCompareImm) ||
         g.CanBeImmediate(selector->input_at(cmp, 1),
                          kConditionalCompareImm)) &&
        (!g.CanBeImmediate(selector->input_at(ccmp, 0),
                           kConditionalCompareImm) &&
         !g.CanBeImmediate(selector->input_at(ccmp, 1),
                           kConditionalCompareImm))) {
      std::swap(lhs, rhs);
      std::swap(cmp, ccmp);
    }

    OpIndex left = selector->input_at(cmp, 0);
    OpIndex right = selector->input_at(cmp, 1);
    if (g.CanBeImmediate(left, kArithmeticImm)) {
      std::swap(left, right);
      lhs->CommuteFlags();
    }
    // Initialize chain with the compare which will hold the continuation.
    RegisterRepresentation rep = selector->Get(cmp).Cast<ComparisonOp>().rep;
    sequence->InitialCompare(cmp, left, right, rep);
  }

  bool is_logical_or =
      selector->Get(logic_node->node()).Is<Opmask::kWord32BitwiseOr>();
  FlagsCondition ccmp_condition =
      is_logical_or ? NegateFlagsCondition(lhs->user_condition())
                    : lhs->user_condition();
  FlagsCondition default_flags =
      is_logical_or ? rhs->user_condition()
                    : NegateFlagsCondition(rhs->user_condition());

  // We canonicalise the chain so that the rhs is always a cmp, whereas lhs
  // will either be the initial cmp or the previous logic, now ccmp, op and
  // only provides ccmp_condition.
  FlagsCondition user_condition = rhs->user_condition();
  OpIndex ccmp = rhs->node();
  OpIndex ccmp_lhs = selector->input_at(ccmp, 0);
  OpIndex ccmp_rhs = selector->input_at(ccmp, 1);

  // Switch ccmp lhs/rhs if lhs is a small immediate.
  if (g.CanBeImmediate(ccmp_lhs, kConditionalCompareImm)) {
    user_condition = CommuteFlagsCondition(user_condition);
    default_flags = CommuteFlagsCondition(default_flags);
    std::swap(ccmp_lhs, ccmp_rhs);
  }

  RegisterRepresentation rep = selector->Get(ccmp).Cast<ComparisonOp>().rep;
  sequence->AddConditionalCompare(rep, ccmp_condition, default_flags, ccmp_lhs,
                                  ccmp_rhs);
  // Ensure the user_condition is kept up-to-date for the next ccmp/cset.
  logic_node->SetCondition(user_condition);
}

static std::optional<FlagsCondition> TryMatchConditionalCompareChainShared(
    InstructionSelectorT<TurboshaftAdapter>* selector, Zone* zone, OpIndex node,
    CompareSequence* sequence) {
  // Instead of:
  //  cmp x0, y0
  //  cset cc0
  //  cmp x1, y1
  //  cset cc1
  //  and/orr
  // Try to merge logical combinations of flags into:
  //  cmp x0, y0
  //  ccmp x1, y1 ..
  //  cset ..
  // So, for AND:
  //  (cset cc1 (ccmp x1 y1 !cc1 cc0 (cmp x0, y0)))
  // and for ORR:
  //  (cset cc1 (ccmp x1 y1 cc1 !cc0 (cmp x0, y0))

  // Look for a potential chain.
  ZoneVector<CompareChainNode*> logic_nodes(zone);
  auto root =
      FindCompareChain(OpIndex::Invalid(), node, selector, zone, logic_nodes);
  if (!root.has_value()) return std::nullopt;

  if (logic_nodes.size() >
      FlagsContinuationT<TurboshaftAdapter>::kMaxCompareChainSize) {
    return std::nullopt;
  }
  if (!logic_nodes.front()->IsLegalFirstCombine()) {
    return std::nullopt;
  }

  for (auto* logic_node : logic_nodes) {
    CombineFlagSettingOps(logic_node, selector, sequence);
  }
  DCHECK_LE(sequence->num_ccmps(),
            FlagsContinuationT<TurboshaftAdapter>::kMaxCompareChainSize);
  return logic_nodes.back()->user_condition();
}

static bool TryMatchConditionalCompareChainBranch(
    InstructionSelectorT<TurboshaftAdapter>* selector, Zone* zone, OpIndex node,
    FlagsContinuationT<TurboshaftAdapter>* cont) {
  if (!cont->IsBranch()) return false;
  DCHECK(cont->condition() == kNotEqual || cont->condition() == kEqual);

  CompareSequence sequence;
  auto final_cond =
      TryMatchConditionalCompareChainShared(selector, zone, node, &sequence);
  if (final_cond.has_value()) {
    FlagsCondition condition = cont->condition() == kNotEqual
                                   ? final_cond.value()
                                   : NegateFlagsCondition(final_cond.value());
    FlagsContinuationT<TurboshaftAdapter> new_cont =
        FlagsContinuationT<TurboshaftAdapter>::ForConditionalBranch(
            sequence.ccmps(), sequence.num_ccmps(), condition,
            cont->true_block(), cont->false_block());

    VisitBinopImpl(selector, sequence.cmp(), sequence.left(), sequence.right(),
                   selector->Get(sequence.cmp()).Cast<ComparisonOp>().rep,
                   sequence.opcode(), kArithmeticImm, &new_cont);

    return true;
  }
  return false;
}

static bool TryMatchConditionalCompareChainSet(
    InstructionSelectorT<TurboshaftAdapter>* selector, Zone* zone,
    OpIndex node) {
  // Create the cmp + ccmp ... sequence.
  CompareSequence sequence;
  auto final_cond =
      TryMatchConditionalCompareChainShared(selector, zone, node, &sequence);
  if (final_cond.has_value()) {
    // The continuation performs the conditional compare and cset.
    FlagsContinuationT<TurboshaftAdapter> cont =
        FlagsContinuationT<TurboshaftAdapter>::ForConditionalSet(
            sequence.ccmps(), sequence.num_ccmps(), final_cond.value(), node);

    VisitBinopImpl(selector, sequence.cmp(), sequence.left(), sequence.right(),
                   selector->Get(sequence.cmp()).Cast<ComparisonOp>().rep,
                   sequence.opcode(), kArithmeticImm, &cont);
    return true;
  }
  return false;
}

}  // end namespace turboshaft

template <typename Adapter, typename Matcher>
static void VisitLogical(InstructionSelectorT<Adapter>* selector, Node* node,
                         Matcher* m, ArchOpcode opcode, bool left_can_cover,
                         bool right_can_cover, ImmediateMode imm_mode) {
  Arm64OperandGeneratorT<Adapter> g(selector);

  // Map instruction to equivalent operation with inverted right input.
  ArchOpcode inv_opcode = opcode;
  switch (opcode) {
    case kArm64And32:
      inv_opcode = kArm64Bic32;
      break;
    case kArm64And:
      inv_opcode = kArm64Bic;
      break;
    case kArm64Or32:
      inv_opcode = kArm64Orn32;
      break;
    case kArm64Or:
      inv_opcode = kArm64Orn;
      break;
    case kArm64Eor32:
      inv_opcode = kArm64Eon32;
      break;
    case kArm64Eor:
      inv_opcode = kArm64Eon;
      break;
    default:
      UNREACHABLE();
  }

  // Select Logical(y, ~x) for Logical(Xor(x, -1), y).
  if ((m->left().IsWord32Xor() || m->left().IsWord64Xor()) && left_can_cover) {
    Matcher mleft(m->left().node());
    if (mleft.right().Is(-1)) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->right().node()),
                     g.UseRegister(mleft.left().node()));
      return;
    }
  }

  // Select Logical(x, ~y) for Logical(x, Xor(y, -1)).
  if ((m->right().IsWord32Xor() || m->right().IsWord64Xor()) &&
      right_can_cover) {
    Matcher mright(m->right().node());
    if (mright.right().Is(-1)) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(m->left().node()),
                     g.UseRegister(mright.left().node()));
      return;
    }
  }

  if (m->IsWord32Xor() && m->right().Is(-1)) {
    selector->Emit(kArm64Not32, g.DefineAsRegister(node),
                   g.UseRegister(m->left().node()));
  } else if (m->IsWord64Xor() && m->right().Is(-1)) {
    selector->Emit(kArm64Not, g.DefineAsRegister(node),
                   g.UseRegister(m->left().node()));
  } else {
    VisitBinop<Adapter, Matcher>(selector, node, opcode, imm_mode);
  }
}

static void VisitLogical(InstructionSelectorT<TurboshaftAdapter>* selector,
                         Zone* zone, turboshaft::OpIndex node,
                         turboshaft::WordRepresentation rep, ArchOpcode opcode,
                         bool left_can_cover, bool right_can_cover,
                         ImmediateMode imm_mode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const WordBinopOp& logical_op = selector->Get(node).Cast<WordBinopOp>();
  const Operation& lhs = selector->Get(logical_op.left());
  const Operation& rhs = selector->Get(logical_op.right());

  // Map instruction to equivalent operation with inverted right input.
  ArchOpcode inv_opcode = opcode;
  switch (opcode) {
    case kArm64And32:
      inv_opcode = kArm64Bic32;
      break;
    case kArm64And:
      inv_opcode = kArm64Bic;
      break;
    case kArm64Or32:
      inv_opcode = kArm64Orn32;
      break;
    case kArm64Or:
      inv_opcode = kArm64Orn;
      break;
    case kArm64Eor32:
      inv_opcode = kArm64Eon32;
      break;
    case kArm64Eor:
      inv_opcode = kArm64Eon;
      break;
    default:
      UNREACHABLE();
  }

  if (turboshaft::TryMatchConditionalCompareChainSet(selector, zone, node)) {
    return;
  }

  // Select Logical(y, ~x) for Logical(Xor(x, -1), y).
  if (lhs.Is<Opmask::kBitwiseXor>() && left_can_cover) {
    const WordBinopOp& xor_op = lhs.Cast<WordBinopOp>();
    int64_t xor_rhs_val;
    if (selector->MatchSignedIntegralConstant(xor_op.right(), &xor_rhs_val) &&
        xor_rhs_val == -1) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(logical_op.right()),
                     g.UseRegister(xor_op.left()));
      return;
    }
  }

  // Select Logical(x, ~y) for Logical(x, Xor(y, -1)).
  if (rhs.Is<Opmask::kBitwiseXor>() && right_can_cover) {
    const WordBinopOp& xor_op = rhs.Cast<WordBinopOp>();
    int64_t xor_rhs_val;
    if (selector->MatchSignedIntegralConstant(xor_op.right(), &xor_rhs_val) &&
        xor_rhs_val == -1) {
      // TODO(all): support shifted operand on right.
      selector->Emit(inv_opcode, g.DefineAsRegister(node),
                     g.UseRegister(logical_op.left()),
                     g.UseRegister(xor_op.left()));
      return;
    }
  }

  int64_t xor_rhs_val;
  if (logical_op.Is<Opmask::kBitwiseXor>() &&
      selector->MatchSignedIntegralConstant(logical_op.right(), &xor_rhs_val) &&
      xor_rhs_val == -1) {
    const WordBinopOp& xor_op = logical_op.Cast<Opmask::kBitwiseXor>();
    bool is32 = rep == WordRepresentation::Word32();
    ArchOpcode opcode = is32 ? kArm64Not32 : kArm64Not;
    selector->Emit(opcode, g.DefineAsRegister(node),
                   g.UseRegister(xor_op.left()));
  } else {
    VisitBinop(selector, node, rep, opcode, imm_mode);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32And(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  const WordBinopOp& bitwise_and =
      this->Get(node).Cast<Opmask::kWord32BitwiseAnd>();
  const Operation& lhs = this->Get(bitwise_and.left());
  if (lhs.Is<Opmask::kWord32ShiftRightLogical>() &&
      CanCover(node, bitwise_and.left()) &&
      this->is_integer_constant(bitwise_and.right())) {
    int64_t constant_rhs = this->integer_constant(bitwise_and.right());
    DCHECK(base::IsInRange(constant_rhs, std::numeric_limits<int32_t>::min(),
                           std::numeric_limits<int32_t>::max()));
    uint32_t mask = static_cast<uint32_t>(constant_rhs);
    uint32_t mask_width = base::bits::CountPopulation(mask);
    uint32_t mask_msb = base::bits::CountLeadingZeros32(mask);
    if ((mask_width != 0) && (mask_width != 32) &&
        (mask_msb + mask_width == 32)) {
      // The mask must be contiguous, and occupy the least-significant bits.
      DCHECK_EQ(0u, base::bits::CountTrailingZeros32(mask));

      // Select Ubfx for And(Shr(x, imm), mask) where the mask is in the least
      // significant bits.
      const ShiftOp& lhs_shift = lhs.Cast<Opmask::kWord32ShiftRightLogical>();
      if (this->is_integer_constant(lhs_shift.right())) {
        // Any shift value can match; int32 shifts use `value % 32`.
        uint32_t lsb = this->integer_constant(lhs_shift.right()) & 0x1F;

        // Ubfx cannot extract bits past the register size, however since
        // shifting the original value would have introduced some zeros we can
        // still use ubfx with a smaller mask and the remaining bits will be
        // zeros.
        if (lsb + mask_width > 32) mask_width = 32 - lsb;

        Emit(kArm64Ubfx32, g.DefineAsRegister(node),
             g.UseRegister(lhs_shift.left()),
             g.UseImmediateOrTemp(lhs_shift.right(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
      // Other cases fall through to the normal And operation.
    }
  }
  VisitLogical(this, zone(), node, bitwise_and.rep, kArm64And32,
               CanCover(node, bitwise_and.left()),
               CanCover(node, bitwise_and.right()), kLogical32Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32And(Node* node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int32BinopMatcher m(node);
  if (m.left().IsWord32Shr() && CanCover(node, m.left().node()) &&
      m.right().HasResolvedValue()) {
    uint32_t mask = m.right().ResolvedValue();
    uint32_t mask_width = base::bits::CountPopulation(mask);
    uint32_t mask_msb = base::bits::CountLeadingZeros32(mask);
    if ((mask_width != 0) && (mask_width != 32) &&
        (mask_msb + mask_width == 32)) {
      // The mask must be contiguous, and occupy the least-significant bits.
      DCHECK_EQ(0u, base::bits::CountTrailingZeros32(mask));

      // Select Ubfx for And(Shr(x, imm), mask) where the mask is in the least
      // significant bits.
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue()) {
        // Any shift value can match; int32 shifts use `value % 32`.
        uint32_t lsb = mleft.right().ResolvedValue() & 0x1F;

        // Ubfx cannot extract bits past the register size, however since
        // shifting the original value would have introduced some zeros we can
        // still use ubfx with a smaller mask and the remaining bits will be
        // zeros.
        if (lsb + mask_width > 32) mask_width = 32 - lsb;

        Emit(kArm64Ubfx32, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseImmediateOrTemp(mleft.right().node(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
      // Other cases fall through to the normal And operation.
    }
  }
  VisitLogical<TurbofanAdapter, Int32BinopMatcher>(
      this, node, &m, kArm64And32, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kLogical32Imm);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64And(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);

  const WordBinopOp& bitwise_and = Get(node).Cast<Opmask::kWord64BitwiseAnd>();
  const Operation& lhs = Get(bitwise_and.left());

  if (lhs.Is<Opmask::kWord64ShiftRightLogical>() &&
      CanCover(node, bitwise_and.left()) &&
      is_integer_constant(bitwise_and.right())) {
    uint64_t mask = integer_constant(bitwise_and.right());
    uint64_t mask_width = base::bits::CountPopulation(mask);
    uint64_t mask_msb = base::bits::CountLeadingZeros64(mask);
    if ((mask_width != 0) && (mask_width != 64) &&
        (mask_msb + mask_width == 64)) {
      // The mask must be contiguous, and occupy the least-significant bits.
      DCHECK_EQ(0u, base::bits::CountTrailingZeros64(mask));

      // Select Ubfx for And(Shr(x, imm), mask) where the mask is in the least
      // significant bits.
      const ShiftOp& shift = lhs.Cast<ShiftOp>();
      if (is_integer_constant(shift.right())) {
        int64_t shift_by = integer_constant(shift.right());
        // Any shift value can match; int64 shifts use `value % 64`.
        uint32_t lsb = static_cast<uint32_t>(shift_by & 0x3F);

        // Ubfx cannot extract bits past the register size, however since
        // shifting the original value would have introduced some zeros we can
        // still use ubfx with a smaller mask and the remaining bits will be
        // zeros.
        if (lsb + mask_width > 64) mask_width = 64 - lsb;

        Emit(kArm64Ubfx, g.DefineAsRegister(node), g.UseRegister(shift.left()),
             g.UseImmediateOrTemp(shift.right(), lsb),
             g.TempImmediate(static_cast<int32_t>(mask_width)));
        return;
      }
      // Other cases fall through to the normal And operation.
    }
  }
  VisitLogical(this, zone(), node, bitwise_and.rep, kArm64And,
               CanCover(node, bitwise_and.left()),
               CanCover(node, bitwise_and.right()), kLogical64Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64And(Node* node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int64BinopMatcher m(node);
  if (m.left().IsWord64Shr() && CanCover(node, m.left().node()) &&
      m.right().HasResolvedValue()) {
    uint64_t mask = m.right().ResolvedValue();
    uint64_t mask_width = base::bits::CountPopulation(mask);
    uint64_t mask_msb = base::bits::CountLeadingZeros64(mask);
    if ((mask_width != 0) && (mask_width != 64) &&
        (mask_msb + mask_width == 64)) {
      // The mask must be contiguous, and occupy the least-significant bits.
      DCHECK_EQ(0u, base::bits::CountTrailingZeros64(mask));

      // Select Ubfx for And(Shr(x, imm), mask) where the mask is in the least
      // significant bits.
      Int64BinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue()) {
        // Any shift value can match; int64 shifts use `value % 64`.
        uint32_t lsb =
            static_cast<uint32_t>(mleft.right().ResolvedValue() & 0x3F);

        // Ubfx cannot extract bits past the register size, however since
        // shifting the original value would have introduced some zeros we can
        // still use ubfx with a smaller mask and the remaining bits will be
        // zeros.
        if (lsb + mask_width > 64) mask_width = 64 - lsb;

        Emit(kArm64Ubfx, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseImmediateOrTemp(mleft.right().node(), lsb),
             g.TempImmediate(static_cast<int32_t>(mask_width)));
        return;
      }
      // Other cases fall through to the normal And operation.
    }
  }
  VisitLogical<TurbofanAdapter, Int64BinopMatcher>(
      this, node, &m, kArm64And, CanCover(node, m.left().node()),
      CanCover(node, m.right().node()), kLogical64Imm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, zone(), node, op.rep, kArm64Or32,
                 CanCover(node, op.left()), CanCover(node, op.right()),
                 kLogical32Imm);
  } else {
    Int32BinopMatcher m(node);
    VisitLogical<Adapter, Int32BinopMatcher>(
        this, node, &m, kArm64Or32, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kLogical32Imm);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Or(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, zone(), node, op.rep, kArm64Or,
                 CanCover(node, op.left()), CanCover(node, op.right()),
                 kLogical64Imm);
  } else {
    Int64BinopMatcher m(node);
    VisitLogical<Adapter, Int64BinopMatcher>(
        this, node, &m, kArm64Or, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kLogical64Imm);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, zone(), node, op.rep, kArm64Eor32,
                 CanCover(node, op.left()), CanCover(node, op.right()),
                 kLogical32Imm);
  } else {
    Int32BinopMatcher m(node);
    VisitLogical<Adapter, Int32BinopMatcher>(
        this, node, &m, kArm64Eor32, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kLogical32Imm);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Xor(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const WordBinopOp& op = this->Get(node).template Cast<WordBinopOp>();
    VisitLogical(this, zone(), node, op.rep, kArm64Eor,
                 CanCover(node, op.left()), CanCover(node, op.right()),
                 kLogical64Imm);
  } else {
    Int64BinopMatcher m(node);
    VisitLogical<Adapter, Int64BinopMatcher>(
        this, node, &m, kArm64Eor, CanCover(node, m.left().node()),
        CanCover(node, m.right().node()), kLogical64Imm);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Shl(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ShiftOp& shift_op = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(shift_op.left());
  if (lhs.Is<Opmask::kWord32BitwiseAnd>() && CanCover(node, shift_op.left()) &&
      is_integer_constant(shift_op.right())) {
    uint32_t shift_by =
        static_cast<uint32_t>(integer_constant(shift_op.right()));
    if (base::IsInRange(shift_by, 1, 31)) {
      const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
      if (is_integer_constant(bitwise_and.right())) {
        uint32_t mask =
            static_cast<uint32_t>(integer_constant(bitwise_and.right()));

        uint32_t mask_width = base::bits::CountPopulation(mask);
        uint32_t mask_msb = base::bits::CountLeadingZeros32(mask);
        if ((mask_width != 0) && (mask_msb + mask_width == 32)) {
          DCHECK_EQ(0u, base::bits::CountTrailingZeros32(mask));
          DCHECK_NE(0u, shift_by);
          Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
          if ((shift_by + mask_width) >= 32) {
            // If the mask is contiguous and reaches or extends beyond the top
            // bit, only the shift is needed.
            Emit(kArm64Lsl32, g.DefineAsRegister(node),
                 g.UseRegister(bitwise_and.left()), g.UseImmediate(shift_by));
            return;
          } else {
            // Select Ubfiz for Shl(And(x, mask), imm) where the mask is
            // contiguous, and the shift immediate non-zero.
            Emit(kArm64Ubfiz32, g.DefineAsRegister(node),
                 g.UseRegister(bitwise_and.left()), g.UseImmediate(shift_by),
                 g.TempImmediate(mask_width));
            return;
          }
        }
      }
    }
  }
  VisitRRO(this, kArm64Lsl32, node, kShift32Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Shl(Node* node) {
  Int32BinopMatcher m(node);
  if (m.left().IsWord32And() && CanCover(node, m.left().node()) &&
      m.right().IsInRange(1, 31)) {
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Int32BinopMatcher mleft(m.left().node());
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
          Emit(kArm64Lsl32, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()),
               g.UseImmediate(m.right().node()));
          return;
        } else {
          // Select Ubfiz for Shl(And(x, mask), imm) where the mask is
          // contiguous, and the shift immediate non-zero.
          Emit(kArm64Ubfiz32, g.DefineAsRegister(node),
               g.UseRegister(mleft.left().node()),
               g.UseImmediate(m.right().node()), g.TempImmediate(mask_width));
          return;
        }
      }
    }
  }
  VisitRRO(this, kArm64Lsl32, node, kShift32Imm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shl(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ShiftOp& shift_op = this->Get(node).template Cast<ShiftOp>();
    const Operation& lhs = this->Get(shift_op.left());
    const Operation& rhs = this->Get(shift_op.right());
    if ((lhs.Is<Opmask::kChangeInt32ToInt64>() ||
         lhs.Is<Opmask::kChangeUint32ToUint64>()) &&
        rhs.Is<Opmask::kWord32Constant>()) {
      int64_t shift_by = rhs.Cast<ConstantOp>().signed_integral();
      if (base::IsInRange(shift_by, 32, 63) &&
          CanCover(node, shift_op.left())) {
        // There's no need to sign/zero-extend to 64-bit if we shift out the
        // upper 32 bits anyway.
        Emit(kArm64Lsl, g.DefineAsRegister(node),
             g.UseRegister(lhs.Cast<ChangeOp>().input()),
             g.UseImmediate64(shift_by));
        return;
      }
    }
    VisitRRO(this, kArm64Lsl, node, kShift64Imm);
  } else {
    Int64BinopMatcher m(node);
    if ((m.left().IsChangeInt32ToInt64() ||
         m.left().IsChangeUint32ToUint64()) &&
        m.right().IsInRange(32, 63) && CanCover(node, m.left().node())) {
      // There's no need to sign/zero-extend to 64-bit if we shift out the upper
      // 32 bits anyway.
      Emit(kArm64Lsl, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()->InputAt(0)),
           g.UseImmediate(m.right().node()));
      return;
    }
    VisitRRO(this, kArm64Lsl, node, kShift64Imm);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(
    node_t node, FlagsContinuationT<Adapter>* cont) {
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

  Arm64OperandGeneratorT<Adapter> g(this);

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

template <typename Adapter>
bool TryEmitBitfieldExtract32(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  Int32BinopMatcher m(node);
  if (selector->CanCover(node, m.left().node()) && m.left().IsWord32Shl()) {
    // Select Ubfx or Sbfx for (x << (K & 0x1F)) OP (K & 0x1F), where
    // OP is >>> or >> and (K & 0x1F) != 0.
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue() && m.right().HasResolvedValue() &&
        (mleft.right().ResolvedValue() & 0x1F) != 0 &&
        (mleft.right().ResolvedValue() & 0x1F) ==
            (m.right().ResolvedValue() & 0x1F)) {
      DCHECK(m.IsWord32Shr() || m.IsWord32Sar());
      ArchOpcode opcode = m.IsWord32Sar() ? kArm64Sbfx32 : kArm64Ubfx32;

      int right_val = m.right().ResolvedValue() & 0x1F;
      DCHECK_NE(right_val, 0);

      selector->Emit(opcode, g.DefineAsRegister(node),
                     g.UseRegister(mleft.left().node()), g.TempImmediate(0),
                     g.TempImmediate(32 - right_val));
      return true;
    }
  }
  return false;
}

template <>
bool TryEmitBitfieldExtract32(InstructionSelectorT<TurboshaftAdapter>* selector,
                              turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const ShiftOp& shift = selector->Get(node).Cast<ShiftOp>();
  const Operation& lhs = selector->Get(shift.left());
  if (selector->CanCover(node, shift.left()) &&
      lhs.Is<Opmask::kWord32ShiftLeft>()) {
    // Select Ubfx or Sbfx for (x << (K & 0x1F)) OP (K & 0x1F), where
    // OP is >>> or >> and (K & 0x1F) != 0.
    const ShiftOp& lhs_shift = lhs.Cast<ShiftOp>();
    int64_t lhs_shift_by_constant, shift_by_constant;
    if (selector->MatchSignedIntegralConstant(lhs_shift.right(),
                                              &lhs_shift_by_constant) &&
        selector->MatchSignedIntegralConstant(shift.right(),
                                              &shift_by_constant) &&
        (lhs_shift_by_constant & 0x1F) != 0 &&
        (lhs_shift_by_constant & 0x1F) == (shift_by_constant & 0x1F)) {
      DCHECK(shift.Is<Opmask::kWord32ShiftRightArithmetic>() ||
             shift.Is<Opmask::kWord32ShiftRightArithmeticShiftOutZeros>() ||
             shift.Is<Opmask::kWord32ShiftRightLogical>());

      ArchOpcode opcode = shift.kind == ShiftOp::Kind::kShiftRightLogical
                              ? kArm64Ubfx32
                              : kArm64Sbfx32;

      int right_val = shift_by_constant & 0x1F;
      DCHECK_NE(right_val, 0);

      selector->Emit(opcode, g.DefineAsRegister(node),
                     g.UseRegister(lhs_shift.left()), g.TempImmediate(0),
                     g.TempImmediate(32 - right_val));
      return true;
    }
  }
  return false;
}

}  // namespace
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Shr(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ShiftOp& shift = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(shift.left());
  if (lhs.Is<Opmask::kWord32BitwiseAnd>() &&
      is_integer_constant(shift.right())) {
    uint32_t lsb = integer_constant(shift.right()) & 0x1F;
    const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
    uint32_t constant_bitmask;
    if (MatchIntegralWord32Constant(bitwise_and.right(), &constant_bitmask) &&
        constant_bitmask != 0) {
      // Select Ubfx for Shr(And(x, mask), imm) where the result of the mask is
      // shifted into the least-significant bits.
      uint32_t mask = (constant_bitmask >> lsb) << lsb;
      unsigned mask_width = base::bits::CountPopulation(mask);
      unsigned mask_msb = base::bits::CountLeadingZeros32(mask);
      if ((mask_msb + mask_width + lsb) == 32) {
        Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
        DCHECK_EQ(lsb, base::bits::CountTrailingZeros32(mask));
        Emit(kArm64Ubfx32, g.DefineAsRegister(node),
             g.UseRegister(bitwise_and.left()),
             g.UseImmediateOrTemp(shift.right(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
    }
  } else if (TryEmitBitfieldExtract32(this, node)) {
    return;
  }

  if (lhs.Is<Opmask::kWord32UnsignedMulOverflownBits>() &&
      is_integer_constant(shift.right()) && CanCover(node, shift.left())) {
    // Combine this shift with the multiply and shift that would be generated
    // by Uint32MulHigh.
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    const WordBinopOp& mul = lhs.Cast<WordBinopOp>();
    int shift_by = integer_constant(shift.right()) & 0x1F;
    InstructionOperand const smull_operand = g.TempRegister();
    Emit(kArm64Umull, smull_operand, g.UseRegister(mul.left()),
         g.UseRegister(mul.right()));
    Emit(kArm64Lsr, g.DefineAsRegister(node), smull_operand,
         g.TempImmediate(32 + shift_by));
    return;
  }

  VisitRRO(this, kArm64Lsr32, node, kShift32Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Shr(Node* node) {
  Int32BinopMatcher m(node);
  if (m.left().IsWord32And() && m.right().HasResolvedValue()) {
    uint32_t lsb = m.right().ResolvedValue() & 0x1F;
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue() &&
        mleft.right().ResolvedValue() != 0) {
      // Select Ubfx for Shr(And(x, mask), imm) where the result of the mask is
      // shifted into the least-significant bits.
      uint32_t mask =
          static_cast<uint32_t>(mleft.right().ResolvedValue() >> lsb) << lsb;
      unsigned mask_width = base::bits::CountPopulation(mask);
      unsigned mask_msb = base::bits::CountLeadingZeros32(mask);
      if ((mask_msb + mask_width + lsb) == 32) {
        Arm64OperandGeneratorT<TurbofanAdapter> g(this);
        DCHECK_EQ(lsb, base::bits::CountTrailingZeros32(mask));
        Emit(kArm64Ubfx32, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseImmediateOrTemp(m.right().node(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
    }
  } else if (TryEmitBitfieldExtract32(this, node)) {
    return;
  }

  if (m.left().IsUint32MulHigh() && m.right().HasResolvedValue() &&
      CanCover(node, node->InputAt(0))) {
    // Combine this shift with the multiply and shift that would be generated
    // by Uint32MulHigh.
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Node* left = m.left().node();
    int shift = m.right().ResolvedValue() & 0x1F;
    InstructionOperand const smull_operand = g.TempRegister();
    Emit(kArm64Umull, smull_operand, g.UseRegister(left->InputAt(0)),
         g.UseRegister(left->InputAt(1)));
    Emit(kArm64Lsr, g.DefineAsRegister(node), smull_operand,
         g.TempImmediate(32 + shift));
    return;
  }

  VisitRRO(this, kArm64Lsr32, node, kShift32Imm);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64Shr(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ShiftOp& op = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(op.left());
  if (lhs.Is<Opmask::kWord64BitwiseAnd>() && is_integer_constant(op.right())) {
    uint32_t lsb = integer_constant(op.right()) & 0x3F;
    const WordBinopOp& bitwise_and = lhs.Cast<WordBinopOp>();
    uint64_t constant_and_rhs;
    if (MatchIntegralWord64Constant(bitwise_and.right(), &constant_and_rhs) &&
        constant_and_rhs != 0) {
      // Select Ubfx for Shr(And(x, mask), imm) where the result of the mask is
      // shifted into the least-significant bits.
      uint64_t mask = static_cast<uint64_t>(constant_and_rhs >> lsb) << lsb;
      unsigned mask_width = base::bits::CountPopulation(mask);
      unsigned mask_msb = base::bits::CountLeadingZeros64(mask);
      if ((mask_msb + mask_width + lsb) == 64) {
        Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
        DCHECK_EQ(lsb, base::bits::CountTrailingZeros64(mask));
        Emit(kArm64Ubfx, g.DefineAsRegister(node),
             g.UseRegister(bitwise_and.left()),
             g.UseImmediateOrTemp(op.right(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
    }
  }
  VisitRRO(this, kArm64Lsr, node, kShift64Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64Shr(Node* node) {
  Int64BinopMatcher m(node);
  if (m.left().IsWord64And() && m.right().HasResolvedValue()) {
    uint32_t lsb = m.right().ResolvedValue() & 0x3F;
    Int64BinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue() &&
        mleft.right().ResolvedValue() != 0) {
      // Select Ubfx for Shr(And(x, mask), imm) where the result of the mask is
      // shifted into the least-significant bits.
      uint64_t mask =
          static_cast<uint64_t>(mleft.right().ResolvedValue() >> lsb) << lsb;
      unsigned mask_width = base::bits::CountPopulation(mask);
      unsigned mask_msb = base::bits::CountLeadingZeros64(mask);
      if ((mask_msb + mask_width + lsb) == 64) {
        Arm64OperandGeneratorT<TurbofanAdapter> g(this);
        DCHECK_EQ(lsb, base::bits::CountTrailingZeros64(mask));
        Emit(kArm64Ubfx, g.DefineAsRegister(node),
             g.UseRegister(mleft.left().node()),
             g.UseImmediateOrTemp(m.right().node(), lsb),
             g.TempImmediate(mask_width));
        return;
      }
    }
  }
  VisitRRO(this, kArm64Lsr, node, kShift64Imm);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Sar(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (TryEmitBitfieldExtract32(this, node)) {
    return;
  }

  const ShiftOp& shift = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(shift.left());
  if (lhs.Is<Opmask::kWord32SignedMulOverflownBits>() &&
      is_integer_constant(shift.right()) && CanCover(node, shift.left())) {
    // Combine this shift with the multiply and shift that would be generated
    // by Int32MulHigh.
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    const WordBinopOp& mul_overflow = lhs.Cast<WordBinopOp>();
    int shift_by = integer_constant(shift.right()) & 0x1F;
    InstructionOperand const smull_operand = g.TempRegister();
    Emit(kArm64Smull, smull_operand, g.UseRegister(mul_overflow.left()),
         g.UseRegister(mul_overflow.right()));
    Emit(kArm64Asr, g.DefineAsRegister(node), smull_operand,
         g.TempImmediate(32 + shift_by));
    return;
  }

  if (lhs.Is<Opmask::kWord32Add>() && is_integer_constant(shift.right()) &&
      CanCover(node, shift.left())) {
    const WordBinopOp& add = Get(shift.left()).Cast<WordBinopOp>();
    const Operation& lhs = Get(add.left());
    if (lhs.Is<Opmask::kWord32SignedMulOverflownBits>() &&
        CanCover(shift.left(), add.left())) {
      // Combine the shift that would be generated by Int32MulHigh with the add
      // on the left of this Sar operation. We do it here, as the result of the
      // add potentially has 33 bits, so we have to ensure the result is
      // truncated by being the input to this 32-bit Sar operation.
      Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
      const WordBinopOp& mul = lhs.Cast<WordBinopOp>();

      InstructionOperand const smull_operand = g.TempRegister();
      Emit(kArm64Smull, smull_operand, g.UseRegister(mul.left()),
           g.UseRegister(mul.right()));

      InstructionOperand const add_operand = g.TempRegister();
      Emit(kArm64Add | AddressingModeField::encode(kMode_Operand2_R_ASR_I),
           add_operand, g.UseRegister(add.right()), smull_operand,
           g.TempImmediate(32));

      Emit(kArm64Asr32, g.DefineAsRegister(node), add_operand,
           g.UseImmediate(shift.right()));
      return;
    }
  }

  VisitRRO(this, kArm64Asr32, node, kShift32Imm);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Sar(Node* node) {
  if (TryEmitBitfieldExtract32(this, node)) {
    return;
  }

  Int32BinopMatcher m(node);
  if (m.left().IsInt32MulHigh() && m.right().HasResolvedValue() &&
      CanCover(node, node->InputAt(0))) {
    // Combine this shift with the multiply and shift that would be generated
    // by Int32MulHigh.
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Node* left = m.left().node();
    int shift = m.right().ResolvedValue() & 0x1F;
    InstructionOperand const smull_operand = g.TempRegister();
    Emit(kArm64Smull, smull_operand, g.UseRegister(left->InputAt(0)),
         g.UseRegister(left->InputAt(1)));
    Emit(kArm64Asr, g.DefineAsRegister(node), smull_operand,
         g.TempImmediate(32 + shift));
    return;
  }

  if (m.left().IsInt32Add() && m.right().HasResolvedValue() &&
      CanCover(node, node->InputAt(0))) {
    Node* add_node = m.left().node();
    Int32BinopMatcher madd_node(add_node);
    if (madd_node.left().IsInt32MulHigh() &&
        CanCover(add_node, madd_node.left().node())) {
      // Combine the shift that would be generated by Int32MulHigh with the add
      // on the left of this Sar operation. We do it here, as the result of the
      // add potentially has 33 bits, so we have to ensure the result is
      // truncated by being the input to this 32-bit Sar operation.
      Arm64OperandGeneratorT<TurbofanAdapter> g(this);
      Node* mul_node = madd_node.left().node();

      InstructionOperand const smull_operand = g.TempRegister();
      Emit(kArm64Smull, smull_operand, g.UseRegister(mul_node->InputAt(0)),
           g.UseRegister(mul_node->InputAt(1)));

      InstructionOperand const add_operand = g.TempRegister();
      Emit(kArm64Add | AddressingModeField::encode(kMode_Operand2_R_ASR_I),
           add_operand, g.UseRegister(add_node->InputAt(1)), smull_operand,
           g.TempImmediate(32));

      Emit(kArm64Asr32, g.DefineAsRegister(node), add_operand,
           g.UseImmediate(node->InputAt(1)));
      return;
    }
  }

  VisitRRO(this, kArm64Asr32, node, kShift32Imm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Sar(node_t node) {
  {
    if (TryEmitExtendingLoad(this, node)) return;

    // Select Sbfx(x, imm, 32-imm) for Word64Sar(ChangeInt32ToInt64(x), imm)
    // where possible
    Int64BinopMatcher m(node);
    if (m.left().IsChangeInt32ToInt64() && m.right().HasResolvedValue() &&
        is_uint5(m.right().ResolvedValue()) &&
        CanCover(node, m.left().node())) {
      // Don't select Sbfx here if Asr(Ldrsw(x), imm) can be selected for
      // Word64Sar(ChangeInt32ToInt64(Load(x)), imm)
      if ((m.left().InputAt(0)->opcode() != IrOpcode::kLoad &&
           m.left().InputAt(0)->opcode() != IrOpcode::kLoadImmutable) ||
          !CanCover(m.left().node(), m.left().InputAt(0))) {
        Arm64OperandGeneratorT<Adapter> g(this);
        int right = static_cast<int>(m.right().ResolvedValue());
        Emit(kArm64Sbfx, g.DefineAsRegister(node),
             g.UseRegister(m.left().node()->InputAt(0)),
             g.UseImmediate(m.right().node()), g.UseImmediate(32 - right));
        return;
      }
    }

    VisitRRO(this, kArm64Asr, node, kShift64Imm);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64Sar(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (TryEmitExtendingLoad(this, node)) return;

  // Select Sbfx(x, imm, 32-imm) for Word64Sar(ChangeInt32ToInt64(x), imm)
  // where possible
  const ShiftOp& shiftop = Get(node).Cast<ShiftOp>();
  const Operation& lhs = Get(shiftop.left());

  int64_t constant_rhs;
  if (lhs.Is<Opmask::kChangeInt32ToInt64>() &&
      MatchIntegralWord64Constant(shiftop.right(), &constant_rhs) &&
      is_uint5(constant_rhs) && CanCover(node, shiftop.left())) {
    // Don't select Sbfx here if Asr(Ldrsw(x), imm) can be selected for
    // Word64Sar(ChangeInt32ToInt64(Load(x)), imm)
    OpIndex input = lhs.Cast<ChangeOp>().input();
    if (!Get(input).Is<LoadOp>() || !CanCover(shiftop.left(), input)) {
      Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
      int right = static_cast<int>(constant_rhs);
      Emit(kArm64Sbfx, g.DefineAsRegister(node), g.UseRegister(input),
           g.UseImmediate(right), g.UseImmediate(32 - right));
      return;
    }
  }

  VisitRRO(this, kArm64Asr, node, kShift64Imm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Rol(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
  VisitRRO(this, kArm64Ror32, node, kShift32Imm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ror(node_t node) {
  VisitRRO(this, kArm64Ror, node, kShift64Imm);
}

#define RR_OP_T_LIST(V)                                       \
  V(Float32Sqrt, kArm64Float32Sqrt)                           \
  V(Float64Sqrt, kArm64Float64Sqrt)                           \
  V(Float32RoundDown, kArm64Float32RoundDown)                 \
  V(Float64RoundDown, kArm64Float64RoundDown)                 \
  V(Float32RoundUp, kArm64Float32RoundUp)                     \
  V(Float64RoundUp, kArm64Float64RoundUp)                     \
  V(Float32RoundTruncate, kArm64Float32RoundTruncate)         \
  V(Float64RoundTruncate, kArm64Float64RoundTruncate)         \
  V(Float64RoundTiesAway, kArm64Float64RoundTiesAway)         \
  V(Float32RoundTiesEven, kArm64Float32RoundTiesEven)         \
  V(Float64RoundTiesEven, kArm64Float64RoundTiesEven)         \
  V(Float64SilenceNaN, kArm64Float64SilenceNaN)               \
  V(ChangeInt32ToFloat64, kArm64Int32ToFloat64)               \
  V(RoundFloat64ToInt32, kArm64Float64ToInt32)                \
  V(ChangeFloat32ToFloat64, kArm64Float32ToFloat64)           \
  V(RoundInt32ToFloat32, kArm64Int32ToFloat32)                \
  V(RoundUint32ToFloat32, kArm64Uint32ToFloat32)              \
  V(ChangeInt64ToFloat64, kArm64Int64ToFloat64)               \
  V(ChangeUint32ToFloat64, kArm64Uint32ToFloat64)             \
  V(ChangeFloat64ToInt32, kArm64Float64ToInt32)               \
  V(ChangeFloat64ToInt64, kArm64Float64ToInt64)               \
  V(ChangeFloat64ToUint32, kArm64Float64ToUint32)             \
  V(ChangeFloat64ToUint64, kArm64Float64ToUint64)             \
  V(RoundInt64ToFloat32, kArm64Int64ToFloat32)                \
  V(RoundInt64ToFloat64, kArm64Int64ToFloat64)                \
  V(RoundUint64ToFloat32, kArm64Uint64ToFloat32)              \
  V(RoundUint64ToFloat64, kArm64Uint64ToFloat64)              \
  V(BitcastFloat32ToInt32, kArm64Float64ExtractLowWord32)     \
  V(BitcastFloat64ToInt64, kArm64U64MoveFloat64)              \
  V(BitcastInt32ToFloat32, kArm64Float64MoveU64)              \
  V(BitcastInt64ToFloat64, kArm64Float64MoveU64)              \
  V(TruncateFloat64ToFloat32, kArm64Float64ToFloat32)         \
  V(TruncateFloat64ToWord32, kArchTruncateDoubleToI)          \
  V(TruncateFloat64ToUint32, kArm64Float64ToUint32)           \
  V(Float64ExtractLowWord32, kArm64Float64ExtractLowWord32)   \
  V(Float64ExtractHighWord32, kArm64Float64ExtractHighWord32) \
  V(Word64Clz, kArm64Clz)                                     \
  V(Word32Clz, kArm64Clz32)                                   \
  V(Word32Popcnt, kArm64Cnt32)                                \
  V(Word64Popcnt, kArm64Cnt64)                                \
  V(Word32ReverseBits, kArm64Rbit32)                          \
  V(Word64ReverseBits, kArm64Rbit)                            \
  V(Word32ReverseBytes, kArm64Rev32)                          \
  V(Word64ReverseBytes, kArm64Rev)                            \
  IF_WASM(V, F16x8Ceil, kArm64Float16RoundUp)                 \
  IF_WASM(V, F16x8Floor, kArm64Float16RoundDown)              \
  IF_WASM(V, F16x8Trunc, kArm64Float16RoundTruncate)          \
  IF_WASM(V, F16x8NearestInt, kArm64Float16RoundTiesEven)     \
  IF_WASM(V, F32x4Ceil, kArm64Float32RoundUp)                 \
  IF_WASM(V, F32x4Floor, kArm64Float32RoundDown)              \
  IF_WASM(V, F32x4Trunc, kArm64Float32RoundTruncate)          \
  IF_WASM(V, F32x4NearestInt, kArm64Float32RoundTiesEven)     \
  IF_WASM(V, F64x2Ceil, kArm64Float64RoundUp)                 \
  IF_WASM(V, F64x2Floor, kArm64Float64RoundDown)              \
  IF_WASM(V, F64x2Trunc, kArm64Float64RoundTruncate)          \
  IF_WASM(V, F64x2NearestInt, kArm64Float64RoundTiesEven)

#define RRR_OP_T_LIST(V)          \
  V(Int32Div, kArm64Idiv32)       \
  V(Int64Div, kArm64Idiv)         \
  V(Uint32Div, kArm64Udiv32)      \
  V(Uint64Div, kArm64Udiv)        \
  V(Int32Mod, kArm64Imod32)       \
  V(Int64Mod, kArm64Imod)         \
  V(Uint32Mod, kArm64Umod32)      \
  V(Uint64Mod, kArm64Umod)        \
  V(Float32Add, kArm64Float32Add) \
  V(Float64Add, kArm64Float64Add) \
  V(Float32Sub, kArm64Float32Sub) \
  V(Float64Sub, kArm64Float64Sub) \
  V(Float32Div, kArm64Float32Div) \
  V(Float64Div, kArm64Float64Div) \
  V(Float32Max, kArm64Float32Max) \
  V(Float64Max, kArm64Float64Max) \
  V(Float32Min, kArm64Float32Min) \
  V(Float64Min, kArm64Float64Min) \
  IF_WASM(V, I8x16Swizzle, kArm64I8x16Swizzle)

#define RR_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, opcode, node);                                 \
  }
RR_OP_T_LIST(RR_VISITOR)
#undef RR_VISITOR
#undef RR_OP_T_LIST

#define RRR_VISITOR(Name, opcode)                                \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRRR(this, opcode, node);                                \
  }
RRR_OP_T_LIST(RRR_VISITOR)
#undef RRR_VISITOR
#undef RRR_OP_T_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ctz(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ctz(node_t node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Add(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const WordBinopOp& add = this->Get(node).Cast<WordBinopOp>();
  DCHECK(add.Is<Opmask::kWord32Add>());
  V<Word32> left = add.left<Word32>();
  V<Word32> right = add.right<Word32>();
  // Select Madd(x, y, z) for Add(Mul(x, y), z) or Add(z, Mul(x, y)).
  if (TryEmitMultiplyAddInt32(this, node, left, right) ||
      TryEmitMultiplyAddInt32(this, node, right, left)) {
    return;
  }
  VisitAddSub(this, node, kArm64Add32, kArm64Sub32);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Add(Node* node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int32BinopMatcher m(node);
  // Select Madd(x, y, z) for Add(Mul(x, y), z).
  if (m.left().IsInt32Mul() && CanCover(node, m.left().node())) {
    Int32BinopMatcher mleft(m.left().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mleft) == 0) {
      Emit(kArm64Madd32, g.DefineAsRegister(node),
           g.UseRegister(mleft.left().node()),
           g.UseRegister(mleft.right().node()),
           g.UseRegister(m.right().node()));
      return;
    }
  }
  // Select Madd(x, y, z) for Add(z, Mul(x, y)).
  if (m.right().IsInt32Mul() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mright) == 0) {
      Emit(kArm64Madd32, g.DefineAsRegister(node),
           g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()),
           g.UseRegister(m.left().node()));
      return;
    }
  }
  VisitAddSub<TurbofanAdapter, Int32BinopMatcher>(this, node, kArm64Add32,
                                                  kArm64Sub32);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt64Add(node_t node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int64BinopMatcher m(node);
  // Select Madd(x, y, z) for Add(Mul(x, y), z).
  if (m.left().IsInt64Mul() && CanCover(node, m.left().node())) {
    Int64BinopMatcher mleft(m.left().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mleft) == 0) {
      Emit(kArm64Madd, g.DefineAsRegister(node),
           g.UseRegister(mleft.left().node()),
           g.UseRegister(mleft.right().node()),
           g.UseRegister(m.right().node()));
      return;
    }
  }
  // Select Madd(x, y, z) for Add(z, Mul(x, y)).
  if (m.right().IsInt64Mul() && CanCover(node, m.right().node())) {
    Int64BinopMatcher mright(m.right().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mright) == 0) {
      Emit(kArm64Madd, g.DefineAsRegister(node),
           g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()),
           g.UseRegister(m.left().node()));
      return;
    }
  }
  VisitAddSub<TurbofanAdapter, Int64BinopMatcher>(this, node, kArm64Add,
                                                  kArm64Sub);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt64Add(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const WordBinopOp& add = this->Get(node).Cast<WordBinopOp>();
  DCHECK(add.Is<Opmask::kWord64Add>());
  V<Word64> left = add.left<Word64>();
  V<Word64> right = add.right<Word64>();
  // Select Madd(x, y, z) for Add(Mul(x, y), z) or Add(z, Mul(x, y)).
  if (TryEmitMultiplyAddInt64(this, node, left, right) ||
      TryEmitMultiplyAddInt64(this, node, right, left)) {
    return;
  }
  VisitAddSub(this, node, kArm64Add, kArm64Sub);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Sub(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Int32BinopMatcher m(node);

  // Select Msub(x, y, a) for Sub(a, Mul(x, y)).
  if (m.right().IsInt32Mul() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mright) == 0) {
      Emit(kArm64Msub32, g.DefineAsRegister(node),
           g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()),
           g.UseRegister(m.left().node()));
      return;
    }
  }

  VisitAddSub<Adapter, Int32BinopMatcher>(this, node, kArm64Sub32, kArm64Add32);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Sub(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(this->Get(node).Is<Opmask::kWord32Sub>());

  // Select Msub(x, y, a) for Sub(a, Mul(x, y)).
  if (TryEmitMultiplySub<Opmask::kWord32Mul>(this, node, kArm64Msub32)) {
    return;
  }

  VisitAddSub(this, node, kArm64Sub32, kArm64Add32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Sub(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Int64BinopMatcher m(node);

  // Select Msub(x, y, a) for Sub(a, Mul(x, y)).
  if (m.right().IsInt64Mul() && CanCover(node, m.right().node())) {
    Int64BinopMatcher mright(m.right().node());
    // Check multiply can't be later reduced to addition with shift.
    if (LeftShiftForReducedMultiply(&mright) == 0) {
      Emit(kArm64Msub, g.DefineAsRegister(node),
           g.UseRegister(mright.left().node()),
           g.UseRegister(mright.right().node()),
           g.UseRegister(m.left().node()));
      return;
    }
  }

  VisitAddSub<Adapter, Int64BinopMatcher>(this, node, kArm64Sub, kArm64Add);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt64Sub(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(this->Get(node).Is<Opmask::kWord64Sub>());

  // Select Msub(x, y, a) for Sub(a, Mul(x, y)).
  if (TryEmitMultiplySub<Opmask::kWord64Mul>(this, node, kArm64Msub)) {
    return;
  }

  VisitAddSub(this, node, kArm64Sub, kArm64Add);
}

namespace {

template <typename Adapter>
void EmitInt32MulWithOverflow(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              FlagsContinuationT<Adapter>* cont) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  Int32BinopMatcher m(node);
  InstructionOperand result = g.DefineAsRegister(node);
  InstructionOperand left = g.UseRegister(m.left().node());

  if (m.right().HasResolvedValue() &&
      base::bits::IsPowerOfTwo(m.right().ResolvedValue())) {
    // Sign extend the bottom 32 bits and shift left.
    int32_t shift = base::bits::WhichPowerOfTwo(m.right().ResolvedValue());
    selector->Emit(kArm64Sbfiz, result, left, g.TempImmediate(shift),
                   g.TempImmediate(32));
  } else {
    InstructionOperand right = g.UseRegister(m.right().node());
    selector->Emit(kArm64Smull, result, left, right);
  }

  InstructionCode opcode =
      kArm64Cmp | AddressingModeField::encode(kMode_Operand2_R_SXTW);
  selector->EmitWithContinuation(opcode, result, result, cont);
}

void EmitInt32MulWithOverflow(InstructionSelectorT<TurboshaftAdapter>* selector,
                              turboshaft::OpIndex node,
                              FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const OverflowCheckedBinopOp& mul =
      selector->Get(node).Cast<OverflowCheckedBinopOp>();
  InstructionOperand result = g.DefineAsRegister(node);
  InstructionOperand left = g.UseRegister(mul.left());

  int32_t constant_rhs;
  if (selector->MatchIntegralWord32Constant(mul.right(), &constant_rhs) &&
      base::bits::IsPowerOfTwo(constant_rhs)) {
    // Sign extend the bottom 32 bits and shift left.
    int32_t shift = base::bits::WhichPowerOfTwo(constant_rhs);
    selector->Emit(kArm64Sbfiz, result, left, g.TempImmediate(shift),
                   g.TempImmediate(32));
  } else {
    InstructionOperand right = g.UseRegister(mul.right());
    selector->Emit(kArm64Smull, result, left, right);
  }

  InstructionCode opcode =
      kArm64Cmp | AddressingModeField::encode(kMode_Operand2_R_SXTW);
  selector->EmitWithContinuation(opcode, result, result, cont);
}

template <typename Adapter>
void EmitInt64MulWithOverflow(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              FlagsContinuationT<Adapter>* cont) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  InstructionOperand result = g.DefineAsRegister(node);
  InstructionOperand left = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand high = g.TempRegister();

  InstructionOperand right = g.UseRegister(selector->input_at(node, 1));
  selector->Emit(kArm64Mul, result, left, right);
  selector->Emit(kArm64Smulh, high, left, right);

  // Test whether {high} is a sign-extension of {result}.
  InstructionCode opcode =
      kArm64Cmp | AddressingModeField::encode(kMode_Operand2_R_ASR_I);
  selector->EmitWithContinuation(opcode, high, result, g.TempImmediate(63),
                                 cont);
}

}  // namespace

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Mul(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  const WordBinopOp& mul = Get(node).Cast<WordBinopOp>();
  DCHECK(mul.Is<Opmask::kWord32Mul>());

  // First, try to reduce the multiplication to addition with left shift.
  // x * (2^k + 1) -> x + (x << k)
  int32_t shift = LeftShiftForReducedMultiply(this, mul.right());
  if (shift > 0) {
    Emit(kArm64Add32 | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
         g.DefineAsRegister(node), g.UseRegister(mul.left()),
         g.UseRegister(mul.left()), g.TempImmediate(shift));
    return;
  }

  // Select Mneg(x, y) for Mul(Sub(0, x), y) or Mul(y, Sub(0, x)).
  if (TryEmitMultiplyNegateInt32(this, node, mul.left(), mul.right()) ||
      TryEmitMultiplyNegateInt32(this, node, mul.right(), mul.left())) {
    return;
  }

  VisitRRR(this, kArm64Mul32, node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Mul(Node* node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int32BinopMatcher m(node);

  // First, try to reduce the multiplication to addition with left shift.
  // x * (2^k + 1) -> x + (x << k)
  int32_t shift = LeftShiftForReducedMultiply(&m);
  if (shift > 0) {
    Emit(kArm64Add32 | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseRegister(m.left().node()), g.TempImmediate(shift));
    return;
  }

  if (m.left().IsInt32Sub() && CanCover(node, m.left().node())) {
    Int32BinopMatcher mleft(m.left().node());

    // Select Mneg(x, y) for Mul(Sub(0, x), y).
    if (mleft.left().Is(0)) {
      Emit(kArm64Mneg32, g.DefineAsRegister(node),
           g.UseRegister(mleft.right().node()),
           g.UseRegister(m.right().node()));
      return;
    }
  }

  if (m.right().IsInt32Sub() && CanCover(node, m.right().node())) {
    Int32BinopMatcher mright(m.right().node());

    // Select Mneg(x, y) for Mul(x, Sub(0, y)).
    if (mright.left().Is(0)) {
      Emit(kArm64Mneg32, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
  }

  VisitRRR(this, kArm64Mul32, node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt64Mul(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  const WordBinopOp& mul = Get(node).Cast<WordBinopOp>();
  DCHECK(mul.Is<Opmask::kWord64Mul>());

  // First, try to reduce the multiplication to addition with left shift.
  // x * (2^k + 1) -> x + (x << k)
  int32_t shift = LeftShiftForReducedMultiply(this, mul.right());
  if (shift > 0) {
    Emit(kArm64Add | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
         g.DefineAsRegister(node), g.UseRegister(mul.left()),
         g.UseRegister(mul.left()), g.TempImmediate(shift));
    return;
  }

  // Select Mneg(x, y) for Mul(Sub(0, x), y) or Mul(y, Sub(0, x)).
  if (TryEmitMultiplyNegateInt64(this, node, mul.left(), mul.right()) ||
      TryEmitMultiplyNegateInt64(this, node, mul.right(), mul.left())) {
    return;
  }

  VisitRRR(this, kArm64Mul, node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt64Mul(Node* node) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(this);
  Int64BinopMatcher m(node);

  // First, try to reduce the multiplication to addition with left shift.
  // x * (2^k + 1) -> x + (x << k)
  int32_t shift = LeftShiftForReducedMultiply(&m);
  if (shift > 0) {
    Emit(kArm64Add | AddressingModeField::encode(kMode_Operand2_R_LSL_I),
         g.DefineAsRegister(node), g.UseRegister(m.left().node()),
         g.UseRegister(m.left().node()), g.TempImmediate(shift));
    return;
  }

  if (m.left().IsInt64Sub() && CanCover(node, m.left().node())) {
    Int64BinopMatcher mleft(m.left().node());

    // Select Mneg(x, y) for Mul(Sub(0, x), y).
    if (mleft.left().Is(0)) {
      Emit(kArm64Mneg, g.DefineAsRegister(node),
           g.UseRegister(mleft.right().node()),
           g.UseRegister(m.right().node()));
      return;
    }
  }

  if (m.right().IsInt64Sub() && CanCover(node, m.right().node())) {
    Int64BinopMatcher mright(m.right().node());

    // Select Mneg(x, y) for Mul(x, Sub(0, y)).
    if (mright.left().Is(0)) {
      Emit(kArm64Mneg, g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.UseRegister(mright.right().node()));
      return;
    }
  }

  VisitRRR(this, kArm64Mul, node);
}

#if V8_ENABLE_WEBASSEMBLY
namespace {
template <typename Adapter>
void VisitExtMul(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
                 typename Adapter::node_t node, int dst_lane_size) {
  InstructionCode code = opcode;
  code |= LaneSizeField::encode(dst_lane_size);
  VisitRRR(selector, code, node);
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtMulLowI8x16S(node_t node) {
  VisitExtMul(this, kArm64Smull, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtMulHighI8x16S(node_t node) {
  VisitExtMul(this, kArm64Smull2, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtMulLowI8x16U(node_t node) {
  VisitExtMul(this, kArm64Umull, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtMulHighI8x16U(node_t node) {
  VisitExtMul(this, kArm64Umull2, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtMulLowI16x8S(node_t node) {
  VisitExtMul(this, kArm64Smull, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtMulHighI16x8S(node_t node) {
  VisitExtMul(this, kArm64Smull2, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtMulLowI16x8U(node_t node) {
  VisitExtMul(this, kArm64Umull, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtMulHighI16x8U(node_t node) {
  VisitExtMul(this, kArm64Umull2, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ExtMulLowI32x4S(node_t node) {
  VisitExtMul(this, kArm64Smull, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ExtMulHighI32x4S(node_t node) {
  VisitExtMul(this, kArm64Smull2, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ExtMulLowI32x4U(node_t node) {
  VisitExtMul(this, kArm64Umull, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ExtMulHighI32x4U(node_t node) {
  VisitExtMul(this, kArm64Umull2, node, 64);
}
#endif  // V8_ENABLE_WEBASSEMBLY

#if V8_ENABLE_WEBASSEMBLY
namespace {
template <typename Adapter>
void VisitExtAddPairwise(InstructionSelectorT<Adapter>* selector,
                         ArchOpcode opcode, typename Adapter::node_t node,
                         int dst_lane_size) {
  InstructionCode code = opcode;
  code |= LaneSizeField::encode(dst_lane_size);
  VisitRR(selector, code, node);
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8S(
    node_t node) {
  VisitExtAddPairwise(this, kArm64Saddlp, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8U(
    node_t node) {
  VisitExtAddPairwise(this, kArm64Uaddlp, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16S(
    node_t node) {
  VisitExtAddPairwise(this, kArm64Saddlp, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16U(
    node_t node) {
  VisitExtAddPairwise(this, kArm64Uaddlp, node, 16);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulHigh(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand const smull_operand = g.TempRegister();
  Emit(kArm64Smull, smull_operand, g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)));
  Emit(kArm64Asr, g.DefineAsRegister(node), smull_operand, g.TempImmediate(32));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulHigh(node_t node) {
  return VisitRRR(this, kArm64Smulh, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand const smull_operand = g.TempRegister();
  Emit(kArm64Umull, smull_operand, g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)));
  Emit(kArm64Lsr, g.DefineAsRegister(node), smull_operand, g.TempImmediate(32));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64MulHigh(node_t node) {
  return VisitRRR(this, kArm64Umulh, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToInt32(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kArm64Float32ToInt32;
    opcode |= MiscField::encode(
        op.Is<Opmask::kTruncateFloat32ToInt32OverflowToMin>());
    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));
  } else {
    InstructionCode opcode = kArm64Float32ToInt32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    opcode |= MiscField::encode(kind == TruncateKind::kSetOverflowToMin);
    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat32ToUint32(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = this->Get(node);
    InstructionCode opcode = kArm64Float32ToUint32;
    if (op.Is<Opmask::kTruncateFloat32ToUint32OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)));

  } else {
    InstructionCode opcode = kArm64Float32ToUint32;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToInt64(
    node_t node) {
    Arm64OperandGeneratorT<Adapter> g(this);

    InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
    InstructionOperand outputs[2];
    size_t output_count = 0;
    outputs[output_count++] = g.DefineAsRegister(node);

    node_t success_output = FindProjection(node, 1);
    if (this->valid(success_output)) {
      outputs[output_count++] = g.DefineAsRegister(success_output);
    }

    Emit(kArm64Float32ToInt64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToInt64(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    InstructionCode opcode = kArm64Float64ToInt64;
    const Operation& op = this->Get(node);
    if (op.Is<Opmask::kTruncateFloat64ToInt64OverflowToMin>()) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(op.input(0)));
  } else {
    InstructionCode opcode = kArm64Float64ToInt64;
    TruncateKind kind = OpParameter<TruncateKind>(node->op());
    if (kind == TruncateKind::kSetOverflowToMin) {
      opcode |= MiscField::encode(true);
    }

    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt64(
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);

  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kArm64Float64ToInt64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToUint64(
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);

  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kArm64Float32ToUint64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint64(
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);

  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kArm64Float64ToUint64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt32(
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kArm64Float64ToInt32, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint32(
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (this->valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kArm64Float64ToUint32, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32ToWord64(node_t node) {
  DCHECK(SmiValuesAre31Bits());
  DCHECK(COMPRESS_POINTERS_BOOL);
  EmitIdentity(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToInt64(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ChangeOp& change_op = this->Get(node).template Cast<ChangeOp>();
    const Operation& input_op = this->Get(change_op.input());
    if (input_op.Is<LoadOp>() && CanCover(node, change_op.input())) {
      // Generate sign-extending load.
      LoadRepresentation load_rep =
          this->load_view(change_op.input()).loaded_rep();
      MachineRepresentation rep = load_rep.representation();
      InstructionCode opcode = kArchNop;
      ImmediateMode immediate_mode = kNoImmediate;
      switch (rep) {
        case MachineRepresentation::kBit:  // Fall through.
        case MachineRepresentation::kWord8:
          opcode = load_rep.IsSigned() ? kArm64Ldrsb : kArm64Ldrb;
          immediate_mode = kLoadStoreImm8;
          break;
        case MachineRepresentation::kWord16:
          opcode = load_rep.IsSigned() ? kArm64Ldrsh : kArm64Ldrh;
          immediate_mode = kLoadStoreImm16;
          break;
        case MachineRepresentation::kWord32:
        case MachineRepresentation::kWord64:
          // Since BitcastElider may remove nodes of
          // IrOpcode::kTruncateInt64ToInt32 and directly use the inputs, values
          // with kWord64 can also reach this line.
        case MachineRepresentation::kTaggedSigned:
        case MachineRepresentation::kTagged:
        case MachineRepresentation::kTaggedPointer:
          opcode = kArm64Ldrsw;
          immediate_mode = kLoadStoreImm32;
          break;
        default:
          UNREACHABLE();
      }
      EmitLoad(this, change_op.input(), opcode, immediate_mode, rep, node);
      return;
    }
    if ((input_op.Is<Opmask::kWord32ShiftRightArithmetic>() ||
         input_op.Is<Opmask::kWord32ShiftRightArithmeticShiftOutZeros>()) &&
        CanCover(node, change_op.input())) {
      const ShiftOp& sar = input_op.Cast<ShiftOp>();
      if (this->is_integer_constant(sar.right())) {
        Arm64OperandGeneratorT<Adapter> g(this);
        // Mask the shift amount, to keep the same semantics as Word32Sar.
        int right = this->integer_constant(sar.right()) & 0x1F;
        Emit(kArm64Sbfx, g.DefineAsRegister(node), g.UseRegister(sar.left()),
             g.TempImmediate(right), g.TempImmediate(32 - right));
        return;
      }
    }
    VisitRR(this, kArm64Sxtw, node);
  } else {
    Node* value = node->InputAt(0);
    if ((value->opcode() == IrOpcode::kLoad ||
         value->opcode() == IrOpcode::kLoadImmutable) &&
        CanCover(node, value)) {
      // Generate sign-extending load.
      LoadRepresentation load_rep = LoadRepresentationOf(value->op());
      MachineRepresentation rep = load_rep.representation();
      InstructionCode opcode = kArchNop;
      ImmediateMode immediate_mode = kNoImmediate;
      switch (rep) {
        case MachineRepresentation::kBit:  // Fall through.
        case MachineRepresentation::kWord8:
          opcode = load_rep.IsSigned() ? kArm64Ldrsb : kArm64Ldrb;
          immediate_mode = kLoadStoreImm8;
          break;
        case MachineRepresentation::kWord16:
          opcode = load_rep.IsSigned() ? kArm64Ldrsh : kArm64Ldrh;
          immediate_mode = kLoadStoreImm16;
          break;
        case MachineRepresentation::kWord32:
        case MachineRepresentation::kWord64:
          // Since BitcastElider may remove nodes of
          // IrOpcode::kTruncateInt64ToInt32 and directly use the inputs, values
          // with kWord64 can also reach this line.
        case MachineRepresentation::kTaggedSigned:
        case MachineRepresentation::kTagged:
        case MachineRepresentation::kTaggedPointer:
          opcode = kArm64Ldrsw;
          immediate_mode = kLoadStoreImm32;
          break;
        default:
          UNREACHABLE();
      }
      EmitLoad(this, value, opcode, immediate_mode, rep, node);
      return;
    }

    if (value->opcode() == IrOpcode::kWord32Sar && CanCover(node, value)) {
      Int32BinopMatcher m(value);
      if (m.right().HasResolvedValue()) {
        Arm64OperandGeneratorT<Adapter> g(this);
        // Mask the shift amount, to keep the same semantics as Word32Sar.
        int right = m.right().ResolvedValue() & 0x1F;
        Emit(kArm64Sbfx, g.DefineAsRegister(node),
             g.UseRegister(m.left().node()), g.TempImmediate(right),
             g.TempImmediate(32 - right));
        return;
      }
    }

    VisitRR(this, kArm64Sxtw, node);
  }
}
template <>
bool InstructionSelectorT<TurboshaftAdapter>::ZeroExtendsWord32ToWord64NoPhis(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(!this->Get(node).Is<PhiOp>());
  const Operation& op = this->Get(node);
  // 32-bit operations will write their result in a W register (implicitly
  // clearing the top 32-bit of the corresponding X register) so the
  // zero-extension is a no-op.
  switch (op.opcode) {
    case Opcode::kWordBinop:
      return op.Cast<WordBinopOp>().rep == WordRepresentation::Word32();
    case Opcode::kShift:
      return op.Cast<ShiftOp>().rep == WordRepresentation::Word32();
    case Opcode::kComparison:
      return op.Cast<ComparisonOp>().rep == RegisterRepresentation::Word32();
    case Opcode::kOverflowCheckedBinop:
      return op.Cast<OverflowCheckedBinopOp>().rep ==
             WordRepresentation::Word32();
    case Opcode::kProjection:
      return ZeroExtendsWord32ToWord64NoPhis(op.Cast<ProjectionOp>().input());
    case Opcode::kLoad: {
      RegisterRepresentation rep =
          op.Cast<LoadOp>().loaded_rep.ToRegisterRepresentation();
      return rep == RegisterRepresentation::Word32();
    }
    default:
      return false;
  }
}

template <>
bool InstructionSelectorT<TurbofanAdapter>::ZeroExtendsWord32ToWord64NoPhis(
    Node* node) {
  DCHECK_NE(node->opcode(), IrOpcode::kPhi);
  switch (node->opcode()) {
    case IrOpcode::kWord32And:
    case IrOpcode::kWord32Or:
    case IrOpcode::kWord32Xor:
    case IrOpcode::kWord32Shl:
    case IrOpcode::kWord32Shr:
    case IrOpcode::kWord32Sar:
    case IrOpcode::kWord32Ror:
    case IrOpcode::kWord32Equal:
    case IrOpcode::kInt32Add:
    case IrOpcode::kInt32AddWithOverflow:
    case IrOpcode::kInt32Sub:
    case IrOpcode::kInt32SubWithOverflow:
    case IrOpcode::kInt32Mul:
    case IrOpcode::kInt32MulHigh:
    case IrOpcode::kInt32Div:
    case IrOpcode::kInt32Mod:
    case IrOpcode::kInt32LessThan:
    case IrOpcode::kInt32LessThanOrEqual:
    case IrOpcode::kUint32Div:
    case IrOpcode::kUint32LessThan:
    case IrOpcode::kUint32LessThanOrEqual:
    case IrOpcode::kUint32Mod:
    case IrOpcode::kUint32MulHigh: {
      // 32-bit operations will write their result in a W register (implicitly
      // clearing the top 32-bit of the corresponding X register) so the
      // zero-extension is a no-op.
      return true;
    }
    case IrOpcode::kLoad:
    case IrOpcode::kLoadImmutable: {
      // As for the operations above, a 32-bit load will implicitly clear the
      // top 32 bits of the destination register.
      LoadRepresentation load_rep = LoadRepresentationOf(node->op());
      switch (load_rep.representation()) {
        case MachineRepresentation::kWord8:
        case MachineRepresentation::kWord16:
        case MachineRepresentation::kWord32:
          return true;
        default:
          return false;
      }
    }
    default:
      return false;
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToUint64(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  node_t value = this->input_at(node, 0);
  if (ZeroExtendsWord32ToWord64(value)) {
    return EmitIdentity(node);
  }
  Emit(kArm64Mov32, g.DefineAsRegister(node), g.UseRegister(value));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateInt64ToInt32(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  // The top 32 bits in the 64-bit register will be undefined, and
  // must not be used by a dependent node.
  EmitIdentity(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Emit(kArm64Float64Mod, g.DefineAsFixed(node, d0),
       g.UseFixed(this->input_at(node, 0), d0),
       g.UseFixed(this->input_at(node, 1), d1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, d0),
       g.UseFixed(this->input_at(node, 0), d0),
       g.UseFixed(this->input_at(node, 1), d1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  Arm64OperandGeneratorT<Adapter> g(this);
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
  Arm64OperandGeneratorT<Adapter> g(this);

  // `arguments` includes alignment "holes". This means that slots bigger than
  // kSystemPointerSize, e.g. Simd128, will span across multiple arguments.
  int claim_count = static_cast<int>(arguments->size());
  bool needs_padding = claim_count % 2 != 0;
  int slot = claim_count - 1;
  claim_count = RoundUp(claim_count, 2);
  // Bump the stack pointer.
  if (claim_count > 0) {
    // TODO(titzer): claim and poke probably take small immediates.
    // TODO(titzer): it would be better to bump the sp here only
    //               and emit paired stores with increment for non c frames.
    Emit(kArm64Claim, g.NoOutput(), g.TempImmediate(claim_count));

    if (needs_padding) {
      Emit(kArm64Poke, g.NoOutput(), g.UseImmediate(0),
           g.TempImmediate(claim_count - 1));
    }
  }

  // Poke the arguments into the stack.
  while (slot >= 0) {
    PushParameter input0 = (*arguments)[slot];
    // Skip holes in the param array. These represent both extra slots for
    // multi-slot values and padding slots for alignment.
    if (!this->valid(input0.node)) {
      slot--;
      continue;
    }
    PushParameter input1 = slot > 0 ? (*arguments)[slot - 1] : PushParameter();
    // Emit a poke-pair if consecutive parameters have the same type.
    // TODO(arm): Support consecutive Simd128 parameters.
    if (this->valid(input1.node) &&
        input0.location.GetType() == input1.location.GetType()) {
      Emit(kArm64PokePair, g.NoOutput(), g.UseRegister(input0.node),
           g.UseRegister(input1.node), g.TempImmediate(slot));
      slot -= 2;
    } else {
      Emit(kArm64Poke, g.NoOutput(), g.UseRegister(input0.node),
           g.TempImmediate(slot));
      slot--;
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);

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
      Emit(kArm64Peek, g.DefineAsRegister(output.node),
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
  if (cont->IsSelect()) {
    Arm64OperandGeneratorT<Adapter> g(selector);
    InstructionOperand inputs[] = {
        left, right, g.UseRegisterOrImmediateZero(cont->true_value()),
        g.UseRegisterOrImmediateZero(cont->false_value())};
    selector->EmitWithContinuation(opcode, 0, nullptr, 4, inputs, cont);
  } else {
    selector->EmitWithContinuation(opcode, left, right, cont);
  }
}

// This function checks whether we can convert:
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

// This function checks if we can perform the transformation:
// ((a <op> b) cmp 0), b.<cond>
// to:
// (a <ops> b), b.<cond'>
// where <ops> is the flag setting version of <op>, and if so,
// updates {node}, {opcode} and {cont} accordingly.
template <typename Adapter>
void MaybeReplaceCmpZeroWithFlagSettingBinop(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t* node,
    typename Adapter::node_t binop, ArchOpcode* opcode, FlagsCondition cond,
    FlagsContinuationT<Adapter>* cont, ImmediateMode* immediate_mode) {
  ArchOpcode binop_opcode;
  ArchOpcode no_output_opcode;
  ImmediateMode binop_immediate_mode;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& op = selector->Get(binop);
    if (op.Is<Opmask::kWord32Add>()) {
      binop_opcode = kArm64Add32;
      no_output_opcode = kArm64Cmn32;
      binop_immediate_mode = kArithmeticImm;
    } else if (op.Is<Opmask::kWord32BitwiseAnd>()) {
      binop_opcode = kArm64And32;
      no_output_opcode = kArm64Tst32;
      binop_immediate_mode = kLogical32Imm;
    } else {
      UNREACHABLE();
    }
  } else {
    switch (binop->opcode()) {
      case IrOpcode::kInt32Add:
        binop_opcode = kArm64Add32;
        no_output_opcode = kArm64Cmn32;
        binop_immediate_mode = kArithmeticImm;
        break;
      case IrOpcode::kWord32And:
        binop_opcode = kArm64And32;
        no_output_opcode = kArm64Tst32;
        binop_immediate_mode = kLogical32Imm;
        break;
      default:
        UNREACHABLE();
    }
  }
  if (selector->CanCover(*node, binop)) {
    // The comparison is the only user of the add or and, so we can generate
    // a cmn or tst instead.
    cont->Overwrite(MapForFlagSettingBinop(cond));
    *opcode = no_output_opcode;
    *node = binop;
    *immediate_mode = binop_immediate_mode;
  } else if (selector->IsOnlyUserOfNodeInSameBlock(*node, binop)) {
    // We can also handle the case where the add and the compare are in the
    // same basic block, and the compare is the only use of add in this basic
    // block (the add has users in other basic blocks).
    cont->Overwrite(MapForFlagSettingBinop(cond));
    *opcode = binop_opcode;
    *node = binop;
    *immediate_mode = binop_immediate_mode;
  }
}

// Map {cond} to kEqual or kNotEqual, so that we can select
// either TBZ or TBNZ when generating code for:
// (x cmp 0), b.{cond}
FlagsCondition MapForTbz(FlagsCondition cond) {
  switch (cond) {
    case kSignedLessThan:  // generate TBNZ
      return kNotEqual;
    case kSignedGreaterThanOrEqual:  // generate TBZ
      return kEqual;
    default:
      UNREACHABLE();
  }
}

// Map {cond} to kEqual or kNotEqual, so that we can select
// either CBZ or CBNZ when generating code for:
// (x cmp 0), b.{cond}
FlagsCondition MapForCbz(FlagsCondition cond) {
  switch (cond) {
    case kEqual:     // generate CBZ
    case kNotEqual:  // generate CBNZ
      return cond;
    case kUnsignedLessThanOrEqual:  // generate CBZ
      return kEqual;
    case kUnsignedGreaterThan:  // generate CBNZ
      return kNotEqual;
    default:
      UNREACHABLE();
  }
}

template <typename Adapter>
void EmitBranchOrDeoptimize(InstructionSelectorT<Adapter>* selector,
                            InstructionCode opcode, InstructionOperand value,
                            FlagsContinuationT<Adapter>* cont) {
  DCHECK(cont->IsBranch() || cont->IsDeoptimize());
  selector->EmitWithContinuation(opcode, value, cont);
}

template <int N>
struct CbzOrTbzMatchTrait {};

template <>
struct CbzOrTbzMatchTrait<32> {
  using IntegralType = uint32_t;
  using BinopMatcher = Int32BinopMatcher;
  static constexpr IrOpcode::Value kAndOpcode = IrOpcode::kWord32And;
  static constexpr ArchOpcode kTestAndBranchOpcode = kArm64TestAndBranch32;
  static constexpr ArchOpcode kCompareAndBranchOpcode =
      kArm64CompareAndBranch32;
  static constexpr unsigned kSignBit = kWSignBit;
};

template <>
struct CbzOrTbzMatchTrait<64> {
  using IntegralType = uint64_t;
  using BinopMatcher = Int64BinopMatcher;
  static constexpr IrOpcode::Value kAndOpcode = IrOpcode::kWord64And;
  static constexpr ArchOpcode kTestAndBranchOpcode = kArm64TestAndBranch;
  static constexpr ArchOpcode kCompareAndBranchOpcode = kArm64CompareAndBranch;
  static constexpr unsigned kSignBit = kXSignBit;
};

// Try to emit TBZ, TBNZ, CBZ or CBNZ for certain comparisons of {node}
// against {value}, depending on the condition.
template <typename Adapter, int N>
bool TryEmitCbzOrTbz(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node,
                     typename CbzOrTbzMatchTrait<N>::IntegralType value,
                     typename Adapter::node_t user, FlagsCondition cond,
                     FlagsContinuationT<Adapter>* cont) {
  // Only handle branches and deoptimisations.
  if (!cont->IsBranch() && !cont->IsDeoptimize()) return false;

  switch (cond) {
    case kSignedLessThan:
    case kSignedGreaterThanOrEqual: {
      // Here we handle sign tests, aka. comparisons with zero.
      if (value != 0) return false;
      // We don't generate TBZ/TBNZ for deoptimisations, as they have a
      // shorter range than conditional branches and generating them for
      // deoptimisations results in more veneers.
      if (cont->IsDeoptimize()) return false;
      Arm64OperandGeneratorT<Adapter> g(selector);
      cont->Overwrite(MapForTbz(cond));

      if (N == 32) {
        if constexpr (Adapter::IsTurboshaft) {
          using namespace turboshaft;  // NOLINT(build/namespaces)
          const Operation& op = selector->Get(node);
          if (op.Is<Opmask::kFloat64ExtractHighWord32>() &&
              selector->CanCover(user, node)) {
            // SignedLessThan(Float64ExtractHighWord32(x), 0) and
            // SignedGreaterThanOrEqual(Float64ExtractHighWord32(x), 0)
            // essentially check the sign bit of a 64-bit floating point value.
            InstructionOperand temp = g.TempRegister();
            selector->Emit(kArm64U64MoveFloat64, temp,
                           g.UseRegister(selector->input_at(node, 0)));
            selector->EmitWithContinuation(kArm64TestAndBranch, temp,
                                           g.TempImmediate(kDSignBit), cont);
            return true;
          }
        } else {
          Int32Matcher m(node);
          if (m.IsFloat64ExtractHighWord32() &&
              selector->CanCover(user, node)) {
            // SignedLessThan(Float64ExtractHighWord32(x), 0) and
            // SignedGreaterThanOrEqual(Float64ExtractHighWord32(x), 0)
            // essentially check the sign bit of a 64-bit floating point value.
            InstructionOperand temp = g.TempRegister();
            selector->Emit(kArm64U64MoveFloat64, temp,
                           g.UseRegister(node->InputAt(0)));
            selector->EmitWithContinuation(kArm64TestAndBranch, temp,
                                           g.TempImmediate(kDSignBit), cont);
            return true;
          }
        }
      }

      selector->EmitWithContinuation(
          CbzOrTbzMatchTrait<N>::kTestAndBranchOpcode, g.UseRegister(node),
          g.TempImmediate(CbzOrTbzMatchTrait<N>::kSignBit), cont);
      return true;
    }
    case kEqual:
    case kNotEqual: {
      if constexpr (Adapter::IsTurboshaft) {
        using namespace turboshaft;  // NOLINT(build/namespaces)
        const Operation& op = selector->Get(node);
        if (const WordBinopOp* bitwise_and =
                op.TryCast<Opmask::kBitwiseAnd>()) {
          // Emit a tbz/tbnz if we are comparing with a single-bit mask:
          //   Branch(WordEqual(WordAnd(x, 1 << N), 1 << N), true, false)
          uint64_t actual_value;
          if (cont->IsBranch() && base::bits::IsPowerOfTwo(value) &&
              selector->MatchUnsignedIntegralConstant(bitwise_and->right(),
                                                      &actual_value) &&
              actual_value == value && selector->CanCover(user, node)) {
            Arm64OperandGeneratorT<Adapter> g(selector);
            // In the code generator, Equal refers to a bit being cleared. We
            // want the opposite here so negate the condition.
            cont->Negate();
            selector->EmitWithContinuation(
                CbzOrTbzMatchTrait<N>::kTestAndBranchOpcode,
                g.UseRegister(bitwise_and->left()),
                g.TempImmediate(base::bits::CountTrailingZeros(value)), cont);
            return true;
          }
        }
      } else {
        if (node->opcode() == CbzOrTbzMatchTrait<N>::kAndOpcode) {
          // Emit a tbz/tbnz if we are comparing with a single-bit mask:
          //   Branch(WordEqual(WordAnd(x, 1 << N), 1 << N), true, false)
          typename CbzOrTbzMatchTrait<N>::BinopMatcher m_and(node);
          if (cont->IsBranch() && base::bits::IsPowerOfTwo(value) &&
              m_and.right().Is(value) && selector->CanCover(user, node)) {
            Arm64OperandGeneratorT<Adapter> g(selector);
            // In the code generator, Equal refers to a bit being cleared. We
            // want the opposite here so negate the condition.
            cont->Negate();
            selector->EmitWithContinuation(
                CbzOrTbzMatchTrait<N>::kTestAndBranchOpcode,
                g.UseRegister(m_and.left().node()),
                g.TempImmediate(base::bits::CountTrailingZeros(value)), cont);
            return true;
          }
        }
      }
      [[fallthrough]];
    }
    case kUnsignedLessThanOrEqual:
    case kUnsignedGreaterThan: {
      if (value != 0) return false;
      Arm64OperandGeneratorT<Adapter> g(selector);
      cont->Overwrite(MapForCbz(cond));
      EmitBranchOrDeoptimize(selector,
                             CbzOrTbzMatchTrait<N>::kCompareAndBranchOpcode,
                             g.UseRegister(node), cont);
      return true;
    }
    default:
      return false;
  }
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont,
                      ImmediateMode immediate_mode) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  // If one of the two inputs is an immediate, make sure it's on the right.
  if (!g.CanBeImmediate(right, immediate_mode) &&
      g.CanBeImmediate(left, immediate_mode)) {
    cont->Commute();
    std::swap(left, right);
  }

  if (opcode == kArm64Cmp && selector->is_constant(right)) {
    auto constant = selector->constant_view(right);
    if (g.IsIntegerConstant(constant)) {
      if (TryEmitCbzOrTbz<Adapter, 64>(selector, left,
                                       g.GetIntegerConstantValue(constant),
                                       node, cont->condition(), cont)) {
        return;
      }
    }
  }

  VisitCompare(selector, opcode, g.UseRegister(left),
               g.UseOperand(right, immediate_mode), cont);
}

template <typename Adapter>
void VisitWord32Compare(InstructionSelectorT<Adapter>* selector,
                        typename Adapter::node_t node,
                        FlagsContinuationT<Adapter>* cont) {
  {
    Int32BinopMatcher m(node);
    FlagsCondition cond = cont->condition();
    if (m.right().HasResolvedValue()) {
      if (TryEmitCbzOrTbz<Adapter, 32>(selector, m.left().node(),
                                       m.right().ResolvedValue(), node, cond,
                                       cont)) {
        return;
      }
    } else if (m.left().HasResolvedValue()) {
      FlagsCondition commuted_cond = CommuteFlagsCondition(cond);
      if (TryEmitCbzOrTbz<Adapter, 32>(selector, m.right().node(),
                                       m.left().ResolvedValue(), node,
                                       commuted_cond, cont)) {
        return;
      }
    }
    ArchOpcode opcode = kArm64Cmp32;
    ImmediateMode immediate_mode = kArithmeticImm;
    if (m.right().Is(0) && (m.left().IsInt32Add() || m.left().IsWord32And())) {
      // Emit flag setting add/and instructions for comparisons against zero.
      if (CanUseFlagSettingBinop(cond)) {
        Node* binop = m.left().node();
        MaybeReplaceCmpZeroWithFlagSettingBinop(selector, &node, binop, &opcode,
                                                cond, cont, &immediate_mode);
      }
    } else if (m.left().Is(0) &&
               (m.right().IsInt32Add() || m.right().IsWord32And())) {
      // Same as above, but we need to commute the condition before we
      // continue with the rest of the checks.
      FlagsCondition commuted_cond = CommuteFlagsCondition(cond);
      if (CanUseFlagSettingBinop(commuted_cond)) {
        Node* binop = m.right().node();
        MaybeReplaceCmpZeroWithFlagSettingBinop(selector, &node, binop, &opcode,
                                                commuted_cond, cont,
                                                &immediate_mode);
      }
    } else if (m.right().IsInt32Sub() &&
               (cond == kEqual || cond == kNotEqual)) {
      // Select negated compare for comparisons with negated right input.
      // Only do this for kEqual and kNotEqual, which do not depend on the
      // C and V flags, as those flags will be different with CMN when the
      // right-hand side of the original subtraction is INT_MIN.
      Node* sub = m.right().node();
      Int32BinopMatcher msub(sub);
      if (msub.left().Is(0)) {
        bool can_cover = selector->CanCover(node, sub);
        node->ReplaceInput(1, msub.right().node());
        // Even if the comparison node covers the subtraction, after the input
        // replacement above, the node still won't cover the input to the
        // subtraction; the subtraction still uses it.
        // In order to get shifted operations to work, we must remove the rhs
        // input to the subtraction, as TryMatchAnyShift requires this node to
        // cover the input shift. We do this by setting it to the lhs input,
        // as we know it's zero, and the result of the subtraction isn't used by
        // any other node.
        if (can_cover) sub->ReplaceInput(1, msub.left().node());
        opcode = kArm64Cmn32;
      }
    }
    VisitBinop<Adapter, Int32BinopMatcher>(selector, node, opcode,
                                           immediate_mode, cont);
  }
}

template <>
void VisitWord32Compare(InstructionSelectorT<TurboshaftAdapter>* selector,
                        typename TurboshaftAdapter::node_t node,
                        FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& compare = selector->Get(node);
  DCHECK_GE(compare.input_count, 2);
  OpIndex lhs = compare.input(0);
  OpIndex rhs = compare.input(1);
  FlagsCondition cond = cont->condition();

  if (selector->is_integer_constant(rhs) &&
      TryEmitCbzOrTbz<TurboshaftAdapter, 32>(
          selector, lhs, static_cast<uint32_t>(selector->integer_constant(rhs)),
          node, cond, cont)) {
    return;
  }
  if (selector->is_integer_constant(lhs) &&
      TryEmitCbzOrTbz<TurboshaftAdapter, 32>(
          selector, rhs, static_cast<uint32_t>(selector->integer_constant(lhs)),
          node, CommuteFlagsCondition(cond), cont)) {
    return;
  }

  const Operation& left = selector->Get(lhs);
  const Operation& right = selector->Get(rhs);
  ArchOpcode opcode = kArm64Cmp32;
  ImmediateMode immediate_mode = kArithmeticImm;

  if (selector->MatchIntegralZero(rhs) &&
      (left.Is<Opmask::kWord32Add>() || left.Is<Opmask::kWord32BitwiseAnd>())) {
    // Emit flag setting add/and instructions for comparisons against zero.
    if (CanUseFlagSettingBinop(cond)) {
      MaybeReplaceCmpZeroWithFlagSettingBinop(selector, &node, lhs, &opcode,
                                              cond, cont, &immediate_mode);
    }
  } else if (selector->MatchIntegralZero(lhs) &&
             (right.Is<Opmask::kWord32Add>() ||
              right.Is<Opmask::kWord32BitwiseAnd>())) {
    // Same as above, but we need to commute the condition before we
    // continue with the rest of the checks.
    FlagsCondition commuted_cond = CommuteFlagsCondition(cond);
    if (CanUseFlagSettingBinop(commuted_cond)) {
      MaybeReplaceCmpZeroWithFlagSettingBinop(
          selector, &node, rhs, &opcode, commuted_cond, cont, &immediate_mode);
    }
  } else if (right.Is<Opmask::kWord32Sub>() &&
             (cond == kEqual || cond == kNotEqual)) {
    const WordBinopOp& sub = right.Cast<WordBinopOp>();
    if (selector->MatchIntegralZero(sub.left())) {
      // For a given compare(x, 0 - y) where compare is kEqual or kNotEqual,
      // it can be expressed as cmn(x, y).
      opcode = kArm64Cmn32;
      VisitBinopImpl(selector, node, lhs, sub.right(),
                     RegisterRepresentation::Word32(), opcode, immediate_mode,
                     cont);
      return;
    }
  }
  VisitBinop(selector, node, RegisterRepresentation::Word32(), opcode,
             immediate_mode, cont);
}

template <typename Adapter>
void VisitWordTest(InstructionSelectorT<Adapter>* selector,
                   typename Adapter::node_t node, InstructionCode opcode,
                   FlagsContinuationT<Adapter>* cont) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  VisitCompare(selector, opcode, g.UseRegister(node), g.UseRegister(node),
               cont);
}

template <typename Adapter>
void VisitWord32Test(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node,
                     FlagsContinuationT<Adapter>* cont) {
  VisitWordTest(selector, node, kArm64Tst32, cont);
}

template <typename Adapter>
void VisitWord64Test(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node,
                     FlagsContinuationT<Adapter>* cont) {
  VisitWordTest(selector, node, kArm64Tst, cont);
}

template <typename Adapter, typename Matcher>
struct TestAndBranchMatcher {
  TestAndBranchMatcher(Node* node, FlagsContinuationT<Adapter>* cont)
      : matches_(false), cont_(cont), matcher_(node) {
    Initialize();
  }
  bool Matches() const { return matches_; }

  unsigned bit() const {
    DCHECK(Matches());
    return base::bits::CountTrailingZeros(matcher_.right().ResolvedValue());
  }

  Node* input() const {
    DCHECK(Matches());
    return matcher_.left().node();
  }

 private:
  bool matches_;
  FlagsContinuationT<Adapter>* cont_;
  Matcher matcher_;

  void Initialize() {
    if (cont_->IsBranch() && matcher_.right().HasResolvedValue() &&
        base::bits::IsPowerOfTwo(matcher_.right().ResolvedValue())) {
      // If the mask has only one bit set, we can use tbz/tbnz.
      DCHECK((cont_->condition() == kEqual) ||
             (cont_->condition() == kNotEqual));
      matches_ = true;
    } else {
      matches_ = false;
    }
  }
};

struct TestAndBranchMatcherTurboshaft {
  TestAndBranchMatcherTurboshaft(
      InstructionSelectorT<TurboshaftAdapter>* selector,
      const turboshaft::WordBinopOp& binop)
      : selector_(selector), binop_(binop) {
    Initialize();
  }

  bool Matches() const { return matches_; }

  unsigned bit() const {
    DCHECK(Matches());
    return bit_;
  }

 private:
  void Initialize() {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    if (binop_.kind != WordBinopOp::Kind::kBitwiseAnd) return;
    uint64_t value{0};
    if (!selector_->MatchUnsignedIntegralConstant(binop_.right(), &value) ||
        !base::bits::IsPowerOfTwo(value)) {
      return;
    }
    // All preconditions for TBZ/TBNZ matched.
    matches_ = true;
    bit_ = base::bits::CountTrailingZeros(value);
  }

  InstructionSelectorT<TurboshaftAdapter>* selector_;
  const turboshaft::WordBinopOp& binop_;
  bool matches_ = false;
  unsigned bit_ = 0;
};

// Shared routine for multiple float32 compare operations.
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& op = selector->Get(node).template Cast<ComparisonOp>();
    OpIndex left = op.left();
    OpIndex right = op.right();
    if (selector->MatchZero(right)) {
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(left),
                   g.UseImmediate(right), cont);
    } else if (selector->MatchZero(left)) {
      cont->Commute();
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(right),
                   g.UseImmediate(left), cont);
    } else {
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(left),
                   g.UseRegister(right), cont);
    }
  } else {
    Float32BinopMatcher m(node);
    if (m.right().Is(0.0f)) {
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(m.left().node()),
                   g.UseImmediate(m.right().node()), cont);
    } else if (m.left().Is(0.0f)) {
      cont->Commute();
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(m.right().node()),
                   g.UseImmediate(m.left().node()), cont);
    } else {
      VisitCompare(selector, kArm64Float32Cmp, g.UseRegister(m.left().node()),
                   g.UseRegister(m.right().node()), cont);
    }
  }
}

// Shared routine for multiple float64 compare operations.
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  if constexpr (Adapter::IsTurboshaft) {
    Arm64OperandGeneratorT<Adapter> g(selector);
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Operation& compare = selector->Get(node);
    DCHECK(compare.Is<ComparisonOp>());
    OpIndex lhs = compare.input(0);
    OpIndex rhs = compare.input(1);
    if (selector->MatchZero(rhs)) {
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(lhs),
                   g.UseImmediate(rhs), cont);
    } else if (selector->MatchZero(lhs)) {
      cont->Commute();
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(rhs),
                   g.UseImmediate(lhs), cont);
    } else {
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(lhs),
                   g.UseRegister(rhs), cont);
    }
  } else {
    Arm64OperandGeneratorT<Adapter> g(selector);
    Float64BinopMatcher m(node);
    if (m.right().Is(0.0)) {
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(m.left().node()),
                   g.UseImmediate(m.right().node()), cont);
    } else if (m.left().Is(0.0)) {
      cont->Commute();
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(m.right().node()),
                   g.UseImmediate(m.left().node()), cont);
    } else {
      VisitCompare(selector, kArm64Float64Cmp, g.UseRegister(m.left().node()),
                   g.UseRegister(m.right().node()), cont);
    }
  }
}

template <typename Adapter>
void VisitAtomicExchange(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode,
                         AtomicWidth width, MemoryAccessKind access_kind) {
  using node_t = typename Adapter::node_t;
  auto atomic_op = selector->atomic_rmw_view(node);
  Arm64OperandGeneratorT<Adapter> g(selector);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();
  InstructionOperand inputs[] = {g.UseRegister(base), g.UseRegister(index),
                                 g.UseUniqueRegister(value)};
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  InstructionCode code = opcode | AddressingModeField::encode(kMode_MRR) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  if (CpuFeatures::IsSupported(LSE)) {
    InstructionOperand temps[] = {g.TempRegister()};
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  } else {
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  }
}

template <typename Adapter>
void VisitAtomicCompareExchange(InstructionSelectorT<Adapter>* selector,
                                typename Adapter::node_t node,
                                ArchOpcode opcode, AtomicWidth width,
                                MemoryAccessKind access_kind) {
  using node_t = typename Adapter::node_t;
  Arm64OperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t old_value = atomic_op.expected();
  node_t new_value = atomic_op.value();
  InstructionOperand inputs[] = {g.UseRegister(base), g.UseRegister(index),
                                 g.UseUniqueRegister(old_value),
                                 g.UseUniqueRegister(new_value)};
  InstructionOperand outputs[1];
  InstructionCode code = opcode | AddressingModeField::encode(kMode_MRR) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  if (CpuFeatures::IsSupported(LSE)) {
    InstructionOperand temps[] = {g.TempRegister()};
    outputs[0] = g.DefineSameAsInput(node, 2);
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  } else {
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    outputs[0] = g.DefineAsRegister(node);
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  }
}

template <typename Adapter>
void VisitAtomicLoad(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node, AtomicWidth width) {
  using node_t = typename Adapter::node_t;
  Arm64OperandGeneratorT<Adapter> g(selector);
  auto load = selector->load_view(node);
  node_t base = load.base();
  node_t index = load.index();
  InstructionOperand inputs[] = {g.UseRegister(base), g.UseRegister(index)};
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  InstructionOperand temps[] = {g.TempRegister()};

  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit LDAR.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  LoadRepresentation load_rep = load.loaded_rep();
  InstructionCode code;
  switch (load_rep.representation()) {
    case MachineRepresentation::kWord8:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt8 : kAtomicLoadUint8;
      break;
    case MachineRepresentation::kWord16:
      DCHECK_IMPLIES(load_rep.IsSigned(), width == AtomicWidth::kWord32);
      code = load_rep.IsSigned() ? kAtomicLoadInt16 : kAtomicLoadUint16;
      break;
    case MachineRepresentation::kWord32:
      code = kAtomicLoadWord32;
      break;
    case MachineRepresentation::kWord64:
      code = kArm64Word64AtomicLoadUint64;
      break;
#ifdef V8_COMPRESS_POINTERS
    case MachineRepresentation::kTaggedSigned:
      code = kArm64LdarDecompressTaggedSigned;
      break;
    case MachineRepresentation::kTaggedPointer:
      code = kArm64LdarDecompressTagged;
      break;
    case MachineRepresentation::kTagged:
      code = kArm64LdarDecompressTagged;
      break;
#else
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:
      if (kTaggedSize == 8) {
        code = kArm64Word64AtomicLoadUint64;
      } else {
        code = kAtomicLoadWord32;
      }
      break;
#endif
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
      DCHECK(COMPRESS_POINTERS_BOOL);
      code = kAtomicLoadWord32;
      break;
    default:
      UNREACHABLE();
  }

  bool traps_on_null;
  if (load.is_protected(&traps_on_null)) {
    // Atomic loads and null dereference are mutually exclusive. This might
    // change with multi-threaded wasm-gc in which case the access mode should
    // probably be kMemoryAccessProtectedNullDereference.
    DCHECK(!traps_on_null);
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  code |=
      AddressingModeField::encode(kMode_MRR) | AtomicWidthField::encode(width);
  selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                 arraysize(temps), temps);
}

template <typename Adapter>
AtomicStoreParameters AtomicStoreParametersOf(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t node) {
  auto store = selector->store_view(node);
  return AtomicStoreParameters(store.stored_rep().representation(),
                               store.stored_rep().write_barrier_kind(),
                               store.memory_order().value(),
                               store.access_kind());
}

template <typename Adapter>
void VisitAtomicStore(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, AtomicWidth width) {
  using node_t = typename Adapter::node_t;
  Arm64OperandGeneratorT<Adapter> g(selector);
  auto store = selector->store_view(node);
  node_t base = store.base();
  node_t index = selector->value(store.index());
  node_t value = store.value();
  DCHECK_EQ(store.displacement(), 0);

  // The memory order is ignored as both release and sequentially consistent
  // stores can emit STLR.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  AtomicStoreParameters store_params = AtomicStoreParametersOf(selector, node);
  WriteBarrierKind write_barrier_kind = store_params.write_barrier_kind();
  MachineRepresentation rep = store_params.representation();

  if (v8_flags.enable_unconditional_write_barriers &&
      CanBeTaggedOrCompressedPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  InstructionOperand inputs[] = {g.UseRegister(base), g.UseRegister(index),
                                 g.UseUniqueRegister(value)};
  InstructionOperand temps[] = {g.TempRegister()};
  InstructionCode code;

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedOrCompressedPointer(rep));
    DCHECK_EQ(AtomicWidthSize(width), kTaggedSize);

    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    code = kArchAtomicStoreWithWriteBarrier;
    code |= RecordWriteModeField::encode(record_write_mode);
  } else {
    switch (rep) {
      case MachineRepresentation::kWord8:
        code = kAtomicStoreWord8;
        break;
      case MachineRepresentation::kWord16:
        code = kAtomicStoreWord16;
        break;
      case MachineRepresentation::kWord32:
        code = kAtomicStoreWord32;
        break;
      case MachineRepresentation::kWord64:
        DCHECK_EQ(width, AtomicWidth::kWord64);
        code = kArm64Word64AtomicStoreWord64;
        break;
      case MachineRepresentation::kTaggedSigned:   // Fall through.
      case MachineRepresentation::kTaggedPointer:  // Fall through.
      case MachineRepresentation::kTagged:
        DCHECK_EQ(AtomicWidthSize(width), kTaggedSize);
        code = kArm64StlrCompressTagged;
        break;
      case MachineRepresentation::kCompressedPointer:  // Fall through.
      case MachineRepresentation::kCompressed:
        CHECK(COMPRESS_POINTERS_BOOL);
        DCHECK_EQ(width, AtomicWidth::kWord32);
        code = kArm64StlrCompressTagged;
        break;
      default:
        UNREACHABLE();
    }
    code |= AtomicWidthField::encode(width);
  }

  if (store_params.kind() == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  code |= AddressingModeField::encode(kMode_MRR);
  selector->Emit(code, 0, nullptr, arraysize(inputs), inputs, arraysize(temps),
                 temps);
}

template <typename Adapter>
void VisitAtomicBinop(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode,
                      AtomicWidth width, MemoryAccessKind access_kind) {
  using node_t = typename Adapter::node_t;
  Arm64OperandGeneratorT<Adapter> g(selector);
  auto atomic_op = selector->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t value = atomic_op.value();
  AddressingMode addressing_mode = kMode_MRR;
  InstructionOperand inputs[] = {g.UseRegister(base), g.UseRegister(index),
                                 g.UseUniqueRegister(value)};
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  if (CpuFeatures::IsSupported(LSE)) {
    InstructionOperand temps[] = {g.TempRegister()};
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  } else {
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister(),
                                  g.TempRegister()};
    selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                   arraysize(temps), temps);
  }
}

}  // namespace

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  {
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    // Try to combine with comparisons against 0 by simply inverting the branch.
    while (value->opcode() == IrOpcode::kWord32Equal && CanCover(user, value)) {
      Int32BinopMatcher m(value);
      if (!m.right().Is(0)) break;

      user = value;
      value = m.left().node();
      cont->Negate();
    }

    // Try to match bit checks to create TBZ/TBNZ instructions.
    // Unlike the switch below, CanCover check is not needed here.
    // If there are several uses of the given operation, we will generate a TBZ
    // instruction for each. This is useful even if there are other uses of the
    // arithmetic result, because it moves dependencies further back.
    switch (value->opcode()) {
      case IrOpcode::kWord64Equal: {
        Int64BinopMatcher m(value);
        if (m.right().Is(0)) {
          Node* const left = m.left().node();
          if (left->opcode() == IrOpcode::kWord64And) {
            // Attempt to merge the Word64Equal(Word64And(x, y), 0) comparison
            // into a tbz/tbnz instruction.
            TestAndBranchMatcher<TurbofanAdapter, Uint64BinopMatcher> tbm(left,
                                                                          cont);
            if (tbm.Matches()) {
              Arm64OperandGeneratorT<TurbofanAdapter> gen(this);
              cont->OverwriteAndNegateIfEqual(kEqual);
              this->EmitWithContinuation(kArm64TestAndBranch,
                                         gen.UseRegister(tbm.input()),
                                         gen.TempImmediate(tbm.bit()), cont);
              return;
            }
          }
        }
        break;
      }
      case IrOpcode::kWord32And: {
        TestAndBranchMatcher<TurbofanAdapter, Uint32BinopMatcher> tbm(value,
                                                                      cont);
        if (tbm.Matches()) {
          Arm64OperandGeneratorT<TurbofanAdapter> gen(this);
          this->EmitWithContinuation(kArm64TestAndBranch32,
                                     gen.UseRegister(tbm.input()),
                                     gen.TempImmediate(tbm.bit()), cont);
          return;
        }
        break;
      }
      case IrOpcode::kWord64And: {
        TestAndBranchMatcher<TurbofanAdapter, Uint64BinopMatcher> tbm(value,
                                                                      cont);
        if (tbm.Matches()) {
          Arm64OperandGeneratorT<TurbofanAdapter> gen(this);
          this->EmitWithContinuation(kArm64TestAndBranch,
                                     gen.UseRegister(tbm.input()),
                                     gen.TempImmediate(tbm.bit()), cont);
          return;
        }
        break;
      }
      default:
        break;
    }

    if (CanCover(user, value)) {
      switch (value->opcode()) {
        case IrOpcode::kWord32Equal:
          cont->OverwriteAndNegateIfEqual(kEqual);
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kInt32LessThan:
          cont->OverwriteAndNegateIfEqual(kSignedLessThan);
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kInt32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kUint32LessThan:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kUint32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kWord64Equal: {
          cont->OverwriteAndNegateIfEqual(kEqual);
          Int64BinopMatcher m(value);
          if (m.right().Is(0)) {
            Node* const left = m.left().node();
            if (CanCover(value, left) &&
                left->opcode() == IrOpcode::kWord64And) {
              return VisitWordCompare(this, left, kArm64Tst, cont,
                                      kLogical64Imm);
            }
          }
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);
        }
        case IrOpcode::kInt64LessThan:
          cont->OverwriteAndNegateIfEqual(kSignedLessThan);
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);
        case IrOpcode::kInt64LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);
        case IrOpcode::kUint64LessThan:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);
        case IrOpcode::kUint64LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);
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
            if (result == nullptr || IsDefined(result)) {
              switch (node->opcode()) {
                case IrOpcode::kInt32AddWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                      this, node, kArm64Add32, kArithmeticImm, cont);
                case IrOpcode::kInt32SubWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop<TurbofanAdapter, Int32BinopMatcher>(
                      this, node, kArm64Sub32, kArithmeticImm, cont);
                case IrOpcode::kInt32MulWithOverflow:
                  // ARM64 doesn't set the overflow flag for multiplication, so
                  // we need to test on kNotEqual. Here is the code sequence
                  // used:
                  //   smull result, left, right
                  //   cmp result.X(), Operand(result, SXTW)
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt32MulWithOverflow(this, node, cont);
                case IrOpcode::kInt64AddWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop<TurbofanAdapter, Int64BinopMatcher>(
                      this, node, kArm64Add, kArithmeticImm, cont);
                case IrOpcode::kInt64SubWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop<TurbofanAdapter, Int64BinopMatcher>(
                      this, node, kArm64Sub, kArithmeticImm, cont);
                case IrOpcode::kInt64MulWithOverflow:
                  // ARM64 doesn't set the overflow flag for multiplication, so
                  // we need to test on kNotEqual. Here is the code sequence
                  // used:
                  //   mul result, left, right
                  //   smulh high, left, right
                  //   cmp high, result, asr 63
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt64MulWithOverflow(this, node, cont);
                default:
                  break;
              }
            }
          }
          break;
        case IrOpcode::kInt32Add:
          return VisitWordCompare(this, value, kArm64Cmn32, cont,
                                  kArithmeticImm);
        case IrOpcode::kInt32Sub:
          return VisitWord32Compare(this, value, cont);
        case IrOpcode::kWord32And:
          return VisitWordCompare(this, value, kArm64Tst32, cont,
                                  kLogical32Imm);
        case IrOpcode::kWord64And:
          return VisitWordCompare(this, value, kArm64Tst, cont, kLogical64Imm);
        case IrOpcode::kStackPointerGreaterThan:
          cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
          return VisitStackPointerGreaterThan(value, cont);
        default:
          break;
      }
    }

    // Branch could not be combined with a compare, compare against 0 and
    // branch.
    if (cont->IsBranch()) {
      Emit(cont->Encode(kArm64CompareAndBranch32), g.NoOutput(),
           g.UseRegister(value), g.Label(cont->true_block()),
           g.Label(cont->false_block()));
    } else {
      VisitCompare(this, cont->Encode(kArm64Tst32), g.UseRegister(value),
                   g.UseRegister(value), cont);
    }
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  // Try to combine with comparisons against 0 by simply inverting the branch.
  ConsumeEqualZero(&user, &value, cont);

  // Remove Word64->Word32 truncation.
  if (this->is_truncate_word64_to_word32(value) && CanCover(user, value)) {
    user = value;
    value = this->remove_truncate_word64_to_word32(value);
  }

  // Try to match bit checks to create TBZ/TBNZ instructions.
  // Unlike the switch below, CanCover check is not needed here.
  // If there are several uses of the given operation, we will generate a TBZ
  // instruction for each. This is useful even if there are other uses of the
  // arithmetic result, because it moves dependencies further back.
  const Operation& value_op = Get(value);

  if (cont->IsBranch()) {
    if (value_op.Is<Opmask::kWord64Equal>()) {
      const ComparisonOp& equal = value_op.Cast<ComparisonOp>();
      if (MatchIntegralZero(equal.right())) {
        const WordBinopOp* left_binop =
            Get(equal.left()).TryCast<WordBinopOp>();
        if (left_binop) {
          TestAndBranchMatcherTurboshaft matcher(this, *left_binop);
          if (matcher.Matches()) {
            // If the mask has only one bit set, we can use tbz/tbnz.
            DCHECK((cont->condition() == kEqual) ||
                   (cont->condition() == kNotEqual));
            Arm64OperandGeneratorT<TurboshaftAdapter> gen(this);
            cont->OverwriteAndNegateIfEqual(kEqual);
            EmitWithContinuation(kArm64TestAndBranch,
                                 gen.UseRegister(left_binop->left()),
                                 gen.TempImmediate(matcher.bit()), cont);
            return;
          }
        }
      }
    }

    if (const WordBinopOp* value_binop = value_op.TryCast<WordBinopOp>()) {
      TestAndBranchMatcherTurboshaft matcher(this, *value_binop);
      if (matcher.Matches()) {
        // If the mask has only one bit set, we can use tbz/tbnz.
        DCHECK((cont->condition() == kEqual) ||
               (cont->condition() == kNotEqual));
        InstructionCode opcode = value_binop->rep.MapTaggedToWord() ==
                                         RegisterRepresentation::Word32()
                                     ? kArm64TestAndBranch32
                                     : kArm64TestAndBranch;
        Arm64OperandGeneratorT<TurboshaftAdapter> gen(this);
        EmitWithContinuation(opcode, gen.UseRegister(value_binop->left()),
                             gen.TempImmediate(matcher.bit()), cont);
        return;
      }
    }
  }

  if (CanCover(user, value)) {
    if (const ComparisonOp* comparison = value_op.TryCast<ComparisonOp>()) {
      switch (comparison->rep.MapTaggedToWord().value()) {
        case RegisterRepresentation::Word32():
          cont->OverwriteAndNegateIfEqual(
              GetComparisonFlagCondition(*comparison));
          return VisitWord32Compare(this, value, cont);

        case RegisterRepresentation::Word64():
          cont->OverwriteAndNegateIfEqual(
              GetComparisonFlagCondition(*comparison));

          if (comparison->kind == ComparisonOp::Kind::kEqual) {
            const Operation& left_op = Get(comparison->left());
            if (MatchIntegralZero(comparison->right()) &&
                left_op.Is<Opmask::kWord64BitwiseAnd>() &&
                CanCover(value, comparison->left())) {
              return VisitWordCompare(this, comparison->left(), kArm64Tst, cont,
                                      kLogical64Imm);
            }
          }
          return VisitWordCompare(this, value, kArm64Cmp, cont, kArithmeticImm);

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
            const bool is64 = binop->rep == WordRepresentation::Word64();
            switch (binop->kind) {
              case OverflowCheckedBinopOp::Kind::kSignedAdd:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, binop->rep,
                                  is64 ? kArm64Add : kArm64Add32,
                                  kArithmeticImm, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, binop->rep,
                                  is64 ? kArm64Sub : kArm64Sub32,
                                  kArithmeticImm, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                if (is64) {
                  // ARM64 doesn't set the overflow flag for multiplication, so
                  // we need to test on kNotEqual. Here is the code sequence
                  // used:
                  //   mul result, left, right
                  //   smulh high, left, right
                  //   cmp high, result, asr 63
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt64MulWithOverflow(this, node, cont);
                } else {
                  // ARM64 doesn't set the overflow flag for multiplication, so
                  // we need to test on kNotEqual. Here is the code sequence
                  // used:
                  //   smull result, left, right
                  //   cmp result.X(), Operand(result, SXTW)
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  return EmitInt32MulWithOverflow(this, node, cont);
                }
            }
          }
        }
      }
    } else if (value_op.Is<Opmask::kWord32Add>()) {
      return VisitWordCompare(this, value, kArm64Cmn32, cont, kArithmeticImm);
    } else if (value_op.Is<Opmask::kWord32Sub>()) {
      return VisitWord32Compare(this, value, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseAnd>()) {
      if (TryMatchConditionalCompareChainBranch(this, zone(), value, cont)) {
        return;
      }
      return VisitWordCompare(this, value, kArm64Tst32, cont, kLogical32Imm);
    } else if (value_op.Is<Opmask::kWord64BitwiseAnd>()) {
      return VisitWordCompare(this, value, kArm64Tst, cont, kLogical64Imm);
    } else if (value_op.Is<Opmask::kWord32BitwiseOr>()) {
      if (TryMatchConditionalCompareChainBranch(this, zone(), value, cont)) {
        return;
      }
    } else if (value_op.Is<StackPointerGreaterThanOp>()) {
      cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
      return VisitStackPointerGreaterThan(value, cont);
    }
  }

  // Branch could not be combined with a compare, compare against 0 and
  // branch.
  if (cont->IsBranch()) {
    Emit(cont->Encode(kArm64CompareAndBranch32), g.NoOutput(),
         g.UseRegister(value), g.Label(cont->true_block()),
         g.Label(cont->false_block()));
  } else {
    VisitCompare(this, cont->Encode(kArm64Tst32), g.UseRegister(value),
                 g.UseRegister(value), cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand value_operand = g.UseRegister(this->input_at(node, 0));

  // Emit either ArchTableSwitch or ArchBinarySearchSwitch.
  if (enable_switch_jump_table_ ==
      InstructionSelector::kEnableSwitchJumpTable) {
    static const size_t kMaxTableSwitchValueRange = 2 << 16;
    size_t table_space_cost = 4 + sw.value_range();
    size_t table_time_cost = 3;
    size_t lookup_space_cost = 3 + 2 * sw.case_count();
    size_t lookup_time_cost = sw.case_count();
    if (sw.case_count() > 4 &&
        table_space_cost + 3 * table_time_cost <=
            lookup_space_cost + 3 * lookup_time_cost &&
        sw.min_value() > std::numeric_limits<int32_t>::min() &&
        sw.value_range() <= kMaxTableSwitchValueRange) {
      InstructionOperand index_operand = value_operand;
      if (sw.min_value()) {
        index_operand = g.TempRegister();
        Emit(kArm64Sub32, index_operand, value_operand,
             g.TempImmediate(sw.min_value()));
      }
      // Generate a table lookup.
      return EmitTableSwitch(sw, index_operand);
    }
  }

  // Generate a tree of conditional jumps.
  return EmitBinarySearchSwitch(sw, value_operand);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Equal(node_t node) {
  {
    Node* const user = node;
    FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
    Int32BinopMatcher m(user);
    if (m.right().Is(0)) {
      Node* const value = m.left().node();
      if (CanCover(user, value)) {
        switch (value->opcode()) {
          case IrOpcode::kInt32Add:
          case IrOpcode::kWord32And:
            return VisitWord32Compare(this, node, &cont);
          case IrOpcode::kInt32Sub:
            return VisitWordCompare(this, value, kArm64Cmp32, &cont,
                                    kArithmeticImm);
          case IrOpcode::kWord32Equal: {
            // Word32Equal(Word32Equal(x, y), 0) => Word32Compare(x, y, ne).
            Int32BinopMatcher mequal(value);
            node->ReplaceInput(0, mequal.left().node());
            node->ReplaceInput(1, mequal.right().node());
            cont.Negate();
            // {node} still does not cover its new operands, because {mequal} is
            // still using them.
            // Since we won't generate any more code for {mequal}, set its
            // operands to zero to make sure {node} can cover them.
            // This improves pattern matching in VisitWord32Compare.
            mequal.node()->ReplaceInput(0, m.right().node());
            mequal.node()->ReplaceInput(1, m.right().node());
            return VisitWord32Compare(this, node, &cont);
          }
          default:
            break;
        }
        return VisitWord32Test(this, value, &cont);
      }
    }

    if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                      (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
      Arm64OperandGeneratorT<TurbofanAdapter> g(this);
      const RootsTable& roots_table = isolate()->roots_table();
      RootIndex root_index;
      Node* left = nullptr;
      Handle<HeapObject> right;
      // HeapConstants and CompressedHeapConstants can be treated the same when
      // using them as an input to a 32-bit comparison. Check whether either is
      // present.
      {
        CompressedHeapObjectBinopMatcher m(node);
        if (m.right().HasResolvedValue()) {
          left = m.left().node();
          right = m.right().ResolvedValue();
        } else {
          HeapObjectBinopMatcher m2(node);
          if (m2.right().HasResolvedValue()) {
            left = m2.left().node();
            right = m2.right().ResolvedValue();
          }
        }
      }
      if (!right.is_null() && roots_table.IsRootHandle(right, &root_index)) {
        DCHECK_NE(left, nullptr);
        if (RootsTable::IsReadOnly(root_index)) {
          Tagged_t ptr =
              MacroAssemblerBase::ReadOnlyRootPtr(root_index, isolate());
          if (g.CanBeImmediate(ptr, ImmediateMode::kArithmeticImm)) {
            return VisitCompare(this, kArm64Cmp32, g.UseRegister(left),
                                g.TempImmediate(ptr), &cont);
          }
        }
      }
    }

    VisitWord32Compare(this, node, &cont);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Equal(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Operation& equal = Get(node);
  DCHECK(equal.Is<ComparisonOp>());
  OpIndex left = equal.input(0);
  OpIndex right = equal.input(1);
  OpIndex user = node;
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);

  if (MatchZero(right)) {
    OpIndex value = left;
    if (CanCover(user, value)) {
      const Operation& value_op = Get(value);
      if (value_op.Is<Opmask::kWord32Add>() ||
          value_op.Is<Opmask::kWord32BitwiseAnd>()) {
        return VisitWord32Compare(this, node, &cont);
      }
      if (value_op.Is<Opmask::kWord32Sub>()) {
        return VisitWordCompare(this, value, kArm64Cmp32, &cont,
                                kArithmeticImm);
      }
      if (value_op.Is<Opmask::kWord32Equal>()) {
        // Word32Equal(Word32Equal(x, y), 0) => Word32Compare(x, y, ne).
        // A new FlagsContinuation is needed as instead of generating the result
        // for {node}, it is generated for {value}.
        FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, value);
        cont.Negate();
        VisitWord32Compare(this, value, &cont);
        EmitIdentity(node);
        return;
      }
      return VisitWord32Test(this, value, &cont);
    }
  }

  if (isolate() && (V8_STATIC_ROOTS_BOOL ||
                    (COMPRESS_POINTERS_BOOL && !isolate()->bootstrapper()))) {
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    const RootsTable& roots_table = isolate()->roots_table();
    RootIndex root_index;
    Handle<HeapObject> right;
    // HeapConstants and CompressedHeapConstants can be treated the same when
    // using them as an input to a 32-bit comparison. Check whether either is
    // present.
    if (MatchHeapConstant(node, &right) && !right.is_null() &&
        roots_table.IsRootHandle(right, &root_index)) {
      if (RootsTable::IsReadOnly(root_index)) {
        Tagged_t ptr =
            MacroAssemblerBase::ReadOnlyRootPtr(root_index, isolate());
        if (g.CanBeImmediate(ptr, ImmediateMode::kArithmeticImm)) {
          return VisitCompare(this, kArm64Cmp32, g.UseRegister(left),
                              g.TempImmediate(ptr), &cont);
        }
      }
    }
  }
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWord32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& equal = this->Get(node).template Cast<ComparisonOp>();
    DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
    if (this->MatchIntegralZero(equal.right()) &&
        CanCover(node, equal.left())) {
      if (this->Get(equal.left()).template Is<Opmask::kWord64BitwiseAnd>()) {
        return VisitWordCompare(this, equal.left(), kArm64Tst, &cont,
                                kLogical64Imm);
      }
      return VisitWord64Test(this, equal.left(), &cont);
    }
    VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
  } else {
    Node* const user = node;
    Int64BinopMatcher m(user);
    if (m.right().Is(0)) {
      Node* const value = m.left().node();
      if (CanCover(user, value)) {
        switch (value->opcode()) {
          case IrOpcode::kWord64And:
            return VisitWordCompare(this, value, kArm64Tst, &cont,
                                    kLogical64Imm);
          default:
            break;
        }
        return VisitWord64Test(this, value, &cont);
      }
    }
    VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AddWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid() && IsUsed(ovf)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop(this, node, RegisterRepresentation::Word32(),
                        kArm64Add32, kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop(this, node, RegisterRepresentation::Word32(), kArm64Add32,
               kArithmeticImm, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kArm64Add32,
                                                    kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kArm64Add32,
                                           kArithmeticImm, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop(this, node, RegisterRepresentation::Word32(),
                        kArm64Sub32, kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop(this, node, RegisterRepresentation::Word32(), kArm64Sub32,
               kArithmeticImm, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int32BinopMatcher>(this, node, kArm64Sub32,
                                                    kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int32BinopMatcher>(this, node, kArm64Sub32,
                                           kArithmeticImm, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    // ARM64 doesn't set the overflow flag for multiplication, so we need to
    // test on kNotEqual. Here is the code sequence used:
    //   smull result, left, right
    //   cmp result.X(), Operand(result, SXTW)
    FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
    return EmitInt32MulWithOverflow(this, node, &cont);
  }
  FlagsContinuation cont;
  EmitInt32MulWithOverflow(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AddWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop(this, node, RegisterRepresentation::Word64(), kArm64Add,
                        kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop(this, node, RegisterRepresentation::Word64(), kArm64Add,
               kArithmeticImm, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kArm64Add,
                                                    kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kArm64Add,
                                           kArithmeticImm, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64SubWithOverflow(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex ovf = FindProjection(node, 1);
    if (ovf.valid()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop(this, node, RegisterRepresentation::Word64(), kArm64Sub,
                        kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop(this, node, RegisterRepresentation::Word64(), kArm64Sub,
               kArithmeticImm, &cont);
  } else {
    if (Node* ovf = NodeProperties::FindProjection(node, 1)) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
      return VisitBinop<Adapter, Int64BinopMatcher>(this, node, kArm64Sub,
                                                    kArithmeticImm, &cont);
    }
    FlagsContinuation cont;
    VisitBinop<Adapter, Int64BinopMatcher>(this, node, kArm64Sub,
                                           kArithmeticImm, &cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    // ARM64 doesn't set the overflow flag for multiplication, so we need to
    // test on kNotEqual. Here is the code sequence used:
    //   mul result, left, right
    //   smulh high, left, right
    //   cmp high, result, asr 63
    FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, ovf);
    return EmitInt64MulWithOverflow(this, node, &cont);
  }
  FlagsContinuation cont;
  EmitInt64MulWithOverflow(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kArm64Cmp, &cont, kArithmeticImm);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Neg(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex input = this->Get(node).input(0);
    const Operation& input_op = this->Get(input);
    if (input_op.Is<Opmask::kFloat32Mul>() && CanCover(node, input)) {
      const FloatBinopOp& mul = input_op.Cast<FloatBinopOp>();
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRR(this, kArm64Float32Neg, node);

  } else {
    Node* in = node->InputAt(0);
    if (in->opcode() == IrOpcode::kFloat32Mul && CanCover(node, in)) {
      Float32BinopMatcher m(in);
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()), g.UseRegister(m.right().node()));
      return;
    }
    VisitRR(this, kArm64Float32Neg, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Mul(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& mul = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& lhs = this->Get(mul.left());

    if (lhs.Is<Opmask::kFloat32Negate>() && CanCover(node, mul.left())) {
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(lhs.input(0)), g.UseRegister(mul.right()));
      return;
    }

    const Operation& rhs = this->Get(mul.right());
    if (rhs.Is<Opmask::kFloat32Negate>() && CanCover(node, mul.right())) {
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(rhs.input(0)), g.UseRegister(mul.left()));
      return;
    }
    return VisitRRR(this, kArm64Float32Mul, node);

  } else {
    Arm64OperandGeneratorT<Adapter> g(this);
    Float32BinopMatcher m(node);

    if (m.left().IsFloat32Neg() && CanCover(node, m.left().node())) {
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()->InputAt(0)),
           g.UseRegister(m.right().node()));
      return;
    }

    if (m.right().IsFloat32Neg() && CanCover(node, m.right().node())) {
      Emit(kArm64Float32Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.right().node()->InputAt(0)),
           g.UseRegister(m.left().node()));
      return;
    }
    return VisitRRR(this, kArm64Float32Mul, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Abs(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    Arm64OperandGeneratorT<Adapter> g(this);
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex in = this->input_at(node, 0);
    const Operation& input_op = this->Get(in);
    if (input_op.Is<Opmask::kFloat32Sub>() && CanCover(node, in)) {
      const FloatBinopOp& sub = input_op.Cast<FloatBinopOp>();
      Emit(kArm64Float32Abd, g.DefineAsRegister(node),
           g.UseRegister(sub.left()), g.UseRegister(sub.right()));
      return;
    }

    return VisitRR(this, kArm64Float32Abs, node);
  } else {
    Arm64OperandGeneratorT<Adapter> g(this);
    Node* in = node->InputAt(0);
    if (in->opcode() == IrOpcode::kFloat32Sub && CanCover(node, in)) {
      Emit(kArm64Float32Abd, g.DefineAsRegister(node),
           g.UseRegister(in->InputAt(0)), g.UseRegister(in->InputAt(1)));
      return;
    }

    return VisitRR(this, kArm64Float32Abs, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Abs(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex in = this->input_at(node, 0);
    const Operation& input_op = this->Get(in);
    if (input_op.Is<Opmask::kFloat64Sub>() && CanCover(node, in)) {
      const FloatBinopOp& sub = input_op.Cast<FloatBinopOp>();
      Emit(kArm64Float64Abd, g.DefineAsRegister(node),
           g.UseRegister(sub.left()), g.UseRegister(sub.right()));
      return;
    }

    return VisitRR(this, kArm64Float64Abs, node);
  } else {
    Node* in = node->InputAt(0);
    if (in->opcode() == IrOpcode::kFloat64Sub && CanCover(node, in)) {
      Emit(kArm64Float64Abd, g.DefineAsRegister(node),
           g.UseRegister(in->InputAt(0)), g.UseRegister(in->InputAt(1)));
      return;
    }

    return VisitRR(this, kArm64Float64Abs, node);
  }
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

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
  const auto& bitcast = this->Cast<BitcastWord32PairToFloat64Op>(node);
  node_t hi = bitcast.high_word32();
  node_t lo = bitcast.low_word32();

  int vreg = g.AllocateVirtualRegister();
  Emit(kArm64Bfi, g.DefineSameAsFirstForVreg(vreg), g.UseRegister(lo),
       g.UseRegister(hi), g.TempImmediate(32), g.TempImmediate(32));
  Emit(kArm64Float64MoveU64, g.DefineAsRegister(node),
       g.UseRegisterForVreg(vreg));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertLowWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    Arm64OperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    if (left->opcode() == IrOpcode::kFloat64InsertHighWord32 &&
        CanCover(node, left)) {
      Node* right_of_left = left->InputAt(1);
      Emit(kArm64Bfi, g.DefineSameAsFirst(left), g.UseRegister(right),
           g.UseRegister(right_of_left), g.TempImmediate(32),
           g.TempImmediate(32));
      Emit(kArm64Float64MoveU64, g.DefineAsRegister(node), g.UseRegister(left));
      return;
    }
    Emit(kArm64Float64InsertLowWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertHighWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    UNIMPLEMENTED();
  } else {
    Arm64OperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    if (left->opcode() == IrOpcode::kFloat64InsertLowWord32 &&
        CanCover(node, left)) {
      Node* right_of_left = left->InputAt(1);
      Emit(kArm64Bfi, g.DefineSameAsFirst(left), g.UseRegister(right_of_left),
           g.UseRegister(right), g.TempImmediate(32), g.TempImmediate(32));
      Emit(kArm64Float64MoveU64, g.DefineAsRegister(node), g.UseRegister(left));
      return;
    }
    Emit(kArm64Float64InsertHighWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.UseRegister(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Neg(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    OpIndex input = this->Get(node).input(0);
    const Operation& input_op = this->Get(input);
    if (input_op.Is<Opmask::kFloat64Mul>() && CanCover(node, input)) {
      const FloatBinopOp& mul = input_op.Cast<FloatBinopOp>();
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(mul.left()), g.UseRegister(mul.right()));
      return;
    }
    VisitRR(this, kArm64Float64Neg, node);
  } else {
    Node* in = node->InputAt(0);
    if (in->opcode() == IrOpcode::kFloat64Mul && CanCover(node, in)) {
      Float64BinopMatcher m(in);
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()), g.UseRegister(m.right().node()));
      return;
    }
    VisitRR(this, kArm64Float64Neg, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mul(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const FloatBinopOp& mul = this->Get(node).template Cast<FloatBinopOp>();
    const Operation& lhs = this->Get(mul.left());
    if (lhs.Is<Opmask::kFloat64Negate>() && CanCover(node, mul.left())) {
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(lhs.input(0)), g.UseRegister(mul.right()));
      return;
    }

    const Operation& rhs = this->Get(mul.right());
    if (rhs.Is<Opmask::kFloat64Negate>() && CanCover(node, mul.right())) {
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(rhs.input(0)), g.UseRegister(mul.left()));
      return;
    }
    return VisitRRR(this, kArm64Float64Mul, node);

  } else {
    Float64BinopMatcher m(node);

    if (m.left().IsFloat64Neg() && CanCover(node, m.left().node())) {
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.left().node()->InputAt(0)),
           g.UseRegister(m.right().node()));
      return;
    }

    if (m.right().IsFloat64Neg() && CanCover(node, m.right().node())) {
      Emit(kArm64Float64Fnmul, g.DefineAsRegister(node),
           g.UseRegister(m.right().node()->InputAt(0)),
           g.UseRegister(m.left().node()));
      return;
    }
    return VisitRRR(this, kArm64Float64Mul, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  // Use DMB ISH for both acquire-release and sequentially consistent barriers.
  Arm64OperandGeneratorT<Adapter> g(this);
  Emit(kArm64DmbIsh, g.NoOutput());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
  VisitAtomicLoad(this, node, AtomicWidth::kWord32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicLoad(node_t node) {
  VisitAtomicLoad(this, node, AtomicWidth::kWord64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
  VisitAtomicStore(this, node, AtomicWidth::kWord32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicStore(node_t node) {
  VisitAtomicStore(this, node, AtomicWidth::kWord64);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32AtomicExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
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
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord32,
                      atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Int8()) {
    opcode = kAtomicExchangeInt8;
  } else if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (params.type() == MachineType::Int16()) {
    opcode = kAtomicExchangeInt16;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (params.type() == MachineType::Int32()
    || params.type() == MachineType::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord32, params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64AtomicExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
    opcode = kArm64Word64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64,
                      atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kArm64Word64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64, params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32AtomicCompareExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
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
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord32,
                             atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicCompareExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Int8()) {
    opcode = kAtomicCompareExchangeInt8;
  } else if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (params.type() == MachineType::Int16()) {
    opcode = kAtomicCompareExchangeInt16;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (params.type() == MachineType::Int32()
    || params.type() == MachineType::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord32,
                             params.kind());
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64AtomicCompareExchange(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
  ArchOpcode opcode;
  if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
    opcode = kArm64Word64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
                             atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicCompareExchange(
    Node* node) {
  ArchOpcode opcode;
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kArm64Word64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
                             params.kind());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicBinaryOperation(
    node_t node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    ArchOpcode opcode;
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
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord32,
                     atomic_op.memory_access_kind);
  } else {
    ArchOpcode opcode;
    AtomicOpParameters params = AtomicOpParametersOf(node->op());
    if (params.type() == MachineType::Int8()) {
      opcode = int8_op;
    } else if (params.type() == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (params.type() == MachineType::Int16()) {
      opcode = int16_op;
    } else if (params.type() == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (params.type() == MachineType::Int32() ||
               params.type() == MachineType::Uint32()) {
      opcode = word32_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord32, params.kind());
  }
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
void InstructionSelectorT<Adapter>::VisitWord64AtomicBinaryOperation(
    node_t node, ArchOpcode uint8_op, ArchOpcode uint16_op,
    ArchOpcode uint32_op, ArchOpcode uint64_op) {
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const AtomicRMWOp& atomic_op = this->Get(node).template Cast<AtomicRMWOp>();
    ArchOpcode opcode;
    if (atomic_op.memory_rep == MemoryRepresentation::Uint8()) {
      opcode = uint8_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint16()) {
      opcode = uint16_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint32()) {
      opcode = uint32_op;
    } else if (atomic_op.memory_rep == MemoryRepresentation::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64,
                     atomic_op.memory_access_kind);
  } else {
    ArchOpcode opcode;
    AtomicOpParameters params = AtomicOpParametersOf(node->op());
    if (params.type() == MachineType::Uint8()) {
      opcode = uint8_op;
    } else if (params.type() == MachineType::Uint16()) {
      opcode = uint16_op;
    } else if (params.type() == MachineType::Uint32()) {
      opcode = uint32_op;
    } else if (params.type() == MachineType::Uint64()) {
      opcode = uint64_op;
    } else {
      UNREACHABLE();
    }
    VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64, params.kind());
  }
}

#define VISIT_ATOMIC_BINOP(op)                                                 \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::VisitWord64Atomic##op(node_t node) {     \
    VisitWord64AtomicBinaryOperation(node, kAtomic##op##Uint8,                 \
                                     kAtomic##op##Uint16, kAtomic##op##Word32, \
                                     kArm64Word64Atomic##op##Uint64);          \
  }
VISIT_ATOMIC_BINOP(Add)
VISIT_ATOMIC_BINOP(Sub)
VISIT_ATOMIC_BINOP(And)
VISIT_ATOMIC_BINOP(Or)
VISIT_ATOMIC_BINOP(Xor)
#undef VISIT_ATOMIC_BINOP

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
#define SIMD_UNOP_LIST(V)                                       \
  V(F64x2ConvertLowI32x4S, kArm64F64x2ConvertLowI32x4S)         \
  V(F64x2ConvertLowI32x4U, kArm64F64x2ConvertLowI32x4U)         \
  V(F64x2PromoteLowF32x4, kArm64F64x2PromoteLowF32x4)           \
  V(F32x4SConvertI32x4, kArm64F32x4SConvertI32x4)               \
  V(F32x4UConvertI32x4, kArm64F32x4UConvertI32x4)               \
  V(F32x4DemoteF64x2Zero, kArm64F32x4DemoteF64x2Zero)           \
  V(F16x8SConvertI16x8, kArm64F16x8SConvertI16x8)               \
  V(F16x8UConvertI16x8, kArm64F16x8UConvertI16x8)               \
  V(I16x8SConvertF16x8, kArm64I16x8SConvertF16x8)               \
  V(I16x8UConvertF16x8, kArm64I16x8UConvertF16x8)               \
  V(F16x8DemoteF32x4Zero, kArm64F16x8DemoteF32x4Zero)           \
  V(F16x8DemoteF64x2Zero, kArm64F16x8DemoteF64x2Zero)           \
  V(F32x4PromoteLowF16x8, kArm64F32x4PromoteLowF16x8)           \
  V(I64x2BitMask, kArm64I64x2BitMask)                           \
  V(I32x4SConvertF32x4, kArm64I32x4SConvertF32x4)               \
  V(I32x4UConvertF32x4, kArm64I32x4UConvertF32x4)               \
  V(I32x4RelaxedTruncF32x4S, kArm64I32x4SConvertF32x4)          \
  V(I32x4RelaxedTruncF32x4U, kArm64I32x4UConvertF32x4)          \
  V(I32x4BitMask, kArm64I32x4BitMask)                           \
  V(I32x4TruncSatF64x2SZero, kArm64I32x4TruncSatF64x2SZero)     \
  V(I32x4TruncSatF64x2UZero, kArm64I32x4TruncSatF64x2UZero)     \
  V(I32x4RelaxedTruncF64x2SZero, kArm64I32x4TruncSatF64x2SZero) \
  V(I32x4RelaxedTruncF64x2UZero, kArm64I32x4TruncSatF64x2UZero) \
  V(I16x8BitMask, kArm64I16x8BitMask)                           \
  V(S128Not, kArm64S128Not)                                     \
  V(V128AnyTrue, kArm64V128AnyTrue)                             \
  V(I64x2AllTrue, kArm64I64x2AllTrue)                           \
  V(I32x4AllTrue, kArm64I32x4AllTrue)                           \
  V(I16x8AllTrue, kArm64I16x8AllTrue)                           \
  V(I8x16AllTrue, kArm64I8x16AllTrue)

#define SIMD_UNOP_LANE_SIZE_LIST(V) \
  V(F64x2Splat, kArm64FSplat, 64)   \
  V(F64x2Abs, kArm64FAbs, 64)       \
  V(F64x2Sqrt, kArm64FSqrt, 64)     \
  V(F64x2Neg, kArm64FNeg, 64)       \
  V(F32x4Splat, kArm64FSplat, 32)   \
  V(F32x4Abs, kArm64FAbs, 32)       \
  V(F32x4Sqrt, kArm64FSqrt, 32)     \
  V(F32x4Neg, kArm64FNeg, 32)       \
  V(I64x2Splat, kArm64ISplat, 64)   \
  V(I64x2Abs, kArm64IAbs, 64)       \
  V(I64x2Neg, kArm64INeg, 64)       \
  V(I32x4Splat, kArm64ISplat, 32)   \
  V(I32x4Abs, kArm64IAbs, 32)       \
  V(I32x4Neg, kArm64INeg, 32)       \
  V(F16x8Splat, kArm64FSplat, 16)   \
  V(F16x8Abs, kArm64FAbs, 16)       \
  V(F16x8Sqrt, kArm64FSqrt, 16)     \
  V(F16x8Neg, kArm64FNeg, 16)       \
  V(I16x8Splat, kArm64ISplat, 16)   \
  V(I16x8Abs, kArm64IAbs, 16)       \
  V(I16x8Neg, kArm64INeg, 16)       \
  V(I8x16Splat, kArm64ISplat, 8)    \
  V(I8x16Abs, kArm64IAbs, 8)        \
  V(I8x16Neg, kArm64INeg, 8)

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

#define SIMD_BINOP_LIST(V)                        \
  V(I32x4Mul, kArm64I32x4Mul)                     \
  V(I32x4DotI16x8S, kArm64I32x4DotI16x8S)         \
  V(I16x8DotI8x16I7x16S, kArm64I16x8DotI8x16S)    \
  V(I16x8SConvertI32x4, kArm64I16x8SConvertI32x4) \
  V(I16x8Mul, kArm64I16x8Mul)                     \
  V(I16x8UConvertI32x4, kArm64I16x8UConvertI32x4) \
  V(I16x8Q15MulRSatS, kArm64I16x8Q15MulRSatS)     \
  V(I16x8RelaxedQ15MulRS, kArm64I16x8Q15MulRSatS) \
  V(I8x16SConvertI16x8, kArm64I8x16SConvertI16x8) \
  V(I8x16UConvertI16x8, kArm64I8x16UConvertI16x8) \
  V(S128Or, kArm64S128Or)                         \
  V(S128Xor, kArm64S128Xor)

#define SIMD_BINOP_LANE_SIZE_LIST(V)                   \
  V(F64x2Min, kArm64FMin, 64)                          \
  V(F64x2Max, kArm64FMax, 64)                          \
  V(F64x2Add, kArm64FAdd, 64)                          \
  V(F64x2Sub, kArm64FSub, 64)                          \
  V(F64x2Div, kArm64FDiv, 64)                          \
  V(F64x2RelaxedMin, kArm64FMin, 64)                   \
  V(F64x2RelaxedMax, kArm64FMax, 64)                   \
  V(F32x4Min, kArm64FMin, 32)                          \
  V(F32x4Max, kArm64FMax, 32)                          \
  V(F32x4Add, kArm64FAdd, 32)                          \
  V(F32x4Sub, kArm64FSub, 32)                          \
  V(F32x4Div, kArm64FDiv, 32)                          \
  V(F32x4RelaxedMin, kArm64FMin, 32)                   \
  V(F32x4RelaxedMax, kArm64FMax, 32)                   \
  V(F16x8Add, kArm64FAdd, 16)                          \
  V(F16x8Sub, kArm64FSub, 16)                          \
  V(F16x8Div, kArm64FDiv, 16)                          \
  V(F16x8Min, kArm64FMin, 16)                          \
  V(F16x8Max, kArm64FMax, 16)                          \
  V(I64x2Sub, kArm64ISub, 64)                          \
  V(I32x4GtU, kArm64IGtU, 32)                          \
  V(I32x4GeU, kArm64IGeU, 32)                          \
  V(I32x4MinS, kArm64IMinS, 32)                        \
  V(I32x4MaxS, kArm64IMaxS, 32)                        \
  V(I32x4MinU, kArm64IMinU, 32)                        \
  V(I32x4MaxU, kArm64IMaxU, 32)                        \
  V(I16x8AddSatS, kArm64IAddSatS, 16)                  \
  V(I16x8SubSatS, kArm64ISubSatS, 16)                  \
  V(I16x8AddSatU, kArm64IAddSatU, 16)                  \
  V(I16x8SubSatU, kArm64ISubSatU, 16)                  \
  V(I16x8GtU, kArm64IGtU, 16)                          \
  V(I16x8GeU, kArm64IGeU, 16)                          \
  V(I16x8RoundingAverageU, kArm64RoundingAverageU, 16) \
  V(I8x16RoundingAverageU, kArm64RoundingAverageU, 8)  \
  V(I16x8MinS, kArm64IMinS, 16)                        \
  V(I16x8MaxS, kArm64IMaxS, 16)                        \
  V(I16x8MinU, kArm64IMinU, 16)                        \
  V(I16x8MaxU, kArm64IMaxU, 16)                        \
  V(I8x16Sub, kArm64ISub, 8)                           \
  V(I8x16AddSatS, kArm64IAddSatS, 8)                   \
  V(I8x16SubSatS, kArm64ISubSatS, 8)                   \
  V(I8x16AddSatU, kArm64IAddSatU, 8)                   \
  V(I8x16SubSatU, kArm64ISubSatU, 8)                   \
  V(I8x16GtU, kArm64IGtU, 8)                           \
  V(I8x16GeU, kArm64IGeU, 8)                           \
  V(I8x16MinS, kArm64IMinS, 8)                         \
  V(I8x16MaxS, kArm64IMaxS, 8)                         \
  V(I8x16MinU, kArm64IMinU, 8)                         \
  V(I8x16MaxU, kArm64IMaxU, 8)

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  static const int kUint32Immediates = 4;
  uint32_t val[kUint32Immediates];
  static_assert(sizeof(val) == kSimd128Size);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ConstantOp& constant =
        this->Get(node).template Cast<turboshaft::Simd128ConstantOp>();
    memcpy(val, constant.value, kSimd128Size);
  } else {
    memcpy(val, S128ImmediateParameterOf(node->op()).data(), kSimd128Size);
  }
  Emit(kArm64S128Const, g.DefineAsRegister(node), g.UseImmediate(val[0]),
       g.UseImmediate(val[1]), g.UseImmediate(val[2]), g.UseImmediate(val[3]));
}

namespace {

struct BicImmParam {
  BicImmParam(uint32_t imm, uint8_t lane_size, uint8_t shift_amount)
      : imm(imm), lane_size(lane_size), shift_amount(shift_amount) {}
  uint8_t imm;
  uint8_t lane_size;
  uint8_t shift_amount;
};

template <typename node_t>
struct BicImmResult {
  BicImmResult(std::optional<BicImmParam> param, node_t const_node,
               node_t other_node)
      : param(param), const_node(const_node), other_node(other_node) {}
  std::optional<BicImmParam> param;
  node_t const_node;
  node_t other_node;
};

std::optional<BicImmParam> BicImm16bitHelper(uint16_t val) {
  uint8_t byte0 = val & 0xFF;
  uint8_t byte1 = val >> 8;
  // Cannot use Bic if both bytes are not 0x00
  if (byte0 == 0x00) {
    return BicImmParam(byte1, 16, 8);
  }
  if (byte1 == 0x00) {
    return BicImmParam(byte0, 16, 0);
  }
  return std::nullopt;
}

std::optional<BicImmParam> BicImm32bitHelper(uint32_t val) {
  for (int i = 0; i < 4; i++) {
    // All bytes are 0 but one
    if ((val & (0xFF << (8 * i))) == val) {
      return BicImmParam(static_cast<uint8_t>(val >> i * 8), 32, i * 8);
    }
  }
  // Low and high 2 bytes are equal
  if ((val >> 16) == (0xFFFF & val)) {
    return BicImm16bitHelper(0xFFFF & val);
  }
  return std::nullopt;
}

std::optional<BicImmParam> BicImmConstHelper(Node* const_node, bool not_imm) {
  const int kUint32Immediates = 4;
  uint32_t val[kUint32Immediates];
  static_assert(sizeof(val) == kSimd128Size);
  memcpy(val, S128ImmediateParameterOf(const_node->op()).data(), kSimd128Size);
  // If 4 uint32s are not the same, cannot emit Bic
  if (!(val[0] == val[1] && val[1] == val[2] && val[2] == val[3])) {
    return std::nullopt;
  }
  return BicImm32bitHelper(not_imm ? ~val[0] : val[0]);
}

std::optional<BicImmParam> BicImmConstHelper(const turboshaft::Operation& op,
                                             bool not_imm) {
  const int kUint32Immediates = 4;
  uint32_t val[kUint32Immediates];
  static_assert(sizeof(val) == kSimd128Size);
  memcpy(val, op.Cast<turboshaft::Simd128ConstantOp>().value, kSimd128Size);
  // If 4 uint32s are not the same, cannot emit Bic
  if (!(val[0] == val[1] && val[1] == val[2] && val[2] == val[3])) {
    return std::nullopt;
  }
  return BicImm32bitHelper(not_imm ? ~val[0] : val[0]);
}

std::optional<BicImmResult<turboshaft::OpIndex>> BicImmHelper(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    turboshaft::OpIndex and_node, bool not_imm) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128BinopOp& op = selector->Get(and_node).Cast<Simd128BinopOp>();
  // If we are negating the immediate then we are producing And(x, imm), and so
  // can take the immediate from the left or right input. Otherwise we are
  // producing And(x, Not(imm)), which can only be used when the immediate is
  // the right (negated) input.
  if (not_imm && selector->Get(op.left()).Is<Simd128ConstantOp>()) {
    return BicImmResult<OpIndex>(
        BicImmConstHelper(selector->Get(op.left()), not_imm), op.left(),
        op.right());
  }
  if (selector->Get(op.right()).Is<Simd128ConstantOp>()) {
    return BicImmResult<OpIndex>(
        BicImmConstHelper(selector->Get(op.right()), not_imm), op.right(),
        op.left());
  }
  return std::nullopt;
}

std::optional<BicImmResult<Node*>> BicImmHelper(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* and_node,
    bool not_imm) {
  Node* left = and_node->InputAt(0);
  Node* right = and_node->InputAt(1);
  // If we are negating the immediate then we are producing And(x, imm), and so
  // can take the immediate from the left or right input. Otherwise we are
  // producing And(x, Not(imm)), which can only be used when the immediate is
  // the right (negated) input.
  if (not_imm && left->opcode() == IrOpcode::kS128Const) {
    return BicImmResult<Node*>(BicImmConstHelper(left, not_imm), left, right);
  }
  if (right->opcode() == IrOpcode::kS128Const) {
    return BicImmResult<Node*>(BicImmConstHelper(right, not_imm), right, left);
  }
  return std::nullopt;
}

template <typename Adapter>
bool TryEmitS128AndNotImm(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node, bool not_imm) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  std::optional<BicImmResult<typename Adapter::node_t>> result =
      BicImmHelper(selector, node, not_imm);
  if (!result.has_value()) return false;
  std::optional<BicImmParam> param = result->param;
  if (param.has_value()) {
    if (selector->CanCover(node, result->other_node)) {
      selector->Emit(
          kArm64S128AndNot | LaneSizeField::encode(param->lane_size),
          g.DefineSameAsFirst(node), g.UseRegister(result->other_node),
          g.UseImmediate(param->imm), g.UseImmediate(param->shift_amount));
      return true;
    }
  }
  return false;
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128AndNot(node_t node) {
  if (!TryEmitS128AndNotImm(this, node, false)) {
    VisitRRR(this, kArm64S128AndNot, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128And(node_t node) {
  // AndNot can be used if we negate the immediate input of And.
  if (!TryEmitS128AndNotImm(this, node, true)) {
    VisitRRR(this, kArm64S128And, node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Emit(kArm64S128Const, g.DefineAsRegister(node), g.UseImmediate(0),
       g.UseImmediate(0), g.UseImmediate(0), g.UseImmediate(0));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand output = CpuFeatures::IsSupported(DOTPROD)
                                  ? g.DefineSameAsInput(node, 2)
                                  : g.DefineAsRegister(node);
  Emit(kArm64I32x4DotI8x16AddS, output, g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 2)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16BitMask(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[1];
  size_t temp_count = 0;

  if (CpuFeatures::IsSupported(PMULL1Q)) {
    temps[0] = g.TempSimd128Register();
    temp_count = 1;
  }

  Emit(kArm64I8x16BitMask, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)), temp_count, temps);
}

#define SIMD_VISIT_EXTRACT_LANE(Type, T, Sign, LaneSize)                     \
  template <typename Adapter>                                                \
  void InstructionSelectorT<Adapter>::Visit##Type##ExtractLane##Sign(        \
      node_t node) {                                                         \
    VisitRRI(this,                                                           \
             kArm64##T##ExtractLane##Sign | LaneSizeField::encode(LaneSize), \
             node);                                                          \
  }
SIMD_VISIT_EXTRACT_LANE(F64x2, F, , 64)
SIMD_VISIT_EXTRACT_LANE(F32x4, F, , 32)
SIMD_VISIT_EXTRACT_LANE(F16x8, F, , 16)
SIMD_VISIT_EXTRACT_LANE(I64x2, I, , 64)
SIMD_VISIT_EXTRACT_LANE(I32x4, I, , 32)
SIMD_VISIT_EXTRACT_LANE(I16x8, I, U, 16)
SIMD_VISIT_EXTRACT_LANE(I16x8, I, S, 16)
SIMD_VISIT_EXTRACT_LANE(I8x16, I, U, 8)
SIMD_VISIT_EXTRACT_LANE(I8x16, I, S, 8)
#undef SIMD_VISIT_EXTRACT_LANE

#define SIMD_VISIT_REPLACE_LANE(Type, T, LaneSize)                            \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##Type##ReplaceLane(node_t node) { \
    VisitRRIR(this, kArm64##T##ReplaceLane | LaneSizeField::encode(LaneSize), \
              node);                                                          \
  }
SIMD_VISIT_REPLACE_LANE(F64x2, F, 64)
SIMD_VISIT_REPLACE_LANE(F32x4, F, 32)
SIMD_VISIT_REPLACE_LANE(F16x8, F, 16)
SIMD_VISIT_REPLACE_LANE(I64x2, I, 64)
SIMD_VISIT_REPLACE_LANE(I32x4, I, 32)
SIMD_VISIT_REPLACE_LANE(I16x8, I, 16)
SIMD_VISIT_REPLACE_LANE(I8x16, I, 8)
#undef SIMD_VISIT_REPLACE_LANE

#define SIMD_VISIT_UNOP(Name, instruction)                       \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, instruction, node);                            \
  }
SIMD_UNOP_LIST(SIMD_VISIT_UNOP)
#undef SIMD_VISIT_UNOP
#undef SIMD_UNOP_LIST

#define SIMD_VISIT_SHIFT_OP(Name, width)                         \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitSimdShiftRRR(this, kArm64##Name, node, width);          \
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

#define SIMD_VISIT_BINOP_LANE_SIZE(Name, instruction, LaneSize)          \
  template <typename Adapter>                                            \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {         \
    VisitRRR(this, instruction | LaneSizeField::encode(LaneSize), node); \
  }
SIMD_BINOP_LANE_SIZE_LIST(SIMD_VISIT_BINOP_LANE_SIZE)
#undef SIMD_VISIT_BINOP_LANE_SIZE
#undef SIMD_BINOP_LANE_SIZE_LIST

#define SIMD_VISIT_UNOP_LANE_SIZE(Name, instruction, LaneSize)          \
  template <typename Adapter>                                           \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {        \
    VisitRR(this, instruction | LaneSizeField::encode(LaneSize), node); \
  }
SIMD_UNOP_LANE_SIZE_LIST(SIMD_VISIT_UNOP_LANE_SIZE)
#undef SIMD_VISIT_UNOP_LANE_SIZE
#undef SIMD_UNOP_LANE_SIZE_LIST

using ShuffleMatcher =
    ValueMatcher<S128ImmediateParameter, IrOpcode::kI8x16Shuffle>;
using BinopWithShuffleMatcher = BinopMatcher<ShuffleMatcher, ShuffleMatcher,
                                             MachineRepresentation::kSimd128>;

namespace {
// Struct holding the result of pattern-matching a mul+dup.
struct MulWithDupResult {
  Node* input;     // Node holding the vector elements.
  Node* dup_node;  // Node holding the lane to multiply.
  int index;
  // Pattern-match is successful if dup_node is set.
  explicit operator bool() const { return dup_node != nullptr; }
};

// Struct holding the result of pattern-matching a mul+dup.
struct MulWithDup {
  turboshaft::OpIndex input;     // Node holding the vector elements.
  turboshaft::OpIndex dup_node;  // Node holding the lane to multiply.
  int index;
  // Pattern-match is successful if dup_node is set.
  explicit operator bool() const { return dup_node.valid(); }
};

template <int LANES>
MulWithDupResult TryMatchMulWithDup(Node* node) {
  // Pattern match:
  //   f32x4.mul(x, shuffle(x, y, indices)) => f32x4.mul(x, y, laneidx)
  //   f64x2.mul(x, shuffle(x, y, indices)) => f64x2.mul(x, y, laneidx)
  //   where shuffle(x, y, indices) = dup(x[laneidx]) or dup(y[laneidx])
  // f32x4.mul and f64x2.mul are commutative, so use BinopMatcher.
  Node* input = nullptr;
  Node* dup_node = nullptr;

  int index = 0;
  BinopWithShuffleMatcher m = BinopWithShuffleMatcher(node);
  ShuffleMatcher left = m.left();
  ShuffleMatcher right = m.right();

  // TODO(zhin): We can canonicalize first to avoid checking index < LANES.
  // e.g. shuffle(x, y, [16, 17, 18, 19...]) => shuffle(y, y, [0, 1, 2,
  // 3]...). But doing so can mutate the inputs of the shuffle node without
  // updating the shuffle immediates themselves. Fix that before we
  // canonicalize here. We don't want CanCover here because in many use cases,
  // the shuffle is generated early in the function, but the f32x4.mul happens
  // in a loop, which won't cover the shuffle since they are different basic
  // blocks.
  if (left.HasResolvedValue() && wasm::SimdShuffle::TryMatchSplat<LANES>(
                                     left.ResolvedValue().data(), &index)) {
    dup_node = left.node()->InputAt(index < LANES ? 0 : 1);
    input = right.node();
  } else if (right.HasResolvedValue() &&
             wasm::SimdShuffle::TryMatchSplat<LANES>(
                 right.ResolvedValue().data(), &index)) {
    dup_node = right.node()->InputAt(index < LANES ? 0 : 1);
    input = left.node();
  }

  // Canonicalization would get rid of this too.
  index %= LANES;

  return {input, dup_node, index};
}

template <int LANES>
MulWithDup TryMatchMulWithDup(InstructionSelectorT<TurboshaftAdapter>* selector,
                              turboshaft::OpIndex node) {
  // Pattern match:
  //   f32x4.mul(x, shuffle(x, y, indices)) => f32x4.mul(x, y, laneidx)
  //   f64x2.mul(x, shuffle(x, y, indices)) => f64x2.mul(x, y, laneidx)
  //   where shuffle(x, y, indices) = dup(x[laneidx]) or dup(y[laneidx])
  // f32x4.mul and f64x2.mul are commutative, so use BinopMatcher.
  using namespace turboshaft;  // NOLINT(build/namespaces)
  OpIndex input;
  OpIndex dup_node;

  int index = 0;
#if V8_ENABLE_WEBASSEMBLY
  const Simd128BinopOp& mul = selector->Get(node).Cast<Simd128BinopOp>();
  const Operation& left = selector->Get(mul.left());
  const Operation& right = selector->Get(mul.right());

  // TODO(zhin): We can canonicalize first to avoid checking index < LANES.
  // e.g. shuffle(x, y, [16, 17, 18, 19...]) => shuffle(y, y, [0, 1, 2,
  // 3]...). But doing so can mutate the inputs of the shuffle node without
  // updating the shuffle immediates themselves. Fix that before we
  // canonicalize here. We don't want CanCover here because in many use cases,
  // the shuffle is generated early in the function, but the f32x4.mul happens
  // in a loop, which won't cover the shuffle since they are different basic
  // blocks.
  if (left.Is<Simd128ShuffleOp>() &&
      wasm::SimdShuffle::TryMatchSplat<LANES>(
          left.Cast<Simd128ShuffleOp>().shuffle, &index)) {
    dup_node = left.input(index < LANES ? 0 : 1);
    input = mul.right();
  } else if (right.Is<Simd128ShuffleOp>() &&
             wasm::SimdShuffle::TryMatchSplat<LANES>(
                 right.Cast<Simd128ShuffleOp>().shuffle, &index)) {
    dup_node = right.input(index < LANES ? 0 : 1);
    input = mul.left();
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Canonicalization would get rid of this too.
  index %= LANES;

  return {input, dup_node, index};
}
}  // namespace

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF16x8Mul(node_t node) {
  if (MulWithDup result = TryMatchMulWithDup<8>(this, node)) {
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(16),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(16), node);
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF16x8Mul(Node* node) {
  if (MulWithDupResult result = TryMatchMulWithDup<8>(node)) {
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(16),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(16), node);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF32x4Mul(node_t node) {
  if (MulWithDup result = TryMatchMulWithDup<4>(this, node)) {
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(32),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(32), node);
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF32x4Mul(Node* node) {
  if (MulWithDupResult result = TryMatchMulWithDup<4>(node)) {
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(32),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(32), node);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF64x2Mul(node_t node) {
  if (MulWithDup result = TryMatchMulWithDup<2>(this, node)) {
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(64),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(64), node);
  }
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF64x2Mul(Node* node) {
  if (MulWithDupResult result = TryMatchMulWithDup<2>(node)) {
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);
    Emit(kArm64FMulElement | LaneSizeField::encode(64),
         g.DefineAsRegister(node), g.UseRegister(result.input),
         g.UseRegister(result.dup_node), g.UseImmediate(result.index));
  } else {
    return VisitRRR(this, kArm64FMul | LaneSizeField::encode(64), node);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Mul(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(kArm64I64x2Mul, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

namespace {

// Used for pattern matching SIMD Add operations where one of the inputs matches
// |opcode| and ensure that the matched input is on the LHS (input 0).
struct SimdAddOpMatcher : public NodeMatcher {
  explicit SimdAddOpMatcher(Node* node, IrOpcode::Value opcode)
      : NodeMatcher(node),
        opcode_(opcode),
        left_(InputAt(0)),
        right_(InputAt(1)) {
    DCHECK(HasProperty(Operator::kCommutative));
    PutOpOnLeft();
  }

  bool Matches() { return left_->opcode() == opcode_; }
  Node* left() const { return left_; }
  Node* right() const { return right_; }

 private:
  void PutOpOnLeft() {
    if (right_->opcode() == opcode_) {
      std::swap(left_, right_);
      node()->ReplaceInput(0, left_);
      node()->ReplaceInput(1, right_);
    }
  }
  IrOpcode::Value opcode_;
  Node* left_;
  Node* right_;
};

// Tries to match either input of a commutative binop to a given Opmask.
class SimdBinopMatcherTurboshaft {
 public:
  SimdBinopMatcherTurboshaft(InstructionSelectorT<TurboshaftAdapter>* selector,
                             turboshaft::OpIndex node)
      : selector_(selector), node_(node) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Simd128BinopOp& add_op = selector->Get(node).Cast<Simd128BinopOp>();
    DCHECK(Simd128BinopOp::IsCommutative(add_op.kind));
    input0_ = add_op.left();
    input1_ = add_op.right();
  }
  template <typename OpmaskT>
  bool InputMatches() {
    if (selector_->Get(input1_).Is<OpmaskT>()) {
      std::swap(input0_, input1_);
      return true;
    }
    return selector_->Get(input0_).Is<OpmaskT>();
  }
  turboshaft::OpIndex matched_input() const { return input0_; }
  turboshaft::OpIndex other_input() const { return input1_; }

 private:
  InstructionSelectorT<TurboshaftAdapter>* selector_;
  turboshaft::OpIndex node_;
  turboshaft::OpIndex input0_;
  turboshaft::OpIndex input1_;
};

bool ShraHelper(InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
                int lane_size, InstructionCode shra_code,
                InstructionCode add_code, IrOpcode::Value shift_op) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(selector);
  SimdAddOpMatcher m(node, shift_op);
  if (!m.Matches() || !selector->CanCover(node, m.left())) return false;
  if (!g.IsIntegerConstant(m.left()->InputAt(1))) return false;

  // If shifting by zero, just do the addition
  if (g.GetIntegerConstantValue(m.left()->InputAt(1)) % lane_size == 0) {
    selector->Emit(add_code, g.DefineAsRegister(node),
                   g.UseRegister(m.left()->InputAt(0)),
                   g.UseRegister(m.right()));
  } else {
    selector->Emit(shra_code | LaneSizeField::encode(lane_size),
                   g.DefineSameAsFirst(node), g.UseRegister(m.right()),
                   g.UseRegister(m.left()->InputAt(0)),
                   g.UseImmediate(m.left()->InputAt(1)));
  }
  return true;
}

template <typename OpmaskT>
bool ShraHelper(InstructionSelectorT<TurboshaftAdapter>* selector,
                turboshaft::OpIndex node, int lane_size,
                InstructionCode shra_code, InstructionCode add_code) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  SimdBinopMatcherTurboshaft m(selector, node);
  if (!m.InputMatches<OpmaskT>() ||
      !selector->CanCover(node, m.matched_input())) {
    return false;
  }
  const Simd128ShiftOp& shiftop =
      selector->Get(m.matched_input()).Cast<Simd128ShiftOp>();
  if (!selector->is_integer_constant(shiftop.shift())) return false;

  // If shifting by zero, just do the addition
  if (selector->integer_constant(shiftop.shift()) % lane_size == 0) {
    selector->Emit(add_code, g.DefineAsRegister(node),
                   g.UseRegister(shiftop.input()),
                   g.UseRegister(m.other_input()));
  } else {
    selector->Emit(shra_code | LaneSizeField::encode(lane_size),
                   g.DefineSameAsFirst(node), g.UseRegister(m.other_input()),
                   g.UseRegister(shiftop.input()),
                   g.UseImmediate(shiftop.shift()));
  }
  return true;
}

bool AdalpHelper(InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
                 int lane_size, InstructionCode adalp_code,
                 IrOpcode::Value ext_op) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(selector);
  SimdAddOpMatcher m(node, ext_op);
  if (!m.Matches() || !selector->CanCover(node, m.left())) return false;
  selector->Emit(adalp_code | LaneSizeField::encode(lane_size),
                 g.DefineSameAsFirst(node), g.UseRegister(m.right()),
                 g.UseRegister(m.left()->InputAt(0)));
  return true;
}

template <typename OpmaskT>
bool AdalpHelper(InstructionSelectorT<TurboshaftAdapter>* selector,
                 turboshaft::OpIndex node, int lane_size,
                 InstructionCode adalp_code) {
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  SimdBinopMatcherTurboshaft m(selector, node);
  if (!m.InputMatches<OpmaskT>() ||
      !selector->CanCover(node, m.matched_input())) {
    return false;
  }
  selector->Emit(adalp_code | LaneSizeField::encode(lane_size),
                 g.DefineSameAsFirst(node), g.UseRegister(m.other_input()),
                 g.UseRegister(selector->Get(m.matched_input()).input(0)));
  return true;
}

bool MlaHelper(InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
               InstructionCode mla_code, IrOpcode::Value mul_op) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(selector);
  SimdAddOpMatcher m(node, mul_op);
  if (!m.Matches() || !selector->CanCover(node, m.left())) return false;
  selector->Emit(mla_code, g.DefineSameAsFirst(node), g.UseRegister(m.right()),
                 g.UseRegister(m.left()->InputAt(0)),
                 g.UseRegister(m.left()->InputAt(1)));
  return true;
}

template <typename OpmaskT>
bool MlaHelper(InstructionSelectorT<TurboshaftAdapter>* selector,
               turboshaft::OpIndex node, InstructionCode mla_code) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  SimdBinopMatcherTurboshaft m(selector, node);
  if (!m.InputMatches<OpmaskT>() ||
      !selector->CanCover(node, m.matched_input())) {
    return false;
  }
  const Operation& mul = selector->Get(m.matched_input());
  selector->Emit(mla_code, g.DefineSameAsFirst(node),
                 g.UseRegister(m.other_input()), g.UseRegister(mul.input(0)),
                 g.UseRegister(mul.input(1)));
  return true;
}

bool SmlalHelper(InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
                 int lane_size, InstructionCode smlal_code,
                 IrOpcode::Value ext_mul_op) {
  Arm64OperandGeneratorT<TurbofanAdapter> g(selector);
  SimdAddOpMatcher m(node, ext_mul_op);
  if (!m.Matches() || !selector->CanCover(node, m.left())) return false;

  selector->Emit(smlal_code | LaneSizeField::encode(lane_size),
                 g.DefineSameAsFirst(node), g.UseRegister(m.right()),
                 g.UseRegister(m.left()->InputAt(0)),
                 g.UseRegister(m.left()->InputAt(1)));
  return true;
}

template <turboshaft::Simd128BinopOp::Kind kind>
bool SmlalHelper(InstructionSelectorT<TurboshaftAdapter>* selector,
                 turboshaft::OpIndex node, int lane_size,
                 InstructionCode smlal_code) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  SimdBinopMatcherTurboshaft m(selector, node);
  using OpmaskT = Opmask::Simd128BinopMask::For<kind>;
  if (!m.InputMatches<OpmaskT>() ||
      !selector->CanCover(node, m.matched_input()))
    return false;

  const Operation& matched = selector->Get(m.matched_input());
  selector->Emit(smlal_code | LaneSizeField::encode(lane_size),
                 g.DefineSameAsFirst(node), g.UseRegister(m.other_input()),
                 g.UseRegister(matched.input(0)),
                 g.UseRegister(matched.input(1)));
  return true;
}

}  // namespace

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI64x2Add(node_t node) {
  if (ShraHelper(this, node, 64, kArm64Ssra,
                 kArm64IAdd | LaneSizeField::encode(64),
                 IrOpcode::kI64x2ShrS) ||
      ShraHelper(this, node, 64, kArm64Usra,
                 kArm64IAdd | LaneSizeField::encode(64),
                 IrOpcode::kI64x2ShrU)) {
    return;
  }
  VisitRRR(this, kArm64IAdd | LaneSizeField::encode(64), node);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI64x2Add(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (ShraHelper<Opmask::kSimd128I64x2ShrS>(
          this, node, 64, kArm64Ssra, kArm64IAdd | LaneSizeField::encode(64)) ||
      ShraHelper<Opmask::kSimd128I64x2ShrU>(
          this, node, 64, kArm64Usra, kArm64IAdd | LaneSizeField::encode(64))) {
    return;
  }
  VisitRRR(this, kArm64IAdd | LaneSizeField::encode(64), node);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI8x16Add(node_t node) {
  if (!ShraHelper(this, node, 8, kArm64Ssra,
                  kArm64IAdd | LaneSizeField::encode(8),
                  IrOpcode::kI8x16ShrS) &&
      !ShraHelper(this, node, 8, kArm64Usra,
                  kArm64IAdd | LaneSizeField::encode(8),
                  IrOpcode::kI8x16ShrU)) {
    VisitRRR(this, kArm64IAdd | LaneSizeField::encode(8), node);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI8x16Add(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  if (!ShraHelper<Opmask::kSimd128I8x16ShrS>(
          this, node, 8, kArm64Ssra, kArm64IAdd | LaneSizeField::encode(8)) &&
      !ShraHelper<Opmask::kSimd128I8x16ShrU>(
          this, node, 8, kArm64Usra, kArm64IAdd | LaneSizeField::encode(8))) {
    VisitRRR(this, kArm64IAdd | LaneSizeField::encode(8), node);
  }
}

#define VISIT_SIMD_ADD(Type, PairwiseType, LaneSize)                          \
  template <>                                                                 \
  void InstructionSelectorT<TurbofanAdapter>::Visit##Type##Add(node_t node) { \
    /* Select Mla(z, x, y) for Add(x, Mul(y, z)). */                          \
    if (MlaHelper(this, node, kArm64Mla | LaneSizeField::encode(LaneSize),    \
                  IrOpcode::k##Type##Mul)) {                                  \
      return;                                                                 \
    }                                                                         \
    /* Select S/Uadalp(x, y) for Add(x, ExtAddPairwise(y)). */                \
    if (AdalpHelper(this, node, LaneSize, kArm64Sadalp,                       \
                    IrOpcode::k##Type##ExtAddPairwise##PairwiseType##S) ||    \
        AdalpHelper(this, node, LaneSize, kArm64Uadalp,                       \
                    IrOpcode::k##Type##ExtAddPairwise##PairwiseType##U)) {    \
      return;                                                                 \
    }                                                                         \
    /* Select S/Usra(x, y) for Add(x, ShiftRight(y, imm)). */                 \
    if (ShraHelper(this, node, LaneSize, kArm64Ssra,                          \
                   kArm64IAdd | LaneSizeField::encode(LaneSize),              \
                   IrOpcode::k##Type##ShrS) ||                                \
        ShraHelper(this, node, LaneSize, kArm64Usra,                          \
                   kArm64IAdd | LaneSizeField::encode(LaneSize),              \
                   IrOpcode::k##Type##ShrU)) {                                \
      return;                                                                 \
    }                                                                         \
    /* Select Smlal/Umlal(x, y, z) for Add(x, ExtMulLow(y, z)) and            \
     * Smlal2/Umlal2(x, y, z) for Add(x, ExtMulHigh(y, z)). */                \
    if (SmlalHelper(this, node, LaneSize, kArm64Smlal,                        \
                    IrOpcode::k##Type##ExtMulLow##PairwiseType##S) ||         \
        SmlalHelper(this, node, LaneSize, kArm64Smlal2,                       \
                    IrOpcode::k##Type##ExtMulHigh##PairwiseType##S) ||        \
        SmlalHelper(this, node, LaneSize, kArm64Umlal,                        \
                    IrOpcode::k##Type##ExtMulLow##PairwiseType##U) ||         \
        SmlalHelper(this, node, LaneSize, kArm64Umlal2,                       \
                    IrOpcode::k##Type##ExtMulHigh##PairwiseType##U)) {        \
      return;                                                                 \
    }                                                                         \
    VisitRRR(this, kArm64IAdd | LaneSizeField::encode(LaneSize), node);       \
  }                                                                           \
                                                                              \
  template <>                                                                 \
  void InstructionSelectorT<TurboshaftAdapter>::Visit##Type##Add(             \
      node_t node) {                                                          \
    using namespace turboshaft; /*NOLINT(build/namespaces)*/                  \
    /* Select Mla(z, x, y) for Add(x, Mul(y, z)). */                          \
    if (MlaHelper<Opmask::kSimd128##Type##Mul>(                               \
            this, node, kArm64Mla | LaneSizeField::encode(LaneSize))) {       \
      return;                                                                 \
    }                                                                         \
    /* Select S/Uadalp(x, y) for Add(x, ExtAddPairwise(y)). */                \
    if (AdalpHelper<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##S>( \
            this, node, LaneSize, kArm64Sadalp) ||                            \
        AdalpHelper<Opmask::kSimd128##Type##ExtAddPairwise##PairwiseType##U>( \
            this, node, LaneSize, kArm64Uadalp)) {                            \
      return;                                                                 \
    }                                                                         \
    /* Select S/Usra(x, y) for Add(x, ShiftRight(y, imm)). */                 \
    if (ShraHelper<Opmask::kSimd128##Type##ShrS>(                             \
            this, node, LaneSize, kArm64Ssra,                                 \
            kArm64IAdd | LaneSizeField::encode(LaneSize)) ||                  \
        ShraHelper<Opmask::kSimd128##Type##ShrU>(                             \
            this, node, LaneSize, kArm64Usra,                                 \
            kArm64IAdd | LaneSizeField::encode(LaneSize))) {                  \
      return;                                                                 \
    }                                                                         \
    /* Select Smlal/Umlal(x, y, z) for Add(x, ExtMulLow(y, z)) and            \
     * Smlal2/Umlal2(x, y, z) for Add(x, ExtMulHigh(y, z)). */                \
    if (SmlalHelper<                                                          \
            Simd128BinopOp::Kind::k##Type##ExtMulLow##PairwiseType##S>(       \
            this, node, LaneSize, kArm64Smlal) ||                             \
        SmlalHelper<                                                          \
            Simd128BinopOp::Kind::k##Type##ExtMulHigh##PairwiseType##S>(      \
            this, node, LaneSize, kArm64Smlal2) ||                            \
        SmlalHelper<                                                          \
            Simd128BinopOp::Kind::k##Type##ExtMulLow##PairwiseType##U>(       \
            this, node, LaneSize, kArm64Umlal) ||                             \
        SmlalHelper<                                                          \
            Simd128BinopOp::Kind::k##Type##ExtMulHigh##PairwiseType##U>(      \
            this, node, LaneSize, kArm64Umlal2)) {                            \
      return;                                                                 \
    }                                                                         \
    VisitRRR(this, kArm64IAdd | LaneSizeField::encode(LaneSize), node);       \
  }

VISIT_SIMD_ADD(I32x4, I16x8, 32)
VISIT_SIMD_ADD(I16x8, I8x16, 16)
#undef VISIT_SIMD_ADD

#define VISIT_SIMD_SUB(Type, LaneSize)                                        \
  template <>                                                                 \
  void InstructionSelectorT<TurboshaftAdapter>::Visit##Type##Sub(             \
      node_t node) {                                                          \
    using namespace turboshaft; /* NOLINT(build/namespaces) */                \
    Arm64OperandGeneratorT<TurboshaftAdapter> g(this);                        \
    const Simd128BinopOp& sub = Get(node).Cast<Simd128BinopOp>();             \
    const Operation& right = Get(sub.right());                                \
    /* Select Mls(z, x, y) for Sub(z, Mul(x, y)). */                          \
    if (right.Is<Opmask::kSimd128##Type##Mul>() &&                            \
        CanCover(node, sub.right())) {                                        \
      Emit(kArm64Mls | LaneSizeField::encode(LaneSize),                       \
           g.DefineSameAsFirst(node), g.UseRegister(sub.left()),              \
           g.UseRegister(right.input(0)), g.UseRegister(right.input(1)));     \
      return;                                                                 \
    }                                                                         \
    VisitRRR(this, kArm64ISub | LaneSizeField::encode(LaneSize), node);       \
  }                                                                           \
  template <>                                                                 \
  void InstructionSelectorT<TurbofanAdapter>::Visit##Type##Sub(Node* node) {  \
    Arm64OperandGeneratorT<TurbofanAdapter> g(this);                          \
    Node* left = node->InputAt(0);                                            \
    Node* right = node->InputAt(1);                                           \
    /* Select Mls(z, x, y) for Sub(z, Mul(x, y)). */                          \
    if (right->opcode() == IrOpcode::k##Type##Mul && CanCover(node, right)) { \
      Emit(kArm64Mls | LaneSizeField::encode(LaneSize),                       \
           g.DefineSameAsFirst(node), g.UseRegister(left),                    \
           g.UseRegister(right->InputAt(0)),                                  \
           g.UseRegister(right->InputAt(1)));                                 \
      return;                                                                 \
    }                                                                         \
    VisitRRR(this, kArm64ISub | LaneSizeField::encode(LaneSize), node);       \
  }

VISIT_SIMD_SUB(I32x4, 32)
VISIT_SIMD_SUB(I16x8, 16)
#undef VISIT_SIMD_SUB

namespace {
void VisitSimdReduce(InstructionSelectorT<TurboshaftAdapter>* selector,
                     turboshaft::OpIndex node, InstructionCode opcode) {
  Arm64OperandGeneratorT<TurboshaftAdapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->Get(node).input(0)));
}

}  // namespace

#define VISIT_SIMD_REDUCE(Type, Opcode)                                 \
  template <>                                                           \
  void InstructionSelectorT<TurboshaftAdapter>::Visit##Type##AddReduce( \
      turboshaft::OpIndex node) {                                       \
    VisitSimdReduce(this, node, Opcode);                                \
  }

VISIT_SIMD_REDUCE(I8x16, kArm64I8x16Addv)
VISIT_SIMD_REDUCE(I16x8, kArm64I16x8Addv)
VISIT_SIMD_REDUCE(I32x4, kArm64I32x4Addv)
VISIT_SIMD_REDUCE(I64x2, kArm64I64x2AddPair)
VISIT_SIMD_REDUCE(F32x4, kArm64F32x4AddReducePairwise)
VISIT_SIMD_REDUCE(F64x2, kArm64F64x2AddPair)
#undef VISIT_SIMD_REDUCE

namespace {
bool isSimdZero(InstructionSelectorT<TurbofanAdapter>* selector, Node* node) {
  auto m = V128ConstMatcher(node);
  if (m.HasResolvedValue()) {
    auto imms = m.ResolvedValue().immediate();
    return (std::all_of(imms.begin(), imms.end(), std::logical_not<uint8_t>()));
  }
  return node->opcode() == IrOpcode::kS128Zero;
}

bool isSimdZero(InstructionSelectorT<TurboshaftAdapter>* selector,
                turboshaft::OpIndex node) {
  const turboshaft::Operation& op = selector->Get(node);
  if (auto constant = op.TryCast<turboshaft::Simd128ConstantOp>()) {
    return constant->IsZero();
  }
  return false;
}

}  // namespace

#define VISIT_SIMD_CM(Type, T, CmOp, CmOpposite, LaneSize)                   \
  template <typename Adapter>                                                \
  void InstructionSelectorT<Adapter>::Visit##Type##CmOp(node_t node) {       \
    Arm64OperandGeneratorT<Adapter> g(this);                                 \
    node_t left = this->input_at(node, 0);                                   \
    node_t right = this->input_at(node, 1);                                  \
    if (isSimdZero(this, left)) {                                            \
      Emit(kArm64##T##CmOpposite | LaneSizeField::encode(LaneSize),          \
           g.DefineAsRegister(node), g.UseRegister(right));                  \
      return;                                                                \
    } else if (isSimdZero(this, right)) {                                    \
      Emit(kArm64##T##CmOp | LaneSizeField::encode(LaneSize),                \
           g.DefineAsRegister(node), g.UseRegister(left));                   \
      return;                                                                \
    }                                                                        \
    VisitRRR(this, kArm64##T##CmOp | LaneSizeField::encode(LaneSize), node); \
  }

VISIT_SIMD_CM(F64x2, F, Eq, Eq, 64)
VISIT_SIMD_CM(F64x2, F, Ne, Ne, 64)
VISIT_SIMD_CM(F64x2, F, Lt, Gt, 64)
VISIT_SIMD_CM(F64x2, F, Le, Ge, 64)
VISIT_SIMD_CM(F32x4, F, Eq, Eq, 32)
VISIT_SIMD_CM(F32x4, F, Ne, Ne, 32)
VISIT_SIMD_CM(F32x4, F, Lt, Gt, 32)
VISIT_SIMD_CM(F32x4, F, Le, Ge, 32)
VISIT_SIMD_CM(F16x8, F, Eq, Eq, 16)
VISIT_SIMD_CM(F16x8, F, Ne, Ne, 16)
VISIT_SIMD_CM(F16x8, F, Lt, Gt, 16)
VISIT_SIMD_CM(F16x8, F, Le, Ge, 16)

VISIT_SIMD_CM(I64x2, I, Eq, Eq, 64)
VISIT_SIMD_CM(I64x2, I, Ne, Ne, 64)
VISIT_SIMD_CM(I64x2, I, GtS, LtS, 64)
VISIT_SIMD_CM(I64x2, I, GeS, LeS, 64)
VISIT_SIMD_CM(I32x4, I, Eq, Eq, 32)
VISIT_SIMD_CM(I32x4, I, Ne, Ne, 32)
VISIT_SIMD_CM(I32x4, I, GtS, LtS, 32)
VISIT_SIMD_CM(I32x4, I, GeS, LeS, 32)
VISIT_SIMD_CM(I16x8, I, Eq, Eq, 16)
VISIT_SIMD_CM(I16x8, I, Ne, Ne, 16)
VISIT_SIMD_CM(I16x8, I, GtS, LtS, 16)
VISIT_SIMD_CM(I16x8, I, GeS, LeS, 16)
VISIT_SIMD_CM(I8x16, I, Eq, Eq, 8)
VISIT_SIMD_CM(I8x16, I, Ne, Ne, 8)
VISIT_SIMD_CM(I8x16, I, GtS, LtS, 8)
VISIT_SIMD_CM(I8x16, I, GeS, LeS, 8)
#undef VISIT_SIMD_CM

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
  Arm64OperandGeneratorT<Adapter> g(this);
  Emit(kArm64S128Select, g.DefineSameAsFirst(node),
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
    Arm64OperandGeneratorT<Adapter> g(this);                   \
    Emit(kArm64##op, g.DefineSameAsInput(node, 2),             \
         g.UseRegister(this->input_at(node, 0)),               \
         g.UseRegister(this->input_at(node, 1)),               \
         g.UseRegister(this->input_at(node, 2)));              \
  }
VISIT_SIMD_QFMOP(F64x2Qfma)
VISIT_SIMD_QFMOP(F64x2Qfms)
VISIT_SIMD_QFMOP(F32x4Qfma)
VISIT_SIMD_QFMOP(F32x4Qfms)
VISIT_SIMD_QFMOP(F16x8Qfma)
VISIT_SIMD_QFMOP(F16x8Qfms)
#undef VISIT_SIMD_QFMOP

namespace {

struct ShuffleEntry {
  uint8_t shuffle[kSimd128Size];
  ArchOpcode opcode;
};

static const ShuffleEntry arch_shuffles[] = {
    {{0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23},
     kArm64S32x4ZipLeft},
    {{8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31},
     kArm64S32x4ZipRight},
    {{0, 1, 2, 3, 8, 9, 10, 11, 16, 17, 18, 19, 24, 25, 26, 27},
     kArm64S32x4UnzipLeft},
    {{4, 5, 6, 7, 12, 13, 14, 15, 20, 21, 22, 23, 28, 29, 30, 31},
     kArm64S32x4UnzipRight},
    {{0, 1, 2, 3, 16, 17, 18, 19, 8, 9, 10, 11, 24, 25, 26, 27},
     kArm64S32x4TransposeLeft},
    {{4, 5, 6, 7, 20, 21, 22, 23, 12, 13, 14, 15, 21, 22, 23, 24},
     kArm64S32x4TransposeRight},
    {{4, 5, 6, 7, 0, 1, 2, 3, 12, 13, 14, 15, 8, 9, 10, 11},
     kArm64S32x2Reverse},

    {{0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23},
     kArm64S16x8ZipLeft},
    {{8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31},
     kArm64S16x8ZipRight},
    {{0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29},
     kArm64S16x8UnzipLeft},
    {{2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31},
     kArm64S16x8UnzipRight},
    {{0, 1, 16, 17, 4, 5, 20, 21, 8, 9, 24, 25, 12, 13, 28, 29},
     kArm64S16x8TransposeLeft},
    {{2, 3, 18, 19, 6, 7, 22, 23, 10, 11, 26, 27, 14, 15, 30, 31},
     kArm64S16x8TransposeRight},
    {{6, 7, 4, 5, 2, 3, 0, 1, 14, 15, 12, 13, 10, 11, 8, 9},
     kArm64S16x4Reverse},
    {{2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13},
     kArm64S16x2Reverse},

    {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23},
     kArm64S8x16ZipLeft},
    {{8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31},
     kArm64S8x16ZipRight},
    {{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30},
     kArm64S8x16UnzipLeft},
    {{1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31},
     kArm64S8x16UnzipRight},
    {{0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30},
     kArm64S8x16TransposeLeft},
    {{1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31},
     kArm64S8x16TransposeRight},
    {{7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8}, kArm64S8x8Reverse},
    {{3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12}, kArm64S8x4Reverse},
    {{1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14},
     kArm64S8x2Reverse}};

bool TryMatchArchShuffle(const uint8_t* shuffle, const ShuffleEntry* table,
                         size_t num_entries, bool is_swizzle,
                         ArchOpcode* opcode) {
  uint8_t mask = is_swizzle ? kSimd128Size - 1 : 2 * kSimd128Size - 1;
  for (size_t i = 0; i < num_entries; i++) {
    const ShuffleEntry& entry = table[i];
    int j = 0;
    for (; j < kSimd128Size; j++) {
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
void ArrangeShuffleTable(Arm64OperandGeneratorT<Adapter>* g,
                         typename Adapter::node_t input0,
                         typename Adapter::node_t input1,
                         InstructionOperand* src0, InstructionOperand* src1) {
  if (input0 == input1) {
    // Unary, any q-register can be the table.
    *src0 = *src1 = g->UseRegister(input0);
  } else {
    // Binary, table registers must be consecutive.
    *src0 = g->UseFixed(input0, fp_fixed1);
    *src1 = g->UseFixed(input1, fp_fixed2);
  }
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
  uint8_t shuffle[kSimd128Size];
  bool is_swizzle;
  auto view = this->simd_shuffle_view(node);
  CanonicalizeShuffle(view, shuffle, &is_swizzle);
  node_t input0 = view.input(0);
  node_t input1 = view.input(1);
  uint8_t shuffle32x4[4];
  Arm64OperandGeneratorT<Adapter> g(this);
  ArchOpcode opcode;
  if (TryMatchArchShuffle(shuffle, arch_shuffles, arraysize(arch_shuffles),
                          is_swizzle, &opcode)) {
    Emit(opcode, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseRegister(input1));
    return;
  }
  uint8_t offset;
  if (wasm::SimdShuffle::TryMatchConcat(shuffle, &offset)) {
    Emit(kArm64S8x16Concat, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseRegister(input1), g.UseImmediate(offset));
    return;
  }
  int index = 0;
  uint8_t from = 0;
  uint8_t to = 0;
  if (wasm::SimdShuffle::TryMatch32x4Shuffle(shuffle, shuffle32x4)) {
    if (wasm::SimdShuffle::TryMatchSplat<4>(shuffle, &index)) {
      DCHECK_GT(4, index);
      Emit(kArm64S128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
           g.UseImmediate(4), g.UseImmediate(index % 4));
    } else if (wasm::SimdShuffle::TryMatch32x4Reverse(shuffle32x4)) {
      Emit(kArm64S32x4Reverse, g.DefineAsRegister(node), g.UseRegister(input0));
    } else if (wasm::SimdShuffle::TryMatch32x4OneLaneSwizzle(shuffle32x4, &from,
                                                             &to)) {
      Emit(kArm64S32x4OneLaneSwizzle, g.DefineAsRegister(node),
           g.UseRegister(input0), g.TempImmediate(from), g.TempImmediate(to));
    } else if (wasm::SimdShuffle::TryMatchIdentity(shuffle)) {
      // Bypass normal shuffle code generation in this case.
      // EmitIdentity
      MarkAsUsed(input0);
      MarkAsDefined(node);
      SetRename(node, input0);
    } else {
      Emit(kArm64S32x4Shuffle, g.DefineAsRegister(node), g.UseRegister(input0),
           g.UseRegister(input1),
           g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle32x4)));
    }
    return;
  }
  if (wasm::SimdShuffle::TryMatchSplat<8>(shuffle, &index)) {
    DCHECK_GT(8, index);
    Emit(kArm64S128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseImmediate(8), g.UseImmediate(index % 8));
    return;
  }
  if (wasm::SimdShuffle::TryMatchSplat<16>(shuffle, &index)) {
    DCHECK_GT(16, index);
    Emit(kArm64S128Dup, g.DefineAsRegister(node), g.UseRegister(input0),
         g.UseImmediate(16), g.UseImmediate(index % 16));
    return;
  }
  // Code generator uses vtbl, arrange sources to form a valid lookup table.
  InstructionOperand src0, src1;
  ArrangeShuffleTable(&g, input0, input1, &src0, &src1);
  Emit(kArm64I8x16Shuffle, g.DefineAsRegister(node), src0, src1,
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 4)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 8)),
       g.UseImmediate(wasm::SimdShuffle::Pack4Lanes(shuffle + 12)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSetStackPointer(node_t node) {
  OperandGenerator g(this);
  auto input = g.UseRegister(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt32(node_t node) {
  VisitRR(this, kArm64Sxtb32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt32(node_t node) {
  VisitRR(this, kArm64Sxth32, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord8ToInt64(node_t node) {
  VisitRR(this, kArm64Sxtb, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord16ToInt64(node_t node) {
  VisitRR(this, kArm64Sxth, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSignExtendWord32ToInt64(node_t node) {
  VisitRR(this, kArm64Sxtw, node);
}

#if V8_ENABLE_WEBASSEMBLY
namespace {
template <typename Adapter>
void VisitPminOrPmax(InstructionSelectorT<Adapter>* selector, ArchOpcode opcode,
                     typename Adapter::node_t node) {
  Arm64OperandGeneratorT<Adapter> g(selector);
  // Need all unique registers because we first compare the two inputs, then
  // we need the inputs to remain unchanged for the bitselect later.
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseUniqueRegister(selector->input_at(node, 0)),
                 g.UseUniqueRegister(selector->input_at(node, 1)));
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Pmin(node_t node) {
  VisitPminOrPmax(this, kArm64F16x8Pmin, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Pmax(node_t node) {
  VisitPminOrPmax(this, kArm64F16x8Pmax, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmin(node_t node) {
  VisitPminOrPmax(this, kArm64F32x4Pmin, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmax(node_t node) {
  VisitPminOrPmax(this, kArm64F32x4Pmax, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmin(node_t node) {
  VisitPminOrPmax(this, kArm64F64x2Pmin, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmax(node_t node) {
  VisitPminOrPmax(this, kArm64F64x2Pmax, node);
}

namespace {
template <typename Adapter>
void VisitSignExtendLong(InstructionSelectorT<Adapter>* selector,
                         ArchOpcode opcode, typename Adapter::node_t node,
                         int lane_size) {
  InstructionCode code = opcode;
  code |= LaneSizeField::encode(lane_size);
  VisitRR(selector, code, node);
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2SConvertI32x4Low(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2SConvertI32x4High(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl2, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2UConvertI32x4Low(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2UConvertI32x4High(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl2, node, 64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertI16x8Low(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertI16x8High(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl2, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertI16x8Low(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertI16x8High(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl2, node, 32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8SConvertI8x16Low(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8SConvertI8x16High(node_t node) {
  VisitSignExtendLong(this, kArm64Sxtl2, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8UConvertI8x16Low(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8UConvertI8x16High(node_t node) {
  VisitSignExtendLong(this, kArm64Uxtl2, node, 16);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Popcnt(node_t node) {
  InstructionCode code = kArm64Cnt;
  code |= LaneSizeField::encode(8);
  VisitRR(this, code, node);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGenerator* g, int first_input_index, node_t node) {
  continuation_outputs_.push_back(g->DefineAsRegister(node));
}

// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  auto flags = MachineOperatorBuilder::kFloat32RoundDown |
               MachineOperatorBuilder::kFloat64RoundDown |
               MachineOperatorBuilder::kFloat32RoundUp |
               MachineOperatorBuilder::kFloat64RoundUp |
               MachineOperatorBuilder::kFloat32RoundTruncate |
               MachineOperatorBuilder::kFloat64RoundTruncate |
               MachineOperatorBuilder::kFloat64RoundTiesAway |
               MachineOperatorBuilder::kFloat32RoundTiesEven |
               MachineOperatorBuilder::kFloat64RoundTiesEven |
               MachineOperatorBuilder::kWord32Popcnt |
               MachineOperatorBuilder::kWord64Popcnt |
               MachineOperatorBuilder::kWord32ShiftIsSafe |
               MachineOperatorBuilder::kInt32DivIsSafe |
               MachineOperatorBuilder::kUint32DivIsSafe |
               MachineOperatorBuilder::kWord32ReverseBits |
               MachineOperatorBuilder::kWord64ReverseBits |
               MachineOperatorBuilder::kSatConversionIsSafe |
               MachineOperatorBuilder::kFloat32Select |
               MachineOperatorBuilder::kFloat64Select |
               MachineOperatorBuilder::kWord32Select |
               MachineOperatorBuilder::kWord64Select |
               MachineOperatorBuilder::kLoadStorePairs;
  if (CpuFeatures::IsSupported(FP16)) {
    flags |= MachineOperatorBuilder::kFloat16 |
             MachineOperatorBuilder::kFloat64ToFloat16;
  }
  return flags;
}

// static
MachineOperatorBuilder::AlignmentRequirements
InstructionSelector::AlignmentRequirements() {
  return MachineOperatorBuilder::AlignmentRequirements::
      FullUnalignedAccessSupport();
}

template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurbofanAdapter>;
template class EXPORT_TEMPLATE_DEFINE(V8_EXPORT_PRIVATE)
    InstructionSelectorT<TurboshaftAdapter>;

}  // namespace compiler
}  // namespace internal
}  // namespace v8
