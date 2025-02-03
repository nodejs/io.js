// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_MIPS64_FRAME_CONSTANTS_MIPS64_H_
#define V8_EXECUTION_MIPS64_FRAME_CONSTANTS_MIPS64_H_

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
  static constexpr int kNextExitFrameFPOffset = -3 * kSystemPointerSize;

  // The offsets for storing the FP and PC of fast API calls.
  static constexpr int kNextFastCallFrameFPOffset =
      kNextExitFrameFPOffset - kSystemPointerSize;
  static constexpr int kNextFastCallFramePCOffset =
      kNextFastCallFrameFPOffset - kSystemPointerSize;
};

class WasmLiftoffSetupFrameConstants : public TypedFrameConstants {
 public:
  // Number of gp parameters, without the instance.
  static constexpr int kNumberOfSavedGpParamRegs = 6;
  static constexpr int kNumberOfSavedFpParamRegs = 7;
  static constexpr int kNumberOfSavedAllParamRegs = 13;

  // On mips64, spilled registers are implicitly sorted backwards by number.
  // We spill:
  //   a2, a3, a4, a5, a6, a7: param1, param2, ..., param6
  // in the following FP-relative order: [a7, a6, a5, a4, a3, a2].
  // The instance slot is in position '0', the first spill slot is at '1'.
  // See wasm::kGpParamRegisters and Builtins::Generate_WasmCompileLazy.
  static constexpr int kInstanceSpillOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(0);

  static constexpr int kParameterSpillsOffset[] = {
      TYPED_FRAME_PUSHED_VALUE_OFFSET(6), TYPED_FRAME_PUSHED_VALUE_OFFSET(5),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(4), TYPED_FRAME_PUSHED_VALUE_OFFSET(3),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(2), TYPED_FRAME_PUSHED_VALUE_OFFSET(1)};

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
  // {v0, v1, a0, a1, a2, a3, a4, a5, a6, a7, t0, t1, t2, s7}
  static constexpr RegList kPushedGpRegs = {v0, v1, a0, a1, a2, a3, a4,
                                            a5, a6, a7, t0, t1, t2, s7};
  // {f0, f2, f4, f6, f8, f10, f12, f14, f16, f18, f20, f22, f24, f26}
  static constexpr DoubleRegList kPushedFpRegs = {
      f0, f2, f4, f6, f8, f10, f12, f14, f16, f18, f20, f22, f24, f26};

  static constexpr int kNumPushedGpRegisters = kPushedGpRegs.Count();
  static constexpr int kNumPushedFpRegisters = kPushedFpRegs.Count();

  static constexpr int kLastPushedGpRegisterOffset =
      -kFixedFrameSizeFromFp - kNumPushedGpRegisters * kSystemPointerSize;
  static constexpr int kLastPushedFpRegisterOffset =
      kLastPushedGpRegisterOffset - kNumPushedFpRegisters * kDoubleSize;

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

#endif  // V8_EXECUTION_MIPS64_FRAME_CONSTANTS_MIPS64_H_
