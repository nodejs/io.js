// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_FOREIGN_INL_H_
#define V8_OBJECTS_FOREIGN_INL_H_

#include "src/common/globals.h"
#include "src/heap/heap-write-barrier-inl.h"
#include "src/objects/foreign.h"
#include "src/objects/objects-inl.h"
#include "src/sandbox/external-pointer-inl.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8::internal {

#include "torque-generated/src/objects/foreign-tq-inl.inc"

TQ_OBJECT_CONSTRUCTORS_IMPL(Foreign)

template <ExternalPointerTag tag>
Address Foreign::foreign_address(IsolateForSandbox isolate) const {
  return HeapObject::ReadExternalPointerField<tag>(kForeignAddressOffset,
                                                   isolate);
}

template <ExternalPointerTag tag>
Address Foreign::foreign_address() const {
  IsolateForSandbox isolate = GetIsolateForSandbox(*this);
  return ReadExternalPointerField<tag>(kForeignAddressOffset, isolate);
}

template <ExternalPointerTag tag>
void Foreign::set_foreign_address(IsolateForSandbox isolate,
                                  const Address value) {
  WriteExternalPointerField<tag>(kForeignAddressOffset, isolate, value);
}

template <ExternalPointerTag tag>
void Foreign::init_foreign_address(IsolateForSandbox isolate,
                                   const Address initial_value) {
  InitExternalPointerField<tag>(kForeignAddressOffset, isolate, initial_value);
}

Address Foreign::foreign_address_unchecked() const {
  Isolate* isolate = GetIsolateForSandbox(*this);
  return ReadExternalPointerField<kAnyForeignTag>(kForeignAddressOffset,
                                                  isolate);
}

ExternalPointerTag Foreign::GetTag() const {
#ifdef V8_ENABLE_SANDBOX
  ExternalPointerHandle handle =
      RawExternalPointerField(kForeignAddressOffset, kAnyExternalPointerTag)
          .Relaxed_LoadHandle();
  return GetIsolateForSandbox(*this)->external_pointer_table().GetTag(handle);
#endif  // V8_ENABLE_SANDBOX
  // Without the sandbox the address is stored untagged; just return
  // kAnyExternalPointerTag.
  return kAnyExternalPointerTag;
}

}  // namespace v8::internal

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_FOREIGN_INL_H_
