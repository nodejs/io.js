// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include "src/base/bounds.h"
#include "src/base/iterator.h"
#include "src/base/logging.h"
#include "src/base/overflowing-math.h"
#include "src/codegen/cpu-features.h"
#include "src/codegen/machine-type.h"
#include "src/common/assert-scope.h"
#include "src/common/globals.h"
#include "src/compiler/backend/instruction-codes.h"
#include "src/compiler/backend/instruction-selector-adapter.h"
#include "src/compiler/backend/instruction-selector-impl.h"
#include "src/compiler/backend/instruction-selector.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/turboshaft/load-store-simplification-reducer.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/handles/handles-inl.h"
#include "src/objects/slots-inl.h"
#include "src/roots/roots-inl.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool IsCompressed(Node* const node) {
  if (node == nullptr) return false;
  const IrOpcode::Value opcode = node->opcode();
  if (opcode == IrOpcode::kLoad || opcode == IrOpcode::kProtectedLoad ||
      opcode == IrOpcode::kLoadTrapOnNull ||
      opcode == IrOpcode::kUnalignedLoad ||
      opcode == IrOpcode::kLoadImmutable) {
    LoadRepresentation load_rep = LoadRepresentationOf(node->op());
    return load_rep.IsCompressed();
  } else if (node->opcode() == IrOpcode::kPhi) {
    MachineRepresentation phi_rep = PhiRepresentationOf(node->op());
    return phi_rep == MachineRepresentation::kCompressed ||
           phi_rep == MachineRepresentation::kCompressedPointer;
  }
  return false;
}

template <typename Adapter>
bool IsCompressed(InstructionSelectorT<Adapter>* selector,
                  turboshaft::OpIndex node) {
  if (!node.valid()) return false;
  if (selector->is_load(node)) {
    auto load = selector->load_view(node);
    return load.loaded_rep().IsCompressed();
  } else if (selector->IsPhi(node)) {
    MachineRepresentation phi_rep = selector->phi_representation_of(node);
    return phi_rep == MachineRepresentation::kCompressed ||
           phi_rep == MachineRepresentation::kCompressedPointer;
  }
  return false;
}

#ifdef DEBUG
// {left_idx} and {right_idx} are assumed to be the inputs of a commutative
// binop. This function checks that {left_idx} is not the only constant input of
// this binop (since the graph should have been normalized before, putting
// constants on the right input of binops when possible).
bool LhsIsNotOnlyConstant(turboshaft::Graph* graph,
                          turboshaft::OpIndex left_idx,
                          turboshaft::OpIndex right_idx) {
  using namespace turboshaft;  // NOLINT(build/namespaces)

  const Operation& left = graph->Get(left_idx);
  const Operation& right = graph->Get(right_idx);

  if (right.Is<ConstantOp>()) {
    // There is a constant on the right.
    return true;
  }
  if (left.Is<ConstantOp>()) {
    // Constant on the left but not on the right.
    return false;
  }

  // Left is not a constant
  return true;
}

#endif

}  // namespace

bool ValueFitsIntoImmediate(int64_t value) {
  // int32_t min will overflow if displacement mode is kNegativeDisplacement.
  constexpr int64_t kImmediateMin = std::numeric_limits<int32_t>::min() + 1;
  constexpr int64_t kImmediateMax = std::numeric_limits<int32_t>::max();
  static_assert(kImmediateMin ==
                turboshaft::LoadStoreSimplificationConfiguration::kMinOffset);
  static_assert(kImmediateMax ==
                turboshaft::LoadStoreSimplificationConfiguration::kMaxOffset);
  return kImmediateMin <= value && value <= kImmediateMax;
}

template <typename Adapter>
bool CanBeImmediate(InstructionSelectorT<Adapter>* selector,
                    typename Adapter::node_t node) {
  // TODO(dmercadier): this is not in sync with GetImmediateIntegerValue, which
  // is surprising because we often use the pattern
  // `if (CanBeImmediate()) { GetImmediateIntegerValue }`. We should make sure
  // that both functions are in sync.
  if (!selector->is_constant(node)) return false;
  auto constant = selector->constant_view(node);
  if (constant.is_compressed_heap_object()) {
    if (!COMPRESS_POINTERS_BOOL) return false;
    // For builtin code we need static roots
    if (selector->isolate()->bootstrapper() && !V8_STATIC_ROOTS_BOOL) {
      return false;
    }
    const RootsTable& roots_table = selector->isolate()->roots_table();
    RootIndex root_index;
    Handle<HeapObject> value = constant.heap_object_value();
    if (roots_table.IsRootHandle(value, &root_index)) {
      return RootsTable::IsReadOnly(root_index);
    }
    return false;
  }
  if (constant.is_int32() || constant.is_relocatable_int32()) {
    const int32_t value = constant.int32_value();
    // int32_t min will overflow if displacement mode is
    // kNegativeDisplacement.
    return value != std::numeric_limits<int32_t>::min();
  }
  if (constant.is_int64()) {
    const int64_t value = constant.int64_value();
    return ValueFitsIntoImmediate(value);
  }
  if (constant.is_number_zero()) {
    return true;
  }
  return false;
}

template <typename Adapter>
int32_t GetImmediateIntegerValue(InstructionSelectorT<Adapter>* selector,
                                 typename Adapter::node_t node) {
  DCHECK(CanBeImmediate(selector, node));
  auto constant = selector->constant_view(node);
  if (constant.is_int32()) return constant.int32_value();
  if (constant.is_int64()) {
    return static_cast<int32_t>(constant.int64_value());
  }
  DCHECK(constant.is_number_zero());
  return 0;
}

template <typename Adapter>
struct ScaledIndexMatch {
  using node_t = typename Adapter::node_t;

  node_t base;
  node_t index;
  int scale;
};

template <typename ScaleMatcher>
std::optional<ScaledIndexMatch<TurbofanAdapter>> TryMatchScaledIndex(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    bool allow_power_of_two_plus_one) {
  ScaleMatcher m(node, allow_power_of_two_plus_one);
  if (!m.matches()) return std::nullopt;
  ScaledIndexMatch<TurbofanAdapter> match;
  match.index = node->InputAt(0);
  match.base = m.power_of_two_plus_one() ? match.index : nullptr;
  match.scale = m.scale();
  return match;
}

std::optional<ScaledIndexMatch<TurbofanAdapter>> TryMatchScaledIndex32(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    bool allow_power_of_two_plus_one) {
  return TryMatchScaledIndex<Int32ScaleMatcher>(selector, node,
                                                allow_power_of_two_plus_one);
}

std::optional<ScaledIndexMatch<TurbofanAdapter>> TryMatchScaledIndex64(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    bool allow_power_of_two_plus_one) {
  return TryMatchScaledIndex<Int64ScaleMatcher>(selector, node,
                                                allow_power_of_two_plus_one);
}

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
    if (constant->kind != ConstantOp::Kind::kWord32 &&
        constant->kind != ConstantOp::Kind::kWord64) {
      return false;
    }
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
    int64_t scale_value;
    if (selector->MatchSignedIntegralConstant(shift->right(), &scale_value)) {
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

std::optional<ScaledIndexMatch<TurboshaftAdapter>> TryMatchScaledIndex32(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex node,
    bool allow_power_of_two_plus_one) {
  return TryMatchScaledIndex(selector, node, allow_power_of_two_plus_one);
}

std::optional<ScaledIndexMatch<TurboshaftAdapter>> TryMatchScaledIndex64(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex node,
    bool allow_power_of_two_plus_one) {
  return TryMatchScaledIndex(selector, node, allow_power_of_two_plus_one);
}

template <typename Adapter>
struct BaseWithScaledIndexAndDisplacementMatch {
  using node_t = typename Adapter::node_t;

  node_t base = {};
  node_t index = {};
  int scale = 0;
  int64_t displacement = 0;
  DisplacementMode displacement_mode = kPositiveDisplacement;
};

template <typename BaseWithIndexAndDisplacementMatcher>
std::optional<BaseWithScaledIndexAndDisplacementMatch<TurbofanAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node) {
  BaseWithScaledIndexAndDisplacementMatch<TurbofanAdapter> result;
  BaseWithIndexAndDisplacementMatcher m(node);
  if (m.matches()) {
    result.base = m.base();
    result.index = m.index();
    result.scale = m.scale();
    if (m.displacement() == nullptr) {
      result.displacement = 0;
    } else {
      if (m.displacement()->opcode() == IrOpcode::kInt64Constant) {
        result.displacement = OpParameter<int64_t>(m.displacement()->op());
      } else {
        DCHECK_EQ(m.displacement()->opcode(), IrOpcode::kInt32Constant);
        result.displacement = OpParameter<int32_t>(m.displacement()->op());
      }
    }
    result.displacement_mode = m.displacement_mode();
    return result;
  }
  return std::nullopt;
}

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurbofanAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement64(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node) {
  return TryMatchBaseWithScaledIndexAndDisplacement<
      BaseWithIndexAndDisplacement64Matcher>(selector, node);
}

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurbofanAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement32(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node) {
  return TryMatchBaseWithScaledIndexAndDisplacement<
      BaseWithIndexAndDisplacement32Matcher>(selector, node);
}

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement64ForWordBinop(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex left,
    turboshaft::OpIndex right, bool is_commutative);

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement64(
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
    DCHECK_EQ(load_transform->offset, 0);

    if (CanBeImmediate(selector, load_transform->index())) {
      result.index = {};
      result.displacement =
          GetImmediateIntegerValue(selector, load_transform->index());
    } else {
      result.index = load_transform->index();
      result.displacement = 0;
    }

    result.scale = 0;
    DCHECK(!load_transform->load_kind.tagged_base);
    return result;
#if V8_ENABLE_WASM_SIMD256_REVEC
  } else if (const Simd256LoadTransformOp* load_transform =
                 op.TryCast<Simd256LoadTransformOp>()) {
    result.base = load_transform->base();
    result.index = load_transform->index();
    DCHECK_EQ(load_transform->offset, 0);
    result.scale = 0;
    result.displacement = 0;
    DCHECK(!load_transform->load_kind.tagged_base);
    return result;
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY
  } else {
    return std::nullopt;
  }

  const WordBinopOp& binop = op.Cast<WordBinopOp>();
  OpIndex left = binop.left();
  OpIndex right = binop.right();
  return TryMatchBaseWithScaledIndexAndDisplacement64ForWordBinop(
      selector, left, right, binop.IsCommutative());
}

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement64ForWordBinop(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex left,
    turboshaft::OpIndex right, bool is_commutative) {
  using namespace turboshaft;  // NOLINT(build/namespaces)

  // In the comments of this function, the following letters have the following
  // meaning:
  //
  //   S: scaled index. That is, "OpIndex * constant" or "OpIndex << constant",
  //      where "constant" is a small power of 2 (1, 2, 4, 8 for the
  //      multiplication, 0, 1, 2 or 3 for the shift). The "constant" is called
  //      "scale" in the BaseWithScaledIndexAndDisplacementMatch struct that is
  //      returned.
  //
  //   B: base. Just a regular OpIndex.
  //
  //   D: displacement. An integral constant.

  // Helper to check (S + ...)
  auto match_S_plus = [&selector](OpIndex left, OpIndex right)
      -> std::optional<
          BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>> {
    BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
    result.displacement_mode = kPositiveDisplacement;

    // Check (S + ...)
    if (MatchScaledIndex(selector, left, &result.index, &result.scale,
                         nullptr)) {
      result.displacement_mode = kPositiveDisplacement;

      // Check (S + (... binop ...))
      if (const WordBinopOp* right_binop =
              selector->Get(right).TryCast<WordBinopOp>()) {
        // Check (S + (B - D))
        if (right_binop->kind == WordBinopOp::Kind::kSub) {
          if (!selector->MatchSignedIntegralConstant(right_binop->right(),
                                                     &result.displacement)) {
            return std::nullopt;
          }
          result.base = right_binop->left();
          result.displacement_mode = kNegativeDisplacement;
          return result;
        }
        // Check (S + (... + ...))
        if (right_binop->kind == WordBinopOp::Kind::kAdd) {
          if (selector->MatchSignedIntegralConstant(right_binop->right(),
                                                    &result.displacement)) {
            // (S + (B + D))
            result.base = right_binop->left();
          } else if (selector->MatchSignedIntegralConstant(
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
      if (selector->MatchSignedIntegralConstant(right, &result.displacement)) {
        result.base = OpIndex{};
        return result;
      }

      // Treat it as (S + B)
      result.base = right;
      result.displacement = 0;
      return result;
    }

    return std::nullopt;
  };

  // Helper to check ((S + ...) + ...)
  auto match_S_plus_plus = [&selector](turboshaft::OpIndex left,
                                       turboshaft::OpIndex right,
                                       turboshaft::OpIndex left_add_left,
                                       turboshaft::OpIndex left_add_right)
      -> std::optional<
          BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>> {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    DCHECK_EQ(selector->Get(left).Cast<WordBinopOp>().kind,
              WordBinopOp::Kind::kAdd);

    BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
    result.displacement_mode = kPositiveDisplacement;

    if (MatchScaledIndex(selector, left_add_left, &result.index, &result.scale,
                         nullptr)) {
      result.displacement_mode = kPositiveDisplacement;
      // Check ((S + D) + B)
      if (selector->MatchSignedIntegralConstant(left_add_right,
                                                &result.displacement)) {
        result.base = right;
        return result;
      }
      // Check ((S + B) + D)
      if (selector->MatchSignedIntegralConstant(right, &result.displacement)) {
        result.base = left_add_right;
        return result;
      }
      // Treat it as (B + B) and use index as right B.
      result.base = left;
      result.index = right;
      result.scale = 0;
      DCHECK_EQ(result.displacement, 0);
      return result;
    }
    return std::nullopt;
  };

  // Helper to check ((... + ...) + ...)
  auto match_plus_plus = [&selector, &match_S_plus_plus](OpIndex left,
                                                         OpIndex right)
      -> std::optional<
          BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>> {
    BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
    result.displacement_mode = kPositiveDisplacement;

    // Check ((... + ...) + ...)
    if (const WordBinopOp* left_add =
            selector->Get(left).TryCast<WordBinopOp>();
        left_add && left_add->kind == WordBinopOp::Kind::kAdd) {
      // Check ((S + ...) + ...)
      auto maybe_res =
          match_S_plus_plus(left, right, left_add->left(), left_add->right());
      if (maybe_res) return maybe_res;
      // Check ((... + S) + ...)
      maybe_res =
          match_S_plus_plus(left, right, left_add->right(), left_add->left());
      if (maybe_res) return maybe_res;
    }

    return std::nullopt;
  };

  // Check (S + ...)
  auto maybe_res = match_S_plus(left, right);
  if (maybe_res) return maybe_res;

  if (is_commutative) {
    // Check (... + S)
    maybe_res = match_S_plus(right, left);
    if (maybe_res) {
      return maybe_res;
    }
  }

  // Check ((... + ...) + ...)
  maybe_res = match_plus_plus(left, right);
  if (maybe_res) return maybe_res;

  if (is_commutative) {
    // Check (... + (... + ...))
    maybe_res = match_plus_plus(right, left);
    if (maybe_res) {
      return maybe_res;
    }
  }

  BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter> result;
  result.displacement_mode = kPositiveDisplacement;

  // Check (B + D)
  if (selector->MatchSignedIntegralConstant(right, &result.displacement)) {
    result.base = left;
    return result;
  }

  // Treat as (B + B) and use index as left B.
  result.index = left;
  result.base = right;
  return result;
}

std::optional<BaseWithScaledIndexAndDisplacementMatch<TurboshaftAdapter>>
TryMatchBaseWithScaledIndexAndDisplacement32(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    turboshaft::OpIndex node) {
  return TryMatchBaseWithScaledIndexAndDisplacement64(selector, node);
}

// Adds X64-specific methods for generating operands.
template <typename Adapter>
class X64OperandGeneratorT final : public OperandGeneratorT<Adapter> {
 public:
  OPERAND_GENERATOR_T_BOILERPLATE(Adapter)

  explicit X64OperandGeneratorT(InstructionSelectorT<Adapter>* selector)
      : super(selector) {}

  template <typename T>
  bool CanBeImmediate(T*) {
    UNREACHABLE(/*REMOVE*/);
  }

  bool CanBeImmediate(node_t node) {
    return compiler::CanBeImmediate(this->selector(), node);
  }

  int32_t GetImmediateIntegerValue(node_t node) {
    return compiler::GetImmediateIntegerValue(this->selector(), node);
  }

  bool CanBeMemoryOperand(InstructionCode opcode, node_t node, node_t input,
                          int effect_level) {
    if (!this->IsLoadOrLoadImmutable(input)) return false;
    if (!selector()->CanCover(node, input)) return false;

    if constexpr (Adapter::IsTurboshaft) {
      // A ProtectedLoad increases the effect level by 1.
      int effect_level_after_input = selector()->GetEffectLevel(input) +
                                     (this->IsProtectedLoad(node) ? 1 : 0);
      if (effect_level != effect_level_after_input) {
        return false;
      }
    } else {
      if (effect_level != selector()->GetEffectLevel(input)) {
        return false;
      }
    }

    MachineRepresentation rep =
        this->load_view(input).loaded_rep().representation();
    switch (opcode) {
      case kX64And:
      case kX64Or:
      case kX64Xor:
      case kX64Add:
      case kX64Sub:
      case kX64Push:
      case kX64Cmp:
      case kX64Test:
        // When pointer compression is enabled 64-bit memory operands can't be
        // used for tagged values.
        return rep == MachineRepresentation::kWord64 ||
               (!COMPRESS_POINTERS_BOOL && IsAnyTagged(rep));
      case kX64And32:
      case kX64Or32:
      case kX64Xor32:
      case kX64Add32:
      case kX64Sub32:
      case kX64Cmp32:
      case kX64Test32:
        // When pointer compression is enabled 32-bit memory operands can be
        // used for tagged values.
        return rep == MachineRepresentation::kWord32 ||
               (COMPRESS_POINTERS_BOOL &&
                (IsAnyTagged(rep) || IsAnyCompressed(rep)));
      case kAVXFloat64Add:
      case kAVXFloat64Sub:
      case kAVXFloat64Mul:
        DCHECK_EQ(MachineRepresentation::kFloat64, rep);
        return true;
      case kAVXFloat32Add:
      case kAVXFloat32Sub:
      case kAVXFloat32Mul:
        DCHECK_EQ(MachineRepresentation::kFloat32, rep);
        return true;
      case kX64Cmp16:
      case kX64Test16:
        return rep == MachineRepresentation::kWord16;
      case kX64Cmp8:
      case kX64Test8:
        return rep == MachineRepresentation::kWord8;
      default:
        break;
    }
    return false;
  }

  bool IsZeroIntConstant(node_t node) const {
    if constexpr (Adapter::IsTurboshaft) {
      if (turboshaft::ConstantOp* op =
              this->turboshaft_graph()
                  ->Get(node)
                  .template TryCast<turboshaft::ConstantOp>()) {
        switch (op->kind) {
          case turboshaft::ConstantOp::Kind::kWord32:
            return op->word32() == 0;
          case turboshaft::ConstantOp::Kind::kWord64:
            return op->word64() == 0;
          default:
            break;
        }
      }
      return false;
    } else {
      if (node->opcode() == IrOpcode::kInt32Constant) {
        return OpParameter<int32_t>(node->op()) == 0;
      } else if (node->opcode() == IrOpcode::kInt64Constant) {
        return OpParameter<int64_t>(node->op()) == 0;
      }
      return false;
    }
  }

  AddressingMode GenerateMemoryOperandInputs(
      optional_node_t index, int scale_exponent, node_t base,
      int64_t displacement, DisplacementMode displacement_mode,
      InstructionOperand inputs[], size_t* input_count,
      RegisterUseKind reg_kind = RegisterUseKind::kUseRegister) {
    AddressingMode mode = kMode_MRI;
    node_t base_before_folding = base;
    bool fold_base_into_displacement = false;
    int64_t fold_value = 0;
    if (this->valid(base) && (this->valid(index) || displacement != 0)) {
      if (CanBeImmediate(base) && this->valid(index) &&
          ValueFitsIntoImmediate(displacement)) {
        fold_value = GetImmediateIntegerValue(base);
        if (displacement_mode == kNegativeDisplacement) {
          fold_value -= displacement;
        } else {
          fold_value += displacement;
        }
        if (V8_UNLIKELY(fold_value == 0)) {
          base = node_t{};
          displacement = 0;
        } else if (ValueFitsIntoImmediate(fold_value)) {
          base = node_t{};
          fold_base_into_displacement = true;
        }
      } else if (IsZeroIntConstant(base)) {
        base = node_t{};
      }
    }
    if (this->valid(base)) {
      inputs[(*input_count)++] = UseRegister(base, reg_kind);
      if (this->valid(index)) {
        DCHECK(scale_exponent >= 0 && scale_exponent <= 3);
        inputs[(*input_count)++] = UseRegister(this->value(index), reg_kind);
        if (displacement != 0) {
          inputs[(*input_count)++] = UseImmediate64(
              displacement_mode == kNegativeDisplacement ? -displacement
                                                         : displacement);
          static const AddressingMode kMRnI_modes[] = {kMode_MR1I, kMode_MR2I,
                                                       kMode_MR4I, kMode_MR8I};
          mode = kMRnI_modes[scale_exponent];
        } else {
          static const AddressingMode kMRn_modes[] = {kMode_MR1, kMode_MR2,
                                                      kMode_MR4, kMode_MR8};
          mode = kMRn_modes[scale_exponent];
        }
      } else {
        if (displacement == 0) {
          mode = kMode_MR;
        } else {
          inputs[(*input_count)++] = UseImmediate64(
              displacement_mode == kNegativeDisplacement ? -displacement
                                                         : displacement);
          mode = kMode_MRI;
        }
      }
    } else {
      DCHECK(scale_exponent >= 0 && scale_exponent <= 3);
      if (fold_base_into_displacement) {
        DCHECK(!this->valid(base));
        DCHECK(this->valid(index));
        inputs[(*input_count)++] = UseRegister(this->value(index), reg_kind);
        inputs[(*input_count)++] = UseImmediate(static_cast<int>(fold_value));
        static const AddressingMode kMnI_modes[] = {kMode_MRI, kMode_M2I,
                                                    kMode_M4I, kMode_M8I};
        mode = kMnI_modes[scale_exponent];
      } else if (displacement != 0) {
        if (!this->valid(index)) {
          // This seems to only occur in (0 + k) cases, but we don't have an
          // addressing mode for a simple constant, so we use the base in a
          // register for kMode_MRI.
          CHECK(IsZeroIntConstant(base_before_folding));
          inputs[(*input_count)++] = UseRegister(base_before_folding, reg_kind);
          inputs[(*input_count)++] = UseImmediate64(
              displacement_mode == kNegativeDisplacement ? -displacement
                                                         : displacement);
          mode = kMode_MRI;
        } else {
          inputs[(*input_count)++] = UseRegister(this->value(index), reg_kind);
          inputs[(*input_count)++] = UseImmediate64(
              displacement_mode == kNegativeDisplacement ? -displacement
                                                         : displacement);
          static const AddressingMode kMnI_modes[] = {kMode_MRI, kMode_M2I,
                                                      kMode_M4I, kMode_M8I};
          mode = kMnI_modes[scale_exponent];
        }
      } else {
        DCHECK(this->valid(index));
        inputs[(*input_count)++] = UseRegister(this->value(index), reg_kind);
        static const AddressingMode kMn_modes[] = {kMode_MR, kMode_MR1,
                                                   kMode_M4, kMode_M8};
        mode = kMn_modes[scale_exponent];
        if (mode == kMode_MR1) {
          // [%r1 + %r1*1] has a smaller encoding than [%r1*2+0]
          inputs[(*input_count)++] = UseRegister(this->value(index), reg_kind);
        }
      }
    }
    return mode;
  }

  AddressingMode GenerateMemoryOperandInputs(
      Node* index, int scale_exponent, Node* base, Node* displacement,
      DisplacementMode displacement_mode, InstructionOperand inputs[],
      size_t* input_count,
      RegisterUseKind reg_kind = RegisterUseKind::kUseRegister) {
    if constexpr (Adapter::IsTurboshaft) {
      // Turboshaft is not using this overload.
      UNREACHABLE();
    } else {
      int64_t displacement_value;
      if (displacement == nullptr) {
        displacement_value = 0;
      } else if (displacement->opcode() == IrOpcode::kInt32Constant) {
        displacement_value = OpParameter<int32_t>(displacement->op());
      } else if (displacement->opcode() == IrOpcode::kInt64Constant) {
        displacement_value = OpParameter<int64_t>(displacement->op());
      } else {
        UNREACHABLE();
      }
      return GenerateMemoryOperandInputs(index, scale_exponent, base,
                                         displacement_value, displacement_mode,
                                         inputs, input_count, reg_kind);
    }
  }

  AddressingMode GetEffectiveAddressMemoryOperand(
      node_t operand, InstructionOperand inputs[], size_t* input_count,
      RegisterUseKind reg_kind = RegisterUseKind::kUseRegister);

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
    return !selector()->IsReallyLive(node);
  }
};

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

}  // namespace

template <>
AddressingMode
X64OperandGeneratorT<TurboshaftAdapter>::GetEffectiveAddressMemoryOperand(
    turboshaft::OpIndex operand, InstructionOperand inputs[],
    size_t* input_count, RegisterUseKind reg_kind) {
  using namespace turboshaft;  // NOLINT(build/namespaces)

  const Operation& op = Get(operand);
  if (op.Is<LoadOp>() || op.Is<StoreOp>()) {
    LoadStoreView load_or_store(op);
    if (ExternalReference reference;
        MatchExternalConstant(load_or_store.base, &reference) &&
        !load_or_store.index.valid()) {
      if (selector()->CanAddressRelativeToRootsRegister(reference)) {
        const ptrdiff_t delta =
            load_or_store.offset +
            MacroAssemblerBase::RootRegisterOffsetForExternalReference(
                selector()->isolate(), reference);
        if (is_int32(delta)) {
          inputs[(*input_count)++] = TempImmediate(static_cast<int32_t>(delta));
          return kMode_Root;
        }
      }
    }
  }

  auto m = TryMatchBaseWithScaledIndexAndDisplacement64(selector(), operand);
  DCHECK(m.has_value());
  if (IsCompressed(selector(), m->base)) {
    DCHECK(!m->index.valid());
    DCHECK(m->displacement == 0 || ValueFitsIntoImmediate(m->displacement));
    AddressingMode mode = kMode_MCR;
    inputs[(*input_count)++] = UseRegister(m->base, reg_kind);
    if (m->displacement != 0) {
      inputs[(*input_count)++] =
          m->displacement_mode == kNegativeDisplacement
              ? UseImmediate(static_cast<int>(-m->displacement))
              : UseImmediate(static_cast<int>(m->displacement));
      mode = kMode_MCRI;
    }
    return mode;
  }
  if (TurboshaftAdapter::valid(m->base) &&
      this->Get(m->base).Is<turboshaft::LoadRootRegisterOp>()) {
    DCHECK(!this->valid(m->index));
    DCHECK_EQ(m->scale, 0);
    DCHECK(ValueFitsIntoImmediate(m->displacement));
    inputs[(*input_count)++] = UseImmediate(static_cast<int>(m->displacement));
    return kMode_Root;
  } else if (ValueFitsIntoImmediate(m->displacement)) {
    return GenerateMemoryOperandInputs(m->index, m->scale, m->base,
                                       m->displacement, m->displacement_mode,
                                       inputs, input_count, reg_kind);
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
    inputs[(*input_count)++] = UseRegister(m->base, reg_kind);
    inputs[(*input_count)++] = UseRegister(m->index, reg_kind);
    return kMode_MR1;
  }
}

template <>
AddressingMode
X64OperandGeneratorT<TurbofanAdapter>::GetEffectiveAddressMemoryOperand(
    node_t operand, InstructionOperand inputs[], size_t* input_count,
    RegisterUseKind reg_kind) {
  {
    LoadMatcher<ExternalReferenceMatcher> m(operand);
    if (m.index().HasResolvedValue() && m.object().HasResolvedValue() &&
        selector()->CanAddressRelativeToRootsRegister(
            m.object().ResolvedValue())) {
      ptrdiff_t const delta =
          m.index().ResolvedValue() +
          MacroAssemblerBase::RootRegisterOffsetForExternalReference(
              selector()->isolate(), m.object().ResolvedValue());
      if (is_int32(delta)) {
        inputs[(*input_count)++] = TempImmediate(static_cast<int32_t>(delta));
        return kMode_Root;
      }
    }
  }
  BaseWithIndexAndDisplacement64Matcher m(operand, AddressOption::kAllowAll);
  DCHECK(m.matches());
  // Decompress pointer by complex addressing mode.
  if (IsCompressed(m.base())) {
    DCHECK(m.index() == nullptr);
    DCHECK(m.displacement() == nullptr || CanBeImmediate(m.displacement()));
    AddressingMode mode = kMode_MCR;
    inputs[(*input_count)++] = UseRegister(m.base(), reg_kind);
    if (m.displacement() != nullptr) {
      inputs[(*input_count)++] = m.displacement_mode() == kNegativeDisplacement
                                     ? UseNegatedImmediate(m.displacement())
                                     : UseImmediate(m.displacement());
      mode = kMode_MCRI;
    }
    return mode;
  }
  if (m.base() != nullptr &&
      m.base()->opcode() == IrOpcode::kLoadRootRegister) {
    DCHECK_EQ(m.index(), nullptr);
    DCHECK_EQ(m.scale(), 0);
    inputs[(*input_count)++] = UseImmediate(m.displacement());
    return kMode_Root;
  } else if (m.displacement() == nullptr || CanBeImmediate(m.displacement())) {
    return GenerateMemoryOperandInputs(m.index(), m.scale(), m.base(),
                                       m.displacement(), m.displacement_mode(),
                                       inputs, input_count, reg_kind);
  } else if (m.base() == nullptr &&
             m.displacement_mode() == kPositiveDisplacement) {
    // The displacement cannot be an immediate, but we can use the
    // displacement as base instead and still benefit from addressing
    // modes for the scale.
    return GenerateMemoryOperandInputs(m.index(), m.scale(), m.displacement(),
                                       nullptr, m.displacement_mode(), inputs,
                                       input_count, reg_kind);
  } else {
    inputs[(*input_count)++] = UseRegister(operand->InputAt(0), reg_kind);
    inputs[(*input_count)++] = UseRegister(operand->InputAt(1), reg_kind);
    return kMode_MR1;
  }
}

namespace {

ArchOpcode GetLoadOpcode(turboshaft::MemoryRepresentation loaded_rep,
                         turboshaft::RegisterRepresentation result_rep) {
  // NOTE: The meaning of `loaded_rep` = `MemoryRepresentation::AnyTagged()` is
  // we are loading a compressed tagged field, while `result_rep` =
  // `RegisterRepresentation::Tagged()` refers to an uncompressed tagged value.
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (loaded_rep) {
    case MemoryRepresentation::Int8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return kX64Movsxbl;
    case MemoryRepresentation::Uint8():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return kX64Movzxbl;
    case MemoryRepresentation::Int16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return kX64Movsxwl;
    case MemoryRepresentation::Uint16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return kX64Movzxwl;
    case MemoryRepresentation::Int32():
    case MemoryRepresentation::Uint32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word32());
      return kX64Movl;
    case MemoryRepresentation::Int64():
    case MemoryRepresentation::Uint64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Word64());
      return kX64Movq;
    case MemoryRepresentation::Float16():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float32());
      return kX64Movsh;
    case MemoryRepresentation::Float32():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float32());
      return kX64Movss;
    case MemoryRepresentation::Float64():
      DCHECK_EQ(result_rep, RegisterRepresentation::Float64());
      return kX64Movsd;
#ifdef V8_COMPRESS_POINTERS
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
      if (result_rep == RegisterRepresentation::Compressed()) {
        return kX64Movl;
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return kX64MovqDecompressTagged;
    case MemoryRepresentation::TaggedSigned():
      if (result_rep == RegisterRepresentation::Compressed()) {
        return kX64Movl;
      }
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return kX64MovqDecompressTaggedSigned;
#else
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
    case MemoryRepresentation::TaggedSigned():
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return kX64Movq;
#endif
    case MemoryRepresentation::AnyUncompressedTagged():
    case MemoryRepresentation::UncompressedTaggedPointer():
    case MemoryRepresentation::UncompressedTaggedSigned():
      DCHECK_EQ(result_rep, RegisterRepresentation::Tagged());
      return kX64Movq;
    case MemoryRepresentation::ProtectedPointer():
      CHECK(V8_ENABLE_SANDBOX_BOOL);
      return kX64MovqDecompressProtected;
    case MemoryRepresentation::IndirectPointer():
      UNREACHABLE();
    case MemoryRepresentation::SandboxedPointer():
      return kX64MovqDecodeSandboxedPointer;
    case MemoryRepresentation::Simd128():
      DCHECK_EQ(result_rep, RegisterRepresentation::Simd128());
      return kX64Movdqu;
    case MemoryRepresentation::Simd256():
      DCHECK_EQ(result_rep, RegisterRepresentation::Simd256());
      return kX64Movdqu256;
  }
}

