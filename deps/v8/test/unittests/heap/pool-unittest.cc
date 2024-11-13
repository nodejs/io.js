// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <optional>

#include "src/base/region-allocator.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"
#include "src/heap/memory-allocator.h"
#include "src/heap/spaces-inl.h"
#include "src/utils/ostreams.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

// This is a v8::PageAllocator implementation that decorates provided page
// allocator object with page tracking functionality.
class TrackingPageAllocator : public ::v8::PageAllocator {
 public:
  explicit TrackingPageAllocator(v8::PageAllocator* page_allocator)
      : page_allocator_(page_allocator),
        allocate_page_size_(page_allocator_->AllocatePageSize()),
        commit_page_size_(page_allocator_->CommitPageSize()),
        region_allocator_(kNullAddress, size_t{0} - commit_page_size_,
                          commit_page_size_) {
    CHECK_NOT_NULL(page_allocator);
    CHECK(IsAligned(allocate_page_size_, commit_page_size_));
  }
  ~TrackingPageAllocator() override = default;

  size_t AllocatePageSize() override { return allocate_page_size_; }

  size_t CommitPageSize() override { return commit_page_size_; }

  void SetRandomMmapSeed(int64_t seed) override {
    return page_allocator_->SetRandomMmapSeed(seed);
  }

  void* GetRandomMmapAddr() override {
    return page_allocator_->GetRandomMmapAddr();
  }

  void* AllocatePages(void* address, size_t size, size_t alignment,
                      PageAllocator::Permission access) override {
    void* result =
        page_allocator_->AllocatePages(address, size, alignment, access);
    if (result) {
      // Mark pages as used.
      Address current_page = reinterpret_cast<Address>(result);
      CHECK(IsAligned(current_page, allocate_page_size_));
      CHECK(IsAligned(size, allocate_page_size_));
      CHECK(region_allocator_.AllocateRegionAt(current_page, size));
      Address end = current_page + size;
      while (current_page < end) {
        PageState state{access, access != kNoAccess};
        page_permissions_.insert({current_page, state});
        current_page += commit_page_size_;
      }
    }
    return result;
  }

  bool FreePages(void* address, size_t size) override {
    bool result = page_allocator_->FreePages(address, size);
    if (result) {
      // Mark pages as free.
      Address start = reinterpret_cast<Address>(address);
      CHECK(IsAligned(start, allocate_page_size_));
      CHECK(IsAligned(size, allocate_page_size_));
      size_t freed_size = region_allocator_.FreeRegion(start);
      CHECK(IsAligned(freed_size, commit_page_size_));
      CHECK_EQ(RoundUp(freed_size, allocate_page_size_), size);
      auto start_iter = page_permissions_.find(start);
      CHECK_NE(start_iter, page_permissions_.end());
      auto end_iter = page_permissions_.lower_bound(start + size);
      page_permissions_.erase(start_iter, end_iter);
    }
    return result;
  }

  bool ReleasePages(void* address, size_t size, size_t new_size) override {
    bool result = page_allocator_->ReleasePages(address, size, new_size);
    if (result) {
      Address start = reinterpret_cast<Address>(address);
      CHECK(IsAligned(start, allocate_page_size_));
      CHECK(IsAligned(size, commit_page_size_));
      CHECK(IsAligned(new_size, commit_page_size_));
      CHECK_LT(new_size, size);
      CHECK_EQ(region_allocator_.TrimRegion(start, new_size), size - new_size);
      auto start_iter = page_permissions_.find(start + new_size);
      CHECK_NE(start_iter, page_permissions_.end());
      auto end_iter = page_permissions_.lower_bound(start + size);
      page_permissions_.erase(start_iter, end_iter);
    }
    return result;
  }

