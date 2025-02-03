// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_PAGE_MEMORY_H_
#define V8_HEAP_CPPGC_PAGE_MEMORY_H_

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "include/cppgc/platform.h"
#include "src/base/macros.h"
#include "src/base/platform/mutex.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-config.h"

namespace cppgc {
namespace internal {

class V8_EXPORT_PRIVATE MemoryRegion final {
 public:
  MemoryRegion() = default;
  MemoryRegion(Address base, size_t size) : base_(base), size_(size) {
    DCHECK(base);
    DCHECK_LT(0u, size);
  }

  Address base() const { return base_; }
  size_t size() const { return size_; }
  Address end() const { return base_ + size_; }

  bool Contains(ConstAddress addr) const {
    return (reinterpret_cast<uintptr_t>(addr) -
            reinterpret_cast<uintptr_t>(base_)) < size_;
  }

  bool Contains(const MemoryRegion& other) const {
    return base_ <= other.base() && other.end() <= end();
  }

 private:
  Address base_ = nullptr;
  size_t size_ = 0;
};

// PageMemory provides the backing of a single normal or large page.
class V8_EXPORT_PRIVATE PageMemory final {
 public:
  PageMemory(MemoryRegion overall, MemoryRegion writeable)
      : overall_(overall), writable_(writeable) {
    DCHECK(overall.Contains(writeable));
  }

  const MemoryRegion writeable_region() const { return writable_; }
  const MemoryRegion overall_region() const { return overall_; }

 private:
  MemoryRegion overall_;
  MemoryRegion writable_;
};

class V8_EXPORT_PRIVATE PageMemoryRegion final {
 public:
  PageMemoryRegion(PageAllocator&, MemoryRegion);
  ~PageMemoryRegion();

  const MemoryRegion reserved_region() const { return reserved_region_; }

  const PageMemory GetPageMemory() const {
    return PageMemory(
        MemoryRegion(reserved_region().base(), reserved_region().size()),
        MemoryRegion(reserved_region().base() + kGuardPageSize,
                     reserved_region().size() - 2 * kGuardPageSize));
  }

  // Lookup writeable base for an |address| that's contained in
  // PageMemoryRegion. Filters out addresses that are contained in non-writeable
  // regions (e.g. guard pages).
  inline Address Lookup(ConstAddress address) const {
    const MemoryRegion writeable_region = GetPageMemory().writeable_region();
    return writeable_region.Contains(address) ? writeable_region.base()
                                              : nullptr;
  }

  PageAllocator& allocator() const { return allocator_; }

  // Disallow copy/move.
  PageMemoryRegion(const PageMemoryRegion&) = delete;
  PageMemoryRegion& operator=(const PageMemoryRegion&) = delete;

  void UnprotectForTesting();

 private:
  PageAllocator& allocator_;
  const MemoryRegion reserved_region_;
};

// A PageMemoryRegionTree is a binary search tree of PageMemoryRegions sorted
// by reserved base addresses.
//
// The tree does not keep its elements alive but merely provides indexing
// capabilities.
class V8_EXPORT_PRIVATE PageMemoryRegionTree final {
 public:
  PageMemoryRegionTree();
  ~PageMemoryRegionTree();

  void Add(PageMemoryRegion*);
  void Remove(PageMemoryRegion*);

  inline PageMemoryRegion* Lookup(ConstAddress) const;

 private:
  std::map<ConstAddress, PageMemoryRegion*> set_;
};

// A pool of PageMemory objects represented by the writeable base addresses.
// TODO (v8:14390): Consider sharing the page-pool across multiple threads.
class V8_EXPORT_PRIVATE NormalPageMemoryPool final {
 public:
  // Adds a new entry to the pool.
  void Add(PageMemoryRegion*);
  // Takes a new entry entry from the pool or nullptr in case the pool is empty.
  PageMemoryRegion* Take();

  // Returns the number of entries pooled.
  size_t pooled() const { return pool_.size(); }
  // Memory in the pool which is neither discarded nor decommitted, i.e. the
  // actual cost of pooled memory.
  size_t PooledMemory() const;

  void DiscardPooledPages(PageAllocator& allocator);

