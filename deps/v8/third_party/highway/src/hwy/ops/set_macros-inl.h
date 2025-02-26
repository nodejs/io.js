// Copyright 2020 Google LLC
// Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause
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

// Sets macros based on HWY_TARGET.

// This include guard is toggled by foreach_target, so avoid the usual _H_
// suffix to prevent copybara from renaming it.
#if defined(HWY_SET_MACROS_PER_TARGET) == defined(HWY_TARGET_TOGGLE)
#ifdef HWY_SET_MACROS_PER_TARGET
#undef HWY_SET_MACROS_PER_TARGET
#else
#define HWY_SET_MACROS_PER_TARGET
#endif

#endif  // HWY_SET_MACROS_PER_TARGET

#include "hwy/detect_compiler_arch.h"  // IWYU: export
#include "hwy/detect_targets.h"        // IWYU: export

#undef HWY_NAMESPACE
#undef HWY_ALIGN
#undef HWY_MAX_BYTES
#undef HWY_LANES

#undef HWY_HAVE_SCALABLE
#undef HWY_HAVE_TUPLE
#undef HWY_HAVE_INTEGER64
#undef HWY_HAVE_FLOAT16
#undef HWY_HAVE_FLOAT64
#undef HWY_MEM_OPS_MIGHT_FAULT
#undef HWY_NATIVE_FMA
#undef HWY_NATIVE_DOT_BF16
#undef HWY_CAP_GE256
#undef HWY_CAP_GE512

#undef HWY_TARGET_IS_SVE
#if HWY_TARGET & HWY_ALL_SVE
#define HWY_TARGET_IS_SVE 1
#else
#define HWY_TARGET_IS_SVE 0
#endif

#undef HWY_TARGET_IS_NEON
#if HWY_TARGET & HWY_ALL_NEON
#define HWY_TARGET_IS_NEON 1
#else
#define HWY_TARGET_IS_NEON 0
#endif

#undef HWY_TARGET_IS_PPC
#if HWY_TARGET & HWY_ALL_PPC
#define HWY_TARGET_IS_PPC 1
#else
#define HWY_TARGET_IS_PPC 0
#endif

// Supported on all targets except RVV (requires GCC 14 or upcoming Clang)
#if HWY_TARGET == HWY_RVV &&                                        \
    ((HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1400) || \
     (HWY_COMPILER_CLANG))
#define HWY_HAVE_TUPLE 0
#else
#define HWY_HAVE_TUPLE 1
#endif

// For internal use (clamping/validating N for Simd<>)
#undef HWY_MAX_N
#if HWY_TARGET == HWY_SCALAR
#define HWY_MAX_N 1
#else
#define HWY_MAX_N 65536
#endif

// For internal use (clamping kPow2 for Simd<>)
#undef HWY_MAX_POW2
// For HWY_TARGET == HWY_RVV, LMUL <= 8. Even on other targets, we want to
// support say Rebind<uint64_t, Simd<uint8_t, 1, 0>> d; whose kPow2 is also 3.
// However, those other targets do not actually support multiple vectors, and
// thus Lanes(d) must not exceed Lanes(ScalableTag<T>()).
#define HWY_MAX_POW2 3

// User-visible. Loose lower bound that guarantees HWY_MAX_BYTES >>
// (-HWY_MIN_POW2) <= 1. Useful for terminating compile-time recursions.
#undef HWY_MIN_POW2
#if HWY_TARGET == HWY_RVV
#define HWY_MIN_POW2 -16
#else
// Tighter bound for other targets, whose vectors are smaller, to potentially
// save compile time.
#define HWY_MIN_POW2 -8
#endif  // HWY_TARGET == HWY_RVV

#undef HWY_TARGET_STR

#if defined(HWY_DISABLE_PCLMUL_AES)
#define HWY_TARGET_STR_PCLMUL_AES ""
#else
#define HWY_TARGET_STR_PCLMUL_AES ",pclmul,aes"
#endif

