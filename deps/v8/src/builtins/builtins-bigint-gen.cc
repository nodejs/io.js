// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-bigint-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/objects/dictionary.h"

namespace v8 {
namespace internal {

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(BigIntToI64, CodeStubAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  auto value = Parameter<Object>(Descriptor::kArgument);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<BigInt> n = ToBigInt(context, value);

  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

  BigIntToRawBytes(n, &var_low, &var_high);
  Return(var_low.value());
}

// https://tc39.github.io/proposal-bigint/#sec-to-big-int64
TF_BUILTIN(BigIntToI32Pair, CodeStubAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  auto value = Parameter<Object>(Descriptor::kArgument);
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<BigInt> bigint = ToBigInt(context, value);

  TVARIABLE(UintPtrT, var_low);
  TVARIABLE(UintPtrT, var_high);

  BigIntToRawBytes(bigint, &var_low, &var_high);
  Return(var_low.value(), var_high.value());
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(I64ToBigInt, CodeStubAssembler) {
  if (!Is64()) {
    Unreachable();
    return;
  }

  auto argument = UncheckedParameter<IntPtrT>(Descriptor::kArgument);

  Return(BigIntFromInt64(argument));
}

// https://tc39.github.io/proposal-bigint/#sec-bigint-constructor-number-value
TF_BUILTIN(I32PairToBigInt, CodeStubAssembler) {
  if (!Is32()) {
    Unreachable();
    return;
  }

  auto low = UncheckedParameter<IntPtrT>(Descriptor::kLow);
  auto high = UncheckedParameter<IntPtrT>(Descriptor::kHigh);

  Return(BigIntFromInt32Pair(low, high));
}

}  // namespace internal
}  // namespace v8
