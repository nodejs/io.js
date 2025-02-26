// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-state-inl.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/remembered-set-inl.h"
#include "src/objects/objects-inl.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

namespace {

void CheckInvariantsOfAbortedPage(PageMetadata* page) {
  // Check invariants:
  // 1) Markbits are cleared
  // 2) The page is not marked as evacuation candidate anymore
  // 3) The page is not marked as aborted compaction anymore.
  CHECK(page->marking_bitmap()->IsClean());
  CHECK(!page->Chunk()->IsEvacuationCandidate());
  CHECK(!page->Chunk()->IsFlagSet(MemoryChunk::COMPACTION_WAS_ABORTED));
}

void CheckAllObjectsOnPage(const DirectHandleVector<FixedArray>& handles,
                           PageMetadata* page) {
  for (DirectHandle<FixedArray> fixed_array : handles) {
    CHECK(PageMetadata::FromHeapObject(*fixed_array) == page);
  }
}

}  // namespace

HEAP_TEST(CompactionFullAbortedPage) {
  if (!v8_flags.compact) return;
  // Test the scenario where we reach OOM during compaction and the whole page
  // is aborted.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  auto reset_oom = [](void* heap, size_t limit, size_t) -> size_t {
    reinterpret_cast<Heap*>(heap)->set_force_oom(false);
    return limit;
  };
  heap->AddNearHeapLimitCallback(reset_oom, heap);
  {
    HandleScope scope1(isolate);

    heap::SealCurrentObjects(heap);

    {
      HandleScope scope2(isolate);
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      DirectHandleVector<FixedArray> compaction_page_handles(isolate);
      heap::CreatePadding(
          heap,
          static_cast<int>(MemoryChunkLayout::AllocatableMemoryInDataPage()),
          AllocationType::kOld, &compaction_page_handles);
      PageMetadata* to_be_aborted_page =
          PageMetadata::FromHeapObject(*compaction_page_handles.front());
      to_be_aborted_page->Chunk()->SetFlagNonExecutable(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
      CheckAllObjectsOnPage(compaction_page_handles, to_be_aborted_page);

      heap->set_force_oom(true);
      heap::InvokeMajorGC(heap);
      heap->EnsureSweepingCompleted(
          Heap::SweepingForcedFinalizationMode::kV8Only);

      // Check that all handles still point to the same page, i.e., compaction
      // has been aborted on the page.
      for (DirectHandle<FixedArray> object : compaction_page_handles) {
        CHECK_EQ(to_be_aborted_page, PageMetadata::FromHeapObject(*object));
      }
      CheckInvariantsOfAbortedPage(to_be_aborted_page);
    }
  }
  heap->RemoveNearHeapLimitCallback(reset_oom, 0u);
}

namespace {

int GetObjectSize(int objects_per_page) {
  int allocatable =
      static_cast<int>(MemoryChunkLayout::AllocatableMemoryInDataPage());
  // Make sure that object_size is a multiple of kTaggedSize.
  int object_size =
      ((allocatable / kTaggedSize) / objects_per_page) * kTaggedSize;
  return std::min(kMaxRegularHeapObjectSize, object_size);
}

}  // namespace

