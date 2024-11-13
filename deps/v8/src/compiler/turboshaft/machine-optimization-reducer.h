// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_
#define V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>

#include "include/v8-internal.h"
#include "src/base/bits.h"
#include "src/base/division-by-constant.h"
#include "src/base/functional.h"
#include "src/base/ieee754.h"
#include "src/base/logging.h"
#include "src/base/macros.h"
#include "src/base/overflowing-math.h"
#include "src/base/small-vector.h"
#include "src/base/template-utils.h"
#include "src/base/vector.h"
#include "src/builtins/builtins.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/backend/instruction.h"
#include "src/compiler/compilation-dependencies.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator-reducer.h"
#include "src/compiler/turboshaft/assembler.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/opmasks.h"
#include "src/compiler/turboshaft/phase.h"
#include "src/compiler/turboshaft/reducer-traits.h"
#include "src/compiler/turboshaft/representations.h"
#include "src/handles/handles.h"
#include "src/numbers/conversions.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/simd-shuffle.h"
#endif

namespace v8::internal::compiler::turboshaft {

// ******************************** OVERVIEW ********************************
//
// The MachineOptimizationAssembler performs basic optimizations on low-level
// operations that can be performed on-the-fly, without requiring type analysis
// or analyzing uses. It largely corresponds to MachineOperatorReducer in
// sea-of-nodes Turbofan.
//
// These peephole optimizations are typically very local: they based on the
// immediate inputs of an operation, we try to constant-fold or strength-reduce
// the operation.
//
// Typical examples include:
//
//   * Reducing `a == a` to `1`
//
//   * Reducing `a + 0` to `a`
//
//   * Reducing `a * 2^k` to `a << k`
//

#include "src/compiler/turboshaft/define-assembler-macros.inc"

template <typename>
class VariableReducer;
template <typename>
class GraphVisitor;

namespace {

// Represents an operation of the form `(source & mask) == masked_value`.
// where each bit set in masked_value also has to be set in mask.
struct BitfieldCheck {
  OpIndex const source;
  uint32_t const mask;
  uint32_t const masked_value;
  bool const truncate_from_64_bit;

  BitfieldCheck(OpIndex source, uint32_t mask, uint32_t masked_value,
                bool truncate_from_64_bit)
      : source(source),
        mask(mask),
        masked_value(masked_value),
        truncate_from_64_bit(truncate_from_64_bit) {
    CHECK_EQ(masked_value & ~mask, 0);
  }

  static std::optional<BitfieldCheck> Detect(const OperationMatcher& matcher,
                                             const Graph& graph,
                                             OpIndex index) {
    // There are two patterns to check for here:
    // 1. Single-bit checks: `(val >> shift) & 1`, where:
    //    - the shift may be omitted, and/or
    //    - the result may be truncated from 64 to 32
    // 2. Equality checks: `(val & mask) == expected`, where:
    //    - val may be truncated from 64 to 32 before masking (see
    //      ReduceWordEqualForConstantRhs)
    const Operation& op = graph.Get(index);
    if (const ComparisonOp* equal = op.TryCast<Opmask::kWord32Equal>()) {
      if (const WordBinopOp* left_and =
              graph.Get(equal->left()).TryCast<Opmask::kWord32BitwiseAnd>()) {
        uint32_t mask;
        uint32_t masked_value;
        if (matcher.MatchIntegralWord32Constant(left_and->right(), &mask) &&
            matcher.MatchIntegralWord32Constant(equal->right(),
                                                &masked_value)) {
          if ((masked_value & ~mask) != 0) return std::nullopt;
          if (const ChangeOp* truncate =
                  graph.Get(left_and->left())
                      .TryCast<Opmask::kTruncateWord64ToWord32>()) {
            return BitfieldCheck{truncate->input(), mask, masked_value, true};
          } else {
            return BitfieldCheck{left_and->left(), mask, masked_value, false};
          }
        }
      }
    } else if (const ChangeOp* truncate =
                   op.TryCast<Opmask::kTruncateWord64ToWord32>()) {
      return TryDetectShiftAndMaskOneBit<Word64>(matcher, truncate->input());
    } else {
      return TryDetectShiftAndMaskOneBit<Word32>(matcher, index);
    }
    return std::nullopt;
  }

  std::optional<BitfieldCheck> TryCombine(const BitfieldCheck& other) {
    if (source != other.source ||
        truncate_from_64_bit != other.truncate_from_64_bit) {
      return std::nullopt;
    }
    uint32_t overlapping_bits = mask & other.mask;
    // It would be kind of strange to have any overlapping bits, but they can be
    // allowed as long as they don't require opposite values in the same
    // positions.
    if ((masked_value & overlapping_bits) !=
        (other.masked_value & overlapping_bits)) {
      return std::nullopt;
    }
    return BitfieldCheck{source, mask | other.mask,
                         masked_value | other.masked_value,
                         truncate_from_64_bit};
  }

 private:
  template <typename WordType>
  static std::optional<BitfieldCheck> TryDetectShiftAndMaskOneBit(
      const OperationMatcher& matcher, OpIndex index) {
    constexpr WordRepresentation Rep = V<WordType>::rep;
    // Look for the pattern `(val >> shift) & 1`. The shift may be omitted.
    V<WordType> value;
    uint64_t constant;
    if (matcher.MatchBitwiseAndWithConstant(index, &value, &constant, Rep) &&
        constant == 1) {
      OpIndex input;
      if (int shift_amount;
          matcher.MatchConstantRightShift(value, &input, Rep, &shift_amount) &&
          shift_amount >= 0 && shift_amount < 32) {
        uint32_t mask = 1 << shift_amount;
        return BitfieldCheck{input, mask, mask,
                             Rep == WordRepresentation::Word64()};
      }
      return BitfieldCheck{value, 1, 1, Rep == WordRepresentation::Word64()};
    }
    return std::nullopt;
  }
};

}  // namespace


template <class Next>
class MachineOptimizationReducer : public Next {
 public:
  TURBOSHAFT_REDUCER_BOILERPLATE(MachineOptimization)
#if defined(__clang__)
  // TODO(dmercadier): this static_assert ensures that the stack contains a
  // VariableReducer. It is currently not very clean, because when GraphVisitor
  // is on the stack, it implicitly adds a VariableReducer that isn't detected
  // by reducer_list_contains. It would be cleaner to have a single "reducer
  // list contains VariableReducer" check that sees the VariableReducer
  // introduced by GraphVisitor.
  static_assert(reducer_list_contains<ReducerList, VariableReducer>::value ||
                reducer_list_contains<ReducerList, GraphVisitor>::value);
#endif

  // TODO(mslekova): Implement ReduceSelect and ReducePhi,
  // by reducing `(f > 0) ? f : -f` to `fabs(f)`.

