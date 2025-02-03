// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "src/base/bits.h"
#include "src/base/flags.h"
#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/ia32/assembler-ia32.h"
#include "src/codegen/ia32/register-ia32.h"
#include "src/codegen/machine-type.h"
#include "src/codegen/macro-assembler-base.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction-selector-adapter.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/frame.h"
#include "src/compiler/globals.h"
#include "src/compiler/linkage.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/write-barrier-kind.h"
#include "src/flags/flags.h"
#include "src/utils/utils.h"
#include "src/zone/zone-containers.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

namespace {

struct LoadStoreView {
  explicit LoadStoreView(const turboshaft::Operation& op) {
    DCHECK(op.Is<turboshaft::LoadOp>() || op.Is<turboshaft::StoreOp>());
    if (const turboshaft::LoadOp* load = op.TryCast<turboshaft::LoadOp>()) {
      base = load->base();
      index = load->index();
      offset = load->offset;
    } else {
      DCHECK(op.Is<turboshaft::StoreOp>());
      const turboshaft::StoreOp& store = op.Cast<turboshaft::StoreOp>();
      base = store.base();
      index = store.index();
      offset = store.offset;
    }
  }
  turboshaft::OpIndex base;
  turboshaft::OptionalOpIndex index;
  int32_t offset;
};

template <typename Adapter>
struct ScaledIndexMatch {
  using node_t = typename Adapter::node_t;

  node_t base;
  node_t index;
  int scale;
};

template <typename Adapter>
struct BaseWithScaledIndexAndDisplacementMatch {
  using node_t = typename Adapter::node_t;

  node_t base = {};
  node_t index = {};
  int scale = 0;
  int32_t displacement = 0;
  DisplacementMode displacement_mode = kPositiveDisplacement;
};

// Copied from x64, dropped kWord64 constant support.
bool MatchScaledIndex(InstructionSelectorT<TurboshaftAdapter>* selector,
                      turboshaft::OpIndex node, turboshaft::OpIndex* index,
                      int* scale, bool* power_of_two_plus_one) {
  DCHECK_NOT_NULL(index);
  DCHECK_NOT_NULL(scale);
  using namespace turboshaft;  // NOLINT(build/namespaces)

  auto MatchScaleConstant = [](const Operation& op, int& scale,
                               bool* plus_one) {
    const ConstantOp* constant = op.TryCast<ConstantOp>();
    if (constant == nullptr) return false;
    if (constant->kind != ConstantOp::Kind::kWord32) return false;

    uint64_t value = constant->integral();
    if (plus_one) *plus_one = false;
    if (value == 1) return (scale = 0), true;
    if (value == 2) return (scale = 1), true;
    if (value == 4) return (scale = 2), true;
    if (value == 8) return (scale = 3), true;
    if (plus_one == nullptr) return false;
    *plus_one = true;
    if (value == 3) return (scale = 1), true;
    if (value == 5) return (scale = 2), true;
    if (value == 9) return (scale = 3), true;
    return false;
  };

  const Operation& op = selector->Get(node);
  if (const WordBinopOp* binop = op.TryCast<WordBinopOp>()) {
    if (binop->kind != WordBinopOp::Kind::kMul) return false;
    if (MatchScaleConstant(selector->Get(binop->right()), *scale,
                           power_of_two_plus_one)) {
      *index = binop->left();
      return true;
    }
    if (MatchScaleConstant(selector->Get(binop->left()), *scale,
                           power_of_two_plus_one)) {
      *index = binop->right();
      return true;
    }
    return false;
  } else if (const ShiftOp* shift = op.TryCast<ShiftOp>()) {
    if (shift->kind != ShiftOp::Kind::kShiftLeft) return false;
    int32_t scale_value;
    if (selector->MatchIntegralWord32Constant(shift->right(), &scale_value)) {
      if (scale_value < 0 || scale_value > 3) return false;
      *index = shift->left();
      *scale = static_cast<int>(scale_value);
      if (power_of_two_plus_one) *power_of_two_plus_one = false;
      return true;
    }
  }
  return false;
}

std::optional<ScaledIndexMatch<TurboshaftAdapter>> TryMatchScaledIndex(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex node,
    bool allow_power_of_two_plus_one) {
  ScaledIndexMatch<TurboshaftAdapter> match;
  bool plus_one = false;
  if (MatchScaledIndex(selector, node, &match.index, &match.scale,
                       allow_power_of_two_plus_one ? &plus_one : nullptr)) {
    match.base = plus_one ? match.index : turboshaft::OpIndex{};
    return match;
  }
  return std::nullopt;
}

// Copied verbatim from x64 (just renamed).
std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacementForWordBinop(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex left,
    turboshaft::OpIndex right) {
  using namespace turboshaft;  // NOLINT(build/namespaces)

  BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
  result.displacement_mode = kPositiveDisplacement;

  auto OwnedByAddressingOperand = [](OpIndex) {
    // TODO(nicohartmann@): Consider providing this. For now we just allow
    // everything to be covered regardless of other uses.
    return true;
  };

  // Check (S + ...)
  if (MatchScaledIndex(selector, left, &result.index, &result.scale, nullptr) &&
      OwnedByAddressingOperand(left)) {
    result.displacement_mode = kPositiveDisplacement;

    // Check (S + (... binop ...))
    if (const WordBinopOp* right_binop =
            selector->Get(right).TryCast<WordBinopOp>()) {
      // Check (S + (B - D))
      if (right_binop->kind == WordBinopOp::Kind::kSub &&
          OwnedByAddressingOperand(right)) {
        if (!selector->MatchIntegralWord32Constant(right_binop->right(),
                                                   &result.displacement)) {
          return std::nullopt;
        }
        result.base = right_binop->left();
        result.displacement_mode = kNegativeDisplacement;
        return result;
      }
      // Check (S + (... + ...))
      if (right_binop->kind == WordBinopOp::Kind::kAdd &&
          OwnedByAddressingOperand(right)) {
        if (selector->MatchIntegralWord32Constant(right_binop->right(),
                                                  &result.displacement)) {
          // (S + (B + D))
          result.base = right_binop->left();
        } else if (selector->MatchIntegralWord32Constant(
                       right_binop->left(), &result.displacement)) {
          // (S + (D + B))
          result.base = right_binop->right();
        } else {
          // Treat it as (S + B)
          result.base = right;
          result.displacement = 0;
        }
        return result;
      }
    }

    // Check (S + D)
    if (selector->MatchIntegralWord32Constant(right, &result.displacement)) {
      result.base = OpIndex{};
      return result;
    }

    // Treat it as (S + B)
    result.base = right;
    result.displacement = 0;
    return result;
  }

  // Check ((... + ...) + ...)
  if (const WordBinopOp* left_add = selector->Get(left).TryCast<WordBinopOp>();
      left_add && left_add->kind == WordBinopOp::Kind::kAdd &&
      OwnedByAddressingOperand(left)) {
    // Check ((S + ...) + ...)
    if (MatchScaledIndex(selector, left_add->left(), &result.index,
                         &result.scale, nullptr)) {
      result.displacement_mode = kPositiveDisplacement;
      // Check ((S + D) + B)
      if (selector->MatchIntegralWord32Constant(left_add->right(),
                                                &result.displacement)) {
        result.base = right;
        return result;
      }
      // Check ((S + B) + D)
      if (selector->MatchIntegralWord32Constant(right, &result.displacement)) {
        result.base = left_add->right();
        return result;
      }
      // Treat it as (B + B) and use index as right B.
      result.base = left;
      result.index = right;
      result.scale = 0;
      DCHECK_EQ(result.displacement, 0);
      return result;
    }
  }

  DCHECK_EQ(result.index, OpIndex{});
  DCHECK_EQ(result.scale, 0);
  result.displacement_mode = kPositiveDisplacement;

  // Check (B + D)
  if (selector->MatchIntegralWord32Constant(right, &result.displacement)) {
    result.base = left;
    return result;
  }

  // Treat as (B + B) and use index as left B.
  result.index = left;
  result.base = right;
  return result;
}

// Copied verbatim from x64 (just renamed).
std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)

  // The BaseWithIndexAndDisplacementMatcher canonicalizes the order of
  // displacements and scale factors that are used as inputs, so instead of
  // enumerating all possible patterns by brute force, checking for node
  // clusters using the following templates in the following order suffices
  // to find all of the interesting cases (S = index * scale, B = base
  // input, D = displacement input):
  //
  // (S + (B + D))
  // (S + (B + B))
  // (S + D)
  // (S + B)
  // ((S + D) + B)
  // ((S + B) + D)
  // ((B + D) + B)
  // ((B + B) + D)
  // (B + D)
  // (B + B)
  BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
  result.displacement_mode = kPositiveDisplacement;

  const Operation& op = selector->Get(node);
  if (const LoadOp* load = op.TryCast<LoadOp>()) {
    result.base = load->base();
    result.index = load->index().value_or_invalid();
    result.scale = load->element_size_log2;
    result.displacement = load->offset;
    if (load->kind.tagged_base) result.displacement -= kHeapObjectTag;
    return result;
  } else if (const StoreOp* store = op.TryCast<StoreOp>()) {
    result.base = store->base();
    result.index = store->index().value_or_invalid();
    result.scale = store->element_size_log2;
    result.displacement = store->offset;
    if (store->kind.tagged_base) result.displacement -= kHeapObjectTag;
    return result;
  } else if (op.Is<WordBinopOp>()) {
    // Nothing to do here, fall into the case below.
#ifdef V8_ENABLE_WEBASSEMBLY
  } else if (const Simd128LaneMemoryOp* lane_op =
                 op.TryCast<Simd128LaneMemoryOp>()) {
    result.base = lane_op->base();
    result.index = lane_op->index();
    result.scale = 0;
    result.displacement = 0;
    if (lane_op->kind.tagged_base) result.displacement -= kHeapObjectTag;
    return result;
  } else if (const Simd128LoadTransformOp* load_transform =
                 op.TryCast<Simd128LoadTransformOp>()) {
    result.base = load_transform->base();
    result.index = load_transform->index();
    DCHECK_EQ(load_transform->offset, 0);
    result.scale = 0;
    result.displacement = 0;
    DCHECK(!load_transform->load_kind.tagged_base);
    return result;
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    return std::nullopt;
  }

  const WordBinopOp& binop = op.Cast<WordBinopOp>();
  OpIndex left = binop.left();
  OpIndex right = binop.right();
  return TryMatchBaseWithScaledIndexAndDisplacementForWordBinop(selector, left,
                                                                right);
}

}  // namespace

// Adds IA32-specific methods for generating operands.
template <typename Adapter>
class IA32OperandGeneratorT final : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit IA32OperandGeneratorT(InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  InstructionOperand UseByteRegister(node_t node) {
    // TODO(titzer): encode byte register use constraints.
    return UseFixed(node, edx);
  }

  bool CanBeMemoryOperand(InstructionCode opcode, node_t node, node_t input,
                          int effect_level) {
    if (!this->IsLoadOrLoadImmutable(input)) return false;
    if (!selector()->CanCover(node, input)) return false;
    if (effect_level != selector()->GetEffectLevel(input)) {
      return false;
    }
    MachineRepresentation rep =
        this->load_view(input).loaded_rep().representation();
    switch (opcode) {
      case kIA32And:
      case kIA32Or:
      case kIA32Xor:
      case kIA32Add:
      case kIA32Sub:
      case kIA32Cmp:
      case kIA32Test:
        return rep == MachineRepresentation::kWord32 || IsAnyTagged(rep);
      case kIA32Cmp16:
      case kIA32Test16:
        return rep == MachineRepresentation::kWord16;
      case kIA32Cmp8:
      case kIA32Test8:
        return rep == MachineRepresentation::kWord8;
      default:
        break;
    }
    return false;
  }

  bool CanBeImmediate(node_t node) {
    if (this->IsExternalConstant(node)) return true;
    if (!this->is_constant(node)) return false;
    auto constant = this->constant_view(node);
    if (constant.is_int32() || constant.is_relocatable_int32() ||
        constant.is_relocatable_int64()) {
      return true;
    }
    if (constant.is_number_zero()) {
      return true;
    }
    // If we want to support HeapConstant nodes here, we must find a way
    // to check that they're not in new-space without dereferencing the
    // handle (which isn't safe to do concurrently).
    return false;
  }

  int32_t GetImmediateIntegerValue(node_t node) {
    DCHECK(CanBeImmediate(node));
    auto constant = this->constant_view(node);
    if (constant.is_int32()) return constant.int32_value();
    DCHECK(constant.is_number_zero());
    return 0;
  }

  bool ValueFitsIntoImmediate(int64_t value) const {
    // int32_t min will overflow if displacement mode is kNegativeDisplacement.
    return std::numeric_limits<int32_t>::min() < value &&
           value <= std::numeric_limits<int32_t>::max();
  }

  AddressingMode GenerateMemoryOperandInputs(
      optional_node_t index, int scale, node_t base, int32_t displacement,
      DisplacementMode displacement_mode, InstructionOperand inputs[],
      size_t* input_count,
      RegisterMode register_mode = RegisterMode::kRegister) {
    AddressingMode mode = kMode_MRI;
    if (displacement_mode == kNegativeDisplacement) {
      displacement = base::bits::WraparoundNeg32(displacement);
    }
    if (this->valid(base) && this->is_constant(base)) {
      auto constant_base = this->constant_view(base);
      if (constant_base.is_int32()) {
        displacement = base::bits::WraparoundAdd32(displacement,
                                                   constant_base.int32_value());
        base = node_t{};
      }
    }
    if (this->valid(base)) {
      inputs[(*input_count)++] = UseRegisterWithMode(base, register_mode);
      if (this->valid(index)) {
        DCHECK(scale >= 0 && scale <= 3);
        inputs[(*input_count)++] =
            UseRegisterWithMode(this->value(index), register_mode);
        if (displacement != 0) {
          inputs[(*input_count)++] = TempImmediate(displacement);
          static const AddressingMode kMRnI_modes[] = {kMode_MR1I, kMode_MR2I,
                                                       kMode_MR4I, kMode_MR8I};
          mode = kMRnI_modes[scale];
        } else {
          static const AddressingMode kMRn_modes[] = {kMode_MR1, kMode_MR2,
                                                      kMode_MR4, kMode_MR8};
          mode = kMRn_modes[scale];
        }
      } else {
        if (displacement == 0) {
          mode = kMode_MR;
        } else {
          inputs[(*input_count)++] = TempImmediate(displacement);
          mode = kMode_MRI;
        }
      }
    } else {
      DCHECK(scale >= 0 && scale <= 3);
      if (this->valid(index)) {
        inputs[(*input_count)++] =
            UseRegisterWithMode(this->value(index), register_mode);
        if (displacement != 0) {
          inputs[(*input_count)++] = TempImmediate(displacement);
          static const AddressingMode kMnI_modes[] = {kMode_MRI, kMode_M2I,
                                                      kMode_M4I, kMode_M8I};
          mode = kMnI_modes[scale];
        } else {
          static const AddressingMode kMn_modes[] = {kMode_MR, kMode_M2,
                                                     kMode_M4, kMode_M8};
          mode = kMn_modes[scale];
        }
      } else {
        inputs[(*input_count)++] = TempImmediate(displacement);
        return kMode_MI;
      }
    }
    return mode;
  }

  AddressingMode GenerateMemoryOperandInputs(
      Node* index, int scale, Node* base, Node* displacement_node,
      DisplacementMode displacement_mode, InstructionOperand inputs[],
      size_t* input_count,
      RegisterMode register_mode = RegisterMode::kRegister) {
    int32_t displacement = (displacement_node == nullptr)
                               ? 0
                               : OpParameter<int32_t>(displacement_node->op());
    return GenerateMemoryOperandInputs(index, scale, base, displacement,
                                       displacement_mode, inputs, input_count,
                                       register_mode);
  }

  AddressingMode GetEffectiveAddressMemoryOperand(
      node_t node, InstructionOperand inputs[], size_t* input_count,
      RegisterMode register_mode = RegisterMode::kRegister) {
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const Operation& op = this->Get(node);
      if (op.Is<LoadOp>() || op.Is<StoreOp>()) {
        LoadStoreView load_or_store(op);
        if (ExternalReference reference;
            this->MatchExternalConstant(load_or_store.base, &reference) &&
            !load_or_store.index.valid()) {
          if (selector()->CanAddressRelativeToRootsRegister(reference)) {
            const ptrdiff_t delta =
                load_or_store.offset +
                MacroAssemblerBase::RootRegisterOffsetForExternalReference(
                    selector()->isolate(), reference);
            if (is_int32(delta)) {
              inputs[(*input_count)++] =
                  TempImmediate(static_cast<int32_t>(delta));
              return kMode_Root;
            }
          }
        }
      }

      auto m = TryMatchBaseWithScaledIndexAndDisplacement(selector(), node);
      DCHECK(m.has_value());
      if (TurboshaftAdapter::valid(m->base) &&
          this->Get(m->base).template Is<LoadRootRegisterOp>()) {
        DCHECK(!this->valid(m->index));
        DCHECK_EQ(m->scale, 0);
        DCHECK(ValueFitsIntoImmediate(m->displacement));
        inputs[(*input_count)++] =
            UseImmediate(static_cast<int>(m->displacement));
        return kMode_Root;
      } else if (ValueFitsIntoImmediate(m->displacement)) {
        return GenerateMemoryOperandInputs(
            m->index, m->scale, m->base, m->displacement, m->displacement_mode,
            inputs, input_count, register_mode);
      } else if (!TurboshaftAdapter::valid(m->base) &&
                 m->displacement_mode == kPositiveDisplacement) {
        // The displacement cannot be an immediate, but we can use the
        // displacement as base instead and still benefit from addressing
        // modes for the scale.
        UNIMPLEMENTED();
      } else {
        // TODO(nicohartmann@): Turn this into a `DCHECK` once we have some
        // coverage.
        CHECK_EQ(m->displacement, 0);
        inputs[(*input_count)++] = UseRegisterWithMode(m->base, register_mode);
        inputs[(*input_count)++] = UseRegisterWithMode(m->index, register_mode);
        return kMode_MR1;
      }
    } else {
      {
        LoadMatcher<ExternalReferenceMatcher> m(node);
        if (m.index().HasResolvedValue() && m.object().HasResolvedValue() &&
            selector()->CanAddressRelativeToRootsRegister(
                m.object().ResolvedValue())) {
          ptrdiff_t const delta =
              m.index().ResolvedValue() +
              MacroAssemblerBase::RootRegisterOffsetForExternalReference(
                  selector()->isolate(), m.object().ResolvedValue());
          if (is_int32(delta)) {
            inputs[(*input_count)++] =
                TempImmediate(static_cast<int32_t>(delta));
            return kMode_Root;
          }
        }
      }

      BaseWithIndexAndDisplacement32Matcher m(node, AddressOption::kAllowAll);
      DCHECK(m.matches());
      if (m.base() != nullptr &&
          m.base()->opcode() == IrOpcode::kLoadRootRegister) {
        DCHECK_EQ(m.index(), nullptr);
        DCHECK_EQ(m.scale(), 0);
        inputs[(*input_count)++] = UseImmediate(m.displacement());
        return kMode_Root;
      } else if ((m.displacement() == nullptr ||
                  CanBeImmediate(m.displacement()))) {
        return GenerateMemoryOperandInputs(
            m.index(), m.scale(), m.base(), m.displacement(),
            m.displacement_mode(), inputs, input_count, register_mode);
      } else {
        inputs[(*input_count)++] =
            UseRegisterWithMode(node->InputAt(0), register_mode);
        inputs[(*input_count)++] =
            UseRegisterWithMode(node->InputAt(1), register_mode);
        return kMode_MR1;
      }
    }
  }

  InstructionOperand GetEffectiveIndexOperand(node_t index,
                                              AddressingMode* mode) {
    if (CanBeImmediate(index)) {
      *mode = kMode_MRI;
      return UseImmediate(index);
    } else {
      *mode = kMode_MR1;
      return UseUniqueRegister(index);
    }
  }

  bool CanBeBetterLeftOperand(node_t node) const {
    return !selector()->IsLive(node);
  }
};

