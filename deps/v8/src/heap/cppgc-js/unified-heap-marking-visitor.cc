// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc-js/unified-heap-marking-visitor.h"

#include "src/heap/cppgc-js/unified-heap-marking-state-inl.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/marking-state.h"
#include "src/heap/cppgc/visitor.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"
#include "src/heap/minor-mark-sweep.h"

namespace v8 {
namespace internal {

struct Dummy;

namespace {
std::unique_ptr<MarkingWorklists::Local> GetV8MarkingWorklists(
    Heap* heap, cppgc::internal::CollectionType collection_type) {
  if (!heap) return {};
  auto* worklist =
      (collection_type == cppgc::internal::CollectionType::kMajor)
          ? heap->mark_compact_collector()->marking_worklists()
          : heap->minor_mark_sweep_collector()->marking_worklists();
  return std::make_unique<MarkingWorklists::Local>(worklist);
}
}  // namespace

UnifiedHeapMarkingVisitorBase::UnifiedHeapMarkingVisitorBase(
    HeapBase& heap, cppgc::internal::BasicMarkingState& marking_state,
    UnifiedHeapMarkingState& unified_heap_marking_state)
    : JSVisitor(cppgc::internal::VisitorFactory::CreateKey()),
      marking_state_(marking_state),
      unified_heap_marking_state_(unified_heap_marking_state) {}

void UnifiedHeapMarkingVisitorBase::Visit(const void* object,
                                          TraceDescriptor desc) {
  marking_state_.MarkAndPush(object, desc);
}

void UnifiedHeapMarkingVisitorBase::VisitMultipleUncompressedMember(
    const void* start, size_t len,
    TraceDescriptorCallback get_trace_descriptor) {
  const char* it = static_cast<const char*>(start);
  const char* end = it + len * cppgc::internal::kSizeOfUncompressedMember;
  for (; it < end; it += cppgc::internal::kSizeOfUncompressedMember) {
    const auto* current =
        reinterpret_cast<const cppgc::internal::RawPointer*>(it);
    const void* object = current->LoadAtomic();
    if (!object) continue;

    marking_state_.MarkAndPush(object, get_trace_descriptor(object));
  }
}

#if defined(CPPGC_POINTER_COMPRESSION)

void UnifiedHeapMarkingVisitorBase::VisitMultipleCompressedMember(
    const void* start, size_t len,
    TraceDescriptorCallback get_trace_descriptor) {
  const char* it = static_cast<const char*>(start);
  const char* end = it + len * cppgc::internal::kSizeofCompressedMember;
  for (; it < end; it += cppgc::internal::kSizeofCompressedMember) {
    const auto* current =
        reinterpret_cast<const cppgc::internal::CompressedPointer*>(it);
    const void* object = current->LoadAtomic();
    if (!object) continue;

    marking_state_.MarkAndPush(object, get_trace_descriptor(object));
  }
}

#endif  // defined(CPPGC_POINTER_COMPRESSION)

void UnifiedHeapMarkingVisitorBase::VisitWeak(const void* object,
                                              TraceDescriptor desc,
                                              WeakCallback weak_callback,
                                              const void* weak_member) {
  marking_state_.RegisterWeakReferenceIfNeeded(object, desc, weak_callback,
                                               weak_member);
}

void UnifiedHeapMarkingVisitorBase::VisitEphemeron(const void* key,
                                                   const void* value,
                                                   TraceDescriptor value_desc) {
  marking_state_.ProcessEphemeron(key, value, value_desc, *this);
}

void UnifiedHeapMarkingVisitorBase::VisitWeakContainer(
    const void* self, TraceDescriptor strong_desc, TraceDescriptor weak_desc,
    WeakCallback callback, const void* data) {
  marking_state_.ProcessWeakContainer(self, weak_desc, callback, data);
}

void UnifiedHeapMarkingVisitorBase::RegisterWeakCallback(WeakCallback callback,
                                                         const void* object) {
  marking_state_.RegisterWeakCustomCallback(callback, object);
}

void UnifiedHeapMarkingVisitorBase::HandleMovableReference(const void** slot) {
  marking_state_.RegisterMovableReference(slot);
}

void UnifiedHeapMarkingVisitorBase::Visit(const TracedReferenceBase& ref) {
  unified_heap_marking_state_.MarkAndPush(ref);
}

MutatorUnifiedHeapMarkingVisitor::MutatorUnifiedHeapMarkingVisitor(
    HeapBase& heap, MutatorMarkingState& marking_state,
    UnifiedHeapMarkingState& unified_heap_marking_state)
    : UnifiedHeapMarkingVisitorBase(heap, marking_state,
                                    unified_heap_marking_state) {}

ConcurrentUnifiedHeapMarkingVisitor::ConcurrentUnifiedHeapMarkingVisitor(
    HeapBase& heap, Heap* v8_heap,
    cppgc::internal::ConcurrentMarkingState& marking_state,
    CppHeap::CollectionType collection_type)
    : UnifiedHeapMarkingVisitorBase(heap, marking_state,
                                    concurrent_unified_heap_marking_state_),
      local_marking_worklist_(GetV8MarkingWorklists(v8_heap, collection_type)),
      concurrent_unified_heap_marking_state_(
          v8_heap, local_marking_worklist_.get(), collection_type) {}

ConcurrentUnifiedHeapMarkingVisitor::~ConcurrentUnifiedHeapMarkingVisitor() {
  if (local_marking_worklist_) {
    local_marking_worklist_->Publish();
  }
}

bool ConcurrentUnifiedHeapMarkingVisitor::DeferTraceToMutatorThreadIfConcurrent(
    const void* parameter, cppgc::TraceCallback callback,
    size_t deferred_size) {
  marking_state_.concurrent_marking_bailout_worklist().Push(
      {parameter, callback, deferred_size});
  static_cast<cppgc::internal::ConcurrentMarkingState&>(marking_state_)
      .AccountDeferredMarkedBytes(
          cppgc::internal::BasePage::FromPayload(const_cast<void*>(parameter)),
          deferred_size);
  return true;
}

}  // namespace internal
}  // namespace v8
