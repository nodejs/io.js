// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/page-memory.h"

#include <algorithm>

#include "src/base/page-allocator.h"
#include "src/heap/cppgc/platform.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

TEST(MemoryRegionTest, Construct) {
  constexpr size_t kSize = 17;
  uint8_t dummy[kSize];
  const MemoryRegion region(dummy, kSize);
  EXPECT_EQ(dummy, region.base());
  EXPECT_EQ(kSize, region.size());
  EXPECT_EQ(dummy + kSize, region.end());
}

namespace {

Address AtOffset(uint8_t* base, intptr_t offset) {
  return reinterpret_cast<Address>(reinterpret_cast<intptr_t>(base) + offset);
}

}  // namespace

TEST(MemoryRegionTest, ContainsAddress) {
  constexpr size_t kSize = 7;
  uint8_t dummy[kSize];
  const MemoryRegion region(dummy, kSize);
  EXPECT_FALSE(region.Contains(AtOffset(dummy, -1)));
  EXPECT_TRUE(region.Contains(dummy));
  EXPECT_TRUE(region.Contains(dummy + kSize - 1));
  EXPECT_FALSE(region.Contains(AtOffset(dummy, kSize)));
}

TEST(MemoryRegionTest, ContainsMemoryRegion) {
  constexpr size_t kSize = 7;
  uint8_t dummy[kSize];
  const MemoryRegion region(dummy, kSize);
  const MemoryRegion contained_region1(dummy, kSize - 1);
  EXPECT_TRUE(region.Contains(contained_region1));
  const MemoryRegion contained_region2(dummy + 1, kSize - 1);
  EXPECT_TRUE(region.Contains(contained_region2));
  const MemoryRegion not_contained_region1(AtOffset(dummy, -1), kSize);
  EXPECT_FALSE(region.Contains(not_contained_region1));
  const MemoryRegion not_contained_region2(AtOffset(dummy, kSize), 1);
  EXPECT_FALSE(region.Contains(not_contained_region2));
}

TEST(PageMemoryTest, Construct) {
  constexpr size_t kOverallSize = 17;
  uint8_t dummy[kOverallSize];
  const MemoryRegion overall_region(dummy, kOverallSize);
  const MemoryRegion writeable_region(dummy + 1, kOverallSize - 2);
  const PageMemory page_memory(overall_region, writeable_region);
  EXPECT_EQ(dummy, page_memory.overall_region().base());
  EXPECT_EQ(dummy + kOverallSize, page_memory.overall_region().end());
  EXPECT_EQ(dummy + 1, page_memory.writeable_region().base());
  EXPECT_EQ(dummy + kOverallSize - 1, page_memory.writeable_region().end());
}

#if DEBUG

TEST(PageMemoryDeathTest, ConstructNonContainedRegions) {
  constexpr size_t kOverallSize = 17;
  uint8_t dummy[kOverallSize];
  const MemoryRegion overall_region(dummy, kOverallSize);
  const MemoryRegion writeable_region(dummy + 1, kOverallSize);
  EXPECT_DEATH_IF_SUPPORTED(PageMemory(overall_region, writeable_region), "");
}

#endif  // DEBUG

// See the comment in globals.h when setting |kGuardPageSize| for details.
#if !(defined(V8_TARGET_ARCH_ARM64) && defined(V8_OS_MACOS))
TEST(PageMemoryRegionTest, PlatformUsesGuardPages) {
  // This tests that the testing allocator actually uses protected guard
  // regions.
  v8::base::PageAllocator allocator;
#if defined(V8_HOST_ARCH_PPC64) && !defined(_AIX)
  EXPECT_FALSE(SupportsCommittingGuardPages(allocator));
#elif defined(V8_HOST_ARCH_ARM64) || defined(V8_HOST_ARCH_LOONG64)
  if (allocator.CommitPageSize() == 4096) {
    EXPECT_TRUE(SupportsCommittingGuardPages(allocator));
  } else {
    // Arm64 supports both 16k and 64k OS pages.
    EXPECT_FALSE(SupportsCommittingGuardPages(allocator));
  }
#else  // Regular case.
  EXPECT_TRUE(SupportsCommittingGuardPages(allocator));
#endif
}
#endif  // !(defined(V8_TARGET_ARCH_ARM64) && defined(V8_OS_MACOS))

