// Copyright 2011 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include <stdlib.h>

#include <memory>

#include "include/v8-initialization.h"
#include "include/v8-platform.h"
#include "src/base/bounded-page-allocator.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/common/globals.h"
#include "src/heap/allocation-result.h"
#include "src/heap/factory.h"
#include "src/heap/heap.h"
#include "src/heap/large-spaces.h"
#include "src/heap/main-allocator.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces-inl.h"
#include "src/heap/spaces.h"
#include "src/objects/free-space.h"
#include "src/objects/objects-inl.h"
#include "src/snapshot/snapshot.h"
#include "test/cctest/cctest.h"
#include "test/cctest/heap/heap-tester.h"
#include "test/cctest/heap/heap-utils.h"

namespace v8 {
namespace internal {
namespace heap {

// Temporarily sets a given allocator in an isolate.
class V8_NODISCARD TestMemoryAllocatorScope {
 public:
  TestMemoryAllocatorScope(Isolate* isolate, size_t max_capacity,
                           PageAllocator* page_allocator = nullptr)
      : isolate_(isolate),
        old_allocator_(std::move(isolate->heap()->memory_allocator_)) {
    // Save the code pages for restoring them later on because the constructor
    // of MemoryAllocator will change them.
    isolate->GetCodePages()->swap(code_pages_);
    isolate->heap()->memory_allocator_.reset(new MemoryAllocator(
        isolate,
        page_allocator != nullptr ? page_allocator : isolate->page_allocator(),
        page_allocator != nullptr ? page_allocator : isolate->page_allocator(),
        max_capacity));
    if (page_allocator != nullptr) {
      isolate->heap()->memory_allocator_->data_page_allocator_ = page_allocator;
    }
  }

  MemoryAllocator* allocator() { return isolate_->heap()->memory_allocator(); }

  ~TestMemoryAllocatorScope() {
    isolate_->heap()->memory_allocator()->TearDown();
    isolate_->heap()->memory_allocator_.swap(old_allocator_);
    isolate_->GetCodePages()->swap(code_pages_);
  }

  TestMemoryAllocatorScope(const TestMemoryAllocatorScope&) = delete;
  TestMemoryAllocatorScope& operator=(const TestMemoryAllocatorScope&) = delete;

 private:
  Isolate* isolate_;
  std::unique_ptr<MemoryAllocator> old_allocator_;
  std::vector<MemoryRange> code_pages_;
};

// Temporarily sets a given code page allocator in an isolate.
class V8_NODISCARD TestCodePageAllocatorScope {
 public:
  TestCodePageAllocatorScope(Isolate* isolate,
                             v8::PageAllocator* code_page_allocator)
      : isolate_(isolate),
        old_code_page_allocator_(
            isolate->heap()->memory_allocator()->code_page_allocator()) {
    isolate->heap()->memory_allocator()->code_page_allocator_ =
        code_page_allocator;
  }

  ~TestCodePageAllocatorScope() {
    isolate_->heap()->memory_allocator()->code_page_allocator_ =
        old_code_page_allocator_;
  }
  TestCodePageAllocatorScope(const TestCodePageAllocatorScope&) = delete;
  TestCodePageAllocatorScope& operator=(const TestCodePageAllocatorScope&) =
      delete;

 private:
  Isolate* isolate_;
  v8::PageAllocator* old_code_page_allocator_;
};

static void VerifyMemoryChunk(Isolate* isolate, Heap* heap,
                              v8::PageAllocator* code_page_allocator,
                              size_t area_size, Executability executable,
                              PageSize page_size, LargeObjectSpace* space) {
  TestMemoryAllocatorScope test_allocator_scope(isolate, heap->MaxReserved());
  MemoryAllocator* memory_allocator = test_allocator_scope.allocator();
  TestCodePageAllocatorScope test_code_page_allocator_scope(
      isolate, code_page_allocator);

  v8::PageAllocator* page_allocator =
      memory_allocator->page_allocator(space->identity());

  size_t allocatable_memory_area_offset =
      MemoryChunkLayout::ObjectStartOffsetInMemoryChunk(space->identity());

  MutablePageMetadata* memory_chunk =
      memory_allocator->AllocateLargePage(space, area_size, executable);
  size_t reserved_size =
      ((executable == EXECUTABLE))
          ? RoundUp(allocatable_memory_area_offset +
                        RoundUp(area_size, page_allocator->CommitPageSize()),
                    page_allocator->CommitPageSize())
          : RoundUp(allocatable_memory_area_offset + area_size,
                    page_allocator->CommitPageSize());
  CHECK(memory_chunk->size() == reserved_size);
  CHECK(memory_chunk->area_start() <
        memory_chunk->ChunkAddress() + memory_chunk->size());
  CHECK(memory_chunk->area_end() <=
        memory_chunk->ChunkAddress() + memory_chunk->size());
  CHECK(static_cast<size_t>(memory_chunk->area_size()) == area_size);

  memory_allocator->Free(MemoryAllocator::FreeMode::kImmediately, memory_chunk);
}

static unsigned int PseudorandomAreaSize() {
  static uint32_t lo = 2345;
  lo = 18273 * (lo & 0xFFFFF) + (lo >> 16);
  return lo & 0xFFFFF;
}

TEST(MutablePageMetadata) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  IsolateSafepointScope safepoint(heap);