#if defined(HWY_DISABLE_BMI2_FMA)
#define HWY_TARGET_STR_BMI2_FMA ""
#else
#define HWY_TARGET_STR_BMI2_FMA ",bmi,bmi2,fma"
#endif

#if defined(HWY_DISABLE_F16C)
#define HWY_TARGET_STR_F16C ""
#else
#define HWY_TARGET_STR_F16C ",f16c"
#endif

#define HWY_TARGET_STR_SSE2 "sse2"

#define HWY_TARGET_STR_SSSE3 "sse2,ssse3"

#define HWY_TARGET_STR_SSE4 \
  HWY_TARGET_STR_SSSE3 ",sse4.1,sse4.2" HWY_TARGET_STR_PCLMUL_AES
// Include previous targets, which are the half-vectors of the next target.
#define HWY_TARGET_STR_AVX2 \
  HWY_TARGET_STR_SSE4 ",avx,avx2" HWY_TARGET_STR_BMI2_FMA HWY_TARGET_STR_F16C
#define HWY_TARGET_STR_AVX3 \
  HWY_TARGET_STR_AVX2 ",avx512f,avx512cd,avx512vl,avx512dq,avx512bw"
#define HWY_TARGET_STR_AVX3_DL                                       \
  HWY_TARGET_STR_AVX3                                                \
  ",vpclmulqdq,avx512vbmi,avx512vbmi2,vaes,avx512vnni,avx512bitalg," \
  "avx512vpopcntdq,gfni"

// Force-disable for compilers that do not properly support avx512bf16.
#if !defined(HWY_AVX3_DISABLE_AVX512BF16) &&                        \
    (HWY_COMPILER_CLANGCL ||                                        \
     (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1000) || \
     (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 900))
#define HWY_AVX3_DISABLE_AVX512BF16
#endif

#if !defined(HWY_AVX3_DISABLE_AVX512BF16)
#define HWY_TARGET_STR_AVX3_ZEN4 HWY_TARGET_STR_AVX3_DL ",avx512bf16"
#else
#define HWY_TARGET_STR_AVX3_ZEN4 HWY_TARGET_STR_AVX3_DL
#endif

#define HWY_TARGET_STR_AVX3_SPR HWY_TARGET_STR_AVX3_ZEN4 ",avx512fp16"

#if defined(HWY_DISABLE_PPC8_CRYPTO)
#define HWY_TARGET_STR_PPC8_CRYPTO ""
#else
#define HWY_TARGET_STR_PPC8_CRYPTO ",crypto"
#endif

#define HWY_TARGET_STR_PPC8 \
  "altivec,vsx,power8-vector" HWY_TARGET_STR_PPC8_CRYPTO
#define HWY_TARGET_STR_PPC9 HWY_TARGET_STR_PPC8 ",power9-vector"

#if HWY_COMPILER_CLANG
#define HWY_TARGET_STR_PPC10 HWY_TARGET_STR_PPC9 ",power10-vector"
#else
// See #1707 and https://gcc.gnu.org/bugzilla/show_bug.cgi?id=102059#c35.
// When the baseline is PPC 8 or 9, inlining functions such as PreventElision
// into PPC10 code fails because PPC10 defaults to no-htm and is thus worse than
// the baseline, which has htm. We cannot have pragma target on functions
// outside HWY_NAMESPACE such as those in base.h. It would be possible for users
// to set -mno-htm globally, but we can also work around this at the library
// level by claiming that PPC10 still has HTM, thus avoiding the mismatch. This
// seems to be safe because HTM uses builtins rather than modifying codegen, see
// https://gcc.gnu.org/legacy-ml/gcc-patches/2013-07/msg00167.html.
#define HWY_TARGET_STR_PPC10 HWY_TARGET_STR_PPC9 ",cpu=power10,htm"
#endif

#define HWY_TARGET_STR_Z14 "arch=z14"
#define HWY_TARGET_STR_Z15 "arch=z15"

// Before include guard so we redefine HWY_TARGET_STR on each include,
// governed by the current HWY_TARGET.

