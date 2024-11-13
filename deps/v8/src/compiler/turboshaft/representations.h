// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_REPRESENTATIONS_H_
#define V8_COMPILER_TURBOSHAFT_REPRESENTATIONS_H_

#include <cstdint>

#include "include/v8-internal.h"
#include "src/base/functional.h"
#include "src/base/logging.h"
#include "src/codegen/machine-type.h"
#include "src/compiler/turboshaft/utils.h"

namespace v8::internal::compiler::turboshaft {

class WordRepresentation;
class FloatRepresentation;

// Optional register representation.
class MaybeRegisterRepresentation {
 public:
  enum class Enum : uint8_t {
    kWord32,
    kWord64,
    kFloat32,
    kFloat64,
    kTagged,
    kCompressed,
    kSimd128,
    kSimd256,
    kNone,  // No register representation.
  };

  explicit constexpr MaybeRegisterRepresentation(Enum value) : value_(value) {}
  constexpr MaybeRegisterRepresentation() : value_(kInvalid) {}

  constexpr bool is_valid() const { return value_ != kInvalid; }

  constexpr Enum value() const {
    DCHECK(is_valid());
    return value_;
  }

  constexpr operator Enum() const { return value(); }

  static constexpr MaybeRegisterRepresentation Word32() {
    return MaybeRegisterRepresentation(Enum::kWord32);
  }

  static constexpr MaybeRegisterRepresentation Word64() {
    return MaybeRegisterRepresentation(Enum::kWord64);
  }

  static constexpr MaybeRegisterRepresentation WordPtr() {
    if constexpr (kSystemPointerSize == 4) {
      return Word32();
    } else {
      DCHECK_EQ(kSystemPointerSize, 8);
      return Word64();
    }
  }

  static constexpr MaybeRegisterRepresentation Float32() {
    return MaybeRegisterRepresentation(Enum::kFloat32);
  }

  static constexpr MaybeRegisterRepresentation Float64() {
    return MaybeRegisterRepresentation(Enum::kFloat64);
  }

  static constexpr MaybeRegisterRepresentation Tagged() {
    return MaybeRegisterRepresentation(Enum::kTagged);
  }

  static constexpr MaybeRegisterRepresentation Compressed() {
    return MaybeRegisterRepresentation(Enum::kCompressed);
  }

  static constexpr MaybeRegisterRepresentation Simd128() {
    return MaybeRegisterRepresentation(Enum::kSimd128);
  }

  static constexpr MaybeRegisterRepresentation Simd256() {
    return MaybeRegisterRepresentation(Enum::kSimd256);
  }

  static constexpr MaybeRegisterRepresentation None() {
    return MaybeRegisterRepresentation(Enum::kNone);
  }

  constexpr bool IsWord() const {
    switch (*this) {
      case Enum::kWord32:
      case Enum::kWord64:
        return true;
      case Enum::kFloat32:
      case Enum::kFloat64:
      case Enum::kTagged:
      case Enum::kCompressed:
      case Enum::kSimd128:
      case Enum::kSimd256:
      case Enum::kNone:
        return false;
    }
  }

  constexpr bool IsFloat() const {
    switch (*this) {
      case Enum::kFloat32:
      case Enum::kFloat64:
        return true;
      case Enum::kWord32:
      case Enum::kWord64:
      case Enum::kTagged:
      case Enum::kCompressed:
      case Enum::kSimd128:
      case Enum::kSimd256:
      case Enum::kNone:
        return false;
    }
  }

  constexpr bool IsTaggedOrCompressed() const {
    switch (*this) {
      case Enum::kTagged:
      case Enum::kCompressed:
        return true;
      case Enum::kWord32:
      case Enum::kWord64:
      case Enum::kFloat32:
      case Enum::kFloat64:
      case Enum::kSimd128:
      case Enum::kSimd256:
      case Enum::kNone:
        return false;
    }
  }

  uint64_t MaxUnsignedValue() const {
    switch (this->value()) {
      case Word32():
        return std::numeric_limits<uint32_t>::max();
      case Word64():
        return std::numeric_limits<uint64_t>::max();
      case Enum::kFloat32:
      case Enum::kFloat64:
      case Enum::kTagged:
      case Enum::kCompressed:
      case Enum::kSimd128:
      case Enum::kSimd256:
      case Enum::kNone:
        UNREACHABLE();
    }
  }