  v8::PageAllocator* page_allocator = GetPlatformPageAllocator();
  size_t area_size;

  for (int i = 0; i < 100; i++) {
    area_size =
        RoundUp(PseudorandomAreaSize(), page_allocator->CommitPageSize());

    const size_t code_range_size = 32 * MB;
#ifdef V8_ENABLE_SANDBOX
    // When the sandbox is enabled, the code assumes that there's only a single
    // code range for easy metadata lookup, so use the process wide code range
    // in this case.
    CodeRange* code_range =
        IsolateGroup::current()->EnsureCodeRange(code_range_size);
    base::BoundedPageAllocator* page_allocator = code_range->page_allocator();
#else
    // With CodeRange.
    bool jitless = isolate->jitless();
    VirtualMemory code_range_reservation(
        page_allocator, code_range_size, nullptr,
        MemoryChunk::GetAlignmentForAllocation(),
        jitless ? PageAllocator::Permission::kNoAccess
                : PageAllocator::Permission::kNoAccessWillJitLater);

    base::PageInitializationMode page_initialization_mode =
        base::PageInitializationMode::kAllocatedPagesCanBeUninitialized;
    base::PageFreeingMode page_freeing_mode =
        base::PageFreeingMode::kMakeInaccessible;

    if (!jitless) {
      page_initialization_mode = base::PageInitializationMode::kRecommitOnly;
      page_freeing_mode = base::PageFreeingMode::kDiscard;
      void* base = reinterpret_cast<void*>(code_range_reservation.address());
      CHECK(page_allocator->SetPermissions(base, code_range_size,
                                           PageAllocator::kReadWriteExecute));
      CHECK(page_allocator->DiscardSystemPages(base, code_range_size));
    }

    CHECK(code_range_reservation.IsReserved());

    base::BoundedPageAllocator code_page_allocator(
        page_allocator, code_range_reservation.address(),
        code_range_reservation.size(), MemoryChunk::GetAlignmentForAllocation(),
        page_initialization_mode, page_freeing_mode);
    base::BoundedPageAllocator* page_allocator = &code_page_allocator;
#endif

    VerifyMemoryChunk(isolate, heap, page_allocator, area_size, EXECUTABLE,
                      PageSize::kLarge, heap->code_lo_space());

    VerifyMemoryChunk(isolate, heap, page_allocator, area_size, NOT_EXECUTABLE,
                      PageSize::kLarge, heap->lo_space());
  }
}

TEST(MemoryAllocator) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  TestMemoryAllocatorScope test_allocator_scope(isolate, heap->MaxReserved());
  MemoryAllocator* memory_allocator = test_allocator_scope.allocator();

  int total_pages = 0;
  OldSpace faked_space(heap);
  CHECK(!faked_space.first_page());
  CHECK(!faked_space.last_page());
  PageMetadata* first_page = memory_allocator->AllocatePage(
      MemoryAllocator::AllocationMode::kRegular,
      static_cast<PagedSpace*>(&faked_space), NOT_EXECUTABLE);

  faked_space.memory_chunk_list().PushBack(first_page);
  CHECK(first_page->next_page() == nullptr);
  total_pages++;

  for (PageMetadata* p = first_page; p != nullptr; p = p->next_page()) {
    CHECK(p->owner() == &faked_space);
  }

