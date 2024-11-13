// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_BOUNDED_PAGE_ALLOCATOR_H_
#define V8_BASE_BOUNDED_PAGE_ALLOCATOR_H_

#include "include/v8-platform.h"
#include "src/base/platform/mutex.h"
#include "src/base/region-allocator.h"

namespace v8 {
namespace base {

// Defines the page initialization mode of a BoundedPageAllocator.
enum class PageInitializationMode {
  // The contents of allocated pages must be zero initialized. This causes any
  // committed pages to be decommitted during FreePages and ReleasePages.
  kAllocatedPagesMustBeZeroInitialized,
  // Allocated pages do not have to be be zero initialized and can contain old
  // data. This is slightly faster as comitted pages are not decommitted
  // during FreePages and ReleasePages, but only made inaccessible.
  kAllocatedPagesCanBeUninitialized,
  // Assume pages are in discarded state and already have the right page
  // permissions. Using this mode requires PageFreeingMode::kDiscard.
  kRecommitOnly,
};

// Defines how BoundedPageAllocator frees pages when FreePages or ReleasePages
// is requested.
enum class PageFreeingMode {
  // Pages are freed/released by setting permissions to kNoAccess. This is the
  // preferred mode when current platform/configuration allows any page
  // permissions reconfiguration.
  kMakeInaccessible,

  // Pages are freed/released by using DiscardSystemPages of the underlying
  // page allocator. This mode should be used for the cases when page permission
  // reconfiguration is not allowed. In particular, on MacOS on ARM64 ("Apple
  // M1"/Apple Silicon) it's not allowed to reconfigure RWX pages to anything
  // else.
  // This mode is not compatible with kAllocatedPagesMustBeZeroInitialized
  // page initialization mode.
  kDiscard,
};

// This is a v8::PageAllocator implementation that allocates pages within the
// pre-reserved region of virtual space. This class requires the virtual space
// to be kept reserved during the lifetime of this object.
// The main application of bounded page allocator are
//  - V8 heap pointer compression which requires the whole V8 heap to be
//    allocated within a contiguous range of virtual address space,
//  - executable page allocation, which allows to use PC-relative 32-bit code
//    displacement on certain 64-bit platforms.
// Bounded page allocator uses other page allocator instance for doing actual
// page allocations.
// The implementation is thread-safe.
class V8_BASE_EXPORT BoundedPageAllocator : public v8::PageAllocator {
 public:
  enum class AllocationStatus {
    kSuccess,
    kFailedToCommit,
    kRanOutOfReservation,
    kHintedAddressTakenOrNotFound,
  };

  using Address = uintptr_t;

  static const char* AllocationStatusToString(AllocationStatus);

  BoundedPageAllocator(v8::PageAllocator* page_allocator, Address start,
                       size_t size, size_t allocate_page_size,
                       PageInitializationMode page_initialization_mode,
                       PageFreeingMode page_freeing_mode);
  BoundedPageAllocator(const BoundedPageAllocator&) = delete;
  BoundedPageAllocator& operator=(const BoundedPageAllocator&) = delete;
  ~BoundedPageAllocator() override = default;

  // These functions are not inlined to avoid https://crbug.com/v8/8275.
  Address begin() const;
  size_t size() const;

  // Returns true if given address is in the range controlled by the bounded
  // page allocator instance.
  bool contains(Address address) const {
    return region_allocator_.contains(address);
  }

  size_t AllocatePageSize() override { return allocate_page_size_; }

  size_t CommitPageSize() override { return commit_page_size_; }

  void SetRandomMmapSeed(int64_t seed) override {
    page_allocator_->SetRandomMmapSeed(seed);
  }

  void* GetRandomMmapAddr() override {
    return page_allocator_->GetRandomMmapAddr();
  }

  void* AllocatePages(void* hint, size_t size, size_t alignment,
                      Permission access) override;

  bool ReserveForSharedMemoryMapping(void* address, size_t size) override;

  // Allocates pages at given address, returns true on success.
  bool AllocatePagesAt(Address address, size_t size, Permission access);

  bool FreePages(void* address, size_t size) override;

  bool ReleasePages(void* address, size_t size, size_t new_size) override;

  bool SetPermissions(void* address, size_t size, Permission access) override;

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override;

  bool DiscardSystemPages(void* address, size_t size) override;

  bool DecommitPages(void* address, size_t size) override;

  AllocationStatus get_last_allocation_status() const {
    return allocation_status_;
  }

 private:
  v8::base::Mutex mutex_;
  const size_t allocate_page_size_;
  const size_t commit_page_size_;
  v8::PageAllocator* const page_allocator_;
  v8::base::RegionAllocator region_allocator_;
  const PageInitializationMode page_initialization_mode_;
  const PageFreeingMode page_freeing_mode_;
  AllocationStatus allocation_status_ = AllocationStatus::kSuccess;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_BOUNDED_PAGE_ALLOCATOR_H_
