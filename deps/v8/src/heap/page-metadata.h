// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_PAGE_METADATA_H_
#define V8_HEAP_PAGE_METADATA_H_

#include "src/heap/base-space.h"
#include "src/heap/free-list.h"
#include "src/heap/mutable-page-metadata.h"
#include "src/heap/spaces.h"

namespace v8 {
namespace internal {

class Heap;

// -----------------------------------------------------------------------------
// A page is a memory chunk of a size 256K. Large object pages may be larger.
//
// The only way to get a page pointer is by calling factory methods:
//   PageMetadata* p = PageMetadata::FromAddress(addr); or
//   PageMetadata* p = PageMetadata::FromAllocationAreaAddress(address);
class PageMetadata : public MutablePageMetadata {
 public:
  PageMetadata(Heap* heap, BaseSpace* space, size_t size, Address area_start,
               Address area_end, VirtualMemory reservation);

  // Returns the page containing a given address. The address ranges
  // from [page_addr .. page_addr + kPageSize]. This only works if the object is
  // in fact in a page.
  V8_INLINE static PageMetadata* FromAddress(Address addr);
  V8_INLINE static PageMetadata* FromHeapObject(Tagged<HeapObject> o);

  static PageMetadata* cast(MemoryChunkMetadata* metadata) {
    return cast(MutablePageMetadata::cast(metadata));
  }

  static PageMetadata* cast(MutablePageMetadata* metadata) {
    DCHECK_IMPLIES(metadata, !metadata->Chunk()->IsLargePage());
    return static_cast<PageMetadata*>(metadata);
  }

  // Returns the page containing the address provided. The address can
  // potentially point righter after the page. To be also safe for tagged values
  // we subtract a hole word. The valid address ranges from
  // [page_addr + area_start_ .. page_addr + kPageSize + kTaggedSize].
  V8_INLINE static PageMetadata* FromAllocationAreaAddress(Address address);

  // Checks if address1 and address2 are on the same new space page.
  static bool OnSamePage(Address address1, Address address2) {
    return MemoryChunk::FromAddress(address1) ==
           MemoryChunk::FromAddress(address2);
  }

  // Checks whether an address is page aligned.
  static bool IsAlignedToPageSize(Address addr) {
    return MemoryChunk::IsAligned(addr);
  }

  static PageMetadata* ConvertNewToOld(PageMetadata* old_page);

  V8_EXPORT_PRIVATE void MarkNeverAllocateForTesting();
  inline void MarkEvacuationCandidate();
  inline void ClearEvacuationCandidate();

  PageMetadata* next_page() {
    return static_cast<PageMetadata*>(list_node_.next());
  }
  PageMetadata* prev_page() {
    return static_cast<PageMetadata*>(list_node_.prev());
  }

  const PageMetadata* next_page() const {
    return static_cast<const PageMetadata*>(list_node_.next());
  }
  const PageMetadata* prev_page() const {
    return static_cast<const PageMetadata*>(list_node_.prev());
  }

  template <typename Callback>
  inline void ForAllFreeListCategories(Callback callback);

  V8_EXPORT_PRIVATE size_t AvailableInFreeList();

  size_t AvailableInFreeListFromAllocatedBytes() {
    DCHECK_GE(area_size(), wasted_memory() + allocated_bytes());
    return area_size() - wasted_memory() - allocated_bytes();
  }

  FreeListCategory* free_list_category(FreeListCategoryType type) {
    return categories_[type];
  }

  V8_EXPORT_PRIVATE size_t ShrinkToHighWaterMark();

  V8_EXPORT_PRIVATE void CreateBlackArea(Address start, Address end);
  void DestroyBlackArea(Address start, Address end);

  void InitializeFreeListCategories();
  void AllocateFreeListCategories();
  void ReleaseFreeListCategories();

  ActiveSystemPages* active_system_pages() { return active_system_pages_; }

  template <RememberedSetType remembered_set>
  void ClearTypedSlotsInFreeMemory(const TypedSlotSet::FreeRangesMap& ranges) {
    TypedSlotSet* typed_slot_set = this->typed_slot_set<remembered_set>();
    if (typed_slot_set != nullptr) {
      typed_slot_set->ClearInvalidSlots(ranges);
    }
  }

  template <RememberedSetType remembered_set>
  void AssertNoTypedSlotsInFreeMemory(
      const TypedSlotSet::FreeRangesMap& ranges) {
#if DEBUG
    TypedSlotSet* typed_slot_set = this->typed_slot_set<remembered_set>();
    if (typed_slot_set != nullptr) {
      typed_slot_set->AssertNoInvalidSlots(ranges);
    }
#endif  // DEBUG
  }

 private:
  friend class MemoryAllocator;
};

// Validate our estimates on the header size.
static_assert(sizeof(MemoryChunkMetadata) <=
              MemoryChunkLayout::kMemoryChunkMetadataSize);
static_assert(sizeof(MutablePageMetadata) <=
              MemoryChunkLayout::kMutablePageMetadataSize);
static_assert(sizeof(PageMetadata) <=
              MemoryChunkLayout::kMutablePageMetadataSize);

}  // namespace internal

namespace base {
// Define special hash function for page pointers, to be used with std data
// structures, e.g. std::unordered_set<PageMetadata*, base::hash<PageMetadata*>
template <>
struct hash<i::PageMetadata*> : hash<i::MemoryChunkMetadata*> {};
template <>
struct hash<const i::PageMetadata*> : hash<const i::MemoryChunkMetadata*> {};
}  // namespace base

}  // namespace v8

#endif  // V8_HEAP_PAGE_METADATA_H_
