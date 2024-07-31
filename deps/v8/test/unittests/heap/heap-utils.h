// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_UNITTESTS_HEAP_HEAP_UTILS_H_
#define V8_UNITTESTS_HEAP_HEAP_UTILS_H_

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/heap.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

class HeapInternalsBase {
 protected:
  void SimulateIncrementalMarking(Heap* heap, bool force_completion);
  void SimulateFullSpace(
      v8::internal::NewSpace* space,
      std::vector<Handle<FixedArray>>* out_handles = nullptr);
  void SimulateFullSpace(v8::internal::PagedSpace* space);
  void FillCurrentPage(v8::internal::NewSpace* space,
                       std::vector<Handle<FixedArray>>* out_handles = nullptr);
};

inline void InvokeMajorGC(i::Isolate* isolate) {
  isolate->heap()->CollectGarbage(OLD_SPACE, GarbageCollectionReason::kTesting);
}

inline void InvokeMajorGC(i::Isolate* isolate, GCFlag gc_flag) {
  isolate->heap()->CollectAllGarbage(gc_flag,
                                     GarbageCollectionReason::kTesting);
}

inline void InvokeMinorGC(i::Isolate* isolate) {
  isolate->heap()->CollectGarbage(NEW_SPACE, GarbageCollectionReason::kTesting);
}

inline void InvokeAtomicMajorGC(i::Isolate* isolate) {
  Heap* heap = isolate->heap();
  heap->PreciseCollectAllGarbage(GCFlag::kNoFlags,
                                 GarbageCollectionReason::kTesting);
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kUnifiedHeap);
  }
}

inline void InvokeAtomicMinorGC(i::Isolate* isolate) {
  InvokeMinorGC(isolate);
  Heap* heap = isolate->heap();
  if (heap->sweeping_in_progress()) {
    heap->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kUnifiedHeap);
  }
}

inline void InvokeMemoryReducingMajorGCs(i::Isolate* isolate) {
  isolate->heap()->CollectAllAvailableGarbage(
      GarbageCollectionReason::kTesting);
}

template <typename TMixin>
class WithHeapInternals : public TMixin, HeapInternalsBase {
 public:
  WithHeapInternals() = default;
  WithHeapInternals(const WithHeapInternals&) = delete;
  WithHeapInternals& operator=(const WithHeapInternals&) = delete;

  void InvokeMajorGC() { i::InvokeMajorGC(this->i_isolate()); }

  void InvokeMajorGC(GCFlag gc_flag) {
    i::InvokeMajorGC(this->i_isolate(), gc_flag);
  }

  void InvokeMinorGC() { i::InvokeMinorGC(this->i_isolate()); }

  void InvokeAtomicMajorGC() { i::InvokeAtomicMajorGC(this->i_isolate()); }

  void InvokeAtomicMinorGC() { i::InvokeAtomicMinorGC(this->i_isolate()); }

  void InvokeMemoryReducingMajorGCs() {
    i::InvokeMemoryReducingMajorGCs(this->i_isolate());
  }

  void PreciseCollectAllGarbage() {
    heap()->PreciseCollectAllGarbage(GCFlag::kNoFlags,
                                     GarbageCollectionReason::kTesting);
  }

  Heap* heap() const { return this->i_isolate()->heap(); }

  void SimulateIncrementalMarking(bool force_completion = true) {
    return HeapInternalsBase::SimulateIncrementalMarking(heap(),
                                                         force_completion);
  }

  void SimulateFullSpace(
      v8::internal::NewSpace* space,
      std::vector<Handle<FixedArray>>* out_handles = nullptr) {
    return HeapInternalsBase::SimulateFullSpace(space, out_handles);
  }
  void SimulateFullSpace(v8::internal::PagedSpace* space) {
    return HeapInternalsBase::SimulateFullSpace(space);
  }

  void GrowNewSpace() {
    IsolateSafepointScope scope(heap());
    NewSpace* new_space = heap()->new_space();
    if (new_space->TotalCapacity() < new_space->MaximumCapacity()) {
      new_space->Grow();
    }
    CHECK(new_space->EnsureCurrentCapacity());
  }

  void SealCurrentObjects() {
    // If you see this check failing, disable the flag at the start of your
    // test: v8_flags.stress_concurrent_allocation = false; Background thread
    // allocating concurrently interferes with this function.
    CHECK(!v8_flags.stress_concurrent_allocation);
    InvokeMajorGC();
    InvokeMajorGC();
    heap()->EnsureSweepingCompleted(
        Heap::SweepingForcedFinalizationMode::kV8Only);
    heap()->FreeMainThreadLinearAllocationAreas();
    for (PageMetadata* page : *heap()->old_space()) {
      page->MarkNeverAllocateForTesting();
    }
  }

  void EmptyNewSpaceUsingGC() { InvokeMajorGC(); }
};

using TestWithHeapInternals =                  //
    WithHeapInternals<                         //
        WithInternalIsolateMixin<              //
            WithIsolateScopeMixin<             //
                WithIsolateMixin<              //
                    WithDefaultPlatformMixin<  //
                        ::testing::Test>>>>>;

using TestWithHeapInternalsAndContext =  //
    WithContextMixin<                    //
        TestWithHeapInternals>;

template <typename GlobalOrPersistent>
bool InYoungGeneration(v8::Isolate* isolate, const GlobalOrPersistent& global) {
  CHECK(!v8_flags.single_generation);
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return Heap::InYoungGeneration(*v8::Utils::OpenDirectHandle(*tmp));
}

bool IsNewObjectInCorrectGeneration(Tagged<HeapObject> object);

template <typename GlobalOrPersistent>
bool IsNewObjectInCorrectGeneration(v8::Isolate* isolate,
                                    const GlobalOrPersistent& global) {
  v8::HandleScope scope(isolate);
  auto tmp = global.Get(isolate);
  return IsNewObjectInCorrectGeneration(*v8::Utils::OpenDirectHandle(*tmp));
}

// ManualGCScope allows for disabling GC heuristics. This is useful for tests
// that want to check specific corner cases around GC.
//
// The scope will finalize any ongoing GC on the provided Isolate.
class V8_NODISCARD ManualGCScope final {
 public:
  explicit ManualGCScope(Isolate* isolate);
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

// DisableHandleChecksForMockingScope disables the checks for v8::Local and
// internal::DirectHandle, so that such handles can be allocated off-stack.
// This is required for mocking functions that take such handles as parameters
// and/or return them as results. For correctness (with direct handles), when
// this scope is used, it is important to ensure that the objects stored in
// handles used for mocking are retained by other means, so that they will not
// be reclaimed by a garbage collection.
class V8_NODISCARD DisableHandleChecksForMockingScope final
    : public StackAllocatedCheck::Scope {
 public:
  DisableHandleChecksForMockingScope() : StackAllocatedCheck::Scope(false) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_UNITTESTS_HEAP_HEAP_UTILS_H_