  MachineRepresentation machine_representation() const {
    switch (this->value()) {
      case Word32():
        return MachineRepresentation::kWord32;
      case Word64():
        return MachineRepresentation::kWord64;
      case Float32():
        return MachineRepresentation::kFloat32;
      case Float64():
        return MachineRepresentation::kFloat64;
      case Tagged():
        return MachineRepresentation::kTagged;
      case Compressed():
        return MachineRepresentation::kCompressed;
      case Simd128():
        return MachineRepresentation::kSimd128;
      case Simd256():
        return MachineRepresentation::kSimd256;
      case None():
        UNREACHABLE();
    }
  }

  constexpr uint16_t bit_width() const {
    switch (this->value()) {
      case Word32():
        return 32;
      case Word64():
        return 64;
      case Float32():
        return 32;
      case Float64():
        return 64;
      case Tagged():
        return kSystemPointerSize;
      case Compressed():
        return kSystemPointerSize;
      case Simd128():
        return 128;
      case Simd256():
        return 256;
      case None():
        UNREACHABLE();
    }
  }

 private:
  Enum value_;

  static constexpr Enum kInvalid = static_cast<Enum>(-1);
};

class RegisterRepresentation : public MaybeRegisterRepresentation {
 public:
  enum class Enum : uint8_t {
    kWord32 = static_cast<int>(MaybeRegisterRepresentation::Enum::kWord32),
    kWord64 = static_cast<int>(MaybeRegisterRepresentation::Enum::kWord64),
    kFloat32 = static_cast<int>(MaybeRegisterRepresentation::Enum::kFloat32),
    kFloat64 = static_cast<int>(MaybeRegisterRepresentation::Enum::kFloat64),
    kTagged = static_cast<int>(MaybeRegisterRepresentation::Enum::kTagged),
    kCompressed =
        static_cast<int>(MaybeRegisterRepresentation::Enum::kCompressed),
    kSimd128 = static_cast<int>(MaybeRegisterRepresentation::Enum::kSimd128),
    kSimd256 = static_cast<int>(MaybeRegisterRepresentation::Enum::kSimd256),
  };

  explicit constexpr RegisterRepresentation(Enum value)
      : MaybeRegisterRepresentation(
            static_cast<MaybeRegisterRepresentation::Enum>(value)) {}
  RegisterRepresentation() = default;

  explicit constexpr RegisterRepresentation(MaybeRegisterRepresentation rep)
      : RegisterRepresentation(static_cast<Enum>(rep.value())) {}

  constexpr operator Enum() const { return value(); }

  constexpr Enum value() const {
    return static_cast<Enum>(MaybeRegisterRepresentation::value());
  }

  static constexpr RegisterRepresentation Word32() {
    return RegisterRepresentation(Enum::kWord32);
  }
  static constexpr RegisterRepresentation Word64() {
    return RegisterRepresentation(Enum::kWord64);
  }
  // The equivalent of intptr_t/uintptr_t: An integral type with the same size
  // as machine pointers.
  static constexpr RegisterRepresentation WordPtr() {
    return RegisterRepresentation(MaybeRegisterRepresentation::WordPtr());
  }
  static constexpr RegisterRepresentation Float32() {
    return RegisterRepresentation(Enum::kFloat32);
  }
  static constexpr RegisterRepresentation Float64() {
    return RegisterRepresentation(Enum::kFloat64);
  }
  // A tagged pointer stored in a register, in the case of pointer compression
  // it is an uncompressed pointer or a Smi.
  static constexpr RegisterRepresentation Tagged() {
    return RegisterRepresentation(Enum::kTagged);
  }
  // A compressed tagged pointer stored in a register, the upper 32bit are
  // unspecified.
  static constexpr RegisterRepresentation Compressed() {
    return RegisterRepresentation(Enum::kCompressed);
  }
  static constexpr RegisterRepresentation Simd128() {
    return RegisterRepresentation(Enum::kSimd128);
  }
  static constexpr RegisterRepresentation Simd256() {
    return RegisterRepresentation(Enum::kSimd256);
  }

