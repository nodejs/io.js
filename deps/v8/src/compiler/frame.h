// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_FRAME_H_
#define V8_COMPILER_FRAME_H_

#include "src/base/bits.h"
#include "src/codegen/aligned-slot-allocator.h"
#include "src/execution/frame-constants.h"
#include "src/utils/bit-vector.h"

namespace v8 {
namespace internal {
namespace compiler {

class CallDescriptor;

// Collects the spill slot and other frame slot requirements for a compiled
// function. Frames are usually populated by the register allocator and are used
// by Linkage to generate code for the prologue and epilogue to compiled
// code. Frame objects must be considered immutable once they've been
// instantiated and the basic information about the frame has been collected
// into them. Mutable state associated with the frame is stored separately in
// FrameAccessState.
//
// Frames are divided up into four regions.
// - The first is the fixed header, which always has a constant size and can be
//   predicted before code generation begins depending on the type of code being
//   generated.
// - The second is the region for spill slots, which is immediately below the
//   fixed header and grows as the register allocator needs to spill to the
//   stack and asks the frame for more space.
// - The third region, which contains the callee-saved registers must be
//   reserved after register allocation, since its size can only be precisely
//   determined after register allocation once the number of used callee-saved
//   register is certain.
// - The fourth region is a scratch area for return values from other functions
//   called, if multiple returns cannot all be passed in registers. This region
//   Must be last in a stack frame, so that it is positioned immediately below
//   the stack frame of a callee to store to.
//
// The frame region immediately below the fixed header contains spill slots
// starting at slot 4 for JSFunctions.  The callee-saved frame region below that
// starts at 4+spill_slot_count_.  Callee stack slots correspond to
// parameters that are accessible through negative slot ids.
//
// Every slot of a caller or callee frame is accessible by the register
// allocator and gap resolver with a SpillSlotOperand containing its
// corresponding slot id.
//
// Below an example JSFunction Frame with slot ids, frame regions and contents:
//
//  slot      JS frame
//       +-----------------+--------------------------------
//  -n-1 |  parameter n    |                            ^
//       |- - - - - - - - -|                            |
//  -n   |  parameter n-1  |                          Caller
//  ...  |       ...       |                       frame slots
//  -2   |  parameter 1    |                       (slot < 0)
//       |- - - - - - - - -|                            |
//  -1   |  parameter 0    |                            v
//  -----+-----------------+--------------------------------
//   0   |   return addr   |   ^                        ^
//       |- - - - - - - - -|   |                        |
//   1   | saved frame ptr | Fixed                      |
//       |- - - - - - - - -| Header <-- frame ptr       |
//   2   |Context/Frm. Type|   |                        |
//       |- - - - - - - - -|   |                        |
//   3   |   [JSFunction]  |   v                        |
//       +-----------------+----                        |
//   4   |    spill 1      |   ^                      Callee
//       |- - - - - - - - -|   |                   frame slots
//  ...  |      ...        | Spill slots           (slot >= 0)
//       |- - - - - - - - -|   |                        |
//  m+3  |    spill m      |   v                        |
//       +-----------------+----                        |
//  m+4  |  callee-saved 1 |   ^                        |
//       |- - - - - - - - -|   |                        |
//       |      ...        | Callee-saved               |
//       |- - - - - - - - -|   |                        |
// m+r+3 |  callee-saved r |   v                        |
//       +-----------------+----                        |
// m+r+4 |    return 0     |   ^                        |
//       |- - - - - - - - -|   |                        |
//       |      ...        | Return                     |
//       |- - - - - - - - -|   |                        |
//       |    return q-1   |   v                        v
//  -----+-----------------+----- <-- stack ptr -------------
//
class V8_EXPORT_PRIVATE Frame : public ZoneObject {
 public:
  explicit Frame(int fixed_frame_size_in_slots, Zone* zone);
  Frame(const Frame&) = delete;
  Frame& operator=(const Frame&) = delete;

  inline int GetTotalFrameSlotCount() const {
    return slot_allocator_.Size() + return_slot_count_;
  }
  inline int GetFixedSlotCount() const { return fixed_slot_count_; }
  inline int GetSpillSlotCount() const { return spill_slot_count_; }
  inline int GetReturnSlotCount() const { return return_slot_count_; }

  void SetAllocatedRegisters(BitVector* regs) {
    DCHECK_NULL(allocated_registers_);
    allocated_registers_ = regs;
  }

  void SetAllocatedDoubleRegisters(BitVector* regs) {
    DCHECK_NULL(allocated_double_registers_);
    allocated_double_registers_ = regs;
  }

  bool DidAllocateDoubleRegisters() const {
    return !allocated_double_registers_->IsEmpty();
  }

  void AlignSavedCalleeRegisterSlots(int alignment = kDoubleSize) {
    DCHECK(!frame_aligned_);
#if DEBUG
    spill_slots_finished_ = true;
#endif
    DCHECK(base::bits::IsPowerOfTwo(alignment));
    DCHECK_LE(alignment, kSimd128Size);
    int alignment_in_slots = AlignedSlotAllocator::NumSlotsForWidth(alignment);
    int padding = slot_allocator_.Align(alignment_in_slots);
    spill_slot_count_ += padding;
  }

  void AllocateSavedCalleeRegisterSlots(int count) {
    DCHECK(!frame_aligned_);
#if DEBUG
    spill_slots_finished_ = true;
#endif
    slot_allocator_.AllocateUnaligned(count);
  }