  auto& get_raw_pool_for_testing() { return pool_; }

  void SetDecommitPooledPages(bool value) { decommit_pooled_pages_ = value; }
  static constexpr bool kDefaultDecommitPooledPage = false;

 private:
  // The pool of pages that are not returned to the OS. Bounded by
  // `primary_pool_capacity_`.
  struct PooledPageMemoryRegion {
    explicit PooledPageMemoryRegion(PageMemoryRegion* region)
        : region(region) {}
    PageMemoryRegion* region;
    // When a page enters the pool, it's from the heap, so it's neither
    // decommitted nor discarded.
    bool is_decommitted = false;
    bool is_discarded = false;
  };
  std::vector<PooledPageMemoryRegion> pool_;
  bool decommit_pooled_pages_ = kDefaultDecommitPooledPage;
};

// A backend that is used for allocating and freeing normal and large pages.
//
// Internally maintains a set of PageMemoryRegions. The backend keeps its used
// regions alive.
class V8_EXPORT_PRIVATE PageBackend final {
 public:
  PageBackend(PageAllocator& normal_page_allocator,
              PageAllocator& large_page_allocator);
  ~PageBackend();

  // Allocates a normal page from the backend.
  //
  // Returns the writeable base of the region.
  Address TryAllocateNormalPageMemory();

  // Returns normal page memory back to the backend. Expects the
  // |writeable_base| returned by |AllocateNormalMemory()|.
  void FreeNormalPageMemory(Address writeable_base, FreeMemoryHandling);

  // Allocates a large page from the backend.
  //
  // Returns the writeable base of the region.
  Address TryAllocateLargePageMemory(size_t size);

  // Returns large page memory back to the backend. Expects the |writeable_base|
  // returned by |AllocateLargePageMemory()|.
  void FreeLargePageMemory(Address writeable_base);

  // Returns the writeable base if |address| is contained in a valid page
  // memory.
  inline Address Lookup(ConstAddress) const;

  // Disallow copy/move.
  PageBackend(const PageBackend&) = delete;
  PageBackend& operator=(const PageBackend&) = delete;

  void DiscardPooledPages();

  PageMemoryRegionTree& get_page_memory_region_tree_for_testing() {
    return page_memory_region_tree_;
  }

  NormalPageMemoryPool& page_pool() { return page_pool_; }

 private:
  // Guards against concurrent uses of `Lookup()`.
  mutable v8::base::Mutex mutex_;
  PageAllocator& normal_page_allocator_;
  PageAllocator& large_page_allocator_;

  // A PageMemoryRegion for a normal page is kept alive by the
  // `normal_page_memory_regions_` and as such is always present there.
  // It's present in:
  //  - `page_pool_` when it's not used (and available for allocation),
  //  - `page_memory_region_tree_` when used (i.e. allocated).
  NormalPageMemoryPool page_pool_;
  PageMemoryRegionTree page_memory_region_tree_;
  std::unordered_map<PageMemoryRegion*, std::unique_ptr<PageMemoryRegion>>
      normal_page_memory_regions_;
  std::unordered_map<PageMemoryRegion*, std::unique_ptr<PageMemoryRegion>>
      large_page_memory_regions_;
};

// Returns true if the provided allocator supports committing at the required
// granularity.
inline bool SupportsCommittingGuardPages(PageAllocator& allocator) {
  return kGuardPageSize != 0 &&
         kGuardPageSize % allocator.CommitPageSize() == 0;
}

PageMemoryRegion* PageMemoryRegionTree::Lookup(ConstAddress address) const {
  auto it = set_.upper_bound(address);
  // This check also covers set_.size() > 0, since for empty container it is
  // guaranteed that begin() == end().
  if (it == set_.begin()) return nullptr;
  auto* result = std::next(it, -1)->second;
  if (address < result->reserved_region().end()) return result;
  return nullptr;
}

Address PageBackend::Lookup(ConstAddress address) const {
  v8::base::MutexGuard guard(&mutex_);
  PageMemoryRegion* pmr = page_memory_region_tree_.Lookup(address);
  return pmr ? pmr->Lookup(address) : nullptr;
}

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_PAGE_MEMORY_H_