  OpIndex REDUCE(Change)(OpIndex input, ChangeOp::Kind kind,
                         ChangeOp::Assumption assumption,
                         RegisterRepresentation from,
                         RegisterRepresentation to) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceChange(input, kind, assumption, from, to);
    }
    using Kind = ChangeOp::Kind;
    if (from == WordRepresentation::Word32()) {
      input = TryRemoveWord32ToWord64Conversion(input);
    }
    if (uint64_t value;
        from.IsWord() && matcher.MatchIntegralWordConstant(
                             input, WordRepresentation(from), &value)) {
      using Rep = RegisterRepresentation;
      switch (multi(kind, from, to)) {
        case multi(Kind::kSignExtend, Rep::Word32(), Rep::Word64()):
          return __ Word64Constant(int64_t{static_cast<int32_t>(value)});
        case multi(Kind::kZeroExtend, Rep::Word32(), Rep::Word64()):
        case multi(Kind::kBitcast, Rep::Word32(), Rep::Word64()):
          return __ Word64Constant(uint64_t{static_cast<uint32_t>(value)});
        case multi(Kind::kBitcast, Rep::Word32(), Rep::Float32()):
          return __ Float32Constant(
              i::Float32::FromBits(static_cast<uint32_t>(value)));
        case multi(Kind::kBitcast, Rep::Word64(), Rep::Float64()):
          return __ Float64Constant(i::Float64::FromBits(value));
        case multi(Kind::kSignedToFloat, Rep::Word32(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<int32_t>(value)));
        case multi(Kind::kSignedToFloat, Rep::Word64(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<int64_t>(value)));
        case multi(Kind::kUnsignedToFloat, Rep::Word32(), Rep::Float64()):
          return __ Float64Constant(
              static_cast<double>(static_cast<uint32_t>(value)));
        case multi(Kind::kTruncate, Rep::Word64(), Rep::Word32()):
          return __ Word32Constant(static_cast<uint32_t>(value));
        default:
          break;
      }
    }
    if (i::Float32 value; from == RegisterRepresentation::Float32() &&
                          matcher.MatchFloat32Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float64()) {
        return __ Float64Constant(value.get_scalar());
      }
      if (kind == Kind::kBitcast && to == WordRepresentation::Word32()) {
        return __ Word32Constant(value.get_bits());
      }
    }
    if (i::Float64 value; from == RegisterRepresentation::Float64() &&
                          matcher.MatchFloat64Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float32()) {
        return __ Float32Constant(DoubleToFloat32_NoInline(value.get_scalar()));
      }
      if (kind == Kind::kBitcast && to == WordRepresentation::Word64()) {
        return __ Word64Constant(base::bit_cast<uint64_t>(value));
      }
      if (kind == Kind::kSignedFloatTruncateOverflowToMin) {
        double truncated = std::trunc(value.get_scalar());
        if (to == WordRepresentation::Word64()) {
          int64_t result = std::numeric_limits<int64_t>::min();
          if (truncated >= std::numeric_limits<int64_t>::min() &&
              truncated <= kMaxDoubleRepresentableInt64) {
            result = static_cast<int64_t>(truncated);
          }
          return __ Word64Constant(result);
        }
        if (to == WordRepresentation::Word32()) {
          int32_t result = std::numeric_limits<int32_t>::min();
          if (truncated >= std::numeric_limits<int32_t>::min() &&
              truncated <= std::numeric_limits<int32_t>::max()) {
            result = static_cast<int32_t>(truncated);
          }
          return __ Word32Constant(result);
        }
      }
      if (kind == Kind::kJSFloatTruncate &&
          to == WordRepresentation::Word32()) {
        return __ Word32Constant(DoubleToInt32_NoInline(value.get_scalar()));
      }
      if (kind == Kind::kExtractHighHalf) {
        DCHECK_EQ(to, RegisterRepresentation::Word32());
        return __ Word32Constant(static_cast<uint32_t>(value.get_bits() >> 32));
      }
      if (kind == Kind::kExtractLowHalf) {
        DCHECK_EQ(to, RegisterRepresentation::Word32());
        return __ Word32Constant(static_cast<uint32_t>(value.get_bits()));
      }
    }
    if (float value; from == RegisterRepresentation::Float32() &&
                     matcher.MatchFloat32Constant(input, &value)) {
      if (kind == Kind::kFloatConversion &&
          to == RegisterRepresentation::Float64()) {
        return __ Float64Constant(value);
      }
    }

    const Operation& input_op = matcher.Get(input);
    if (const ChangeOp* change_op = input_op.TryCast<ChangeOp>()) {
      if (change_op->from == to && change_op->to == from &&
          change_op->IsReversibleBy(kind, signalling_nan_possible)) {
        return change_op->input();
      }
    }
    return Next::ReduceChange(input, kind, assumption, from, to);
  }

  V<Float64> REDUCE(BitcastWord32PairToFloat64)(V<Word32> hi_word32,
                                                V<Word32> lo_word32) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceBitcastWord32PairToFloat64(hi_word32, lo_word32);
    }
    uint32_t lo, hi;
    if (matcher.MatchIntegralWord32Constant(hi_word32, &hi) &&
        matcher.MatchIntegralWord32Constant(lo_word32, &lo)) {
      return __ Float64Constant(
          base::bit_cast<double>(uint64_t{hi} << 32 | uint64_t{lo}));
    }
    return Next::ReduceBitcastWord32PairToFloat64(hi_word32, lo_word32);
  }

  OpIndex REDUCE(TaggedBitcast)(OpIndex input, RegisterRepresentation from,
                                RegisterRepresentation to,
                                TaggedBitcastOp::Kind kind) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceTaggedBitcast(input, from, to, kind);
    }
    // A Tagged -> Untagged -> Tagged sequence can be short-cut.
    // An Untagged -> Tagged -> Untagged sequence however cannot be removed,
    // because the GC might have modified the pointer.
    if (auto* input_bitcast = matcher.TryCast<TaggedBitcastOp>(input)) {
      if (all_of(input_bitcast->to, from) ==
              RegisterRepresentation::WordPtr() &&
          all_of(input_bitcast->from, to) == RegisterRepresentation::Tagged()) {
        return input_bitcast->input();
      }
    }
    // An Untagged -> Smi -> Untagged sequence can be short-cut.
    if (auto* input_bitcast = matcher.TryCast<TaggedBitcastOp>(input);
        input_bitcast && to.IsWord() &&
        (kind == TaggedBitcastOp::Kind::kSmi ||
         input_bitcast->kind == TaggedBitcastOp::Kind::kSmi)) {
      if (input_bitcast->from == to) return input_bitcast->input();
      if (input_bitcast->from == RegisterRepresentation::Word32()) {
        DCHECK_EQ(to, RegisterRepresentation::Word64());
        return __ BitcastWord32ToWord64(input_bitcast->input());
      }
      DCHECK(input_bitcast->from == RegisterRepresentation::Word64() &&
             to == RegisterRepresentation::Word32());
      return __ TruncateWord64ToWord32(input_bitcast->input());
    }
    // Try to constant-fold TaggedBitcast from Word Constant to Word.
    if (to.IsWord()) {
      if (const ConstantOp* cst = matcher.TryCast<ConstantOp>(input)) {
        if (cst->kind == ConstantOp::Kind::kWord32 ||
            cst->kind == ConstantOp::Kind::kWord64) {
          if (to == RegisterRepresentation::Word64()) {
            return __ Word64Constant(cst->integral());
          } else {
            DCHECK_EQ(to, RegisterRepresentation::Word32());
            return __ Word32Constant(static_cast<uint32_t>(cst->integral()));
          }
        }
      }
    }
    if (const ConstantOp* cst = matcher.TryCast<ConstantOp>(input)) {
      // Try to constant-fold Word constant -> Tagged (Smi).
      if (cst->IsIntegral() && to == RegisterRepresentation::Tagged()) {
        if (Smi::IsValid(cst->integral())) {
          return __ SmiConstant(static_cast<intptr_t>(cst->integral()));
        }
      }
      // Try to constant-fold Smi -> Untagged.
      if (cst->kind == ConstantOp::Kind::kSmi) {
        if (to == RegisterRepresentation::Word32()) {
          return __ Word32Constant(static_cast<uint32_t>(cst->smi().ptr()));
        } else if (to == RegisterRepresentation::Word64()) {
          return __ Word64Constant(static_cast<uint64_t>(cst->smi().ptr()));
        }
      }
    }
    return Next::ReduceTaggedBitcast(input, from, to, kind);
  }

  V<Float> REDUCE(FloatUnary)(V<Float> input, FloatUnaryOp::Kind kind,
                              FloatRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceFloatUnary(input, kind, rep);
    }
    if (float k; rep == FloatRepresentation::Float32() &&
                 matcher.MatchFloat32Constant(input, &k)) {
      if (std::isnan(k) && !signalling_nan_possible) {
        return __ Float32Constant(std::numeric_limits<float>::quiet_NaN());
      }
      switch (kind) {
        case FloatUnaryOp::Kind::kAbs:
          return __ Float32Constant(std::abs(k));
        case FloatUnaryOp::Kind::kNegate:
          return __ Float32Constant(-k);
        case FloatUnaryOp::Kind::kSilenceNaN:
          DCHECK(!std::isnan(k));
          return __ Float32Constant(k);
        case FloatUnaryOp::Kind::kRoundDown:
          return __ Float32Constant(std::floor(k));
        case FloatUnaryOp::Kind::kRoundUp:
          return __ Float32Constant(std::ceil(k));
        case FloatUnaryOp::Kind::kRoundToZero:
          return __ Float32Constant(std::trunc(k));
        case FloatUnaryOp::Kind::kRoundTiesEven:
          DCHECK_EQ(std::nearbyint(1.5), 2);
          DCHECK_EQ(std::nearbyint(2.5), 2);
          return __ Float32Constant(std::nearbyint(k));
        case FloatUnaryOp::Kind::kLog:
          return __ Float32Constant(base::ieee754::log(k));
        case FloatUnaryOp::Kind::kSqrt:
          return __ Float32Constant(std::sqrt(k));
        case FloatUnaryOp::Kind::kExp:
          return __ Float32Constant(base::ieee754::exp(k));
        case FloatUnaryOp::Kind::kExpm1:
          return __ Float32Constant(base::ieee754::expm1(k));
        case FloatUnaryOp::Kind::kSin:
          return __ Float32Constant(SIN_IMPL(k));
        case FloatUnaryOp::Kind::kCos:
          return __ Float32Constant(COS_IMPL(k));
        case FloatUnaryOp::Kind::kSinh:
          return __ Float32Constant(base::ieee754::sinh(k));
        case FloatUnaryOp::Kind::kCosh:
          return __ Float32Constant(base::ieee754::cosh(k));
        case FloatUnaryOp::Kind::kAcos:
          return __ Float32Constant(base::ieee754::acos(k));
        case FloatUnaryOp::Kind::kAsin:
          return __ Float32Constant(base::ieee754::asin(k));
        case FloatUnaryOp::Kind::kAsinh:
          return __ Float32Constant(base::ieee754::asinh(k));
        case FloatUnaryOp::Kind::kAcosh:
          return __ Float32Constant(base::ieee754::acosh(k));
        case FloatUnaryOp::Kind::kTan:
          return __ Float32Constant(base::ieee754::tan(k));
        case FloatUnaryOp::Kind::kTanh:
          return __ Float32Constant(base::ieee754::tanh(k));
        case FloatUnaryOp::Kind::kLog2:
          return __ Float32Constant(base::ieee754::log2(k));
        case FloatUnaryOp::Kind::kLog10:
          return __ Float32Constant(base::ieee754::log10(k));
        case FloatUnaryOp::Kind::kLog1p:
          return __ Float32Constant(base::ieee754::log1p(k));
        case FloatUnaryOp::Kind::kCbrt:
          return __ Float32Constant(base::ieee754::cbrt(k));
        case FloatUnaryOp::Kind::kAtan:
          return __ Float32Constant(base::ieee754::atan(k));
        case FloatUnaryOp::Kind::kAtanh:
          return __ Float32Constant(base::ieee754::atanh(k));
      }
    } else if (double k; rep == FloatRepresentation::Float64() &&
                         matcher.MatchFloat64Constant(input, &k)) {
      if (std::isnan(k) && !signalling_nan_possible) {
        return __ Float64Constant(std::numeric_limits<double>::quiet_NaN());
      }
      switch (kind) {
        case FloatUnaryOp::Kind::kAbs:
          return __ Float64Constant(std::abs(k));
        case FloatUnaryOp::Kind::kNegate:
          return __ Float64Constant(-k);
        case FloatUnaryOp::Kind::kSilenceNaN:
          DCHECK(!std::isnan(k));
          return __ Float64Constant(k);
        case FloatUnaryOp::Kind::kRoundDown:
          return __ Float64Constant(std::floor(k));
        case FloatUnaryOp::Kind::kRoundUp:
          return __ Float64Constant(std::ceil(k));
        case FloatUnaryOp::Kind::kRoundToZero:
          return __ Float64Constant(std::trunc(k));
        case FloatUnaryOp::Kind::kRoundTiesEven:
          DCHECK_EQ(std::nearbyint(1.5), 2);
          DCHECK_EQ(std::nearbyint(2.5), 2);
          return __ Float64Constant(std::nearbyint(k));
        case FloatUnaryOp::Kind::kLog:
          return __ Float64Constant(base::ieee754::log(k));
        case FloatUnaryOp::Kind::kSqrt:
          return __ Float64Constant(std::sqrt(k));
        case FloatUnaryOp::Kind::kExp:
          return __ Float64Constant(base::ieee754::exp(k));
        case FloatUnaryOp::Kind::kExpm1:
          return __ Float64Constant(base::ieee754::expm1(k));
        case FloatUnaryOp::Kind::kSin:
          return __ Float64Constant(SIN_IMPL(k));
        case FloatUnaryOp::Kind::kCos:
          return __ Float64Constant(COS_IMPL(k));
        case FloatUnaryOp::Kind::kSinh:
          return __ Float64Constant(base::ieee754::sinh(k));
        case FloatUnaryOp::Kind::kCosh:
          return __ Float64Constant(base::ieee754::cosh(k));
        case FloatUnaryOp::Kind::kAcos:
          return __ Float64Constant(base::ieee754::acos(k));
        case FloatUnaryOp::Kind::kAsin:
          return __ Float64Constant(base::ieee754::asin(k));
        case FloatUnaryOp::Kind::kAsinh:
          return __ Float64Constant(base::ieee754::asinh(k));
        case FloatUnaryOp::Kind::kAcosh:
          return __ Float64Constant(base::ieee754::acosh(k));
        case FloatUnaryOp::Kind::kTan:
          return __ Float64Constant(base::ieee754::tan(k));
        case FloatUnaryOp::Kind::kTanh:
          return __ Float64Constant(base::ieee754::tanh(k));
        case FloatUnaryOp::Kind::kLog2:
          return __ Float64Constant(base::ieee754::log2(k));
        case FloatUnaryOp::Kind::kLog10:
          return __ Float64Constant(base::ieee754::log10(k));
        case FloatUnaryOp::Kind::kLog1p:
          return __ Float64Constant(base::ieee754::log1p(k));
        case FloatUnaryOp::Kind::kCbrt:
          return __ Float64Constant(base::ieee754::cbrt(k));
        case FloatUnaryOp::Kind::kAtan:
          return __ Float64Constant(base::ieee754::atan(k));
        case FloatUnaryOp::Kind::kAtanh:
          return __ Float64Constant(base::ieee754::atanh(k));
      }
    }
    return Next::ReduceFloatUnary(input, kind, rep);
  }

  V<Word> REDUCE(WordUnary)(V<Word> input, WordUnaryOp::Kind kind,
                            WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceWordUnary(input, kind, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      input = TryRemoveWord32ToWord64Conversion(input);
    }
    if (uint32_t k; rep == WordRepresentation::Word32() &&
                    matcher.MatchIntegralWord32Constant(input, &k)) {
      switch (kind) {
        case WordUnaryOp::Kind::kReverseBytes:
          return __ Word32Constant(base::bits::ReverseBytes(k));
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return __ Word32Constant(base::bits::CountLeadingZeros(k));
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return __ Word32Constant(base::bits::CountTrailingZeros(k));
        case WordUnaryOp::Kind::kPopCount:
          return __ Word32Constant(base::bits::CountPopulation(k));
        case WordUnaryOp::Kind::kSignExtend8:
          return __ Word32Constant(int32_t{static_cast<int8_t>(k)});
        case WordUnaryOp::Kind::kSignExtend16:
          return __ Word32Constant(int32_t{static_cast<int16_t>(k)});
      }
    } else if (uint64_t k; rep == WordRepresentation::Word64() &&
                           matcher.MatchIntegralWord64Constant(input, &k)) {
      switch (kind) {
        case WordUnaryOp::Kind::kReverseBytes:
          return __ Word64Constant(base::bits::ReverseBytes(k));
        case WordUnaryOp::Kind::kCountLeadingZeros:
          return __ Word64Constant(uint64_t{base::bits::CountLeadingZeros(k)});
        case WordUnaryOp::Kind::kCountTrailingZeros:
          return __ Word64Constant(uint64_t{base::bits::CountTrailingZeros(k)});
        case WordUnaryOp::Kind::kPopCount:
          return __ Word64Constant(uint64_t{base::bits::CountPopulation(k)});
        case WordUnaryOp::Kind::kSignExtend8:
          return __ Word64Constant(int64_t{static_cast<int8_t>(k)});
        case WordUnaryOp::Kind::kSignExtend16:
          return __ Word64Constant(int64_t{static_cast<int16_t>(k)});
      }
    }
    return Next::ReduceWordUnary(input, kind, rep);
  }

  V<Float> REDUCE(FloatBinop)(V<Float> lhs, V<Float> rhs,
                              FloatBinopOp::Kind kind,
                              FloatRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceFloatBinop(lhs, rhs, kind, rep);
    }

    using Kind = FloatBinopOp::Kind;

    // Place constant on the right for commutative operators.
    if (FloatBinopOp::IsCommutative(kind) && matcher.Is<ConstantOp>(lhs) &&
        !matcher.Is<ConstantOp>(rhs)) {
      return ReduceFloatBinop(rhs, lhs, kind, rep);
    }

    // constant folding
    if (float k1, k2; rep == FloatRepresentation::Float32() &&
                      matcher.MatchFloat32Constant(lhs, &k1) &&
                      matcher.MatchFloat32Constant(rhs, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ Float32Constant(k1 + k2);
        case Kind::kMul:
          return __ Float32Constant(k1 * k2);
        case Kind::kSub:
          return __ Float32Constant(k1 - k2);
        case Kind::kMin:
          return __ Float32Constant(JSMin(k1, k2));
        case Kind::kMax:
          return __ Float32Constant(JSMax(k1, k2));
        case Kind::kDiv:
          return __ Float32Constant(k1 / k2);
        case Kind::kPower:
          return __ Float32Constant(base::ieee754::pow(k1, k2));
        case Kind::kAtan2:
          return __ Float32Constant(base::ieee754::atan2(k1, k2));
        case Kind::kMod:
          UNREACHABLE();
      }
    }
    if (double k1, k2; rep == FloatRepresentation::Float64() &&
                       matcher.MatchFloat64Constant(lhs, &k1) &&
                       matcher.MatchFloat64Constant(rhs, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ Float64Constant(k1 + k2);
        case Kind::kMul:
          return __ Float64Constant(k1 * k2);
        case Kind::kSub:
          return __ Float64Constant(k1 - k2);
        case Kind::kMin:
          return __ Float64Constant(JSMin(k1, k2));
        case Kind::kMax:
          return __ Float64Constant(JSMax(k1, k2));
        case Kind::kDiv:
          return __ Float64Constant(k1 / k2);
        case Kind::kMod:
          return __ Float64Constant(Modulo(k1, k2));
        case Kind::kPower:
          return __ Float64Constant(base::ieee754::pow(k1, k2));
        case Kind::kAtan2:
          return __ Float64Constant(base::ieee754::atan2(k1, k2));
      }
    }

    // lhs <op> NaN  =>  NaN
    if (matcher.MatchNaN(rhs) ||
        (matcher.MatchNaN(lhs) && kind != Kind::kPower)) {
      // Return a quiet NaN since Wasm operations could have signalling NaN as
      // input but not as output.
      return __ FloatConstant(std::numeric_limits<double>::quiet_NaN(), rep);
    }

    if (matcher.Is<ConstantOp>(rhs)) {
      if (kind == Kind::kMul) {
        // lhs * 1  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, 1.0)) {
          return lhs;
        }
        // lhs * 2  =>  lhs + lhs
        if (matcher.MatchFloat(rhs, 2.0)) {
          return __ FloatAdd(lhs, lhs, rep);
        }
        // lhs * -1  =>  -lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, -1.0)) {
          return __ FloatNegate(lhs, rep);
        }
      }

      if (kind == Kind::kDiv) {
        // lhs / 1  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, 1.0)) {
          return lhs;
        }
        // lhs / -1  =>  -lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, -1.0)) {
          return __ FloatNegate(lhs, rep);
        }
        // All reciprocals of non-denormal powers of two can be represented
        // exactly, so division by power of two can be reduced to
        // multiplication by reciprocal, with the same result.
        // x / k  =>  x * (1 / k)
        if (rep == FloatRepresentation::Float32()) {
          if (float k;
              matcher.MatchFloat32Constant(rhs, &k) && std::isnormal(k) &&
              k != 0 && std::isfinite(k) &&
              base::bits::IsPowerOfTwo(base::Double(k).Significand())) {
            return __ FloatMul(lhs, __ FloatConstant(1.0 / k, rep), rep);
          }
        } else {
          DCHECK_EQ(rep, FloatRepresentation::Float64());
          if (double k;
              matcher.MatchFloat64Constant(rhs, &k) && std::isnormal(k) &&
              k != 0 && std::isfinite(k) &&
              base::bits::IsPowerOfTwo(base::Double(k).Significand())) {
            return __ FloatMul(lhs, __ FloatConstant(1.0 / k, rep), rep);
          }
        }
      }

      if (kind == Kind::kMod) {
        // x % 0  =>  NaN
        if (matcher.MatchFloat(rhs, 0.0)) {
          return __ FloatConstant(std::numeric_limits<double>::quiet_NaN(),
                                  rep);
        }
      }

      if (kind == Kind::kSub) {
        // lhs - +0.0  =>  lhs
        if (!signalling_nan_possible && matcher.MatchFloat(rhs, +0.0)) {
          return lhs;
        }
      }

      if (kind == Kind::kPower) {
        if (matcher.MatchFloat(rhs, 0.0) || matcher.MatchFloat(rhs, -0.0)) {
          // lhs ** 0  ==>  1
          return __ FloatConstant(1.0, rep);
        }
        if (matcher.MatchFloat(rhs, 2.0)) {
          // lhs ** 2  ==>  lhs * lhs
          return __ FloatMul(lhs, lhs, rep);
        }
        if (matcher.MatchFloat(rhs, 0.5)) {
          // lhs ** 0.5  ==>  sqrt(lhs)
          // (unless if lhs is -infinity)
          Variable result = __ NewLoopInvariantVariable(rep);
          IF (UNLIKELY(__ FloatLessThanOrEqual(
                  lhs, __ FloatConstant(-V8_INFINITY, rep), rep))) {
            __ SetVariable(result, __ FloatConstant(V8_INFINITY, rep));
          } ELSE {
            __ SetVariable(result, __ FloatSqrt(lhs, rep));
          }

          return __ GetVariable(result);
        }
      }
    }

    if (!signalling_nan_possible && kind == Kind::kSub &&
        matcher.MatchFloat(lhs, -0.0)) {
      // -0.0 - round_down(-0.0 - y) => round_up(y)
      if (V<Float> a, b, c;
          FloatUnaryOp::IsSupported(FloatUnaryOp::Kind::kRoundUp, rep) &&
          matcher.MatchFloatRoundDown(rhs, &a, rep) &&
          matcher.MatchFloatSub(a, &b, &c, rep) &&
          matcher.MatchFloat(b, -0.0)) {
        return __ FloatRoundUp(c, rep);
      }
      // -0.0 - rhs  =>  -rhs
      return __ FloatNegate(rhs, rep);
    }

    return Next::ReduceFloatBinop(lhs, rhs, kind, rep);
  }

  V<Word> REDUCE(WordBinop)(V<Word> left, V<Word> right, WordBinopOp::Kind kind,
                            WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceWordBinop(left, right, kind, rep);
    }

    using Kind = WordBinopOp::Kind;

    DCHECK_EQ(rep, any_of(WordRepresentation::Word32(),
                          WordRepresentation::Word64()));
    bool is_64 = rep == WordRepresentation::Word64();

    if (!is_64) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }

    // Place constant on the right for commutative operators.
    if (WordBinopOp::IsCommutative(kind) && matcher.Is<ConstantOp>(left) &&
        !matcher.Is<ConstantOp>(right)) {
      return ReduceWordBinop(right, left, kind, rep);
    }
    // constant folding
    if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(left, rep, &k1) &&
                         matcher.MatchIntegralWordConstant(right, rep, &k2)) {
      switch (kind) {
        case Kind::kAdd:
          return __ WordConstant(k1 + k2, rep);
        case Kind::kMul:
          return __ WordConstant(k1 * k2, rep);
        case Kind::kBitwiseAnd:
          return __ WordConstant(k1 & k2, rep);
        case Kind::kBitwiseOr:
          return __ WordConstant(k1 | k2, rep);
        case Kind::kBitwiseXor:
          return __ WordConstant(k1 ^ k2, rep);
        case Kind::kSub:
          return __ WordConstant(k1 - k2, rep);
        case Kind::kSignedMulOverflownBits:
          return __ WordConstant(
              is_64 ? base::bits::SignedMulHigh64(static_cast<int64_t>(k1),
                                                  static_cast<int64_t>(k2))
                    : base::bits::SignedMulHigh32(static_cast<int32_t>(k1),
                                                  static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedMulOverflownBits:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedMulHigh64(k1, k2)
                    : base::bits::UnsignedMulHigh32(static_cast<uint32_t>(k1),
                                                    static_cast<uint32_t>(k2)),
              rep);
        case Kind::kSignedDiv:
          return __ WordConstant(
              is_64 ? base::bits::SignedDiv64(k1, k2)
                    : base::bits::SignedDiv32(static_cast<int32_t>(k1),
                                              static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedDiv:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedDiv64(k1, k2)
                    : base::bits::UnsignedDiv32(static_cast<uint32_t>(k1),
                                                static_cast<uint32_t>(k2)),
              rep);
        case Kind::kSignedMod:
          return __ WordConstant(
              is_64 ? base::bits::SignedMod64(k1, k2)
                    : base::bits::SignedMod32(static_cast<int32_t>(k1),
                                              static_cast<int32_t>(k2)),
              rep);
        case Kind::kUnsignedMod:
          return __ WordConstant(
              is_64 ? base::bits::UnsignedMod64(k1, k2)
                    : base::bits::UnsignedMod32(static_cast<uint32_t>(k1),
                                                static_cast<uint32_t>(k2)),
              rep);
      }
    }

    if (kind == WordBinopOp::Kind::kBitwiseAnd &&
        rep == WordRepresentation::Word32()) {
      if (auto right_bitfield =
              BitfieldCheck::Detect(matcher, __ output_graph(), right)) {
        if (auto left_bitfield =
                BitfieldCheck::Detect(matcher, __ output_graph(), left)) {
          if (auto combined_bitfield =
                  left_bitfield->TryCombine(*right_bitfield)) {
            OpIndex source = combined_bitfield->source;
            if (combined_bitfield->truncate_from_64_bit) {
              source = __ TruncateWord64ToWord32(source);
            }
            return __ Word32Equal(
                __ Word32BitwiseAnd(source, combined_bitfield->mask),
                combined_bitfield->masked_value);
          }
        }
      }
    }

    if (uint64_t right_value;
        matcher.MatchIntegralWordConstant(right, rep, &right_value)) {
      // TODO(jkummerow): computing {right_value_signed} could probably be
      // handled by the 4th argument to {MatchIntegralWordConstant}.
      int64_t right_value_signed =
          is_64 ? static_cast<int64_t>(right_value)
                : int64_t{static_cast<int32_t>(right_value)};
      // (a <op> k1) <op> k2  =>  a <op> (k1 <op> k2)
      if (V<Word> a, k1; WordBinopOp::IsAssociative(kind) &&
                         matcher.MatchWordBinop(left, &a, &k1, kind, rep) &&
                         matcher.Is<ConstantOp>(k1)) {
        V<Word> k2 = right;
        // This optimization allows to do constant folding of `k1` and `k2`.
        // However, if (a <op> k1) has to be calculated anyways, then constant
        // folding does not save any calculations during runtime, and it may
        // increase register pressure because it extends the lifetime of `a`.
        // Therefore we do the optimization only when `left = (a <op k1)` has no
        // other uses.
        if (matcher.Get(left).saturated_use_count.IsZero()) {
          return ReduceWordBinop(a, ReduceWordBinop(k1, k2, kind, rep), kind,
                                 rep);
        }
      }
      switch (kind) {
        case Kind::kSub:
          // left - k  =>  left + -k
          return ReduceWordBinop(left, __ WordConstant(-right_value, rep),
                                 Kind::kAdd, rep);
        case Kind::kAdd:
          // left + 0  =>  left
          if (right_value == 0) {
            return left;
          }
          break;
        case Kind::kBitwiseXor:
          // left ^ 0  =>  left
          if (right_value == 0) {
            return left;
          }
          // left ^ 1  =>  left == 0  if left is 0 or 1
          if (right_value == 1 && IsBit(left)) {
            return __ Word32Equal(V<Word32>::Cast(left), 0);
          }
          // (x ^ -1) ^ -1  =>  x
          {
            V<Word> x, y;
            int64_t k;
            if (right_value_signed == -1 &&
                matcher.MatchBitwiseAnd(left, &x, &y, rep) &&
                matcher.MatchIntegralWordConstant(y, rep, &k) && k == -1) {
              return x;
            }
          }
          break;
        case Kind::kBitwiseOr:
          // left | 0  =>  left
          if (right_value == 0) {
            return left;
          }
          // left | -1  =>  -1
          if (right_value_signed == -1) {
            return right;
          }
          // (x & K1) | K2 => x | K2 if K2 has ones for every zero bit in K1.
          // This case can be constructed by UpdateWord and UpdateWord32 in CSA.
          {
            V<Word> x, y;
            uint64_t k1;
            uint64_t k2 = right_value;
            if (matcher.MatchBitwiseAnd(left, &x, &y, rep) &&
                matcher.MatchIntegralWordConstant(y, rep, &k1) &&
                (k1 | k2) == rep.MaxUnsignedValue()) {
              return __ WordBitwiseOr(x, right, rep);
            }
          }
          break;
        case Kind::kMul:
          // left * 0  =>  0
          if (right_value == 0) {
            return __ WordConstant(0, rep);
          }
          // left * 1  =>  left
          if (right_value == 1) {
            return left;
          }
          // left * -1 => 0 - left
          if (right_value_signed == -1) {
            return __ WordSub(__ WordConstant(0, rep), left, rep);
          }
          // left * 2^k  =>  left << k
          if (base::bits::IsPowerOfTwo(right_value)) {
            return __ ShiftLeft(left, base::bits::WhichPowerOfTwo(right_value),
                                rep);
          }
          break;
        case Kind::kBitwiseAnd:
          // left & -1 => left
          if (right_value_signed == -1) {
            return left;
          }
          // x & 0  =>  0
          if (right_value == 0) {
            return right;
          }

          if (right_value == 1) {
            // (x + x) & 1  =>  0
            V<Word> left_ignore_extensions =
                IsWord32ConvertedToWord64(left)
                    ? UndoWord32ToWord64Conversion(left)
                    : left;
            if (V<Word> a, b;
                matcher.MatchWordAdd(left_ignore_extensions, &a, &b,
                                     WordRepresentation::Word32()) &&
                a == b) {
              return __ WordConstant(0, rep);
            }

            // CMP & 1  =>  CMP
            if (IsBit(left_ignore_extensions)) {
              return left;
            }

            static_assert(kSmiTagMask == 1);
            // HeapObject & 1 => 1  ("& 1" is a Smi-check)
            if (TryMatchHeapObject(left)) {
              return __ WordConstant(1, rep);
            }
          }

          // asm.js often benefits from these transformations, to optimize out
          // unnecessary memory access alignment masks. Conventions used in
          // the comments below:
          // x, y: arbitrary values
          // K, L, M: arbitrary constants
          // (-1 << K) == mask: the right-hand side of the bitwise AND.
          if (IsNegativePowerOfTwo(right_value_signed)) {
            uint64_t mask = right_value;
            int K = base::bits::CountTrailingZeros64(mask);
            V<Word> x, y;
            {
              int L;
              //   (x << L) & (-1 << K)
              // => x << L               iff L >= K
              if (matcher.MatchConstantLeftShift(left, &x, rep, &L) && L >= K) {
                return left;
              }
            }

            if (matcher.MatchWordAdd(left, &x, &y, rep)) {
              uint64_t L;  // L == (M << K) iff (L & mask) == L.

              //    (x              + (M << K)) & (-1 << K)
              // => (x & (-1 << K)) + (M << K)
              if (matcher.MatchIntegralWordConstant(y, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep),
                                  __ WordConstant(L, rep), rep);
              }

              //   (x1 * (M << K) + y) & (-1 << K)
              // => x1 * (M << K) + (y & (-1 << K))
              V<Word> x1, x2, y1, y2;
              if (matcher.MatchWordMul(x, &x1, &x2, rep) &&
                  matcher.MatchIntegralWordConstant(x2, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(x, __ WordBitwiseAnd(y, right, rep), rep);
              }
              // Same as above with swapped order:
              //    (x              + y1 * (M << K)) & (-1 << K)
              // => (x & (-1 << K)) + y1 * (M << K)
              if (matcher.MatchWordMul(y, &y1, &y2, rep) &&
                  matcher.MatchIntegralWordConstant(y2, rep, &L) &&
                  (L & mask) == L) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep), y, rep);
              }

              //   ((x1 << K) + y) & (-1 << K)
              // => (x1 << K) + (y & (-1 << K))
              int K2;
              if (matcher.MatchConstantLeftShift(x, &x1, rep, &K2) && K2 == K) {
                return __ WordAdd(x, __ WordBitwiseAnd(y, right, rep), rep);
              }
              // Same as above with swapped order:
              //    (x +              (y1 << K)) & (-1 << K)
              // => (x & (-1 << K)) + (y1 << K)
              if (matcher.MatchConstantLeftShift(y, &y1, rep, &K2) && K2 == K) {
                return __ WordAdd(__ WordBitwiseAnd(x, right, rep), y, rep);
              }
            } else if (matcher.MatchWordMul(left, &x, &y, rep)) {
              // (x * (M << K)) & (-1 << K) => x * (M << K)
              uint64_t L;  // L == (M << K) iff (L & mask) == L.
              if (matcher.MatchIntegralWordConstant(y, rep, &L) &&
                  (L & mask) == L) {
                return left;
              }
            }
          }
          break;
        case WordBinopOp::Kind::kSignedDiv:
          return ReduceSignedDiv(left, right_value_signed, rep);
        case WordBinopOp::Kind::kUnsignedDiv:
          return ReduceUnsignedDiv(left, right_value, rep);
        case WordBinopOp::Kind::kSignedMod:
          // left % 0  =>  0
          // left % 1  =>  0
          // left % -1  =>  0
          if (right_value_signed == any_of(0, 1, -1)) {
            return __ WordConstant(0, rep);
          }
          if (right_value_signed != rep.MinSignedValue()) {
            right_value_signed = Abs(right_value_signed);
          }
          // left % 2^n  =>  ((left + m) & (2^n - 1)) - m
          // where m = (left >> bits-1) >>> bits-n
          // This is a branch-free version of the following:
          // left >= 0 ? left & (2^n - 1)
          //           : ((left + (2^n - 1)) & (2^n - 1)) - (2^n - 1)
          // Adding and subtracting (2^n - 1) before and after the bitwise-and
          // keeps the result congruent modulo 2^n, but shifts the resulting
          // value range to become -(2^n - 1) ... 0.
          if (base::bits::IsPowerOfTwo(right_value_signed)) {
            uint32_t bits = rep.bit_width();
            uint32_t n = base::bits::WhichPowerOfTwo(right_value_signed);
            V<Word> m = __ ShiftRightLogical(
                __ ShiftRightArithmetic(left, bits - 1, rep), bits - n, rep);
            return __ WordSub(
                __ WordBitwiseAnd(__ WordAdd(left, m, rep),
                                  __ WordConstant(right_value_signed - 1, rep),
                                  rep),
                m, rep);
          }
          // The `IntDiv` with a constant right-hand side will be turned into a
          // multiplication, avoiding the expensive integer division.
          return __ WordSub(
              left, __ WordMul(__ IntDiv(left, right, rep), right, rep), rep);
        case WordBinopOp::Kind::kUnsignedMod:
          // left % 0  =>  0
          // left % 1  =>  0
          if (right_value == 0 || right_value == 1) {
            return __ WordConstant(0, rep);
          }
          // x % 2^n => x & (2^n - 1)
          if (base::bits::IsPowerOfTwo(right_value)) {
            return __ WordBitwiseAnd(
                left, __ WordConstant(right_value - 1, rep), rep);
          }
          // The `UintDiv` with a constant right-hand side will be turned into a
          // multiplication, avoiding the expensive integer division.
          return __ WordSub(
              left, __ WordMul(right, __ UintDiv(left, right, rep), rep), rep);
        case WordBinopOp::Kind::kSignedMulOverflownBits:
        case WordBinopOp::Kind::kUnsignedMulOverflownBits:
          break;
      }
    }

    if (kind == Kind::kAdd) {
      V<Word> x, y, zero;
      // (0 - x) + y => y - x
      if (matcher.MatchWordSub(left, &zero, &x, rep) &&
          matcher.MatchZero(zero)) {
        y = right;
        return __ WordSub(y, x, rep);
      }
      // x + (0 - y) => x - y
      if (matcher.MatchWordSub(right, &zero, &y, rep) &&
          matcher.MatchZero(zero)) {
        x = left;
        return __ WordSub(x, y, rep);
      }
    }

    // 0 / right  =>  0
    // 0 % right  =>  0
    if (matcher.MatchZero(left) &&
        kind == any_of(Kind::kSignedDiv, Kind::kUnsignedDiv, Kind::kUnsignedMod,
                       Kind::kSignedMod)) {
      return __ WordConstant(0, rep);
    }

    if (left == right) {
      V<Word> x = left;
      switch (kind) {
        // x & x  =>  x
        // x | x  =>  x
        case WordBinopOp::Kind::kBitwiseAnd:
        case WordBinopOp::Kind::kBitwiseOr:
          return x;
        // x ^ x  =>  0
        // x - x  =>  0
        // x % x  =>  0
        case WordBinopOp::Kind::kBitwiseXor:
        case WordBinopOp::Kind::kSub:
        case WordBinopOp::Kind::kSignedMod:
        case WordBinopOp::Kind::kUnsignedMod:
          return __ WordConstant(0, rep);
        // x / x  =>  x != 0
        case WordBinopOp::Kind::kSignedDiv:
        case WordBinopOp::Kind::kUnsignedDiv: {
          V<Word> zero = __ WordConstant(0, rep);
          V<Word32> result = __ Word32Equal(__ Equal(left, zero, rep), 0);
          return __ ZeroExtendWord32ToRep(result, rep);
        }
        case WordBinopOp::Kind::kAdd:
        case WordBinopOp::Kind::kMul:
        case WordBinopOp::Kind::kSignedMulOverflownBits:
        case WordBinopOp::Kind::kUnsignedMulOverflownBits:
          break;
      }
    }

    if (std::optional<OpIndex> ror = TryReduceToRor(left, right, kind, rep)) {
      return *ror;
    }

    return Next::ReduceWordBinop(left, right, kind, rep);
  }

  bool TryMatchHeapObject(V<Any> idx, int depth = 0) {
    constexpr int kMaxDepth = 2;
    if (depth == kMaxDepth) return false;

    if (matcher.MatchHeapConstant(idx)) return true;
    if (matcher.Is<AllocateOp>(idx)) return true;
    if (matcher.Is<Opmask::kTaggedBitcastHeapObject>(idx)) return true;

    // A Phi whose inputs are all HeapObject is itself a HeapObject.
    if (const PhiOp* phi = matcher.TryCast<Opmask::kTaggedPhi>(idx)) {
      return base::all_of(phi->inputs(), [depth, this](V<Any> input) {
        return TryMatchHeapObject(input, depth + 1);
      });
    }

    // For anything else, assume that it's not a heap object.
    return false;
  }

  std::optional<V<Word>> TryReduceToRor(V<Word> left, V<Word> right,
                                        WordBinopOp::Kind kind,
                                        WordRepresentation rep) {
    // Recognize rotation, we are matcher.Matching and transforming as follows
    // (assuming kWord32, kWord64 is handled correspondingly):
    //   x << y         |  x >>> (32 - y)    =>  x ror (32 - y)
    //   x << (32 - y)  |  x >>> y           =>  x ror y
    //   x << y         ^  x >>> (32 - y)    =>  x ror (32 - y)   if 1 <= y < 32
    //   x << (32 - y)  ^  x >>> y           =>  x ror y          if 1 <= y < 32
    // (As well as the commuted forms.)
    // Note the side condition for XOR: the optimization doesn't hold for
    // an effective rotation amount of 0.

    if (!(kind == any_of(WordBinopOp::Kind::kBitwiseOr,
                         WordBinopOp::Kind::kBitwiseXor))) {
      return {};
    }

    const ShiftOp* high = matcher.TryCast<ShiftOp>(left);
    if (!high) return {};
    const ShiftOp* low = matcher.TryCast<ShiftOp>(right);
    if (!low) return {};

    if (low->kind == ShiftOp::Kind::kShiftLeft) {
      std::swap(low, high);
    }
    if (high->kind != ShiftOp::Kind::kShiftLeft ||
        low->kind != ShiftOp::Kind::kShiftRightLogical) {
      return {};
    }
    V<Word> x = high->left();
    if (low->left() != x) return {};
    V<Word> amount;
    uint64_t k;
    if (V<Word> a, b; matcher.MatchWordSub(high->right(), &a, &b, rep) &&
                      matcher.MatchIntegralWordConstant(a, rep, &k) &&
                      b == low->right() && k == rep.bit_width()) {
      amount = b;
    } else if (V<Word> a, b; matcher.MatchWordSub(low->right(), &a, &b, rep) &&
                             a == high->right() &&
                             matcher.MatchIntegralWordConstant(b, rep, &k) &&
                             k == rep.bit_width()) {
      amount = low->right();
    } else if (uint64_t k1, k2;
               matcher.MatchIntegralWordConstant(high->right(), rep, &k1) &&
               matcher.MatchIntegralWordConstant(low->right(), rep, &k2) &&
               k1 + k2 == rep.bit_width() && k1 >= 0 && k2 >= 0) {
      if (k1 == 0 || k2 == 0) {
        if (kind == WordBinopOp::Kind::kBitwiseXor) {
          return __ WordConstant(0, rep);
        } else {
          DCHECK_EQ(kind, WordBinopOp::Kind::kBitwiseOr);
          return x;
        }
      }
      return __ RotateRight(x, low->right(), rep);
    } else {
      return {};
    }
    if (kind == WordBinopOp::Kind::kBitwiseOr) {
      return __ RotateRight(x, amount, rep);
    } else {
      DCHECK_EQ(kind, WordBinopOp::Kind::kBitwiseXor);
      // Can't guarantee that rotation amount is not 0.
      return {};
    }
  }

  V<Tuple<Word, Word32>> REDUCE(OverflowCheckedBinop)(
      V<Word> left, V<Word> right, OverflowCheckedBinopOp::Kind kind,
      WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceOverflowCheckedBinop(left, right, kind, rep);
    }
    using Kind = OverflowCheckedBinopOp::Kind;
    if (OverflowCheckedBinopOp::IsCommutative(kind) &&
        matcher.Is<ConstantOp>(left) && !matcher.Is<ConstantOp>(right)) {
      return ReduceOverflowCheckedBinop(right, left, kind, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(left);
      right = TryRemoveWord32ToWord64Conversion(right);
    }
    // constant folding
    if (rep == WordRepresentation::Word32()) {
      if (int32_t k1, k2; matcher.MatchIntegralWord32Constant(left, &k1) &&
                          matcher.MatchIntegralWord32Constant(right, &k2)) {
        bool overflow;
        int32_t res;
        switch (kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            overflow = base::bits::SignedAddOverflow32(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            overflow = base::bits::SignedMulOverflow32(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            overflow = base::bits::SignedSubOverflow32(k1, k2, &res);
            break;
        }
        return __ Tuple(__ Word32Constant(res), __ Word32Constant(overflow));
      }
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      if (int64_t k1, k2; matcher.MatchIntegralWord64Constant(left, &k1) &&
                          matcher.MatchIntegralWord64Constant(right, &k2)) {
        bool overflow;
        int64_t res;
        switch (kind) {
          case OverflowCheckedBinopOp::Kind::kSignedAdd:
            overflow = base::bits::SignedAddOverflow64(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedMul:
            overflow = base::bits::SignedMulOverflow64(k1, k2, &res);
            break;
          case OverflowCheckedBinopOp::Kind::kSignedSub:
            overflow = base::bits::SignedSubOverflow64(k1, k2, &res);
            break;
        }
        return __ Tuple(__ Word64Constant(res), __ Word32Constant(overflow));
      }
    }

    // left + 0  =>  (left, false)
    // left - 0  =>  (left, false)
    if (kind == any_of(Kind::kSignedAdd, Kind::kSignedSub) &&
        matcher.MatchZero(right)) {
      return __ Tuple(left, __ Word32Constant(0));
    }

    if (kind == Kind::kSignedMul) {
      if (int64_t k; matcher.MatchIntegralWordConstant(right, rep, &k)) {
        // left * 0  =>  (0, false)
        if (k == 0) {
          return __ Tuple(__ WordConstant(0, rep), __ Word32Constant(false));
        }
        // left * 1  =>  (left, false)
        if (k == 1) {
          return __ Tuple(left, __ Word32Constant(false));
        }
        // left * -1  =>  0 - left
        if (k == -1) {
          return __ IntSubCheckOverflow(__ WordConstant(0, rep), left, rep);
        }
        // left * 2  =>  left + left
        if (k == 2) {
          return __ IntAddCheckOverflow(left, left, rep);
        }
      }
    }

    // UntagSmi(x) + UntagSmi(x)  =>  (x, false)
    // (where UntagSmi(x) = x >> 1   with a ShiftOutZeros shift)
    if (kind == Kind::kSignedAdd && left == right) {
      uint16_t amount;
      if (V<Word32> x; matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                           left, &x, WordRepresentation::Word32(), &amount) &&
                       amount == 1) {
        return __ Tuple(x, __ Word32Constant(0));
      }
    }

    return Next::ReduceOverflowCheckedBinop(left, right, kind, rep);
  }

  V<Word32> REDUCE(Comparison)(V<Any> left, V<Any> right,
                               ComparisonOp::Kind kind,
                               RegisterRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceComparison(left, right, kind, rep);
    }
    if (kind == ComparisonOp::Kind::kEqual) {
      return ReduceCompareEqual(left, right, rep);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(V<Word>::Cast(left));
      right = TryRemoveWord32ToWord64Conversion(V<Word>::Cast(right));
    }
    using Kind = ComparisonOp::Kind;
    if (left == right &&
        !(rep == any_of(RegisterRepresentation::Float32(),
                        RegisterRepresentation::Float64())) &&
        kind == any_of(Kind::kSignedLessThanOrEqual,
                       Kind::kUnsignedLessThanOrEqual)) {
      switch (kind) {
        case Kind::kEqual:
          UNREACHABLE();
        case Kind::kUnsignedLessThanOrEqual:
        case Kind::kSignedLessThanOrEqual:
          return __ Word32Constant(1);
        case Kind::kUnsignedLessThan:
        case Kind::kSignedLessThan:
          return __ Word32Constant(0);
      }
    }
    // constant folding
    if (matcher.Is<ConstantOp>(right) && matcher.Is<ConstantOp>(left)) {
      switch (rep.value()) {
        case RegisterRepresentation::Word32():
        case RegisterRepresentation::Word64(): {
          if (kind ==
              any_of(Kind::kSignedLessThan, Kind::kSignedLessThanOrEqual)) {
            if (int64_t k1, k2; matcher.MatchIntegralWordConstant(
                                    left, WordRepresentation(rep), &k1) &&
                                matcher.MatchIntegralWordConstant(
                                    right, WordRepresentation(rep), &k2)) {
              switch (kind) {
                case ComparisonOp::Kind::kSignedLessThan:
                  return __ Word32Constant(k1 < k2);
                case ComparisonOp::Kind::kSignedLessThanOrEqual:
                  return __ Word32Constant(k1 <= k2);
                case ComparisonOp::Kind::kEqual:
                case ComparisonOp::Kind::kUnsignedLessThan:
                case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                  UNREACHABLE();
              }
            }
          } else {
            if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(
                                     left, WordRepresentation(rep), &k1) &&
                                 matcher.MatchIntegralWordConstant(
                                     right, WordRepresentation(rep), &k2)) {
              switch (kind) {
                case ComparisonOp::Kind::kUnsignedLessThan:
                  return __ Word32Constant(k1 < k2);
                case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                  return __ Word32Constant(k1 <= k2);
                case ComparisonOp::Kind::kEqual:
                case ComparisonOp::Kind::kSignedLessThan:
                case ComparisonOp::Kind::kSignedLessThanOrEqual:
                  UNREACHABLE();
              }
            }
          }
          break;
        }
        case RegisterRepresentation::Float32(): {
          if (float k1, k2; matcher.MatchFloat32Constant(left, &k1) &&
                            matcher.MatchFloat32Constant(right, &k2)) {
            switch (kind) {
              case ComparisonOp::Kind::kSignedLessThan:
                return __ Word32Constant(k1 < k2);
              case ComparisonOp::Kind::kSignedLessThanOrEqual:
                return __ Word32Constant(k1 <= k2);
              case ComparisonOp::Kind::kEqual:
              case ComparisonOp::Kind::kUnsignedLessThan:
              case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                UNREACHABLE();
            }
          }
          break;
        }
        case RegisterRepresentation::Float64(): {
          if (double k1, k2; matcher.MatchFloat64Constant(left, &k1) &&
                             matcher.MatchFloat64Constant(right, &k2)) {
            switch (kind) {
              case ComparisonOp::Kind::kSignedLessThan:
                return __ Word32Constant(k1 < k2);
              case ComparisonOp::Kind::kSignedLessThanOrEqual:
                return __ Word32Constant(k1 <= k2);
              case ComparisonOp::Kind::kEqual:
              case ComparisonOp::Kind::kUnsignedLessThan:
              case ComparisonOp::Kind::kUnsignedLessThanOrEqual:
                UNREACHABLE();
            }
          }
          break;
        }
        default:
          UNREACHABLE();
      }
    }
    if (rep == RegisterRepresentation::Float64() &&
        IsFloat32ConvertedToFloat64(left) &&
        IsFloat32ConvertedToFloat64(right)) {
      return __ Comparison(UndoFloat32ToFloat64Conversion(left),
                           UndoFloat32ToFloat64Conversion(right), kind,
                           RegisterRepresentation::Float32());
    }
    if (rep.IsWord()) {
      WordRepresentation rep_w{rep};
      if (kind == Kind::kUnsignedLessThanOrEqual) {
        // 0 <= x  =>  true
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(left, rep_w, &k) && k == 0) {
          return __ Word32Constant(1);
        }
        // x <= MaxUint  =>  true
        if (uint64_t k; matcher.MatchIntegralWordConstant(right, rep_w, &k) &&
                        k == rep.MaxUnsignedValue()) {
          return __ Word32Constant(1);
        }
        // x <= 0  =>  x == 0
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(right, rep_w, &k) && k == 0) {
          return __ Equal(left, __ WordConstant(0, rep_w), rep_w);
        }
      }
      if (kind == Kind::kUnsignedLessThan) {
        // x < 0  =>  false
        if (uint64_t k;
            matcher.MatchIntegralWordConstant(right, rep_w, &k) && k == 0) {
          return __ Word32Constant(0);
        }
        // MaxUint < x  =>  true
        if (uint64_t k; matcher.MatchIntegralWordConstant(left, rep_w, &k) &&
                        k == rep.MaxUnsignedValue()) {
          return __ Word32Constant(0);
        }
      }
      {
        // (x >> k) </<=  (y >> k)  =>  x </<=  y   if shifts reversible
        V<Word> x, y;
        uint16_t k1, k2;
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                left, &x, rep_w, &k1) &&
            matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                right, &y, rep_w, &k2) &&
            k1 == k2) {
          return __ Comparison(x, y, kind, rep_w);
        }
      }
      {
        // (x >> k1) </<= k2  =>  x </<= (k2 << k1)  if shifts reversible
        // Only perform the transformation if the shift is not used yet, to
        // avoid keeping both the shift and x alive.
        V<Word> x;
        uint16_t k1;
        int64_t k2;
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                left, &x, rep_w, &k1) &&
            matcher.MatchIntegralWordConstant(right, rep_w, &k2) &&
            CountLeadingSignBits(k2, rep_w) > k1) {
          if (matcher.Get(left).saturated_use_count.IsZero()) {
            return __ Comparison(
                x, __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w), kind,
                rep_w);
          } else if constexpr (reducer_list_contains<
                                   ReducerList, ValueNumberingReducer>::value) {
            // If the shift has uses, we only apply the transformation if the
            // result would be GVNed away.
            OpIndex rhs =
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w);
            static_assert(ComparisonOp::input_count == 2);
            static_assert(sizeof(ComparisonOp) == 8);
            base::SmallVector<OperationStorageSlot, 32> storage;
            ComparisonOp* cmp =
                CreateOperation<ComparisonOp>(storage, x, rhs, kind, rep_w);
            if (__ WillGVNOp(*cmp)) {
              return __ Comparison(x, rhs, kind, rep_w);
            }
          }
        }
        // k2 </<= (x >> k1)  =>  (k2 << k1) </<= x  if shifts reversible
        // Only perform the transformation if the shift is not used yet, to
        // avoid keeping both the shift and x alive.
        if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                right, &x, rep_w, &k1) &&
            matcher.MatchIntegralWordConstant(left, rep_w, &k2) &&
            CountLeadingSignBits(k2, rep_w) > k1) {
          if (matcher.Get(right).saturated_use_count.IsZero()) {
            return __ Comparison(
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w), x, kind,
                rep_w);
          } else if constexpr (reducer_list_contains<
                                   ReducerList, ValueNumberingReducer>::value) {
            // If the shift has uses, we only apply the transformation if the
            // result would be GVNed away.
            OpIndex lhs =
                __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w);
            static_assert(ComparisonOp::input_count == 2);
            static_assert(sizeof(ComparisonOp) == 8);
            base::SmallVector<OperationStorageSlot, 32> storage;
            ComparisonOp* cmp =
                CreateOperation<ComparisonOp>(storage, lhs, x, kind, rep_w);
            if (__ WillGVNOp(*cmp)) {
              return __ Comparison(lhs, x, kind, rep_w);
            }
          }
        }
      }
      // Map 64bit to 32bit comparisons.
      if (rep_w == WordRepresentation::Word64()) {
        std::optional<bool> left_sign_extended;
        std::optional<bool> right_sign_extended;
        if (IsWord32ConvertedToWord64(left, &left_sign_extended) &&
            IsWord32ConvertedToWord64(right, &right_sign_extended)) {
          if (left_sign_extended != true && right_sign_extended != true) {
            // Both sides were zero-extended, so the resulting comparison always
            // behaves unsigned even if it was a signed 64bit comparison.
            auto SetSigned = [](Kind kind, bool is_signed) {
              switch (kind) {
                case Kind::kSignedLessThan:
                case Kind::kUnsignedLessThan:
                  return is_signed ? Kind::kSignedLessThan
                                   : Kind::kUnsignedLessThan;
                case Kind::kSignedLessThanOrEqual:
                case Kind::kUnsignedLessThanOrEqual:
                  return is_signed ? Kind::kSignedLessThanOrEqual
                                   : Kind::kUnsignedLessThanOrEqual;
                case Kind::kEqual:
                  UNREACHABLE();
              }
            };
            return __ Comparison(
                UndoWord32ToWord64Conversion(V<Word64>::Cast(left)),
                UndoWord32ToWord64Conversion(V<Word64>::Cast(right)),
                SetSigned(kind, false), WordRepresentation::Word32());
          } else if (left_sign_extended != false &&
                     right_sign_extended != false) {
            // Both sides were sign-extended, this preserves both signed and
            // unsigned comparisons.
            return __ Comparison(
                UndoWord32ToWord64Conversion(V<Word64>::Cast(left)),
                UndoWord32ToWord64Conversion(V<Word64>::Cast(right)), kind,
                WordRepresentation::Word32());
          }
        }
      }
    }
    return Next::ReduceComparison(left, right, kind, rep);
  }

  OpIndex REDUCE(Shift)(OpIndex left, OpIndex right, ShiftOp::Kind kind,
                        WordRepresentation rep) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceShift(left, right, kind, rep);
    }

    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(left);
    }

    using Kind = ShiftOp::Kind;
    uint64_t c_unsigned;
    int64_t c_signed;
    if (matcher.MatchIntegralWordConstant(left, rep, &c_unsigned, &c_signed)) {
      if (uint32_t amount;
          matcher.MatchIntegralWord32Constant(right, &amount)) {
        amount = amount & (rep.bit_width() - 1);
        switch (kind) {
          case Kind::kShiftRightArithmeticShiftOutZeros:
            if (base::bits::CountTrailingZeros(c_signed) < amount) {
              // This assumes that we never hoist operations to before their
              // original place in the control flow.
              __ Unreachable();
              return OpIndex::Invalid();
            }
            [[fallthrough]];
          case Kind::kShiftRightArithmetic:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(static_cast<int32_t>(c_signed) >>
                                         amount);
              case WordRepresentation::Word64():
                return __ Word64Constant(c_signed >> amount);
            }
          case Kind::kShiftRightLogical:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(static_cast<uint32_t>(c_unsigned) >>
                                         amount);
              case WordRepresentation::Word64():
                return __ Word64Constant(c_unsigned >> amount);
            }
          case Kind::kShiftLeft:
            return __ WordConstant(c_unsigned << amount, rep);
          case Kind::kRotateRight:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(base::bits::RotateRight32(
                    static_cast<uint32_t>(c_unsigned), amount));
              case WordRepresentation::Word64():
                return __ Word64Constant(
                    base::bits::RotateRight64(c_unsigned, amount));
            }
          case Kind::kRotateLeft:
            switch (rep.value()) {
              case WordRepresentation::Word32():
                return __ Word32Constant(base::bits::RotateLeft32(
                    static_cast<uint32_t>(c_unsigned), amount));
              case WordRepresentation::Word64():
                return __ Word64Constant(
                    base::bits::RotateLeft64(c_unsigned, amount));
            }
        }
      }
    }
    if (int32_t amount; matcher.MatchIntegralWord32Constant(right, &amount) &&
                        0 <= amount && amount < rep.bit_width()) {
      if (amount == 0) {
        return left;
      }
      if (kind == Kind::kShiftLeft) {
        // If x >> K only shifted out zeros:
        // (x >> K) << L => x           if K == L
        // (x >> K) << L => x >> (K-L) if K > L
        // (x >> K) << L => x << (L-K)  if K < L
        // Since this is used for Smi untagging, we currently only need it for
        // signed shifts.
        int k;
        OpIndex x;
        if (matcher.MatchConstantShift(
                left, &x, Kind::kShiftRightArithmeticShiftOutZeros, rep, &k)) {
          int32_t l = amount;
          if (k == l) {
            return x;
          } else if (k > l) {
            return __ ShiftRightArithmeticShiftOutZeros(
                x, __ Word32Constant(k - l), rep);
          } else if (k < l) {
            return __ ShiftLeft(x, __ Word32Constant(l - k), rep);
          }
        }
        // (x >>> K) << K => x & ~(2^K - 1)
        // (x >> K) << K => x & ~(2^K - 1)
        if (matcher.MatchConstantRightShift(left, &x, rep, &k) && k == amount) {
          return __ WordBitwiseAnd(
              x, __ WordConstant(rep.MaxUnsignedValue() << k, rep), rep);
        }
      }
      if (kind == any_of(Kind::kShiftRightArithmetic,
                         Kind::kShiftRightArithmeticShiftOutZeros)) {
        OpIndex x;
        int left_shift_amount;
        // (x << k) >> k
        if (matcher.MatchConstantShift(left, &x, ShiftOp::Kind::kShiftLeft, rep,
                                       &left_shift_amount) &&
            amount == left_shift_amount) {
          // x << (bit_width - 1) >> (bit_width - 1)  =>  0 - x  if x is 0 or 1
          if (amount == rep.bit_width() - 1 && IsBit(x)) {
            return __ WordSub(__ WordConstant(0, rep), x, rep);
          }
          // x << (bit_width - 8) >> (bit_width - 8)  =>  x  if x is within Int8
          if (amount <= rep.bit_width() - 8 && IsInt8(x)) {
            return x;
          }
          // x << (bit_width - 8) >> (bit_width - 8)  =>  x  if x is within Int8
          if (amount <= rep.bit_width() - 16 && IsInt16(x)) {
            return x;
          }
        }
      }
      if (rep == WordRepresentation::Word32() &&
          SupportedOperations::word32_shift_is_safe()) {
        // Remove the explicit 'and' with 0x1F if the shift provided by the
        // machine instruction matcher.Matches that required by JavaScript.
        if (V<Word32> a, b; matcher.MatchBitwiseAnd(
                right, &a, &b, WordRepresentation::Word32())) {
#if defined(__clang__)
          static_assert(0x1f == WordRepresentation::Word32().bit_width() - 1);
#endif
          if (uint32_t b_value;
              matcher.MatchIntegralWord32Constant(b, &b_value) &&
              b_value == 0x1f) {
            return __ Shift(left, a, kind, rep);
          }
        }
      }
    }
    return Next::ReduceShift(left, right, kind, rep);
  }

  OpIndex REDUCE(Branch)(OpIndex condition, Block* if_true, Block* if_false,
                         BranchHint hint) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceBranch(condition, if_true, if_false, hint);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    // Try to replace the Branch by a Goto.
    if (std::optional<bool> decision = MatchBoolConstant(condition)) {
      __ Goto(*decision ? if_true : if_false);
      return OpIndex::Invalid();
    }

    // Try to simplify the Branch's condition (eg, `if (x == 0) A else B` can
    // become `if (x) B else A`).
    bool negated = false;
    if (std::optional<OpIndex> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      if (negated) {
        std::swap(if_true, if_false);
        hint = NegateBranchHint(hint);
      }

      return __ ReduceBranch(new_condition.value(), if_true, if_false, hint);
    }

    goto no_change;
  }

  V<None> REDUCE(DeoptimizeIf)(V<Word32> condition, V<FrameState> frame_state,
                               bool negated,
                               const DeoptimizeParameters* parameters) {
    if (ShouldSkipOptimizationStep()) {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
    if (std::optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision != negated) {
        __ Deoptimize(frame_state, parameters);
      }
      // `DeoptimizeIf` doesn't produce a value.
      return OpIndex::Invalid();
    }
    if (std::optional<V<Word32>> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      return __ ReduceDeoptimizeIf(new_condition.value(), frame_state, negated,
                                   parameters);
    } else {
      return Next::ReduceDeoptimizeIf(condition, frame_state, negated,
                                      parameters);
    }
  }