  // Again, we should get n or n - 1 pages.
  PageMetadata* other = memory_allocator->AllocatePage(
      MemoryAllocator::AllocationMode::kRegular,
      static_cast<PagedSpace*>(&faked_space), NOT_EXECUTABLE);
  total_pages++;
  faked_space.memory_chunk_list().PushBack(other);
  int page_count = 0;
  for (PageMetadata* p = first_page; p != nullptr; p = p->next_page()) {
    CHECK(p->owner() == &faked_space);
    page_count++;
  }
  CHECK(total_pages == page_count);

  PageMetadata* second_page = first_page->next_page();
  CHECK_NOT_NULL(second_page);

  // OldSpace's destructor will tear down the space and free up all pages.
}

TEST(ComputeDiscardMemoryAreas) {
  base::AddressRegion memory_area;
  size_t page_size = MemoryAllocator::GetCommitPageSize();
  size_t free_header_size = FreeSpace::kSize;

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(0, 0);
  CHECK_EQ(memory_area.begin(), 0);
  CHECK_EQ(memory_area.size(), 0);

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(
      0, page_size + free_header_size);
  CHECK_EQ(memory_area.begin(), 0);
  CHECK_EQ(memory_area.size(), 0);

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(
      page_size - free_header_size, page_size + free_header_size);
  CHECK_EQ(memory_area.begin(), page_size);
  CHECK_EQ(memory_area.size(), page_size);

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(page_size, page_size);
  CHECK_EQ(memory_area.begin(), 0);
  CHECK_EQ(memory_area.size(), 0);

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(
      page_size / 2, page_size + page_size / 2);
  CHECK_EQ(memory_area.begin(), page_size);
  CHECK_EQ(memory_area.size(), page_size);

  memory_area = MemoryAllocator::ComputeDiscardMemoryArea(
      page_size / 2, page_size + page_size / 4);
  CHECK_EQ(memory_area.begin(), 0);
  CHECK_EQ(memory_area.size(), 0);

  memory_area =
      MemoryAllocator::ComputeDiscardMemoryArea(page_size / 2, page_size * 3);
  CHECK_EQ(memory_area.begin(), page_size);
  CHECK_EQ(memory_area.size(), page_size * 2);
}

TEST(SemiSpaceNewSpace) {
  if (v8_flags.single_generation) return;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  TestMemoryAllocatorScope test_allocator_scope(isolate, heap->MaxReserved());
  MemoryAllocator* memory_allocator = test_allocator_scope.allocator();
  LinearAllocationArea allocation_info;

  auto new_space = std::make_unique<SemiSpaceNewSpace>(
      heap, heap->InitialSemiSpaceSize(), heap->InitialSemiSpaceSize());
  MainAllocator allocator(heap->main_thread_local_heap(), new_space.get(),
                          MainAllocator::IsNewGeneration::kYes,
                          &allocation_info);
  CHECK(new_space->MaximumCapacity());

  size_t successful_allocations = 0;
  while (new_space->Available() >= kMaxRegularHeapObjectSize) {
    AllocationResult allocation = allocator.AllocateRaw(
        kMaxRegularHeapObjectSize, kTaggedAligned, AllocationOrigin::kRuntime);
    if (allocation.IsFailure()) break;
    successful_allocations++;
    Tagged<Object> obj = allocation.ToObjectChecked();
    Tagged<HeapObject> ho = Cast<HeapObject>(obj);
    CHECK(new_space->Contains(ho));
  }
  CHECK_LT(0, successful_allocations);

  new_space.reset();
  memory_allocator->pool()->ReleasePooledChunks();
}

TEST(PagedNewSpace) {
  if (v8_flags.single_generation) return;
  ManualGCScope manual_gc_scope;
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  TestMemoryAllocatorScope test_allocator_scope(isolate, heap->MaxReserved());
  MemoryAllocator* memory_allocator = test_allocator_scope.allocator();
  LinearAllocationArea allocation_info;

  auto new_space = std::make_unique<PagedNewSpace>(
      heap, heap->InitialSemiSpaceSize(), heap->InitialSemiSpaceSize());
  MainAllocator allocator(heap->main_thread_local_heap(), new_space.get(),
                          MainAllocator::IsNewGeneration::kYes,
                          &allocation_info);
  CHECK(new_space->MaximumCapacity());
  CHECK(new_space->EnsureCurrentCapacity());
  CHECK_LT(0, new_space->TotalCapacity());

  size_t successful_allocations = 0;
  while (true) {
    AllocationResult allocation = allocator.AllocateRaw(
        kMaxRegularHeapObjectSize, kTaggedAligned, AllocationOrigin::kRuntime);
    if (allocation.IsFailure()) break;
    successful_allocations++;
    Tagged<Object> obj = allocation.ToObjectChecked();
    Tagged<HeapObject> ho = Cast<HeapObject>(obj);
    CHECK(new_space->Contains(ho));
  }
  CHECK_LT(0, successful_allocations);

  new_space.reset();
  memory_allocator->pool()->ReleasePooledChunks();
}

TEST(OldSpace) {
  v8_flags.max_heap_size = 20;
  // This test uses its own old space, which confuses the incremental marker.
  v8_flags.incremental_marking = false;
  // This test doesn't expect GCs caused by concurrent allocations in the
  // background thread.
  v8_flags.stress_concurrent_allocation = false;

  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  TestMemoryAllocatorScope test_allocator_scope(isolate, heap->MaxReserved());
  LinearAllocationArea allocation_info;

  auto old_space = std::make_unique<OldSpace>(heap);
  MainAllocator allocator(heap->main_thread_local_heap(), old_space.get(),
                          MainAllocator::IsNewGeneration::kNo,
                          &allocation_info);
  const int obj_size = kMaxRegularHeapObjectSize;

  size_t successful_allocations = 0;

  while (true) {
    AllocationResult allocation = allocator.AllocateRaw(
        obj_size, kTaggedAligned, AllocationOrigin::kRuntime);
    if (allocation.IsFailure()) break;
    successful_allocations++;
    Tagged<Object> obj = allocation.ToObjectChecked();
    Tagged<HeapObject> ho = Cast<HeapObject>(obj);
    CHECK(old_space->Contains(ho));
  }
  CHECK_LT(0, successful_allocations);
}

TEST(OldLargeObjectSpace) {
  v8_flags.max_heap_size = 20;
  // This test uses its own old large object space, which confuses the
  // incremental marker.
  v8_flags.incremental_marking = false;
  // This test doesn't expect GCs caused by concurrent allocations in the
  // background thread.
  v8_flags.stress_concurrent_allocation = false;

  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  auto lo = std::make_unique<OldLargeObjectSpace>(heap);
  const int lo_size = PageMetadata::kPageSize;

  HandleScope handle_scope(isolate);
  Tagged<Map> map = ReadOnlyRoots(isolate).fixed_double_array_map();
  size_t successful_allocations = 0;

  while (true) {
    AllocationResult allocation =
        lo->AllocateRaw(heap->main_thread_local_heap(), lo_size);
    if (allocation.IsFailure()) break;
    successful_allocations++;
    Tagged<Object> obj = allocation.ToObjectChecked();
    CHECK(IsHeapObject(obj));
    Tagged<HeapObject> ho = Cast<HeapObject>(obj);
    CHECK(lo->Contains(ho));
    CHECK_EQ(0, Heap::GetFillToAlign(ho.address(), kTaggedAligned));
    // All large objects have the same alignment because they start at the
    // same offset within a page. Fixed double arrays have the most strict
    // alignment requirements.
    CHECK_EQ(0, Heap::GetFillToAlign(ho.address(),
                                     HeapObject::RequiredAlignment(map)));
    DirectHandle<HeapObject> keep_alive(ho, isolate);
  }
  CHECK_LT(0, successful_allocations);

  CHECK(!lo->IsEmpty());
  CHECK(lo->AllocateRaw(heap->main_thread_local_heap(), lo_size).IsFailure());
}

#ifndef DEBUG
// The test verifies that committed size of a space is less then some threshold.
// Debug builds pull in all sorts of additional instrumentation that increases
// heap sizes. E.g. CSA_DCHECK creates on-heap strings for error messages. These
// messages are also not stable if files are moved and modified during the build
// process (jumbo builds).
TEST(SizeOfInitialHeap) {
  ManualGCScope manual_gc_scope;
  if (i::v8_flags.always_turbofan) return;
  // Bootstrapping without a snapshot causes more allocations.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  if (!isolate->snapshot_available()) return;
  HandleScope scope(isolate);
  v8::Local<v8::Context> context = CcTest::isolate()->GetCurrentContext();
  // Skip this test on the custom snapshot builder.
  if (!CcTest::global()
           ->Get(context, v8_str("assertEquals"))
           .ToLocalChecked()
           ->IsUndefined()) {
    return;
  }
  // Initial size of LO_SPACE
  size_t initial_lo_space = isolate->heap()->lo_space()->Size();

// The limit for each space for an empty isolate containing just the
// snapshot.
// In PPC the page size is 64K, causing more internal fragmentation
// hence requiring a larger limit.
#if V8_OS_LINUX && (V8_HOST_ARCH_PPC || V8_HOST_ARCH_PPC64)
  const size_t kMaxInitialSizePerSpace = 3 * MB;
#else
  const size_t kMaxInitialSizePerSpace = 2 * MB;
#endif

  // Freshly initialized VM gets by with the snapshot size (which is below
  // kMaxInitialSizePerSpace per space).
  Heap* heap = isolate->heap();
  for (int i = FIRST_GROWABLE_PAGED_SPACE; i <= LAST_GROWABLE_PAGED_SPACE;
       i++) {
    if (!heap->paged_space(i)) continue;

    // Debug code can be very large, so skip CODE_SPACE if we are generating it.
    if (i == CODE_SPACE && i::v8_flags.debug_code) continue;

    // Check that the initial heap is also below the limit.
    CHECK_LE(heap->paged_space(i)->CommittedMemory(), kMaxInitialSizePerSpace);
  }

  CompileRun("/*empty*/");

  // No large objects required to perform the above steps.
  CHECK_EQ(initial_lo_space,
           static_cast<size_t>(isolate->heap()->lo_space()->Size()));
}
#endif  // DEBUG

class Observer : public AllocationObserver {
 public:
  explicit Observer(intptr_t step_size)
      : AllocationObserver(step_size), count_(0) {}

