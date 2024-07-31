// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/base/region-allocator.h"
#include "test/unittests/test-utils.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace base {

using Address = RegionAllocator::Address;
using RegionState = RegionAllocator::RegionState;
using v8::internal::KB;
using v8::internal::MB;

TEST(RegionAllocatorTest, SimpleAllocateRegionAt) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.free_size(), kEnd - address);
    CHECK(ra.AllocateRegionAt(address, kPageSize));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free one region and then the allocation should succeed.
  CHECK_EQ(ra.FreeRegion(kBegin), kPageSize);
  CHECK_EQ(ra.free_size(), kPageSize);
  CHECK(ra.AllocateRegionAt(kBegin, kPageSize));

  // Free all the pages.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.FreeRegion(address), kPageSize);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, SimpleAllocateRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    CHECK_EQ(ra.free_size(), kSize - kPageSize * i);
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free one page and ensure that we are able to allocate it again.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.FreeRegion(address), kPageSize);
    CHECK_EQ(ra.AllocateRegion(kPageSize), address);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST(RegionAllocatorTest, SimpleAllocateAlignedRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate regions with different alignments and verify that they are
  // correctly aligned.
  const size_t alignments[] = {kPageSize,     kPageSize * 8, kPageSize,
                               kPageSize * 4, kPageSize * 2, kPageSize * 2,
                               kPageSize * 4, kPageSize * 2};
  for (auto alignment : alignments) {
    Address address = ra.AllocateAlignedRegion(kPageSize, alignment);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK(IsAligned(address, alignment));
  }
  CHECK_EQ(ra.free_size(), 8 * kPageSize);
}

TEST(RegionAllocatorTest, AllocateRegionRandom) {
  const size_t kPageSize = 8 * KB;
  const size_t kPageCountLog = 16;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(153 * MB);
  const Address kEnd = kBegin + kSize;

  base::RandomNumberGenerator rng(GTEST_FLAG_GET(random_seed));
  RegionAllocator ra(kBegin, kSize, kPageSize);

  std::set<Address> allocated_pages;
  // The page addresses must be randomized this number of allocated pages.
  const size_t kRandomizationLimit = ra.max_load_for_randomization_ / kPageSize;
  CHECK_LT(kRandomizationLimit, kPageCount);

  Address last_address = kBegin;
  bool saw_randomized_pages = false;

  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(&rng, kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK(IsAligned(address, kPageSize));
    CHECK_LE(kBegin, address);
    CHECK_LT(address, kEnd);
    CHECK_EQ(allocated_pages.find(address), allocated_pages.end());
    allocated_pages.insert(address);

    saw_randomized_pages |= (address < last_address);
    last_address = address;

    if (i == kRandomizationLimit) {
      // We must evidence allocation randomization till this point.
      // The rest of the allocations may still be randomized depending on
      // the free ranges distribution, however it is not guaranteed.
      CHECK(saw_randomized_pages);
    }
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);
}

TEST(RegionAllocatorTest, AllocateBigRegions) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog) - 1;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCountLog; i++) {
    Address address = ra.AllocateRegion(kPageSize * (size_t{1} << i));
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * ((size_t{1} << i) - 1));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free one page and ensure that we are able to allocate it again.
  for (size_t i = 0; i < kPageCountLog; i++) {
    const size_t size = kPageSize * (size_t{1} << i);
    Address address = kBegin + kPageSize * ((size_t{1} << i) - 1);
    CHECK_EQ(ra.FreeRegion(address), size);
    CHECK_EQ(ra.AllocateRegion(size), address);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST(RegionAllocatorTest, MergeLeftToRightCoalecsingRegions) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region using the following page size pattern:
  // |0|1|22|3333|...
  CHECK_EQ(ra.AllocateRegion(kPageSize), kBegin);
  for (size_t i = 0; i < kPageCountLog; i++) {
    Address address = ra.AllocateRegion(kPageSize * (size_t{1} << i));
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * (size_t{1} << i));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Try to free two coalescing regions and ensure the new page of bigger size
  // can be allocated.
  size_t current_size = kPageSize;
  for (size_t i = 0; i < kPageCountLog; i++) {
    CHECK_EQ(ra.FreeRegion(kBegin), current_size);
    CHECK_EQ(ra.FreeRegion(kBegin + current_size), current_size);
    current_size += current_size;
    CHECK_EQ(ra.AllocateRegion(current_size), kBegin);
  }
  CHECK_EQ(ra.free_size(), 0);
}

