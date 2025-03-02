// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_HEAP_UTILS_H_
#define V8_HEAP_HEAP_UTILS_H_

#include "src/common/globals.h"
#include "src/objects/tagged.h"

namespace v8::internal {

// This class provides heap-internal helper functions to provide
// data/information about heap objects.
class HeapUtils final : public AllStatic {
 public:
  // Returns the Heap (or nullptr) which owns the page of this object.
  static V8_INLINE Heap* GetOwnerHeap(Tagged<HeapObject> object);
};

}  // namespace v8::internal

#endif  // V8_HEAP_HEAP_UTILS_H_
