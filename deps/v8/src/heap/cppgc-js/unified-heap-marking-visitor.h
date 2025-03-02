// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VISITOR_H_
#define V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VISITOR_H_

#include "include/cppgc/trace-trait.h"
#include "include/v8-cppgc.h"
#include "src/base/macros.h"
#include "src/heap/cppgc-js/unified-heap-marking-state.h"
#include "src/heap/cppgc/marking-visitor.h"

namespace cppgc {

namespace internal {
class ConcurrentMarkingState;
class BasicMarkingState;
class MutatorMarkingState;
}  // namespace internal
}  // namespace cppgc

namespace v8 {

class SourceLocation;

namespace internal {

using cppgc::TraceDescriptor;
using cppgc::TraceDescriptorCallback;
using cppgc::WeakCallback;
using cppgc::internal::HeapBase;
using cppgc::internal::MutatorMarkingState;

class UnifiedHeapMarker;

class V8_EXPORT_PRIVATE UnifiedHeapMarkingVisitorBase : public JSVisitor {
 public:
  UnifiedHeapMarkingVisitorBase(HeapBase&, cppgc::internal::BasicMarkingState&,
                                UnifiedHeapMarkingState&);
  ~UnifiedHeapMarkingVisitorBase() override = default;

 protected:
  // C++ handling.
  void Visit(const void*, TraceDescriptor) final;
  void VisitMultipleUncompressedMember(const void*, size_t,
                                       TraceDescriptorCallback) final;
#if defined(CPPGC_POINTER_COMPRESSION)
  void VisitMultipleCompressedMember(const void*, size_t,
                                     TraceDescriptorCallback) final;
#endif  // defined(CPPGC_POINTER_COMPRESSION)
  void VisitWeak(const void*, TraceDescriptor, WeakCallback, const void*) final;
  void VisitEphemeron(const void*, const void*, TraceDescriptor) final;
  void VisitWeakContainer(const void* self, TraceDescriptor strong_desc,
                          TraceDescriptor weak_desc, WeakCallback callback,
                          const void* data) final;
  void RegisterWeakCallback(WeakCallback, const void*) final;
  void HandleMovableReference(const void**) final;

  // JS handling.
  void Visit(const TracedReferenceBase& ref) override;

  cppgc::internal::BasicMarkingState& marking_state_;
  UnifiedHeapMarkingState& unified_heap_marking_state_;

  friend class UnifiedHeapMarker;
};

class V8_EXPORT_PRIVATE MutatorUnifiedHeapMarkingVisitor
    : public UnifiedHeapMarkingVisitorBase {
 public:
  MutatorUnifiedHeapMarkingVisitor(HeapBase&, MutatorMarkingState&,
                                   UnifiedHeapMarkingState&);
  ~MutatorUnifiedHeapMarkingVisitor() override = default;
};

class V8_EXPORT_PRIVATE ConcurrentUnifiedHeapMarkingVisitor
    : public UnifiedHeapMarkingVisitorBase {
 public:
  ConcurrentUnifiedHeapMarkingVisitor(HeapBase&, Heap*,
                                      cppgc::internal::ConcurrentMarkingState&,
                                      CppHeap::CollectionType);
  ~ConcurrentUnifiedHeapMarkingVisitor() override;

 protected:
  bool DeferTraceToMutatorThreadIfConcurrent(const void*, cppgc::TraceCallback,
                                             size_t) final;

 private:
  // Visitor owns the local worklist. All remaining items are published on
  // destruction of the visitor. This is good enough as concurrent visitation
  // ends before computing the rest of the transitive closure on the main
  // thread. Dynamically allocated as it is only present when the heaps are
  // attached.
  std::unique_ptr<MarkingWorklists::Local> local_marking_worklist_;
  UnifiedHeapMarkingState concurrent_unified_heap_marking_state_;

#ifdef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
  IsolateGroup* saved_isolate_group_ = nullptr;
#endif
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_VISITOR_H_