#if V8_ENABLE_WEBASSEMBLY
  V<None> REDUCE(TrapIf)(V<Word32> condition, OptionalV<FrameState> frame_state,
                         bool negated, TrapId trap_id) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceTrapIf(condition, frame_state, negated, trap_id);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (std::optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision != negated) {
        Next::ReduceTrapIf(condition, frame_state, negated, trap_id);
        __ Unreachable();
      }
      // `TrapIf` doesn't produce a value.
      return V<None>::Invalid();
    }
    if (std::optional<V<Word32>> new_condition =
            ReduceBranchCondition(condition, &negated)) {
      return __ ReduceTrapIf(new_condition.value(), frame_state, negated,
                             trap_id);
    } else {
      goto no_change;
    }
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  V<Any> REDUCE(Select)(V<Word32> cond, V<Any> vtrue, V<Any> vfalse,
                        RegisterRepresentation rep, BranchHint hint,
                        SelectOp::Implementation implem) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSelect(cond, vtrue, vfalse, rep, hint, implem);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    // Try to remove the Select.
    if (std::optional<bool> decision = MatchBoolConstant(cond)) {
      return *decision ? vtrue : vfalse;
    }

    goto no_change;
  }

  V<None> REDUCE(StaticAssert)(V<Word32> condition, const char* source) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceStaticAssert(condition, source);
    }
    if (std::optional<bool> decision = MatchBoolConstant(condition)) {
      if (*decision) {
        // Drop the assert, the condition holds true.
        return OpIndex::Invalid();
      } else {
        // Leave the assert, as the condition is not true.
        goto no_change;
      }
    }
    goto no_change;
  }

  V<None> REDUCE(Switch)(V<Word32> input, base::Vector<SwitchOp::Case> cases,
                         Block* default_case, BranchHint default_hint) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSwitch(input, cases, default_case, default_hint);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
    if (int32_t value; matcher.MatchIntegralWord32Constant(input, &value)) {
      for (const SwitchOp::Case& if_value : cases) {
        if (if_value.value == value) {
          __ Goto(if_value.destination);
          return {};
        }
      }
      __ Goto(default_case);
      return {};
    }
    goto no_change;
  }

  OpIndex REDUCE(Store)(OpIndex base_idx, OptionalOpIndex index, OpIndex value,
                        StoreOp::Kind kind, MemoryRepresentation stored_rep,
                        WriteBarrierKind write_barrier, int32_t offset,
                        uint8_t element_scale,
                        bool maybe_initializing_or_transitioning,
                        IndirectPointerTag maybe_indirect_pointer_tag) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceStore(base_idx, index, value, kind, stored_rep,
                               write_barrier, offset, element_scale,
                               maybe_initializing_or_transitioning,
                               maybe_indirect_pointer_tag);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
