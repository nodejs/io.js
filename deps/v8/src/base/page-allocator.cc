// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/page-allocator.h"

#include "src/base/platform/platform.h"

#if V8_OS_DARWIN
#include <sys/mman.h>  // For MAP_JIT.
#endif

namespace v8 {
namespace base {

#define STATIC_ASSERT_ENUM(a, b)                            \
  static_assert(static_cast<int>(a) == static_cast<int>(b), \
                "mismatching enum: " #a)

STATIC_ASSERT_ENUM(PageAllocator::kNoAccess,
                   base::OS::MemoryPermission::kNoAccess);
STATIC_ASSERT_ENUM(PageAllocator::kReadWrite,
                   base::OS::MemoryPermission::kReadWrite);
STATIC_ASSERT_ENUM(PageAllocator::kReadWriteExecute,
                   base::OS::MemoryPermission::kReadWriteExecute);
STATIC_ASSERT_ENUM(PageAllocator::kReadExecute,
                   base::OS::MemoryPermission::kReadExecute);
STATIC_ASSERT_ENUM(PageAllocator::kNoAccessWillJitLater,
                   base::OS::MemoryPermission::kNoAccessWillJitLater);

#undef STATIC_ASSERT_ENUM

PageAllocator::PageAllocator()
    : allocate_page_size_(base::OS::AllocatePageSize()),
      commit_page_size_(base::OS::CommitPageSize()) {}

void PageAllocator::SetRandomMmapSeed(int64_t seed) {
  base::OS::SetRandomMmapSeed(seed);
}

void* PageAllocator::GetRandomMmapAddr() {
  return base::OS::GetRandomMmapAddr();
}

void* PageAllocator::AllocatePages(void* hint, size_t size, size_t alignment,
                                   PageAllocator::Permission access) {
#if !V8_HAS_PTHREAD_JIT_WRITE_PROTECT && !V8_HAS_BECORE_JIT_WRITE_PROTECT
  // kNoAccessWillJitLater is only used on Apple Silicon. Map it to regular
  // kNoAccess on other platforms, so code doesn't have to handle both enum
  // values.
  if (access == PageAllocator::kNoAccessWillJitLater) {
    access = PageAllocator::kNoAccess;
  }
#endif
  return base::OS::Allocate(hint, size, alignment,
                            static_cast<base::OS::MemoryPermission>(access));
}

class SharedMemoryMapping : public ::v8::PageAllocator::SharedMemoryMapping {
 public:
  explicit SharedMemoryMapping(PageAllocator* page_allocator, void* ptr,
                               size_t size)
      : page_allocator_(page_allocator), ptr_(ptr), size_(size) {}
  ~SharedMemoryMapping() override { page_allocator_->FreePages(ptr_, size_); }
  void* GetMemory() const override { return ptr_; }

 private:
  PageAllocator* page_allocator_;
  void* ptr_;
  size_t size_;
};

class SharedMemory : public ::v8::PageAllocator::SharedMemory {
 public:
  SharedMemory(PageAllocator* allocator, void* memory, size_t size)
      : allocator_(allocator), ptr_(memory), size_(size) {}
  void* GetMemory() const override { return ptr_; }
  size_t GetSize() const override { return size_; }
  std::unique_ptr<::v8::PageAllocator::SharedMemoryMapping> RemapTo(
      void* new_address) const override {
    if (allocator_->RemapShared(ptr_, new_address, size_)) {
      return std::make_unique<SharedMemoryMapping>(allocator_, new_address,
                                                   size_);
    } else {
      return {};
    }
  }

  ~SharedMemory() override { allocator_->FreePages(ptr_, size_); }

 private:
  PageAllocator* allocator_;
  void* ptr_;
  size_t size_;
};

bool PageAllocator::CanAllocateSharedPages() {
#ifdef V8_OS_LINUX
  return true;
#else
  return false;
#endif
}

std::unique_ptr<v8::PageAllocator::SharedMemory>
PageAllocator::AllocateSharedPages(size_t size, const void* original_address) {
#ifdef V8_OS_LINUX
  void* ptr =
      base::OS::AllocateShared(size, base::OS::MemoryPermission::kReadWrite);
  CHECK_NOT_NULL(ptr);
  memcpy(ptr, original_address, size);
  bool success = base::OS::SetPermissions(
      ptr, size, base::OS::MemoryPermission::kReadWrite);
  CHECK(success);

  auto shared_memory =
      std::make_unique<v8::base::SharedMemory>(this, ptr, size);
  return shared_memory;
#else
  return {};
#endif
}

void* PageAllocator::RemapShared(void* old_address, void* new_address,
                                 size_t size) {
#ifdef V8_OS_LINUX
  return base::OS::RemapShared(old_address, new_address, size);
#else
  return nullptr;
#endif
}

bool PageAllocator::FreePages(void* address, size_t size) {
  base::OS::Free(address, size);
  return true;
}

bool PageAllocator::ReleasePages(void* address, size_t size, size_t new_size) {
  DCHECK_LT(new_size, size);
  base::OS::Release(reinterpret_cast<uint8_t*>(address) + new_size,
                    size - new_size);
  return true;
}

bool PageAllocator::SetPermissions(void* address, size_t size,
                                   PageAllocator::Permission access) {
  return base::OS::SetPermissions(
      address, size, static_cast<base::OS::MemoryPermission>(access));
}

bool PageAllocator::RecommitPages(void* address, size_t size,
                                  PageAllocator::Permission access) {
  return base::OS::RecommitPages(
      address, size, static_cast<base::OS::MemoryPermission>(access));
}

bool PageAllocator::DiscardSystemPages(void* address, size_t size) {
  return base::OS::DiscardSystemPages(address, size);
}

bool PageAllocator::DecommitPages(void* address, size_t size) {
  return base::OS::DecommitPages(address, size);
}

bool PageAllocator::SealPages(void* address, size_t size) {
  return base::OS::SealPages(address, size);
}

}  // namespace base
}  // namespace v8
