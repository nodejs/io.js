// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_OPMASKS_H_
#define V8_COMPILER_TURBOSHAFT_OPMASKS_H_

#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/representations.h"

// The Opmasks allow performing a type check or cast with an operation mask
// that doesn't only encode the opcode but also additional properties, i.e.
// fields of an operation.
// The type check will be expressed by masking out the first 8 bytes of the
// object based on a generic Opmask and then comparing it against a specific
// shape of that mask.
//
// Given the following operation and mask definitions:
//
//   struct ConvertOp : FixedArityOperationT<1, ConvertOp> {
//     enum Type : int8_t {kBool, kInt, kFloat};
//     Type from;
//     Type to;
//   };
//
//   using ConvertOpMask =
//     MaskBuilder<ConvertOp, FIELD(ConvertOp, from), FIELD(ConvertOp, to)>;
//   using ConvertOpTargetMask = MaskBuilder<ConvertOp, FIELD(ConvertOp, to)>;
//
//   using ConvertFloatToInt =
//     ConvertOpMask::For<ConvertOp::kFloat, ConvertOp::kInt>;
//   using ConvertToInt =
//     ConvertOpTargetMask::For<ConvertOp::kInt>;
//
// The masks can be used in the following way:
//
//    const Operation& my_op = ...;
//    bool is_float_to_int = my_op.Is<ConvertFloatToInt>();
//    const ConvertOp* to_int = my_op.TryCast<ConvertToInt>();
//
// Where to_int will be non-null iff my_op is a ConvertOp *and* the target type
// is int.

namespace v8::internal::compiler::turboshaft::Opmask {

template <typename T, size_t Offset>
struct OpMaskField {
  using type = T;
  static constexpr size_t offset = Offset;
  static constexpr size_t size = sizeof(T);

  static_assert(offset + size <= sizeof(uint64_t));
};

template <typename T>
constexpr uint64_t encode_for_mask(T value) {
  return static_cast<uint64_t>(value);
}

template <typename T>
struct UnwrapRepresentation {
  using type = T;
};
template <>
struct UnwrapRepresentation<WordRepresentation> {
  using type = WordRepresentation::Enum;
};
template <>
struct UnwrapRepresentation<FloatRepresentation> {
  using type = FloatRepresentation::Enum;
};
template <>
struct UnwrapRepresentation<RegisterRepresentation> {
  using type = RegisterRepresentation::Enum;
};

template <typename Op, typename... Fields>
struct MaskBuilder {
  static constexpr uint64_t BuildBaseMask() {
    static_assert(OFFSET_OF(Operation, opcode) == 0);
    static_assert(sizeof(Operation::opcode) == sizeof(uint8_t));
    static_assert(sizeof(Operation) == 4);
#if V8_TARGET_BIG_ENDIAN
    return static_cast<uint64_t>(0xFF)
           << ((sizeof(uint64_t) - sizeof(uint8_t)) * kBitsPerByte);
#else
    return static_cast<uint64_t>(0xFF);
#endif
  }

  static constexpr uint64_t EncodeBaseValue(Opcode opcode) {
    static_assert(OFFSET_OF(Operation, opcode) == 0);
#if V8_TARGET_BIG_ENDIAN
    return static_cast<uint64_t>(opcode)
           << ((sizeof(uint64_t) - sizeof(Operation::opcode)) * kBitsPerByte);
#else
    return static_cast<uint64_t>(opcode);
#endif
  }

  static constexpr uint64_t BuildMask() {
    constexpr uint64_t base_mask = BuildBaseMask();
    return (base_mask | ... | BuildFieldMask<Fields>());
  }

  static constexpr uint64_t EncodeValue(typename Fields::type... args) {
    constexpr uint64_t base_mask = EncodeBaseValue(operation_to_opcode_v<Op>);
    return (base_mask | ... | EncodeFieldValue<Fields>(args));
  }

  template <typename F>
  static constexpr uint64_t BuildFieldMask() {
    static_assert(F::size < sizeof(uint64_t));
    static_assert(F::offset + F::size <= sizeof(uint64_t));
    constexpr uint64_t ones = static_cast<uint64_t>(-1) >>
                              ((sizeof(uint64_t) - F::size) * kBitsPerByte);
#if V8_TARGET_BIG_ENDIAN
    return ones << ((sizeof(uint64_t) - F::size - F::offset) * kBitsPerByte);
#else
    return ones << (F::offset * kBitsPerByte);
#endif
  }

  template <typename F>
  static constexpr uint64_t EncodeFieldValue(typename F::type value) {
#if V8_TARGET_BIG_ENDIAN
    return encode_for_mask(value)
           << ((sizeof(uint64_t) - F::size - F::offset) * kBitsPerByte);
#else
    return encode_for_mask(value) << (F::offset * kBitsPerByte);
#endif
  }

