// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_USE_INFO_H_
#define V8_COMPILER_USE_INFO_H_

#include "src/base/functional.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/feedback-source.h"
#include "src/compiler/globals.h"

namespace v8::internal::compiler {

// Enum to specify if `+0` and `-0` should be treated as the same value.
enum IdentifyZeros : uint8_t {
  // `+0` and `-0` should be treated as the same value.
  kIdentifyZeros,
  // `+0` and `-0` should be treated as different values.
  kDistinguishZeros
};

class Truncation;
size_t hash_value(const Truncation&);

class Truncation final {
 public:
  // Constructors.
  static Truncation None() {
    return Truncation(TruncationKind::kNone, kIdentifyZeros);
  }
  static Truncation Bool() {
    return Truncation(TruncationKind::kBool, kIdentifyZeros);
  }
  static Truncation Word32() {
    return Truncation(TruncationKind::kWord32, kIdentifyZeros);
  }
  static Truncation Word64() {
    return Truncation(TruncationKind::kWord64, kIdentifyZeros);
  }
  static Truncation OddballAndBigIntToNumber(
      IdentifyZeros identify_zeros = kDistinguishZeros) {
    return Truncation(TruncationKind::kOddballAndBigIntToNumber,
                      identify_zeros);
  }
  static Truncation Any(IdentifyZeros identify_zeros = kDistinguishZeros) {
    return Truncation(TruncationKind::kAny, identify_zeros);
  }

  static Truncation Generalize(Truncation t1, Truncation t2) {
    return Truncation(
        Generalize(t1.kind(), t2.kind()),
        GeneralizeIdentifyZeros(t1.identify_zeros(), t2.identify_zeros()));
  }

  // Queries.
  bool IsUnused() const { return kind_ == TruncationKind::kNone; }
  bool IsUsedAsBool() const {
    return LessGeneral(kind_, TruncationKind::kBool);
  }
  bool IsUsedAsWord32() const {
    return LessGeneral(kind_, TruncationKind::kWord32);
  }
  bool IsUsedAsWord64() const {
    DCHECK(Is64());
    return LessGeneral(kind_, TruncationKind::kWord64);
  }
  bool TruncatesOddballAndBigIntToNumber() const {
    return LessGeneral(kind_, TruncationKind::kOddballAndBigIntToNumber);
  }
  bool IdentifiesUndefinedAndZero() {
    return LessGeneral(kind_, TruncationKind::kWord32) ||
           LessGeneral(kind_, TruncationKind::kBool);
  }
  bool IdentifiesZeroAndMinusZero() const {
    return identify_zeros() == kIdentifyZeros;
  }

  // Operators.
  bool operator==(Truncation other) const {
    return kind() == other.kind() && identify_zeros() == other.identify_zeros();
  }
  bool operator!=(Truncation other) const { return !(*this == other); }

  // Debug utilities.
  const char* description() const;
  bool IsLessGeneralThan(Truncation other) const {
    return LessGeneral(kind(), other.kind()) &&
           LessGeneralIdentifyZeros(identify_zeros(), other.identify_zeros());
  }

  IdentifyZeros identify_zeros() const { return identify_zeros_; }

 private:
  enum class TruncationKind : uint8_t {
    kNone,
    kBool,
    kWord32,
    kWord64,
    kOddballAndBigIntToNumber,
    kAny
  };

  explicit Truncation(TruncationKind kind, IdentifyZeros identify_zeros)
      : kind_(kind), identify_zeros_(identify_zeros) {}

  TruncationKind kind() const { return kind_; }

  friend class SimplifiedLoweringVerifier;
  friend size_t hash_value(const Truncation&);
  TruncationKind kind_;
  IdentifyZeros identify_zeros_;

