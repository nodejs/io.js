// Copyright 2021 Google LLC
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef HIGHWAY_HWY_DETECT_TARGETS_H_
#define HIGHWAY_HWY_DETECT_TARGETS_H_

// Defines targets and chooses which to enable.

#include "hwy/detect_compiler_arch.h"

//------------------------------------------------------------------------------
// Optional configuration

// See g3doc/quick_reference.md for documentation of these macros.

// Uncomment to override the default baseline determined from predefined macros:
// #define HWY_BASELINE_TARGETS (HWY_SSE4 | HWY_SCALAR)

// Uncomment to override the default blocklist:
// #define HWY_BROKEN_TARGETS HWY_AVX3

// Uncomment to definitely avoid generating those target(s):
// #define HWY_DISABLED_TARGETS HWY_SSE4

// Uncomment to avoid emitting BMI/BMI2/FMA instructions (allows generating
// AVX2 target for VMs which support AVX2 but not the other instruction sets)
// #define HWY_DISABLE_BMI2_FMA

// Uncomment to enable these on MSVC even if the predefined macros are not set.
// #define HWY_WANT_SSE2 1
// #define HWY_WANT_SSSE3 1
// #define HWY_WANT_SSE4 1

//------------------------------------------------------------------------------
// Targets

// Unique bit value for each target. A lower value is "better" (e.g. more lanes)
// than a higher value within the same group/platform - see HWY_STATIC_TARGET.
//
// All values are unconditionally defined so we can test HWY_TARGETS without
// first checking the HWY_ARCH_*.
//
// The C99 preprocessor evaluates #if expressions using intmax_t types. This
// holds at least 64 bits in practice (verified 2022-07-18 via Godbolt on
// 32-bit clang/GCC/MSVC compilers for x86/Arm7/AArch32/RISC-V/WASM). We now
// avoid overflow when computing HWY_TARGETS (subtracting one instead of
// left-shifting 2^62), but still do not use bit 63 because it is the sign bit.

// --------------------------- x86: 15 targets (+ one fallback)
// Bits 0..3 reserved (4 targets)
#define HWY_AVX3_SPR (1LL << 4)
// Bit 5 reserved (likely AVX10.2 with 256-bit vectors)
// Currently HWY_AVX3_DL plus AVX512BF16 and a special case for CompressStore
// (10x as fast).
// We may later also use VPCONFLICT.
#define HWY_AVX3_ZEN4 (1LL << 6)  // see HWY_WANT_AVX3_ZEN4 below

// Currently satisfiable by Ice Lake (VNNI, VPCLMULQDQ, VPOPCNTDQ, VBMI, VBMI2,
// VAES, BITALG, GFNI). Later to be added: BF16 (Cooper Lake). VP2INTERSECT is
// only in Tiger Lake?
#define HWY_AVX3_DL (1LL << 7)  // see HWY_WANT_AVX3_DL below
#define HWY_AVX3 (1LL << 8)     // HWY_AVX2 plus AVX-512F/BW/CD/DQ/VL
#define HWY_AVX2 (1LL << 9)     // HWY_SSE4 plus BMI2 + F16 + FMA
// Bit 10: reserved
#define HWY_SSE4 (1LL << 11)   // SSE4.2 plus AES + CLMUL
#define HWY_SSSE3 (1LL << 12)  // S-SSE3
// Bit 13: reserved for SSE3
#define HWY_SSE2 (1LL << 14)
// The highest bit in the HWY_TARGETS mask that a x86 target can have. Used for
// dynamic dispatch. All x86 target bits must be lower or equal to
// (1 << HWY_HIGHEST_TARGET_BIT_X86) and they can only use
// HWY_MAX_DYNAMIC_TARGETS in total.
#define HWY_HIGHEST_TARGET_BIT_X86 14

// --------------------------- Arm: 15 targets (+ one fallback)
// Bits 15..17 reserved (3 targets)
#define HWY_SVE2_128 (1LL << 18)  // specialized (e.g. Neoverse V2/N2/N3)
#define HWY_SVE_256 (1LL << 19)   // specialized (Neoverse V1)
// Bits 20-22 reserved for later SVE (3 targets)
#define HWY_SVE2 (1LL << 23)
#define HWY_SVE (1LL << 24)
// Bit 25 reserved for NEON
#define HWY_NEON_BF16 (1LL << 26)  // fp16/dot/bf16 (e.g. Neoverse V2/N2/N3)
// Bit 27 reserved for NEON
#define HWY_NEON (1LL << 28)  // Implies support for AES
#define HWY_NEON_WITHOUT_AES (1LL << 29)
#define HWY_HIGHEST_TARGET_BIT_ARM 29

#define HWY_ALL_NEON (HWY_NEON_WITHOUT_AES | HWY_NEON | HWY_NEON_BF16)
#define HWY_ALL_SVE (HWY_SVE | HWY_SVE2 | HWY_SVE_256 | HWY_SVE2_128)

