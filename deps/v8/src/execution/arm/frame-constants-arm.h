// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ARM_FRAME_CONSTANTS_ARM_H_
#define V8_EXECUTION_ARM_FRAME_CONSTANTS_ARM_H_

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/codegen/register.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

// The layout of an EntryFrame is as follows:
//            TOP OF THE STACK     LOWEST ADDRESS
//         +---------------------+-----------------------
//   -6    |  outermost marker   |
//         |- - - - - - - - - - -|
//   -5    |   fast api call pc  |
//         |- - - - - - - - - - -|
//   -4    |   fast api call fp  |
//         |- - - - - - - - - - -|
//   -3    |      centry fp      |
//         |- - - - - - - - - - -|
//   -2    | stack frame marker  |
//         |- - - - - - - - - - -|
//   -1    | stack frame marker  |
//         |- - - - - - - - - - -|
//   0     |   saved fp (r11)    |  <-- frame ptr
//         |- - - - - - - - - - -|
//   1     |   saved lr (r14)    |
//         |- - - - - - - - - - -|
//  2..3   | saved register d8   |
//  ...    |        ...          |
//  16..17 | saved register d15  |
//         |- - - - - - - - - - -|
//  18     | saved register r4   |
//  ...    |        ...          |
//  24     | saved register r10  |
//    -----+---------------------+-----------------------
//           BOTTOM OF THE STACK   HIGHEST ADDRESS
class EntryFrameConstants : public AllStatic {
 public:
  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  static constexpr int kNextExitFrameFPOffset = -3 * kSystemPointerSize;

  static constexpr int kNextFastCallFrameFPOffset =
      kNextExitFrameFPOffset - kSystemPointerSize;
  static constexpr int kNextFastCallFramePCOffset =
      kNextFastCallFrameFPOffset - kSystemPointerSize;

  // Stack offsets for arguments passed to JSEntry.
  static constexpr int kArgcOffset = +0 * kSystemPointerSize;
  static constexpr int kArgvOffset = +1 * kSystemPointerSize;

  // These offsets refer to the immediate caller (i.e a native frame).
  static constexpr int kDirectCallerFPOffset = 0;
  static constexpr int kDirectCallerPCOffset =
      kDirectCallerFPOffset + 1 * kSystemPointerSize;
  static constexpr int kDirectCallerGeneralRegistersOffset =
      kDirectCallerPCOffset +
      /* saved caller PC */
      kSystemPointerSize +
      /* d8...d15 */
      kNumDoubleCalleeSaved * kDoubleSize;
  static constexpr int kDirectCallerSPOffset =
      kDirectCallerGeneralRegistersOffset +
      /* r4...r10 (i.e. callee saved without fp) */
      (kNumCalleeSaved - 1) * kSystemPointerSize;
};

class WasmLiftoffSetupFrameConstants : public TypedFrameConstants {
 public:
  // Number of gp parameters, without the instance.
  static constexpr int kNumberOfSavedGpParamRegs = 3;
  static constexpr int kNumberOfSavedFpParamRegs = 8;

  // On arm, spilled registers are implicitly sorted backwards by number.
  // We spill:
  //   r3: param0 = instance
  //   r0, r2, r6: param1, param2, param3
  //   lr (== r14): internal usage of the caller
  // in the following FP-relative order: [lr, r6, r3, r2, r0].
  static constexpr int kInstanceSpillOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(2);

  static constexpr int kParameterSpillsOffset[] = {
      TYPED_FRAME_PUSHED_VALUE_OFFSET(4), TYPED_FRAME_PUSHED_VALUE_OFFSET(3),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1)};

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
  // r10: root, r11: fp, r12: ip, r13: sp, r14: lr, r15: pc.
  static constexpr RegList kPushedGpRegs = {r0, r1, r2, r3, r4,
                                            r5, r6, r7, r8, r9};

  // d13: zero, d14-d15: scratch
  static constexpr DoubleRegList kPushedFpRegs = {d0, d1, d2, d3,  d4,  d5, d6,
                                                  d7, d8, d9, d10, d11, d12};

  static constexpr int kNumPushedGpRegisters = kPushedGpRegs.Count();
  static constexpr int kNumPushedFpRegisters = kPushedFpRegs.Count();

  static constexpr int kLastPushedGpRegisterOffset =
      -TypedFrameConstants::kFixedFrameSizeFromFp -
      kSystemPointerSize * kNumPushedGpRegisters;
  static constexpr int kLastPushedFpRegisterOffset =
      kLastPushedGpRegisterOffset - kDoubleSize * kNumPushedFpRegisters;

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
           base::bits::CountPopulation(lower_regs) * kDoubleSize;
  }
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_ARM_FRAME_CONSTANTS_ARM_H_
