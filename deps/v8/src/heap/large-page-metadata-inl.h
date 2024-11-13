// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_LARGE_PAGE_METADATA_INL_H_
#define V8_HEAP_LARGE_PAGE_METADATA_INL_H_

#include "src/heap/large-page-metadata.h"
#include "src/heap/mutable-page-metadata-inl.h"

namespace v8 {
namespace internal {

// static
LargePageMetadata* LargePageMetadata::FromHeapObject(Tagged<HeapObject> o) {
  DCHECK(!V8_ENABLE_THIRD_PARTY_HEAP_BOOL);
  return cast(MutablePageMetadata::FromHeapObject(o));
}

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_LARGE_PAGE_METADATA_INL_H_
