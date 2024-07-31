// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_IMPL_H_
#define V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_IMPL_H_

#include "src/codegen/macro-assembler.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/linkage.h"
#include "src/compiler/schedule.h"
#include "src/objects/tagged-index.h"

namespace v8 {
namespace internal {
namespace compiler {

template <typename Adapter>
struct CaseInfoT {
  int32_t value;  // The case value.
  int32_t order;  // The order for lowering to comparisons (less means earlier).
  typename Adapter::block_t
      branch;  // The basic blocks corresponding to the case value.
};

template <typename Adapter>
inline bool operator<(const CaseInfoT<Adapter>& l,
                      const CaseInfoT<Adapter>& r) {
  return l.order < r.order;
}

// Helper struct containing data about a table or lookup switch.
template <typename Adapter>
class SwitchInfoT {
 public:
  using CaseInfo = CaseInfoT<Adapter>;
  using block_t = typename Adapter::block_t;
  SwitchInfoT(ZoneVector<CaseInfo> const& cases, int32_t min_value,
              int32_t max_value, block_t default_branch)
      : cases_(cases),
        min_value_(min_value),
        max_value_(max_value),
        default_branch_(default_branch) {
    if (cases.size() != 0) {
      DCHECK_LE(min_value, max_value);
      // Note that {value_range} can be 0 if {min_value} is -2^31 and
      // {max_value} is 2^31-1, so don't assume that it's non-zero below.
      value_range_ = 1u + base::bit_cast<uint32_t>(max_value) -
                     base::bit_cast<uint32_t>(min_value);
    } else {
      value_range_ = 0;
    }
  }

  std::vector<CaseInfo> CasesSortedByValue() const {
    std::vector<CaseInfo> result(cases_.begin(), cases_.end());
    std::stable_sort(result.begin(), result.end(),
                     [](CaseInfo a, CaseInfo b) { return a.value < b.value; });
    return result;
  }
  const ZoneVector<CaseInfo>& CasesUnsorted() const { return cases_; }
  int32_t min_value() const { return min_value_; }
  int32_t max_value() const { return max_value_; }
  size_t value_range() const { return value_range_; }
  size_t case_count() const { return cases_.size(); }
  block_t default_branch() const { return default_branch_; }

 private:
  const ZoneVector<CaseInfo>& cases_;
  int32_t min_value_;   // minimum value of {cases_}
  int32_t max_value_;   // maximum value of {cases_}
  size_t value_range_;  // |max_value - min_value| + 1
  block_t default_branch_;
};

#define OPERAND_GENERATOR_T_BOILERPLATE(adapter)             \
  using super = OperandGeneratorT<adapter>;                  \
  using node_t = typename adapter::node_t;                   \
  using optional_node_t = typename adapter::optional_node_t; \
  using RegisterMode = typename super::RegisterMode;         \
  using RegisterUseKind = typename super::RegisterUseKind;   \
  using super::selector;                                     \
  using super::DefineAsRegister;                             \
  using super::TempImmediate;                                \
  using super::UseFixed;                                     \
  using super::UseImmediate;                                 \
  using super::UseImmediate64;                               \
  using super::UseNegatedImmediate;                          \
  using super::UseRegister;                                  \
  using super::UseRegisterWithMode;                          \
  using super::UseUniqueRegister;

// A helper class for the instruction selector that simplifies construction of
// Operands. This class implements a base for architecture-specific helpers.
template <typename Adapter>
class OperandGeneratorT : public Adapter {
 public:
  using block_t = typename Adapter::block_t;
  using node_t = typename Adapter::node_t;
  using optional_node_t = typename Adapter::optional_node_t;

  explicit OperandGeneratorT(InstructionSelectorT<Adapter>* selector)
      : Adapter(selector->schedule()), selector_(selector) {}

  InstructionOperand NoOutput() {
    return InstructionOperand();  // Generates an invalid operand.
  }

  InstructionOperand DefineAsRegister(node_t node) {
    return Define(node,
                  UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                     GetVReg(node)));
  }

  InstructionOperand DefineSameAsInput(node_t node, int input_index) {
    return Define(node, UnallocatedOperand(GetVReg(node), input_index));
  }

