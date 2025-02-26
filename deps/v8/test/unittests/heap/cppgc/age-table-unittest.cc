// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <vector>

#include "include/cppgc/internal/caged-heap-local-data.h"
#include "include/cppgc/internal/caged-heap.h"
#include "src/base/logging.h"
#include "src/heap/cppgc/heap-page.h"
#include "test/unittests/heap/cppgc/tests.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc::internal {

namespace {

class AgeTableTest : public testing::TestSupportingAllocationOnly {
 public:
  using Age = AgeTable::Age;
  using AdjacentCardsPolicy = AgeTable::AdjacentCardsPolicy;
  static constexpr auto kCardSizeInBytes = AgeTable::kCardSizeInBytes;

  AgeTableTest() : age_table_(CagedHeapLocalData::Get().age_table) {
    CagedHeap::CommitAgeTable(*(GetPlatform().GetPageAllocator()));
  }

  ~AgeTableTest() override { age_table_.ResetForTesting(); }

  NormalPage* AllocateNormalPage() {
    RawHeap& heap = Heap::From(GetHeap())->raw_heap();
    auto* space = static_cast<NormalPageSpace*>(
        heap.Space(RawHeap::RegularSpaceType::kNormal1));
    auto* page =
        NormalPage::TryCreate(*Heap::From(GetHeap())->page_backend(), *space);
    CHECK_NOT_NULL(page);
    allocated_pages_.push_back({page, DestroyPage});
    return page;
  }

  LargePage* AllocateLargePage() {
    constexpr size_t kObjectSize = 2 * kLargeObjectSizeThreshold;
    RawHeap& heap = Heap::From(GetHeap())->raw_heap();
    auto* space = static_cast<LargePageSpace*>(
        heap.Space(RawHeap::RegularSpaceType::kLarge));
    auto* page = LargePage::TryCreate(*Heap::From(GetHeap())->page_backend(),
                                      *space, kObjectSize);
    CHECK_NOT_NULL(page);
    allocated_pages_.push_back({page, DestroyPage});
    return page;
  }

  void SetAgeForAddressRange(void* begin, void* end, Age age,
                             AdjacentCardsPolicy adjacent_cards_policy) {
    age_table_.SetAgeForRange(CagedHeap::OffsetFromAddress(begin),
                              CagedHeap::OffsetFromAddress(end), age,
                              adjacent_cards_policy);
  }

  Age GetAge(void* ptr) const {
    return age_table_.GetAge(CagedHeap::OffsetFromAddress(ptr));
  }

  void SetAge(void* ptr, Age age) {
    age_table_.SetAge(CagedHeap::OffsetFromAddress(ptr), age);
  }

  void AssertAgeForAddressRange(void* begin, void* end, Age age) {
    const uintptr_t offset_begin = CagedHeap::OffsetFromAddress(begin);
    const uintptr_t offset_end = CagedHeap::OffsetFromAddress(end);
    for (auto offset = RoundDown(offset_begin, kCardSizeInBytes);
         offset < RoundUp(offset_end, kCardSizeInBytes);
         offset += kCardSizeInBytes)
      EXPECT_EQ(age, age_table_.GetAge(offset));
  }

 private:
  static void DestroyPage(BasePage* page) { BasePage::Destroy(page); }

  std::vector<std::unique_ptr<BasePage, void (*)(BasePage*)>> allocated_pages_;
  AgeTable& age_table_;
};

}  // namespace

TEST_F(AgeTableTest, SetAgeForNormalPage) {
  auto* page = AllocateNormalPage();
  // By default, everything is old.
  AssertAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(), Age::kOld);
  // Set age for the entire page.
  SetAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(), Age::kYoung,
                        AdjacentCardsPolicy::kIgnore);
  // Check that all cards have been set as young.
  AssertAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(),
                           Age::kYoung);
}

TEST_F(AgeTableTest, SetAgeForLargePage) {
  auto* page = AllocateLargePage();
  // By default, everything is old.
  AssertAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(), Age::kOld);
  // Set age for the entire page.
  SetAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(), Age::kYoung,
                        AdjacentCardsPolicy::kIgnore);
  // Check that all cards have been set as young.
  AssertAgeForAddressRange(page->PayloadStart(), page->PayloadEnd(),
                           Age::kYoung);
}

TEST_F(AgeTableTest, SetAgeForSingleCardWithUnalignedAddresses) {
  auto* page = AllocateNormalPage();
  Address object_begin = reinterpret_cast<Address>(
      RoundUp(reinterpret_cast<uintptr_t>(page->PayloadStart()),
              kCardSizeInBytes) +
      1);
  Address object_end = object_begin + kCardSizeInBytes / 2;
  EXPECT_EQ(Age::kOld, GetAge(object_begin));
  // Try mark the card as young. This will mark the card as kMixed, since the
  // card was previously marked as old.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kConsider);
  EXPECT_EQ(Age::kMixed, GetAge(object_begin));
  SetAge(object_begin, Age::kOld);
  // Try mark as old, but ignore ages of outer cards.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kIgnore);
  EXPECT_EQ(Age::kYoung, GetAge(object_begin));
}

