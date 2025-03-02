// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/free-list.h"

#include "src/base/macros.h"
#include "src/common/globals.h"
#include "src/heap/free-list-inl.h"
#include "src/heap/heap.h"
#include "src/heap/mutable-page-metadata-inl.h"
#include "src/heap/page-metadata-inl.h"
#include "src/objects/free-space-inl.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// Free lists for old object spaces implementation

void FreeListCategory::Unlink(FreeList* owner) {
  if (is_linked(owner) && !top().is_null()) {
    owner->DecreaseAvailableBytes(available_);
  }
  set_prev(nullptr);
  set_next(nullptr);
}

void FreeListCategory::Reset(FreeList* owner) {
  Unlink(owner);
  set_top(FreeSpace());
  available_ = 0;
}

Tagged<FreeSpace> FreeListCategory::PickNodeFromList(size_t minimum_size,
                                                     size_t* node_size) {
  Tagged<FreeSpace> node = top();
  DCHECK(!node.is_null());
  DCHECK(MemoryChunk::FromHeapObject(node)->CanAllocate());
  if (static_cast<size_t>(node->Size()) < minimum_size) {
    *node_size = 0;
    return FreeSpace();
  }
  set_top(node->next());
  *node_size = node->Size();
  UpdateCountersAfterAllocation(*node_size);
  return node;
}

Tagged<FreeSpace> FreeListCategory::SearchForNodeInList(size_t minimum_size,
                                                        size_t* node_size) {
  Tagged<FreeSpace> prev_non_evac_node;
  for (Tagged<FreeSpace> cur_node = top(); !cur_node.is_null();
       cur_node = cur_node->next()) {
    DCHECK(MemoryChunk::FromHeapObject(cur_node)->CanAllocate());
    size_t size = cur_node->size(kRelaxedLoad);
    if (size >= minimum_size) {
      DCHECK_GE(available_, size);
      UpdateCountersAfterAllocation(size);
      if (cur_node == top()) {
        set_top(cur_node->next());
      }
      if (!prev_non_evac_node.is_null()) {
        if (MemoryChunk::FromHeapObject(prev_non_evac_node)->executable()) {
          WritableJitPage jit_page(prev_non_evac_node->address(),
                                   prev_non_evac_node->Size());
          WritableFreeSpace free_space = jit_page.FreeRange(
              prev_non_evac_node->address(), prev_non_evac_node->Size());
          prev_non_evac_node->SetNext(free_space, cur_node->next());
        } else {
          prev_non_evac_node->SetNext(
              WritableFreeSpace::ForNonExecutableMemory(
                  prev_non_evac_node->address(), prev_non_evac_node->Size()),
              cur_node->next());
        }
      }
      *node_size = size;
      return cur_node;
    }

    prev_non_evac_node = cur_node;
  }
  return FreeSpace();
}

void FreeListCategory::Free(const WritableFreeSpace& writable_free_space,
                            FreeMode mode, FreeList* owner) {
  Tagged<FreeSpace> free_space =
      Cast<FreeSpace>(HeapObject::FromAddress(writable_free_space.Address()));
  DCHECK_EQ(free_space->Size(), writable_free_space.Size());
  free_space->SetNext(writable_free_space, top());
  set_top(free_space);
  size_t size_in_bytes = writable_free_space.Size();
  available_ += size_in_bytes;
  if (mode == kLinkCategory) {
    if (is_linked(owner)) {
      owner->IncreaseAvailableBytes(size_in_bytes);
    } else {
      owner->AddCategory(this);
    }
  }
}

void FreeListCategory::RepairFreeList(Heap* heap) {
  Tagged<Map> free_space_map = ReadOnlyRoots(heap).free_space_map();
  Tagged<FreeSpace> n = top();
  while (!n.is_null()) {
    ObjectSlot map_slot = n->map_slot();
    if (map_slot.contains_map_value(kNullAddress)) {
      map_slot.store_map(free_space_map);
    } else {
      DCHECK(map_slot.contains_map_value(free_space_map.ptr()));
    }
    n = n->next();
  }
}

void FreeListCategory::Relink(FreeList* owner) {
  DCHECK(!is_linked(owner));
  owner->AddCategory(this);
}

// ------------------------------------------------
// Generic FreeList methods (alloc/free related)

FreeList::FreeList(int number_of_categories, size_t min_block_size)
    : number_of_categories_(number_of_categories),
      last_category_(number_of_categories - 1),
      min_block_size_(min_block_size) {}

std::unique_ptr<FreeList> FreeList::CreateFreeList() {
  return std::make_unique<FreeListManyCachedOrigin>();
}

std::unique_ptr<FreeList> FreeList::CreateFreeListForNewSpace() {
  return std::make_unique<FreeListManyCachedFastPathForNewSpace>();
}