namespace {

ArchOpcode GetLoadOpcode(LoadRepresentation load_rep) {
  ArchOpcode opcode;
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat32:
      opcode = kIA32Movss;
      break;
    case MachineRepresentation::kFloat64:
      opcode = kIA32Movsd;
      break;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsSigned() ? kIA32Movsxbl : kIA32Movzxbl;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsSigned() ? kIA32Movsxwl : kIA32Movzxwl;
      break;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      opcode = kIA32Movl;
      break;
    case MachineRepresentation::kSimd128:
      opcode = kIA32Movdqu;
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
  return opcode;
}

template <typename Adapter>
void VisitRO(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  typename Adapter::node_t input = selector->input_at(node, 0);
  // We have to use a byte register as input to movsxb.
  InstructionOperand input_op =
      opcode == kIA32Movsxbl ? g.UseFixed(input, eax) : g.Use(input);
  selector->Emit(opcode, g.DefineAsRegister(node), input_op);
}

template <typename Adapter>
void VisitROWithTemp(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempRegister()};
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.Use(selector->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void VisitROWithTempSimd(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseUniqueRegister(selector->input_at(node, 0)),
                 arraysize(temps), temps);
}

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t node, InstructionCode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRROFloat(InstructionSelectorT<Adapter>* selector,
                   typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand operand1 = g.Use(selector->input_at(node, 1));
  if (selector->IsSupported(AVX)) {
    selector->Emit(opcode, g.DefineAsRegister(node), operand0, operand1);
  } else {
    selector->Emit(opcode, g.DefineSameAsFirst(node), operand0, operand1);
  }
}

// For float unary operations. Also allocates a temporary general register for
// used in external operands. If a temp is not required, use VisitRRSimd (since
// float and SIMD registers are the same on IA32).
template <typename Adapter>
void VisitFloatUnop(InstructionSelectorT<Adapter>* selector,
                    typename Adapter::node_t node,
                    typename Adapter::node_t input, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempRegister()};
  // No need for unique because inputs are float but temp is general.
  if (selector->IsSupported(AVX)) {
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(input),
                   arraysize(temps), temps);
  } else {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(input),
                   arraysize(temps), temps);
  }
}

#if V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void VisitRRSimd(InstructionSelectorT<Adapter>* selector,
                 typename Adapter::node_t node, ArchOpcode avx_opcode,
                 ArchOpcode sse_opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  if (selector->IsSupported(AVX)) {
    selector->Emit(avx_opcode, g.DefineAsRegister(node), operand0);
  } else {
    selector->Emit(sse_opcode, g.DefineSameAsFirst(node), operand0);
  }
}

template <typename Adapter>
void VisitRRSimd(InstructionSelectorT<Adapter>* selector,
                 typename Adapter::node_t node, ArchOpcode opcode) {
  VisitRRSimd(selector, node, opcode, opcode);
}

// TODO(v8:9198): Like VisitRROFloat, but for SIMD. SSE requires operand1 to be
// a register as we don't have memory alignment yet. For AVX, memory operands
// are fine, but can have performance issues if not aligned to 16/32 bytes
// (based on load size), see SDM Vol 1, chapter 14.9
template <typename Adapter>
void VisitRROSimd(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode avx_opcode,
                  ArchOpcode sse_opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  if (selector->IsSupported(AVX)) {
    selector->Emit(avx_opcode, g.DefineAsRegister(node), operand0,
                   g.UseRegister(selector->input_at(node, 1)));
  } else {
    selector->Emit(sse_opcode, g.DefineSameAsFirst(node), operand0,
                   g.UseRegister(selector->input_at(node, 1)));
  }
}

template <typename Adapter>
void VisitRRRSimd(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand dst = selector->IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand operand1 = g.UseRegister(selector->input_at(node, 1));
  selector->Emit(opcode, dst, operand0, operand1);
}

int32_t GetSimdLaneConstant(InstructionSelectorT<TurboshaftAdapter>* selector,
                            turboshaft::OpIndex node) {
  const turboshaft::Simd128ExtractLaneOp& op =
      selector->Get(node).template Cast<turboshaft::Simd128ExtractLaneOp>();
  return op.lane;
}

int32_t GetSimdLaneConstant(InstructionSelectorT<TurbofanAdapter>* selector,
                            Node* node) {
  return OpParameter<int32_t>(node->op());
}

template <typename Adapter>
void VisitRRISimd(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand operand1 =
      g.UseImmediate(GetSimdLaneConstant(selector, node));
  // 8x16 uses movsx_b on dest to extract a byte, which only works
  // if dest is a byte register.
  InstructionOperand dest = opcode == kIA32I8x16ExtractLaneS
                                ? g.DefineAsFixed(node, eax)
                                : g.DefineAsRegister(node);
  selector->Emit(opcode, dest, operand0, operand1);
}

template <typename Adapter>
void VisitRRISimd(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode avx_opcode,
                  ArchOpcode sse_opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand operand1 =
      g.UseImmediate(GetSimdLaneConstant(selector, node));
  if (selector->IsSupported(AVX)) {
    selector->Emit(avx_opcode, g.DefineAsRegister(node), operand0, operand1);
  } else {
    selector->Emit(sse_opcode, g.DefineSameAsFirst(node), operand0, operand1);
  }
}

template <typename Adapter>
void VisitRROSimdShift(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  if (g.CanBeImmediate(selector->input_at(node, 1))) {
    selector->Emit(opcode, g.DefineSameAsFirst(node),
                   g.UseRegister(selector->input_at(node, 0)),
                   g.UseImmediate(selector->input_at(node, 1)));
  } else {
    InstructionOperand operand0 =
        g.UseUniqueRegister(selector->input_at(node, 0));
    InstructionOperand operand1 =
        g.UseUniqueRegister(selector->input_at(node, 1));
    InstructionOperand temps[] = {g.TempSimd128Register(), g.TempRegister()};
    selector->Emit(opcode, g.DefineSameAsFirst(node), operand0, operand1,
                   arraysize(temps), temps);
  }
}

template <typename Adapter>
void VisitRRRR(InstructionSelectorT<Adapter>* selector,
               typename Adapter::node_t node, InstructionCode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.UseRegister(selector->input_at(node, 1)),
                 g.UseRegister(selector->input_at(node, 2)));
}

