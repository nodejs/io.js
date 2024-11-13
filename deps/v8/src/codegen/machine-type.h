// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MACHINE_TYPE_H_
#define V8_CODEGEN_MACHINE_TYPE_H_

#include <iosfwd>
#include <limits>

#include "include/v8-fast-api-calls.h"
#include "src/base/bits.h"
#include "src/common/globals.h"
#include "src/flags/flags.h"

namespace v8 {
namespace internal {

enum class MachineRepresentation : uint8_t {
  kNone,
  kBit,
  // Integral representations must be consecutive, in order of increasing order.
  kWord8,
  kWord16,
  kWord32,
  kWord64,
  // (uncompressed) MapWord
  // kMapWord is the representation of a map word, i.e. a map in the header
  // of a HeapObject.
  // If V8_MAP_PACKING is disabled, a map word is just the map itself. Hence
  //     kMapWord is equivalent to kTaggedPointer -- in fact it will be
  //     translated to kTaggedPointer during memory lowering.
  // If V8_MAP_PACKING is enabled, a map word is a Smi-like encoding of a map
  //     and some meta data. Memory lowering of kMapWord loads/stores
  //     produces low-level kTagged loads/stores plus the necessary
  //     decode/encode operations.
  // In either case, the kMapWord representation is not used after memory
  // lowering.
  kMapWord,
  kTaggedSigned,       // (uncompressed) Smi
  kTaggedPointer,      // (uncompressed) HeapObject
  kTagged,             // (uncompressed) Object (Smi or HeapObject)
  kCompressedPointer,  // (compressed) HeapObject
  kCompressed,         // (compressed) Object (Smi or HeapObject)
  kProtectedPointer,   // (uncompressed) TrustedObject
  kIndirectPointer,    // (indirect) HeapObject
  // A 64-bit pointer encoded in a way (e.g. as offset) that guarantees it will
  // point into the sandbox.
  kSandboxedPointer,
  // FP and SIMD representations must be last, and in order of increasing size.
  kFloat16,
  kFloat32,
  kFloat64,
  kSimd128,
  kSimd256,
  kFirstFPRepresentation = kFloat16,
  kLastRepresentation = kSimd256
};

bool IsSubtype(MachineRepresentation rep1, MachineRepresentation rep2);

#define ASSERT_CONSECUTIVE(rep1, rep2)                                      \
  static_assert(static_cast<uint8_t>(MachineRepresentation::k##rep1) + 1 == \
                    static_cast<uint8_t>(MachineRepresentation::k##rep2),   \
                #rep1 " and " #rep2 " must be consecutive.");

ASSERT_CONSECUTIVE(Word8, Word16)
ASSERT_CONSECUTIVE(Word16, Word32)
ASSERT_CONSECUTIVE(Word32, Word64)
ASSERT_CONSECUTIVE(Float16, Float32)
ASSERT_CONSECUTIVE(Float32, Float64)
ASSERT_CONSECUTIVE(Float64, Simd128)
ASSERT_CONSECUTIVE(Simd128, Simd256)
#undef ASSERT_CONSECUTIVE

static_assert(MachineRepresentation::kLastRepresentation ==
                  MachineRepresentation::kSimd256,
              "FP and SIMD representations must be last.");

static_assert(static_cast<int>(MachineRepresentation::kLastRepresentation) <
                  kIntSize * kBitsPerByte,
              "Bit masks of MachineRepresentation should fit in an int");

V8_EXPORT_PRIVATE const char* MachineReprToString(MachineRepresentation);

enum class MachineSemantic : uint8_t {
  kNone,
  kBool,
  kInt32,
  kUint32,
  kInt64,
  kUint64,
  kSignedBigInt64,
  kUnsignedBigInt64,
  kNumber,
  kHoleyFloat64,
  kAny
};

V8_EXPORT_PRIVATE inline constexpr int ElementSizeLog2Of(MachineRepresentation);

V8_EXPORT_PRIVATE inline constexpr int ElementSizeInBytes(
    MachineRepresentation);

class MachineType {
 public:
  constexpr MachineType()
      : representation_(MachineRepresentation::kNone),
        semantic_(MachineSemantic::kNone) {}
  constexpr MachineType(MachineRepresentation representation,
                        MachineSemantic semantic)
      : representation_(representation), semantic_(semantic) {}

  constexpr bool operator==(MachineType other) const {
    return representation() == other.representation() &&
           semantic() == other.semantic();
  }

  constexpr bool operator!=(MachineType other) const {
    return !(*this == other);
  }

  constexpr MachineRepresentation representation() const {
    return representation_;
  }
  constexpr MachineSemantic semantic() const { return semantic_; }

  constexpr bool IsNone() const {
    return representation() == MachineRepresentation::kNone;
  }

  constexpr bool IsMapWord() const {
    return representation() == MachineRepresentation::kMapWord;
  }

  constexpr bool IsSigned() const {
    return semantic() == MachineSemantic::kInt32 ||
           semantic() == MachineSemantic::kInt64;
  }
  constexpr bool IsUnsigned() const {
    return semantic() == MachineSemantic::kUint32 ||
           semantic() == MachineSemantic::kUint64;
  }
  constexpr bool IsTagged() const {
    return representation() == MachineRepresentation::kTaggedPointer ||
           representation() == MachineRepresentation::kTaggedSigned ||
           representation() == MachineRepresentation::kTagged;
  }
  constexpr bool IsTaggedSigned() const {
    return representation() == MachineRepresentation::kTaggedSigned;
  }
  constexpr bool IsTaggedPointer() const {
    return representation() == MachineRepresentation::kTaggedPointer;
  }
  constexpr bool IsCompressed() const {
    return representation() == MachineRepresentation::kCompressedPointer ||
           representation() == MachineRepresentation::kCompressed;
  }
  constexpr bool IsCompressedPointer() const {
    return representation() == MachineRepresentation::kCompressedPointer;
  }
  constexpr bool IsIndirectPointer() const {
    return representation() == MachineRepresentation::kIndirectPointer;
  }
  constexpr static MachineRepresentation PointerRepresentation() {
    return (kSystemPointerSize == 4) ? MachineRepresentation::kWord32
                                     : MachineRepresentation::kWord64;
  }
  constexpr static MachineType UintPtr() {
    return (kSystemPointerSize == 4) ? Uint32() : Uint64();
  }
  constexpr static MachineType IntPtr() {
    return (kSystemPointerSize == 4) ? Int32() : Int64();
  }
  constexpr static MachineType Int8() {
    return MachineType(MachineRepresentation::kWord8, MachineSemantic::kInt32);
  }
  constexpr static MachineType Uint8() {
    return MachineType(MachineRepresentation::kWord8, MachineSemantic::kUint32);
  }
  constexpr static MachineType Int16() {
    return MachineType(MachineRepresentation::kWord16, MachineSemantic::kInt32);
  }
  constexpr static MachineType Uint16() {
    return MachineType(MachineRepresentation::kWord16,
                       MachineSemantic::kUint32);
  }
  constexpr static MachineType Int32() {
    return MachineType(MachineRepresentation::kWord32, MachineSemantic::kInt32);
  }
  constexpr static MachineType Uint32() {
    return MachineType(MachineRepresentation::kWord32,
                       MachineSemantic::kUint32);
  }
  constexpr static MachineType Int64() {
    return MachineType(MachineRepresentation::kWord64, MachineSemantic::kInt64);
  }
  constexpr static MachineType Uint64() {
    return MachineType(MachineRepresentation::kWord64,
                       MachineSemantic::kUint64);
  }
  constexpr static MachineType SignedBigInt64() {
    return MachineType(MachineRepresentation::kWord64,
                       MachineSemantic::kSignedBigInt64);
  }
  constexpr static MachineType UnsignedBigInt64() {
    return MachineType(MachineRepresentation::kWord64,
                       MachineSemantic::kUnsignedBigInt64);
  }
  constexpr static MachineType Float16() {
    return MachineType(MachineRepresentation::kFloat16,
                       MachineSemantic::kNumber);
  }
  constexpr static MachineType Float32() {
    return MachineType(MachineRepresentation::kFloat32,
                       MachineSemantic::kNumber);
  }
  constexpr static MachineType Float64() {
    return MachineType(MachineRepresentation::kFloat64,
                       MachineSemantic::kNumber);
  }
  constexpr static MachineType HoleyFloat64() {
    return MachineType(MachineRepresentation::kFloat64,
                       MachineSemantic::kHoleyFloat64);
  }
  constexpr static MachineType Simd128() {
    return MachineType(MachineRepresentation::kSimd128, MachineSemantic::kNone);
  }
  constexpr static MachineType Simd256() {
    return MachineType(MachineRepresentation::kSimd256, MachineSemantic::kNone);
  }
  constexpr static MachineType Pointer() {
    return MachineType(PointerRepresentation(), MachineSemantic::kNone);
  }
  constexpr static MachineType TaggedPointer() {
    return MachineType(MachineRepresentation::kTaggedPointer,
                       MachineSemantic::kAny);
  }
  constexpr static MachineType MapInHeader() {
    return MachineType(MachineRepresentation::kMapWord, MachineSemantic::kAny);
  }
  constexpr static MachineType TaggedSigned() {
    return MachineType(MachineRepresentation::kTaggedSigned,
                       MachineSemantic::kInt32);
  }
  constexpr static MachineType AnyTagged() {
    return MachineType(MachineRepresentation::kTagged, MachineSemantic::kAny);
  }
  constexpr static MachineType CompressedPointer() {
    return MachineType(MachineRepresentation::kCompressedPointer,
                       MachineSemantic::kAny);
  }
  constexpr static MachineType AnyCompressed() {
    return MachineType(MachineRepresentation::kCompressed,
                       MachineSemantic::kAny);
  }
  constexpr static MachineType SandboxedPointer() {
    return MachineType(MachineRepresentation::kSandboxedPointer,
                       MachineSemantic::kInt64);
  }
  constexpr static MachineType ProtectedPointer() {
    return MachineType(MachineRepresentation::kProtectedPointer,
                       MachineSemantic::kAny);
  }
  constexpr static MachineType IndirectPointer() {
    return MachineType(MachineRepresentation::kIndirectPointer,
                       MachineSemantic::kInt32);
  }
  constexpr static MachineType Bool() {
    return MachineType(MachineRepresentation::kBit, MachineSemantic::kBool);
  }
  constexpr static MachineType None() {
    return MachineType(MachineRepresentation::kNone, MachineSemantic::kNone);
  }

  static MachineType TypeForRepresentation(const MachineRepresentation& rep,
                                           bool isSigned = true) {
    switch (rep) {
      case MachineRepresentation::kNone:
        return MachineType::None();
      case MachineRepresentation::kBit:
        return MachineType::Bool();
      case MachineRepresentation::kWord8:
        return isSigned ? MachineType::Int8() : MachineType::Uint8();
      case MachineRepresentation::kWord16:
        return isSigned ? MachineType::Int16() : MachineType::Uint16();
      case MachineRepresentation::kWord32:
        return isSigned ? MachineType::Int32() : MachineType::Uint32();
      case MachineRepresentation::kWord64:
        return isSigned ? MachineType::Int64() : MachineType::Uint64();
      case MachineRepresentation::kFloat16:
        return MachineType::Float16();
      case MachineRepresentation::kFloat32:
        return MachineType::Float32();
      case MachineRepresentation::kFloat64:
        return MachineType::Float64();
      case MachineRepresentation::kSimd128:
        return MachineType::Simd128();
      case MachineRepresentation::kSimd256:
        return MachineType::Simd256();
      case MachineRepresentation::kTagged:
        return MachineType::AnyTagged();
      case MachineRepresentation::kTaggedSigned:
        return MachineType::TaggedSigned();
      case MachineRepresentation::kTaggedPointer:
        return MachineType::TaggedPointer();
      case MachineRepresentation::kCompressed:
        return MachineType::AnyCompressed();
      case MachineRepresentation::kIndirectPointer:
        return MachineType::IndirectPointer();
      case MachineRepresentation::kCompressedPointer:
        return MachineType::CompressedPointer();
      case MachineRepresentation::kSandboxedPointer:
        return MachineType::SandboxedPointer();
      default:
        UNREACHABLE();
    }
  }

  static MachineType TypeForCType(const CTypeInfo& type) {
    switch (type.GetType()) {
      case CTypeInfo::Type::kVoid:
        return MachineType::AnyTagged();
      case CTypeInfo::Type::kBool:
        return MachineType::Bool();
      case CTypeInfo::Type::kUint8:
        return MachineType::Uint8();
      case CTypeInfo::Type::kInt32:
        return MachineType::Int32();
      case CTypeInfo::Type::kUint32:
        return MachineType::Uint32();
      case CTypeInfo::Type::kInt64:
        return MachineType::Int64();
      case CTypeInfo::Type::kAny:
        static_assert(
            sizeof(AnyCType) == kInt64Size,
            "CTypeInfo::Type::kAny is assumed to be of size 64 bits.");
        return MachineType::Int64();
      case CTypeInfo::Type::kUint64:
        return MachineType::Uint64();
      case CTypeInfo::Type::kFloat32:
        return MachineType::Float32();
      case CTypeInfo::Type::kFloat64:
        return MachineType::Float64();
      case CTypeInfo::Type::kPointer:
        return MachineType::Pointer();
      case CTypeInfo::Type::kV8Value:
      case CTypeInfo::Type::kSeqOneByteString:
      case CTypeInfo::Type::kApiObject:
        return MachineType::AnyTagged();
    }
  }

  constexpr bool LessThanOrEqualPointerSize() const {
    return ElementSizeLog2Of(this->representation()) <= kSystemPointerSizeLog2;
  }

  constexpr uint8_t MemSize() const {
    return 1 << i::ElementSizeLog2Of(this->representation());
  }

 private:
  MachineRepresentation representation_;
  MachineSemantic semantic_;
};

V8_INLINE size_t hash_value(MachineRepresentation rep) {
  return static_cast<size_t>(rep);
}

V8_INLINE size_t hash_value(MachineType type) {
  return static_cast<size_t>(type.representation()) +
         static_cast<size_t>(type.semantic()) * 16;
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           MachineRepresentation rep);
std::ostream& operator<<(std::ostream& os, MachineSemantic type);
V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os, MachineType type);

constexpr inline bool IsIntegral(MachineRepresentation rep) {
  return rep >= MachineRepresentation::kWord8 &&
         rep <= MachineRepresentation::kWord64;
}

constexpr inline bool IsFloatingPoint(MachineRepresentation rep) {
  return rep >= MachineRepresentation::kFirstFPRepresentation;
}

constexpr inline bool IsSimd128(MachineRepresentation rep) {
  return rep == MachineRepresentation::kSimd128;
}

constexpr inline bool CanBeTaggedPointer(MachineRepresentation rep) {
  return rep == MachineRepresentation::kTagged ||
         rep == MachineRepresentation::kTaggedPointer ||
         rep == MachineRepresentation::kMapWord;
}

constexpr inline bool CanBeTaggedSigned(MachineRepresentation rep) {
  return rep == MachineRepresentation::kTagged ||
         rep == MachineRepresentation::kTaggedSigned;
}

constexpr inline bool IsAnyTagged(MachineRepresentation rep) {
  return CanBeTaggedPointer(rep) || rep == MachineRepresentation::kTaggedSigned;
}

constexpr inline bool CanBeCompressedPointer(MachineRepresentation rep) {
  return rep == MachineRepresentation::kCompressed ||
         rep == MachineRepresentation::kCompressedPointer;
}

constexpr inline bool CanBeIndirectPointer(MachineRepresentation rep) {
  return rep == MachineRepresentation::kIndirectPointer;
}

// Note: this is used in particular to decide which spill slots need
// to be visited by the GC.
constexpr inline bool CanBeTaggedOrCompressedPointer(
    MachineRepresentation rep) {
  return CanBeTaggedPointer(rep) || CanBeCompressedPointer(rep) ||
         rep == MachineRepresentation::kProtectedPointer;
}

constexpr inline bool CanBeTaggedOrCompressedOrIndirectPointer(
    MachineRepresentation rep) {
  return CanBeTaggedPointer(rep) || CanBeCompressedPointer(rep) ||
         CanBeIndirectPointer(rep);
}

constexpr inline bool IsAnyCompressed(MachineRepresentation rep) {
  return CanBeCompressedPointer(rep);
}

// Gets the log2 of the element size in bytes of the machine type.
V8_EXPORT_PRIVATE inline constexpr int ElementSizeLog2Of(
    MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kBit:
    case MachineRepresentation::kWord8:
      return 0;
    case MachineRepresentation::kWord16:
    case MachineRepresentation::kFloat16:
      return 1;
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kIndirectPointer:
      return 2;
    case MachineRepresentation::kWord64:
    case MachineRepresentation::kFloat64:
      return 3;
    case MachineRepresentation::kSimd128:
      return 4;
    case MachineRepresentation::kSimd256:
      return 5;
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kMapWord:
    case MachineRepresentation::kCompressedPointer:
    case MachineRepresentation::kCompressed:
    case MachineRepresentation::kProtectedPointer:
      return kTaggedSizeLog2;
    case MachineRepresentation::kSandboxedPointer:
      return kSystemPointerSizeLog2;
    default:
      UNREACHABLE();
  }
}

constexpr int kMaximumReprSizeLog2 =
    ElementSizeLog2Of(MachineRepresentation::kSimd128);
constexpr int kMaximumReprSizeInBytes = 1 << kMaximumReprSizeLog2;

static_assert(kMaximumReprSizeLog2 >=
              ElementSizeLog2Of(MachineRepresentation::kTagged));
static_assert(kMaximumReprSizeLog2 >=
              ElementSizeLog2Of(MachineRepresentation::kWord64));

V8_EXPORT_PRIVATE inline constexpr int ElementSizeInBytes(
    MachineRepresentation rep) {
  return 1 << ElementSizeLog2Of(rep);
}

inline constexpr int ElementSizeInBits(MachineRepresentation rep) {
  return 8 * ElementSizeInBytes(rep);
}

inline constexpr uint64_t MaxUnsignedValue(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kWord8:
      return std::numeric_limits<uint8_t>::max();
    case MachineRepresentation::kWord16:
      return std::numeric_limits<uint16_t>::max();
    case MachineRepresentation::kWord32:
      return std::numeric_limits<uint32_t>::max();
    case MachineRepresentation::kWord64:
      return std::numeric_limits<uint64_t>::max();
    default:
      UNREACHABLE();
  }
}

V8_EXPORT_PRIVATE inline constexpr int ElementSizeInPointers(
    MachineRepresentation rep) {
  return (ElementSizeInBytes(rep) + kSystemPointerSize - 1) /
         kSystemPointerSize;
}

// Converts representation to bit for representation masks.
V8_EXPORT_PRIVATE inline constexpr int RepresentationBit(
    MachineRepresentation rep) {
  return 1 << static_cast<int>(rep);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_MACHINE_TYPE_H_
