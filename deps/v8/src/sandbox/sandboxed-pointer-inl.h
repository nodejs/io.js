// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_SANDBOXED_POINTER_INL_H_
#define V8_SANDBOX_SANDBOXED_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/common/ptr-compr-inl.h"
#include "src/sandbox/sandbox.h"
#include "src/sandbox/sandboxed-pointer.h"

namespace v8 {
namespace internal {

V8_INLINE Address ReadSandboxedPointerField(Address field_address,
                                            PtrComprCageBase cage_base) {
#ifdef V8_ENABLE_SANDBOX
  SandboxedPointer_t sandboxed_pointer =
      base::ReadUnalignedValue<SandboxedPointer_t>(field_address);

  Address offset = sandboxed_pointer >> kSandboxedPointerShift;
  Address pointer = cage_base.address() + offset;
  return pointer;
#else
  return ReadMaybeUnalignedValue<Address>(field_address);
#endif
}

V8_INLINE void WriteSandboxedPointerField(Address field_address,
                                          PtrComprCageBase cage_base,
                                          Address pointer) {
#ifdef V8_ENABLE_SANDBOX
  // The pointer must point into the sandbox.
  CHECK(Sandbox::current()->Contains(pointer));

  Address offset = pointer - cage_base.address();
  SandboxedPointer_t sandboxed_pointer = offset << kSandboxedPointerShift;
  base::WriteUnalignedValue<SandboxedPointer_t>(field_address,
                                                sandboxed_pointer);
#else
  WriteMaybeUnalignedValue<Address>(field_address, pointer);
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_SANDBOXED_POINTER_INL_H_
