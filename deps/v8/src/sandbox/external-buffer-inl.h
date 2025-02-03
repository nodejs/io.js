// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_BUFFER_INL_H_
#define V8_SANDBOX_EXTERNAL_BUFFER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/objects/slots-inl.h"
#include "src/sandbox/external-buffer-table-inl.h"
#include "src/sandbox/external-buffer.h"
#include "src/sandbox/isolate-inl.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

template <ExternalBufferTag tag>
inline void ExternalBufferMember<tag>::Init(Address host_address,
                                            IsolateForSandbox isolate,
                                            std::pair<Address, size_t> value) {
  InitExternalBufferField<tag>(
      host_address, reinterpret_cast<Address>(storage_), isolate, value);
}

template <ExternalBufferTag tag>
inline std::pair<Address, size_t> ExternalBufferMember<tag>::load(
    const IsolateForSandbox isolate) const {
  return ReadExternalBufferField<tag>(reinterpret_cast<Address>(storage_),
                                      isolate);
}

template <ExternalBufferTag tag>
V8_INLINE void InitExternalBufferField(Address host_address,
                                       Address field_address,
                                       IsolateForSandbox isolate,
                                       std::pair<Address, size_t> value) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalBufferNullTag);
  ExternalBufferTable& table = isolate.GetExternalBufferTableFor(tag);
  ExternalBufferHandle handle = table.AllocateAndInitializeEntry(
      isolate.GetExternalBufferTableSpaceFor(tag, host_address), value, tag);
  // Use a Release_Store to ensure that the store of the pointer into the
  // table is not reordered after the store of the handle. Otherwise, other
  // threads may access an uninitialized table entry and crash.
  auto location = reinterpret_cast<ExternalBufferHandle*>(field_address);
  base::AsAtomic32::Release_Store(location, handle);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

template <ExternalBufferTag tag>
V8_INLINE std::pair<Address, size_t> ReadExternalBufferField(
    Address field_address, IsolateForSandbox isolate) {
#ifdef V8_ENABLE_SANDBOX
  static_assert(tag != kExternalBufferNullTag);
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  auto location = reinterpret_cast<ExternalBufferHandle*>(field_address);
  ExternalBufferHandle handle = base::AsAtomic32::Relaxed_Load(location);
  return isolate.GetExternalBufferTableFor(tag).Get(handle, tag);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_BUFFER_INL_H_