// --------------------------- RISC-V: 9 targets (+ one fallback)
// Bits 30..36 reserved (7 targets)
#define HWY_RVV (1LL << 37)
// Bit 38 reserved
#define HWY_HIGHEST_TARGET_BIT_RVV 38

// --------------------------- Future expansion: 4 targets
// Bits 39..42 reserved

// --------------------------- IBM Power/ZSeries: 9 targets (+ one fallback)
// Bits 43..46 reserved (4 targets)
#define HWY_PPC10 (1LL << 47)  // v3.1
#define HWY_PPC9 (1LL << 48)   // v3.0
#define HWY_PPC8 (1LL << 49)   // v2.07
#define HWY_Z15 (1LL << 50)    // Z15
#define HWY_Z14 (1LL << 51)    // Z14
#define HWY_HIGHEST_TARGET_BIT_PPC 51

#define HWY_ALL_PPC (HWY_PPC8 | HWY_PPC9 | HWY_PPC10)

// --------------------------- WebAssembly: 9 targets (+ one fallback)
// Bits 52..57 reserved (6 targets)
#define HWY_WASM_EMU256 (1LL << 58)  // Experimental
#define HWY_WASM (1LL << 59)
// Bits 60 reserved
#define HWY_HIGHEST_TARGET_BIT_WASM 60

// --------------------------- Emulation: 2 targets

#define HWY_EMU128 (1LL << 61)
// We do not add/left-shift, so this will not overflow to a negative number.
#define HWY_SCALAR (1LL << 62)
#define HWY_HIGHEST_TARGET_BIT_SCALAR 62

// Do not use bit 63 - would be confusing to have negative numbers.

//------------------------------------------------------------------------------
// Set default blocklists

// Disabled means excluded from enabled at user's request. A separate config
// macro allows disabling without deactivating the blocklist below.
#ifndef HWY_DISABLED_TARGETS
#define HWY_DISABLED_TARGETS 0
#endif

// Broken means excluded from enabled due to known compiler issues. We define
// separate HWY_BROKEN_* and then OR them together (more than one might apply).

// x86 clang-6: we saw multiple AVX2/3 compile errors and in one case invalid
// SSE4 codegen (possibly only for msan), so disable all those targets.
#if HWY_ARCH_X86 && (HWY_COMPILER_CLANG != 0 && HWY_COMPILER_CLANG < 700)

#define HWY_BROKEN_CLANG6 (HWY_SSE4 | (HWY_SSE4 - 1))
// This entails a major speed reduction, so warn unless the user explicitly
// opts in to scalar-only.
#if !defined(HWY_COMPILE_ONLY_SCALAR)
#pragma message("x86 Clang <= 6: define HWY_COMPILE_ONLY_SCALAR or upgrade.")
#endif

#else
#define HWY_BROKEN_CLANG6 0
#endif

// 32-bit may fail to compile AVX2/3.
#if HWY_ARCH_X86_32
#define HWY_BROKEN_32BIT (HWY_AVX2 | (HWY_AVX2 - 1))
#else
#define HWY_BROKEN_32BIT 0
#endif

// MSVC AVX3 support is buggy: https://github.com/Mysticial/Flops/issues/16
#if HWY_COMPILER_MSVC != 0
#define HWY_BROKEN_MSVC (HWY_AVX3 | (HWY_AVX3 - 1))
#else
#define HWY_BROKEN_MSVC 0
#endif

// AVX3_DL and AVX3_ZEN4 require clang >= 7 (ensured above), gcc >= 8.1 or ICC
// 2021.
#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 801) || \
    (HWY_COMPILER_ICC && HWY_COMPILER_ICC < 2021)
#define HWY_BROKEN_AVX3_DL_ZEN4 (HWY_AVX3_DL | HWY_AVX3_ZEN4)
#else
#define HWY_BROKEN_AVX3_DL_ZEN4 0
#endif

// AVX3_SPR requires clang >= 14, gcc >= 12, or ICC 2021.
#if (HWY_COMPILER_CLANG != 0 && HWY_COMPILER_CLANG < 1400) ||      \
    (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1200) || \
    (HWY_COMPILER_ICC && HWY_COMPILER_ICC < 2021)
#define HWY_BROKEN_AVX3_SPR (HWY_AVX3_SPR)
#else
#define HWY_BROKEN_AVX3_SPR 0
#endif

// armv7be has not been tested and is not yet supported.
#if HWY_ARCH_ARM_V7 && HWY_IS_BIG_ENDIAN
#define HWY_BROKEN_ARM7_BIG_ENDIAN HWY_ALL_NEON
#else
#define HWY_BROKEN_ARM7_BIG_ENDIAN 0
#endif

// armv7-a without a detected vfpv4 is not supported
// (for example Cortex-A8, Cortex-A9)
// vfpv4 always have neon half-float _and_ FMA.
#if HWY_ARCH_ARM_V7 && (__ARM_ARCH_PROFILE == 'A') && \
    !defined(__ARM_VFPV4__) &&                        \
    !((__ARM_NEON_FP & 0x2 /* half-float */) && (__ARM_FEATURE_FMA == 1))