#if V8_TARGET_ARCH_32_BIT
    if (kind.is_atomic && stored_rep.SizeInBytes() == 8) {
      // AtomicWord32PairOp (as used by Int64Lowering) cannot handle
      // element_scale != 0 currently.
      // TODO(jkummerow): Add support for element_scale in AtomicWord32PairOp.
      goto no_change;
    }
#endif
    if (stored_rep.SizeInBytes() <= 4) {
      value = TryRemoveWord32ToWord64Conversion(value);
    }
    index = ReduceMemoryIndex(index.value_or_invalid(), &offset, &element_scale,
                              kind.tagged_base);
    switch (stored_rep) {
      case MemoryRepresentation::Uint8():
      case MemoryRepresentation::Int8():
        value = ReduceWithTruncation(value, std::numeric_limits<uint8_t>::max(),
                                     WordRepresentation::Word32());
        break;
      case MemoryRepresentation::Uint16():
      case MemoryRepresentation::Int16():
        value =
            ReduceWithTruncation(value, std::numeric_limits<uint16_t>::max(),
                                 WordRepresentation::Word32());
        break;
      case MemoryRepresentation::Uint32():
      case MemoryRepresentation::Int32():
        value =
            ReduceWithTruncation(value, std::numeric_limits<uint32_t>::max(),
                                 WordRepresentation::Word32());
        break;
      default:
        break;
    }

    // If index is invalid and base is `left+right`, we use `left` as base and
    // `right` as index.
    if (!index.valid() && matcher.Is<Opmask::kWord64Add>(base_idx)) {
      DCHECK_EQ(element_scale, 0);
      const WordBinopOp& base = matcher.Cast<WordBinopOp>(base_idx);
      base_idx = base.left();
      index = base.right();
      // We go through the Store stack again, which might merge {index} into
      // {offset}, or just do other optimizations on this Store.
      __ Store(base_idx, index, value, kind, stored_rep, write_barrier, offset,
               element_scale, maybe_initializing_or_transitioning,
               maybe_indirect_pointer_tag);
      return OpIndex::Invalid();
    }

    return Next::ReduceStore(base_idx, index, value, kind, stored_rep,
                             write_barrier, offset, element_scale,
                             maybe_initializing_or_transitioning,
                             maybe_indirect_pointer_tag);
  }

  OpIndex REDUCE(Load)(OpIndex base_idx, OptionalOpIndex index,
                       LoadOp::Kind kind, MemoryRepresentation loaded_rep,
                       RegisterRepresentation result_rep, int32_t offset,
                       uint8_t element_scale) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceLoad(base_idx, index, kind, loaded_rep, result_rep,
                              offset, element_scale);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;
