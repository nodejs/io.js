// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_ARM64_FRAME_CONSTANTS_ARM64_H_
#define V8_EXECUTION_ARM64_FRAME_CONSTANTS_ARM64_H_

#include "src/base/bits.h"
#include "src/base/macros.h"
#include "src/codegen/register.h"
#include "src/codegen/reglist.h"
#include "src/common/globals.h"
#include "src/execution/frame-constants.h"

namespace v8 {
namespace internal {

// The layout of an EntryFrame is as follows:
//
//         BOTTOM OF THE STACK   HIGHEST ADDRESS
//  slot      Entry frame
//       +---------------------+-----------------------
// -19   | saved register d15  |
// ...   |        ...          |
// -12   | saved register d8   |
//       |- - - - - - - - - - -|
// -11   | saved register x28  |
// ...   |        ...          |
//  -2   | saved register x19  |
//       |- - - - - - - - - - -|
//  -1   |   saved lr (x30)    |
//       |- - - - - - - - - - -|
//   0   |   saved fp (x29)    |  <-- frame ptr
//       |- - - - - - - - - - -|
//   1   | stack frame marker  |
//       |      (ENTRY)        |
//       |- - - - - - - - - - -|
//   2   | stack frame marker  |
//       |        (0)          |
//       |- - - - - - - - - - -|
//   3   |     C entry FP      |
//       |- - - - - - - - - - -|
//   4   |   JS entry frame    |
//       |       marker        |
//       |- - - - - - - - - - -|
//   5   |  fast api call fp   |
//       |- - - - - - - - - - -|
//   6   |  fast api call pc   |  <-- stack ptr
//  -----+---------------------+-----------------------
//          TOP OF THE STACK     LOWEST ADDRESS
//
class EntryFrameConstants : public AllStatic {
 public:
  // This is the offset to where JSEntry pushes the current value of
  // Isolate::c_entry_fp onto the stack.
  static constexpr int kNextExitFrameFPOffset = -3 * kSystemPointerSize;
  // The offsets for storing the FP and PC of fast API calls.
  static constexpr int kNextFastCallFrameFPOffset = -5 * kSystemPointerSize;
  static constexpr int kNextFastCallFramePCOffset = -6 * kSystemPointerSize;

  static constexpr int kFixedFrameSize = 6 * kSystemPointerSize;

  // The following constants are defined so we can static-assert their values
  // near the relevant JSEntry assembly code, not because they're actually very
  // useful.
  static constexpr int kCalleeSavedRegisterBytesPushedBeforeFpLrPair =
      18 * kSystemPointerSize;
  static constexpr int kCalleeSavedRegisterBytesPushedAfterFpLrPair = 0;
  static constexpr int kOffsetToCalleeSavedRegisters = 0;

  // These offsets refer to the immediate caller (a native frame), not to the
  // previous JS exit frame like kCallerFPOffset above.
  static constexpr int kDirectCallerFPOffset =
      kCalleeSavedRegisterBytesPushedAfterFpLrPair +
      kOffsetToCalleeSavedRegisters;
  static constexpr int kDirectCallerPCOffset =
      kDirectCallerFPOffset + 1 * kSystemPointerSize;
  static constexpr int kDirectCallerSPOffset =
      kDirectCallerPCOffset + 1 * kSystemPointerSize +
      kCalleeSavedRegisterBytesPushedBeforeFpLrPair;
};

class WasmLiftoffSetupFrameConstants : public TypedFrameConstants {
 public:
  // Number of gp parameters, without the instance.
  static constexpr int kNumberOfSavedGpParamRegs = 6;
  static constexpr int kNumberOfSavedFpParamRegs = 8;

  // On arm, spilled registers are implicitly sorted backwards by number.
  // We spill:
  //   x0, x2, x3, x4, x5, x6: param1, param2, ..., param6
  // in the following FP-relative order: [x6, x5, x4, x3, x2, x0].
  // The instance slot is in position '0', the first spill slot is at '1'.
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
  // x16: ip0, x17: ip1, x18: platform register, x26: root, x28: base, x29: fp,
  // x30: lr, x31: xzr.
  static constexpr RegList kPushedGpRegs = {
      x0,  x1,  x2,  x3,  x4,  x5,  x6,  x7,  x8,  x9,  x10, x11,
      x12, x13, x14, x15, x19, x20, x21, x22, x23, x24, x25, x27};

  // We push FpRegs as 128-bit SIMD registers, so 16-byte frame alignment
  // is guaranteed regardless of register count.
  static constexpr DoubleRegList kPushedFpRegs = {
      d0,  d1,  d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9,  d10, d11, d12, d13,
      d14, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27};

  static constexpr int kNumPushedGpRegisters = kPushedGpRegs.Count();
  static_assert(kNumPushedGpRegisters % 2 == 0,
                "stack frames need to be 16-byte aligned");

  static constexpr int kNumPushedFpRegisters = kPushedFpRegs.Count();

  static constexpr int kLastPushedGpRegisterOffset =
      // Header is padded to 16 byte (see {MacroAssembler::EnterFrame}).
      -RoundUp<16>(TypedFrameConstants::kFixedFrameSizeFromFp) -
      kSystemPointerSize * kNumPushedGpRegisters;
  static constexpr int kLastPushedFpRegisterOffset =
      kLastPushedGpRegisterOffset - kSimd128Size * kNumPushedFpRegisters;

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

#endif  // V8_EXECUTION_ARM64_FRAME_CONSTANTS_ARM64_H_
