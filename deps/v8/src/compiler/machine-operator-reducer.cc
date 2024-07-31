// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/machine-operator-reducer.h"

#include <cmath>
#include <cstdint>
#include <limits>

#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/base/ieee754.h"
#include "src/base/logging.h"
#include "src/base/overflowing-math.h"
#include "src/builtins/builtins.h"
#include "src/compiler/diamond.h"
#include "src/compiler/graph.h"
#include "src/compiler/js-operator.h"
#include "src/compiler/machine-graph.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/node-properties.h"
#include "src/compiler/opcodes.h"
#include "src/numbers/conversions-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

// Some optimizations performed by the MachineOperatorReducer can be applied
// to both Word32 and Word64 operations. Those are implemented in a generic
// way to be reused for different word sizes.
// This class adapts a generic algorithm to Word32 operations.
class Word32Adapter {
 public:
  using IntNBinopMatcher = Int32BinopMatcher;
  using UintNBinopMatcher = Uint32BinopMatcher;
  using intN_t = int32_t;
  using uintN_t = uint32_t;
  // WORD_SIZE refers to the N for which this adapter specializes.
  static constexpr std::size_t WORD_SIZE = 32;

  explicit Word32Adapter(MachineOperatorReducer* reducer) : r_(reducer) {}

  template <typename T>
  static bool IsWordNAnd(const T& x) {
    return x.IsWord32And();
  }
  template <typename T>
  static bool IsWordNShl(const T& x) {
    return x.IsWord32Shl();
  }
  template <typename T>
  static bool IsWordNShr(const T& x) {
    return x.IsWord32Shr();
  }
  template <typename T>
  static bool IsWordNSar(const T& x) {
    return x.IsWord32Sar();
  }
  static bool IsWordNSarShiftOutZeros(const Operator* op) {
    return op->opcode() == IrOpcode::kWord32Sar &&
           OpParameter<ShiftKind>(op) == ShiftKind::kShiftOutZeros;
  }
  template <typename T>
  static bool IsWordNXor(const T& x) {
    return x.IsWord32Xor();
  }
  template <typename T>
  static bool IsIntNAdd(const T& x) {
    return x.IsInt32Add();
  }
  template <typename T>
  static bool IsIntNMul(const T& x) {
    return x.IsInt32Mul();
  }

  const Operator* IntNAdd(MachineOperatorBuilder* machine) {
    return machine->Int32Add();
  }
  static const Operator* WordNEqual(MachineOperatorBuilder* machine) {
    return machine->Word32Equal();
  }

  Reduction ReplaceIntN(int32_t value) { return r_->ReplaceInt32(value); }
  Reduction ReduceWordNAnd(Node* node) { return r_->ReduceWord32And(node); }
  Reduction ReduceIntNAdd(Node* node) { return r_->ReduceInt32Add(node); }
  Reduction TryMatchWordNRor(Node* node) { return r_->TryMatchWord32Ror(node); }

  Node* IntNConstant(int32_t value) { return r_->Int32Constant(value); }
  Node* UintNConstant(uint32_t value) { return r_->Uint32Constant(value); }
  Node* WordNAnd(Node* lhs, Node* rhs) { return r_->Word32And(lhs, rhs); }

  Reduction ReduceWordNComparisons(Node* node) {
    return r_->ReduceWord32Comparisons(node);
  }

 private:
  MachineOperatorReducer* r_;
};

// Some optimizations performed by the MachineOperatorReducer can be applied
// to both Word32 and Word64 operations. Those are implemented in a generic
// way to be reused for different word sizes.
// This class adapts a generic algorithm to Word64 operations.
class Word64Adapter {
 public:
  using IntNBinopMatcher = Int64BinopMatcher;
  using UintNBinopMatcher = Uint64BinopMatcher;
  using intN_t = int64_t;
  using uintN_t = uint64_t;
  // WORD_SIZE refers to the N for which this adapter specializes.
  static constexpr std::size_t WORD_SIZE = 64;

  explicit Word64Adapter(MachineOperatorReducer* reducer) : r_(reducer) {}

  template <typename T>
  static bool IsWordNAnd(const T& x) {
    return x.IsWord64And();
  }
  template <typename T>
  static bool IsWordNShl(const T& x) {
    return x.IsWord64Shl();
  }
  template <typename T>
  static bool IsWordNShr(const T& x) {
    return x.IsWord64Shr();
  }
  template <typename T>
  static bool IsWordNSar(const T& x) {
    return x.IsWord64Sar();
  }
  static bool IsWordNSarShiftOutZeros(const Operator* op) {
    return op->opcode() == IrOpcode::kWord64Sar &&
           OpParameter<ShiftKind>(op) == ShiftKind::kShiftOutZeros;
  }
  template <typename T>
  static bool IsWordNXor(const T& x) {
    return x.IsWord64Xor();
  }
  template <typename T>
  static bool IsIntNAdd(const T& x) {
    return x.IsInt64Add();
  }
  template <typename T>
  static bool IsIntNMul(const T& x) {
    return x.IsInt64Mul();
  }

  static const Operator* IntNAdd(MachineOperatorBuilder* machine) {
    return machine->Int64Add();
  }
  static const Operator* WordNEqual(MachineOperatorBuilder* machine) {
    return machine->Word64Equal();
  }

  Reduction ReplaceIntN(int64_t value) { return r_->ReplaceInt64(value); }
  Reduction ReduceWordNAnd(Node* node) { return r_->ReduceWord64And(node); }
  Reduction ReduceIntNAdd(Node* node) { return r_->ReduceInt64Add(node); }
  Reduction TryMatchWordNRor(Node* node) {
    // TODO(nicohartmann@): Add a MachineOperatorReducer::TryMatchWord64Ror.
    return r_->NoChange();
  }

  Node* IntNConstant(int64_t value) { return r_->Int64Constant(value); }
  Node* UintNConstant(uint64_t value) { return r_->Uint64Constant(value); }
  Node* WordNAnd(Node* lhs, Node* rhs) { return r_->Word64And(lhs, rhs); }

  Reduction ReduceWordNComparisons(Node* node) {
    return r_->ReduceWord64Comparisons(node);
  }

 private:
  MachineOperatorReducer* r_;
};

namespace {

// TODO(jgruber): Consider replacing all uses of this function by
// std::numeric_limits<T>::quiet_NaN().
template <class T>
T SilenceNaN(T x) {
  DCHECK(std::isnan(x));
  // Do some calculation to make a signalling NaN quiet.
  return x - x;
}

}  // namespace

MachineOperatorReducer::MachineOperatorReducer(
    Editor* editor, MachineGraph* mcgraph,
    SignallingNanPropagation signalling_nan_propagation)
    : AdvancedReducer(editor),
      mcgraph_(mcgraph),
      signalling_nan_propagation_(signalling_nan_propagation) {}

MachineOperatorReducer::~MachineOperatorReducer() = default;

Node* MachineOperatorReducer::Float32Constant(float value) {
  return graph()->NewNode(common()->Float32Constant(value));
}

Node* MachineOperatorReducer::Float64Constant(double value) {
  return mcgraph()->Float64Constant(value);
}

Node* MachineOperatorReducer::Int32Constant(int32_t value) {
  return mcgraph()->Int32Constant(value);
}

Node* MachineOperatorReducer::Int64Constant(int64_t value) {
  return graph()->NewNode(common()->Int64Constant(value));
}

Node* MachineOperatorReducer::Float64Mul(Node* lhs, Node* rhs) {
  return graph()->NewNode(machine()->Float64Mul(), lhs, rhs);
}

Node* MachineOperatorReducer::Float64PowHalf(Node* value) {
  value =
      graph()->NewNode(machine()->Float64Add(), Float64Constant(0.0), value);
  Diamond d(graph(), common(),
            graph()->NewNode(machine()->Float64LessThanOrEqual(), value,
                             Float64Constant(-V8_INFINITY)),
            BranchHint::kFalse);
  return d.Phi(MachineRepresentation::kFloat64, Float64Constant(V8_INFINITY),
               graph()->NewNode(machine()->Float64Sqrt(), value));
}