//-----------------------------------------------------------------------------
// SSE2
#if HWY_TARGET == HWY_SSE2

#define HWY_NAMESPACE N_SSE2
#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_TARGET_STR HWY_TARGET_STR_SSE2
//-----------------------------------------------------------------------------
// SSSE3
#elif HWY_TARGET == HWY_SSSE3

#define HWY_NAMESPACE N_SSSE3
#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_TARGET_STR HWY_TARGET_STR_SSSE3

//-----------------------------------------------------------------------------
// SSE4
#elif HWY_TARGET == HWY_SSE4

#define HWY_NAMESPACE N_SSE4
#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_TARGET_STR HWY_TARGET_STR_SSE4

//-----------------------------------------------------------------------------
// AVX2
#elif HWY_TARGET == HWY_AVX2

#define HWY_NAMESPACE N_AVX2
#define HWY_ALIGN alignas(32)
#define HWY_MAX_BYTES 32
#define HWY_LANES(T) (32 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1

#ifdef HWY_DISABLE_BMI2_FMA
#define HWY_NATIVE_FMA 0
#else
#define HWY_NATIVE_FMA 1
#endif
#define HWY_NATIVE_DOT_BF16 0

#define HWY_CAP_GE256 1
#define HWY_CAP_GE512 0

#define HWY_TARGET_STR HWY_TARGET_STR_AVX2

//-----------------------------------------------------------------------------
// AVX3[_DL]
#elif HWY_TARGET == HWY_AVX3 || HWY_TARGET == HWY_AVX3_DL || \
    HWY_TARGET == HWY_AVX3_ZEN4 || HWY_TARGET == HWY_AVX3_SPR

#define HWY_ALIGN alignas(64)
#define HWY_MAX_BYTES 64
#define HWY_LANES(T) (64 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#if HWY_TARGET == HWY_AVX3_SPR &&                              \
    (HWY_COMPILER_GCC_ACTUAL || HWY_COMPILER_CLANG >= 1901) && \
    HWY_HAVE_SCALAR_F16_TYPE
#define HWY_HAVE_FLOAT16 1
#else
#define HWY_HAVE_FLOAT16 0
#endif
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 0
#define HWY_NATIVE_FMA 1
#if (HWY_TARGET <= HWY_AVX3_ZEN4) && !defined(HWY_AVX3_DISABLE_AVX512BF16)
#define HWY_NATIVE_DOT_BF16 1
#else
#define HWY_NATIVE_DOT_BF16 0
#endif
#define HWY_CAP_GE256 1
#define HWY_CAP_GE512 1

#if HWY_TARGET == HWY_AVX3

#define HWY_NAMESPACE N_AVX3
#define HWY_TARGET_STR HWY_TARGET_STR_AVX3

#elif HWY_TARGET == HWY_AVX3_DL

#define HWY_NAMESPACE N_AVX3_DL
#define HWY_TARGET_STR HWY_TARGET_STR_AVX3_DL

#elif HWY_TARGET == HWY_AVX3_ZEN4

#define HWY_NAMESPACE N_AVX3_ZEN4
#define HWY_TARGET_STR HWY_TARGET_STR_AVX3_ZEN4

#elif HWY_TARGET == HWY_AVX3_SPR

#define HWY_NAMESPACE N_AVX3_SPR
#define HWY_TARGET_STR HWY_TARGET_STR_AVX3_SPR

#else
#error "Logic error"
#endif  // HWY_TARGET

//-----------------------------------------------------------------------------
// PPC8, PPC9, PPC10
#elif HWY_TARGET_IS_PPC

#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 1
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#if HWY_TARGET == HWY_PPC8

#define HWY_NAMESPACE N_PPC8
#define HWY_TARGET_STR HWY_TARGET_STR_PPC8

#elif HWY_TARGET == HWY_PPC9

#define HWY_NAMESPACE N_PPC9
#define HWY_TARGET_STR HWY_TARGET_STR_PPC9

