// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "src/base/build_config.h"
#include "src/base/sanitizer/asan.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

#ifdef V8_CC_GNU

#if V8_HOST_ARCH_X64
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("mov %%rsp, %0" : "=g"(sp_addr))
#elif V8_HOST_ARCH_IA32
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("mov %%esp, %0" : "=g"(sp_addr))
#elif V8_HOST_ARCH_ARM
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("str sp, %0" : "=g"(sp_addr))
#elif V8_HOST_ARCH_ARM64
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("mov x16, sp; str x16, %0" : "=g"(sp_addr))
#elif V8_HOST_ARCH_MIPS
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("sw $sp, %0" : "=g"(sp_addr))
#elif V8_HOST_ARCH_MIPS64
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("sd $sp, %0" : "=g"(sp_addr))
#elif V8_OS_ZOS
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__(" stg 15,%0" : "=m"(sp_addr))
#elif defined(__s390x__) || defined(_ARCH_S390X)
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("stg %%r15, %0" : "=m"(sp_addr))
#elif defined(__s390__) || defined(_ARCH_S390)
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("st 15, %0" : "=m"(sp_addr))
#elif defined(__PPC64__) || defined(_ARCH_PPC64)
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("std 1, %0" : "=m"(sp_addr))
#elif defined(__PPC__) || defined(_ARCH_PPC)
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("stw 1, %0" : "=m"(sp_addr))
#elif V8_TARGET_ARCH_RISCV64 || V8_TARGET_ARCH_RISCV32
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("add %0, sp, x0" : "=r"(sp_addr))
#elif V8_HOST_ARCH_LOONG64
#define GET_STACK_POINTER_TO(sp_addr) \
  __asm__ __volatile__("st.d $sp, %0" : "=m"(sp_addr))
#else
#error Host architecture was not detected as supported by v8
#endif

DISABLE_ASAN inline uintptr_t GetStackPointer() {
  // MSAN doesn't seem to treat initializing stores in inline assembly as such,
  // so we initialize this value here.
  uintptr_t sp_addr = 0;
  GET_STACK_POINTER_TO(sp_addr);
  return sp_addr;
}

#endif  // V8_CC_GNU

}  // namespace internal
}  // namespace v8
