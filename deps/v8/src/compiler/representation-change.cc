// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/representation-change.h"

#include <sstream>

#include "src/base/safe_conversions.h"
#include "src/compiler/js-heap-broker.h"
#include "src/compiler/machine-operator.h"
#include "src/compiler/node-matchers.h"
#include "src/compiler/simplified-lowering-verifier.h"
#include "src/compiler/simplified-operator.h"
#include "src/compiler/type-cache.h"
#include "src/heap/factory-inl.h"

namespace v8 {
namespace internal {
namespace compiler {

const char* Truncation::description() const {
  switch (kind()) {
    case TruncationKind::kNone:
      return "no-value-use";
    case TruncationKind::kBool:
      return "truncate-to-bool";
    case TruncationKind::kWord32:
      return "truncate-to-word32";
    case TruncationKind::kWord64:
      return "truncate-to-word64";
    case TruncationKind::kOddballAndBigIntToNumber:
      switch (identify_zeros()) {
        case kIdentifyZeros:
          return "truncate-oddball&bigint-to-number (identify zeros)";
        case kDistinguishZeros:
          return "truncate-oddball&bigint-to-number (distinguish zeros)";
      }
    case TruncationKind::kAny:
      switch (identify_zeros()) {
        case kIdentifyZeros:
          return "no-truncation (but identify zeros)";
        case kDistinguishZeros:
          return "no-truncation (but distinguish zeros)";
      }
  }
  UNREACHABLE();
}

// Partial order for truncations:
//
//               kAny <-------+
//                 ^          |
//                 |          |
//  kOddballAndBigIntToNumber |
//               ^            |
//               /            |
//        kWord64             |
//             ^              |
//             |              |
//        kWord32           kBool
//              ^            ^
//              \            /
//               \          /
//                \        /
//                 \      /
//                  \    /
//                  kNone
//
// TODO(jarin) We might consider making kBool < kOddballAndBigIntToNumber.

// static
Truncation::TruncationKind Truncation::Generalize(TruncationKind rep1,
                                                  TruncationKind rep2) {
  if (LessGeneral(rep1, rep2)) return rep2;
  if (LessGeneral(rep2, rep1)) return rep1;
  // Handle the generalization of float64-representable values.
  if (LessGeneral(rep1, TruncationKind::kOddballAndBigIntToNumber) &&
      LessGeneral(rep2, TruncationKind::kOddballAndBigIntToNumber)) {
    return TruncationKind::kOddballAndBigIntToNumber;
  }
  // Handle the generalization of any-representable values.
  if (LessGeneral(rep1, TruncationKind::kAny) &&
      LessGeneral(rep2, TruncationKind::kAny)) {
    return TruncationKind::kAny;
  }
  // All other combinations are illegal.
  FATAL("Tried to combine incompatible truncations");
}

// static
IdentifyZeros Truncation::GeneralizeIdentifyZeros(IdentifyZeros i1,
                                                  IdentifyZeros i2) {
  if (i1 == i2) {
    return i1;
  } else {
    return kDistinguishZeros;
  }
}

// static
bool Truncation::LessGeneral(TruncationKind rep1, TruncationKind rep2) {
  switch (rep1) {
    case TruncationKind::kNone:
      return true;
    case TruncationKind::kBool:
      return rep2 == TruncationKind::kBool || rep2 == TruncationKind::kAny;
    case TruncationKind::kWord32:
      return rep2 == TruncationKind::kWord32 ||
             rep2 == TruncationKind::kWord64 ||
             rep2 == TruncationKind::kOddballAndBigIntToNumber ||
             rep2 == TruncationKind::kAny;
    case TruncationKind::kWord64:
      return rep2 == TruncationKind::kWord64 ||
             rep2 == TruncationKind::kOddballAndBigIntToNumber ||
             rep2 == TruncationKind::kAny;
    case TruncationKind::kOddballAndBigIntToNumber:
      return rep2 == TruncationKind::kOddballAndBigIntToNumber ||
             rep2 == TruncationKind::kAny;
    case TruncationKind::kAny:
      return rep2 == TruncationKind::kAny;
  }
  UNREACHABLE();
}

// static
bool Truncation::LessGeneralIdentifyZeros(IdentifyZeros i1, IdentifyZeros i2) {
  return i1 == i2 || i1 == kIdentifyZeros;
}

namespace {

bool IsWord(MachineRepresentation rep) {
  return rep == MachineRepresentation::kWord8 ||
         rep == MachineRepresentation::kWord16 ||
         rep == MachineRepresentation::kWord32;
}

bool TypeCheckIsBigInt(TypeCheckKind type_check) {
  return type_check == TypeCheckKind::kBigInt ||
         type_check == TypeCheckKind::kBigInt64;
}

}  // namespace

RepresentationChanger::RepresentationChanger(
    JSGraph* jsgraph, JSHeapBroker* broker,
    SimplifiedLoweringVerifier* verifier)
    : cache_(TypeCache::Get()),
      jsgraph_(jsgraph),
      broker_(broker),
      verifier_(verifier),
      testing_type_errors_(false),
      type_error_(false) {}

// Changes representation from {output_rep} to {use_rep}. The {truncation}
// parameter is only used for checking - if the changer cannot figure
// out signedness for the word32->float64 conversion, then we check that the
// uses truncate to word32 (so they do not care about signedness).
Node* RepresentationChanger::GetRepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  // We are currently not inserting conversions in machine graphs.
  // We might add that, though.
  DCHECK_IMPLIES(!output_type.IsNone(), !output_type.Is(Type::Machine()));
  if (output_rep == MachineRepresentation::kNone && !output_type.IsNone()) {
    // The output representation should be set if the type is inhabited (i.e.,
    // if the value is possible).
    return TypeError(node, output_rep, output_type, use_info.representation());
  }

  // Rematerialize any truncated BigInt if user is not expecting a BigInt.
  if (output_type.Is(Type::BigInt()) &&
      output_rep == MachineRepresentation::kWord64 &&
      !TypeCheckIsBigInt(use_info.type_check())) {
    if (output_type.Is(Type::UnsignedBigInt64())) {
      node = InsertConversion(node, simplified()->ChangeUint64ToBigInt(),
                              use_node);
    } else {
      node =
          InsertConversion(node, simplified()->ChangeInt64ToBigInt(), use_node);
    }
    output_rep = MachineRepresentation::kTaggedPointer;
  }

  // Handle the no-op shortcuts when no checking is necessary.
  if (use_info.type_check() == TypeCheckKind::kNone ||
      // TODO(nicohartmann@, chromium:1077804): Ignoring {use_info.type_check()}
      // in case the representation already matches is not correct. For now,
      // this behavior is disabled only for TypeCheckKind::kBigInt, but should
      // be fixed for all other type checks.
      (output_rep != MachineRepresentation::kWord32 &&
       !TypeCheckIsBigInt(use_info.type_check()))) {
    if (use_info.representation() == output_rep) {
      // Representations are the same. That's a no-op.
      return node;
    }
    if (IsWord(use_info.representation()) && IsWord(output_rep)) {
      // Both are words less than or equal to 32-bits.
      // Since loads of integers from memory implicitly sign or zero extend the
      // value to the full machine word size and stores implicitly truncate,
      // no representation change is necessary.
      return node;
    }
  }

