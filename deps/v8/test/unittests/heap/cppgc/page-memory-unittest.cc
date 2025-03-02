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

TEST(PageBackendTreeTest, AddNormalLookupRemove) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto* writable_base = backend.TryAllocateNormalPageMemory();
  auto* reserved_base = writable_base;
  auto& tree = backend.get_page_memory_region_tree_for_testing();
  ASSERT_EQ(reserved_base, tree.Lookup(reserved_base)->region().base());
  ASSERT_EQ(reserved_base,
            tree.Lookup(reserved_base + kPageSize - 1)->region().base());
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base - 1));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + kPageSize));
  backend.FreeNormalPageMemory(writable_base);
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base));
  ASSERT_EQ(nullptr, tree.Lookup(reserved_base + kPageSize - 1));
}

TEST(PageBackendTreeTest, AddLargeLookupRemove) {
  v8::base::PageAllocator allocator;
  constexpr size_t kLargeSize = 5012;
  const size_t allocated_page_size =
      RoundUp(kLargeSize, allocator.AllocatePageSize());
  PageBackend backend(allocator, allocator);
  auto* writable_base = backend.TryAllocateLargePageMemory(kLargeSize);
  auto* reserved_base = writable_base;
  auto& tree = backend.get_page_memory_region_tree_for_testing();
  ASSERT_EQ(reserved_base, tree.Lookup(reserved_base)->region().base());
  ASSERT_EQ(
      reserved_base,
      tree.Lookup(reserved_base + allocated_page_size - 1)->region().base());
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
      RoundUp(kLargeSize, allocator.AllocatePageSize());

  PageBackend backend(allocator, allocator);
  auto& tree = backend.get_page_memory_region_tree_for_testing();

  auto* writable_normal_base = backend.TryAllocateNormalPageMemory();
  auto* reserved_normal_base = writable_normal_base;
  auto* writable_large_base = backend.TryAllocateLargePageMemory(kLargeSize);
  auto* reserved_large_base = writable_large_base;

  ASSERT_EQ(reserved_normal_base,
            tree.Lookup(reserved_normal_base)->region().base());
  ASSERT_EQ(reserved_normal_base,
            tree.Lookup(reserved_normal_base + kPageSize - 1)->region().base());
  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base)->region().base());
  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base + allocated_page_size - 1)
                ->region()
                .base());

  backend.FreeNormalPageMemory(writable_normal_base);

  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base)->region().base());
  ASSERT_EQ(reserved_large_base,
            tree.Lookup(reserved_large_base + allocated_page_size - 1)
                ->region()
                .base());

  backend.FreeLargePageMemory(writable_large_base);

  ASSERT_EQ(nullptr, tree.Lookup(reserved_large_base));
  ASSERT_EQ(nullptr,
            tree.Lookup(reserved_large_base + allocated_page_size - 1));
}

TEST(PageBackendPoolTest, ConstructorEmpty) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.page_pool();
  EXPECT_EQ(nullptr, pool.Take());
}

TEST(PageBackendPoolTest, AddTake) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.page_pool();
  auto& raw_pool = pool.get_raw_pool_for_testing();

  EXPECT_TRUE(raw_pool.empty());
  auto* writable_base1 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());

  backend.FreeNormalPageMemory(writable_base1);
  EXPECT_FALSE(raw_pool.empty());
  EXPECT_TRUE(raw_pool[0].region);
  EXPECT_EQ(raw_pool[0].region->region().base(), writable_base1);

  auto* writable_base2 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());
  EXPECT_EQ(writable_base1, writable_base2);
}

namespace {
void AddTakeWithDiscardInBetween(bool decommit_pooled_pages) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.page_pool();
  pool.SetDecommitPooledPages(decommit_pooled_pages);
  auto& raw_pool = pool.get_raw_pool_for_testing();

  EXPECT_TRUE(raw_pool.empty());
  auto* writable_base1 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());
  EXPECT_EQ(0u, pool.PooledMemory());

  backend.FreeNormalPageMemory(writable_base1);
  EXPECT_FALSE(raw_pool.empty());
  EXPECT_TRUE(raw_pool[0].region);
  EXPECT_EQ(raw_pool[0].region->region().base(), writable_base1);
  size_t size = raw_pool[0].region->region().size();
  EXPECT_EQ(size, pool.PooledMemory());

  backend.ReleasePooledPages();
  // Not couting discarded memory.
  EXPECT_EQ(0u, pool.PooledMemory());

  auto* writable_base2 = backend.TryAllocateNormalPageMemory();
  EXPECT_TRUE(raw_pool.empty());
  EXPECT_EQ(0u, pool.PooledMemory());
  EXPECT_EQ(writable_base1, writable_base2);
  // Should not die: memory is writable.
  memset(writable_base2, 12, size);
}
}  // namespace

TEST(PageBackendPoolTest, AddTakeWithDiscardInBetween) {
  AddTakeWithDiscardInBetween(false);
}

TEST(PageBackendPoolTest, AddTakeWithDiscardInBetweenWithDecommit) {
  AddTakeWithDiscardInBetween(true);
}

TEST(PageBackendPoolTest, PoolMemoryAccounting) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  auto& pool = backend.page_pool();

  auto* writable_base1 = backend.TryAllocateNormalPageMemory();
  auto* writable_base2 = backend.TryAllocateNormalPageMemory();
  backend.FreeNormalPageMemory(writable_base1);
  backend.FreeNormalPageMemory(writable_base2);
  size_t normal_page_size =
      pool.get_raw_pool_for_testing()[0].region->region().size();

  EXPECT_EQ(2 * normal_page_size, pool.PooledMemory());
  backend.ReleasePooledPages();
  EXPECT_EQ(0u, pool.PooledMemory());

  auto* writable_base3 = backend.TryAllocateNormalPageMemory();
  backend.FreeNormalPageMemory(writable_base3);
  // One discarded, one not discarded.
  EXPECT_EQ(normal_page_size, pool.PooledMemory());
  backend.ReleasePooledPages();
  EXPECT_EQ(0u, pool.PooledMemory());
}

TEST(PageBackendTest, AllocateNormalUsesPool) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  Address writeable_base1 = backend.TryAllocateNormalPageMemory();
  EXPECT_NE(nullptr, writeable_base1);
  backend.FreeNormalPageMemory(writeable_base1);
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
  EXPECT_EQ(nullptr, backend.Lookup(writeable_base - 1));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base));
  EXPECT_EQ(writeable_base, backend.Lookup(writeable_base + kPageSize - 1));
  EXPECT_EQ(nullptr, backend.Lookup(writeable_base + kPageSize));
}

TEST(PageBackendTest, LookupLarge) {
  v8::base::PageAllocator allocator;
  PageBackend backend(allocator, allocator);
  constexpr size_t kSize = 7934;
  Address writeable_base = backend.TryAllocateLargePageMemory(kSize);
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