  void Step(int bytes_allocated, Address addr, size_t) override { count_++; }

  int count() const { return count_; }

 private:
  int count_;
};

HEAP_TEST(Regress777177) {
  v8_flags.stress_concurrent_allocation = false;  // For SimulateFullSpace.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  OldSpace* old_space = heap->old_space();
  MainAllocator* old_space_allocator = heap->allocator()->old_space_allocator();
  Observer observer(128);
  old_space_allocator->FreeLinearAllocationArea();
  old_space_allocator->AddAllocationObserver(&observer);

  int area_size = old_space->AreaSize();
  int max_object_size = kMaxRegularHeapObjectSize;
  int filler_size = area_size - max_object_size;

  {
    // Ensure a new linear allocation area on a fresh page.
    AlwaysAllocateScopeForTesting always_allocate(heap);
    heap::SimulateFullSpace(old_space);
    AllocationResult result = old_space_allocator->AllocateRaw(
        filler_size, kTaggedAligned, AllocationOrigin::kRuntime);
    Tagged<HeapObject> obj = result.ToObjectChecked();
    heap->CreateFillerObjectAt(obj.address(), filler_size);
  }

  {
    // Allocate all bytes of the linear allocation area. This moves top_ and
    // top_on_previous_step_ to the next page.
    AllocationResult result = old_space_allocator->AllocateRaw(
        max_object_size, kTaggedAligned, AllocationOrigin::kRuntime);
    Tagged<HeapObject> obj = result.ToObjectChecked();
    // Simulate allocation folding moving the top pointer back.
    old_space_allocator->ResetLab(
        obj.address(), heap->allocator()->old_space_allocator()->limit(),
        heap->allocator()->old_space_allocator()->limit());
  }

  {
    // This triggers assert in crbug.com/777177.
    AllocationResult result = old_space_allocator->AllocateRaw(
        filler_size, kTaggedAligned, AllocationOrigin::kRuntime);
    Tagged<HeapObject> obj = result.ToObjectChecked();
    heap->CreateFillerObjectAt(obj.address(), filler_size);
  }
  old_space_allocator->RemoveAllocationObserver(&observer);
}

HEAP_TEST(Regress791582) {
  if (v8_flags.single_generation) return;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();
  HandleScope scope(isolate);
  MainAllocator* new_space_allocator = heap->allocator()->new_space_allocator();
  GrowNewSpace(heap);

  int until_page_end =
      static_cast<int>(heap->NewSpaceLimit() - heap->NewSpaceTop());

  if (!IsAligned(until_page_end, kTaggedSize)) {
    // The test works if the size of allocation area size is a multiple of
    // pointer size. This is usually the case unless some allocation observer
    // is already active (e.g. incremental marking observer).
    return;
  }

  Observer observer(128);
  new_space_allocator->FreeLinearAllocationArea();
  new_space_allocator->AddAllocationObserver(&observer);

  {
    AllocationResult result = new_space_allocator->AllocateRaw(
        until_page_end, kTaggedAligned, AllocationOrigin::kRuntime);
    Tagged<HeapObject> obj = result.ToObjectChecked();
    heap->CreateFillerObjectAt(obj.address(), until_page_end);
    // Simulate allocation folding moving the top pointer back.
    *heap->NewSpaceAllocationTopAddress() = obj.address();
  }

  {
    // This triggers assert in crbug.com/791582
    AllocationResult result = new_space_allocator->AllocateRaw(
        256, kTaggedAligned, AllocationOrigin::kRuntime);
    Tagged<HeapObject> obj = result.ToObjectChecked();
    heap->CreateFillerObjectAt(obj.address(), 256);
  }
  new_space_allocator->RemoveAllocationObserver(&observer);
}

TEST(ShrinkPageToHighWaterMarkFreeSpaceEnd) {
  v8_flags.stress_incremental_marking = false;
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  heap::SealCurrentObjects(CcTest::heap());

  // Prepare page that only contains a single object and a trailing FreeSpace
  // filler.
  DirectHandle<FixedArray> array =
      isolate->factory()->NewFixedArray(128, AllocationType::kOld);
  PageMetadata* page = PageMetadata::FromHeapObject(*array);

  // Reset space so high water mark is consistent.
  PagedSpace* old_space = CcTest::heap()->old_space();
  CcTest::heap()->FreeMainThreadLinearAllocationAreas();
  old_space->ResetFreeList();

  Tagged<HeapObject> filler =
      HeapObject::FromAddress(array->address() + array->Size());
  CHECK(IsFreeSpace(filler));
  size_t shrunk = old_space->ShrinkPageToHighWaterMark(page);
  size_t should_have_shrunk = RoundDown(
      static_cast<size_t>(MemoryChunkLayout::AllocatableMemoryInDataPage() -
                          array->Size()),
      CommitPageSize());
  CHECK_EQ(should_have_shrunk, shrunk);
}

TEST(ShrinkPageToHighWaterMarkNoFiller) {
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  heap::SealCurrentObjects(CcTest::heap());

  const int kFillerSize = 0;
  std::vector<Handle<FixedArray>> arrays =
      heap::FillOldSpacePageWithFixedArrays(CcTest::heap(), kFillerSize);
  DirectHandle<FixedArray> array = arrays.back();
  PageMetadata* page = PageMetadata::FromHeapObject(*array);
  CHECK_EQ(page->area_end(), array->address() + array->Size() + kFillerSize);

  // Reset space so high water mark and fillers are consistent.
  PagedSpace* old_space = CcTest::heap()->old_space();
  CcTest::heap()->FreeMainThreadLinearAllocationAreas();
  old_space->ResetFreeList();

  size_t shrunk = old_space->ShrinkPageToHighWaterMark(page);
  CHECK_EQ(0u, shrunk);
}

TEST(ShrinkPageToHighWaterMarkOneWordFiller) {
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  heap::SealCurrentObjects(CcTest::heap());

  const int kFillerSize = kTaggedSize;
  std::vector<Handle<FixedArray>> arrays =
      heap::FillOldSpacePageWithFixedArrays(CcTest::heap(), kFillerSize);
  DirectHandle<FixedArray> array = arrays.back();
  PageMetadata* page = PageMetadata::FromHeapObject(*array);
  CHECK_EQ(page->area_end(), array->address() + array->Size() + kFillerSize);

  // Reset space so high water mark and fillers are consistent.
  PagedSpace* old_space = CcTest::heap()->old_space();
  CcTest::heap()->FreeMainThreadLinearAllocationAreas();
  old_space->ResetFreeList();

  Tagged<HeapObject> filler =
      HeapObject::FromAddress(array->address() + array->Size());
  CHECK_EQ(filler->map(),
           ReadOnlyRoots(CcTest::heap()).one_pointer_filler_map());

  size_t shrunk = old_space->ShrinkPageToHighWaterMark(page);
  CHECK_EQ(0u, shrunk);
}

TEST(ShrinkPageToHighWaterMarkTwoWordFiller) {
  v8_flags.stress_concurrent_allocation = false;  // For SealCurrentObjects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  heap::SealCurrentObjects(CcTest::heap());

  const int kFillerSize = 2 * kTaggedSize;
  std::vector<Handle<FixedArray>> arrays =
      heap::FillOldSpacePageWithFixedArrays(CcTest::heap(), kFillerSize);
  DirectHandle<FixedArray> array = arrays.back();
  PageMetadata* page = PageMetadata::FromHeapObject(*array);
  CHECK_EQ(page->area_end(), array->address() + array->Size() + kFillerSize);

  // Reset space so high water mark and fillers are consistent.
  PagedSpace* old_space = CcTest::heap()->old_space();
  CcTest::heap()->FreeMainThreadLinearAllocationAreas();
  old_space->ResetFreeList();

  Tagged<HeapObject> filler =
      HeapObject::FromAddress(array->address() + array->Size());
  CHECK_EQ(filler->map(),
           ReadOnlyRoots(CcTest::heap()).two_pointer_filler_map());

  size_t shrunk = old_space->ShrinkPageToHighWaterMark(page);
  CHECK_EQ(0u, shrunk);
}

namespace {
// PageAllocator that always fails.
class FailingPageAllocator : public v8::PageAllocator {
 public:
  size_t AllocatePageSize() override { return 1024; }
  size_t CommitPageSize() override { return 1024; }
  void SetRandomMmapSeed(int64_t seed) override {}
  void* GetRandomMmapAddr() override { return nullptr; }
  void* AllocatePages(void* address, size_t length, size_t alignment,
                      Permission permissions) override {
    return nullptr;
  }
  bool FreePages(void* address, size_t length) override { return false; }
  bool ReleasePages(void* address, size_t length, size_t new_length) override {
    return false;
  }
  bool SetPermissions(void* address, size_t length,
                      Permission permissions) override {
    return false;
  }
  bool RecommitPages(void* address, size_t length,
                     Permission permissions) override {
    return false;
  }
  bool DecommitPages(void* address, size_t length) override { return false; }
};
}  // namespace

TEST(NoMemoryForNewPage) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  // Memory allocator that will fail to allocate any pages.
  FailingPageAllocator failing_allocator;
  TestMemoryAllocatorScope test_allocator_scope(isolate, 0, &failing_allocator);
  MemoryAllocator* memory_allocator = test_allocator_scope.allocator();
  OldSpace faked_space(heap);
  PageMetadata* page = memory_allocator->AllocatePage(
      MemoryAllocator::AllocationMode::kRegular,
      static_cast<PagedSpace*>(&faked_space), NOT_EXECUTABLE);

