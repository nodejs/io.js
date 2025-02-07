// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_VIRTUAL_ADDRESS_SPACE_PAGE_ALLOCATOR_H_
#define V8_BASE_VIRTUAL_ADDRESS_SPACE_PAGE_ALLOCATOR_H_

#include <unordered_map>

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/platform/platform.h"

namespace v8 {
namespace base {

// This class bridges a VirtualAddressSpace, the future memory management API,
// to a PageAllocator, the current API.
class V8_BASE_EXPORT VirtualAddressSpacePageAllocator
    : public v8::PageAllocator {
 public:
  using Address = uintptr_t;

  explicit VirtualAddressSpacePageAllocator(v8::VirtualAddressSpace* vas);

  VirtualAddressSpacePageAllocator(const VirtualAddressSpacePageAllocator&) =
      delete;
  VirtualAddressSpacePageAllocator& operator=(
      const VirtualAddressSpacePageAllocator&) = delete;
  ~VirtualAddressSpacePageAllocator() override = default;

  size_t AllocatePageSize() override { return vas_->allocation_granularity(); }

  size_t CommitPageSize() override { return vas_->page_size(); }

  void SetRandomMmapSeed(int64_t seed) override { vas_->SetRandomSeed(seed); }

  void* GetRandomMmapAddr() override {
    return reinterpret_cast<void*>(vas_->RandomPageAddress());
  }

  void* AllocatePages(void* hint, size_t size, size_t alignment,
                      Permission access) override;

  bool FreePages(void* address, size_t size) override;

  bool ReleasePages(void* address, size_t size, size_t new_size) override;

  bool SetPermissions(void* address, size_t size, Permission access) override;

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override;

  bool DiscardSystemPages(void* address, size_t size) override;

  bool DecommitPages(void* address, size_t size) override;

  bool SealPages(void* address, size_t size) override;

 private:
  // Client of this class must keep the VirtualAddressSpace alive during the
  // lifetime of this instance.
  v8::VirtualAddressSpace* vas_;

  // As the VirtualAddressSpace class doesn't support ReleasePages, this map is
  // required to keep track of the original size of resized page allocations.
  // See the ReleasePages implementation.
  std::unordered_map<Address, size_t> resized_allocations_;

  // Mutex guarding the above map.
  Mutex mutex_;
};

}  // namespace base
}  // namespace v8

#endif  // V8_BASE_VIRTUAL_ADDRESS_SPACE_PAGE_ALLOCATOR_H_
