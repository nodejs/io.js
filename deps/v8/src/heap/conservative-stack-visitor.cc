// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"

#include "src/execution/isolate-inl.h"
#include "src/heap/marking-inl.h"
#include "src/heap/memory-chunk-metadata.h"
#include "src/heap/memory-chunk.h"
#include "src/objects/visitors.h"

#ifdef V8_COMPRESS_POINTERS
#include "src/common/ptr-compr-inl.h"
#endif  // V8_COMPRESS_POINTERS

namespace v8 {
namespace internal {

ConservativeStackVisitor::ConservativeStackVisitor(Isolate* isolate,
                                                   RootVisitor* delegate)
    : ConservativeStackVisitor(isolate, delegate, delegate->collector()) {}

ConservativeStackVisitor::ConservativeStackVisitor(Isolate* isolate,
                                                   RootVisitor* delegate,
                                                   GarbageCollector collector)
    : cage_base_(isolate),
#ifdef V8_EXTERNAL_CODE_SPACE
      code_cage_base_(isolate->code_cage_base()),
      code_address_region_(isolate->heap()->code_region()),
#endif
#ifdef V8_ENABLE_SANDBOX
      trusted_cage_base_(isolate->isolate_data()->trusted_cage_base_address()),
#endif
      delegate_(delegate),
      allocator_(isolate->heap()->memory_allocator()),
      collector_(collector) {
}

#ifdef V8_COMPRESS_POINTERS
bool ConservativeStackVisitor::IsInterestingCage(
    PtrComprCageBase cage_base) const {
  if (cage_base == cage_base_) return true;
#ifdef V8_EXTERNAL_CODE_SPACE
  if (cage_base == code_cage_base_) return true;
#endif
#ifdef V8_ENABLE_SANDBOX
  if (cage_base == trusted_cage_base_) return true;
#endif
  return false;
}
#endif  // V8_COMPRESS_POINTERS

Address ConservativeStackVisitor::FindBasePtr(
    Address maybe_inner_ptr, PtrComprCageBase cage_base) const {
#ifdef V8_COMPRESS_POINTERS
  DCHECK(IsInterestingCage(cage_base));
#endif  // V8_COMPRESS_POINTERS
  // Check if the pointer is contained by a normal or large page owned by this
  // heap. Bail out if it is not.
  const MemoryChunk* chunk =
      allocator_->LookupChunkContainingAddress(maybe_inner_ptr);
  if (chunk == nullptr) return kNullAddress;
  const MemoryChunkMetadata* chunk_metadata = chunk->Metadata();
  DCHECK(chunk_metadata->Contains(maybe_inner_ptr));
  // If it is contained in a large page, we want to mark the only object on it.
  if (chunk->IsLargePage()) {
    // This could be simplified if we could guarantee that there are no free
    // space or filler objects in large pages. A few cctests violate this now.
    Tagged<HeapObject> obj(
        static_cast<const LargePageMetadata*>(chunk_metadata)->GetObject());
    return IsFreeSpaceOrFiller(obj, cage_base) ? kNullAddress : obj.address();
  }
  // Otherwise, we have a pointer inside a normal page.
  const PageMetadata* page = static_cast<const PageMetadata*>(chunk_metadata);
  // If it is not in the young generation and we're only interested in young
  // generation pointers, we must ignore it.
  if (v8_flags.sticky_mark_bits) {
    if (Heap::IsYoungGenerationCollector(collector_) &&
        chunk->IsFlagSet(MemoryChunk::CONTAINS_ONLY_OLD))
      return kNullAddress;
  } else {
    if (Heap::IsYoungGenerationCollector(collector_) &&
        !chunk->InYoungGeneration())
      return kNullAddress;

    // If it is in the young generation "from" semispace, it is not used and we
    // must ignore it, as its markbits may not be clean.
    if (chunk->IsFromPage()) return kNullAddress;
  }

  // Try to find the address of a previous valid object on this page.
  Address base_ptr =
      MarkingBitmap::FindPreviousValidObject(page, maybe_inner_ptr);
  // Iterate through the objects in the page forwards, until we find the object
  // containing maybe_inner_ptr.
  DCHECK_LE(base_ptr, maybe_inner_ptr);
  while (true) {
    Tagged<HeapObject> obj(HeapObject::FromAddress(base_ptr));
    const int size = obj->Size(cage_base);
    DCHECK_LT(0, size);
    if (maybe_inner_ptr < base_ptr + size)
      return IsFreeSpaceOrFiller(obj, cage_base) ? kNullAddress : base_ptr;
    base_ptr += size;
    DCHECK_LT(base_ptr, page->area_end());
  }
}

void ConservativeStackVisitor::VisitPointer(const void* pointer) {
  auto address = reinterpret_cast<Address>(const_cast<void*>(pointer));
  VisitConservativelyIfPointer(address);
#ifdef V8_COMPRESS_POINTERS
  V8HeapCompressionScheme::ProcessIntermediatePointers(
      cage_base_, address,
      [this](Address ptr) { VisitConservativelyIfPointer(ptr, cage_base_); });
#ifdef V8_EXTERNAL_CODE_SPACE
  ExternalCodeCompressionScheme::ProcessIntermediatePointers(
      code_cage_base_, address, [this](Address ptr) {
        VisitConservativelyIfPointer(ptr, code_cage_base_);
      });
#endif  // V8_EXTERNAL_CODE_SPACE
#ifdef V8_ENABLE_SANDBOX
  TrustedSpaceCompressionScheme::ProcessIntermediatePointers(
      trusted_cage_base_, address, [this](Address ptr) {
        VisitConservativelyIfPointer(ptr, trusted_cage_base_);
      });
#endif  // V8_ENABLE_SANDBOX
#endif  // V8_COMPRESS_POINTERS
}

void ConservativeStackVisitor::VisitConservativelyIfPointer(Address address) {
#ifdef V8_COMPRESS_POINTERS
  // Only proceed if the address falls in one of the interesting cages,
  // otherwise bail out.
  if (V8HeapCompressionScheme::GetPtrComprCageBaseAddress(address) ==
      cage_base_.address()) {
    VisitConservativelyIfPointer(address, cage_base_);
  }
#ifdef V8_EXTERNAL_CODE_SPACE
  else if (code_address_region_.contains(address)) {
    VisitConservativelyIfPointer(address, code_cage_base_);
  }
#endif  // V8_EXTERNAL_CODE_SPACE
#else   // !V8_COMPRESS_POINTERS
  VisitConservativelyIfPointer(address, cage_base_);
#endif  // V8_COMPRESS_POINTERS
}

void ConservativeStackVisitor::VisitConservativelyIfPointer(
    Address address, PtrComprCageBase cage_base) {
  // Bail out immediately if the pointer is not in the space managed by the
  // allocator.
  if (allocator_->IsOutsideAllocatedSpace(address)) {
    DCHECK_EQ(nullptr, allocator_->LookupChunkContainingAddress(address));
    return;
  }
  // Proceed with inner-pointer resolution.
  Address base_ptr = FindBasePtr(address, cage_base);
  if (base_ptr == kNullAddress) return;
  Tagged<HeapObject> obj = HeapObject::FromAddress(base_ptr);
  Tagged<Object> root = obj;
  DCHECK_NOT_NULL(delegate_);
  delegate_->VisitRootPointer(Root::kStackRoots, nullptr,
                              FullObjectSlot(&root));
  // Check that the delegate visitor did not modify the root slot.
  DCHECK_EQ(root, obj);
}

}  // namespace internal
}  // namespace v8