#define HWY_BROKEN_ARM7_WITHOUT_VFP4 HWY_ALL_NEON
#else
#define HWY_BROKEN_ARM7_WITHOUT_VFP4 0
#endif

// HWY_NEON_BF16 requires recent compilers.
#if (HWY_COMPILER_CLANG != 0 && HWY_COMPILER_CLANG < 1700) || \
    (HWY_COMPILER_GCC_ACTUAL != 0 && HWY_COMPILER_GCC_ACTUAL < 1302)
#define HWY_BROKEN_NEON_BF16 (HWY_NEON_BF16)
#else
#define HWY_BROKEN_NEON_BF16 0
#endif

// SVE[2] require recent clang or gcc versions.
#if (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1900) || \
    (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1000)
#define HWY_BROKEN_SVE (HWY_SVE | HWY_SVE2 | HWY_SVE_256 | HWY_SVE2_128)
#else
#define HWY_BROKEN_SVE 0
#endif

#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1100)
// GCC 10 supports the -mcpu=power10 option but does not support the PPC10
// vector intrinsics
#define HWY_BROKEN_PPC10 (HWY_PPC10)
#elif HWY_ARCH_PPC && HWY_IS_BIG_ENDIAN &&                                   \
    ((HWY_COMPILER3_CLANG && HWY_COMPILER3_CLANG < 160001) ||                \
     (HWY_COMPILER_GCC_ACTUAL >= 1200 && HWY_COMPILER_GCC_ACTUAL <= 1203) || \
     (HWY_COMPILER_GCC_ACTUAL >= 1300 && HWY_COMPILER_GCC_ACTUAL <= 1301))
// GCC 12.0 through 12.3 and GCC 13.0 through 13.1 have a compiler bug where the
// vsldoi instruction is sometimes incorrectly optimized out (and this causes
// some of the Highway unit tests to fail on big-endian PPC10). Details about
// this compiler bug can be found at
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=109069, and this bug will be
// fixed in the upcoming GCC 12.4 and 13.2 releases.

// Clang 16.0.0 and earlier (but not Clang 16.0.1 and later) have a compiler
// bug in the LLVM DAGCombiner that causes a zero-extend followed by an
// element insert into a vector, followed by a vector shuffle to be incorrectly
// optimized on big-endian PPC (and which caused some of the Highway unit tests
// to fail on big-endian PPC10).

// Details about this bug, which has already been fixed in Clang 16.0.1 and
// later, can be found at https://github.com/llvm/llvm-project/issues/61315.
#define HWY_BROKEN_PPC10 (HWY_PPC10)
#else
#define HWY_BROKEN_PPC10 0
#endif

// PPC8/PPC9/PPC10 targets may fail to compile on 32-bit PowerPC
#if HWY_ARCH_PPC && !HWY_ARCH_PPC_64
#define HWY_BROKEN_PPC_32BIT (HWY_PPC8 | HWY_PPC9 | HWY_PPC10)
#else
#define HWY_BROKEN_PPC_32BIT 0
#endif

// HWY_RVV fails to compile with GCC < 13 or Clang < 16.
#if HWY_ARCH_RISCV &&                                     \
    ((HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1600) || \
     (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1300))
#define HWY_BROKEN_RVV (HWY_RVV)
#else
#define HWY_BROKEN_RVV 0
#endif

// Allow the user to override this without any guarantee of success.
#ifndef HWY_BROKEN_TARGETS

#define HWY_BROKEN_TARGETS                                     \
  (HWY_BROKEN_CLANG6 | HWY_BROKEN_32BIT | HWY_BROKEN_MSVC |    \
   HWY_BROKEN_AVX3_DL_ZEN4 | HWY_BROKEN_AVX3_SPR |             \
   HWY_BROKEN_ARM7_BIG_ENDIAN | HWY_BROKEN_ARM7_WITHOUT_VFP4 | \
   HWY_BROKEN_NEON_BF16 | HWY_BROKEN_SVE | HWY_BROKEN_PPC10 |  \
   HWY_BROKEN_PPC_32BIT | HWY_BROKEN_RVV)

#endif  // HWY_BROKEN_TARGETS

// Enabled means not disabled nor blocklisted.
#define HWY_ENABLED(targets) \
  ((targets) & ~((HWY_DISABLED_TARGETS) | (HWY_BROKEN_TARGETS)))

// Opt-out for EMU128 (affected by a GCC bug on multiple arches, fixed in 12.3:
// see https://gcc.gnu.org/bugzilla/show_bug.cgi?id=106322). An issue still
// remains with 13.2, see #1683. This is separate from HWY_BROKEN_TARGETS
// because it affects the fallback target, which must always be enabled. If 1,
// we instead choose HWY_SCALAR even without HWY_COMPILE_ONLY_SCALAR being set.
#if !defined(HWY_BROKEN_EMU128)  // allow overriding
#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1400) || \
    defined(HWY_NO_LIBCXX)
