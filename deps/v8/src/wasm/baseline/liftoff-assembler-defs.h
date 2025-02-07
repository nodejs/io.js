// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_
#define V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_

#include "src/codegen/assembler-arch.h"
#include "src/codegen/reglist.h"

namespace v8 {
namespace internal {
namespace wasm {

#if V8_TARGET_ARCH_IA32

// Omit ebx, which is the root register.
constexpr RegList kLiftoffAssemblerGpCacheRegs = {eax, ecx, edx, esi, edi};

// Omit xmm7, which is the kScratchDoubleReg.
// Omit xmm0, which is not an allocatable register (see register-ia32.h).
constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {xmm1, xmm2, xmm3,
                                                        xmm4, xmm5, xmm6};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = edi;

#elif V8_TARGET_ARCH_X64

// r10: kScratchRegister (MacroAssembler)
// r11: kScratchRegister2 (Liftoff)
// r13: kRootRegister
// r14: kPtrComprCageBaseRegister
constexpr RegList kLiftoffAssemblerGpCacheRegs = {rax, rcx, rdx, rbx, rsi,
                                                  rdi, r8,  r9,  r12, r15};

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {xmm0, xmm1, xmm2, xmm3,
                                                        xmm4, xmm5, xmm6, xmm7};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = r12;

#elif V8_TARGET_ARCH_MIPS

constexpr RegList kLiftoffAssemblerGpCacheRegs = {a0, a1, a2, a3, t0, t1, t2,
                                                  t3, t4, t5, t6, s7, v0, v1};

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    f0, f2, f4, f6, f8, f10, f12, f14, f16, f18, f20, f22, f24};

#elif V8_TARGET_ARCH_MIPS64

constexpr RegList kLiftoffAssemblerGpCacheRegs = {a0, a1, a2, a3, a4, a5, a6,
                                                  a7, t0, t1, t2, s7, v0, v1};

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    f0, f2, f4, f6, f8, f10, f12, f14, f16, f18, f20, f22, f24, f26};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = t0;

#elif V8_TARGET_ARCH_LOONG64

// t6-t8 and s3-s4: scratch registers, s6: root
// s8: pointer-compression-cage base
constexpr RegList kLiftoffAssemblerGpCacheRegs = {a0, a1, a2, a3, a4, a5, a6,
                                                  a7, t0, t1, t2, t3, t4, t5,
                                                  s0, s1, s2, s5, s7};

// f29: zero, f30-f31: macro-assembler scratch float Registers.
constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    f0,  f1,  f2,  f3,  f4,  f5,  f6,  f7,  f8,  f9,  f10, f11, f12, f13, f14,
    f15, f16, f17, f18, f19, f20, f21, f22, f23, f24, f25, f26, f27, f28};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = t0;

#elif V8_TARGET_ARCH_ARM

// r10: root, r11: fp, r12: ip, r13: sp, r14: lr, r15: pc.
constexpr RegList kLiftoffAssemblerGpCacheRegs = {r0, r1, r2, r3, r4,
                                                  r5, r6, r7, r8, r9};

// d13: zero, d14-d15: scratch
constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = r4;

#elif V8_TARGET_ARCH_ARM64

// x16: ip0, x17: ip1, x18: platform register, x26: root, x28: base, x29: fp,
// x30: lr, x31: xzr.
constexpr RegList kLiftoffAssemblerGpCacheRegs = {
    x0,  x1,  x2,  x3,  x4,  x5,  x6,  x7,  x8,  x9,  x10, x11,
    x12, x13, x14, x15, x19, x20, x21, x22, x23, x24, x25, x27};

// d15: fp_zero, d28-d31: not allocatable registers, d30-d31: macro-assembler
// scratch V Registers.
constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    d0,  d1,  d2,  d3,  d4,  d5,  d6,  d7,  d8,  d9,  d10, d11, d12, d13,
    d14, d16, d17, d18, d19, d20, d21, d22, d23, d24, d25, d26, d27};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = x8;

#elif V8_TARGET_ARCH_S390X

constexpr RegList kLiftoffAssemblerGpCacheRegs = {r2, r3, r4, r5,
                                                  r6, r7, r8, cp};

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = r7;

#elif V8_TARGET_ARCH_PPC64

constexpr RegList kLiftoffAssemblerGpCacheRegs = {r3, r4,  r5,  r6,  r7, r8,
                                                  r9, r10, r11, r15, cp};

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    d0, d1, d2, d3, d4, d5, d6, d7, d8, d9, d10, d11, d12};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = r15;

#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
// Any change of kLiftoffAssemblerGpCacheRegs also need to update
// kPushedGpRegs in frame-constants-riscv.h
constexpr RegList kLiftoffAssemblerGpCacheRegs = {a0, a1, a2, a3, a4, a5,
                                                  a6, a7, t0, t1, t2, s7};

// Any change of kLiftoffAssemblerGpCacheRegs also need to update
// kPushedFpRegs in frame-constants-riscv.h
// ft0 don't be putted int kLiftoffAssemblerFpCacheRegs because v0 is a special
// simd register and code of ft0 and v0 is same.
constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs = {
    ft1, ft2, ft3, ft4, ft5, ft6, ft7, fa0,  fa1, fa2,
    fa3, fa4, fa5, fa6, fa7, ft8, ft9, ft10, ft11};

// For the "WasmLiftoffFrameSetup" builtin.
constexpr Register kLiftoffFrameSetupFunctionReg = t0;
#else

constexpr RegList kLiftoffAssemblerGpCacheRegs = RegList::FromBits(0xff);

constexpr DoubleRegList kLiftoffAssemblerFpCacheRegs =
    DoubleRegList::FromBits(0xff);

#endif

static_assert(kLiftoffFrameSetupFunctionReg != kWasmImplicitArgRegister);
static_assert(kLiftoffFrameSetupFunctionReg != kRootRegister);
#ifdef V8_COMPRESS_POINTERS
static_assert(kLiftoffFrameSetupFunctionReg != kPtrComprCageBaseRegister);
#endif

}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif  // V8_WASM_BASELINE_LIFTOFF_ASSEMBLER_DEFS_H_
