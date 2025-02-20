// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions.h"
#include "src/objects/bigint.h"
#include "src/objects/objects-inl.h"
#include "test/unittests/test-utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using BigIntWithIsolate = TestWithIsolate;

void Compare(DirectHandle<BigInt> x, double value, ComparisonResult expected) {
  CHECK_EQ(expected, BigInt::CompareToDouble(x, value));
}

DirectHandle<BigInt> NewFromInt(Isolate* isolate, int value) {
  DirectHandle<Smi> smi_value(Smi::FromInt(value), isolate);
  return BigInt::FromNumber(isolate, smi_value).ToHandleChecked();
}

TEST_F(BigIntWithIsolate, CompareToDouble) {
  DirectHandle<BigInt> zero = NewFromInt(isolate(), 0);
  DirectHandle<BigInt> one = NewFromInt(isolate(), 1);
  DirectHandle<BigInt> minus_one = NewFromInt(isolate(), -1);

  // Non-finite doubles.
  Compare(zero, std::nan(""), ComparisonResult::kUndefined);
  Compare(one, INFINITY, ComparisonResult::kLessThan);
  Compare(one, -INFINITY, ComparisonResult::kGreaterThan);

  // Unequal sign.
  Compare(one, -1, ComparisonResult::kGreaterThan);
  Compare(minus_one, 1, ComparisonResult::kLessThan);

  // Cases involving zero.
  Compare(zero, 0, ComparisonResult::kEqual);
  Compare(zero, -0, ComparisonResult::kEqual);
  Compare(one, 0, ComparisonResult::kGreaterThan);
  Compare(minus_one, 0, ComparisonResult::kLessThan);
  Compare(zero, 1, ComparisonResult::kLessThan);
  Compare(zero, -1, ComparisonResult::kGreaterThan);

  // Small doubles.
  Compare(zero, 0.25, ComparisonResult::kLessThan);
  Compare(one, 0.5, ComparisonResult::kGreaterThan);
  Compare(one, -0.5, ComparisonResult::kGreaterThan);
  Compare(zero, -0.25, ComparisonResult::kGreaterThan);
  Compare(minus_one, -0.5, ComparisonResult::kLessThan);

  // Different bit lengths.
  DirectHandle<BigInt> four = NewFromInt(isolate(), 4);
  DirectHandle<BigInt> minus_five = NewFromInt(isolate(), -5);
  Compare(four, 3.9, ComparisonResult::kGreaterThan);
  Compare(four, 1.5, ComparisonResult::kGreaterThan);
  Compare(four, 8, ComparisonResult::kLessThan);
  Compare(four, 16, ComparisonResult::kLessThan);
  Compare(minus_five, -4.9, ComparisonResult::kLessThan);
  Compare(minus_five, -4, ComparisonResult::kLessThan);
  Compare(minus_five, -25, ComparisonResult::kGreaterThan);

  // Same bit length, difference in first digit.
  double big_double = 4428155326412785451008.0;
  DirectHandle<BigInt> big =
      BigIntLiteral(isolate(), "0xF10D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0xE00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  double other_double = -13758438578910658560.0;
  DirectHandle<BigInt> other =
      BigIntLiteral(isolate(), "-0xBEEFC1FE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kGreaterThan);
  other = BigIntLiteral(isolate(), "-0xBEEFCBFE00000000").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in non-first digit.
  big = BigIntLiteral(isolate(), "0xF00D00000000000001").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0xF00A00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kLessThan);

  other = BigIntLiteral(isolate(), "-0xBEEFCAFE00000001").ToHandleChecked();
  Compare(other, other_double, ComparisonResult::kLessThan);

  // Same bit length, difference in fractional part.
  Compare(one, 1.5, ComparisonResult::kLessThan);
  Compare(minus_one, -1.25, ComparisonResult::kGreaterThan);
  big = NewFromInt(isolate(), 0xF00D00);
  Compare(big, 15731968.125, ComparisonResult::kLessThan);
  Compare(big, 15731967.875, ComparisonResult::kGreaterThan);
  big = BigIntLiteral(isolate(), "0x123456789AB").ToHandleChecked();
  Compare(big, 1250999896491.125, ComparisonResult::kLessThan);

  // Equality!
  Compare(one, 1, ComparisonResult::kEqual);
  Compare(minus_one, -1, ComparisonResult::kEqual);
  big = BigIntLiteral(isolate(), "0xF00D00000000000000").ToHandleChecked();
  Compare(big, big_double, ComparisonResult::kEqual);

  DirectHandle<BigInt> two_52 =
      BigIntLiteral(isolate(), "0x10000000000000").ToHandleChecked();
  Compare(two_52, 4503599627370496.0, ComparisonResult::kEqual);
}

}  // namespace internal
}  // namespace v8
