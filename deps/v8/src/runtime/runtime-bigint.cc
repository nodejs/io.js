// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/arguments-inl.h"
#include "src/objects/bigint.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_BigIntCompareToNumber) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(3, args.length());
  int mode = args.smi_value_at(0);
  DirectHandle<BigInt> lhs = args.at<BigInt>(1);
  DirectHandle<Object> rhs = args.at(2);
  bool result = ComparisonResultToBool(static_cast<Operation>(mode),
                                       BigInt::CompareToNumber(lhs, rhs));
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntCompareToString) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  int mode = args.smi_value_at(0);
  DirectHandle<BigInt> lhs = args.at<BigInt>(1);
  DirectHandle<String> rhs = args.at<String>(2);
  Maybe<ComparisonResult> maybe_result =
      BigInt::CompareToString(isolate, lhs, rhs);
  MAYBE_RETURN(maybe_result, ReadOnlyRoots(isolate).exception());
  bool result = ComparisonResultToBool(static_cast<Operation>(mode),
                                       maybe_result.FromJust());
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntEqualToBigInt) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<BigInt> lhs = args.at<BigInt>(0);
  DirectHandle<BigInt> rhs = args.at<BigInt>(1);
  bool result = BigInt::EqualToBigInt(*lhs, *rhs);
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntEqualToNumber) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<BigInt> lhs = args.at<BigInt>(0);
  DirectHandle<Object> rhs = args.at(1);
  bool result = BigInt::EqualToNumber(lhs, rhs);
  return *isolate->factory()->ToBoolean(result);
}

RUNTIME_FUNCTION(Runtime_BigIntEqualToString) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<BigInt> lhs = args.at<BigInt>(0);
  DirectHandle<String> rhs = args.at<String>(1);
  Maybe<bool> maybe_result = BigInt::EqualToString(isolate, lhs, rhs);
  MAYBE_RETURN(maybe_result, ReadOnlyRoots(isolate).exception());
  return *isolate->factory()->ToBoolean(maybe_result.FromJust());
}

RUNTIME_FUNCTION(Runtime_BigIntToNumber) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<BigInt> x = args.at<BigInt>(0);
  return *BigInt::ToNumber(isolate, x);
}

RUNTIME_FUNCTION(Runtime_ToBigInt) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<Object> x = args.at(0);
  RETURN_RESULT_OR_FAILURE(isolate, BigInt::FromObject(isolate, x));
}

RUNTIME_FUNCTION(Runtime_ToBigIntConvertNumber) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<Object> x = args.at(0);

  if (IsJSReceiver(*x)) {
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, x,
        JSReceiver::ToPrimitive(isolate, Cast<JSReceiver>(x),
                                ToPrimitiveHint::kNumber));
  }

  if (IsNumber(*x)) {
    RETURN_RESULT_OR_FAILURE(isolate, BigInt::FromNumber(isolate, x));
  } else {
    RETURN_RESULT_OR_FAILURE(isolate, BigInt::FromObject(isolate, x));
  }
}

RUNTIME_FUNCTION(Runtime_BigIntExponentiate) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> left_obj = args.at(0);
  DirectHandle<Object> right_obj = args.at(1);

  if (!IsBigInt(*left_obj) || !IsBigInt(*right_obj)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kBigIntMixedTypes));
  }
  auto left = Cast<BigInt>(left_obj);
  auto right = Cast<BigInt>(right_obj);
  RETURN_RESULT_OR_FAILURE(isolate, BigInt::Exponentiate(isolate, left, right));
}

RUNTIME_FUNCTION(Runtime_BigIntUnaryOp) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<BigInt> x = args.at<BigInt>(0);
  int opcode = args.smi_value_at(1);
  Operation op = static_cast<Operation>(opcode);

  MaybeDirectHandle<BigInt> result;
  switch (op) {
    case Operation::kBitwiseNot:
      result = BigInt::BitwiseNot(isolate, x);
      break;
    case Operation::kNegate:
      result = BigInt::UnaryMinus(isolate, x);
      break;
    case Operation::kIncrement:
      result = BigInt::Increment(isolate, x);
      break;
    case Operation::kDecrement:
      result = BigInt::Decrement(isolate, x);
      break;
    default:
      UNREACHABLE();
  }
  RETURN_RESULT_OR_FAILURE(isolate, result);
}

}  // namespace internal
}  // namespace v8