HEAP_TEST(CompactionPartiallyAbortedPage) {
  if (!v8_flags.compact) return;
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  const int objects_per_page = 10;
  const int object_size = GetObjectSize(objects_per_page);

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  auto reset_oom = [](void* heap, size_t limit, size_t) -> size_t {
    reinterpret_cast<Heap*>(heap)->set_force_oom(false);
    return limit;
  };
  heap->AddNearHeapLimitCallback(reset_oom, heap);
  {
    HandleScope scope1(isolate);

    heap::SealCurrentObjects(heap);

    {
      HandleScope scope2(isolate);
      // Fill another page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      DirectHandleVector<FixedArray> compaction_page_handles(isolate);
      heap::CreatePadding(
          heap,
          static_cast<int>(MemoryChunkLayout::AllocatableMemoryInDataPage()),
          AllocationType::kOld, &compaction_page_handles, object_size);
      PageMetadata* to_be_aborted_page =
          PageMetadata::FromHeapObject(*compaction_page_handles.front());
      to_be_aborted_page->Chunk()->SetFlagNonExecutable(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
      CheckAllObjectsOnPage(compaction_page_handles, to_be_aborted_page);

      {
        // Add another page that is filled with {num_objects} objects of size
        // {object_size}.
        HandleScope scope3(isolate);
        CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                           AllocationOrigin::kRuntime));
        const int num_objects = 3;
        DirectHandleVector<FixedArray> page_to_fill_handles(isolate);
        heap::CreatePadding(heap, object_size * num_objects,
                            AllocationType::kOld, &page_to_fill_handles,
                            object_size);
        PageMetadata* page_to_fill =
            PageMetadata::FromAddress(page_to_fill_handles.front()->address());

        heap->set_force_oom(true);
        heap::InvokeMajorGC(heap);
        heap->EnsureSweepingCompleted(
            Heap::SweepingForcedFinalizationMode::kV8Only);

        bool migration_aborted = false;
        for (DirectHandle<FixedArray> object : compaction_page_handles) {
          // Once compaction has been aborted, all following objects still have
          // to be on the initial page.
          CHECK(!migration_aborted ||
                (PageMetadata::FromHeapObject(*object) == to_be_aborted_page));
          if (PageMetadata::FromHeapObject(*object) == to_be_aborted_page) {
            // This object has not been migrated.
            migration_aborted = true;
          } else {
            CHECK_EQ(PageMetadata::FromHeapObject(*object), page_to_fill);
          }
        }
        // Check that we actually created a scenario with a partially aborted
        // page.
        CHECK(migration_aborted);
        CheckInvariantsOfAbortedPage(to_be_aborted_page);
      }
    }
  }
  heap->RemoveNearHeapLimitCallback(reset_oom, 0u);
}

HEAP_TEST(CompactionPartiallyAbortedPageIntraAbortedPointers) {
  if (!v8_flags.compact) return;
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one. Objects on the aborted page
  // are linked together. This test makes sure that intra-aborted page pointers
  // get properly updated.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  const int objects_per_page = 10;
  const int object_size = GetObjectSize(objects_per_page);

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  auto reset_oom = [](void* heap, size_t limit, size_t) -> size_t {
    reinterpret_cast<Heap*>(heap)->set_force_oom(false);
    return limit;
  };
  heap->AddNearHeapLimitCallback(reset_oom, heap);
  {
    HandleScope scope1(isolate);
    IndirectHandle<FixedArray> root_array =
        isolate->factory()->NewFixedArray(10, AllocationType::kOld);

    heap::SealCurrentObjects(heap);

    PageMetadata* to_be_aborted_page = nullptr;
    {
      HandleScope temporary_scope(isolate);
      // Fill a fresh page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      DirectHandleVector<FixedArray> compaction_page_handles(isolate);
      heap::CreatePadding(
          heap,
          static_cast<int>(MemoryChunkLayout::AllocatableMemoryInDataPage()),
          AllocationType::kOld, &compaction_page_handles, object_size);
      to_be_aborted_page =
          PageMetadata::FromHeapObject(*compaction_page_handles.front());
      to_be_aborted_page->Chunk()->SetFlagNonExecutable(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);
      for (size_t i = compaction_page_handles.size() - 1; i > 0; i--) {
        compaction_page_handles[i]->set(0, *compaction_page_handles[i - 1]);
      }
      root_array->set(0, *compaction_page_handles.back());
      CheckAllObjectsOnPage(compaction_page_handles, to_be_aborted_page);
    }
    {
      // Add another page that is filled with {num_objects} objects of size
      // {object_size}.
      HandleScope scope3(isolate);
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      const int num_objects = 2;
      int used_memory = object_size * num_objects;
      DirectHandleVector<FixedArray> page_to_fill_handles(isolate);
      heap::CreatePadding(heap, used_memory, AllocationType::kOld,
                          &page_to_fill_handles, object_size);
      PageMetadata* page_to_fill =
          PageMetadata::FromHeapObject(*page_to_fill_handles.front());

      // We need to invoke GC without stack, otherwise no compaction is
      // performed.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

      heap->set_force_oom(true);
      heap::InvokeMajorGC(heap);
      heap->EnsureSweepingCompleted(
          Heap::SweepingForcedFinalizationMode::kV8Only);

      // The following check makes sure that we compacted "some" objects, while
      // leaving others in place.
      bool in_place = true;
      IndirectHandle<FixedArray> current = root_array;
      while (current->get(0) != ReadOnlyRoots(heap).undefined_value()) {
        current = IndirectHandle<FixedArray>(Cast<FixedArray>(current->get(0)),
                                             isolate);
        CHECK(IsFixedArray(*current));
        if (PageMetadata::FromHeapObject(*current) != to_be_aborted_page) {
          in_place = false;
        }
        bool on_aborted_page =
            PageMetadata::FromHeapObject(*current) == to_be_aborted_page;
        bool on_fill_page =
            PageMetadata::FromHeapObject(*current) == page_to_fill;
        CHECK((in_place && on_aborted_page) || (!in_place && on_fill_page));
      }
      // Check that we at least migrated one object, as otherwise the test would
      // not trigger.
      CHECK(!in_place);
      CheckInvariantsOfAbortedPage(to_be_aborted_page);
    }
  }
  heap->RemoveNearHeapLimitCallback(reset_oom, 0u);
}

