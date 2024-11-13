// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/conservative-stack-visitor.h"
#include "src/heap/gc-tracer.h"
#include "test/unittests/heap/heap-utils.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

namespace {

constexpr int FullCell = MarkingBitmap::kBitsPerCell * kTaggedSize;

template <typename TMixin>
class WithInnerPointerResolutionMixin : public TMixin {
 public:
  Address ResolveInnerPointer(Address maybe_inner_ptr) {
    // This can only resolve inner pointers in the regular cage.
    PtrComprCageBase cage_base{this->isolate()};
    return ConservativeStackVisitor::ForTesting(
               this->isolate(), GarbageCollector::MARK_COMPACTOR)
        .FindBasePtr(maybe_inner_ptr, cage_base);
  }
};

class InnerPointerResolutionTest
    : public WithInnerPointerResolutionMixin<TestWithIsolate> {
 public:
  struct ObjectRequest {
    int size;  // The only required field.
    enum { REGULAR, FREE, LARGE } type = REGULAR;
    enum { UNMARKED, MARKED, MARKED_AREA } marked = UNMARKED;
    // If index_in_cell >= 0, the object is placed at the lowest address s.t.
    // MarkingBitmap::IndexInCell(MarkingBitmap::AddressToIndex(address)) ==
    // index_in_cell. To achieve this, padding (i.e., introducing a free-space
    // object of the appropriate size) may be necessary. If padding ==
    // CONSECUTIVE, no such padding is allowed and it is just checked that
    // object layout is as intended.
    int index_in_cell = -1;
    enum { CONSECUTIVE, PAD_UNMARKED, PAD_MARKED } padding = CONSECUTIVE;
    // The id of the page on which the object was allocated and its address are
    // stored here.
    int page_id = -1;
    Address address = kNullAddress;
  };

  InnerPointerResolutionTest() = default;

  ~InnerPointerResolutionTest() override {
    for (auto [id, page] : pages_)
      allocator()->Free(MemoryAllocator::FreeMode::kImmediately, page);
  }

  InnerPointerResolutionTest(const InnerPointerResolutionTest&) = delete;
  InnerPointerResolutionTest& operator=(const InnerPointerResolutionTest&) =
      delete;

  Heap* heap() { return isolate()->heap(); }
  MemoryAllocator* allocator() { return heap()->memory_allocator(); }

  // Create, free and lookup pages, normal or large.

  int CreateNormalPage() {
    OldSpace* old_space = heap()->old_space();
    DCHECK_NE(nullptr, old_space);
    auto* page = allocator()->AllocatePage(
        MemoryAllocator::AllocationMode::kRegular, old_space, NOT_EXECUTABLE);
    EXPECT_NE(nullptr, page);
    int page_id = next_page_id_++;
    DCHECK_EQ(pages_.end(), pages_.find(page_id));
    pages_[page_id] = page;
    return page_id;
  }

  int CreateLargePage(size_t size) {
    OldLargeObjectSpace* lo_space = heap()->lo_space();
    EXPECT_NE(nullptr, lo_space);
    LargePageMetadata* page =
        allocator()->AllocateLargePage(lo_space, size, NOT_EXECUTABLE);
    EXPECT_NE(nullptr, page);
    int page_id = next_page_id_++;
    DCHECK_EQ(pages_.end(), pages_.find(page_id));
    pages_[page_id] = page;
    return page_id;
  }

  void FreePage(int page_id) {
    DCHECK_LE(0, page_id);
    auto it = pages_.find(page_id);
    DCHECK_NE(pages_.end(), it);
    allocator()->Free(MemoryAllocator::FreeMode::kImmediately, it->second);
    pages_.erase(it);
  }

  MutablePageMetadata* LookupPage(int page_id) {
    DCHECK_LE(0, page_id);
    auto it = pages_.find(page_id);
    DCHECK_NE(pages_.end(), it);
    return it->second;
  }

  bool IsPageAlive(int page_id) {
    DCHECK_LE(0, page_id);
    return pages_.find(page_id) != pages_.end();
  }

  // Creates a list of objects in a page and ensures that the page is iterable.
  int CreateObjectsInPage(const std::vector<ObjectRequest>& objects) {
    int page_id = CreateNormalPage();
    MutablePageMetadata* page = LookupPage(page_id);
    Address ptr = page->area_start();
    for (auto object : objects) {
      DCHECK_NE(ObjectRequest::LARGE, object.type);
      DCHECK_EQ(0, object.size % kTaggedSize);

      // Check if padding is needed.
      int index_in_cell =
          MarkingBitmap::IndexInCell(MarkingBitmap::AddressToIndex(ptr));
      if (object.index_in_cell < 0) {
        object.index_in_cell = index_in_cell;
      } else if (object.padding != ObjectRequest::CONSECUTIVE) {
        DCHECK_LE(0, object.index_in_cell);
        DCHECK_GT(MarkingBitmap::kBitsPerCell, object.index_in_cell);
        const int needed_padding_size =
            ((MarkingBitmap::kBitsPerCell + object.index_in_cell -
              index_in_cell) %
             MarkingBitmap::kBitsPerCell) *
            kTaggedSize;
        if (needed_padding_size > 0) {
          ObjectRequest pad{needed_padding_size,
                            ObjectRequest::FREE,
                            object.padding == ObjectRequest::PAD_MARKED
                                ? ObjectRequest::MARKED_AREA
                                : ObjectRequest::UNMARKED,
                            index_in_cell,
                            ObjectRequest::CONSECUTIVE,
                            page_id,
                            ptr};
          ptr += needed_padding_size;
          DCHECK_LE(ptr, page->area_end());
          CreateObject(pad);
          index_in_cell =
              MarkingBitmap::IndexInCell(MarkingBitmap::AddressToIndex(ptr));
        }
      }

      // This will fail if the marking bitmap's implementation parameters change
      // (e.g., MarkingBitmap::kBitsPerCell) or the size of the page header
      // changes. In this case, the tests will need to be revised accordingly.
      EXPECT_EQ(index_in_cell, object.index_in_cell);

      object.page_id = page_id;
      object.address = ptr;
      ptr += object.size;
      DCHECK_LE(ptr, page->area_end());
      CreateObject(object);
    }

    // Create one last object that uses the remaining space on the page; this
    // simulates freeing the page's LAB.
    const int remaining_size = static_cast<int>(page->area_end() - ptr);
    const auto index = MarkingBitmap::AddressToIndex(ptr);
    const auto index_in_cell = MarkingBitmap::IndexInCell(index);
    ObjectRequest last{remaining_size,
                       ObjectRequest::FREE,
                       ObjectRequest::UNMARKED,
                       static_cast<int>(index_in_cell),
                       ObjectRequest::CONSECUTIVE,
                       page_id,
                       ptr};
    CreateObject(last);
    return page_id;
  }

  std::vector<int> CreateLargeObjects(
      const std::vector<ObjectRequest>& objects) {
    std::vector<int> result;
    for (auto object : objects) {
      DCHECK_EQ(ObjectRequest::LARGE, object.type);
      int page_id = CreateLargePage(object.size);
      MutablePageMetadata* page = LookupPage(page_id);
      object.page_id = page_id;
      object.address = page->area_start();
      CHECK_EQ(object.address + object.size, page->area_end());
      CreateObject(object);
      result.push_back(page_id);
    }
    return result;
  }

  void CreateObject(const ObjectRequest& object) {
    objects_.push_back(object);

    // "Allocate" (i.e., manually place) the object in the page, set the map
    // and the size.
    switch (object.type) {
      case ObjectRequest::REGULAR:
      case ObjectRequest::LARGE: {
        DCHECK_LE(2 * kTaggedSize, object.size);
        ReadOnlyRoots roots(heap());
        Tagged<HeapObject> heap_object(HeapObject::FromAddress(object.address));
        heap_object->set_map_after_allocation(roots.unchecked_fixed_array_map(),
                                              SKIP_WRITE_BARRIER);
        Tagged<FixedArray> arr(Cast<FixedArray>(heap_object));
        arr->set_length((object.size - FixedArray::SizeFor(0)) / kTaggedSize);
        DCHECK_EQ(object.size, arr->AllocatedSize());
        break;
      }
      case ObjectRequest::FREE:
        heap()->CreateFillerObjectAt(object.address, object.size);
        break;
    }

    // Mark the object in the bitmap, if necessary.
    switch (object.marked) {
      case ObjectRequest::UNMARKED:
        break;
      case ObjectRequest::MARKED:
        heap()->marking_state()->TryMark(
            HeapObject::FromAddress(object.address));
        break;
      case ObjectRequest::MARKED_AREA: {
        MutablePageMetadata* page = LookupPage(object.page_id);
        page->marking_bitmap()->SetRange<AccessMode::NON_ATOMIC>(
            MarkingBitmap::AddressToIndex(object.address),
            MarkingBitmap::LimitAddressToIndex(object.address + object.size));
        break;
      }
    }
  }

  // This must be called with a created object and an offset inside it.
  void RunTestInside(const ObjectRequest& object, int offset) {
    DCHECK_LE(0, offset);
    DCHECK_GT(object.size, offset);
    Address base_ptr = ResolveInnerPointer(object.address + offset);
    bool should_return_null =
        !IsPageAlive(object.page_id) || object.type == ObjectRequest::FREE;
    if (should_return_null)
      EXPECT_EQ(kNullAddress, base_ptr);
    else
      EXPECT_EQ(object.address, base_ptr);
  }

  // This must be called with an address not contained in any created object.
  void RunTestOutside(Address ptr) {
    Address base_ptr = ResolveInnerPointer(ptr);
    EXPECT_EQ(kNullAddress, base_ptr);
  }

  void TestAll() {
    for (auto object : objects_) {
      RunTestInside(object, 0);
      RunTestInside(object, 1);
      RunTestInside(object, object.size / 2);
      RunTestInside(object, object.size - 1);
    }
    for (auto [id, page] : pages_) {
      const Address outside_ptr = page->ChunkAddress() + 1;
      DCHECK_LE(page->ChunkAddress(), outside_ptr);
      RunTestOutside(outside_ptr);
    }
    RunTestOutside(kNullAddress);
    RunTestOutside(static_cast<Address>(42));
    RunTestOutside(static_cast<Address>(kZapValue));
  }

 private:
  std::map<int, MutablePageMetadata*> pages_;
  int next_page_id_ = 0;
  std::vector<ObjectRequest> objects_;
};

}  // namespace

