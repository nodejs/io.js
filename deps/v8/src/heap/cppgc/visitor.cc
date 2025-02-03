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
    // First, check the full pointer.
    if (maybe_full_ptr > SentinelPointer::kSentinelValue)
      this->TraceConservativelyIfNeeded(
          reinterpret_cast<Address>(maybe_full_ptr));
#if defined(CPPGC_POINTER_COMPRESSION)
    // Then, check for compressed pointers.
    auto decompressed_low = reinterpret_cast<Address>(
        CompressedPointer::Decompress(static_cast<uint32_t>(maybe_full_ptr)));
    if (decompressed_low >
        reinterpret_cast<void*>(SentinelPointer::kSentinelValue))
      this->TraceConservativelyIfNeeded(decompressed_low);
    auto decompressed_high = reinterpret_cast<Address>(
        CompressedPointer::Decompress(static_cast<uint32_t>(
            maybe_full_ptr >> (sizeof(uint32_t) * CHAR_BIT))));
    if (decompressed_high >
        reinterpret_cast<void*>(SentinelPointer::kSentinelValue))
      this->TraceConservativelyIfNeeded(decompressed_high);
#endif  // !defined(CPPGC_POINTER_COMPRESSION)
  }
}

void ConservativeTracingVisitor::TryTracePointerConservatively(
    Address address) {
#if defined(CPPGC_CAGED_HEAP)
  // TODO(chromium:1056170): Add support for SIMD in stack scanning.
  if (V8_LIKELY(!CagedHeapBase::IsWithinCage(address))) return;
#endif  // defined(CPPGC_CAGED_HEAP)

  const BasePage* page = reinterpret_cast<const BasePage*>(
      page_backend_.Lookup(const_cast<ConstAddress>(address)));

  if (!page) return;

  DCHECK_EQ(&heap_, &page->heap());

  auto* header = page->TryObjectHeaderFromInnerAddress(address);

  if (!header) return;

  TraceConservativelyIfNeeded(*header);
}

void ConservativeTracingVisitor::TraceConservativelyIfNeeded(
    const void* address) {
  auto pointer = reinterpret_cast<Address>(const_cast<void*>(address));
  TryTracePointerConservatively(pointer);
#if defined(CPPGC_POINTER_COMPRESSION)
  auto try_trace = [this](Address ptr) {
    if (ptr > reinterpret_cast<Address>(SentinelPointer::kSentinelValue))
      TryTracePointerConservatively(ptr);
  };
  // If pointer compression enabled, we may have random compressed pointers on
  // stack (e.g. due to inlined collections). Extract, decompress and trace both
  // halfwords.
  auto decompressed_low = static_cast<Address>(CompressedPointer::Decompress(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer))));
  try_trace(decompressed_low);
  auto decompressed_high = static_cast<Address>(CompressedPointer::Decompress(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer) >>
                            (sizeof(uint32_t) * CHAR_BIT))));
  try_trace(decompressed_high);
#if !defined(CPPGC_2GB_CAGE)
  // In addition, check half-compressed halfwords, since the compiler is free to
  // spill intermediate results of compression/decompression onto the stack.
  const uintptr_t base = CagedHeapBase::GetBase();
  DCHECK(base);
  auto intermediate_decompressed_low = reinterpret_cast<Address>(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)) | base);
  try_trace(intermediate_decompressed_low);
  auto intermediate_decompressed_high = reinterpret_cast<Address>(
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer) >>
                            (sizeof(uint32_t) * CHAR_BIT)) |
      base);
  try_trace(intermediate_decompressed_high);
#endif  // !defined(CPPGC_2GB_CAGE)
#endif  // defined(CPPGC_POINTER_COMPRESSION)
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
