// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/page-memory.h"

#include <algorithm>
#include <cstddef>
#include <optional>

#include "include/v8config.h"
#include "src/base/macros.h"
#include "src/base/sanitizer/asan.h"
#include "src/heap/cppgc/memory.h"
#include "src/heap/cppgc/platform.h"

#if V8_OS_POSIX
#include <errno.h>
#endif

namespace cppgc {
namespace internal {

namespace {

V8_WARN_UNUSED_RESULT bool TryUnprotect(PageAllocator& allocator,
                                        const MemoryRegion& memory_region) {
  // The allocator needs to support committing the overall range.
  CHECK_EQ(0u, memory_region.size() % allocator.CommitPageSize());
  return allocator.SetPermissions(memory_region.base(), memory_region.size(),
                                  PageAllocator::Permission::kReadWrite);
}

V8_WARN_UNUSED_RESULT bool TryDiscard(PageAllocator& allocator,
                                      const MemoryRegion& memory_region) {
  // See Unprotect().
  CHECK_EQ(0u, memory_region.size() % allocator.CommitPageSize());
  return allocator.DiscardSystemPages(memory_region.base(),
                                      memory_region.size());
}

std::optional<MemoryRegion> ReserveMemoryRegion(PageAllocator& allocator,
                                                size_t allocation_size) {
  void* region_memory =
      allocator.AllocatePages(nullptr, allocation_size, kPageSize,
                              PageAllocator::Permission::kNoAccess);
  if (!region_memory) {
    return std::nullopt;
  }
  const MemoryRegion reserved_region(static_cast<Address>(region_memory),
                                     allocation_size);
  DCHECK_EQ(reserved_region.base() + allocation_size, reserved_region.end());
  return reserved_region;
}

void FreeMemoryRegion(PageAllocator& allocator,
                      const MemoryRegion& reserved_region) {
  // Make sure pages returned to OS are unpoisoned.
  ASAN_UNPOISON_MEMORY_REGION(reserved_region.base(), reserved_region.size());
  allocator.FreePages(reserved_region.base(), reserved_region.size());
}

std::unique_ptr<PageMemoryRegion> CreateNormalPageMemoryRegion(
    PageAllocator& allocator) {
  DCHECK_EQ(0u, kPageSize % allocator.AllocatePageSize());
  const auto region = ReserveMemoryRegion(allocator, kPageSize);
  if (!region) return {};
  auto result = std::unique_ptr<PageMemoryRegion>(
      new PageMemoryRegion(allocator, *region));
  return result;
}

std::unique_ptr<PageMemoryRegion> CreateLargePageMemoryRegion(
    PageAllocator& allocator, size_t length) {
  const auto region = ReserveMemoryRegion(
      allocator, RoundUp(length, allocator.AllocatePageSize()));
  if (!region) return {};
  auto result = std::unique_ptr<PageMemoryRegion>(
      new PageMemoryRegion(allocator, *region));
  return result;
}

}  // namespace

PageMemoryRegion::PageMemoryRegion(PageAllocator& allocator,
                                   MemoryRegion reserved_region)
    : allocator_(allocator), reserved_region_(reserved_region) {}

PageMemoryRegion::~PageMemoryRegion() {
  FreeMemoryRegion(allocator_, region());
}

void PageMemoryRegion::UnprotectForTesting() {
  CHECK(TryUnprotect(allocator_, region()));
}

PageMemoryRegionTree::PageMemoryRegionTree() = default;

PageMemoryRegionTree::~PageMemoryRegionTree() = default;

void PageMemoryRegionTree::Add(PageMemoryRegion* region) {
  DCHECK(region);
  const auto result = set_.emplace(region->region().base(), region);
  USE(result);
  DCHECK(result.second);
}

void PageMemoryRegionTree::Remove(PageMemoryRegion* region) {
  DCHECK(region);
  const auto size = set_.erase(region->region().base());
  USE(size);
  DCHECK_EQ(1u, size);
}

void NormalPageMemoryPool::Add(PageMemoryRegion* pmr) {
  DCHECK_NOT_NULL(pmr);
  DCHECK_EQ(pmr->region().size(), kPageSize);
  // Oilpan requires the pages to be zero-initialized.
  {
    void* base = pmr->region().base();
    const size_t size = pmr->region().size();
    AsanUnpoisonScope unpoison_for_memset(base, size);
    std::memset(base, 0, size);
  }
  pool_.emplace_back(PooledPageMemoryRegion(pmr));
}

PageMemoryRegion* NormalPageMemoryPool::Take() {
  if (pool_.empty()) return nullptr;
  PooledPageMemoryRegion entry = pool_.back();
  DCHECK_NOT_NULL(entry.region);
  pool_.pop_back();
  void* base = entry.region->region().base();
  const size_t size = entry.region->region().size();
  ASAN_UNPOISON_MEMORY_REGION(base, size);

  DCHECK_IMPLIES(!decommit_pooled_pages_, !entry.is_decommitted);
  if (entry.is_decommitted) {
    // Also need to make the pages accessible.
    CHECK(entry.region->allocator().RecommitPages(
        base, size, v8::PageAllocator::kReadWrite));
    bool ok = entry.region->allocator().SetPermissions(
        base, size, v8::PageAllocator::kReadWrite);
    if (!ok) {
#if V8_OS_POSIX
      // Changing permissions can return ENOMEM in several cases, including
      // (since there is PROT_WRITE) when it would exceed the RLIMIT_DATA
      // resource limit, at least on Linux. Check errno in this case, and
      // declare that this is an OOM in this case.
      if (errno == ENOMEM) {
        GetGlobalOOMHandler()("Cannot change page permissions");
      }
#endif
      CHECK(false);
    }
  }
#if DEBUG
  CheckMemoryIsZero(base, size);
#endif
  return entry.region;
}

size_t NormalPageMemoryPool::PooledMemory() const {
  size_t total_size = 0;
  for (auto& entry : pool_) {
    if (entry.is_decommitted || entry.is_discarded) {
      continue;
    }
    total_size += entry.region->region().size();
  }
  return total_size;
}

void NormalPageMemoryPool::ReleasePooledPages(PageAllocator& page_allocator) {
  for (auto& entry : pool_) {
    DCHECK_NOT_NULL(entry.region);
    void* base = entry.region->region().base();
    size_t size = entry.region->region().size();
    // Unpoison the memory before giving back to the OS.
    ASAN_UNPOISON_MEMORY_REGION(base, size);
    if (decommit_pooled_pages_) {
      if (entry.is_decommitted) {
        continue;
      }
      CHECK(page_allocator.DecommitPages(base, size));
      entry.is_decommitted = true;
    } else {
      if (entry.is_discarded) {
        continue;
      }
      CHECK(TryDiscard(page_allocator, entry.region->region()));
      entry.is_discarded = true;
    }
  }
}

PageBackend::PageBackend(PageAllocator& normal_page_allocator,
                         PageAllocator& large_page_allocator)
    : normal_page_allocator_(normal_page_allocator),
      large_page_allocator_(large_page_allocator) {}

PageBackend::~PageBackend() = default;

Address PageBackend::TryAllocateNormalPageMemory() {
  v8::base::SpinningMutexGuard guard(&mutex_);
  if (PageMemoryRegion* cached = page_pool_.Take()) {
    const auto region = cached->region();
    DCHECK_NE(normal_page_memory_regions_.end(),
              normal_page_memory_regions_.find(cached));
    page_memory_region_tree_.Add(cached);
    return region.base();
  }
  auto pmr = CreateNormalPageMemoryRegion(normal_page_allocator_);
  if (!pmr) {
    return nullptr;
  }
  const auto memory_region = pmr->region();
  if (V8_LIKELY(TryUnprotect(normal_page_allocator_, memory_region))) {
    page_memory_region_tree_.Add(pmr.get());
    normal_page_memory_regions_.emplace(pmr.get(), std::move(pmr));
    return memory_region.base();
  }
  return nullptr;
}

void PageBackend::FreeNormalPageMemory(Address writeable_base) {
  v8::base::SpinningMutexGuard guard(&mutex_);
  auto* pmr = page_memory_region_tree_.Lookup(writeable_base);
  DCHECK_NOT_NULL(pmr);
  page_memory_region_tree_.Remove(pmr);
  page_pool_.Add(pmr);
}

Address PageBackend::TryAllocateLargePageMemory(size_t size) {
  v8::base::SpinningMutexGuard guard(&mutex_);
  auto pmr = CreateLargePageMemoryRegion(large_page_allocator_, size);
  if (!pmr) {
    return nullptr;
  }
  const auto memory_region = pmr->region();
  if (V8_LIKELY(TryUnprotect(large_page_allocator_, memory_region))) {
    page_memory_region_tree_.Add(pmr.get());
    large_page_memory_regions_.emplace(pmr.get(), std::move(pmr));
    return memory_region.base();
  }
  return nullptr;
}

void PageBackend::FreeLargePageMemory(Address writeable_base) {
  v8::base::SpinningMutexGuard guard(&mutex_);
  PageMemoryRegion* pmr = page_memory_region_tree_.Lookup(writeable_base);
  page_memory_region_tree_.Remove(pmr);
  auto size = large_page_memory_regions_.erase(pmr);
  USE(size);
  DCHECK_EQ(1u, size);
}

void PageBackend::ReleasePooledPages() {
  page_pool_.ReleasePooledPages(normal_page_allocator_);
}

}  // namespace internal
}  // namespace cppgc
