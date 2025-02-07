// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/unified-heap-marking-verifier.h"

#include <memory>

#include "include/cppgc/internal/name-trait.h"
#include "include/v8-cppgc.h"
#include "src/handles/traced-handles.h"
#include "src/heap/cppgc-js/unified-heap-marking-state-inl.h"
#include "src/heap/cppgc/marking-verifier.h"

namespace v8 {
namespace internal {

namespace {

class UnifiedHeapVerificationVisitor final : public JSVisitor {
 public:
  explicit UnifiedHeapVerificationVisitor(UnifiedHeapVerificationState& state)
      : JSVisitor(cppgc::internal::VisitorFactory::CreateKey()),
        state_(state) {}

  void Visit(const void*, cppgc::TraceDescriptor desc) final {
    state_.VerifyMarked(desc.base_object_payload);
  }

  void VisitWeak(const void*, cppgc::TraceDescriptor desc, cppgc::WeakCallback,
                 const void*) final {
    // Weak objects should have been cleared at this point. As a consequence,
    // all objects found through weak references have to point to live objects
    // at this point.
    state_.VerifyMarked(desc.base_object_payload);
  }

  void VisitWeakContainer(const void* object, cppgc::TraceDescriptor,
                          cppgc::TraceDescriptor weak_desc, cppgc::WeakCallback,
                          const void*) final {
    if (!object) return;

    // Contents of weak containers are found themselves through page iteration
    // and are treated strongly, similar to how they are treated strongly when
    // found through stack scanning. The verification here only makes sure that
    // the container itself is properly marked.
    state_.VerifyMarked(weak_desc.base_object_payload);
  }

  void Visit(const TracedReferenceBase& ref) final {
    state_.VerifyMarkedTracedReference(ref);
  }

 private:
  UnifiedHeapVerificationState& state_;
};

}  // namespace

void UnifiedHeapVerificationState::VerifyMarkedTracedReference(
    const TracedReferenceBase& ref) const {
  // The following code will crash with null pointer derefs when finding a
  // non-empty `TracedReferenceBase` when `CppHeap` is in detached mode.
  Address* traced_handle_location =
      BasicTracedReferenceExtractor::GetObjectSlotForMarking(ref);
  // We cannot assume that the reference is non-null as we may get here by
  // tracing an ephemeron which doesn't have early bailouts, see
  // `cppgc::Visitor::TraceEphemeron()` for non-Member values.
  if (!traced_handle_location) {
    return;
  }
  // Verification runs after unamrked nodes are freed. The node for this
  // TracedReference should still be marked as in use.
  if (!TracedHandles::IsValidInUseNode(traced_handle_location)) {
    FATAL(
        "MarkingVerifier: Encountered unmarked TracedReference.\n"
        "#\n"
        "# Hint:\n"
        "#   %s (%p)\n"
        "#     \\-> TracedReference (%p)",
        parent_
            ? parent_
                  ->GetName(cppgc::internal::HeapObjectNameForUnnamedObject::
                                kUseClassNameIfSupported)
                  .value
            : "Stack",
        parent_ ? parent_->ObjectStart() : nullptr, &ref);
  }
}

UnifiedHeapMarkingVerifier::UnifiedHeapMarkingVerifier(
    cppgc::internal::HeapBase& heap_base,
    cppgc::internal::CollectionType collection_type)
    : MarkingVerifierBase(
          heap_base, collection_type, state_,
          std::make_unique<UnifiedHeapVerificationVisitor>(state_)) {}

}  // namespace internal
}  // namespace v8