#define HWY_BROKEN_EMU128 1
#else
#define HWY_BROKEN_EMU128 0
#endif
#endif  // HWY_BROKEN_EMU128

//------------------------------------------------------------------------------
// Detect baseline targets using predefined macros

// Baseline means the targets for which the compiler is allowed to generate
// instructions, implying the target CPU would have to support them. This does
// not take the blocklist into account.

#if defined(HWY_COMPILE_ONLY_SCALAR) || HWY_BROKEN_EMU128
#define HWY_BASELINE_SCALAR HWY_SCALAR
#else
#define HWY_BASELINE_SCALAR HWY_EMU128
#endif

// Also check HWY_ARCH to ensure that simulating unknown platforms ends up with
// HWY_TARGET == HWY_BASELINE_SCALAR.

#if HWY_ARCH_WASM && defined(__wasm_simd128__)
#if defined(HWY_WANT_WASM2)
#define HWY_BASELINE_WASM HWY_WASM_EMU256
#else
#define HWY_BASELINE_WASM HWY_WASM
#endif  // HWY_WANT_WASM2
#else
#define HWY_BASELINE_WASM 0
#endif

// GCC or Clang.
#if HWY_ARCH_PPC && HWY_COMPILER_GCC && defined(__ALTIVEC__) && \
    defined(__VSX__) && defined(__POWER8_VECTOR__) &&           \
    (defined(__CRYPTO__) || defined(HWY_DISABLE_PPC8_CRYPTO))
#define HWY_BASELINE_PPC8 HWY_PPC8
#else
#define HWY_BASELINE_PPC8 0
#endif

#if HWY_BASELINE_PPC8 != 0 && defined(__POWER9_VECTOR__)
#define HWY_BASELINE_PPC9 HWY_PPC9
#else
#define HWY_BASELINE_PPC9 0
#endif

#if HWY_BASELINE_PPC9 != 0 && \
    (defined(_ARCH_PWR10) || defined(__POWER10_VECTOR__))
#define HWY_BASELINE_PPC10 HWY_PPC10
#else
#define HWY_BASELINE_PPC10 0
#endif

#if HWY_ARCH_S390X && defined(__VEC__) && defined(__ARCH__) && __ARCH__ >= 12
#define HWY_BASELINE_Z14 HWY_Z14
#else
#define HWY_BASELINE_Z14 0
#endif

#if HWY_BASELINE_Z14 && __ARCH__ >= 13
#define HWY_BASELINE_Z15 HWY_Z15
#else
#define HWY_BASELINE_Z15 0
#endif

#define HWY_BASELINE_SVE2 0
#define HWY_BASELINE_SVE 0
#define HWY_BASELINE_NEON 0

#if HWY_ARCH_ARM

// Also check compiler version as done for HWY_ATTAINABLE_SVE2 because the
// static target (influenced here) must be one of the attainable targets.
#if defined(__ARM_FEATURE_SVE2) && \
    (HWY_COMPILER_CLANG >= 1400 || HWY_COMPILER_GCC_ACTUAL >= 1200)
#undef HWY_BASELINE_SVE2  // was 0, will be re-defined
// If user specified -msve-vector-bits=128, they assert the vector length is
// 128 bits and we should use the HWY_SVE2_128 (more efficient for some ops).
#if defined(__ARM_FEATURE_SVE_BITS) && __ARM_FEATURE_SVE_BITS == 128
#define HWY_BASELINE_SVE2 HWY_SVE2_128
// Otherwise we're not sure what the vector length will be. The baseline must be
// unconditionally valid, so we can only assume HWY_SVE2. However, when running
// on a CPU with 128-bit vectors, user code that supports dynamic dispatch will
// still benefit from HWY_SVE2_128 because we add it to HWY_ATTAINABLE_TARGETS.
#else
#define HWY_BASELINE_SVE2 HWY_SVE2
#endif  // __ARM_FEATURE_SVE_BITS
#endif  // __ARM_FEATURE_SVE2

#if defined(__ARM_FEATURE_SVE) && \
    (HWY_COMPILER_CLANG >= 900 || HWY_COMPILER_GCC_ACTUAL >= 800)
#undef HWY_BASELINE_SVE  // was 0, will be re-defined
// See above. If user-specified vector length matches our optimization, use it.
#if defined(__ARM_FEATURE_SVE_BITS) && __ARM_FEATURE_SVE_BITS == 256
#define HWY_BASELINE_SVE HWY_SVE_256
#else
#define HWY_BASELINE_SVE HWY_SVE
#endif  // __ARM_FEATURE_SVE_BITS
#endif  // __ARM_FEATURE_SVE

// GCC 4.5.4 only defines __ARM_NEON__; 5.4 defines both.
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#undef HWY_BASELINE_NEON
#if defined(__ARM_FEATURE_AES) &&                    \
    defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC) && \
    defined(__ARM_FEATURE_DOTPROD) &&                \
    defined(__ARM_FEATURE_BF16_VECTOR_ARITHMETIC)