#if V8_TARGET_ARCH_32_BIT
    if (kind.is_atomic && loaded_rep.SizeInBytes() == 8) {
      // AtomicWord32PairOp (as used by Int64Lowering) cannot handle
      // element_scale != 0 currently.
      // TODO(jkummerow): Add support for element_scale in AtomicWord32PairOp.
      goto no_change;
    }
#endif

    while (true) {
      index = ReduceMemoryIndex(index.value_or_invalid(), &offset,
                                &element_scale, kind.tagged_base);
      if (!kind.tagged_base && !index.valid()) {
        if (V<WordPtr> left, right;
            matcher.MatchWordAdd(base_idx, &left, &right,
                                 WordRepresentation::WordPtr()) &&
            TryAdjustOffset(&offset, matcher.Get(right), element_scale,
                            kind.tagged_base)) {
          base_idx = left;
          continue;
        }
      }
      break;
    }

    if (!index.valid() && matcher.Is<ConstantOp>(base_idx)) {
      const ConstantOp& base = matcher.Cast<ConstantOp>(base_idx);
      if (base.kind == any_of(ConstantOp::Kind::kHeapObject,
                              ConstantOp::Kind::kCompressedHeapObject)) {
        if (offset == HeapObject::kMapOffset) {
          // Only few loads should be loading the map from a ConstantOp
          // HeapObject, so unparking the JSHeapBroker here rather than before
          // the optimization pass itself it probably more efficient.

          DCHECK_IMPLIES(
              __ data()->pipeline_kind() != TurboshaftPipelineKind::kCSA,
              broker != nullptr);
          if (broker != nullptr) {
            UnparkedScopeIfNeeded scope(broker);
            AllowHandleDereference allow_handle_dereference;
            OptionalMapRef map = TryMakeRef(broker, base.handle()->map());
            if (MapLoadCanBeConstantFolded(map)) {
              return __ HeapConstant(map->object());
            }
          }
        }
        // TODO(dmercadier): consider constant-folding other accesses, in
        // particular for constant objects (ie, if
        // base.handle()->InReadOnlySpace() is true). We have to be a bit
        // careful though, because loading could be invalid (since we could
        // be in unreachable code). (all objects have a map, so loading the map
        // should always be safe, regardless of whether we are generating
        // unreachable code or not)
      }
    }

    // If index is invalid and base is `left+right`, we use `left` as base and
    // `right` as index.
    if (!index.valid() && matcher.Is<Opmask::kWord64Add>(base_idx)) {
      DCHECK_EQ(element_scale, 0);
      const WordBinopOp& base = matcher.Cast<WordBinopOp>(base_idx);
      base_idx = base.left();
      index = base.right();
      // We go through the Load stack again, which might merge {index} into
      // {offset}, or just do other optimizations on this Load.
      return __ Load(base_idx, index, kind, loaded_rep, result_rep, offset,
                     element_scale);
    }

    return Next::ReduceLoad(base_idx, index, kind, loaded_rep, result_rep,
                            offset, element_scale);
  }