  CHECK_NULL(page);
}

namespace {
// ReadOnlySpace cannot be torn down by a destructor because the destructor
// cannot take an argument. Since these tests create ReadOnlySpaces not attached
// to the Heap directly, they need to be destroyed to ensure the
// MemoryAllocator's stats are all 0 at exit.
class V8_NODISCARD ReadOnlySpaceScope {
 public:
  explicit ReadOnlySpaceScope(Heap* heap) : ro_space_(heap) {}
  ~ReadOnlySpaceScope() {
    ro_space_.TearDown(CcTest::heap()->memory_allocator());
  }

  ReadOnlySpace* space() { return &ro_space_; }

 private:
  ReadOnlySpace ro_space_;
};
}  // namespace

TEST(ReadOnlySpaceMetrics_OnePage) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  // Create a read-only space and allocate some memory, shrink the pages and
  // check the allocated object size is as expected.

  ReadOnlySpaceScope scope(heap);
  ReadOnlySpace* faked_space = scope.space();

  // Initially no memory.
  CHECK_EQ(faked_space->Size(), 0);
  CHECK_EQ(faked_space->Capacity(), 0);
  CHECK_EQ(faked_space->CommittedMemory(), 0);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(), 0);

  faked_space->AllocateRaw(16, kTaggedAligned);

  faked_space->ShrinkPages();
  faked_space->Seal(ReadOnlySpace::SealMode::kDoNotDetachFromHeap);

  // Allocated objects size.
  CHECK_EQ(faked_space->Size(), 16);

  size_t committed_memory =
      RoundUp(MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage() +
                  faked_space->Size(),
              MemoryAllocator::GetCommitPageSize());

  // Amount of OS allocated memory.
  CHECK_EQ(faked_space->CommittedMemory(), committed_memory);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(), committed_memory);

  // Capacity will be one OS page minus the page header.
  CHECK_EQ(
      faked_space->Capacity(),
      committed_memory - MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage());
}

