// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/wasm/baseline/liftoff-assembler-defs.h"
#if V8_TARGET_ARCH_IA32
#include "src/execution/ia32/frame-constants-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/execution/x64/frame-constants-x64.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/execution/mips64/frame-constants-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/execution/loong64/frame-constants-loong64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/execution/arm/frame-constants-arm.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/execution/arm64/frame-constants-arm64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/execution/s390/frame-constants-s390.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/execution/ppc/frame-constants-ppc.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/execution/riscv/frame-constants-riscv.h"
#endif

#include "src/wasm/baseline/liftoff-register.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {
namespace wasm {

// The registers used by Liftoff and the registers spilled by the
// WasmDebugBreak builtin should match.
static_assert(kLiftoffAssemblerGpCacheRegs ==
              WasmDebugBreakFrameConstants::kPushedGpRegs);

static_assert(kLiftoffAssemblerFpCacheRegs ==
              WasmDebugBreakFrameConstants::kPushedFpRegs);

class WasmRegisterTest : public ::testing::Test {};

TEST_F(WasmRegisterTest, SpreadSetBitsToAdjacentFpRegs) {
  LiftoffRegList input(
  // GP reg selection criteria: an even and an odd register belonging to
  // separate adjacent pairs, and contained in kLiftoffAssemblerGpCacheRegs
  // for the given platform.
#if V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_PPC64 || V8_TARGET_ARCH_LOONG64
      LiftoffRegister::from_code(kGpReg, 4),
      LiftoffRegister::from_code(kGpReg, 7),
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
      LiftoffRegister::from_code(kGpReg, 10),
      LiftoffRegister::from_code(kGpReg, 13),
#else
      LiftoffRegister::from_code(kGpReg, 1),
      LiftoffRegister::from_code(kGpReg, 2),
#endif
      LiftoffRegister::from_code(kFpReg, 1),
      LiftoffRegister::from_code(kFpReg, 4));
  // GP regs are left alone, FP regs are spread to adjacent pairs starting
  // at an even index: 1 → (0, 1) and 4 → (4, 5).
#if V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_IA32
  // RISCV and ia32 don't have code 0 in kLiftoffAssemblerFpCacheRegs
  LiftoffRegList expected =
      input | LiftoffRegList(LiftoffRegister::from_code(kFpReg, 5));
#else
  LiftoffRegList expected =
      input | LiftoffRegList(LiftoffRegister::from_code(kFpReg, 0),
                             LiftoffRegister::from_code(kFpReg, 5));
#endif
  LiftoffRegList actual = input.SpreadSetBitsToAdjacentFpRegs();
  EXPECT_EQ(expected, actual);
}

}  // namespace wasm
}  // namespace internal
}  // namespace v8
