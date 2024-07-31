// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VERIFIER_H_
#define V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VERIFIER_H_

#include "src/heap/cppgc/marking-verifier.h"
#include "v8-traced-handle.h"

namespace v8 {
namespace internal {

class UnifiedHeapVerificationState : public cppgc::internal::VerificationState {
 public:
  void VerifyMarkedTracedReference(const TracedReferenceBase& ref) const;
};

class V8_EXPORT_PRIVATE UnifiedHeapMarkingVerifier final
    : public cppgc::internal::MarkingVerifierBase {
 public:
  UnifiedHeapMarkingVerifier(cppgc::internal::HeapBase&,
                             cppgc::internal::CollectionType);
  ~UnifiedHeapMarkingVerifier() final = default;

 private:
  UnifiedHeapVerificationState state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VERIFIER_H_