TEST(ReadOnlySpaceMetrics_AlignedAllocations) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  // Create a read-only space and allocate some memory, shrink the pages and
  // check the allocated object size is as expected.

  ReadOnlySpaceScope scope(heap);
  ReadOnlySpace* faked_space = scope.space();

  // Initially no memory.
  CHECK_EQ(faked_space->Size(), 0);
  CHECK_EQ(faked_space->Capacity(), 0);
  CHECK_EQ(faked_space->CommittedMemory(), 0);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(), 0);

  // Allocate an object just under an OS page in size.
  int object_size =
      static_cast<int>(MemoryAllocator::GetCommitPageSize() - kApiTaggedSize);

  int alignment = USE_ALLOCATION_ALIGNMENT_BOOL ? kDoubleSize : kTaggedSize;

  Tagged<HeapObject> object =
      faked_space->AllocateRaw(object_size, kDoubleAligned).ToObjectChecked();
  CHECK_EQ(object.address() % alignment, 0);
  object =
      faked_space->AllocateRaw(object_size, kDoubleAligned).ToObjectChecked();
  CHECK_EQ(object.address() % alignment, 0);

  // Calculate size of allocations based on area_start.
  Address area_start = faked_space->pages().back()->GetAreaStart();
  Address top = RoundUp(area_start, alignment) + object_size;
  top = RoundUp(top, alignment) + object_size;
  size_t expected_size = top - area_start;

  faked_space->ShrinkPages();
  faked_space->Seal(ReadOnlySpace::SealMode::kDoNotDetachFromHeap);

  // Allocated objects size may will contain 4 bytes of padding on 32-bit or
  // with pointer compression.
  CHECK_EQ(faked_space->Size(), expected_size);

  size_t committed_memory =
      RoundUp(MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage() +
                  faked_space->Size(),
              MemoryAllocator::GetCommitPageSize());

  CHECK_EQ(faked_space->CommittedMemory(), committed_memory);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(), committed_memory);

  // Capacity will be 3 OS pages minus the page header.
  CHECK_EQ(
      faked_space->Capacity(),
      committed_memory - MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage());
}

