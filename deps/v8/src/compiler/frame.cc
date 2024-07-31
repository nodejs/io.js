// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/frame.h"

namespace v8 {
namespace internal {
namespace compiler {

Frame::Frame(int fixed_frame_size_in_slots, Zone* zone)
    : fixed_slot_count_(fixed_frame_size_in_slots),
      allocated_registers_(nullptr),
      allocated_double_registers_(nullptr),
      zone_(zone) {
  slot_allocator_.AllocateUnaligned(fixed_frame_size_in_slots);
}

void Frame::AlignFrame(int alignment) {
#if DEBUG
  spill_slots_finished_ = true;
  frame_aligned_ = true;
#endif
  // In the calculations below we assume that alignment is a power of 2.
  DCHECK(base::bits::IsPowerOfTwo(alignment));
  int alignment_in_slots = AlignedSlotAllocator::NumSlotsForWidth(alignment);

  // We have to align return slots separately, because they are claimed
  // separately on the stack.
  const int mask = alignment_in_slots - 1;
  int return_delta = alignment_in_slots - (return_slot_count_ & mask);
  if (return_delta != alignment_in_slots) {
    return_slot_count_ += return_delta;
  }
  int delta = alignment_in_slots - (slot_allocator_.Size() & mask);
  if (delta != alignment_in_slots) {
    slot_allocator_.Align(alignment_in_slots);
    if (spill_slot_count_ != 0) {
      spill_slot_count_ += delta;
    }
  }
}

void FrameAccessState::MarkHasFrame(bool state) {
  has_frame_ = state;
  SetFrameAccessToDefault();
}

void FrameAccessState::SetFPRelativeOnly(bool state) {
  fp_relative_only_ = state;
}

void FrameAccessState::SetFrameAccessToDefault() {
  if (has_frame()) {
    SetFrameAccessToFP();
  } else {
    SetFrameAccessToSP();
  }
}

FrameOffset FrameAccessState::GetFrameOffset(int spill_slot) const {
  const int frame_offset = FrameSlotToFPOffset(spill_slot);
  if (access_frame_with_fp()) {
    return FrameOffset::FromFramePointer(frame_offset);
  } else {
    // No frame. Retrieve all parameters relative to stack pointer.
    int sp_offset = frame_offset + GetSPToFPOffset();
    DCHECK_GE(sp_offset, 0);
    return FrameOffset::FromStackPointer(sp_offset);
  }
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8
