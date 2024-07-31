// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_
#define V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_

#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

class OperationMatcher {
 public:
  explicit OperationMatcher(Graph& graph) : graph_(graph) {}

  template <class Op>
  bool Is(OpIndex op_idx) const {
    return graph_.Get(op_idx).Is<Op>();
  }

  template <class Op>
  const underlying_operation_t<Op>* TryCast(OpIndex op_idx) const {
    return graph_.Get(op_idx).TryCast<Op>();
  }

  template <class Op>
  const underlying_operation_t<Op>& Cast(OpIndex op_idx) const {
    return graph_.Get(op_idx).Cast<Op>();
  }

  const Operation& Get(OpIndex op_idx) const { return graph_.Get(op_idx); }

  OpIndex Index(const Operation& op) const { return graph_.Index(op); }

  bool MatchZero(OpIndex matched) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
        return op->integral() == 0;
      case ConstantOp::Kind::kFloat32:
        return op->float32() == 0;
      case ConstantOp::Kind::kFloat64:
        return op->float64() == 0;
      default:
        return false;
    }
  }

  bool MatchIntegralZero(OpIndex matched) const {
    int64_t constant;
    return MatchSignedIntegralConstant(matched, &constant) && constant == 0;
  }

  bool MatchFloat32Constant(OpIndex matched, float* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat32) return false;
    *constant = op->float32();
    return true;
  }

  bool MatchFloat64Constant(OpIndex matched, double* constant) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kFloat64) return false;
    *constant = op->float64();
    return true;
  }

  bool MatchFloat(OpIndex matched, double* value) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind == ConstantOp::Kind::kFloat64) {
      *value = op->float64();
      return true;
    } else if (op->kind == ConstantOp::Kind::kFloat32) {
      *value = op->float32();
      return true;
    }
    return false;
  }

  bool MatchFloat(OpIndex matched, double value) const {
    double k;
    if (!MatchFloat(matched, &k)) return false;
    return base::bit_cast<uint64_t>(value) == base::bit_cast<uint64_t>(k) ||
           (std::isnan(k) && std::isnan(value));
  }

  bool MatchNaN(OpIndex matched) const {
    double k;
    return MatchFloat(matched, &k) && std::isnan(k);
  }

  bool MatchTaggedConstant(OpIndex matched, Handle<HeapObject>* tagged) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (!(op->kind == any_of(ConstantOp::Kind::kHeapObject,
                             ConstantOp::Kind::kCompressedHeapObject))) {
      return false;
    }
    *tagged = op->handle();
    return true;
  }

  bool MatchIntegralWordConstant(OpIndex matched, WordRepresentation rep,
                                 uint64_t* unsigned_constant,
                                 int64_t* signed_constant = nullptr) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    switch (op->kind) {
      case ConstantOp::Kind::kWord32:
      case ConstantOp::Kind::kWord64:
      case ConstantOp::Kind::kRelocatableWasmCall:
      case ConstantOp::Kind::kRelocatableWasmStubCall:
        if (rep.value() == WordRepresentation::Word32()) {
          if (unsigned_constant) {
            *unsigned_constant = static_cast<uint32_t>(op->integral());
          }
          if (signed_constant) {
            *signed_constant = static_cast<int32_t>(op->signed_integral());
          }
          return true;
        } else if (rep.value() == WordRepresentation::Word64()) {
          if (unsigned_constant) {
            *unsigned_constant = op->integral();
          }
          if (signed_constant) {
            *signed_constant = op->signed_integral();
          }
          return true;
        }
        return false;
      default:
        return false;
    }
    UNREACHABLE();
  }

  bool MatchIntegralWordConstant(OpIndex matched, WordRepresentation rep,
                                 int64_t* signed_constant) const {
    return MatchIntegralWordConstant(matched, rep, nullptr, signed_constant);
  }

  bool MatchIntegralWord64Constant(OpIndex matched, uint64_t* constant) const {
    return MatchIntegralWordConstant(matched, WordRepresentation::Word64(),
                                     constant);
  }

  bool MatchIntegralWord32Constant(OpIndex matched, uint32_t* constant) const {
    if (uint64_t value; MatchIntegralWordConstant(
            matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<uint32_t>(value);
      return true;
    }
    return false;
  }

  bool MatchIntegralWord64Constant(OpIndex matched, int64_t* constant) const {
    return MatchIntegralWordConstant(matched, WordRepresentation::Word64(),
                                     constant);
  }

  bool MatchIntegralWord32Constant(OpIndex matched, int32_t* constant) const {
    if (int64_t value; MatchIntegralWordConstant(
            matched, WordRepresentation::Word32(), &value)) {
      *constant = static_cast<int32_t>(value);
      return true;
    }
    return false;
  }

  bool MatchSignedIntegralConstant(OpIndex matched, int64_t* constant) const {
    if (const ConstantOp* c = TryCast<ConstantOp>(matched)) {
      if (c->kind == ConstantOp::Kind::kWord32 ||
          c->kind == ConstantOp::Kind::kWord64) {
        *constant = c->signed_integral();
        return true;
      }
    }
    return false;
  }

  bool MatchUnsignedIntegralConstant(OpIndex matched,
                                     uint64_t* constant) const {
    if (const ConstantOp* c = TryCast<ConstantOp>(matched)) {
      if (c->kind == ConstantOp::Kind::kWord32 ||
          c->kind == ConstantOp::Kind::kWord64) {
        *constant = c->integral();
        return true;
      }
    }
    return false;
  }

  bool MatchExternalConstant(OpIndex matched,
                             ExternalReference* reference) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kExternal) return false;
    *reference = op->storage.external;
    return true;
  }

  bool MatchWasmStubCallConstant(OpIndex matched, uint64_t* stub_id) const {
    const ConstantOp* op = TryCast<ConstantOp>(matched);
    if (!op) return false;
    if (op->kind != ConstantOp::Kind::kRelocatableWasmStubCall) {
      return false;
    }
    *stub_id = op->integral();
    return true;
  }

  bool MatchChange(OpIndex matched, OpIndex* input, ChangeOp::Kind kind,
                   RegisterRepresentation from,
                   RegisterRepresentation to) const {
    const ChangeOp* op = TryCast<ChangeOp>(matched);
    if (!op || op->kind != kind || op->from != from || op->to != to) {
      return false;
    }
    *input = op->input();
    return true;
  }

  bool MatchWordBinop(OpIndex matched, OpIndex* left, OpIndex* right,
                      WordBinopOp::Kind* kind, WordRepresentation* rep) const {
    const WordBinopOp* op = TryCast<WordBinopOp>(matched);
    if (!op) return false;
    *kind = op->kind;
    *left = op->left();
    *right = op->right();
    if (rep) *rep = op->rep;
    return true;
  }

  bool MatchWordBinop(OpIndex matched, OpIndex* left, OpIndex* right,
                      WordBinopOp::Kind kind, WordRepresentation rep) const {
    const WordBinopOp* op = TryCast<WordBinopOp>(matched);
    if (!op || kind != op->kind) {
      return false;
    }
    if (!(rep == op->rep ||
          (WordBinopOp::AllowsWord64ToWord32Truncation(kind) &&
           rep == WordRepresentation::Word32() &&
           op->rep == WordRepresentation::Word64()))) {
      return false;
    }
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchWordAdd(OpIndex matched, OpIndex* left, OpIndex* right,
                    WordRepresentation rep) const {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kAdd, rep);
  }

  bool MatchWordSub(OpIndex matched, OpIndex* left, OpIndex* right,
                    WordRepresentation rep) const {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kSub, rep);
  }

  bool MatchWordMul(OpIndex matched, OpIndex* left, OpIndex* right,
                    WordRepresentation rep) const {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kMul, rep);
  }

  bool MatchBitwiseAnd(OpIndex matched, OpIndex* left, OpIndex* right,
                       WordRepresentation rep) const {
    return MatchWordBinop(matched, left, right, WordBinopOp::Kind::kBitwiseAnd,
                          rep);
  }

  bool MatchBitwiseAndWithConstant(OpIndex matched, OpIndex* value,
                                   uint64_t* constant,
                                   WordRepresentation rep) const {
    OpIndex left, right;
    if (!MatchBitwiseAnd(matched, &left, &right, rep)) return false;
    if (MatchIntegralWordConstant(right, rep, constant)) {
      *value = left;
      return true;
    } else if (MatchIntegralWordConstant(left, rep, constant)) {
      *value = right;
      return true;
    }
    return false;
  }

  bool MatchEqual(OpIndex matched, OpIndex* left, OpIndex* right,
                  WordRepresentation rep) const {
    const ComparisonOp* op = TryCast<ComparisonOp>(matched);
    if (!op || op->kind != ComparisonOp::Kind::kEqual || rep != op->rep) {
      return false;
    }
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchComparison(OpIndex matched, OpIndex* left, OpIndex* right,
                       ComparisonOp::Kind* kind,
                       RegisterRepresentation* rep) const {
    const ComparisonOp* op = TryCast<ComparisonOp>(matched);
    if (!op) return false;
    *kind = op->kind;
    *rep = op->rep;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchFloatUnary(OpIndex matched, OpIndex* input, FloatUnaryOp::Kind kind,
                       FloatRepresentation rep) const {
    const FloatUnaryOp* op = TryCast<FloatUnaryOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *input = op->input();
    return true;
  }

  bool MatchFloatRoundDown(OpIndex matched, OpIndex* input,
                           FloatRepresentation rep) const {
    return MatchFloatUnary(matched, input, FloatUnaryOp::Kind::kRoundDown, rep);
  }

  bool MatchFloatBinary(OpIndex matched, OpIndex* left, OpIndex* right,
                        FloatBinopOp::Kind kind,
                        FloatRepresentation rep) const {
    const FloatBinopOp* op = TryCast<FloatBinopOp>(matched);
    if (!op || op->kind != kind || op->rep != rep) return false;
    *left = op->left();
    *right = op->right();
    return true;
  }

  bool MatchFloatSub(OpIndex matched, OpIndex* left, OpIndex* right,
                     FloatRepresentation rep) const {
    return MatchFloatBinary(matched, left, right, FloatBinopOp::Kind::kSub,
                            rep);
  }

  bool MatchConstantShift(OpIndex matched, OpIndex* input, ShiftOp::Kind* kind,
                          WordRepresentation* rep, int* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(op->rep.bit_width())) {
      *input = op->left();
      *kind = op->kind;
      *rep = op->rep;
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantShift(OpIndex matched, OpIndex* input, ShiftOp::Kind kind,
                          WordRepresentation rep, int* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == kind &&
        (op->rep == rep || (ShiftOp::AllowsWord64ToWord32Truncation(kind) &&
                            rep == WordRepresentation::Word32() &&
                            op->rep == WordRepresentation::Word64())) &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantRightShift(OpIndex matched, OpIndex* input,
                               WordRepresentation rep, int* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && ShiftOp::IsRightShift(op->kind) && op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint32_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantLeftShift(OpIndex matched, OpIndex* input,
                              WordRepresentation rep, int* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == ShiftOp::Kind::kShiftLeft && op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint32_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<int>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchConstantShiftRightArithmeticShiftOutZeros(OpIndex matched,
                                                      OpIndex* input,
                                                      WordRepresentation rep,
                                                      uint16_t* amount) const {
    const ShiftOp* op = TryCast<ShiftOp>(matched);
    if (uint32_t rhs_constant;
        op && op->kind == ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros &&
        op->rep == rep &&
        MatchIntegralWord32Constant(op->right(), &rhs_constant) &&
        rhs_constant < static_cast<uint64_t>(rep.bit_width())) {
      *input = op->left();
      *amount = static_cast<uint16_t>(rhs_constant);
      return true;
    }
    return false;
  }

  bool MatchPhi(OpIndex matched,
                base::Optional<int> input_count = base::nullopt) const {
    if (const PhiOp* phi = TryCast<PhiOp>(matched)) {
      return !input_count.has_value() || phi->input_count == *input_count;
    }
    return false;
  }

  bool MatchPowerOfTwoWord32Constant(OpIndex matched, int32_t* divisor) const {
    int32_t cst;
    if (MatchIntegralWord32Constant(matched, &cst)) {
      if (base::bits::IsPowerOfTwo(cst)) {
        *divisor = cst;
        return true;
      }
    }
    return false;
  }

 private:
  Graph& graph_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_OPERATION_MATCHER_H_