  switch (use_info.representation()) {
    case MachineRepresentation::kTaggedSigned:
      DCHECK(use_info.type_check() == TypeCheckKind::kNone ||
             use_info.type_check() == TypeCheckKind::kSignedSmall);
      return GetTaggedSignedRepresentationFor(node, output_rep, output_type,
                                              use_node, use_info);
    case MachineRepresentation::kTaggedPointer:
      DCHECK(use_info.type_check() == TypeCheckKind::kNone ||
             use_info.type_check() == TypeCheckKind::kHeapObject ||
             use_info.type_check() == TypeCheckKind::kBigInt);
      return GetTaggedPointerRepresentationFor(node, output_rep, output_type,
                                               use_node, use_info);
    case MachineRepresentation::kTagged:
      DCHECK_EQ(TypeCheckKind::kNone, use_info.type_check());
      return GetTaggedRepresentationFor(node, output_rep, output_type,
                                        use_info.truncation());
    case MachineRepresentation::kFloat16:
    case MachineRepresentation::kFloat32:
      DCHECK_EQ(TypeCheckKind::kNone, use_info.type_check());
      return GetFloat32RepresentationFor(node, output_rep, output_type,
                                         use_info.truncation());
    case MachineRepresentation::kFloat64:
      DCHECK(use_info.type_check() == TypeCheckKind::kNone ||
             use_info.type_check() == TypeCheckKind::kNumber ||
             use_info.type_check() == TypeCheckKind::kNumberOrBoolean ||
             use_info.type_check() == TypeCheckKind::kNumberOrOddball);
      return GetFloat64RepresentationFor(node, output_rep, output_type,
                                         use_node, use_info);
    case MachineRepresentation::kBit:
      DCHECK_EQ(TypeCheckKind::kNone, use_info.type_check());
      return GetBitRepresentationFor(node, output_rep, output_type);
    case MachineRepresentation::kWord8:
    case MachineRepresentation::kWord16:
    case MachineRepresentation::kWord32:
      return GetWord32RepresentationFor(node, output_rep, output_type, use_node,
                                        use_info);
    case MachineRepresentation::kWord64:
      DCHECK(use_info.type_check() == TypeCheckKind::kNone ||
             use_info.type_check() == TypeCheckKind::kSigned64 ||
             TypeCheckIsBigInt(use_info.type_check()) ||
             use_info.type_check() == TypeCheckKind::kArrayIndex);
      return GetWord64RepresentationFor(node, output_rep, output_type, use_node,
                                        use_info);
    case MachineRepresentation::kSimd128:
    case MachineRepresentation::kSimd256:
    case MachineRepresentation::kNone:
      return node;
    case MachineRepresentation::kCompressed:
    case MachineRepresentation::kCompressedPointer:
    case MachineRepresentation::kSandboxedPointer:
    case MachineRepresentation::kProtectedPointer:
    case MachineRepresentation::kIndirectPointer:
    case MachineRepresentation::kMapWord:
      UNREACHABLE();
  }
  UNREACHABLE();
}

Node* RepresentationChanger::GetTaggedSignedRepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kNumberConstant:
      if (output_type.Is(Type::SignedSmall())) {
        return node;
      }
      break;
    default:
      break;
  }
  // Select the correct X -> Tagged operator.
  const Operator* op;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kTaggedSigned),
        node);
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Signed31())) {
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(Type::Signed32())) {
      if (SmiValuesAre32Bits()) {
        op = simplified()->ChangeInt32ToTagged();
      } else if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
        op = simplified()->CheckedInt32ToTaggedSigned(use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kTaggedSigned);
      }
    } else if (output_type.Is(Type::Unsigned32()) &&
               use_info.type_check() == TypeCheckKind::kSignedSmall) {
      op = simplified()->CheckedUint32ToTaggedSigned(use_info.feedback());
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(Type::Signed31())) {
      // int64 -> int32 -> tagged signed
      node = InsertTruncateInt64ToInt32(node);
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(Type::Signed32()) && SmiValuesAre32Bits()) {
      // int64 -> int32 -> tagged signed
      node = InsertTruncateInt64ToInt32(node);
      op = simplified()->ChangeInt32ToTagged();
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      if (output_type.Is(cache_->kPositiveSafeInteger)) {
        op = simplified()->CheckedUint64ToTaggedSigned(use_info.feedback());
      } else if (output_type.Is(cache_->kSafeInteger)) {
        op = simplified()->CheckedInt64ToTaggedSigned(use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kTaggedSigned);
      }
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else if (output_rep == MachineRepresentation::kFloat64) {
    if (output_type.Is(Type::Signed31())) {
      // float64 -> int32 -> tagged signed
      node = InsertChangeFloat64ToInt32(node);
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(Type::Signed32())) {
      // float64 -> int32 -> tagged signed
      node = InsertChangeFloat64ToInt32(node);
      if (SmiValuesAre32Bits()) {
        op = simplified()->ChangeInt32ToTagged();
      } else if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
        op = simplified()->CheckedInt32ToTaggedSigned(use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kTaggedSigned);
      }
    } else if (output_type.Is(Type::Unsigned32()) &&
               use_info.type_check() == TypeCheckKind::kSignedSmall) {
      // float64 -> uint32 -> tagged signed
      node = InsertChangeFloat64ToUint32(node);
      op = simplified()->CheckedUint32ToTaggedSigned(use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      node = InsertCheckedFloat64ToInt32(
          node,
          output_type.Maybe(Type::MinusZero())
              ? CheckForMinusZeroMode::kCheckForMinusZero
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback(), use_node);
      if (SmiValuesAre32Bits()) {
        op = simplified()->ChangeInt32ToTagged();
      } else {
        op = simplified()->CheckedInt32ToTaggedSigned(use_info.feedback());
      }
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else if (output_rep == MachineRepresentation::kFloat32) {
    if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      node = InsertChangeFloat32ToFloat64(node);
      node = InsertCheckedFloat64ToInt32(
          node,
          output_type.Maybe(Type::MinusZero())
              ? CheckForMinusZeroMode::kCheckForMinusZero
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback(), use_node);
      if (SmiValuesAre32Bits()) {
        op = simplified()->ChangeInt32ToTagged();
      } else {
        op = simplified()->CheckedInt32ToTaggedSigned(use_info.feedback());
      }
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else if (CanBeTaggedPointer(output_rep)) {
    if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      op = simplified()->CheckedTaggedToTaggedSigned(use_info.feedback());
    } else if (output_type.Is(Type::SignedSmall())) {
      op = simplified()->ChangeTaggedToTaggedSigned();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else if (output_rep == MachineRepresentation::kBit) {
    if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      // TODO(turbofan): Consider adding a Bailout operator that just deopts.
      // Also use that for MachineRepresentation::kPointer case above.
      node = InsertChangeBitToTagged(node);
      op = simplified()->CheckedTaggedToTaggedSigned(use_info.feedback());
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedSigned);
    }
  } else {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kTaggedSigned);
  }
  return InsertConversion(node, op, use_node);
}

Node* RepresentationChanger::GetTaggedPointerRepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant:
      if (TypeCheckIsBigInt(use_info.type_check())) break;
      return node;  // No change necessary.
    case IrOpcode::kInt32Constant:
    case IrOpcode::kFloat64Constant:
    case IrOpcode::kFloat32Constant:
      UNREACHABLE();
    default:
      break;
  }
  // Select the correct X -> TaggedPointer operator.
  Operator const* op;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kTaggedPointer),
        node);
  }

  if (TypeCheckIsBigInt(use_info.type_check()) &&
      !output_type.Is(Type::BigInt())) {
    // BigInt checks can only be performed on tagged representations. Note that
    // a corresponding check is inserted down below.
    if (!CanBeTaggedPointer(output_rep)) {
      Node* unreachable =
          InsertUnconditionalDeopt(use_node, DeoptimizeReason::kNotABigInt);
      return jsgraph()->graph()->NewNode(
          jsgraph()->common()->DeadValue(MachineRepresentation::kTaggedPointer),
          unreachable);
    }
  }

  if (output_rep == MachineRepresentation::kBit) {
    if (output_type.Is(Type::Boolean())) {
      op = simplified()->ChangeBitToTagged();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTagged);
    }
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Unsigned32())) {
      // uint32 -> float64 -> tagged
      node = InsertChangeUint32ToFloat64(node);
    } else if (output_type.Is(Type::Signed32())) {
      // int32 -> float64 -> tagged
      node = InsertChangeInt32ToFloat64(node);
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedPointer);
    }
    op = simplified()->ChangeFloat64ToTaggedPointer();
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(cache_->kSafeInteger)) {
      // int64 -> float64 -> tagged pointer
      op = machine()->ChangeInt64ToFloat64();
      node = jsgraph()->graph()->NewNode(op, node);
      op = simplified()->ChangeFloat64ToTaggedPointer();
    } else if (output_type.Is(Type::SignedBigInt64()) &&
               use_info.type_check() == TypeCheckKind::kBigInt) {
      op = simplified()->ChangeInt64ToBigInt();
    } else if (output_type.Is(Type::UnsignedBigInt64()) &&
               use_info.type_check() == TypeCheckKind::kBigInt) {
      op = simplified()->ChangeUint64ToBigInt();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedPointer);
    }
  } else if (output_rep == MachineRepresentation::kFloat32) {
    if (output_type.Is(Type::Number())) {
      // float32 -> float64 -> tagged
      node = InsertChangeFloat32ToFloat64(node);
      op = simplified()->ChangeFloat64ToTaggedPointer();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedPointer);
    }
  } else if (output_rep == MachineRepresentation::kFloat64) {
    if (output_type.Is(Type::Number())) {
      // float64 -> tagged
      op = simplified()->ChangeFloat64ToTaggedPointer();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTaggedPointer);
    }
  } else if (IsAnyTagged(output_rep)) {
    if (use_info.type_check() == TypeCheckKind::kBigInt) {
      if (output_type.Is(Type::BigInt())) {
        DCHECK_NE(output_rep, MachineRepresentation::kTaggedSigned);
        return node;
      }
      op = simplified()->CheckBigInt(use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kBigInt64) {
      if (output_type.Is(Type::SignedBigInt64())) {
        DCHECK_NE(output_rep, MachineRepresentation::kTaggedSigned);
        return node;
      }
      if (!output_type.Is(Type::BigInt())) {
        node = InsertConversion(
            node, simplified()->CheckBigInt(use_info.feedback()), use_node);
      }
      op = simplified()->CheckedBigIntToBigInt64(use_info.feedback());
    } else if (output_rep == MachineRepresentation::kTaggedPointer ||
               !output_type.Maybe(Type::SignedSmall())) {
      DCHECK_NE(output_rep, MachineRepresentation::kTaggedSigned);
      return node;
    } else {
      op = simplified()->CheckedTaggedToTaggedPointer(use_info.feedback());
    }
  } else {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kTaggedPointer);
  }
  return InsertConversion(node, op, use_node);
}