TEST_F(InnerPointerResolutionTest, EmptyPage) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({});
  TestAll();
}

// Tests with some objects laid out randomly.

TEST_F(InnerPointerResolutionTest, NothingMarked) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize},
      {12 * kTaggedSize},
      {13 * kTaggedSize},
      {128 * kTaggedSize},
      {1 * kTaggedSize, ObjectRequest::FREE},
      {15 * kTaggedSize},
      {2 * kTaggedSize, ObjectRequest::FREE},
      {2 * kTaggedSize},
      {10544 * kTaggedSize},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, AllMarked) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, SomeMarked) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, MarkedAreas) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  TestAll();
}

// Tests with specific object layout, to cover interesting and corner cases.

TEST_F(InnerPointerResolutionTest, ThreeMarkedObjectsInSameCell) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      // Some initial large unmarked object, followed by a small marked object
      // towards the end of the cell.
      {128 * kTaggedSize},
      {5 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED, 20,
       ObjectRequest::PAD_UNMARKED},
      // Then three marked objects in the same cell.
      {8 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED, 3,
       ObjectRequest::PAD_UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED, 11},
      {5 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED, 23},
      // This marked object is in the next cell.
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED, 17,
       ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, ThreeMarkedAreasInSameCell) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      // Some initial large unmarked object, followed by a small marked area
      // towards the end of the cell.
      {128 * kTaggedSize},
      {5 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 20,
       ObjectRequest::PAD_UNMARKED},
      // Then three marked areas in the same cell.
      {8 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 3,
       ObjectRequest::PAD_UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA,
       11},
      {5 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 23},
      // This marked area is in the next cell.
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 17,
       ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, SmallMarkedAreaAtPageStart) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 30,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest,
       SmallMarkedAreaAtPageStartUntilCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 0,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, LargeMarkedAreaAtPageStart) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {42 * FullCell, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 30,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest,
       LargeMarkedAreaAtPageStartUntilCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {42 * FullCell, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 0,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, SmallMarkedAreaStartingAtCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {5 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 0,
       ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, LargeMarkedAreaStartingAtCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {42 * FullCell + 16 * kTaggedSize, ObjectRequest::REGULAR,
       ObjectRequest::MARKED_AREA, 0, ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, SmallMarkedAreaEndingAtCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 13,
       ObjectRequest::PAD_UNMARKED},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 0,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, LargeMarkedAreaEndingAtCellBoundary) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {42 * FullCell + 16 * kTaggedSize, ObjectRequest::REGULAR,
       ObjectRequest::MARKED_AREA, 0, ObjectRequest::PAD_UNMARKED},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 0,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, TwoSmallMarkedAreasAtCellBoundaries) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {6 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 0,
       ObjectRequest::PAD_UNMARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 25,
       ObjectRequest::PAD_UNMARKED},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED, 0,
       ObjectRequest::PAD_MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, MarkedAreaOfOneCell) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {1 * FullCell, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 0,
       ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, MarkedAreaOfManyCells) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {128 * kTaggedSize},
      {17 * FullCell, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA, 0,
       ObjectRequest::PAD_UNMARKED},
  });
  TestAll();
}

