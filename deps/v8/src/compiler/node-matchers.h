// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_NODE_MATCHERS_H_
#define V8_COMPILER_NODE_MATCHERS_H_

#include <cmath>
#include <limits>

#include "src/base/bounds.h"
#include "src/base/compiler-specific.h"
#include "src/base/numbers/double.h"
#include "src/codegen/external-reference.h"
#include "src/common/globals.h"
#include "src/compiler/common-operator.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator.h"
#include "src/objects/heap-object.h"

namespace v8::internal::compiler {

class JSHeapBroker;

// A pattern matcher for nodes.
struct NodeMatcher {
  explicit NodeMatcher(Node* node) : node_(node) {}

  Node* node() const { return node_; }
  const Operator* op() const { return node()->op(); }
  IrOpcode::Value opcode() const { return node()->opcode(); }

  bool HasProperty(Operator::Property property) const {
    return op()->HasProperty(property);
  }
  Node* InputAt(int index) const { return node()->InputAt(index); }

  bool Equals(const Node* node) const { return node_ == node; }

  bool IsComparison() const;

#define DEFINE_IS_OPCODE(Opcode, ...) \
  bool Is##Opcode() const { return opcode() == IrOpcode::k##Opcode; }
  ALL_OP_LIST(DEFINE_IS_OPCODE)
#undef DEFINE_IS_OPCODE

 private:
  Node* node_;
};

inline Node* SkipValueIdentities(Node* node) {
  while (NodeProperties::IsValueIdentity(node, &node)) {
  }
  DCHECK_NOT_NULL(node);
  return node;
}

// A pattern matcher for abitrary value constants.
//
// Note that value identities on the input node are skipped when matching. The
// resolved value may not be a parameter of the input node. The node() method
// returns the unmodified input node. This is by design, as reducers may wish to
// match value constants but delay reducing the node until a later phase.
template <typename T, IrOpcode::Value kOpcode>
struct ValueMatcher : public NodeMatcher {
  using ValueType = T;

  explicit ValueMatcher(Node* node)
      : NodeMatcher(node), resolved_value_(), has_resolved_value_(false) {
    node = SkipValueIdentities(node);
    has_resolved_value_ = node->opcode() == kOpcode;
    if (has_resolved_value_) {
      resolved_value_ = OpParameter<T>(node->op());
    }
  }

  bool HasResolvedValue() const { return has_resolved_value_; }
  const T& ResolvedValue() const {
    CHECK(HasResolvedValue());
    return resolved_value_;
  }

 private:
  T resolved_value_;
  bool has_resolved_value_;
};

template <>
inline ValueMatcher<uint32_t, IrOpcode::kInt32Constant>::ValueMatcher(
    Node* node)
    : NodeMatcher(node), resolved_value_(), has_resolved_value_(false) {
  node = SkipValueIdentities(node);
  has_resolved_value_ = node->opcode() == IrOpcode::kInt32Constant;
  if (has_resolved_value_) {
    resolved_value_ = static_cast<uint32_t>(OpParameter<int32_t>(node->op()));
  }
}

template <>
inline ValueMatcher<int64_t, IrOpcode::kInt64Constant>::ValueMatcher(Node* node)
    : NodeMatcher(node), resolved_value_(), has_resolved_value_(false) {
  node = SkipValueIdentities(node);
  if (node->opcode() == IrOpcode::kInt32Constant) {
    resolved_value_ = OpParameter<int32_t>(node->op());
    has_resolved_value_ = true;
  } else if (node->opcode() == IrOpcode::kInt64Constant) {
    resolved_value_ = OpParameter<int64_t>(node->op());
    has_resolved_value_ = true;
  }
}

template <>
inline ValueMatcher<uint64_t, IrOpcode::kInt64Constant>::ValueMatcher(
    Node* node)
    : NodeMatcher(node), resolved_value_(), has_resolved_value_(false) {
  node = SkipValueIdentities(node);
  if (node->opcode() == IrOpcode::kInt32Constant) {
    resolved_value_ = static_cast<uint32_t>(OpParameter<int32_t>(node->op()));
    has_resolved_value_ = true;
  } else if (node->opcode() == IrOpcode::kInt64Constant) {
    resolved_value_ = static_cast<uint64_t>(OpParameter<int64_t>(node->op()));
    has_resolved_value_ = true;
  }
}

// A pattern matcher for integer constants.
template <typename T, IrOpcode::Value kOpcode>
struct IntMatcher final : public ValueMatcher<T, kOpcode> {
  explicit IntMatcher(Node* node) : ValueMatcher<T, kOpcode>(node) {}