template <typename Adapter>
void VisitI8x16Shift(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand output = CpuFeatures::IsSupported(AVX)
                                  ? g.UseRegister(node)
                                  : g.DefineSameAsFirst(node);

  if (g.CanBeImmediate(selector->input_at(node, 1))) {
    if (opcode == kIA32I8x16ShrS) {
      selector->Emit(opcode, output, g.UseRegister(selector->input_at(node, 0)),
                     g.UseImmediate(selector->input_at(node, 1)));
    } else {
      InstructionOperand temps[] = {g.TempRegister()};
      selector->Emit(opcode, output, g.UseRegister(selector->input_at(node, 0)),
                     g.UseImmediate(selector->input_at(node, 1)),
                     arraysize(temps), temps);
    }
  } else {
    InstructionOperand operand0 =
        g.UseUniqueRegister(selector->input_at(node, 0));
    InstructionOperand operand1 =
        g.UseUniqueRegister(selector->input_at(node, 1));
    InstructionOperand temps[] = {g.TempRegister(), g.TempSimd128Register()};
    selector->Emit(opcode, output, operand0, operand1, arraysize(temps), temps);
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

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
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(kArchAbortCSADcheck, g.NoOutput(),
       g.UseFixed(this->input_at(node, 0), edx));
}

#if V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoadLane(node_t node) {
  InstructionCode opcode;
  int lane;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Simd128LaneMemoryOp& load =
        this->Get(node).template Cast<Simd128LaneMemoryOp>();
    lane = load.lane;
    switch (load.lane_kind) {
      case Simd128LaneMemoryOp::LaneKind::k8:
        opcode = kIA32Pinsrb;
        break;
      case Simd128LaneMemoryOp::LaneKind::k16:
        opcode = kIA32Pinsrw;
        break;
      case Simd128LaneMemoryOp::LaneKind::k32:
        opcode = kIA32Pinsrd;
        break;
      case Simd128LaneMemoryOp::LaneKind::k64:
        // pinsrq not available on IA32.
        if (lane == 0) {
          opcode = kIA32Movlps;
        } else {
          DCHECK_EQ(1, lane);
          opcode = kIA32Movhps;
        }
        break;
    }
    // IA32 supports unaligned loads.
    DCHECK(!load.kind.maybe_unaligned);
    // Trap handler is not supported on IA32.
    DCHECK(!load.kind.with_trap_handler);
  } else {
    // Turbofan.
    LoadLaneParameters params = LoadLaneParametersOf(node->op());
    lane = params.laneidx;
    if (params.rep == MachineType::Int8()) {
      opcode = kIA32Pinsrb;
    } else if (params.rep == MachineType::Int16()) {
      opcode = kIA32Pinsrw;
    } else if (params.rep == MachineType::Int32()) {
      opcode = kIA32Pinsrd;
    } else if (params.rep == MachineType::Int64()) {
      // pinsrq not available on IA32.
      if (params.laneidx == 0) {
        opcode = kIA32Movlps;
      } else {
        DCHECK_EQ(1, params.laneidx);
        opcode = kIA32Movhps;
      }
    } else {
      UNREACHABLE();
    }
    // IA32 supports unaligned loads.
    DCHECK_NE(params.kind, MemoryAccessKind::kUnaligned);
    // Trap handler is not supported on IA32.
    DCHECK_NE(params.kind, MemoryAccessKind::kProtected);
  }

  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand outputs[] = {IsSupported(AVX) ? g.DefineAsRegister(node)
                                                   : g.DefineSameAsFirst(node)};
  // Input 0 is value node, 1 is lane idx, and GetEffectiveAddressMemoryOperand
  // uses up to 3 inputs. This ordering is consistent with other operations that
  // use the same opcode.
  InstructionOperand inputs[5];
  size_t input_count = 0;

  inputs[input_count++] = g.UseRegister(this->input_at(node, 2));
  inputs[input_count++] = g.UseImmediate(lane);

  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(mode);

  DCHECK_GE(5, input_count);

  Emit(opcode, 1, outputs, input_count, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadTransform(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LoadTransformOp& op =
      this->Get(node).Cast<Simd128LoadTransformOp>();
  ArchOpcode opcode;
  switch (op.transform_kind) {
    case Simd128LoadTransformOp::TransformKind::k8x8S:
      opcode = kIA32S128Load8x8S;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8U:
      opcode = kIA32S128Load8x8U;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4S:
      opcode = kIA32S128Load16x4S;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4U:
      opcode = kIA32S128Load16x4U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2S:
      opcode = kIA32S128Load32x2S;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2U:
      opcode = kIA32S128Load32x2U;
      break;
    case Simd128LoadTransformOp::TransformKind::k8Splat:
      opcode = kIA32S128Load8Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k16Splat:
      opcode = kIA32S128Load16Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Splat:
      opcode = kIA32S128Load32Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Splat:
      opcode = kIA32S128Load64Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Zero:
      opcode = kIA32Movss;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Zero:
      opcode = kIA32Movsd;
      break;
  }

  // IA32 supports unaligned loads
  DCHECK(!op.load_kind.maybe_unaligned);
  // Trap handler is not supported on IA32.
  DCHECK(!op.load_kind.with_trap_handler);

  VisitLoad(node, node, opcode);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadTransform(Node* node) {
  LoadTransformParameters params = LoadTransformParametersOf(node->op());
  InstructionCode opcode;
  switch (params.transformation) {
    case LoadTransformation::kS128Load8Splat:
      opcode = kIA32S128Load8Splat;
      break;
    case LoadTransformation::kS128Load16Splat:
      opcode = kIA32S128Load16Splat;
      break;
    case LoadTransformation::kS128Load32Splat:
      opcode = kIA32S128Load32Splat;
      break;
    case LoadTransformation::kS128Load64Splat:
      opcode = kIA32S128Load64Splat;
      break;
    case LoadTransformation::kS128Load8x8S:
      opcode = kIA32S128Load8x8S;
      break;
    case LoadTransformation::kS128Load8x8U:
      opcode = kIA32S128Load8x8U;
      break;
    case LoadTransformation::kS128Load16x4S:
      opcode = kIA32S128Load16x4S;
      break;
    case LoadTransformation::kS128Load16x4U:
      opcode = kIA32S128Load16x4U;
      break;
    case LoadTransformation::kS128Load32x2S:
      opcode = kIA32S128Load32x2S;
      break;
    case LoadTransformation::kS128Load32x2U:
      opcode = kIA32S128Load32x2U;
      break;
    case LoadTransformation::kS128Load32Zero:
      opcode = kIA32Movss;
      break;
    case LoadTransformation::kS128Load64Zero:
      opcode = kIA32Movsd;
      break;
    default:
      UNREACHABLE();
  }

  // IA32 supports unaligned loads.
  DCHECK_NE(params.kind, MemoryAccessKind::kUnaligned);
  // Trap handler is not supported on IA32.
  DCHECK_NE(params.kind, MemoryAccessKind::kProtected);

  VisitLoad(node, node, opcode);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node, node_t value,
                                              InstructionCode opcode) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(node);
  InstructionOperand inputs[3];
  size_t input_count = 0;
  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(value, inputs, &input_count);
  InstructionCode code = opcode | AddressingModeField::encode(mode);
  Emit(code, 1, outputs, input_count, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node) {
  LoadRepresentation load_rep = this->load_view(node).loaded_rep();
  DCHECK(!load_rep.IsMapWord());
  VisitLoad(node, node, GetLoadOpcode(load_rep));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  // Trap handler is not supported on IA32.
  UNREACHABLE();
}

namespace {

ArchOpcode GetStoreOpcode(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kFloat32:
      return kIA32Movss;
    case MachineRepresentation::kFloat64:
      return kIA32Movsd;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      return kIA32Movb;
    case MachineRepresentation::kWord16:
      return kIA32Movw;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      return kIA32Movl;
    case MachineRepresentation::kSimd128:
      return kIA32Movdqu;
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

ArchOpcode GetSeqCstStoreOpcode(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kWord8:
      return kAtomicExchangeInt8;
    case MachineRepresentation::kWord16:
      return kAtomicExchangeInt16;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
    case MachineRepresentation::kWord32:
      return kAtomicExchangeWord32;
    default:
      UNREACHABLE();
  }
}

template <typename Adapter>
void VisitAtomicExchange(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode,
                         MachineRepresentation rep) {
  using node_t = typename Adapter::node_t;
  IA32OperandGeneratorT<Adapter> g(selector);
  node_t base = selector->input_at(node, 0);
  node_t index = selector->input_at(node, 1);
  node_t value = selector->input_at(node, 2);

  AddressingMode addressing_mode;
  InstructionOperand value_operand = (rep == MachineRepresentation::kWord8)
                                         ? g.UseFixed(value, edx)
                                         : g.UseUniqueRegister(value);
  InstructionOperand inputs[] = {
      value_operand, g.UseUniqueRegister(base),
      g.GetEffectiveIndexOperand(index, &addressing_mode)};
  InstructionOperand outputs[] = {
      (rep == MachineRepresentation::kWord8)
          // Using DefineSameAsFirst requires the register to be unallocated.
          ? g.DefineAsFixed(node, edx)
          : g.DefineSameAsFirst(node)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  selector->Emit(code, 1, outputs, arraysize(inputs), inputs);
}

template <typename Adapter>
void VisitStoreCommon(InstructionSelectorT<Adapter>* selector,
                      const typename Adapter::StoreView& store) {
  using node_t = typename Adapter::node_t;
  using optional_node_t = typename Adapter::optional_node_t;
  IA32OperandGeneratorT<Adapter> g(selector);

  node_t base = store.base();
  optional_node_t index = store.index();
  node_t value = store.value();
  int32_t displacement = store.displacement();
  uint8_t element_size_log2 = store.element_size_log2();
  std::optional<AtomicMemoryOrder> atomic_order = store.memory_order();
  StoreRepresentation store_rep = store.stored_rep();

  WriteBarrierKind write_barrier_kind = store_rep.write_barrier_kind();
  MachineRepresentation rep = store_rep.representation();
  const bool is_seqcst =
      atomic_order && *atomic_order == AtomicMemoryOrder::kSeqCst;

  if (v8_flags.enable_unconditional_write_barriers && CanBeTaggedPointer(rep)) {
    write_barrier_kind = kFullWriteBarrier;
  }

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(CanBeTaggedPointer(rep));
    AddressingMode addressing_mode;
    InstructionOperand inputs[4];
    size_t input_count = 0;
    addressing_mode = g.GenerateMemoryOperandInputs(
        index, element_size_log2, base, displacement,
        DisplacementMode::kPositiveDisplacement, inputs, &input_count,
        IA32OperandGeneratorT<Adapter>::RegisterMode::kUniqueRegister);
    DCHECK_LT(input_count, 4);
    inputs[input_count++] = g.UseUniqueRegister(value);
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    size_t const temp_count = arraysize(temps);
    InstructionCode code = is_seqcst ? kArchAtomicStoreWithWriteBarrier
                                     : kArchStoreWithWriteBarrier;
    code |= AddressingModeField::encode(addressing_mode);
    code |= RecordWriteModeField::encode(record_write_mode);
    selector->Emit(code, 0, nullptr, input_count, inputs, temp_count, temps);
  } else {
    InstructionOperand inputs[4];
    size_t input_count = 0;
    // To inform the register allocator that xchg clobbered its input.
    InstructionOperand outputs[1];
    size_t output_count = 0;
    ArchOpcode opcode;
    AddressingMode addressing_mode;

    if (is_seqcst) {
      // SeqCst stores emit XCHG instead of MOV, so encode the inputs as we
      // would for XCHG. XCHG can't encode the value as an immediate and has
      // fewer addressing modes available.
      if (rep == MachineRepresentation::kWord8 ||
          rep == MachineRepresentation::kBit) {
        inputs[input_count++] = g.UseFixed(value, edx);
        outputs[output_count++] = g.DefineAsFixed(store, edx);
      } else {
        inputs[input_count++] = g.UseUniqueRegister(value);
        outputs[output_count++] = g.DefineSameAsFirst(store);
      }
      addressing_mode = g.GetEffectiveAddressMemoryOperand(
          store, inputs, &input_count,
          IA32OperandGeneratorT<Adapter>::RegisterMode::kUniqueRegister);
      opcode = GetSeqCstStoreOpcode(rep);
    } else {
      // Release and non-atomic stores emit MOV.
      // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
      InstructionOperand val;
      if (g.CanBeImmediate(value)) {
        val = g.UseImmediate(value);
      } else if (!atomic_order && (rep == MachineRepresentation::kWord8 ||
                                   rep == MachineRepresentation::kBit)) {
        val = g.UseByteRegister(value);
      } else {
        val = g.UseUniqueRegister(value);
      }
      addressing_mode = g.GetEffectiveAddressMemoryOperand(
          store, inputs, &input_count,
          IA32OperandGeneratorT<Adapter>::RegisterMode::kUniqueRegister);
      inputs[input_count++] = val;
      opcode = GetStoreOpcode(rep);
    }
    InstructionCode code =
        opcode | AddressingModeField::encode(addressing_mode);
    selector->Emit(code, output_count, outputs, input_count, inputs);
  }
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(node_t node) {
  VisitStoreCommon(this, this->store_view(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  // Trap handler is not supported on IA32.
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStoreLane(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionCode opcode = kArchNop;
  int lane;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const Simd128LaneMemoryOp& store =
        this->Get(node).template Cast<Simd128LaneMemoryOp>();
    lane = store.lane;
    switch (store.lane_kind) {
      case Simd128LaneMemoryOp::LaneKind::k8:
        opcode = kIA32Pextrb;
        break;
      case Simd128LaneMemoryOp::LaneKind::k16:
        opcode = kIA32Pextrw;
        break;
      case Simd128LaneMemoryOp::LaneKind::k32:
        opcode = kIA32S128Store32Lane;
        break;
      case Simd128LaneMemoryOp::LaneKind::k64:
        if (lane == 0) {
          opcode = kIA32Movlps;
        } else {
          DCHECK_EQ(1, lane);
          opcode = kIA32Movhps;
        }
        break;
    }
  } else {
    StoreLaneParameters params = StoreLaneParametersOf(node->op());
    lane = params.laneidx;
    if (params.rep == MachineRepresentation::kWord8) {
      opcode = kIA32Pextrb;
    } else if (params.rep == MachineRepresentation::kWord16) {
      opcode = kIA32Pextrw;
    } else if (params.rep == MachineRepresentation::kWord32) {
      opcode = kIA32S128Store32Lane;
    } else if (params.rep == MachineRepresentation::kWord64) {
      if (params.laneidx == 0) {
        opcode = kIA32Movlps;
      } else {
        DCHECK_EQ(1, params.laneidx);
        opcode = kIA32Movhps;
      }
    } else {
      UNREACHABLE();
    }
  }

  InstructionOperand inputs[4];
  size_t input_count = 0;
  AddressingMode addressing_mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(addressing_mode);

  InstructionOperand value_operand = g.UseRegister(this->input_at(node, 2));
  inputs[input_count++] = value_operand;
  inputs[input_count++] = g.UseImmediate(lane);
  DCHECK_GE(4, input_count);
  Emit(opcode, 0, nullptr, input_count, inputs);
}
#endif  // V8_ENABLE_WEBASSEMBLY

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

namespace {

// Shared routine for multiple binary operations.
template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode,
                FlagsContinuationT<Adapter>* cont) {
  IA32OperandGeneratorT<Adapter> g(selector);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  InstructionOperand inputs[6];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  // TODO(turbofan): match complex addressing modes.
  if (left == right) {
    // If both inputs refer to the same operand, enforce allocating a register
    // for both of them to ensure that we don't end up generating code like
    // this:
    //
    //   mov eax, [ebp-0x10]
    //   add eax, [ebp-0x10]
    //   jo label
    InstructionOperand const input = g.UseRegister(left);
    inputs[input_count++] = input;
    inputs[input_count++] = input;
  } else if (g.CanBeImmediate(right)) {
    inputs[input_count++] = g.UseRegister(left);
    inputs[input_count++] = g.UseImmediate(right);
  } else {
    int effect_level = selector->GetEffectLevel(node, cont);
    if (selector->IsCommutative(node) && g.CanBeBetterLeftOperand(right) &&
        (!g.CanBeBetterLeftOperand(left) ||
         !g.CanBeMemoryOperand(opcode, node, right, effect_level))) {
      std::swap(left, right);
    }
    if (g.CanBeMemoryOperand(opcode, node, right, effect_level)) {
      inputs[input_count++] = g.UseRegister(left);
      AddressingMode addressing_mode =
          g.GetEffectiveAddressMemoryOperand(right, inputs, &input_count);
      opcode |= AddressingModeField::encode(addressing_mode);
    } else {
      inputs[input_count++] = g.UseRegister(left);
      inputs[input_count++] = g.Use(right);
    }
  }

  outputs[output_count++] = g.DefineSameAsFirst(node);

  DCHECK_NE(0u, input_count);
  DCHECK_EQ(1u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

template <typename Adapter>
void VisitBinop(InstructionSelectorT<Adapter>* selector,
                typename Adapter::node_t node, InstructionCode opcode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop(selector, node, opcode, &cont);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32And(node_t node) {
  VisitBinop(this, node, kIA32And);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
  VisitBinop(this, node, kIA32Or);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::WordBinopOp& binop =
        this->Get(node).template Cast<turboshaft::WordBinopOp>();
    int32_t constant;
    if (this->MatchIntegralWord32Constant(binop.right(), &constant) &&
        constant == -1) {
      Emit(kIA32Not, g.DefineSameAsFirst(node), g.UseRegister(binop.left()));
      return;
    }
  } else {
    Int32BinopMatcher m(node);
    if (m.right().Is(-1)) {
      Emit(kIA32Not, g.DefineSameAsFirst(node), g.UseRegister(m.left().node()));
      return;
    }
  }
  VisitBinop(this, node, kIA32Xor);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStackPointerGreaterThan(
    node_t node, FlagsContinuation* cont) {
  StackCheckKind kind;
  if constexpr (Adapter::IsTurboshaft) {
    kind = this->Get(node)
               .template Cast<turboshaft::StackPointerGreaterThanOp>()
               .kind;
  } else {
    kind = StackCheckKindOf(node->op());
  }
  {  // Temporary scope to minimize indentation change churn below.
    InstructionCode opcode = kArchStackPointerGreaterThan |
                             MiscField::encode(static_cast<int>(kind));

    int effect_level = GetEffectLevel(node, cont);

    IA32OperandGeneratorT<Adapter> g(this);

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

    node_t value = this->input_at(node, 0);
    if (g.CanBeMemoryOperand(kIA32Cmp, node, value, effect_level)) {
      DCHECK(this->IsLoadOrLoadImmutable(value));

      // GetEffectiveAddressMemoryOperand can create at most 3 inputs.
      static constexpr int kMaxInputCount = 3;

      size_t input_count = 0;
      InstructionOperand inputs[kMaxInputCount];
      AddressingMode addressing_mode = g.GetEffectiveAddressMemoryOperand(
          value, inputs, &input_count, register_mode);
      opcode |= AddressingModeField::encode(addressing_mode);
      DCHECK_LE(input_count, kMaxInputCount);

      EmitWithContinuation(opcode, output_count, outputs, input_count, inputs,
                           temp_count, temps, cont);
    } else {
      InstructionOperand inputs[] = {
          g.UseRegisterWithMode(value, register_mode)};
      static constexpr int input_count = arraysize(inputs);
      EmitWithContinuation(opcode, output_count, outputs, input_count, inputs,
                           temp_count, temps, cont);
    }
  }
}

// Shared routine for multiple shift operations.
template <typename Adapter>
static inline void VisitShift(InstructionSelectorT<Adapter>* selector,
                              typename Adapter::node_t node,
                              ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  if (g.CanBeImmediate(right)) {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseImmediate(right));
  } else {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseFixed(right, ecx));
  }
}

namespace {

template <typename Adapter>
void VisitMulHigh(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempRegister(eax)};
  selector->Emit(opcode, g.DefineAsFixed(node, edx),
                 g.UseFixed(selector->input_at(node, 0), eax),
                 g.UseUniqueRegister(selector->input_at(node, 1)),
                 arraysize(temps), temps);
}

template <typename Adapter>
void VisitDiv(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempRegister(edx)};
  selector->Emit(opcode, g.DefineAsFixed(node, eax),
                 g.UseFixed(selector->input_at(node, 0), eax),
                 g.UseUnique(selector->input_at(node, 1)), arraysize(temps),
                 temps);
}

template <typename Adapter>
void VisitMod(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode opcode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand temps[] = {g.TempRegister(eax)};
  selector->Emit(opcode, g.DefineAsFixed(node, edx),
                 g.UseFixed(selector->input_at(node, 0), eax),
                 g.UseUnique(selector->input_at(node, 1)), arraysize(temps),
                 temps);
}

// {Displacement} is either Adapter::node_t or int32_t.
template <typename Adapter, typename Displacement>
void EmitLea(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t result, typename Adapter::node_t index,
             int scale, typename Adapter::node_t base,
             Displacement displacement, DisplacementMode displacement_mode) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand inputs[4];
  size_t input_count = 0;
  AddressingMode mode =
      g.GenerateMemoryOperandInputs(index, scale, base, displacement,
                                    displacement_mode, inputs, &input_count);

  DCHECK_NE(0u, input_count);
  DCHECK_GE(arraysize(inputs), input_count);

  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(result);

  InstructionCode opcode = AddressingModeField::encode(mode) | kIA32Lea;

  selector->Emit(opcode, 1, outputs, input_count, inputs);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shl(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    if (auto m = TryMatchScaledIndex(this, node, true)) {
      EmitLea(this, node, m->index, m->scale, m->base, 0,
              kPositiveDisplacement);
      return;
    }
  } else {
    Int32ScaleMatcher m(node, true);
    if (m.matches()) {
      Node* index = node->InputAt(0);
      Node* base = m.power_of_two_plus_one() ? index : nullptr;
      EmitLea(this, node, index, m.scale(), base, nullptr,
              kPositiveDisplacement);
      return;
    }
  }
  VisitShift(this, node, kIA32Shl);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shr(node_t node) {
  VisitShift(this, node, kIA32Shr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Sar(node_t node) {
  VisitShift(this, node, kIA32Sar);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairAdd(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);

  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection1)) {
    // We use UseUniqueRegister here to avoid register sharing with the temp
    // register.
    InstructionOperand inputs[] = {
        g.UseRegister(this->input_at(node, 0)),
        g.UseUniqueRegisterOrSlotOrConstant(this->input_at(node, 1)),
        g.UseRegister(this->input_at(node, 2)),
        g.UseUniqueRegister(this->input_at(node, 3))};

    InstructionOperand outputs[] = {g.DefineSameAsFirst(node),
                                    g.DefineAsRegister(projection1)};

    InstructionOperand temps[] = {g.TempRegister()};

    Emit(kIA32AddPair, 2, outputs, 4, inputs, 1, temps);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kIA32Add, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)),
         g.Use(this->input_at(node, 2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairSub(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);

  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection1)) {
    // We use UseUniqueRegister here to avoid register sharing with the temp
    // register.
    InstructionOperand inputs[] = {
        g.UseRegister(this->input_at(node, 0)),
        g.UseUniqueRegisterOrSlotOrConstant(this->input_at(node, 1)),
        g.UseRegister(this->input_at(node, 2)),
        g.UseUniqueRegister(this->input_at(node, 3))};

    InstructionOperand outputs[] = {g.DefineSameAsFirst(node),
                                    g.DefineAsRegister(projection1)};

    InstructionOperand temps[] = {g.TempRegister()};

    Emit(kIA32SubPair, 2, outputs, 4, inputs, 1, temps);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kIA32Sub, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)),
         g.Use(this->input_at(node, 2)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32PairMul(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);

  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection1)) {
    // InputAt(3) explicitly shares ecx with OutputRegister(1) to save one
    // register and one mov instruction.
    InstructionOperand inputs[] = {
        g.UseUnique(this->input_at(node, 0)),
        g.UseUniqueRegisterOrSlotOrConstant(this->input_at(node, 1)),
        g.UseUniqueRegister(this->input_at(node, 2)),
        g.UseFixed(this->input_at(node, 3), ecx)};

    InstructionOperand outputs[] = {g.DefineAsFixed(node, eax),
                                    g.DefineAsFixed(projection1, ecx)};

    InstructionOperand temps[] = {g.TempRegister(edx)};

    Emit(kIA32MulPair, 2, outputs, 4, inputs, 1, temps);
  } else {
    // The high word of the result is not used, so we emit the standard 32 bit
    // instruction.
    Emit(kIA32Imul, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)),
         g.Use(this->input_at(node, 2)));
  }
}

template <typename Adapter>
void VisitWord32PairShift(InstructionSelectorT<Adapter>* selector,
                          InstructionCode opcode,
                          typename Adapter::node_t node) {
  using node_t = typename Adapter::node_t;
  IA32OperandGeneratorT<Adapter> g(selector);

  node_t shift = selector->input_at(node, 2);
  InstructionOperand shift_operand;
  if (g.CanBeImmediate(shift)) {
    shift_operand = g.UseImmediate(shift);
  } else {
    shift_operand = g.UseFixed(shift, ecx);
  }
  InstructionOperand inputs[] = {g.UseFixed(selector->input_at(node, 0), eax),
                                 g.UseFixed(selector->input_at(node, 1), edx),
                                 shift_operand};

  InstructionOperand outputs[2];
  InstructionOperand temps[1];
  int32_t output_count = 0;
  int32_t temp_count = 0;
  outputs[output_count++] = g.DefineAsFixed(node, eax);
  node_t projection1 = selector->FindProjection(node, 1);
  if (selector->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, edx);
  } else {
    temps[temp_count++] = g.TempRegister(edx);
  }

  selector->Emit(opcode, output_count, outputs, 3, inputs, temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairShl(node_t node) {
  VisitWord32PairShift(this, kIA32ShlPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairShr(node_t node) {
  VisitWord32PairShift(this, kIA32ShrPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32PairSar(node_t node) {
  VisitWord32PairShift(this, kIA32SarPair, node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
  VisitShift(this, node, kIA32Rol);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
  VisitShift(this, node, kIA32Ror);
}

#define RO_OP_T_LIST(V)                                      \
  V(Float32Sqrt, kIA32Float32Sqrt)                           \
  V(Float64Sqrt, kIA32Float64Sqrt)                           \
  V(ChangeInt32ToFloat64, kSSEInt32ToFloat64)                \
  V(TruncateFloat32ToInt32, kIA32Float32ToInt32)             \
  V(TruncateFloat64ToFloat32, kIA32Float64ToFloat32)         \
  V(BitcastFloat32ToInt32, kIA32BitcastFI)                   \
  V(BitcastInt32ToFloat32, kIA32BitcastIF)                   \
  V(Float64ExtractLowWord32, kIA32Float64ExtractLowWord32)   \
  V(Float64ExtractHighWord32, kIA32Float64ExtractHighWord32) \
  V(ChangeFloat64ToInt32, kIA32Float64ToInt32)               \
  V(ChangeFloat32ToFloat64, kIA32Float32ToFloat64)           \
  V(RoundInt32ToFloat32, kSSEInt32ToFloat32)                 \
  V(RoundFloat64ToInt32, kIA32Float64ToInt32)                \
  V(Word32Clz, kIA32Lzcnt)                                   \
  V(Word32Ctz, kIA32Tzcnt)                                   \
  V(Word32Popcnt, kIA32Popcnt)                               \
  V(SignExtendWord8ToInt32, kIA32Movsxbl)                    \
  V(SignExtendWord16ToInt32, kIA32Movsxwl)                   \
  IF_WASM(V, F64x2Sqrt, kIA32F64x2Sqrt)

#define RO_WITH_TEMP_OP_T_LIST(V) V(ChangeUint32ToFloat64, kIA32Uint32ToFloat64)

#define RO_WITH_TEMP_SIMD_OP_T_LIST(V)             \
  V(TruncateFloat64ToUint32, kIA32Float64ToUint32) \
  V(TruncateFloat32ToUint32, kIA32Float32ToUint32) \
  V(ChangeFloat64ToUint32, kIA32Float64ToUint32)

#define RR_OP_T_LIST(V)                                                        \
  V(Float32RoundDown, kIA32Float32Round | MiscField::encode(kRoundDown))       \
  V(Float64RoundDown, kIA32Float64Round | MiscField::encode(kRoundDown))       \
  V(Float32RoundUp, kIA32Float32Round | MiscField::encode(kRoundUp))           \
  V(Float64RoundUp, kIA32Float64Round | MiscField::encode(kRoundUp))           \
  V(Float32RoundTruncate, kIA32Float32Round | MiscField::encode(kRoundToZero)) \
  V(Float64RoundTruncate, kIA32Float64Round | MiscField::encode(kRoundToZero)) \
  V(Float32RoundTiesEven,                                                      \
    kIA32Float32Round | MiscField::encode(kRoundToNearest))                    \
  V(Float64RoundTiesEven,                                                      \
    kIA32Float64Round | MiscField::encode(kRoundToNearest))                    \
  V(TruncateFloat64ToWord32, kArchTruncateDoubleToI)                           \
  IF_WASM(V, F32x4Ceil, kIA32F32x4Round | MiscField::encode(kRoundUp))         \
  IF_WASM(V, F32x4Floor, kIA32F32x4Round | MiscField::encode(kRoundDown))      \
  IF_WASM(V, F32x4Trunc, kIA32F32x4Round | MiscField::encode(kRoundToZero))    \
  IF_WASM(V, F32x4NearestInt,                                                  \
          kIA32F32x4Round | MiscField::encode(kRoundToNearest))                \
  IF_WASM(V, F64x2Ceil, kIA32F64x2Round | MiscField::encode(kRoundUp))         \
  IF_WASM(V, F64x2Floor, kIA32F64x2Round | MiscField::encode(kRoundDown))      \
  IF_WASM(V, F64x2Trunc, kIA32F64x2Round | MiscField::encode(kRoundToZero))    \
  IF_WASM(V, F64x2NearestInt,                                                  \
          kIA32F64x2Round | MiscField::encode(kRoundToNearest))

#define RRO_FLOAT_OP_T_LIST(V)        \
  V(Float32Add, kFloat32Add)          \
  V(Float64Add, kFloat64Add)          \
  V(Float32Sub, kFloat32Sub)          \
  V(Float64Sub, kFloat64Sub)          \
  V(Float32Mul, kFloat32Mul)          \
  V(Float64Mul, kFloat64Mul)          \
  V(Float32Div, kFloat32Div)          \
  V(Float64Div, kFloat64Div)          \
  IF_WASM(V, F64x2Add, kIA32F64x2Add) \
  IF_WASM(V, F64x2Sub, kIA32F64x2Sub) \
  IF_WASM(V, F64x2Mul, kIA32F64x2Mul) \
  IF_WASM(V, F64x2Div, kIA32F64x2Div) \
  IF_WASM(V, F64x2Eq, kIA32F64x2Eq)   \
  IF_WASM(V, F64x2Ne, kIA32F64x2Ne)   \
  IF_WASM(V, F64x2Lt, kIA32F64x2Lt)   \
  IF_WASM(V, F64x2Le, kIA32F64x2Le)

#define FLOAT_UNOP_T_LIST(V)        \
  V(Float32Abs, kFloat32Abs)        \
  V(Float64Abs, kFloat64Abs)        \
  V(Float32Neg, kFloat32Neg)        \
  V(Float64Neg, kFloat64Neg)        \
  IF_WASM(V, F32x4Abs, kFloat32Abs) \
  IF_WASM(V, F32x4Neg, kFloat32Neg) \
  IF_WASM(V, F64x2Abs, kFloat64Abs) \
  IF_WASM(V, F64x2Neg, kFloat64Neg)

#define RO_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRO(this, node, opcode);                                 \
  }
RO_OP_T_LIST(RO_VISITOR)
#undef RO_VISITOR
#undef RO_OP_T_LIST

#define RO_WITH_TEMP_VISITOR(Name, opcode)                       \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitROWithTemp(this, node, opcode);                         \
  }
RO_WITH_TEMP_OP_T_LIST(RO_WITH_TEMP_VISITOR)
#undef RO_WITH_TEMP_VISITOR
#undef RO_WITH_TEMP_OP_T_LIST

#define RO_WITH_TEMP_SIMD_VISITOR(Name, opcode)                  \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitROWithTempSimd(this, node, opcode);                     \
  }
RO_WITH_TEMP_SIMD_OP_T_LIST(RO_WITH_TEMP_SIMD_VISITOR)
#undef RO_WITH_TEMP_SIMD_VISITOR
#undef RO_WITH_TEMP_SIMD_OP_T_LIST

#define RR_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, node, opcode);                                 \
  }
RR_OP_T_LIST(RR_VISITOR)
#undef RR_VISITOR
#undef RR_OP_T_LIST

#define RRO_FLOAT_VISITOR(Name, opcode)                          \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRROFloat(this, node, opcode);                           \
  }
RRO_FLOAT_OP_T_LIST(RRO_FLOAT_VISITOR)
#undef RRO_FLOAT_VISITOR
#undef RRO_FLOAT_OP_T_LIST

#define FLOAT_UNOP_VISITOR(Name, opcode)                         \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    DCHECK_EQ(this->value_input_count(node), 1);                 \
    VisitFloatUnop(this, node, this->input_at(node, 0), opcode); \
  }
FLOAT_UNOP_T_LIST(FLOAT_UNOP_VISITOR)
#undef FLOAT_UNOP_VISITOR
#undef FLOAT_UNOP_T_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBits(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBytes(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kIA32Bswap, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Add(node_t node) {
  IA32OperandGeneratorT<TurboshaftAdapter> g(this);
  const turboshaft::WordBinopOp& add =
      this->Get(node).template Cast<turboshaft::WordBinopOp>();
  turboshaft::OpIndex left = add.left();
  turboshaft::OpIndex right = add.right();

  std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>> m =
      TryMatchBaseWithScaledIndexAndDisplacementForWordBinop(this, left, right);
  if (m.has_value()) {
    if (g.ValueFitsIntoImmediate(m->displacement)) {
      EmitLea(this, node, m->index, m->scale, m->base, m->displacement,
              m->displacement_mode);
      return;
    }
  }
  // No lea pattern, use add.
  VisitBinop(this, node, kIA32Add);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Add(Node* node) {
  IA32OperandGeneratorT<TurbofanAdapter> g(this);

  // Try to match the Add to a lea pattern
  BaseWithIndexAndDisplacement32Matcher m(node);
  if (m.matches() &&
      (m.displacement() == nullptr || g.CanBeImmediate(m.displacement()))) {
    InstructionOperand inputs[4];
    size_t input_count = 0;
    AddressingMode mode = g.GenerateMemoryOperandInputs(
        m.index(), m.scale(), m.base(), m.displacement(), m.displacement_mode(),
        inputs, &input_count);

    DCHECK_NE(0u, input_count);
    DCHECK_GE(arraysize(inputs), input_count);

    InstructionOperand outputs[1];
    outputs[0] = g.DefineAsRegister(node);

    InstructionCode opcode = AddressingModeField::encode(mode) | kIA32Lea;
    Emit(opcode, 1, outputs, input_count, inputs);
    return;
  }

  // No lea pattern match, use add
  VisitBinop(this, node, kIA32Add);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Sub(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    IA32OperandGeneratorT<Adapter> g(this);
    auto binop = this->word_binop_view(node);
    auto left = binop.left();
    auto right = binop.right();
    if (this->MatchIntegralZero(left)) {
      Emit(kIA32Neg, g.DefineSameAsFirst(node), g.Use(right));
    } else {
      VisitBinop(this, node, kIA32Sub);
    }
  } else {
    IA32OperandGeneratorT<Adapter> g(this);
    Int32BinopMatcher m(node);
    if (m.left().Is(0)) {
      Emit(kIA32Neg, g.DefineSameAsFirst(node), g.Use(m.right().node()));
    } else {
      VisitBinop(this, node, kIA32Sub);
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mul(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    if (auto m = TryMatchScaledIndex(this, node, true)) {
      EmitLea(this, node, m->index, m->scale, m->base, 0,
              kPositiveDisplacement);
      return;
    }
  } else {
    Int32ScaleMatcher m(node, true);
    if (m.matches()) {
      Node* index = node->InputAt(0);
      Node* base = m.power_of_two_plus_one() ? index : nullptr;
      EmitLea(this, node, index, m.scale(), base, nullptr,
              kPositiveDisplacement);
      return;
    }
  }
  IA32OperandGeneratorT<Adapter> g(this);
  auto left = this->input_at(node, 0);
  auto right = this->input_at(node, 1);
  if (g.CanBeImmediate(right)) {
    Emit(kIA32Imul, g.DefineAsRegister(node), g.Use(left),
         g.UseImmediate(right));
  } else {
    if (g.CanBeBetterLeftOperand(right)) {
      std::swap(left, right);
    }
    Emit(kIA32Imul, g.DefineSameAsFirst(node), g.UseRegister(left),
         g.Use(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulHigh(node_t node) {
  VisitMulHigh(this, node, kIA32ImulHigh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
  VisitMulHigh(this, node, kIA32UmulHigh);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Div(node_t node) {
  VisitDiv(this, node, kIA32Idiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Div(node_t node) {
  VisitDiv(this, node, kIA32Udiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mod(node_t node) {
  VisitMod(this, node, kIA32Idiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Mod(node_t node) {
  VisitMod(this, node, kIA32Udiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitRoundUint32ToFloat32(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32Uint32ToFloat32, g.DefineAsRegister(node),
       g.Use(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister(eax), g.TempRegister()};
  Emit(kIA32Float64Mod, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Max(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32Float32Max, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), g.Use(this->input_at(node, 1)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Max(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32Float64Max, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), g.Use(this->input_at(node, 1)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Min(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32Float32Min, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), g.Use(this->input_at(node, 1)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Min(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32Float64Min, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), g.Use(this->input_at(node, 1)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesAway(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)))
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
  IA32OperandGeneratorT<Adapter> g(this);

  {  // Temporary scope to minimize indentation change churn below.
    // Prepare for C function call.
    if (call_descriptor->IsCFunctionCall()) {
      InstructionOperand temps[] = {g.TempRegister()};
      size_t const temp_count = arraysize(temps);
      Emit(kArchPrepareCallCFunction | MiscField::encode(static_cast<int>(
                                           call_descriptor->ParameterCount())),
           0, nullptr, 0, nullptr, temp_count, temps);

      // Poke any stack arguments.
      for (size_t n = 0; n < arguments->size(); ++n) {
        PushParameter input = (*arguments)[n];
        if (this->valid(input.node)) {
          int const slot = static_cast<int>(n);
          // TODO(jkummerow): The next line should use `input.node`, but
          // fixing it causes mksnapshot failures. Investigate.
          InstructionOperand value = g.CanBeImmediate(node)
                                         ? g.UseImmediate(input.node)
                                         : g.UseRegister(input.node);
          Emit(kIA32Poke | MiscField::encode(slot), g.NoOutput(), value);
        }
      }
    } else {
      // Push any stack arguments.
      int effect_level = GetEffectLevel(node);
      int stack_decrement = 0;
      for (PushParameter input : base::Reversed(*arguments)) {
        stack_decrement += kSystemPointerSize;
        // Skip holes in the param array. These represent both extra slots for
        // multi-slot values and padding slots for alignment.
        if (!this->valid(input.node)) continue;
        InstructionOperand decrement = g.UseImmediate(stack_decrement);
        stack_decrement = 0;
        if (g.CanBeImmediate(input.node)) {
          Emit(kIA32Push, g.NoOutput(), decrement, g.UseImmediate(input.node));
        } else if (IsSupported(INTEL_ATOM) ||
                   sequence()->IsFP(GetVirtualRegister(input.node))) {
          // TODO(bbudge): IA32Push cannot handle stack->stack double moves
          // because there is no way to encode fixed double slots.
          Emit(kIA32Push, g.NoOutput(), decrement, g.UseRegister(input.node));
        } else if (g.CanBeMemoryOperand(kIA32Push, node, input.node,
                                        effect_level)) {
          InstructionOperand outputs[1];
          InstructionOperand inputs[5];
          size_t input_count = 0;
          inputs[input_count++] = decrement;
          AddressingMode mode = g.GetEffectiveAddressMemoryOperand(
              input.node, inputs, &input_count);
          InstructionCode opcode =
              kIA32Push | AddressingModeField::encode(mode);
          Emit(opcode, 0, outputs, input_count, inputs);
        } else {
          Emit(kIA32Push, g.NoOutput(), decrement, g.UseAny(input.node));
        }
      }
    }  // End of temporary scope.
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
  {  // Temporary scope to minimize indentation change churn below.
    IA32OperandGeneratorT<Adapter> g(this);

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
        Emit(kIA32Peek, g.DefineAsRegister(output.node),
             g.UseImmediate(reverse_slot));
      }
    }
  }  // End of temporary scope.
}

template <typename Adapter>
bool InstructionSelectorT<Adapter>::IsTailCallAddressImmediate() {
  return true;
}

namespace {

template <typename Adapter>
void VisitCompareWithMemoryOperand(InstructionSelectorT<Adapter>* selector,
                                   InstructionCode opcode,
                                   typename Adapter::node_t left,
                                   InstructionOperand right,
                                   FlagsContinuationT<Adapter>* cont) {
  DCHECK(selector->IsLoadOrLoadImmutable(left));
  IA32OperandGeneratorT<Adapter> g(selector);
  size_t input_count = 0;
  InstructionOperand inputs[4];
  AddressingMode addressing_mode =
      g.GetEffectiveAddressMemoryOperand(left, inputs, &input_count);
  opcode |= AddressingModeField::encode(addressing_mode);
  inputs[input_count++] = right;

  selector->EmitWithContinuation(opcode, 0, nullptr, input_count, inputs, cont);
}

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, InstructionOperand left,
                  InstructionOperand right, FlagsContinuationT<Adapter>* cont) {
  selector->EmitWithContinuation(opcode, left, right, cont);
}

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, typename Adapter::node_t left,
                  typename Adapter::node_t right,
                  FlagsContinuationT<Adapter>* cont, bool commutative) {
  IA32OperandGeneratorT<Adapter> g(selector);
  if (commutative && g.CanBeBetterLeftOperand(right)) {
    std::swap(left, right);
  }
  VisitCompare(selector, opcode, g.UseRegister(left), g.Use(right), cont);
}

template <typename Adapter>
MachineType MachineTypeForNarrow(InstructionSelectorT<Adapter>* selector,
                                 typename Adapter::node_t node,
                                 typename Adapter::node_t hint_node) {
  if (selector->IsLoadOrLoadImmutable(hint_node)) {
    MachineType hint = selector->load_view(hint_node).loaded_rep();
    if (selector->is_integer_constant(node)) {
      int64_t constant = selector->integer_constant(node);
      if (hint == MachineType::Int8()) {
        if (constant >= std::numeric_limits<int8_t>::min() &&
            constant <= std::numeric_limits<int8_t>::max()) {
          return hint;
        }
      } else if (hint == MachineType::Uint8()) {
        if (constant >= std::numeric_limits<uint8_t>::min() &&
            constant <= std::numeric_limits<uint8_t>::max()) {
          return hint;
        }
      } else if (hint == MachineType::Int16()) {
        if (constant >= std::numeric_limits<int16_t>::min() &&
            constant <= std::numeric_limits<int16_t>::max()) {
          return hint;
        }
      } else if (hint == MachineType::Uint16()) {
        if (constant >= std::numeric_limits<uint16_t>::min() &&
            constant <= std::numeric_limits<uint16_t>::max()) {
          return hint;
        }
      } else if (hint == MachineType::Int32()) {
        return hint;
      } else if (hint == MachineType::Uint32()) {
        if (constant >= 0) return hint;
      }
    }
  }
  return selector->IsLoadOrLoadImmutable(node)
             ? selector->load_view(node).loaded_rep()
             : MachineType::None();
}

// Tries to match the size of the given opcode to that of the operands, if
// possible.
template <typename Adapter>
InstructionCode TryNarrowOpcodeSize(InstructionSelectorT<Adapter>* selector,
                                    InstructionCode opcode,
                                    typename Adapter::node_t left,
                                    typename Adapter::node_t right,
                                    FlagsContinuationT<Adapter>* cont) {
  // TODO(epertoso): we can probably get some size information out of phi nodes.
  // If the load representations don't match, both operands will be
  // zero/sign-extended to 32bit.
  MachineType left_type = MachineTypeForNarrow(selector, left, right);
  MachineType right_type = MachineTypeForNarrow(selector, right, left);
  if (left_type == right_type) {
    switch (left_type.representation()) {
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8: {
        if (opcode == kIA32Test) return kIA32Test8;
        if (opcode == kIA32Cmp) {
          if (left_type.semantic() == MachineSemantic::kUint32) {
            cont->OverwriteUnsignedIfSigned();
          } else {
            CHECK_EQ(MachineSemantic::kInt32, left_type.semantic());
          }
          return kIA32Cmp8;
        }
        break;
      }
      case MachineRepresentation::kWord16:
        if (opcode == kIA32Test) return kIA32Test16;
        if (opcode == kIA32Cmp) {
          if (left_type.semantic() == MachineSemantic::kUint32) {
            cont->OverwriteUnsignedIfSigned();
          } else {
            CHECK_EQ(MachineSemantic::kInt32, left_type.semantic());
          }
          return kIA32Cmp16;
        }
        break;
      default:
        break;
    }
  }
  return opcode;
}

// Shared routine for multiple float32 compare operations (inputs commuted).
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  VisitCompare(selector, kIA32Float32Cmp, right, left, cont, false);
}

// Shared routine for multiple float64 compare operations (inputs commuted).
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  VisitCompare(selector, kIA32Float64Cmp, right, left, cont, false);
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont) {
  {  // Temporary scope to minimize indentation change churn below.
    IA32OperandGeneratorT<Adapter> g(selector);
    auto left = selector->input_at(node, 0);
    auto right = selector->input_at(node, 1);

    InstructionCode narrowed_opcode =
        TryNarrowOpcodeSize(selector, opcode, left, right, cont);

    int effect_level = selector->GetEffectLevel(node, cont);

    // If one of the two inputs is an immediate, make sure it's on the right, or
    // if one of the two inputs is a memory operand, make sure it's on the left.
    if ((!g.CanBeImmediate(right) && g.CanBeImmediate(left)) ||
        (g.CanBeMemoryOperand(narrowed_opcode, node, right, effect_level) &&
         !g.CanBeMemoryOperand(narrowed_opcode, node, left, effect_level))) {
      if (!selector->IsCommutative(node)) cont->Commute();
      std::swap(left, right);
    }

    // Match immediates on right side of comparison.
    if (g.CanBeImmediate(right)) {
      if (g.CanBeMemoryOperand(narrowed_opcode, node, left, effect_level)) {
        return VisitCompareWithMemoryOperand(selector, narrowed_opcode, left,
                                             g.UseImmediate(right), cont);
      }
      return VisitCompare(selector, opcode, g.Use(left), g.UseImmediate(right),
                          cont);
    }

    // Match memory operands on left side of comparison.
    if (g.CanBeMemoryOperand(narrowed_opcode, node, left, effect_level)) {
      bool needs_byte_register =
          narrowed_opcode == kIA32Test8 || narrowed_opcode == kIA32Cmp8;
      return VisitCompareWithMemoryOperand(
          selector, narrowed_opcode, left,
          needs_byte_register ? g.UseByteRegister(right) : g.UseRegister(right),
          cont);
    }

    return VisitCompare(selector, opcode, left, right, cont,
                        selector->IsCommutative(node));
  }
}

template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node,
                      FlagsContinuationT<Adapter>* cont) {
  VisitWordCompare(selector, node, kIA32Cmp, cont);
}

template <typename Adapter>
void VisitAtomicBinOp(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode,
                      MachineRepresentation rep) {
  using node_t = typename Adapter::node_t;
  AddressingMode addressing_mode;
  IA32OperandGeneratorT<Adapter> g(selector);
  node_t base = selector->input_at(node, 0);
  node_t index = selector->input_at(node, 1);
  node_t value = selector->input_at(node, 2);
  InstructionOperand inputs[] = {
      g.UseUniqueRegister(value), g.UseUniqueRegister(base),
      g.GetEffectiveIndexOperand(index, &addressing_mode)};
  InstructionOperand outputs[] = {g.DefineAsFixed(node, eax)};
  InstructionOperand temp[] = {(rep == MachineRepresentation::kWord8)
                                   ? g.UseByteRegister(node)
                                   : g.TempRegister()};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                 arraysize(temp), temp);
}

template <typename Adapter>
void VisitPairAtomicBinOp(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node, ArchOpcode opcode) {
  using node_t = typename Adapter::node_t;
  IA32OperandGeneratorT<Adapter> g(selector);
  node_t base = selector->input_at(node, 0);
  node_t index = selector->input_at(node, 1);
  node_t value = selector->input_at(node, 2);
  // For Word64 operations, the value input is split into the a high node,
  // and a low node in the int64-lowering phase.
  node_t value_high = selector->input_at(node, 3);

  // Wasm lives in 32-bit address space, so we do not need to worry about
  // base/index lowering. This will need to be fixed for Wasm64.
  AddressingMode addressing_mode;
  InstructionOperand inputs[] = {
      g.UseUniqueRegisterOrSlotOrConstant(value), g.UseFixed(value_high, ecx),
      g.UseUniqueRegister(base),
      g.GetEffectiveIndexOperand(index, &addressing_mode)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  node_t projection0 = selector->FindProjection(node, 0);
  node_t projection1 = selector->FindProjection(node, 1);
  InstructionOperand outputs[2];
  size_t output_count = 0;
  InstructionOperand temps[2];
  size_t temp_count = 0;
  if (selector->valid(projection0)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, eax);
  } else {
    temps[temp_count++] = g.TempRegister(eax);
  }
  if (selector->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, edx);
  } else {
    temps[temp_count++] = g.TempRegister(edx);
  }
  selector->Emit(code, output_count, outputs, arraysize(inputs), inputs,
                 temp_count, temps);
}

}  // namespace

// Shared routine for word comparison with zero.
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
              cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
              return VisitFloat32Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
              return VisitFloat32Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        case RegisterRepresentation::Float64():
          switch (comparison->kind) {
            case ComparisonOp::Kind::kEqual:
              cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThan:
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
              return VisitFloat64Compare(this, value, cont);
            case ComparisonOp::Kind::kSignedLessThanOrEqual:
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
              return VisitFloat64Compare(this, value, cont);
            default:
              UNREACHABLE();
          }
        default:
          break;
      }
    } else if (value_op.Is<Opmask::kWord32Sub>()) {
      return VisitWordCompare(this, value, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseAnd>()) {
      return VisitWordCompare(this, value, kIA32Test, cont);
    } else if (const ProjectionOp* projection =
                   value_op.TryCast<ProjectionOp>()) {
      // Check if this is the overflow output projection of an
      // OverflowCheckedBinop operation.
      if (projection->index == 1u) {
        // We cannot combine the OverflowCheckedBinop operation with this branch
        // unless the 0th projection (the use of the actual value of the
        // operation is either {OpIndex::Invalid()}, which means there's no use
        // of the actual value, or was already defined, which means it is
        // scheduled *AFTER* this branch).
        OpIndex node = projection->input();
        OpIndex result = FindProjection(node, 0);
        if (!result.valid() || IsDefined(result)) {
          if (const OverflowCheckedBinopOp* binop =
                  this->TryCast<OverflowCheckedBinopOp>(node)) {
            DCHECK_EQ(binop->rep, WordRepresentation::Word32());
            cont->OverwriteAndNegateIfEqual(kOverflow);
            switch (binop->kind) {
              case OverflowCheckedBinopOp::Kind::kSignedAdd:
                return VisitBinop(this, node, kIA32Add, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                return VisitBinop(this, node, kIA32Sub, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                return VisitBinop(this, node, kIA32Imul, cont);
            }
            UNREACHABLE();
          }
        }
      }
    } else if (value_op.Is<StackPointerGreaterThanOp>()) {
      cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
      return VisitStackPointerGreaterThan(value, cont);
    }
  }

  // Branch could not be combined with a compare, emit compare against 0.
  IA32OperandGeneratorT<TurboshaftAdapter> g(this);
  VisitCompare(this, kIA32Cmp, g.Use(value), g.TempImmediate(0), cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  if constexpr (Adapter::IsTurboshaft) {
    UNREACHABLE();  // Template-specialized above.
  } else {
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
          cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat32LessThan:
          cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat32LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
          return VisitFloat32Compare(this, value, cont);
        case IrOpcode::kFloat64Equal:
          cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
          return VisitFloat64Compare(this, value, cont);
        case IrOpcode::kFloat64LessThan:
          cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
          return VisitFloat64Compare(this, value, cont);
        case IrOpcode::kFloat64LessThanOrEqual:
          cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
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
                  return VisitBinop(this, node, kIA32Add, cont);
                case IrOpcode::kInt32SubWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop(this, node, kIA32Sub, cont);
                case IrOpcode::kInt32MulWithOverflow:
                  cont->OverwriteAndNegateIfEqual(kOverflow);
                  return VisitBinop(this, node, kIA32Imul, cont);
                default:
                  break;
              }
            }
          }
          break;
        case IrOpcode::kInt32Sub:
          return VisitWordCompare(this, value, cont);
        case IrOpcode::kWord32And:
          return VisitWordCompare(this, value, kIA32Test, cont);
        case IrOpcode::kStackPointerGreaterThan:
          cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
          return VisitStackPointerGreaterThan(value, cont);
        default:
          break;
      }
    }

    // Continuation could not be combined with a compare, emit compare against
    // 0.
    IA32OperandGeneratorT<Adapter> g(this);
    VisitCompare(this, kIA32Cmp, g.Use(value), g.TempImmediate(0), cont);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
  {  // Temporary scope to minimize indentation change churn below.
    IA32OperandGeneratorT<Adapter> g(this);
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
          Emit(kIA32Lea | AddressingModeField::encode(kMode_MRI), index_operand,
               value_operand, g.TempImmediate(-sw.min_value()));
        }
        // Generate a table lookup.
        return EmitTableSwitch(sw, index_operand);
      }
    }

    // Generate a tree of conditional jumps.
    return EmitBinarySearchSwitch(sw, value_operand);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::ComparisonOp& comparison =
        this->Get(node).template Cast<turboshaft::ComparisonOp>();
    if (this->MatchIntegralZero(comparison.right())) {
      return VisitWordCompareZero(node, comparison.left(), &cont);
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
    return VisitBinop(this, node, kIA32Add, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kIA32Add, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kIA32Sub, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kIA32Sub, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (this->valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kIA32Imul, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kIA32Imul, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnorderedEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThan(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedGreaterThan, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedGreaterThanOrEqual, node);
  VisitFloat32Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnorderedEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThan(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedGreaterThan, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedGreaterThanOrEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertLowWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // Turboshaft uses {BitcastWord32PairToFloat64}.
    UNREACHABLE();
  } else {
    IA32OperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    Float64Matcher mleft(left);
    if (mleft.HasResolvedValue() &&
        (base::bit_cast<uint64_t>(mleft.ResolvedValue()) >> 32) == 0u) {
      Emit(kIA32Float64LoadLowWord32, g.DefineAsRegister(node), g.Use(right));
      return;
    }
    Emit(kIA32Float64InsertLowWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.Use(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64InsertHighWord32(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // Turboshaft uses {BitcastWord32PairToFloat64}.
    UNREACHABLE();
  } else {
    IA32OperandGeneratorT<Adapter> g(this);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    Emit(kIA32Float64InsertHighWord32, g.DefineSameAsFirst(node),
         g.UseRegister(left), g.Use(right));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  if constexpr (Adapter::IsTurbofan) {
    // Turbofan uses {Float64Insert{High,Low}Word32}.
    UNREACHABLE();
  } else {
    IA32OperandGeneratorT<Adapter> g(this);
    const turboshaft::BitcastWord32PairToFloat64Op& cast_op =
        this->Get(node)
            .template Cast<turboshaft::BitcastWord32PairToFloat64Op>();
    Emit(kIA32Float64FromWord32Pair, g.DefineAsRegister(node),
         g.Use(cast_op.low_word32()), g.Use(cast_op.high_word32()));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64SilenceNaN(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(kIA32Float64SilenceNaN, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  // ia32 is no weaker than release-acquire and only needs to emit an
  // instruction for SeqCst memory barriers.
  AtomicMemoryOrder order = AtomicOrder(this, node);
  if (order == AtomicMemoryOrder::kSeqCst) {
    IA32OperandGeneratorT<Adapter> g(this);
    Emit(kIA32MFence, g.NoOutput());
    return;
  }
  DCHECK_EQ(AtomicMemoryOrder::kAcqRel, order);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
  LoadRepresentation load_rep = this->load_view(node).loaded_rep();
  DCHECK(load_rep.representation() == MachineRepresentation::kWord8 ||
         load_rep.representation() == MachineRepresentation::kWord16 ||
         load_rep.representation() == MachineRepresentation::kWord32 ||
         load_rep.representation() == MachineRepresentation::kTaggedSigned ||
         load_rep.representation() == MachineRepresentation::kTaggedPointer ||
         load_rep.representation() == MachineRepresentation::kTagged);
  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit MOV.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  VisitLoad(node, node, GetLoadOpcode(load_rep));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
  VisitStoreCommon(this, this->store_view(node));
}

MachineType AtomicOpType(InstructionSelectorT<TurboshaftAdapter>* selector,
                         turboshaft::OpIndex node) {
  const turboshaft::AtomicRMWOp& atomic_op =
      selector->Get(node).template Cast<turboshaft::AtomicRMWOp>();
  return atomic_op.memory_rep.ToMachineType();
}

MachineType AtomicOpType(InstructionSelectorT<TurbofanAdapter>* selector,
                         Node* node) {
  return AtomicOpType(node->op());
}

AtomicMemoryOrder AtomicOrder(InstructionSelectorT<TurboshaftAdapter>* selector,
                              turboshaft::OpIndex node) {
  const turboshaft::Operation& op = selector->Get(node);
  if (op.Is<turboshaft::AtomicWord32PairOp>()) {
    // TODO(nicohartmann): Turboshaft doesn't support configurable memory
    // orders yet; see also {TurboshaftAdapter::StoreView}.
    return AtomicMemoryOrder::kSeqCst;
  }
  if (const turboshaft::MemoryBarrierOp* barrier =
          op.TryCast<turboshaft::MemoryBarrierOp>()) {
    return barrier->memory_order;
  }
  UNREACHABLE();
}

AtomicMemoryOrder AtomicOrder(InstructionSelectorT<TurbofanAdapter>* selector,
                              Node* node) {
  return OpParameter<AtomicMemoryOrder>(node->op());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicExchange(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  MachineType type = AtomicOpType(this, node);
  ArchOpcode opcode;
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
  VisitAtomicExchange(this, node, opcode, type.representation());
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicCompareExchange(
    node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  auto atomic_op = this->atomic_rmw_view(node);
  node_t base = atomic_op.base();
  node_t index = atomic_op.index();
  node_t old_value = atomic_op.expected();
  node_t new_value = atomic_op.value();

  MachineType type = AtomicOpType(this, node);
  ArchOpcode opcode;
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
  AddressingMode addressing_mode;
  InstructionOperand new_val_operand =
      (type.representation() == MachineRepresentation::kWord8)
          ? g.UseByteRegister(new_value)
          : g.UseUniqueRegister(new_value);
  InstructionOperand inputs[] = {
      g.UseFixed(old_value, eax), new_val_operand, g.UseUniqueRegister(base),
      g.GetEffectiveIndexOperand(index, &addressing_mode)};
  InstructionOperand outputs[] = {g.DefineAsFixed(node, eax)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode);
  Emit(code, 1, outputs, arraysize(inputs), inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicBinaryOperation(
    node_t node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  {  // Temporary scope to minimize indentation change churn below.
    MachineType type = AtomicOpType(this, node);
    ArchOpcode opcode;
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
    VisitAtomicBinOp(this, node, opcode, type.representation());
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
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairLoad(node_t node) {
  // Both acquire and sequentially consistent loads can emit MOV.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  IA32OperandGeneratorT<Adapter> g(this);
  AddressingMode mode;
  node_t base = this->input_at(node, 0);
  node_t index = this->input_at(node, 1);
  node_t projection0 = FindProjection(node, 0);
  node_t projection1 = FindProjection(node, 1);
  if (this->valid(projection0) && this->valid(projection1)) {
    InstructionOperand inputs[] = {g.UseUniqueRegister(base),
                                   g.GetEffectiveIndexOperand(index, &mode)};
    InstructionCode code =
        kIA32Word32AtomicPairLoad | AddressingModeField::encode(mode);
    InstructionOperand outputs[] = {g.DefineAsRegister(projection0),
                                    g.DefineAsRegister(projection1)};
    Emit(code, 2, outputs, 2, inputs);
  } else if (this->valid(projection0) || this->valid(projection1)) {
    // Only one word is needed, so it's enough to load just that.
    ArchOpcode opcode = kIA32Movl;

    InstructionOperand outputs[] = {g.DefineAsRegister(
        this->valid(projection0) ? projection0 : projection1)};
    InstructionOperand inputs[3];
    size_t input_count = 0;
    // TODO(ahaas): Introduce an enum for {scale} instead of an integer.
    // {scale = 0} means *1 in the generated code.
    int scale = 0;
    AddressingMode mode = g.GenerateMemoryOperandInputs(
        index, scale, base, this->valid(projection0) ? 0 : 4,
        kPositiveDisplacement, inputs, &input_count);
    InstructionCode code = opcode | AddressingModeField::encode(mode);
    Emit(code, 1, outputs, input_count, inputs);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairStore(node_t node) {
  // Release pair stores emit a MOVQ via a double register, and sequentially
  // consistent stores emit CMPXCHG8B.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html

  IA32OperandGeneratorT<Adapter> g(this);
  node_t base = this->input_at(node, 0);
  node_t index = this->input_at(node, 1);
  node_t value = this->input_at(node, 2);
  node_t value_high = this->input_at(node, 3);

  AtomicMemoryOrder order = AtomicOrder(this, node);
  if (order == AtomicMemoryOrder::kAcqRel) {
    AddressingMode addressing_mode;
    InstructionOperand inputs[] = {
        g.UseUniqueRegisterOrSlotOrConstant(value),
        g.UseUniqueRegisterOrSlotOrConstant(value_high),
        g.UseUniqueRegister(base),
        g.GetEffectiveIndexOperand(index, &addressing_mode),
    };
    InstructionCode code = kIA32Word32ReleasePairStore |
                           AddressingModeField::encode(addressing_mode);
    Emit(code, 0, nullptr, arraysize(inputs), inputs);
  } else {
    DCHECK_EQ(order, AtomicMemoryOrder::kSeqCst);

    AddressingMode addressing_mode;
    InstructionOperand inputs[] = {
        g.UseUniqueRegisterOrSlotOrConstant(value), g.UseFixed(value_high, ecx),
        g.UseUniqueRegister(base),
        g.GetEffectiveIndexOperand(index, &addressing_mode)};
    // Allocating temp registers here as stores are performed using an atomic
    // exchange, the output of which is stored in edx:eax, which should be saved
    // and restored at the end of the instruction.
    InstructionOperand temps[] = {g.TempRegister(eax), g.TempRegister(edx)};
    const int num_temps = arraysize(temps);
    InstructionCode code = kIA32Word32SeqCstPairStore |
                           AddressingModeField::encode(addressing_mode);
    Emit(code, 0, nullptr, arraysize(inputs), inputs, num_temps, temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAdd(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairAdd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairSub(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairSub);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairAnd(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairAnd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairOr(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairOr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairXor(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairXor);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairExchange(node_t node) {
  VisitPairAtomicBinOp(this, node, kIA32Word32AtomicPairExchange);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicPairCompareExchange(
    node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  node_t index = this->input_at(node, 1);
  AddressingMode addressing_mode;

  // In the Turbofan and the Turboshaft graph the order of expected and value is
  // swapped.
  const size_t expected_offset = Adapter::IsTurboshaft ? 4 : 2;
  const size_t value_offset = Adapter::IsTurboshaft ? 2 : 4;
  InstructionOperand inputs[] = {
      // High, Low values of old value
      g.UseFixed(this->input_at(node, expected_offset), eax),
      g.UseFixed(this->input_at(node, expected_offset + 1), edx),
      // High, Low values of new value
      g.UseUniqueRegisterOrSlotOrConstant(this->input_at(node, value_offset)),
      g.UseFixed(this->input_at(node, value_offset + 1), ecx),
      // InputAt(0) => base
      g.UseUniqueRegister(this->input_at(node, 0)),
      g.GetEffectiveIndexOperand(index, &addressing_mode)};
  node_t projection0 = FindProjection(node, 0);
  node_t projection1 = FindProjection(node, 1);
  InstructionCode code = kIA32Word32AtomicPairCompareExchange |
                         AddressingModeField::encode(addressing_mode);

  InstructionOperand outputs[2];
  size_t output_count = 0;
  InstructionOperand temps[2];
  size_t temp_count = 0;
  if (this->valid(projection0)) {
    outputs[output_count++] = g.DefineAsFixed(projection0, eax);
  } else {
    temps[temp_count++] = g.TempRegister(eax);
  }
  if (this->valid(projection1)) {
    outputs[output_count++] = g.DefineAsFixed(projection1, edx);
  } else {
    temps[temp_count++] = g.TempRegister(edx);
  }
  Emit(code, output_count, outputs, arraysize(inputs), inputs, temp_count,
       temps);
}

#define SIMD_INT_TYPES(V) \
  V(I32x4)                \
  V(I16x8)                \
  V(I8x16)

#define SIMD_BINOP_LIST(V) \
  V(I32x4GtU)              \
  V(I32x4GeU)              \
  V(I16x8Ne)               \
  V(I16x8GeS)              \
  V(I16x8GtU)              \
  V(I16x8GeU)              \
  V(I8x16Ne)               \
  V(I8x16GeS)              \
  V(I8x16GtU)              \
  V(I8x16GeU)

#define SIMD_BINOP_UNIFIED_SSE_AVX_LIST(V) \
  V(F32x4Add)                              \
  V(F32x4Sub)                              \
  V(F32x4Mul)                              \
  V(F32x4Div)                              \
  V(F32x4Eq)                               \
  V(F32x4Ne)                               \
  V(F32x4Lt)                               \
  V(F32x4Le)                               \
  V(F32x4Min)                              \
  V(F32x4Max)                              \
  V(I64x2Add)                              \
  V(I64x2Sub)                              \
  V(I64x2Eq)                               \
  V(I64x2Ne)                               \
  V(I32x4Add)                              \
  V(I32x4Sub)                              \
  V(I32x4Mul)                              \
  V(I32x4MinS)                             \
  V(I32x4MaxS)                             \
  V(I32x4Eq)                               \
  V(I32x4Ne)                               \
  V(I32x4GtS)                              \
  V(I32x4GeS)                              \
  V(I32x4MinU)                             \
  V(I32x4MaxU)                             \
  V(I32x4DotI16x8S)                        \
  V(I16x8Add)                              \
  V(I16x8AddSatS)                          \
  V(I16x8Sub)                              \
  V(I16x8SubSatS)                          \
  V(I16x8Mul)                              \
  V(I16x8Eq)                               \
  V(I16x8GtS)                              \
  V(I16x8MinS)                             \
  V(I16x8MaxS)                             \
  V(I16x8AddSatU)                          \
  V(I16x8SubSatU)                          \
  V(I16x8MinU)                             \
  V(I16x8MaxU)                             \
  V(I16x8SConvertI32x4)                    \
  V(I16x8UConvertI32x4)                    \
  V(I16x8RoundingAverageU)                 \
  V(I8x16Add)                              \
  V(I8x16AddSatS)                          \
  V(I8x16Sub)                              \
  V(I8x16SubSatS)                          \
  V(I8x16MinS)                             \
  V(I8x16MaxS)                             \
  V(I8x16Eq)                               \
  V(I8x16GtS)                              \
  V(I8x16AddSatU)                          \
  V(I8x16SubSatU)                          \
  V(I8x16MinU)                             \
  V(I8x16MaxU)                             \
  V(I8x16SConvertI16x8)                    \
  V(I8x16UConvertI16x8)                    \
  V(I8x16RoundingAverageU)                 \
  V(S128And)                               \
  V(S128Or)                                \
  V(S128Xor)

// These opcodes require all inputs to be registers because the codegen is
// simpler with all registers.
#define SIMD_BINOP_RRR(V)  \
  V(I64x2ExtMulLowI32x4S)  \
  V(I64x2ExtMulHighI32x4S) \
  V(I64x2ExtMulLowI32x4U)  \
  V(I64x2ExtMulHighI32x4U) \
  V(I32x4ExtMulLowI16x8S)  \
  V(I32x4ExtMulHighI16x8S) \
  V(I32x4ExtMulLowI16x8U)  \
  V(I32x4ExtMulHighI16x8U) \
  V(I16x8ExtMulLowI8x16S)  \
  V(I16x8ExtMulHighI8x16S) \
  V(I16x8ExtMulLowI8x16U)  \
  V(I16x8ExtMulHighI8x16U) \
  V(I16x8Q15MulRSatS)      \
  V(I16x8RelaxedQ15MulRS)

#define SIMD_UNOP_LIST(V)   \
  V(F64x2ConvertLowI32x4S)  \
  V(F32x4DemoteF64x2Zero)   \
  V(F32x4Sqrt)              \
  V(F32x4SConvertI32x4)     \
  V(I64x2BitMask)           \
  V(I64x2SConvertI32x4Low)  \
  V(I64x2SConvertI32x4High) \
  V(I64x2UConvertI32x4Low)  \
  V(I64x2UConvertI32x4High) \
  V(I32x4SConvertI16x8Low)  \
  V(I32x4SConvertI16x8High) \
  V(I32x4Neg)               \
  V(I32x4UConvertI16x8Low)  \
  V(I32x4UConvertI16x8High) \
  V(I32x4Abs)               \
  V(I32x4BitMask)           \
  V(I16x8SConvertI8x16Low)  \
  V(I16x8SConvertI8x16High) \
  V(I16x8Neg)               \
  V(I16x8UConvertI8x16Low)  \
  V(I16x8UConvertI8x16High) \
  V(I16x8Abs)               \
  V(I8x16Neg)               \
  V(I8x16Abs)               \
  V(I8x16BitMask)           \
  V(S128Not)

#define SIMD_ALLTRUE_LIST(V) \
  V(I64x2AllTrue)            \
  V(I32x4AllTrue)            \
  V(I16x8AllTrue)            \
  V(I8x16AllTrue)

#define SIMD_SHIFT_OPCODES_UNIFED_SSE_AVX(V) \
  V(I64x2Shl)                                \
  V(I64x2ShrU)                               \
  V(I32x4Shl)                                \
  V(I32x4ShrS)                               \
  V(I32x4ShrU)                               \
  V(I16x8Shl)                                \
  V(I16x8ShrS)                               \
  V(I16x8ShrU)

#if V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  static const int kUint32Immediates = kSimd128Size / sizeof(uint32_t);
  uint32_t val[kUint32Immediates];
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ConstantOp& constant =
        this->Get(node).template Cast<turboshaft::Simd128ConstantOp>();
    memcpy(val, constant.value, kSimd128Size);
  } else {
    memcpy(val, S128ImmediateParameterOf(node->op()).data(), kSimd128Size);
  }
  // If all bytes are zeros or ones, avoid emitting code for generic constants
  bool all_zeros = !(val[0] || val[1] || val[2] || val[3]);
  bool all_ones = val[0] == UINT32_MAX && val[1] == UINT32_MAX &&
                  val[2] == UINT32_MAX && val[3] == UINT32_MAX;
  InstructionOperand dst = g.DefineAsRegister(node);
  if (all_zeros) {
    Emit(kIA32S128Zero, dst);
  } else if (all_ones) {
    Emit(kIA32S128AllOnes, dst);
  } else {
    InstructionOperand inputs[kUint32Immediates];
    for (int i = 0; i < kUint32Immediates; ++i) {
      inputs[i] = g.UseImmediate(val[i]);
    }
    InstructionOperand temp(g.TempRegister());
    Emit(kIA32S128Const, 1, &dst, kUint32Immediates, inputs, 1, &temp);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Min(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand operand0 = g.UseRegister(this->input_at(node, 0));
  InstructionOperand operand1 = g.UseRegister(this->input_at(node, 1));

  if (IsSupported(AVX)) {
    Emit(kIA32F64x2Min, g.DefineAsRegister(node), operand0, operand1);
  } else {
    Emit(kIA32F64x2Min, g.DefineSameAsFirst(node), operand0, operand1);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Max(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand operand0 = g.UseRegister(this->input_at(node, 0));
  InstructionOperand operand1 = g.UseRegister(this->input_at(node, 1));
  if (IsSupported(AVX)) {
    Emit(kIA32F64x2Max, g.DefineAsRegister(node), operand0, operand1);
  } else {
    Emit(kIA32F64x2Max, g.DefineSameAsFirst(node), operand0, operand1);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Splat(node_t node) {
  VisitRRSimd(this, node, kIA32F64x2Splat);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ExtractLane(node_t node) {
  VisitRRISimd(this, node, kIA32F64x2ExtractLane, kIA32F64x2ExtractLane);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI64x2SplatI32Pair(
    node_t node) {
  // In turboshaft it gets lowered to an I32x4Splat.
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI64x2SplatI32Pair(Node* node) {
  IA32OperandGeneratorT<TurbofanAdapter> g(this);
  Int32Matcher match_left(node->InputAt(0));
  Int32Matcher match_right(node->InputAt(1));
  if (match_left.Is(0) && match_right.Is(0)) {
    Emit(kIA32S128Zero, g.DefineAsRegister(node));
  } else {
    InstructionOperand operand0 = g.UseRegister(node->InputAt(0));
    InstructionOperand operand1 = g.Use(node->InputAt(1));
    Emit(kIA32I64x2SplatI32Pair, g.DefineAsRegister(node), operand0, operand1);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI64x2ReplaceLaneI32Pair(
    node_t node) {
  // In turboshaft it gets lowered to an I32x4ReplaceLane.
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI64x2ReplaceLaneI32Pair(
    Node* node) {
  IA32OperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand operand = g.UseRegister(node->InputAt(0));
  InstructionOperand lane = g.UseImmediate(OpParameter<int32_t>(node->op()));
  InstructionOperand low = g.Use(node->InputAt(1));
  InstructionOperand high = g.Use(node->InputAt(2));
  Emit(kIA32I64x2ReplaceLaneI32Pair, g.DefineSameAsFirst(node), operand, lane,
       low, high);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Neg(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  // If AVX unsupported, make sure dst != src to avoid a move.
  InstructionOperand operand0 = IsSupported(AVX)
                                    ? g.UseRegister(this->input_at(node, 0))
                                    : g.UseUnique(this->input_at(node, 0));
  Emit(kIA32I64x2Neg, g.DefineAsRegister(node), operand0);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ShrS(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);

  if (g.CanBeImmediate(this->input_at(node, 1))) {
    Emit(kIA32I64x2ShrS, dst, g.UseRegister(this->input_at(node, 0)),
         g.UseImmediate(this->input_at(node, 1)));
  } else {
    InstructionOperand temps[] = {g.TempSimd128Register(), g.TempRegister()};
    Emit(kIA32I64x2ShrS, dst, g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)), arraysize(temps), temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Mul(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register(),
                                g.TempSimd128Register()};
  Emit(kIA32I64x2Mul, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseUniqueRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Splat(node_t node) {
  VisitRRSimd(this, node, kIA32F32x4Splat);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4ExtractLane(node_t node) {
  VisitRRISimd(this, node, kIA32F32x4ExtractLane);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4UConvertI32x4(node_t node) {
  VisitRRSimd(this, node, kIA32F32x4UConvertI32x4);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertF32x4(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kIA32I32x4SConvertF32x4, dst, g.UseRegister(this->input_at(node, 0)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertF32x4(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register(),
                                g.TempSimd128Register()};
  InstructionCode opcode =
      IsSupported(AVX) ? kAVXI32x4UConvertF32x4 : kSSEI32x4UConvertF32x4;
  Emit(opcode, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(kIA32S128Zero, g.DefineAsRegister(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kIA32S128Select, dst, g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 2)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128AndNot(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  // andnps a b does ~a & b, but we want a & !b, so flip the input.
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kIA32S128AndNot, dst, g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 0)));
}

#define VISIT_SIMD_SPLAT(Type)                                          \
  template <typename Adapter>                                           \
  void InstructionSelectorT<Adapter>::Visit##Type##Splat(node_t node) { \
    bool set_zero;                                                      \
    if constexpr (Adapter::IsTurboshaft) {                              \
      set_zero = this->MatchIntegralZero(this->input_at(node, 0));      \
    } else {                                                            \
      set_zero = Int32Matcher(node->InputAt(0)).Is(0);                  \
    }                                                                   \
    if (set_zero) {                                                     \
      IA32OperandGeneratorT<Adapter> g(this);                           \
      Emit(kIA32S128Zero, g.DefineAsRegister(node));                    \
    } else {                                                            \
      VisitRO(this, node, kIA32##Type##Splat);                          \
    }                                                                   \
  }
SIMD_INT_TYPES(VISIT_SIMD_SPLAT)
#undef SIMD_INT_TYPES
#undef VISIT_SIMD_SPLAT

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Splat(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ExtractLaneU(node_t node) {
  VisitRRISimd(this, node, kIA32Pextrb);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ExtractLaneS(node_t node) {
  VisitRRISimd(this, node, kIA32I8x16ExtractLaneS);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtractLaneU(node_t node) {
  VisitRRISimd(this, node, kIA32Pextrw);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtractLaneS(node_t node) {
  VisitRRISimd(this, node, kIA32I16x8ExtractLaneS);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtractLane(node_t node) {
  VisitRRISimd(this, node, kIA32I32x4ExtractLane);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8ExtractLane(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8ReplaceLane(node_t node) {
  UNIMPLEMENTED();
}

#define SIMD_REPLACE_LANE_TYPE_OP(V) \
  V(I32x4, kIA32Pinsrd)              \
  V(I16x8, kIA32Pinsrw)              \
  V(I8x16, kIA32Pinsrb)              \
  V(F32x4, kIA32Insertps)            \
  V(F64x2, kIA32F64x2ReplaceLane)

#define VISIT_SIMD_REPLACE_LANE(TYPE, OPCODE)                                 \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##TYPE##ReplaceLane(node_t node) { \
    IA32OperandGeneratorT<Adapter> g(this);                                   \
    int lane;                                                                 \
    if constexpr (Adapter::IsTurboshaft) {                                    \
      const turboshaft::Simd128ReplaceLaneOp& op =                            \
          this->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();  \
      lane = op.lane;                                                         \
    } else {                                                                  \
      lane = OpParameter<int32_t>(node->op());                                \
    }                                                                         \
    InstructionOperand operand0 = g.UseRegister(this->input_at(node, 0));     \
    InstructionOperand operand1 = g.UseImmediate(lane);                       \
    auto input1 = this->input_at(node, 1);                                    \
    InstructionOperand operand2;                                              \
    if constexpr (OPCODE == kIA32F64x2ReplaceLane) {                          \
      operand2 = g.UseRegister(input1);                                       \
    } else {                                                                  \
      operand2 = g.Use(input1);                                               \
    }                                                                         \
    /* When no-AVX, define dst == src to save a move. */                      \
    InstructionOperand dst = IsSupported(AVX) ? g.DefineAsRegister(node)      \
                                              : g.DefineSameAsFirst(node);    \
    Emit(OPCODE, dst, operand0, operand1, operand2);                          \
  }
SIMD_REPLACE_LANE_TYPE_OP(VISIT_SIMD_REPLACE_LANE)
#undef VISIT_SIMD_REPLACE_LANE
#undef SIMD_REPLACE_LANE_TYPE_OP

#define VISIT_SIMD_SHIFT_UNIFIED_SSE_AVX(Opcode)                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    VisitRROSimdShift(this, node, kIA32##Opcode);                  \
  }
SIMD_SHIFT_OPCODES_UNIFED_SSE_AVX(VISIT_SIMD_SHIFT_UNIFIED_SSE_AVX)
#undef VISIT_SIMD_SHIFT_UNIFIED_SSE_AVX
#undef SIMD_SHIFT_OPCODES_UNIFED_SSE_AVX

// TODO(v8:9198): SSE requires operand0 to be a register as we don't have memory
// alignment yet. For AVX, memory operands are fine, but can have performance
// issues if not aligned to 16/32 bytes (based on load size), see SDM Vol 1,
// chapter 14.9
#define VISIT_SIMD_UNOP(Opcode)                                    \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    IA32OperandGeneratorT<Adapter> g(this);                        \
    Emit(kIA32##Opcode, g.DefineAsRegister(node),                  \
         g.UseRegister(this->input_at(node, 0)));                  \
  }
SIMD_UNOP_LIST(VISIT_SIMD_UNOP)
#undef VISIT_SIMD_UNOP
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

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitV128AnyTrue(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(kIA32S128AnyTrue, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

#define VISIT_SIMD_ALLTRUE(Opcode)                                            \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) {            \
    IA32OperandGeneratorT<Adapter> g(this);                                   \
    InstructionOperand temps[] = {g.TempRegister(), g.TempSimd128Register()}; \
    Emit(kIA32##Opcode, g.DefineAsRegister(node),                             \
         g.UseUniqueRegister(this->input_at(node, 0)), arraysize(temps),      \
         temps);                                                              \
  }
SIMD_ALLTRUE_LIST(VISIT_SIMD_ALLTRUE)
#undef VISIT_SIMD_ALLTRUE
#undef SIMD_ALLTRUE_LIST

#define VISIT_SIMD_BINOP(Opcode)                                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    VisitRROSimd(this, node, kAVX##Opcode, kSSE##Opcode);          \
  }
SIMD_BINOP_LIST(VISIT_SIMD_BINOP)
#undef VISIT_SIMD_BINOP
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

#define VISIT_SIMD_BINOP_UNIFIED_SSE_AVX(Opcode)                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    VisitRROSimd(this, node, kIA32##Opcode, kIA32##Opcode);        \
  }
SIMD_BINOP_UNIFIED_SSE_AVX_LIST(VISIT_SIMD_BINOP_UNIFIED_SSE_AVX)
#undef VISIT_SIMD_BINOP_UNIFIED_SSE_AVX
#undef SIMD_BINOP_UNIFIED_SSE_AVX_LIST

#define VISIT_SIMD_BINOP_RRR(OPCODE)                               \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##OPCODE(node_t node) { \
    VisitRRRSimd(this, node, kIA32##OPCODE);                       \
  }
SIMD_BINOP_RRR(VISIT_SIMD_BINOP_RRR)
#undef VISIT_SIMD_BINOP_RRR
#undef SIMD_BINOP_RRR

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8BitMask(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(kIA32I16x8BitMask, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shl(node_t node) {
  VisitI8x16Shift(this, node, kIA32I8x16Shl);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ShrS(node_t node) {
  VisitI8x16Shift(this, node, kIA32I8x16ShrS);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ShrU(node_t node) {
  VisitI8x16Shift(this, node, kIA32I8x16ShrU);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AbsWithOverflow(node_t node) {
  UNREACHABLE();
}

#if V8_ENABLE_WEBASSEMBLY
namespace {

// Returns true if shuffle can be decomposed into two 16x4 half shuffles
// followed by a 16x8 blend.
// E.g. [3 2 1 0 15 14 13 12].
bool TryMatch16x8HalfShuffle(uint8_t* shuffle16x8, uint8_t* blend_mask) {
  *blend_mask = 0;
  for (int i = 0; i < 8; i++) {
    if ((shuffle16x8[i] & 0x4) != (i & 0x4)) return false;
    *blend_mask |= (shuffle16x8[i] > 7 ? 1 : 0) << i;
  }
  return true;
}

struct ShuffleEntry {
  uint8_t shuffle[kSimd128Size];
  ArchOpcode opcode;
  ArchOpcode avx_opcode;
  bool src0_needs_reg;
  bool src1_needs_reg;
};

// Shuffles that map to architecture-specific instruction sequences. These are
// matched very early, so we shouldn't include shuffles that match better in
// later tests, like 32x4 and 16x8 shuffles. In general, these patterns should
// map to either a single instruction, or be finer grained, such as zip/unzip or
// transpose patterns.
static const ShuffleEntry arch_shuffles[] = {
    {{0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23},
     kIA32S64x2UnpackLow,
     kIA32S64x2UnpackLow,
     true,
     false},
    {{8, 9, 10, 11, 12, 13, 14, 15, 24, 25, 26, 27, 28, 29, 30, 31},
     kIA32S64x2UnpackHigh,
     kIA32S64x2UnpackHigh,
     true,
     false},
    {{0, 1, 2, 3, 16, 17, 18, 19, 4, 5, 6, 7, 20, 21, 22, 23},
     kIA32S32x4UnpackLow,
     kIA32S32x4UnpackLow,
     true,
     false},
    {{8, 9, 10, 11, 24, 25, 26, 27, 12, 13, 14, 15, 28, 29, 30, 31},
     kIA32S32x4UnpackHigh,
     kIA32S32x4UnpackHigh,
     true,
     false},
    {{0, 1, 16, 17, 2, 3, 18, 19, 4, 5, 20, 21, 6, 7, 22, 23},
     kIA32S16x8UnpackLow,
     kIA32S16x8UnpackLow,
     true,
     false},
    {{8, 9, 24, 25, 10, 11, 26, 27, 12, 13, 28, 29, 14, 15, 30, 31},
     kIA32S16x8UnpackHigh,
     kIA32S16x8UnpackHigh,
     true,
     false},
    {{0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23},
     kIA32S8x16UnpackLow,
     kIA32S8x16UnpackLow,
     true,
     false},
    {{8, 24, 9, 25, 10, 26, 11, 27, 12, 28, 13, 29, 14, 30, 15, 31},
     kIA32S8x16UnpackHigh,
     kIA32S8x16UnpackHigh,
     true,
     false},

    {{0, 1, 4, 5, 8, 9, 12, 13, 16, 17, 20, 21, 24, 25, 28, 29},
     kSSES16x8UnzipLow,
     kAVXS16x8UnzipLow,
     true,
     false},
    {{2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31},
     kSSES16x8UnzipHigh,
     kAVXS16x8UnzipHigh,
     true,
     true},
    {{0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30},
     kSSES8x16UnzipLow,
     kAVXS8x16UnzipLow,
     true,
     true},
    {{1, 3, 5, 7, 9, 11, 13, 15, 17, 19, 21, 23, 25, 27, 29, 31},
     kSSES8x16UnzipHigh,
     kAVXS8x16UnzipHigh,
     true,
     true},

    {{0, 16, 2, 18, 4, 20, 6, 22, 8, 24, 10, 26, 12, 28, 14, 30},
     kSSES8x16TransposeLow,
     kAVXS8x16TransposeLow,
     true,
     true},
    {{1, 17, 3, 19, 5, 21, 7, 23, 9, 25, 11, 27, 13, 29, 15, 31},
     kSSES8x16TransposeHigh,
     kAVXS8x16TransposeHigh,
     true,
     true},
    {{7, 6, 5, 4, 3, 2, 1, 0, 15, 14, 13, 12, 11, 10, 9, 8},
     kSSES8x8Reverse,
     kAVXS8x8Reverse,
     true,
     true},
    {{3, 2, 1, 0, 7, 6, 5, 4, 11, 10, 9, 8, 15, 14, 13, 12},
     kSSES8x4Reverse,
     kAVXS8x4Reverse,
     true,
     true},
    {{1, 0, 3, 2, 5, 4, 7, 6, 9, 8, 11, 10, 13, 12, 15, 14},
     kSSES8x2Reverse,
     kAVXS8x2Reverse,
     true,
     true}};

bool TryMatchArchShuffle(const uint8_t* shuffle, const ShuffleEntry* table,
                         size_t num_entries, bool is_swizzle,
                         const ShuffleEntry** arch_shuffle) {
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
      *arch_shuffle = &entry;
      return true;
    }
  }
  return false;
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Shuffle(node_t node) {
  uint8_t shuffle[kSimd128Size];
  bool is_swizzle;
  auto view = this->simd_shuffle_view(node);
  CanonicalizeShuffle(view, shuffle, &is_swizzle);

  int imm_count = 0;
  static const int kMaxImms = 6;
  uint32_t imms[kMaxImms];
  int temp_count = 0;
  static const int kMaxTemps = 2;
  InstructionOperand temps[kMaxTemps];

  IA32OperandGeneratorT<Adapter> g(this);
  bool use_avx = CpuFeatures::IsSupported(AVX);
  // AVX and swizzles don't generally need DefineSameAsFirst to avoid a move.
  bool no_same_as_first = use_avx || is_swizzle;
  // We generally need UseRegister for input0, Use for input1.
  // TODO(v8:9198): We don't have 16-byte alignment for SIMD operands yet, but
  // we retain this logic (continue setting these in the various shuffle match
  // clauses), but ignore it when selecting registers or slots.
  bool src0_needs_reg = true;
  bool src1_needs_reg = false;
  ArchOpcode opcode = kIA32I8x16Shuffle;  // general shuffle is the default

  uint8_t offset;
  uint8_t shuffle32x4[4];
  uint8_t shuffle16x8[8];
  int index;
  const ShuffleEntry* arch_shuffle;
  if (wasm::SimdShuffle::TryMatchConcat(shuffle, &offset)) {
    if (wasm::SimdShuffle::TryMatch32x4Rotate(shuffle, shuffle32x4,
                                              is_swizzle)) {
      uint8_t shuffle_mask = wasm::SimdShuffle::PackShuffle4(shuffle32x4);
      opcode = kIA32S32x4Rotate;
      imms[imm_count++] = shuffle_mask;
    } else {
      // Swap inputs from the normal order for (v)palignr.
      SwapShuffleInputs(view);
      is_swizzle = false;  // It's simpler to just handle the general case.
      no_same_as_first = use_avx;  // SSE requires same-as-first.
      opcode = kIA32S8x16Alignr;
      // palignr takes a single imm8 offset.
      imms[imm_count++] = offset;
    }
  } else if (TryMatchArchShuffle(shuffle, arch_shuffles,
                                 arraysize(arch_shuffles), is_swizzle,
                                 &arch_shuffle)) {
    opcode = use_avx ? arch_shuffle->avx_opcode : arch_shuffle->opcode;
    src0_needs_reg = !use_avx || arch_shuffle->src0_needs_reg;
    // SSE can't take advantage of both operands in registers and needs
    // same-as-first.
    src1_needs_reg = use_avx && arch_shuffle->src1_needs_reg;
    no_same_as_first = use_avx;
  } else if (wasm::SimdShuffle::TryMatch32x4Shuffle(shuffle, shuffle32x4)) {
    uint8_t shuffle_mask = wasm::SimdShuffle::PackShuffle4(shuffle32x4);
    if (is_swizzle) {
      if (wasm::SimdShuffle::TryMatchIdentity(shuffle)) {
        // Bypass normal shuffle code generation in this case.
        node_t input = view.input(0);
        // EmitIdentity
        MarkAsUsed(input);
        MarkAsDefined(node);
        SetRename(node, input);
        return;
      } else {
        // pshufd takes a single imm8 shuffle mask.
        opcode = kIA32S32x4Swizzle;
        no_same_as_first = true;
        // TODO(v8:9198): This doesn't strictly require a register, forcing the
        // swizzles to always use registers until generation of incorrect memory
        // operands can be fixed.
        src0_needs_reg = true;
        imms[imm_count++] = shuffle_mask;
      }
    } else {
      // 2 operand shuffle
      // A blend is more efficient than a general 32x4 shuffle; try it first.
      if (wasm::SimdShuffle::TryMatchBlend(shuffle)) {
        opcode = kIA32S16x8Blend;
        uint8_t blend_mask = wasm::SimdShuffle::PackBlend4(shuffle32x4);
        imms[imm_count++] = blend_mask;
      } else {
        opcode = kIA32S32x4Shuffle;
        no_same_as_first = true;
        // TODO(v8:9198): src0 and src1 is used by pshufd in codegen, which
        // requires memory to be 16-byte aligned, since we cannot guarantee that
        // yet, force using a register here.
        src0_needs_reg = true;
        src1_needs_reg = true;
        imms[imm_count++] = shuffle_mask;
        int8_t blend_mask = wasm::SimdShuffle::PackBlend4(shuffle32x4);
        imms[imm_count++] = blend_mask;
      }
    }
  } else if (wasm::SimdShuffle::TryMatch16x8Shuffle(shuffle, shuffle16x8)) {
    uint8_t blend_mask;
    if (wasm::SimdShuffle::TryMatchBlend(shuffle)) {
      opcode = kIA32S16x8Blend;
      blend_mask = wasm::SimdShuffle::PackBlend8(shuffle16x8);
      imms[imm_count++] = blend_mask;
    } else if (wasm::SimdShuffle::TryMatchSplat<8>(shuffle, &index)) {
      opcode = kIA32S16x8Dup;
      src0_needs_reg = false;
      imms[imm_count++] = index;
    } else if (TryMatch16x8HalfShuffle(shuffle16x8, &blend_mask)) {
      opcode = is_swizzle ? kIA32S16x8HalfShuffle1 : kIA32S16x8HalfShuffle2;
      // Half-shuffles don't need DefineSameAsFirst or UseRegister(src0).
      no_same_as_first = true;
      src0_needs_reg = false;
      uint8_t mask_lo = wasm::SimdShuffle::PackShuffle4(shuffle16x8);
      uint8_t mask_hi = wasm::SimdShuffle::PackShuffle4(shuffle16x8 + 4);
      imms[imm_count++] = mask_lo;
      imms[imm_count++] = mask_hi;
      if (!is_swizzle) imms[imm_count++] = blend_mask;
    }
  } else if (wasm::SimdShuffle::TryMatchSplat<16>(shuffle, &index)) {
    opcode = kIA32S8x16Dup;
    no_same_as_first = use_avx;
    src0_needs_reg = true;
    imms[imm_count++] = index;
  }
  if (opcode == kIA32I8x16Shuffle) {
    // Use same-as-first for general swizzle, but not shuffle.
    no_same_as_first = !is_swizzle;
    src0_needs_reg = !no_same_as_first;
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 4);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 8);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 12);
    temps[temp_count++] = g.TempRegister();
  }

  // Use DefineAsRegister(node) and Use(src0) if we can without forcing an extra
  // move instruction in the CodeGenerator.
  node_t input0 = view.input(0);
  InstructionOperand dst =
      no_same_as_first ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  // TODO(v8:9198): Use src0_needs_reg when we have memory alignment for SIMD.
  InstructionOperand src0 = g.UseRegister(input0);
  USE(src0_needs_reg);

  int input_count = 0;
  InstructionOperand inputs[2 + kMaxImms + kMaxTemps];
  inputs[input_count++] = src0;
  if (!is_swizzle) {
    node_t input1 = view.input(1);
    // TODO(v8:9198): Use src1_needs_reg when we have memory alignment for SIMD.
    inputs[input_count++] = g.UseRegister(input1);
    USE(src1_needs_reg);
  }
  for (int i = 0; i < imm_count; ++i) {
    inputs[input_count++] = g.UseImmediate(imms[i]);
  }
  Emit(opcode, 1, &dst, input_count, inputs, temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Swizzle(node_t node) {
  InstructionCode op = kIA32I8x16Swizzle;

  node_t left = this->input_at(node, 0);
  node_t right = this->input_at(node, 1);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128BinopOp& binop =
        this->Get(node).template Cast<turboshaft::Simd128BinopOp>();
    DCHECK(binop.kind ==
           turboshaft::any_of(
               turboshaft::Simd128BinopOp::Kind::kI8x16Swizzle,
               turboshaft::Simd128BinopOp::Kind::kI8x16RelaxedSwizzle));
    bool relaxed =
        binop.kind == turboshaft::Simd128BinopOp::Kind::kI8x16RelaxedSwizzle;
    if (relaxed) {
      op |= MiscField::encode(true);
    } else {
      // If the indices vector is a const, check if they are in range, or if the
      // top bit is set, then we can avoid the paddusb in the codegen and simply
      // emit a pshufb.
      const turboshaft::Operation& right_op = this->Get(right);
      if (auto c = right_op.TryCast<turboshaft::Simd128ConstantOp>()) {
        std::array<uint8_t, kSimd128Size> imms;
        std::memcpy(&imms, c->value, kSimd128Size);
        op |= MiscField::encode(wasm::SimdSwizzle::AllInRangeOrTopBitSet(imms));
      }
    }
  } else {
    // Turbofan.
    bool relaxed = OpParameter<bool>(node->op());
    if (relaxed) {
      op |= MiscField::encode(true);
    } else {
      auto m = V128ConstMatcher(node->InputAt(1));
      if (m.HasResolvedValue()) {
        // If the indices vector is a const, check if they are in range, or if
        // the top bit is set, then we can avoid the paddusb in the codegen and
        // simply emit a pshufb.
        auto imms = m.ResolvedValue().immediate();
        op |= MiscField::encode(wasm::SimdSwizzle::AllInRangeOrTopBitSet(imms));
      }
    }
  }

  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  Emit(op,
       IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSetStackPointer(node_t node) {
  OperandGenerator g(this);
  auto input = g.UseAny(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

namespace {

template <typename Adapter>
void VisitMinOrMax(InstructionSelectorT<Adapter>* selector,
                   typename Adapter::node_t node, ArchOpcode opcode,
                   bool flip_inputs) {
  // Due to the way minps/minpd work, we want the dst to be same as the second
  // input: b = pmin(a, b) directly maps to minps b a.
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand dst = selector->IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  if (flip_inputs) {
    // Due to the way minps/minpd work, we want the dst to be same as the second
    // input: b = pmin(a, b) directly maps to minps b a.
    selector->Emit(opcode, dst, g.UseRegister(selector->input_at(node, 1)),
                   g.UseRegister(selector->input_at(node, 0)));
  } else {
    selector->Emit(opcode, dst, g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));
  }
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmin(node_t node) {
  VisitMinOrMax(this, node, kIA32Minps, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmax(node_t node) {
  VisitMinOrMax(this, node, kIA32Maxps, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmin(node_t node) {
  VisitMinOrMax(this, node, kIA32Minpd, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmax(node_t node) {
  VisitMinOrMax(this, node, kIA32Maxpd, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMin(node_t node) {
  VisitMinOrMax(this, node, kIA32Minps, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMax(node_t node) {
  VisitMinOrMax(this, node, kIA32Maxps, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMin(node_t node) {
  VisitMinOrMax(this, node, kIA32Minpd, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMax(node_t node) {
  VisitMinOrMax(this, node, kIA32Maxpd, false);
}

namespace {

template <typename Adapter>
void VisitExtAddPairwise(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode,
                         bool need_temp) {
  IA32OperandGeneratorT<Adapter> g(selector);
  InstructionOperand operand0 = g.UseRegister(selector->input_at(node, 0));
  InstructionOperand dst = (selector->IsSupported(AVX))
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  if (need_temp) {
    InstructionOperand temps[] = {g.TempRegister()};
    selector->Emit(opcode, dst, operand0, arraysize(temps), temps);
  } else {
    selector->Emit(opcode, dst, operand0);
  }
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8S(
    node_t node) {
  VisitExtAddPairwise(this, node, kIA32I32x4ExtAddPairwiseI16x8S, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8U(
    node_t node) {
  VisitExtAddPairwise(this, node, kIA32I32x4ExtAddPairwiseI16x8U, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16S(
    node_t node) {
  VisitExtAddPairwise(this, node, kIA32I16x8ExtAddPairwiseI8x16S, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16U(
    node_t node) {
  VisitExtAddPairwise(this, node, kIA32I16x8ExtAddPairwiseI8x16U, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Popcnt(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand dst = CpuFeatures::IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineAsRegister(node);
  InstructionOperand temps[] = {g.TempSimd128Register(), g.TempRegister()};
  Emit(kIA32I8x16Popcnt, dst, g.UseUniqueRegister(this->input_at(node, 0)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ConvertLowI32x4U(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kIA32F64x2ConvertLowI32x4U, dst, g.UseRegister(this->input_at(node, 0)),
       arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2SZero(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  if (IsSupported(AVX)) {
    // Requires dst != src.
    Emit(kIA32I32x4TruncSatF64x2SZero, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)), arraysize(temps), temps);
  } else {
    Emit(kIA32I32x4TruncSatF64x2SZero, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2UZero(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister()};
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kIA32I32x4TruncSatF64x2UZero, dst,
       g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2SZero(
    node_t node) {
  VisitRRSimd(this, node, kIA32Cvttpd2dq);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2UZero(
    node_t node) {
  VisitFloatUnop(this, node, this->input_at(node, 0),
                 kIA32I32x4TruncF64x2UZero);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF32x4S(node_t node) {
  VisitRRSimd(this, node, kIA32Cvttps2dq);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF32x4U(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  node_t input = this->input_at(node, 0);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  // No need for unique because inputs are float but temp is general.
  if (IsSupported(AVX)) {
    Emit(kIA32I32x4TruncF32x4U, g.DefineAsRegister(node), g.UseRegister(input),
         arraysize(temps), temps);
  } else {
    Emit(kIA32I32x4TruncF32x4U, g.DefineSameAsFirst(node), g.UseRegister(input),
         arraysize(temps), temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2GtS(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  if (CpuFeatures::IsSupported(AVX)) {
    Emit(kIA32I64x2GtS, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    Emit(kIA32I64x2GtS, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else {
    Emit(kIA32I64x2GtS, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2GeS(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  if (CpuFeatures::IsSupported(AVX)) {
    Emit(kIA32I64x2GeS, g.DefineAsRegister(node),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    Emit(kIA32I64x2GeS, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else {
    Emit(kIA32I64x2GeS, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Abs(node_t node) {
  VisitRRSimd(this, node, kIA32I64x2Abs, kIA32I64x2Abs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2PromoteLowF32x4(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionCode code = kIA32F64x2PromoteLowF32x4;
  node_t input = this->input_at(node, 0);
  if constexpr (Adapter::IsTurboshaft) {
    // TODO(nicohartmann@): Implement this special case for turboshaft. Note
    // that this special case may require adaptions in instruction-selector.cc
    // in `FinishEmittedInstructions`, similar to what exists for TurboFan.
  } else {
    LoadTransformMatcher m(input);

    if (m.Is(LoadTransformation::kS128Load64Zero) && CanCover(node, input)) {
      // Trap handler is not supported on IA32.
      DCHECK_NE(m.ResolvedValue().kind, MemoryAccessKind::kProtected);
      // LoadTransforms cannot be eliminated, so they are visited even if
      // unused. Mark it as defined so that we don't visit it.
      MarkAsDefined(input);
      VisitLoad(node, input, code);
      return;
    }
  }

  VisitRR(this, node, code);
}

namespace {
template <typename Adapter>
void VisitRelaxedLaneSelect(InstructionSelectorT<Adapter>* selector,
                            typename Adapter::node_t node,
                            InstructionCode code = kIA32Pblendvb) {
  IA32OperandGeneratorT<Adapter> g(selector);
  // pblendvb/blendvps/blendvpd copies src2 when mask is set, opposite from Wasm
  // semantics. node's inputs are: mask, lhs, rhs (determined in
  // wasm-compiler.cc).
  if (selector->IsSupported(AVX)) {
    selector->Emit(code, g.DefineAsRegister(node),
                   g.UseRegister(selector->input_at(node, 2)),
                   g.UseRegister(selector->input_at(node, 1)),
                   g.UseRegister(selector->input_at(node, 0)));
  } else {
    // SSE4.1 pblendvb/blendvps/blendvpd requires xmm0 to hold the mask as an
    // implicit operand.
    selector->Emit(code, g.DefineSameAsFirst(node),
                   g.UseRegister(selector->input_at(node, 2)),
                   g.UseRegister(selector->input_at(node, 1)),
                   g.UseFixed(selector->input_at(node, 0), xmm0));
  }
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node, kIA32Blendvps);
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node, kIA32Blendvpd);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Qfma(node_t node) {
  VisitRRRR(this, node, kIA32F64x2Qfma);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Qfms(node_t node) {
  VisitRRRR(this, node, kIA32F64x2Qfms);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Qfma(node_t node) {
  VisitRRRR(this, node, kIA32F32x4Qfma);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Qfms(node_t node) {
  VisitRRRR(this, node, kIA32F32x4Qfms);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Qfma(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Qfms(node_t node) {
  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8DotI8x16I7x16S(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  Emit(kIA32I16x8DotI8x16I7x16S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
  IA32OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(kIA32I32x4DotI8x16I7x16AddS, g.DefineSameAsInput(node, 2),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseUniqueRegister(this->input_at(node, 1)),
       g.UseUniqueRegister(this->input_at(node, 2)), arraysize(temps), temps);
}
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGeneratorT<Adapter>* g, int first_input_index, node_t node) {
  UNREACHABLE();
}

// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  MachineOperatorBuilder::Flags flags =
      MachineOperatorBuilder::kWord32ShiftIsSafe |
      MachineOperatorBuilder::kWord32Ctz | MachineOperatorBuilder::kWord32Rol;
  if (CpuFeatures::IsSupported(POPCNT)) {
    flags |= MachineOperatorBuilder::kWord32Popcnt;
  }
  if (CpuFeatures::IsSupported(SSE4_1)) {
    flags |= MachineOperatorBuilder::kFloat32RoundDown |
             MachineOperatorBuilder::kFloat64RoundDown |
             MachineOperatorBuilder::kFloat32RoundUp |
             MachineOperatorBuilder::kFloat64RoundUp |
             MachineOperatorBuilder::kFloat32RoundTruncate |
             MachineOperatorBuilder::kFloat64RoundTruncate |
             MachineOperatorBuilder::kFloat32RoundTiesEven |
             MachineOperatorBuilder::kFloat64RoundTiesEven;
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