  static constexpr RegisterRepresentation FromMachineRepresentation(
      MachineRepresentation rep) {
    switch (rep) {
      case MachineRepresentation::kBit:
      case MachineRepresentation::kWord8:
      case MachineRepresentation::kWord16:
      case MachineRepresentation::kWord32:
        return Word32();
      case MachineRepresentation::kWord64:
        return Word64();
      case MachineRepresentation::kTaggedSigned:
      case MachineRepresentation::kTaggedPointer:
      case MachineRepresentation::kTagged:
      case MachineRepresentation::kProtectedPointer:
        return Tagged();
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
        return Compressed();
      case MachineRepresentation::kFloat16:
      case MachineRepresentation::kFloat32:
        return Float32();
      case MachineRepresentation::kFloat64:
        return Float64();
      case MachineRepresentation::kSimd128:
        return Simd128();
      case MachineRepresentation::kSimd256:
        return Simd256();
      case MachineRepresentation::kMapWord:
        // Turboshaft does not support map packing.
        DCHECK(!V8_MAP_PACKING_BOOL);
        return RegisterRepresentation::Tagged();
      case MachineRepresentation::kIndirectPointer:
      case MachineRepresentation::kSandboxedPointer:
        // TODO(saelo/jkummerow): This is suspicious: after resolving the
        // indirection, we have a Tagged pointer.
        return WordPtr();
      case MachineRepresentation::kNone:
        UNREACHABLE();
    }
  }

  static constexpr RegisterRepresentation FromMachineType(MachineType type) {
    return FromMachineRepresentation(type.representation());
  }

  constexpr bool AllowImplicitRepresentationChangeTo(
      RegisterRepresentation dst_rep, bool graph_created_from_turbofan) const;

  constexpr RegisterRepresentation MapTaggedToWord() const {
    if (this->value() == RegisterRepresentation::Tagged()) {
      return COMPRESS_POINTERS_BOOL ? RegisterRepresentation::Word32()
                                    : RegisterRepresentation::WordPtr();
    }
    return *this;
  }
};

V8_INLINE constexpr bool operator==(MaybeRegisterRepresentation a,
                                    MaybeRegisterRepresentation b) {
  return a.value() == b.value();
}
V8_INLINE constexpr bool operator!=(MaybeRegisterRepresentation a,
                                    MaybeRegisterRepresentation b) {
  return a.value() != b.value();
}

V8_INLINE size_t hash_value(MaybeRegisterRepresentation rep) {
  return static_cast<size_t>(rep.value());
}

constexpr bool RegisterRepresentation::AllowImplicitRepresentationChangeTo(
    RegisterRepresentation dst_rep, bool graph_created_from_turbofan) const {
  if (*this == dst_rep) {
    return true;
  }
  switch (dst_rep.value()) {
    case RegisterRepresentation::Word32():
      // We allow implicit tagged -> untagged conversions.
      // Even without pointer compression, we use `Word32And` for Smi-checks on
      // tagged values.
      if (*this == any_of(RegisterRepresentation::Tagged(),
                          RegisterRepresentation::Compressed())) {
        return true;
      }
      if (graph_created_from_turbofan &&
          *this == RegisterRepresentation::Word64()) {
        // TODO(12783): Remove this once Turboshaft graphs are not constructed
        // via Turbofan any more. Unfortunately Turbofan has many implicit
        // truncations which are hard to fix. Still, for wasm it is required
        // that truncations in Turboshaft are explicit.
        return true;
      }
      break;
    case RegisterRepresentation::Word64():
      // We allow implicit tagged -> untagged conversions.
      if (kTaggedSize == kInt64Size &&
          *this == RegisterRepresentation::Tagged()) {
        return true;
      }
      break;
    case RegisterRepresentation::Tagged():
      // We allow implicit untagged -> tagged conversions. This is only safe for
      // Smi values.
      if (*this == RegisterRepresentation::WordPtr()) {
        return true;
      }
      break;
    case RegisterRepresentation::Compressed():
      // Compression is a no-op.
      if (*this == any_of(RegisterRepresentation::Tagged(),
                          RegisterRepresentation::WordPtr(),
                          RegisterRepresentation::Word32())) {
        return true;
      }
      break;
    default:
      break;
  }
  return false;
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           MaybeRegisterRepresentation rep);

template <typename T>
struct MultiSwitch<
    T, std::enable_if_t<std::is_base_of_v<MaybeRegisterRepresentation, T>>> {
  static constexpr uint64_t max_value = 8;
  static constexpr uint64_t encode(T rep) {
    const uint64_t value = static_cast<uint64_t>(rep.value());
    DCHECK_LT(value, max_value);
    return value;
  }
};

class WordRepresentation : public RegisterRepresentation {
 public:
  enum class Enum : uint8_t {
    kWord32 = static_cast<int>(RegisterRepresentation::Enum::kWord32),
    kWord64 = static_cast<int>(RegisterRepresentation::Enum::kWord64)
  };
  explicit constexpr WordRepresentation(Enum value)
      : RegisterRepresentation(
            static_cast<RegisterRepresentation::Enum>(value)) {}
  WordRepresentation() = default;
  explicit constexpr WordRepresentation(RegisterRepresentation rep)
      : WordRepresentation(static_cast<Enum>(rep.value())) {
    DCHECK(rep.IsWord());
  }

