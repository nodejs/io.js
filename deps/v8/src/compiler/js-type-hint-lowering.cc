// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/js-type-hint-lowering.h"

#include "src/compiler/js-graph.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/operator-properties.h"
#include "src/compiler/simplified-operator.h"
#include "src/objects/type-hints.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {

bool BinaryOperationHintToNumberOperationHint(
    BinaryOperationHint binop_hint, NumberOperationHint* number_hint) {
  switch (binop_hint) {
    case BinaryOperationHint::kSignedSmall:
      *number_hint = NumberOperationHint::kSignedSmall;
      return true;
    case BinaryOperationHint::kSignedSmallInputs:
      *number_hint = NumberOperationHint::kSignedSmallInputs;
      return true;
    case BinaryOperationHint::kNumber:
      *number_hint = NumberOperationHint::kNumber;
      return true;
    case BinaryOperationHint::kNumberOrOddball:
      *number_hint = NumberOperationHint::kNumberOrOddball;
      return true;
    case BinaryOperationHint::kAny:
    case BinaryOperationHint::kNone:
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
    case BinaryOperationHint::kBigInt:
    case BinaryOperationHint::kBigInt64:
      break;
  }
  return false;
}

bool BinaryOperationHintToBigIntOperationHint(
    BinaryOperationHint binop_hint, BigIntOperationHint* bigint_hint) {
  switch (binop_hint) {
    case BinaryOperationHint::kSignedSmall:
    case BinaryOperationHint::kSignedSmallInputs:
    case BinaryOperationHint::kNumber:
    case BinaryOperationHint::kNumberOrOddball:
    case BinaryOperationHint::kAny:
    case BinaryOperationHint::kNone:
    case BinaryOperationHint::kString:
    case BinaryOperationHint::kStringOrStringWrapper:
      return false;
    case BinaryOperationHint::kBigInt64:
      *bigint_hint = BigIntOperationHint::kBigInt64;
      return true;
    case BinaryOperationHint::kBigInt:
      *bigint_hint = BigIntOperationHint::kBigInt;
      return true;
  }
  UNREACHABLE();
}

}  // namespace

class JSSpeculativeBinopBuilder final {
 public:
  JSSpeculativeBinopBuilder(const JSTypeHintLowering* lowering,
                            const Operator* op, Node* left, Node* right,
                            Node* effect, Node* control, FeedbackSlot slot)
      : lowering_(lowering),
        op_(op),
        left_(left),
        right_(right),
        effect_(effect),
        control_(control),
        slot_(slot) {}

  bool GetBinaryNumberOperationHint(NumberOperationHint* hint) {
    return BinaryOperationHintToNumberOperationHint(GetBinaryOperationHint(),
                                                    hint);
  }

  bool GetBinaryBigIntOperationHint(BigIntOperationHint* hint) {
    return BinaryOperationHintToBigIntOperationHint(GetBinaryOperationHint(),
                                                    hint);
  }

  bool GetCompareNumberOperationHint(NumberOperationHint* hint) {
    switch (GetCompareOperationHint()) {
      case CompareOperationHint::kSignedSmall:
        *hint = NumberOperationHint::kSignedSmall;
        return true;
      case CompareOperationHint::kNumber:
        *hint = NumberOperationHint::kNumber;
        return true;
      case CompareOperationHint::kNumberOrBoolean:
        *hint = NumberOperationHint::kNumberOrBoolean;
        return true;
      case CompareOperationHint::kNumberOrOddball:
        *hint = NumberOperationHint::kNumberOrOddball;
        return true;
      case CompareOperationHint::kAny:
      case CompareOperationHint::kNone:
      case CompareOperationHint::kString:
      case CompareOperationHint::kSymbol:
      case CompareOperationHint::kBigInt:
      case CompareOperationHint::kBigInt64:
      case CompareOperationHint::kReceiver:
      case CompareOperationHint::kReceiverOrNullOrUndefined:
      case CompareOperationHint::kInternalizedString:
        break;
    }
    return false;
  }

