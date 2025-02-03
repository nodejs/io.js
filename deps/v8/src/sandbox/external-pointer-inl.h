// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_INL_H_
#define V8_SANDBOX_EXTERNAL_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/objects/slots-inl.h"
#include "src/sandbox/external-buffer-table-inl.h"
#include "src/sandbox/external-pointer-table-inl.h"
#include "src/sandbox/external-pointer.h"
#include "src/sandbox/isolate-inl.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

template <ExternalPointerTag tag>
inline void ExternalPointerMember<tag>::Init(Address host_address,
                                             IsolateForSandbox isolate,
                                             Address value) {
  InitExternalPointerField<tag>(
      host_address, reinterpret_cast<Address>(storage_), isolate, value);
}

template <ExternalPointerTag tag>
inline Address ExternalPointerMember<tag>::load(
    const IsolateForSandbox isolate) const {
  return ReadExternalPointerField<tag>(reinterpret_cast<Address>(storage_),
                                       isolate);
}

template <ExternalPointerTag tag>
inline void ExternalPointerMember<tag>::store(IsolateForSandbox isolate,
                                              Address value) {
  WriteExternalPointerField<tag>(reinterpret_cast<Address>(storage_), isolate,
                                 value);
}

template <ExternalPointerTag tag>
inline ExternalPointer_t ExternalPointerMember<tag>::load_encoded() const {
  return base::bit_cast<ExternalPointer_t>(storage_);
}

template <ExternalPointerTag tag>
inline void ExternalPointerMember<tag>::store_encoded(ExternalPointer_t value) {
  memcpy(storage_, &value, sizeof(ExternalPointer_t));
}

template <ExternalPointerTag tag>
V8_INLINE void InitExternalPointerField(Address host_address,
                                        Address field_address,
                                        IsolateForSandbox isolate,
                                        Address value) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalPointerNullTag);
  ExternalPointerTable& table = isolate.GetExternalPointerTableFor(tag);
  ExternalPointerHandle handle = table.AllocateAndInitializeEntry(
      isolate.GetExternalPointerTableSpaceFor(tag, host_address), value, tag);
  // Use a Release_Store to ensure that the store of the pointer into the
  // table is not reordered after the store of the handle. Otherwise, other
  // threads may access an uninitialized table entry and crash.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, handle);
#else
  WriteExternalPointerField<tag>(field_address, isolate, value);
#endif  // V8_ENABLE_SANDBOX
}

template <ExternalPointerTag tag>
V8_INLINE Address ReadExternalPointerField(Address field_address,
                                           IsolateForSandbox isolate) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalPointerNullTag);
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  return isolate.GetExternalPointerTableFor(tag).Get(handle, tag);
#else
  return ReadMaybeUnalignedValue<Address>(field_address);
#endif  // V8_ENABLE_SANDBOX
}

template <ExternalPointerTag tag>
V8_INLINE void WriteExternalPointerField(Address field_address,
                                         IsolateForSandbox isolate,
                                         Address value) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalPointerNullTag);
  // See comment above for why this is a Relaxed_Load.
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  isolate.GetExternalPointerTableFor(tag).Set(handle, value, tag);
#else
  WriteMaybeUnalignedValue<Address>(field_address, value);
#endif  // V8_ENABLE_SANDBOX
}

V8_INLINE void SetupLazilyInitializedExternalPointerField(
    Address field_address) {
#ifdef V8_ENABLE_SANDBOX
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, kNullExternalPointerHandle);
#else
  WriteMaybeUnalignedValue<Address>(field_address, kNullAddress);
#endif  // V8_ENABLE_SANDBOX
}

template <ExternalPointerTag tag>
V8_INLINE void WriteLazilyInitializedExternalPointerField(
    Address host_address, Address field_address, IsolateForSandbox isolate,
    Address value) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalPointerNullTag);
  // See comment above for why this uses a Relaxed_Load and Release_Store.
  ExternalPointerTable& table = isolate.GetExternalPointerTableFor(tag);
  auto location = reinterpret_cast<ExternalPointerHandle*>(field_address);
  ExternalPointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  if (handle == kNullExternalPointerHandle) {
    // Field has not been initialized yet.
    ExternalPointerHandle handle = table.AllocateAndInitializeEntry(
        isolate.GetExternalPointerTableSpaceFor(tag, host_address), value, tag);
    base::AsAtomic32::Release_Store(location, handle);
  } else {
    table.Set(handle, value, tag);
  }
#else
  WriteMaybeUnalignedValue<Address>(field_address, value);
#endif  // V8_ENABLE_SANDBOX
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_POINTER_INL_H_
