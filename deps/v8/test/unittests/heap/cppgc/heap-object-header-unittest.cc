// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/cppgc/heap-object-header.h"

#include <atomic>
#include <memory>

#include "include/cppgc/allocation.h"
#include "src/base/atomic-utils.h"
#include "src/base/macros.h"
#include "src/base/platform/platform.h"
#include "src/heap/cppgc/globals.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace cppgc {
namespace internal {

TEST(HeapObjectHeaderTest, Constructor) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(kSize, header.AllocatedSize());
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_TRUE(header.IsInConstruction());
  EXPECT_FALSE(header.IsMarked());
}

TEST(HeapObjectHeaderTest, Payload) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(reinterpret_cast<ConstAddress>(&header) + sizeof(HeapObjectHeader),
            header.ObjectStart());
}

TEST(HeapObjectHeaderTest, PayloadEnd) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(reinterpret_cast<ConstAddress>(&header) + kSize,
            header.ObjectEnd());
}

TEST(HeapObjectHeaderTest, GetGCInfoIndex) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex<AccessMode::kAtomic>());
}

TEST(HeapObjectHeaderTest, AllocatedSize) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity * 23;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(kSize, header.AllocatedSize());
  EXPECT_EQ(kSize, header.AllocatedSize<AccessMode::kAtomic>());
}

TEST(HeapObjectHeaderTest, IsLargeObject) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity * 23;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_EQ(false, header.IsLargeObject());
  EXPECT_EQ(false, header.IsLargeObject<AccessMode::kAtomic>());
  HeapObjectHeader large_header(0, kGCInfoIndex + 1);
  EXPECT_EQ(true, large_header.IsLargeObject());
  EXPECT_EQ(true, large_header.IsLargeObject<AccessMode::kAtomic>());
}

TEST(HeapObjectHeaderTest, MarkObjectAsFullyConstructed) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_TRUE(header.IsInConstruction());
  header.MarkAsFullyConstructed();
  EXPECT_FALSE(header.IsInConstruction());
  // Size shares the same bitfield and should be unaffected by
  // MarkObjectAsFullyConstructed.
  EXPECT_EQ(kSize, header.AllocatedSize());
}

TEST(HeapObjectHeaderTest, TryMark) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity * 7;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_FALSE(header.IsMarked());
  EXPECT_TRUE(header.TryMarkAtomic());
  // GCInfoIndex shares the same bitfield and should be unaffected by
  // TryMarkAtomic.
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_FALSE(header.TryMarkAtomic());
  // GCInfoIndex shares the same bitfield and should be unaffected by
  // TryMarkAtomic.
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_TRUE(header.IsMarked());
}

TEST(HeapObjectHeaderTest, Unmark) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = kAllocationGranularity * 7;
  HeapObjectHeader header(kSize, kGCInfoIndex);
  EXPECT_FALSE(header.IsMarked());
  EXPECT_TRUE(header.TryMarkAtomic());
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_TRUE(header.IsMarked());
  header.Unmark();
  // GCInfoIndex shares the same bitfield and should be unaffected by Unmark.
  EXPECT_EQ(kGCInfoIndex, header.GetGCInfoIndex());
  EXPECT_FALSE(header.IsMarked());
  HeapObjectHeader header2(kSize, kGCInfoIndex);
  EXPECT_FALSE(header2.IsMarked());
  EXPECT_TRUE(header2.TryMarkAtomic());
  EXPECT_TRUE(header2.IsMarked());
  header2.Unmark<AccessMode::kAtomic>();
  // GCInfoIndex shares the same bitfield and should be unaffected by Unmark.
  EXPECT_EQ(kGCInfoIndex, header2.GetGCInfoIndex());
  EXPECT_FALSE(header2.IsMarked());
}

namespace {

struct Payload {
  volatile size_t value{5};
};

class ConcurrentGCThread final : public v8::base::Thread {
 public:
  explicit ConcurrentGCThread(HeapObjectHeader* header, Payload* payload)
      : v8::base::Thread(Options("Thread accessing object.")),
        header_(header),
        payload_(payload) {}

  void Run() final {
    while (header_->IsInConstruction<AccessMode::kAtomic>()) {
    }
    USE(v8::base::AsAtomicPtr(const_cast<size_t*>(&payload_->value))
            ->load(std::memory_order_relaxed));
  }

 private:
  HeapObjectHeader* header_;
  Payload* payload_;
};

}  // namespace

TEST(HeapObjectHeaderTest, ConstructionBitProtectsNonAtomicWrites) {
  // Object publishing: Test checks that non-atomic stores in the payload can be
  // guarded using MarkObjectAsFullyConstructed/IsInConstruction. The test
  // relies on TSAN to find data races.
  constexpr size_t kSize =
      (sizeof(HeapObjectHeader) + sizeof(Payload) + kAllocationMask) &
      ~kAllocationMask;
  alignas(kAllocationGranularity) char data[kSize];
  HeapObjectHeader* header = new (data) HeapObjectHeader(kSize, 1);
  ConcurrentGCThread gc_thread(
      header, reinterpret_cast<Payload*>(header->ObjectStart()));
  CHECK(gc_thread.Start());
  new (header->ObjectStart()) Payload();
  header->MarkAsFullyConstructed();
  gc_thread.Join();
}

#ifdef DEBUG

TEST(HeapObjectHeaderDeathTest, ConstructorTooLargeSize) {
  constexpr GCInfoIndex kGCInfoIndex = 17;
  constexpr size_t kSize = HeapObjectHeader::kMaxSize + 1;
  EXPECT_DEATH_IF_SUPPORTED(HeapObjectHeader header(kSize, kGCInfoIndex), "");
}

TEST(HeapObjectHeaderDeathTest, ConstructorTooLargeGCInfoIndex) {
  constexpr GCInfoIndex kGCInfoIndex = GCInfoTable::kMaxIndex + 1;
  constexpr size_t kSize = kAllocationGranularity;
  EXPECT_DEATH_IF_SUPPORTED(HeapObjectHeader header(kSize, kGCInfoIndex), "");
}

#endif  // DEBUG

}  // namespace internal
}  // namespace cppgc