#elif HWY_TARGET == HWY_PPC10

#define HWY_NAMESPACE N_PPC10
#define HWY_TARGET_STR HWY_TARGET_STR_PPC10

#else
#error "Logic error"
#endif  // HWY_TARGET

//-----------------------------------------------------------------------------
// Z14, Z15
#elif HWY_TARGET == HWY_Z14 || HWY_TARGET == HWY_Z15

#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 1
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#if HWY_TARGET == HWY_Z14

#define HWY_NAMESPACE N_Z14
#define HWY_TARGET_STR HWY_TARGET_STR_Z14

#elif HWY_TARGET == HWY_Z15

#define HWY_NAMESPACE N_Z15
#define HWY_TARGET_STR HWY_TARGET_STR_Z15

#else
#error "Logic error"
#endif  // HWY_TARGET == HWY_Z15

//-----------------------------------------------------------------------------
// NEON
#elif HWY_TARGET_IS_NEON

#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#if defined(__ARM_FEATURE_FP16_VECTOR_ARITHMETIC) || HWY_TARGET == HWY_NEON_BF16
#define HWY_HAVE_FLOAT16 1
#else
#define HWY_HAVE_FLOAT16 0
#endif

#if HWY_ARCH_ARM_A64
#define HWY_HAVE_FLOAT64 1
#else
#define HWY_HAVE_FLOAT64 0
#endif

#define HWY_MEM_OPS_MIGHT_FAULT 1

#if defined(__ARM_FEATURE_FMA) || defined(__ARM_VFPV4__) || HWY_ARCH_ARM_A64
#define HWY_NATIVE_FMA 1
#else
#define HWY_NATIVE_FMA 0
#endif
#if HWY_NEON_HAVE_F32_TO_BF16C || HWY_TARGET == HWY_NEON_BF16
#define HWY_NATIVE_DOT_BF16 1
#else
#define HWY_NATIVE_DOT_BF16 0
#endif

#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#if HWY_TARGET == HWY_NEON_WITHOUT_AES
#define HWY_NAMESPACE N_NEON_WITHOUT_AES
#elif HWY_TARGET == HWY_NEON
#define HWY_NAMESPACE N_NEON
#elif HWY_TARGET == HWY_NEON_BF16
#define HWY_NAMESPACE N_NEON_BF16
#else
#error "Logic error, missing case"
#endif  // HWY_TARGET

// Can use pragmas instead of -march compiler flag
#if HWY_HAVE_RUNTIME_DISPATCH
#if HWY_ARCH_ARM_V7

// The __attribute__((target(+neon-vfpv4)) was introduced in gcc >= 8.
#if HWY_COMPILER_GCC_ACTUAL >= 800
#define HWY_TARGET_STR "+neon-vfpv4"
#else   // GCC < 7
// Do not define HWY_TARGET_STR (no pragma).
#endif  // HWY_COMPILER_GCC_ACTUAL

#else  // !HWY_ARCH_ARM_V7

#if (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1300) || \
    (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1300)
// GCC 12 or earlier and Clang 12 or earlier require +crypto be added to the
// target string to enable AArch64 AES intrinsics
#define HWY_TARGET_STR_NEON "+crypto"
#else
#define HWY_TARGET_STR_NEON "+aes"
#endif

// Clang >= 16 requires +fullfp16 instead of fp16, but Apple Clang 15 = 1600
// fails to parse unless the string starts with armv8, whereas 1700 refuses it.
#if HWY_COMPILER_CLANG >= 1700
#define HWY_TARGET_STR_FP16 "+fullfp16"
#elif HWY_COMPILER_CLANG >= 1600 && defined(__apple_build_version__)
#define HWY_TARGET_STR_FP16 "armv8.4-a+fullfp16"
#else
#define HWY_TARGET_STR_FP16 "+fp16"
#endif

