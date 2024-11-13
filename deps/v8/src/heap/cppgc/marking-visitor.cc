// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/marking-visitor.h"

#include "include/cppgc/internal/member-storage.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marking-state.h"

namespace cppgc {
namespace internal {

struct Dummy;

MarkingVisitorBase::MarkingVisitorBase(HeapBase& heap,
                                       BasicMarkingState& marking_state)
    : marking_state_(marking_state) {}

void MarkingVisitorBase::Visit(const void* object, TraceDescriptor desc) {
  marking_state_.MarkAndPush(object, desc);
}

void MarkingVisitorBase::VisitMultipleUncompressedMember(
    const void* start, size_t len,
    TraceDescriptorCallback get_trace_descriptor) {
  const char* it = static_cast<const char*>(start);
  const char* end = it + len * cppgc::internal::kSizeOfUncompressedMember;
  for (; it < end; it += cppgc::internal::kSizeOfUncompressedMember) {
    const auto* current = reinterpret_cast<const internal::RawPointer*>(it);
    const void* object = current->LoadAtomic();
    if (!object) continue;

    marking_state_.MarkAndPush(object, get_trace_descriptor(object));
  }
}

#if defined(CPPGC_POINTER_COMPRESSION)

void MarkingVisitorBase::VisitMultipleCompressedMember(
    const void* start, size_t len,
    TraceDescriptorCallback get_trace_descriptor) {
  const char* it = static_cast<const char*>(start);
  const char* end = it + len * cppgc::internal::kSizeofCompressedMember;
  for (; it < end; it += cppgc::internal::kSizeofCompressedMember) {
    const auto* current =
        reinterpret_cast<const internal::CompressedPointer*>(it);
    const void* object = current->LoadAtomic();
    if (!object) continue;

    marking_state_.MarkAndPush(object, get_trace_descriptor(object));
  }
}

#endif  // defined(CPPGC_POINTER_COMPRESSION)

void MarkingVisitorBase::VisitWeak(const void* object, TraceDescriptor desc,
                                   WeakCallback weak_callback,
                                   const void* weak_member) {
  marking_state_.RegisterWeakReferenceIfNeeded(object, desc, weak_callback,
                                               weak_member);
}

void MarkingVisitorBase::VisitEphemeron(const void* key, const void* value,
                                        TraceDescriptor value_desc) {
  marking_state_.ProcessEphemeron(key, value, value_desc, *this);
}

void MarkingVisitorBase::VisitWeakContainer(const void* object,
                                            TraceDescriptor strong_desc,
                                            TraceDescriptor weak_desc,
                                            WeakCallback callback,
                                            const void* data) {
  marking_state_.ProcessWeakContainer(object, weak_desc, callback, data);
}

void MarkingVisitorBase::RegisterWeakCallback(WeakCallback callback,
                                              const void* object) {
  marking_state_.RegisterWeakCustomCallback(callback, object);
}

void MarkingVisitorBase::HandleMovableReference(const void** slot) {
  marking_state_.RegisterMovableReference(slot);
}

ConservativeMarkingVisitor::ConservativeMarkingVisitor(
    HeapBase& heap, MutatorMarkingState& marking_state, cppgc::Visitor& visitor)
    : ConservativeTracingVisitor(heap, *heap.page_backend(), visitor),
      marking_state_(marking_state) {}

void ConservativeMarkingVisitor::VisitFullyConstructedConservatively(
    HeapObjectHeader& header) {
  if (header.IsMarked<AccessMode::kAtomic>()) {
    if (marking_state_.IsMarkedWeakContainer(header))
      marking_state_.ReTraceMarkedWeakContainer(visitor_, header);
    return;
  }
  ConservativeTracingVisitor::VisitFullyConstructedConservatively(header);
}

void ConservativeMarkingVisitor::VisitInConstructionConservatively(
    HeapObjectHeader& header, TraceConservativelyCallback callback) {
  DCHECK(!marking_state_.IsMarkedWeakContainer(header));
  // In construction objects found through conservative can be marked if they
  // hold a reference to themselves.
  if (!marking_state_.MarkNoPush(header)) return;
  marking_state_.AccountMarkedBytes(header);
#if defined(CPPGC_YOUNG_GENERATION)
  // An in-construction object can add a reference to a young object that may
  // miss the write-barrier on an initializing store. Remember object in the
  // root-set to be retraced on the next GC.
  if (heap_.generational_gc_supported()) {
    heap_.remembered_set().AddInConstructionObjectToBeRetraced(header);
  }
#endif  // defined(CPPGC_YOUNG_GENERATION)
  callback(this, header);
}

MutatorMarkingVisitor::MutatorMarkingVisitor(HeapBase& heap,
                                             MutatorMarkingState& marking_state)
    : MarkingVisitorBase(heap, marking_state) {}

RootMarkingVisitor::RootMarkingVisitor(MutatorMarkingState& marking_state)
    : mutator_marking_state_(marking_state) {}

void RootMarkingVisitor::VisitRoot(const void* object, TraceDescriptor desc,
                                   const SourceLocation&) {
  mutator_marking_state_.MarkAndPush(object, desc);
}

void RootMarkingVisitor::VisitWeakRoot(const void* object, TraceDescriptor desc,
                                       WeakCallback weak_callback,
                                       const void* weak_root,
                                       const SourceLocation&) {
  mutator_marking_state_.InvokeWeakRootsCallbackIfNeeded(
      object, desc, weak_callback, weak_root);
}

ConcurrentMarkingVisitor::ConcurrentMarkingVisitor(
    HeapBase& heap, ConcurrentMarkingState& marking_state)
    : MarkingVisitorBase(heap, marking_state) {}

void ConservativeMarkingVisitor::VisitPointer(const void* address) {
  TraceConservativelyIfNeeded(address);
}

bool ConcurrentMarkingVisitor::DeferTraceToMutatorThreadIfConcurrent(
    const void* parameter, TraceCallback callback, size_t deferred_size) {
  marking_state_.concurrent_marking_bailout_worklist().Push(
      {parameter, callback, deferred_size});
  static_cast<ConcurrentMarkingState&>(marking_state_)
      .AccountDeferredMarkedBytes(
          BasePage::FromPayload(const_cast<void*>(parameter)), deferred_size);
  return true;
}

}  // namespace internal
}  // namespace cppgc