TEST(ReadOnlySpaceMetrics_TwoPages) {
  Isolate* isolate = CcTest::i_isolate();
  Heap* heap = isolate->heap();

  // Create a read-only space and allocate some memory, shrink the pages and
  // check the allocated object size is as expected.

  ReadOnlySpaceScope scope(heap);
  ReadOnlySpace* faked_space = scope.space();

  // Initially no memory.
  CHECK_EQ(faked_space->Size(), 0);
  CHECK_EQ(faked_space->Capacity(), 0);
  CHECK_EQ(faked_space->CommittedMemory(), 0);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(), 0);

  // Allocate an object that's too big to have more than one on a page.

  int object_size = RoundUp(
      static_cast<int>(
          MemoryChunkLayout::AllocatableMemoryInMemoryChunk(RO_SPACE) / 2 + 16),
      kTaggedSize);
  CHECK_GT(object_size * 2,
           MemoryChunkLayout::AllocatableMemoryInMemoryChunk(RO_SPACE));
  faked_space->AllocateRaw(object_size, kTaggedAligned);

  // Then allocate another so it expands the space to two pages.
  faked_space->AllocateRaw(object_size, kTaggedAligned);

  faked_space->ShrinkPages();
  faked_space->Seal(ReadOnlySpace::SealMode::kDoNotDetachFromHeap);

  // Allocated objects size.
  CHECK_EQ(faked_space->Size(), object_size * 2);

  // Amount of OS allocated memory.
  size_t committed_memory_per_page = RoundUp(
      MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage() + object_size,
      MemoryAllocator::GetCommitPageSize());
  CHECK_EQ(faked_space->CommittedMemory(), 2 * committed_memory_per_page);
  CHECK_EQ(faked_space->CommittedPhysicalMemory(),
           2 * committed_memory_per_page);

  // Capacity will be the space up to the amount of committed memory minus the
  // page headers.
  size_t capacity_per_page =
      RoundUp(
          MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage() + object_size,
          MemoryAllocator::GetCommitPageSize()) -
      MemoryChunkLayout::ObjectStartOffsetInReadOnlyPage();
  CHECK_EQ(faked_space->Capacity(), 2 * capacity_per_page);
}

}  // namespace heap
}  // namespace internal
}  // namespace v8