  bool RecommitPages(void* address, size_t size,
                     PageAllocator::Permission access) override {
    bool result = page_allocator_->RecommitPages(address, size, access);
    if (result) {
      // Check that given range had given access permissions.
      CheckPagePermissions(reinterpret_cast<Address>(address), size, access,
                           {});
      UpdatePagePermissions(reinterpret_cast<Address>(address), size, access,
                            true);
    }
    return result;
  }

  bool DiscardSystemPages(void* address, size_t size) override {
    bool result = page_allocator_->DiscardSystemPages(address, size);
    if (result) {
      UpdatePagePermissions(reinterpret_cast<Address>(address), size, {},
                            false);
    }
    return result;
  }

  bool DecommitPages(void* address, size_t size) override {
    bool result = page_allocator_->DecommitPages(address, size);
    if (result) {
      // Mark pages as non-accessible.
      UpdatePagePermissions(reinterpret_cast<Address>(address), size, kNoAccess,
                            false);
    }
    return result;
  }

  bool SetPermissions(void* address, size_t size,
                      PageAllocator::Permission access) override {
    bool result = page_allocator_->SetPermissions(address, size, access);
    if (result) {
      bool committed = access != kNoAccess && access != kNoAccessWillJitLater;
      UpdatePagePermissions(reinterpret_cast<Address>(address), size, access,
                            committed);
    }
    return result;
  }

  // Returns true if all the allocated pages were freed.
  bool IsEmpty() { return page_permissions_.empty(); }

  void CheckIsFree(Address address, size_t size) {
    CHECK(IsAligned(address, allocate_page_size_));
    CHECK(IsAligned(size, allocate_page_size_));
    EXPECT_TRUE(region_allocator_.IsFree(address, size));
  }

  void CheckPagePermissions(Address address, size_t size,
                            PageAllocator::Permission access,
                            std::optional<bool> committed = {true}) {
    CHECK_IMPLIES(committed.has_value() && committed.value(),
                  access != PageAllocator::kNoAccess);
    ForEachPage(address, size, [=](PagePermissionsMap::value_type* value) {
      if (committed.has_value()) {
        EXPECT_EQ(committed.value(), value->second.committed);
      }
      EXPECT_EQ(access, value->second.access);
    });
  }

  void Print(const char* comment) const {
    i::StdoutStream os;
    os << "\n========================================="
       << "\nTracingPageAllocator state: ";
    if (comment) os << comment;
    os << "\n-----------------------------------------\n";
    region_allocator_.Print(os);
    os << "-----------------------------------------"
       << "\nPage permissions:";
    if (page_permissions_.empty()) {
      os << " empty\n";
      return;
    }
    os << "\n" << std::hex << std::showbase;

    Address contiguous_region_start = static_cast<Address>(-1);
    Address contiguous_region_end = contiguous_region_start;
    PageAllocator::Permission contiguous_region_access =
        PageAllocator::kNoAccess;
    bool contiguous_region_access_committed = false;
    for (auto& pair : page_permissions_) {
      if (contiguous_region_end == pair.first &&
          pair.second.access == contiguous_region_access &&
          pair.second.committed == contiguous_region_access_committed) {
        contiguous_region_end += commit_page_size_;
        continue;
      }
      if (contiguous_region_start != contiguous_region_end) {
        PrintRegion(os, contiguous_region_start, contiguous_region_end,
                    contiguous_region_access,
                    contiguous_region_access_committed);
      }
      contiguous_region_start = pair.first;
      contiguous_region_end = pair.first + commit_page_size_;
      contiguous_region_access = pair.second.access;
      contiguous_region_access_committed = pair.second.committed;
    }
    if (contiguous_region_start != contiguous_region_end) {
      PrintRegion(os, contiguous_region_start, contiguous_region_end,
                  contiguous_region_access, contiguous_region_access_committed);
    }
  }

 private:
  struct PageState {
    PageAllocator::Permission access;
    bool committed;
  };
  using PagePermissionsMap = std::map<Address, PageState>;
  using ForEachFn = std::function<void(PagePermissionsMap::value_type*)>;