  InstructionOperand DefineSameAsFirst(node_t node) {
    return DefineSameAsInput(node, 0);
  }

  InstructionOperand DefineAsFixed(node_t node, Register reg) {
    return Define(node, UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER,
                                           reg.code(), GetVReg(node)));
  }

  template <typename FPRegType>
  InstructionOperand DefineAsFixed(node_t node, FPRegType reg) {
    return Define(node,
                  UnallocatedOperand(UnallocatedOperand::FIXED_FP_REGISTER,
                                     reg.code(), GetVReg(node)));
  }

  InstructionOperand DefineAsConstant(node_t node) {
    selector()->MarkAsDefined(node);
    int virtual_register = GetVReg(node);
    sequence()->AddConstant(virtual_register, ToConstant(node));
    return ConstantOperand(virtual_register);
  }

  InstructionOperand DefineAsLocation(node_t node, LinkageLocation location) {
    return Define(node, ToUnallocatedOperand(location, GetVReg(node)));
  }

  InstructionOperand DefineAsDualLocation(node_t node,
                                          LinkageLocation primary_location,
                                          LinkageLocation secondary_location) {
    return Define(node,
                  ToDualLocationUnallocatedOperand(
                      primary_location, secondary_location, GetVReg(node)));
  }

  InstructionOperand Use(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::NONE,
                                        UnallocatedOperand::USED_AT_START,
                                        GetVReg(node)));
  }

  InstructionOperand UseAnyAtEnd(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::REGISTER_OR_SLOT,
                                        UnallocatedOperand::USED_AT_END,
                                        GetVReg(node)));
  }

  InstructionOperand UseAny(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::REGISTER_OR_SLOT,
                                        UnallocatedOperand::USED_AT_START,
                                        GetVReg(node)));
  }

  InstructionOperand UseRegisterOrSlotOrConstant(node_t node) {
    return Use(node, UnallocatedOperand(
                         UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT,
                         UnallocatedOperand::USED_AT_START, GetVReg(node)));
  }

  InstructionOperand UseUniqueRegisterOrSlotOrConstant(node_t node) {
    return Use(node, UnallocatedOperand(
                         UnallocatedOperand::REGISTER_OR_SLOT_OR_CONSTANT,
                         GetVReg(node)));
  }

  InstructionOperand UseRegister(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                        UnallocatedOperand::USED_AT_START,
                                        GetVReg(node)));
  }

  InstructionOperand UseRegisterAtEnd(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                        UnallocatedOperand::USED_AT_END,
                                        GetVReg(node)));
  }

  InstructionOperand UseUniqueSlot(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_SLOT,
                                        GetVReg(node)));
  }

  // Use register or operand for the node. If a register is chosen, it won't
  // alias any temporary or output registers.
  InstructionOperand UseUnique(node_t node) {
    return Use(node,
               UnallocatedOperand(UnallocatedOperand::NONE, GetVReg(node)));
  }

  // Use a unique register for the node that does not alias any temporary or
  // output registers.
  InstructionOperand UseUniqueRegister(node_t node) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                        GetVReg(node)));
  }

  enum class RegisterUseKind { kUseRegister, kUseUniqueRegister };
  InstructionOperand UseRegister(node_t node, RegisterUseKind unique_reg) {
    if (V8_LIKELY(unique_reg == RegisterUseKind::kUseRegister)) {
      return UseRegister(node);
    } else {
      DCHECK_EQ(unique_reg, RegisterUseKind::kUseUniqueRegister);
      return UseUniqueRegister(node);
    }
  }

  InstructionOperand UseFixed(node_t node, Register reg) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER,
                                        reg.code(), GetVReg(node)));
  }

  template <typename FPRegType>
  InstructionOperand UseFixed(node_t node, FPRegType reg) {
    return Use(node, UnallocatedOperand(UnallocatedOperand::FIXED_FP_REGISTER,
                                        reg.code(), GetVReg(node)));
  }

  InstructionOperand UseImmediate(int immediate) {
    return sequence()->AddImmediate(Constant(immediate));
  }

  InstructionOperand UseImmediate64(int64_t immediate) {
    return sequence()->AddImmediate(Constant(immediate));
  }

  InstructionOperand UseImmediate(node_t node) {
    return sequence()->AddImmediate(ToConstant(node));
  }

  InstructionOperand UseNegatedImmediate(node_t node) {
    return sequence()->AddImmediate(ToNegatedConstant(node));
  }

  InstructionOperand UseLocation(node_t node, LinkageLocation location) {
    return Use(node, ToUnallocatedOperand(location, GetVReg(node)));
  }

  // Used to force gap moves from the from_location to the to_location
  // immediately before an instruction.
  InstructionOperand UsePointerLocation(LinkageLocation to_location,
                                        LinkageLocation from_location) {
    UnallocatedOperand casted_from_operand =
        UnallocatedOperand::cast(TempLocation(from_location));
    selector_->Emit(kArchNop, casted_from_operand);
    return ToUnallocatedOperand(to_location,
                                casted_from_operand.virtual_register());
  }

  InstructionOperand TempRegister() {
    return UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                              UnallocatedOperand::USED_AT_START,
                              sequence()->NextVirtualRegister());
  }

  int AllocateVirtualRegister() { return sequence()->NextVirtualRegister(); }

  InstructionOperand DefineSameAsFirstForVreg(int vreg) {
    return UnallocatedOperand(UnallocatedOperand::SAME_AS_INPUT, vreg);
  }

  InstructionOperand DefineAsRegistertForVreg(int vreg) {
    return UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER, vreg);
  }

  InstructionOperand UseRegisterForVreg(int vreg) {
    return UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                              UnallocatedOperand::USED_AT_START, vreg);
  }

  // The kind of register generated for memory operands. kRegister is alive
  // until the start of the operation, kUniqueRegister until the end.
  enum RegisterMode {
    kRegister,
    kUniqueRegister,
  };

  InstructionOperand UseRegisterWithMode(node_t node,
                                         RegisterMode register_mode) {
    return register_mode == kRegister ? UseRegister(node)
                                      : UseUniqueRegister(node);
  }

  InstructionOperand TempDoubleRegister() {
    UnallocatedOperand op = UnallocatedOperand(
        UnallocatedOperand::MUST_HAVE_REGISTER,
        UnallocatedOperand::USED_AT_START, sequence()->NextVirtualRegister());
    sequence()->MarkAsRepresentation(MachineRepresentation::kFloat64,
                                     op.virtual_register());
    return op;
  }

  InstructionOperand TempSimd128Register() {
    UnallocatedOperand op = UnallocatedOperand(
        UnallocatedOperand::MUST_HAVE_REGISTER,
        UnallocatedOperand::USED_AT_START, sequence()->NextVirtualRegister());
    sequence()->MarkAsRepresentation(MachineRepresentation::kSimd128,
                                     op.virtual_register());
    return op;
  }

  InstructionOperand TempSimd256Register() {
    UnallocatedOperand op = UnallocatedOperand(
        UnallocatedOperand::MUST_HAVE_REGISTER,
        UnallocatedOperand::USED_AT_START, sequence()->NextVirtualRegister());
    sequence()->MarkAsRepresentation(MachineRepresentation::kSimd256,
                                     op.virtual_register());
    return op;
  }

  InstructionOperand TempRegister(Register reg) {
    return UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER, reg.code(),
                              InstructionOperand::kInvalidVirtualRegister);
  }

  InstructionOperand TempRegister(int code) {
    return UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER, code,
                              sequence()->NextVirtualRegister());
  }

  template <typename FPRegType>
  InstructionOperand TempFpRegister(FPRegType reg) {
    UnallocatedOperand op =
        UnallocatedOperand(UnallocatedOperand::FIXED_FP_REGISTER, reg.code(),
                           sequence()->NextVirtualRegister());
    sequence()->MarkAsRepresentation(MachineRepresentation::kSimd128,
                                     op.virtual_register());
    return op;
  }

  InstructionOperand TempImmediate(int32_t imm) {
    return sequence()->AddImmediate(Constant(imm));
  }

  InstructionOperand TempLocation(LinkageLocation location) {
    return ToUnallocatedOperand(location, sequence()->NextVirtualRegister());
  }

  InstructionOperand Label(block_t block) {
    return sequence()->AddImmediate(Constant(this->rpo_number(block)));
  }

 protected:
  InstructionSelectorT<Adapter>* selector() const { return selector_; }
  InstructionSequence* sequence() const { return selector()->sequence(); }
  Zone* zone() const { return selector()->instruction_zone(); }

 private:
  int GetVReg(node_t node) const { return selector_->GetVirtualRegister(node); }

  Constant ToConstant(node_t node) {
    if constexpr (Adapter::IsTurboshaft) {
      using Kind = turboshaft::ConstantOp::Kind;
      if (const turboshaft::ConstantOp* constant =
              this->turboshaft_graph()
                  ->Get(node)
                  .template TryCast<turboshaft::ConstantOp>()) {
        switch (constant->kind) {
          case Kind::kWord32:
            return Constant(static_cast<int32_t>(constant->word32()));
          case Kind::kWord64:
            return Constant(static_cast<int64_t>(constant->word64()));
          case Kind::kSmi:
            if constexpr (Is64()) {
              return Constant(static_cast<int64_t>(constant->smi().ptr()));
            } else {
              return Constant(static_cast<int32_t>(constant->smi().ptr()));
            }
          case Kind::kHeapObject:
          case Kind::kCompressedHeapObject:
            return Constant(constant->handle(),
                            constant->kind == Kind::kCompressedHeapObject);
          case Kind::kExternal:
            return Constant(constant->external_reference());
          case Kind::kNumber:
            return Constant(constant->number());
          case Kind::kFloat32:
            return Constant(constant->float32());
          case Kind::kFloat64:
            return Constant(constant->float64());
          case Kind::kTaggedIndex: {
            // Unencoded index value.
            intptr_t value = static_cast<intptr_t>(constant->tagged_index());
            DCHECK(TaggedIndex::IsValid(value));
            // Generate it as 32/64-bit constant in a tagged form.
            Address tagged_index = TaggedIndex::FromIntptr(value).ptr();
            if (kSystemPointerSize == kInt32Size) {
              return Constant(static_cast<int32_t>(tagged_index));
            } else {
              return Constant(static_cast<int64_t>(tagged_index));
            }
          }
          case Kind::kRelocatableWasmCall:
          case Kind::kRelocatableWasmStubCall: {
            uint64_t value = constant->integral();
            auto mode = constant->kind == Kind::kRelocatableWasmCall
                            ? RelocInfo::WASM_CALL
                            : RelocInfo::WASM_STUB_CALL;
            if (Is64()) {
              return Constant(RelocatablePtrConstantInfo(
                  base::checked_cast<int64_t>(value), mode));
            } else {
              return Constant(RelocatablePtrConstantInfo(
                  base::checked_cast<int32_t>(value), mode));
            }
          }
        }
      }
      UNREACHABLE();
    } else {
      switch (node->opcode()) {
        case IrOpcode::kInt32Constant:
          return Constant(OpParameter<int32_t>(node->op()));
        case IrOpcode::kInt64Constant:
          return Constant(OpParameter<int64_t>(node->op()));
        case IrOpcode::kTaggedIndexConstant: {
          // Unencoded index value.
          intptr_t value =
              static_cast<intptr_t>(OpParameter<int32_t>(node->op()));
          DCHECK(TaggedIndex::IsValid(value));
          // Generate it as 32/64-bit constant in a tagged form.
          Address tagged_index = TaggedIndex::FromIntptr(value).ptr();
          if (kSystemPointerSize == kInt32Size) {
            return Constant(static_cast<int32_t>(tagged_index));
          } else {
            return Constant(static_cast<int64_t>(tagged_index));
          }
        }
        case IrOpcode::kFloat32Constant:
          return Constant(OpParameter<float>(node->op()));
        case IrOpcode::kRelocatableInt32Constant:
        case IrOpcode::kRelocatableInt64Constant:
          return Constant(OpParameter<RelocatablePtrConstantInfo>(node->op()));
        case IrOpcode::kFloat64Constant:
        case IrOpcode::kNumberConstant:
          return Constant(OpParameter<double>(node->op()));
        case IrOpcode::kExternalConstant:
          return Constant(OpParameter<ExternalReference>(node->op()));
        case IrOpcode::kComment: {
          // We cannot use {intptr_t} here, since the Constant constructor would
          // be ambiguous on some architectures.
          using ptrsize_int_t =
              std::conditional<kSystemPointerSize == 8, int64_t, int32_t>::type;
          return Constant(reinterpret_cast<ptrsize_int_t>(
              OpParameter<const char*>(node->op())));
        }
        case IrOpcode::kHeapConstant:
          return Constant(HeapConstantOf(node->op()));
        case IrOpcode::kCompressedHeapConstant:
          return Constant(HeapConstantOf(node->op()), true);
        case IrOpcode::kDeadValue: {
          switch (DeadValueRepresentationOf(node->op())) {
            case MachineRepresentation::kBit:
            case MachineRepresentation::kWord32:
            case MachineRepresentation::kTagged:
            case MachineRepresentation::kTaggedSigned:
            case MachineRepresentation::kTaggedPointer:
            case MachineRepresentation::kCompressed:
            case MachineRepresentation::kCompressedPointer:
              return Constant(static_cast<int32_t>(0));
            case MachineRepresentation::kWord64:
              return Constant(static_cast<int64_t>(0));
            case MachineRepresentation::kFloat64:
              return Constant(static_cast<double>(0));
            case MachineRepresentation::kFloat32:
              return Constant(static_cast<float>(0));
            default:
              UNREACHABLE();
          }
          break;
        }
        default:
          break;
      }
    }
    UNREACHABLE();
  }

  Constant ToNegatedConstant(node_t node) {
    auto constant = this->constant_view(node);
    if (constant.is_int32()) return Constant(-constant.int32_value());
    DCHECK(constant.is_int64());
    return Constant(-constant.int64_value());
  }

  UnallocatedOperand Define(node_t node, UnallocatedOperand operand) {
    DCHECK(this->valid(node));
    DCHECK_EQ(operand.virtual_register(), GetVReg(node));
    selector()->MarkAsDefined(node);
    return operand;
  }

  UnallocatedOperand Use(node_t node, UnallocatedOperand operand) {
    DCHECK(this->valid(node));
    DCHECK_EQ(operand.virtual_register(), GetVReg(node));
    selector()->MarkAsUsed(node);
    return operand;
  }

  UnallocatedOperand ToDualLocationUnallocatedOperand(
      LinkageLocation primary_location, LinkageLocation secondary_location,
      int virtual_register) {
    // We only support the primary location being a register and the secondary
    // one a slot.
    DCHECK(primary_location.IsRegister() &&
           secondary_location.IsCalleeFrameSlot());
    int reg_id = primary_location.AsRegister();
    int slot_id = secondary_location.AsCalleeFrameSlot();
    return UnallocatedOperand(reg_id, slot_id, virtual_register);
  }

  UnallocatedOperand ToUnallocatedOperand(LinkageLocation location,
                                          int virtual_register) {
    if (location.IsAnyRegister() || location.IsNullRegister()) {
      // any machine register.
      return UnallocatedOperand(UnallocatedOperand::MUST_HAVE_REGISTER,
                                virtual_register);
    }
    if (location.IsCallerFrameSlot()) {
      // a location on the caller frame.
      return UnallocatedOperand(UnallocatedOperand::FIXED_SLOT,
                                location.AsCallerFrameSlot(), virtual_register);
    }
    if (location.IsCalleeFrameSlot()) {
      // a spill location on this (callee) frame.
      return UnallocatedOperand(UnallocatedOperand::FIXED_SLOT,
                                location.AsCalleeFrameSlot(), virtual_register);
    }
    // a fixed register.
    if (IsFloatingPoint(location.GetType().representation())) {
      return UnallocatedOperand(UnallocatedOperand::FIXED_FP_REGISTER,
                                location.AsRegister(), virtual_register);
    }
    return UnallocatedOperand(UnallocatedOperand::FIXED_REGISTER,
                              location.AsRegister(), virtual_register);
  }

  InstructionSelectorT<Adapter>* selector_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_INSTRUCTION_SELECTOR_IMPL_H_