TEST_F(AgeTableTest, SetAgeForSingleCardWithAlignedAddresses) {
  auto* page = AllocateNormalPage();
  Address object_begin = reinterpret_cast<Address>(RoundUp(
      reinterpret_cast<uintptr_t>(page->PayloadStart()), kCardSizeInBytes));
  Address object_end = object_begin + kCardSizeInBytes;
  EXPECT_EQ(Age::kOld, GetAge(object_begin));
  EXPECT_EQ(Age::kOld, GetAge(object_end));
  // Try mark the card as young. This will mark the entire card as kYoung, since
  // it's aligned.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kConsider);
  EXPECT_EQ(Age::kYoung, GetAge(object_begin));
  // The end card should not be touched.
  EXPECT_EQ(Age::kOld, GetAge(object_end));
}

TEST_F(AgeTableTest, SetAgeForSingleCardWithAlignedBeginButUnalignedEnd) {
  auto* page = AllocateNormalPage();
  Address object_begin = reinterpret_cast<Address>(RoundUp(
      reinterpret_cast<uintptr_t>(page->PayloadStart()), kCardSizeInBytes));
  Address object_end = object_begin + kCardSizeInBytes + 1;
  EXPECT_EQ(Age::kOld, GetAge(object_begin));
  EXPECT_EQ(Age::kOld, GetAge(object_end));
  // Try mark the card as young. This will mark the entire card as kYoung, since
  // it's aligned.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kConsider);
  EXPECT_EQ(Age::kYoung, GetAge(object_begin));
  // The end card should be marked as mixed.
  EXPECT_EQ(Age::kMixed, GetAge(object_end));
}

TEST_F(AgeTableTest, SetAgeForMultipleCardsWithUnalignedAddresses) {
  static constexpr size_t kNumberOfCards = 4;
  auto* page = AllocateNormalPage();
  Address object_begin = reinterpret_cast<Address>(
      RoundUp(reinterpret_cast<uintptr_t>(page->PayloadStart()),
              kCardSizeInBytes) +
      kCardSizeInBytes / 2);
  Address object_end = object_begin + kNumberOfCards * kCardSizeInBytes;
  AssertAgeForAddressRange(object_begin, object_end, Age::kOld);
  // Try mark the cards as young. The inner 2 cards must be marked as young, the
  // outer cards will be marked as mixed.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kConsider);
  EXPECT_EQ(Age::kMixed, GetAge(object_begin));
  EXPECT_EQ(Age::kYoung, GetAge(object_begin + kCardSizeInBytes));
  EXPECT_EQ(Age::kYoung, GetAge(object_begin + 2 * kCardSizeInBytes));
  EXPECT_EQ(Age::kMixed, GetAge(object_end));
}

TEST_F(AgeTableTest, SetAgeForMultipleCardsConsiderAdjacentCards) {
  static constexpr size_t kNumberOfCards = 4;
  auto* page = AllocateNormalPage();
  Address object_begin = reinterpret_cast<Address>(
      RoundUp(reinterpret_cast<uintptr_t>(page->PayloadStart()),
              kCardSizeInBytes) +
      kCardSizeInBytes / 2);
  Address object_end = object_begin + kNumberOfCards * kCardSizeInBytes;
  // Mark the first and the last card as young.
  SetAge(object_begin, Age::kYoung);
  SetAge(object_end, Age::kYoung);
  // Mark all the cards as young. The inner 2 cards must be marked as young, the
  // outer cards will also be marked as young.
  SetAgeForAddressRange(object_begin, object_end, Age::kYoung,
                        AdjacentCardsPolicy::kConsider);
  EXPECT_EQ(Age::kYoung, GetAge(object_begin));
  EXPECT_EQ(Age::kYoung, GetAge(object_begin + kCardSizeInBytes));
  EXPECT_EQ(Age::kYoung, GetAge(object_begin + 2 * kCardSizeInBytes));
  EXPECT_EQ(Age::kYoung, GetAge(object_end));
}

TEST_F(AgeTableTest, MarkAllCardsAsYoung) {
  uint8_t* heap_start = reinterpret_cast<uint8_t*>(CagedHeapBase::GetBase());
  void* heap_end =
      heap_start + api_constants::kCagedHeapDefaultReservationSize - 1;
  AssertAgeForAddressRange(heap_start, heap_end, Age::kOld);
  SetAgeForAddressRange(heap_start, heap_end, Age::kYoung,
                        AdjacentCardsPolicy::kIgnore);
  AssertAgeForAddressRange(heap_start, heap_end, Age::kYoung);
}

TEST_F(AgeTableTest, AgeTableSize) {
  // The default cage size should yield a 1MB table.
  EXPECT_EQ(1 * kMB, CagedHeapBase::GetAgeTableSize());

  // Pretend there's a larger cage and verify that the age table reserves the
  // correct amount of space for itself.
  size_t age_table_size = AgeTable::CalculateAgeTableSizeForHeapSize(
      api_constants::kCagedHeapDefaultReservationSize * 4);
  EXPECT_EQ(4 * kMB, age_table_size);
}

}  // namespace cppgc::internal