namespace {

V8_NOINLINE uint8_t access(volatile const uint8_t& u) { return u; }

}  // namespace

TEST(PageBackendDeathTest, ReservationIsFreed) {
  // Full sequence as part of the death test macro as otherwise, the macro
  // may expand to statements that re-purpose the previously freed memory
  // and thus not crash.
  EXPECT_DEATH_IF_SUPPORTED(
      v8::base::PageAllocator allocator; Address base; {
        PageBackend backend(allocator, allocator);
        base = backend.TryAllocateLargePageMemory(1024);
      } access(*base);
      , "");
}

TEST(PageBackendDeathTest, FrontGuardPageAccessCrashes) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto* base = backend.TryAllocateNormalPageMemory();
  if (SupportsCommittingGuardPages(allocator)) {
    EXPECT_DEATH_IF_SUPPORTED(access(base[-kGuardPageSize]), "");
  }
}

TEST(PageBackendDeathTest, BackGuardPageAccessCrashes) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto* base = backend.TryAllocateNormalPageMemory();
  if (SupportsCommittingGuardPages(allocator)) {
    EXPECT_DEATH_IF_SUPPORTED(access(base[kPageSize - 2 * kGuardPageSize]), "");
  }
}

TEST(PageBackendTreeTest, AddNormalLookupRemove) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto* writable_base = backend.TryAllocateNormalPageMemory();
  auto* reserved_base = writable_base - kGuardPageSize;
  auto& tree = backend.get_page_memory_region_tree_for_testing();
  ASSERT_EQ(
      reserved_base,
      tree.Lookup(reserved_base)->GetPageMemory().overall_region().base());
  ASSERT_EQ(reserved_base, tree.Lookup(reserved_base + kPageSize - 1)
                               ->GetPageMemory()
                               .overall_region()
                               .base());
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base - 1));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + kPageSize));
  backend.FreeNormalPageMemory(writable_base,
                               FreeMemoryHandling::kDoNotDiscard);
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + kPageSize - 1));
}

TEST(PageBackendTreeTest, AddLargeLookupRemove) {
  v8::base::PageAllocator allocator;
  constexpr size_t kLargeSize = 5012;
  const size_t allocated_page_size =
      RoundUp(kLargeSize + 2 * kGuardPageSize, allocator.AllocatePageSize());
  PageBackend backend(allocator, allocator);
  auto* writable_base = backend.TryAllocateLargePageMemory(kLargeSize);
  auto* reserved_base = writable_base - kGuardPageSize;
  auto& tree = backend.get_page_memory_region_tree_for_testing();
  ASSERT_EQ(
      reserved_base,
      tree.Lookup(reserved_base)->GetPageMemory().overall_region().base());
  ASSERT_EQ(reserved_base, tree.Lookup(reserved_base + allocated_page_size - 1)
                               ->GetPageMemory()
                               .overall_region()
                               .base());
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base - 1));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + allocated_page_size));
  backend.FreeLargePageMemory(writable_base);
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + allocated_page_size - 1));
}

TEST(PageBackendTreeTest, AddLookupRemoveMultiple) {
  v8::base::PageAllocator allocator;
  constexpr size_t kLargeSize = 3127;
  const size_t allocated_page_size =
      RoundUp(kLargeSize + 2 * kGuardPageSize, allocator.AllocatePageSize());

  PageBackend backend(allocator, allocator);
  auto& tree = backend.get_page_memory_region_tree_for_testing();

  auto* writable_normal_base = backend.TryAllocateNormalPageMemory();
  auto* reserved_normal_base = writable_normal_base - kGuardPageSize;
  auto* writable_large_base = backend.TryAllocateLargePageMemory(kLargeSize);
  auto* reserved_large_base = writable_large_base - kGuardPageSize;

  ASSERT_EQ(reserved_normal_base, tree.Lookup(reserved_normal_base)
                                      ->GetPageMemory()
                                      .overall_region()
                                      .base());
  ASSERT_EQ(reserved_normal_base,
            tree.Lookup(reserved_normal_base + kPageSize - 1)
                ->GetPageMemory()
                .overall_region()
                .base());
  ASSERT_EQ(reserved_large_base, tree.Lookup(reserved_large_base)
                                     ->GetPageMemory()
                                     .overall_region()
                                     .base());
  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base + allocated_page_size - 1)
                ->GetPageMemory()
                .overall_region()
                .base());

  backend.FreeNormalPageMemory(writable_normal_base,
                               FreeMemoryHandling::kDoNotDiscard);

  ASSERT_EQ(reserved_large_base, tree.Lookup(reserved_large_base)
                                     ->GetPageMemory()
                                     .overall_region()
                                     .base());
  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base + allocated_page_size - 1)
                ->GetPageMemory()
                .overall_region()
                .base());

  backend.FreeLargePageMemory(writable_large_base);

  ASSERT_EQ(nullptr, tree.Lookup(reserved_large_base));
  ASSERT_EQ(nullptr,
            tree.Lookup(reserved_large_base + allocated_page_size - 1));
}

