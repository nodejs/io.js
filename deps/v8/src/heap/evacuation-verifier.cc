// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/reloc-info.h"
#include "src/heap/evacuation-verifier-inl.h"
#include "src/objects/map-inl.h"

namespace v8 {
namespace internal {

#ifdef VERIFY_HEAP

EvacuationVerifier::EvacuationVerifier(Heap* heap)
    : ObjectVisitorWithCageBases(heap), heap_(heap) {}

void EvacuationVerifier::Run() {
  DCHECK(!heap_->sweeping_in_progress());
  VerifyRoots();
  VerifyEvacuation(heap_->new_space());
  VerifyEvacuation(heap_->old_space());
  VerifyEvacuation(heap_->code_space());
  if (heap_->shared_space()) VerifyEvacuation(heap_->shared_space());
}

void EvacuationVerifier::VisitPointers(Tagged<HeapObject> host,
                                       ObjectSlot start, ObjectSlot end) {
  VerifyPointersImpl(start, end);
}

void EvacuationVerifier::VisitPointers(Tagged<HeapObject> host,
                                       MaybeObjectSlot start,
                                       MaybeObjectSlot end) {
  VerifyPointersImpl(start, end);
}

void EvacuationVerifier::VisitInstructionStreamPointer(
    Tagged<Code> host, InstructionStreamSlot slot) {
  Tagged<Object> maybe_code = slot.load(code_cage_base());
  Tagged<HeapObject> code;
  // The slot might contain smi during Code creation, so skip it.
  if (maybe_code.GetHeapObject(&code)) {
    VerifyHeapObjectImpl(code);
  }
}

void EvacuationVerifier::VisitRootPointers(Root root, const char* description,
                                           FullObjectSlot start,
                                           FullObjectSlot end) {
  VerifyPointersImpl(start, end);
}

void EvacuationVerifier::VisitMapPointer(Tagged<HeapObject> object) {
  VerifyHeapObjectImpl(object->map(cage_base()));
}

void EvacuationVerifier::VisitCodeTarget(Tagged<InstructionStream> host,
                                         RelocInfo* rinfo) {
  Tagged<InstructionStream> target =
      InstructionStream::FromTargetAddress(rinfo->target_address());
  VerifyHeapObjectImpl(target);
}

void EvacuationVerifier::VisitEmbeddedPointer(Tagged<InstructionStream> host,
                                              RelocInfo* rinfo) {
  VerifyHeapObjectImpl(rinfo->target_object(cage_base()));
}

void EvacuationVerifier::VerifyRoots() {
  heap_->IterateRootsIncludingClients(
      this,
      base::EnumSet<SkipRoot>{SkipRoot::kWeak, SkipRoot::kConservativeStack});
}

void EvacuationVerifier::VerifyEvacuationOnPage(Address start, Address end) {
  Address current = start;
  while (current < end) {
    Tagged<HeapObject> object = HeapObject::FromAddress(current);
    if (!IsFreeSpaceOrFiller(object, cage_base())) {
      object->Iterate(cage_base(), this);
    }
    current += ALIGN_TO_ALLOCATION_ALIGNMENT(object->Size(cage_base()));
  }
}

void EvacuationVerifier::VerifyEvacuation(NewSpace* space) {
  if (!space) return;

  if (v8_flags.minor_ms) {
    VerifyEvacuation(PagedNewSpace::From(space)->paged_space());
    return;
  }

  for (PageMetadata* p : *space) {
    VerifyEvacuationOnPage(p->area_start(), p->area_end());
  }
}

void EvacuationVerifier::VerifyEvacuation(PagedSpaceBase* space) {
  for (PageMetadata* p : *space) {
    if (p->Chunk()->IsEvacuationCandidate()) continue;
    VerifyEvacuationOnPage(p->area_start(), p->area_end());
  }
}

#endif  // VERIFY_HEAP

}  // namespace internal
}  // namespace v8
