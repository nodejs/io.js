// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/zone/zone-allocator.h"

#include <list>
#include <vector>

#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

template <template <typename T> typename Allocator>
void TestWithStdContainers() {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  Allocator<int> zone_allocator(&zone);

  // Vector does not require allocator rebinding, list and set do.
  {
    std::vector<int, Allocator<int>> v(10, zone_allocator);
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    int sum_of_v = 0;
    for (int i : v) sum_of_v += i;
    CHECK_EQ(5050, sum_of_v);
  }

  {
    std::list<int, Allocator<int>> l(zone_allocator);
    for (int i = 1; i <= 100; ++i) l.push_back(i);
    int sum_of_l = 0;
    for (int i : l) sum_of_l += i;
    CHECK_EQ(5050, sum_of_l);
  }

  {
    std::set<int, std::less<int>, Allocator<int>> s(zone_allocator);
    for (int i = 1; i <= 100; ++i) s.insert(i);
    int sum_of_s = 0;
    for (int i : s) sum_of_s += i;
    CHECK_EQ(5050, sum_of_s);
  }
}

using ZoneAllocatorTest = TestWithPlatform;

TEST_F(ZoneAllocatorTest, UseWithStdContainers) {
  TestWithStdContainers<ZoneAllocator>();
}

using RecyclingZoneAllocatorTest = TestWithPlatform;

TEST_F(RecyclingZoneAllocatorTest, ReuseSameSize) {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  RecyclingZoneAllocator<int> zone_allocator(&zone);

  int* allocated = zone_allocator.allocate(10);
  zone_allocator.deallocate(allocated, 10);
  CHECK_EQ(zone_allocator.allocate(10), allocated);
}

TEST_F(RecyclingZoneAllocatorTest, ReuseSmallerSize) {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  RecyclingZoneAllocator<int> zone_allocator(&zone);

  int* allocated = zone_allocator.allocate(100);
  zone_allocator.deallocate(allocated, 100);
  CHECK_EQ(zone_allocator.allocate(10), allocated);
}

TEST_F(RecyclingZoneAllocatorTest, DontReuseTooSmallSize) {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  RecyclingZoneAllocator<int> zone_allocator(&zone);

  // The sizeof(FreeBlock) will be larger than a single int, so we can't keep
  // store the free list in the deallocated block.
  int* allocated = zone_allocator.allocate(1);
  zone_allocator.deallocate(allocated, 1);
  CHECK_NE(zone_allocator.allocate(1), allocated);
}

TEST_F(RecyclingZoneAllocatorTest, ReuseMultipleSize) {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  RecyclingZoneAllocator<int> zone_allocator(&zone);

  int* allocated1 = zone_allocator.allocate(10);
  int* allocated2 = zone_allocator.allocate(20);
  int* allocated3 = zone_allocator.allocate(30);
  zone_allocator.deallocate(allocated1, 10);
  zone_allocator.deallocate(allocated2, 20);
  zone_allocator.deallocate(allocated3, 30);
  CHECK_EQ(zone_allocator.allocate(10), allocated3);
  CHECK_EQ(zone_allocator.allocate(10), allocated2);
  CHECK_EQ(zone_allocator.allocate(10), allocated1);
}

TEST_F(RecyclingZoneAllocatorTest, DontChainSmallerSizes) {
  AccountingAllocator accounting_allocator;
  Zone zone(&accounting_allocator, ZONE_NAME);
  RecyclingZoneAllocator<int> zone_allocator(&zone);

  int* allocated1 = zone_allocator.allocate(10);
  int* allocated2 = zone_allocator.allocate(5);
  int* allocated3 = zone_allocator.allocate(10);
  zone_allocator.deallocate(allocated1, 10);
  zone_allocator.deallocate(allocated2, 5);
  zone_allocator.deallocate(allocated3, 10);
  CHECK_EQ(zone_allocator.allocate(5), allocated3);
  CHECK_EQ(zone_allocator.allocate(5), allocated1);
  CHECK_NE(zone_allocator.allocate(5), allocated2);
}

TEST_F(RecyclingZoneAllocatorTest, UseWithStdContainers) {
  TestWithStdContainers<RecyclingZoneAllocator>();
}

}  // namespace internal
}  // namespace v8