#define HWY_BASELINE_NEON HWY_ALL_NEON
#elif defined(__ARM_FEATURE_AES)
#define HWY_BASELINE_NEON (HWY_NEON_WITHOUT_AES | HWY_NEON)
#else
#define HWY_BASELINE_NEON (HWY_NEON_WITHOUT_AES)
#endif  // __ARM_FEATURE*
#endif  // __ARM_NEON

#endif  // HWY_ARCH_ARM

// Special handling for MSVC because it has fewer predefined macros:
#if HWY_COMPILER_MSVC

#if HWY_ARCH_X86_32
#if _M_IX86_FP >= 2
#define HWY_CHECK_SSE2 1
#else
#define HWY_CHECK_SSE2 0
#endif
#elif HWY_ARCH_X86_64
#define HWY_CHECK_SSE2 1
#else
#define HWY_CHECK_SSE2 0
#endif

// 1) We can only be sure SSSE3/SSE4 are enabled if AVX is:
//    https://stackoverflow.com/questions/18563978/.
#if defined(__AVX__)
#define HWY_CHECK_SSSE3 1
#define HWY_CHECK_SSE4 1
#else
#define HWY_CHECK_SSSE3 0
#define HWY_CHECK_SSE4 0
#endif

// 2) Cannot check for PCLMUL/AES and BMI2/FMA/F16C individually; we assume
//    PCLMUL/AES are available if SSE4 is, and BMI2/FMA/F16C if AVX2 is.
#define HWY_CHECK_PCLMUL_AES 1
#define HWY_CHECK_BMI2_FMA 1
#define HWY_CHECK_F16C 1

#else  // non-MSVC

#if defined(__SSE2__)
#define HWY_CHECK_SSE2 1
#else
#define HWY_CHECK_SSE2 0
#endif

#if defined(__SSSE3__)
#define HWY_CHECK_SSSE3 1
#else
#define HWY_CHECK_SSSE3 0
#endif

#if defined(__SSE4_1__) && defined(__SSE4_2__)
#define HWY_CHECK_SSE4 1
#else
#define HWY_CHECK_SSE4 0
#endif

// If these are disabled, they should not gate the availability of SSE4/AVX2.
#if defined(HWY_DISABLE_PCLMUL_AES) || (defined(__PCLMUL__) && defined(__AES__))
#define HWY_CHECK_PCLMUL_AES 1
#else
#define HWY_CHECK_PCLMUL_AES 0
#endif

#if defined(HWY_DISABLE_BMI2_FMA) || (defined(__BMI2__) && defined(__FMA__))
#define HWY_CHECK_BMI2_FMA 1
#else
#define HWY_CHECK_BMI2_FMA 0
#endif

#if defined(HWY_DISABLE_F16C) || defined(__F16C__)
#define HWY_CHECK_F16C 1
#else
#define HWY_CHECK_F16C 0
#endif

#endif  // non-MSVC

#if HWY_ARCH_X86 && (HWY_WANT_SSE2 || HWY_CHECK_SSE2)
#define HWY_BASELINE_SSE2 HWY_SSE2
#else
#define HWY_BASELINE_SSE2 0
#endif

#if HWY_ARCH_X86 && (HWY_WANT_SSSE3 || HWY_CHECK_SSSE3)
#define HWY_BASELINE_SSSE3 HWY_SSSE3
#else
#define HWY_BASELINE_SSSE3 0
#endif

#if HWY_ARCH_X86 && (HWY_WANT_SSE4 || (HWY_CHECK_SSE4 && HWY_CHECK_PCLMUL_AES))
#define HWY_BASELINE_SSE4 HWY_SSE4
#else
#define HWY_BASELINE_SSE4 0
#endif

#if HWY_BASELINE_SSE4 != 0 && HWY_CHECK_BMI2_FMA && HWY_CHECK_F16C && \
    defined(__AVX2__)
#define HWY_BASELINE_AVX2 HWY_AVX2
#else
#define HWY_BASELINE_AVX2 0
#endif

// Require everything in AVX2 plus AVX-512 flags (also set by MSVC)
#if HWY_BASELINE_AVX2 != 0 && defined(__AVX512F__) && defined(__AVX512BW__) && \
    defined(__AVX512DQ__) && defined(__AVX512VL__)
#define HWY_BASELINE_AVX3 HWY_AVX3
#else
#define HWY_BASELINE_AVX3 0
#endif

// TODO(janwas): not yet known whether these will be set by MSVC
#if HWY_BASELINE_AVX3 != 0 && defined(__AVX512VNNI__) && defined(__VAES__) && \
    defined(__VPCLMULQDQ__) && defined(__AVX512VBMI__) &&                     \
    defined(__AVX512VBMI2__) && defined(__AVX512VPOPCNTDQ__) &&               \
    defined(__AVX512BITALG__)