  bool Is(const T& value) const {
    return this->HasResolvedValue() && this->ResolvedValue() == value;
  }
  bool IsInRange(const T& low, const T& high) const {
    return this->HasResolvedValue() &&
           base::IsInRange(this->ResolvedValue(), low, high);
  }
  bool IsMultipleOf(T n) const {
    return this->HasResolvedValue() && (this->ResolvedValue() % n) == 0;
  }
  bool IsPowerOf2() const {
    return this->HasResolvedValue() && this->ResolvedValue() > 0 &&
           (this->ResolvedValue() & (this->ResolvedValue() - 1)) == 0;
  }
  bool IsNegativePowerOf2() const {
    return this->HasResolvedValue() && this->ResolvedValue() < 0 &&
           ((this->ResolvedValue() == std::numeric_limits<T>::min()) ||
            (-this->ResolvedValue() & (-this->ResolvedValue() - 1)) == 0);
  }
  bool IsNegative() const {
    return this->HasResolvedValue() && this->ResolvedValue() < 0;
  }
};

using Int32Matcher = IntMatcher<int32_t, IrOpcode::kInt32Constant>;
using Uint32Matcher = IntMatcher<uint32_t, IrOpcode::kInt32Constant>;
using Int64Matcher = IntMatcher<int64_t, IrOpcode::kInt64Constant>;
using Uint64Matcher = IntMatcher<uint64_t, IrOpcode::kInt64Constant>;
#if V8_ENABLE_WEBASSEMBLY
using V128ConstMatcher =
    ValueMatcher<S128ImmediateParameter, IrOpcode::kS128Const>;
#endif  // V8_ENABLE_WEBASSEMBLY
#if V8_HOST_ARCH_32_BIT
using IntPtrMatcher = Int32Matcher;
using UintPtrMatcher = Uint32Matcher;
#else
using IntPtrMatcher = Int64Matcher;
using UintPtrMatcher = Uint64Matcher;
#endif


// A pattern matcher for floating point constants.
template <typename T, IrOpcode::Value kOpcode>
struct FloatMatcher final : public ValueMatcher<T, kOpcode> {
  explicit FloatMatcher(Node* node) : ValueMatcher<T, kOpcode>(node) {}

  bool Is(const T& value) const {
    return this->HasResolvedValue() && this->ResolvedValue() == value;
  }
  bool IsInRange(const T& low, const T& high) const {
    return this->HasResolvedValue() && low <= this->ResolvedValue() &&
           this->ResolvedValue() <= high;
  }
  bool IsMinusZero() const {
    return this->Is(0.0) && std::signbit(this->ResolvedValue());
  }
  bool IsNegative() const {
    return this->HasResolvedValue() && this->ResolvedValue() < 0.0;
  }
  bool IsNaN() const {
    return this->HasResolvedValue() && std::isnan(this->ResolvedValue());
  }
  bool IsZero() const {
    return this->Is(0.0) && !std::signbit(this->ResolvedValue());
  }
  bool IsNormal() const {
    return this->HasResolvedValue() && std::isnormal(this->ResolvedValue());
  }
  bool IsInteger() const {
    return this->HasResolvedValue() &&
           std::nearbyint(this->ResolvedValue()) == this->ResolvedValue();
  }
  bool IsPositiveOrNegativePowerOf2() const {
    if (!this->HasResolvedValue() || (this->ResolvedValue() == 0.0)) {
      return false;
    }
    base::Double value = base::Double(this->ResolvedValue());
    return !value.IsInfinite() && base::bits::IsPowerOfTwo(value.Significand());
  }
};

using Float32Matcher = FloatMatcher<float, IrOpcode::kFloat32Constant>;
using Float64Matcher = FloatMatcher<double, IrOpcode::kFloat64Constant>;
using NumberMatcher = FloatMatcher<double, IrOpcode::kNumberConstant>;

// A pattern matcher for heap object constants.
template <IrOpcode::Value kHeapConstantOpcode>
struct HeapObjectMatcherImpl final
    : public ValueMatcher<Handle<HeapObject>, kHeapConstantOpcode> {
  explicit HeapObjectMatcherImpl(Node* node)
      : ValueMatcher<Handle<HeapObject>, kHeapConstantOpcode>(node) {}

  bool Is(Handle<HeapObject> const& value) const {
    return this->HasResolvedValue() &&
           this->ResolvedValue().address() == value.address();
  }

  HeapObjectRef Ref(JSHeapBroker* broker) const {
    // TODO(jgruber,chromium:1209798): Using kAssumeMemoryFence works around
    // the fact that the graph stores handles (and not refs). The assumption is
    // that any handle inserted into the graph is safe to read; but we don't
    // preserve the reason why it is safe to read. Thus we must over-approximate
    // here and assume the existence of a memory fence. In the future, we should
    // consider having the graph store ObjectRefs or ObjectData pointer instead,
    // which would make new ref construction here unnecessary.
    return MakeRefAssumeMemoryFence(broker, this->ResolvedValue());
  }
};

using HeapObjectMatcher = HeapObjectMatcherImpl<IrOpcode::kHeapConstant>;
using CompressedHeapObjectMatcher =
    HeapObjectMatcherImpl<IrOpcode::kCompressedHeapConstant>;

// A pattern matcher for external reference constants.
struct ExternalReferenceMatcher final
    : public ValueMatcher<ExternalReference, IrOpcode::kExternalConstant> {
  explicit ExternalReferenceMatcher(Node* node)
      : ValueMatcher<ExternalReference, IrOpcode::kExternalConstant>(node) {}
  bool Is(const ExternalReference& value) const {
    return this->HasResolvedValue() && this->ResolvedValue() == value;
  }
};


// For shorter pattern matching code, this struct matches the inputs to
// machine-level load operations.
template <typename Object>
struct LoadMatcher : public NodeMatcher {
  explicit LoadMatcher(Node* node)
      : NodeMatcher(node), object_(InputAt(0)), index_(InputAt(1)) {}