  static void PrintRegion(std::ostream& os, Address start, Address end,
                          PageAllocator::Permission access, bool committed) {
    os << "  page: [" << start << ", " << end << "), access: ";
    switch (access) {
      case PageAllocator::kNoAccess:
      case PageAllocator::kNoAccessWillJitLater:
        os << "--";
        break;
      case PageAllocator::kRead:
        os << "R";
        break;
      case PageAllocator::kReadWrite:
        os << "RW";
        break;
      case PageAllocator::kReadWriteExecute:
        os << "RWX";
        break;
      case PageAllocator::kReadExecute:
        os << "RX";
        break;
    }
    os << ", committed: " << static_cast<int>(committed) << "\n";
  }

  void ForEachPage(Address address, size_t size, const ForEachFn& fn) {
    CHECK(IsAligned(address, commit_page_size_));
    CHECK(IsAligned(size, commit_page_size_));
    auto start_iter = page_permissions_.find(address);
    // Start page must exist in page_permissions_.
    CHECK_NE(start_iter, page_permissions_.end());
    auto end_iter = page_permissions_.find(address + size - commit_page_size_);
    // Ensure the last but one page exists in page_permissions_.
    CHECK_NE(end_iter, page_permissions_.end());
    // Now make it point to the next element in order to also process is by the
    // following for loop.
    ++end_iter;
    for (auto iter = start_iter; iter != end_iter; ++iter) {
      PagePermissionsMap::value_type& pair = *iter;
      fn(&pair);
    }
  }

  void UpdatePagePermissions(Address address, size_t size,
                             std::optional<PageAllocator::Permission> access,
                             bool committed) {
    ForEachPage(address, size, [=](PagePermissionsMap::value_type* value) {
      if (access.has_value()) {
        value->second.access = access.value();
      }
      value->second.committed = committed;
    });
  }

  v8::PageAllocator* const page_allocator_;
  const size_t allocate_page_size_;
  const size_t commit_page_size_;
  // Region allocator tracks page allocation/deallocation requests.
  base::RegionAllocator region_allocator_;
  // This map keeps track of allocated pages' permissions.
  PagePermissionsMap page_permissions_;
};

// This test is currently incompatible with the sandbox. Enable it
// once the VirtualAddressSpace interface is stable.
#if !V8_OS_FUCHSIA && !V8_ENABLE_SANDBOX

template <typename TMixin>
class PoolTestMixin : public TMixin {
 public:
  PoolTestMixin();
  ~PoolTestMixin() override;
};