Node* MachineOperatorReducer::Word32And(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Word32And(), lhs, rhs);
  Reduction const reduction = ReduceWord32And(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Word32Sar(Node* lhs, uint32_t rhs) {
  if (rhs == 0) return lhs;
  return graph()->NewNode(machine()->Word32Sar(), lhs, Uint32Constant(rhs));
}

Node* MachineOperatorReducer::Word64Sar(Node* lhs, uint32_t rhs) {
  if (rhs == 0) return lhs;
  return graph()->NewNode(machine()->Word64Sar(), lhs, Uint64Constant(rhs));
}

Node* MachineOperatorReducer::Word32Shr(Node* lhs, uint32_t rhs) {
  if (rhs == 0) return lhs;
  return graph()->NewNode(machine()->Word32Shr(), lhs, Uint32Constant(rhs));
}

Node* MachineOperatorReducer::Word64Shr(Node* lhs, uint32_t rhs) {
  if (rhs == 0) return lhs;
  return graph()->NewNode(machine()->Word64Shr(), lhs, Uint64Constant(rhs));
}

Node* MachineOperatorReducer::Word32Equal(Node* lhs, Node* rhs) {
  return graph()->NewNode(machine()->Word32Equal(), lhs, rhs);
}

Node* MachineOperatorReducer::Word64Equal(Node* lhs, Node* rhs) {
  return graph()->NewNode(machine()->Word64Equal(), lhs, rhs);
}

Node* MachineOperatorReducer::Word64And(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Word64And(), lhs, rhs);
  Reduction const reduction = ReduceWord64And(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Int32Add(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Int32Add(), lhs, rhs);
  Reduction const reduction = ReduceInt32Add(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Int64Add(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Int64Add(), lhs, rhs);
  Reduction const reduction = ReduceInt64Add(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Int32Sub(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Int32Sub(), lhs, rhs);
  Reduction const reduction = ReduceInt32Sub(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Int64Sub(Node* lhs, Node* rhs) {
  Node* const node = graph()->NewNode(machine()->Int64Sub(), lhs, rhs);
  Reduction const reduction = ReduceInt64Sub(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::Int32Mul(Node* lhs, Node* rhs) {
  return graph()->NewNode(machine()->Int32Mul(), lhs, rhs);
}

Node* MachineOperatorReducer::Int64Mul(Node* lhs, Node* rhs) {
  return graph()->NewNode(machine()->Int64Mul(), lhs, rhs);
}

Node* MachineOperatorReducer::Int32Div(Node* dividend, int32_t divisor) {
  DCHECK_NE(0, divisor);
  DCHECK_NE(std::numeric_limits<int32_t>::min(), divisor);
  base::MagicNumbersForDivision<uint32_t> const mag =
      base::SignedDivisionByConstant(base::bit_cast<uint32_t>(divisor));
  Node* quotient = graph()->NewNode(machine()->Int32MulHigh(), dividend,
                                    Uint32Constant(mag.multiplier));
  if (divisor > 0 && base::bit_cast<int32_t>(mag.multiplier) < 0) {
    quotient = Int32Add(quotient, dividend);
  } else if (divisor < 0 && base::bit_cast<int32_t>(mag.multiplier) > 0) {
    quotient = Int32Sub(quotient, dividend);
  }
  return Int32Add(Word32Sar(quotient, mag.shift), Word32Shr(dividend, 31));
}

Node* MachineOperatorReducer::Int64Div(Node* dividend, int64_t divisor) {
  DCHECK_NE(0, divisor);
  DCHECK_NE(std::numeric_limits<int64_t>::min(), divisor);
  base::MagicNumbersForDivision<uint64_t> const mag =
      base::SignedDivisionByConstant(base::bit_cast<uint64_t>(divisor));
  Node* quotient = graph()->NewNode(machine()->Int64MulHigh(), dividend,
                                    Uint64Constant(mag.multiplier));
  if (divisor > 0 && base::bit_cast<int64_t>(mag.multiplier) < 0) {
    quotient = Int64Add(quotient, dividend);
  } else if (divisor < 0 && base::bit_cast<int64_t>(mag.multiplier) > 0) {
    quotient = Int64Sub(quotient, dividend);
  }
  return Int64Add(Word64Sar(quotient, mag.shift), Word64Shr(dividend, 63));
}

Node* MachineOperatorReducer::Uint32Div(Node* dividend, uint32_t divisor) {
  DCHECK_LT(0u, divisor);
  // If the divisor is even, we can avoid using the expensive fixup by shifting
  // the dividend upfront.
  unsigned const shift = base::bits::CountTrailingZeros(divisor);
  dividend = Word32Shr(dividend, shift);
  divisor >>= shift;
  // Compute the magic number for the (shifted) divisor.
  base::MagicNumbersForDivision<uint32_t> const mag =
      base::UnsignedDivisionByConstant(divisor, shift);
  Node* quotient = graph()->NewNode(machine()->Uint32MulHigh(), dividend,
                                    Uint32Constant(mag.multiplier));
  if (mag.add) {
    DCHECK_LE(1u, mag.shift);
    quotient = Word32Shr(
        Int32Add(Word32Shr(Int32Sub(dividend, quotient), 1), quotient),
        mag.shift - 1);
  } else {
    quotient = Word32Shr(quotient, mag.shift);
  }
  return quotient;
}

Node* MachineOperatorReducer::Uint64Div(Node* dividend, uint64_t divisor) {
  DCHECK_LT(0u, divisor);
  // If the divisor is even, we can avoid using the expensive fixup by shifting
  // the dividend upfront.
  unsigned const shift = base::bits::CountTrailingZeros(divisor);
  dividend = Word64Shr(dividend, shift);
  divisor >>= shift;
  // Compute the magic number for the (shifted) divisor.
  base::MagicNumbersForDivision<uint64_t> const mag =
      base::UnsignedDivisionByConstant(divisor, shift);
  Node* quotient = graph()->NewNode(machine()->Uint64MulHigh(), dividend,
                                    Uint64Constant(mag.multiplier));
  if (mag.add) {
    DCHECK_LE(1u, mag.shift);
    quotient = Word64Shr(
        Int64Add(Word64Shr(Int64Sub(dividend, quotient), 1), quotient),
        mag.shift - 1);
  } else {
    quotient = Word64Shr(quotient, mag.shift);
  }
  return quotient;
}

Node* MachineOperatorReducer::TruncateInt64ToInt32(Node* value) {
  Node* const node = graph()->NewNode(machine()->TruncateInt64ToInt32(), value);
  Reduction const reduction = ReduceTruncateInt64ToInt32(node);
  return reduction.Changed() ? reduction.replacement() : node;
}

Node* MachineOperatorReducer::ChangeInt32ToInt64(Node* value) {
  return graph()->NewNode(machine()->ChangeInt32ToInt64(), value);
}

// Perform constant folding and strength reduction on machine operators.
Reduction MachineOperatorReducer::Reduce(Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kProjection:
      return ReduceProjection(ProjectionIndexOf(node->op()), node->InputAt(0));
    case IrOpcode::kWord32And:
      return ReduceWord32And(node);
    case IrOpcode::kWord64And:
      return ReduceWord64And(node);
    case IrOpcode::kWord32Or:
      return ReduceWord32Or(node);
    case IrOpcode::kWord64Or:
      return ReduceWord64Or(node);
    case IrOpcode::kWord32Xor:
      return ReduceWord32Xor(node);
    case IrOpcode::kWord64Xor:
      return ReduceWord64Xor(node);
    case IrOpcode::kWord32Shl:
      return ReduceWord32Shl(node);
    case IrOpcode::kWord64Shl:
      return ReduceWord64Shl(node);
    case IrOpcode::kWord32Shr:
      return ReduceWord32Shr(node);
    case IrOpcode::kWord64Shr:
      return ReduceWord64Shr(node);
    case IrOpcode::kWord32Sar:
      return ReduceWord32Sar(node);
    case IrOpcode::kWord64Sar:
      return ReduceWord64Sar(node);
    case IrOpcode::kWord32Ror: {
      Int32BinopMatcher m(node);
      if (m.right().Is(0)) return Replace(m.left().node());  // x ror 0 => x
      if (m.IsFoldable()) {  // K ror K => K  (K stands for arbitrary constants)
        return ReplaceInt32(base::bits::RotateRight32(
            m.left().ResolvedValue(), m.right().ResolvedValue() & 31));
      }
      break;
    }
    case IrOpcode::kWord32Equal:
      return ReduceWord32Equal(node);
    case IrOpcode::kWord64Equal:
      return ReduceWord64Equal(node);
    case IrOpcode::kInt32Add:
      return ReduceInt32Add(node);
    case IrOpcode::kInt64Add:
      return ReduceInt64Add(node);
    case IrOpcode::kInt32Sub:
      return ReduceInt32Sub(node);
    case IrOpcode::kInt64Sub:
      return ReduceInt64Sub(node);
    case IrOpcode::kInt32Mul: {
      Int32BinopMatcher m(node);
      if (m.right().Is(0)) return Replace(m.right().node());  // x * 0 => 0
      if (m.right().Is(1)) return Replace(m.left().node());   // x * 1 => x
      if (m.IsFoldable()) {  // K * K => K  (K stands for arbitrary constants)
        return ReplaceInt32(base::MulWithWraparound(m.left().ResolvedValue(),
                                                    m.right().ResolvedValue()));
      }
      if (m.right().Is(-1)) {  // x * -1 => 0 - x
        node->ReplaceInput(0, Int32Constant(0));
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Int32Sub());
        return Changed(node);
      }
      if (m.right().IsPowerOf2()) {  // x * 2^n => x << n
        node->ReplaceInput(1, Int32Constant(base::bits::WhichPowerOfTwo(
                                  m.right().ResolvedValue())));
        NodeProperties::ChangeOp(node, machine()->Word32Shl());
        return Changed(node).FollowedBy(ReduceWord32Shl(node));
      }
      // (x * Int32Constant(a)) * Int32Constant(b)) => x * Int32Constant(a * b)
      if (m.right().HasResolvedValue() && m.left().IsInt32Mul()) {
        Int32BinopMatcher n(m.left().node());
        if (n.right().HasResolvedValue() && m.OwnsInput(m.left().node())) {
          node->ReplaceInput(
              1, Int32Constant(base::MulWithWraparound(
                     m.right().ResolvedValue(), n.right().ResolvedValue())));
          node->ReplaceInput(0, n.left().node());
          return Changed(node);
        }
      }
      break;
    }
    case IrOpcode::kInt32MulWithOverflow: {
      Int32BinopMatcher m(node);
      if (m.right().Is(2)) {
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Int32AddWithOverflow());
        return Changed(node);
      }
      if (m.right().Is(-1)) {
        node->ReplaceInput(0, Int32Constant(0));
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Int32SubWithOverflow());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kInt64Mul:
      return ReduceInt64Mul(node);
    case IrOpcode::kInt32Div:
      return ReduceInt32Div(node);
    case IrOpcode::kInt64Div:
      return ReduceInt64Div(node);
    case IrOpcode::kUint32Div:
      return ReduceUint32Div(node);
    case IrOpcode::kUint64Div:
      return ReduceUint64Div(node);
    case IrOpcode::kInt32Mod:
      return ReduceInt32Mod(node);
    case IrOpcode::kInt64Mod:
      return ReduceInt64Mod(node);
    case IrOpcode::kUint32Mod:
      return ReduceUint32Mod(node);
    case IrOpcode::kUint64Mod:
      return ReduceUint64Mod(node);
    case IrOpcode::kInt32LessThan: {
      Int32BinopMatcher m(node);
      if (m.IsFoldable()) {  // K < K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <
                           m.right().ResolvedValue());
      }
      if (m.LeftEqualsRight()) return ReplaceBool(false);  // x < x => false
      if (m.left().IsWord32Or() && m.right().Is(0)) {
        // (x | K) < 0 => true or (K | x) < 0 => true iff K < 0
        Int32BinopMatcher mleftmatcher(m.left().node());
        if (mleftmatcher.left().IsNegative() ||
            mleftmatcher.right().IsNegative()) {
          return ReplaceBool(true);
        }
      }
      return ReduceWord32Comparisons(node);
    }
    case IrOpcode::kInt32LessThanOrEqual: {
      Int32BinopMatcher m(node);
      if (m.IsFoldable()) {  // K <= K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <=
                           m.right().ResolvedValue());
      }
      if (m.LeftEqualsRight()) return ReplaceBool(true);  // x <= x => true
      return ReduceWord32Comparisons(node);
    }
    case IrOpcode::kUint32LessThan: {
      Uint32BinopMatcher m(node);
      if (m.left().Is(kMaxUInt32)) return ReplaceBool(false);  // M < x => false
      if (m.right().Is(0)) return ReplaceBool(false);          // x < 0 => false
      if (m.IsFoldable()) {  // K < K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <
                           m.right().ResolvedValue());
      }
      if (m.LeftEqualsRight()) return ReplaceBool(false);  // x < x => false
      if (m.left().IsWord32Sar() && m.right().HasResolvedValue()) {
        Int32BinopMatcher mleft(m.left().node());
        if (mleft.right().HasResolvedValue()) {
          // (x >> K) < C => x < (C << K)
          // when C < (M >> K)
          const uint32_t c = m.right().ResolvedValue();
          const uint32_t k = mleft.right().ResolvedValue() & 0x1F;
          if (c < static_cast<uint32_t>(kMaxInt >> k)) {
            node->ReplaceInput(0, mleft.left().node());
            node->ReplaceInput(1, Uint32Constant(c << k));
            return Changed(node);
          }
          // TODO(turbofan): else the comparison is always true.
        }
      }
      return ReduceWord32Comparisons(node);
    }
    case IrOpcode::kUint32LessThanOrEqual: {
      return ReduceUintNLessThanOrEqual<Word32Adapter>(node);
    }
    case IrOpcode::kFloat32Sub: {
      Float32BinopMatcher m(node);
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.right().Is(0) &&
          (std::copysign(1.0, m.right().ResolvedValue()) > 0)) {
        return Replace(m.left().node());  // x - 0 => x
      }
      if (m.right().IsNaN()) {  // x - NaN => NaN
        return ReplaceFloat32(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {  // NaN - x => NaN
        return ReplaceFloat32(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // L - R => (L - R)
        return ReplaceFloat32(m.left().ResolvedValue() -
                              m.right().ResolvedValue());
      }
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.left().IsMinusZero()) {
        // -0.0 - round_down(-0.0 - R) => round_up(R)
        if (machine()->Float32RoundUp().IsSupported() &&
            m.right().IsFloat32RoundDown()) {
          if (m.right().InputAt(0)->opcode() == IrOpcode::kFloat32Sub) {
            Float32BinopMatcher mright0(m.right().InputAt(0));
            if (mright0.left().IsMinusZero()) {
              return Replace(graph()->NewNode(machine()->Float32RoundUp().op(),
                                              mright0.right().node()));
            }
          }
        }
        // -0.0 - R => -R
        node->RemoveInput(0);
        NodeProperties::ChangeOp(node, machine()->Float32Neg());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kFloat64Add: {
      Float64BinopMatcher m(node);
      if (m.right().IsNaN()) {  // x + NaN => NaN
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {  // NaN + x => NaN
        return ReplaceFloat64(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // K + K => K  (K stands for arbitrary constants)
        return ReplaceFloat64(m.left().ResolvedValue() +
                              m.right().ResolvedValue());
      }
      break;
    }
    case IrOpcode::kFloat64Sub: {
      Float64BinopMatcher m(node);
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.right().Is(0) &&
          (base::Double(m.right().ResolvedValue()).Sign() > 0)) {
        return Replace(m.left().node());  // x - 0 => x
      }
      if (m.right().IsNaN()) {  // x - NaN => NaN
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {  // NaN - x => NaN
        return ReplaceFloat64(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // L - R => (L - R)
        return ReplaceFloat64(m.left().ResolvedValue() -
                              m.right().ResolvedValue());
      }
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.left().IsMinusZero()) {
        // -0.0 - round_down(-0.0 - R) => round_up(R)
        if (machine()->Float64RoundUp().IsSupported() &&
            m.right().IsFloat64RoundDown()) {
          if (m.right().InputAt(0)->opcode() == IrOpcode::kFloat64Sub) {
            Float64BinopMatcher mright0(m.right().InputAt(0));
            if (mright0.left().IsMinusZero()) {
              return Replace(graph()->NewNode(machine()->Float64RoundUp().op(),
                                              mright0.right().node()));
            }
          }
        }
        // -0.0 - R => -R
        node->RemoveInput(0);
        NodeProperties::ChangeOp(node, machine()->Float64Neg());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kFloat64Mul: {
      Float64BinopMatcher m(node);
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.right().Is(1))
        return Replace(m.left().node());  // x * 1.0 => x
      if (m.right().Is(-1)) {             // x * -1.0 => -0.0 - x
        node->ReplaceInput(0, Float64Constant(-0.0));
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Float64Sub());
        return Changed(node);
      }
      if (m.right().IsNaN()) {  // x * NaN => NaN
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // K * K => K  (K stands for arbitrary constants)
        return ReplaceFloat64(m.left().ResolvedValue() *
                              m.right().ResolvedValue());
      }
      if (m.right().Is(2)) {  // x * 2.0 => x + x
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Float64Add());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kFloat64Div: {
      Float64BinopMatcher m(node);
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.right().Is(1))
        return Replace(m.left().node());  // x / 1.0 => x
      // TODO(ahaas): We could do x / 1.0 = x if we knew that x is not an sNaN.
      if (m.right().IsNaN()) {  // x / NaN => NaN
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {  // NaN / x => NaN
        return ReplaceFloat64(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // K / K => K  (K stands for arbitrary constants)
        return ReplaceFloat64(
            base::Divide(m.left().ResolvedValue(), m.right().ResolvedValue()));
      }
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.right().Is(-1)) {  // x / -1.0 => -x
        node->RemoveInput(1);
        NodeProperties::ChangeOp(node, machine()->Float64Neg());
        return Changed(node);
      }
      if (m.right().IsNormal() && m.right().IsPositiveOrNegativePowerOf2()) {
        // All reciprocals of non-denormal powers of two can be represented
        // exactly, so division by power of two can be reduced to
        // multiplication by reciprocal, with the same result.
        node->ReplaceInput(1, Float64Constant(1.0 / m.right().ResolvedValue()));
        NodeProperties::ChangeOp(node, machine()->Float64Mul());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kFloat64Mod: {
      Float64BinopMatcher m(node);
      if (m.right().Is(0)) {  // x % 0 => NaN
        return ReplaceFloat64(std::numeric_limits<double>::quiet_NaN());
      }
      if (m.right().IsNaN()) {  // x % NaN => NaN
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {  // NaN % x => NaN
        return ReplaceFloat64(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {  // K % K => K  (K stands for arbitrary constants)
        return ReplaceFloat64(
            Modulo(m.left().ResolvedValue(), m.right().ResolvedValue()));
      }
      break;
    }
    case IrOpcode::kFloat64Acos: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::acos(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Acosh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::acosh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Asin: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::asin(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Asinh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::asinh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Atan: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::atan(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Atanh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::atanh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Atan2: {
      Float64BinopMatcher m(node);
      if (m.right().IsNaN()) {
        return ReplaceFloat64(SilenceNaN(m.right().ResolvedValue()));
      }
      if (m.left().IsNaN()) {
        return ReplaceFloat64(SilenceNaN(m.left().ResolvedValue()));
      }
      if (m.IsFoldable()) {
        return ReplaceFloat64(base::ieee754::atan2(m.left().ResolvedValue(),
                                                   m.right().ResolvedValue()));
      }
      break;
    }
    case IrOpcode::kFloat64Cbrt: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::cbrt(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Cos: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(COS_IMPL(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Cosh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::cosh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Exp: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::exp(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Expm1: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::expm1(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Log: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::log(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Log1p: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::log1p(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Log10: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::log10(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Log2: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::log2(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Pow: {
      Float64BinopMatcher m(node);
      if (m.IsFoldable()) {
        return ReplaceFloat64(base::ieee754::pow(m.left().ResolvedValue(),
                                                 m.right().ResolvedValue()));
      } else if (m.right().Is(0.0)) {  // x ** +-0.0 => 1.0
        return ReplaceFloat64(1.0);
      } else if (m.right().Is(2.0)) {  // x ** 2.0 => x * x
        node->ReplaceInput(1, m.left().node());
        NodeProperties::ChangeOp(node, machine()->Float64Mul());
        return Changed(node);
      } else if (m.right().Is(0.5)) {
        // x ** 0.5 => if x <= -Infinity then Infinity else sqrt(0.0 + x)
        return Replace(Float64PowHalf(m.left().node()));
      }
      break;
    }
    case IrOpcode::kFloat64Sin: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(SIN_IMPL(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Sinh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::sinh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Tan: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::tan(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kFloat64Tanh: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(base::ieee754::tanh(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kChangeFloat32ToFloat64: {
      Float32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) {
        if (signalling_nan_propagation_ == kSilenceSignallingNan &&
            std::isnan(m.ResolvedValue())) {
          return ReplaceFloat64(SilenceNaN(m.ResolvedValue()));
        }
        return ReplaceFloat64(m.ResolvedValue());
      }
      break;
    }
    case IrOpcode::kChangeFloat64ToInt32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt32(FastD2IChecked(m.ResolvedValue()));
      if (m.IsChangeInt32ToFloat64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeFloat64ToInt64: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt64(static_cast<int64_t>(m.ResolvedValue()));
      if (m.IsChangeInt64ToFloat64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeFloat64ToUint32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt32(FastD2UI(m.ResolvedValue()));
      if (m.IsChangeUint32ToFloat64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeInt32ToFloat64: {
      Int32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(FastI2D(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kBitcastWord32ToWord64: {
      Int32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) return ReplaceInt64(m.ResolvedValue());
      // No need to truncate the value, since top 32 bits are not important.
      if (m.IsTruncateInt64ToInt32()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeInt32ToInt64: {
      Int32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) return ReplaceInt64(m.ResolvedValue());
      break;
    }
    case IrOpcode::kChangeInt64ToFloat64: {
      Int64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(static_cast<double>(m.ResolvedValue()));
      if (m.IsChangeFloat64ToInt64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kChangeUint32ToFloat64: {
      Uint32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceFloat64(FastUI2D(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kChangeUint32ToUint64: {
      Uint32Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt64(static_cast<uint64_t>(m.ResolvedValue()));
      break;
    }
    case IrOpcode::kTruncateFloat64ToWord32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue())
        return ReplaceInt32(DoubleToInt32(m.ResolvedValue()));
      if (m.IsChangeInt32ToFloat64()) return Replace(m.node()->InputAt(0));
      return NoChange();
    }
    case IrOpcode::kTruncateInt64ToInt32:
      return ReduceTruncateInt64ToInt32(node);
    case IrOpcode::kTruncateFloat64ToFloat32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) {
        if (signalling_nan_propagation_ == kSilenceSignallingNan && m.IsNaN()) {
          return ReplaceFloat32(DoubleToFloat32(SilenceNaN(m.ResolvedValue())));
        }
        return ReplaceFloat32(DoubleToFloat32(m.ResolvedValue()));
      }
      if (signalling_nan_propagation_ == kPropagateSignallingNan &&
          m.IsChangeFloat32ToFloat64())
        return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kRoundFloat64ToInt32: {
      Float64Matcher m(node->InputAt(0));
      if (m.HasResolvedValue()) {
        return ReplaceInt32(DoubleToInt32(m.ResolvedValue()));
      }
      if (m.IsChangeInt32ToFloat64()) return Replace(m.node()->InputAt(0));
      break;
    }
    case IrOpcode::kFloat64InsertLowWord32:
      return ReduceFloat64InsertLowWord32(node);
    case IrOpcode::kFloat64InsertHighWord32:
      return ReduceFloat64InsertHighWord32(node);
    case IrOpcode::kStore:
    case IrOpcode::kUnalignedStore:
      return ReduceStore(node);
    case IrOpcode::kFloat64Equal:
    case IrOpcode::kFloat64LessThan:
    case IrOpcode::kFloat64LessThanOrEqual:
      return ReduceFloat64Compare(node);
    case IrOpcode::kFloat64RoundDown:
      return ReduceFloat64RoundDown(node);
    case IrOpcode::kBitcastTaggedToWord:
    case IrOpcode::kBitcastTaggedToWordForTagAndSmiBits: {
      NodeMatcher m(node->InputAt(0));
      if (m.IsBitcastWordToTaggedSigned()) {
        RelaxEffectsAndControls(node);
        return Replace(m.InputAt(0));
      }
      break;
    }
    case IrOpcode::kBranch:
    case IrOpcode::kDeoptimizeIf:
    case IrOpcode::kDeoptimizeUnless:
#if V8_ENABLE_WEBASSEMBLY
    case IrOpcode::kTrapIf:
    case IrOpcode::kTrapUnless:
#endif
      return ReduceConditional(node);
    case IrOpcode::kInt64LessThan: {
      Int64BinopMatcher m(node);
      if (m.IsFoldable()) {  // K < K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <
                           m.right().ResolvedValue());
      }
      return ReduceWord64Comparisons(node);
    }
    case IrOpcode::kInt64LessThanOrEqual: {
      Int64BinopMatcher m(node);
      if (m.IsFoldable()) {  // K <= K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <=
                           m.right().ResolvedValue());
      }
      return ReduceWord64Comparisons(node);
    }
    case IrOpcode::kUint64LessThan: {
      Uint64BinopMatcher m(node);
      if (m.IsFoldable()) {  // K < K => K  (K stands for arbitrary constants)
        return ReplaceBool(m.left().ResolvedValue() <
                           m.right().ResolvedValue());
      }
      return ReduceWord64Comparisons(node);
    }
    case IrOpcode::kUint64LessThanOrEqual: {
      return ReduceUintNLessThanOrEqual<Word64Adapter>(node);
    }
    case IrOpcode::kFloat32Select:
    case IrOpcode::kFloat64Select:
    case IrOpcode::kWord32Select:
    case IrOpcode::kWord64Select: {
      Int32Matcher match(node->InputAt(0));
      if (match.HasResolvedValue()) {
        if (match.Is(0)) {
          return Replace(node->InputAt(2));
        } else {
          return Replace(node->InputAt(1));
        }
      }
      break;
    }
    case IrOpcode::kLoad:
    case IrOpcode::kProtectedLoad:
    case IrOpcode::kLoadTrapOnNull: {
      Node* input0 = node->InputAt(0);
      Node* input1 = node->InputAt(1);
      if (input0->opcode() == IrOpcode::kInt64Add) {
        Int64BinopMatcher m(input0);
        if (m.right().HasResolvedValue()) {
          int64_t value = m.right().ResolvedValue();
          node->ReplaceInput(0, m.left().node());
          Node* new_node = Int64Add(input1, Int64Constant(value));
          node->ReplaceInput(1, new_node);
          return Changed(node);
        }
      }
      break;
    }
    default:
      break;
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceTruncateInt64ToInt32(Node* node) {
  Int64Matcher m(node->InputAt(0));
  if (m.HasResolvedValue())
    return ReplaceInt32(static_cast<int32_t>(m.ResolvedValue()));
  if (m.IsChangeInt32ToInt64() || m.IsChangeUint32ToUint64())
    return Replace(m.node()->InputAt(0));
  // TruncateInt64ToInt32(BitcastTaggedToWordForTagAndSmiBits(Load(x))) =>
  // Load(x)
  // where the new Load uses Int32 rather than the tagged representation.
  if (m.IsBitcastTaggedToWordForTagAndSmiBits() && m.node()->UseCount() == 1) {
    Node* input = m.node()->InputAt(0);
    if (input->opcode() == IrOpcode::kLoad ||
        input->opcode() == IrOpcode::kLoadImmutable) {
      LoadRepresentation load_rep = LoadRepresentationOf(input->op());
      if (ElementSizeLog2Of(load_rep.representation()) == 2) {
        // Ensure that the value output of the load is only ever used by the
        // BitcastTaggedToWordForTagAndSmiBits.
        int value_edges = 0;
        for (Edge edge : input->use_edges()) {
          if (NodeProperties::IsValueEdge(edge)) ++value_edges;
        }
        if (value_edges == 1) {
          // Removing the input is required as node is replaced by the Load, but
          // is still used by the the BitcastTaggedToWordForTagAndSmiBits, so
          // will prevent future CanCover calls being true.
          m.node()->RemoveInput(0);
          NodeProperties::ChangeOp(
              input,
              input->opcode() == IrOpcode::kLoad
                  ? machine()->Load(LoadRepresentation::Int32())
                  : machine()->LoadImmutable(LoadRepresentation::Int32()));
          return Replace(input);
        }
      }
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt32Add(Node* node) {
  DCHECK_EQ(IrOpcode::kInt32Add, node->opcode());
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x + 0 => x
  if (m.IsFoldable()) {  // K + K => K  (K stands for arbitrary constants)
    return ReplaceInt32(base::AddWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.left().IsInt32Sub()) {
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.left().Is(0)) {  // (0 - x) + y => y - x
      node->ReplaceInput(0, m.right().node());
      node->ReplaceInput(1, mleft.right().node());
      NodeProperties::ChangeOp(node, machine()->Int32Sub());
      return Changed(node).FollowedBy(ReduceInt32Sub(node));
    }
  }
  if (m.right().IsInt32Sub()) {
    Int32BinopMatcher mright(m.right().node());
    if (mright.left().Is(0)) {  // y + (0 - x) => y - x
      node->ReplaceInput(1, mright.right().node());
      NodeProperties::ChangeOp(node, machine()->Int32Sub());
      return Changed(node).FollowedBy(ReduceInt32Sub(node));
    }
  }
  // (x + Int32Constant(a)) + Int32Constant(b)) => x + Int32Constant(a + b)
  if (m.right().HasResolvedValue() && m.left().IsInt32Add()) {
    Int32BinopMatcher n(m.left().node());
    if (n.right().HasResolvedValue() && m.OwnsInput(m.left().node())) {
      node->ReplaceInput(
          1, Int32Constant(base::AddWithWraparound(m.right().ResolvedValue(),
                                                   n.right().ResolvedValue())));
      node->ReplaceInput(0, n.left().node());
      return Changed(node);
    }
  }

  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt64Add(Node* node) {
  DCHECK_EQ(IrOpcode::kInt64Add, node->opcode());
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x + 0 => 0
  if (m.IsFoldable()) {
    return ReplaceInt64(base::AddWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  // (x + Int64Constant(a)) + Int64Constant(b) => x + Int64Constant(a + b)
  if (m.right().HasResolvedValue() && m.left().IsInt64Add()) {
    Int64BinopMatcher n(m.left().node());
    if (n.right().HasResolvedValue() && m.OwnsInput(m.left().node())) {
      node->ReplaceInput(
          1, Int64Constant(base::AddWithWraparound(m.right().ResolvedValue(),
                                                   n.right().ResolvedValue())));
      node->ReplaceInput(0, n.left().node());
      return Changed(node);
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt32Sub(Node* node) {
  DCHECK_EQ(IrOpcode::kInt32Sub, node->opcode());
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x - 0 => x
  if (m.IsFoldable()) {  // K - K => K  (K stands for arbitrary constants)
    return ReplaceInt32(base::SubWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) return ReplaceInt32(0);  // x - x => 0
  if (m.right().HasResolvedValue()) {               // x - K => x + -K
    node->ReplaceInput(
        1,
        Int32Constant(base::NegateWithWraparound(m.right().ResolvedValue())));
    NodeProperties::ChangeOp(node, machine()->Int32Add());
    return Changed(node).FollowedBy(ReduceInt32Add(node));
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt64Sub(Node* node) {
  DCHECK_EQ(IrOpcode::kInt64Sub, node->opcode());
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x - 0 => x
  if (m.IsFoldable()) {  // K - K => K  (K stands for arbitrary constants)
    return ReplaceInt64(base::SubWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) return Replace(Int64Constant(0));  // x - x => 0
  if (m.right().HasResolvedValue()) {                         // x - K => x + -K
    node->ReplaceInput(
        1,
        Int64Constant(base::NegateWithWraparound(m.right().ResolvedValue())));
    NodeProperties::ChangeOp(node, machine()->Int64Add());
    return Changed(node).FollowedBy(ReduceInt64Add(node));
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt64Mul(Node* node) {
  DCHECK_EQ(IrOpcode::kInt64Mul, node->opcode());
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.right().node());  // x * 0 => 0
  if (m.right().Is(1)) return Replace(m.left().node());   // x * 1 => x
  if (m.IsFoldable()) {  // K * K => K  (K stands for arbitrary constants)
    return ReplaceInt64(base::MulWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.right().Is(-1)) {  // x * -1 => 0 - x
    node->ReplaceInput(0, Int64Constant(0));
    node->ReplaceInput(1, m.left().node());
    NodeProperties::ChangeOp(node, machine()->Int64Sub());
    return Changed(node);
  }
  if (m.right().IsPowerOf2()) {  // x * 2^n => x << n
    node->ReplaceInput(
        1,
        Int64Constant(base::bits::WhichPowerOfTwo(m.right().ResolvedValue())));
    NodeProperties::ChangeOp(node, machine()->Word64Shl());
    return Changed(node).FollowedBy(ReduceWord64Shl(node));
  }
  // (x * Int64Constant(a)) * Int64Constant(b)) => x * Int64Constant(a * b)
  if (m.right().HasResolvedValue() && m.left().IsInt64Mul()) {
    Int64BinopMatcher n(m.left().node());
    if (n.right().HasResolvedValue() && m.OwnsInput(m.left().node())) {
      node->ReplaceInput(
          1, Int64Constant(base::MulWithWraparound(m.right().ResolvedValue(),
                                                   n.right().ResolvedValue())));
      node->ReplaceInput(0, n.left().node());
      return Changed(node);
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt32Div(Node* node) {
  Int32BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 / x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x / 0 => 0
  if (m.right().Is(1)) return Replace(m.left().node());   // x / 1 => x
  if (m.IsFoldable()) {  // K / K => K  (K stands for arbitrary constants)
    return ReplaceInt32(base::bits::SignedDiv32(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) {  // x / x => x != 0
    Node* const zero = Int32Constant(0);
    return Replace(Word32Equal(Word32Equal(m.left().node(), zero), zero));
  }
  if (m.right().Is(-1)) {  // x / -1 => 0 - x
    node->ReplaceInput(0, Int32Constant(0));
    node->ReplaceInput(1, m.left().node());
    node->TrimInputCount(2);
    NodeProperties::ChangeOp(node, machine()->Int32Sub());
    return Changed(node);
  }
  if (m.right().HasResolvedValue()) {
    int32_t const divisor = m.right().ResolvedValue();
    Node* const dividend = m.left().node();
    Node* quotient = dividend;
    if (base::bits::IsPowerOfTwo(Abs(divisor))) {
      uint32_t const shift = base::bits::WhichPowerOfTwo(Abs(divisor));
      DCHECK_NE(0u, shift);
      if (shift > 1) {
        quotient = Word32Sar(quotient, 31);
      }
      quotient = Int32Add(Word32Shr(quotient, 32u - shift), dividend);
      quotient = Word32Sar(quotient, shift);
    } else {
      quotient = Int32Div(quotient, Abs(divisor));
    }
    if (divisor < 0) {
      node->ReplaceInput(0, Int32Constant(0));
      node->ReplaceInput(1, quotient);
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int32Sub());
      return Changed(node);
    }
    return Replace(quotient);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt64Div(Node* node) {
  Int64BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 / x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x / 0 => 0
  if (m.right().Is(1)) return Replace(m.left().node());   // x / 1 => x
  if (m.IsFoldable()) {  // K / K => K  (K stands for arbitrary constants)
    return ReplaceInt64(base::bits::SignedDiv64(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) {  // x / x => x != 0
    Node* const zero = Int64Constant(0);
    // {Word64Equal} can get reduced to a bool/int32, but we need this
    // operation to produce an int64.
    return Replace(ChangeInt32ToInt64(
        Word64Equal(Word64Equal(m.left().node(), zero), zero)));
  }
  if (m.right().Is(-1)) {  // x / -1 => 0 - x
    node->ReplaceInput(0, Int64Constant(0));
    node->ReplaceInput(1, m.left().node());
    node->TrimInputCount(2);
    NodeProperties::ChangeOp(node, machine()->Int64Sub());
    return Changed(node);
  }
  if (m.right().HasResolvedValue()) {
    int64_t const divisor = m.right().ResolvedValue();
    Node* const dividend = m.left().node();
    Node* quotient = dividend;
    if (base::bits::IsPowerOfTwo(Abs(divisor))) {
      uint32_t const shift = base::bits::WhichPowerOfTwo(Abs(divisor));
      DCHECK_NE(0u, shift);
      if (shift > 1) {
        quotient = Word64Sar(quotient, 63);
      }
      quotient = Int64Add(Word64Shr(quotient, 64u - shift), dividend);
      quotient = Word64Sar(quotient, shift);
    } else {
      quotient = Int64Div(quotient, Abs(divisor));
    }
    if (divisor < 0) {
      node->ReplaceInput(0, Int64Constant(0));
      node->ReplaceInput(1, quotient);
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int64Sub());
      return Changed(node);
    }
    return Replace(quotient);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceUint32Div(Node* node) {
  Uint32BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 / x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x / 0 => 0
  if (m.right().Is(1)) return Replace(m.left().node());   // x / 1 => x
  if (m.IsFoldable()) {  // K / K => K  (K stands for arbitrary constants)
    return ReplaceUint32(base::bits::UnsignedDiv32(m.left().ResolvedValue(),
                                                   m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) {  // x / x => x != 0
    Node* const zero = Int32Constant(0);
    return Replace(Word32Equal(Word32Equal(m.left().node(), zero), zero));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint32_t const divisor = m.right().ResolvedValue();
    if (base::bits::IsPowerOfTwo(divisor)) {  // x / 2^n => x >> n
      node->ReplaceInput(1, Uint32Constant(base::bits::WhichPowerOfTwo(
                                m.right().ResolvedValue())));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Word32Shr());
      return Changed(node);
    } else {
      return Replace(Uint32Div(dividend, divisor));
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceUint64Div(Node* node) {
  Uint64BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 / x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x / 0 => 0
  if (m.right().Is(1)) return Replace(m.left().node());   // x / 1 => x
  if (m.IsFoldable()) {  // K / K => K  (K stands for arbitrary constants)
    return ReplaceUint64(base::bits::UnsignedDiv64(m.left().ResolvedValue(),
                                                   m.right().ResolvedValue()));
  }
  if (m.LeftEqualsRight()) {  // x / x => x != 0
    Node* const zero = Int64Constant(0);
    // {Word64Equal} can get reduced to a bool/int32, but we need this
    // operation to produce an int64.
    return Replace(ChangeInt32ToInt64(
        Word64Equal(Word64Equal(m.left().node(), zero), zero)));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint64_t const divisor = m.right().ResolvedValue();
    if (base::bits::IsPowerOfTwo(divisor)) {  // x / 2^n => x >> n
      node->ReplaceInput(1, Uint64Constant(base::bits::WhichPowerOfTwo(
                                m.right().ResolvedValue())));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Word64Shr());
      return Changed(node);
    } else {
      return Replace(Uint64Div(dividend, divisor));
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt32Mod(Node* node) {
  Int32BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 % x  => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x % 0  => 0
  if (m.right().Is(1)) return ReplaceInt32(0);            // x % 1  => 0
  if (m.right().Is(-1)) return ReplaceInt32(0);           // x % -1 => 0
  if (m.LeftEqualsRight()) return ReplaceInt32(0);        // x % x  => 0
  if (m.IsFoldable()) {  // K % K => K  (K stands for arbitrary constants)
    return ReplaceInt32(base::bits::SignedMod32(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint32_t const divisor = Abs(m.right().ResolvedValue());
    if (base::bits::IsPowerOfTwo(divisor)) {
      uint32_t const mask = divisor - 1;
      Node* const zero = Int32Constant(0);
      Diamond d(graph(), common(),
                graph()->NewNode(machine()->Int32LessThan(), dividend, zero),
                BranchHint::kFalse);
      return Replace(
          d.Phi(MachineRepresentation::kWord32,
                Int32Sub(zero, Word32And(Int32Sub(zero, dividend), mask)),
                Word32And(dividend, mask)));
    } else {
      Node* quotient = Int32Div(dividend, divisor);
      DCHECK_EQ(dividend, node->InputAt(0));
      node->ReplaceInput(1, Int32Mul(quotient, Int32Constant(divisor)));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int32Sub());
    }
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceInt64Mod(Node* node) {
  Int64BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 % x  => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x % 0  => 0
  if (m.right().Is(1)) return ReplaceInt64(0);            // x % 1  => 0
  if (m.right().Is(-1)) return ReplaceInt64(0);           // x % -1 => 0
  if (m.LeftEqualsRight()) return ReplaceInt64(0);        // x % x  => 0
  if (m.IsFoldable()) {  // K % K => K  (K stands for arbitrary constants)
    return ReplaceInt64(base::bits::SignedMod64(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint64_t const divisor = Abs(m.right().ResolvedValue());
    if (base::bits::IsPowerOfTwo(divisor)) {
      uint64_t const mask = divisor - 1;
      Node* const zero = Int64Constant(0);
      Diamond d(graph(), common(),
                graph()->NewNode(machine()->Int64LessThan(), dividend, zero),
                BranchHint::kFalse);
      return Replace(
          d.Phi(MachineRepresentation::kWord64,
                Int64Sub(zero, Word64And(Int64Sub(zero, dividend), mask)),
                Word64And(dividend, mask)));
    } else {
      Node* quotient = Int64Div(dividend, divisor);
      DCHECK_EQ(dividend, node->InputAt(0));
      node->ReplaceInput(1, Int64Mul(quotient, Int64Constant(divisor)));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int64Sub());
    }
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceUint32Mod(Node* node) {
  Uint32BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 % x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x % 0 => 0
  if (m.right().Is(1)) return ReplaceUint32(0);           // x % 1 => 0
  if (m.LeftEqualsRight()) return ReplaceUint32(0);       // x % x  => 0
  if (m.IsFoldable()) {  // K % K => K  (K stands for arbitrary constants)
    return ReplaceUint32(base::bits::UnsignedMod32(m.left().ResolvedValue(),
                                                   m.right().ResolvedValue()));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint32_t const divisor = m.right().ResolvedValue();
    if (base::bits::IsPowerOfTwo(divisor)) {  // x % 2^n => x & 2^n-1
      node->ReplaceInput(1, Uint32Constant(m.right().ResolvedValue() - 1));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Word32And());
    } else {
      Node* quotient = Uint32Div(dividend, divisor);
      DCHECK_EQ(dividend, node->InputAt(0));
      node->ReplaceInput(1, Int32Mul(quotient, Uint32Constant(divisor)));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int32Sub());
    }
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceUint64Mod(Node* node) {
  Uint64BinopMatcher m(node);
  if (m.left().Is(0)) return Replace(m.left().node());    // 0 % x => 0
  if (m.right().Is(0)) return Replace(m.right().node());  // x % 0 => 0
  if (m.right().Is(1)) return ReplaceUint64(0);           // x % 1 => 0
  if (m.LeftEqualsRight()) return ReplaceUint64(0);       // x % x  => 0
  if (m.IsFoldable()) {  // K % K => K  (K stands for arbitrary constants)
    return ReplaceUint64(base::bits::UnsignedMod64(m.left().ResolvedValue(),
                                                   m.right().ResolvedValue()));
  }
  if (m.right().HasResolvedValue()) {
    Node* const dividend = m.left().node();
    uint64_t const divisor = m.right().ResolvedValue();
    if (base::bits::IsPowerOfTwo(divisor)) {  // x % 2^n => x & 2^n-1
      node->ReplaceInput(1, Uint64Constant(m.right().ResolvedValue() - 1));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Word64And());
    } else {
      Node* quotient = Uint64Div(dividend, divisor);
      DCHECK_EQ(dividend, node->InputAt(0));
      node->ReplaceInput(1, Int64Mul(quotient, Uint64Constant(divisor)));
      node->TrimInputCount(2);
      NodeProperties::ChangeOp(node, machine()->Int64Sub());
    }
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceStore(Node* node) {
  NodeMatcher nm(node);
  DCHECK(nm.IsStore() || nm.IsUnalignedStore());
  MachineRepresentation rep =
      nm.IsStore() ? StoreRepresentationOf(node->op()).representation()
                   : UnalignedStoreRepresentationOf(node->op());

  const int value_input = 2;
  Node* const value = node->InputAt(value_input);

  switch (value->opcode()) {
    case IrOpcode::kWord32And: {
      Uint32BinopMatcher m(value);
      if (m.right().HasResolvedValue() &&
          ((rep == MachineRepresentation::kWord8 &&
            (m.right().ResolvedValue() & 0xFF) == 0xFF) ||
           (rep == MachineRepresentation::kWord16 &&
            (m.right().ResolvedValue() & 0xFFFF) == 0xFFFF))) {
        node->ReplaceInput(value_input, m.left().node());
        return Changed(node);
      }
      break;
    }
    case IrOpcode::kWord32Sar: {
      Int32BinopMatcher m(value);
      if (m.left().IsWord32Shl() && ((rep == MachineRepresentation::kWord8 &&
                                      m.right().IsInRange(1, 24)) ||
                                     (rep == MachineRepresentation::kWord16 &&
                                      m.right().IsInRange(1, 16)))) {
        Int32BinopMatcher mleft(m.left().node());
        if (mleft.right().Is(m.right().ResolvedValue())) {
          node->ReplaceInput(value_input, mleft.left().node());
          return Changed(node);
        }
      }
      break;
    }
    default:
      break;
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceProjection(size_t index, Node* node) {
  switch (node->opcode()) {
    case IrOpcode::kInt32AddWithOverflow: {
      DCHECK(index == 0 || index == 1);
      Int32BinopMatcher m(node);
      if (m.IsFoldable()) {
        int32_t val;
        bool ovf = base::bits::SignedAddOverflow32(
            m.left().ResolvedValue(), m.right().ResolvedValue(), &val);
        return ReplaceInt32(index == 0 ? val : ovf);
      }
      if (m.right().Is(0)) {
        return Replace(index == 0 ? m.left().node() : m.right().node());
      }
      break;
    }
    case IrOpcode::kInt32SubWithOverflow: {
      DCHECK(index == 0 || index == 1);
      Int32BinopMatcher m(node);
      if (m.IsFoldable()) {
        int32_t val;
        bool ovf = base::bits::SignedSubOverflow32(
            m.left().ResolvedValue(), m.right().ResolvedValue(), &val);
        return ReplaceInt32(index == 0 ? val : ovf);
      }
      if (m.right().Is(0)) {
        return Replace(index == 0 ? m.left().node() : m.right().node());
      }
      break;
    }
    case IrOpcode::kInt32MulWithOverflow: {
      DCHECK(index == 0 || index == 1);
      Int32BinopMatcher m(node);
      if (m.IsFoldable()) {
        int32_t val;
        bool ovf = base::bits::SignedMulOverflow32(
            m.left().ResolvedValue(), m.right().ResolvedValue(), &val);
        return ReplaceInt32(index == 0 ? val : ovf);
      }
      if (m.right().Is(0)) {
        return Replace(m.right().node());
      }
      if (m.right().Is(1)) {
        return index == 0 ? Replace(m.left().node()) : ReplaceInt32(0);
      }
      break;
    }
    default:
      break;
  }
  return NoChange();
}

namespace {

// Returns true if "value << shift >> shift == value". This can be interpreted
// as "left shifting |value| by |shift| doesn't shift away significant bits".
// Or, equivalently, "left shifting |value| by |shift| doesn't have signed
// overflow".
template <typename T>
bool CanRevertLeftShiftWithRightShift(T value, T shift) {
  using unsigned_T = typename std::make_unsigned<T>::type;
  if (shift < 0 || shift >= std::numeric_limits<T>::digits + 1) {
    // This shift would be UB in C++
    return false;
  }
  if (static_cast<T>(static_cast<unsigned_T>(value) << shift) >> shift !=
      static_cast<T>(value)) {
    return false;
  }
  return true;
}

bool CanTruncate(int64_t value) {
  return value >= std::numeric_limits<int32_t>::min() &&
         value <= std::numeric_limits<int32_t>::max();
}

}  // namespace

Reduction MachineOperatorReducer::ReduceWord32Comparisons(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kInt32LessThan ||
         node->opcode() == IrOpcode::kInt32LessThanOrEqual ||
         node->opcode() == IrOpcode::kUint32LessThan ||
         node->opcode() == IrOpcode::kUint32LessThanOrEqual);
  Int32BinopMatcher m(node);
  // (x >> K) < (y >> K) => x < y   if only zeros shifted out
  if (m.left().op() == machine()->Word32SarShiftOutZeros() &&
      m.right().op() == machine()->Word32SarShiftOutZeros()) {
    Int32BinopMatcher mleft(m.left().node());
    Int32BinopMatcher mright(m.right().node());
    if (mleft.right().HasResolvedValue() &&
        mright.right().Is(mleft.right().ResolvedValue())) {
      node->ReplaceInput(0, mleft.left().node());
      node->ReplaceInput(1, mright.left().node());
      return Changed(node);
    }
  }
  // Simplifying (x >> n) <= k into x <= (k << n), with "k << n" being
  // computed here at compile time.
  if (m.right().HasResolvedValue() &&
      m.left().op() == machine()->Word32SarShiftOutZeros() &&
      m.left().node()->UseCount() == 1) {
    uint32_t right = m.right().ResolvedValue();
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue()) {
      auto shift = mleft.right().ResolvedValue();
      if (CanRevertLeftShiftWithRightShift<int32_t>(right, shift)) {
        node->ReplaceInput(0, mleft.left().node());
        node->ReplaceInput(1, Int32Constant(right << shift));
        return Changed(node);
      }
    }
  }
  // Simplifying k <= (x >> n) into (k << n) <= x, with "k << n" being
  // computed here at compile time.
  if (m.left().HasResolvedValue() &&
      m.right().op() == machine()->Word32SarShiftOutZeros() &&
      m.right().node()->UseCount() == 1) {
    uint32_t left = m.left().ResolvedValue();
    Int32BinopMatcher mright(m.right().node());
    if (mright.right().HasResolvedValue()) {
      auto shift = mright.right().ResolvedValue();
      if (CanRevertLeftShiftWithRightShift<int32_t>(left, shift)) {
        node->ReplaceInput(0, Int32Constant(left << shift));
        node->ReplaceInput(1, mright.left().node());
        return Changed(node);
      }
    }
  }
  return NoChange();
}

const Operator* MachineOperatorReducer::Map64To32Comparison(
    const Operator* op, bool sign_extended) {
  switch (op->opcode()) {
    case IrOpcode::kInt64LessThan:
      return sign_extended ? machine()->Int32LessThan()
                           : machine()->Uint32LessThan();
    case IrOpcode::kInt64LessThanOrEqual:
      return sign_extended ? machine()->Int32LessThanOrEqual()
                           : machine()->Uint32LessThanOrEqual();
    case IrOpcode::kUint64LessThan:
      return machine()->Uint32LessThan();
    case IrOpcode::kUint64LessThanOrEqual:
      return machine()->Uint32LessThanOrEqual();
    default:
      UNREACHABLE();
  }
}

Reduction MachineOperatorReducer::ReduceWord64Comparisons(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kInt64LessThan ||
         node->opcode() == IrOpcode::kInt64LessThanOrEqual ||
         node->opcode() == IrOpcode::kUint64LessThan ||
         node->opcode() == IrOpcode::kUint64LessThanOrEqual);
  Int64BinopMatcher m(node);

  bool sign_extended =
      m.left().IsChangeInt32ToInt64() && m.right().IsChangeInt32ToInt64();
  if (sign_extended || (m.left().IsChangeUint32ToUint64() &&
                        m.right().IsChangeUint32ToUint64())) {
    node->ReplaceInput(0, NodeProperties::GetValueInput(m.left().node(), 0));
    node->ReplaceInput(1, NodeProperties::GetValueInput(m.right().node(), 0));
    NodeProperties::ChangeOp(node,
                             Map64To32Comparison(node->op(), sign_extended));
    return Changed(node).FollowedBy(Reduce(node));
  }

  // (x >> K) < (y >> K) => x < y   if only zeros shifted out
  // This is useful for Smi untagging, which results in such a shift.
  if (m.left().op() == machine()->Word64SarShiftOutZeros() &&
      m.right().op() == machine()->Word64SarShiftOutZeros()) {
    Int64BinopMatcher mleft(m.left().node());
    Int64BinopMatcher mright(m.right().node());
    if (mleft.right().HasResolvedValue() &&
        mright.right().Is(mleft.right().ResolvedValue())) {
      node->ReplaceInput(0, mleft.left().node());
      node->ReplaceInput(1, mright.left().node());
      return Changed(node);
    }
  }

  // Simplifying (x >> n) <= k into x <= (k << n), with "k << n" being
  // computed here at compile time.
  if (m.right().HasResolvedValue() &&
      m.left().op() == machine()->Word64SarShiftOutZeros() &&
      m.left().node()->UseCount() == 1) {
    Int64BinopMatcher mleft(m.left().node());
    uint64_t right = m.right().ResolvedValue();
    if (mleft.right().HasResolvedValue()) {
      auto shift = mleft.right().ResolvedValue();
      if (CanRevertLeftShiftWithRightShift<int64_t>(right, shift)) {
        sign_extended = mleft.left().IsChangeInt32ToInt64();
        uint64_t value = right << shift;
        // Reducing to 32-bit comparison when possible.
        if ((sign_extended || mleft.left().IsChangeUint32ToUint64()) &&
            CanTruncate(static_cast<int64_t>(value))) {
          NodeProperties::ChangeOp(
              node, Map64To32Comparison(node->op(), sign_extended));
          node->ReplaceInput(0, mleft.left().node()->InputAt(0));
          node->ReplaceInput(1, Int32Constant(static_cast<int32_t>(value)));
          return Changed(node).FollowedBy(Reduce(node));
        }
        node->ReplaceInput(0, mleft.left().node());
        node->ReplaceInput(1, Int64Constant(value));
        return Changed(node);
      }
    }
  }

  // Simplifying k <= (x >> n) into (k << n) <= x, with "k << n" being
  // computed here at compile time.
  if (m.left().HasResolvedValue() &&
      m.right().op() == machine()->Word64SarShiftOutZeros() &&
      m.right().node()->UseCount() == 1) {
    uint64_t left = m.left().ResolvedValue();
    Int64BinopMatcher mright(m.right().node());
    if (mright.right().HasResolvedValue()) {
      auto shift = mright.right().ResolvedValue();
      if (CanRevertLeftShiftWithRightShift<int64_t>(left, shift)) {
        sign_extended = mright.left().IsChangeInt32ToInt64();
        uint64_t value = left << shift;
        // Reducing to 32-bit comparison when possible.
        if ((sign_extended || mright.left().IsChangeUint32ToUint64()) &&
            CanTruncate(static_cast<int64_t>(value))) {
          NodeProperties::ChangeOp(
              node, Map64To32Comparison(node->op(), sign_extended));
          node->ReplaceInput(0, Int32Constant(static_cast<int32_t>(value)));
          node->ReplaceInput(1, mright.left().node()->InputAt(0));
          return Changed(node).FollowedBy(Reduce(node));
        }
        node->ReplaceInput(0, Int64Constant(value));
        node->ReplaceInput(1, mright.left().node());
        return Changed(node);
      }
    }
  }

  /*
    If Int64Constant(c) can be casted from an Int32Constant:
    -------------------------------------------------
    Int64LessThan(Int32ToInt64(a), Int64Constant(c))
    ====>
    Int32LessThan(a,Int32Constant(c))
    -------------------------------------------------
  */
  if (node->opcode() == IrOpcode::kInt64LessThan ||
      node->opcode() == IrOpcode::kInt64LessThanOrEqual) {
    // Int64LessThan(Int32ToInt64(a), Int64Constant(c))
    if (m.left().IsChangeInt32ToInt64() && m.right().HasResolvedValue()) {
      int64_t right_value = static_cast<int64_t>(m.right().ResolvedValue());
      // Int64Constant can be casted from an Int32Constant
      if (right_value == static_cast<int32_t>(right_value)) {
        const Operator* new_op;

        if (node->opcode() == IrOpcode::kInt64LessThan) {
          new_op = machine()->Int32LessThan();
        } else {
          new_op = machine()->Int32LessThanOrEqual();
        }
        NodeProperties::ChangeOp(node, new_op);
        node->ReplaceInput(0, m.left().InputAt(0));
        node->ReplaceInput(1, Int32Constant(static_cast<int32_t>(right_value)));
        return Changed(node);
      } else if (right_value < std::numeric_limits<int32_t>::min()) {
        // left > right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(0));
        return Changed(node);
      } else if (right_value > std::numeric_limits<int32_t>::max()) {
        // left < right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(1));
        return Changed(node);
      }
    }
    // Int64LessThan(Int64Constant(c), Int32ToInt64(a))
    if (m.right().IsChangeInt32ToInt64() && m.left().HasResolvedValue()) {
      int64_t left_value = static_cast<int64_t>(m.left().ResolvedValue());
      // Int64Constant can be casted from an Int32Constant
      if (left_value == static_cast<int32_t>(left_value)) {
        const Operator* new_op;

        if (node->opcode() == IrOpcode::kInt64LessThan) {
          new_op = machine()->Int32LessThan();
        } else {
          new_op = machine()->Int32LessThanOrEqual();
        }
        NodeProperties::ChangeOp(node, new_op);
        node->ReplaceInput(1, m.right().InputAt(0));
        node->ReplaceInput(0, Int32Constant(static_cast<int32_t>(left_value)));
        return Changed(node);
      } else if (left_value < std::numeric_limits<int32_t>::min()) {
        // left < right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(1));
        return Changed(node);
      } else if (left_value > std::numeric_limits<int32_t>::max()) {
        // left > right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(0));
        return Changed(node);
      }
    }
  }

  /*
    If Uint64Constant(c) can be casted from an Uint32Constant:
    -------------------------------------------------
    Uint64LessThan(Uint32ToInt64(a), Uint64Constant(c))
    ====>
    Uint32LessThan(a,Uint32Constant(c))
    -------------------------------------------------
  */
  if (node->opcode() == IrOpcode::kUint64LessThan ||
      node->opcode() == IrOpcode::kUint64LessThanOrEqual) {
    // Uint64LessThan(Uint32ToInt64(a), Uint32Constant(c))
    if (m.left().IsChangeUint32ToUint64() && m.right().HasResolvedValue()) {
      uint64_t right_value = static_cast<uint64_t>(m.right().ResolvedValue());
      // Uint64Constant can be casted from an Uint32Constant
      if (right_value == static_cast<uint32_t>(right_value)) {
        const Operator* new_op;

        if (node->opcode() == IrOpcode::kUint64LessThan) {
          new_op = machine()->Uint32LessThan();
        } else {
          new_op = machine()->Uint32LessThanOrEqual();
        }
        NodeProperties::ChangeOp(node, new_op);
        node->ReplaceInput(0, m.left().InputAt(0));
        node->ReplaceInput(1,
                           Uint32Constant(static_cast<uint32_t>(right_value)));
        return Changed(node);
      } else {
        // left < right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(1));
        return Changed(node);
      }
    }
    // Uint64LessThan(Uint64Constant(c), Uint32ToInt64(a))
    if (m.right().IsChangeUint32ToUint64() && m.left().HasResolvedValue()) {
      uint64_t left_value = static_cast<uint64_t>(m.left().ResolvedValue());
      // Uint64Constant can be casted from an Uint32Constant
      if (left_value == static_cast<uint32_t>(left_value)) {
        const Operator* new_op;
        if (node->opcode() == IrOpcode::kUint64LessThan) {
          new_op = machine()->Uint32LessThan();
        } else {
          new_op = machine()->Uint32LessThanOrEqual();
        }
        NodeProperties::ChangeOp(node, new_op);
        node->ReplaceInput(1, m.right().InputAt(0));
        node->ReplaceInput(0,
                           Uint32Constant(static_cast<uint32_t>(left_value)));
        return Changed(node);
      } else {
        // left > right always
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(0));
        return Changed(node);
      }
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord32Shifts(Node* node) {
  DCHECK((node->opcode() == IrOpcode::kWord32Shl) ||
         (node->opcode() == IrOpcode::kWord32Shr) ||
         (node->opcode() == IrOpcode::kWord32Sar));
  if (machine()->Word32ShiftIsSafe()) {
    // Remove the explicit 'and' with 0x1F if the shift provided by the machine
    // instruction matches that required by JavaScript.
    Int32BinopMatcher m(node);
    if (m.right().IsWord32And()) {
      Int32BinopMatcher mright(m.right().node());
      if (mright.right().Is(0x1F)) {
        node->ReplaceInput(1, mright.left().node());
        return Changed(node);
      }
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord32Shl(Node* node) {
  DCHECK_EQ(IrOpcode::kWord32Shl, node->opcode());
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x << 0 => x
  if (m.IsFoldable()) {  // K << K => K  (K stands for arbitrary constants)
    return ReplaceInt32(base::ShlWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.right().IsInRange(1, 31)) {
    if (m.left().IsWord32Sar() || m.left().IsWord32Shr()) {
      Int32BinopMatcher mleft(m.left().node());

      // If x >> K only shifted out zeros:
      // (x >> K) << L => x           if K == L
      // (x >> K) << L => x >> (K-L) if K > L
      // (x >> K) << L => x << (L-K)  if K < L
      // Since this is used for Smi untagging, we currently only need it for
      // signed shifts.
      if (mleft.op() == machine()->Word32SarShiftOutZeros() &&
          mleft.right().IsInRange(1, 31)) {
        Node* x = mleft.left().node();
        int k = mleft.right().ResolvedValue();
        int l = m.right().ResolvedValue();
        if (k == l) {
          return Replace(x);
        } else if (k > l) {
          node->ReplaceInput(0, x);
          node->ReplaceInput(1, Uint32Constant(k - l));
          NodeProperties::ChangeOp(node, machine()->Word32Sar());
          return Changed(node).FollowedBy(ReduceWord32Sar(node));
        } else {
          DCHECK(k < l);
          node->ReplaceInput(0, x);
          node->ReplaceInput(1, Uint32Constant(l - k));
          return Changed(node);
        }
      }

      // (x >>> K) << K => x & ~(2^K - 1)
      // (x >> K) << K => x & ~(2^K - 1)
      if (mleft.right().Is(m.right().ResolvedValue())) {
        node->ReplaceInput(0, mleft.left().node());
        node->ReplaceInput(1,
                           Uint32Constant(std::numeric_limits<uint32_t>::max()
                                          << m.right().ResolvedValue()));
        NodeProperties::ChangeOp(node, machine()->Word32And());
        return Changed(node).FollowedBy(ReduceWord32And(node));
      }
    }
  }
  return ReduceWord32Shifts(node);
}

Reduction MachineOperatorReducer::ReduceWord64Shl(Node* node) {
  DCHECK_EQ(IrOpcode::kWord64Shl, node->opcode());
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x << 0 => x
  if (m.IsFoldable()) {  // K << K => K  (K stands for arbitrary constants)
    return ReplaceInt64(base::ShlWithWraparound(m.left().ResolvedValue(),
                                                m.right().ResolvedValue()));
  }
  if (m.right().IsInRange(1, 63) &&
      (m.left().IsWord64Sar() || m.left().IsWord64Shr())) {
    Int64BinopMatcher mleft(m.left().node());

    // If x >> K only shifted out zeros:
    // (x >> K) << L => x           if K == L
    // (x >> K) << L => x >> (K-L) if K > L
    // (x >> K) << L => x << (L-K)  if K < L
    // Since this is used for Smi untagging, we currently only need it for
    // signed shifts.
    if (mleft.op() == machine()->Word64SarShiftOutZeros() &&
        mleft.right().IsInRange(1, 63)) {
      Node* x = mleft.left().node();
      int64_t k = mleft.right().ResolvedValue();
      int64_t l = m.right().ResolvedValue();
      if (k == l) {
        return Replace(x);
      } else if (k > l) {
        node->ReplaceInput(0, x);
        node->ReplaceInput(1, Uint64Constant(k - l));
        NodeProperties::ChangeOp(node, machine()->Word64Sar());
        return Changed(node).FollowedBy(ReduceWord64Sar(node));
      } else {
        DCHECK(k < l);
        node->ReplaceInput(0, x);
        node->ReplaceInput(1, Uint64Constant(l - k));
        return Changed(node);
      }
    }

    // (x >>> K) << K => x & ~(2^K - 1)
    // (x >> K) << K => x & ~(2^K - 1)
    if (mleft.right().Is(m.right().ResolvedValue())) {
      node->ReplaceInput(0, mleft.left().node());
      node->ReplaceInput(1, Uint64Constant(std::numeric_limits<uint64_t>::max()
                                           << m.right().ResolvedValue()));
      NodeProperties::ChangeOp(node, machine()->Word64And());
      return Changed(node).FollowedBy(ReduceWord64And(node));
    }
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord32Shr(Node* node) {
  Uint32BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x >>> 0 => x
  if (m.IsFoldable()) {  // K >>> K => K  (K stands for arbitrary constants)
    return ReplaceInt32(m.left().ResolvedValue() >>
                        (m.right().ResolvedValue() & 31));
  }
  if (m.left().IsWord32And() && m.right().HasResolvedValue()) {
    Uint32BinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue()) {
      uint32_t shift = m.right().ResolvedValue() & 31;
      uint32_t mask = mleft.right().ResolvedValue();
      if ((mask >> shift) == 0) {
        // (m >>> s) == 0 implies ((x & m) >>> s) == 0
        return ReplaceInt32(0);
      }
    }
  }
  return ReduceWord32Shifts(node);
}

Reduction MachineOperatorReducer::ReduceWord64Shr(Node* node) {
  DCHECK_EQ(IrOpcode::kWord64Shr, node->opcode());
  Uint64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x >>> 0 => x
  if (m.IsFoldable()) {  // K >> K => K  (K stands for arbitrary constants)
    return ReplaceInt64(m.left().ResolvedValue() >>
                        (m.right().ResolvedValue() & 63));
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord32Sar(Node* node) {
  Int32BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x >> 0 => x
  if (m.IsFoldable()) {  // K >> K => K  (K stands for arbitrary constants)
    return ReplaceInt32(m.left().ResolvedValue() >>
                        (m.right().ResolvedValue() & 31));
  }
  if (m.left().IsWord32Shl()) {
    Int32BinopMatcher mleft(m.left().node());
    if (mleft.left().IsComparison()) {
      if (m.right().Is(31) && mleft.right().Is(31)) {
        // Comparison << 31 >> 31 => 0 - Comparison
        node->ReplaceInput(0, Int32Constant(0));
        node->ReplaceInput(1, mleft.left().node());
        NodeProperties::ChangeOp(node, machine()->Int32Sub());
        return Changed(node).FollowedBy(ReduceInt32Sub(node));
      }
    } else if (mleft.left().IsLoad()) {
      LoadRepresentation const rep =
          LoadRepresentationOf(mleft.left().node()->op());
      if (m.right().Is(24) && mleft.right().Is(24) &&
          rep == MachineType::Int8()) {
        // Load[kMachInt8] << 24 >> 24 => Load[kMachInt8]
        return Replace(mleft.left().node());
      }
      if (m.right().Is(16) && mleft.right().Is(16) &&
          rep == MachineType::Int16()) {
        // Load[kMachInt16] << 16 >> 16 => Load[kMachInt8]
        return Replace(mleft.left().node());
      }
    }
  }
  return ReduceWord32Shifts(node);
}

Reduction MachineOperatorReducer::ReduceWord64Sar(Node* node) {
  Int64BinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x >> 0 => x
  if (m.IsFoldable()) {
    return ReplaceInt64(m.left().ResolvedValue() >>
                        (m.right().ResolvedValue() & 63));
  }
  return NoChange();
}

template <typename WordNAdapter>
Reduction MachineOperatorReducer::ReduceWordNAnd(Node* node) {
  using A = WordNAdapter;
  A a(this);

  typename A::IntNBinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.right().node());  // x & 0  => 0
  if (m.right().Is(-1)) return Replace(m.left().node());  // x & -1 => x
  if (m.right().Is(1)) {
    // (x + x) & 1 => 0
    Node* left = m.left().node();
    while (left->opcode() == IrOpcode::kTruncateInt64ToInt32 ||
           left->opcode() == IrOpcode::kChangeInt32ToInt64 ||
           left->opcode() == IrOpcode::kChangeUint32ToUint64) {
      left = left->InputAt(0);
    }
    if ((left->opcode() == IrOpcode::kInt32Add ||
         left->opcode() == IrOpcode::kInt64Add) &&
        left->InputAt(0) == left->InputAt(1)) {
      return a.ReplaceIntN(0);
    }
  }
  if (m.left().IsComparison() && m.right().Is(1)) {  // CMP & 1 => CMP
    return Replace(m.left().node());
  }
  if (m.IsFoldable()) {  // K & K  => K  (K stands for arbitrary constants)
    return a.ReplaceIntN(m.left().ResolvedValue() & m.right().ResolvedValue());
  }
  if (m.LeftEqualsRight()) return Replace(m.left().node());  // x & x => x
  if (A::IsWordNAnd(m.left()) && m.right().HasResolvedValue()) {
    typename A::IntNBinopMatcher mleft(m.left().node());
    if (mleft.right().HasResolvedValue()) {  // (x & K) & K => x & K
      node->ReplaceInput(0, mleft.left().node());
      node->ReplaceInput(1, a.IntNConstant(m.right().ResolvedValue() &
                                           mleft.right().ResolvedValue()));
      return Changed(node).FollowedBy(a.ReduceWordNAnd(node));
    }
  }
  if (m.right().IsNegativePowerOf2()) {
    typename A::intN_t const mask = m.right().ResolvedValue();
    typename A::intN_t const neg_mask = base::NegateWithWraparound(mask);
    if (A::IsWordNShl(m.left())) {
      typename A::UintNBinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue() &&
          (mleft.right().ResolvedValue() & (A::WORD_SIZE - 1)) >=
              base::bits::CountTrailingZeros(mask)) {
        // (x << L) & (-1 << K) => x << L iff L >= K
        return Replace(mleft.node());
      }
    } else if (A::IsIntNAdd(m.left())) {
      typename A::IntNBinopMatcher mleft(m.left().node());
      if (mleft.right().HasResolvedValue() &&
          (mleft.right().ResolvedValue() & mask) ==
              mleft.right().ResolvedValue()) {
        // (x + (K << L)) & (-1 << L) => (x & (-1 << L)) + (K << L)
        node->ReplaceInput(0,
                           a.WordNAnd(mleft.left().node(), m.right().node()));
        node->ReplaceInput(1, mleft.right().node());
        NodeProperties::ChangeOp(node, a.IntNAdd(machine()));
        return Changed(node).FollowedBy(a.ReduceIntNAdd(node));
      }
      if (A::IsIntNMul(mleft.left())) {
        typename A::IntNBinopMatcher mleftleft(mleft.left().node());
        if (mleftleft.right().IsMultipleOf(neg_mask)) {
          // (y * (K << L) + x) & (-1 << L) => (x & (-1 << L)) + y * (K << L)
          node->ReplaceInput(
              0, a.WordNAnd(mleft.right().node(), m.right().node()));
          node->ReplaceInput(1, mleftleft.node());
          NodeProperties::ChangeOp(node, a.IntNAdd(machine()));
          return Changed(node).FollowedBy(a.ReduceIntNAdd(node));
        }
      }
      if (A::IsIntNMul(mleft.right())) {
        typename A::IntNBinopMatcher mleftright(mleft.right().node());
        if (mleftright.right().IsMultipleOf(neg_mask)) {
          // (x + y * (K << L)) & (-1 << L) => (x & (-1 << L)) + y * (K << L)
          node->ReplaceInput(0,
                             a.WordNAnd(mleft.left().node(), m.right().node()));
          node->ReplaceInput(1, mleftright.node());
          NodeProperties::ChangeOp(node, a.IntNAdd(machine()));
          return Changed(node).FollowedBy(a.ReduceIntNAdd(node));
        }
      }
      if (A::IsWordNShl(mleft.left())) {
        typename A::IntNBinopMatcher mleftleft(mleft.left().node());
        if (mleftleft.right().Is(base::bits::CountTrailingZeros(mask))) {
          // (y << L + x) & (-1 << L) => (x & (-1 << L)) + y << L
          node->ReplaceInput(
              0, a.WordNAnd(mleft.right().node(), m.right().node()));
          node->ReplaceInput(1, mleftleft.node());
          NodeProperties::ChangeOp(node, a.IntNAdd(machine()));
          return Changed(node).FollowedBy(a.ReduceIntNAdd(node));
        }
      }
      if (A::IsWordNShl(mleft.right())) {
        typename A::IntNBinopMatcher mleftright(mleft.right().node());
        if (mleftright.right().Is(base::bits::CountTrailingZeros(mask))) {
          // (x + y << L) & (-1 << L) => (x & (-1 << L)) + y << L
          node->ReplaceInput(0,
                             a.WordNAnd(mleft.left().node(), m.right().node()));
          node->ReplaceInput(1, mleftright.node());
          NodeProperties::ChangeOp(node, a.IntNAdd(machine()));
          return Changed(node).FollowedBy(a.ReduceIntNAdd(node));
        }
      }
    } else if (A::IsIntNMul(m.left())) {
      typename A::IntNBinopMatcher mleft(m.left().node());
      if (mleft.right().IsMultipleOf(neg_mask)) {
        // (x * (K << L)) & (-1 << L) => x * (K << L)
        return Replace(mleft.node());
      }
    }
  }
  return NoChange();
}

template <typename WordNAdapter>
Reduction MachineOperatorReducer::ReduceUintNLessThanOrEqual(Node* node) {
  using A = WordNAdapter;
  A a(this);

  typename A::UintNBinopMatcher m(node);
  typename A::uintN_t kMaxUIntN =
      std::numeric_limits<typename A::uintN_t>::max();
  if (m.left().Is(0)) return ReplaceBool(true);           // 0 <= x  =>  true
  if (m.right().Is(kMaxUIntN)) return ReplaceBool(true);  // x <= M  =>  true
  if (m.IsFoldable()) {  // K <= K  =>  K  (K stands for arbitrary constants)
    return ReplaceBool(m.left().ResolvedValue() <= m.right().ResolvedValue());
  }
  if (m.LeftEqualsRight()) return ReplaceBool(true);  // x <= x  =>  true
  if (m.right().Is(0)) {                              // x <= 0  =>  x == 0
    NodeProperties::ChangeOp(node, a.WordNEqual(machine()));
    return Changed(node);
  }
  return a.ReduceWordNComparisons(node);
}

namespace {

// Represents an operation of the form `(source & mask) == masked_value`.
// where each bit set in masked_value also has to be set in mask.
struct BitfieldCheck {
  Node* const source;
  uint32_t const mask;
  uint32_t const masked_value;
  bool const truncate_from_64_bit;

  BitfieldCheck(Node* source, uint32_t mask, uint32_t masked_value,
                bool truncate_from_64_bit)
      : source(source),
        mask(mask),
        masked_value(masked_value),
        truncate_from_64_bit(truncate_from_64_bit) {
    CHECK_EQ(masked_value & ~mask, 0);
  }

  static base::Optional<BitfieldCheck> Detect(Node* node) {
    // There are two patterns to check for here:
    // 1. Single-bit checks: `(val >> shift) & 1`, where:
    //    - the shift may be omitted, and/or
    //    - the result may be truncated from 64 to 32
    // 2. Equality checks: `(val & mask) == expected`, where:
    //    - val may be truncated from 64 to 32 before masking (see
    //      ReduceWordEqualForConstantRhs)
    if (node->opcode() == IrOpcode::kWord32Equal) {
      Uint32BinopMatcher eq(node);
      if (eq.left().IsWord32And()) {
        Uint32BinopMatcher mand(eq.left().node());
        if (mand.right().HasResolvedValue() && eq.right().HasResolvedValue()) {
          uint32_t mask = mand.right().ResolvedValue();
          uint32_t masked_value = eq.right().ResolvedValue();
          if ((masked_value & ~mask) != 0) return {};
          if (mand.left().IsTruncateInt64ToInt32()) {
            return BitfieldCheck(
                NodeProperties::GetValueInput(mand.left().node(), 0), mask,
                masked_value, true);
          } else {
            return BitfieldCheck(mand.left().node(), mask, masked_value, false);
          }
        }
      }
    } else {
      if (node->opcode() == IrOpcode::kTruncateInt64ToInt32) {
        return TryDetectShiftAndMaskOneBit<Word64Adapter>(
            NodeProperties::GetValueInput(node, 0));
      } else {
        return TryDetectShiftAndMaskOneBit<Word32Adapter>(node);
      }
    }
    return {};
  }

  base::Optional<BitfieldCheck> TryCombine(const BitfieldCheck& other) {
    if (source != other.source ||
        truncate_from_64_bit != other.truncate_from_64_bit) {
      return {};
    }
    uint32_t overlapping_bits = mask & other.mask;
    // It would be kind of strange to have any overlapping bits, but they can be
    // allowed as long as they don't require opposite values in the same
    // positions.
    if ((masked_value & overlapping_bits) !=
        (other.masked_value & overlapping_bits)) {
      return {};
    }
    return BitfieldCheck{source, mask | other.mask,
                         masked_value | other.masked_value,
                         truncate_from_64_bit};
  }

 private:
  template <typename WordNAdapter>
  static base::Optional<BitfieldCheck> TryDetectShiftAndMaskOneBit(Node* node) {
    // Look for the pattern `(val >> shift) & 1`. The shift may be omitted.
    if (WordNAdapter::IsWordNAnd(NodeMatcher(node))) {
      typename WordNAdapter::IntNBinopMatcher mand(node);
      if (mand.right().HasResolvedValue() &&
          mand.right().ResolvedValue() == 1) {
        if (WordNAdapter::IsWordNShr(mand.left()) ||
            WordNAdapter::IsWordNSar(mand.left())) {
          typename WordNAdapter::UintNBinopMatcher shift(mand.left().node());
          if (shift.right().HasResolvedValue() &&
              shift.right().ResolvedValue() < 32u) {
            uint32_t mask = 1 << shift.right().ResolvedValue();
            return BitfieldCheck{shift.left().node(), mask, mask,
                                 WordNAdapter::WORD_SIZE == 64};
          }
        }
        return BitfieldCheck{mand.left().node(), 1, 1,
                             WordNAdapter::WORD_SIZE == 64};
      }
    }
    return {};
  }
};

}  // namespace

Reduction MachineOperatorReducer::ReduceWord32And(Node* node) {
  DCHECK_EQ(IrOpcode::kWord32And, node->opcode());
  Reduction reduction = ReduceWordNAnd<Word32Adapter>(node);
  if (reduction.Changed()) {
    return reduction;
  }

  // Attempt to detect multiple bitfield checks from the same bitfield struct
  // and fold them into a single check.
  Int32BinopMatcher m(node);
  if (auto right_bitfield = BitfieldCheck::Detect(m.right().node())) {
    if (auto left_bitfield = BitfieldCheck::Detect(m.left().node())) {
      if (auto combined_bitfield = left_bitfield->TryCombine(*right_bitfield)) {
        Node* source = combined_bitfield->source;
        if (combined_bitfield->truncate_from_64_bit) {
          source = TruncateInt64ToInt32(source);
        }
        node->ReplaceInput(0, Word32And(source, combined_bitfield->mask));
        node->ReplaceInput(1, Int32Constant(combined_bitfield->masked_value));
        NodeProperties::ChangeOp(node, machine()->Word32Equal());
        return Changed(node).FollowedBy(ReduceWord32Equal(node));
      }
    }
  }

  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord64And(Node* node) {
  DCHECK_EQ(IrOpcode::kWord64And, node->opcode());
  return ReduceWordNAnd<Word64Adapter>(node);
}

Reduction MachineOperatorReducer::TryMatchWord32Ror(Node* node) {
  // Recognize rotation, we are matching and transforming as follows:
  //   x << y         |  x >>> (32 - y)    =>  x ror (32 - y)
  //   x << (32 - y)  |  x >>> y           =>  x ror y
  //   x << y         ^  x >>> (32 - y)    =>  x ror (32 - y)   if y & 31 != 0
  //   x << (32 - y)  ^  x >>> y           =>  x ror y          if y & 31 != 0
  // (As well as the commuted forms.)
  // Note the side condition for XOR: the optimization doesn't hold for
  // multiples of 32.

  DCHECK(IrOpcode::kWord32Or == node->opcode() ||
         IrOpcode::kWord32Xor == node->opcode());
  Int32BinopMatcher m(node);
  Node* shl = nullptr;
  Node* shr = nullptr;
  if (m.left().IsWord32Shl() && m.right().IsWord32Shr()) {
    shl = m.left().node();
    shr = m.right().node();
  } else if (m.left().IsWord32Shr() && m.right().IsWord32Shl()) {
    shl = m.right().node();
    shr = m.left().node();
  } else {
    return NoChange();
  }

  Int32BinopMatcher mshl(shl);
  Int32BinopMatcher mshr(shr);
  if (mshl.left().node() != mshr.left().node()) return NoChange();

  if (mshl.right().HasResolvedValue() && mshr.right().HasResolvedValue()) {
    // Case where y is a constant.
    if (mshl.right().ResolvedValue() + mshr.right().ResolvedValue() != 32) {
      return NoChange();
    }
    if (node->opcode() == IrOpcode::kWord32Xor &&
        (mshl.right().ResolvedValue() & 31) == 0) {
      return NoChange();
    }
  } else {
    Node* sub = nullptr;
    Node* y = nullptr;
    if (mshl.right().IsInt32Sub()) {
      sub = mshl.right().node();
      y = mshr.right().node();
    } else if (mshr.right().IsInt32Sub()) {
      sub = mshr.right().node();
      y = mshl.right().node();
    } else {
      return NoChange();
    }

    Int32BinopMatcher msub(sub);
    if (!msub.left().Is(32) || msub.right().node() != y) return NoChange();
    if (node->opcode() == IrOpcode::kWord32Xor) {
      return NoChange();  // Can't guarantee y & 31 != 0.
    }
  }

  node->ReplaceInput(0, mshl.left().node());
  node->ReplaceInput(1, mshr.right().node());
  NodeProperties::ChangeOp(node, machine()->Word32Ror());
  return Changed(node);
}

template <typename WordNAdapter>
Reduction MachineOperatorReducer::ReduceWordNOr(Node* node) {
  using A = WordNAdapter;
  A a(this);

  typename A::IntNBinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());    // x | 0  => x
  if (m.right().Is(-1)) return Replace(m.right().node());  // x | -1 => -1
  if (m.IsFoldable()) {  // K | K  => K  (K stands for arbitrary constants)
    return a.ReplaceIntN(m.left().ResolvedValue() | m.right().ResolvedValue());
  }
  if (m.LeftEqualsRight()) return Replace(m.left().node());  // x | x => x

  // (x & K1) | K2 => x | K2 if K2 has ones for every zero bit in K1.
  // This case can be constructed by UpdateWord and UpdateWord32 in CSA.
  if (m.right().HasResolvedValue()) {
    if (A::IsWordNAnd(m.left())) {
      typename A::IntNBinopMatcher mand(m.left().node());
      if (mand.right().HasResolvedValue()) {
        if ((m.right().ResolvedValue() | mand.right().ResolvedValue()) == -1) {
          node->ReplaceInput(0, mand.left().node());
          return Changed(node);
        }
      }
    }
  }

  return a.TryMatchWordNRor(node);
}

Reduction MachineOperatorReducer::ReduceWord32Or(Node* node) {
  DCHECK_EQ(IrOpcode::kWord32Or, node->opcode());
  return ReduceWordNOr<Word32Adapter>(node);
}

Reduction MachineOperatorReducer::ReduceWord64Or(Node* node) {
  DCHECK_EQ(IrOpcode::kWord64Or, node->opcode());
  return ReduceWordNOr<Word64Adapter>(node);
}

template <typename WordNAdapter>
Reduction MachineOperatorReducer::ReduceWordNXor(Node* node) {
  using A = WordNAdapter;
  A a(this);

  typename A::IntNBinopMatcher m(node);
  if (m.right().Is(0)) return Replace(m.left().node());  // x ^ 0 => x
  if (m.IsFoldable()) {  // K ^ K => K  (K stands for arbitrary constants)
    return a.ReplaceIntN(m.left().ResolvedValue() ^ m.right().ResolvedValue());
  }
  if (m.LeftEqualsRight()) return Replace(a.IntNConstant(0));  // x ^ x => 0
  if (A::IsWordNXor(m.left()) && m.right().Is(-1)) {
    typename A::IntNBinopMatcher mleft(m.left().node());
    if (mleft.right().Is(-1)) {  // (x ^ -1) ^ -1 => x
      return Replace(mleft.left().node());
    }
  }

  return a.TryMatchWordNRor(node);
}

Reduction MachineOperatorReducer::ReduceWord32Xor(Node* node) {
  DCHECK_EQ(IrOpcode::kWord32Xor, node->opcode());
  Int32BinopMatcher m(node);
  if (m.right().IsWord32Equal() && m.left().Is(1)) {
    return Replace(Word32Equal(m.right().node(), Int32Constant(0)));
  }
  return ReduceWordNXor<Word32Adapter>(node);
}

Reduction MachineOperatorReducer::ReduceWord64Xor(Node* node) {
  DCHECK_EQ(IrOpcode::kWord64Xor, node->opcode());
  return ReduceWordNXor<Word64Adapter>(node);
}

Reduction MachineOperatorReducer::ReduceWord32Equal(Node* node) {
  Int32BinopMatcher m(node);
  if (m.IsFoldable()) {  // K == K => K  (K stands for arbitrary constants)
    return ReplaceBool(m.left().ResolvedValue() == m.right().ResolvedValue());
  }
  if (m.left().IsInt32Sub() && m.right().Is(0)) {  // x - y == 0 => x == y
    Int32BinopMatcher msub(m.left().node());
    node->ReplaceInput(0, msub.left().node());
    node->ReplaceInput(1, msub.right().node());
    return Changed(node);
  }
  // TODO(turbofan): fold HeapConstant, ExternalReference, pointer compares
  if (m.LeftEqualsRight()) return ReplaceBool(true);  // x == x => true
  if (m.right().HasResolvedValue()) {
    base::Optional<std::pair<Node*, uint32_t>> replacements;
    if (m.left().IsTruncateInt64ToInt32()) {
      replacements = ReduceWordEqualForConstantRhs<Word64Adapter, uint32_t>(
          NodeProperties::GetValueInput(m.left().node(), 0),
          static_cast<uint32_t>(m.right().ResolvedValue()));
    } else {
      replacements = ReduceWordEqualForConstantRhs<Word32Adapter, uint32_t>(
          m.left().node(), static_cast<uint32_t>(m.right().ResolvedValue()));
    }
    if (replacements) {
      node->ReplaceInput(0, replacements->first);
      node->ReplaceInput(1, Uint32Constant(replacements->second));
      return Changed(node);
    }

    // Simplifying (x+k1)==k2 into x==k2-k1.
    if (m.left().IsInt32Add() && m.right().IsInt32Constant()) {
      Int32AddMatcher m_add(m.left().node());
      if (m_add.right().IsInt32Constant()) {
        int32_t lte_right = m.right().ResolvedValue();
        int32_t add_right = m_add.right().ResolvedValue();
        // No need to consider overflow in this condition (==).
        node->ReplaceInput(0, m_add.left().node());
        node->ReplaceInput(1, Int32Constant(static_cast<uint32_t>(lte_right) -
                                            static_cast<uint32_t>(add_right)));
        return Changed(node);
      }
    }
  }

  return NoChange();
}

Reduction MachineOperatorReducer::ReduceWord64Equal(Node* node) {
  Int64BinopMatcher m(node);
  if (m.IsFoldable()) {  // K == K => K  (K stands for arbitrary constants)
    return ReplaceBool(m.left().ResolvedValue() == m.right().ResolvedValue());
  }
  if (m.left().IsInt64Sub() && m.right().Is(0)) {  // x - y == 0 => x == y
    Int64BinopMatcher msub(m.left().node());
    node->ReplaceInput(0, msub.left().node());
    node->ReplaceInput(1, msub.right().node());
    return Changed(node);
  }
  // TODO(turbofan): fold HeapConstant, ExternalReference, pointer compares
  if (m.LeftEqualsRight()) return ReplaceBool(true);  // x == x => true
  if (m.right().HasResolvedValue()) {
    base::Optional<std::pair<Node*, uint64_t>> replacements =
        ReduceWordEqualForConstantRhs<Word64Adapter, uint64_t>(
            m.left().node(), static_cast<uint64_t>(m.right().ResolvedValue()));
    if (replacements) {
      node->ReplaceInput(0, replacements->first);
      node->ReplaceInput(1, Uint64Constant(replacements->second));
      return Changed(node);
    }

    // Simplifying (x+k1)==k2 into x==k2-k1.
    if (m.left().IsInt64Add() && m.right().IsInt64Constant()) {
      Int64AddMatcher m_add(m.left().node());
      if (m_add.right().IsInt64Constant()) {
        int64_t lte_right = m.right().ResolvedValue();
        int64_t add_right = m_add.right().ResolvedValue();
        // No need to consider overflow in this condition (==).
        node->ReplaceInput(0, m_add.left().node());
        node->ReplaceInput(1, Int64Constant(static_cast<uint64_t>(lte_right) -
                                            static_cast<uint64_t>(add_right)));
        return Changed(node);
      }
    }

    /*
      If Int64Constant(c) can be casted from an Int32Constant:
      -------------------------------------------------
      Word64Equal(Int32ToInt64(a), Int64Constant(c))
      ====>
      Word32Equal(a,Int32Constant(c))
      -------------------------------------------------
    */
    if (m.left().IsChangeInt32ToInt64()) {
      int64_t right_value = m.right().ResolvedValue();
      // Int64Constant can be casted from an Int32Constant
      if (right_value == static_cast<int32_t>(right_value)) {
        NodeProperties::ChangeOp(node, machine()->Word32Equal());
        node->ReplaceInput(0, m.left().InputAt(0));
        node->ReplaceInput(1, Int32Constant(static_cast<int32_t>(right_value)));
        return Changed(node);
      } else {
        // Always false, change node op to zero(false).
        node->TrimInputCount(0);
        NodeProperties::ChangeOp(node, common()->Int32Constant(0));
        return Changed(node);
      }
    }
  }

  return NoChange();
}

Reduction MachineOperatorReducer::ReduceFloat64InsertLowWord32(Node* node) {
  DCHECK_EQ(IrOpcode::kFloat64InsertLowWord32, node->opcode());
  Float64Matcher mlhs(node->InputAt(0));
  Uint32Matcher mrhs(node->InputAt(1));
  if (mlhs.HasResolvedValue() && mrhs.HasResolvedValue()) {
    return ReplaceFloat64(
        base::bit_cast<double>((base::bit_cast<uint64_t>(mlhs.ResolvedValue()) &
                                uint64_t{0xFFFFFFFF00000000}) |
                               mrhs.ResolvedValue()));
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceFloat64InsertHighWord32(Node* node) {
  DCHECK_EQ(IrOpcode::kFloat64InsertHighWord32, node->opcode());
  Float64Matcher mlhs(node->InputAt(0));
  Uint32Matcher mrhs(node->InputAt(1));
  if (mlhs.HasResolvedValue() && mrhs.HasResolvedValue()) {
    return ReplaceFloat64(base::bit_cast<double>(
        (base::bit_cast<uint64_t>(mlhs.ResolvedValue()) &
         uint64_t{0xFFFFFFFF}) |
        (static_cast<uint64_t>(mrhs.ResolvedValue()) << 32)));
  }
  return NoChange();
}

namespace {

bool IsFloat64RepresentableAsFloat32(const Float64Matcher& m) {
  if (m.HasResolvedValue()) {
    double v = m.ResolvedValue();
    return DoubleToFloat32(v) == v;
  }
  return false;
}

}  // namespace

Reduction MachineOperatorReducer::ReduceFloat64Compare(Node* node) {
  DCHECK(IrOpcode::kFloat64Equal == node->opcode() ||
         IrOpcode::kFloat64LessThan == node->opcode() ||
         IrOpcode::kFloat64LessThanOrEqual == node->opcode());
  Float64BinopMatcher m(node);
  if (m.IsFoldable()) {
    switch (node->opcode()) {
      case IrOpcode::kFloat64Equal:
        return ReplaceBool(m.left().ResolvedValue() ==
                           m.right().ResolvedValue());
      case IrOpcode::kFloat64LessThan:
        return ReplaceBool(m.left().ResolvedValue() <
                           m.right().ResolvedValue());
      case IrOpcode::kFloat64LessThanOrEqual:
        return ReplaceBool(m.left().ResolvedValue() <=
                           m.right().ResolvedValue());
      default:
        UNREACHABLE();
    }
  } else if ((m.left().IsChangeFloat32ToFloat64() &&
              m.right().IsChangeFloat32ToFloat64()) ||
             (m.left().IsChangeFloat32ToFloat64() &&
              IsFloat64RepresentableAsFloat32(m.right())) ||
             (IsFloat64RepresentableAsFloat32(m.left()) &&
              m.right().IsChangeFloat32ToFloat64())) {
    // As all Float32 values have an exact representation in Float64, comparing
    // two Float64 values both converted from Float32 is equivalent to comparing
    // the original Float32s, so we can ignore the conversions. We can also
    // reduce comparisons of converted Float64 values against constants that
    // can be represented exactly as Float32.
    switch (node->opcode()) {
      case IrOpcode::kFloat64Equal:
        NodeProperties::ChangeOp(node, machine()->Float32Equal());
        break;
      case IrOpcode::kFloat64LessThan:
        NodeProperties::ChangeOp(node, machine()->Float32LessThan());
        break;
      case IrOpcode::kFloat64LessThanOrEqual:
        NodeProperties::ChangeOp(node, machine()->Float32LessThanOrEqual());
        break;
      default:
        UNREACHABLE();
    }
    node->ReplaceInput(
        0, m.left().HasResolvedValue()
               ? Float32Constant(static_cast<float>(m.left().ResolvedValue()))
               : m.left().InputAt(0));
    node->ReplaceInput(
        1, m.right().HasResolvedValue()
               ? Float32Constant(static_cast<float>(m.right().ResolvedValue()))
               : m.right().InputAt(0));
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceFloat64RoundDown(Node* node) {
  DCHECK_EQ(IrOpcode::kFloat64RoundDown, node->opcode());
  Float64Matcher m(node->InputAt(0));
  if (m.HasResolvedValue()) {
    return ReplaceFloat64(std::floor(m.ResolvedValue()));
  }
  return NoChange();
}

namespace {

// Returns true if |node| is a constant whose value is 0.
bool IsZero(Node* node) {
  switch (node->opcode()) {
#define CASE_IS_ZERO(opcode, matcher) \
  case IrOpcode::opcode: {            \
    matcher m(node);                  \
    return m.Is(0);                   \
  }
    CASE_IS_ZERO(kInt32Constant, Int32Matcher)
    CASE_IS_ZERO(kInt64Constant, Int64Matcher)
#undef CASE_IS_ZERO
    default:
      break;
  }
  return false;
}

// If |node| is of the form "x == 0", then return "x" (in order to remove the
// "== 0" part).
base::Optional<Node*> TryGetInvertedCondition(Node* cond) {
  if (cond->opcode() == IrOpcode::kWord32Equal) {
    Int32BinopMatcher m(cond);
    if (IsZero(m.right().node())) {
      return m.left().node();
    }
  }
  return base::nullopt;
}

struct SimplifiedCondition {
  Node* condition;
  bool is_inverted;
};

// Tries to simplifies |cond| by removing all top-level "== 0". Everytime such a
// construction is removed, the meaning of the comparison is inverted. This is
// recorded by the variable |is_inverted| throughout this function, and returned
// at the end. If |is_inverted| is true at the end, the caller should invert the
// if/else branches following the comparison.
base::Optional<SimplifiedCondition> TrySimplifyCompareZero(Node* cond) {
  bool is_inverted = false;
  bool changed = false;
  base::Optional<Node*> new_cond;
  while ((new_cond = TryGetInvertedCondition(cond)).has_value()) {
    cond = *new_cond;
    is_inverted = !is_inverted;
    changed = true;
  }
  if (changed) {
    return SimplifiedCondition{cond, is_inverted};
  } else {
    return {};
  }
}

/*
Remove WordEqual after WordAnd if it aims to test a bit.
For Example:
------------------------
691:  Int32Constant[8]
1857: Word32And(1838,691)
1858: Word32Equal(1857,691)
1859: Branch(1858,2141)
======>
691:  Int32Constant[8]
1857: Word32And(1838,691)
1859: Branch(1857,2141)
------------------------

Assembly code:
------------------------
andl r9,0x8
cmpb r9l,0x8
jz 0x7f242017bf3c
======>
testb r9,0x8
jnz 0x7f56c017be2e
------------------------
*/
Node* TrySimplifyCompareForTestBit(Node* cond) {
  if (cond->opcode() != IrOpcode::kWord32Equal) {
    return nullptr;
  }
  Node* word_equal_left = cond->InputAt(0);
  Node* word_equal_right = cond->InputAt(1);

  if (word_equal_left->opcode() != IrOpcode::kWord32And ||
      word_equal_right->opcode() != IrOpcode::kInt32Constant) {
    return nullptr;
  }

  Node* word_and_right = word_equal_left->InputAt(1);
  if (word_and_right->opcode() != IrOpcode::kInt32Constant) {
    return nullptr;
  }
  int32_t a = OpParameter<int32_t>(word_and_right->op());
  int32_t b = OpParameter<int32_t>(word_equal_right->op());
  if (a != b || !base::bits::IsPowerOfTwo(a)) {
    return nullptr;
  }
  DCHECK_EQ(word_equal_left->opcode(), IrOpcode::kWord32And);
  return word_equal_left;
}

}  // namespace

void MachineOperatorReducer::SwapBranches(Node* node) {
  DCHECK_EQ(node->opcode(), IrOpcode::kBranch);
  for (Node* const use : node->uses()) {
    switch (use->opcode()) {
      case IrOpcode::kIfTrue:
        NodeProperties::ChangeOp(use, common()->IfFalse());
        break;
      case IrOpcode::kIfFalse:
        NodeProperties::ChangeOp(use, common()->IfTrue());
        break;
      default:
        UNREACHABLE();
    }
  }
  NodeProperties::ChangeOp(
      node, common()->Branch(NegateBranchHint(BranchHintOf(node->op()))));
}

// If |node| is a branch, removes all top-level 32-bit "== 0" from |node|.
Reduction MachineOperatorReducer::SimplifyBranch(Node* node) {
  Node* cond = node->InputAt(0);
  if (auto simplified = TrySimplifyCompareZero(cond)) {
    node->ReplaceInput(0, simplified->condition);
    if (simplified->is_inverted) {
      switch (node->opcode()) {
        case IrOpcode::kBranch:
          SwapBranches(node);
          break;
#if V8_ENABLE_WEBASSEMBLY
        case IrOpcode::kTrapIf: {
          const bool has_frame_state = node->op()->ValueInputCount() > 1;
          NodeProperties::ChangeOp(
              node,
              common()->TrapUnless(TrapIdOf(node->op()), has_frame_state));
          break;
        }
        case IrOpcode::kTrapUnless: {
          const bool has_frame_state = node->op()->ValueInputCount() > 1;
          NodeProperties::ChangeOp(
              node, common()->TrapIf(TrapIdOf(node->op()), has_frame_state));
          break;
        }
#endif  // V8_ENABLE_WEBASSEMBLY
        case IrOpcode::kDeoptimizeIf: {
          DeoptimizeParameters p = DeoptimizeParametersOf(node->op());
          NodeProperties::ChangeOp(
              node, common()->DeoptimizeUnless(p.reason(), p.feedback()));
          break;
        }
        case IrOpcode::kDeoptimizeUnless: {
          DeoptimizeParameters p = DeoptimizeParametersOf(node->op());
          NodeProperties::ChangeOp(
              node, common()->DeoptimizeIf(p.reason(), p.feedback()));
          break;
        }
        default:

          UNREACHABLE();
      }
    }
    return Changed(node);
  } else if (auto new_cond = TrySimplifyCompareForTestBit(cond)) {
    node->ReplaceInput(0, new_cond);
    return Changed(node);
  }
  return NoChange();
}

Reduction MachineOperatorReducer::ReduceConditional(Node* node) {
  DCHECK(node->opcode() == IrOpcode::kBranch ||
         node->opcode() == IrOpcode::kDeoptimizeIf ||
         node->opcode() == IrOpcode::kDeoptimizeUnless ||
         node->opcode() == IrOpcode::kTrapIf ||
         node->opcode() == IrOpcode::kTrapUnless);
  // This reducer only applies operator reductions to the branch condition.
  // Reductions involving control flow happen elsewhere. Non-zero inputs are
  // considered true in all conditional ops.
  NodeMatcher condition(NodeProperties::GetValueInput(node, 0));
  Reduction reduction = NoChange();
  if (condition.IsTruncateInt64ToInt32()) {
    if (auto replacement =
            ReduceConditionalN<Word64Adapter>(condition.node())) {
      NodeProperties::ReplaceValueInput(node, *replacement, 0);
      reduction = Changed(node);
    }
  } else if (auto replacement = ReduceConditionalN<Word32Adapter>(node)) {
    NodeProperties::ReplaceValueInput(node, *replacement, 0);
    reduction = Changed(node);
  }
  return reduction.FollowedBy(SimplifyBranch(node));
}

template <typename WordNAdapter>
base::Optional<Node*> MachineOperatorReducer::ReduceConditionalN(Node* node) {
  NodeMatcher condition(NodeProperties::GetValueInput(node, 0));
  // Branch conditions are 32-bit comparisons against zero, so they are the
  // opposite of a 32-bit `x == 0` node. To avoid repetition, we can reuse logic
  // for Word32Equal: if `x == 0` can reduce to `y == 0`, then branch(x) can
  // reduce to branch(y).
  auto replacements = ReduceWordEqualForConstantRhs<WordNAdapter, uint32_t>(
      condition.node(), 0);
  if (replacements && replacements->second == 0) return replacements->first;
  return {};
}

template <typename WordNAdapter, typename uintN_t, typename intN_t>
base::Optional<std::pair<Node*, uintN_t>>
MachineOperatorReducer::ReduceWordEqualForConstantRhs(Node* lhs, uintN_t rhs) {
  if (WordNAdapter::IsWordNAnd(NodeMatcher(lhs))) {
    typename WordNAdapter::UintNBinopMatcher mand(lhs);
    if ((WordNAdapter::IsWordNShr(mand.left()) ||
         WordNAdapter::IsWordNSar(mand.left())) &&
        mand.right().HasResolvedValue()) {
      typename WordNAdapter::UintNBinopMatcher mshift(mand.left().node());
      // ((x >> K1) & K2) == K3 => (x & (K2 << K1)) == (K3 << K1)
      if (mshift.right().HasResolvedValue()) {
        auto shift_bits = mshift.right().ResolvedValue();
        auto mask = mand.right().ResolvedValue();
        // Make sure that we won't shift data off the end, and that all of the
        // data ends up in the lower 32 bits for 64-bit mode.
        if (shift_bits <= base::bits::CountLeadingZeros(mask) &&
            shift_bits <= base::bits::CountLeadingZeros(rhs) &&
            (std::is_same_v<uintN_t, uint64_t> ||
             mask << shift_bits <= std::numeric_limits<uintN_t>::max())) {
          Node* new_input = mshift.left().node();
          uintN_t new_mask = static_cast<uintN_t>(mask << shift_bits);
          uintN_t new_rhs = rhs << shift_bits;
          if (std::is_same_v<uintN_t, uint32_t> &&
              WordNAdapter::WORD_SIZE == 64) {
            // We can truncate before performing the And.
            new_input = TruncateInt64ToInt32(new_input);
            return std::make_pair(Word32And(new_input, new_mask), new_rhs);
          } else {
            WordNAdapter a(this);
            return std::make_pair(
                a.WordNAnd(new_input, a.UintNConstant(new_mask)), new_rhs);
          }
        }
      }
    }
  }
  // Replaces (x >> n) == k with x == k << n, with "k << n" being computed
  // here at compile time.
  if (std::is_same_v<intN_t, typename WordNAdapter::intN_t> &&
      WordNAdapter::IsWordNSarShiftOutZeros(lhs->op()) &&
      lhs->UseCount() == 1) {
    typename WordNAdapter::UintNBinopMatcher mshift(lhs);
    if (mshift.right().HasResolvedValue()) {
      intN_t shift = static_cast<intN_t>(mshift.right().ResolvedValue());
      if (CanRevertLeftShiftWithRightShift<intN_t>(rhs, shift)) {
        return std::make_pair(mshift.left().node(), rhs << shift);
      }
    }
  }
  return {};
}

CommonOperatorBuilder* MachineOperatorReducer::common() const {
  return mcgraph()->common();
}

MachineOperatorBuilder* MachineOperatorReducer::machine() const {
  return mcgraph()->machine();
}

Graph* MachineOperatorReducer::graph() const { return mcgraph()->graph(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
