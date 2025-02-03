// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_INDIRECT_POINTER_H_
#define V8_SANDBOX_INDIRECT_POINTER_H_

#include "src/common/globals.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

// Indirect pointers.
//
// An indirect pointer references a HeapObject (like a tagged pointer), but
// does so through a pointer table indirection. Indirect pointers are used when
// the sandbox is enabled to reference objects _outside_ of the sandbox in a
// memory-safe way. For that, each indirect pointer has an associated
// IndirectPointerTag which encodes the type of the referenced object. The
// pointer table indirection then ensures that the tag of the entry in the
// table matches the type of the referenced object, or else the pointer will be
// invalid (it cannot be dereferenced).

// Initialize the 'self' indirect pointer that contains a reference back to the
// owning object through its pointer table entry. For Code objects, this will
// allocate an entry in the code pointer table. For all other trusted objects,
// this will allocate an entry in the trusted pointer table.
//
// Only available when the sandbox is enabled.
V8_INLINE void InitSelfIndirectPointerField(Address field_address,
                                            IsolateForSandbox isolate,
                                            Tagged<HeapObject> host,
                                            IndirectPointerTag tag);

// Reads the IndirectPointerHandle from the field and loads the Object
// referenced by this handle from the appropriate pointer table. The given
// IndirectPointerTag specifies the expected type of object and determines
// which pointer table is used: the code pointer table for Code objects and the
// trusted pointer table for all other trusted objects.
//
// Only available when the sandbox is enabled.
template <IndirectPointerTag tag>
V8_INLINE Tagged<Object> ReadIndirectPointerField(Address field_address,
                                                  IsolateForSandbox isolate,
                                                  AcquireLoadTag);

// Loads the 'self' IndirectPointerHandle from the given object and stores it
// into the indirect pointer field. In this way, the field becomes a (indirect)
// reference to the given object.
//
// Only available when the sandbox is enabled.
template <IndirectPointerTag tag>
V8_INLINE void WriteIndirectPointerField(Address field_address,
                                         Tagged<ExposedTrustedObject> value,
                                         ReleaseStoreTag);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_INDIRECT_POINTER_H_