HEAP_TEST(CompactionPartiallyAbortedPageWithRememberedSetEntries) {
  if (!v8_flags.compact || v8_flags.single_generation) return;
  // Test the scenario where we reach OOM during compaction and parts of the
  // page have already been migrated to a new one. Objects on the aborted page
  // are linked together and the very first object on the aborted page points
  // into new space. The test verifies that the remembered set entries are
  // properly cleared and rebuilt after aborting a page. Failing to do so can
  // result in other objects being allocated in the free space where their
  // payload looks like a valid new space pointer.

  // Disable concurrent sweeping to ensure memory is in an expected state, i.e.,
  // we can reach the state of a half aborted page.
  ManualGCScope manual_gc_scope;
  heap::ManualEvacuationCandidatesSelectionScope
      manual_evacuation_candidate_selection_scope(manual_gc_scope);

  const int objects_per_page = 10;
  const int object_size = GetObjectSize(objects_per_page);

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  auto reset_oom = [](void* heap, size_t limit, size_t) -> size_t {
    reinterpret_cast<Heap*>(heap)->set_force_oom(false);
    return limit;
  };
  heap->AddNearHeapLimitCallback(reset_oom, heap);
  {
    HandleScope scope1(isolate);
    IndirectHandle<FixedArray> root_array =
        isolate->factory()->NewFixedArray(10, AllocationType::kOld);
    heap::SealCurrentObjects(heap);

    PageMetadata* to_be_aborted_page = nullptr;
    {
      HandleScope temporary_scope(isolate);
      // Fill another page with objects of size {object_size} (last one is
      // properly adjusted).
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      DirectHandleVector<FixedArray> compaction_page_handles(isolate);
      heap::CreatePadding(
          heap,
          static_cast<int>(MemoryChunkLayout::AllocatableMemoryInDataPage()),
          AllocationType::kOld, &compaction_page_handles, object_size);
      // Sanity check that we have enough space for linking up arrays.
      CHECK_GE(compaction_page_handles.front()->length(), 2);
      to_be_aborted_page =
          PageMetadata::FromHeapObject(*compaction_page_handles.front());
      to_be_aborted_page->Chunk()->SetFlagNonExecutable(
          MemoryChunk::FORCE_EVACUATION_CANDIDATE_FOR_TESTING);

      for (size_t i = compaction_page_handles.size() - 1; i > 0; i--) {
        compaction_page_handles[i]->set(0, *compaction_page_handles[i - 1]);
      }
      root_array->set(0, *compaction_page_handles.back());
      DirectHandle<FixedArray> new_space_array =
          isolate->factory()->NewFixedArray(1, AllocationType::kYoung);
      CHECK(HeapLayout::InYoungGeneration(*new_space_array));
      compaction_page_handles.front()->set(1, *new_space_array);
      CheckAllObjectsOnPage(compaction_page_handles, to_be_aborted_page);
    }

    {
      // Add another page that is filled with {num_objects} objects of size
      // {object_size}.
      HandleScope scope3(isolate);
      CHECK(heap->old_space()->TryExpand(heap->main_thread_local_heap(),
                                         AllocationOrigin::kRuntime));
      const int num_objects = 2;
      int used_memory = object_size * num_objects;
      DirectHandleVector<FixedArray> page_to_fill_handles(isolate);
      heap::CreatePadding(heap, used_memory, AllocationType::kOld,
                          &page_to_fill_handles, object_size);
      PageMetadata* page_to_fill =
          PageMetadata::FromHeapObject(*page_to_fill_handles.front());

      // We need to invoke GC without stack, otherwise no compaction is
      // performed.
      DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap);

      heap->set_force_oom(true);
      heap::InvokeMajorGC(heap);
      heap->EnsureSweepingCompleted(
          Heap::SweepingForcedFinalizationMode::kV8Only);

      // The following check makes sure that we compacted "some" objects, while
      // leaving others in place.
      bool in_place = true;
      IndirectHandle<FixedArray> current = root_array;
      while (current->get(0) != ReadOnlyRoots(heap).undefined_value()) {
        current = IndirectHandle<FixedArray>(Cast<FixedArray>(current->get(0)),
                                             isolate);
        CHECK(!HeapLayout::InYoungGeneration(*current));
        CHECK(IsFixedArray(*current));
        if (PageMetadata::FromHeapObject(*current) != to_be_aborted_page) {
          in_place = false;
        }
        bool on_aborted_page =
            PageMetadata::FromHeapObject(*current) == to_be_aborted_page;
        bool on_fill_page =
            PageMetadata::FromHeapObject(*current) == page_to_fill;
        CHECK((in_place && on_aborted_page) || (!in_place && on_fill_page));
      }
      // Check that we at least migrated one object, as otherwise the test would
      // not trigger.
      CHECK(!in_place);
      CheckInvariantsOfAbortedPage(to_be_aborted_page);

      // Allocate a new object in new space.
      IndirectHandle<FixedArray> holder =
          isolate->factory()->NewFixedArray(10, AllocationType::kYoung);
      // Create a broken address that looks like a tagged pointer to a new space
      // object.
      Address broken_address = holder->address() + 2 * kTaggedSize + 1;
      // Convert it to a vector to create a string from it.
      base::Vector<const uint8_t> string_to_broken_address(
          reinterpret_cast<const uint8_t*>(&broken_address), kTaggedSize);

      IndirectHandle<String> string;
      do {
        // We know that the interesting slot will be on the aborted page and
        // hence we allocate until we get our string on the aborted page.
        // We used slot 1 in the fixed size array which corresponds to the
        // the first word in the string. Since the first object definitely
        // migrated we can just allocate until we hit the aborted page.
        string = isolate->factory()
                     ->NewStringFromOneByte(string_to_broken_address,
                                            AllocationType::kOld)
                     .ToHandleChecked();
      } while (PageMetadata::FromHeapObject(*string) != to_be_aborted_page);

      // If remembered set entries are not properly filtered/reset for aborted
      // pages we have now a broken address at an object slot in old space and
      // the following scavenge will crash.
      heap::InvokeMinorGC(CcTest::heap());
    }
  }
  heap->RemoveNearHeapLimitCallback(reset_oom, 0u);
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