  using ObjectMatcher = Object;

  Object const& object() const { return object_; }
  IntPtrMatcher const& index() const { return index_; }

 private:
  Object const object_;
  IntPtrMatcher const index_;
};


// For shorter pattern matching code, this struct matches both the left and
// right hand sides of a binary operation and can put constants on the right
// if they appear on the left hand side of a commutative operation.
template <typename Left, typename Right, MachineRepresentation rep>
struct BinopMatcher : public NodeMatcher {
  explicit BinopMatcher(Node* node)
      : NodeMatcher(node), left_(InputAt(0)), right_(InputAt(1)) {
    if (HasProperty(Operator::kCommutative)) PutConstantOnRight();
  }
  BinopMatcher(Node* node, bool allow_input_swap)
      : NodeMatcher(node), left_(InputAt(0)), right_(InputAt(1)) {
    if (allow_input_swap) PutConstantOnRight();
  }

  using LeftMatcher = Left;
  using RightMatcher = Right;

  static constexpr MachineRepresentation representation = rep;

  const Left& left() const { return left_; }
  const Right& right() const { return right_; }

  bool IsFoldable() const {
    return left().HasResolvedValue() && right().HasResolvedValue();
  }
  bool LeftEqualsRight() const { return left().node() == right().node(); }

  bool OwnsInput(Node* input) {
    for (Node* use : input->uses()) {
      if (use != node()) {
        return false;
      }
    }
    return true;
  }

 protected:
  void SwapInputs() {
    std::swap(left_, right_);
    // TODO(turbofan): This modification should notify the reducers using
    // BinopMatcher. Alternatively, all reducers (especially value numbering)
    // could ignore the ordering for commutative binops.
    node()->ReplaceInput(0, left().node());
    node()->ReplaceInput(1, right().node());
  }

 private:
  void PutConstantOnRight() {
    if (left().HasResolvedValue() && !right().HasResolvedValue()) {
      SwapInputs();
    }
  }