#define HWY_BASELINE_AVX3_DL HWY_AVX3_DL
#else
#define HWY_BASELINE_AVX3_DL 0
#endif

// The ZEN4-optimized AVX3 target is numerically lower than AVX3_DL and is thus
// considered better. Do not enable it unless the user explicitly requests it -
// we do not want to choose the ZEN4 path on Intel because it could be slower.
#if defined(HWY_WANT_AVX3_ZEN4) && HWY_BASELINE_AVX3_DL != 0
#define HWY_BASELINE_AVX3_ZEN4 HWY_AVX3_ZEN4
#else
#define HWY_BASELINE_AVX3_ZEN4 0
#endif

#if HWY_BASELINE_AVX3_DL != 0 && defined(__AVX512BF16__) && \
    defined(__AVX512FP16__)
#define HWY_BASELINE_AVX3_SPR HWY_AVX3_SPR
#else
#define HWY_BASELINE_AVX3_SPR 0
#endif

// RVV requires intrinsics 0.11 or later, see #1156.
#if HWY_ARCH_RISCV && defined(__riscv_v_intrinsic) && \
    __riscv_v_intrinsic >= 11000
#define HWY_BASELINE_RVV HWY_RVV
#else
#define HWY_BASELINE_RVV 0
#endif

// Allow the user to override this without any guarantee of success.
#ifndef HWY_BASELINE_TARGETS
#define HWY_BASELINE_TARGETS                                               \
  (HWY_BASELINE_SCALAR | HWY_BASELINE_WASM | HWY_BASELINE_PPC8 |           \
   HWY_BASELINE_PPC9 | HWY_BASELINE_PPC10 | HWY_BASELINE_Z14 |             \
   HWY_BASELINE_Z15 | HWY_BASELINE_SVE2 | HWY_BASELINE_SVE |               \
   HWY_BASELINE_NEON | HWY_BASELINE_SSE2 | HWY_BASELINE_SSSE3 |            \
   HWY_BASELINE_SSE4 | HWY_BASELINE_AVX2 | HWY_BASELINE_AVX3 |             \
   HWY_BASELINE_AVX3_DL | HWY_BASELINE_AVX3_ZEN4 | HWY_BASELINE_AVX3_SPR | \
   HWY_BASELINE_RVV)
#endif  // HWY_BASELINE_TARGETS

//------------------------------------------------------------------------------
// Choose target for static dispatch

#define HWY_ENABLED_BASELINE HWY_ENABLED(HWY_BASELINE_TARGETS)
#if HWY_ENABLED_BASELINE == 0
#error "At least one baseline target must be defined and enabled"
#endif

// Best baseline, used for static dispatch. This is the least-significant 1-bit
// within HWY_ENABLED_BASELINE and lower bit values imply "better".
#define HWY_STATIC_TARGET (HWY_ENABLED_BASELINE & -HWY_ENABLED_BASELINE)

// Start by assuming static dispatch. If we later use dynamic dispatch, this
// will be defined to other targets during the multiple-inclusion, and finally
// return to the initial value. Defining this outside begin/end_target ensures
// inl headers successfully compile by themselves (required by Bazel).
#define HWY_TARGET HWY_STATIC_TARGET

//------------------------------------------------------------------------------
// Choose targets for dynamic dispatch according to one of four policies

#if 1 < (defined(HWY_COMPILE_ONLY_SCALAR) + defined(HWY_COMPILE_ONLY_EMU128) + \
         defined(HWY_COMPILE_ONLY_STATIC))
#error "Can only define one of HWY_COMPILE_ONLY_{SCALAR|EMU128|STATIC} - bug?"
#endif
// Defining one of HWY_COMPILE_ONLY_* will trump HWY_COMPILE_ALL_ATTAINABLE.

#ifndef HWY_HAVE_AUXV  // allow override
#ifdef TOOLCHAIN_MISS_SYS_AUXV_H
#define HWY_HAVE_AUXV 0  // CMake failed to find the header
// glibc 2.16 added auxv, but checking for that requires features.h, and we do
// not want to include system headers here. Instead check for the header
// directly, which has been supported at least since GCC 5.4 and Clang 3.
#elif defined(__has_include)  // note: wrapper macro fails on Clang ~17
// clang-format off
#if __has_include(<sys/auxv.h>)
// clang-format on
#define HWY_HAVE_AUXV 1       // header present
#else
#define HWY_HAVE_AUXV 0  // header not present
#endif                   // __has_include
#else                    // compiler lacks __has_include
#define HWY_HAVE_AUXV 0
#endif
#endif  // HWY_HAVE_AUXV

