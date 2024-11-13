// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_EXTERNAL_POINTER_H_
#define V8_SANDBOX_EXTERNAL_POINTER_H_

#include "src/common/globals.h"
#include "src/sandbox/isolate.h"

namespace v8 {
namespace internal {

template <ExternalPointerTag tag>
class ExternalPointerMember {
 public:
  ExternalPointerMember() = default;

  void Init(Address host_address, IsolateForSandbox isolate, Address value);

  inline Address load(const IsolateForSandbox isolate) const;
  inline void store(IsolateForSandbox isolate, Address value);

  inline ExternalPointer_t load_encoded() const;
  inline void store_encoded(ExternalPointer_t value);

  Address storage_address() { return reinterpret_cast<Address>(storage_); }

 private:
  alignas(alignof(Tagged_t)) char storage_[sizeof(ExternalPointer_t)];
};

// Creates and initializes an entry in the external pointer table and writes the
// handle for that entry to the field.
template <ExternalPointerTag tag>
V8_INLINE void InitExternalPointerField(Address host_address,
                                        Address field_address,
                                        IsolateForSandbox isolate,
                                        Address value);

// If the sandbox is enabled: reads the ExternalPointerHandle from the field and
// loads the corresponding external pointer from the external pointer table. If
// the sandbox is disabled: load the external pointer from the field.
//
// This can be used for both regular and lazily-initialized external pointer
// fields since lazily-initialized field will initially contain
// kNullExternalPointerHandle, which is guaranteed to result in kNullAddress
// being returned from the external pointer table.
template <ExternalPointerTag tag>
V8_INLINE Address ReadExternalPointerField(Address field_address,
                                           IsolateForSandbox isolate);

// If the sandbox is enabled: reads the ExternalPointerHandle from the field and
// stores the external pointer to the corresponding entry in the external
// pointer table. If the sandbox is disabled: stores the external pointer to the
// field.
template <ExternalPointerTag tag>
V8_INLINE void WriteExternalPointerField(Address field_address,
                                         IsolateForSandbox isolate,
                                         Address value);

// Set up a lazily-initialized external pointer field. If the sandbox is
// enabled, this will set the field to the kNullExternalPointerHandle. It will
// *not* allocate an entry in the external pointer table. That will only happen
// on the first call to WriteLazilyInitializedExternalPointerField. If the
// sandbox is disabled, this is equivalent to InitExternalPointerField with a
// nullptr value.
V8_INLINE void SetupLazilyInitializedExternalPointerField(
    Address field_address);

// Writes and possibly initializes a lazily-initialized external pointer field.
// When the sandbox is enabled, a lazily initialized external pointer field
// initially contains the kNullExternalPointerHandle and will only be properly
// initialized (i.e. allocate an entry in the external pointer table) once a
// value is written into it for the first time.
// If the sandbox is disabled, this is equivalent to WriteExternalPointerField.
template <ExternalPointerTag tag>
V8_INLINE void WriteLazilyInitializedExternalPointerField(
    Address host_address, Address field_address, IsolateForSandbox isolate,
    Address value);

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_EXTERNAL_POINTER_H_
