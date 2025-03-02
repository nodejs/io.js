// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/visitor.h"

#include "src/base/sanitizer/asan.h"
#include "src/base/sanitizer/msan.h"
#include "src/heap/cppgc/gc-info-table.h"
#include "src/heap/cppgc/heap-base.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-page.h"
#include "src/heap/cppgc/object-view.h"
#include "src/heap/cppgc/page-memory.h"

#if defined(CPPGC_CAGED_HEAP)
#include "src/heap/cppgc/caged-heap.h"
#endif  // defined(CPPGC_CAGED_HEAP)

namespace cppgc {

#ifdef V8_ENABLE_CHECKS
void Visitor::CheckObjectNotInConstruction(const void* address) {
  // TODO(chromium:1056170): |address| is an inner pointer of an object. Check
  // that the object is not in construction.
}
#endif  // V8_ENABLE_CHECKS

namespace internal {

#if defined(CPPGC_POINTER_COMPRESSION)
// CompressedPointer is compatible with RawPointer and will find full pointers
// as well.
using PointerRepresentation = CompressedPointer;
#else
using PointerRepresentation = RawPointer;
#endif  // defined(CPPGC_POINTER_COMPRESSION)

ConservativeTracingVisitor::ConservativeTracingVisitor(
    HeapBase& heap, PageBackend& page_backend, cppgc::Visitor& visitor)
    : heap_(heap), page_backend_(page_backend), visitor_(visitor) {}

// Conservative scanning of objects is not compatible with ASAN as we may scan
// over objects reading poisoned memory. One such example was added to libc++
// (June 2024) in the form of container annotations for short std::string.
DISABLE_ASAN
void ConservativeTracingVisitor::TraceConservatively(
    const HeapObjectHeader& header) {
  const auto object_view = ObjectView<>(header);
  uintptr_t* word = reinterpret_cast<uintptr_t*>(object_view.Start());
  for (size_t i = 0; i < (object_view.Size() / sizeof(uintptr_t)); ++i) {
    uintptr_t maybe_full_ptr = word[i];
    // |object| may be uninitialized by design or just contain padding bytes.
    // Copy into a local variable that is not poisoned for conservative marking.
    // Copy into a temporary variable to maintain the original MSAN state.
    MSAN_MEMORY_IS_INITIALIZED(&maybe_full_ptr, sizeof(maybe_full_ptr));
    // Neither first OS page, nor first cage page contain Oilpan objects.
    if (maybe_full_ptr <= SentinelPointer::kSentinelValue) {
      continue;
    }
    PointerRepresentation::VisitPossiblePointers(
        reinterpret_cast<void*>(maybe_full_ptr),
        [this](const void* raw_pointer) {
          this->TraceConservativelyIfNeeded(raw_pointer);
        });
    // We must also trace full pointers here as the conservative tracing visitor
    // may be overridden to find pointers to other areas conservatively as well.
    // E.g., v8::TracedReference points into a different memory region and is
    // scanned conservatively when the GCed object is in construction. See
    // `UnifiedHeapConservativeMarkingVisitor::TraceConservativelyIfNeeded()`.
    this->TraceConservativelyIfNeeded(reinterpret_cast<void*>(maybe_full_ptr));
  }
}

void ConservativeTracingVisitor::TryTracePointerConservatively(
    ConstAddress address) {
#if defined(CPPGC_CAGED_HEAP)
  // TODO(chromium:1056170): Add support for SIMD in stack scanning.
  if (V8_LIKELY(!CagedHeapBase::IsWithinCage(address))) return;
#endif  // defined(CPPGC_CAGED_HEAP)

  const BasePage* page =
      reinterpret_cast<const BasePage*>(page_backend_.Lookup(address));
  if (!page) {
    return;
  }
  DCHECK_EQ(&heap_, &page->heap());
  auto* header = const_cast<HeapObjectHeader*>(
      page->TryObjectHeaderFromInnerAddress(address));
  if (!header) {
    return;
  }
  TraceConservativelyIfNeeded(*header);
}

void ConservativeTracingVisitor::TraceConservativelyIfNeeded(
    const void* address) {
  // Neither first OS page, nor first cage page contain Oilpan objects.
  if (reinterpret_cast<ConstAddress>(address) <=
      reinterpret_cast<ConstAddress>(SentinelPointer::kSentinelValue)) {
    return;
  }
  PointerRepresentation::VisitPossiblePointers(
      address, [this](const void* raw_pointer) {
        this->TryTracePointerConservatively(
            reinterpret_cast<ConstAddress>(raw_pointer));
      });
}

void ConservativeTracingVisitor::TraceConservativelyIfNeeded(
    HeapObjectHeader& header) {
  if (!header.IsInConstruction<AccessMode::kNonAtomic>()) {
    VisitFullyConstructedConservatively(header);
  } else {
    VisitInConstructionConservatively(
        header,
        [](ConservativeTracingVisitor* v, const HeapObjectHeader& header) {
          v->TraceConservatively(header);
        });
  }
}

void ConservativeTracingVisitor::VisitFullyConstructedConservatively(
    HeapObjectHeader& header) {
  visitor_.Visit(
      header.ObjectStart(),
      {header.ObjectStart(),
       GlobalGCInfoTable::GCInfoFromIndex(header.GetGCInfoIndex()).trace});
}

}  // namespace internal
}  // namespace cppgc
