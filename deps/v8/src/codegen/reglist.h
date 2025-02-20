// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_REGLIST_H_
#define V8_CODEGEN_REGLIST_H_

#if V8_TARGET_ARCH_IA32
#include "src/codegen/ia32/reglist-ia32.h"
#elif V8_TARGET_ARCH_X64
#include "src/codegen/x64/reglist-x64.h"
#elif V8_TARGET_ARCH_ARM64
#include "src/codegen/arm64/reglist-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/codegen/arm/reglist-arm.h"
#elif V8_TARGET_ARCH_PPC64
#include "src/codegen/ppc/reglist-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/codegen/mips64/reglist-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/codegen/loong64/reglist-loong64.h"
#elif V8_TARGET_ARCH_S390X
#include "src/codegen/s390/reglist-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/codegen/riscv/reglist-riscv.h"
#else
#error Unknown architecture.
#endif

namespace v8 {
namespace internal {

static constexpr RegList kEmptyRegList = {};

#define LIST_REG(V) V,
static constexpr RegList kAllocatableGeneralRegisters = {
    ALLOCATABLE_GENERAL_REGISTERS(LIST_REG) Register::no_reg()};
#undef LIST_REG

static constexpr DoubleRegList kEmptyDoubleRegList = {};

#define LIST_REG(V) V,
static constexpr DoubleRegList kAllocatableDoubleRegisters = {
    ALLOCATABLE_DOUBLE_REGISTERS(LIST_REG) DoubleRegister::no_reg()};
#undef LIST_REG

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_REGLIST_H_