  static TruncationKind Generalize(TruncationKind rep1, TruncationKind rep2);
  static IdentifyZeros GeneralizeIdentifyZeros(IdentifyZeros i1,
                                               IdentifyZeros i2);
  static bool LessGeneral(TruncationKind rep1, TruncationKind rep2);
  static bool LessGeneralIdentifyZeros(IdentifyZeros u1, IdentifyZeros u2);
};

inline size_t hash_value(const Truncation& truncation) {
  return base::hash_combine(truncation.kind(), truncation.identify_zeros());
}

inline std::ostream& operator<<(std::ostream& os,
                                const Truncation& truncation) {
  return os << truncation.description();
}

enum class TypeCheckKind : uint8_t {
  kNone,
  kSignedSmall,
  kSigned32,
  kSigned64,
  kNumber,
  kNumberOrBoolean,
  kNumberOrOddball,
  kHeapObject,
  kBigInt,
  kBigInt64,
  kArrayIndex
};

inline std::ostream& operator<<(std::ostream& os, TypeCheckKind type_check) {
  switch (type_check) {
    case TypeCheckKind::kNone:
      return os << "None";
    case TypeCheckKind::kSignedSmall:
      return os << "SignedSmall";
    case TypeCheckKind::kSigned32:
      return os << "Signed32";
    case TypeCheckKind::kSigned64:
      return os << "Signed64";
    case TypeCheckKind::kNumber:
      return os << "Number";
    case TypeCheckKind::kNumberOrBoolean:
      return os << "NumberOrBoolean";
    case TypeCheckKind::kNumberOrOddball:
      return os << "NumberOrOddball";
    case TypeCheckKind::kHeapObject:
      return os << "HeapObject";
    case TypeCheckKind::kBigInt:
      return os << "BigInt";
    case TypeCheckKind::kBigInt64:
      return os << "BigInt64";
    case TypeCheckKind::kArrayIndex:
      return os << "ArrayIndex";
  }
  UNREACHABLE();
}

// The {UseInfo} class is used to describe a use of an input of a node.
//
// This information is used in two different ways, based on the phase:
//
// 1. During propagation, the use info is used to inform the input node
//    about what part of the input is used (we call this truncation) and what
//    is the preferred representation. For conversions that will require
//    checks, we also keep track of whether a minus zero check is needed.
//
// 2. During lowering, the use info is used to properly convert the input
//    to the preferred representation. The preferred representation might be
//    insufficient to do the conversion (e.g. word32->float64 conv), so we also
//    need the signedness information to produce the correct value.
//    Additionally, use info may contain {CheckParameters} which contains
//    information for the deoptimizer such as a CallIC on which speculation
//    should be disallowed if the check fails.
class UseInfo {
 public:
  UseInfo(MachineRepresentation representation, Truncation truncation,
          TypeCheckKind type_check = TypeCheckKind::kNone,
          const FeedbackSource& feedback = FeedbackSource())
      : representation_(representation),
        truncation_(truncation),
        type_check_(type_check),
        feedback_(feedback) {}
  static UseInfo TruncatingWord32() {
    return UseInfo(MachineRepresentation::kWord32, Truncation::Word32());
  }
  static UseInfo TruncatingWord64() {
    return UseInfo(MachineRepresentation::kWord64, Truncation::Word64());
  }
  static UseInfo CheckedBigIntTruncatingWord64(const FeedbackSource& feedback) {
    DCHECK(Is64());
    // Note that Trunction::Word64() can safely use kIdentifyZero, because
    // TypeCheckKind::kBigInt will make sure we deopt for anything other than
    // type BigInt anyway.
    return UseInfo(MachineRepresentation::kWord64, Truncation::Word64(),
                   TypeCheckKind::kBigInt, feedback);
  }
  static UseInfo CheckedBigInt64AsWord64(const FeedbackSource& feedback) {
    DCHECK(Is64());
    return UseInfo(MachineRepresentation::kWord64, Truncation::Any(),
                   TypeCheckKind::kBigInt64, feedback);
  }
  static UseInfo Word64(IdentifyZeros identify_zeros = kDistinguishZeros) {
    return UseInfo(MachineRepresentation::kWord64,
                   Truncation::Any(identify_zeros));
  }
  static UseInfo Word() {
    return UseInfo(MachineType::PointerRepresentation(), Truncation::Any());
  }
  static UseInfo Bool() {
    return UseInfo(MachineRepresentation::kBit, Truncation::Bool());
  }
  static UseInfo Float32() {
    return UseInfo(MachineRepresentation::kFloat32, Truncation::Any());
  }
  static UseInfo Float64() {
    return UseInfo(MachineRepresentation::kFloat64, Truncation::Any());
  }
  static UseInfo TruncatingFloat64(
      IdentifyZeros identify_zeros = kDistinguishZeros) {
    return UseInfo(MachineRepresentation::kFloat64,
                   Truncation::OddballAndBigIntToNumber(identify_zeros));
  }
  static UseInfo AnyTagged() {
    return UseInfo(MachineRepresentation::kTagged, Truncation::Any());
  }
  static UseInfo TaggedSigned() {
    return UseInfo(MachineRepresentation::kTaggedSigned, Truncation::Any());
  }
  static UseInfo TaggedPointer() {
    return UseInfo(MachineRepresentation::kTaggedPointer, Truncation::Any());
  }