  static constexpr WordRepresentation Word32() {
    return WordRepresentation(Enum::kWord32);
  }
  static constexpr WordRepresentation Word64() {
    return WordRepresentation(Enum::kWord64);
  }

  static constexpr WordRepresentation WordPtr() {
    return WordRepresentation(RegisterRepresentation::WordPtr());
  }

  constexpr Enum value() const {
    return static_cast<Enum>(RegisterRepresentation::value());
  }
  constexpr operator Enum() const { return value(); }

  constexpr uint64_t MaxUnsignedValue() const {
    switch (this->value()) {
      case Word32():
        return std::numeric_limits<uint32_t>::max();
      case Word64():
        return std::numeric_limits<uint64_t>::max();
    }
  }
  constexpr int64_t MinSignedValue() const {
    switch (this->value()) {
      case Word32():
        return std::numeric_limits<int32_t>::min();
      case Word64():
        return std::numeric_limits<int64_t>::min();
    }
  }
  constexpr int64_t MaxSignedValue() const {
    switch (this->value()) {
      case Word32():
        return std::numeric_limits<int32_t>::max();
      case Word64():
        return std::numeric_limits<int64_t>::max();
    }
  }
};

class FloatRepresentation : public RegisterRepresentation {
 public:
  enum class Enum : uint8_t {
    kFloat32 = static_cast<int>(RegisterRepresentation::Enum::kFloat32),
    kFloat64 = static_cast<int>(RegisterRepresentation::Enum::kFloat64)
  };

  static constexpr FloatRepresentation Float32() {
    return FloatRepresentation(Enum::kFloat32);
  }
  static constexpr FloatRepresentation Float64() {
    return FloatRepresentation(Enum::kFloat64);
  }

  explicit constexpr FloatRepresentation(Enum value)
      : RegisterRepresentation(
            static_cast<RegisterRepresentation::Enum>(value)) {}
  explicit constexpr FloatRepresentation(RegisterRepresentation rep)
      : FloatRepresentation(static_cast<Enum>(rep.value())) {
    DCHECK(rep.IsFloat());
  }
  FloatRepresentation() = default;

  constexpr Enum value() const {
    return static_cast<Enum>(RegisterRepresentation::value());
  }
  constexpr operator Enum() const { return value(); }
};

class MemoryRepresentation {
 public:
  enum class Enum : uint8_t {
    kInt8,
    kUint8,
    kInt16,
    kUint16,
    kInt32,
    kUint32,
    kInt64,
    kUint64,
    kFloat16,
    kFloat32,
    kFloat64,
    kAnyTagged,
    kTaggedPointer,
    kTaggedSigned,
    kAnyUncompressedTagged,
    kUncompressedTaggedPointer,
    kUncompressedTaggedSigned,
    kProtectedPointer,
    kIndirectPointer,
    kSandboxedPointer,
    kSimd128,
    kSimd256
  };

  explicit constexpr MemoryRepresentation(Enum value) : value_(value) {}
  constexpr MemoryRepresentation() : value_(kInvalid) {}

  constexpr bool is_valid() const { return value_ != kInvalid; }

  constexpr Enum value() const {
    DCHECK(is_valid());
    return value_;
  }
  constexpr operator Enum() const { return value(); }