#if V8_ENABLE_WEBASSEMBLY
#ifdef V8_TARGET_ARCH_ARM64
  V<Any> REDUCE(Simd128ExtractLane)(V<Simd128> input,
                                    Simd128ExtractLaneOp::Kind kind,
                                    uint8_t lane) {
    LABEL_BLOCK(no_change) {
      return Next::ReduceSimd128ExtractLane(input, kind, lane);
    }
    if (ShouldSkipOptimizationStep()) goto no_change;

    // Turbofan and the RecreateSchedulePhase don't support the optimized
    // reduce operation.
    if (!v8_flags.turboshaft_wasm_instruction_selection_staged) goto no_change;

    if (lane != 0) {
      goto no_change;
    }

    const Simd128BinopOp* binop = matcher.TryCast<Simd128BinopOp>(input);
    if (!binop) {
      goto no_change;
    }

    // Support pairwise addition: int and fp.
    switch (binop->kind) {
      default:
        goto no_change;
      case Simd128BinopOp::Kind::kI8x16Add:
      case Simd128BinopOp::Kind::kI16x8Add:
      case Simd128BinopOp::Kind::kI32x4Add:
      case Simd128BinopOp::Kind::kF32x4Add:
      case Simd128BinopOp::Kind::kI64x2Add:
      case Simd128BinopOp::Kind::kF64x2Add:
        break;
    }

    auto MatchUnaryShuffle =
        [this](V<Simd128> maybe_shuffle) -> const Simd128ShuffleOp* {
      if (const Simd128ShuffleOp* shuffle =
              matcher.TryCast<Simd128ShuffleOp>(maybe_shuffle)) {
        if (shuffle->left() == shuffle->right()) {
          return shuffle;
        }
      }
      return nullptr;
    };

    auto MatchBinop =
        [this](
            V<Simd128> maybe_binop,
            Simd128BinopOp::Kind required_binop_kind) -> const Simd128BinopOp* {
      if (const Simd128BinopOp* binop =
              matcher.TryCast<Simd128BinopOp>(maybe_binop)) {
        if (required_binop_kind == binop->kind) {
          return binop;
        }
      }
      return nullptr;
    };

    // We're going to look for vector reductions performed with
    // shuffles and binops. The TS operations are defined as pairwise
    // to map well onto hardware, although the ordering is only
    // important for FP operations. For an example of the Word32
    // UpperToLower case:
    //
    // input    = (V<Simd128>)
    // shuffle1 = (Simd128ShuffleOp input, input, [ 2, 3, X, X])
    // add1     = (Simd128BinopOp input, shuffle1)
    // shuffle2 = (Simd128ShuffleOp add1, add1, [1, X, X, X])
    // add2     = (Simd128BinopOp add1, shuffle2)
    // result   = (ExtractLaneOp add2, 0)

    // Walk up from binop to discover the tree of binops and shuffles:
    // (extract (binop (binop (reduce_input), shuffle), shuffle), 0)
    base::SmallVector<const Simd128ShuffleOp*, 4> shuffles;
    base::SmallVector<const Simd128BinopOp*, 4> binops;
    binops.push_back(binop);
    while (!binops.empty()) {
      const Simd128BinopOp* binop = binops.back();
      binops.pop_back();
      V<Simd128> operands[2] = {binop->left(), binop->right()};
      for (unsigned i = 0; i < 2; ++i) {
        V<Simd128> operand = operands[i];
        if (const Simd128ShuffleOp* shuffle = MatchUnaryShuffle(operand)) {
          // Ensure that the input to the shuffle is also the other input to
          // current binop.
          V<Simd128> shuffle_in = shuffle->left();
          DCHECK_EQ(shuffle_in, shuffle->right());
          V<Simd128> other_operand = operands[i ^ 1];
          if (shuffle_in != other_operand) {
            break;
          }
          shuffles.push_back(shuffle);
          if (const Simd128BinopOp* other_binop =
                  MatchBinop(shuffle_in, binop->kind)) {
            binops.push_back(other_binop);
            break;
          }
        }
      }
    }
    if (shuffles.empty()) {
      goto no_change;
    }

    // Reverse so that they're in execution order, just for readability.
    std::reverse(shuffles.begin(), shuffles.end());
    V<Simd128> reduce_input = shuffles.front()->left();
    MachineRepresentation rep = Simd128ExtractLaneOp::element_rep(kind);
    switch (rep) {
      default:
        goto no_change;
      case MachineRepresentation::kWord8: {
        if (shuffles.size() == 4) {
          const uint8_t* shuffle1 = shuffles[0]->shuffle;
          const uint8_t* shuffle2 = shuffles[1]->shuffle;
          const uint8_t* shuffle3 = shuffles[2]->shuffle;
          const uint8_t* shuffle4 = shuffles[3]->shuffle;
          if (wasm::SimdShuffle::TryMatch8x16UpperToLowerReduce(
                  shuffle1, shuffle2, shuffle3, shuffle4)) {
            V<Simd128> reduce = __ Simd128Reduce(
                reduce_input, Simd128ReduceOp::Kind::kI8x16AddReduce);
            return __ Simd128ExtractLane(reduce, kind, 0);
          }
        }
        break;
      }
      case MachineRepresentation::kWord16: {
        if (shuffles.size() == 3) {
          const uint8_t* shuffle1 = shuffles[0]->shuffle;
          const uint8_t* shuffle2 = shuffles[1]->shuffle;
          const uint8_t* shuffle3 = shuffles[2]->shuffle;
          if (wasm::SimdShuffle::TryMatch16x8UpperToLowerReduce(
                  shuffle1, shuffle2, shuffle3)) {
            V<Simd128> reduce = __ Simd128Reduce(
                reduce_input, Simd128ReduceOp::Kind::kI16x8AddReduce);
            return __ Simd128ExtractLane(reduce, kind, 0);
          }
        }
        break;
      }
      case MachineRepresentation::kWord32: {
        if (shuffles.size() == 2) {
          const uint8_t* shuffle1 = shuffles[0]->shuffle;
          const uint8_t* shuffle2 = shuffles[1]->shuffle;
          if (wasm::SimdShuffle::TryMatch32x4UpperToLowerReduce(shuffle1,
                                                                shuffle2)) {
            V<Simd128> reduce = __ Simd128Reduce(
                reduce_input, Simd128ReduceOp::Kind::kI32x4AddReduce);
            return __ Simd128ExtractLane(reduce, kind, 0);
          }
        }
        break;
      }
      case MachineRepresentation::kFloat32: {
        if (shuffles.size() == 2) {
          const uint8_t* shuffle1 = shuffles[0]->shuffle;
          const uint8_t* shuffle2 = shuffles[1]->shuffle;
          if (wasm::SimdShuffle::TryMatch32x4PairwiseReduce(shuffle1,
                                                            shuffle2)) {
            V<Simd128> reduce = __ Simd128Reduce(
                reduce_input, Simd128ReduceOp::Kind::kF32x4AddReduce);
            return __ Simd128ExtractLane(reduce, kind, 0);
          }
        }
        break;
      }
      case MachineRepresentation::kWord64:
      case MachineRepresentation::kFloat64: {
        if (shuffles.size() == 1) {
          uint8_t shuffle64x2[2];
          if (wasm::SimdShuffle::TryMatch64x2Shuffle(shuffles[0]->shuffle,
                                                     shuffle64x2) &&
              wasm::SimdShuffle::TryMatch64x2Reduce(shuffle64x2)) {
            V<Simd128> reduce =
                rep == MachineRepresentation::kWord64
                    ? __ Simd128Reduce(reduce_input,
                                       Simd128ReduceOp::Kind::kI64x2AddReduce)
                    : __ Simd128Reduce(reduce_input,
                                       Simd128ReduceOp::Kind::kF64x2AddReduce);
            return __ Simd128ExtractLane(reduce, kind, 0);
          }
        }
        break;
      }
    }
    goto no_change;
  }