// Test with more pages, normal and large.

TEST_F(InnerPointerResolutionTest, TwoPages) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  CreateObjectsInPage({
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, OneLargePage) {
  if (v8_flags.enable_third_party_heap) return;
  CreateLargeObjects({
      {1 * MB, ObjectRequest::LARGE, ObjectRequest::UNMARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, SeveralLargePages) {
  if (v8_flags.enable_third_party_heap) return;
  CreateLargeObjects({
      {1 * MB, ObjectRequest::LARGE, ObjectRequest::UNMARKED},
      {32 * MB, ObjectRequest::LARGE, ObjectRequest::MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, PagesOfBothKind) {
  if (v8_flags.enable_third_party_heap) return;
  CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  CreateObjectsInPage({
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  CreateLargeObjects({
      {1 * MB, ObjectRequest::LARGE, ObjectRequest::UNMARKED},
      {32 * MB, ObjectRequest::LARGE, ObjectRequest::MARKED},
  });
  TestAll();
}

TEST_F(InnerPointerResolutionTest, FreePages) {
  if (v8_flags.enable_third_party_heap) return;
  int some_normal_page = CreateObjectsInPage({
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {10544 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  CreateObjectsInPage({
      {128 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {16 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {12 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED_AREA},
      {13 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
      {1 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::FREE, ObjectRequest::MARKED},
      {2 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::UNMARKED},
      {15 * kTaggedSize, ObjectRequest::REGULAR, ObjectRequest::MARKED},
  });
  auto large_pages = CreateLargeObjects({
      {1 * MB, ObjectRequest::LARGE, ObjectRequest::UNMARKED},
      {32 * MB, ObjectRequest::LARGE, ObjectRequest::MARKED},
  });
  TestAll();
  FreePage(some_normal_page);
  TestAll();
  FreePage(large_pages[0]);
  TestAll();
}

using InnerPointerResolutionHeapTest =
    WithInnerPointerResolutionMixin<TestWithHeapInternalsAndContext>;

TEST_F(InnerPointerResolutionHeapTest, UnusedRegularYoungPages) {
  ManualGCScope manual_gc_scope(isolate());
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());

  Persistent<v8::FixedArray> weak1, weak2, strong;
  Address inner_ptr1, inner_ptr2, inner_ptr3, outside_ptr1, outside_ptr2;
  MemoryChunk *page1, *page2;

  auto allocator = heap()->memory_allocator();

  {
    PtrComprCageBase cage_base{isolate()};
    HandleScope scope(isolate());

    // Allocate two objects, large enough that they fall in two different young
    // generation pages. Keep weak references to these objects.
    const int length =
        (heap()->MaxRegularHeapObjectSize(AllocationType::kYoung) -
         FixedArray::SizeFor(0)) /
        kTaggedSize;
    auto h1 = factory()->NewFixedArray(length, AllocationType::kYoung);
    auto h2 = factory()->NewFixedArray(length, AllocationType::kYoung);
    weak1.Reset(v8_isolate(), Utils::FixedArrayToLocal(h1));
    weak2.Reset(v8_isolate(), Utils::FixedArrayToLocal(h2));
    weak1.SetWeak();
    weak2.SetWeak();
    auto obj1 = *h1;
    auto obj2 = *h2;
    page1 = MemoryChunk::FromHeapObject(obj1);
    EXPECT_TRUE(!page1->IsLargePage());
    EXPECT_TRUE(v8_flags.minor_ms || page1->IsToPage());
    page2 = MemoryChunk::FromHeapObject(obj2);
    EXPECT_TRUE(!page2->IsLargePage());
    EXPECT_TRUE(v8_flags.minor_ms || page2->IsToPage());
    EXPECT_NE(page1, page2);

    // Allocate one more object, small enough that it fits in either page1 or
    // page2. Keep a strong reference to this object.
    auto h3 = factory()->NewFixedArray(16, AllocationType::kYoung);
    strong.Reset(v8_isolate(), Utils::FixedArrayToLocal(h3));
    auto obj3 = *h3;
    auto page3 = MemoryChunk::FromHeapObject(obj3);
    EXPECT_TRUE(page3 == page1 || page3 == page2);
    if (page3 == page1) {
      EXPECT_EQ(obj3.address(), obj1.address() + obj1->Size(cage_base));
    } else {
      EXPECT_EQ(obj3.address(), obj2.address() + obj2->Size(cage_base));
    }

    // Keep inner pointers to all objects.
    inner_ptr1 = obj1.address() + 17 * kTaggedSize;
    inner_ptr2 = obj2.address() + 37 * kTaggedSize;
    inner_ptr3 = obj3.address() + 7 * kTaggedSize;

    // Keep pointers to the end of the pages, after the objects.
    outside_ptr1 = page1->Metadata()->area_end() - 3 * kTaggedSize;
    outside_ptr2 = page2->Metadata()->area_end() - 2 * kTaggedSize;
    EXPECT_LE(obj1.address() + obj1->Size(cage_base), outside_ptr1);
    EXPECT_LE(obj2.address() + obj2->Size(cage_base), outside_ptr2);
    if (page3 == page1) {
      EXPECT_LE(obj3.address() + obj3->Size(cage_base), outside_ptr1);
    } else {
      EXPECT_LE(obj3.address() + obj3->Size(cage_base), outside_ptr2);
    }

    // Ensure the young generation space is iterable.
    heap()
        ->allocator()
        ->new_space_allocator()
        ->MakeLinearAllocationAreaIterable();

    // Inner pointer resolution should work now, finding the objects in the
    // case of the inner pointers.
    EXPECT_EQ(obj1.address(), ResolveInnerPointer(inner_ptr1));
    EXPECT_EQ(obj2.address(), ResolveInnerPointer(inner_ptr2));
    EXPECT_EQ(obj3.address(), ResolveInnerPointer(inner_ptr3));
    EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr1));
    EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr2));

    // Start incremental marking and mark the third object.
    i::IncrementalMarking* marking = heap()->incremental_marking();
    if (marking->IsStopped()) {
      IsolateSafepointScope scope(heap());
      heap()->tracer()->StartCycle(
          GarbageCollector::MARK_COMPACTOR, GarbageCollectionReason::kTesting,
          "unit test", GCTracer::MarkingType::kIncremental);
      marking->Start(GarbageCollector::MARK_COMPACTOR,
                     i::GarbageCollectionReason::kTesting);
    }
    MarkingState* marking_state = heap()->marking_state();
    marking_state->TryMarkAndAccountLiveBytes(obj3);
  }

  // Garbage collection should reclaim the two large objects with the weak
  // references, but not the small one with the strong reference.
  InvokeAtomicMinorGC();
  EXPECT_TRUE(weak1.IsEmpty());
  EXPECT_TRUE(weak2.IsEmpty());
  EXPECT_TRUE(!strong.IsEmpty());
  // The two pages should still be around, in the new space.
  EXPECT_EQ(page1, allocator->LookupChunkContainingAddress(inner_ptr1));
  EXPECT_EQ(page2, allocator->LookupChunkContainingAddress(inner_ptr2));
  EXPECT_EQ(AllocationSpace::NEW_SPACE,
            MutablePageMetadata::cast(page1->Metadata())->owner_identity());
  EXPECT_EQ(AllocationSpace::NEW_SPACE,
            MutablePageMetadata::cast(page2->Metadata())->owner_identity());
  EXPECT_TRUE(v8_flags.minor_ms || page1->IsFromPage());
  EXPECT_TRUE(v8_flags.minor_ms || page2->IsFromPage());

  // Inner pointer resolution should work with pointers to unused young
  // generation pages (in case of the scavenger, the two pages are now in the
  // "from" semispace). There are no objects to be found.
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr1));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr2));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr3));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr1));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr2));

  // Garbage collection once more.
  InvokeAtomicMinorGC();
  EXPECT_EQ(AllocationSpace::NEW_SPACE,
            MutablePageMetadata::cast(page1->Metadata())->owner_identity());
  EXPECT_EQ(AllocationSpace::NEW_SPACE,
            MutablePageMetadata::cast(page2->Metadata())->owner_identity());
  // The two pages should still be around, in the new space.
  EXPECT_EQ(page1, allocator->LookupChunkContainingAddress(inner_ptr1));
  EXPECT_EQ(page2, allocator->LookupChunkContainingAddress(inner_ptr2));
  EXPECT_TRUE(v8_flags.minor_ms || page1->IsToPage());
  EXPECT_TRUE(v8_flags.minor_ms || page2->IsToPage());

  // Inner pointer resolution should work with pointers to unused young
  // generation pages (in case of the scavenger, the two pages are now in the
  // "to" semispace). There are no objects to be found.
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr1));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr2));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr3));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr1));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(outside_ptr2));
}

