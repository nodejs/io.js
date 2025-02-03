// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEAP_HEAP_UTILS_H_
#define HEAP_HEAP_UTILS_H_

#include "src/api/api-inl.h"
#include "src/flags/flags.h"
#include "src/heap/heap.h"
#include "test/cctest/cctest.h"

namespace v8::internal {

namespace heap {

void SealCurrentObjects(Heap* heap);

int FixedArrayLenFromSize(int size);

// Fill a page with fixed arrays leaving remainder behind. The function does
// not create additional fillers and assumes that the space has just been
// sealed.
std::vector<Handle<FixedArray>> FillOldSpacePageWithFixedArrays(Heap* heap,
                                                                int remainder);

std::vector<Handle<FixedArray>> CreatePadding(
    Heap* heap, int padding_size, AllocationType allocation,
    int object_size = kMaxRegularHeapObjectSize);

void FillCurrentPage(v8::internal::NewSpace* space,
                     std::vector<Handle<FixedArray>>* out_handles = nullptr);

void FillCurrentPageButNBytes(
    v8::internal::SemiSpaceNewSpace* space, int extra_bytes,
    std::vector<Handle<FixedArray>>* out_handles = nullptr);

// Helper function that simulates many incremental marking steps until
// marking is completed.
void SimulateIncrementalMarking(i::Heap* heap, bool force_completion = true);

// Helper function that simulates a full old-space in the heap.
void SimulateFullSpace(v8::internal::PagedSpace* space);

void AbandonCurrentlyFreeMemory(PagedSpace* space);

void InvokeMajorGC(Heap* heap);
void InvokeMajorGC(Heap* heap, GCFlag gc_flag);
void InvokeMinorGC(Heap* heap);
void InvokeAtomicMajorGC(Heap* heap);
void InvokeAtomicMinorGC(Heap* heap);
void InvokeMemoryReducingMajorGCs(Heap* heap);
void CollectSharedGarbage(Heap* heap);

void EmptyNewSpaceUsingGC(Heap* heap);

void ForceEvacuationCandidate(PageMetadata* page);

void GrowNewSpace(Heap* heap);

void GrowNewSpaceToMaximumCapacity(Heap* heap);

template <typename GlobalOrPersistent>
bool InYoungGeneration(v8::Isolate* isolate, const GlobalOrPersistent& global) {
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return i::Heap::InYoungGeneration(*v8::Utils::OpenDirectHandle(*tmp));
}

bool InCorrectGeneration(Tagged<HeapObject> object);

template <typename GlobalOrPersistent>
bool InCorrectGeneration(v8::Isolate* isolate,
                         const GlobalOrPersistent& global) {
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return InCorrectGeneration(*v8::Utils::OpenDirectHandle(*tmp));
}

class ManualEvacuationCandidatesSelectionScope {
 public:
  // Marking a page as an evacuation candidate update the page flags which may
  // race with reading the page flag during concurrent marking.
  explicit ManualEvacuationCandidatesSelectionScope(ManualGCScope&) {
    DCHECK(!v8_flags.manual_evacuation_candidates_selection);
    v8_flags.manual_evacuation_candidates_selection = true;
  }
  ~ManualEvacuationCandidatesSelectionScope() {
    DCHECK(v8_flags.manual_evacuation_candidates_selection);
    v8_flags.manual_evacuation_candidates_selection = false;
  }

 private:
};

}  // namespace heap

// ManualGCScope allows for disabling GC heuristics. This is useful for tests
// that want to check specific corner cases around GC.
//
// The scope will finalize any ongoing GC on the provided Isolate. If no Isolate
// is manually provided, it is assumed that a CcTest setup (e.g.
// CcTest::InitializeVM()) is used.
class V8_NODISCARD ManualGCScope final {
 public:
  explicit ManualGCScope(
      Isolate* isolate = reinterpret_cast<Isolate*>(CcTest::isolate_));
  ~ManualGCScope();

 private:
  Isolate* const isolate_;
  const bool flag_concurrent_marking_;
  const bool flag_concurrent_sweeping_;
  const bool flag_concurrent_minor_ms_marking_;
  const bool flag_stress_concurrent_allocation_;
  const bool flag_stress_incremental_marking_;
  const bool flag_parallel_marking_;
  const bool flag_detect_ineffective_gcs_near_heap_limit_;
  const bool flag_cppheap_concurrent_marking_;
};

}  // namespace v8::internal

#endif  // HEAP_HEAP_UTILS_H_