  static constexpr MemoryRepresentation Int8() {
    return MemoryRepresentation(Enum::kInt8);
  }
  static constexpr MemoryRepresentation Uint8() {
    return MemoryRepresentation(Enum::kUint8);
  }
  static constexpr MemoryRepresentation Int16() {
    return MemoryRepresentation(Enum::kInt16);
  }
  static constexpr MemoryRepresentation Uint16() {
    return MemoryRepresentation(Enum::kUint16);
  }
  static constexpr MemoryRepresentation Int32() {
    return MemoryRepresentation(Enum::kInt32);
  }
  static constexpr MemoryRepresentation Uint32() {
    return MemoryRepresentation(Enum::kUint32);
  }
  static constexpr MemoryRepresentation Int64() {
    return MemoryRepresentation(Enum::kInt64);
  }
  static constexpr MemoryRepresentation Uint64() {
    return MemoryRepresentation(Enum::kUint64);
  }
  static constexpr MemoryRepresentation UintPtr() {
    if constexpr (Is64()) {
      return Uint64();
    } else {
      return Uint32();
    }
  }
  static constexpr MemoryRepresentation Float16() {
    return MemoryRepresentation(Enum::kFloat16);
  }
  static constexpr MemoryRepresentation Float32() {
    return MemoryRepresentation(Enum::kFloat32);
  }
  static constexpr MemoryRepresentation Float64() {
    return MemoryRepresentation(Enum::kFloat64);
  }
  static constexpr MemoryRepresentation AnyTagged() {
    return MemoryRepresentation(Enum::kAnyTagged);
  }
  static constexpr MemoryRepresentation TaggedPointer() {
    return MemoryRepresentation(Enum::kTaggedPointer);
  }
  static constexpr MemoryRepresentation TaggedSigned() {
    return MemoryRepresentation(Enum::kTaggedSigned);
  }
  static constexpr MemoryRepresentation AnyUncompressedTagged() {
    return MemoryRepresentation(Enum::kAnyUncompressedTagged);
  }
  static constexpr MemoryRepresentation UncompressedTaggedPointer() {
    return MemoryRepresentation(Enum::kUncompressedTaggedPointer);
  }
  static constexpr MemoryRepresentation UncompressedTaggedSigned() {
    return MemoryRepresentation(Enum::kUncompressedTaggedSigned);
  }
  static constexpr MemoryRepresentation ProtectedPointer() {
    return MemoryRepresentation(Enum::kProtectedPointer);
  }
  static constexpr MemoryRepresentation IndirectPointer() {
    return MemoryRepresentation(Enum::kIndirectPointer);
  }
  static constexpr MemoryRepresentation SandboxedPointer() {
    return MemoryRepresentation(Enum::kSandboxedPointer);
  }
  static constexpr MemoryRepresentation Simd128() {
    return MemoryRepresentation(Enum::kSimd128);
  }
  static constexpr MemoryRepresentation Simd256() {
    return MemoryRepresentation(Enum::kSimd256);
  }

  bool IsSigned() const {
    switch (*this) {
      case Int8():
      case Int16():
      case Int32():
      case Int64():
        return true;
      case Uint8():
      case Uint16():
      case Uint32():
      case Uint64():
        return false;
      case Float16():
      case Float32():
      case Float64():
      case AnyTagged():
      case TaggedPointer():
      case TaggedSigned():
      case AnyUncompressedTagged():
      case UncompressedTaggedPointer():
      case UncompressedTaggedSigned():
      case ProtectedPointer():
      case IndirectPointer():
      case SandboxedPointer():
      case Simd128():
      case Simd256():
        UNREACHABLE();
    }
  }

  // This predicate is used in particular to decide which load/store ops
  // have to deal with pointer compression. Indirect/sandboxed pointers,
  // while they resolve to tagged pointers, return {false} because they
  // use incompatible compression schemes.
  bool IsCompressibleTagged() const {
    switch (*this) {
      case AnyTagged():
      case TaggedPointer():
      case TaggedSigned():
        return true;
      case Int8():
      case Int16():
      case Int32():
      case Int64():
      case Uint8():
      case Uint16():
      case Uint32():
      case Uint64():
      case Float16():
      case Float32():
      case Float64():
      case AnyUncompressedTagged():
      case UncompressedTaggedPointer():
      case UncompressedTaggedSigned():
      case IndirectPointer():
      case ProtectedPointer():
      case SandboxedPointer():
      case Simd128():
      case Simd256():
        return false;
    }
  }