  // Possibly deoptimizing conversions.
  static UseInfo CheckedTaggedAsArrayIndex(const FeedbackSource& feedback) {
    return UseInfo(MachineType::PointerRepresentation(),
                   Truncation::Any(kIdentifyZeros), TypeCheckKind::kArrayIndex,
                   feedback);
  }
  static UseInfo CheckedHeapObjectAsTaggedPointer(
      const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kTaggedPointer, Truncation::Any(),
                   TypeCheckKind::kHeapObject, feedback);
  }

  static UseInfo CheckedBigIntAsTaggedPointer(const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kTaggedPointer, Truncation::Any(),
                   TypeCheckKind::kBigInt, feedback);
  }

  static UseInfo CheckedSignedSmallAsTaggedSigned(
      const FeedbackSource& feedback,
      IdentifyZeros identify_zeros = kDistinguishZeros) {
    return UseInfo(MachineRepresentation::kTaggedSigned,
                   Truncation::Any(identify_zeros), TypeCheckKind::kSignedSmall,
                   feedback);
  }
  static UseInfo CheckedSignedSmallAsWord32(IdentifyZeros identify_zeros,
                                            const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kWord32,
                   Truncation::Any(identify_zeros), TypeCheckKind::kSignedSmall,
                   feedback);
  }
  static UseInfo CheckedSigned32AsWord32(IdentifyZeros identify_zeros,
                                         const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kWord32,
                   Truncation::Any(identify_zeros), TypeCheckKind::kSigned32,
                   feedback);
  }
  static UseInfo CheckedSigned64AsWord64(IdentifyZeros identify_zeros,
                                         const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kWord64,
                   Truncation::Any(identify_zeros), TypeCheckKind::kSigned64,
                   feedback);
  }
  static UseInfo CheckedNumberAsFloat64(IdentifyZeros identify_zeros,
                                        const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kFloat64,
                   Truncation::Any(identify_zeros), TypeCheckKind::kNumber,
                   feedback);
  }
  static UseInfo CheckedNumberAsWord32(const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kWord32, Truncation::Word32(),
                   TypeCheckKind::kNumber, feedback);
  }
  static UseInfo CheckedNumberOrBooleanAsFloat64(
      IdentifyZeros identify_zeros, const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kFloat64,
                   Truncation::Any(identify_zeros),
                   TypeCheckKind::kNumberOrBoolean, feedback);
  }
  static UseInfo CheckedNumberOrOddballAsFloat64(
      IdentifyZeros identify_zeros, const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kFloat64,
                   Truncation::Any(identify_zeros),
                   TypeCheckKind::kNumberOrOddball, feedback);
  }
  static UseInfo CheckedNumberOrOddballAsWord32(
      const FeedbackSource& feedback) {
    return UseInfo(MachineRepresentation::kWord32, Truncation::Word32(),
                   TypeCheckKind::kNumberOrOddball, feedback);
  }

  // Undetermined representation.
  static UseInfo Any() {
    return UseInfo(MachineRepresentation::kNone, Truncation::Any());
  }
  static UseInfo AnyTruncatingToBool() {
    return UseInfo(MachineRepresentation::kNone, Truncation::Bool());
  }

  // Value not used.
  static UseInfo None() {
    return UseInfo(MachineRepresentation::kNone, Truncation::None());
  }

  MachineRepresentation representation() const { return representation_; }
  Truncation truncation() const { return truncation_; }
  TypeCheckKind type_check() const { return type_check_; }
  CheckForMinusZeroMode minus_zero_check() const {
    return truncation().IdentifiesZeroAndMinusZero()
               ? CheckForMinusZeroMode::kDontCheckForMinusZero
               : CheckForMinusZeroMode::kCheckForMinusZero;
  }
  const FeedbackSource& feedback() const { return feedback_; }

 private:
  MachineRepresentation representation_;
  Truncation truncation_;
  TypeCheckKind type_check_;
  FeedbackSource feedback_;
};

inline bool operator==(const UseInfo& lhs, const UseInfo& rhs) {
  return lhs.representation() == rhs.representation() &&
         lhs.truncation() == rhs.truncation() &&
         lhs.type_check() == rhs.type_check() &&
         lhs.feedback() == rhs.feedback();
}

inline size_t hash_value(const UseInfo& use_info) {
  return base::hash_combine(use_info.representation(), use_info.truncation(),
                            use_info.type_check(), use_info.feedback());
}

inline std::ostream& operator<<(std::ostream& os, const UseInfo& use_info) {
  return os << use_info.representation() << ", " << use_info.truncation()
            << ", " << use_info.type_check() << ", " << use_info.feedback();
}

}  // namespace v8::internal::compiler

#endif  // V8_COMPILER_USE_INFO_H_
