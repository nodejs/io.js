// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/free-list.h"

#include <memory>
#include <numeric>
#include <vector>

#include "src/base/bits.h"
#include "src/heap/cppgc/globals.h"
#include "src/heap/cppgc/heap-object-header.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {
namespace {

class Block {
 public:
  Block() = default;
  explicit Block(size_t size) : address_(calloc(1, size)), size_(size) {}

  Block(Block&& other) V8_NOEXCEPT : address_(other.address_),
                                     size_(other.size_) {
    other.address_ = nullptr;
    other.size_ = 0;
  }

  Block& operator=(Block&& other) V8_NOEXCEPT {
    address_ = other.address_;
    size_ = other.size_;
    other.address_ = nullptr;
    other.size_ = 0;
    return *this;
  }

  ~Block() { free(address_); }

  void* Address() const { return address_; }
  size_t Size() const { return size_; }

 private:
  void* address_ = nullptr;
  size_t size_ = 0;
};

std::vector<Block> CreateEntries() {
  static constexpr size_t kFreeListEntrySizeLog2 =
      v8::base::bits::WhichPowerOfTwo(kFreeListEntrySize);
  std::vector<Block> vector;
  vector.reserve(kPageSizeLog2);
  for (size_t i = kFreeListEntrySizeLog2; i < kPageSizeLog2; ++i) {
    vector.emplace_back(static_cast<size_t>(1u) << i);
  }
  return vector;
}

FreeList CreatePopulatedFreeList(const std::vector<Block>& blocks) {
  FreeList list;
  for (const auto& block : blocks) {
    list.Add({block.Address(), block.Size()});
  }
  return list;
}

}  // namespace

TEST(FreeListTest, Empty) {
  FreeList list;
  EXPECT_TRUE(list.IsEmpty());
  EXPECT_EQ(0u, list.Size());

  auto block = list.Allocate(16);
  EXPECT_EQ(nullptr, block.address);
  EXPECT_EQ(0u, block.size);
}

TEST(FreeListTest, Add) {
  auto blocks = CreateEntries();
  FreeList list = CreatePopulatedFreeList(blocks);
  EXPECT_FALSE(list.IsEmpty());
  const size_t allocated_size = std::accumulate(
      blocks.cbegin(), blocks.cend(), 0u,
      [](size_t acc, const Block& b) { return acc + b.Size(); });
  EXPECT_EQ(allocated_size, list.Size());
}

TEST(FreeListTest, AddWasted) {
  FreeList list;
  alignas(HeapObjectHeader) uint8_t buffer[sizeof(HeapObjectHeader)];
  list.Add({buffer, sizeof(buffer)});
  EXPECT_EQ(0u, list.Size());
  EXPECT_TRUE(list.IsEmpty());
}

TEST(FreeListTest, Clear) {
  auto blocks = CreateEntries();
  FreeList list = CreatePopulatedFreeList(blocks);
  list.Clear();
  EXPECT_EQ(0u, list.Size());
  EXPECT_TRUE(list.IsEmpty());
}

TEST(FreeListTest, Move) {
  {
    auto blocks = CreateEntries();
    FreeList list1 = CreatePopulatedFreeList(blocks);
    const size_t expected_size = list1.Size();
    FreeList list2 = std::move(list1);
    EXPECT_EQ(expected_size, list2.Size());
    EXPECT_FALSE(list2.IsEmpty());
    EXPECT_EQ(0u, list1.Size());
    EXPECT_TRUE(list1.IsEmpty());
  }
  {
    auto blocks1 = CreateEntries();
    FreeList list1 = CreatePopulatedFreeList(blocks1);
    const size_t expected_size = list1.Size();

    auto blocks2 = CreateEntries();
    FreeList list2 = CreatePopulatedFreeList(blocks2);

    list2 = std::move(list1);
    EXPECT_EQ(expected_size, list2.Size());
    EXPECT_FALSE(list2.IsEmpty());
    EXPECT_EQ(0u, list1.Size());
    EXPECT_TRUE(list1.IsEmpty());
  }
}

TEST(FreeListTest, Append) {
  auto blocks1 = CreateEntries();
  FreeList list1 = CreatePopulatedFreeList(blocks1);
  const size_t list1_size = list1.Size();

  auto blocks2 = CreateEntries();
  FreeList list2 = CreatePopulatedFreeList(blocks2);
  const size_t list2_size = list1.Size();

  list2.Append(std::move(list1));
  EXPECT_EQ(list1_size + list2_size, list2.Size());
  EXPECT_FALSE(list2.IsEmpty());
  EXPECT_EQ(0u, list1.Size());
  EXPECT_TRUE(list1.IsEmpty());
}

#ifdef DEBUG
TEST(FreeListTest, AppendSelf) {
  auto blocks = CreateEntries();
  FreeList list = CreatePopulatedFreeList(blocks);
  // Appending a free list to itself should fail in debug builds.
  EXPECT_DEATH_IF_SUPPORTED({ list.Append(std::move(list)); }, "");
}
#endif

TEST(FreeListTest, Contains) {
  auto blocks = CreateEntries();
  FreeList list = CreatePopulatedFreeList(blocks);

  for (const auto& block : blocks) {
    EXPECT_TRUE(list.ContainsForTesting({block.Address(), block.Size()}));
  }
}

TEST(FreeListTest, Allocate) {
  static constexpr size_t kFreeListEntrySizeLog2 =
      v8::base::bits::WhichPowerOfTwo(kFreeListEntrySize);

  std::vector<Block> blocks;
  blocks.reserve(kPageSizeLog2);
  for (size_t i = kFreeListEntrySizeLog2; i < kPageSizeLog2; ++i) {
    blocks.emplace_back(static_cast<size_t>(1u) << i);
  }

  FreeList list = CreatePopulatedFreeList(blocks);

  // Try allocate from the biggest block.
  for (auto it = blocks.rbegin(); it < blocks.rend(); ++it) {
    const auto result = list.Allocate(it->Size());
    EXPECT_EQ(it->Address(), result.address);
    EXPECT_EQ(it->Size(), result.size);
  }

  EXPECT_EQ(0u, list.Size());
  EXPECT_TRUE(list.IsEmpty());

  // Check that allocation fails for empty list:
  const auto empty_block = list.Allocate(8);
  EXPECT_EQ(nullptr, empty_block.address);
  EXPECT_EQ(0u, empty_block.size);
}

}  // namespace internal
}  // namespace cppgc