Tagged<FreeSpace> FreeList::TryFindNodeIn(FreeListCategoryType type,
                                          size_t minimum_size,
                                          size_t* node_size) {
  FreeListCategory* category = categories_[type];
  if (category == nullptr) return FreeSpace();
  Tagged<FreeSpace> node = category->PickNodeFromList(minimum_size, node_size);
  if (!node.is_null()) {
    DecreaseAvailableBytes(*node_size);
    VerifyAvailable();
  }
  if (category->is_empty()) {
    RemoveCategory(category);
  }
  return node;
}

Tagged<FreeSpace> FreeList::SearchForNodeInList(FreeListCategoryType type,
                                                size_t minimum_size,
                                                size_t* node_size) {
  FreeListCategoryIterator it(this, type);
  Tagged<FreeSpace> node;
  while (it.HasNext()) {
    FreeListCategory* current = it.Next();
    node = current->SearchForNodeInList(minimum_size, node_size);
    if (!node.is_null()) {
      DecreaseAvailableBytes(*node_size);
      VerifyAvailable();
      if (current->is_empty()) {
        RemoveCategory(current);
      }
      return node;
    }
  }
  return node;
}

size_t FreeList::Free(const WritableFreeSpace& free_space, FreeMode mode) {
  Address start = free_space.Address();
  size_t size_in_bytes = free_space.Size();
  PageMetadata* page = PageMetadata::FromAddress(start);
  page->DecreaseAllocatedBytes(size_in_bytes);

  // Blocks have to be a minimum size to hold free list items.
  if (size_in_bytes < min_block_size_) {
    page->add_wasted_memory(size_in_bytes);
    return size_in_bytes;
  }

  // Insert other blocks at the head of a free list of the appropriate
  // magnitude.
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  page->free_list_category(type)->Free(free_space, mode, this);
  DCHECK_EQ(page->AvailableInFreeList(),
            page->AvailableInFreeListFromAllocatedBytes());
  return 0;
}

// ------------------------------------------------
// FreeListMany implementation

constexpr unsigned int FreeListMany::categories_min[kNumberOfCategories];

FreeListMany::FreeListMany() : FreeList(kNumberOfCategories, kMinBlockSize) {
  // Initializing base (FreeList) fields
  categories_ = new FreeListCategory*[number_of_categories_]();
  Reset();
}

FreeListMany::~FreeListMany() { delete[] categories_; }

PageMetadata* FreeListMany::GetPageForSize(size_t size_in_bytes) {
  FreeListCategoryType minimum_category =
      SelectFreeListCategoryType(size_in_bytes);
  PageMetadata* page = nullptr;
  for (int cat = minimum_category + 1; !page && cat <= last_category_; cat++) {
    page = GetPageForCategoryType(cat);
  }
  if (!page) {
    // Might return a page in which |size_in_bytes| will not fit.
    page = GetPageForCategoryType(minimum_category);
  }
  return page;
}

