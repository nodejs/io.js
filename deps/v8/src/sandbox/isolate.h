// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_ISOLATE_H_
#define V8_SANDBOX_ISOLATE_H_

#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/cppheap-pointer-table.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/indirect-pointer-tag.h"
#include "src/sandbox/js-dispatch-table.h"
#include "src/sandbox/trusted-pointer-table.h"

namespace v8 {
namespace internal {

class Isolate;
class TrustedPointerPublishingScope;

// A reference to an Isolate that only exposes the sandbox-related parts of an
// isolate, in particular the various pointer tables. Can be used off-thread
// and implicitly constructed from both an Isolate* and a LocalIsolate*.
class V8_EXPORT_PRIVATE IsolateForSandbox final {
 public:
  template <typename IsolateT>
  IsolateForSandbox(IsolateT* isolate);  // NOLINT(runtime/explicit)

#ifndef V8_ENABLE_SANDBOX
  IsolateForSandbox() {}
#endif

#ifdef V8_ENABLE_SANDBOX
  inline ExternalPointerTable& GetExternalPointerTableFor(
      ExternalPointerTagRange tag_range);
  inline ExternalPointerTable::Space* GetExternalPointerTableSpaceFor(
      ExternalPointerTagRange tag_range, Address host);

  inline CodePointerTable::Space* GetCodePointerTableSpaceFor(
      Address owning_slot);

  inline TrustedPointerTable& GetTrustedPointerTableFor(IndirectPointerTag tag);
  inline TrustedPointerTable::Space* GetTrustedPointerTableSpaceFor(
      IndirectPointerTag tag);

  // Object is needed as a witness that this handle does not come from the
  // shared space.
  inline ExternalPointerTag GetExternalPointerTableTagFor(
      Tagged<HeapObject> witness, ExternalPointerHandle handle);

  // There can be only one publishing scope at a time.
  inline TrustedPointerPublishingScope* GetTrustedPointerPublishingScope();
  inline void SetTrustedPointerPublishingScope(
      TrustedPointerPublishingScope* scope);
#endif  // V8_ENABLE_SANDBOX

 private:
#ifdef V8_ENABLE_SANDBOX
  Isolate* const isolate_;
#endif  // V8_ENABLE_SANDBOX
};

class V8_EXPORT_PRIVATE IsolateForPointerCompression final {
 public:
  template <typename IsolateT>
  IsolateForPointerCompression(IsolateT* isolate);  // NOLINT(runtime/explicit)

#ifdef V8_COMPRESS_POINTERS
  inline ExternalPointerTable& GetExternalPointerTableFor(
      ExternalPointerTagRange tag_range);
  inline ExternalPointerTable::Space* GetExternalPointerTableSpaceFor(
      ExternalPointerTagRange tag_range, Address host);

  inline CppHeapPointerTable& GetCppHeapPointerTable();
  inline CppHeapPointerTable::Space* GetCppHeapPointerTableSpace();
#endif  // V8_COMPRESS_POINTERS

 private:
#ifdef V8_COMPRESS_POINTERS
  Isolate* const isolate_;
#endif  // V8_COMPRESS_POINTERS
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_ISOLATE_H_
