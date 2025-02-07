// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ISOLATE_UTILS_INL_H_
#define V8_EXECUTION_ISOLATE_UTILS_INL_H_

#include "src/common/ptr-compr-inl.h"
#include "src/execution/isolate-utils.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-write-barrier-inl.h"

namespace v8 {
namespace internal {

V8_INLINE Heap* GetHeapFromWritableObject(Tagged<HeapObject> object) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  return chunk->GetHeap();
}

V8_INLINE Isolate* GetIsolateFromWritableObject(Tagged<HeapObject> object) {
  return Isolate::FromHeap(GetHeapFromWritableObject(object));
}

V8_INLINE Heap* GetHeapFromWritableObject(const HeapObjectLayout& object) {
  return GetHeapFromWritableObject(Tagged(&object));
}

V8_INLINE Isolate* GetIsolateFromWritableObject(
    const HeapObjectLayout& object) {
  return GetIsolateFromWritableObject(Tagged(&object));
}

V8_INLINE bool GetIsolateFromHeapObject(Tagged<HeapObject> object,
                                        Isolate** isolate) {
  MemoryChunk* chunk = MemoryChunk::FromHeapObject(object);
  if (chunk->InReadOnlySpace()) {
    *isolate = nullptr;
    return false;
  }
  *isolate = Isolate::FromHeap(chunk->GetHeap());
  return true;
}

// Use this function instead of Internals::GetIsolateForSandbox for internal
// code, as this function is fully inlinable.
V8_INLINE static Isolate* GetIsolateForSandbox(Tagged<HeapObject> object) {
#ifdef V8_ENABLE_SANDBOX
  return GetIsolateFromWritableObject(object);
#else
  // Not used in non-sandbox mode.
  return nullptr;
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ISOLATE_UTILS_INL_H_
