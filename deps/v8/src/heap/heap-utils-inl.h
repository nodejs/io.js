// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_UTILS_INL_H_
#define V8_HEAP_HEAP_UTILS_INL_H_

#include "src/heap/heap-utils.h"
#include "src/heap/memory-chunk-inl.h"

namespace v8::internal {

// static
Heap* HeapUtils::GetOwnerHeap(Tagged<HeapObject> object) {
  return MemoryChunk::FromHeapObject(object)->GetHeap();
}

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_UTILS_INL_H_
