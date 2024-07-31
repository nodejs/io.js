// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_ISOLATE_H_
#define V8_SANDBOX_ISOLATE_H_

#include "src/sandbox/code-pointer-table.h"
#include "src/sandbox/external-pointer-table.h"
#include "src/sandbox/trusted-pointer-table.h"

namespace v8 {
namespace internal {

class Isolate;

// A reference to an Isolate that only exposes the sandbox-related parts of an
// isolate, in particular the various pointer tables. Can be used off-thread
// and implicitly constructed from both an Isolate* and a LocalIsolate*.
class V8_EXPORT_PRIVATE IsolateForSandbox final {
 public:
  template <typename IsolateT>
  IsolateForSandbox(IsolateT* isolate);  // NOLINT(runtime/explicit)

#ifdef V8_ENABLE_SANDBOX
  inline ExternalPointerTable& GetExternalPointerTableFor(
      ExternalPointerTag tag);
  inline ExternalPointerTable::Space* GetExternalPointerTableSpaceFor(
      ExternalPointerTag tag, Address owning_slot);

  inline CodePointerTable::Space* GetCodePointerTableSpaceFor(
      Address owning_slot);

  inline TrustedPointerTable& GetTrustedPointerTable();
  inline TrustedPointerTable::Space* GetTrustedPointerTableSpace();
#endif  // V8_ENABLE_SANDBOX

 private:
#ifdef V8_ENABLE_SANDBOX
  Isolate* const isolate_;
#endif  // V8_ENABLE_SANDBOX
};

}  // namespace internal
}  // namespace v8

#endif  // V8_SANDBOX_ISOLATE_H_