Node* RepresentationChanger::GetTaggedRepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Truncation truncation) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kNumberConstant:
    case IrOpcode::kHeapConstant:
      return node;  // No change necessary.
    case IrOpcode::kInt32Constant:
    case IrOpcode::kFloat64Constant:
    case IrOpcode::kFloat32Constant:
      UNREACHABLE();
    default:
      break;
  }
  if (output_rep == MachineRepresentation::kTaggedSigned ||
      output_rep == MachineRepresentation::kTaggedPointer ||
      output_rep == MachineRepresentation::kMapWord) {
    // this is a no-op.
    return node;
  }
  // Select the correct X -> Tagged operator.
  const Operator* op;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kTagged), node);
  } else if (output_rep == MachineRepresentation::kBit) {
    if (output_type.Is(Type::Boolean())) {
      op = simplified()->ChangeBitToTagged();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTagged);
    }
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Signed31())) {
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(Type::Signed32()) ||
               (output_type.Is(Type::Signed32OrMinusZero()) &&
                truncation.IdentifiesZeroAndMinusZero())) {
      op = simplified()->ChangeInt32ToTagged();
    } else if (output_type.Is(Type::Unsigned32()) ||
               (output_type.Is(Type::Unsigned32OrMinusZero()) &&
                truncation.IdentifiesZeroAndMinusZero()) ||
               truncation.IsUsedAsWord32()) {
      // Either the output is uint32 or the uses only care about the
      // low 32 bits (so we can pick uint32 safely).
      op = simplified()->ChangeUint32ToTagged();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTagged);
    }
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(Type::Signed31())) {
      // int64 -> int32 -> tagged signed
      node = InsertTruncateInt64ToInt32(node);
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(Type::Signed32())) {
      // int64 -> int32 -> tagged
      node = InsertTruncateInt64ToInt32(node);
      op = simplified()->ChangeInt32ToTagged();
    } else if (output_type.Is(Type::Unsigned32())) {
      // int64 -> uint32 -> tagged
      node = InsertTruncateInt64ToInt32(node);
      op = simplified()->ChangeUint32ToTagged();
    } else if (output_type.Is(cache_->kPositiveSafeInteger)) {
      // uint64 -> tagged
      op = simplified()->ChangeUint64ToTagged();
    } else if (output_type.Is(cache_->kSafeInteger)) {
      // int64 -> tagged
      op = simplified()->ChangeInt64ToTagged();
    } else if (output_type.Is(Type::SignedBigInt64())) {
      // int64 -> BigInt
      op = simplified()->ChangeInt64ToBigInt();
    } else if (output_type.Is(Type::UnsignedBigInt64())) {
      // uint64 -> BigInt
      op = simplified()->ChangeUint64ToBigInt();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTagged);
    }
  } else if (output_rep ==
             MachineRepresentation::kFloat32) {  // float32 -> float64 -> tagged
    node = InsertChangeFloat32ToFloat64(node);
    op = simplified()->ChangeFloat64ToTagged(
        output_type.Maybe(Type::MinusZero())
            ? CheckForMinusZeroMode::kCheckForMinusZero
            : CheckForMinusZeroMode::kDontCheckForMinusZero);
  } else if (output_rep == MachineRepresentation::kFloat64) {
    if (output_type.Is(Type::Signed31())) {  // float64 -> int32 -> tagged
      node = InsertChangeFloat64ToInt32(node);
      op = simplified()->ChangeInt31ToTaggedSigned();
    } else if (output_type.Is(
                   Type::Signed32())) {  // float64 -> int32 -> tagged
      node = InsertChangeFloat64ToInt32(node);
      op = simplified()->ChangeInt32ToTagged();
    } else if (output_type.Is(
                   Type::Unsigned32())) {  // float64 -> uint32 -> tagged
      node = InsertChangeFloat64ToUint32(node);
      op = simplified()->ChangeUint32ToTagged();
    } else if (output_type.Is(Type::Number()) ||
               (output_type.Is(Type::NumberOrOddball()) &&
                truncation.TruncatesOddballAndBigIntToNumber())) {
      op = simplified()->ChangeFloat64ToTagged(
          output_type.Maybe(Type::MinusZero())
              ? CheckForMinusZeroMode::kCheckForMinusZero
              : CheckForMinusZeroMode::kDontCheckForMinusZero);
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kTagged);
    }
  } else {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kTagged);
  }
  return jsgraph()->graph()->NewNode(op, node);
}

Node* RepresentationChanger::GetFloat32RepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Truncation truncation) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kNumberConstant:
      return jsgraph()->Float32Constant(
          DoubleToFloat32(OpParameter<double>(node->op())));
    case IrOpcode::kInt32Constant:
    case IrOpcode::kFloat64Constant:
    case IrOpcode::kFloat32Constant:
      UNREACHABLE();
    default:
      break;
  }
  // Select the correct X -> Float32 operator.
  const Operator* op = nullptr;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kFloat32), node);
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Signed32())) {
      // int32 -> float64 -> float32
      op = machine()->ChangeInt32ToFloat64();
      node = jsgraph()->graph()->NewNode(op, node);
      op = machine()->TruncateFloat64ToFloat32();
    } else if (output_type.Is(Type::Unsigned32()) ||
               truncation.IsUsedAsWord32()) {
      // Either the output is uint32 or the uses only care about the
      // low 32 bits (so we can pick uint32 safely).

      // uint32 -> float64 -> float32
      op = machine()->ChangeUint32ToFloat64();
      node = jsgraph()->graph()->NewNode(op, node);
      op = machine()->TruncateFloat64ToFloat32();
    }
  } else if (IsAnyTagged(output_rep)) {
    if (output_type.Is(Type::NumberOrOddball())) {
      // tagged -> float64 -> float32
      if (output_type.Is(Type::Number())) {
        op = simplified()->ChangeTaggedToFloat64();
      } else {
        op = simplified()->TruncateTaggedToFloat64();
      }
      node = jsgraph()->graph()->NewNode(op, node);
      op = machine()->TruncateFloat64ToFloat32();
    }
  } else if (output_rep == MachineRepresentation::kFloat64) {
    op = machine()->TruncateFloat64ToFloat32();
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(cache_->kSafeInteger)) {
      // int64 -> float64 -> float32
      op = machine()->ChangeInt64ToFloat64();
      node = jsgraph()->graph()->NewNode(op, node);
      op = machine()->TruncateFloat64ToFloat32();
    }
  }
  if (op == nullptr) {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kFloat32);
  }
  return jsgraph()->graph()->NewNode(op, node);
}

