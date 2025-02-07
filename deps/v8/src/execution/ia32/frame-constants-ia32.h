// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_IA32_FRAME_CONSTANTS_IA32_H_
#define V8_EXECUTION_IA32_FRAME_CONSTANTS_IA32_H_

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/codegen/register.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

class EntryFrameConstants : public AllStatic {
 public:
  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  static constexpr int kNextExitFrameFPOffset = -6 * kSystemPointerSize;

  // The offsets for storing the FP and PC of fast API calls.
  static constexpr int kNextFastCallFrameFPOffset =
      kNextExitFrameFPOffset - kSystemPointerSize;
  static constexpr int kNextFastCallFramePCOffset =
      kNextFastCallFrameFPOffset - kSystemPointerSize;

  // EntryFrame is used by JSEntry, JSConstructEntry and JSRunMicrotasksEntry.
  // All of them take |root_register_value| as the first parameter.
  static constexpr int kRootRegisterValueOffset = +2 * kSystemPointerSize;

  // Rest of parameters passed to JSEntry and JSConstructEntry.
  static constexpr int kNewTargetArgOffset = +3 * kSystemPointerSize;
  static constexpr int kFunctionArgOffset = +4 * kSystemPointerSize;
  static constexpr int kReceiverArgOffset = +5 * kSystemPointerSize;
  static constexpr int kArgcOffset = +6 * kSystemPointerSize;
  static constexpr int kArgvOffset = +7 * kSystemPointerSize;

  // Rest of parameters passed to JSRunMicrotasksEntry.
  static constexpr int kMicrotaskQueueArgOffset = +3 * kSystemPointerSize;
};

class WasmLiftoffSetupFrameConstants : public TypedFrameConstants {
 public:
  // Number of gp parameters, without the instance.
  static constexpr int kNumberOfSavedGpParamRegs = 3;
  static constexpr int kNumberOfSavedFpParamRegs = 6;

  // There's one spilled value (which doesn't need visiting) below the instance.
  static constexpr int kInstanceSpillOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1);

  static constexpr int kParameterSpillsOffset[] = {
      TYPED_FRAME_PUSHED_VALUE_OFFSET(2), TYPED_FRAME_PUSHED_VALUE_OFFSET(3),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(4)};

  // SP-relative.
  static constexpr int kWasmInstanceDataOffset = 2 * kSystemPointerSize;
  static constexpr int kDeclaredFunctionIndexOffset = 1 * kSystemPointerSize;
  static constexpr int kNativeModuleOffset = 0;
};

class WasmLiftoffFrameConstants : public TypedFrameConstants {
 public:
  static constexpr int kFeedbackVectorOffset = 3 * kSystemPointerSize;
  static constexpr int kInstanceDataOffset = 2 * kSystemPointerSize;
};

// Frame constructed by the {WasmDebugBreak} builtin.
// After pushing the frame type marker, the builtin pushes all Liftoff cache
// registers (see liftoff-assembler-defs.h).
class WasmDebugBreakFrameConstants : public TypedFrameConstants {
 public:
  // Omit ebx, which is the root register.
  static constexpr RegList kPushedGpRegs = {eax, ecx, edx, esi, edi};

  // Omit xmm0, which is not an allocatable fp register.
  // Omit xmm7, which is the kScratchDoubleReg.
  static constexpr DoubleRegList kPushedFpRegs = {xmm1, xmm2, xmm3,
                                                  xmm4, xmm5, xmm6};

  static constexpr int kNumPushedGpRegisters = kPushedGpRegs.Count();
  static constexpr int kNumPushedFpRegisters = kPushedFpRegs.Count();

  static constexpr int kLastPushedGpRegisterOffset =
      -kFixedFrameSizeFromFp - kNumPushedGpRegisters * kSystemPointerSize;
  static constexpr int kLastPushedFpRegisterOffset =
      kLastPushedGpRegisterOffset - kNumPushedFpRegisters * kSimd128Size;

  // Offsets are fp-relative.
  static int GetPushedGpRegisterOffset(int reg_code) {
    DCHECK_NE(0, kPushedGpRegs.bits() & (1 << reg_code));
    uint32_t lower_regs =
        kPushedGpRegs.bits() & ((uint32_t{1} << reg_code) - 1);
    return kLastPushedGpRegisterOffset +
           base::bits::CountPopulation(lower_regs) * kSystemPointerSize;
  }

  static int GetPushedFpRegisterOffset(int reg_code) {
    DCHECK_NE(0, kPushedFpRegs.bits() & (1 << reg_code));
    uint32_t lower_regs =
        kPushedFpRegs.bits() & ((uint32_t{1} << reg_code) - 1);
    return kLastPushedFpRegisterOffset +
           base::bits::CountPopulation(lower_regs) * kSimd128Size;
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_IA32_FRAME_CONSTANTS_IA32_H_