class PoolTest : public                                     //
                 WithInternalIsolateMixin<                  //
                     WithIsolateScopeMixin<                 //
                         WithIsolateMixin<                  //
                             PoolTestMixin<                 //
                                 WithDefaultPlatformMixin<  //
                                     ::testing::Test>>>>> {
 public:
  PoolTest() = default;
  ~PoolTest() override = default;
  PoolTest(const PoolTest&) = delete;
  PoolTest& operator=(const PoolTest&) = delete;

  static void FreeProcessWidePtrComprCageForTesting() {
    IsolateGroup::ReleaseGlobal();
  }

  static void DoMixinSetUp() {
    CHECK_NULL(tracking_page_allocator_);
    old_page_allocator_ = GetPlatformPageAllocator();
    tracking_page_allocator_ = new TrackingPageAllocator(old_page_allocator_);
    CHECK(tracking_page_allocator_->IsEmpty());
    CHECK_EQ(old_page_allocator_,
             SetPlatformPageAllocatorForTesting(tracking_page_allocator_));
    old_sweeping_flag_ = i::v8_flags.concurrent_sweeping;
    i::v8_flags.concurrent_sweeping = false;
#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
    // Reinitialize the process-wide pointer cage so it can pick up the
    // TrackingPageAllocator.
    // The pointer cage must be destroyed before the sandbox.
    FreeProcessWidePtrComprCageForTesting();
#ifdef V8_ENABLE_SANDBOX
    // Reinitialze the sandbox so it uses the TrackingPageAllocator.
    GetProcessWideSandbox()->TearDown();
    constexpr bool use_guard_regions = false;
    CHECK(GetProcessWideSandbox()->Initialize(
        tracking_page_allocator_, kSandboxMinimumSize, use_guard_regions));
#endif
    IsolateGroup::InitializeOncePerProcess();
#endif
  }

  static void DoMixinTearDown() {
#ifndef V8_COMPRESS_POINTERS_IN_MULTIPLE_CAGES
    // Free the process-wide cage reservation, otherwise the pages won't be
    // freed until process teardown.
    FreeProcessWidePtrComprCageForTesting();
#endif
#ifdef V8_ENABLE_SANDBOX
    GetProcessWideSandbox()->TearDown();
#endif
    i::v8_flags.concurrent_sweeping = old_sweeping_flag_;
    CHECK(tracking_page_allocator_->IsEmpty());

    // Restore the original v8::PageAllocator and delete the tracking one.
    CHECK_EQ(tracking_page_allocator_,
             SetPlatformPageAllocatorForTesting(old_page_allocator_));
    delete tracking_page_allocator_;
    tracking_page_allocator_ = nullptr;
  }

  Heap* heap() { return isolate()->heap(); }
  MemoryAllocator* allocator() { return heap()->memory_allocator(); }
  MemoryAllocator::Pool* pool() { return allocator()->pool(); }

  TrackingPageAllocator* tracking_page_allocator() {
    return tracking_page_allocator_;
  }

 private:
  static TrackingPageAllocator* tracking_page_allocator_;
  static v8::PageAllocator* old_page_allocator_;
  static bool old_sweeping_flag_;
};

TrackingPageAllocator* PoolTest::tracking_page_allocator_ = nullptr;
v8::PageAllocator* PoolTest::old_page_allocator_ = nullptr;
bool PoolTest::old_sweeping_flag_;

template <typename TMixin>
PoolTestMixin<TMixin>::PoolTestMixin() {
  PoolTest::DoMixinSetUp();
}
template <typename TMixin>
PoolTestMixin<TMixin>::~PoolTestMixin() {
  PoolTest::DoMixinTearDown();
}

// See v8:5945.
TEST_F(PoolTest, UnmapOnTeardown) {
  if (v8_flags.enable_third_party_heap) return;
  PageMetadata* page =
      allocator()->AllocatePage(MemoryAllocator::AllocationMode::kRegular,
                                static_cast<PagedSpace*>(heap()->old_space()),
                                Executability::NOT_EXECUTABLE);
  Address chunk_address = page->ChunkAddress();
  EXPECT_NE(nullptr, page);
  const size_t page_size = tracking_page_allocator()->AllocatePageSize();
  tracking_page_allocator()->CheckPagePermissions(chunk_address, page_size,
                                                  PageAllocator::kReadWrite);

  allocator()->Free(MemoryAllocator::FreeMode::kPool, page);
  tracking_page_allocator()->CheckPagePermissions(chunk_address, page_size,
                                                  PageAllocator::kReadWrite);
  pool()->ReleasePooledChunks();
#ifdef V8_COMPRESS_POINTERS
  // In this mode Isolate uses bounded page allocator which allocates pages
  // inside prereserved region. Thus these pages are kept reserved until
  // the Isolate dies.
  tracking_page_allocator()->CheckPagePermissions(
      chunk_address, page_size, PageAllocator::kNoAccess, false);
#else
  tracking_page_allocator()->CheckIsFree(chunk_address, page_size);
#endif  // V8_COMPRESS_POINTERS
}
#endif  // !V8_OS_FUCHSIA && !V8_ENABLE_SANDBOX

}  // namespace internal
}  // namespace v8
