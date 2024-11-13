// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_BACKEND_REGISTER_ALLOCATION_H_
#define V8_COMPILER_BACKEND_REGISTER_ALLOCATION_H_

#include "src/codegen/register-configuration.h"
#include "src/zone/zone.h"

namespace v8 {
namespace internal {
namespace compiler {

enum class RegisterKind { kGeneral, kDouble, kSimd128 };

inline int GetRegisterCount(const RegisterConfiguration* config,
                            RegisterKind kind) {
  switch (kind) {
    case RegisterKind::kGeneral:
      return config->num_general_registers();
    case RegisterKind::kDouble:
      return config->num_double_registers();
    case RegisterKind::kSimd128:
      return config->num_simd128_registers();
  }
}

inline int GetAllocatableRegisterCount(const RegisterConfiguration* config,
                                       RegisterKind kind) {
  switch (kind) {
    case RegisterKind::kGeneral:
      return config->num_allocatable_general_registers();
    case RegisterKind::kDouble:
      return config->num_allocatable_double_registers();
    case RegisterKind::kSimd128:
      return config->num_allocatable_simd128_registers();
  }
}

inline const int* GetAllocatableRegisterCodes(
    const RegisterConfiguration* config, RegisterKind kind) {
  switch (kind) {
    case RegisterKind::kGeneral:
      return config->allocatable_general_codes();
    case RegisterKind::kDouble:
      return config->allocatable_double_codes();
    case RegisterKind::kSimd128:
      return config->allocatable_simd128_codes();
  }
}

inline int ByteWidthForStackSlot(MachineRepresentation rep) {
  switch (rep) {
    case MachineRepresentation::kBit:
    case MachineRepresentation::kWord8:
    case MachineRepresentation::kWord16:
    case MachineRepresentation::kWord32:
    case MachineRepresentation::kFloat16:
    case MachineRepresentation::kFloat32:
    case MachineRepresentation::kSandboxedPointer:
      return kSystemPointerSize;
    case MachineRepresentation::kTaggedSigned:
    case MachineRepresentation::kTaggedPointer:
    case MachineRepresentation::kTagged:
    case MachineRepresentation::kCompressedPointer:
    case MachineRepresentation::kCompressed:
    case MachineRepresentation::kProtectedPointer:
      // TODO(ishell): kTaggedSize once half size locations are supported.
      return kSystemPointerSize;
    case MachineRepresentation::kWord64:
    case MachineRepresentation::kFloat64:
      return kDoubleSize;
    case MachineRepresentation::kSimd128:
      return kSimd128Size;
    case MachineRepresentation::kSimd256:
      return kSimd256Size;
    case MachineRepresentation::kNone:
    case MachineRepresentation::kMapWord:
    case MachineRepresentation::kIndirectPointer:
      UNREACHABLE();
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_BACKEND_REGISTER_ALLOCATION_H_