TEST_F(InnerPointerResolutionHeapTest, UnusedLargeYoungPage) {
  ManualGCScope manual_gc_scope(isolate());
  DisableConservativeStackScanningScopeForTesting no_stack_scanning(heap());

  Global<v8::FixedArray> weak;
  Address inner_ptr;

  {
    PtrComprCageBase cage_base{isolate()};
    HandleScope scope(isolate());

    // Allocate a large object in the young generation.
    const int length =
        std::max(1 << kPageSizeBits,
                 2 * heap()->MaxRegularHeapObjectSize(AllocationType::kYoung)) /
        kTaggedSize;
    auto h = factory()->NewFixedArray(length, AllocationType::kYoung);
    weak.Reset(v8_isolate(), Utils::FixedArrayToLocal(h));
    weak.SetWeak();
    auto obj = *h;
    auto page = MemoryChunk::FromHeapObject(obj);
    EXPECT_TRUE(page->IsLargePage());
    EXPECT_EQ(AllocationSpace::NEW_LO_SPACE,
              MutablePageMetadata::cast(page->Metadata())->owner_identity());
    EXPECT_TRUE(v8_flags.minor_ms || page->IsToPage());

    // Keep inner pointer.
    inner_ptr = obj.address() + 17 * kTaggedSize;

    // Inner pointer resolution should work now, finding the object.
    EXPECT_EQ(obj.address(), ResolveInnerPointer(inner_ptr));
  }

  // Garbage collection should reclaim the object.
  InvokeAtomicMinorGC();
  EXPECT_TRUE(weak.IsEmpty());

  // Inner pointer resolution should work with a pointer to an unused young
  // generation large page. There is no object to be found.
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr));
}