  RegisterRepresentation ToRegisterRepresentation() const {
    switch (*this) {
      case Int8():
      case Uint8():
      case Int16():
      case Uint16():
      case Int32():
      case Uint32():
        return RegisterRepresentation::Word32();
      case Int64():
      case Uint64():
        return RegisterRepresentation::Word64();
      case Float16():
      case Float32():
        return RegisterRepresentation::Float32();
      case Float64():
        return RegisterRepresentation::Float64();
      case AnyTagged():
      case TaggedPointer():
      case TaggedSigned():
      case AnyUncompressedTagged():
      case UncompressedTaggedPointer():
      case UncompressedTaggedSigned():
      case IndirectPointer():
      case ProtectedPointer():
        return RegisterRepresentation::Tagged();
      case SandboxedPointer():
        return RegisterRepresentation::Word64();
      case Simd128():
        return RegisterRepresentation::Simd128();
      case Simd256():
        return RegisterRepresentation::Simd256();
    }
  }

  static MemoryRepresentation FromRegisterRepresentation(
      RegisterRepresentation repr, bool is_signed) {
    switch (repr.value()) {
      case RegisterRepresentation::Word32():
        return is_signed ? Int32() : Uint32();
      case RegisterRepresentation::Word64():
        return is_signed ? Int64() : Uint64();
      case RegisterRepresentation::Float32():
        return Float32();
      case RegisterRepresentation::Float64():
        return Float64();
      case RegisterRepresentation::Tagged():
        return AnyTagged();
      case RegisterRepresentation::Simd128():
        return Simd128();
      case RegisterRepresentation::Simd256():
        return Simd256();
      case RegisterRepresentation::Compressed():
        UNREACHABLE();
    }
  }

  // The required register representation for storing a value. When pointer
  // compression is enabled, we only store the lower 32bit of a tagged value,
  // which we indicate as `RegisterRepresentation::Compressed()` here.
  RegisterRepresentation ToRegisterRepresentationForStore() const {
    RegisterRepresentation result = ToRegisterRepresentation();
#ifdef V8_COMPRESS_POINTERS
    if (result == RegisterRepresentation::Tagged()) {
      result = RegisterRepresentation::Compressed();
    }
#endif
    return result;
  }

  MachineType ToMachineType() const {
    switch (*this) {
      case Int8():
        return MachineType::Int8();
      case Uint8():
        return MachineType::Uint8();
      case Int16():
        return MachineType::Int16();
      case Uint16():
        return MachineType::Uint16();
      case Int32():
        return MachineType::Int32();
      case Uint32():
        return MachineType::Uint32();
      case Int64():
        return MachineType::Int64();
      case Uint64():
        return MachineType::Uint64();
      case Float16():
        return MachineType::Float16();
      case Float32():
        return MachineType::Float32();
      case Float64():
        return MachineType::Float64();
      case AnyTagged():
        return MachineType::AnyTagged();
      case TaggedPointer():
        return MachineType::TaggedPointer();
      case TaggedSigned():
        return MachineType::TaggedSigned();
      case AnyUncompressedTagged():
        return MachineType::AnyTagged();
      case UncompressedTaggedPointer():
        return MachineType::TaggedPointer();
      case UncompressedTaggedSigned():
        return MachineType::TaggedSigned();
      case ProtectedPointer():
        return MachineType::ProtectedPointer();
      case IndirectPointer():
        return MachineType::IndirectPointer();
      case SandboxedPointer():
        return MachineType::SandboxedPointer();
      case Simd128():
        return MachineType::Simd128();
      case Simd256():
        return MachineType::Simd256();
    }
  }

