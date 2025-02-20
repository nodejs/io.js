// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HEAP_HEAP_TESTER_H_
#define HEAP_HEAP_TESTER_H_

#include "src/heap/spaces.h"
#include "src/objects/fixed-array.h"

// Tests that should have access to private methods of {v8::internal::Heap}.
// Those tests need to be defined using HEAP_TEST(Name) { ... }.
#define HEAP_TEST_METHODS(V)                                \
  V(CodeLargeObjectSpace)                                   \
  V(CodeLargeObjectSpace64k)                                \
  V(CompactionFullAbortedPage)                              \
  V(CompactionPartiallyAbortedPage)                         \
  V(CompactionPartiallyAbortedPageIntraAbortedPointers)     \
  V(CompactionPartiallyAbortedPageWithInvalidatedSlots)     \
  V(CompactionPartiallyAbortedPageWithRememberedSetEntries) \
  V(CompactionSpaceDivideMultiplePages)                     \
  V(CompactionSpaceDivideSinglePage)                        \
  V(InvalidatedSlotsAfterTrimming)                          \
  V(InvalidatedSlotsAllInvalidatedRanges)                   \
  V(InvalidatedSlotsCleanupEachObject)                      \
  V(InvalidatedSlotsCleanupFull)                            \
  V(InvalidatedSlotsCleanupRightTrim)                       \
  V(InvalidatedSlotsCleanupOverlapRight)                    \
  V(InvalidatedSlotsEvacuationCandidate)                    \
  V(InvalidatedSlotsNoInvalidatedRanges)                    \
  V(InvalidatedSlotsResetObjectRegression)                  \
  V(InvalidatedSlotsRightTrimFixedArray)                    \
  V(InvalidatedSlotsRightTrimLargeFixedArray)               \
  V(InvalidatedSlotsFastToSlow)                             \
  V(InvalidatedSlotsSomeInvalidatedRanges)                  \
  V(TestNewSpaceRefsInCopiedCode)                           \
  V(GCFlags)                                                \
  V(MarkCompactCollector)                                   \
  V(MarkCompactEpochCounter)                                \
  V(MemoryReducerActivationForSmallHeaps)                   \
  V(NoPromotion)                                            \
  V(NumberStringCacheSize)                                  \
  V(ObjectGroups)                                           \
  V(Promotion)                                              \
  V(Regression39128)                                        \
  V(ResetWeakHandle)                                        \
  V(StressHandles)                                          \
  V(TestMemoryReducerSampleJsCalls)                         \
  V(TestSizeOfObjects)                                      \
  V(Regress10560)                                           \
  V(Regress538257)                                          \
  V(Regress587004)                                          \
  V(Regress589413)                                          \
  V(Regress658718)                                          \
  V(Regress670675)                                          \
  V(Regress777177)                                          \
  V(Regress779503)                                          \
  V(Regress791582)                                          \
  V(Regress845060)                                          \
  V(RegressMissingWriteBarrierInAllocate)                   \
  V(WriteBarrier_Marking)                                   \
  V(WriteBarrier_MarkingExtension)                          \
  V(WriteBarriersInCopyJSObject)                            \
  V(DoNotEvacuatePinnedPages)                               \
  V(ObjectStartBitmap)

#define HEAP_TEST(Name)                                                   \
  CcTest register_test_##Name(v8::internal::heap::HeapTester::Test##Name, \
                              __FILE__, #Name, true, true);               \
  void v8::internal::heap::HeapTester::Test##Name()

#define UNINITIALIZED_HEAP_TEST(Name)                                     \
  CcTest register_test_##Name(v8::internal::heap::HeapTester::Test##Name, \
                              __FILE__, #Name, true, false);              \
  void v8::internal::heap::HeapTester::Test##Name()

#define THREADED_HEAP_TEST(Name)                          \
  RegisterThreadedTest register_##Name(                   \
      v8::internal::heap::HeapTester::Test##Name, #Name); \
  /* */ HEAP_TEST(Name)

namespace v8 {
namespace internal {
namespace heap {

class HeapTester {
 public:
#define DECLARE_STATIC(Name) static void Test##Name();

  HEAP_TEST_METHODS(DECLARE_STATIC)
#undef HEAP_TEST_METHODS

  // test-alloc.cc
  static AllocationResult AllocateAfterFailures();
  static DirectHandle<Object> TestAllocateAfterFailures();

  // test-invalidated-slots.cc
  static PageMetadata* AllocateByteArraysOnPage(
      Heap* heap, std::vector<ByteArray>* byte_arrays);

  // test-api.cc
  static void ResetWeakHandle(bool global_gc);

  // test-heap.cc
  static AllocationResult AllocateByteArrayForTest(Heap* heap, int length,
                                                   AllocationType allocation);
  static bool CodeEnsureLinearAllocationArea(Heap* heap, int size_in_bytes);

  // test-mark-compact.cc
  static AllocationResult AllocateMapForTest(v8::internal::Isolate* isolate);
  static AllocationResult AllocateFixedArrayForTest(Heap* heap, int length,
                                                    AllocationType allocation);

  static void UncommitUnusedMemory(Heap* heap);
};

}  // namespace heap
}  // namespace internal
}  // namespace v8

#endif  // HEAP_HEAP_TESTER_H_