TEST(PageBackendPoolTest, ConstructorEmpty) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.get_page_pool_for_testing();
  EXPECT_EQ(nullptr, pool.Take());
}

TEST(PageBackendPoolTest, AddTake) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.get_page_pool_for_testing();
  auto& raw_pool = pool.get_raw_pool_for_testing();

  EXPECT_TRUE(raw_pool.empty());
  auto* writable_base1 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());

  backend.FreeNormalPageMemory(writable_base1,
                               FreeMemoryHandling::kDoNotDiscard);
  EXPECT_FALSE(raw_pool.empty());
  EXPECT_TRUE(raw_pool[0]);
  EXPECT_EQ(raw_pool[0]->GetPageMemory().writeable_region().base(),
            writable_base1);

  auto* writable_base2 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());
  EXPECT_EQ(writable_base1, writable_base2);
}

TEST(PageBackendTest, AllocateNormalUsesPool) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  Address writeable_base1 = backend.TryAllocateNormalPageMemory();
  EXPECT_NE(nullptr, writeable_base1);
  backend.FreeNormalPageMemory(writeable_base1,
                               FreeMemoryHandling::kDoNotDiscard);
  Address writeable_base2 = backend.TryAllocateNormalPageMemory();
  EXPECT_NE(nullptr, writeable_base2);
  EXPECT_EQ(writeable_base1, writeable_base2);
}

TEST(PageBackendTest, AllocateLarge) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  Address writeable_base1 = backend.TryAllocateLargePageMemory(13731);
  EXPECT_NE(nullptr, writeable_base1);
  Address writeable_base2 = backend.TryAllocateLargePageMemory(9478);
  EXPECT_NE(nullptr, writeable_base2);
  EXPECT_NE(writeable_base1, writeable_base2);
  backend.FreeLargePageMemory(writeable_base1);
  backend.FreeLargePageMemory(writeable_base2);
}

TEST(PageBackendTest, LookupNormal) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  Address writeable_base = backend.TryAllocateNormalPageMemory();
  if (kGuardPageSize) {
    EXPECT_EQ(nullptr, backend.Lookup(writeable_base - kGuardPageSize));
  }
  EXPECT_EQ(nullptr, backend.Lookup(writeable_base - 1));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base + kPageSize -
                                           2 * kGuardPageSize - 1));
  EXPECT_EQ(nullptr,
            backend.Lookup(writeable_base + kPageSize - 2 * kGuardPageSize));
  if (kGuardPageSize) {
    EXPECT_EQ(nullptr,
              backend.Lookup(writeable_base - kGuardPageSize + kPageSize - 1));
  }
}

TEST(PageBackendTest, LookupLarge) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  constexpr size_t kSize = 7934;
  Address writeable_base = backend.TryAllocateLargePageMemory(kSize);
  if (kGuardPageSize) {
    EXPECT_EQ(nullptr, backend.Lookup(writeable_base - kGuardPageSize));
  }
  EXPECT_EQ(nullptr, backend.Lookup(writeable_base - 1));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base + kSize - 1));
}

TEST(PageBackendDeathTest, DestructingBackendDestroysPageMemory) {
  v8::base::PageAllocator allocator;
  Address base;
  {
    PageBackend backend(allocator, allocator);
    base = backend.TryAllocateNormalPageMemory();
  }
  EXPECT_DEATH_IF_SUPPORTED(access(base[0]), "");
}

}  // namespace internal
}  // namespace cppgc