Node* RepresentationChanger::GetFloat64RepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  NumberMatcher m(node);
  if (m.HasResolvedValue()) {
    // BigInts are not used as number constants.
    DCHECK(!TypeCheckIsBigInt(use_info.type_check()));
    switch (use_info.type_check()) {
      case TypeCheckKind::kNone:
      case TypeCheckKind::kNumber:
      case TypeCheckKind::kNumberOrBoolean:
      case TypeCheckKind::kNumberOrOddball:
        return jsgraph()->Float64Constant(m.ResolvedValue());
      case TypeCheckKind::kBigInt:
      case TypeCheckKind::kBigInt64:
      case TypeCheckKind::kHeapObject:
      case TypeCheckKind::kSigned32:
      case TypeCheckKind::kSigned64:
      case TypeCheckKind::kSignedSmall:
      case TypeCheckKind::kArrayIndex:
        break;
    }
  }
  // Select the correct X -> Float64 operator.
  const Operator* op = nullptr;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kFloat64), node);
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Signed32()) ||
        (output_type.Is(Type::Signed32OrMinusZero()) &&
         use_info.truncation().IdentifiesZeroAndMinusZero())) {
      op = machine()->ChangeInt32ToFloat64();
    } else if (output_type.Is(Type::Unsigned32()) ||
               (output_type.Is(Type::Unsigned32OrMinusZero()) &&
                use_info.truncation().IdentifiesZeroAndMinusZero()) ||
               use_info.truncation().IsUsedAsWord32()) {
      // Either the output is uint32 or the uses only care about the
      // low 32 bits (so we can pick uint32 safely).
      op = machine()->ChangeUint32ToFloat64();
    }
  } else if (output_rep == MachineRepresentation::kBit) {
    CHECK(output_type.Is(Type::Boolean()));
    if (use_info.truncation().TruncatesOddballAndBigIntToNumber() ||
        use_info.type_check() == TypeCheckKind::kNumberOrBoolean ||
        use_info.type_check() == TypeCheckKind::kNumberOrOddball) {
      op = machine()->ChangeUint32ToFloat64();
    } else {
      CHECK_NE(use_info.type_check(), TypeCheckKind::kNone);
      Node* unreachable =
          InsertUnconditionalDeopt(use_node, DeoptimizeReason::kNotAHeapNumber);
      return jsgraph()->graph()->NewNode(
          jsgraph()->common()->DeadValue(MachineRepresentation::kFloat64),
          unreachable);
    }
  } else if (IsAnyTagged(output_rep)) {
    if (output_type.Is(Type::Undefined())) {
      if (use_info.type_check() == TypeCheckKind::kNumberOrOddball ||
          (use_info.type_check() == TypeCheckKind::kNone &&
           use_info.truncation().TruncatesOddballAndBigIntToNumber())) {
        return jsgraph()->Float64Constant(
            std::numeric_limits<double>::quiet_NaN());
      } else {
        DCHECK(use_info.type_check() == TypeCheckKind::kNone ||
               use_info.type_check() == TypeCheckKind::kNumber ||
               use_info.type_check() == TypeCheckKind::kNumberOrBoolean);
        Node* unreachable = InsertUnconditionalDeopt(
            use_node, use_info.type_check() == TypeCheckKind::kNumber
                          ? DeoptimizeReason::kNotANumber
                          : DeoptimizeReason::kNotANumberOrBoolean);
        return jsgraph()->graph()->NewNode(
            jsgraph()->common()->DeadValue(MachineRepresentation::kFloat64),
            unreachable);
      }
    } else if (output_rep == MachineRepresentation::kTaggedSigned) {
      node = InsertChangeTaggedSignedToInt32(node);
      op = machine()->ChangeInt32ToFloat64();
    } else if (output_type.Is(Type::Number())) {
      op = simplified()->ChangeTaggedToFloat64();
    } else if ((output_type.Is(Type::NumberOrOddball()) &&
                use_info.truncation().TruncatesOddballAndBigIntToNumber()) ||
               output_type.Is(Type::NumberOrHole())) {
      // JavaScript 'null' is an Oddball that results in +0 when truncated to
      // Number. In a context like -0 == null, which must evaluate to false,
      // this truncation must not happen. For this reason we restrict this
      // case to when either the user explicitly requested a float (and thus
      // wants +0 if null is the input) or we know from the types that the
      // input can only be Number | Hole. The latter is necessary to handle
      // the operator CheckFloat64Hole. We did not put in the type (Number |
      // Oddball \ Null) to discover more bugs related to this conversion via
      // crashes.
      op = simplified()->TruncateTaggedToFloat64();
    } else if (use_info.type_check() == TypeCheckKind::kNumber ||
               (use_info.type_check() == TypeCheckKind::kNumberOrOddball &&
                !output_type.Maybe(Type::BooleanOrNullOrNumber()))) {
      op = simplified()->CheckedTaggedToFloat64(CheckTaggedInputMode::kNumber,
                                                use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kNumberOrBoolean) {
      op = simplified()->CheckedTaggedToFloat64(
          CheckTaggedInputMode::kNumberOrBoolean, use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kNumberOrOddball) {
      op = simplified()->CheckedTaggedToFloat64(
          CheckTaggedInputMode::kNumberOrOddball, use_info.feedback());
    }
  } else if (output_rep == MachineRepresentation::kFloat32) {
    op = machine()->ChangeFloat32ToFloat64();
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(cache_->kSafeInteger)) {
      op = machine()->ChangeInt64ToFloat64();
    }
  }
  if (op == nullptr) {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kFloat64);
  }
  return InsertConversion(node, op, use_node);
}

Node* RepresentationChanger::MakeTruncatedInt32Constant(double value) {
  return jsgraph()->Int32Constant(DoubleToInt32(value));
}

Node* RepresentationChanger::InsertUnconditionalDeopt(
    Node* node, DeoptimizeReason reason, const FeedbackSource& feedback) {
  Node* effect = NodeProperties::GetEffectInput(node);
  Node* control = NodeProperties::GetControlInput(node);
  effect =
      jsgraph()->graph()->NewNode(simplified()->CheckIf(reason, feedback),
                                  jsgraph()->Int32Constant(0), effect, control);
  Node* unreachable = effect = jsgraph()->graph()->NewNode(
      jsgraph()->common()->Unreachable(), effect, control);
  NodeProperties::ReplaceEffectInput(node, effect);
  return unreachable;
}

