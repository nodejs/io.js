// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_CODE_POINTER_INL_H_
#define V8_SANDBOX_CODE_POINTER_INL_H_

#include "include/v8-internal.h"
#include "src/base/atomic-utils.h"
#include "src/execution/isolate.h"
#include "src/sandbox/code-pointer-table-inl.h"
#include "src/sandbox/code-pointer.h"

namespace v8 {
namespace internal {

V8_INLINE Address ReadCodeEntrypointViaCodePointerField(Address field_address,
                                                        CodeEntrypointTag tag) {
#ifdef V8_ENABLE_SANDBOX
  // Handles may be written to objects from other threads so the handle needs
  // to be loaded atomically. We assume that the load from the table cannot
  // be reordered before the load of the handle due to the data dependency
  // between the two loads and therefore use relaxed memory ordering, but
  // technically we should use memory_order_consume here.
  auto location = reinterpret_cast<CodePointerHandle*>(field_address);
  CodePointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  return GetProcessWideCodePointerTable()->GetEntrypoint(handle, tag);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

V8_INLINE void WriteCodeEntrypointViaCodePointerField(Address field_address,
                                                      Address value,
                                                      CodeEntrypointTag tag) {
#ifdef V8_ENABLE_SANDBOX
  // See comment above for why this is a Relaxed_Load.
  auto location = reinterpret_cast<CodePointerHandle*>(field_address);
  CodePointerHandle handle = base::AsAtomic32::Relaxed_Load(location);
  GetProcessWideCodePointerTable()->SetEntrypoint(handle, value, tag);
#else
  UNREACHABLE();
#endif  // V8_ENABLE_SANDBOX
}

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_CODE_POINTER_INL_H_