  template <typename Fields::type... Args>
  using For = OpMaskT<Op, BuildMask(), EncodeValue(Args...)>;
};

#define FIELD(op, field_name)                                       \
  OpMaskField<UnwrapRepresentation<decltype(op::field_name)>::type, \
              OFFSET_OF(op, field_name)>

// === Definitions of masks for Turboshaft operations === //

using WordBinopMask =
    MaskBuilder<WordBinopOp, FIELD(WordBinopOp, kind), FIELD(WordBinopOp, rep)>;
using WordBinopKindMask = MaskBuilder<WordBinopOp, FIELD(WordBinopOp, kind)>;

using kWord32Add =
    WordBinopMask::For<WordBinopOp::Kind::kAdd, WordRepresentation::Word32()>;
using kWord32Sub =
    WordBinopMask::For<WordBinopOp::Kind::kSub, WordRepresentation::Word32()>;
using kWord32Mul =
    WordBinopMask::For<WordBinopOp::Kind::kMul, WordRepresentation::Word32()>;
using kWord32SignedMulOverflownBits =
    WordBinopMask::For<WordBinopOp::Kind::kSignedMulOverflownBits,
                       WordRepresentation::Word32()>;
using kWord32UnsignedMulOverflownBits =
    WordBinopMask::For<WordBinopOp::Kind::kUnsignedMulOverflownBits,
                       WordRepresentation::Word32()>;

using kWord32BitwiseAnd = WordBinopMask::For<WordBinopOp::Kind::kBitwiseAnd,
                                             WordRepresentation::Word32()>;
using kWord32BitwiseOr = WordBinopMask::For<WordBinopOp::Kind::kBitwiseOr,
                                            WordRepresentation::Word32()>;
using kWord32BitwiseXor = WordBinopMask::For<WordBinopOp::Kind::kBitwiseXor,
                                             WordRepresentation::Word32()>;
using kWord64Add =
    WordBinopMask::For<WordBinopOp::Kind::kAdd, WordRepresentation::Word64()>;
using kWord64Sub =
    WordBinopMask::For<WordBinopOp::Kind::kSub, WordRepresentation::Word64()>;
using kWord64Mul =
    WordBinopMask::For<WordBinopOp::Kind::kMul, WordRepresentation::Word64()>;
using kWord64BitwiseAnd = WordBinopMask::For<WordBinopOp::Kind::kBitwiseAnd,
                                             WordRepresentation::Word64()>;
using kWord64BitwiseOr = WordBinopMask::For<WordBinopOp::Kind::kBitwiseOr,
                                            WordRepresentation::Word64()>;
using kWord64BitwiseXor = WordBinopMask::For<WordBinopOp::Kind::kBitwiseXor,
                                             WordRepresentation::Word64()>;

using kBitwiseAnd = WordBinopKindMask::For<WordBinopOp::Kind::kBitwiseAnd>;
using kBitwiseXor = WordBinopKindMask::For<WordBinopOp::Kind::kBitwiseXor>;

using WordUnaryMask =
    MaskBuilder<WordUnaryOp, FIELD(WordUnaryOp, kind), FIELD(WordUnaryOp, rep)>;
using kWord32ReverseBytes = WordUnaryMask::For<WordUnaryOp::Kind::kReverseBytes,
                                               WordRepresentation::Word32()>;
using kWord64ReverseBytes = WordUnaryMask::For<WordUnaryOp::Kind::kReverseBytes,
                                               WordRepresentation::Word64()>;

using FloatUnaryMask = MaskBuilder<FloatUnaryOp, FIELD(FloatUnaryOp, kind),
                                   FIELD(FloatUnaryOp, rep)>;

using kFloat32Negate = FloatUnaryMask::For<FloatUnaryOp::Kind::kNegate,
                                           FloatRepresentation::Float32()>;
using kFloat64Abs = FloatUnaryMask::For<FloatUnaryOp::Kind::kAbs,
                                        FloatRepresentation::Float64()>;
using kFloat64Negate = FloatUnaryMask::For<FloatUnaryOp::Kind::kNegate,
                                           FloatRepresentation::Float64()>;

using FloatBinopMask = MaskBuilder<FloatBinopOp, FIELD(FloatBinopOp, kind),
                                   FIELD(FloatBinopOp, rep)>;

using kFloat32Sub = FloatBinopMask::For<FloatBinopOp::Kind::kSub,
                                        FloatRepresentation::Float32()>;
using kFloat32Mul = FloatBinopMask::For<FloatBinopOp::Kind::kMul,
                                        FloatRepresentation::Float32()>;
using kFloat64Sub = FloatBinopMask::For<FloatBinopOp::Kind::kSub,
                                        FloatRepresentation::Float64()>;
using kFloat64Mul = FloatBinopMask::For<FloatBinopOp::Kind::kMul,
                                        FloatRepresentation::Float64()>;

using ShiftMask =
    MaskBuilder<ShiftOp, FIELD(ShiftOp, kind), FIELD(ShiftOp, rep)>;
using ShiftKindMask = MaskBuilder<ShiftOp, FIELD(ShiftOp, kind)>;

using kWord32ShiftLeft =
    ShiftMask::For<ShiftOp::Kind::kShiftLeft, WordRepresentation::Word32()>;
using kWord32ShiftRightArithmetic =
    ShiftMask::For<ShiftOp::Kind::kShiftRightArithmetic,
                   WordRepresentation::Word32()>;
using kWord32ShiftRightArithmeticShiftOutZeros =
    ShiftMask::For<ShiftOp::Kind::kShiftRightArithmeticShiftOutZeros,
                   WordRepresentation::Word32()>;
using kWord32ShiftRightLogical =
    ShiftMask::For<ShiftOp::Kind::kShiftRightLogical,
                   WordRepresentation::Word32()>;
using kWord32RotateRight =
    ShiftMask::For<ShiftOp::Kind::kRotateRight, WordRepresentation::Word32()>;
using kWord64ShiftLeft =
    ShiftMask::For<ShiftOp::Kind::kShiftLeft, WordRepresentation::Word64()>;
using kWord64ShiftRightArithmetic =
    ShiftMask::For<ShiftOp::Kind::kShiftRightArithmetic,
                   WordRepresentation::Word64()>;
using kWord64ShiftRightLogical =
    ShiftMask::For<ShiftOp::Kind::kShiftRightLogical,
                   WordRepresentation::Word64()>;
using kShiftLeft = ShiftKindMask::For<ShiftOp::Kind::kShiftLeft>;

using PhiMask = MaskBuilder<PhiOp, FIELD(PhiOp, rep)>;
using kTaggedPhi = PhiMask::For<RegisterRepresentation::Tagged()>;

using ConstantMask = MaskBuilder<ConstantOp, FIELD(ConstantOp, kind)>;

using kWord32Constant = ConstantMask::For<ConstantOp::Kind::kWord32>;
using kWord64Constant = ConstantMask::For<ConstantOp::Kind::kWord64>;
using kExternalConstant = ConstantMask::For<ConstantOp::Kind::kExternal>;

using ProjectionMask = MaskBuilder<ProjectionOp, FIELD(ProjectionOp, index)>;

using kProjection0 = ProjectionMask::For<0>;
using kProjection1 = ProjectionMask::For<1>;

using ComparisonMask = MaskBuilder<ComparisonOp, FIELD(ComparisonOp, kind),
                                   FIELD(ComparisonOp, rep)>;

using kWord32Equal = ComparisonMask::For<ComparisonOp::Kind::kEqual,
                                         WordRepresentation::Word32()>;
using kWord64Equal = ComparisonMask::For<ComparisonOp::Kind::kEqual,
                                         WordRepresentation::Word64()>;
using ComparisonKindMask = MaskBuilder<ComparisonOp, FIELD(ComparisonOp, kind)>;
using kComparisonEqual = ComparisonKindMask::For<ComparisonOp::Kind::kEqual>;

using ChangeOpMask =
    MaskBuilder<ChangeOp, FIELD(ChangeOp, kind), FIELD(ChangeOp, assumption),
                FIELD(ChangeOp, from), FIELD(ChangeOp, to)>;

using kChangeInt32ToInt64 = ChangeOpMask::For<
    ChangeOp::Kind::kSignExtend, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Word32(), RegisterRepresentation::Word64()>;
using kChangeUint32ToUint64 = ChangeOpMask::For<
    ChangeOp::Kind::kZeroExtend, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Word32(), RegisterRepresentation::Word64()>;
using kFloat64ExtractHighWord32 = ChangeOpMask::For<
    ChangeOp::Kind::kExtractHighHalf, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Float64(), RegisterRepresentation::Word32()>;
using kTruncateFloat64ToInt64OverflowToMin =
    ChangeOpMask::For<ChangeOp::Kind::kSignedFloatTruncateOverflowToMin,
                      ChangeOp::Assumption::kNoAssumption,
                      RegisterRepresentation::Float64(),
                      RegisterRepresentation::Word64()>;
using kTruncateFloat32ToInt32OverflowToMin =
    ChangeOpMask::For<ChangeOp::Kind::kSignedFloatTruncateOverflowToMin,
                      ChangeOp::Assumption::kNoAssumption,
                      RegisterRepresentation::Float32(),
                      RegisterRepresentation::Word32()>;
using kTruncateFloat32ToUint32OverflowToMin =
    ChangeOpMask::For<ChangeOp::Kind::kUnsignedFloatTruncateOverflowToMin,
                      ChangeOp::Assumption::kNoAssumption,
                      RegisterRepresentation::Float32(),
                      RegisterRepresentation::Word32()>;

using kTruncateWord64ToWord32 = ChangeOpMask::For<
    ChangeOp::Kind::kTruncate, ChangeOp::Assumption::kNoAssumption,
    RegisterRepresentation::Word64(), RegisterRepresentation::Word32()>;

using OverflowCheckedBinopMask =
    MaskBuilder<OverflowCheckedBinopOp, FIELD(OverflowCheckedBinopOp, kind),
                FIELD(OverflowCheckedBinopOp, rep)>;
using kOverflowCheckedWord32Add =
    OverflowCheckedBinopMask::For<OverflowCheckedBinopOp::Kind::kSignedAdd,
                                  WordRepresentation::Word32()>;

using TaggedBitcastMask =
    MaskBuilder<TaggedBitcastOp, FIELD(TaggedBitcastOp, from),
                FIELD(TaggedBitcastOp, to), FIELD(TaggedBitcastOp, kind)>;
using kBitcastTaggedToWordPtrForTagAndSmiBits =
    TaggedBitcastMask::For<RegisterRepresentation::Tagged(),
                           RegisterRepresentation::WordPtr(),
                           TaggedBitcastOp::Kind::kTagAndSmiBits>;
using kBitcastWordPtrToSmi =
    TaggedBitcastMask::For<RegisterRepresentation::WordPtr(),
                           RegisterRepresentation::Tagged(),
                           TaggedBitcastOp::Kind::kSmi>;

using TaggedBitcastKindMask =
    MaskBuilder<TaggedBitcastOp, FIELD(TaggedBitcastOp, kind)>;
using kTaggedBitcastSmi =
    TaggedBitcastKindMask::For<TaggedBitcastOp::Kind::kSmi>;
using kTaggedBitcastHeapObject =
    TaggedBitcastKindMask::For<TaggedBitcastOp::Kind::kHeapObject>;

#if V8_ENABLE_WEBASSEMBLY

using Simd128BinopMask =
    MaskBuilder<Simd128BinopOp, FIELD(Simd128BinopOp, kind)>;
using kSimd128I32x4Mul = Simd128BinopMask::For<Simd128BinopOp::Kind::kI32x4Mul>;
using kSimd128I16x8Mul = Simd128BinopMask::For<Simd128BinopOp::Kind::kI16x8Mul>;

#define SIMD_SIGN_EXTENSION_BINOP_MASK(kind) \
  using kSimd128##kind = Simd128BinopMask::For<Simd128BinopOp::Kind::k##kind>;
FOREACH_SIMD_128_BINARY_SIGN_EXTENSION_OPCODE(SIMD_SIGN_EXTENSION_BINOP_MASK)
#undef SIMD_SIGN_EXTENSION_BINOP_MASK

using Simd128UnaryMask =
    MaskBuilder<Simd128UnaryOp, FIELD(Simd128UnaryOp, kind)>;
#define SIMD_UNARY_MASK(kind) \
  using kSimd128##kind = Simd128UnaryMask::For<Simd128UnaryOp::Kind::k##kind>;
FOREACH_SIMD_128_UNARY_OPCODE(SIMD_UNARY_MASK)
#undef SIMD_UNARY_MASK

using Simd128ShiftMask =
    MaskBuilder<Simd128ShiftOp, FIELD(Simd128ShiftOp, kind)>;
#define SIMD_SHIFT_MASK(kind) \
  using kSimd128##kind = Simd128ShiftMask::For<Simd128ShiftOp::Kind::k##kind>;
FOREACH_SIMD_128_SHIFT_OPCODE(SIMD_SHIFT_MASK)
#undef SIMD_SHIFT_MASK

using Simd128LoadTransformMask =
    MaskBuilder<Simd128LoadTransformOp,
                FIELD(Simd128LoadTransformOp, transform_kind)>;
#define SIMD_LOAD_TRANSFORM_MASK(kind)                               \
  using kSimd128LoadTransform##kind = Simd128LoadTransformMask::For< \
      Simd128LoadTransformOp::TransformKind::k##kind>;
FOREACH_SIMD_128_LOAD_TRANSFORM_OPCODE(SIMD_LOAD_TRANSFORM_MASK)
#undef SIMD_LOAD_TRANSFORM_MASK

#endif  // V8_ENABLE_WEBASSEMBLY

#ifndef TURBOSHAFT_OPMASK_EXPORT_FIELD_MACRO_FOR_UNITTESTS
#undef FIELD
#endif

}  // namespace v8::internal::compiler::turboshaft::Opmask

#endif  // V8_COMPILER_TURBOSHAFT_OPMASKS_H_
