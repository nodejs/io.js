// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_BIGINT_INL_H_
#define V8_OBJECTS_BIGINT_INL_H_

#include "src/objects/bigint.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

Tagged<BigIntBase> BigIntBase::cast(Tagged<Object> object) {
  SLOW_DCHECK(IsBigIntBase(object));
  return BigIntBase::unchecked_cast(object);
}

Tagged<BigInt> BigInt::cast(Tagged<Object> object) {
  SLOW_DCHECK(IsBigInt(object));
  return BigInt::unchecked_cast(object);
}

Tagged<FreshlyAllocatedBigInt> FreshlyAllocatedBigInt::cast(
    Tagged<Object> object) {
  SLOW_DCHECK(IsBigInt(object));
  return FreshlyAllocatedBigInt::unchecked_cast(object);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_OBJECTS_BIGINT_INL_H_