#if HWY_TARGET == HWY_NEON_WITHOUT_AES
// Do not define HWY_TARGET_STR (no pragma).
#elif HWY_TARGET == HWY_NEON
#define HWY_TARGET_STR HWY_TARGET_STR_NEON
#elif HWY_TARGET == HWY_NEON_BF16
#define HWY_TARGET_STR HWY_TARGET_STR_FP16 "+bf16+dotprod" HWY_TARGET_STR_NEON
#else
#error "Logic error, missing case"
#endif  // HWY_TARGET

#endif  // !HWY_ARCH_ARM_V7
#else   // !HWY_HAVE_RUNTIME_DISPATCH
// HWY_TARGET_STR remains undefined
#endif

//-----------------------------------------------------------------------------
// SVE[2]
#elif HWY_TARGET_IS_SVE

// SVE only requires lane alignment, not natural alignment of the entire vector.
#define HWY_ALIGN alignas(8)

// Value ensures MaxLanes() is the tightest possible upper bound to reduce
// overallocation.
#define HWY_LANES(T) ((HWY_MAX_BYTES) / sizeof(T))

#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 1
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 0
#define HWY_NATIVE_FMA 1
#if HWY_SVE_HAVE_BF16_FEATURE
#define HWY_NATIVE_DOT_BF16 1
#else
#define HWY_NATIVE_DOT_BF16 0
#endif
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#if HWY_TARGET == HWY_SVE2
#define HWY_NAMESPACE N_SVE2
#define HWY_MAX_BYTES 256
#define HWY_HAVE_SCALABLE 1
#elif HWY_TARGET == HWY_SVE_256
#define HWY_NAMESPACE N_SVE_256
#define HWY_MAX_BYTES 32
#define HWY_HAVE_SCALABLE 0
#elif HWY_TARGET == HWY_SVE2_128
#define HWY_NAMESPACE N_SVE2_128
#define HWY_MAX_BYTES 16
#define HWY_HAVE_SCALABLE 0
#else
#define HWY_NAMESPACE N_SVE
#define HWY_MAX_BYTES 256
#define HWY_HAVE_SCALABLE 1
#endif

// Can use pragmas instead of -march compiler flag
#if HWY_HAVE_RUNTIME_DISPATCH
#if HWY_TARGET == HWY_SVE2 || HWY_TARGET == HWY_SVE2_128
// Static dispatch with -march=armv8-a+sve2+aes, or no baseline, hence dynamic
// dispatch, which checks for AES support at runtime.
#if defined(__ARM_FEATURE_SVE2_AES) || (HWY_BASELINE_SVE2 == 0)
#define HWY_TARGET_STR "+sve2+sve2-aes,+sve"
#else  // SVE2 without AES
#define HWY_TARGET_STR "+sve2,+sve"
#endif
#else  // not SVE2 target
#define HWY_TARGET_STR "+sve"
#endif
#else  // !HWY_HAVE_RUNTIME_DISPATCH
// HWY_TARGET_STR remains undefined
#endif

//-----------------------------------------------------------------------------
// WASM
#elif HWY_TARGET == HWY_WASM

#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_NAMESPACE N_WASM

#define HWY_TARGET_STR "simd128"

//-----------------------------------------------------------------------------
// WASM_EMU256
#elif HWY_TARGET == HWY_WASM_EMU256

#define HWY_ALIGN alignas(32)
#define HWY_MAX_BYTES 32
#define HWY_LANES(T) (32 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 1
#define HWY_CAP_GE512 0

#define HWY_NAMESPACE N_WASM_EMU256

#define HWY_TARGET_STR "simd128"

//-----------------------------------------------------------------------------
// RVV
#elif HWY_TARGET == HWY_RVV

// RVV only requires lane alignment, not natural alignment of the entire vector,
// and the compiler already aligns builtin types, so nothing to do here.
#define HWY_ALIGN

// The spec requires VLEN <= 2^16 bits, so the limit is 2^16 bytes (LMUL=8).
#define HWY_MAX_BYTES 65536

// = HWY_MAX_BYTES divided by max LMUL=8 because MaxLanes includes the actual
// LMUL. This is the tightest possible upper bound.
#define HWY_LANES(T) (8192 / sizeof(T))