#ifndef HWY_HAVE_RUNTIME_DISPATCH_RVV  // allow override
// The riscv_vector.h in Clang 16-18 requires compiler flags, and 19 still has
// some missing intrinsics, see
// https://github.com/llvm/llvm-project/issues/56592. GCC 13.3 also has an
// #error check, whereas 14.1 fails with "argument type 'vuint16m8_t' requires
// the V ISA extension": https://gcc.gnu.org/bugzilla/show_bug.cgi?id=115325.
#if HWY_ARCH_RISCV && HWY_COMPILER_CLANG >= 1900 && 0
#define HWY_HAVE_RUNTIME_DISPATCH_RVV 1
#else
#define HWY_HAVE_RUNTIME_DISPATCH_RVV 0
#endif
#endif  // HWY_HAVE_RUNTIME_DISPATCH_RVV

#ifndef HWY_HAVE_RUNTIME_DISPATCH_APPLE  // allow override
#if HWY_ARCH_ARM_A64 && HWY_OS_APPLE && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_COMPILER_CLANG >= 1700)
#define HWY_HAVE_RUNTIME_DISPATCH_APPLE 1
#else
#define HWY_HAVE_RUNTIME_DISPATCH_APPLE 0
#endif
#endif  // HWY_HAVE_RUNTIME_DISPATCH_APPLE

#ifndef HWY_HAVE_RUNTIME_DISPATCH_LINUX  // allow override
#if (HWY_ARCH_ARM || HWY_ARCH_PPC || HWY_ARCH_S390X) && HWY_OS_LINUX && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_COMPILER_CLANG >= 1700) && HWY_HAVE_AUXV
#define HWY_HAVE_RUNTIME_DISPATCH_LINUX 1
#else
#define HWY_HAVE_RUNTIME_DISPATCH_LINUX 0
#endif
#endif  // HWY_HAVE_RUNTIME_DISPATCH_LINUX

// Allow opting out, and without a guarantee of success, opting-in.
#ifndef HWY_HAVE_RUNTIME_DISPATCH
// Clang, GCC and MSVC allow OS-independent runtime dispatch on x86.
#if HWY_ARCH_X86 || HWY_HAVE_RUNTIME_DISPATCH_RVV || \
    HWY_HAVE_RUNTIME_DISPATCH_APPLE || HWY_HAVE_RUNTIME_DISPATCH_LINUX
#define HWY_HAVE_RUNTIME_DISPATCH 1
#else
#define HWY_HAVE_RUNTIME_DISPATCH 0
#endif
#endif  // HWY_HAVE_RUNTIME_DISPATCH

// AVX3_DL is not widely available yet. To reduce code size and compile time,
// only include it in the set of attainable targets (for dynamic dispatch) if
// the user opts in, OR it is in the baseline (we check whether enabled below).
#if defined(HWY_WANT_AVX3_DL) || (HWY_BASELINE_TARGETS & HWY_AVX3_DL)
#define HWY_ATTAINABLE_AVX3_DL (HWY_AVX3_DL)
#else
#define HWY_ATTAINABLE_AVX3_DL 0
#endif

#if HWY_ARCH_ARM_A64 && HWY_HAVE_RUNTIME_DISPATCH
#define HWY_ATTAINABLE_NEON HWY_ALL_NEON
#elif HWY_ARCH_ARM  // static dispatch, or HWY_ARCH_ARM_V7
#define HWY_ATTAINABLE_NEON (HWY_BASELINE_NEON)
#else
#define HWY_ATTAINABLE_NEON 0
#endif

#if HWY_ARCH_ARM_A64 &&                                              \
    (HWY_COMPILER_CLANG >= 900 || HWY_COMPILER_GCC_ACTUAL >= 800) && \
    (HWY_HAVE_RUNTIME_DISPATCH ||                                    \
     (HWY_ENABLED_BASELINE & (HWY_SVE | HWY_SVE_256)))
#define HWY_ATTAINABLE_SVE (HWY_SVE | HWY_SVE_256)
#else
#define HWY_ATTAINABLE_SVE 0
#endif

#if HWY_ARCH_ARM_A64 &&                                                \
    (HWY_COMPILER_CLANG >= 1400 || HWY_COMPILER_GCC_ACTUAL >= 1200) && \
    (HWY_HAVE_RUNTIME_DISPATCH ||                                      \
     (HWY_ENABLED_BASELINE & (HWY_SVE2 | HWY_SVE2_128)))
#define HWY_ATTAINABLE_SVE2 (HWY_SVE2 | HWY_SVE2_128)
#else
#define HWY_ATTAINABLE_SVE2 0
#endif

#if HWY_ARCH_PPC && defined(__ALTIVEC__) && \
    (!HWY_COMPILER_CLANG || HWY_BASELINE_PPC8 != 0)

#if (HWY_BASELINE_PPC9 | HWY_BASELINE_PPC10) && \
    !defined(HWY_SKIP_NON_BEST_BASELINE)
// On POWER with -m flags, we get compile errors (#1707) for targets older than
// the baseline specified via -m, so only generate the static target and better.
// Note that some Linux distros actually do set POWER9 as the baseline.
// This works by skipping case 3 below, so case 4 is reached.
#define HWY_SKIP_NON_BEST_BASELINE
#endif

#define HWY_ATTAINABLE_PPC (HWY_PPC8 | HWY_PPC9 | HWY_PPC10)