Tagged<FreeSpace> FreeListMany::Allocate(size_t size_in_bytes,
                                         size_t* node_size,
                                         AllocationOrigin origin) {
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  Tagged<FreeSpace> node;
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  for (int i = type; i < last_category_ && node.is_null(); i++) {
    node = TryFindNodeIn(static_cast<FreeListCategoryType>(i), size_in_bytes,
                         node_size);
  }

  if (node.is_null()) {
    // Searching each element of the last category.
    node = SearchForNodeInList(last_category_, size_in_bytes, node_size);
  }

  if (!node.is_null()) {
    PageMetadata::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  VerifyAvailable();
  return node;
}

// ------------------------------------------------
// FreeListManyCached implementation

FreeListManyCached::FreeListManyCached() { ResetCache(); }

void FreeListManyCached::Reset() {
  ResetCache();
  FreeListMany::Reset();
}

void FreeListManyCached::ResetForNonBlackAllocatedPages() {
  ResetCache();
  FreeListMany::ResetForNonBlackAllocatedPages();
}

bool FreeListManyCached::AddCategory(FreeListCategory* category) {
  bool was_added = FreeList::AddCategory(category);

  // Updating cache
  if (was_added) {
    UpdateCacheAfterAddition(category->type_);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  return was_added;
}

void FreeListManyCached::RemoveCategory(FreeListCategory* category) {
  FreeList::RemoveCategory(category);

  // Updating cache
  int type = category->type_;
  if (categories_[type] == nullptr) {
    UpdateCacheAfterRemoval(type);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif
}

size_t FreeListManyCached::Free(const WritableFreeSpace& free_space,
                                FreeMode mode) {
  Address start = free_space.Address();
  size_t size_in_bytes = free_space.Size();
  PageMetadata* page = PageMetadata::FromAddress(start);
  page->DecreaseAllocatedBytes(size_in_bytes);

  // Blocks have to be a minimum size to hold free list items.
  if (size_in_bytes < min_block_size_) {
    page->add_wasted_memory(size_in_bytes);
    return size_in_bytes;
  }

  // Insert other blocks at the head of a free list of the appropriate
  // magnitude.
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  page->free_list_category(type)->Free(free_space, mode, this);

  // Updating cache
  if (mode == kLinkCategory) {
    UpdateCacheAfterAddition(type);

#ifdef DEBUG
    CheckCacheIntegrity();
#endif
  }

  DCHECK_EQ(page->AvailableInFreeList(),
            page->AvailableInFreeListFromAllocatedBytes());
  return 0;
}

Tagged<FreeSpace> FreeListManyCached::Allocate(size_t size_in_bytes,
                                               size_t* node_size,
                                               AllocationOrigin origin) {
  USE(origin);
  DCHECK_GE(kMaxBlockSize, size_in_bytes);

  Tagged<FreeSpace> node;
  FreeListCategoryType type = SelectFreeListCategoryType(size_in_bytes);
  type = next_nonempty_category[type];
  for (; type < last_category_; type = next_nonempty_category[type + 1]) {
    node = TryFindNodeIn(type, size_in_bytes, node_size);
    if (!node.is_null()) break;
  }

  if (node.is_null()) {
    // Searching each element of the last category.
    type = last_category_;
    node = SearchForNodeInList(type, size_in_bytes, node_size);
  }

  // Updating cache
  if (!node.is_null() && categories_[type] == nullptr) {
    UpdateCacheAfterRemoval(type);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  if (!node.is_null()) {
    PageMetadata::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

  VerifyAvailable();
  return node;
}

// ------------------------------------------------
// FreeListManyCachedFastPathBase implementation

Tagged<FreeSpace> FreeListManyCachedFastPathBase::Allocate(
    size_t size_in_bytes, size_t* node_size, AllocationOrigin origin) {
  USE(origin);
  DCHECK_GE(kMaxBlockSize, size_in_bytes);
  Tagged<FreeSpace> node;

  // Fast path part 1: searching the last categories
  FreeListCategoryType first_category =
      SelectFastAllocationFreeListCategoryType(size_in_bytes);
  FreeListCategoryType type = first_category;
  for (type = next_nonempty_category[type]; type <= last_category_;
       type = next_nonempty_category[type + 1]) {
    node = TryFindNodeIn(type, size_in_bytes, node_size);
    if (!node.is_null()) break;
  }

  // Fast path part 2: searching the medium categories for tiny objects
  if (small_blocks_mode_ == SmallBlocksMode::kAllow) {
    if (node.is_null()) {
      if (size_in_bytes <= kTinyObjectMaxSize) {
        DCHECK_EQ(kFastPathFirstCategory, first_category);
        for (type = next_nonempty_category[kFastPathFallBackTiny];
             type < kFastPathFirstCategory;
             type = next_nonempty_category[type + 1]) {
          node = TryFindNodeIn(type, size_in_bytes, node_size);
          if (!node.is_null()) break;
        }
        first_category = kFastPathFallBackTiny;
      }
    }
  }

  // Searching the last category
  if (node.is_null()) {
    // Searching each element of the last category.
    type = last_category_;
    node = SearchForNodeInList(type, size_in_bytes, node_size);
  }

  // Finally, search the most precise category
  if (node.is_null()) {
    type = SelectFreeListCategoryType(size_in_bytes);
    for (type = next_nonempty_category[type]; type < first_category;
         type = next_nonempty_category[type + 1]) {
      node = TryFindNodeIn(type, size_in_bytes, node_size);
      if (!node.is_null()) break;
    }
  }

  if (!node.is_null()) {
    if (categories_[type] == nullptr) UpdateCacheAfterRemoval(type);
    PageMetadata::FromHeapObject(node)->IncreaseAllocatedBytes(*node_size);
  }

#ifdef DEBUG
  CheckCacheIntegrity();
#endif

  VerifyAvailable();
  return node;
}

// ------------------------------------------------
// FreeListManyCachedOrigin implementation

Tagged<FreeSpace> FreeListManyCachedOrigin::Allocate(size_t size_in_bytes,
                                                     size_t* node_size,
                                                     AllocationOrigin origin) {
  if (origin == AllocationOrigin::kGC) {
    return FreeListManyCached::Allocate(size_in_bytes, node_size, origin);
  } else {
    return FreeListManyCachedFastPath::Allocate(size_in_bytes, node_size,
                                                origin);
  }
}

// ------------------------------------------------
// Generic FreeList methods (non alloc/free related)

void FreeList::Reset() {
  ForAllFreeListCategories(
      [this](FreeListCategory* category) { category->Reset(this); });
  for (int i = kFirstCategory; i < number_of_categories_; i++) {
    categories_[i] = nullptr;
  }
  wasted_bytes_ = 0;
  available_ = 0;
}

void FreeList::ResetForNonBlackAllocatedPages() {
  DCHECK(v8_flags.black_allocated_pages);
  ForAllFreeListCategories([this](FreeListCategory* category) {
    if (!category->is_empty()) {
      auto* chunk = MemoryChunk::FromHeapObject(category->top());
      if (chunk->IsFlagSet(MemoryChunk::BLACK_ALLOCATED)) {
        category->Unlink(this);
        return;
      }
    }
    category->Reset(this);
  });
  for (int i = kFirstCategory; i < number_of_categories_; i++) {
    categories_[i] = nullptr;
  }
  wasted_bytes_ = 0;
  available_ = 0;
}

void FreeList::EvictFreeListItems(PageMetadata* page) {
  size_t sum = 0;
  page->ForAllFreeListCategories([this, &sum](FreeListCategory* category) {
    sum += category->available();
    RemoveCategory(category);
    category->Reset(this);
  });
  page->add_wasted_memory(sum);
}

void FreeList::RepairLists(Heap* heap) {
  ForAllFreeListCategories(
      [heap](FreeListCategory* category) { category->RepairFreeList(heap); });
}

bool FreeList::AddCategory(FreeListCategory* category) {
  FreeListCategoryType type = category->type_;
  DCHECK_LT(type, number_of_categories_);
  FreeListCategory* top = categories_[type];

  if (category->is_empty()) return false;
  DCHECK_NE(top, category);

  // Common double-linked list insertion.
  if (top != nullptr) {
    top->set_prev(category);
  }
  category->set_next(top);
  categories_[type] = category;

  IncreaseAvailableBytes(category->available());
  return true;
}

void FreeList::RemoveCategory(FreeListCategory* category) {
  FreeListCategoryType type = category->type_;
  DCHECK_LT(type, number_of_categories_);
  FreeListCategory* top = categories_[type];

  if (category->is_linked(this)) {
    DecreaseAvailableBytes(category->available());
  }

  // Common double-linked list removal.
  if (top == category) {
    categories_[type] = category->next();
  }
  if (category->prev() != nullptr) {
    category->prev()->set_next(category->next());
  }
  if (category->next() != nullptr) {
    category->next()->set_prev(category->prev());
  }
  category->set_next(nullptr);
  category->set_prev(nullptr);
}

void FreeList::PrintCategories(FreeListCategoryType type) {
  FreeListCategoryIterator it(this, type);
  PrintF("FreeList[%p, top=%p, %d] ", static_cast<void*>(this),
         static_cast<void*>(categories_[type]), type);
  while (it.HasNext()) {
    FreeListCategory* current = it.Next();
    PrintF("%p -> ", static_cast<void*>(current));
  }
  PrintF("null\n");
}

size_t FreeListCategory::SumFreeList() {
  size_t sum = 0;
  Tagged<FreeSpace> cur = top();
  while (!cur.is_null()) {
    // We can't use "cur->map()" here because both cur's map and the
    // root can be null during bootstrapping.
    DCHECK(
        cur->map_slot().contains_map_value(PageMetadata::FromHeapObject(cur)
                                               ->heap()
                                               ->isolate()
                                               ->root(RootIndex::kFreeSpaceMap)
                                               .ptr()));
    sum += cur->size(kRelaxedLoad);
    cur = cur->next();
  }
  return sum;
}
int FreeListCategory::FreeListLength() {
  int length = 0;
  Tagged<FreeSpace> cur = top();
  while (!cur.is_null()) {
    length++;
    cur = cur->next();
  }
  return length;
}

#ifdef DEBUG
bool FreeList::IsVeryLong() {
  int len = 0;
  for (int i = kFirstCategory; i < number_of_categories_; i++) {
    FreeListCategoryIterator it(this, static_cast<FreeListCategoryType>(i));
    while (it.HasNext()) {
      len += it.Next()->FreeListLength();
      if (len >= FreeListCategory::kVeryLongFreeList) return true;
    }
  }
  return false;
}

// This can take a very long time because it is linear in the number of entries
// on the free list, so it should not be called if FreeListLength returns
// kVeryLongFreeList.
size_t FreeList::SumFreeLists() {
  size_t sum = 0;
  ForAllFreeListCategories(
      [&sum](FreeListCategory* category) { sum += category->SumFreeList(); });
  return sum;
}
#endif

}  // namespace internal
}  // namespace v8