TEST_F(InnerPointerResolutionHeapTest, RegularPageAfterEnd) {
  auto allocator = heap()->memory_allocator();

  // Allocate a regular page.
  OldSpace* old_space = heap()->old_space();
  DCHECK_NE(nullptr, old_space);
  auto* page = allocator->AllocatePage(
      MemoryAllocator::AllocationMode::kRegular, old_space, NOT_EXECUTABLE);
  EXPECT_NE(nullptr, page);

  // The end of the page area is expected not to coincide with the beginning of
  // the next page.
  const int size = (1 << kPageSizeBits) / 2;
  const Address mark = page->area_start() + size;
  heap()->CreateFillerObjectAt(page->area_start(), size);
  heap()->CreateFillerObjectAt(mark, static_cast<int>(page->area_end() - mark));
  PageMetadata::UpdateHighWaterMark(mark);
  page->ShrinkToHighWaterMark();
  EXPECT_FALSE(PageMetadata::IsAlignedToPageSize(page->area_end()));

  // Inner pointer resolution after the end of the page area should work.
  Address inner_ptr = page->area_end() + kTaggedSize;
  EXPECT_FALSE(PageMetadata::IsAlignedToPageSize(inner_ptr));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr));

  // Deallocate the page.
  allocator->Free(MemoryAllocator::FreeMode::kImmediately, page);
}

TEST_F(InnerPointerResolutionHeapTest, LargePageAfterEnd) {
  auto allocator = heap()->memory_allocator();

  // Allocate a large page.
  OldLargeObjectSpace* lo_space = heap()->lo_space();
  EXPECT_NE(nullptr, lo_space);
  const int size = 3 * (1 << kPageSizeBits) / 2;
  LargePageMetadata* page =
      allocator->AllocateLargePage(lo_space, size, NOT_EXECUTABLE);
  EXPECT_NE(nullptr, page);

  // The end of the page area is expected not to coincide with the beginning of
  // the next page.
  EXPECT_FALSE(PageMetadata::IsAlignedToPageSize(page->area_end()));

  // Inner pointer resolution after the end of the pare area should work.
  Address inner_ptr = page->area_end() + kTaggedSize;
  EXPECT_FALSE(PageMetadata::IsAlignedToPageSize(inner_ptr));
  EXPECT_EQ(kNullAddress, ResolveInnerPointer(inner_ptr));

  // Deallocate the page.
  allocator->Free(MemoryAllocator::FreeMode::kImmediately, page);
}

}  // namespace internal
}  // namespace v8
