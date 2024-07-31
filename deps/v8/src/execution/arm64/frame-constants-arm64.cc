// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/frame-constants.h"

#if V8_TARGET_ARCH_ARM64

#include "src/execution/arm64/frame-constants-arm64.h"

#include "src/codegen/arm64/assembler-arm64-inl.h"
#include "src/codegen/assembler.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

Register JavaScriptFrame::fp_register() { return v8::internal::fp; }
Register JavaScriptFrame::context_register() { return cp; }
Register JavaScriptFrame::constant_pool_pointer_register() { UNREACHABLE(); }

int UnoptimizedFrameConstants::RegisterStackSlotCount(int register_count) {
  static_assert(InterpreterFrameConstants::kFixedFrameSize % 16 == 0);
  // Round up to a multiple of two, to make the frame a multiple of 16 bytes.
  return RoundUp(register_count, 2);
}

int BuiltinContinuationFrameConstants::PaddingSlotCount(int register_count) {
  // Round the total slot count up to a multiple of two, to make the frame a
  // multiple of 16 bytes.
  int slot_count = kFixedSlotCount + register_count;
  int rounded_slot_count = RoundUp(slot_count, 2);
  return rounded_slot_count - slot_count;
}

// static
intptr_t MaglevFrame::StackGuardFrameSize(int register_input_count) {
  // Include any paddings from kFixedFrameSizeFromFp, an extra slot + padding
  // for the single argument into StackGuardWithGap and finally padded register
  // input count.
  int slot_count = RoundUp(StandardFrameConstants::kFixedSlotCountFromFp, 2) +
                   2 /* argument */ + RoundUp(register_input_count, 2);
  return slot_count * kSystemPointerSize;
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64