#define HWY_HAVE_SCALABLE 1
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 0
#define HWY_NATIVE_FMA 1
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#if HWY_RVV_HAVE_F16_VEC
#define HWY_HAVE_FLOAT16 1
#else
#define HWY_HAVE_FLOAT16 0
#endif

#define HWY_NAMESPACE N_RVV

#if HWY_COMPILER_CLANG >= 1900
// https://github.com/riscv/riscv-v-spec/blob/master/v-spec.adoc#181-zvl-minimum-vector-length-standard-extensions
#define HWY_TARGET_STR "Zvl128b,Zve64d"
#else
// HWY_TARGET_STR remains undefined so HWY_ATTR is a no-op.
#endif

//-----------------------------------------------------------------------------
// EMU128
#elif HWY_TARGET == HWY_EMU128

#define HWY_ALIGN alignas(16)
#define HWY_MAX_BYTES 16
#define HWY_LANES(T) (16 / sizeof(T))

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 1
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_NAMESPACE N_EMU128

// HWY_TARGET_STR remains undefined so HWY_ATTR is a no-op.

//-----------------------------------------------------------------------------
// SCALAR
#elif HWY_TARGET == HWY_SCALAR

#define HWY_ALIGN
#define HWY_MAX_BYTES 8
#define HWY_LANES(T) 1

#define HWY_HAVE_SCALABLE 0
#define HWY_HAVE_INTEGER64 1
#define HWY_HAVE_FLOAT16 0
#define HWY_HAVE_FLOAT64 1
#define HWY_MEM_OPS_MIGHT_FAULT 0
#define HWY_NATIVE_FMA 0
#define HWY_NATIVE_DOT_BF16 0
#define HWY_CAP_GE256 0
#define HWY_CAP_GE512 0

#define HWY_NAMESPACE N_SCALAR

// HWY_TARGET_STR remains undefined so HWY_ATTR is a no-op.

#else
#pragma message("HWY_TARGET does not match any known target")
#endif  // HWY_TARGET

//-----------------------------------------------------------------------------

// Sanity check: if we have f16 vector support, then base.h should also be
// using a built-in type for f16 scalars.
#if HWY_HAVE_FLOAT16 && !HWY_HAVE_SCALAR_F16_TYPE
#error "Logic error: f16 vectors but no scalars"
#endif

// Override this to 1 in asan/msan builds, which will still fault.
#if HWY_IS_ASAN || HWY_IS_MSAN
#undef HWY_MEM_OPS_MIGHT_FAULT
#define HWY_MEM_OPS_MIGHT_FAULT 1
#endif

// Clang <9 requires this be invoked at file scope, before any namespace.
#undef HWY_BEFORE_NAMESPACE
#if defined(HWY_TARGET_STR)
#define HWY_BEFORE_NAMESPACE()        \
  HWY_PUSH_ATTRIBUTES(HWY_TARGET_STR) \
  static_assert(true, "For requiring trailing semicolon")
#else
// avoids compiler warning if no HWY_TARGET_STR
#define HWY_BEFORE_NAMESPACE() \
  static_assert(true, "For requiring trailing semicolon")
#endif

// Clang <9 requires any namespaces be closed before this macro.
#undef HWY_AFTER_NAMESPACE
#if defined(HWY_TARGET_STR)
#define HWY_AFTER_NAMESPACE() \
  HWY_POP_ATTRIBUTES          \
  static_assert(true, "For requiring trailing semicolon")
#else
// avoids compiler warning if no HWY_TARGET_STR
#define HWY_AFTER_NAMESPACE() \
  static_assert(true, "For requiring trailing semicolon")
#endif

#undef HWY_ATTR
#if defined(HWY_TARGET_STR) && HWY_HAS_ATTRIBUTE(target)
#define HWY_ATTR __attribute__((target(HWY_TARGET_STR)))
#else
#define HWY_ATTR
#endif