  int AllocateSpillSlot(int width, int alignment = 0, bool is_tagged = false) {
    DCHECK_EQ(GetTotalFrameSlotCount(),
              fixed_slot_count_ + spill_slot_count_ + return_slot_count_);
    DCHECK_IMPLIES(is_tagged, width == sizeof(uintptr_t));
    DCHECK_IMPLIES(is_tagged, alignment == sizeof(uintptr_t));
    // Never allocate spill slots after the callee-saved slots are defined.
    DCHECK(!spill_slots_finished_);
    DCHECK(!frame_aligned_);
    int actual_width = std::max({width, AlignedSlotAllocator::kSlotSize});
    int actual_alignment =
        std::max({alignment, AlignedSlotAllocator::kSlotSize});
    int slots = AlignedSlotAllocator::NumSlotsForWidth(actual_width);
    int old_end = slot_allocator_.Size();
    int slot;
    if (actual_width == actual_alignment) {
      // Simple allocation, alignment equal to width.
      slot = slot_allocator_.Allocate(slots);
    } else {
      // Complex allocation, alignment different from width.
      if (actual_alignment > AlignedSlotAllocator::kSlotSize) {
        // Alignment required.
        int alignment_in_slots =
            AlignedSlotAllocator::NumSlotsForWidth(actual_alignment);
        slot_allocator_.Align(alignment_in_slots);
      }
      slot = slot_allocator_.AllocateUnaligned(slots);
    }
    int end = slot_allocator_.Size();

    spill_slot_count_ += end - old_end;
    int result_slot = slot + slots - 1;
    if (is_tagged) tagged_slots_bits_.Add(result_slot, zone_);
    return result_slot;
  }

  void EnsureReturnSlots(int count) {
    DCHECK(!frame_aligned_);
    return_slot_count_ = std::max(return_slot_count_, count);
  }

  void AlignFrame(int alignment = kDoubleSize);

  int ReserveSpillSlots(size_t slot_count) {
    DCHECK_EQ(0, spill_slot_count_);
    DCHECK(!frame_aligned_);
    spill_slot_count_ += static_cast<int>(slot_count);
    slot_allocator_.AllocateUnaligned(static_cast<int>(slot_count));
    return slot_allocator_.Size() - 1;
  }

  const GrowableBitVector& tagged_slots() const { return tagged_slots_bits_; }

 private:
  int fixed_slot_count_;
  int spill_slot_count_ = 0;
  // Account for return slots separately. Conceptually, they follow all
  // allocated spill slots.
  int return_slot_count_ = 0;
  AlignedSlotAllocator slot_allocator_;
  BitVector* allocated_registers_;
  BitVector* allocated_double_registers_;
  Zone* zone_;
  GrowableBitVector tagged_slots_bits_;
#if DEBUG
  bool spill_slots_finished_ = false;
  bool frame_aligned_ = false;
#endif
};

// Represents an offset from either the stack pointer or frame pointer.
class FrameOffset {
 public:
  inline bool from_stack_pointer() { return (offset_ & 1) == kFromSp; }
  inline bool from_frame_pointer() { return (offset_ & 1) == kFromFp; }
  inline int offset() { return offset_ & ~1; }

  inline static FrameOffset FromStackPointer(int offset) {
    DCHECK_EQ(0, offset & 1);
    return FrameOffset(offset | kFromSp);
  }

  inline static FrameOffset FromFramePointer(int offset) {
    DCHECK_EQ(0, offset & 1);
    return FrameOffset(offset | kFromFp);
  }

 private:
  explicit FrameOffset(int offset) : offset_(offset) {}

  int offset_;  // Encodes SP or FP in the low order bit.

  static const int kFromSp = 1;
  static const int kFromFp = 0;
};

// Encapsulates the mutable state maintained during code generation about the
// current function's frame.
class FrameAccessState : public ZoneObject {
 public:
  explicit FrameAccessState(const Frame* const frame)
      : frame_(frame),
        access_frame_with_fp_(false),
        fp_relative_only_(false),
        sp_delta_(0),
        has_frame_(false) {}

  const Frame* frame() const { return frame_; }
  V8_EXPORT_PRIVATE void MarkHasFrame(bool state);
  void SetFPRelativeOnly(bool state);
  bool FPRelativeOnly() { return fp_relative_only_; }

  int sp_delta() const { return sp_delta_; }
  void ClearSPDelta() { sp_delta_ = 0; }
  void IncreaseSPDelta(int amount) { sp_delta_ += amount; }

  bool access_frame_with_fp() const { return access_frame_with_fp_; }

  // Regardless of how we access slots on the stack - using sp or fp - do we
  // have a frame, at the current stage in code generation.
  bool has_frame() const { return has_frame_; }

  void SetFrameAccessToDefault();
  void SetFrameAccessToFP() { access_frame_with_fp_ = true; }
  void SetFrameAccessToSP() { access_frame_with_fp_ = false; }

  int GetSPToFPSlotCount() const {
    int frame_slot_count =
        (has_frame() ? frame()->GetTotalFrameSlotCount() : kElidedFrameSlots) -
        StandardFrameConstants::kFixedSlotCountAboveFp;
    return frame_slot_count + sp_delta();
  }
  int GetSPToFPOffset() const {
    return GetSPToFPSlotCount() * kSystemPointerSize;
  }

  // Get the frame offset for a given spill slot. The location depends on the
  // calling convention and the specific frame layout, and may thus be
  // architecture-specific. Negative spill slots indicate arguments on the
  // caller's frame.
  FrameOffset GetFrameOffset(int spill_slot) const;

 private:
  const Frame* const frame_;
  bool access_frame_with_fp_;
  bool fp_relative_only_;
  int sp_delta_;
  bool has_frame_;
};
}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_FRAME_H_
