// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/turboshaft/representations.h"

namespace v8::internal::compiler::turboshaft {

std::ostream& operator<<(std::ostream& os, MaybeRegisterRepresentation rep) {
  switch (rep) {
    case MaybeRegisterRepresentation::Word32():
      return os << "Word32";
    case MaybeRegisterRepresentation::Word64():
      return os << "Word64";
    case MaybeRegisterRepresentation::Float32():
      return os << "Float32";
    case MaybeRegisterRepresentation::Float64():
      return os << "Float64";
    case MaybeRegisterRepresentation::Tagged():
      return os << "Tagged";
    case MaybeRegisterRepresentation::Compressed():
      return os << "Compressed";
    case MaybeRegisterRepresentation::Simd128():
      return os << "Simd128";
    case MaybeRegisterRepresentation::Simd256():
      return os << "Simd256";
    case MaybeRegisterRepresentation::None():
      return os << "None";
  }
}

std::ostream& operator<<(std::ostream& os, MemoryRepresentation rep) {
  switch (rep) {
    case MemoryRepresentation::Int8():
      return os << "Int8";
    case MemoryRepresentation::Uint8():
      return os << "Uint8";
    case MemoryRepresentation::Int16():
      return os << "Int16";
    case MemoryRepresentation::Uint16():
      return os << "Uint16";
    case MemoryRepresentation::Int32():
      return os << "Int32";
    case MemoryRepresentation::Uint32():
      return os << "Uint32";
    case MemoryRepresentation::Int64():
      return os << "Int64";
    case MemoryRepresentation::Uint64():
      return os << "Uint64";
    case MemoryRepresentation::Float16():
      return os << "Float16";
    case MemoryRepresentation::Float32():
      return os << "Float32";
    case MemoryRepresentation::Float64():
      return os << "Float64";
    case MemoryRepresentation::AnyTagged():
      return os << "AnyTagged";
    case MemoryRepresentation::TaggedPointer():
      return os << "TaggedPointer";
    case MemoryRepresentation::TaggedSigned():
      return os << "TaggedSigned";
    case MemoryRepresentation::AnyUncompressedTagged():
      return os << "AnyUncompressedTagged";
    case MemoryRepresentation::UncompressedTaggedPointer():
      return os << "UncompressedTaggedPointer";
    case MemoryRepresentation::UncompressedTaggedSigned():
      return os << "UncompressedTaggedSigned";
    case MemoryRepresentation::ProtectedPointer():
      return os << "ProtectedPointer";
    case MemoryRepresentation::IndirectPointer():
      return os << "IndirectPointer";
    case MemoryRepresentation::SandboxedPointer():
      return os << "SandboxedPointer";
    case MemoryRepresentation::Simd128():
      return os << "Simd128";
    case MemoryRepresentation::Simd256():
      return os << "Simd256";
  }
}
}  // namespace v8::internal::compiler::turboshaft