  Left left_;
  Right right_;
};

using Int32BinopMatcher =
    BinopMatcher<Int32Matcher, Int32Matcher, MachineRepresentation::kWord32>;
using Uint32BinopMatcher =
    BinopMatcher<Uint32Matcher, Uint32Matcher, MachineRepresentation::kWord32>;
using Int64BinopMatcher =
    BinopMatcher<Int64Matcher, Int64Matcher, MachineRepresentation::kWord64>;
using Uint64BinopMatcher =
    BinopMatcher<Uint64Matcher, Uint64Matcher, MachineRepresentation::kWord64>;
using IntPtrBinopMatcher = BinopMatcher<IntPtrMatcher, IntPtrMatcher,
                                        MachineType::PointerRepresentation()>;
using UintPtrBinopMatcher = BinopMatcher<UintPtrMatcher, UintPtrMatcher,
                                         MachineType::PointerRepresentation()>;
using Float32BinopMatcher = BinopMatcher<Float32Matcher, Float32Matcher,
                                         MachineRepresentation::kFloat32>;
using Float64BinopMatcher = BinopMatcher<Float64Matcher, Float64Matcher,
                                         MachineRepresentation::kFloat64>;
using NumberBinopMatcher =
    BinopMatcher<NumberMatcher, NumberMatcher, MachineRepresentation::kTagged>;
using HeapObjectBinopMatcher =
    BinopMatcher<HeapObjectMatcher, HeapObjectMatcher,
                 MachineRepresentation::kTagged>;
using CompressedHeapObjectBinopMatcher =
    BinopMatcher<CompressedHeapObjectMatcher, CompressedHeapObjectMatcher,
                 MachineRepresentation::kCompressed>;

template <class BinopMatcher, IrOpcode::Value kMulOpcode,
          IrOpcode::Value kShiftOpcode>
struct ScaleMatcher {
  explicit ScaleMatcher(Node* node, bool allow_power_of_two_plus_one = false)
      : scale_(-1), power_of_two_plus_one_(false) {
    if (node->InputCount() < 2) return;
    BinopMatcher m(node);
    if (node->opcode() == kShiftOpcode) {
      if (m.right().HasResolvedValue()) {
        typename BinopMatcher::RightMatcher::ValueType value =
            m.right().ResolvedValue();
        if (value >= 0 && value <= 3) {
          scale_ = static_cast<int>(value);
        }
      }
    } else if (node->opcode() == kMulOpcode) {
      if (m.right().HasResolvedValue()) {
        typename BinopMatcher::RightMatcher::ValueType value =
            m.right().ResolvedValue();
        if (value == 1) {
          scale_ = 0;
        } else if (value == 2) {
          scale_ = 1;
        } else if (value == 4) {
          scale_ = 2;
        } else if (value == 8) {
          scale_ = 3;
        } else if (allow_power_of_two_plus_one) {
          if (value == 3) {
            scale_ = 1;
            power_of_two_plus_one_ = true;
          } else if (value == 5) {
            scale_ = 2;
            power_of_two_plus_one_ = true;
          } else if (value == 9) {
            scale_ = 3;
            power_of_two_plus_one_ = true;
          }
        }
      }
    }
  }

  bool matches() const { return scale_ != -1; }
  int scale() const { return scale_; }
  bool power_of_two_plus_one() const { return power_of_two_plus_one_; }

 private:
  int scale_;
  bool power_of_two_plus_one_;
};

using Int32ScaleMatcher =
    ScaleMatcher<Int32BinopMatcher, IrOpcode::kInt32Mul, IrOpcode::kWord32Shl>;
using Int64ScaleMatcher =
    ScaleMatcher<Int64BinopMatcher, IrOpcode::kInt64Mul, IrOpcode::kWord64Shl>;

template <class BinopMatcher, IrOpcode::Value AddOpcode,
          IrOpcode::Value SubOpcode, IrOpcode::Value kMulOpcode,
          IrOpcode::Value kShiftOpcode>
struct AddMatcher : public BinopMatcher {
  static const IrOpcode::Value kAddOpcode = AddOpcode;
  static const IrOpcode::Value kSubOpcode = SubOpcode;
  using Matcher = ScaleMatcher<BinopMatcher, kMulOpcode, kShiftOpcode>;

  AddMatcher(Node* node, bool allow_input_swap)
      : BinopMatcher(node, allow_input_swap),
        scale_(-1),
        power_of_two_plus_one_(false) {
    Initialize(node, allow_input_swap);
  }
  explicit AddMatcher(Node* node)
      : BinopMatcher(node, node->op()->HasProperty(Operator::kCommutative)),
        scale_(-1),
        power_of_two_plus_one_(false) {
    Initialize(node, node->op()->HasProperty(Operator::kCommutative));
  }

  bool HasIndexInput() const { return scale_ != -1; }
  Node* IndexInput() const {
    DCHECK(HasIndexInput());
    return this->left().node()->InputAt(0);
  }
  int scale() const {
    DCHECK(HasIndexInput());
    return scale_;
  }
  bool power_of_two_plus_one() const { return power_of_two_plus_one_; }

