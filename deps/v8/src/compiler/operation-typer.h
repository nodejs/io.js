// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_OPERATION_TYPER_H_
#define V8_COMPILER_OPERATION_TYPER_H_

#include "src/base/flags.h"
#include "src/compiler/opcodes.h"
#include "src/compiler/turbofan-types.h"

#define TYPER_SUPPORTED_MACHINE_BINOP_LIST(V) \
  V(Int32Add)                                 \
  V(Int32LessThanOrEqual)                     \
  V(Int64Add)                                 \
  V(Int32Sub)                                 \
  V(Int64Sub)                                 \
  V(Load)                                     \
  V(Uint32Div)                                \
  V(Uint64Div)                                \
  V(Uint32LessThan)                           \
  V(Uint32LessThanOrEqual)                    \
  V(Uint64LessThan)                           \
  V(Uint64LessThanOrEqual)                    \
  V(Word32And)                                \
  V(Word32Equal)                              \
  V(Word32Or)                                 \
  V(Word32Shl)                                \
  V(Word32Shr)                                \
  V(Word64And)                                \
  V(Word64Shl)                                \
  V(Word64Shr)

namespace v8 {
namespace internal {

// Forward declarations.
class Isolate;
class RangeType;
class Zone;

namespace compiler {

// Forward declarations.
class Operator;
class Type;
class TypeCache;

class V8_EXPORT_PRIVATE OperationTyper {
 public:
  OperationTyper(JSHeapBroker* broker, Zone* zone);

  // Typing Phi.
  Type Merge(Type left, Type right);

  Type ToPrimitive(Type type);
  Type ToNumber(Type type);
  Type ToNumberConvertBigInt(Type type);
  Type ToBigInt(Type type);
  Type ToBigIntConvertNumber(Type type);
  Type ToNumeric(Type type);
  Type ToBoolean(Type type);

  Type WeakenRange(Type current_range, Type previous_range);

// Unary operators.
#define DECLARE_METHOD(Name) Type Name(Type type);
  SIMPLIFIED_NUMBER_UNOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_BIGINT_UNOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_SPECULATIVE_NUMBER_UNOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_SPECULATIVE_BIGINT_UNOP_LIST(DECLARE_METHOD)
  DECLARE_METHOD(ConvertReceiver)
#undef DECLARE_METHOD

// Numeric binary operators.
#define DECLARE_METHOD(Name) Type Name(Type lhs, Type rhs);
  SIMPLIFIED_NUMBER_BINOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_BIGINT_BINOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_SPECULATIVE_NUMBER_BINOP_LIST(DECLARE_METHOD)
  SIMPLIFIED_SPECULATIVE_BIGINT_BINOP_LIST(DECLARE_METHOD)
  TYPER_SUPPORTED_MACHINE_BINOP_LIST(DECLARE_METHOD)
#undef DECLARE_METHOD

  Type ChangeUint32ToUint64(Type input);

  // Comparison operators.
  Type SameValue(Type lhs, Type rhs);
  Type SameValueNumbersOnly(Type lhs, Type rhs);
  Type StrictEqual(Type lhs, Type rhs);

  // Check operators.
  Type CheckBounds(Type index, Type length);
  Type CheckFloat64Hole(Type type);
  Type CheckNumber(Type type);
  Type CheckNumberFitsInt32(Type type);
  Type ConvertTaggedHoleToUndefined(Type type);

  Type TypeTypeGuard(const Operator* sigma_op, Type input);

  enum ComparisonOutcomeFlags {
    kComparisonTrue = 1,
    kComparisonFalse = 2,
    kComparisonUndefined = 4
  };

  Type singleton_false() const { return singleton_false_; }
  Type singleton_true() const { return singleton_true_; }

 private:
  using ComparisonOutcome = base::Flags<ComparisonOutcomeFlags>;

  ComparisonOutcome Invert(ComparisonOutcome);
  Type Invert(Type);
  Type FalsifyUndefined(ComparisonOutcome);

  Type Rangify(Type);
  Type AddRanger(double lhs_min, double lhs_max, double rhs_min,
                 double rhs_max);
  Type SubtractRanger(double lhs_min, double lhs_max, double rhs_min,
                      double rhs_max);
  Type MultiplyRanger(double lhs_min, double lhs_max, double rhs_min,
                      double rhs_max);

  Zone* zone() const { return zone_; }

  Zone* const zone_;
  TypeCache const* cache_;

  Type infinity_;
  Type minus_infinity_;
  Type singleton_NaN_string_;
  Type singleton_zero_string_;
  Type singleton_false_;
  Type singleton_true_;
  Type signed32ish_;
  Type unsigned32ish_;
  Type singleton_empty_string_;
  Type truish_;
  Type falsish_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_OPERATION_TYPER_H_