  static MemoryRepresentation FromMachineType(MachineType type) {
    switch (type.representation()) {
      case MachineRepresentation::kWord8:
        return type.IsSigned() ? Int8() : Uint8();
      case MachineRepresentation::kWord16:
        return type.IsSigned() ? Int16() : Uint16();
      case MachineRepresentation::kWord32:
        return type.IsSigned() ? Int32() : Uint32();
      case MachineRepresentation::kWord64:
        return type.IsSigned() ? Int64() : Uint64();
      case MachineRepresentation::kTaggedSigned:
        return TaggedSigned();
      case MachineRepresentation::kTaggedPointer:
        return TaggedPointer();
      case MachineRepresentation::kMapWord:
        // Turboshaft does not support map packing.
        DCHECK(!V8_MAP_PACKING_BOOL);
        return TaggedPointer();
      case MachineRepresentation::kProtectedPointer:
        return ProtectedPointer();
      case MachineRepresentation::kIndirectPointer:
        return IndirectPointer();
      case MachineRepresentation::kTagged:
        return AnyTagged();
      case MachineRepresentation::kFloat16:
        return Float16();
      case MachineRepresentation::kFloat32:
        return Float32();
      case MachineRepresentation::kFloat64:
        return Float64();
      case MachineRepresentation::kSandboxedPointer:
        return SandboxedPointer();
      case MachineRepresentation::kSimd128:
        return Simd128();
      case MachineRepresentation::kSimd256:
        return Simd256();
      case MachineRepresentation::kNone:
      case MachineRepresentation::kBit:
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
        UNREACHABLE();
    }
  }

  static constexpr MemoryRepresentation FromMachineRepresentation(
      MachineRepresentation rep) {
    switch (rep) {
      case MachineRepresentation::kWord8:
        return Uint8();
      case MachineRepresentation::kWord16:
        return Uint16();
      case MachineRepresentation::kWord32:
        return Uint32();
      case MachineRepresentation::kWord64:
        return Uint64();
      case MachineRepresentation::kTaggedSigned:
        return TaggedSigned();
      case MachineRepresentation::kTaggedPointer:
        return TaggedPointer();
      case MachineRepresentation::kTagged:
        return AnyTagged();
      case MachineRepresentation::kFloat16:
        return Float16();
      case MachineRepresentation::kFloat32:
        return Float32();
      case MachineRepresentation::kFloat64:
        return Float64();
      case MachineRepresentation::kSandboxedPointer:
        return SandboxedPointer();
      case MachineRepresentation::kSimd128:
        return Simd128();
      case MachineRepresentation::kSimd256:
        return Simd256();
      case MachineRepresentation::kNone:
      case MachineRepresentation::kMapWord:
      case MachineRepresentation::kBit:
      case MachineRepresentation::kCompressedPointer:
      case MachineRepresentation::kCompressed:
      case MachineRepresentation::kProtectedPointer:
      case MachineRepresentation::kIndirectPointer:
        UNREACHABLE();
    }
  }

  constexpr uint8_t SizeInBytes() const {
    return uint8_t{1} << SizeInBytesLog2();
  }

  constexpr uint8_t SizeInBytesLog2() const {
    switch (*this) {
      case Int8():
      case Uint8():
        return 0;
      case Int16():
      case Uint16():
      case Float16():
        return 1;
      case Int32():
      case Uint32():
      case Float32():
      case IndirectPointer():
        return 2;
      case Int64():
      case Uint64():
      case Float64():
      case SandboxedPointer():
        return 3;
      case AnyTagged():
      case TaggedPointer():
      case TaggedSigned():
      case ProtectedPointer():
        return kTaggedSizeLog2;
      case AnyUncompressedTagged():
      case UncompressedTaggedPointer():
      case UncompressedTaggedSigned():
        return kSystemPointerSizeLog2;
      case Simd128():
        return 4;
      case Simd256():
        return 5;
    }
  }

 private:
  Enum value_;

  static constexpr Enum kInvalid = static_cast<Enum>(-1);
};

V8_INLINE constexpr bool operator==(MemoryRepresentation a,
                                    MemoryRepresentation b) {
  return a.value() == b.value();
}
V8_INLINE constexpr bool operator!=(MemoryRepresentation a,
                                    MemoryRepresentation b) {
  return a.value() != b.value();
}

V8_INLINE size_t hash_value(MemoryRepresentation rep) {
  return static_cast<size_t>(rep.value());
}

V8_EXPORT_PRIVATE std::ostream& operator<<(std::ostream& os,
                                           MemoryRepresentation rep);

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_REPRESENTATIONS_H_