 private:
  void Initialize(Node* node, bool allow_input_swap) {
    Matcher left_matcher(this->left().node(), true);
    if (left_matcher.matches()) {
      scale_ = left_matcher.scale();
      power_of_two_plus_one_ = left_matcher.power_of_two_plus_one();
      return;
    }

    if (!allow_input_swap) {
      return;
    }

    Matcher right_matcher(this->right().node(), true);
    if (right_matcher.matches()) {
      scale_ = right_matcher.scale();
      power_of_two_plus_one_ = right_matcher.power_of_two_plus_one();
      this->SwapInputs();
      return;
    }

    if ((this->left().opcode() != kSubOpcode &&
         this->left().opcode() != kAddOpcode) &&
        (this->right().opcode() == kAddOpcode ||
         this->right().opcode() == kSubOpcode)) {
      this->SwapInputs();
    }
  }

  int scale_;
  bool power_of_two_plus_one_;
};

using Int32AddMatcher =
    AddMatcher<Int32BinopMatcher, IrOpcode::kInt32Add, IrOpcode::kInt32Sub,
               IrOpcode::kInt32Mul, IrOpcode::kWord32Shl>;
using Int64AddMatcher =
    AddMatcher<Int64BinopMatcher, IrOpcode::kInt64Add, IrOpcode::kInt64Sub,
               IrOpcode::kInt64Mul, IrOpcode::kWord64Shl>;

enum DisplacementMode { kPositiveDisplacement, kNegativeDisplacement };

enum class AddressOption : uint8_t {
  kAllowNone = 0u,
  kAllowInputSwap = 1u << 0,
  kAllowScale = 1u << 1,
  kAllowAll = kAllowInputSwap | kAllowScale
};

using AddressOptions = base::Flags<AddressOption, uint8_t>;
DEFINE_OPERATORS_FOR_FLAGS(AddressOptions)

template <class AddMatcher>
struct BaseWithIndexAndDisplacementMatcher {
  BaseWithIndexAndDisplacementMatcher(Node* node, AddressOptions options)
      : matches_(false),
        index_(nullptr),
        scale_(0),
        base_(nullptr),
        displacement_(nullptr),
        displacement_mode_(kPositiveDisplacement) {
    Initialize(node, options);
  }

  explicit BaseWithIndexAndDisplacementMatcher(Node* node)
      : matches_(false),
        index_(nullptr),
        scale_(0),
        base_(nullptr),
        displacement_(nullptr),
        displacement_mode_(kPositiveDisplacement) {
    Initialize(node, AddressOption::kAllowScale |
                         (node->op()->HasProperty(Operator::kCommutative)
                              ? AddressOption::kAllowInputSwap
                              : AddressOption::kAllowNone));
  }

  bool matches() const { return matches_; }
  Node* index() const { return index_; }
  int scale() const { return scale_; }
  Node* base() const { return base_; }
  Node* displacement() const { return displacement_; }
  DisplacementMode displacement_mode() const { return displacement_mode_; }

 private:
  bool matches_;
  Node* index_;
  int scale_;
  Node* base_;
  Node* displacement_;
  DisplacementMode displacement_mode_;