  bool GetCompareBigIntOperationHint(BigIntOperationHint* hint) {
    switch (GetCompareOperationHint()) {
      case CompareOperationHint::kSignedSmall:
      case CompareOperationHint::kNumber:
      case CompareOperationHint::kNumberOrBoolean:
      case CompareOperationHint::kNumberOrOddball:
      case CompareOperationHint::kAny:
      case CompareOperationHint::kNone:
      case CompareOperationHint::kString:
      case CompareOperationHint::kSymbol:
      case CompareOperationHint::kReceiver:
      case CompareOperationHint::kReceiverOrNullOrUndefined:
      case CompareOperationHint::kInternalizedString:
        return false;
      case CompareOperationHint::kBigInt:
        *hint = BigIntOperationHint::kBigInt;
        return true;
      case CompareOperationHint::kBigInt64:
        *hint = BigIntOperationHint::kBigInt64;
        return true;
    }
  }

  const Operator* SpeculativeNumberOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSAdd:
        if (hint == NumberOperationHint::kSignedSmall) {
          return simplified()->SpeculativeSafeIntegerAdd(hint);
        } else {
          return simplified()->SpeculativeNumberAdd(hint);
        }
      case IrOpcode::kJSSubtract:
        if (hint == NumberOperationHint::kSignedSmall) {
          return simplified()->SpeculativeSafeIntegerSubtract(hint);
        } else {
          return simplified()->SpeculativeNumberSubtract(hint);
        }
      case IrOpcode::kJSMultiply:
        return simplified()->SpeculativeNumberMultiply(hint);
      case IrOpcode::kJSExponentiate:
        return simplified()->SpeculativeNumberPow(hint);
      case IrOpcode::kJSDivide:
        return simplified()->SpeculativeNumberDivide(hint);
      case IrOpcode::kJSModulus:
        return simplified()->SpeculativeNumberModulus(hint);
      case IrOpcode::kJSBitwiseAnd:
        return simplified()->SpeculativeNumberBitwiseAnd(hint);
      case IrOpcode::kJSBitwiseOr:
        return simplified()->SpeculativeNumberBitwiseOr(hint);
      case IrOpcode::kJSBitwiseXor:
        return simplified()->SpeculativeNumberBitwiseXor(hint);
      case IrOpcode::kJSShiftLeft:
        return simplified()->SpeculativeNumberShiftLeft(hint);
      case IrOpcode::kJSShiftRight:
        return simplified()->SpeculativeNumberShiftRight(hint);
      case IrOpcode::kJSShiftRightLogical:
        return simplified()->SpeculativeNumberShiftRightLogical(hint);
      default:
        break;
    }
    UNREACHABLE();
  }

  const Operator* SpeculativeBigIntOp(BigIntOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSAdd:
        return simplified()->SpeculativeBigIntAdd(hint);
      case IrOpcode::kJSSubtract:
        return simplified()->SpeculativeBigIntSubtract(hint);
      case IrOpcode::kJSMultiply:
        return simplified()->SpeculativeBigIntMultiply(hint);
      case IrOpcode::kJSDivide:
        return simplified()->SpeculativeBigIntDivide(hint);
      case IrOpcode::kJSModulus:
        return simplified()->SpeculativeBigIntModulus(hint);
      case IrOpcode::kJSBitwiseAnd:
        return simplified()->SpeculativeBigIntBitwiseAnd(hint);
      case IrOpcode::kJSBitwiseOr:
        return simplified()->SpeculativeBigIntBitwiseOr(hint);
      case IrOpcode::kJSBitwiseXor:
        return simplified()->SpeculativeBigIntBitwiseXor(hint);
      case IrOpcode::kJSShiftLeft:
        return simplified()->SpeculativeBigIntShiftLeft(hint);
      case IrOpcode::kJSShiftRight:
        return simplified()->SpeculativeBigIntShiftRight(hint);
      default:
        break;
    }
    UNREACHABLE();
  }

  const Operator* SpeculativeNumberCompareOp(NumberOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSEqual:
        return simplified()->SpeculativeNumberEqual(hint);
      case IrOpcode::kJSLessThan:
        return simplified()->SpeculativeNumberLessThan(hint);
      case IrOpcode::kJSGreaterThan:
        std::swap(left_, right_);  // a > b => b < a
        return simplified()->SpeculativeNumberLessThan(hint);
      case IrOpcode::kJSLessThanOrEqual:
        return simplified()->SpeculativeNumberLessThanOrEqual(hint);
      case IrOpcode::kJSGreaterThanOrEqual:
        std::swap(left_, right_);  // a >= b => b <= a
        return simplified()->SpeculativeNumberLessThanOrEqual(hint);
      default:
        break;
    }
    UNREACHABLE();
  }

  const Operator* SpeculativeBigIntCompareOp(BigIntOperationHint hint) {
    switch (op_->opcode()) {
      case IrOpcode::kJSEqual:
        return simplified()->SpeculativeBigIntEqual(hint);
      case IrOpcode::kJSLessThan:
        return simplified()->SpeculativeBigIntLessThan(hint);
      case IrOpcode::kJSGreaterThan:
        std::swap(left_, right_);
        return simplified()->SpeculativeBigIntLessThan(hint);
      case IrOpcode::kJSLessThanOrEqual:
        return simplified()->SpeculativeBigIntLessThanOrEqual(hint);
      case IrOpcode::kJSGreaterThanOrEqual:
        std::swap(left_, right_);
        return simplified()->SpeculativeBigIntLessThanOrEqual(hint);
      default:
        break;
    }
    UNREACHABLE();
  }

  Node* BuildSpeculativeOperation(const Operator* op) {
    DCHECK_EQ(2, op->ValueInputCount());
    DCHECK_EQ(1, op->EffectInputCount());
    DCHECK_EQ(1, op->ControlInputCount());
    DCHECK_EQ(false, OperatorProperties::HasFrameStateInput(op));
    DCHECK_EQ(false, OperatorProperties::HasContextInput(op));
    DCHECK_EQ(1, op->EffectOutputCount());
    DCHECK_EQ(0, op->ControlOutputCount());
    return graph()->NewNode(op, left_, right_, effect_, control_);
  }

  Node* TryBuildNumberBinop() {
    NumberOperationHint hint;
    if (GetBinaryNumberOperationHint(&hint)) {
      const Operator* op = SpeculativeNumberOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  Node* TryBuildBigIntBinop() {
    BigIntOperationHint hint;
    if (GetBinaryBigIntOperationHint(&hint)) {
      const Operator* op = SpeculativeBigIntOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  Node* TryBuildNumberCompare() {
    NumberOperationHint hint;
    if (GetCompareNumberOperationHint(&hint)) {
      const Operator* op = SpeculativeNumberCompareOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  Node* TryBuildBigIntCompare() {
    BigIntOperationHint hint;
    if (GetCompareBigIntOperationHint(&hint)) {
      const Operator* op = SpeculativeBigIntCompareOp(hint);
      Node* node = BuildSpeculativeOperation(op);
      return node;
    }
    return nullptr;
  }

  JSGraph* jsgraph() const { return lowering_->jsgraph(); }
  Isolate* isolate() const { return jsgraph()->isolate(); }
  Graph* graph() const { return jsgraph()->graph(); }
  JSOperatorBuilder* javascript() { return jsgraph()->javascript(); }
  SimplifiedOperatorBuilder* simplified() { return jsgraph()->simplified(); }
  CommonOperatorBuilder* common() { return jsgraph()->common(); }

 private:
  BinaryOperationHint GetBinaryOperationHint() {
    return lowering_->GetBinaryOperationHint(slot_);
  }

  CompareOperationHint GetCompareOperationHint() {
    return lowering_->GetCompareOperationHint(slot_);
  }

  JSTypeHintLowering const* const lowering_;
  Operator const* const op_;
  Node* left_;
  Node* right_;
  Node* const effect_;
  Node* const control_;
  FeedbackSlot const slot_;
};

JSTypeHintLowering::JSTypeHintLowering(JSHeapBroker* broker, JSGraph* jsgraph,
                                       FeedbackVectorRef feedback_vector,
                                       Flags flags)
    : broker_(broker),
      jsgraph_(jsgraph),
      flags_(flags),
      feedback_vector_(feedback_vector) {}

Isolate* JSTypeHintLowering::isolate() const { return jsgraph()->isolate(); }

BinaryOperationHint JSTypeHintLowering::GetBinaryOperationHint(
    FeedbackSlot slot) const {
  FeedbackSource source(feedback_vector(), slot);
  return broker()->GetFeedbackForBinaryOperation(source);
}

CompareOperationHint JSTypeHintLowering::GetCompareOperationHint(
    FeedbackSlot slot) const {
  FeedbackSource source(feedback_vector(), slot);
  return broker()->GetFeedbackForCompareOperation(source);
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceUnaryOperation(
    const Operator* op, Node* operand, Node* effect, Node* control,
    FeedbackSlot slot) const {
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForUnaryOperation)) {
    return LoweringResult::Exit(node);
  }

  // Note: Unary and binary operations collect the same kind of feedback.
  FeedbackSource feedback(feedback_vector(), slot);

  Node* node;
  switch (op->opcode()) {
    case IrOpcode::kJSBitwiseNot: {
      // Lower to a speculative xor with -1 if we have some kind of Number
      // feedback.
      JSSpeculativeBinopBuilder b(
          this, jsgraph()->javascript()->BitwiseXor(feedback), operand,
          jsgraph()->SmiConstant(-1), effect, control, slot);
      node = b.TryBuildNumberBinop();
      break;
    }
    case IrOpcode::kJSDecrement: {
      // Lower to a speculative subtraction of 1 if we have some kind of Number
      // feedback.
      JSSpeculativeBinopBuilder b(
          this, jsgraph()->javascript()->Subtract(feedback), operand,
          jsgraph()->SmiConstant(1), effect, control, slot);
      node = b.TryBuildNumberBinop();
      break;
    }
    case IrOpcode::kJSIncrement: {
      // Lower to a speculative addition of 1 if we have some kind of Number
      // feedback.
      JSSpeculativeBinopBuilder b(this, jsgraph()->javascript()->Add(feedback),
                                  operand, jsgraph()->SmiConstant(1), effect,
                                  control, slot);
      node = b.TryBuildNumberBinop();
      break;
    }
    case IrOpcode::kJSNegate: {
      // Lower to a speculative multiplication with -1 if we have some kind of
      // Number feedback.
      JSSpeculativeBinopBuilder b(
          this, jsgraph()->javascript()->Multiply(feedback), operand,
          jsgraph()->SmiConstant(-1), effect, control, slot);
      node = b.TryBuildNumberBinop();
      if (!node) {
        if (jsgraph()->machine()->Is64()) {
          if (GetBinaryOperationHint(slot) == BinaryOperationHint::kBigInt) {
            op = jsgraph()->simplified()->SpeculativeBigIntNegate(
                BigIntOperationHint::kBigInt);
            node = jsgraph()->graph()->NewNode(op, operand, effect, control);
          }
        }
      }
      break;
    }
    default:
      UNREACHABLE();
  }

  if (node != nullptr) {
    return LoweringResult::SideEffectFree(node, node, control);
  } else {
    return LoweringResult::NoChange();
  }
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceBinaryOperation(
    const Operator* op, Node* left, Node* right, Node* effect, Node* control,
    FeedbackSlot slot) const {
  switch (op->opcode()) {
    case IrOpcode::kJSStrictEqual: {
      if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
              slot, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation)) {
        return LoweringResult::Exit(node);
      }
      // TODO(turbofan): Should we generally support early lowering of
      // JSStrictEqual operators here?
      break;
    }
    case IrOpcode::kJSEqual:
    case IrOpcode::kJSLessThan:
    case IrOpcode::kJSGreaterThan:
    case IrOpcode::kJSLessThanOrEqual:
    case IrOpcode::kJSGreaterThanOrEqual: {
      if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
              slot, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation)) {
        return LoweringResult::Exit(node);
      }
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberCompare()) {
        return LoweringResult::SideEffectFree(node, node, control);
      }
      if (Node* node = b.TryBuildBigIntCompare()) {
        return LoweringResult::SideEffectFree(node, node, control);
      }
      break;
    }
    case IrOpcode::kJSInstanceOf: {
      if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
              slot, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForCompareOperation)) {
        return LoweringResult::Exit(node);
      }
      // TODO(turbofan): Should we generally support early lowering of
      // JSInstanceOf operators here?
      break;
    }
    case IrOpcode::kJSBitwiseOr:
    case IrOpcode::kJSBitwiseXor:
    case IrOpcode::kJSBitwiseAnd:
    case IrOpcode::kJSShiftLeft:
    case IrOpcode::kJSShiftRight:
    case IrOpcode::kJSShiftRightLogical:
    case IrOpcode::kJSAdd:
    case IrOpcode::kJSSubtract:
    case IrOpcode::kJSMultiply:
    case IrOpcode::kJSDivide:
    case IrOpcode::kJSModulus:
    case IrOpcode::kJSExponentiate: {
      if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
              slot, effect, control,
              DeoptimizeReason::kInsufficientTypeFeedbackForBinaryOperation)) {
        return LoweringResult::Exit(node);
      }
      JSSpeculativeBinopBuilder b(this, op, left, right, effect, control, slot);
      if (Node* node = b.TryBuildNumberBinop()) {
        return LoweringResult::SideEffectFree(node, node, control);
      }
      if (op->opcode() != IrOpcode::kJSShiftRightLogical &&
          op->opcode() != IrOpcode::kJSExponentiate) {
        if (Node* node = b.TryBuildBigIntBinop()) {
          return LoweringResult::SideEffectFree(node, node, control);
        }
      }
      break;
    }
    default:
      UNREACHABLE();
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceForInNextOperation(
    Node* receiver, Node* cache_array, Node* cache_type, Node* index,
    Node* effect, Node* control, FeedbackSlot slot) const {
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForForIn)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceForInPrepareOperation(Node* enumerator, Node* effect,
                                                Node* control,
                                                FeedbackSlot slot) const {
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForForIn)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceToNumberOperation(
    Node* input, Node* effect, Node* control, FeedbackSlot slot) const {
  DCHECK(!slot.IsInvalid());
  NumberOperationHint hint;
  if (BinaryOperationHintToNumberOperationHint(GetBinaryOperationHint(slot),
                                               &hint)) {
    Node* node = jsgraph()->graph()->NewNode(
        jsgraph()->simplified()->SpeculativeToNumber(hint, FeedbackSource()),
        input, effect, control);
    return LoweringResult::SideEffectFree(node, node, control);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceCallOperation(
    const Operator* op, Node* const* args, int arg_count, Node* effect,
    Node* control, FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSCall ||
         op->opcode() == IrOpcode::kJSCallWithSpread);
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForCall)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceConstructOperation(
    const Operator* op, Node* const* args, int arg_count, Node* effect,
    Node* control, FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSConstruct ||
         op->opcode() == IrOpcode::kJSConstructWithSpread ||
         op->opcode() == IrOpcode::kJSConstructForwardAllArgs);
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForConstruct)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceGetIteratorOperation(const Operator* op,
                                               Node* receiver, Node* effect,
                                               Node* control,
                                               FeedbackSlot load_slot,
                                               FeedbackSlot call_slot) const {
  DCHECK_EQ(IrOpcode::kJSGetIterator, op->opcode());
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          load_slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceLoadNamedOperation(
    const Operator* op, Node* effect, Node* control, FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSLoadNamed ||
         op->opcode() == IrOpcode::kJSLoadNamedFromSuper);
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult JSTypeHintLowering::ReduceLoadKeyedOperation(
    const Operator* op, Node* obj, Node* key, Node* effect, Node* control,
    FeedbackSlot slot) const {
  DCHECK_EQ(IrOpcode::kJSLoadProperty, op->opcode());
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceStoreNamedOperation(const Operator* op, Node* obj,
                                              Node* val, Node* effect,
                                              Node* control,
                                              FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSSetNamedProperty ||
         op->opcode() == IrOpcode::kJSDefineNamedOwnProperty);
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericNamedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

JSTypeHintLowering::LoweringResult
JSTypeHintLowering::ReduceStoreKeyedOperation(const Operator* op, Node* obj,
                                              Node* key, Node* val,
                                              Node* effect, Node* control,
                                              FeedbackSlot slot) const {
  DCHECK(op->opcode() == IrOpcode::kJSSetKeyedProperty ||
         op->opcode() == IrOpcode::kJSStoreInArrayLiteral ||
         op->opcode() == IrOpcode::kJSDefineKeyedOwnPropertyInLiteral ||
         op->opcode() == IrOpcode::kJSDefineKeyedOwnProperty);
  if (Node* node = BuildDeoptIfFeedbackIsInsufficient(
          slot, effect, control,
          DeoptimizeReason::kInsufficientTypeFeedbackForGenericKeyedAccess)) {
    return LoweringResult::Exit(node);
  }
  return LoweringResult::NoChange();
}

Node* JSTypeHintLowering::BuildDeoptIfFeedbackIsInsufficient(
    FeedbackSlot slot, Node* effect, Node* control,
    DeoptimizeReason reason) const {
  if (!(flags() & kBailoutOnUninitialized)) return nullptr;

  FeedbackSource source(feedback_vector(), slot);
  if (!broker()->FeedbackIsInsufficient(source)) return nullptr;

  Node* deoptimize = jsgraph()->graph()->NewNode(
      jsgraph()->common()->Deoptimize(reason, FeedbackSource()),
      jsgraph()->Dead(), effect, control);
  Node* frame_state =
      NodeProperties::FindFrameStateBefore(deoptimize, jsgraph()->Dead());
  deoptimize->ReplaceInput(0, frame_state);
  return deoptimize;
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
