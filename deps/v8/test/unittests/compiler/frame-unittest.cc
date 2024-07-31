// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/frame.h"

#include "src/codegen/aligned-slot-allocator.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace v8 {
namespace internal {
namespace compiler {

namespace {
constexpr int kSlotSize = AlignedSlotAllocator::kSlotSize;

constexpr int kFixed1 = 1;
constexpr int kFixed3 = 3;
}  // namespace

class FrameTest : public ::testing::Test {
 public:
  FrameTest() = default;
  ~FrameTest() override = default;
};

TEST_F(FrameTest, Constructor) {
  Frame frame(kFixed3, nullptr);
  EXPECT_EQ(kFixed3, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, ReserveSpillSlots) {
  Frame frame(kFixed3, nullptr);
  constexpr int kReserve2 = 2;

  frame.ReserveSpillSlots(kReserve2);
  EXPECT_EQ(kFixed3 + kReserve2, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(kReserve2, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, EnsureReturnSlots) {
  Frame frame(kFixed3, nullptr);
  constexpr int kReturn3 = 3;
  constexpr int kReturn5 = 5;
  constexpr int kReturn2 = 2;

  frame.EnsureReturnSlots(kReturn3);
  EXPECT_EQ(kFixed3 + kReturn3, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn3, frame.GetReturnSlotCount());

  // Returns should grow by 2 slots.
  frame.EnsureReturnSlots(kReturn5);
  EXPECT_EQ(kFixed3 + kReturn5, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn5, frame.GetReturnSlotCount());

  // Returns shouldn't grow.
  frame.EnsureReturnSlots(kReturn2);
  EXPECT_EQ(kFixed3 + kReturn5, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn5, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSavedCalleeRegisterSlots) {
  Frame frame(kFixed3, nullptr);
  constexpr int kFirstSlots = 2;
  constexpr int kSecondSlots = 3;

  frame.AllocateSavedCalleeRegisterSlots(kFirstSlots);
  EXPECT_EQ(kFixed3 + kFirstSlots, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  frame.AllocateSavedCalleeRegisterSlots(kSecondSlots);
  EXPECT_EQ(kFixed3 + kFirstSlots + kSecondSlots,
            frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(0, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AlignSavedCalleeRegisterSlots) {
  Frame frame(kFixed3, nullptr);
  constexpr int kSlots = 2;  // An even number leaves the slots misaligned.

  frame.AllocateSavedCalleeRegisterSlots(kSlots);

  // Align, which should add 1 padding slot.
  frame.AlignSavedCalleeRegisterSlots(2 * kSlotSize);
  EXPECT_EQ(kFixed3 + kSlots + 1, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(1, frame.GetSpillSlotCount());  // padding
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  // Align again, which should not add a padding slot.
  frame.AlignSavedCalleeRegisterSlots(2 * kSlotSize);
  EXPECT_EQ(kFixed3 + kSlots + 1, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(1, frame.GetSpillSlotCount());  // padding
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSpillSlotAligned) {
  Frame frame(kFixed1, nullptr);

  // Allocate a quad slot, which must add 3 padding slots. Frame returns the
  // last index of the 4 slot allocation.
  int end = kFixed1 + 3 + 4;
  int slot = kFixed1 + 3 + 4 - 1;
  EXPECT_EQ(slot, frame.AllocateSpillSlot(4 * kSlotSize, 4 * kSlotSize));
  EXPECT_EQ(end, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(end - kFixed1, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  // Allocate a double slot, which should leave the first padding slot and
  // take the last two slots of padding.
  slot = kFixed1 + 1 + 2 - 1;
  EXPECT_EQ(slot, frame.AllocateSpillSlot(2 * kSlotSize, 2 * kSlotSize));
  EXPECT_EQ(end, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(end - kFixed1, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  // Allocate a single slot, which should take the last padding slot.
  slot = kFixed1;
  EXPECT_EQ(slot, frame.AllocateSpillSlot(kSlotSize, kSlotSize));
  EXPECT_EQ(end, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(end - kFixed1, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSpillSlotAlignedWithReturns) {
  Frame frame(kFixed3, nullptr);
  constexpr int kReturn3 = 3;
  constexpr int kReturn5 = 5;

  frame.EnsureReturnSlots(kReturn3);

  // Allocate a double slot, which must add 1 padding slot. This should occupy
  // slots 4 and 5, and AllocateSpillSlot returns the last slot index.
  EXPECT_EQ(kFixed3 + 2, frame.AllocateSpillSlot(2 * kSlotSize, 2 * kSlotSize));
  EXPECT_EQ(kFixed3 + kReturn3 + 3, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(3, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn3, frame.GetReturnSlotCount());

  frame.EnsureReturnSlots(kReturn5);

  // Allocate a single slot, which should take the padding slot.
  EXPECT_EQ(kFixed3, frame.AllocateSpillSlot(kSlotSize, kSlotSize));
  EXPECT_EQ(kFixed3 + kReturn5 + 3, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(3, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn5, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSpillSlotAndEndSpillArea) {
  Frame frame(kFixed3, nullptr);

  // Allocate a double slot, which must add 1 padding slot.
  EXPECT_EQ(kFixed3 + 2, frame.AllocateSpillSlot(2 * kSlotSize, 2 * kSlotSize));

  // Allocate an unaligned double slot. This should be at the end.
  EXPECT_EQ(kFixed3 + 4, frame.AllocateSpillSlot(2 * kSlotSize));
  EXPECT_EQ(kFixed3 + 5, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(5, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  // Allocate a single slot. This should not be the padding slot, since that
  // area has been closed by the unaligned allocation.
  EXPECT_EQ(kFixed3 + 5, frame.AllocateSpillSlot(kSlotSize, kSlotSize));
  EXPECT_EQ(kFixed3 + 6, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(6, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSpillSlotOverAligned) {
  Frame frame(kFixed1, nullptr);

  // Allocate a 4-aligned double slot, which must add 3 padding slots. This
  // also terminates the slot area. Returns the starting slot in this case.
  EXPECT_EQ(kFixed1 + 4, frame.AllocateSpillSlot(2 * kSlotSize, 4 * kSlotSize));
  EXPECT_EQ(kFixed1 + 5, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(5, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());

  // Allocate a single slot. This should not use any padding slot.
  EXPECT_EQ(kFixed1 + 5, frame.AllocateSpillSlot(kSlotSize, kSlotSize));
  EXPECT_EQ(kFixed1 + 6, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(6, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AllocateSpillSlotUnderAligned) {
  Frame frame(kFixed1, nullptr);

  // Allocate a 1-aligned double slot. This also terminates the slot area.
  EXPECT_EQ(kFixed1 + 1, frame.AllocateSpillSlot(2 * kSlotSize, kSlotSize));
  EXPECT_EQ(kFixed1 + 2, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed1, frame.GetFixedSlotCount());
  EXPECT_EQ(2, frame.GetSpillSlotCount());
  EXPECT_EQ(0, frame.GetReturnSlotCount());
}

TEST_F(FrameTest, AlignFrame) {
  Frame frame(kFixed3, nullptr);
  constexpr int kReturn3 = 3;

  frame.EnsureReturnSlots(kReturn3);

  // Allocate two single slots, which leaves spill slots not 2-aligned.
  EXPECT_EQ(kFixed3, frame.AllocateSpillSlot(kSlotSize, kSlotSize));
  EXPECT_EQ(kFixed3 + 1, frame.AllocateSpillSlot(kSlotSize, kSlotSize));

  // Align to 2 slots. This should pad the spill and return slot areas.
  frame.AlignFrame(2 * kSlotSize);

  EXPECT_EQ(kFixed3 + 3 + kReturn3 + 1, frame.GetTotalFrameSlotCount());
  EXPECT_EQ(kFixed3, frame.GetFixedSlotCount());
  EXPECT_EQ(3, frame.GetSpillSlotCount());
  EXPECT_EQ(kReturn3 + 1, frame.GetReturnSlotCount());
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
