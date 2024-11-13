// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/bounded-page-allocator.h"

namespace v8 {
namespace base {

BoundedPageAllocator::BoundedPageAllocator(
    v8::PageAllocator* page_allocator, Address start, size_t size,
    size_t allocate_page_size, PageInitializationMode page_initialization_mode,
    PageFreeingMode page_freeing_mode)
    : allocate_page_size_(allocate_page_size),
      commit_page_size_(page_allocator->CommitPageSize()),
      page_allocator_(page_allocator),
      region_allocator_(start, size, allocate_page_size_),
      page_initialization_mode_(page_initialization_mode),
      page_freeing_mode_(page_freeing_mode) {
  DCHECK_NOT_NULL(page_allocator);
  DCHECK(IsAligned(allocate_page_size, page_allocator->AllocatePageSize()));
  DCHECK(IsAligned(allocate_page_size_, commit_page_size_));
}

BoundedPageAllocator::Address BoundedPageAllocator::begin() const {
  return region_allocator_.begin();
}

size_t BoundedPageAllocator::size() const { return region_allocator_.size(); }

void* BoundedPageAllocator::AllocatePages(void* hint, size_t size,
                                          size_t alignment,
                                          PageAllocator::Permission access) {
  MutexGuard guard(&mutex_);
  DCHECK(IsAligned(alignment, region_allocator_.page_size()));
  DCHECK(IsAligned(alignment, allocate_page_size_));

  Address address = RegionAllocator::kAllocationFailure;

  Address hint_address = reinterpret_cast<Address>(hint);
  if (hint_address && IsAligned(hint_address, alignment) &&
      region_allocator_.contains(hint_address, size)) {
    if (region_allocator_.AllocateRegionAt(hint_address, size)) {
      address = hint_address;
    }
  }

  if (address == RegionAllocator::kAllocationFailure) {
    if (alignment <= allocate_page_size_) {
      // TODO(ishell): Consider using randomized version here.
      address = region_allocator_.AllocateRegion(size);
    } else {
      address = region_allocator_.AllocateAlignedRegion(size, alignment);
    }
  }

  if (address == RegionAllocator::kAllocationFailure) {
    allocation_status_ = AllocationStatus::kRanOutOfReservation;
    return nullptr;
  }

  void* ptr = reinterpret_cast<void*>(address);
  // It's assumed that free regions are in kNoAccess/kNoAccessWillJitLater
  // state.
  if (access == PageAllocator::kNoAccess ||
      access == PageAllocator::kNoAccessWillJitLater) {
    allocation_status_ = AllocationStatus::kSuccess;
    return ptr;
  }

  if (page_initialization_mode_ == PageInitializationMode::kRecommitOnly) {
    if (page_allocator_->RecommitPages(ptr, size, access)) {
      allocation_status_ = AllocationStatus::kSuccess;
      return ptr;
    }
  } else {
    if (page_allocator_->SetPermissions(ptr, size, access)) {
      allocation_status_ = AllocationStatus::kSuccess;
      return ptr;
    }
  }

  // This most likely means that we ran out of memory.
  CHECK_EQ(region_allocator_.FreeRegion(address), size);
  allocation_status_ = AllocationStatus::kFailedToCommit;
  return nullptr;
}

bool BoundedPageAllocator::AllocatePagesAt(Address address, size_t size,
                                           PageAllocator::Permission access) {
  DCHECK(IsAligned(address, allocate_page_size_));
  DCHECK(IsAligned(size, allocate_page_size_));

  {
    MutexGuard guard(&mutex_);

    DCHECK(region_allocator_.contains(address, size));

    if (!region_allocator_.AllocateRegionAt(address, size)) {
      allocation_status_ = AllocationStatus::kHintedAddressTakenOrNotFound;
      return false;
    }
  }

  void* ptr = reinterpret_cast<void*>(address);
  if (!page_allocator_->SetPermissions(ptr, size, access)) {
    // This most likely means that we ran out of memory.
    CHECK_EQ(region_allocator_.FreeRegion(address), size);
    allocation_status_ = AllocationStatus::kFailedToCommit;
    return false;
  }

  allocation_status_ = AllocationStatus::kSuccess;
  return true;
}

bool BoundedPageAllocator::ReserveForSharedMemoryMapping(void* ptr,
                                                         size_t size) {
  Address address = reinterpret_cast<Address>(ptr);
  DCHECK(IsAligned(address, allocate_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));

  {
    MutexGuard guard(&mutex_);
    DCHECK(region_allocator_.contains(address, size));

    // Region allocator requires page size rather than commit size so just over-
    // allocate there since any extra space couldn't be used anyway.
    size_t region_size = RoundUp(size, allocate_page_size_);
    if (!region_allocator_.AllocateRegionAt(
            address, region_size, RegionAllocator::RegionState::kExcluded)) {
      allocation_status_ = AllocationStatus::kHintedAddressTakenOrNotFound;
      return false;
    }
  }

  const bool success = page_allocator_->SetPermissions(
      ptr, size, PageAllocator::Permission::kNoAccess);
  if (success) {
    allocation_status_ = AllocationStatus::kSuccess;
  } else {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::FreePages(void* raw_address, size_t size) {
  MutexGuard guard(&mutex_);

  Address address = reinterpret_cast<Address>(raw_address);
  CHECK_EQ(size, region_allocator_.FreeRegion(address));
  if (page_initialization_mode_ ==
      PageInitializationMode::kAllocatedPagesMustBeZeroInitialized) {
    DCHECK_NE(page_freeing_mode_, PageFreeingMode::kDiscard);
    // When we are required to return zero-initialized pages, we decommit the
    // pages here, which will cause any wired pages to be removed by the OS.
    return page_allocator_->DecommitPages(raw_address, size);
  }
  if (page_freeing_mode_ == PageFreeingMode::kMakeInaccessible) {
    DCHECK_EQ(page_initialization_mode_,
              PageInitializationMode::kAllocatedPagesCanBeUninitialized);
    return page_allocator_->SetPermissions(raw_address, size,
                                           PageAllocator::kNoAccess);
  }
  CHECK_EQ(page_freeing_mode_, PageFreeingMode::kDiscard);
  return page_allocator_->DiscardSystemPages(raw_address, size);
}

bool BoundedPageAllocator::ReleasePages(void* raw_address, size_t size,
                                        size_t new_size) {
  Address address = reinterpret_cast<Address>(raw_address);
  DCHECK(IsAligned(address, allocate_page_size_));

  DCHECK_LT(new_size, size);
  DCHECK(IsAligned(size - new_size, commit_page_size_));

  // This must be held until the page permissions are updated.
  MutexGuard guard(&mutex_);

  // Check if we freed any allocatable pages by this release.
  size_t allocated_size = RoundUp(size, allocate_page_size_);
  size_t new_allocated_size = RoundUp(new_size, allocate_page_size_);

#ifdef DEBUG
  {
    // There must be an allocated region at given |address| of a size not
    // smaller than |size|.
    DCHECK_EQ(allocated_size, region_allocator_.CheckRegion(address));
  }
#endif

  if (new_allocated_size < allocated_size) {
    region_allocator_.TrimRegion(address, new_allocated_size);
  }

  // Keep the region in "used" state just uncommit some pages.
  void* free_address = reinterpret_cast<void*>(address + new_size);
  size_t free_size = size - new_size;
  if (page_initialization_mode_ ==
      PageInitializationMode::kAllocatedPagesMustBeZeroInitialized) {
    DCHECK_NE(page_freeing_mode_, PageFreeingMode::kDiscard);
    // See comment in FreePages().
    return (page_allocator_->DecommitPages(free_address, free_size));
  }
  if (page_freeing_mode_ == PageFreeingMode::kMakeInaccessible) {
    DCHECK_EQ(page_initialization_mode_,
              PageInitializationMode::kAllocatedPagesCanBeUninitialized);
    return page_allocator_->SetPermissions(free_address, free_size,
                                           PageAllocator::kNoAccess);
  }
  CHECK_EQ(page_freeing_mode_, PageFreeingMode::kDiscard);
  return page_allocator_->DiscardSystemPages(free_address, free_size);
}

bool BoundedPageAllocator::SetPermissions(void* address, size_t size,
                                          PageAllocator::Permission access) {
  DCHECK(IsAligned(reinterpret_cast<Address>(address), commit_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));
  DCHECK(region_allocator_.contains(reinterpret_cast<Address>(address), size));
  const bool success = page_allocator_->SetPermissions(address, size, access);
  if (!success) {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::RecommitPages(void* address, size_t size,
                                         PageAllocator::Permission access) {
  DCHECK(IsAligned(reinterpret_cast<Address>(address), commit_page_size_));
  DCHECK(IsAligned(size, commit_page_size_));
  DCHECK(region_allocator_.contains(reinterpret_cast<Address>(address), size));
  const bool success = page_allocator_->RecommitPages(address, size, access);
  if (!success) {
    allocation_status_ = AllocationStatus::kFailedToCommit;
  }
  return success;
}

bool BoundedPageAllocator::DiscardSystemPages(void* address, size_t size) {
  return page_allocator_->DiscardSystemPages(address, size);
}

bool BoundedPageAllocator::DecommitPages(void* address, size_t size) {
  return page_allocator_->DecommitPages(address, size);
}

const char* BoundedPageAllocator::AllocationStatusToString(
    AllocationStatus allocation_status) {
  switch (allocation_status) {
    case AllocationStatus::kSuccess:
      return "Success";
    case AllocationStatus::kFailedToCommit:
      return "Failed to commit";
    case AllocationStatus::kRanOutOfReservation:
      return "Ran out of reservation";
    case AllocationStatus::kHintedAddressTakenOrNotFound:
      return "Hinted address was taken or not found";
  }
}

}  // namespace base
}  // namespace v8