#else
#define HWY_ATTAINABLE_PPC 0
#endif

#if HWY_ARCH_S390X && HWY_BASELINE_Z14 != 0
#define HWY_ATTAINABLE_S390X (HWY_Z14 | HWY_Z15)
#else
#define HWY_ATTAINABLE_S390X 0
#endif

#if HWY_ARCH_RISCV && HWY_HAVE_RUNTIME_DISPATCH
#define HWY_ATTAINABLE_RISCV HWY_RVV
#else
#define HWY_ATTAINABLE_RISCV HWY_BASELINE_RVV
#endif

#ifndef HWY_ATTAINABLE_TARGETS_X86  // allow override
#if HWY_COMPILER_MSVC && defined(HWY_SLOW_MSVC)
// Fewer targets for faster builds.
#define HWY_ATTAINABLE_TARGETS_X86 \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_STATIC_TARGET | HWY_AVX2)
#else  // !HWY_COMPILER_MSVC
#define HWY_ATTAINABLE_TARGETS_X86                                           \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_SSE2 | HWY_SSSE3 | HWY_SSE4 |        \
              HWY_AVX2 | HWY_AVX3 | HWY_ATTAINABLE_AVX3_DL | HWY_AVX3_ZEN4 | \
              HWY_AVX3_SPR)
#endif  // !HWY_COMPILER_MSVC
#endif  // HWY_ATTAINABLE_TARGETS_X86

// Attainable means enabled and the compiler allows intrinsics (even when not
// allowed to autovectorize). Used in 3 and 4.
#if HWY_ARCH_X86
#define HWY_ATTAINABLE_TARGETS HWY_ATTAINABLE_TARGETS_X86
#elif HWY_ARCH_ARM
#define HWY_ATTAINABLE_TARGETS                                                 \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_ATTAINABLE_NEON | HWY_ATTAINABLE_SVE | \
              HWY_ATTAINABLE_SVE2)
#elif HWY_ARCH_PPC
#define HWY_ATTAINABLE_TARGETS \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_ATTAINABLE_PPC)
#elif HWY_ARCH_S390X
#define HWY_ATTAINABLE_TARGETS \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_ATTAINABLE_S390X)
#elif HWY_ARCH_RISCV
#define HWY_ATTAINABLE_TARGETS \
  HWY_ENABLED(HWY_BASELINE_SCALAR | HWY_ATTAINABLE_RISCV)
#else
#define HWY_ATTAINABLE_TARGETS (HWY_ENABLED_BASELINE)
#endif  // HWY_ARCH_*

// 1) For older compilers: avoid SIMD intrinsics, but still support all ops.
#if defined(HWY_COMPILE_ONLY_EMU128) && !HWY_BROKEN_EMU128
#undef HWY_STATIC_TARGET
#define HWY_STATIC_TARGET HWY_EMU128  // override baseline
#define HWY_TARGETS HWY_EMU128

// 1b) HWY_SCALAR is less capable than HWY_EMU128 (which supports all ops), but
// we currently still support it for backwards compatibility.
#elif defined(HWY_COMPILE_ONLY_SCALAR) || \
    (defined(HWY_COMPILE_ONLY_EMU128) && HWY_BROKEN_EMU128)
#undef HWY_STATIC_TARGET
#define HWY_STATIC_TARGET HWY_SCALAR  // override baseline
#define HWY_TARGETS HWY_SCALAR

// 2) For forcing static dispatch without code changes (removing HWY_EXPORT)
#elif defined(HWY_COMPILE_ONLY_STATIC)
#define HWY_TARGETS HWY_STATIC_TARGET

// 3) For tests: include all attainable targets (in particular: scalar)
#elif (defined(HWY_COMPILE_ALL_ATTAINABLE) || defined(HWY_IS_TEST)) && \
    !defined(HWY_SKIP_NON_BEST_BASELINE)
#define HWY_TARGETS HWY_ATTAINABLE_TARGETS

// 4) Default: attainable WITHOUT non-best baseline. This reduces code size by
// excluding superseded targets, in particular scalar. Note: HWY_STATIC_TARGET
// may be 2^62 (HWY_SCALAR), so we must not left-shift/add it. Subtracting one
// sets all lower bits (better targets), then we also include the static target.
#else
#define HWY_TARGETS \
  (HWY_ATTAINABLE_TARGETS & ((HWY_STATIC_TARGET - 1LL) | HWY_STATIC_TARGET))

#endif  // target policy

// HWY_ONCE and the multiple-inclusion mechanism rely on HWY_STATIC_TARGET being
// one of the dynamic targets. This also implies HWY_TARGETS != 0 and
// (HWY_TARGETS & HWY_ENABLED_BASELINE) != 0.
#if (HWY_TARGETS & HWY_STATIC_TARGET) == 0
#error "Logic error: best baseline should be included in dynamic targets"
#endif

#endif  // HIGHWAY_HWY_DETECT_TARGETS_H_