Node* RepresentationChanger::GetWord32RepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kInt32Constant:
    case IrOpcode::kInt64Constant:
    case IrOpcode::kFloat32Constant:
    case IrOpcode::kFloat64Constant:
      UNREACHABLE();
    case IrOpcode::kNumberConstant: {
      double const fv = OpParameter<double>(node->op());
      if (use_info.type_check() == TypeCheckKind::kNone ||
          ((use_info.type_check() == TypeCheckKind::kSignedSmall ||
            use_info.type_check() == TypeCheckKind::kSigned32 ||
            use_info.type_check() == TypeCheckKind::kNumber ||
            use_info.type_check() == TypeCheckKind::kNumberOrOddball ||
            use_info.type_check() == TypeCheckKind::kArrayIndex) &&
           IsInt32Double(fv))) {
        return InsertTypeOverrideForVerifier(NodeProperties::GetType(node),
                                             MakeTruncatedInt32Constant(fv));
      }
      break;
    }
    default:
      break;
  }

  // Select the correct X -> Word32 operator.
  const Operator* op = nullptr;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kWord32), node);
  } else if (output_rep == MachineRepresentation::kBit) {
    CHECK(output_type.Is(Type::Boolean()));
    if (use_info.truncation().IsUsedAsWord32()) {
      return node;
    } else {
      CHECK(Truncation::Any(kIdentifyZeros)
                .IsLessGeneralThan(use_info.truncation()));
      CHECK_NE(use_info.type_check(), TypeCheckKind::kNone);
      CHECK_NE(use_info.type_check(), TypeCheckKind::kNumberOrOddball);
      Node* unreachable =
          InsertUnconditionalDeopt(use_node, DeoptimizeReason::kNotASmi);
      return jsgraph()->graph()->NewNode(
          jsgraph()->common()->DeadValue(MachineRepresentation::kWord32),
          unreachable);
    }
  } else if (output_rep == MachineRepresentation::kFloat64) {
    if (output_type.Is(Type::Signed32())) {
      op = machine()->ChangeFloat64ToInt32();
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall ||
               use_info.type_check() == TypeCheckKind::kSigned32 ||
               use_info.type_check() == TypeCheckKind::kArrayIndex) {
      op = simplified()->CheckedFloat64ToInt32(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else if (output_type.Is(Type::Unsigned32())) {
      op = machine()->ChangeFloat64ToUint32();
    } else if (use_info.truncation().IsUsedAsWord32()) {
      op = machine()->TruncateFloat64ToWord32();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord32);
    }
  } else if (output_rep == MachineRepresentation::kFloat32) {
    node = InsertChangeFloat32ToFloat64(node);  // float32 -> float64 -> int32
    if (output_type.Is(Type::Signed32())) {
      op = machine()->ChangeFloat64ToInt32();
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall ||
               use_info.type_check() == TypeCheckKind::kSigned32 ||
               use_info.type_check() == TypeCheckKind::kArrayIndex) {
      op = simplified()->CheckedFloat64ToInt32(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else if (output_type.Is(Type::Unsigned32())) {
      op = machine()->ChangeFloat64ToUint32();
    } else if (use_info.truncation().IsUsedAsWord32()) {
      op = machine()->TruncateFloat64ToWord32();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord32);
    }
  } else if (IsAnyTagged(output_rep)) {
    if (output_rep == MachineRepresentation::kTaggedSigned &&
        output_type.Is(Type::SignedSmall())) {
      op = simplified()->ChangeTaggedSignedToInt32();
    } else if (output_type.Is(Type::Signed32())) {
      op = simplified()->ChangeTaggedToInt32();
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall) {
      op = simplified()->CheckedTaggedSignedToInt32(use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kSigned32) {
      op = simplified()->CheckedTaggedToInt32(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kArrayIndex) {
      op = simplified()->CheckedTaggedToArrayIndex(use_info.feedback());
    } else if (output_type.Is(Type::Unsigned32())) {
      op = simplified()->ChangeTaggedToUint32();
    } else if (use_info.truncation().IsUsedAsWord32()) {
      if (output_type.Is(Type::NumberOrOddballOrHole())) {
        op = simplified()->TruncateTaggedToWord32();
      } else if (use_info.type_check() == TypeCheckKind::kNumber) {
        op = simplified()->CheckedTruncateTaggedToWord32(
            CheckTaggedInputMode::kNumber, use_info.feedback());
      } else if (use_info.type_check() == TypeCheckKind::kNumberOrOddball) {
        op = simplified()->CheckedTruncateTaggedToWord32(
            CheckTaggedInputMode::kNumberOrOddball, use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kWord32);
      }
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord32);
    }
  } else if (output_rep == MachineRepresentation::kWord32) {
    // Only the checked case should get here, the non-checked case is
    // handled in GetRepresentationFor.
    if (use_info.type_check() == TypeCheckKind::kSignedSmall ||
        use_info.type_check() == TypeCheckKind::kSigned32 ||
        use_info.type_check() == TypeCheckKind::kArrayIndex) {
      bool identify_zeros = use_info.truncation().IdentifiesZeroAndMinusZero();
      if (output_type.Is(Type::Signed32()) ||
          (identify_zeros && output_type.Is(Type::Signed32OrMinusZero()))) {
        return node;
      } else if (output_type.Is(Type::Unsigned32()) ||
                 (identify_zeros &&
                  output_type.Is(Type::Unsigned32OrMinusZero()))) {
        op = simplified()->CheckedUint32ToInt32(use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kWord32);
      }
    } else if (use_info.type_check() == TypeCheckKind::kNumber ||
               use_info.type_check() == TypeCheckKind::kNumberOrOddball) {
      return node;
    }
  } else if (output_rep == MachineRepresentation::kWord8 ||
             output_rep == MachineRepresentation::kWord16) {
    DCHECK_EQ(MachineRepresentation::kWord32, use_info.representation());
    DCHECK(use_info.type_check() == TypeCheckKind::kSignedSmall ||
           use_info.type_check() == TypeCheckKind::kSigned32);
    return node;
  } else if (output_rep == MachineRepresentation::kWord64) {
    if (output_type.Is(Type::Signed32()) ||
        (output_type.Is(Type::Unsigned32()) &&
         use_info.type_check() == TypeCheckKind::kNone) ||
        (output_type.Is(cache_->kSafeInteger) &&
         use_info.truncation().IsUsedAsWord32())) {
      op = machine()->TruncateInt64ToInt32();
    } else if (use_info.type_check() == TypeCheckKind::kSignedSmall ||
               use_info.type_check() == TypeCheckKind::kSigned32 ||
               use_info.type_check() == TypeCheckKind::kArrayIndex) {
      if (output_type.Is(cache_->kPositiveSafeInteger)) {
        op = simplified()->CheckedUint64ToInt32(use_info.feedback());
      } else if (output_type.Is(cache_->kSafeInteger)) {
        op = simplified()->CheckedInt64ToInt32(use_info.feedback());
      } else {
        return TypeError(node, output_rep, output_type,
                         MachineRepresentation::kWord32);
      }
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord32);
    }
  }

  if (op == nullptr) {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kWord32);
  }
  return InsertConversion(node, op, use_node);
}

Node* RepresentationChanger::InsertConversion(Node* node, const Operator* op,
                                              Node* use_node) {
  if (op->ControlInputCount() > 0) {
    // If the operator can deoptimize (which means it has control
    // input), we need to connect it to the effect and control chains.
    Node* effect = NodeProperties::GetEffectInput(use_node);
    Node* control = NodeProperties::GetControlInput(use_node);
    Node* conversion = jsgraph()->graph()->NewNode(op, node, effect, control);
    NodeProperties::ReplaceEffectInput(use_node, conversion);
    return conversion;
  }
  return jsgraph()->graph()->NewNode(op, node);
}

Node* RepresentationChanger::GetBitRepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher m(node);
      if (m.Is(factory()->false_value())) {
        return InsertTypeOverrideForVerifier(
            Type::Constant(broker_, broker_->false_value(), jsgraph()->zone()),
            jsgraph()->Int32Constant(0));
      } else if (m.Is(factory()->true_value())) {
        return InsertTypeOverrideForVerifier(
            Type::Constant(broker_, broker_->true_value(), jsgraph()->zone()),
            jsgraph()->Int32Constant(1));
      }
      break;
    }
    default:
      break;
  }
  // Select the correct X -> Bit operator.
  const Operator* op;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kBit), node);
  } else if (output_rep == MachineRepresentation::kTagged ||
             output_rep == MachineRepresentation::kTaggedPointer) {
    if (output_type.Is(Type::BooleanOrNullOrUndefined())) {
      // true is the only trueish Oddball.
      op = simplified()->ChangeTaggedToBit();
    } else {
      if (output_rep == MachineRepresentation::kTagged &&
          output_type.Maybe(Type::SignedSmall())) {
        op = simplified()->TruncateTaggedToBit();
      } else {
        // The {output_type} either doesn't include the Smi range,
        // or the {output_rep} is known to be TaggedPointer.
        op = simplified()->TruncateTaggedPointerToBit();
      }
    }
  } else if (output_rep == MachineRepresentation::kTaggedSigned) {
    if (COMPRESS_POINTERS_BOOL) {
      node = jsgraph()->graph()->NewNode(machine()->Word32Equal(), node,
                                         jsgraph()->Int32Constant(0));
    } else {
      node = jsgraph()->graph()->NewNode(machine()->WordEqual(), node,
                                         jsgraph()->IntPtrConstant(0));
    }
    return jsgraph()->graph()->NewNode(machine()->Word32Equal(), node,
                                       jsgraph()->Int32Constant(0));
  } else if (IsWord(output_rep)) {
    node = jsgraph()->graph()->NewNode(machine()->Word32Equal(), node,
                                       jsgraph()->Int32Constant(0));
    return jsgraph()->graph()->NewNode(machine()->Word32Equal(), node,
                                       jsgraph()->Int32Constant(0));
  } else if (output_rep == MachineRepresentation::kWord64) {
    node = jsgraph()->graph()->NewNode(machine()->Word64Equal(), node,
                                       jsgraph()->Int64Constant(0));
    return jsgraph()->graph()->NewNode(machine()->Word32Equal(), node,
                                       jsgraph()->Int32Constant(0));
  } else if (output_rep == MachineRepresentation::kFloat32) {
    node = jsgraph()->graph()->NewNode(machine()->Float32Abs(), node);
    return jsgraph()->graph()->NewNode(machine()->Float32LessThan(),
                                       jsgraph()->Float32Constant(0.0), node);
  } else if (output_rep == MachineRepresentation::kFloat64) {
    node = jsgraph()->graph()->NewNode(machine()->Float64Abs(), node);
    return jsgraph()->graph()->NewNode(machine()->Float64LessThan(),
                                       jsgraph()->Float64Constant(0.0), node);
  } else {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kBit);
  }
  return jsgraph()->graph()->NewNode(op, node);
}