TEST(RegionAllocatorTest, MergeRightToLeftCoalecsingRegions) {
  base::RandomNumberGenerator rng(GTEST_FLAG_GET(random_seed));
  const size_t kPageSize = 4 * KB;
  const size_t kPageCountLog = 10;
  const size_t kPageCount = (size_t{1} << kPageCountLog);
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free pages with even indices left-to-right.
  for (size_t i = 0; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * i;
    CHECK_EQ(ra.FreeRegion(address), kPageSize);
  }

  // Free pages with odd indices right-to-left.
  for (size_t i = 1; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * (kPageCount - i);
    CHECK_EQ(ra.FreeRegion(address), kPageSize);
    // Now we should be able to allocate a double-sized page.
    CHECK_EQ(ra.AllocateRegion(kPageSize * 2), address - kPageSize);
    // .. but there's a window for only one such page.
    CHECK_EQ(ra.AllocateRegion(kPageSize * 2),
             RegionAllocator::kAllocationFailure);
  }

  // Free all the double-sized pages.
  for (size_t i = 0; i < kPageCount; i += 2) {
    Address address = kBegin + kPageSize * i;
    CHECK_EQ(ra.FreeRegion(address), kPageSize * 2);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, Fragmentation) {
  const size_t kPageSize = 64 * KB;
  const size_t kPageCount = 9;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = ra.AllocateRegion(kPageSize);
    CHECK_NE(address, RegionAllocator::kAllocationFailure);
    CHECK_EQ(address, kBegin + kPageSize * i);
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // Free pages in the following order and check the freed size.
  struct {
    size_t page_index_to_free;
    size_t expected_page_count;
  } testcase[] = {          // .........
                  {0, 9},   // x........
                  {2, 9},   // x.x......
                  {4, 9},   // x.x.x....
                  {6, 9},   // x.x.x.x..
                  {8, 9},   // x.x.x.x.x
                  {1, 7},   // xxx.x.x.x
                  {7, 5},   // xxx.x.xxx
                  {3, 3},   // xxxxx.xxx
                  {5, 1}};  // xxxxxxxxx
  CHECK_EQ(kPageCount, arraysize(testcase));

  CHECK_EQ(ra.all_regions_.size(), kPageCount);
  for (size_t i = 0; i < kPageCount; i++) {
    Address address = kBegin + kPageSize * testcase[i].page_index_to_free;
    CHECK_EQ(ra.FreeRegion(address), kPageSize);
    CHECK_EQ(ra.all_regions_.size(), testcase[i].expected_page_count);
  }

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, FindRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 16;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);
  const Address kEnd = kBegin + kSize;

  RegionAllocator ra(kBegin, kSize, kPageSize);

  // Allocate the whole region.
  for (Address address = kBegin; address < kEnd; address += kPageSize) {
    CHECK_EQ(ra.free_size(), kEnd - address);
    CHECK(ra.AllocateRegionAt(address, kPageSize));
  }

  // No free regions left, the allocation should fail.
  CHECK_EQ(ra.free_size(), 0);
  CHECK_EQ(ra.AllocateRegion(kPageSize), RegionAllocator::kAllocationFailure);

  // The out-of region requests must return end iterator.
  CHECK_EQ(ra.FindRegion(kBegin - 1), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kBegin - kPageSize), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kBegin / 2), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd + kPageSize), ra.all_regions_.end());
  CHECK_EQ(ra.FindRegion(kEnd * 2), ra.all_regions_.end());

  for (Address address = kBegin; address < kEnd; address += kPageSize / 4) {
    RegionAllocator::AllRegionsSet::iterator region_iter =
        ra.FindRegion(address);
    CHECK_NE(region_iter, ra.all_regions_.end());
    RegionAllocator::Region* region = *region_iter;
    Address region_start = RoundDown(address, kPageSize);
    CHECK_EQ(region->begin(), region_start);
    CHECK_LE(region->begin(), address);
    CHECK_LT(address, region->end());
  }
}

TEST(RegionAllocatorTest, TrimRegion) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 64;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  Address address = kBegin + 13 * kPageSize;
  size_t size = 37 * kPageSize;
  size_t free_size = kSize - size;
  CHECK(ra.AllocateRegionAt(address, size));

  size_t trim_size = kPageSize;
  do {
    CHECK_EQ(ra.CheckRegion(address), size);
    CHECK_EQ(ra.free_size(), free_size);

    trim_size = std::min(size, trim_size);
    size -= trim_size;
    free_size += trim_size;
    CHECK_EQ(ra.TrimRegion(address, size), trim_size);
    trim_size *= 2;
  } while (size != 0);

  // Check that the whole region is free and can be fully allocated.
  CHECK_EQ(ra.free_size(), kSize);
  CHECK_EQ(ra.AllocateRegion(kSize), kBegin);
}

TEST(RegionAllocatorTest, AllocateExcluded) {
  const size_t kPageSize = 4 * KB;
  const size_t kPageCount = 64;
  const size_t kSize = kPageSize * kPageCount;
  const Address kBegin = static_cast<Address>(kPageSize * 153);

  RegionAllocator ra(kBegin, kSize, kPageSize);

  Address address = kBegin + 13 * kPageSize;
  size_t size = 37 * kPageSize;
  CHECK(ra.AllocateRegionAt(address, size, RegionState::kExcluded));

  // The region is not free and cannot be allocated at again.
  CHECK(!ra.IsFree(address, size));
  CHECK(!ra.AllocateRegionAt(address, size));
  auto region_iter = ra.FindRegion(address);

  CHECK((*region_iter)->is_excluded());

  // It's not possible to free or trim an excluded region.
  CHECK_EQ(ra.FreeRegion(address), 0);
  CHECK_EQ(ra.TrimRegion(address, kPageSize), 0);
}

}  // namespace base
}  // namespace v8