ArchOpcode GetLoadOpcode(LoadRepresentation load_rep) {
  ArchOpcode opcode;
  switch (load_rep.representation()) {
    case MachineRepresentation::kFloat16:
      opcode = kX64Movsh;
      break;
    case MachineRepresentation::kFloat32:
      opcode = kX64Movss;
      break;
    case MachineRepresentation::kFloat64:
      opcode = kX64Movsd;
      break;
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsSigned() ? kX64Movsxbl : kX64Movzxbl;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsSigned() ? kX64Movsxwl : kX64Movzxwl;
      break;
    case MachineRepresentation::kWord32:
      opcode = kX64Movl;
      break;
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
      opcode = kX64Movl;
      break;
#else
      UNREACHABLE();
#endif
#ifdef V8_COMPRESS_POINTERS
    case MachineRepresentation::kTaggedSigned:
      opcode = kX64MovqDecompressTaggedSigned;
      break;
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      opcode = kX64MovqDecompressTagged;
      break;
#else
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:         // Fall through.
#endif
    case MachineRepresentation::kWord64:
      opcode = kX64Movq;
      break;
    case MachineRepresentation::kProtectedPointer:
      CHECK(V8_ENABLE_SANDBOX_BOOL);
      opcode = kX64MovqDecompressProtected;
      break;
    case MachineRepresentation::kSandboxedPointer:
      opcode = kX64MovqDecodeSandboxedPointer;
      break;
    case MachineRepresentation::kSimd128:
      opcode = kX64Movdqu;
      break;
    case MachineRepresentation::kSimd256:  // Fall through.
      opcode = kX64Movdqu256;
      break;
    case MachineRepresentation::kNone:     // Fall through.
    case MachineRepresentation::kMapWord:  // Fall through.
    case MachineRepresentation::kIndirectPointer:  // Fall through.
      UNREACHABLE();
  }
  return opcode;
}

ArchOpcode GetStoreOpcode(turboshaft::MemoryRepresentation stored_rep) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  switch (stored_rep) {
    case MemoryRepresentation::Int8():
    case MemoryRepresentation::Uint8():
      return kX64Movb;
    case MemoryRepresentation::Int16():
    case MemoryRepresentation::Uint16():
      return kX64Movw;
    case MemoryRepresentation::Int32():
    case MemoryRepresentation::Uint32():
      return kX64Movl;
    case MemoryRepresentation::Int64():
    case MemoryRepresentation::Uint64():
      return kX64Movq;
    case MemoryRepresentation::Float16():
      return kX64Movsh;
    case MemoryRepresentation::Float32():
      return kX64Movss;
    case MemoryRepresentation::Float64():
      return kX64Movsd;
    case MemoryRepresentation::AnyTagged():
    case MemoryRepresentation::TaggedPointer():
    case MemoryRepresentation::TaggedSigned():
      return kX64MovqCompressTagged;
    case MemoryRepresentation::AnyUncompressedTagged():
    case MemoryRepresentation::UncompressedTaggedPointer():
    case MemoryRepresentation::UncompressedTaggedSigned():
      return kX64Movq;
    case MemoryRepresentation::ProtectedPointer():
      // We never store directly to protected pointers from generated code.
      UNREACHABLE();
    case MemoryRepresentation::IndirectPointer():
      return kX64MovqStoreIndirectPointer;
    case MemoryRepresentation::SandboxedPointer():
      return kX64MovqEncodeSandboxedPointer;
    case MemoryRepresentation::Simd128():
      return kX64Movdqu;
    case MemoryRepresentation::Simd256():
      return kX64Movdqu256;
  }
}

ArchOpcode GetStoreOpcode(StoreRepresentation store_rep) {
  switch (store_rep.representation()) {
    case MachineRepresentation::kFloat16:
      return kX64Movsh;
    case MachineRepresentation::kFloat32:
      return kX64Movss;
    case MachineRepresentation::kFloat64:
      return kX64Movsd;
    case MachineRepresentation::kBit:
    case MachineRepresentation::kWord8:
      return kX64Movb;
    case MachineRepresentation::kWord16:
      return kX64Movw;
    case MachineRepresentation::kWord32:
      return kX64Movl;
    case MachineRepresentation::kCompressedPointer:
    case MachineRepresentation::kCompressed:
#ifdef V8_COMPRESS_POINTERS
      return kX64MovqCompressTagged;
#else
      UNREACHABLE();
#endif
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
      return kX64MovqCompressTagged;
    case MachineRepresentation::kWord64:
      return kX64Movq;
    case MachineRepresentation::kIndirectPointer:
      return kX64MovqStoreIndirectPointer;
    case MachineRepresentation::kSandboxedPointer:
      return kX64MovqEncodeSandboxedPointer;
    case MachineRepresentation::kSimd128:
      return kX64Movdqu;
    case MachineRepresentation::kSimd256:
      return kX64Movdqu256;
    case MachineRepresentation::kNone:
    case MachineRepresentation::kMapWord:
    case MachineRepresentation::kProtectedPointer:
      // We never store directly to protected pointers from generated code.
      UNREACHABLE();
  }
}

ArchOpcode GetSeqCstStoreOpcode(StoreRepresentation store_rep) {
  switch (store_rep.representation()) {
    case MachineRepresentation::kWord8:
      return kAtomicStoreWord8;
    case MachineRepresentation::kWord16:
      return kAtomicStoreWord16;
    case MachineRepresentation::kWord32:
      return kAtomicStoreWord32;
    case MachineRepresentation::kWord64:
      return kX64Word64AtomicStoreWord64;
    case MachineRepresentation::kTaggedSigned:   // Fall through.
    case MachineRepresentation::kTaggedPointer:  // Fall through.
    case MachineRepresentation::kTagged:
      if (COMPRESS_POINTERS_BOOL) return kAtomicStoreWord32;
      return kX64Word64AtomicStoreWord64;
    case MachineRepresentation::kCompressedPointer:  // Fall through.
    case MachineRepresentation::kCompressed:
      CHECK(COMPRESS_POINTERS_BOOL);
      return kAtomicStoreWord32;
    default:
      UNREACHABLE();
  }
}

// Used for pmin/pmax and relaxed min/max.
template <typename Adapter, VectorLength vec_len>
void VisitMinOrMax(InstructionSelectorT<Adapter>* selector,
                   typename Adapter::node_t node, ArchOpcode opcode,
                   bool flip_inputs) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  InstructionOperand dst = selector->IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  InstructionCode instr_code = opcode | VectorLengthField::encode(vec_len);
  if (flip_inputs) {
    // Due to the way minps/minpd work, we want the dst to be same as the second
    // input: b = pmin(a, b) directly maps to minps b a.
    selector->Emit(instr_code, dst, g.UseRegister(selector->input_at(node, 1)),
                   g.UseRegister(selector->input_at(node, 0)));
  } else {
    selector->Emit(instr_code, dst, g.UseRegister(selector->input_at(node, 0)),
                   g.UseRegister(selector->input_at(node, 1)));
  }
}
}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTraceInstruction(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // Currently not used by Turboshaft.
    UNIMPLEMENTED();
  } else {
    X64OperandGeneratorT<Adapter> g(this);
    uint32_t markid = OpParameter<uint32_t>(node->op());
    Emit(kX64TraceInstruction, g.Use(node), g.UseImmediate(markid));
  }
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
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kArchAbortCSADcheck, g.NoOutput(),
       g.UseFixed(this->input_at(node, 0), rdx));
}