Node* RepresentationChanger::GetWord64RepresentationFor(
    Node* node, MachineRepresentation output_rep, Type output_type,
    Node* use_node, UseInfo use_info) {
  // Eagerly fold representation changes for constants.
  switch (node->opcode()) {
    case IrOpcode::kInt32Constant:
    case IrOpcode::kInt64Constant:
    case IrOpcode::kFloat32Constant:
    case IrOpcode::kFloat64Constant:
      UNREACHABLE();
    case IrOpcode::kNumberConstant: {
      if (!TypeCheckIsBigInt(use_info.type_check())) {
        double const fv = OpParameter<double>(node->op());
        if (base::IsValueInRangeForNumericType<int64_t>(fv)) {
          int64_t const iv = static_cast<int64_t>(fv);
          if (static_cast<double>(iv) == fv) {
            return InsertTypeOverrideForVerifier(NodeProperties::GetType(node),
                                                 jsgraph()->Int64Constant(iv));
          }
        }
      }
      break;
    }
    case IrOpcode::kHeapConstant: {
      HeapObjectMatcher m(node);
      if (m.HasResolvedValue() && m.Ref(broker_).IsBigInt() &&
          (Is64() && use_info.truncation().IsUsedAsWord64())) {
        BigIntRef bigint = m.Ref(broker_).AsBigInt();
        return InsertTypeOverrideForVerifier(
            NodeProperties::GetType(node),
            jsgraph()->Int64Constant(static_cast<int64_t>(bigint.AsUint64())));
      }
      break;
    }
    default:
      break;
  }

  if (TypeCheckIsBigInt(use_info.type_check())) {
    // BigInts are only represented as tagged pointer and word64.
    if (!CanBeTaggedPointer(output_rep) &&
        output_rep != MachineRepresentation::kWord64) {
      DCHECK(!output_type.Equals(Type::BigInt()));
      Node* unreachable = InsertUnconditionalDeopt(
          use_node, DeoptimizeReason::kNotABigInt, use_info.feedback());
      return jsgraph()->graph()->NewNode(
          jsgraph()->common()->DeadValue(MachineRepresentation::kWord64),
          unreachable);
    }
  }

  // Select the correct X -> Word64 operator.
  const Operator* op;
  if (output_type.Is(Type::None())) {
    // This is an impossible value; it should not be used at runtime.
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kWord64), node);
  } else if (output_rep == MachineRepresentation::kBit) {
    CHECK(output_type.Is(Type::Boolean()));
    CHECK_NE(use_info.type_check(), TypeCheckKind::kNone);
    CHECK_NE(use_info.type_check(), TypeCheckKind::kNumberOrOddball);
    CHECK_NE(use_info.type_check(), TypeCheckKind::kBigInt);
    CHECK_NE(use_info.type_check(), TypeCheckKind::kBigInt64);
    Node* unreachable =
        InsertUnconditionalDeopt(use_node, DeoptimizeReason::kNotASmi);
    return jsgraph()->graph()->NewNode(
        jsgraph()->common()->DeadValue(MachineRepresentation::kWord64),
        unreachable);
  } else if (IsWord(output_rep)) {
    if (output_type.Is(Type::Unsigned32OrMinusZero())) {
      // uint32 -> uint64
      CHECK_IMPLIES(output_type.Maybe(Type::MinusZero()),
                    use_info.truncation().IdentifiesZeroAndMinusZero());
      op = machine()->ChangeUint32ToUint64();
    } else if (output_type.Is(Type::Signed32OrMinusZero())) {
      // int32 -> int64
      CHECK_IMPLIES(output_type.Maybe(Type::MinusZero()),
                    use_info.truncation().IdentifiesZeroAndMinusZero());
      op = machine()->ChangeInt32ToInt64();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else if (output_rep == MachineRepresentation::kFloat32) {
    if (output_type.Is(cache_->kDoubleRepresentableInt64) ||
        (output_type.Is(cache_->kDoubleRepresentableInt64OrMinusZero) &&
         use_info.truncation().IdentifiesZeroAndMinusZero())) {
      // float32 -> float64 -> int64
      node = InsertChangeFloat32ToFloat64(node);
      op = machine()->ChangeFloat64ToInt64();
    } else if (output_type.Is(cache_->kDoubleRepresentableUint64)) {
      // float32 -> float64 -> uint64
      node = InsertChangeFloat32ToFloat64(node);
      op = machine()->ChangeFloat64ToUint64();
    } else if (use_info.type_check() == TypeCheckKind::kSigned64 ||
               use_info.type_check() == TypeCheckKind::kArrayIndex) {
      // float32 -> float64 -> int64
      node = InsertChangeFloat32ToFloat64(node);
      op = simplified()->CheckedFloat64ToInt64(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else if (output_rep == MachineRepresentation::kFloat64) {
    if (output_type.Is(cache_->kDoubleRepresentableInt64) ||
        (output_type.Is(cache_->kDoubleRepresentableInt64OrMinusZero) &&
         use_info.truncation().IdentifiesZeroAndMinusZero())) {
      op = machine()->ChangeFloat64ToInt64();
    } else if (output_type.Is(cache_->kDoubleRepresentableUint64)) {
      op = machine()->ChangeFloat64ToUint64();
    } else if (use_info.type_check() == TypeCheckKind::kSigned64 ||
               use_info.type_check() == TypeCheckKind::kArrayIndex) {
      op = simplified()->CheckedFloat64ToInt64(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else if (output_rep == MachineRepresentation::kTaggedSigned) {
    if (output_type.Is(Type::SignedSmall())) {
      op = simplified()->ChangeTaggedSignedToInt64();
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else if (IsAnyTagged(output_rep) &&
             ((Is64() && use_info.truncation().IsUsedAsWord64() &&
               (use_info.type_check() == TypeCheckKind::kBigInt ||
                output_type.Is(Type::BigInt()))) ||
              use_info.type_check() == TypeCheckKind::kBigInt64)) {
    node = GetTaggedPointerRepresentationFor(node, output_rep, output_type,
                                             use_node, use_info);
    op = simplified()->TruncateBigIntToWord64();
  } else if (CanBeTaggedPointer(output_rep)) {
    if (output_type.Is(cache_->kDoubleRepresentableInt64) ||
        (output_type.Is(cache_->kDoubleRepresentableInt64OrMinusZero) &&
         use_info.truncation().IdentifiesZeroAndMinusZero())) {
      op = simplified()->ChangeTaggedToInt64();
    } else if (use_info.type_check() == TypeCheckKind::kSigned64) {
      op = simplified()->CheckedTaggedToInt64(
          output_type.Maybe(Type::MinusZero())
              ? use_info.minus_zero_check()
              : CheckForMinusZeroMode::kDontCheckForMinusZero,
          use_info.feedback());
    } else if (use_info.type_check() == TypeCheckKind::kArrayIndex) {
      op = simplified()->CheckedTaggedToArrayIndex(use_info.feedback());
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else if (output_rep == MachineRepresentation::kWord64) {
    DCHECK(TypeCheckIsBigInt(use_info.type_check()));
    if (output_type.Is(Type::UnsignedBigInt64()) &&
        use_info.type_check() == TypeCheckKind::kBigInt64) {
      op = simplified()->CheckedUint64ToInt64(use_info.feedback());
    } else if ((output_type.Is(Type::BigInt()) &&
                use_info.type_check() == TypeCheckKind::kBigInt) ||
               (output_type.Is(Type::SignedBigInt64()) &&
                use_info.type_check() == TypeCheckKind::kBigInt64)) {
      return node;
    } else {
      DCHECK(output_type != Type::BigInt() ||
             use_info.type_check() != TypeCheckKind::kBigInt64);
      Node* unreachable = InsertUnconditionalDeopt(
          use_node, DeoptimizeReason::kNotABigInt, use_info.feedback());
      return jsgraph()->graph()->NewNode(
          jsgraph()->common()->DeadValue(MachineRepresentation::kWord64),
          unreachable);
    }
  } else if (output_rep == MachineRepresentation::kSandboxedPointer) {
    if (output_type.Is(Type::SandboxedPointer())) {
      return node;
    } else {
      return TypeError(node, output_rep, output_type,
                       MachineRepresentation::kWord64);
    }
  } else {
    return TypeError(node, output_rep, output_type,
                     MachineRepresentation::kWord64);
  }
  return InsertConversion(node, op, use_node);
}

const Operator* RepresentationChanger::Int32OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeNumberAdd:  // Fall through.
    case IrOpcode::kSpeculativeSafeIntegerAdd:
    case IrOpcode::kNumberAdd:
      return machine()->Int32Add();
    case IrOpcode::kSpeculativeNumberSubtract:  // Fall through.
    case IrOpcode::kSpeculativeSafeIntegerSubtract:
    case IrOpcode::kNumberSubtract:
      return machine()->Int32Sub();
    case IrOpcode::kSpeculativeNumberMultiply:
    case IrOpcode::kNumberMultiply:
      return machine()->Int32Mul();
    case IrOpcode::kSpeculativeNumberDivide:
    case IrOpcode::kNumberDivide:
      return machine()->Int32Div();
    case IrOpcode::kSpeculativeNumberModulus:
    case IrOpcode::kNumberModulus:
      return machine()->Int32Mod();
    case IrOpcode::kSpeculativeNumberBitwiseOr:  // Fall through.
    case IrOpcode::kNumberBitwiseOr:
      return machine()->Word32Or();
    case IrOpcode::kSpeculativeNumberBitwiseXor:  // Fall through.
    case IrOpcode::kNumberBitwiseXor:
      return machine()->Word32Xor();
    case IrOpcode::kSpeculativeNumberBitwiseAnd:  // Fall through.
    case IrOpcode::kNumberBitwiseAnd:
      return machine()->Word32And();
    case IrOpcode::kNumberEqual:
    case IrOpcode::kSpeculativeNumberEqual:
      return machine()->Word32Equal();
    case IrOpcode::kNumberLessThan:
    case IrOpcode::kSpeculativeNumberLessThan:
      return machine()->Int32LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
    case IrOpcode::kSpeculativeNumberLessThanOrEqual:
      return machine()->Int32LessThanOrEqual();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Int32OverflowOperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeSafeIntegerAdd:
      return simplified()->CheckedInt32Add();
    case IrOpcode::kSpeculativeSafeIntegerSubtract:
      return simplified()->CheckedInt32Sub();
    case IrOpcode::kSpeculativeNumberDivide:
      return simplified()->CheckedInt32Div();
    case IrOpcode::kSpeculativeNumberModulus:
      return simplified()->CheckedInt32Mod();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Int64OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeNumberAdd:  // Fall through.
    case IrOpcode::kSpeculativeSafeIntegerAdd:
    case IrOpcode::kNumberAdd:
    case IrOpcode::kSpeculativeBigIntAdd:
      return machine()->Int64Add();
    case IrOpcode::kSpeculativeNumberSubtract:  // Fall through.
    case IrOpcode::kSpeculativeSafeIntegerSubtract:
    case IrOpcode::kNumberSubtract:
    case IrOpcode::kSpeculativeBigIntSubtract:
      return machine()->Int64Sub();
    case IrOpcode::kSpeculativeBigIntMultiply:
      return machine()->Int64Mul();
    case IrOpcode::kSpeculativeBigIntBitwiseAnd:
      return machine()->Word64And();
    case IrOpcode::kSpeculativeBigIntBitwiseOr:
      return machine()->Word64Or();
    case IrOpcode::kSpeculativeBigIntBitwiseXor:
      return machine()->Word64Xor();
    case IrOpcode::kSpeculativeBigIntEqual:
      return machine()->Word64Equal();
    case IrOpcode::kSpeculativeBigIntLessThan:
      return machine()->Int64LessThan();
    case IrOpcode::kSpeculativeBigIntLessThanOrEqual:
      return machine()->Int64LessThanOrEqual();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Int64OverflowOperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeBigIntAdd:
      return simplified()->CheckedInt64Add();
    case IrOpcode::kSpeculativeBigIntSubtract:
      return simplified()->CheckedInt64Sub();
    case IrOpcode::kSpeculativeBigIntMultiply:
      return simplified()->CheckedInt64Mul();
    case IrOpcode::kSpeculativeBigIntDivide:
      return simplified()->CheckedInt64Div();
    case IrOpcode::kSpeculativeBigIntModulus:
      return simplified()->CheckedInt64Mod();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::BigIntOperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeBigIntAdd:
      return simplified()->BigIntAdd();
    case IrOpcode::kSpeculativeBigIntSubtract:
      return simplified()->BigIntSubtract();
    case IrOpcode::kSpeculativeBigIntMultiply:
      return simplified()->BigIntMultiply();
    case IrOpcode::kSpeculativeBigIntDivide:
      return simplified()->BigIntDivide();
    case IrOpcode::kSpeculativeBigIntModulus:
      return simplified()->BigIntModulus();
    case IrOpcode::kSpeculativeBigIntBitwiseAnd:
      return simplified()->BigIntBitwiseAnd();
    case IrOpcode::kSpeculativeBigIntBitwiseOr:
      return simplified()->BigIntBitwiseOr();
    case IrOpcode::kSpeculativeBigIntBitwiseXor:
      return simplified()->BigIntBitwiseXor();
    case IrOpcode::kSpeculativeBigIntShiftLeft:
      return simplified()->BigIntShiftLeft();
    case IrOpcode::kSpeculativeBigIntShiftRight:
      return simplified()->BigIntShiftRight();
    case IrOpcode::kSpeculativeBigIntEqual:
      return simplified()->BigIntEqual();
    case IrOpcode::kSpeculativeBigIntLessThan:
      return simplified()->BigIntLessThan();
    case IrOpcode::kSpeculativeBigIntLessThanOrEqual:
      return simplified()->BigIntLessThanOrEqual();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::TaggedSignedOperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeNumberLessThan:
      return (COMPRESS_POINTERS_BOOL || machine()->Is32())
                 ? machine()->Int32LessThan()
                 : machine()->Int64LessThan();
    case IrOpcode::kSpeculativeNumberLessThanOrEqual:
      return (COMPRESS_POINTERS_BOOL || machine()->Is32())
                 ? machine()->Int32LessThanOrEqual()
                 : machine()->Int64LessThanOrEqual();
    case IrOpcode::kSpeculativeNumberEqual:
      return (COMPRESS_POINTERS_BOOL || machine()->Is32())
                 ? machine()->Word32Equal()
                 : machine()->Word64Equal();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Uint32OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kNumberAdd:
      return machine()->Int32Add();
    case IrOpcode::kNumberSubtract:
      return machine()->Int32Sub();
    case IrOpcode::kSpeculativeNumberMultiply:
    case IrOpcode::kNumberMultiply:
      return machine()->Int32Mul();
    case IrOpcode::kSpeculativeNumberDivide:
    case IrOpcode::kNumberDivide:
      return machine()->Uint32Div();
    case IrOpcode::kSpeculativeNumberModulus:
    case IrOpcode::kNumberModulus:
      return machine()->Uint32Mod();
    case IrOpcode::kNumberEqual:
    case IrOpcode::kSpeculativeNumberEqual:
      return machine()->Word32Equal();
    case IrOpcode::kNumberLessThan:
    case IrOpcode::kSpeculativeNumberLessThan:
      return machine()->Uint32LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
    case IrOpcode::kSpeculativeNumberLessThanOrEqual:
      return machine()->Uint32LessThanOrEqual();
    case IrOpcode::kNumberClz32:
      return machine()->Word32Clz();
    case IrOpcode::kNumberImul:
      return machine()->Int32Mul();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Uint32OverflowOperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeNumberDivide:
      return simplified()->CheckedUint32Div();
    case IrOpcode::kSpeculativeNumberModulus:
      return simplified()->CheckedUint32Mod();
    default:
      UNREACHABLE();
  }
}

const Operator* RepresentationChanger::Float64OperatorFor(
    IrOpcode::Value opcode) {
  switch (opcode) {
    case IrOpcode::kSpeculativeNumberAdd:
    case IrOpcode::kSpeculativeSafeIntegerAdd:
    case IrOpcode::kNumberAdd:
      return machine()->Float64Add();
    case IrOpcode::kSpeculativeNumberSubtract:
    case IrOpcode::kSpeculativeSafeIntegerSubtract:
    case IrOpcode::kNumberSubtract:
      return machine()->Float64Sub();
    case IrOpcode::kSpeculativeNumberMultiply:
    case IrOpcode::kNumberMultiply:
      return machine()->Float64Mul();
    case IrOpcode::kSpeculativeNumberDivide:
    case IrOpcode::kNumberDivide:
      return machine()->Float64Div();
    case IrOpcode::kSpeculativeNumberModulus:
    case IrOpcode::kNumberModulus:
      return machine()->Float64Mod();
    case IrOpcode::kNumberEqual:
    case IrOpcode::kSpeculativeNumberEqual:
      return machine()->Float64Equal();
    case IrOpcode::kNumberLessThan:
    case IrOpcode::kSpeculativeNumberLessThan:
      return machine()->Float64LessThan();
    case IrOpcode::kNumberLessThanOrEqual:
    case IrOpcode::kSpeculativeNumberLessThanOrEqual:
      return machine()->Float64LessThanOrEqual();
    case IrOpcode::kNumberAbs:
      return machine()->Float64Abs();
    case IrOpcode::kNumberAcos:
      return machine()->Float64Acos();
    case IrOpcode::kNumberAcosh:
      return machine()->Float64Acosh();
    case IrOpcode::kNumberAsin:
      return machine()->Float64Asin();
    case IrOpcode::kNumberAsinh:
      return machine()->Float64Asinh();
    case IrOpcode::kNumberAtan:
      return machine()->Float64Atan();
    case IrOpcode::kNumberAtanh:
      return machine()->Float64Atanh();
    case IrOpcode::kNumberAtan2:
      return machine()->Float64Atan2();
    case IrOpcode::kNumberCbrt:
      return machine()->Float64Cbrt();
    case IrOpcode::kNumberCeil:
      return machine()->Float64RoundUp().placeholder();
    case IrOpcode::kNumberCos:
      return machine()->Float64Cos();
    case IrOpcode::kNumberCosh:
      return machine()->Float64Cosh();
    case IrOpcode::kNumberExp:
      return machine()->Float64Exp();
    case IrOpcode::kNumberExpm1:
      return machine()->Float64Expm1();
    case IrOpcode::kNumberFloor:
      return machine()->Float64RoundDown().placeholder();
    case IrOpcode::kNumberFround:
      return machine()->TruncateFloat64ToFloat32();
    case IrOpcode::kNumberLog:
      return machine()->Float64Log();
    case IrOpcode::kNumberLog1p:
      return machine()->Float64Log1p();
    case IrOpcode::kNumberLog2:
      return machine()->Float64Log2();
    case IrOpcode::kNumberLog10:
      return machine()->Float64Log10();
    case IrOpcode::kNumberMax:
      return machine()->Float64Max();
    case IrOpcode::kNumberMin:
      return machine()->Float64Min();
    case IrOpcode::kSpeculativeNumberPow:
    case IrOpcode::kNumberPow:
      return machine()->Float64Pow();
    case IrOpcode::kNumberSin:
      return machine()->Float64Sin();
    case IrOpcode::kNumberSinh:
      return machine()->Float64Sinh();
    case IrOpcode::kNumberSqrt:
      return machine()->Float64Sqrt();
    case IrOpcode::kNumberTan:
      return machine()->Float64Tan();
    case IrOpcode::kNumberTanh:
      return machine()->Float64Tanh();
    case IrOpcode::kNumberTrunc:
      return machine()->Float64RoundTruncate().placeholder();
    case IrOpcode::kNumberSilenceNaN:
      return machine()->Float64SilenceNaN();
    default:
      UNREACHABLE();
  }
}

Node* RepresentationChanger::TypeError(Node* node,
                                       MachineRepresentation output_rep,
                                       Type output_type,
                                       MachineRepresentation use) {
  type_error_ = true;
  if (!testing_type_errors_) {
    std::ostringstream out_str;
    out_str << output_rep << " (";
    output_type.PrintTo(out_str);
    out_str << ")";

    std::ostringstream use_str;
    use_str << use;

    FATAL(
        "RepresentationChangerError: node #%d:%s of "
        "%s cannot be changed to %s",
        node->id(), node->op()->mnemonic(), out_str.str().c_str(),
        use_str.str().c_str());
  }
  return node;
}

Node* RepresentationChanger::InsertChangeBitToTagged(Node* node) {
  return jsgraph()->graph()->NewNode(simplified()->ChangeBitToTagged(), node);
}

Node* RepresentationChanger::InsertChangeFloat32ToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeFloat32ToFloat64(), node);
}

Node* RepresentationChanger::InsertChangeFloat64ToUint32(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeFloat64ToUint32(), node);
}

Node* RepresentationChanger::InsertChangeFloat64ToInt32(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeFloat64ToInt32(), node);
}

Node* RepresentationChanger::InsertChangeInt32ToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeInt32ToFloat64(), node);
}

Node* RepresentationChanger::InsertChangeTaggedSignedToInt32(Node* node) {
  return jsgraph()->graph()->NewNode(simplified()->ChangeTaggedSignedToInt32(),
                                     node);
}

Node* RepresentationChanger::InsertChangeTaggedToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(simplified()->ChangeTaggedToFloat64(),
                                     node);
}

Node* RepresentationChanger::InsertChangeUint32ToFloat64(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->ChangeUint32ToFloat64(), node);
}

Node* RepresentationChanger::InsertTruncateInt64ToInt32(Node* node) {
  return jsgraph()->graph()->NewNode(machine()->TruncateInt64ToInt32(), node);
}

Node* RepresentationChanger::InsertCheckedFloat64ToInt32(
    Node* node, CheckForMinusZeroMode check, const FeedbackSource& feedback,
    Node* use_node) {
  return InsertConversion(
      node, simplified()->CheckedFloat64ToInt32(check, feedback), use_node);
}

Node* RepresentationChanger::InsertTypeOverrideForVerifier(const Type& type,
                                                           Node* node) {
  if (verification_enabled()) {
    DCHECK(!type.IsInvalid());
    node = jsgraph()->graph()->NewNode(
        jsgraph()->common()->SLVerifierHint(nullptr, type), node);
    verifier_->RecordHint(node);
  }
  return node;
}

Isolate* RepresentationChanger::isolate() const { return broker_->isolate(); }

}  // namespace compiler
}  // namespace internal
}  // namespace v8
