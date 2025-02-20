// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_REGEXP_REGEXP_MACRO_ASSEMBLER_ARCH_H_
#define V8_REGEXP_REGEXP_MACRO_ASSEMBLER_ARCH_H_

#include "src/regexp/regexp-macro-assembler.h"

#if V8_TARGET_ARCH_IA32
#include "src/regexp/ia32/regexp-macro-assembler-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/regexp/x64/regexp-macro-assembler-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/regexp/arm64/regexp-macro-assembler-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/regexp/arm/regexp-macro-assembler-arm.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/regexp/ppc/regexp-macro-assembler-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/regexp/mips64/regexp-macro-assembler-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/regexp/loong64/regexp-macro-assembler-loong64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/regexp/s390/regexp-macro-assembler-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/regexp/riscv/regexp-macro-assembler-riscv.h"
#else
#error Unsupported target architecture.
#endif

#endif  // V8_REGEXP_REGEXP_MACRO_ASSEMBLER_ARCH_H_
