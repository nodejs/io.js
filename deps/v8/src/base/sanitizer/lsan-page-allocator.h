// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_SANITIZER_LSAN_PAGE_ALLOCATOR_H_
#define V8_BASE_SANITIZER_LSAN_PAGE_ALLOCATOR_H_

#include <set>

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/compiler-specific.h"
#if defined(LEAK_SANITIZER)
#include "src/base/platform/mutex.h"
#endif

namespace v8 {
namespace base {

// This is a v8::PageAllocator implementation that decorates provided page
// allocator object with leak sanitizer notifications when LEAK_SANITIZER
// is defined.
class V8_BASE_EXPORT LsanPageAllocator : public v8::PageAllocator {
 public:
  explicit LsanPageAllocator(v8::PageAllocator* page_allocator);
  ~LsanPageAllocator() override = default;

  size_t AllocatePageSize() override { return allocate_page_size_; }

  size_t CommitPageSize() override { return commit_page_size_; }

  void SetRandomMmapSeed(int64_t seed) override {
    return page_allocator_->SetRandomMmapSeed(seed);
  }

  void* GetRandomMmapAddr() override {
    return page_allocator_->GetRandomMmapAddr();
  }

  void* AllocatePages(void* address, size_t size, size_t alignment,
                      PageAllocator::Permission access) override;

  std::unique_ptr<SharedMemory> AllocateSharedPages(
      size_t size, const void* original_address) override;

  bool CanAllocateSharedPages() override;

  bool FreePages(void* address, size_t size) override;

  bool ReleasePages(void* address, size_t size, size_t new_size) override;

  bool SetPermissions(void* address, size_t size,
                      PageAllocator::Permission access) override {
    return page_allocator_->SetPermissions(address, size, access);
  }

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override {
    return page_allocator_->RecommitPages(address, size, access);
  }

  bool DiscardSystemPages(void* address, size_t size) override {
    return page_allocator_->DiscardSystemPages(address, size);
  }

  bool DecommitPages(void* address, size_t size) override {
    return page_allocator_->DecommitPages(address, size);
  }

 private:
  v8::PageAllocator* const page_allocator_;
  const size_t allocate_page_size_;
  const size_t commit_page_size_;
#if defined(LEAK_SANITIZER)
  base::Mutex not_registered_regions_mutex_;
  std::set<void*> not_registered_regions_;
#endif
};

}  // namespace base
}  // namespace v8
#endif  // V8_BASE_SANITIZER_LSAN_PAGE_ALLOCATOR_H_
