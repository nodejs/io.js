// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_COMBINED_HEAP_H_
#define V8_HEAP_COMBINED_HEAP_H_

#include "src/heap/heap.h"
#include "src/heap/read-only-heap.h"
#include "src/heap/safepoint.h"
#include "src/objects/objects.h"

namespace v8 {
namespace internal {

// This class allows iteration over the entire heap (Heap and ReadOnlyHeap). It
// uses the HeapObjectIterator to iterate over non-read-only objects and accepts
// the same filtering option.
class V8_EXPORT_PRIVATE CombinedHeapObjectIterator final {
 public:
  CombinedHeapObjectIterator(
      Heap* heap, HeapObjectIterator::HeapObjectsFiltering filtering =
                      HeapObjectIterator::HeapObjectsFiltering::kNoFiltering);
  Tagged<HeapObject> Next();

 private:
  HeapObjectIterator heap_iterator_;
  ReadOnlyHeapObjectIterator ro_heap_iterator_;
};

V8_WARN_UNUSED_RESULT inline bool IsValidHeapObject(Heap* heap,
                                                    Tagged<HeapObject> object) {
  return ReadOnlyHeap::Contains(object) || heap->Contains(object) ||
         heap->SharedHeapContains(object);
}

V8_WARN_UNUSED_RESULT inline bool IsValidCodeObject(Heap* heap,
                                                    Tagged<HeapObject> object) {
  if (V8_EXTERNAL_CODE_SPACE_BOOL) {
    return heap->ContainsCode(object);
  } else {
    return ReadOnlyHeap::Contains(object) || heap->ContainsCode(object);
  }
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_COMBINED_HEAP_H_
