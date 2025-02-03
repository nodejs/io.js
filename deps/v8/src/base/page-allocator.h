// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_PAGE_ALLOCATOR_H_
#define V8_BASE_PAGE_ALLOCATOR_H_

#include <memory>

#include "include/v8-platform.h"
#include "src/base/base-export.h"
#include "src/base/compiler-specific.h"

namespace v8 {
namespace base {

class SharedMemory;

class V8_BASE_EXPORT PageAllocator
    : public NON_EXPORTED_BASE(::v8::PageAllocator) {
 public:
  PageAllocator();
  ~PageAllocator() override = default;

  size_t AllocatePageSize() override { return allocate_page_size_; }

  size_t CommitPageSize() override { return commit_page_size_; }

  void SetRandomMmapSeed(int64_t seed) override;

  void* GetRandomMmapAddr() override;

  void* AllocatePages(void* hint, size_t size, size_t alignment,
                      PageAllocator::Permission access) override;

  bool CanAllocateSharedPages() override;

  std::unique_ptr<v8::PageAllocator::SharedMemory> AllocateSharedPages(
      size_t size, const void* original_address) override;

  bool FreePages(void* address, size_t size) override;

  bool ReleasePages(void* address, size_t size, size_t new_size) override;

  bool SetPermissions(void* address, size_t size,
                      PageAllocator::Permission access) override;

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override;

  bool DiscardSystemPages(void* address, size_t size) override;

  bool DecommitPages(void* address, size_t size) override;

  bool SealPages(void* address, size_t size) override;

 private:
  friend class v8::base::SharedMemory;

  void* RemapShared(void* old_address, void* new_address, size_t size);

  const size_t allocate_page_size_;
  const size_t commit_page_size_;
};

}  // namespace base
}  // namespace v8
#endif  // V8_BASE_PAGE_ALLOCATOR_H_
