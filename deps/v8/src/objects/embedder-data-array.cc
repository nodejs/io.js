// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/objects/embedder-data-array.h"

#include "src/execution/isolate.h"
#include "src/objects/embedder-data-array-inl.h"

namespace v8 {
namespace internal {

#ifdef V8_ENABLE_SANDBOX
namespace {
ExternalPointerHandle LoadExternalPointerHandle(const EmbedderDataSlot& slot) {
  Address loc = slot.address() + EmbedderDataSlot::kExternalPointerOffset;
  return ExternalPointerSlot(loc, kAnyExternalPointerTag).Relaxed_LoadHandle();
}
void StoreTaggedWithoutBarrier(const EmbedderDataSlot& slot,
                               Tagged<Object> value) {
  Address loc = slot.address() + EmbedderDataSlot::kTaggedPayloadOffset;
  ObjectSlot(loc).Relaxed_Store(value);
}
}  // namespace
#endif

// static
Handle<EmbedderDataArray> EmbedderDataArray::EnsureCapacity(
    Isolate* isolate, Handle<EmbedderDataArray> array, int index) {
  if (index < array->length()) return array;
  DCHECK_LT(index, kMaxLength);
  Handle<EmbedderDataArray> new_array =
      isolate->factory()->NewEmbedderDataArray(index + 1);
  DisallowGarbageCollection no_gc;
  // Last new space allocation does not require any write barriers.
#ifdef V8_ENABLE_SANDBOX
  for (int i = 0; i < array->length(); i++) {
    EmbedderDataSlot src(*array, i);
    EmbedderDataSlot dest(*new_array, i);
    ExternalPointerHandle src_handle = LoadExternalPointerHandle(src);
    if (src_handle != kNullExternalPointerHandle) {
      void* value;
      CHECK(src.ToAlignedPointer(isolate, &value));
      CHECK(dest.store_aligned_pointer(isolate, *new_array, value));
    } else {
      StoreTaggedWithoutBarrier(dest, src.load_tagged());
    }
  }
#else
  size_t size = array->length() * kEmbedderDataSlotSize;
  MemCopy(reinterpret_cast<void*>(new_array->slots_start()),
          reinterpret_cast<void*>(array->slots_start()), size);
#endif  // V8_ENABLE_SANDBOX
  return new_array;
}

}  // namespace internal
}  // namespace v8
