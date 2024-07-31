// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_UTILS_H_
#define V8_EXECUTION_ISOLATE_UTILS_H_

#include "src/common/globals.h"

namespace v8 {
namespace internal {

class HeapObjectLayout;

// Computes the pointer compression cage base from any read only or writable
// heap object. The resulting value is intended to be used only as a hoisted
// computation of cage base inside trivial accessors for optimizing value
// decompression. When pointer compression is disabled this function always
// returns nullptr.
V8_INLINE PtrComprCageBase GetPtrComprCageBase(Tagged<HeapObject> object);

V8_INLINE Heap* GetHeapFromWritableObject(Tagged<HeapObject> object);

V8_INLINE Isolate* GetIsolateFromWritableObject(Tagged<HeapObject> object);

// Support `*this` for HeapObjectLayout subclasses.
// TODO(leszeks): Change the NEVER_READ_ONLY_SPACE_IMPL macro to pass `this`
// instead of `*this` and use `const HeapObjectLayout*` here.
V8_INLINE Heap* GetHeapFromWritableObject(const HeapObjectLayout& object);
V8_INLINE Isolate* GetIsolateFromWritableObject(const HeapObjectLayout& object);

// Returns true if it succeeded to obtain isolate from given object.
// If it fails then the object is definitely a read-only object but it may also
// succeed for read only objects if pointer compression is enabled.
V8_INLINE bool GetIsolateFromHeapObject(Tagged<HeapObject> object,
                                        Isolate** isolate);

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ISOLATE_UTILS_H_