  void Initialize(Node* node, AddressOptions options) {
    // The BaseWithIndexAndDisplacementMatcher canonicalizes the order of
    // displacements and scale factors that are used as inputs, so instead of
    // enumerating all possible patterns by brute force, checking for node
    // clusters using the following templates in the following order suffices to
    // find all of the interesting cases (S = index * scale, B = base input, D =
    // displacement input):
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
    if (node->InputCount() < 2) return;
    AddMatcher m(node, options & AddressOption::kAllowInputSwap);
    Node* left = m.left().node();
    Node* right = m.right().node();
    Node* displacement = nullptr;
    Node* base = nullptr;
    Node* index = nullptr;
    Node* scale_expression = nullptr;
    bool power_of_two_plus_one = false;
    DisplacementMode displacement_mode = kPositiveDisplacement;
    int scale = 0;
    if (m.HasIndexInput() && OwnedByAddressingOperand(left)) {
      index = m.IndexInput();
      scale = m.scale();
      scale_expression = left;
      power_of_two_plus_one = m.power_of_two_plus_one();
      bool match_found = false;
      if (right->opcode() == AddMatcher::kSubOpcode &&
          OwnedByAddressingOperand(right)) {
        AddMatcher right_matcher(right);
        if (right_matcher.right().HasResolvedValue()) {
          // (S + (B - D))
          base = right_matcher.left().node();
          displacement = right_matcher.right().node();
          displacement_mode = kNegativeDisplacement;
          match_found = true;
        }
      }
      if (!match_found) {
        if (right->opcode() == AddMatcher::kAddOpcode &&
            OwnedByAddressingOperand(right)) {
          AddMatcher right_matcher(right);
          if (right_matcher.right().HasResolvedValue()) {
            // (S + (B + D))
            base = right_matcher.left().node();
            displacement = right_matcher.right().node();
          } else {
            // (S + (B + B))
            base = right;
          }
        } else if (m.right().HasResolvedValue()) {
          // (S + D)
          displacement = right;
        } else {
          // (S + B)
          base = right;
        }
      }
    } else {
      bool match_found = false;
      if (left->opcode() == AddMatcher::kSubOpcode &&
          OwnedByAddressingOperand(left)) {
        AddMatcher left_matcher(left);
        Node* left_left = left_matcher.left().node();
        Node* left_right = left_matcher.right().node();
        if (left_matcher.right().HasResolvedValue()) {
          if (left_matcher.HasIndexInput() &&
              OwnedByAddressingOperand(left_left)) {
            // ((S - D) + B)
            index = left_matcher.IndexInput();
            scale = left_matcher.scale();
            scale_expression = left_left;
            power_of_two_plus_one = left_matcher.power_of_two_plus_one();
            displacement = left_right;
            displacement_mode = kNegativeDisplacement;
            base = right;
          } else {
            // ((B - D) + B)
            index = left_left;
            displacement = left_right;
            displacement_mode = kNegativeDisplacement;
            base = right;
          }
          match_found = true;
        }
      }
      if (!match_found) {
        if (left->opcode() == AddMatcher::kAddOpcode &&
            OwnedByAddressingOperand(left)) {
          AddMatcher left_matcher(left);
          Node* left_left = left_matcher.left().node();
          Node* left_right = left_matcher.right().node();
          if (left_matcher.HasIndexInput() &&
              OwnedByAddressingOperand(left_left)) {
            if (left_matcher.right().HasResolvedValue()) {
              // ((S + D) + B)
              index = left_matcher.IndexInput();
              scale = left_matcher.scale();
              scale_expression = left_left;
              power_of_two_plus_one = left_matcher.power_of_two_plus_one();
              displacement = left_right;
              base = right;
            } else if (m.right().HasResolvedValue()) {
              if (left->OwnedBy(node)) {
                // ((S + B) + D)
                index = left_matcher.IndexInput();
                scale = left_matcher.scale();
                scale_expression = left_left;
                power_of_two_plus_one = left_matcher.power_of_two_plus_one();
                base = left_right;
                displacement = right;
              } else {
                // (B + D)
                base = left;
                displacement = right;
              }
            } else {
              // (B + B)
              index = left;
              base = right;
            }
          } else {
            if (left_matcher.right().HasResolvedValue()) {
              // ((B + D) + B)
              index = left_left;
              displacement = left_right;
              base = right;
            } else if (m.right().HasResolvedValue()) {
              if (left->OwnedBy(node)) {
                // ((B + B) + D)
                index = left_left;
                base = left_right;
                displacement = right;
              } else {
                // (B + D)
                base = left;
                displacement = right;
              }
            } else {
              // (B + B)
              index = left;
              base = right;
            }
          }
        } else {
          if (m.right().HasResolvedValue()) {
            // (B + D)
            base = left;
            displacement = right;
          } else {
            // (B + B)
            base = left;
            index = right;
          }
        }
      }
    }
    if (displacement != nullptr) {
      if (displacement->opcode() == IrOpcode::kInt32Constant) {
        if (OpParameter<int32_t>(displacement->op()) == 0) {
          displacement = nullptr;
        }
      } else {
        DCHECK_EQ(displacement->opcode(), IrOpcode::kInt64Constant);
        if (OpParameter<int64_t>(displacement->op()) == 0) {
          displacement = nullptr;
        }
      }
    }
    if (power_of_two_plus_one) {
      if (base != nullptr) {
        // If the scale requires explicitly using the index as the base, but a
        // base is already part of the match, then the (1 << N + 1) scale factor
        // can't be folded into the match and the entire index * scale
        // calculation must be computed separately.
        index = scale_expression;
        scale = 0;
      } else {
        base = index;
      }
    }
    if (!(options & AddressOption::kAllowScale) && scale != 0) {
      index = scale_expression;
      scale = 0;
    }
    base_ = base;
    displacement_ = displacement;
    displacement_mode_ = displacement_mode;
    index_ = index;
    scale_ = scale;
    matches_ = true;
  }

