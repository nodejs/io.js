// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ASSEMBLER_ARCH_H_
#define V8_CODEGEN_ASSEMBLER_ARCH_H_

#include "src/codegen/assembler.h"

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/assembler-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/assembler-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/codegen/arm64/assembler-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/codegen/arm/assembler-arm.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/codegen/ppc/assembler-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/codegen/mips64/assembler-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/codegen/loong64/assembler-loong64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/codegen/s390/assembler-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/codegen/riscv/assembler-riscv.h"
#else
#error Unknown architecture.
#endif

#endif  // V8_CODEGEN_ASSEMBLER_ARCH_H_