#ifdef V8_ENABLE_WEBASSEMBLY
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoadLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd128LaneMemoryOp& load = this->Get(node).Cast<Simd128LaneMemoryOp>();
  InstructionCode opcode = kArchNop;
  switch (load.lane_kind) {
    case Simd128LaneMemoryOp::LaneKind::k8:
      opcode = kX64Pinsrb;
      break;
    case Simd128LaneMemoryOp::LaneKind::k16:
      opcode = kX64Pinsrw;
      break;
    case Simd128LaneMemoryOp::LaneKind::k32:
      opcode = kX64Pinsrd;
      break;
    case Simd128LaneMemoryOp::LaneKind::k64:
      opcode = kX64Pinsrq;
      break;
  }

  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  // Input 0 is value node, 1 is lane idx, and GetEffectiveAddressMemoryOperand
  // uses up to 3 inputs. This ordering is consistent with other operations that
  // use the same opcode.
  InstructionOperand inputs[5];
  size_t input_count = 0;

  inputs[input_count++] = g.UseRegister(load.value());
  inputs[input_count++] = g.UseImmediate(load.lane);

  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(mode);

  DCHECK_GE(5, input_count);

  // x64 supports unaligned loads.
  DCHECK(!load.kind.maybe_unaligned);
  if (load.kind.with_trap_handler) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  Emit(opcode, 1, outputs, input_count, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadLane(Node* node) {
  LoadLaneParameters params = LoadLaneParametersOf(node->op());
  InstructionCode opcode = kArchNop;
  if (params.rep == MachineType::Int8()) {
    opcode = kX64Pinsrb;
  } else if (params.rep == MachineType::Int16()) {
    opcode = kX64Pinsrw;
  } else if (params.rep == MachineType::Int32()) {
    opcode = kX64Pinsrd;
  } else if (params.rep == MachineType::Int64()) {
    opcode = kX64Pinsrq;
  } else {
    UNREACHABLE();
  }

  X64OperandGeneratorT<TurbofanAdapter> g(this);
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  // Input 0 is value node, 1 is lane idx, and GetEffectiveAddressMemoryOperand
  // uses up to 3 inputs. This ordering is consistent with other operations that
  // use the same opcode.
  InstructionOperand inputs[5];
  size_t input_count = 0;

  inputs[input_count++] = g.UseRegister(node->InputAt(2));
  inputs[input_count++] = g.UseImmediate(params.laneidx);

  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(mode);

  DCHECK_GE(5, input_count);

  // x64 supports unaligned loads.
  DCHECK_NE(params.kind, MemoryAccessKind::kUnaligned);
  if (params.kind == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
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
      opcode = kX64S128Load8x8S;
      break;
    case Simd128LoadTransformOp::TransformKind::k8x8U:
      opcode = kX64S128Load8x8U;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4S:
      opcode = kX64S128Load16x4S;
      break;
    case Simd128LoadTransformOp::TransformKind::k16x4U:
      opcode = kX64S128Load16x4U;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2S:
      opcode = kX64S128Load32x2S;
      break;
    case Simd128LoadTransformOp::TransformKind::k32x2U:
      opcode = kX64S128Load32x2U;
      break;
    case Simd128LoadTransformOp::TransformKind::k8Splat:
      opcode = kX64S128Load8Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k16Splat:
      opcode = kX64S128Load16Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Splat:
      opcode = kX64S128Load32Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Splat:
      opcode = kX64S128Load64Splat;
      break;
    case Simd128LoadTransformOp::TransformKind::k32Zero:
      opcode = kX64Movss;
      break;
    case Simd128LoadTransformOp::TransformKind::k64Zero:
      opcode = kX64Movsd;
      break;
  }

  // x64 supports unaligned loads
  DCHECK(!op.load_kind.maybe_unaligned);
  InstructionCode code = opcode;
  if (op.load_kind.with_trap_handler) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  VisitLoad(node, node, code);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoadTransform(Node* node) {
  LoadTransformParameters params = LoadTransformParametersOf(node->op());
  ArchOpcode opcode;
  switch (params.transformation) {
    case LoadTransformation::kS128Load8Splat:
      opcode = kX64S128Load8Splat;
      break;
    case LoadTransformation::kS128Load16Splat:
      opcode = kX64S128Load16Splat;
      break;
    case LoadTransformation::kS128Load32Splat:
      opcode = kX64S128Load32Splat;
      break;
    case LoadTransformation::kS128Load64Splat:
      opcode = kX64S128Load64Splat;
      break;
    case LoadTransformation::kS128Load8x8S:
      opcode = kX64S128Load8x8S;
      break;
    case LoadTransformation::kS128Load8x8U:
      opcode = kX64S128Load8x8U;
      break;
    case LoadTransformation::kS128Load16x4S:
      opcode = kX64S128Load16x4S;
      break;
    case LoadTransformation::kS128Load16x4U:
      opcode = kX64S128Load16x4U;
      break;
    case LoadTransformation::kS128Load32x2S:
      opcode = kX64S128Load32x2S;
      break;
    case LoadTransformation::kS128Load32x2U:
      opcode = kX64S128Load32x2U;
      break;
    case LoadTransformation::kS128Load32Zero:
      opcode = kX64Movss;
      break;
    case LoadTransformation::kS128Load64Zero:
      opcode = kX64Movsd;
      break;
    // Simd256
    case LoadTransformation::kS256Load8Splat:
      opcode = kX64S256Load8Splat;
      break;
    case LoadTransformation::kS256Load16Splat:
      opcode = kX64S256Load16Splat;
      break;
    case LoadTransformation::kS256Load32Splat:
      opcode = kX64S256Load32Splat;
      break;
    case LoadTransformation::kS256Load64Splat:
      opcode = kX64S256Load64Splat;
      break;
    case LoadTransformation::kS256Load8x16S:
      opcode = kX64S256Load8x16S;
      break;
    case LoadTransformation::kS256Load8x16U:
      opcode = kX64S256Load8x16U;
      break;
    case LoadTransformation::kS256Load16x8S:
      opcode = kX64S256Load16x8S;
      break;
    case LoadTransformation::kS256Load16x8U:
      opcode = kX64S256Load16x8U;
      break;
    case LoadTransformation::kS256Load32x4S:
      opcode = kX64S256Load32x4S;
      break;
    case LoadTransformation::kS256Load32x4U:
      opcode = kX64S256Load32x4U;
      break;
    default:
      UNREACHABLE();
  }
  // x64 supports unaligned loads
  DCHECK_NE(params.kind, MemoryAccessKind::kUnaligned);
  InstructionCode code = opcode;
  if (params.kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  VisitLoad(node, node, code);
}

#if V8_ENABLE_WASM_SIMD256_REVEC
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS256Const(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  static const int kUint32Immediates = kSimd256Size / sizeof(uint32_t);
  uint32_t val[kUint32Immediates];
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd256ConstantOp& constant =
        this->Get(node).template Cast<turboshaft::Simd256ConstantOp>();
    memcpy(val, constant.value, kSimd256Size);
  } else {
    memcpy(val, S256ImmediateParameterOf(node->op()).data(), kSimd256Size);
  }
  // If all bytes are zeros or ones, avoid emitting code for generic constants
  bool all_zeros = std::all_of(std::begin(val), std::end(val),
                               [](uint32_t v) { return v == 0; });
  // It's should not happen for Turboshaft, IsZero is checked earlier in
  // instruction selector
  DCHECK_IMPLIES(Adapter::IsTurboshaft, !all_zeros);
  bool all_ones = std::all_of(std::begin(val), std::end(val),
                              [](uint32_t v) { return v == UINT32_MAX; });
  InstructionOperand dst = g.DefineAsRegister(node);
  if (all_zeros) {
    Emit(kX64SZero | VectorLengthField::encode(kV256), dst);
  } else if (all_ones) {
    Emit(kX64SAllOnes | VectorLengthField::encode(kV256), dst);
  } else {
    Emit(kX64S256Const, dst, g.UseImmediate(val[0]), g.UseImmediate(val[1]),
         g.UseImmediate(val[2]), g.UseImmediate(val[3]), g.UseImmediate(val[4]),
         g.UseImmediate(val[5]), g.UseImmediate(val[6]),
         g.UseImmediate(val[7]));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS256Zero(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  Emit(kX64SZero | VectorLengthField::encode(kV256), g.DefineAsRegister(node));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSimd256LoadTransform(
    Node* node) {
  // For Turbofan, VisitLoadTransform should be called instead.
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSimd256LoadTransform(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const Simd256LoadTransformOp& op =
      this->Get(node).Cast<Simd256LoadTransformOp>();
  ArchOpcode opcode;
  switch (op.transform_kind) {
    case Simd256LoadTransformOp::TransformKind::k8x16S:
      opcode = kX64S256Load8x16S;
      break;
    case Simd256LoadTransformOp::TransformKind::k8x16U:
      opcode = kX64S256Load8x16U;
      break;
    case Simd256LoadTransformOp::TransformKind::k16x8S:
      opcode = kX64S256Load16x8S;
      break;
    case Simd256LoadTransformOp::TransformKind::k16x8U:
      opcode = kX64S256Load16x8U;
      break;
    case Simd256LoadTransformOp::TransformKind::k32x4S:
      opcode = kX64S256Load32x4S;
      break;
    case Simd256LoadTransformOp::TransformKind::k32x4U:
      opcode = kX64S256Load32x4U;
      break;
    case Simd256LoadTransformOp::TransformKind::k8Splat:
      opcode = kX64S256Load8Splat;
      break;
    case Simd256LoadTransformOp::TransformKind::k16Splat:
      opcode = kX64S256Load16Splat;
      break;
    case Simd256LoadTransformOp::TransformKind::k32Splat:
      opcode = kX64S256Load32Splat;
      break;
    case Simd256LoadTransformOp::TransformKind::k64Splat:
      opcode = kX64S256Load64Splat;
      break;
  }

  // x64 supports unaligned loads
  DCHECK(!op.load_kind.maybe_unaligned);
  InstructionCode code = opcode;
  if (op.load_kind.with_trap_handler) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  VisitLoad(node, node, code);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF32x8RelaxedMin(Node* node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF32x8RelaxedMax(Node* node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF64x4RelaxedMin(Node* node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitF64x4RelaxedMax(Node* node) {
  UNREACHABLE();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF32x8RelaxedMin(
    node_t node) {
  VisitMinOrMax<TurboshaftAdapter, kV256>(this, node, kX64Minps, false);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF32x8RelaxedMax(
    node_t node) {
  VisitMinOrMax<TurboshaftAdapter, kV256>(this, node, kX64Maxps, false);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF64x4RelaxedMin(
    node_t node) {
  VisitMinOrMax<TurboshaftAdapter, kV256>(this, node, kX64Minpd, false);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitF64x4RelaxedMax(
    node_t node) {
  VisitMinOrMax<TurboshaftAdapter, kV256>(this, node, kX64Maxpd, false);
}

#ifdef V8_TARGET_ARCH_X64
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSimd256Shufd(Node* node) {
  UNIMPLEMENTED();
}
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSimd256Shufps(Node* node) {
  UNIMPLEMENTED();
}
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSimd256Unpack(Node* node) {
  UNIMPLEMENTED();
}
template <>
void InstructionSelectorT<TurbofanAdapter>::VisitSimdPack128To256(Node* node) {
  UNIMPLEMENTED();
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSimd256Shufd(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const turboshaft::Simd256ShufdOp& shufd =
      Get(node).Cast<turboshaft::Simd256ShufdOp>();
  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionOperand src = g.UseUniqueRegister(shufd.input());
  InstructionOperand imm = g.UseImmediate(shufd.control);
  InstructionOperand inputs[] = {src, imm};
  Emit(kX64Vpshufd, 1, &dst, 2, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSimd256Shufps(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const turboshaft::Simd256ShufpsOp& shufps =
      Get(node).Cast<turboshaft::Simd256ShufpsOp>();
  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionOperand src1 = g.UseUniqueRegister(shufps.left());
  InstructionOperand src2 = g.UseUniqueRegister(shufps.right());
  InstructionOperand imm = g.UseImmediate(shufps.control);
  InstructionOperand inputs[] = {src1, src2, imm};
  Emit(kX64Shufps, 1, &dst, 3, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSimd256Unpack(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const turboshaft::Simd256UnpackOp& unpack =
      Get(node).Cast<turboshaft::Simd256UnpackOp>();
  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionOperand src1 = g.UseUniqueRegister(unpack.left());
  InstructionOperand src2 = g.UseUniqueRegister(unpack.right());
  InstructionOperand inputs[] = {src1, src2};
  ArchOpcode code;
  switch (unpack.kind) {
    case turboshaft::Simd256UnpackOp::Kind::k32x8High:
      code = kX64S32x8UnpackHigh;
      break;
    case turboshaft::Simd256UnpackOp::Kind::k32x8Low:
      code = kX64S32x8UnpackLow;
      break;
    default:
      UNIMPLEMENTED();
  }
  Emit(code, 1, &dst, 2, inputs);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitSimdPack128To256(
    node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);

  const turboshaft::SimdPack128To256Op& op =
      Get(node).Cast<turboshaft::SimdPack128To256Op>();

  turboshaft::OpIndex input0 = op.input(0);
  turboshaft::OpIndex input1 = op.input(1);
  constexpr int kHighLaneIndex = 1;

  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionOperand src0 = g.UseUniqueRegister(input0);
  InstructionOperand src1 = g.UseUniqueRegister(input1);
  InstructionOperand imm = g.UseImmediate(kHighLaneIndex);

  InstructionOperand inputs[] = {src0, src1, imm};

  Emit(kX64InsertI128, 1, &dst, 3, inputs);
}
#endif  // V8_TARGET_ARCH_X64

#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif  // V8_ENABLE_WEBASSEMBLY

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitLoad(node_t node, node_t value,
                                              InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(this);
#ifdef V8_IS_TSAN
  // On TSAN builds we require one scratch register. Because of this we also
  // have to modify the inputs to take into account possible aliasing and use
  // UseUniqueRegister which is not required for non-TSAN builds.
  InstructionOperand temps[] = {g.TempRegister()};
  size_t temp_count = arraysize(temps);
  auto reg_kind = OperandGenerator::RegisterUseKind::kUseUniqueRegister;
#else
  InstructionOperand* temps = nullptr;
  size_t temp_count = 0;
  auto reg_kind = OperandGenerator::RegisterUseKind::kUseRegister;
#endif  // V8_IS_TSAN
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  InstructionOperand inputs[3];
  size_t input_count = 0;
  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(value, inputs, &input_count, reg_kind);
  InstructionCode code = opcode | AddressingModeField::encode(mode);
  if (this->is_load(node)) {
    auto load = this->load_view(node);
    bool traps_on_null;
    if (load.is_protected(&traps_on_null)) {
      if (traps_on_null) {
        code |= AccessModeField::encode(kMemoryAccessProtectedNullDereference);
      } else {
        code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
      }
    }
  }
  Emit(code, 1, outputs, input_count, inputs, temp_count, temps);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitLoad(node_t node) {
  LoadRepresentation load_rep = this->load_view(node).loaded_rep();
  DCHECK(!load_rep.IsMapWord());
  VisitLoad(node, node, GetLoadOpcode(load_rep));
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitLoad(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  TurboshaftAdapter::LoadView view = this->load_view(node);
  VisitLoad(node, node,
            GetLoadOpcode(view.ts_loaded_rep(), view.ts_result_rep()));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedLoad(node_t node) {
  VisitLoad(node);
}

namespace {

// Shared routine for Word32/Word64 Atomic Exchange
template <typename Adapter>
void VisitAtomicExchange(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node, ArchOpcode opcode,
                         AtomicWidth width, MemoryAccessKind access_kind) {
  auto atomic_op = selector->atomic_rmw_view(node);
  X64OperandGeneratorT<Adapter> g(selector);
  AddressingMode addressing_mode;
  InstructionOperand inputs[] = {
      g.UseUniqueRegister(atomic_op.value()),
      g.UseUniqueRegister(atomic_op.base()),
      g.GetEffectiveIndexOperand(atomic_op.index(), &addressing_mode)};
  InstructionOperand outputs[] = {g.DefineSameAsFirst(node)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs);
}

template <typename Adapter>
void VisitStoreCommon(InstructionSelectorT<Adapter>* selector,
                      const typename Adapter::StoreView& store) {
  using node_t = typename Adapter::node_t;
  using optional_node_t = typename Adapter::optional_node_t;
  X64OperandGeneratorT<Adapter> g(selector);
  node_t base = store.base();
  optional_node_t index = store.index();
  node_t value = store.value();
  int32_t displacement = store.displacement();
  uint8_t element_size_log2 = store.element_size_log2();
  std::optional<AtomicMemoryOrder> atomic_order = store.memory_order();
  MemoryAccessKind acs_kind = store.access_kind();

  const StoreRepresentation store_rep = store.stored_rep();
  DCHECK_NE(store_rep.representation(), MachineRepresentation::kMapWord);
  WriteBarrierKind write_barrier_kind = store_rep.write_barrier_kind();
  const bool is_seqcst =
      atomic_order && *atomic_order == AtomicMemoryOrder::kSeqCst;

  if (v8_flags.enable_unconditional_write_barriers &&
      CanBeTaggedOrCompressedPointer(store_rep.representation())) {
    write_barrier_kind = kFullWriteBarrier;
  }

  const auto access_mode =
      acs_kind == MemoryAccessKind::kProtected
          ? (store.is_store_trap_on_null()
                 ? kMemoryAccessProtectedNullDereference
                 : MemoryAccessMode::kMemoryAccessProtectedMemOutOfBounds)
          : MemoryAccessMode::kMemoryAccessDirect;

  if (write_barrier_kind != kNoWriteBarrier &&
      !v8_flags.disable_write_barriers) {
    DCHECK(
        CanBeTaggedOrCompressedOrIndirectPointer(store_rep.representation()));
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      // Uncompressed stores should not happen if we need a write barrier.
      CHECK((store.ts_stored_rep() !=
             MemoryRepresentation::AnyUncompressedTagged()) &&
            (store.ts_stored_rep() !=
             MemoryRepresentation::UncompressedTaggedPointer()) &&
            (store.ts_stored_rep() !=
             MemoryRepresentation::UncompressedTaggedPointer()));
    }
    AddressingMode addressing_mode;
    InstructionOperand inputs[5];
    size_t input_count = 0;
    addressing_mode = g.GenerateMemoryOperandInputs(
        index, element_size_log2, base, displacement,
        DisplacementMode::kPositiveDisplacement, inputs, &input_count,
        X64OperandGeneratorT<Adapter>::RegisterUseKind::kUseUniqueRegister);
    DCHECK_LT(input_count, 4);
    inputs[input_count++] = g.UseUniqueRegister(value);
    RecordWriteMode record_write_mode =
        WriteBarrierKindToRecordWriteMode(write_barrier_kind);
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    InstructionCode code;
    if (store_rep.representation() == MachineRepresentation::kIndirectPointer) {
      DCHECK_EQ(write_barrier_kind, kIndirectPointerWriteBarrier);
      // In this case we need to add the IndirectPointerTag as additional input.
      code = kArchStoreIndirectWithWriteBarrier;
      IndirectPointerTag tag = store.indirect_pointer_tag();
      inputs[input_count++] = g.UseImmediate64(static_cast<int64_t>(tag));
    } else {
      code = is_seqcst ? kArchAtomicStoreWithWriteBarrier
                       : kArchStoreWithWriteBarrier;
    }
    code |= AddressingModeField::encode(addressing_mode);
    code |= RecordWriteModeField::encode(record_write_mode);
    code |= AccessModeField::encode(access_mode);
    selector->Emit(code, 0, nullptr, input_count, inputs, arraysize(temps),
                   temps);
  } else {
#ifdef V8_IS_TSAN
    // On TSAN builds we require two scratch registers. Because of this we also
    // have to modify the inputs to take into account possible aliasing and use
    // UseUniqueRegister which is not required for non-TSAN builds.
    InstructionOperand temps[] = {g.TempRegister(), g.TempRegister()};
    size_t temp_count = arraysize(temps);
    auto reg_kind =
        OperandGeneratorT<Adapter>::RegisterUseKind::kUseUniqueRegister;
#else
    InstructionOperand* temps = nullptr;
    size_t temp_count = 0;
    auto reg_kind = OperandGeneratorT<Adapter>::RegisterUseKind::kUseRegister;
#endif  // V8_IS_TSAN

    // Release and non-atomic stores emit MOV and sequentially consistent stores
    // emit XCHG.
    // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html

    ArchOpcode opcode;
    AddressingMode addressing_mode;
    InstructionOperand inputs[4];
    size_t input_count = 0;

    if (is_seqcst) {
      // SeqCst stores emit XCHG instead of MOV, so encode the inputs as we
      // would for XCHG. XCHG can't encode the value as an immediate and has
      // fewer addressing modes available.
      inputs[input_count++] = g.UseUniqueRegister(value);
      inputs[input_count++] = g.UseUniqueRegister(base);
      DCHECK_EQ(element_size_log2, 0);
      if (selector->valid(index)) {
        DCHECK_EQ(displacement, 0);
        inputs[input_count++] = g.GetEffectiveIndexOperand(
            selector->value(index), &addressing_mode);
      } else if (displacement != 0) {
        DCHECK(ValueFitsIntoImmediate(displacement));
        inputs[input_count++] = g.UseImmediate(displacement);
        addressing_mode = kMode_MRI;
      } else {
        addressing_mode = kMode_MR;
      }
      opcode = GetSeqCstStoreOpcode(store_rep);
    } else {
      if (ElementSizeLog2Of(store_rep.representation()) <
          kSystemPointerSizeLog2) {
        if (selector->is_truncate_word64_to_word32(value)) {
          value = selector->input_at(value, 0);
        }
      }

      addressing_mode = g.GetEffectiveAddressMemoryOperand(
          store, inputs, &input_count, reg_kind);
      InstructionOperand value_operand = g.CanBeImmediate(value)
                                             ? g.UseImmediate(value)
                                             : g.UseRegister(value, reg_kind);
      inputs[input_count++] = value_operand;
      if constexpr (Adapter::IsTurboshaft) {
        opcode = GetStoreOpcode(store.ts_stored_rep());
      } else {
        opcode = GetStoreOpcode(store_rep);
      }
    }

    InstructionCode code = opcode
      | AddressingModeField::encode(addressing_mode)
      | AccessModeField::encode(access_mode);
    selector->Emit(code, 0, static_cast<InstructionOperand*>(nullptr),
                   input_count, inputs, temp_count, temps);
  }
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStorePair(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitStore(node_t node) {
  return VisitStoreCommon(this, this->store_view(node));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitProtectedStore(node_t node) {
  return VisitStoreCommon(this, this->store_view(node));
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

#ifdef V8_ENABLE_WEBASSEMBLY
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitStoreLane(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const Simd128LaneMemoryOp& store = Get(node).Cast<Simd128LaneMemoryOp>();
  InstructionCode opcode = kArchNop;
  switch (store.lane_kind) {
    case Simd128LaneMemoryOp::LaneKind::k8:
      opcode = kX64Pextrb;
      break;
    case Simd128LaneMemoryOp::LaneKind::k16:
      opcode = kX64Pextrw;
      break;
    case Simd128LaneMemoryOp::LaneKind::k32:
      opcode = kX64S128Store32Lane;
      break;
    case Simd128LaneMemoryOp::LaneKind::k64:
      opcode = kX64S128Store64Lane;
      break;
  }

  InstructionOperand inputs[4];
  size_t input_count = 0;
  AddressingMode addressing_mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(addressing_mode);

  if (store.kind.with_trap_handler) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  InstructionOperand value_operand = g.UseRegister(store.value());
  inputs[input_count++] = value_operand;
  inputs[input_count++] = g.UseImmediate(store.lane);
  DCHECK_GE(4, input_count);
  Emit(opcode, 0, nullptr, input_count, inputs);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitStoreLane(Node* node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);

  StoreLaneParameters params = StoreLaneParametersOf(node->op());
  InstructionCode opcode = kArchNop;
  if (params.rep == MachineRepresentation::kWord8) {
    opcode = kX64Pextrb;
  } else if (params.rep == MachineRepresentation::kWord16) {
    opcode = kX64Pextrw;
  } else if (params.rep == MachineRepresentation::kWord32) {
    opcode = kX64S128Store32Lane;
  } else if (params.rep == MachineRepresentation::kWord64) {
    opcode = kX64S128Store64Lane;
  } else {
    UNREACHABLE();
  }

  InstructionOperand inputs[4];
  size_t input_count = 0;
  AddressingMode addressing_mode =
      g.GetEffectiveAddressMemoryOperand(node, inputs, &input_count);
  opcode |= AddressingModeField::encode(addressing_mode);

  if (params.kind == MemoryAccessKind::kProtected) {
    opcode |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }

  InstructionOperand value_operand = g.UseRegister(node->InputAt(2));
  inputs[input_count++] = value_operand;
  inputs[input_count++] = g.UseImmediate(params.laneidx);
  DCHECK_GE(4, input_count);
  Emit(opcode, 0, nullptr, input_count, inputs);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// Shared routine for multiple binary operations.
template <typename Adapter>
static void VisitBinop(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, InstructionCode opcode,
                       FlagsContinuationT<Adapter>* cont) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  if (selector->IsCommutative(node)) {
    if (selector->is_constant(left) && !selector->is_constant(right)) {
      std::swap(left, right);
    }
  }
  InstructionOperand inputs[8];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  // TODO(turbofan): match complex addressing modes.
  if (left == right) {
    // If both inputs refer to the same operand, enforce allocating a register
    // for both of them to ensure that we don't end up generating code like
    // this:
    //
    //   mov rax, [rbp-0x10]
    //   add rax, [rbp-0x10]
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

  if (cont->IsBranch()) {
    inputs[input_count++] = g.Label(cont->true_block());
    inputs[input_count++] = g.Label(cont->false_block());
  }

  outputs[output_count++] = g.DefineSameAsFirst(node);

  DCHECK_NE(0u, input_count);
  DCHECK_EQ(1u, output_count);
  DCHECK_GE(arraysize(inputs), input_count);
  DCHECK_GE(arraysize(outputs), output_count);

  selector->EmitWithContinuation(opcode, output_count, outputs, input_count,
                                 inputs, cont);
}

// Shared routine for multiple binary operations.
std::optional<int32_t> GetWord32Constant(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    bool allow_implicit_int64_truncation =
        TurbofanAdapter::AllowsImplicitWord64ToWord32Truncation) {
  DCHECK(!allow_implicit_int64_truncation);
  if (node->opcode() != IrOpcode::kInt32Constant) return std::nullopt;
  return OpParameter<int32_t>(node->op());
}

std::optional<int32_t> GetWord32Constant(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex node,
    bool allow_implicit_int64_truncation =
        TurboshaftAdapter::AllowsImplicitWord64ToWord32Truncation) {
  if (auto* constant = selector->Get(node).TryCast<turboshaft::ConstantOp>()) {
    if (constant->kind == turboshaft::ConstantOp::Kind::kWord32) {
      return constant->word32();
    }
    if (allow_implicit_int64_truncation &&
        constant->kind == turboshaft::ConstantOp::Kind::kWord64) {
      return static_cast<int32_t>(constant->word64());
    }
  }
  return std::nullopt;
}

template <typename Adapter>
static void VisitBinop(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, InstructionCode opcode) {
  FlagsContinuationT<Adapter> cont;
  VisitBinop(selector, node, opcode, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32And(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  auto binop = this->word_binop_view(node);
  if (auto c = GetWord32Constant(this, binop.right())) {
    if (*c == 0xFF) {
      if (this->is_load(binop.left())) {
        LoadRepresentation load_rep =
            this->load_view(binop.left()).loaded_rep();
        if (load_rep.representation() == MachineRepresentation::kWord8 &&
            load_rep.IsUnsigned()) {
          EmitIdentity(node);
          return;
        }
      }
      Emit(kX64Movzxbl, g.DefineAsRegister(node), g.Use(binop.left()));
      return;
    } else if (*c == 0xFFFF) {
      if (this->is_load(binop.left())) {
        LoadRepresentation load_rep =
            this->load_view(binop.left()).loaded_rep();
        if ((load_rep.representation() == MachineRepresentation::kWord16 ||
             load_rep.representation() == MachineRepresentation::kWord8) &&
            load_rep.IsUnsigned()) {
          EmitIdentity(node);
          return;
        }
      }
      Emit(kX64Movzxwl, g.DefineAsRegister(node), g.Use(binop.left()));
      return;
    }
  }
  VisitBinop(this, node, kX64And32);
}

std::optional<uint64_t> TryGetRightWordConstant(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node) {
  Uint64BinopMatcher m(node);
  if (!m.right().HasResolvedValue()) return std::nullopt;
  return static_cast<uint64_t>(m.right().ResolvedValue());
}

std::optional<uint64_t> TryGetRightWordConstant(
    InstructionSelectorT<TurboshaftAdapter>* selector,
    turboshaft::OpIndex node) {
  if (const turboshaft::WordBinopOp* binop =
          selector->Get(node).TryCast<turboshaft::WordBinopOp>()) {
    uint64_t value;
    if (selector->MatchUnsignedIntegralConstant(binop->right(), &value)) {
      return value;
    }
  }
  return std::nullopt;
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64And(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if (std::optional<uint64_t> constant = TryGetRightWordConstant(this, node)) {
    auto left = this->input_at(node, 0);
    if (*constant == 0xFF) {
      Emit(kX64Movzxbq, g.DefineAsRegister(node), g.Use(left));
      return;
    } else if (*constant == 0xFFFF) {
      Emit(kX64Movzxwq, g.DefineAsRegister(node), g.Use(left));
      return;
    } else if (*constant == 0xFFFFFFFF) {
      Emit(kX64Movl, g.DefineAsRegister(node), g.Use(left));
      return;
    } else if (std::numeric_limits<uint32_t>::min() <= *constant &&
               *constant <= std::numeric_limits<uint32_t>::max()) {
      Emit(kX64And32, g.DefineSameAsFirst(node), g.UseRegister(left),
           g.UseImmediate(static_cast<int32_t>(*constant)));
      return;
    }
  }
  VisitBinop(this, node, kX64And);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Or(node_t node) {
  VisitBinop(this, node, kX64Or32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Or(node_t node) {
  VisitBinop(this, node, kX64Or);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Xor(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if (std::optional<uint64_t> constant = TryGetRightWordConstant(this, node)) {
    if (*constant == static_cast<uint64_t>(-1)) {
      Emit(kX64Not32, g.DefineSameAsFirst(node),
           g.UseRegister(this->input_at(node, 0)));
      return;
    }
  }
  VisitBinop(this, node, kX64Xor32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Xor(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if (std::optional<uint64_t> constant = TryGetRightWordConstant(this, node)) {
    if (*constant == static_cast<uint64_t>(-1)) {
      Emit(kX64Not, g.DefineSameAsFirst(node),
           g.UseRegister(this->input_at(node, 0)));
      return;
    }
  }
  VisitBinop(this, node, kX64Xor);
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
  InstructionCode opcode =
      kArchStackPointerGreaterThan | MiscField::encode(static_cast<int>(kind));

  int effect_level = GetEffectLevel(node, cont);

  X64OperandGeneratorT<Adapter> g(this);
  node_t value = this->input_at(node, 0);
  if (g.CanBeMemoryOperand(kX64Cmp, node, value, effect_level)) {
    DCHECK(this->IsLoadOrLoadImmutable(value));

    // GetEffectiveAddressMemoryOperand can create at most 3 inputs.
    static constexpr int kMaxInputCount = 3;

    size_t input_count = 0;
    InstructionOperand inputs[kMaxInputCount];
    AddressingMode addressing_mode =
        g.GetEffectiveAddressMemoryOperand(value, inputs, &input_count);
    opcode |= AddressingModeField::encode(addressing_mode);
    DCHECK_LE(input_count, kMaxInputCount);

    EmitWithContinuation(opcode, 0, nullptr, input_count, inputs, cont);
  } else {
    EmitWithContinuation(opcode, g.UseRegister(value), cont);
  }
}

namespace {

template <typename Adapter>
void TryMergeTruncateInt64ToInt32IntoLoad(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t node,
    typename Adapter::node_t load) {
  typename Adapter::LoadView load_view = selector->load_view(load);
  LoadRepresentation load_rep = load_view.loaded_rep();
  MachineRepresentation rep = load_rep.representation();
  InstructionCode opcode;
  switch (rep) {
    case MachineRepresentation::kBit:  // Fall through.
    case MachineRepresentation::kWord8:
      opcode = load_rep.IsSigned() ? kX64Movsxbl : kX64Movzxbl;
      break;
    case MachineRepresentation::kWord16:
      opcode = load_rep.IsSigned() ? kX64Movsxwl : kX64Movzxwl;
      break;
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kWord64:
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kCompressed:  // Fall through.
      opcode = kX64Movl;
      break;
    default:
      UNREACHABLE();
  }
  X64OperandGeneratorT<Adapter> g(selector);
#ifdef V8_IS_TSAN
  // On TSAN builds we require one scratch register. Because of this we also
  // have to modify the inputs to take into account possible aliasing and use
  // UseUniqueRegister which is not required for non-TSAN builds.
  InstructionOperand temps[] = {g.TempRegister()};
  size_t temp_count = arraysize(temps);
  auto reg_kind =
      OperandGeneratorT<Adapter>::RegisterUseKind::kUseUniqueRegister;
#else
  InstructionOperand* temps = nullptr;
  size_t temp_count = 0;
  auto reg_kind = OperandGeneratorT<Adapter>::RegisterUseKind::kUseRegister;
#endif  // V8_IS_TSAN
  InstructionOperand outputs[] = {g.DefineAsRegister(node)};
  size_t input_count = 0;
  InstructionOperand inputs[3];
  AddressingMode mode =
      g.GetEffectiveAddressMemoryOperand(load, inputs, &input_count, reg_kind);
  opcode |= AddressingModeField::encode(mode);

  selector->Emit(opcode, 1, outputs, input_count, inputs, temp_count, temps);
}

// Shared routine for multiple 32-bit shift operations.
// TODO(bmeurer): Merge this with VisitWord64Shift using template magic?
template <typename Adapter>
void VisitWord32Shift(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  if (selector->is_truncate_word64_to_word32(left)) {
    left = selector->input_at(left, 0);
  }

  if (g.CanBeImmediate(right)) {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseImmediate(right));
  } else {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseFixed(right, rcx));
  }
}

// Shared routine for multiple 64-bit shift operations.
// TODO(bmeurer): Merge this with VisitWord32Shift using template magic?
template <typename Adapter>
void VisitWord64Shift(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  if (g.CanBeImmediate(right)) {
    if constexpr (Adapter::IsTurboshaft) {
      // TODO(nicohartmann@): Implement this for Turboshaft.
    } else {
      Int64BinopMatcher m(node);
      if (opcode == kX64Shr && m.left().IsChangeUint32ToUint64() &&
          m.right().HasResolvedValue() && m.right().ResolvedValue() < 32 &&
          m.right().ResolvedValue() >= 0) {
        opcode = kX64Shr32;
        left = left->InputAt(0);
      }
    }
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseImmediate(right));
  } else {
    if constexpr (Adapter::IsTurboshaft) {
      // TODO(nicohartmann@): Implement this for Turboshaft.
    } else {
      Int64BinopMatcher m(node);
      if (m.right().IsWord64And()) {
        Int64BinopMatcher mright(right);
        if (mright.right().Is(0x3F)) {
          right = mright.left().node();
        }
      }
    }
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.UseFixed(right, rcx));
  }
}

// Shared routine for multiple shift operations with continuation.
template <typename Adapter>
bool TryVisitWordShift(InstructionSelectorT<Adapter>* selector,
                       typename Adapter::node_t node, int bits,
                       ArchOpcode opcode, FlagsContinuationT<Adapter>* cont) {
  DCHECK(bits == 32 || bits == 64);
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  // If the shift count is 0, the flags are not affected.
  if (!g.CanBeImmediate(right) ||
      (g.GetImmediateIntegerValue(right) & (bits - 1)) == 0) {
    return false;
  }
  InstructionOperand output = g.DefineSameAsFirst(node);
  InstructionOperand inputs[2];
  inputs[0] = g.UseRegister(left);
  inputs[1] = g.UseImmediate(right);
  selector->EmitWithContinuation(opcode, 1, &output, 2, inputs, cont);
  return true;
}

template <typename Adapter>
void EmitLea(InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
             typename Adapter::node_t result, typename Adapter::node_t index,
             int scale, typename Adapter::node_t base, int64_t displacement,
             DisplacementMode displacement_mode) {
  X64OperandGeneratorT<Adapter> g(selector);

  InstructionOperand inputs[4];
  size_t input_count = 0;
  AddressingMode mode =
      g.GenerateMemoryOperandInputs(index, scale, base, displacement,
                                    displacement_mode, inputs, &input_count);

  DCHECK_NE(0u, input_count);
  DCHECK_GE(arraysize(inputs), input_count);

  InstructionOperand outputs[1];
  outputs[0] = g.DefineAsRegister(result);

  opcode = AddressingModeField::encode(mode) | opcode;

  selector->Emit(opcode, 1, outputs, input_count, inputs);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shl(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    bool plus_one;
    turboshaft::OpIndex index;
    int scale;
    if (MatchScaledIndex(this, node, &index, &scale, &plus_one)) {
      node_t base = plus_one ? index : node_t{};
      EmitLea(this, kX64Lea32, node, index, scale, base, 0,
              kPositiveDisplacement);
      return;
    }
  } else {
    Int32ScaleMatcher m(node, true);
    if (m.matches()) {
      Node* index = node->InputAt(0);
      Node* base = m.power_of_two_plus_one() ? index : nullptr;
      EmitLea(this, kX64Lea32, node, index, m.scale(), base, 0,
              kPositiveDisplacement);
      return;
    }
  }
  VisitWord32Shift(this, node, kX64Shl32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shl(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    // TODO(nicohartmann,dmercadier): Port the Int64ScaleMatcher part of the
    // Turbofan version. This is used in the builtin pipeline.
    const ShiftOp& shift = this->Get(node).template Cast<ShiftOp>();
    OpIndex left = shift.left();
    OpIndex right = shift.right();
    int32_t cst;
    if ((this->Get(left).template Is<Opmask::kChangeUint32ToUint64>() ||
         this->Get(left).template Is<Opmask::kChangeInt32ToInt64>()) &&
        this->MatchIntegralWord32Constant(right, &cst) &&
        base::IsInRange(cst, 32, 63)) {
      // There's no need to sign/zero-extend to 64-bit if we shift out the
      // upper 32 bits anyway.
      Emit(kX64Shl, g.DefineSameAsFirst(node),
           g.UseRegister(this->Get(left).input(0)), g.UseImmediate(right));
      return;
    }
  } else {
    Int64ScaleMatcher m(node, true);
    if (m.matches()) {
      Node* index = node->InputAt(0);
      Node* base = m.power_of_two_plus_one() ? index : nullptr;
      EmitLea(this, kX64Lea, node, index, m.scale(), base, 0,
              kPositiveDisplacement);
      return;
    } else {
      Int64BinopMatcher bm(node);
      if ((bm.left().IsChangeInt32ToInt64() ||
           bm.left().IsChangeUint32ToUint64()) &&
          bm.right().IsInRange(32, 63)) {
        // There's no need to sign/zero-extend to 64-bit if we shift out the
        // upper 32 bits anyway.
        Emit(kX64Shl, g.DefineSameAsFirst(node),
             g.UseRegister(bm.left().node()->InputAt(0)),
             g.UseImmediate(bm.right().node()));
        return;
      }
    }
  }
  VisitWord64Shift(this, node, kX64Shl);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Shr(node_t node) {
  VisitWord32Shift(this, node, kX64Shr32);
}

namespace {

inline AddressingMode AddDisplacementToAddressingMode(AddressingMode mode) {
  switch (mode) {
    case kMode_MR:
      return kMode_MRI;
    case kMode_MR1:
      return kMode_MR1I;
    case kMode_MR2:
      return kMode_MR2I;
    case kMode_MR4:
      return kMode_MR4I;
    case kMode_MR8:
      return kMode_MR8I;
    case kMode_M1:
      return kMode_M1I;
    case kMode_M2:
      return kMode_M2I;
    case kMode_M4:
      return kMode_M4I;
    case kMode_M8:
      return kMode_M8I;
    case kMode_None:
    case kMode_MRI:
    case kMode_MR1I:
    case kMode_MR2I:
    case kMode_MR4I:
    case kMode_MR8I:
    case kMode_M1I:
    case kMode_M2I:
    case kMode_M4I:
    case kMode_M8I:
    case kMode_Root:
    case kMode_MCR:
    case kMode_MCRI:
      UNREACHABLE();
  }
  UNREACHABLE();
}

// {node} should be a right shift. If its input is a 64-bit Load and {node}
// shifts it to the right by 32 bits, then this function emits a 32-bit Load of
// the high bits only (allowing 1. to load fewer bits and 2. to get rid of the
// shift).
template <typename T>
bool TryEmitLoadForLoadWord64AndShiftRight(
    InstructionSelectorT<TurboshaftAdapter>* selector, T node,
    InstructionCode opcode) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(selector->Get(node).template Cast<ShiftOp>().IsRightShift());
  const ShiftOp& shift = selector->Get(node).template Cast<ShiftOp>();
  X64OperandGeneratorT<TurboshaftAdapter> g(selector);
  if (selector->CanCover(node, shift.left()) &&
      selector->Get(shift.left()).Is<LoadOp>() &&
      selector->MatchIntegralWord32Constant(shift.right(), 32)) {
    DCHECK_EQ(selector->GetEffectLevel(node),
              selector->GetEffectLevel(shift.left()));
    // Just load and sign-extend the interesting 4 bytes instead. This happens,
    // for example, when we're loading and untagging SMIs.
    auto m =
        TryMatchBaseWithScaledIndexAndDisplacement64(selector, shift.left());
    if (m.has_value() &&
        (m->displacement == 0 || ValueFitsIntoImmediate(m->displacement))) {
#ifdef V8_IS_TSAN
      // On TSAN builds we require one scratch register. Because of this we also
      // have to modify the inputs to take into account possible aliasing and
      // use UseUniqueRegister which is not required for non-TSAN builds.
      InstructionOperand temps[] = {g.TempRegister()};
      size_t temp_count = arraysize(temps);
      auto reg_kind = OperandGeneratorT<
          TurboshaftAdapter>::RegisterUseKind::kUseUniqueRegister;
#else
      InstructionOperand* temps = nullptr;
      size_t temp_count = 0;
      auto reg_kind =
          OperandGeneratorT<TurboshaftAdapter>::RegisterUseKind::kUseRegister;
#endif  // V8_IS_TSAN
      size_t input_count = 0;
      InstructionOperand inputs[3];
      AddressingMode mode = g.GetEffectiveAddressMemoryOperand(
          shift.left(), inputs, &input_count, reg_kind);
      if (m->displacement == 0) {
        // Make sure that the addressing mode indicates the presence of an
        // immediate displacement. It seems that we never use M1 and M2, but we
        // handle them here anyways.
        mode = AddDisplacementToAddressingMode(mode);
        inputs[input_count++] =
            ImmediateOperand(ImmediateOperand::INLINE_INT32, 4);
      } else {
        // In the case that the base address was zero, the displacement will be
        // in a register and replacing it with an immediate is not allowed. This
        // usually only happens in dead code anyway.
        if (!inputs[input_count - 1].IsImmediate()) return false;
        inputs[input_count - 1] =
            ImmediateOperand(ImmediateOperand::INLINE_INT32,
                             static_cast<int32_t>(m->displacement) + 4);
      }
      InstructionOperand outputs[] = {g.DefineAsRegister(node)};
      InstructionCode code = opcode | AddressingModeField::encode(mode);
      selector->Emit(code, 1, outputs, input_count, inputs, temp_count, temps);
      return true;
    }
  }
  return false;
}

bool TryEmitLoadForLoadWord64AndShiftRight(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    InstructionCode opcode) {
  DCHECK(IrOpcode::kWord64Sar == node->opcode() ||
         IrOpcode::kWord64Shr == node->opcode());
  X64OperandGeneratorT<TurbofanAdapter> g(selector);
  Int64BinopMatcher m(node);
  if (selector->CanCover(m.node(), m.left().node()) && m.left().IsLoad() &&
      m.right().Is(32)) {
    DCHECK_EQ(selector->GetEffectLevel(node),
              selector->GetEffectLevel(m.left().node()));
    // Just load and sign-extend the interesting 4 bytes instead. This happens,
    // for example, when we're loading and untagging SMIs.
    BaseWithIndexAndDisplacement64Matcher mleft(m.left().node(),
                                                AddressOption::kAllowAll);
    if (mleft.matches() && (mleft.displacement() == nullptr ||
                            g.CanBeImmediate(mleft.displacement()))) {
#ifdef V8_IS_TSAN
      // On TSAN builds we require one scratch register. Because of this we also
      // have to modify the inputs to take into account possible aliasing and
      // use UseUniqueRegister which is not required for non-TSAN builds.
      InstructionOperand temps[] = {g.TempRegister()};
      size_t temp_count = arraysize(temps);
      auto reg_kind = OperandGeneratorT<
          TurbofanAdapter>::RegisterUseKind::kUseUniqueRegister;
#else
      InstructionOperand* temps = nullptr;
      size_t temp_count = 0;
      auto reg_kind =
          OperandGeneratorT<TurbofanAdapter>::RegisterUseKind::kUseRegister;
#endif  // V8_IS_TSAN
      size_t input_count = 0;
      InstructionOperand inputs[3];
      AddressingMode mode = g.GetEffectiveAddressMemoryOperand(
          m.left().node(), inputs, &input_count, reg_kind);
      if (mleft.displacement() == nullptr) {
        // Make sure that the addressing mode indicates the presence of an
        // immediate displacement. It seems that we never use M1 and M2, but we
        // handle them here anyways.
        mode = AddDisplacementToAddressingMode(mode);
        inputs[input_count++] =
            ImmediateOperand(ImmediateOperand::INLINE_INT32, 4);
      } else {
        // In the case that the base address was zero, the displacement will be
        // in a register and replacing it with an immediate is not allowed. This
        // usually only happens in dead code anyway.
        if (!inputs[input_count - 1].IsImmediate()) return false;
        int32_t displacement = g.GetImmediateIntegerValue(mleft.displacement());
        inputs[input_count - 1] =
            ImmediateOperand(ImmediateOperand::INLINE_INT32, displacement + 4);
      }
      InstructionOperand outputs[] = {g.DefineAsRegister(node)};
      InstructionCode code = opcode | AddressingModeField::encode(mode);
      selector->Emit(code, 1, outputs, input_count, inputs, temp_count, temps);
      return true;
    }
  }
  return false;
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Shr(node_t node) {
  if (TryEmitLoadForLoadWord64AndShiftRight(this, node, kX64Movl)) return;
  VisitWord64Shift(this, node, kX64Shr);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Sar(node_t node) {
  if constexpr (Adapter::IsTurboshaft) {
    // TODO(nicohartmann@): Add this optimization for Turboshaft.
  } else {
    X64OperandGeneratorT<Adapter> g(this);
    Int32BinopMatcher m(node);
    if (CanCover(m.node(), m.left().node()) && m.left().IsWord32Shl()) {
      Int32BinopMatcher mleft(m.left().node());
      if (mleft.right().Is(16) && m.right().Is(16)) {
        Emit(kX64Movsxwl, g.DefineAsRegister(node), g.Use(mleft.left().node()));
        return;
      } else if (mleft.right().Is(24) && m.right().Is(24)) {
        Emit(kX64Movsxbl, g.DefineAsRegister(node), g.Use(mleft.left().node()));
        return;
      }
    }
  }
  VisitWord32Shift(this, node, kX64Sar32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Sar(node_t node) {
  if (TryEmitLoadForLoadWord64AndShiftRight(this, node, kX64Movsxlq)) return;
  VisitWord64Shift(this, node, kX64Sar);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Rol(node_t node) {
  VisitWord32Shift(this, node, kX64Rol32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Rol(node_t node) {
  VisitWord64Shift(this, node, kX64Rol);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32Ror(node_t node) {
  VisitWord32Shift(this, node, kX64Ror32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64Ror(node_t node) {
  VisitWord64Shift(this, node, kX64Ror);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBits(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBits(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64ReverseBytes(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64Bswap, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32ReverseBytes(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64Bswap32, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSimd128ReverseBytes(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Add(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);

  std::optional<BaseWithScaledIndexAndDisplacementMatch<Adapter>> m;
  if constexpr (Adapter::IsTurbofan) {
    DCHECK_EQ(node->InputCount(), 2);
    Node* left = node->InputAt(0);
    Node* right = node->InputAt(1);
    // No need to truncate the values before Int32Add.
    if (left->opcode() == IrOpcode::kTruncateInt64ToInt32) {
      node->ReplaceInput(0, left->InputAt(0));
    }
    if (right->opcode() == IrOpcode::kTruncateInt64ToInt32) {
      node->ReplaceInput(1, right->InputAt(0));
    }

    // Try to match the Add to a leal pattern
    m = TryMatchBaseWithScaledIndexAndDisplacement32(this, node);

  } else {
    const turboshaft::WordBinopOp& add =
        this->Get(node).template Cast<turboshaft::WordBinopOp>();
    turboshaft::OpIndex left = add.left();
    turboshaft::OpIndex right = add.right();
    // No need to truncate the values before Int32Add.
    left = this->remove_truncate_word64_to_word32(left);
    right = this->remove_truncate_word64_to_word32(right);

    DCHECK(LhsIsNotOnlyConstant(this->turboshaft_graph(), left, right));

    // Try to match the Add to a leal pattern
    m = TryMatchBaseWithScaledIndexAndDisplacement64ForWordBinop(this, left,
                                                                 right, true);
  }

  if (m.has_value()) {
    if (ValueFitsIntoImmediate(m->displacement)) {
      EmitLea(this, kX64Lea32, node, m->index, m->scale, m->base,
              m->displacement, m->displacement_mode);
      return;
    }
  }

  // No leal pattern match, use addl
  VisitBinop(this, node, kX64Add32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Add(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  // Try to match the Add to a leaq pattern
  if (auto match = TryMatchBaseWithScaledIndexAndDisplacement64(this, node)) {
    if (ValueFitsIntoImmediate(match->displacement)) {
      EmitLea(this, kX64Lea, node, match->index, match->scale, match->base,
              match->displacement, match->displacement_mode);
      return;
    }
  }

  // No leal pattern match, use addq
  VisitBinop(this, node, kX64Add);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Add, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Add, &cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt32Sub(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  auto binop = this->word_binop_view(node);
  auto left = binop.left();
  auto right = binop.right();
  if (g.CanBeImmediate(right)) {
    int32_t imm = g.GetImmediateIntegerValue(right);
    if (imm == 0) {
      if (this->Get(left).outputs_rep()[0] ==
          turboshaft::RegisterRepresentation::Word32()) {
        // {EmitIdentity} reuses the virtual register of the first input
        // for the output. This is exactly what we want here.
        EmitIdentity(node);
      } else {
        // Emit "movl" for subtraction of 0.
        Emit(kX64Movl, g.DefineAsRegister(node), g.UseRegister(left));
      }
    } else {
      // Omit truncation and turn subtractions of constant values into immediate
      // "leal" instructions by negating the value.
      Emit(kX64Lea32 | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), g.UseRegister(left),
           g.TempImmediate(base::NegateWithWraparound(imm)));
    }
    return;
  }

  if (MatchIntegralZero(left)) {
    Emit(kX64Neg32, g.DefineSameAsFirst(node), g.UseRegister(right));
    return;
  }

  VisitBinop(this, node, kX64Sub32);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt32Sub(Node* node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  DCHECK_EQ(node->InputCount(), 2);
  Node* input1 = node->InputAt(0);
  Node* input2 = node->InputAt(1);
  if (input1->opcode() == IrOpcode::kTruncateInt64ToInt32 &&
      g.CanBeImmediate(input2)) {
    int32_t imm = g.GetImmediateIntegerValue(input2);
    InstructionOperand int64_input = g.UseRegister(input1->InputAt(0));
    if (imm == 0) {
      // Emit "movl" for subtraction of 0.
      Emit(kX64Movl, g.DefineAsRegister(node), int64_input);
    } else {
      // Omit truncation and turn subtractions of constant values into immediate
      // "leal" instructions by negating the value.
      Emit(kX64Lea32 | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), int64_input,
           g.TempImmediate(base::NegateWithWraparound(imm)));
    }
    return;
  }

  Int32BinopMatcher m(node);
  if (m.left().Is(0)) {
    Emit(kX64Neg32, g.DefineSameAsFirst(node), g.UseRegister(m.right().node()));
  } else if (m.right().Is(0)) {
    // {EmitIdentity} reuses the virtual register of the first input
    // for the output. This is exactly what we want here.
    EmitIdentity(node);
  } else if (m.right().HasResolvedValue() &&
             g.CanBeImmediate(m.right().node())) {
    // Turn subtractions of constant values into immediate "leal" instructions
    // by negating the value.
    Emit(
        kX64Lea32 | AddressingModeField::encode(kMode_MRI),
        g.DefineAsRegister(node), g.UseRegister(m.left().node()),
        g.TempImmediate(base::NegateWithWraparound(m.right().ResolvedValue())));
  } else {
    VisitBinop(this, node, kX64Sub32);
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitInt64Sub(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const WordBinopOp& binop = this->Get(node).Cast<WordBinopOp>();
  DCHECK_EQ(binop.kind, WordBinopOp::Kind::kSub);

  if (MatchIntegralZero(binop.left())) {
    Emit(kX64Neg, g.DefineSameAsFirst(node), g.UseRegister(binop.right()));
    return;
  }
  if (auto constant = TryGetRightWordConstant(this, node)) {
    int64_t immediate_value = -*constant;
    if (ValueFitsIntoImmediate(immediate_value)) {
      // Turn subtractions of constant values into immediate "leaq" instructions
      // by negating the value.
      Emit(kX64Lea | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), g.UseRegister(binop.left()),
           g.TempImmediate(static_cast<int32_t>(immediate_value)));
      return;
    }
  }
  VisitBinop(this, node, kX64Sub);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitInt64Sub(Node* node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  Int64BinopMatcher m(node);
  if (m.left().Is(0)) {
    Emit(kX64Neg, g.DefineSameAsFirst(node), g.UseRegister(m.right().node()));
  } else {
    if (m.right().HasResolvedValue() && g.CanBeImmediate(m.right().node())) {
      // Turn subtractions of constant values into immediate "leaq" instructions
      // by negating the value.
      Emit(kX64Lea | AddressingModeField::encode(kMode_MRI),
           g.DefineAsRegister(node), g.UseRegister(m.left().node()),
           g.TempImmediate(-static_cast<int32_t>(m.right().ResolvedValue())));
      return;
    }
    VisitBinop(this, node, kX64Sub);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Sub, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Sub, &cont);
}

namespace {

template <typename Adapter>
void VisitMul(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  auto binop = selector->word_binop_view(node);
  auto left = binop.left();
  auto right = binop.right();
  if (g.CanBeImmediate(right)) {
    selector->Emit(opcode, g.DefineAsRegister(node), g.Use(left),
                   g.UseImmediate(right));
  } else {
    if (g.CanBeBetterLeftOperand(right)) {
      std::swap(left, right);
    }
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(left),
                   g.Use(right));
  }
}

template <typename Adapter>
void VisitMulHigh(InstructionSelectorT<Adapter>* selector,
                  typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  auto binop = selector->word_binop_view(node);
  auto left = binop.left();
  auto right = binop.right();
  if (selector->IsLive(left) && !selector->IsLive(right)) {
    std::swap(left, right);
  }
  InstructionOperand temps[] = {g.TempRegister(rax)};
  // TODO(turbofan): We use UseUniqueRegister here to improve register
  // allocation.
  selector->Emit(opcode, g.DefineAsFixed(node, rdx), g.UseFixed(left, rax),
                 g.UseUniqueRegister(right), arraysize(temps), temps);
}

template <typename Adapter>
void VisitDiv(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  auto binop = selector->word_binop_view(node);
  InstructionOperand temps[] = {g.TempRegister(rdx)};
  selector->Emit(opcode, g.DefineAsFixed(node, rax),
                 g.UseFixed(binop.left(), rax),
                 g.UseUniqueRegister(binop.right()), arraysize(temps), temps);
}

template <typename Adapter>
void VisitMod(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, ArchOpcode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  auto binop = selector->word_binop_view(node);
  InstructionOperand temps[] = {g.TempRegister(rax)};
  selector->Emit(opcode, g.DefineAsFixed(node, rdx),
                 g.UseFixed(binop.left(), rax),
                 g.UseUniqueRegister(binop.right()), arraysize(temps), temps);
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mul(node_t node) {
  if (auto m = TryMatchScaledIndex32(this, node, true)) {
    EmitLea(this, kX64Lea32, node, m->index, m->scale, m->base, 0,
            kPositiveDisplacement);
    return;
  }
  VisitMul(this, node, kX64Imul32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Imul32, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Imul32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Mul(node_t node) {
  if (auto m = TryMatchScaledIndex64(this, node, true)) {
    EmitLea(this, kX64Lea, node, m->index, m->scale, m->base, 0,
            kPositiveDisplacement);
    return;
  }
  VisitMul(this, node, kX64Imul);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Imul, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Imul, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32MulHigh(node_t node) {
  VisitMulHigh(this, node, kX64ImulHigh32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64MulHigh(node_t node) {
  VisitMulHigh(this, node, kX64ImulHigh64);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Div(node_t node) {
  VisitDiv(this, node, kX64Idiv32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Div(node_t node) {
  VisitDiv(this, node, kX64Idiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Div(node_t node) {
  VisitDiv(this, node, kX64Udiv32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Div(node_t node) {
  VisitDiv(this, node, kX64Udiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32Mod(node_t node) {
  VisitMod(this, node, kX64Idiv32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64Mod(node_t node) {
  VisitMod(this, node, kX64Idiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32Mod(node_t node) {
  VisitMod(this, node, kX64Udiv32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64Mod(node_t node) {
  VisitMod(this, node, kX64Udiv);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32MulHigh(node_t node) {
  VisitMulHigh(this, node, kX64UmulHigh32);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64MulHigh(node_t node) {
  VisitMulHigh(this, node, kX64UmulHigh64);
}

// TryTruncateFloat32ToInt64 and TryTruncateFloat64ToInt64 operations attempt
// truncation from 32|64-bit float to 64-bit integer by performing roughly the
// following steps:
// 1. Round the original FP value to zero, store in `rounded`;
// 2. Convert the original FP value to integer;
// 3. Convert the integer value back to floating point, store in
// `converted_back`;
// 4. If `rounded` == `converted_back`:
//      Set Projection(1) := 1;   -- the value was in range
//    Else:
//      Set Projection(1) := 0;   -- the value was out of range
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToInt64(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  InstructionOperand temps[1];
  size_t output_count = 0;
  size_t temp_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
    temps[temp_count++] = g.TempSimd128Register();
  }

  Emit(kSSEFloat32ToInt64, output_count, outputs, 1, inputs, temp_count, temps);
}

// TryTruncateFloatNNToUintDD operations attempt truncation from NN-bit
// float to DD-bit integer by using ConvertFloatToUintDD macro instructions.
// It performs a float-to-int instruction, rounding to zero and tests whether
// the result is positive integer (the default, fast case), which means the
// value is in range. Then, we set Projection(1) := 1. Else, we perform
// additional subtraction, conversion and (in case the value was originally
// negative, but still within range) we restore it and set Projection(1) := 1.
// In all other cases we set Projection(1) := 0, denoting value out of range.
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint32(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kSSEFloat64ToUint32, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat32ToUint64(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kSSEFloat32ToUint64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToUint64(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  size_t output_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
  }

  Emit(kSSEFloat64ToUint64, output_count, outputs, 1, inputs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt64(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  InstructionOperand temps[1];
  size_t output_count = 0;
  size_t temp_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
    temps[temp_count++] = g.TempSimd128Register();
  }

  Emit(kSSEFloat64ToInt64, output_count, outputs, 1, inputs, temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTryTruncateFloat64ToInt32(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand inputs[] = {g.UseRegister(this->input_at(node, 0))};
  InstructionOperand outputs[2];
  InstructionOperand temps[1];
  size_t output_count = 0;
  size_t temp_count = 0;
  outputs[output_count++] = g.DefineAsRegister(node);

  node_t success_output = FindProjection(node, 1);
  if (Adapter::valid(success_output)) {
    outputs[output_count++] = g.DefineAsRegister(success_output);
    temps[temp_count++] = g.TempSimd128Register();
  }

  Emit(kSSEFloat64ToInt32, output_count, outputs, 1, inputs, temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitBitcastWord32ToWord64(node_t node) {
  DCHECK(SmiValuesAre31Bits());
  DCHECK(COMPRESS_POINTERS_BOOL);
  EmitIdentity(node);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeInt32ToInt64(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);

  X64OperandGeneratorT<Adapter> g(this);
  auto value = this->input_at(node, 0);
  if (this->IsLoadOrLoadImmutable(value) && CanCover(node, value)) {
    LoadRepresentation load_rep = this->load_view(value).loaded_rep();
    MachineRepresentation rep = load_rep.representation();
    InstructionCode opcode;
    switch (rep) {
      case MachineRepresentation::kBit:  // Fall through.
      case MachineRepresentation::kWord8:
        opcode = load_rep.IsSigned() ? kX64Movsxbq : kX64Movzxbq;
        break;
      case MachineRepresentation::kWord16:
        opcode = load_rep.IsSigned() ? kX64Movsxwq : kX64Movzxwq;
        break;
      case MachineRepresentation::kWord32:
      case MachineRepresentation::kWord64:
        // Since BitcastElider may remove nodes of
        // IrOpcode::kTruncateInt64ToInt32 and directly use the inputs, values
        // with kWord64 can also reach this line.
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kTaggedPointer:
        // ChangeInt32ToInt64 must interpret its input as a _signed_ 32-bit
        // integer, so here we must sign-extend the loaded value in any case.
        opcode = kX64Movsxlq;
        break;
      default:
        UNREACHABLE();
    }
    InstructionOperand outputs[] = {g.DefineAsRegister(node)};
    size_t input_count = 0;
    InstructionOperand inputs[3];
    AddressingMode mode = g.GetEffectiveAddressMemoryOperand(
        this->input_at(node, 0), inputs, &input_count);
    opcode |= AddressingModeField::encode(mode);
    Emit(opcode, 1, outputs, input_count, inputs);
  } else {
    Emit(kX64Movsxlq, g.DefineAsRegister(node), g.Use(this->input_at(node, 0)));
  }
}

template <>
bool InstructionSelectorT<TurboshaftAdapter>::ZeroExtendsWord32ToWord64NoPhis(
    turboshaft::OpIndex node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const auto& op = this->Get(node);
  switch (op.opcode) {
    case turboshaft::Opcode::kWordBinop: {
      const auto& binop = op.Cast<WordBinopOp>();
      if (binop.rep != WordRepresentation::Word32()) return false;
      DCHECK(binop.kind == WordBinopOp::Kind::kBitwiseAnd ||
             binop.kind == WordBinopOp::Kind::kBitwiseOr ||
             binop.kind == WordBinopOp::Kind::kBitwiseXor ||
             binop.kind == WordBinopOp::Kind::kAdd ||
             binop.kind == WordBinopOp::Kind::kSub ||
             binop.kind == WordBinopOp::Kind::kMul ||
             binop.kind == WordBinopOp::Kind::kSignedDiv ||
             binop.kind == WordBinopOp::Kind::kUnsignedDiv ||
             binop.kind == WordBinopOp::Kind::kSignedMod ||
             binop.kind == WordBinopOp::Kind::kUnsignedMod ||
             binop.kind == WordBinopOp::Kind::kSignedMulOverflownBits ||
             binop.kind == WordBinopOp::Kind::kUnsignedMulOverflownBits);
      return true;
    }
    case Opcode::kShift: {
      const auto& shift = op.Cast<ShiftOp>();
      if (shift.rep != WordRepresentation::Word32()) return false;
      DCHECK(shift.kind == ShiftOp::Kind::kShiftLeft ||
             shift.kind == ShiftOp::Kind::kShiftRightLogical ||
             shift.kind == ShiftOp::Kind::kShiftRightArithmetic ||
             shift.kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros ||
             shift.kind == ShiftOp::Kind::kRotateLeft ||
             shift.kind == ShiftOp::Kind::kRotateRight);
      return true;
    }
    case Opcode::kComparison: {
      const auto& comparison = op.Cast<ComparisonOp>();
      DCHECK(comparison.kind == ComparisonOp::Kind::kEqual ||
             comparison.kind == ComparisonOp::Kind::kSignedLessThan ||
             comparison.kind == ComparisonOp::Kind::kSignedLessThanOrEqual ||
             comparison.kind == ComparisonOp::Kind::kUnsignedLessThan ||
             comparison.kind == ComparisonOp::Kind::kUnsignedLessThanOrEqual);
      return comparison.rep == RegisterRepresentation::Word32();
    }
    case Opcode::kProjection: {
      const auto& projection = op.Cast<ProjectionOp>();
      if (const auto* binop =
              this->Get(projection.input()).TryCast<OverflowCheckedBinopOp>()) {
        DCHECK(binop->kind == OverflowCheckedBinopOp::Kind::kSignedAdd ||
               binop->kind == OverflowCheckedBinopOp::Kind::kSignedSub ||
               binop->kind == OverflowCheckedBinopOp::Kind::kSignedMul);
        return binop->rep == RegisterRepresentation::Word32();
      }
      return false;
    }
    case Opcode::kLoad: {
      const auto& load = op.Cast<LoadOp>();
      // The movzxbl/movsxbl/movzxwl/movsxwl/movl operations implicitly
      // zero-extend to 64-bit on x64, so the zero-extension is a no-op.
      switch (load.loaded_rep.ToMachineType().representation()) {
        case MachineRepresentation::kWord8:
        case MachineRepresentation::kWord16:
        case MachineRepresentation::kWord32:
          return true;
        default:
          break;
      }
      return false;
    }
    case Opcode::kConstant: {
      X64OperandGeneratorT<TurboshaftAdapter> g(this);
      // Constants are loaded with movl or movq, or xorl for zero; see
      // CodeGenerator::AssembleMove. So any non-negative constant that fits
      // in a 32-bit signed integer is zero-extended to 64 bits.
      if (g.CanBeImmediate(node)) {
        return g.GetImmediateIntegerValue(node) >= 0;
      }
      return false;
    }
    case Opcode::kChange:
      return this->is_truncate_word64_to_word32(node);
    default:
      return false;
  }
}

template <>
bool InstructionSelectorT<TurbofanAdapter>::ZeroExtendsWord32ToWord64NoPhis(
    Node* node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  DCHECK_NE(node->opcode(), IrOpcode::kPhi);
  switch (node->opcode()) {
    case IrOpcode::kWord32And:
    case IrOpcode::kWord32Or:
    case IrOpcode::kWord32Xor:
    case IrOpcode::kWord32Shl:
    case IrOpcode::kWord32Shr:
    case IrOpcode::kWord32Sar:
    case IrOpcode::kWord32Rol:
    case IrOpcode::kWord32Ror:
    case IrOpcode::kWord32Equal:
    case IrOpcode::kInt32Add:
    case IrOpcode::kInt32Sub:
    case IrOpcode::kInt32Mul:
    case IrOpcode::kInt32MulHigh:
    case IrOpcode::kInt32Div:
    case IrOpcode::kInt32LessThan:
    case IrOpcode::kInt32LessThanOrEqual:
    case IrOpcode::kInt32Mod:
    case IrOpcode::kUint32Div:
    case IrOpcode::kUint32LessThan:
    case IrOpcode::kUint32LessThanOrEqual:
    case IrOpcode::kUint32Mod:
    case IrOpcode::kUint32MulHigh:
    case IrOpcode::kTruncateInt64ToInt32:
      // These 32-bit operations implicitly zero-extend to 64-bit on x64, so the
      // zero-extension is a no-op.
      return true;
    case IrOpcode::kProjection: {
      Node* const value = node->InputAt(0);
      switch (value->opcode()) {
        case IrOpcode::kInt32AddWithOverflow:
        case IrOpcode::kInt32SubWithOverflow:
        case IrOpcode::kInt32MulWithOverflow:
          return true;
        default:
          return false;
      }
    }
    case IrOpcode::kLoad:
    case IrOpcode::kLoadImmutable:
    case IrOpcode::kProtectedLoad:
    case IrOpcode::kLoadTrapOnNull: {
      // The movzxbl/movsxbl/movzxwl/movsxwl/movl operations implicitly
      // zero-extend to 64-bit on x64, so the zero-extension is a no-op.
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
    case IrOpcode::kInt32Constant:
    case IrOpcode::kInt64Constant:
      // Constants are loaded with movl or movq, or xorl for zero; see
      // CodeGenerator::AssembleMove. So any non-negative constant that fits
      // in a 32-bit signed integer is zero-extended to 64 bits.
      if (g.CanBeImmediate(node)) {
        return g.GetImmediateIntegerValue(node) >= 0;
      }
      return false;
    default:
      return false;
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitChangeUint32ToUint64(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  node_t value = this->input_at(node, 0);
  if (ZeroExtendsWord32ToWord64(value)) {
    // These 32-bit operations implicitly zero-extend to 64-bit on x64, so the
    // zero-extension is a no-op.
    return EmitIdentity(node);
  }
  Emit(kX64Movl, g.DefineAsRegister(node), g.Use(value));
}

namespace {

template <typename Adapter>
void VisitRO(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t node, InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 1);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.Use(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRR(InstructionSelectorT<Adapter>* selector,
             typename Adapter::node_t node, InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineAsRegister(node),
                 g.UseRegister(selector->input_at(node, 0)));
}

template <typename Adapter>
void VisitRRO(InstructionSelectorT<Adapter>* selector,
              typename Adapter::node_t node, InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  selector->Emit(opcode, g.DefineSameAsFirst(node),
                 g.UseRegister(selector->input_at(node, 0)),
                 g.Use(selector->input_at(node, 1)));
}

template <typename Adapter>
void VisitFloatBinop(InstructionSelectorT<Adapter>* selector,
                     typename Adapter::node_t node, InstructionCode avx_opcode,
                     InstructionCode sse_opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  InstructionOperand inputs[8];
  size_t input_count = 0;
  InstructionOperand outputs[1];
  size_t output_count = 0;

  if (left == right) {
    // If both inputs refer to the same operand, enforce allocating a register
    // for both of them to ensure that we don't end up generating code like
    // this:
    //
    //   movss rax, [rbp-0x10]
    //   addss rax, [rbp-0x10]
    //   jo label
    InstructionOperand const input = g.UseRegister(left);
    inputs[input_count++] = input;
    inputs[input_count++] = input;
  } else {
    int effect_level = selector->GetEffectLevel(node);
    if (selector->IsCommutative(node) &&
        (g.CanBeBetterLeftOperand(right) ||
         g.CanBeMemoryOperand(avx_opcode, node, left, effect_level)) &&
        (!g.CanBeBetterLeftOperand(left) ||
         !g.CanBeMemoryOperand(avx_opcode, node, right, effect_level))) {
      std::swap(left, right);
    }
    if (g.CanBeMemoryOperand(avx_opcode, node, right, effect_level)) {
      inputs[input_count++] = g.UseRegister(left);
      AddressingMode addressing_mode =
          g.GetEffectiveAddressMemoryOperand(right, inputs, &input_count);
      avx_opcode |= AddressingModeField::encode(addressing_mode);
      sse_opcode |= AddressingModeField::encode(addressing_mode);
      if constexpr (Adapter::IsTurboshaft) {
        if (g.IsProtectedLoad(right)) {
          // In {CanBeMemoryOperand} we have already checked that
          // CanCover(node, right) succeds, which means that there is no
          // instruction with Effects required_when_unused or
          // produces.control_flow between right and node, and that the node has
          // no other uses. Therefore, we can record the fact that 'right' was
          // embedded in 'node' and we can later delete the Load instruction.
          selector->MarkAsProtected(node);
          avx_opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
          sse_opcode |=
              AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
          selector->SetProtectedLoadToRemove(right);
        }
      }
    } else {
      inputs[input_count++] = g.UseRegister(left);
      inputs[input_count++] = g.Use(right);
    }
  }

  DCHECK_NE(0u, input_count);
  DCHECK_GE(arraysize(inputs), input_count);

  if (selector->IsSupported(AVX)) {
    outputs[output_count++] = g.DefineAsRegister(node);
    DCHECK_EQ(1u, output_count);
    DCHECK_GE(arraysize(outputs), output_count);
    selector->Emit(avx_opcode, output_count, outputs, input_count, inputs);
  } else {
    outputs[output_count++] = g.DefineSameAsFirst(node);
    DCHECK_EQ(1u, output_count);
    DCHECK_GE(arraysize(outputs), output_count);
    selector->Emit(sse_opcode, output_count, outputs, input_count, inputs);
  }
}

template <typename Adapter>
void VisitFloatUnop(InstructionSelectorT<Adapter>* selector,
                    typename Adapter::node_t node,
                    typename Adapter::node_t input, InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(selector);
  if (selector->IsSupported(AVX)) {
    selector->Emit(opcode, g.DefineAsRegister(node), g.UseRegister(input));
  } else {
    selector->Emit(opcode, g.DefineSameAsFirst(node), g.UseRegister(input));
  }
}

}  // namespace

#define RO_OP_T_LIST(V)                                                \
  V(Word64Clz, kX64Lzcnt)                                              \
  V(Word32Clz, kX64Lzcnt32)                                            \
  V(Word64Ctz, kX64Tzcnt)                                              \
  V(Word32Ctz, kX64Tzcnt32)                                            \
  V(Word64Popcnt, kX64Popcnt)                                          \
  V(Word32Popcnt, kX64Popcnt32)                                        \
  V(Float64Sqrt, kSSEFloat64Sqrt)                                      \
  V(Float32Sqrt, kSSEFloat32Sqrt)                                      \
  V(RoundFloat64ToInt32, kSSEFloat64ToInt32)                           \
  V(ChangeInt32ToFloat64, kSSEInt32ToFloat64)                          \
  V(TruncateFloat64ToFloat32, kSSEFloat64ToFloat32)                    \
  V(ChangeFloat32ToFloat64, kSSEFloat32ToFloat64)                      \
  V(ChangeFloat64ToInt32, kSSEFloat64ToInt32)                          \
  V(ChangeFloat64ToUint32, kSSEFloat64ToUint32 | MiscField::encode(1)) \
  V(ChangeFloat64ToInt64, kSSEFloat64ToInt64)                          \
  V(ChangeFloat64ToUint64, kSSEFloat64ToUint64)                        \
  V(RoundInt32ToFloat32, kSSEInt32ToFloat32)                           \
  V(RoundInt64ToFloat32, kSSEInt64ToFloat32)                           \
  V(RoundUint64ToFloat32, kSSEUint64ToFloat32)                         \
  V(RoundInt64ToFloat64, kSSEInt64ToFloat64)                           \
  V(RoundUint64ToFloat64, kSSEUint64ToFloat64)                         \
  V(RoundUint32ToFloat32, kSSEUint32ToFloat32)                         \
  V(ChangeInt64ToFloat64, kSSEInt64ToFloat64)                          \
  V(ChangeUint32ToFloat64, kSSEUint32ToFloat64)                        \
  V(Float64ExtractLowWord32, kSSEFloat64ExtractLowWord32)              \
  V(Float64ExtractHighWord32, kSSEFloat64ExtractHighWord32)            \
  V(BitcastFloat32ToInt32, kX64BitcastFI)                              \
  V(BitcastFloat64ToInt64, kX64BitcastDL)                              \
  V(BitcastInt32ToFloat32, kX64BitcastIF)                              \
  V(BitcastInt64ToFloat64, kX64BitcastLD)                              \
  V(SignExtendWord8ToInt32, kX64Movsxbl)                               \
  V(SignExtendWord16ToInt32, kX64Movsxwl)                              \
  V(SignExtendWord8ToInt64, kX64Movsxbq)                               \
  V(SignExtendWord16ToInt64, kX64Movsxwq)                              \
  V(TruncateFloat64ToInt64, kSSEFloat64ToInt64)                        \
  V(TruncateFloat32ToInt32, kSSEFloat32ToInt32)                        \
  V(TruncateFloat32ToUint32, kSSEFloat32ToUint32)

#ifdef V8_ENABLE_WEBASSEMBLY
#define RR_OP_T_LIST_WEBASSEMBLY(V)                                       \
  V(F16x8Ceil, kX64F16x8Round | MiscField::encode(kRoundUp))              \
  V(F16x8Floor, kX64F16x8Round | MiscField::encode(kRoundDown))           \
  V(F16x8Trunc, kX64F16x8Round | MiscField::encode(kRoundToZero))         \
  V(F16x8NearestInt, kX64F16x8Round | MiscField::encode(kRoundToNearest)) \
  V(F32x4Ceil, kX64F32x4Round | MiscField::encode(kRoundUp))              \
  V(F32x4Floor, kX64F32x4Round | MiscField::encode(kRoundDown))           \
  V(F32x4Trunc, kX64F32x4Round | MiscField::encode(kRoundToZero))         \
  V(F32x4NearestInt, kX64F32x4Round | MiscField::encode(kRoundToNearest)) \
  V(F64x2Ceil, kX64F64x2Round | MiscField::encode(kRoundUp))              \
  V(F64x2Floor, kX64F64x2Round | MiscField::encode(kRoundDown))           \
  V(F64x2Trunc, kX64F64x2Round | MiscField::encode(kRoundToZero))         \
  V(F64x2NearestInt, kX64F64x2Round | MiscField::encode(kRoundToNearest))
#else
#define RR_OP_T_LIST_WEBASSEMBLY(V)
#endif  // V8_ENABLE_WEBASSEMBLY

#define RR_OP_T_LIST(V)                                                       \
  V(TruncateFloat64ToUint32, kSSEFloat64ToUint32 | MiscField::encode(0))      \
  V(SignExtendWord32ToInt64, kX64Movsxlq)                                     \
  V(Float32RoundDown, kSSEFloat32Round | MiscField::encode(kRoundDown))       \
  V(Float64RoundDown, kSSEFloat64Round | MiscField::encode(kRoundDown))       \
  V(Float32RoundUp, kSSEFloat32Round | MiscField::encode(kRoundUp))           \
  V(Float64RoundUp, kSSEFloat64Round | MiscField::encode(kRoundUp))           \
  V(Float32RoundTruncate, kSSEFloat32Round | MiscField::encode(kRoundToZero)) \
  V(Float64RoundTruncate, kSSEFloat64Round | MiscField::encode(kRoundToZero)) \
  V(Float32RoundTiesEven,                                                     \
    kSSEFloat32Round | MiscField::encode(kRoundToNearest))                    \
  V(Float64RoundTiesEven,                                                     \
    kSSEFloat64Round | MiscField::encode(kRoundToNearest))                    \
  RR_OP_T_LIST_WEBASSEMBLY(V)

#define RO_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRO(this, node, opcode);                                 \
  }
RO_OP_T_LIST(RO_VISITOR)
#undef RO_VIISTOR
#undef RO_OP_T_LIST

#define RR_VISITOR(Name, opcode)                                 \
  template <typename Adapter>                                    \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) { \
    VisitRR(this, node, opcode);                                 \
  }
RR_OP_T_LIST(RR_VISITOR)
#undef RR_VISITOR
#undef RR_OP_T_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateFloat64ToWord32(node_t node) {
  VisitRR(this, node, kArchTruncateDoubleToI);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitTruncateInt64ToInt32(node_t node) {
  // We rely on the fact that TruncateInt64ToInt32 zero extends the
  // value (see ZeroExtendsWord32ToWord64). So all code paths here
  // have to satisfy that condition.
  X64OperandGeneratorT<Adapter> g(this);

  node_t value = this->input_at(node, 0);
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    bool can_cover = false;
    if (const TaggedBitcastOp* value_op =
            this->Get(value)
                .template TryCast<
                    Opmask::kBitcastTaggedToWordPtrForTagAndSmiBits>()) {
      can_cover = CanCover(node, value) && CanCover(node, value_op->input());
      value = value_op->input();
    } else {
      can_cover = CanCover(node, value);
    }
    if (can_cover) {
      const Operation& value_op = this->Get(value);
      if (const ShiftOp * shift;
          (shift = value_op.TryCast<Opmask::kWord64ShiftRightArithmetic>()) ||
          (shift = value_op.TryCast<Opmask::kWord64ShiftRightLogical>())) {
        if (this->MatchIntegralWord32Constant(shift->right(), 32)) {
          if (CanCover(value, shift->left()) &&
              TryEmitLoadForLoadWord64AndShiftRight(this, value, kX64Movl)) {
            // We just defined and emitted a 32-bit Load for {value} (the upper
            // 32 bits only since it was getting shifted by 32 bits to the right
            // afterwards); we now define {node} as a rename of {value} without
            // needing to do a truncation.
            return EmitIdentity(node);
          }
          Emit(kX64Shr, g.DefineSameAsFirst(node), g.UseRegister(shift->left()),
               g.TempImmediate(32));
          return;
        }
      }
    }
  } else {
    bool can_cover = false;
    if (value->opcode() == IrOpcode::kBitcastTaggedToWordForTagAndSmiBits) {
      can_cover = CanCover(node, value) && CanCover(value, value->InputAt(0));
      value = value->InputAt(0);
    } else {
      can_cover = CanCover(node, value);
    }
    if (can_cover) {
      switch (value->opcode()) {
        case IrOpcode::kWord64Sar:
        case IrOpcode::kWord64Shr: {
          Int64BinopMatcher m(value);
          if (m.right().Is(32)) {
            if (CanCover(value, value->InputAt(0)) &&
                TryEmitLoadForLoadWord64AndShiftRight(this, value, kX64Movl)) {
              return EmitIdentity(node);
            }
            Emit(kX64Shr, g.DefineSameAsFirst(node),
                 g.UseRegister(m.left().node()), g.TempImmediate(32));
            return;
          }
          break;
        }
        case IrOpcode::kLoad:
        case IrOpcode::kLoadImmutable: {
          // Note: in Turboshaft, we shouldn't reach this point, because we'd
          // have a BitcastTaggedToWord32 instead of a TruncateInt64ToInt32.
          TryMergeTruncateInt64ToInt32IntoLoad(this, node, value);
          return;
        }
        default:
          break;
      }
    }
  }
  Emit(kX64Movl, g.DefineAsRegister(node), g.Use(value));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Add(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat32Add, kSSEFloat32Add);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Sub(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat32Sub, kSSEFloat32Sub);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Mul(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat32Mul, kSSEFloat32Mul);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Div(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat32Div, kSSEFloat32Div);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Abs(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Float32Abs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Max(node_t node) {
  VisitRRO(this, node, kSSEFloat32Max);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Min(node_t node) {
  VisitRRO(this, node, kSSEFloat32Min);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Add(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat64Add, kSSEFloat64Add);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Sub(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat64Sub, kSSEFloat64Sub);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mul(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat64Mul, kSSEFloat64Mul);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Div(node_t node) {
  VisitFloatBinop(this, node, kAVXFloat64Div, kSSEFloat64Div);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Mod(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 2);
  X64OperandGeneratorT<Adapter> g(this);
  InstructionOperand temps[] = {g.TempRegister(rax)};
  Emit(kSSEFloat64Mod, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)), 1, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Max(node_t node) {
  VisitRRO(this, node, kSSEFloat64Max);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Min(node_t node) {
  VisitRRO(this, node, kSSEFloat64Min);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Abs(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Float64Abs);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64RoundTiesAway(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat32Neg(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Float32Neg);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Neg(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Float64Neg);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Binop(
    node_t node, InstructionCode opcode) {
  DCHECK_EQ(this->value_input_count(node), 2);
  X64OperandGeneratorT<Adapter> g(this);
  Emit(opcode, g.DefineAsFixed(node, xmm0),
       g.UseFixed(this->input_at(node, 0), xmm0),
       g.UseFixed(this->input_at(node, 1), xmm1))
      ->MarkAsCall();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64Ieee754Unop(
    node_t node, InstructionCode opcode) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(opcode, g.DefineAsFixed(node, xmm0),
       g.UseFixed(this->input_at(node, 0), xmm0))
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
  X64OperandGeneratorT<Adapter> g(this);

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
        InstructionOperand value = g.CanBeImmediate(input.node)
                                       ? g.UseImmediate(input.node)
                                       : g.UseRegister(input.node);
        Emit(kX64Poke | MiscField::encode(slot), g.NoOutput(), value);
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
        Emit(kX64Push, g.NoOutput(), decrement, g.UseImmediate(input.node));
      } else if (IsSupported(INTEL_ATOM) ||
                 sequence()->IsFP(GetVirtualRegister(input.node))) {
        // TODO(titzer): X64Push cannot handle stack->stack double moves
        // because there is no way to encode fixed double slots.
        Emit(kX64Push, g.NoOutput(), decrement, g.UseRegister(input.node));
      } else if (g.CanBeMemoryOperand(kX64Push, node, input.node,
                                      effect_level)) {
        InstructionOperand outputs[1];
        InstructionOperand inputs[5];
        size_t input_count = 0;
        inputs[input_count++] = decrement;
        AddressingMode mode = g.GetEffectiveAddressMemoryOperand(
            input.node, inputs, &input_count);
        InstructionCode opcode = kX64Push | AddressingModeField::encode(mode);
        Emit(opcode, 0, outputs, input_count, inputs);
      } else {
        Emit(kX64Push, g.NoOutput(), decrement, g.UseAny(input.node));
      }
    }
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::EmitPrepareResults(
    ZoneVector<PushParameter>* results, const CallDescriptor* call_descriptor,
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
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
      InstructionOperand result = g.DefineAsRegister(output.node);
      int offset = call_descriptor->GetOffsetToReturns();
      int reverse_slot = -output.location.GetLocation() - offset;
      InstructionOperand slot = g.UseImmediate(reverse_slot);
      Emit(kX64Peek, 1, &result, 1, &slot);
    }
  }
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
  X64OperandGeneratorT<Adapter> g(selector);
  size_t input_count = 0;
  InstructionOperand inputs[6];
  AddressingMode addressing_mode =
      g.GetEffectiveAddressMemoryOperand(left, inputs, &input_count);
  opcode |= AddressingModeField::encode(addressing_mode);
  inputs[input_count++] = right;
  if (cont->IsSelect()) {
    if (opcode == kUnorderedEqual) {
      cont->Negate();
      inputs[input_count++] = g.UseRegister(cont->true_value());
      inputs[input_count++] = g.Use(cont->false_value());
    } else {
      inputs[input_count++] = g.UseRegister(cont->false_value());
      inputs[input_count++] = g.Use(cont->true_value());
    }
  }

  selector->EmitWithContinuation(opcode, 0, nullptr, input_count, inputs, cont);
}

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, InstructionOperand left,
                  InstructionOperand right, FlagsContinuationT<Adapter>* cont) {
  if (cont->IsSelect()) {
    X64OperandGeneratorT<Adapter> g(selector);
    InstructionOperand inputs[4] = {left, right};
    if (cont->condition() == kUnorderedEqual) {
      cont->Negate();
      inputs[2] = g.UseRegister(cont->true_value());
      inputs[3] = g.Use(cont->false_value());
    } else {
      inputs[2] = g.UseRegister(cont->false_value());
      inputs[3] = g.Use(cont->true_value());
    }
    selector->EmitWithContinuation(opcode, 0, nullptr, 4, inputs, cont);
    return;
  }
  selector->EmitWithContinuation(opcode, left, right, cont);
}

// Shared routine for multiple compare operations.
template <typename Adapter>
void VisitCompare(InstructionSelectorT<Adapter>* selector,
                  InstructionCode opcode, typename Adapter::node_t left,
                  typename Adapter::node_t right,
                  FlagsContinuationT<Adapter>* cont, bool commutative) {
  X64OperandGeneratorT<Adapter> g(selector);
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
        if (constant >= std::numeric_limits<int32_t>::min() &&
            constant <= std::numeric_limits<int32_t>::max()) {
          return hint;
        }
      } else if (hint == MachineType::Uint32()) {
        if (constant >= std::numeric_limits<uint32_t>::min() &&
            constant <= std::numeric_limits<uint32_t>::max())
          return hint;
      }
    }
  }
  if (selector->IsLoadOrLoadImmutable(node)) {
    return selector->load_view(node).loaded_rep();
  }
  return MachineType::None();
}

bool IsIntConstant(InstructionSelectorT<TurbofanAdapter>*, Node* node) {
  return node->opcode() == IrOpcode::kInt32Constant ||
         node->opcode() == IrOpcode::kInt64Constant;
}
bool IsIntConstant(InstructionSelectorT<TurboshaftAdapter>* selector,
                   turboshaft::OpIndex node) {
  if (auto constant = selector->Get(node).TryCast<turboshaft::ConstantOp>()) {
    return constant->kind == turboshaft::ConstantOp::Kind::kWord32 ||
           constant->kind == turboshaft::ConstantOp::Kind::kWord64;
  }
  return false;
}
bool IsWordAnd(InstructionSelectorT<TurbofanAdapter>*, Node* node) {
  return node->opcode() == IrOpcode::kWord32And ||
         node->opcode() == IrOpcode::kWord64And;
}
bool IsWordAnd(InstructionSelectorT<TurboshaftAdapter>* selector,
               turboshaft::OpIndex node) {
  if (auto binop = selector->Get(node).TryCast<turboshaft::WordBinopOp>()) {
    return binop->kind == turboshaft::WordBinopOp::Kind::kBitwiseAnd;
  }
  return false;
}

// The result of WordAnd with a positive interger constant in X64 is known to
// be sign(zero)-extended. Comparing this result with another positive interger
// constant can have narrowed operand.
template <typename Adapter>
MachineType MachineTypeForNarrowWordAnd(
    InstructionSelectorT<Adapter>* selector, typename Adapter::node_t and_node,
    typename Adapter::node_t constant_node) {
  DCHECK_EQ(selector->value_input_count(and_node), 2);
  auto and_left = selector->input_at(and_node, 0);
  auto and_right = selector->input_at(and_node, 1);
  auto and_constant_node = IsIntConstant(selector, and_right) ? and_right
                           : IsIntConstant(selector, and_left)
                               ? and_left
                               : typename Adapter::node_t{};

  if (Adapter::valid(and_constant_node)) {
    int64_t and_constant = selector->integer_constant(and_constant_node);
    int64_t cmp_constant = selector->integer_constant(constant_node);
    if (and_constant >= 0 && cmp_constant >= 0) {
      int64_t constant =
          and_constant > cmp_constant ? and_constant : cmp_constant;
      if (constant <= std::numeric_limits<int8_t>::max()) {
        return MachineType::Int8();
      } else if (constant <= std::numeric_limits<uint8_t>::max()) {
        return MachineType::Uint8();
      } else if (constant <= std::numeric_limits<int16_t>::max()) {
        return MachineType::Int16();
      } else if (constant <= std::numeric_limits<uint16_t>::max()) {
        return MachineType::Uint16();
      } else if (constant <= std::numeric_limits<int32_t>::max()) {
        return MachineType::Int32();
      } else if (constant <= std::numeric_limits<uint32_t>::max()) {
        return MachineType::Uint32();
      }
    }
  }

  return MachineType::None();
}

// Tries to match the size of the given opcode to that of the operands, if
// possible.
template <typename Adapter>
InstructionCode TryNarrowOpcodeSize(InstructionSelectorT<Adapter>* selector,
                                    InstructionCode opcode,
                                    typename Adapter::node_t left,
                                    typename Adapter::node_t right,
                                    FlagsContinuationT<Adapter>* cont) {
  MachineType left_type = MachineType::None();
  MachineType right_type = MachineType::None();
  if (IsWordAnd(selector, left) && IsIntConstant(selector, right)) {
    left_type = MachineTypeForNarrowWordAnd(selector, left, right);
    right_type = left_type;
  } else if (IsWordAnd(selector, right) && IsIntConstant(selector, left)) {
    right_type = MachineTypeForNarrowWordAnd(selector, right, left);
    left_type = right_type;
  } else {
    // TODO(epertoso): we can probably get some size information out phi nodes.
    // If the load representations don't match, both operands will be
    // zero/sign-extended to 32bit.
    left_type = MachineTypeForNarrow(selector, left, right);
    right_type = MachineTypeForNarrow(selector, right, left);
  }
  if (left_type == right_type) {
    switch (left_type.representation()) {
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8: {
        if (opcode == kX64Test || opcode == kX64Test32) return kX64Test8;
        if (opcode == kX64Cmp || opcode == kX64Cmp32) {
          if (left_type.semantic() == MachineSemantic::kUint32) {
            cont->OverwriteUnsignedIfSigned();
          } else {
            CHECK_EQ(MachineSemantic::kInt32, left_type.semantic());
          }
          return kX64Cmp8;
        }
        break;
      }
      // Cmp16/Test16 may introduce LCP(Length-Changing-Prefixes) stall, use
      // Cmp32/Test32 instead.
      case MachineRepresentation::kWord16:  // Fall through.
      case MachineRepresentation::kWord32:
        if (opcode == kX64Test) return kX64Test32;
        if (opcode == kX64Cmp) {
          if (left_type.semantic() == MachineSemantic::kUint32) {
            cont->OverwriteUnsignedIfSigned();
          } else {
            CHECK_EQ(MachineSemantic::kInt32, left_type.semantic());
          }
          return kX64Cmp32;
        }
        break;
#ifdef V8_COMPRESS_POINTERS
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTagged:
        // When pointer compression is enabled the lower 32-bits uniquely
        // identify tagged value.
        if (opcode == kX64Cmp) return kX64Cmp32;
        break;
#endif
      default:
        break;
    }
  }
  return opcode;
}

/*
Remove unnecessary WordAnd
For example:
33:  IfFalse(31)
517: Int32Constant[65535]
518: Word32And(18, 517)
36:  Int32Constant[266]
37:  Int32LessThanOrEqual(36, 518)
38:  Branch[None]

If Int32LessThanOrEqual select cmp16, the above Word32And can be removed:
33:  IfFalse(31)
36:  Int32Constant[266]
37:  Int32LessThanOrEqual(36, 18)
38:  Branch[None]
*/
template <typename Adapter>
typename Adapter::node_t RemoveUnnecessaryWordAnd(
    InstructionSelectorT<Adapter>* selector, InstructionCode opcode,
    typename Adapter::node_t and_node) {
  int64_t mask = 0;

  if (opcode == kX64Cmp32 || opcode == kX64Test32) {
    mask = std::numeric_limits<uint32_t>::max();
  } else if (opcode == kX64Cmp16 || opcode == kX64Test16) {
    mask = std::numeric_limits<uint16_t>::max();
  } else if (opcode == kX64Cmp8 || opcode == kX64Test8) {
    mask = std::numeric_limits<uint8_t>::max();
  } else {
    return and_node;
  }

  DCHECK_EQ(selector->value_input_count(and_node), 2);
  auto and_left = selector->input_at(and_node, 0);
  auto and_right = selector->input_at(and_node, 1);
  auto and_constant_node = typename Adapter::node_t{};
  auto and_other_node = typename Adapter::node_t{};
  if (IsIntConstant(selector, and_left)) {
    and_constant_node = and_left;
    and_other_node = and_right;
  } else if (IsIntConstant(selector, and_right)) {
    and_constant_node = and_right;
    and_other_node = and_left;
  }

  if (Adapter::valid(and_constant_node)) {
    int64_t and_constant = selector->integer_constant(and_constant_node);
    if (and_constant == mask) return and_other_node;
  }
  return and_node;
}

// Shared routine for multiple word compare operations.
template <typename Adapter>
void VisitWordCompare(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, InstructionCode opcode,
                      FlagsContinuationT<Adapter>* cont) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 2);
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);

  // The 32-bit comparisons automatically truncate Word64
  // values to Word32 range, no need to do that explicitly.
  if (opcode == kX64Cmp32 || opcode == kX64Test32) {
    if (selector->is_truncate_word64_to_word32(left)) {
      left = selector->input_at(left, 0);
    }
    if (selector->is_truncate_word64_to_word32(right)) {
      right = selector->input_at(right, 0);
    }
  }

  opcode = TryNarrowOpcodeSize(selector, opcode, left, right, cont);

  // If one of the two inputs is an immediate, make sure it's on the right, or
  // if one of the two inputs is a memory operand, make sure it's on the left.
  int effect_level = selector->GetEffectLevel(node, cont);

  if ((!g.CanBeImmediate(right) && g.CanBeImmediate(left)) ||
      (g.CanBeMemoryOperand(opcode, node, right, effect_level) &&
       !g.CanBeMemoryOperand(opcode, node, left, effect_level))) {
    if (!selector->IsCommutative(node)) cont->Commute();
    std::swap(left, right);
  }

  if (IsWordAnd(selector, left)) {
    left = RemoveUnnecessaryWordAnd(selector, opcode, left);
  }

  // Match immediates on right side of comparison.
  if (g.CanBeImmediate(right)) {
    if (g.CanBeMemoryOperand(opcode, node, left, effect_level)) {
      return VisitCompareWithMemoryOperand(selector, opcode, left,
                                           g.UseImmediate(right), cont);
    }
    return VisitCompare(selector, opcode, g.Use(left), g.UseImmediate(right),
                        cont);
  }

  // Match memory operands on left side of comparison.
  if (g.CanBeMemoryOperand(opcode, node, left, effect_level)) {
    return VisitCompareWithMemoryOperand(selector, opcode, left,
                                         g.UseRegister(right), cont);
  }

  return VisitCompare(selector, opcode, left, right, cont,
                      selector->IsCommutative(node));
}

template <typename Adapter>
void VisitWord64EqualImpl(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node,
                          FlagsContinuationT<Adapter>* cont) {
  if (selector->CanUseRootsRegister()) {
    X64OperandGeneratorT<Adapter> g(selector);
    const RootsTable& roots_table = selector->isolate()->roots_table();
    RootIndex root_index;
    if constexpr (Adapter::IsTurboshaft) {
      using namespace turboshaft;  // NOLINT(build/namespaces)
      const ComparisonOp& equal =
          selector->Get(node).template Cast<ComparisonOp>();
      DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
      Handle<HeapObject> object;
      if (equal.rep == RegisterRepresentation::Tagged() &&
          selector->MatchHeapConstant(equal.right(), &object)) {
        if (roots_table.IsRootHandle(object, &root_index)) {
          InstructionCode opcode =
              kX64Cmp | AddressingModeField::encode(kMode_Root);
          return VisitCompare(
              selector, opcode,
              g.TempImmediate(
                  MacroAssemblerBase::RootRegisterOffsetForRootIndex(
                      root_index)),
              g.UseRegister(equal.left()), cont);
        }
      }
    } else {
      HeapObjectBinopMatcher m(node);
      if (m.right().HasResolvedValue() &&
          roots_table.IsRootHandle(m.right().ResolvedValue(), &root_index)) {
        InstructionCode opcode =
            kX64Cmp | AddressingModeField::encode(kMode_Root);
        return VisitCompare(
            selector, opcode,
            g.TempImmediate(
                MacroAssemblerBase::RootRegisterOffsetForRootIndex(root_index)),
            g.UseRegister(m.left().node()), cont);
      }
    }
  }
  VisitWordCompare(selector, node, kX64Cmp, cont);
}

bool MatchHeapObjectEqual(InstructionSelectorT<TurbofanAdapter>* selector,
                          Node* node, Node** left, Handle<HeapObject>* right) {
  DCHECK_EQ(node->opcode(), IrOpcode::kWord32Equal);
  CompressedHeapObjectBinopMatcher m(node);
  if (m.right().HasResolvedValue()) {
    *left = m.left().node();
    *right = m.right().ResolvedValue();
    return true;
  }
  HeapObjectBinopMatcher m2(node);
  if (m2.right().HasResolvedValue()) {
    *left = m2.left().node();
    *right = m2.right().ResolvedValue();
    return true;
  }
  return false;
}

bool MatchHeapObjectEqual(InstructionSelectorT<TurboshaftAdapter>* selector,
                          turboshaft::OpIndex node, turboshaft::OpIndex* left,
                          Handle<HeapObject>* right) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  const ComparisonOp& equal = selector->Get(node).Cast<ComparisonOp>();
  DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
  if (selector->MatchHeapConstant(equal.right(), right)) {
    *left = equal.left();
    return true;
  }
  return false;
}

template <typename Adapter>
void VisitWord32EqualImpl(InstructionSelectorT<Adapter>* selector,
                          typename Adapter::node_t node,
                          FlagsContinuationT<Adapter>* cont) {
  if (COMPRESS_POINTERS_BOOL && selector->isolate()) {
    X64OperandGeneratorT<Adapter> g(selector);
    const RootsTable& roots_table = selector->isolate()->roots_table();
    RootIndex root_index;
    typename Adapter::node_t left;
    Handle<HeapObject> right;
    // HeapConstants and CompressedHeapConstants can be treated the same when
    // using them as an input to a 32-bit comparison. Check whether either is
    // present.
    if (MatchHeapObjectEqual(selector, node, &left, &right)) {
      if (roots_table.IsRootHandle(right, &root_index)) {
        DCHECK(Adapter::valid(left));
        if (RootsTable::IsReadOnly(root_index) &&
            (V8_STATIC_ROOTS_BOOL || !selector->isolate()->bootstrapper())) {
          return VisitCompare(
              selector, kX64Cmp32, g.UseRegister(left),
              g.TempImmediate(MacroAssemblerBase::ReadOnlyRootPtr(
                  root_index, selector->isolate())),
              cont);
        }
        if (selector->CanUseRootsRegister()) {
          InstructionCode opcode =
              kX64Cmp32 | AddressingModeField::encode(kMode_Root);
          return VisitCompare(
              selector, opcode,
              g.TempImmediate(
                  MacroAssemblerBase::RootRegisterOffsetForRootIndex(
                      root_index)),
              g.UseRegister(left), cont);
        }
      }
    }
  }
  VisitWordCompare(selector, node, kX64Cmp32, cont);
}

void VisitCompareZero(InstructionSelectorT<TurboshaftAdapter>* selector,
                      turboshaft::OpIndex user, turboshaft::OpIndex node,
                      InstructionCode opcode,
                      FlagsContinuationT<TurboshaftAdapter>* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  X64OperandGeneratorT<TurboshaftAdapter> g(selector);
  const Operation& op = selector->turboshaft_graph()->Get(node);
  if (cont->IsBranch() &&
      (cont->condition() == kNotEqual || cont->condition() == kEqual)) {
    if (const WordBinopOp* binop = op.TryCast<WordBinopOp>()) {
      if (selector->IsOnlyUserOfNodeInSameBlock(user, node)) {
        const bool is64 = binop->rep == WordRepresentation::Word64();
        switch (binop->kind) {
          case WordBinopOp::Kind::kAdd:
            return VisitBinop(selector, node, is64 ? kX64Add : kX64Add32, cont);
          case WordBinopOp::Kind::kSub:
            return VisitBinop(selector, node, is64 ? kX64Sub : kX64Sub32, cont);
          case WordBinopOp::Kind::kBitwiseAnd:
            return VisitBinop(selector, node, is64 ? kX64And : kX64And32, cont);
          case WordBinopOp::Kind::kBitwiseOr:
            return VisitBinop(selector, node, is64 ? kX64Or : kX64Or32, cont);
          default:
            break;
        }
      }
    } else if (const ShiftOp* shift = op.TryCast<ShiftOp>()) {
      if (selector->IsOnlyUserOfNodeInSameBlock(user, node)) {
        const bool is64 = shift->rep == WordRepresentation::Word64();
        switch (shift->kind) {
          case ShiftOp::Kind::kShiftLeft:
            if (TryVisitWordShift(selector, node, is64 ? 64 : 32,
                                  is64 ? kX64Shl : kX64Shl32, cont)) {
              return;
            }
            break;
          case ShiftOp::Kind::kShiftRightLogical:
            if (TryVisitWordShift(selector, node, is64 ? 64 : 32,
                                  is64 ? kX64Shr : kX64Shr32, cont)) {
              return;
            }
            break;
          default:
            break;
        }
      }
    }
  }

  int effect_level = selector->GetEffectLevel(node, cont);
  if (const auto load = op.TryCast<turboshaft::LoadOp>()) {
    if (load->loaded_rep == turboshaft::MemoryRepresentation::Int8() ||
        load->loaded_rep == turboshaft::MemoryRepresentation::Uint8()) {
      if (opcode == kX64Cmp32) {
        opcode = kX64Cmp8;
      } else if (opcode == kX64Test32) {
        opcode = kX64Test8;
      }
    } else if (load->loaded_rep == turboshaft::MemoryRepresentation::Int16() ||
               load->loaded_rep == turboshaft::MemoryRepresentation::Uint16()) {
      if (opcode == kX64Cmp32) {
        opcode = kX64Cmp16;
      } else if (opcode == kX64Test32) {
        opcode = kX64Test16;
      }
    }
  }
  if (g.CanBeMemoryOperand(opcode, user, node, effect_level)) {
    VisitCompareWithMemoryOperand(selector, opcode, node, g.TempImmediate(0),
                                  cont);
  } else {
    VisitCompare(selector, opcode, g.Use(node), g.TempImmediate(0), cont);
  }
}

// Shared routine for comparison with zero.
void VisitCompareZero(InstructionSelectorT<TurbofanAdapter>* selector,
                      Node* user, Node* node, InstructionCode opcode,
                      FlagsContinuationT<TurbofanAdapter>* cont) {
  X64OperandGeneratorT<TurbofanAdapter> g(selector);
  if (cont->IsBranch() &&
      (cont->condition() == kNotEqual || cont->condition() == kEqual)) {
    switch (node->opcode()) {
#define FLAGS_SET_BINOP_LIST(V)        \
  V(kInt32Add, VisitBinop, kX64Add32)  \
  V(kInt32Sub, VisitBinop, kX64Sub32)  \
  V(kWord32And, VisitBinop, kX64And32) \
  V(kWord32Or, VisitBinop, kX64Or32)   \
  V(kInt64Add, VisitBinop, kX64Add)    \
  V(kInt64Sub, VisitBinop, kX64Sub)    \
  V(kWord64And, VisitBinop, kX64And)   \
  V(kWord64Or, VisitBinop, kX64Or)
#define FLAGS_SET_BINOP(opcode, Visit, archOpcode)           \
  case IrOpcode::opcode:                                     \
    if (selector->IsOnlyUserOfNodeInSameBlock(user, node)) { \
      return Visit(selector, node, archOpcode, cont);        \
    }                                                        \
    break;
      FLAGS_SET_BINOP_LIST(FLAGS_SET_BINOP)
#undef FLAGS_SET_BINOP_LIST
#undef FLAGS_SET_BINOP

// Skip Word64Sar/Word32Sar since no instruction reduction in most cases.
#define FLAGS_SET_SHIFT_LIST(V) \
  V(kWord32Shl, 32, kX64Shl32)  \
  V(kWord32Shr, 32, kX64Shr32)  \
  V(kWord64Shl, 64, kX64Shl)    \
  V(kWord64Shr, 64, kX64Shr)
#define FLAGS_SET_SHIFT(opcode, bits, archOpcode)                            \
  case IrOpcode::opcode:                                                     \
    if (selector->IsOnlyUserOfNodeInSameBlock(user, node)) {                 \
      if (TryVisitWordShift(selector, node, bits, archOpcode, cont)) return; \
    }                                                                        \
    break;
      FLAGS_SET_SHIFT_LIST(FLAGS_SET_SHIFT)
#undef TRY_VISIT_WORD32_SHIFT
#undef TRY_VISIT_WORD64_SHIFT
#undef FLAGS_SET_SHIFT_LIST
#undef FLAGS_SET_SHIFT
      default:
        break;
    }
  }
  int effect_level = selector->GetEffectLevel(node, cont);
  if (node->opcode() == IrOpcode::kLoad ||
      node->opcode() == IrOpcode::kLoadImmutable) {
    switch (LoadRepresentationOf(node->op()).representation()) {
      case MachineRepresentation::kWord8:
        if (opcode == kX64Cmp32) {
          opcode = kX64Cmp8;
        } else if (opcode == kX64Test32) {
          opcode = kX64Test8;
        }
        break;
      case MachineRepresentation::kWord16:
        if (opcode == kX64Cmp32) {
          opcode = kX64Cmp16;
        } else if (opcode == kX64Test32) {
          opcode = kX64Test16;
        }
        break;
      default:
        break;
    }
  }
  if (g.CanBeMemoryOperand(opcode, user, node, effect_level)) {
    VisitCompareWithMemoryOperand(selector, opcode, node, g.TempImmediate(0),
                                  cont);
  } else {
    VisitCompare(selector, opcode, g.Use(node), g.TempImmediate(0), cont);
  }
}

// Shared routine for multiple float32 compare operations (inputs commuted).
template <typename Adapter>
void VisitFloat32Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  InstructionCode const opcode =
      selector->IsSupported(AVX) ? kAVXFloat32Cmp : kSSEFloat32Cmp;
  VisitCompare(selector, opcode, right, left, cont, false);
}

// Shared routine for multiple float64 compare operations (inputs commuted).
template <typename Adapter>
void VisitFloat64Compare(InstructionSelectorT<Adapter>* selector,
                         typename Adapter::node_t node,
                         FlagsContinuationT<Adapter>* cont) {
  auto left = selector->input_at(node, 0);
  auto right = selector->input_at(node, 1);
  InstructionCode const opcode =
      selector->IsSupported(AVX) ? kAVXFloat64Cmp : kSSEFloat64Cmp;
  VisitCompare(selector, opcode, right, left, cont, false);
}

// Shared routine for Word32/Word64 Atomic Binops
template <typename Adapter>
void VisitAtomicBinop(InstructionSelectorT<Adapter>* selector,
                      typename Adapter::node_t node, ArchOpcode opcode,
                      AtomicWidth width, MemoryAccessKind access_kind) {
  auto atomic_op = selector->atomic_rmw_view(node);
  X64OperandGeneratorT<Adapter> g(selector);
  AddressingMode addressing_mode;
  InstructionOperand inputs[] = {
      g.UseUniqueRegister(atomic_op.value()),
      g.UseUniqueRegister(atomic_op.base()),
      g.GetEffectiveIndexOperand(atomic_op.index(), &addressing_mode)};
  InstructionOperand outputs[] = {g.DefineAsFixed(node, rax)};
  InstructionOperand temps[] = {g.TempRegister()};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs,
                 arraysize(temps), temps);
}

// Shared routine for Word32/Word64 Atomic CmpExchg
template <typename Adapter>
void VisitAtomicCompareExchange(InstructionSelectorT<Adapter>* selector,
                                typename Adapter::node_t node,
                                ArchOpcode opcode, AtomicWidth width,
                                MemoryAccessKind access_kind) {
  auto atomic_op = selector->atomic_rmw_view(node);
  X64OperandGeneratorT<Adapter> g(selector);
  AddressingMode addressing_mode;
  InstructionOperand inputs[] = {
      g.UseFixed(atomic_op.expected(), rax),
      g.UseUniqueRegister(atomic_op.value()),
      g.UseUniqueRegister(atomic_op.base()),
      g.GetEffectiveIndexOperand(atomic_op.index(), &addressing_mode)};
  InstructionOperand outputs[] = {g.DefineAsFixed(node, rax)};
  InstructionCode code = opcode | AddressingModeField::encode(addressing_mode) |
                         AtomicWidthField::encode(width);
  if (access_kind == MemoryAccessKind::kProtected) {
    code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
  }
  selector->Emit(code, arraysize(outputs), outputs, arraysize(inputs), inputs);
}

}  // namespace

// Shared routine for word comparison against zero.
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWordCompareZero(
    node_t user, node_t value, FlagsContinuation* cont) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  // Try to combine with comparisons against 0 by simply inverting the branch.
  ConsumeEqualZero(&user, &value, cont);

  if (CanCover(user, value)) {
    const Operation& value_op = this->Get(value);
    if (const ComparisonOp* comparison = value_op.TryCast<ComparisonOp>()) {
      if (comparison->kind == ComparisonOp::Kind::kEqual) {
        switch (comparison->rep.MapTaggedToWord().value()) {
          case RegisterRepresentation::Word32():
            cont->OverwriteAndNegateIfEqual(kEqual);
            return VisitWord32EqualImpl(this, value, cont);
          case RegisterRepresentation::Word64(): {
            cont->OverwriteAndNegateIfEqual(kEqual);
            if (this->MatchIntegralZero(comparison->right())) {
              // Try to combine the branch with a comparison.
              if (CanCover(value, comparison->left())) {
                const Operation& left_op = this->Get(comparison->left());
                if (left_op.Is<Opmask::kWord64Sub>()) {
                  return VisitWordCompare(this, comparison->left(), kX64Cmp,
                                          cont);
                } else if (left_op.Is<Opmask::kWord64BitwiseAnd>()) {
                  return VisitWordCompare(this, comparison->left(), kX64Test,
                                          cont);
                }
              }
              return VisitCompareZero(this, value, comparison->left(), kX64Cmp,
                                      cont);
            }
            return VisitWord64EqualImpl(this, value, cont);
          }
          case RegisterRepresentation::Float32():
            cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
            return VisitFloat32Compare(this, value, cont);
          case RegisterRepresentation::Float64(): {
            bool is_self_compare =
                this->input_at(value, 0) == this->input_at(value, 1);
            cont->OverwriteAndNegateIfEqual(is_self_compare ? kIsNotNaN
                                                            : kUnorderedEqual);
            return VisitFloat64Compare(this, value, cont);
          }
          default:
            break;
        }
      } else {
        switch (comparison->rep.MapTaggedToWord().value()) {
          case RegisterRepresentation::Word32(): {
            cont->OverwriteAndNegateIfEqual(
                GetComparisonFlagCondition(*comparison));
            return VisitWordCompare(this, value, kX64Cmp32, cont);
          }
          case RegisterRepresentation::Word64(): {
            cont->OverwriteAndNegateIfEqual(
                GetComparisonFlagCondition(*comparison));
            return VisitWordCompare(this, value, kX64Cmp, cont);
          }
          case RegisterRepresentation::Float32():
            if (comparison->kind == ComparisonOp::Kind::kSignedLessThan) {
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
              return VisitFloat32Compare(this, value, cont);
            } else {
              DCHECK_EQ(comparison->kind,
                        ComparisonOp::Kind::kSignedLessThanOrEqual);
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
              return VisitFloat32Compare(this, value, cont);
            }
          case RegisterRepresentation::Float64():
            if (comparison->kind == ComparisonOp::Kind::kSignedLessThan) {
              if (MatchZero(comparison->left())) {
                const Operation& right = this->Get(comparison->right());
                if (right.Is<Opmask::kFloat64Abs>()) {
                  // This matches the pattern
                  //
                  //   Float64LessThan(#0.0, Float64Abs(x))
                  //
                  // which TurboFan generates for NumberToBoolean in the general
                  // case, and which evaluates to false if x is 0, -0 or NaN. We
                  // can compile this to a simple (v)ucomisd using not_equal
                  // flags condition, which avoids the costly Float64Abs.
                  cont->OverwriteAndNegateIfEqual(kNotEqual);
                  InstructionCode const opcode =
                      IsSupported(AVX) ? kAVXFloat64Cmp : kSSEFloat64Cmp;
                  return VisitCompare(this, opcode, comparison->left(),
                                      right.Cast<FloatUnaryOp>().input(), cont,
                                      false);
                }
              }
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
              return VisitFloat64Compare(this, value, cont);
            } else {
              DCHECK_EQ(comparison->kind,
                        ComparisonOp::Kind::kSignedLessThanOrEqual);
              cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
              return VisitFloat64Compare(this, value, cont);
            }
          default:
            break;
        }
      }
    } else if (value_op.Is<Opmask::kWord32Sub>()) {
      return VisitWordCompare(this, value, kX64Cmp32, cont);
    } else if (value_op.Is<Opmask::kWord32BitwiseAnd>()) {
      return VisitWordCompare(this, value, kX64Test32, cont);
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
            const bool is64 = binop->rep == WordRepresentation::Word64();
            cont->OverwriteAndNegateIfEqual(kOverflow);
            switch (binop->kind) {
              case OverflowCheckedBinopOp::Kind::kSignedAdd:
                return VisitBinop(this, node, is64 ? kX64Add : kX64Add32, cont);
              case OverflowCheckedBinopOp::Kind::kSignedSub:
                return VisitBinop(this, node, is64 ? kX64Sub : kX64Sub32, cont);
              case OverflowCheckedBinopOp::Kind::kSignedMul:
                return VisitBinop(this, node, is64 ? kX64Imul : kX64Imul32,
                                  cont);
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
  VisitCompareZero(this, user, value, kX64Cmp32, cont);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWordCompareZero(
    Node* user, Node* value, FlagsContinuation* cont) {
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
        return VisitWord32EqualImpl(this, value, cont);
      case IrOpcode::kInt32LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWordCompare(this, value, kX64Cmp32, cont);
      case IrOpcode::kInt32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWordCompare(this, value, kX64Cmp32, cont);
      case IrOpcode::kUint32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWordCompare(this, value, kX64Cmp32, cont);
      case IrOpcode::kUint32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWordCompare(this, value, kX64Cmp32, cont);
      case IrOpcode::kWord64Equal: {
        cont->OverwriteAndNegateIfEqual(kEqual);
        Int64BinopMatcher m(value);
        if (m.right().Is(0)) {
          // Try to combine the branch with a comparison.
          Node* const eq_user = m.node();
          Node* const eq_value = m.left().node();
          if (CanCover(eq_user, eq_value)) {
            switch (eq_value->opcode()) {
              case IrOpcode::kInt64Sub:
                return VisitWordCompare(this, eq_value, kX64Cmp, cont);
              case IrOpcode::kWord64And:
                return VisitWordCompare(this, eq_value, kX64Test, cont);
              default:
                break;
            }
          }
          return VisitCompareZero(this, eq_user, eq_value, kX64Cmp, cont);
        }
        return VisitWord64EqualImpl(this, value, cont);
      }
      case IrOpcode::kInt64LessThan:
        cont->OverwriteAndNegateIfEqual(kSignedLessThan);
        return VisitWordCompare(this, value, kX64Cmp, cont);
      case IrOpcode::kInt64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kSignedLessThanOrEqual);
        return VisitWordCompare(this, value, kX64Cmp, cont);
      case IrOpcode::kUint64LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThan);
        return VisitWordCompare(this, value, kX64Cmp, cont);
      case IrOpcode::kUint64LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedLessThanOrEqual);
        return VisitWordCompare(this, value, kX64Cmp, cont);
      case IrOpcode::kFloat32Equal:
        cont->OverwriteAndNegateIfEqual(kUnorderedEqual);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat32LessThan:
        cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat32LessThanOrEqual:
        cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThanOrEqual);
        return VisitFloat32Compare(this, value, cont);
      case IrOpcode::kFloat64Equal: {
        bool is_self_compare =
            this->input_at(value, 0) == this->input_at(value, 1);
        cont->OverwriteAndNegateIfEqual(is_self_compare ? kIsNotNaN
                                                        : kUnorderedEqual);
        return VisitFloat64Compare(this, value, cont);
      }
      case IrOpcode::kFloat64LessThan: {
        Float64BinopMatcher m(value);
        if (m.left().Is(0.0) && m.right().IsFloat64Abs()) {
          // This matches the pattern
          //
          //   Float64LessThan(#0.0, Float64Abs(x))
          //
          // which TurboFan generates for NumberToBoolean in the general case,
          // and which evaluates to false if x is 0, -0 or NaN. We can compile
          // this to a simple (v)ucomisd using not_equal flags condition, which
          // avoids the costly Float64Abs.
          cont->OverwriteAndNegateIfEqual(kNotEqual);
          InstructionCode const opcode =
              IsSupported(AVX) ? kAVXFloat64Cmp : kSSEFloat64Cmp;
          return VisitCompare(this, opcode, m.left().node(),
                              m.right().InputAt(0), cont, false);
        }
        cont->OverwriteAndNegateIfEqual(kUnsignedGreaterThan);
        return VisitFloat64Compare(this, value, cont);
      }
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
                return VisitBinop(this, node, kX64Add32, cont);
              case IrOpcode::kInt32SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kX64Sub32, cont);
              case IrOpcode::kInt32MulWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kX64Imul32, cont);
              case IrOpcode::kInt64AddWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kX64Add, cont);
              case IrOpcode::kInt64SubWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kX64Sub, cont);
              case IrOpcode::kInt64MulWithOverflow:
                cont->OverwriteAndNegateIfEqual(kOverflow);
                return VisitBinop(this, node, kX64Imul, cont);
              default:
                break;
            }
          }
        }
        break;
      case IrOpcode::kInt32Sub:
        return VisitWordCompare(this, value, kX64Cmp32, cont);
      case IrOpcode::kWord32And:
        return VisitWordCompare(this, value, kX64Test32, cont);
      case IrOpcode::kStackPointerGreaterThan:
        cont->OverwriteAndNegateIfEqual(kStackPointerGreaterThanCondition);
        return VisitStackPointerGreaterThan(value, cont);
      default:
        break;
    }
  }

  // Branch could not be combined with a compare, emit compare against 0.
  VisitCompareZero(this, user, value, kX64Cmp32, cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSwitch(node_t node,
                                                const SwitchInfo& sw) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
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
      InstructionOperand index_operand = g.TempRegister();
      if (sw.min_value()) {
        // The leal automatically zero extends, so result is a valid 64-bit
        // index.
        Emit(kX64Lea32 | AddressingModeField::encode(kMode_MRI), index_operand,
             value_operand, g.TempImmediate(-sw.min_value()));
      } else {
        // Zero extend, because we use it as 64-bit index into the jump table.
        if (ZeroExtendsWord32ToWord64(this->input_at(node, 0))) {
          // Input value has already been zero-extended.
          index_operand = value_operand;
        } else {
          Emit(kX64Movl, index_operand, value_operand);
        }
      }
      // Generate a table lookup.
      return EmitTableSwitch(sw, index_operand);
    }
  }

  // Generate a tree of conditional jumps.
  return EmitBinarySearchSwitch(sw, value_operand);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32Equal(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  const ComparisonOp& equal = this->Get(node).Cast<ComparisonOp>();
  DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
  DCHECK(equal.rep == RegisterRepresentation::Word32() ||
         equal.rep == RegisterRepresentation::Tagged());
  if (MatchIntegralZero(equal.right())) {
    return VisitWordCompareZero(node, equal.left(), &cont);
  }
  VisitWord32EqualImpl(this, node, &cont);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) {
    return VisitWordCompareZero(m.node(), m.left().node(), &cont);
  }
  VisitWord32EqualImpl(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWordCompare(this, node, kX64Cmp32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kX64Cmp32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWordCompare(this, node, kX64Cmp32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint32LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kX64Cmp32, &cont);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64Equal(node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  const ComparisonOp& equal = this->Get(node).Cast<ComparisonOp>();
  DCHECK_EQ(equal.kind, ComparisonOp::Kind::kEqual);
  DCHECK(equal.rep == RegisterRepresentation::Word64() ||
         equal.rep == RegisterRepresentation::Tagged());
  if (MatchIntegralZero(equal.right())) {
    if (CanCover(node, equal.left())) {
      const Operation& left_op = this->Get(equal.left());
      if (left_op.Is<Opmask::kWord64Sub>()) {
        return VisitWordCompare(this, equal.left(), kX64Cmp, &cont);
      } else if (left_op.Is<Opmask::kWord64BitwiseAnd>()) {
        return VisitWordCompare(this, equal.left(), kX64Test, &cont);
      }
    }
  }
  VisitWord64EqualImpl(this, node, &cont);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64Equal(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kEqual, node);
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) {
    // Try to combine the equality check with a comparison.
    Node* const user = m.node();
    Node* const value = m.left().node();
    if (CanCover(user, value)) {
      switch (value->opcode()) {
        case IrOpcode::kInt64Sub:
          return VisitWordCompare(this, value, kX64Cmp, &cont);
        case IrOpcode::kWord64And:
          return VisitWordCompare(this, value, kX64Test, &cont);
        default:
          break;
      }
    }
  }
  VisitWord64EqualImpl(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32AddWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Add32, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Add32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt32SubWithOverflow(node_t node) {
  node_t ovf = FindProjection(node, 1);
  if (Adapter::valid(ovf)) {
    FlagsContinuation cont = FlagsContinuation::ForSet(kOverflow, ovf);
    return VisitBinop(this, node, kX64Sub32, &cont);
  }
  FlagsContinuation cont;
  VisitBinop(this, node, kX64Sub32, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kSignedLessThan, node);
  VisitWordCompare(this, node, kX64Cmp, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitInt64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kSignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kX64Cmp, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThan(node_t node) {
  FlagsContinuation cont = FlagsContinuation::ForSet(kUnsignedLessThan, node);
  VisitWordCompare(this, node, kX64Cmp, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitUint64LessThanOrEqual(node_t node) {
  FlagsContinuation cont =
      FlagsContinuation::ForSet(kUnsignedLessThanOrEqual, node);
  VisitWordCompare(this, node, kX64Cmp, &cont);
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
  bool is_self_compare = this->input_at(node, 0) == this->input_at(node, 1);
  FlagsContinuation cont = FlagsContinuation::ForSet(
      is_self_compare ? kIsNotNaN : kUnorderedEqual, node);
  VisitFloat64Compare(this, node, &cont);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64LessThan(node_t node) {
  // Check for the pattern
  //
  //   Float64LessThan(#0.0, Float64Abs(x))
  //
  // which TurboFan generates for NumberToBoolean in the general case,
  // and which evaluates to false if x is 0, -0 or NaN. We can compile
  // this to a simple (v)ucomisd using not_equal flags condition, which
  // avoids the costly Float64Abs.
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    const ComparisonOp& cmp = this->Get(node).template Cast<ComparisonOp>();
    DCHECK_EQ(cmp.rep, RegisterRepresentation::Float64());
    DCHECK_EQ(cmp.kind, ComparisonOp::Kind::kSignedLessThan);
    if (this->MatchZero(cmp.left())) {
      if (const FloatUnaryOp* right_op =
              this->Get(cmp.right()).template TryCast<Opmask::kFloat64Abs>()) {
        FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, node);
        InstructionCode const opcode =
            IsSupported(AVX) ? kAVXFloat64Cmp : kSSEFloat64Cmp;
        return VisitCompare(this, opcode, cmp.left(), right_op->input(), &cont,
                            false);
      }
    }
  } else {
    Float64BinopMatcher m(node);
    if (m.left().Is(0.0) && m.right().IsFloat64Abs()) {
      FlagsContinuation cont = FlagsContinuation::ForSet(kNotEqual, node);
      InstructionCode const opcode =
          IsSupported(AVX) ? kAVXFloat64Cmp : kSSEFloat64Cmp;
      return VisitCompare(this, opcode, m.left().node(), m.right().InputAt(0),
                          &cont, false);
    }
  }
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

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitBitcastWord32PairToFloat64(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const auto& bitcast =
      this->Cast<turboshaft::BitcastWord32PairToFloat64Op>(node);
  node_t hi = bitcast.high_word32();
  node_t lo = bitcast.low_word32();

  // TODO(nicohartmann@): We could try to emit a better sequence here.
  InstructionOperand zero = sequence()->AddImmediate(Constant(0.0));
  InstructionOperand temp = g.TempDoubleRegister();
  Emit(kSSEFloat64InsertHighWord32, temp, zero, g.Use(hi));
  Emit(kSSEFloat64InsertLowWord32, g.DefineSameAsFirst(node), temp, g.Use(lo));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitFloat64InsertLowWord32(
    node_t node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  node_t left = this->input_at(node, 0);
  node_t right = this->input_at(node, 1);
  Float64Matcher mleft(left);
  if (mleft.HasResolvedValue() &&
      (base::bit_cast<uint64_t>(mleft.ResolvedValue()) >> 32) == 0u) {
    Emit(kSSEFloat64LoadLowWord32, g.DefineAsRegister(node), g.Use(right));
    return;
  }
  Emit(kSSEFloat64InsertLowWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.Use(right));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitFloat64InsertHighWord32(
    node_t node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  node_t left = this->input_at(node, 0);
  node_t right = this->input_at(node, 1);
  Emit(kSSEFloat64InsertHighWord32, g.DefineSameAsFirst(node),
       g.UseRegister(left), g.Use(right));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitFloat64SilenceNaN(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kSSEFloat64SilenceNaN, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitMemoryBarrier(node_t node) {
  AtomicMemoryOrder order;
  if constexpr (Adapter::IsTurboshaft) {
    order = this->Get(node)
                .template Cast<turboshaft::MemoryBarrierOp>()
                .memory_order;
  } else {
    order = OpParameter<AtomicMemoryOrder>(node->op());
  }
  // x64 is no weaker than release-acquire and only needs to emit an
  // instruction for SeqCst memory barriers.
  if (order == AtomicMemoryOrder::kSeqCst) {
    X64OperandGeneratorT<Adapter> g(this);
    Emit(kX64MFence, g.NoOutput());
    return;
  }
  DCHECK_EQ(AtomicMemoryOrder::kAcqRel, order);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicLoad(node_t node) {
  LoadRepresentation load_rep = this->load_view(node).loaded_rep();
  DCHECK(IsIntegral(load_rep.representation()) ||
         IsAnyTagged(load_rep.representation()) ||
         (COMPRESS_POINTERS_BOOL &&
          CanBeCompressedPointer(load_rep.representation())));
  DCHECK_NE(load_rep.representation(), MachineRepresentation::kWord64);
  DCHECK(!load_rep.IsMapWord());
  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit MOV.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  VisitLoad(node, node, GetLoadOpcode(load_rep));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicLoad(node_t node) {
  LoadRepresentation load_rep = this->load_view(node).loaded_rep();
  DCHECK(!load_rep.IsMapWord());
  // The memory order is ignored as both acquire and sequentially consistent
  // loads can emit MOV.
  // https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html
  VisitLoad(node, node, GetLoadOpcode(load_rep));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord32AtomicStore(node_t node) {
  auto store = this->store_view(node);
  DCHECK_NE(store.stored_rep().representation(),
            MachineRepresentation::kWord64);
  DCHECK_IMPLIES(
      CanBeTaggedOrCompressedPointer(store.stored_rep().representation()),
      kTaggedSize == 4);
  VisitStoreCommon(this, store);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitWord64AtomicStore(node_t node) {
  auto store = this->store_view(node);
  DCHECK_IMPLIES(
      CanBeTaggedOrCompressedPointer(store.stored_rep().representation()),
      kTaggedSize == 8);
  VisitStoreCommon(this, store);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicExchange(
    Node* node) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
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
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicExchange(
    Node* node) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kX64Word64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64, params.kind());
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
    opcode = kX64Word64AtomicExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicExchange(this, node, opcode, AtomicWidth::kWord64,
                      atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicCompareExchange(
    Node* node) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
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
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicCompareExchange(
    Node* node) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
  if (params.type() == MachineType::Uint8()) {
    opcode = kAtomicCompareExchangeUint8;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = kAtomicCompareExchangeUint16;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = kAtomicCompareExchangeWord32;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = kX64Word64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
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
    opcode = kX64Word64AtomicCompareExchangeUint64;
  } else {
    UNREACHABLE();
  }
  VisitAtomicCompareExchange(this, node, opcode, AtomicWidth::kWord64,
                             atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord32AtomicBinaryOperation(
    turboshaft::OpIndex node, ArchOpcode int8_op, ArchOpcode uint8_op,
    ArchOpcode int16_op, ArchOpcode uint16_op, ArchOpcode word32_op) {
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
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord32AtomicBinaryOperation(
    Node* node, ArchOpcode int8_op, ArchOpcode uint8_op, ArchOpcode int16_op,
    ArchOpcode uint16_op, ArchOpcode word32_op) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
  if (params.type() == MachineType::Int8()) {
    opcode = int8_op;
  } else if (params.type() == MachineType::Uint8()) {
    opcode = uint8_op;
  } else if (params.type() == MachineType::Int16()) {
    opcode = int16_op;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = uint16_op;
  } else if (params.type() == MachineType::Int32()
    || params.type() == MachineType::Uint32()) {
    opcode = word32_op;
  } else {
    UNREACHABLE();
  }
  VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord32, params.kind());
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

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitWord64AtomicBinaryOperation(
    node_t node, ArchOpcode uint8_op, ArchOpcode uint16_op,
    ArchOpcode uint32_op, ArchOpcode word64_op) {
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
    opcode = word64_op;
  } else {
    UNREACHABLE();
  }
  VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64,
                   atomic_op.memory_access_kind);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitWord64AtomicBinaryOperation(
    Node* node, ArchOpcode uint8_op, ArchOpcode uint16_op, ArchOpcode uint32_op,
    ArchOpcode word64_op) {
  AtomicOpParameters params = AtomicOpParametersOf(node->op());
  ArchOpcode opcode;
  if (params.type() == MachineType::Uint8()) {
    opcode = uint8_op;
  } else if (params.type() == MachineType::Uint16()) {
    opcode = uint16_op;
  } else if (params.type() == MachineType::Uint32()) {
    opcode = uint32_op;
  } else if (params.type() == MachineType::Uint64()) {
    opcode = word64_op;
  } else {
    UNREACHABLE();
  }
  VisitAtomicBinop(this, node, opcode, AtomicWidth::kWord64, params.kind());
}

#define VISIT_ATOMIC_BINOP(op)                                                 \
  template <typename Adapter>                                                  \
  void InstructionSelectorT<Adapter>::VisitWord64Atomic##op(node_t node) {     \
    VisitWord64AtomicBinaryOperation(node, kAtomic##op##Uint8,                 \
                                     kAtomic##op##Uint16, kAtomic##op##Word32, \
                                     kX64Word64Atomic##op##Uint64);            \
  }
VISIT_ATOMIC_BINOP(Add)
VISIT_ATOMIC_BINOP(Sub)
VISIT_ATOMIC_BINOP(And)
VISIT_ATOMIC_BINOP(Or)
VISIT_ATOMIC_BINOP(Xor)
#undef VISIT_ATOMIC_BINOP

#ifdef V8_ENABLE_WEBASSEMBLY
#define SIMD_BINOP_SSE_AVX_LIST(V) \
  V(I64x2ExtMulLowI32x4S)          \
  V(I64x2ExtMulHighI32x4S)         \
  V(I64x2ExtMulLowI32x4U)          \
  V(I64x2ExtMulHighI32x4U)         \
  V(I32x4DotI16x8S)                \
  V(I32x8DotI16x16S)               \
  V(I32x4ExtMulLowI16x8S)          \
  V(I32x4ExtMulHighI16x8S)         \
  V(I32x4ExtMulLowI16x8U)          \
  V(I32x4ExtMulHighI16x8U)         \
  V(I16x8SConvertI32x4)            \
  V(I16x8UConvertI32x4)            \
  V(I16x8ExtMulLowI8x16S)          \
  V(I16x8ExtMulHighI8x16S)         \
  V(I16x8ExtMulLowI8x16U)          \
  V(I16x8ExtMulHighI8x16U)         \
  V(I16x8Q15MulRSatS)              \
  V(I16x8RelaxedQ15MulRS)          \
  V(I8x16SConvertI16x8)            \
  V(I8x16UConvertI16x8)            \
  V(I16x16SConvertI32x8)           \
  V(I16x16UConvertI32x8)           \
  V(I8x32SConvertI16x16)           \
  V(I8x32UConvertI16x16)           \
  V(I64x4ExtMulI32x4S)             \
  V(I64x4ExtMulI32x4U)             \
  V(I32x8ExtMulI16x8S)             \
  V(I32x8ExtMulI16x8U)             \
  V(I16x16ExtMulI8x16S)            \
  V(I16x16ExtMulI8x16U)

#define SIMD_BINOP_SSE_AVX_LANE_SIZE_VECTOR_LENGTH_LIST(V)  \
  V(F64x2Add, FAdd, kL64, kV128)                            \
  V(F64x4Add, FAdd, kL64, kV256)                            \
  V(F32x4Add, FAdd, kL32, kV128)                            \
  V(F32x8Add, FAdd, kL32, kV256)                            \
  V(I64x2Add, IAdd, kL64, kV128)                            \
  V(I64x4Add, IAdd, kL64, kV256)                            \
  V(I32x8Add, IAdd, kL32, kV256)                            \
  V(I16x16Add, IAdd, kL16, kV256)                           \
  V(I8x32Add, IAdd, kL8, kV256)                             \
  V(I32x4Add, IAdd, kL32, kV128)                            \
  V(I16x8Add, IAdd, kL16, kV128)                            \
  V(I8x16Add, IAdd, kL8, kV128)                             \
  V(F64x4Sub, FSub, kL64, kV256)                            \
  V(F64x2Sub, FSub, kL64, kV128)                            \
  V(F32x4Sub, FSub, kL32, kV128)                            \
  V(F32x8Sub, FSub, kL32, kV256)                            \
  V(I64x2Sub, ISub, kL64, kV128)                            \
  V(I64x4Sub, ISub, kL64, kV256)                            \
  V(I32x8Sub, ISub, kL32, kV256)                            \
  V(I16x16Sub, ISub, kL16, kV256)                           \
  V(I8x32Sub, ISub, kL8, kV256)                             \
  V(I32x4Sub, ISub, kL32, kV128)                            \
  V(I16x8Sub, ISub, kL16, kV128)                            \
  V(I8x16Sub, ISub, kL8, kV128)                             \
  V(F64x2Mul, FMul, kL64, kV128)                            \
  V(F32x4Mul, FMul, kL32, kV128)                            \
  V(F64x4Mul, FMul, kL64, kV256)                            \
  V(F32x8Mul, FMul, kL32, kV256)                            \
  V(I32x8Mul, IMul, kL32, kV256)                            \
  V(I16x16Mul, IMul, kL16, kV256)                           \
  V(I32x4Mul, IMul, kL32, kV128)                            \
  V(I16x8Mul, IMul, kL16, kV128)                            \
  V(F64x2Div, FDiv, kL64, kV128)                            \
  V(F32x4Div, FDiv, kL32, kV128)                            \
  V(F64x4Div, FDiv, kL64, kV256)                            \
  V(F32x8Div, FDiv, kL32, kV256)                            \
  V(I16x8AddSatS, IAddSatS, kL16, kV128)                    \
  V(I16x16AddSatS, IAddSatS, kL16, kV256)                   \
  V(I8x16AddSatS, IAddSatS, kL8, kV128)                     \
  V(I8x32AddSatS, IAddSatS, kL8, kV256)                     \
  V(I16x8SubSatS, ISubSatS, kL16, kV128)                    \
  V(I16x16SubSatS, ISubSatS, kL16, kV256)                   \
  V(I8x16SubSatS, ISubSatS, kL8, kV128)                     \
  V(I8x32SubSatS, ISubSatS, kL8, kV256)                     \
  V(I16x8AddSatU, IAddSatU, kL16, kV128)                    \
  V(I16x16AddSatU, IAddSatU, kL16, kV256)                   \
  V(I8x16AddSatU, IAddSatU, kL8, kV128)                     \
  V(I8x32AddSatU, IAddSatU, kL8, kV256)                     \
  V(I16x8SubSatU, ISubSatU, kL16, kV128)                    \
  V(I16x16SubSatU, ISubSatU, kL16, kV256)                   \
  V(I8x16SubSatU, ISubSatU, kL8, kV128)                     \
  V(I8x32SubSatU, ISubSatU, kL8, kV256)                     \
  V(F64x2Eq, FEq, kL64, kV128)                              \
  V(F32x4Eq, FEq, kL32, kV128)                              \
  V(F32x8Eq, FEq, kL32, kV256)                              \
  V(F64x4Eq, FEq, kL64, kV256)                              \
  V(I8x32Eq, IEq, kL8, kV256)                               \
  V(I16x16Eq, IEq, kL16, kV256)                             \
  V(I32x8Eq, IEq, kL32, kV256)                              \
  V(I64x4Eq, IEq, kL64, kV256)                              \
  V(I64x2Eq, IEq, kL64, kV128)                              \
  V(I32x4Eq, IEq, kL32, kV128)                              \
  V(I16x8Eq, IEq, kL16, kV128)                              \
  V(I8x16Eq, IEq, kL8, kV128)                               \
  V(F64x2Ne, FNe, kL64, kV128)                              \
  V(F32x4Ne, FNe, kL32, kV128)                              \
  V(F32x8Ne, FNe, kL32, kV256)                              \
  V(F64x4Ne, FNe, kL64, kV256)                              \
  V(I32x4GtS, IGtS, kL32, kV128)                            \
  V(I16x8GtS, IGtS, kL16, kV128)                            \
  V(I8x16GtS, IGtS, kL8, kV128)                             \
  V(I8x32GtS, IGtS, kL8, kV256)                             \
  V(I16x16GtS, IGtS, kL16, kV256)                           \
  V(I32x8GtS, IGtS, kL32, kV256)                            \
  V(I64x4GtS, IGtS, kL64, kV256)                            \
  V(F64x2Lt, FLt, kL64, kV128)                              \
  V(F32x4Lt, FLt, kL32, kV128)                              \
  V(F64x4Lt, FLt, kL64, kV256)                              \
  V(F32x8Lt, FLt, kL32, kV256)                              \
  V(F64x2Le, FLe, kL64, kV128)                              \
  V(F32x4Le, FLe, kL32, kV128)                              \
  V(F64x4Le, FLe, kL64, kV256)                              \
  V(F32x8Le, FLe, kL32, kV256)                              \
  V(I32x4MinS, IMinS, kL32, kV128)                          \
  V(I16x8MinS, IMinS, kL16, kV128)                          \
  V(I8x16MinS, IMinS, kL8, kV128)                           \
  V(I32x4MinU, IMinU, kL32, kV128)                          \
  V(I16x8MinU, IMinU, kL16, kV128)                          \
  V(I8x16MinU, IMinU, kL8, kV128)                           \
  V(I32x4MaxS, IMaxS, kL32, kV128)                          \
  V(I16x8MaxS, IMaxS, kL16, kV128)                          \
  V(I8x16MaxS, IMaxS, kL8, kV128)                           \
  V(I32x4MaxU, IMaxU, kL32, kV128)                          \
  V(I16x8MaxU, IMaxU, kL16, kV128)                          \
  V(I8x16MaxU, IMaxU, kL8, kV128)                           \
  V(I32x8MinS, IMinS, kL32, kV256)                          \
  V(I16x16MinS, IMinS, kL16, kV256)                         \
  V(I8x32MinS, IMinS, kL8, kV256)                           \
  V(I32x8MinU, IMinU, kL32, kV256)                          \
  V(I16x16MinU, IMinU, kL16, kV256)                         \
  V(I8x32MinU, IMinU, kL8, kV256)                           \
  V(I32x8MaxS, IMaxS, kL32, kV256)                          \
  V(I16x16MaxS, IMaxS, kL16, kV256)                         \
  V(I8x32MaxS, IMaxS, kL8, kV256)                           \
  V(I32x8MaxU, IMaxU, kL32, kV256)                          \
  V(I16x16MaxU, IMaxU, kL16, kV256)                         \
  V(I8x32MaxU, IMaxU, kL8, kV256)                           \
  V(I16x8RoundingAverageU, IRoundingAverageU, kL16, kV128)  \
  V(I16x16RoundingAverageU, IRoundingAverageU, kL16, kV256) \
  V(I8x16RoundingAverageU, IRoundingAverageU, kL8, kV128)   \
  V(I8x32RoundingAverageU, IRoundingAverageU, kL8, kV256)   \
  V(S128And, SAnd, kL8, kV128)                              \
  V(S256And, SAnd, kL8, kV256)                              \
  V(S128Or, SOr, kL8, kV128)                                \
  V(S256Or, SOr, kL8, kV256)                                \
  V(S128Xor, SXor, kL8, kV128)                              \
  V(S256Xor, SXor, kL8, kV256)

#define SIMD_F16x8_BINOP_LIST(V) \
  V(F16x8Add, FAdd)              \
  V(F16x8Sub, FSub)              \
  V(F16x8Mul, FMul)              \
  V(F16x8Div, FDiv)              \
  V(F16x8Min, FMin)              \
  V(F16x8Max, FMax)              \
  V(F16x8Eq, FEq)                \
  V(F16x8Ne, FNe)                \
  V(F16x8Lt, FLt)                \
  V(F16x8Le, FLe)

#define SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH_LIST(V) \
  V(F64x2Min, FMin, kL64, kV128)                   \
  V(F32x4Min, FMin, kL32, kV128)                   \
  V(F64x4Min, FMin, kL64, kV256)                   \
  V(F32x8Min, FMin, kL32, kV256)                   \
  V(F64x2Max, FMax, kL64, kV128)                   \
  V(F32x4Max, FMax, kL32, kV128)                   \
  V(F64x4Max, FMax, kL64, kV256)                   \
  V(F32x8Max, FMax, kL32, kV256)                   \
  V(I64x2Ne, INe, kL64, kV128)                     \
  V(I32x4Ne, INe, kL32, kV128)                     \
  V(I16x8Ne, INe, kL16, kV128)                     \
  V(I8x16Ne, INe, kL8, kV128)                      \
  V(I64x4Ne, INe, kL64, kV256)                     \
  V(I32x8Ne, INe, kL32, kV256)                     \
  V(I16x16Ne, INe, kL16, kV256)                    \
  V(I8x32Ne, INe, kL8, kV256)                      \
  V(I32x4GtU, IGtU, kL32, kV128)                   \
  V(I16x8GtU, IGtU, kL16, kV128)                   \
  V(I8x16GtU, IGtU, kL8, kV128)                    \
  V(I32x8GtU, IGtU, kL32, kV256)                   \
  V(I16x16GtU, IGtU, kL16, kV256)                  \
  V(I8x32GtU, IGtU, kL8, kV256)                    \
  V(I32x4GeS, IGeS, kL32, kV128)                   \
  V(I16x8GeS, IGeS, kL16, kV128)                   \
  V(I8x16GeS, IGeS, kL8, kV128)                    \
  V(I32x8GeS, IGeS, kL32, kV256)                   \
  V(I16x16GeS, IGeS, kL16, kV256)                  \
  V(I8x32GeS, IGeS, kL8, kV256)                    \
  V(I32x4GeU, IGeU, kL32, kV128)                   \
  V(I16x8GeU, IGeU, kL16, kV128)                   \
  V(I8x16GeU, IGeU, kL8, kV128)                    \
  V(I32x8GeU, IGeU, kL32, kV256)                   \
  V(I16x16GeU, IGeU, kL16, kV256)                  \
  V(I8x32GeU, IGeU, kL8, kV256)

#define SIMD_UNOP_LIST(V)   \
  V(F64x2ConvertLowI32x4S)  \
  V(F64x4ConvertI32x4S)     \
  V(F32x4SConvertI32x4)     \
  V(F32x8SConvertI32x8)     \
  V(F32x4DemoteF64x2Zero)   \
  V(F32x4DemoteF64x4)       \
  V(I16x8SConvertF16x8)     \
  V(I16x8UConvertF16x8)     \
  V(F16x8SConvertI16x8)     \
  V(F16x8UConvertI16x8)     \
  V(F16x8DemoteF32x4Zero)   \
  V(F32x4PromoteLowF16x8)   \
  V(I64x2SConvertI32x4Low)  \
  V(I64x2SConvertI32x4High) \
  V(I64x4SConvertI32x4)     \
  V(I64x2UConvertI32x4Low)  \
  V(I64x2UConvertI32x4High) \
  V(I64x4UConvertI32x4)     \
  V(I32x4SConvertI16x8Low)  \
  V(I32x4SConvertI16x8High) \
  V(I32x8SConvertI16x8)     \
  V(I32x4UConvertI16x8Low)  \
  V(I32x4UConvertI16x8High) \
  V(I32x8UConvertI16x8)     \
  V(I16x8SConvertI8x16Low)  \
  V(I16x8SConvertI8x16High) \
  V(I16x16SConvertI8x16)    \
  V(I16x8UConvertI8x16Low)  \
  V(I16x8UConvertI8x16High) \
  V(I16x16UConvertI8x16)

#define SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH_LIST(V) \
  V(F32x4Abs, FAbs, kL32, kV128)                  \
  V(I32x4Abs, IAbs, kL32, kV128)                  \
  V(F16x8Abs, FAbs, kL16, kV128)                  \
  V(I16x8Abs, IAbs, kL16, kV128)                  \
  V(I8x16Abs, IAbs, kL8, kV128)                   \
  V(F32x4Neg, FNeg, kL32, kV128)                  \
  V(I32x4Neg, INeg, kL32, kV128)                  \
  V(F16x8Neg, FNeg, kL16, kV128)                  \
  V(I16x8Neg, INeg, kL16, kV128)                  \
  V(I8x16Neg, INeg, kL8, kV128)                   \
  V(F64x2Sqrt, FSqrt, kL64, kV128)                \
  V(F32x4Sqrt, FSqrt, kL32, kV128)                \
  V(F16x8Sqrt, FSqrt, kL16, kV128)                \
  V(I64x2BitMask, IBitMask, kL64, kV128)          \
  V(I32x4BitMask, IBitMask, kL32, kV128)          \
  V(I16x8BitMask, IBitMask, kL16, kV128)          \
  V(I8x16BitMask, IBitMask, kL8, kV128)           \
  V(I64x2AllTrue, IAllTrue, kL64, kV128)          \
  V(I32x4AllTrue, IAllTrue, kL32, kV128)          \
  V(I16x8AllTrue, IAllTrue, kL16, kV128)          \
  V(I8x16AllTrue, IAllTrue, kL8, kV128)           \
  V(S128Not, SNot, kL8, kV128)                    \
  V(F32x8Abs, FAbs, kL32, kV256)                  \
  V(I32x8Abs, IAbs, kL32, kV256)                  \
  V(I16x16Abs, IAbs, kL16, kV256)                 \
  V(I8x32Abs, IAbs, kL8, kV256)                   \
  V(F32x8Neg, FNeg, kL32, kV256)                  \
  V(I32x8Neg, INeg, kL32, kV256)                  \
  V(I16x16Neg, INeg, kL16, kV256)                 \
  V(I8x32Neg, INeg, kL8, kV256)                   \
  V(F64x4Sqrt, FSqrt, kL64, kV256)                \
  V(F32x8Sqrt, FSqrt, kL32, kV256)                \
  V(S256Not, SNot, kL8, kV256)

#define SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(V) \
  V(I64x2Shl, IShl, kL64, kV128)                      \
  V(I32x4Shl, IShl, kL32, kV128)                      \
  V(I16x8Shl, IShl, kL16, kV128)                      \
  V(I32x4ShrS, IShrS, kL32, kV128)                    \
  V(I16x8ShrS, IShrS, kL16, kV128)                    \
  V(I64x2ShrU, IShrU, kL64, kV128)                    \
  V(I32x4ShrU, IShrU, kL32, kV128)                    \
  V(I16x8ShrU, IShrU, kL16, kV128)                    \
  V(I64x4Shl, IShl, kL64, kV256)                      \
  V(I32x8Shl, IShl, kL32, kV256)                      \
  V(I16x16Shl, IShl, kL16, kV256)                     \
  V(I32x8ShrS, IShrS, kL32, kV256)                    \
  V(I16x16ShrS, IShrS, kL16, kV256)                   \
  V(I64x4ShrU, IShrU, kL64, kV256)                    \
  V(I32x8ShrU, IShrU, kL32, kV256)                    \
  V(I16x16ShrU, IShrU, kL16, kV256)

#define SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(V) \
  V(I8x16Shl, IShl, kL8, kV128)                              \
  V(I8x16ShrS, IShrS, kL8, kV128)                            \
  V(I8x16ShrU, IShrU, kL8, kV128)

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Const(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
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
    Emit(kX64SZero | VectorLengthField::encode(kV128), dst);
  } else if (all_ones) {
    Emit(kX64SAllOnes | VectorLengthField::encode(kV128), dst);
  } else {
    Emit(kX64S128Const, dst, g.UseImmediate(val[0]), g.UseImmediate(val[1]),
         g.UseImmediate(val[2]), g.UseImmediate(val[3]));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Zero(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  Emit(kX64SZero | VectorLengthField::encode(kV128), g.DefineAsRegister(node));
}
// Name, LaneSize, VectorLength
#define SIMD_INT_TYPES_FOR_SPLAT(V) \
  V(I64x2, kL64, kV128)             \
  V(I32x4, kL32, kV128)             \
  V(I16x8, kL16, kV128)             \
  V(I8x16, kL8, kV128)              \
  V(I64x4, kL64, kV256)             \
  V(I32x8, kL32, kV256)             \
  V(I16x16, kL16, kV256)            \
  V(I8x32, kL8, kV256)

// Splat with an optimization for const 0.
#define VISIT_INT_SIMD_SPLAT(Type, LaneSize, VectorLength)                   \
  template <typename Adapter>                                                \
  void InstructionSelectorT<Adapter>::Visit##Type##Splat(node_t node) {      \
    X64OperandGeneratorT<Adapter> g(this);                                   \
    DCHECK_EQ(this->value_input_count(node), 1);                             \
    node_t input = this->input_at(node, 0);                                  \
    if (g.CanBeImmediate(input) && g.GetImmediateIntegerValue(input) == 0) { \
      Emit(kX64SZero | VectorLengthField::encode(VectorLength),              \
           g.DefineAsRegister(node));                                        \
    } else {                                                                 \
      Emit(kX64ISplat | LaneSizeField::encode(LaneSize) |                    \
               VectorLengthField::encode(VectorLength),                      \
           g.DefineAsRegister(node), g.Use(input));                          \
    }                                                                        \
  }
SIMD_INT_TYPES_FOR_SPLAT(VISIT_INT_SIMD_SPLAT)
#undef VISIT_INT_SIMD_SPLAT
#undef SIMD_INT_TYPES_FOR_SPLAT

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Splat(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64FSplat | LaneSizeField::encode(kL64) |
           VectorLengthField::encode(kV128),
       g.DefineAsRegister(node), g.Use(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Splat(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64FSplat | LaneSizeField::encode(kL32) |
           VectorLengthField::encode(kV128),
       g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Splat(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64FSplat | LaneSizeField::encode(kL16) |
           VectorLengthField::encode(kV128),
       g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x4Splat(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64FSplat | LaneSizeField::encode(kL64) |
           VectorLengthField::encode(kV256),
       g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x8Splat(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64FSplat | LaneSizeField::encode(kL32) |
           VectorLengthField::encode(kV256),
       g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)));
}

#define SIMD_VISIT_EXTRACT_LANE(IF, Type, Sign, LaneSize, VectorLength)        \
  template <>                                                                  \
  void InstructionSelectorT<TurbofanAdapter>::Visit##Type##ExtractLane##Sign(  \
      node_t node) {                                                           \
    X64OperandGeneratorT<TurbofanAdapter> g(this);                             \
    int32_t lane = OpParameter<int32_t>(node->op());                           \
    Emit(kX64##IF##ExtractLane##Sign | LaneSizeField::encode(LaneSize) |       \
             VectorLengthField::encode(VectorLength),                          \
         g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)),            \
         g.UseImmediate(lane));                                                \
  }                                                                            \
  template <>                                                                  \
  void                                                                         \
      InstructionSelectorT<TurboshaftAdapter>::Visit##Type##ExtractLane##Sign( \
          node_t node) {                                                       \
    X64OperandGeneratorT<TurboshaftAdapter> g(this);                           \
    const turboshaft::Simd128ExtractLaneOp& op =                               \
        this->Get(node).template Cast<turboshaft::Simd128ExtractLaneOp>();     \
    int32_t lane = op.lane;                                                    \
    Emit(kX64##IF##ExtractLane##Sign | LaneSizeField::encode(LaneSize) |       \
             VectorLengthField::encode(VectorLength),                          \
         g.DefineAsRegister(node), g.UseRegister(op.input()),                  \
         g.UseImmediate(lane));                                                \
  }

SIMD_VISIT_EXTRACT_LANE(F, F64x2, , kL64, kV128)
SIMD_VISIT_EXTRACT_LANE(F, F32x4, , kL32, kV128)
SIMD_VISIT_EXTRACT_LANE(F, F16x8, , kL16, kV128)
SIMD_VISIT_EXTRACT_LANE(I, I64x2, , kL64, kV128)
SIMD_VISIT_EXTRACT_LANE(I, I32x4, , kL32, kV128)
SIMD_VISIT_EXTRACT_LANE(I, I16x8, S, kL16, kV128)
SIMD_VISIT_EXTRACT_LANE(I, I8x16, S, kL8, kV128)
#undef SIMD_VISIT_EXTRACT_LANE

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtractLaneU(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ExtractLaneOp& op =
        this->Get(node).template Cast<turboshaft::Simd128ExtractLaneOp>();
    Emit(kX64Pextrw, g.DefineAsRegister(node), g.UseRegister(op.input()),
         g.UseImmediate(static_cast<int32_t>(op.lane)));

  } else {
    int32_t lane = OpParameter<int32_t>(node->op());
    Emit(kX64Pextrw, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)),
         g.UseImmediate(lane));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16ExtractLaneU(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ExtractLaneOp& op =
        this->Get(node).template Cast<turboshaft::Simd128ExtractLaneOp>();
    Emit(kX64Pextrb, g.DefineAsRegister(node), g.UseRegister(op.input()),
         g.UseImmediate(op.lane));

  } else {
    int32_t lane = OpParameter<int32_t>(node->op());
    Emit(kX64Pextrb, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)),
         g.UseImmediate(lane));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8ReplaceLane(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    auto& op =
        this->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL16) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(op.into()),
         g.UseImmediate(op.lane), g.Use(op.new_lane()));

  } else {
    int32_t lane = OpParameter<int32_t>(node->op());
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL16) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(node->InputAt(0)),
         g.UseImmediate(lane), g.Use(node->InputAt(1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4ReplaceLane(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ReplaceLaneOp& op =
        this->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL32) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(op.into()),
         g.UseImmediate(op.lane), g.Use(op.new_lane()));

  } else {
    int32_t lane = OpParameter<int32_t>(node->op());
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL32) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(node->InputAt(0)),
         g.UseImmediate(lane), g.Use(node->InputAt(1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ReplaceLane(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  // When no-AVX, define dst == src to save a move.
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128ReplaceLaneOp& op =
        this->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         dst, g.UseRegister(op.into()), g.UseImmediate(op.lane),
         g.UseRegister(op.new_lane()));

  } else {
    int32_t lane = OpParameter<int32_t>(node->op());
    Emit(kX64FReplaceLane | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         dst, g.UseRegister(node->InputAt(0)), g.UseImmediate(lane),
         g.UseRegister(node->InputAt(1)));
  }
}

#define VISIT_SIMD_REPLACE_LANE(TYPE, OPCODE)                                 \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##TYPE##ReplaceLane(node_t node) { \
    X64OperandGeneratorT<Adapter> g(this);                                    \
    if constexpr (Adapter::IsTurboshaft) {                                    \
      const turboshaft::Simd128ReplaceLaneOp& op =                            \
          this->Get(node).template Cast<turboshaft::Simd128ReplaceLaneOp>();  \
      Emit(OPCODE, g.DefineAsRegister(node), g.UseRegister(op.into()),        \
           g.UseImmediate(op.lane), g.Use(op.new_lane()));                    \
    } else {                                                                  \
      int32_t lane = OpParameter<int32_t>(node->op());                        \
      Emit(OPCODE, g.DefineAsRegister(node), g.UseRegister(node->InputAt(0)), \
           g.UseImmediate(lane), g.Use(node->InputAt(1)));                    \
    }                                                                         \
  }

#define SIMD_TYPES_FOR_REPLACE_LANE(V) \
  V(I64x2, kX64Pinsrq)                 \
  V(I32x4, kX64Pinsrd)                 \
  V(I16x8, kX64Pinsrw)                 \
  V(I8x16, kX64Pinsrb)

SIMD_TYPES_FOR_REPLACE_LANE(VISIT_SIMD_REPLACE_LANE)
#undef SIMD_TYPES_FOR_REPLACE_LANE
#undef VISIT_SIMD_REPLACE_LANE

#define VISIT_SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(                  \
    Name, Opcode, LaneSize, VectorLength)                                  \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {           \
    X64OperandGeneratorT<Adapter> g(this);                                 \
    DCHECK_EQ(this->value_input_count(node), 2);                           \
    InstructionOperand dst = IsSupported(AVX) ? g.DefineAsRegister(node)   \
                                              : g.DefineSameAsFirst(node); \
    if (g.CanBeImmediate(this->input_at(node, 1))) {                       \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                \
               VectorLengthField::encode(VectorLength),                    \
           dst, g.UseRegister(this->input_at(node, 0)),                    \
           g.UseImmediate(this->input_at(node, 1)));                       \
    } else {                                                               \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                \
               VectorLengthField::encode(VectorLength),                    \
           dst, g.UseRegister(this->input_at(node, 0)),                    \
           g.UseRegister(this->input_at(node, 1)));                        \
    }                                                                      \
  }
SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(
    VISIT_SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES)

#undef VISIT_SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES
#undef SIMD_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES

#define VISIT_SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(            \
    Name, Opcode, LaneSize, VectorLength)                                   \
  template <typename Adapter>                                               \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {            \
    X64OperandGeneratorT<Adapter> g(this);                                  \
    DCHECK_EQ(this->value_input_count(node), 2);                            \
    InstructionOperand output =                                             \
        IsSupported(AVX) ? g.UseRegister(node) : g.DefineSameAsFirst(node); \
    if (g.CanBeImmediate(this->input_at(node, 1))) {                        \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                 \
               VectorLengthField::encode(VectorLength),                     \
           output, g.UseRegister(this->input_at(node, 0)),                  \
           g.UseImmediate(this->input_at(node, 1)));                        \
    } else {                                                                \
      InstructionOperand temps[] = {g.TempSimd128Register()};               \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                 \
               VectorLengthField::encode(VectorLength),                     \
           output, g.UseUniqueRegister(this->input_at(node, 0)),            \
           g.UseUniqueRegister(this->input_at(node, 1)), arraysize(temps),  \
           temps);                                                          \
    }                                                                       \
  }
SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES(
    VISIT_SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES)
#undef VISIT_SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES
#undef SIMD_NARROW_SHIFT_LANE_SIZE_VECTOR_LENGTH_OPCODES

#define VISIT_SIMD_UNOP(Opcode)                                    \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    X64OperandGeneratorT<Adapter> g(this);                         \
    DCHECK_EQ(this->value_input_count(node), 1);                   \
    Emit(kX64##Opcode, g.DefineAsRegister(node),                   \
         g.UseRegister(this->input_at(node, 0)));                  \
  }
SIMD_UNOP_LIST(VISIT_SIMD_UNOP)
#undef VISIT_SIMD_UNOP
#undef SIMD_UNOP_LIST

#define VISIT_SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH(Name, Opcode, LaneSize,     \
                                                VectorLength)               \
  template <typename Adapter>                                               \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {            \
    X64OperandGeneratorT<Adapter> g(this);                                  \
    DCHECK_EQ(this->value_input_count(node), 1);                            \
    Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                   \
             VectorLengthField::encode(VectorLength),                       \
         g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0))); \
  }

SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH_LIST(VISIT_SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH)

#undef VISIT_SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH
#undef SIMD_UNOP_LANE_SIZE_VECTOR_LENGTH_LIST

#define VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH(Name, Opcode, LaneSize,    \
                                                 VectorLength)              \
  template <typename Adapter>                                               \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {            \
    X64OperandGeneratorT<Adapter> g(this);                                  \
    DCHECK_EQ(this->value_input_count(node), 2);                            \
    Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                   \
             VectorLengthField::encode(VectorLength),                       \
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)), \
         g.UseRegister(this->input_at(node, 1)));                           \
  }

SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH_LIST(
    VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH)

#undef VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH
#undef SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH_LIST

#define VISIT_SIMD_BINOP(Opcode)                                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    X64OperandGeneratorT<Adapter> g(this);                         \
    DCHECK_EQ(this->value_input_count(node), 2);                   \
    if (IsSupported(AVX)) {                                        \
      Emit(kX64##Opcode, g.DefineAsRegister(node),                 \
           g.UseRegister(this->input_at(node, 0)),                 \
           g.UseRegister(this->input_at(node, 1)));                \
    } else {                                                       \
      Emit(kX64##Opcode, g.DefineSameAsFirst(node),                \
           g.UseRegister(this->input_at(node, 0)),                 \
           g.UseRegister(this->input_at(node, 1)));                \
    }                                                              \
  }

SIMD_BINOP_SSE_AVX_LIST(VISIT_SIMD_BINOP)
#undef VISIT_SIMD_BINOP
#undef SIMD_BINOP_SSE_AVX_LIST

#define VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH(Name, Opcode, LaneSize,      \
                                                 VectorLength)                \
  template <typename Adapter>                                                 \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {              \
    X64OperandGeneratorT<Adapter> g(this);                                    \
    DCHECK_EQ(this->value_input_count(node), 2);                              \
    if (IsSupported(AVX)) {                                                   \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                   \
               VectorLengthField::encode(VectorLength),                       \
           g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)),  \
           g.UseRegister(this->input_at(node, 1)));                           \
    } else {                                                                  \
      Emit(kX64##Opcode | LaneSizeField::encode(LaneSize) |                   \
               VectorLengthField::encode(VectorLength),                       \
           g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)), \
           g.UseRegister(this->input_at(node, 1)));                           \
    }                                                                         \
  }

SIMD_BINOP_SSE_AVX_LANE_SIZE_VECTOR_LENGTH_LIST(
    VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH)
#undef VISIT_SIMD_BINOP_LANE_SIZE_VECTOR_LENGTH
#undef SIMD_BINOP_SSE_AVX_LANE_SIZE_VECTOR_LENGTH_LIST

#define VISIT_SIMD_F16x8_BINOP(Name, Opcode)                               \
  template <typename Adapter>                                              \
  void InstructionSelectorT<Adapter>::Visit##Name(node_t node) {           \
    X64OperandGeneratorT<Adapter> g(this);                                 \
    DCHECK_EQ(this->value_input_count(node), 2);                           \
    InstructionOperand temps[] = {g.TempSimd256Register(),                 \
                                  g.TempSimd256Register()};                \
    size_t temp_count = arraysize(temps);                                  \
    Emit(kX64##Opcode | LaneSizeField::encode(kL16) |                      \
             VectorLengthField::encode(kV128),                             \
         g.DefineAsRegister(node),                                         \
         g.UseUniqueRegister(this->input_at(node, 0)),                     \
         g.UseUniqueRegister(this->input_at(node, 1)), temp_count, temps); \
  }

SIMD_F16x8_BINOP_LIST(VISIT_SIMD_F16x8_BINOP)
#undef VISIT_SIMD_F16x8_BINOP
#undef SIMD_F16x8_BINOP_LIST

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitV128AnyTrue(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64V128AnyTrue, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)));
}

namespace {

static bool IsV128ZeroConst(InstructionSelectorT<TurbofanAdapter>* selector,
                            Node* node) {
  if (node->opcode() == IrOpcode::kS128Zero) {
    return true;
  }
  // If the node is a V128 const, check all the elements
  auto m = V128ConstMatcher(node);
  if (m.HasResolvedValue()) {
    auto imms = m.ResolvedValue().immediate();
    return std::all_of(imms.begin(), imms.end(), [](auto i) { return i == 0; });
  }
  return false;
}

static bool IsV128ZeroConst(InstructionSelectorT<TurboshaftAdapter>* selector,
                            turboshaft::OpIndex node) {
  const turboshaft::Operation& op = selector->Get(node);
  if (auto constant = op.TryCast<turboshaft::Simd128ConstantOp>()) {
    return constant->IsZero();
  }
  return false;
}

static bool MatchSimd128Constant(
    InstructionSelectorT<TurbofanAdapter>* selector, Node* node,
    std::array<uint8_t, kSimd128Size>* constant) {
  DCHECK_NOT_NULL(constant);
  auto m = V128ConstMatcher(node);
  if (m.HasResolvedValue()) {
    // If the indices vector is a const, check if they are in range, or if the
    // top bit is set, then we can avoid the paddusb in the codegen and simply
    // emit a pshufb.
    *constant = m.ResolvedValue().immediate();
    return true;
  }
  return false;
}

static bool MatchSimd128Constant(
    InstructionSelectorT<TurboshaftAdapter>* selector, turboshaft::OpIndex node,
    std::array<uint8_t, kSimd128Size>* constant) {
  DCHECK_NOT_NULL(constant);
  const turboshaft::Operation& op = selector->Get(node);
  if (auto c = op.TryCast<turboshaft::Simd128ConstantOp>()) {
    std::memcpy(constant, c->value, kSimd128Size);
    return true;
  }
  return false;
}

}  // namespace

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128Select(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 3);

  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  if (IsV128ZeroConst(this, this->input_at(node, 2))) {
    // select(cond, input1, 0) -> and(cond, input1)
    Emit(kX64SAnd | VectorLengthField::encode(kV128), dst,
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else if (IsV128ZeroConst(this, this->input_at(node, 1))) {
    // select(cond, 0, input2) -> and(not(cond), input2)
    Emit(kX64SAndNot | VectorLengthField::encode(kV128), dst,
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 2)));
  } else {
    Emit(kX64SSelect | VectorLengthField::encode(kV128), dst,
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)),
         g.UseRegister(this->input_at(node, 2)));
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitS256Select(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  Emit(kX64SSelect | VectorLengthField::encode(kV256), g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 2)));
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitS256Select(node_t node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  Emit(kX64SSelect | VectorLengthField::encode(kV256), g.DefineAsRegister(node),
       g.UseRegister(node->InputAt(0)), g.UseRegister(node->InputAt(1)),
       g.UseRegister(node->InputAt(2)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS128AndNot(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  // andnps a b does ~a & b, but we want a & !b, so flip the input.
  Emit(kX64SAndNot | VectorLengthField::encode(kV128),
       IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitS256AndNot(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  // andnps a b does ~a & b, but we want a & !b, so flip the input.
  Emit(kX64SAndNot | VectorLengthField::encode(kV256),
       IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 1)),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Abs(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0),
                 kX64FAbs | LaneSizeField::encode(kL64) |
                     VectorLengthField::encode(kV128));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Neg(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0),
                 kX64FNeg | LaneSizeField::encode(kL64) |
                     VectorLengthField::encode(kV128));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4UConvertI32x4(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64F32x4UConvertI32x4, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x8UConvertI32x8(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64F32x8UConvertI32x8, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

#define VISIT_SIMD_QFMOP(Opcode)                                   \
  template <typename Adapter>                                      \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) { \
    X64OperandGeneratorT<Adapter> g(this);                         \
    DCHECK_EQ(this->value_input_count(node), 3);                   \
    Emit(kX64##Opcode, g.UseRegister(node),                        \
         g.UseRegister(this->input_at(node, 0)),                   \
         g.UseRegister(this->input_at(node, 1)),                   \
         g.UseRegister(this->input_at(node, 2)));                  \
  }
VISIT_SIMD_QFMOP(F64x2Qfma)
VISIT_SIMD_QFMOP(F64x2Qfms)
VISIT_SIMD_QFMOP(F32x4Qfma)
VISIT_SIMD_QFMOP(F32x4Qfms)

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
VISIT_SIMD_QFMOP(F64x4Qfma)
VISIT_SIMD_QFMOP(F64x4Qfms)
VISIT_SIMD_QFMOP(F32x8Qfma)
VISIT_SIMD_QFMOP(F32x8Qfms)
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#undef VISIT_SIMD_QFMOP

#define VISIT_SIMD_F16x8_QFMOP(Opcode)                                   \
  template <typename Adapter>                                            \
  void InstructionSelectorT<Adapter>::Visit##Opcode(node_t node) {       \
    X64OperandGeneratorT<Adapter> g(this);                               \
    DCHECK_EQ(this->value_input_count(node), 3);                         \
    InstructionOperand temps[] = {g.TempSimd256Register(),               \
                                  g.TempSimd256Register()};              \
    Emit(kX64##Opcode, g.UseRegister(node),                              \
         g.UseUniqueRegister(this->input_at(node, 0)),                   \
         g.UseUniqueRegister(this->input_at(node, 1)),                   \
         g.UseUniqueRegister(this->input_at(node, 2)), arraysize(temps), \
         temps);                                                         \
  }

VISIT_SIMD_F16x8_QFMOP(F16x8Qfma) VISIT_SIMD_F16x8_QFMOP(F16x8Qfms)
#undef VISIT_SIMD_F16x8_QFMOP

    template <typename Adapter>
    void InstructionSelectorT<Adapter>::VisitI64x2Neg(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  // If AVX unsupported, make sure dst != src to avoid a move.
  InstructionOperand operand0 = IsSupported(AVX)
                                    ? g.UseRegister(this->input_at(node, 0))
                                    : g.UseUnique(this->input_at(node, 0));
  Emit(
      kX64INeg | LaneSizeField::encode(kL64) | VectorLengthField::encode(kV128),
      g.DefineAsRegister(node), operand0);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2ShrS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);

  if (g.CanBeImmediate(this->input_at(node, 1))) {
    Emit(kX64IShrS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         dst, g.UseRegister(this->input_at(node, 0)),
         g.UseImmediate(this->input_at(node, 1)));
  } else {
    InstructionOperand temps[] = {g.TempSimd128Register()};
    Emit(kX64IShrS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         dst, g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)), arraysize(temps), temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Mul(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(
      kX64IMul | LaneSizeField::encode(kL64) | VectorLengthField::encode(kV128),
      g.DefineAsRegister(node), g.UseUniqueRegister(this->input_at(node, 0)),
      g.UseUniqueRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x4Mul(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  InstructionOperand temps[] = {g.TempSimd256Register()};
  Emit(
      kX64IMul | LaneSizeField::encode(kL64) | VectorLengthField::encode(kV256),
      g.DefineAsRegister(node), g.UseUniqueRegister(this->input_at(node, 0)),
      g.UseUniqueRegister(this->input_at(node, 1)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4SConvertF32x4(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64I32x4SConvertF32x4,
       IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8SConvertF32x8(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64I32x8SConvertF32x8, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4UConvertF32x4(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand temps[] = {g.TempSimd128Register(),
                                g.TempSimd128Register()};
  Emit(kX64I32x4UConvertF32x4, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8UConvertF32x8(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand temps[] = {g.TempSimd256Register(),
                                g.TempSimd256Register()};
  Emit(kX64I32x8UConvertF32x8, g.DefineSameAsFirst(node),
       g.UseRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitExtractF128(node_t node) {
  X64OperandGeneratorT<TurbofanAdapter> g(this);
  int32_t lane = OpParameter<int32_t>(node->op());
  if (lane == 0) {
    EmitIdentity(node);
  } else {
    Emit(kX64ExtractF128, g.DefineAsRegister(node),
         g.UseRegister(node->InputAt(0)), g.UseImmediate(lane));
  }
}

#if V8_ENABLE_WASM_SIMD256_REVEC
template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitExtractF128(node_t node) {
  X64OperandGeneratorT<TurboshaftAdapter> g(this);
  const turboshaft::Simd256Extract128LaneOp& op =
      this->Get(node).template Cast<turboshaft::Simd256Extract128LaneOp>();
  if (op.lane == 0) {
    EmitIdentity(node);
  } else {
    Emit(kX64ExtractF128, g.DefineAsRegister(node), g.UseRegister(op.input()),
         g.UseImmediate(op.lane));
  }
}

template <>
void InstructionSelectorT<TurboshaftAdapter>::VisitI8x32Shuffle(node_t node) {
  UNREACHABLE();
}
#endif  // V8_ENABLE_WASM_SIMD256_REVEC
#endif

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

bool TryMatchShufps(const uint8_t* shuffle32x4) {
  DCHECK_GT(8, shuffle32x4[2]);
  DCHECK_GT(8, shuffle32x4[3]);
  // shufps can be used if the first 2 indices select the first input [0-3], and
  // the other 2 indices select the second input [4-7].
  return shuffle32x4[0] < 4 && shuffle32x4[1] < 4 && shuffle32x4[2] > 3 &&
         shuffle32x4[3] > 3;
}

template <typename Adapter>
static bool TryMatchOneInputIsZeros(InstructionSelectorT<Adapter>* selector,
                                    typename Adapter::SimdShuffleView& view,
                                    uint8_t* shuffle, bool* needs_swap) {
  *needs_swap = false;
  bool input0_is_zero = IsV128ZeroConst(selector, view.input(0));
  bool input1_is_zero = IsV128ZeroConst(selector, view.input(1));
  if (!input0_is_zero && !input1_is_zero) {
    return false;
  }

  if (input0_is_zero) {
    *needs_swap = true;
  }
  return true;
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

  X64OperandGeneratorT<Adapter> g(this);
  // Swizzles don't generally need DefineSameAsFirst to avoid a move.
  bool no_same_as_first = is_swizzle;
  // We generally need UseRegister for input0, Use for input1.
  // TODO(v8:9198): We don't have 16-byte alignment for SIMD operands yet, but
  // we retain this logic (continue setting these in the various shuffle match
  // clauses), but ignore it when selecting registers or slots.
  bool src0_needs_reg = true;
  bool src1_needs_reg = false;
  ArchOpcode opcode = kX64I8x16Shuffle;  // general shuffle is the default

  uint8_t offset;
  uint8_t shuffle32x4[4];
  uint8_t shuffle16x8[8];
  int index;
  const wasm::ShuffleEntry<kSimd128Size>* arch_shuffle;
  bool needs_swap;
  if (wasm::SimdShuffle::TryMatchConcat(shuffle, &offset)) {
    if (wasm::SimdShuffle::TryMatch32x4Rotate(shuffle, shuffle32x4,
                                              is_swizzle)) {
      uint8_t shuffle_mask = wasm::SimdShuffle::PackShuffle4(shuffle32x4);
      opcode = kX64S32x4Rotate;
      imms[imm_count++] = shuffle_mask;
    } else {
      // Swap inputs from the normal order for (v)palignr.
      SwapShuffleInputs(view);
      is_swizzle = false;  // It's simpler to just handle the general case.
      no_same_as_first = CpuFeatures::IsSupported(AVX);
      // TODO(v8:9608): also see v8:9083
      src1_needs_reg = true;
      opcode = kX64S8x16Alignr;
      // palignr takes a single imm8 offset.
      imms[imm_count++] = offset;
    }
  } else if (wasm::SimdShuffle::TryMatchArchShuffle(shuffle, is_swizzle,
                                                    &arch_shuffle)) {
    opcode = arch_shuffle->opcode;
    src0_needs_reg = arch_shuffle->src0_needs_reg;
    // SSE can't take advantage of both operands in registers and needs
    // same-as-first.
    src1_needs_reg = arch_shuffle->src1_needs_reg;
    no_same_as_first =
        IsSupported(AVX) && arch_shuffle->no_same_as_first_if_avx;
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
        opcode = kX64S32x4Swizzle;
        no_same_as_first = true;
        // TODO(v8:9083): This doesn't strictly require a register, forcing the
        // swizzles to always use registers until generation of incorrect memory
        // operands can be fixed.
        src0_needs_reg = true;
        imms[imm_count++] = shuffle_mask;
      }
    } else {
      // 2 operand shuffle
      // A blend is more efficient than a general 32x4 shuffle; try it first.
      if (wasm::SimdShuffle::TryMatchBlend(shuffle)) {
        opcode = kX64S16x8Blend;
        uint8_t blend_mask = wasm::SimdShuffle::PackBlend4(shuffle32x4);
        imms[imm_count++] = blend_mask;
        no_same_as_first = CpuFeatures::IsSupported(AVX);
      } else if (TryMatchShufps(shuffle32x4)) {
        opcode = kX64Shufps;
        uint8_t mask = wasm::SimdShuffle::PackShuffle4(shuffle32x4);
        imms[imm_count++] = mask;
        src1_needs_reg = true;
        no_same_as_first = IsSupported(AVX);
      } else {
        opcode = kX64S32x4Shuffle;
        no_same_as_first = true;
        // TODO(v8:9083): src0 and src1 is used by pshufd in codegen, which
        // requires memory to be 16-byte aligned, since we cannot guarantee that
        // yet, force using a register here.
        src0_needs_reg = true;
        src1_needs_reg = true;
        imms[imm_count++] = shuffle_mask;
        uint8_t blend_mask = wasm::SimdShuffle::PackBlend4(shuffle32x4);
        imms[imm_count++] = blend_mask;
      }
    }
  } else if (wasm::SimdShuffle::TryMatch16x8Shuffle(shuffle, shuffle16x8)) {
    uint8_t blend_mask;
    if (wasm::SimdShuffle::TryMatchBlend(shuffle)) {
      opcode = kX64S16x8Blend;
      blend_mask = wasm::SimdShuffle::PackBlend8(shuffle16x8);
      imms[imm_count++] = blend_mask;
      no_same_as_first = CpuFeatures::IsSupported(AVX);
    } else if (wasm::SimdShuffle::TryMatchSplat<8>(shuffle, &index)) {
      opcode = kX64S16x8Dup;
      src0_needs_reg = false;
      imms[imm_count++] = index;
    } else if (TryMatch16x8HalfShuffle(shuffle16x8, &blend_mask)) {
      opcode = is_swizzle ? kX64S16x8HalfShuffle1 : kX64S16x8HalfShuffle2;
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
    opcode = kX64S8x16Dup;
    no_same_as_first = false;
    src0_needs_reg = true;
    imms[imm_count++] = index;
  } else if (TryMatchOneInputIsZeros(this, view, shuffle, &needs_swap)) {
    is_swizzle = true;
    // Swap zeros to input1
    if (needs_swap) {
      SwapShuffleInputs(view);
      for (int i = 0; i < kSimd128Size; ++i) {
        shuffle[i] ^= kSimd128Size;
      }
    }
    if (wasm::SimdShuffle::TryMatchByteToDwordZeroExtend(shuffle)) {
      opcode = kX64I32X4ShiftZeroExtendI8x16;
      no_same_as_first = true;
      src0_needs_reg = true;
      imms[imm_count++] = shuffle[0];
    } else {
      // If the most significant bit (bit 7) of each byte of the shuffle control
      // mask is set, then constant zero is written in the result byte. Input1
      // is zeros now, we can avoid using input1 by setting bit 7 of shuffle[i]
      // to 1.
      for (int i = 0; i < kSimd128Size; ++i) {
        if (shuffle[i] >= kSimd128Size) {
          shuffle[i] = 0x80;
        }
      }
    }
  }
  if (opcode == kX64I8x16Shuffle) {
    // Use same-as-first for general swizzle, but not shuffle.
    no_same_as_first = !is_swizzle;
    src0_needs_reg = !no_same_as_first;
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 4);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 8);
    imms[imm_count++] = wasm::SimdShuffle::Pack4Lanes(shuffle + 12);
    temps[temp_count++] = g.TempSimd128Register();
  }

  // Use DefineAsRegister(node) and Use(src0) if we can without forcing an extra
  // move instruction in the CodeGenerator.
  node_t input0 = view.input(0);
  InstructionOperand dst =
      no_same_as_first ? g.DefineAsRegister(view) : g.DefineSameAsFirst(view);
  // TODO(v8:9198): Use src0_needs_reg when we have memory alignment for SIMD.
  // We only need a unique register for input0 if we use temp registers.
  InstructionOperand src0 =
      temp_count ? g.UseUniqueRegister(input0) : g.UseRegister(input0);
  USE(src0_needs_reg);

  int input_count = 0;
  InstructionOperand inputs[2 + kMaxImms + kMaxTemps];
  inputs[input_count++] = src0;
  if (!is_swizzle) {
    node_t input1 = view.input(1);
    // TODO(v8:9198): Use src1_needs_reg when we have memory alignment for SIMD.
    // We only need a unique register for input1 if we use temp registers.
    inputs[input_count++] =
        temp_count ? g.UseUniqueRegister(input1) : g.UseRegister(input1);
    USE(src1_needs_reg);
  }
  for (int i = 0; i < imm_count; ++i) {
    inputs[input_count++] = g.UseImmediate(imms[i]);
  }
  Emit(opcode, 1, &dst, input_count, inputs, temp_count, temps);
}

template <>
void InstructionSelectorT<TurbofanAdapter>::VisitI8x32Shuffle(node_t node) {
  uint8_t shuffle[kSimd256Size];
  bool is_swizzle;
  auto view = this->simd_shuffle_view(node);
  CanonicalizeShuffle<kSimd256Size>(view, shuffle, &is_swizzle);

  X64OperandGeneratorT<TurbofanAdapter> g(this);

  if (uint8_t shuffle32x8[8];
      wasm::SimdShuffle::TryMatch32x8Shuffle(shuffle, shuffle32x8)) {
    if (is_swizzle) {
      Node* input0 = node->InputAt(0);
      InstructionOperand dst = g.DefineAsRegister(node);
      InstructionOperand src = g.UseUniqueRegister(input0);
      uint8_t control;
      if (wasm::SimdShuffle::TryMatchVpshufd(shuffle32x8, &control)) {
        InstructionOperand imm = g.UseImmediate(control);
        InstructionOperand inputs[] = {src, imm};
        Emit(kX64Vpshufd, 1, &dst, 2, inputs);
        return;
      }
    }
  }

  UNIMPLEMENTED();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Swizzle(node_t node) {
  InstructionCode op = kX64I8x16Swizzle;
  DCHECK_EQ(this->value_input_count(node), 2);
  node_t left = this->input_at(node, 0);
  node_t right = this->input_at(node, 1);

  bool relaxed;
  if constexpr (Adapter::IsTurboshaft) {
    const turboshaft::Simd128BinopOp& binop =
        this->Get(node).template Cast<turboshaft::Simd128BinopOp>();
    DCHECK(binop.kind ==
           turboshaft::any_of(
               turboshaft::Simd128BinopOp::Kind::kI8x16Swizzle,
               turboshaft::Simd128BinopOp::Kind::kI8x16RelaxedSwizzle));
    relaxed =
        binop.kind == turboshaft::Simd128BinopOp::Kind::kI8x16RelaxedSwizzle;
  } else {
    relaxed = OpParameter<bool>(node->op());
  }

  if (relaxed) {
    op |= MiscField::encode(true);
  } else {
    std::array<uint8_t, kSimd128Size> imms;
    if (MatchSimd128Constant(this, right, &imms)) {
      // If the indices vector is a const, check if they are in range, or if the
      // top bit is set, then we can avoid the paddusb in the codegen and simply
      // emit a pshufb.
      op |= MiscField::encode(wasm::SimdSwizzle::AllInRangeOrTopBitSet(imms));
    }
  }

  X64OperandGeneratorT<Adapter> g(this);
  Emit(op,
       IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node),
       g.UseRegister(left), g.UseRegister(right));
}

namespace {
template <typename Adapter>
void VisitRelaxedLaneSelect(InstructionSelectorT<Adapter>* selector,
                            typename Adapter::node_t node,
                            InstructionCode code) {
  X64OperandGeneratorT<Adapter> g(selector);
  DCHECK_EQ(selector->value_input_count(node), 3);
  // pblendvb/blendvps/blendvpd copies src2 when mask is set, opposite from Wasm
  // semantics. Node's inputs are: mask, lhs, rhs (determined in
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
  VisitRelaxedLaneSelect(this, node,
                         kX64Pblendvb | VectorLengthField::encode(kV128));
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Pblendvb | VectorLengthField::encode(kV128));
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Blendvps | VectorLengthField::encode(kV128));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Blendvpd | VectorLengthField::encode(kV128));
}

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x32RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Pblendvb | VectorLengthField::encode(kV256));
}
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x16RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Pblendvb | VectorLengthField::encode(kV256));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Blendvps | VectorLengthField::encode(kV256));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x4RelaxedLaneSelect(node_t node) {
  VisitRelaxedLaneSelect(this, node,
                         kX64Blendvpd | VectorLengthField::encode(kV256));
}
#endif  // V8_ENABLE_WASM_SIMD256_REVEC

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Pmin(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionCode instr_code = kX64Minph | VectorLengthField::encode(kV128);
  InstructionOperand temps[] = {g.TempSimd256Register(),
                                g.TempSimd256Register()};
  size_t temp_count = arraysize(temps);

  Emit(instr_code, dst, g.UseUniqueRegister(this->input_at(node, 1)),
       g.UseUniqueRegister(this->input_at(node, 0)), temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8Pmax(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  InstructionOperand dst = g.DefineAsRegister(node);
  InstructionCode instr_code = kX64Maxph | VectorLengthField::encode(kV128);
  InstructionOperand temps[] = {g.TempSimd256Register(),
                                g.TempSimd256Register()};
  size_t temp_count = arraysize(temps);

  Emit(instr_code, dst, g.UseUniqueRegister(this->input_at(node, 1)),
       g.UseUniqueRegister(this->input_at(node, 0)), temp_count, temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF16x8DemoteF64x2Zero(node_t node) {
  UNREACHABLE();
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmin(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Minps, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4Pmax(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Maxps, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmin(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Minpd, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2Pmax(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Maxpd, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x8Pmin(node_t node) {
  VisitMinOrMax<Adapter, kV256>(this, node, kX64F32x8Pmin, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x8Pmax(node_t node) {
  VisitMinOrMax<Adapter, kV256>(this, node, kX64F32x8Pmax, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x4Pmin(node_t node) {
  VisitMinOrMax<Adapter, kV256>(this, node, kX64F64x4Pmin, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x4Pmax(node_t node) {
  VisitMinOrMax<Adapter, kV256>(this, node, kX64F64x4Pmax, true);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMin(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Minps, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF32x4RelaxedMax(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Maxps, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMin(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Minpd, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2RelaxedMax(node_t node) {
  VisitMinOrMax<Adapter, kV128>(this, node, kX64Maxpd, false);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8S(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand dst = CpuFeatures::IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  Emit(kX64I32x4ExtAddPairwiseI16x8S, dst,
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8ExtAddPairwiseI16x16S(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64I32x8ExtAddPairwiseI16x16S, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4ExtAddPairwiseI16x8U(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand dst = CpuFeatures::IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  Emit(kX64I32x4ExtAddPairwiseI16x8U, dst,
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8ExtAddPairwiseI16x16U(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64I32x8ExtAddPairwiseI16x16U, g.DefineAsRegister(node),
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16S(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  // Codegen depends on dst != src.
  Emit(kX64I16x8ExtAddPairwiseI8x16S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x16ExtAddPairwiseI8x32S(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  Emit(kX64I16x16ExtAddPairwiseI8x32S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8ExtAddPairwiseI8x16U(
    node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand dst = CpuFeatures::IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  Emit(kX64I16x8ExtAddPairwiseI8x16U, dst,
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x16ExtAddPairwiseI8x32U(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  Emit(kX64I16x16ExtAddPairwiseI8x32U, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI8x16Popcnt(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  Emit(kX64I8x16Popcnt, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)), arraysize(temps), temps);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2ConvertLowI32x4U(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand dst =
      IsSupported(AVX) ? g.DefineAsRegister(node) : g.DefineSameAsFirst(node);
  Emit(kX64F64x2ConvertLowI32x4U, dst, g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2SZero(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  if (CpuFeatures::IsSupported(AVX)) {
    // Requires dst != src.
    Emit(kX64I32x4TruncSatF64x2SZero, g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)));
  } else {
    Emit(kX64I32x4TruncSatF64x2SZero, g.DefineSameAsFirst(node),
         g.UseRegister(this->input_at(node, 0)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4TruncSatF64x2UZero(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionOperand dst = CpuFeatures::IsSupported(AVX)
                               ? g.DefineAsRegister(node)
                               : g.DefineSameAsFirst(node);
  Emit(kX64I32x4TruncSatF64x2UZero, dst,
       g.UseRegister(this->input_at(node, 0)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2SZero(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Cvttpd2dq);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF64x2UZero(
    node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64I32x4TruncF64x2UZero);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF32x4S(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  VisitFloatUnop(this, node, this->input_at(node, 0), kX64Cvttps2dq);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4RelaxedTruncF32x4U(node_t node) {
  DCHECK_EQ(this->value_input_count(node), 1);
  X64OperandGeneratorT<Adapter> g(this);
  node_t input = this->input_at(node, 0);
  InstructionOperand temps[] = {g.TempSimd128Register()};
  if (IsSupported(AVX)) {
    Emit(kX64I32x4TruncF32x4U, g.DefineAsRegister(node), g.UseRegister(input),
         arraysize(temps), temps);
  } else {
    Emit(kX64I32x4TruncF32x4U, g.DefineSameAsFirst(node), g.UseRegister(input),
         arraysize(temps), temps);
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2GtS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  if (CpuFeatures::IsSupported(AVX)) {
    Emit(kX64IGtS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    Emit(kX64IGtS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else {
    Emit(kX64IGtS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node), g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2GeS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  if (CpuFeatures::IsSupported(AVX)) {
    Emit(kX64IGeS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else if (CpuFeatures::IsSupported(SSE4_2)) {
    Emit(kX64IGeS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node), g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)));
  } else {
    Emit(kX64IGeS | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node), g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)));
  }
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x4GeS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  DCHECK(CpuFeatures::IsSupported(AVX2));
  Emit(
      kX64IGeS | LaneSizeField::encode(kL64) | VectorLengthField::encode(kV256),
      g.DefineAsRegister(node), g.UseRegister(this->input_at(node, 0)),
      g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI64x2Abs(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  if (CpuFeatures::IsSupported(AVX)) {
    Emit(kX64IAbs | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineAsRegister(node),
         g.UseUniqueRegister(this->input_at(node, 0)));
  } else {
    Emit(kX64IAbs | LaneSizeField::encode(kL64) |
             VectorLengthField::encode(kV128),
         g.DefineSameAsFirst(node), g.UseRegister(this->input_at(node, 0)));
  }
}

template <>
bool InstructionSelectorT<TurboshaftAdapter>::CanOptimizeF64x2PromoteLowF32x4(
    node_t node) {
  using namespace turboshaft;  // NOLINT(build/namespaces)
  DCHECK(this->Get(node).Is<Opmask::kSimd128F64x2PromoteLowF32x4>());
  V<Simd128> input = this->input_at(node, 0);
  return this->Get(input).template Is<Opmask::kSimd128LoadTransform64Zero>() &&
         CanCover(node, input);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitF64x2PromoteLowF32x4(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  InstructionCode code = kX64F64x2PromoteLowF32x4;
  if constexpr (Adapter::IsTurboshaft) {
    using namespace turboshaft;  // NOLINT(build/namespaces)
    if (CanOptimizeF64x2PromoteLowF32x4(node)) {
      V<Simd128> input = this->input_at(node, 0);
      const Simd128LoadTransformOp& load_transform =
          this->Get(input).template Cast<Simd128LoadTransformOp>();
      if (load_transform.load_kind.with_trap_handler) {
        code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
      }
      // LoadTransforms cannot be eliminated, so they are visited even if
      // unused. Mark it as defined so that we don't visit it.
      MarkAsDefined(input);
      VisitLoad(node, input, code);
      return;
    }
  } else {
    node_t input = this->input_at(node, 0);
    LoadTransformMatcher m(input);

    if (m.Is(LoadTransformation::kS128Load64Zero) && CanCover(node, input)) {
      if (m.ResolvedValue().kind == MemoryAccessKind::kProtected) {
        code |= AccessModeField::encode(kMemoryAccessProtectedMemOutOfBounds);
      }
      // LoadTransforms cannot be eliminated, so they are visited even if
      // unused. Mark it as defined so that we don't visit it.
      MarkAsDefined(input);
      VisitLoad(node, input, code);
      return;
    }
  }

  VisitRR(this, node, code);
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x8DotI8x16I7x16S(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  Emit(kX64I16x8DotI8x16I7x16S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x4DotI8x16I7x16AddS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 3);
  if (CpuFeatures::IsSupported(AVX_VNNI)) {
    Emit(kX64I32x4DotI8x16I7x16AddS, g.DefineSameAsInput(node, 2),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)),
         g.UseRegister(this->input_at(node, 2)));
  } else {
    InstructionOperand temps[] = {g.TempSimd128Register()};
    Emit(kX64I32x4DotI8x16I7x16AddS, g.DefineSameAsInput(node, 2),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)),
         g.UseUniqueRegister(this->input_at(node, 2)), arraysize(temps), temps);
  }
}

#ifdef V8_ENABLE_WASM_SIMD256_REVEC
template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI16x16DotI8x32I7x32S(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 2);
  Emit(kX64I16x16DotI8x32I7x32S, g.DefineAsRegister(node),
       g.UseUniqueRegister(this->input_at(node, 0)),
       g.UseRegister(this->input_at(node, 1)));
}

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitI32x8DotI8x32I7x32AddS(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 3);
  if (CpuFeatures::IsSupported(AVX_VNNI)) {
    Emit(kX64I32x8DotI8x32I7x32AddS, g.DefineSameAsInput(node, 2),
         g.UseRegister(this->input_at(node, 0)),
         g.UseRegister(this->input_at(node, 1)),
         g.UseRegister(this->input_at(node, 2)));
  } else {
    InstructionOperand temps[] = {g.TempSimd256Register()};
    Emit(kX64I32x8DotI8x32I7x32AddS, g.DefineSameAsInput(node, 2),
         g.UseUniqueRegister(this->input_at(node, 0)),
         g.UseUniqueRegister(this->input_at(node, 1)),
         g.UseUniqueRegister(this->input_at(node, 2)), arraysize(temps), temps);
  }
}
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::VisitSetStackPointer(node_t node) {
  X64OperandGeneratorT<Adapter> g(this);
  DCHECK_EQ(this->value_input_count(node), 1);
  auto input = g.UseAny(this->input_at(node, 0));
  Emit(kArchSetStackPointer, 0, nullptr, 1, &input);
}

#endif  // V8_ENABLE_WEBASSEMBLY

#ifndef V8_ENABLE_WEBASSEMBLY
#define VISIT_UNSUPPORTED_OP(op)                          \
  template <typename Adapter>                             \
  void InstructionSelectorT<Adapter>::Visit##op(node_t) { \
    UNREACHABLE();                                        \
  }
MACHINE_SIMD128_OP_LIST(VISIT_UNSUPPORTED_OP)
MACHINE_SIMD256_OP_LIST(VISIT_UNSUPPORTED_OP)
#endif

template <typename Adapter>
void InstructionSelectorT<Adapter>::AddOutputToSelectContinuation(
    OperandGenerator* g, int first_input_index, node_t node) {
  continuation_outputs_.push_back(
      g->DefineSameAsInput(node, first_input_index));
}

// static
MachineOperatorBuilder::Flags
InstructionSelector::SupportedMachineOperatorFlags() {
  MachineOperatorBuilder::Flags flags =
      MachineOperatorBuilder::kWord32ShiftIsSafe |
      MachineOperatorBuilder::kWord32Ctz | MachineOperatorBuilder::kWord64Ctz |
      MachineOperatorBuilder::kWord32Rol | MachineOperatorBuilder::kWord64Rol |
      MachineOperatorBuilder::kWord32Select |
      MachineOperatorBuilder::kWord64Select;
  if (CpuFeatures::IsSupported(POPCNT)) {
    flags |= MachineOperatorBuilder::kWord32Popcnt |
             MachineOperatorBuilder::kWord64Popcnt;
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
  if (CpuFeatures::IsSupported(F16C)) {
    flags |= MachineOperatorBuilder::kFloat16;
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
