// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef INCLUDE_V8_UNWINDER_STATE_H_
#define INCLUDE_V8_UNWINDER_STATE_H_

namespace v8 {

#ifdef V8_TARGET_ARCH_ARM
struct CalleeSavedRegisters {
  void* arm_r4;
  void* arm_r5;
  void* arm_r6;
  void* arm_r7;
  void* arm_r8;
  void* arm_r9;
  void* arm_r10;
};
#elif V8_TARGET_ARCH_X64 || V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_ARM64 ||     \
    V8_TARGET_ARCH_MIPS64 || V8_TARGET_ARCH_PPC64 || V8_TARGET_ARCH_RISCV64 || \
    V8_TARGET_ARCH_S390X || V8_TARGET_ARCH_LOONG64 || V8_TARGET_ARCH_RISCV32
struct CalleeSavedRegisters {};
#else
#error Target architecture was not detected as supported by v8
#endif

}  // namespace v8

#endif  // INCLUDE_V8_UNWINDER _STATE_H_
