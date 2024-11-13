// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_DEOPTIMIZER_DEOPTIMIZE_REASON_H_
#define V8_DEOPTIMIZER_DEOPTIMIZE_REASON_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

#define DEOPTIMIZE_REASON_LIST(V)                                              \
  V(ArrayBufferWasDetached, "array buffer was detached")                       \
  V(BigIntTooBig, "BigInt too big")                                            \
  V(ConstTrackingLet, "const tracking let constness invalidated")              \
  V(CowArrayElementsChanged, "copy-on-write array's elements changed")         \
  V(CouldNotGrowElements, "failed to grow elements store")                     \
  V(PrepareForOnStackReplacement, "prepare for on stack replacement (OSR)")    \
  V(OSREarlyExit, "exit from OSR'd inner loop")                                \
  V(DeoptimizeNow, "%_DeoptimizeNow")                                          \
  V(DivisionByZero, "division by zero")                                        \
  V(Hole, "hole")                                                              \
  V(InstanceMigrationFailed, "instance migration failed")                      \
  V(InsufficientTypeFeedbackForCall, "Insufficient type feedback for call")    \
  V(InsufficientTypeFeedbackForConstruct,                                      \
    "Insufficient type feedback for construct")                                \
  V(InsufficientTypeFeedbackForForIn, "Insufficient type feedback for for-in") \
  V(InsufficientTypeFeedbackForBinaryOperation,                                \
    "Insufficient type feedback for binary operation")                         \
  V(InsufficientTypeFeedbackForCompareOperation,                               \
    "Insufficient type feedback for compare operation")                        \
  V(InsufficientTypeFeedbackForGenericNamedAccess,                             \
    "Insufficient type feedback for generic named access")                     \
  V(InsufficientTypeFeedbackForGenericGlobalAccess,                            \
    "Insufficient type feedback for generic global access")                    \
  V(InsufficientTypeFeedbackForGenericKeyedAccess,                             \
    "Insufficient type feedback for generic keyed access")                     \
  V(InsufficientTypeFeedbackForUnaryOperation,                                 \
    "Insufficient type feedback for unary operation")                          \
  V(InsufficientTypeFeedbackForArrayLiteral,                                   \
    "Insufficient type feedback for array literal")                            \
  V(InsufficientTypeFeedbackForObjectLiteral,                                  \
    "Insufficient type feedback for object literal")                           \
  V(InsufficientTypeFeedbackForInstanceOf,                                     \
    "Insufficient type feedback for instanceof")                               \
  V(InsufficientTypeFeedbackForTypeOf,                                         \
    "Insufficient type feedback for typeof")                                   \
  V(LostPrecision, "lost precision")                                           \
  V(LostPrecisionOrNaN, "lost precision or NaN")                               \
  V(MinusZero, "minus zero")                                                   \
  V(NaN, "NaN")                                                                \
  V(NoCache, "no cache")                                                       \
  V(NotABigInt, "not a BigInt")                                                \
  V(NotABigInt64, "not a BigInt64")                                            \
  V(NotAHeapNumber, "not a heap number")                                       \
  V(NotAJavaScriptObject, "not a JavaScript object")                           \
  V(NotAJavaScriptObjectOrNullOrUndefined,                                     \
    "not a JavaScript object, Null or Undefined")                              \
  V(NotANumber, "not a Number")                                                \
  V(NotANumberOrBoolean, "not a Number or Boolean")                            \
  V(NotANumberOrOddball, "not a Number or Oddball")                            \
  V(NotAnArrayIndex, "not an array index")                                     \
  V(NotASmi, "not a Smi")                                                      \
  V(NotAString, "not a String")                                                \
  V(NotAStringOrStringWrapper, "not a String or a string wrapper")             \
  V(NotASymbol, "not a Symbol")                                                \
  V(NotDetectableReceiver, "not a detectable receiver")                        \
  V(NotInt32, "not int32")                                                     \
  V(NotUint32, "not unsigned int32")                                           \
  V(OutOfBounds, "out of bounds")                                              \
  V(Overflow, "overflow")                                                      \
  V(Smi, "Smi")                                                                \
  V(StoreToConstant, "Storing to a constant field")                            \
  V(SuspendGeneratorIsDead, "SuspendGenerator is in a dead branch")            \
  V(Unknown, "(unknown)")                                                      \
  V(UnoptimizedCatch, "First use of catch block")                              \
  V(ValueMismatch, "value mismatch")                                           \
  V(WrongCallTarget, "wrong call target")                                      \
  V(WrongEnumIndices, "wrong enum indices")                                    \
  V(WrongFeedbackCell, "wrong feedback cell")                                  \
  V(WrongInstanceType, "wrong instance type")                                  \
  V(WrongMap, "wrong map")                                                     \
  V(DeprecatedMap, "deprecated map")                                           \
  V(WrongName, "wrong name")                                                   \
  V(WrongValue, "wrong value")                                                 \
  V(NoInitialElement, "no initial element")                                    \
  V(ArrayLengthChanged, "the array length changed")                            \
  V(GreaterThanMaxFastElementArray,                                            \
    "length is greater than the maximum for fast elements array")              \
  V(Float16NotYetSupported, "float16 is not supported as machine operation")

enum class DeoptimizeReason : uint8_t {
#define DEOPTIMIZE_REASON(Name, message) k##Name,
  DEOPTIMIZE_REASON_LIST(DEOPTIMIZE_REASON)
#undef DEOPTIMIZE_REASON
};

constexpr DeoptimizeReason kFirstDeoptimizeReason =
    DeoptimizeReason::kArrayBufferWasDetached;
constexpr DeoptimizeReason kLastDeoptimizeReason =
    DeoptimizeReason::kArrayLengthChanged;
static_assert(static_cast<int>(kFirstDeoptimizeReason) == 0);
constexpr int kDeoptimizeReasonCount =
    static_cast<int>(kLastDeoptimizeReason) + 1;

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream&, DeoptimizeReason);

size_t hash_value(DeoptimizeReason reason);

V8_EXPORT_PRIVATE char const* DeoptimizeReasonToString(DeoptimizeReason reason);

constexpr bool IsDeoptimizationWithoutCodeInvalidation(
    DeoptimizeReason reason) {
  // Maglev OSRs into Turbofan by first deoptimizing in order to restore the
  // unoptimized frame layout. Since no actual assumptions in the Maglev code
  // object are violated, it (and any associated cached optimized code) should
  // not be invalidated s.t. we may reenter it in the future.
  return reason == DeoptimizeReason::kPrepareForOnStackReplacement ||
         reason == DeoptimizeReason::kOSREarlyExit;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_DEOPTIMIZER_DEOPTIMIZE_REASON_H_
