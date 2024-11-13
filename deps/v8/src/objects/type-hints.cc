// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/type-hints.h"

#include <ostream>

#include "src/base/logging.h"

namespace v8 {
namespace internal {

std::ostream& operator<<(std::ostream& os, BinaryOperationHint hint) {
  switch (hint) {
    case BinaryOperationHint::kNone:
      return os << "None";
    case BinaryOperationHint::kSignedSmall:
      return os << "SignedSmall";
    case BinaryOperationHint::kSignedSmallInputs:
      return os << "SignedSmallInputs";
    case BinaryOperationHint::kNumber:
      return os << "Number";
    case BinaryOperationHint::kNumberOrOddball:
      return os << "NumberOrOddball";
    case BinaryOperationHint::kString:
      return os << "String";
    case BinaryOperationHint::kStringOrStringWrapper:
      return os << "StringOrStringWrapper";
    case BinaryOperationHint::kBigInt:
      return os << "BigInt";
    case BinaryOperationHint::kBigInt64:
      return os << "BigInt64";
    case BinaryOperationHint::kAny:
      return os << "Any";
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os, CompareOperationHint hint) {
  switch (hint) {
    case CompareOperationHint::kNone:
      return os << "None";
    case CompareOperationHint::kSignedSmall:
      return os << "SignedSmall";
    case CompareOperationHint::kNumber:
      return os << "Number";
    case CompareOperationHint::kNumberOrBoolean:
      return os << "NumberOrBoolean";
    case CompareOperationHint::kNumberOrOddball:
      return os << "NumberOrOddball";
    case CompareOperationHint::kInternalizedString:
      return os << "InternalizedString";
    case CompareOperationHint::kString:
      return os << "String";
    case CompareOperationHint::kSymbol:
      return os << "Symbol";
    case CompareOperationHint::kBigInt:
      return os << "BigInt";
    case CompareOperationHint::kBigInt64:
      return os << "BigInt64";
    case CompareOperationHint::kReceiver:
      return os << "Receiver";
    case CompareOperationHint::kReceiverOrNullOrUndefined:
      return os << "ReceiverOrNullOrUndefined";
    case CompareOperationHint::kAny:
      return os << "Any";
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os, ForInHint hint) {
  switch (hint) {
    case ForInHint::kNone:
      return os << "None";
    case ForInHint::kEnumCacheKeys:
      return os << "EnumCacheKeys";
    case ForInHint::kEnumCacheKeysAndIndices:
      return os << "EnumCacheKeysAndIndices";
    case ForInHint::kAny:
      return os << "Any";
  }
  UNREACHABLE();
}

std::ostream& operator<<(std::ostream& os, const StringAddFlags& flags) {
  switch (flags) {
    case STRING_ADD_CHECK_NONE:
      return os << "CheckNone";
    case STRING_ADD_CONVERT_LEFT:
      return os << "ConvertLeft";
    case STRING_ADD_CONVERT_RIGHT:
      return os << "ConvertRight";
  }
  UNREACHABLE();
}

}  // namespace internal
}  // namespace v8
