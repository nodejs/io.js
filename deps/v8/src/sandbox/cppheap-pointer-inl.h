// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CPPHEAP_POINTER_INL_H_
#define V8_SANDBOX_CPPHEAP_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/objects/slots-inl.h"
#include "src/sandbox/cppheap-pointer-table-inl.h"
#include "src/sandbox/isolate-inl.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

// TODO(saelo): consider passing a CppHeapPointerTagRange as template parameter
// once C++20 is supported everywhere.
template <CppHeapPointerTag lower_bound, CppHeapPointerTag upper_bound>
V8_INLINE Address ReadCppHeapPointerField(
    Address field_address, IsolateForPointerCompression isolate) {
  CppHeapPointerSlot slot(field_address);
  CppHeapPointerTagRange tag_range(lower_bound, upper_bound);
#ifdef V8_COMPRESS_POINTERS
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  CppHeapPointerHandle handle = slot.Relaxed_LoadHandle();
  return isolate.GetCppHeapPointerTable().Get(handle, tag_range);
#else   // !V8_COMPRESS_POINTERS
  return slot.try_load(isolate, tag_range);
#endif  // !V8_COMPRESS_POINTERS
}

V8_INLINE Address ReadCppHeapPointerField(Address field_address,
                                          IsolateForPointerCompression isolate,
                                          CppHeapPointerTagRange tag_range) {
  CppHeapPointerSlot slot(field_address);
#ifdef V8_COMPRESS_POINTERS
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  CppHeapPointerHandle handle = slot.Relaxed_LoadHandle();
  return isolate.GetCppHeapPointerTable().Get(handle, tag_range);
#else   // !V8_COMPRESS_POINTERS
  return slot.try_load(isolate, tag_range);
#endif  // !V8_COMPRESS_POINTERS
}

template <CppHeapPointerTag tag>
V8_INLINE void WriteLazilyInitializedCppHeapPointerField(
    Address field_address, IsolateForPointerCompression isolate,
    Address value) {
  CppHeapPointerSlot slot(field_address);
#ifdef V8_COMPRESS_POINTERS
  static_assert(tag != CppHeapPointerTag::kNullTag);
  // See comment above for why this uses a Relaxed_Load and Release_Store.
  CppHeapPointerTable& table = isolate.GetCppHeapPointerTable();
  const CppHeapPointerHandle handle = slot.Relaxed_LoadHandle();
  if (handle == kNullCppHeapPointerHandle) {
    // Field has not been initialized yet.
    const CppHeapPointerHandle new_handle = table.AllocateAndInitializeEntry(
        isolate.GetCppHeapPointerTableSpace(), value, tag);
    slot.Release_StoreHandle(new_handle);
  } else {
    table.Set(handle, value, tag);
  }
#else   // !V8_COMPRESS_POINTERS
  slot.store(isolate, value, tag);
#endif  // !V8_COMPRESS_POINTERS
}

V8_INLINE void WriteLazilyInitializedCppHeapPointerField(
    Address field_address, IsolateForPointerCompression isolate, Address value,
    CppHeapPointerTag tag) {
  CppHeapPointerSlot slot(field_address);
#ifdef V8_COMPRESS_POINTERS
  DCHECK_NE(tag, CppHeapPointerTag::kNullTag);
  // See comment above for why this uses a Relaxed_Load and Release_Store.
  CppHeapPointerTable& table = isolate.GetCppHeapPointerTable();
  const CppHeapPointerHandle handle = slot.Relaxed_LoadHandle();
  if (handle == kNullCppHeapPointerHandle) {
    // Field has not been initialized yet.
    const CppHeapPointerHandle new_handle = table.AllocateAndInitializeEntry(
        isolate.GetCppHeapPointerTableSpace(), value, tag);
    slot.Release_StoreHandle(new_handle);
  } else {
    table.Set(handle, value, tag);
  }
#else   // !V8_COMPRESS_POINTERS
  slot.store(isolate, value, tag);
#endif  // !V8_COMPRESS_POINTERS
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CPPHEAP_POINTER_INL_H_
