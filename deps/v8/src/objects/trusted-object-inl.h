// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_TRUSTED_OBJECT_INL_H_
#define V8_OBJECTS_TRUSTED_OBJECT_INL_H_

#include "src/objects/instance-type-inl.h"
#include "src/objects/trusted-object.h"
#include "src/sandbox/sandbox.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

CAST_ACCESSOR(TrustedObject)
OBJECT_CONSTRUCTORS_IMPL(TrustedObject, HeapObject)

Tagged<TrustedObject> TrustedObject::ReadProtectedPointerField(
    int offset) const {
  return TaggedField<TrustedObject, 0, TrustedSpaceCompressionScheme>::load(
      *this, offset);
}

Tagged<TrustedObject> TrustedObject::ReadProtectedPointerField(
    int offset, AcquireLoadTag) const {
  return TaggedField<TrustedObject, 0,
                     TrustedSpaceCompressionScheme>::Acquire_Load(*this,
                                                                  offset);
}

void TrustedObject::WriteProtectedPointerField(int offset,
                                               Tagged<TrustedObject> value) {
  TaggedField<TrustedObject, 0, TrustedSpaceCompressionScheme>::store(
      *this, offset, value);
}

void TrustedObject::WriteProtectedPointerField(int offset,
                                               Tagged<TrustedObject> value,
                                               ReleaseStoreTag) {
  TaggedField<TrustedObject, 0, TrustedSpaceCompressionScheme>::Release_Store(
      *this, offset, value);
}

bool TrustedObject::IsProtectedPointerFieldCleared(int offset) const {
  return TaggedField<Object, 0, TrustedSpaceCompressionScheme>::load(
             *this, offset) == Smi::zero();
}

bool TrustedObject::IsProtectedPointerFieldCleared(int offset,
                                                   AcquireLoadTag) const {
  return TaggedField<Object, 0, TrustedSpaceCompressionScheme>::Acquire_Load(
             *this, offset) == Smi::zero();
}

void TrustedObject::ClearProtectedPointerField(int offset) {
  TaggedField<Object, 0, TrustedSpaceCompressionScheme>::store(*this, offset,
                                                               Smi::zero());
}

void TrustedObject::ClearProtectedPointerField(int offset, ReleaseStoreTag) {
  TaggedField<Object, 0, TrustedSpaceCompressionScheme>::Release_Store(
      *this, offset, Smi::zero());
}

ProtectedPointerSlot TrustedObject::RawProtectedPointerField(
    int byte_offset) const {
  return ProtectedPointerSlot(field_address(byte_offset));
}

#ifdef VERIFY_HEAP
void TrustedObject::VerifyProtectedPointerField(Isolate* isolate, int offset) {
  Object::VerifyPointer(isolate, ReadProtectedPointerField(offset));
}
#endif

CAST_ACCESSOR(ExposedTrustedObject)
OBJECT_CONSTRUCTORS_IMPL(ExposedTrustedObject, TrustedObject)

void ExposedTrustedObject::init_self_indirect_pointer(
    IsolateForSandbox isolate) {
#ifdef V8_ENABLE_SANDBOX
  InitSelfIndirectPointerField(kSelfIndirectPointerOffset, isolate);
#endif
}

IndirectPointerHandle ExposedTrustedObject::self_indirect_pointer_handle()
    const {
#ifdef V8_ENABLE_SANDBOX
  return Relaxed_ReadField<IndirectPointerHandle>(kSelfIndirectPointerOffset);
#else
  UNREACHABLE();
#endif
}

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_TRUSTED_OBJECT_INL_H_