#endif  // V8_TARGET_ARCH_ARM64
#endif  // V8_ENABLE_WEBASSEMBLY

 private:
  V<Word32> ReduceCompareEqual(V<Any> left, V<Any> right,
                               RegisterRepresentation rep) {
    if (left == right && !rep.IsFloat()) {
      return __ Word32Constant(1);
    }
    if (rep == WordRepresentation::Word32()) {
      left = TryRemoveWord32ToWord64Conversion(V<Word>::Cast(left));
      right = TryRemoveWord32ToWord64Conversion(V<Word>::Cast(right));
    }
    if (matcher.Is<ConstantOp>(left) && !matcher.Is<ConstantOp>(right)) {
      return ReduceCompareEqual(right, left, rep);
    }
    if (matcher.Is<ConstantOp>(right)) {
      if (matcher.Is<ConstantOp>(left)) {
        // k1 == k2  =>  k
        switch (rep.value()) {
          case RegisterRepresentation::Word32():
          case RegisterRepresentation::Word64(): {
            if (uint64_t k1, k2; matcher.MatchIntegralWordConstant(
                                     left, WordRepresentation(rep), &k1) &&
                                 matcher.MatchIntegralWordConstant(
                                     right, WordRepresentation(rep), &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Float32(): {
            if (float k1, k2; matcher.MatchFloat32Constant(left, &k1) &&
                              matcher.MatchFloat32Constant(right, &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Float64(): {
            if (double k1, k2; matcher.MatchFloat64Constant(left, &k1) &&
                               matcher.MatchFloat64Constant(right, &k2)) {
              return __ Word32Constant(k1 == k2);
            }
            break;
          }
          case RegisterRepresentation::Tagged(): {
            if (Handle<HeapObject> o1, o2;
                matcher.MatchHeapConstant(left, &o1) &&
                matcher.MatchHeapConstant(right, &o2)) {
              return __ Word32Constant(o1.address() == o2.address());
            }
            break;
          }
          default:
            UNREACHABLE();
        }
      }
      if (rep.IsWord()) {
        WordRepresentation rep_w{rep};
        // x - y == 0  =>  x == y
        if (V<Word> x, y; matcher.MatchWordSub(left, &x, &y, rep_w) &&
                          matcher.MatchZero(right)) {
          return ReduceCompareEqual(x, y, rep);
        }
        {
          //     ((x >> shift_amount) & mask) == k
          // =>  (x & (mask << shift_amount)) == (k << shift_amount)
          V<Word> shift, x, mask_op;
          int shift_amount;
          uint64_t mask, k;
          if (matcher.MatchBitwiseAnd(left, &shift, &mask_op, rep_w) &&
              matcher.MatchConstantRightShift(shift, &x, rep_w,
                                              &shift_amount) &&
              matcher.MatchIntegralWordConstant(mask_op, rep_w, &mask) &&
              matcher.MatchIntegralWordConstant(right, rep_w, &k) &&
              mask <= rep.MaxUnsignedValue() >> shift_amount &&
              k <= rep.MaxUnsignedValue() >> shift_amount) {
            return ReduceCompareEqual(
                __ WordBitwiseAnd(
                    x, __ WordConstant(mask << shift_amount, rep_w), rep_w),
                __ WordConstant(k << shift_amount, rep_w), rep_w);
          }
        }
        {
          // (x >> k1) == k2  =>  x == (k2 << k1)  if shifts reversible
          // Only perform the transformation if the shift is not used yet, to
          // avoid keeping both the shift and x alive.
          V<Word> x;
          uint16_t k1;
          int64_t k2;
          if (matcher.MatchConstantShiftRightArithmeticShiftOutZeros(
                  left, &x, rep_w, &k1) &&
              matcher.MatchIntegralWordConstant(right, rep_w, &k2) &&
              CountLeadingSignBits(k2, rep_w) > k1 &&
              matcher.Get(left).saturated_use_count.IsZero()) {
            return __ Equal(
                x, __ WordConstant(base::bits::Unsigned(k2) << k1, rep_w),
                rep_w);
          }
        }
        // Map 64bit to 32bit equals.
        if (rep_w == WordRepresentation::Word64()) {
          std::optional<bool> left_sign_extended;
          std::optional<bool> right_sign_extended;
          if (IsWord32ConvertedToWord64(left, &left_sign_extended) &&
              IsWord32ConvertedToWord64(right, &right_sign_extended)) {
            if (left_sign_extended == right_sign_extended) {
              return __ Equal(
                  UndoWord32ToWord64Conversion(V<Word64>::Cast(left)),
                  UndoWord32ToWord64Conversion(V<Word64>::Cast(right)),
                  WordRepresentation::Word32());
            }
          }
        }
      }
    }
    return Next::ReduceComparison(left, right, ComparisonOp::Kind::kEqual, rep);
  }

  // Try to match a constant and add it to `offset`. Return `true` if
  // successful.
  bool TryAdjustOffset(int32_t* offset, const Operation& maybe_constant,
                       uint8_t element_scale, bool tagged_base) {
    if (!maybe_constant.Is<ConstantOp>()) return false;
    const ConstantOp& constant = maybe_constant.Cast<ConstantOp>();
    if (constant.rep != WordRepresentation::WordPtr() ||
        !constant.IsIntegral()) {
      // This can only happen in unreachable code. Ideally, we identify this
      // situation and use `__ Unreachable()`. However, this is difficult to
      // do from within this helper, so we just don't perform the reduction.
      return false;
    }
    int64_t diff = constant.signed_integral();
    int32_t new_offset;
    if (diff <= (std::numeric_limits<int32_t>::max() >> element_scale) &&
        diff >= (std::numeric_limits<int32_t>::min() >> element_scale) &&
        !base::bits::SignedAddOverflow32(
            *offset,
            static_cast<int32_t>(base::bits::Unsigned(diff) << element_scale),
            &new_offset) &&
        LoadOp::OffsetIsValid(new_offset, tagged_base)) {
      *offset = new_offset;
      return true;
    }
    return false;
  }

  bool TryAdjustIndex(int32_t offset, OpIndex* index,
                      const Operation& maybe_constant, uint8_t element_scale) {
    if (!maybe_constant.Is<ConstantOp>()) return false;
    const ConstantOp& constant = maybe_constant.Cast<ConstantOp>();
    if (constant.rep != WordRepresentation::WordPtr() ||
        !constant.IsIntegral()) {
      // This can only happen in unreachable code. Ideally, we identify this
      // situation and use `__ Unreachable()`. However, this is difficult to
      // do from within this helper, so we just don't perform the reduction.
      return false;
    }
    int64_t diff = constant.signed_integral();
    int64_t new_index;
    if (!base::bits::SignedAddOverflow64(offset, diff << element_scale,
                                         &new_index)) {
      *index = __ IntPtrConstant(new_index);
      return true;
    }
    return false;
  }

  bool TryAdjustElementScale(uint8_t* element_scale, OpIndex maybe_constant) {
    uint64_t diff;
    if (!matcher.MatchIntegralWordConstant(
            maybe_constant, WordRepresentation::WordPtr(), &diff)) {
      return false;
    }
    DCHECK_LT(*element_scale, WordRepresentation::WordPtr().bit_width());
    if (diff < (WordRepresentation::WordPtr().bit_width() -
                uint64_t{*element_scale})) {
      *element_scale += diff;
      return true;
    }
    return false;
  }

  // Fold away operations in the computation of `index` while preserving the
  // value of `(index << element_scale) + offset)` by updating `offset`,
  // `element_scale` and returning the updated `index`.
  // Return `OpIndex::Invalid()` if the resulting index is zero.
  OpIndex ReduceMemoryIndex(OpIndex index, int32_t* offset,
                            uint8_t* element_scale, bool tagged_base) {
    while (index.valid()) {
      const Operation& index_op = matcher.Get(index);
      if (TryAdjustOffset(offset, index_op, *element_scale, tagged_base)) {
        index = OpIndex::Invalid();
        *element_scale = 0;
      } else if (TryAdjustIndex(*offset, &index, index_op, *element_scale)) {
        *element_scale = 0;
        *offset = 0;
        // This function cannot optimize the index further since at this point
        // it's just a WordPtrConstant.
        return index;
      } else if (const ShiftOp* shift_op = index_op.TryCast<ShiftOp>()) {
        if (shift_op->kind == ShiftOp::Kind::kShiftLeft &&
            TryAdjustElementScale(element_scale, shift_op->right())) {
          index = shift_op->left();
          continue;
        }
      } else if (const WordBinopOp* binary_op =
                     index_op.TryCast<WordBinopOp>()) {
        // TODO(jkummerow): This doesn't trigger for wasm32 memory operations
        // on 64-bit platforms, because `index_op` is a `Change` (from uint32
        // to uint64) in that case, and that Change's input is the addition
        // we're looking for. When we fix that, we must also teach the x64
        // instruction selector to support xchg with index *and* offset.
        if (binary_op->kind == WordBinopOp::Kind::kAdd &&
            TryAdjustOffset(offset, matcher.Get(binary_op->right()),
                            *element_scale, tagged_base)) {
          index = binary_op->left();
          continue;
        }
      }
      break;
    }
    return index;
  }

  bool IsFloat32ConvertedToFloat64(OpIndex value) {
    if (OpIndex input;
        matcher.MatchChange(value, &input, ChangeOp::Kind::kFloatConversion,
                            RegisterRepresentation::Float32(),
                            RegisterRepresentation::Float64())) {
      return true;
    }
    if (double c;
        matcher.MatchFloat64Constant(value, &c) && DoubleToFloat32(c) == c) {
      return true;
    }
    return false;
  }

  OpIndex UndoFloat32ToFloat64Conversion(OpIndex value) {
    if (OpIndex input;
        matcher.MatchChange(value, &input, ChangeOp::Kind::kFloatConversion,
                            RegisterRepresentation::Float32(),
                            RegisterRepresentation::Float64())) {
      return input;
    }
    if (double c;
        matcher.MatchFloat64Constant(value, &c) && DoubleToFloat32(c) == c) {
      return __ Float32Constant(DoubleToFloat32(c));
    }
    UNREACHABLE();
  }

  bool IsBit(OpIndex value) { return matcher.Is<ComparisonOp>(value); }

  bool IsInt8(OpIndex value) {
    if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == MemoryRepresentation::Int8();
    } else if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == MemoryRepresentation::Int8();
    }
    return false;
  }

  bool IsInt16(OpIndex value) {
    if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == any_of(MemoryRepresentation::Int16(),
                                      MemoryRepresentation::Int8());
    } else if (auto* op = matcher.TryCast<LoadOp>(value)) {
      return op->loaded_rep == any_of(MemoryRepresentation::Int16(),
                                      MemoryRepresentation::Int8());
    }
    return false;
  }

  bool IsWord32ConvertedToWord64(OpIndex value,
                                 std::optional<bool>* sign_extended = nullptr) {
    if (const ChangeOp* change_op = matcher.TryCast<ChangeOp>(value)) {
      if (change_op->from == WordRepresentation::Word32() &&
          change_op->to == WordRepresentation::Word64()) {
        if (change_op->kind == ChangeOp::Kind::kSignExtend) {
          if (sign_extended) *sign_extended = true;
          return true;
        } else if (change_op->kind == ChangeOp::Kind::kZeroExtend) {
          if (sign_extended) *sign_extended = false;
          return true;
        }
      }
    }
    if (int64_t c; matcher.MatchIntegralWord64Constant(value, &c) &&
                   c >= std::numeric_limits<int32_t>::min()) {
      if (c < 0) {
        if (sign_extended) *sign_extended = true;
        return true;
      } else if (c <= std::numeric_limits<int32_t>::max()) {
        // Sign- and zero-extension produce the same result.
        if (sign_extended) *sign_extended = {};
        return true;
      } else if (c <= std::numeric_limits<uint32_t>::max()) {
        if (sign_extended) *sign_extended = false;
        return true;
      }
    }
    return false;
  }

  V<Word32> UndoWord32ToWord64Conversion(V<Word> value) {
    DCHECK(IsWord32ConvertedToWord64(value));
    if (const ChangeOp* op = matcher.TryCast<ChangeOp>(value)) {
      return V<Word32>::Cast(op->input());
    }
    return __ Word32Constant(matcher.Cast<ConstantOp>(value).word32());
  }

  V<Word> TryRemoveWord32ToWord64Conversion(V<Word> value) {
    if (const ChangeOp* op = matcher.TryCast<ChangeOp>(value)) {
      if (op->from == WordRepresentation::Word32() &&
          op->to == WordRepresentation::Word64() &&
          op->kind == any_of(ChangeOp::Kind::kZeroExtend,
                             ChangeOp::Kind::kSignExtend)) {
        return V<Word32>::Cast(op->input());
      }
    }
    return value;
  }

  uint64_t TruncateWord(uint64_t value, WordRepresentation rep) {
    if (rep == WordRepresentation::Word32()) {
      return static_cast<uint32_t>(value);
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return value;
    }
  }

  // Reduce the given value under the assumption that only the bits set in
  // `truncation_mask` will be observed.
  V<Word> ReduceWithTruncation(V<Word> value, uint64_t truncation_mask,
                               WordRepresentation rep) {
    {  // Remove bitwise-and with a mask whose zero-bits are not observed.
      V<Word> input, mask;
      uint64_t mask_value;
      if (matcher.MatchBitwiseAnd(value, &input, &mask, rep) &&
          matcher.MatchIntegralWordConstant(mask, rep, &mask_value)) {
        if ((mask_value & truncation_mask) == truncation_mask) {
          return ReduceWithTruncation(input, truncation_mask, rep);
        }
      }
    }
    {
      int left_shift_amount;
      int right_shift_amount;
      WordRepresentation rep;
      V<Word> left_shift;
      ShiftOp::Kind right_shift_kind;
      V<Word> left_shift_input;
      if (matcher.MatchConstantShift(value, &left_shift, &right_shift_kind,
                                     &rep, &right_shift_amount) &&
          ShiftOp::IsRightShift(right_shift_kind) &&
          matcher.MatchConstantShift(left_shift, &left_shift_input,
                                     ShiftOp::Kind::kShiftLeft, rep,
                                     &left_shift_amount) &&
          ((rep.MaxUnsignedValue() >> right_shift_amount) & truncation_mask) ==
              truncation_mask) {
        if (left_shift_amount == right_shift_amount) {
          return left_shift_input;
        } else if (left_shift_amount < right_shift_amount) {
          OpIndex shift_amount =
              __ WordConstant(right_shift_amount - left_shift_amount, rep);
          return __ Shift(left_shift_input, shift_amount, right_shift_kind,
                          rep);
        } else if (left_shift_amount > right_shift_amount) {
          OpIndex shift_amount =
              __ WordConstant(left_shift_amount - right_shift_amount, rep);
          return __ Shift(left_shift_input, shift_amount,
                          ShiftOp::Kind::kShiftLeft, rep);
        }
      }
    }
    return value;
  }

  OpIndex ReduceSignedDiv(OpIndex left, int64_t right, WordRepresentation rep) {
    // left / -1 => 0 - left
    if (right == -1) {
      return __ WordSub(__ WordConstant(0, rep), left, rep);
    }
    // left / 0 => 0
    if (right == 0) {
      return __ WordConstant(0, rep);
    }
    // left / 1 => left
    if (right == 1) {
      return left;
    }
    // left / MinSignedValue  =>  left == MinSignedValue
    if (right == rep.MinSignedValue()) {
      OpIndex equal_op = __ Equal(left, __ WordConstant(right, rep), rep);
      return rep == WordRepresentation::Word64()
                 ? __ ChangeUint32ToUint64(equal_op)
                 : equal_op;
    }
    // left / -right  => -(left / right)
    if (right < 0) {
      DCHECK_NE(right, rep.MinSignedValue());
      return __ WordSub(__ WordConstant(0, rep),
                        ReduceSignedDiv(left, Abs(right), rep), rep);
    }

    OpIndex quotient = left;
    if (base::bits::IsPowerOfTwo(right)) {
      uint32_t shift = base::bits::WhichPowerOfTwo(right);
      DCHECK_GT(shift, 0);
      if (shift > 1) {
        quotient = __ ShiftRightArithmetic(quotient, rep.bit_width() - 1, rep);
      }
      quotient = __ ShiftRightLogical(quotient, rep.bit_width() - shift, rep);
      quotient = __ WordAdd(quotient, left, rep);
      quotient = __ ShiftRightArithmetic(quotient, shift, rep);
      return quotient;
    }
    DCHECK_GT(right, 0);
    // Compute the magic number for `right`, using a generic lambda to treat
    // 32- and 64-bit uniformly.
    auto LowerToMul = [this, left](auto right, WordRepresentation rep) {
      base::MagicNumbersForDivision<decltype(right)> magic =
          base::SignedDivisionByConstant(right);
      OpIndex quotient = __ IntMulOverflownBits(
          left, __ WordConstant(magic.multiplier, rep), rep);
      if (magic.multiplier < 0) {
        quotient = __ WordAdd(quotient, left, rep);
      }
      OpIndex sign_bit = __ ShiftRightLogical(left, rep.bit_width() - 1, rep);
      return __ WordAdd(__ ShiftRightArithmetic(quotient, magic.shift, rep),
                        sign_bit, rep);
    };
    if (rep == WordRepresentation::Word32()) {
      return LowerToMul(static_cast<int32_t>(right),
                        WordRepresentation::Word32());
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return LowerToMul(static_cast<int64_t>(right),
                        WordRepresentation::Word64());
    }
  }

  OpIndex ReduceUnsignedDiv(OpIndex left, uint64_t right,
                            WordRepresentation rep) {
    // left / 0 => 0
    if (right == 0) {
      return __ WordConstant(0, rep);
    }
    // left / 1 => left
    if (right == 1) {
      return left;
    }
    // left / 2^k  => left >> k
    if (base::bits::IsPowerOfTwo(right)) {
      return __ ShiftRightLogical(left, base::bits::WhichPowerOfTwo(right),
                                  rep);
    }
    DCHECK_GT(right, 0);
    // If `right` is even, we can avoid using the expensive fixup by
    // shifting `left` upfront.
    unsigned const shift = base::bits::CountTrailingZeros(right);
    left = __ ShiftRightLogical(left, shift, rep);
    right >>= shift;
    // Compute the magic number for `right`, using a generic lambda to treat
    // 32- and 64-bit uniformly.
    auto LowerToMul = [this, left, shift](auto right, WordRepresentation rep) {
      base::MagicNumbersForDivision<decltype(right)> const mag =
          base::UnsignedDivisionByConstant(right, shift);
      OpIndex quotient = __ UintMulOverflownBits(
          left, __ WordConstant(mag.multiplier, rep), rep);
      if (mag.add) {
        DCHECK_GE(mag.shift, 1);
        // quotient = (((left - quotient) >> 1) + quotient) >> (mag.shift -
        // 1)
        quotient = __ ShiftRightLogical(
            __ WordAdd(
                __ ShiftRightLogical(__ WordSub(left, quotient, rep), 1, rep),
                quotient, rep),
            mag.shift - 1, rep);
      } else {
        quotient = __ ShiftRightLogical(quotient, mag.shift, rep);
      }
      return quotient;
    };
    if (rep == WordRepresentation::Word32()) {
      return LowerToMul(static_cast<uint32_t>(right),
                        WordRepresentation::Word32());
    } else {
      DCHECK_EQ(rep, WordRepresentation::Word64());
      return LowerToMul(static_cast<uint64_t>(right),
                        WordRepresentation::Word64());
    }
  }

  std::optional<V<Word32>> ReduceBranchCondition(V<Word32> condition,
                                                 bool* negated) {
    // TODO(dmercadier): consider generalizing this function both Word32 and
    // Word64.
    bool reduced = false;
    while (true) {
      // x == 0  =>  x with flipped branches
      if (V<Word32> left, right; matcher.MatchEqual(condition, &left, &right) &&
                                 matcher.MatchZero(right)) {
        reduced = true;
        condition = left;
        *negated = !*negated;
        continue;
      }
      // x - y  =>  x == y with flipped branches
      if (V<Word32> left, right; matcher.MatchWordSub(
              condition, &left, &right, WordRepresentation::Word32())) {
        reduced = true;
        condition = __ Word32Equal(left, right);
        *negated = !*negated;
        continue;
      }
      // x & (1 << k) == (1 << k)  =>  x & (1 << k)
      if (V<Word32> left, right; matcher.MatchEqual(condition, &left, &right)) {
        V<Word32> x, mask;
        uint32_t k1, k2;
        if (matcher.MatchBitwiseAnd(left, &x, &mask,
                                    WordRepresentation::Word32()) &&
            matcher.MatchIntegralWord32Constant(mask, &k1) &&
            matcher.MatchIntegralWord32Constant(right, &k2) && k1 == k2 &&
            base::bits::IsPowerOfTwo(k1)) {
          reduced = true;
          condition = left;
          continue;
        }
      }
      // (x >> k1) & k2   =>   x & (k2 << k1)
      {
        V<Word32> shift, k2_index, x;
        int k1_int;
        uint32_t k1, k2;
        if (matcher.MatchBitwiseAnd(condition, &shift, &k2_index,
                                    WordRepresentation::Word32()) &&
            matcher.MatchConstantRightShift(
                shift, &x, WordRepresentation::Word32(), &k1_int) &&
            matcher.MatchIntegralWord32Constant(k2_index, &k2)) {
          k1 = static_cast<uint32_t>(k1_int);
          if (k1 <= base::bits::CountLeadingZeros(k2) &&
              (static_cast<uint64_t>(k2) << k1 <=
               std::numeric_limits<uint32_t>::max())) {
            return __ Word32BitwiseAnd(x, k2 << k1);
          }
        }
      }
      // Select(x, true, false) => x
      if (const SelectOp* select = matcher.TryCast<SelectOp>(condition)) {
        auto left_val = MatchBoolConstant(select->vtrue());
        auto right_val = MatchBoolConstant(select->vfalse());
        if (left_val && right_val) {
          if (*left_val == *right_val) {
            // Select(x, v, v) => v
            return __ Word32Constant(*left_val);
          }
          if (*left_val == false) {
            // Select(x, false, true) => !x
            *negated = !*negated;
          }
          condition = select->cond();
          reduced = true;
          continue;
        }
      }
      break;
    }
    return reduced ? std::optional<V<Word32>>(condition) : std::nullopt;
  }

  std::optional<bool> MatchBoolConstant(OpIndex condition) {
    if (uint32_t value;
        matcher.MatchIntegralWord32Constant(condition, &value)) {
      return value != 0;
    }
    return std::nullopt;
  }

  // Returns true if loading the map of an object with map {map} can be constant
  // folded and done at compile time or not. For instance, doing this for
  // strings is not safe, since the map of a string could change during a GC,
  // but doing this for a HeapNumber is always safe.
  bool MapLoadCanBeConstantFolded(OptionalMapRef map) {
    if (!map.has_value()) return false;

    if (map->IsJSObjectMap() && map->is_stable()) {
      broker->dependencies()->DependOnStableMap(*map);
      // For JS objects, this is only safe is the map is stable.
      return true;
    }

    if (map->instance_type() ==
        any_of(BIG_INT_BASE_TYPE, HEAP_NUMBER_TYPE, ODDBALL_TYPE)) {
      return true;
    }

    return false;
  }

  static constexpr bool IsNegativePowerOfTwo(int64_t x) {
    if (x >= 0) return false;
    if (x == std::numeric_limits<int64_t>::min()) return true;
    int64_t x_abs = -x;   // This can't overflow after the check above.
    DCHECK_GE(x_abs, 1);  // The subtraction below can't underflow.
    return (x_abs & (x_abs - 1)) == 0;
  }

  static constexpr uint16_t CountLeadingSignBits(int64_t c,
                                                 WordRepresentation rep) {
    return base::bits::CountLeadingSignBits(c) - (64 - rep.bit_width());
  }

  JSHeapBroker* broker = __ data() -> broker();
  const OperationMatcher& matcher = __ matcher();
#if V8_ENABLE_WEBASSEMBLY
  const bool signalling_nan_possible = __ data() -> is_wasm();
#else
  static constexpr bool signalling_nan_possible = false;
#endif  // V8_ENABLE_WEBASSEMBLY
};

#include "src/compiler/turboshaft/undef-assembler-macros.inc"

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_MACHINE_OPTIMIZATION_REDUCER_H_
