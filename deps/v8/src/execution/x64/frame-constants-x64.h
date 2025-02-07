// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_
#define V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/codegen/register.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

class EntryFrameConstants : public AllStatic {
 public:
  // The layout of an EntryFrame is as follows:
  //
  //         BOTTOM OF THE STACK   HIGHEST ADDRESS
  //  slot      Entry frame
  //       +---------------------+-----------------------
  //  -1   |   return address    |
  //       |- - - - - - - - - - -|
  //   0   |      saved fp       |  <-- frame ptr
  //       |- - - - - - - - - - -|
  //   1   | stack frame marker  |
  //       |      (ENTRY)        |
  //       |- - - - - - - - - - -|
  //   2   |       context       |
  //       |- - - - - - - - - - -|
  //   3   | callee-saved regs * |
  //  ...  |         ...         |
  //       |- - - - - - - - - - -|
  //   3   |     C entry FP      |
  //       |- - - - - - - - - - -|
  //   5   |  fast api call fp   |
  //       |- - - - - - - - - - -|
  //   6   |  fast api call pc   |
  //       |- - - - - - - - - - -|
  //   6   |  outermost marker   |  <-- stack ptr
  //  -----+---------------------+-----------------------
  //          TOP OF THE STACK     LOWEST ADDRESS
  // * On Windows the callee-saved registers are (in push order):
  // r12, r13, r14, r15, rdi, rsi, rbx, xmm6, xmm7, xmm8, xmm9, xmm10, xmm11,
  // xmm12, xmm13, xmm14, xmm15
  // xmm register pushes take 16 bytes on the stack.
  // On other OS, the callee-saved registers are (in push order):
  // r12, r13, r14, r15, rbx

  static constexpr int kXMMRegisterSize = 16;
#ifdef V8_TARGET_OS_WIN
  static constexpr int kCalleeSaveXMMRegisters = 10;
  static constexpr int kXMMRegistersBlockSize =
      kXMMRegisterSize * kCalleeSaveXMMRegisters;

  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  // On x64, there are 7 pushq() and 3 Push() calls between setting up rbp and
  // pushing the c_entry_fp, plus we manually allocate kXMMRegistersBlockSize
  // bytes on the stack.
  static constexpr int kNextExitFrameFPOffset = -3 * kSystemPointerSize +
                                                -7 * kSystemPointerSize -
                                                kXMMRegistersBlockSize;

  // Stack offsets for arguments passed to JSEntry.
  static constexpr int kArgcOffset = 6 * kSystemPointerSize;
  static constexpr int kArgvOffset = 7 * kSystemPointerSize;
#else
  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  // On x64, there are 5 pushq() and 3 Push() calls between setting up rbp and
  // pushing the c_entry_fp.
  static constexpr int kNextExitFrameFPOffset =
      -3 * kSystemPointerSize + -5 * kSystemPointerSize;
#endif
  // This are the offsets to where JSEntry pushes the current values of
  // IsolateData::fast_c_call_caller_fp and IsolateData::fast_c_call_caller_pc.
  static constexpr int kNextFastCallFrameFPOffset =
      kNextExitFrameFPOffset - kSystemPointerSize;
  static constexpr int kNextFastCallFramePCOffset =
      kNextFastCallFrameFPOffset - kSystemPointerSize;
};

class WasmLiftoffSetupFrameConstants : public TypedFrameConstants {
 public:
  // Number of gp parameters, without the instance.
  static constexpr int kNumberOfSavedGpParamRegs = 5;
  static constexpr int kNumberOfSavedFpParamRegs = 6;

  // There's one spilled value (which doesn't need visiting) below the instance.
  static constexpr int kInstanceSpillOffset =
      TYPED_FRAME_PUSHED_VALUE_OFFSET(1);

  static constexpr int kParameterSpillsOffset[] = {
      TYPED_FRAME_PUSHED_VALUE_OFFSET(2), TYPED_FRAME_PUSHED_VALUE_OFFSET(3),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(4), TYPED_FRAME_PUSHED_VALUE_OFFSET(5),
      TYPED_FRAME_PUSHED_VALUE_OFFSET(6)};

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
  static constexpr RegList kPushedGpRegs = {rax, rcx, rdx, rbx, rsi,
                                            rdi, r8,  r9,  r12, r15};

  static constexpr DoubleRegList kPushedFpRegs = {xmm0, xmm1, xmm2, xmm3,
                                                  xmm4, xmm5, xmm6, xmm7};

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

#endif  // V8_EXECUTION_X64_FRAME_CONSTANTS_X64_H_
