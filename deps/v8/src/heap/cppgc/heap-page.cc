// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-page.h"

#include <algorithm>
#include <cstddef>

#include "include/cppgc/internal/api-constants.h"
#include "src/base/logging.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-space.h"
#include "src/heap/cppgc/heap.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/object-start-bitmap.h"
#include "src/heap/cppgc/page-memory.h"
#include "src/heap/cppgc/raw-heap.h"
#include "src/heap/cppgc/remembered-set.h"
#include "src/heap/cppgc/stats-collector.h"

namespace cppgc {
namespace internal {

static_assert(api_constants::kGuardPageSize == kGuardPageSize);

namespace {

Address AlignAddress(Address address, size_t alignment) {
  return reinterpret_cast<Address>(
      RoundUp(reinterpret_cast<uintptr_t>(address), alignment));
}

}  // namespace

HeapBase& BasePage::heap() const {
  return static_cast<HeapBase&>(heap_handle_);
}

// static
BasePage* BasePage::FromInnerAddress(const HeapBase* heap, void* address) {
  return const_cast<BasePage*>(
      FromInnerAddress(heap, const_cast<const void*>(address)));
}

// static
const BasePage* BasePage::FromInnerAddress(const HeapBase* heap,
                                           const void* address) {
  return reinterpret_cast<const BasePage*>(
      heap->page_backend()->Lookup(static_cast<ConstAddress>(address)));
}

// static
void BasePage::Destroy(BasePage* page,
                       FreeMemoryHandling free_memory_handling) {
  if (page->discarded_memory()) {
    page->space()
        .raw_heap()
        ->heap()
        ->stats_collector()
        ->DecrementDiscardedMemory(page->discarded_memory());
  }
  if (page->is_large()) {
    LargePage::Destroy(LargePage::From(page));
  } else {
    NormalPage::Destroy(NormalPage::From(page), free_memory_handling);
  }
}

Address BasePage::PayloadStart() {
  return is_large() ? LargePage::From(this)->PayloadStart()
                    : NormalPage::From(this)->PayloadStart();
}

ConstAddress BasePage::PayloadStart() const {
  return const_cast<BasePage*>(this)->PayloadStart();
}

Address BasePage::PayloadEnd() {
  return is_large() ? LargePage::From(this)->PayloadEnd()
                    : NormalPage::From(this)->PayloadEnd();
}

ConstAddress BasePage::PayloadEnd() const {
  return const_cast<BasePage*>(this)->PayloadEnd();
}

size_t BasePage::AllocatedSize() const {
  return is_large() ? LargePage::PageHeaderSize() +
                          LargePage::From(this)->PayloadSize()
                    : NormalPage::From(this)->PayloadSize() +
                          RoundUp(sizeof(NormalPage), kAllocationGranularity);
}

size_t BasePage::AllocatedBytesAtLastGC() const {
  return is_large() ? LargePage::From(this)->AllocatedBytesAtLastGC()
                    : NormalPage::From(this)->AllocatedBytesAtLastGC();
}

HeapObjectHeader* BasePage::TryObjectHeaderFromInnerAddress(
    void* address) const {
  return const_cast<HeapObjectHeader*>(
      TryObjectHeaderFromInnerAddress(const_cast<const void*>(address)));
}

const HeapObjectHeader* BasePage::TryObjectHeaderFromInnerAddress(
    const void* address) const {
  if (is_large()) {
    if (!LargePage::From(this)->PayloadContains(
            static_cast<ConstAddress>(address)))
      return nullptr;
  } else {
    const NormalPage* normal_page = NormalPage::From(this);
    if (!normal_page->PayloadContains(static_cast<ConstAddress>(address)))
      return nullptr;
    // Check that the space has no linear allocation buffer.
    DCHECK(!NormalPageSpace::From(normal_page->space())
                .linear_allocation_buffer()
                .size());
  }

  // |address| is on the heap, so we FromInnerAddress can get the header.
  const HeapObjectHeader* header =
      ObjectHeaderFromInnerAddressImpl(this, address);
  if (header->IsFree()) return nullptr;
  DCHECK_NE(kFreeListGCInfoIndex, header->GetGCInfoIndex());
  return header;
}

#if defined(CPPGC_YOUNG_GENERATION)
void BasePage::AllocateSlotSet() {
  DCHECK_NULL(slot_set_);
  slot_set_ = decltype(slot_set_)(
      static_cast<SlotSet*>(
          SlotSet::Allocate(SlotSet::BucketsForSize(AllocatedSize()))),
      SlotSetDeleter{AllocatedSize()});
}

void BasePage::SlotSetDeleter::operator()(SlotSet* slot_set) const {
  DCHECK_NOT_NULL(slot_set);
  SlotSet::Delete(slot_set, SlotSet::BucketsForSize(page_size_));
}

void BasePage::ResetSlotSet() { slot_set_.reset(); }
#endif  // defined(CPPGC_YOUNG_GENERATION)

BasePage::BasePage(HeapBase& heap, BaseSpace& space, PageType type)
    : BasePageHandle(heap),
      space_(space),
      type_(type)
#if defined(CPPGC_YOUNG_GENERATION)
      ,
      slot_set_(nullptr, SlotSetDeleter{})
#endif  // defined(CPPGC_YOUNG_GENERATION)
{
  DCHECK_EQ(0u, (reinterpret_cast<uintptr_t>(this) - kGuardPageSize) &
                    kPageOffsetMask);
  DCHECK_EQ(&heap.raw_heap(), space_.raw_heap());
}

// static
NormalPage* NormalPage::TryCreate(PageBackend& page_backend,
                                  NormalPageSpace& space) {
  void* memory = page_backend.TryAllocateNormalPageMemory();
  if (!memory) return nullptr;

  auto* normal_page = new (memory) NormalPage(*space.raw_heap()->heap(), space);
  normal_page->SynchronizedStore();
  normal_page->heap().stats_collector()->NotifyAllocatedMemory(kPageSize);
  // Memory is zero initialized as
  // a) memory retrieved from the OS is zeroed;
  // b) memory retrieved from the page pool was swept and thus is zeroed except
  //    for the first header which will anyways serve as header again.
  //
  // The following is a subset of SetMemoryInaccessible() to establish the
  // invariant that memory is in the same state as it would be after sweeping.
  // This allows to return newly allocated pages to go into that LAB and back
  // into the free list.
  Address begin = normal_page->PayloadStart() + sizeof(HeapObjectHeader);
  const size_t size = normal_page->PayloadSize() - sizeof(HeapObjectHeader);
#if defined(V8_USE_MEMORY_SANITIZER)
  MSAN_ALLOCATED_UNINITIALIZED_MEMORY(begin, size);
#elif defined(V8_USE_ADDRESS_SANITIZER)
  ASAN_POISON_MEMORY_REGION(begin, size);
#elif DEBUG
  cppgc::internal::ZapMemory(begin, size);
#endif  // Release builds.
  CheckMemoryIsInaccessible(begin, size);
  return normal_page;
}

// static
void NormalPage::Destroy(NormalPage* page,
                         FreeMemoryHandling free_memory_handling) {
  DCHECK(page);
  const BaseSpace& space = page->space();
  DCHECK_EQ(space.end(), std::find(space.begin(), space.end(), page));
  USE(space);
  page->~NormalPage();
  PageBackend* backend = page->heap().page_backend();
  page->heap().stats_collector()->NotifyFreedMemory(kPageSize);
  backend->FreeNormalPageMemory(reinterpret_cast<Address>(page),
                                free_memory_handling);
}

NormalPage::NormalPage(HeapBase& heap, BaseSpace& space)
    : BasePage(heap, space, PageType::kNormal), object_start_bitmap_() {
  DCHECK_LT(kLargeObjectSizeThreshold,
            static_cast<size_t>(PayloadEnd() - PayloadStart()));
}

NormalPage::~NormalPage() = default;

NormalPage::iterator NormalPage::begin() {
  const auto& lab = NormalPageSpace::From(space()).linear_allocation_buffer();
  return iterator(reinterpret_cast<HeapObjectHeader*>(PayloadStart()),
                  lab.start(), lab.size());
}

NormalPage::const_iterator NormalPage::begin() const {
  const auto& lab = NormalPageSpace::From(space()).linear_allocation_buffer();
  return const_iterator(
      reinterpret_cast<const HeapObjectHeader*>(PayloadStart()), lab.start(),
      lab.size());
}

Address NormalPage::PayloadStart() {
  return AlignAddress((reinterpret_cast<Address>(this + 1)),
                      kAllocationGranularity);
}

ConstAddress NormalPage::PayloadStart() const {
  return const_cast<NormalPage*>(this)->PayloadStart();
}

Address NormalPage::PayloadEnd() { return PayloadStart() + PayloadSize(); }

ConstAddress NormalPage::PayloadEnd() const {
  return const_cast<NormalPage*>(this)->PayloadEnd();
}

// static
size_t NormalPage::PayloadSize() {
  const size_t header_size =
      RoundUp(sizeof(NormalPage), kAllocationGranularity);
  return kPageSize - 2 * kGuardPageSize - header_size;
}

LargePage::LargePage(HeapBase& heap, BaseSpace& space, size_t size)
    : BasePage(heap, space, PageType::kLarge), payload_size_(size) {}

LargePage::~LargePage() = default;

// static
size_t LargePage::AllocationSize(size_t payload_size) {
  return PageHeaderSize() + payload_size;
}

// static
LargePage* LargePage::TryCreate(PageBackend& page_backend,
                                LargePageSpace& space, size_t size) {
  // Ensure that the API-provided alignment guarantees does not violate the
  // internally guaranteed alignment of large page allocations.
  static_assert(kGuaranteedObjectAlignment <=
                api_constants::kMaxSupportedAlignment);
  static_assert(
      api_constants::kMaxSupportedAlignment % kGuaranteedObjectAlignment == 0);

  DCHECK_LE(kLargeObjectSizeThreshold, size);
  const size_t allocation_size = AllocationSize(size);

  auto* heap = space.raw_heap()->heap();
  void* memory = page_backend.TryAllocateLargePageMemory(allocation_size);
  if (!memory) return nullptr;

  LargePage* page = new (memory) LargePage(*heap, space, size);
  page->SynchronizedStore();
  page->heap().stats_collector()->NotifyAllocatedMemory(allocation_size);
  return page;
}

// static
void LargePage::Destroy(LargePage* page) {
  DCHECK(page);
  HeapBase& heap = page->heap();
  const size_t payload_size = page->PayloadSize();
#if DEBUG
  const BaseSpace& space = page->space();
  {
    // Destroy() happens on the mutator but another concurrent sweeper task may
    // add add a live object using `BaseSpace::AddPage()` while iterating the
    // pages.
    v8::base::LockGuard<v8::base::Mutex> guard(&space.pages_mutex());
    DCHECK_EQ(space.end(), std::find(space.begin(), space.end(), page));
  }
#endif  // DEBUG
  page->~LargePage();
  PageBackend* backend = heap.page_backend();
  heap.stats_collector()->NotifyFreedMemory(AllocationSize(payload_size));
  backend->FreeLargePageMemory(reinterpret_cast<Address>(page));
}

HeapObjectHeader* LargePage::ObjectHeader() {
  return reinterpret_cast<HeapObjectHeader*>(PayloadStart());
}

const HeapObjectHeader* LargePage::ObjectHeader() const {
  return reinterpret_cast<const HeapObjectHeader*>(PayloadStart());
}

Address LargePage::PayloadStart() {
  return reinterpret_cast<Address>(this) + PageHeaderSize();
}

ConstAddress LargePage::PayloadStart() const {
  return const_cast<LargePage*>(this)->PayloadStart();
}

Address LargePage::PayloadEnd() { return PayloadStart() + PayloadSize(); }

ConstAddress LargePage::PayloadEnd() const {
  return const_cast<LargePage*>(this)->PayloadEnd();
}

}  // namespace internal
}  // namespace cppgc