  // Warning: When {node} is used by a Add/Sub instruction, this function does
  // not guarantee the Add/Sub will be part of a addressing operand.
  static bool OwnedByAddressingOperand(Node* node) {
    for (auto use : node->use_edges()) {
      Node* from = use.from();
      switch (from->opcode()) {
        case IrOpcode::kLoad:
        case IrOpcode::kLoadImmutable:
        case IrOpcode::kProtectedLoad:
        case IrOpcode::kLoadTrapOnNull:
        case IrOpcode::kInt32Add:
        case IrOpcode::kInt64Add:
          // Skip addressing uses.
          break;
        case IrOpcode::kInt32Sub:
          // If the subtrahend is not a constant, it is not an addressing use.
          if (from->InputAt(1)->opcode() != IrOpcode::kInt32Constant)
            return false;
          break;
        case IrOpcode::kInt64Sub:
          // If the subtrahend is not a constant, it is not an addressing use.
          if (from->InputAt(1)->opcode() != IrOpcode::kInt64Constant)
            return false;
          break;
        case IrOpcode::kStore:
        case IrOpcode::kProtectedStore:
        case IrOpcode::kStoreTrapOnNull:
          // If the stored value is this node, it is not an addressing use.
          if (from->InputAt(2) == node) return false;
          // Otherwise it is used as an address and skipped.
          break;
        default:
          // Non-addressing use found.
          return false;
      }
    }
    return true;
  }
};

using BaseWithIndexAndDisplacement32Matcher =
    BaseWithIndexAndDisplacementMatcher<Int32AddMatcher>;
using BaseWithIndexAndDisplacement64Matcher =
    BaseWithIndexAndDisplacementMatcher<Int64AddMatcher>;

struct V8_EXPORT_PRIVATE BranchMatcher : public NON_EXPORTED_BASE(NodeMatcher) {
  explicit BranchMatcher(Node* branch);

  bool Matched() const { return if_true_ && if_false_; }

  Node* Branch() const { return node(); }
  Node* IfTrue() const { return if_true_; }
  Node* IfFalse() const { return if_false_; }

 private:
  Node* if_true_;
  Node* if_false_;
};

struct V8_EXPORT_PRIVATE DiamondMatcher
    : public NON_EXPORTED_BASE(NodeMatcher) {
  explicit DiamondMatcher(Node* merge);

  bool Matched() const { return branch_; }
  bool IfProjectionsAreOwned() const {
    return if_true_->OwnedBy(node()) && if_false_->OwnedBy(node());
  }

  Node* Branch() const { return branch_; }
  Node* IfTrue() const { return if_true_; }
  Node* IfFalse() const { return if_false_; }
  Node* Merge() const { return node(); }

  Node* TrueInputOf(Node* phi) const {
    DCHECK(IrOpcode::IsPhiOpcode(phi->opcode()));
    DCHECK_EQ(3, phi->InputCount());
    DCHECK_EQ(Merge(), phi->InputAt(2));
    return phi->InputAt(if_true_ == Merge()->InputAt(0) ? 0 : 1);
  }

  Node* FalseInputOf(Node* phi) const {
    DCHECK(IrOpcode::IsPhiOpcode(phi->opcode()));
    DCHECK_EQ(3, phi->InputCount());
    DCHECK_EQ(Merge(), phi->InputAt(2));
    return phi->InputAt(if_true_ == Merge()->InputAt(0) ? 1 : 0);
  }

 private:
  Node* branch_;
  Node* if_true_;
  Node* if_false_;
};

#if V8_ENABLE_WEBASSEMBLY
struct LoadTransformMatcher
    : ValueMatcher<LoadTransformParameters, IrOpcode::kLoadTransform> {
  explicit LoadTransformMatcher(Node* node) : ValueMatcher(node) {}
  bool Is(LoadTransformation t) {
    return HasResolvedValue() && ResolvedValue().transformation == t;
  }
};
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_NODE_MATCHERS_H_
