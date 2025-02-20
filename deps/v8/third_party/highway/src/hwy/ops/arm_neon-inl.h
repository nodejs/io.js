// Copyright 2019 Google LLC
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

// 128-bit Arm NEON vectors and operations.
// External include guard in highway.h - see comment there.

// Arm NEON intrinsics are documented at:
// https://developer.arm.com/architectures/instruction-sets/intrinsics/#f:@navigationhierarchiessimdisa=[Neon]

#include "hwy/ops/shared-inl.h"

HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4701, ignored "-Wuninitialized")
#include <arm_neon.h>  // NOLINT(build/include_order)
HWY_DIAGNOSTICS(pop)

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

namespace detail {  // for code folding and Raw128

// Macros used to define single and double function calls for multiple types
// for full and half vectors. These macros are undefined at the end of the file.

// HWY_NEON_BUILD_TPL_* is the template<...> prefix to the function.
#define HWY_NEON_BUILD_TPL_1
#define HWY_NEON_BUILD_TPL_2
#define HWY_NEON_BUILD_TPL_3

// HWY_NEON_BUILD_RET_* is return type; type arg is without _t suffix so we can
// extend it to int32x4x2_t packs.
#define HWY_NEON_BUILD_RET_1(type, size) Vec128<type##_t, size>
#define HWY_NEON_BUILD_RET_2(type, size) Vec128<type##_t, size>
#define HWY_NEON_BUILD_RET_3(type, size) Vec128<type##_t, size>

// HWY_NEON_BUILD_PARAM_* is the list of parameters the function receives.
#define HWY_NEON_BUILD_PARAM_1(type, size) const Vec128<type##_t, size> a
#define HWY_NEON_BUILD_PARAM_2(type, size) \
  const Vec128<type##_t, size> a, const Vec128<type##_t, size> b
#define HWY_NEON_BUILD_PARAM_3(type, size)                        \
  const Vec128<type##_t, size> a, const Vec128<type##_t, size> b, \
      const Vec128<type##_t, size> c

// HWY_NEON_BUILD_ARG_* is the list of arguments passed to the underlying
// function.
#define HWY_NEON_BUILD_ARG_1 a.raw
#define HWY_NEON_BUILD_ARG_2 a.raw, b.raw
#define HWY_NEON_BUILD_ARG_3 a.raw, b.raw, c.raw

// We use HWY_NEON_EVAL(func, ...) to delay the evaluation of func until after
// the __VA_ARGS__ have been expanded. This allows "func" to be a macro on
// itself like with some of the library "functions" such as vshlq_u8. For
// example, HWY_NEON_EVAL(vshlq_u8, MY_PARAMS) where MY_PARAMS is defined as
// "a, b" (without the quotes) will end up expanding "vshlq_u8(a, b)" if needed.
// Directly writing vshlq_u8(MY_PARAMS) would fail since vshlq_u8() macro
// expects two arguments.
#define HWY_NEON_EVAL(func, ...) func(__VA_ARGS__)

// Main macro definition that defines a single function for the given type and
// size of vector, using the underlying (prefix##infix##suffix) function and
// the template, return type, parameters and arguments defined by the "args"
// parameters passed here (see HWY_NEON_BUILD_* macros defined before).
#define HWY_NEON_DEF_FUNCTION(type, size, name, prefix, infix, suffix, args) \
  HWY_CONCAT(HWY_NEON_BUILD_TPL_, args)                                      \
  HWY_API HWY_CONCAT(HWY_NEON_BUILD_RET_, args)(type, size)                  \
      name(HWY_CONCAT(HWY_NEON_BUILD_PARAM_, args)(type, size)) {            \
    return HWY_CONCAT(HWY_NEON_BUILD_RET_, args)(type, size)(                \
        HWY_NEON_EVAL(prefix##infix##suffix, HWY_NEON_BUILD_ARG_##args));    \
  }

// The HWY_NEON_DEF_FUNCTION_* macros define all the variants of a function
// called "name" using the set of neon functions starting with the given
// "prefix" for all the variants of certain types, as specified next to each
// macro. For example, the prefix "vsub" can be used to define the operator-
// using args=2.

// uint8_t
#define HWY_NEON_DEF_FUNCTION_UINT_8(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(uint8, 16, name, prefix##q, infix, u8, args) \
  HWY_NEON_DEF_FUNCTION(uint8, 8, name, prefix, infix, u8, args)     \
  HWY_NEON_DEF_FUNCTION(uint8, 4, name, prefix, infix, u8, args)     \
  HWY_NEON_DEF_FUNCTION(uint8, 2, name, prefix, infix, u8, args)     \
  HWY_NEON_DEF_FUNCTION(uint8, 1, name, prefix, infix, u8, args)

// int8_t
#define HWY_NEON_DEF_FUNCTION_INT_8(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(int8, 16, name, prefix##q, infix, s8, args) \
  HWY_NEON_DEF_FUNCTION(int8, 8, name, prefix, infix, s8, args)     \
  HWY_NEON_DEF_FUNCTION(int8, 4, name, prefix, infix, s8, args)     \
  HWY_NEON_DEF_FUNCTION(int8, 2, name, prefix, infix, s8, args)     \
  HWY_NEON_DEF_FUNCTION(int8, 1, name, prefix, infix, s8, args)

// uint16_t
#define HWY_NEON_DEF_FUNCTION_UINT_16(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(uint16, 8, name, prefix##q, infix, u16, args) \
  HWY_NEON_DEF_FUNCTION(uint16, 4, name, prefix, infix, u16, args)    \
  HWY_NEON_DEF_FUNCTION(uint16, 2, name, prefix, infix, u16, args)    \
  HWY_NEON_DEF_FUNCTION(uint16, 1, name, prefix, infix, u16, args)

// int16_t
#define HWY_NEON_DEF_FUNCTION_INT_16(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(int16, 8, name, prefix##q, infix, s16, args) \
  HWY_NEON_DEF_FUNCTION(int16, 4, name, prefix, infix, s16, args)    \
  HWY_NEON_DEF_FUNCTION(int16, 2, name, prefix, infix, s16, args)    \
  HWY_NEON_DEF_FUNCTION(int16, 1, name, prefix, infix, s16, args)

// uint32_t
#define HWY_NEON_DEF_FUNCTION_UINT_32(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(uint32, 4, name, prefix##q, infix, u32, args) \
  HWY_NEON_DEF_FUNCTION(uint32, 2, name, prefix, infix, u32, args)    \
  HWY_NEON_DEF_FUNCTION(uint32, 1, name, prefix, infix, u32, args)

// int32_t
#define HWY_NEON_DEF_FUNCTION_INT_32(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(int32, 4, name, prefix##q, infix, s32, args) \
  HWY_NEON_DEF_FUNCTION(int32, 2, name, prefix, infix, s32, args)    \
  HWY_NEON_DEF_FUNCTION(int32, 1, name, prefix, infix, s32, args)

// uint64_t
#define HWY_NEON_DEF_FUNCTION_UINT_64(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(uint64, 2, name, prefix##q, infix, u64, args) \
  HWY_NEON_DEF_FUNCTION(uint64, 1, name, prefix, infix, u64, args)

// int64_t
#define HWY_NEON_DEF_FUNCTION_INT_64(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(int64, 2, name, prefix##q, infix, s64, args) \
  HWY_NEON_DEF_FUNCTION(int64, 1, name, prefix, infix, s64, args)

// Clang 17 crashes with bf16, see github.com/llvm/llvm-project/issues/64179.
#undef HWY_NEON_HAVE_BFLOAT16
#if HWY_HAVE_SCALAR_BF16_TYPE &&                              \
    ((HWY_TARGET == HWY_NEON_BF16 &&                          \
      (!HWY_COMPILER_CLANG || HWY_COMPILER_CLANG >= 1800)) || \
     defined(__ARM_FEATURE_BF16_VECTOR_ARITHMETIC))
#define HWY_NEON_HAVE_BFLOAT16 1
#else
#define HWY_NEON_HAVE_BFLOAT16 0
#endif

// HWY_NEON_HAVE_F32_TO_BF16C is defined if NEON vcvt_bf16_f32 and
// vbfdot_f32 are available, even if the __bf16 type is disabled due to
// GCC/Clang bugs.
#undef HWY_NEON_HAVE_F32_TO_BF16C
#if HWY_NEON_HAVE_BFLOAT16 || HWY_TARGET == HWY_NEON_BF16 || \
    (defined(__ARM_FEATURE_BF16_VECTOR_ARITHMETIC) &&        \
     (HWY_COMPILER_GCC_ACTUAL >= 1000 || HWY_COMPILER_CLANG >= 1100))
#define HWY_NEON_HAVE_F32_TO_BF16C 1
#else
#define HWY_NEON_HAVE_F32_TO_BF16C 0
#endif

// bfloat16_t
#if HWY_NEON_HAVE_BFLOAT16
#define HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)       \
  HWY_NEON_DEF_FUNCTION(bfloat16, 8, name, prefix##q, infix, bf16, args) \
  HWY_NEON_DEF_FUNCTION(bfloat16, 4, name, prefix, infix, bf16, args)    \
  HWY_NEON_DEF_FUNCTION(bfloat16, 2, name, prefix, infix, bf16, args)    \
  HWY_NEON_DEF_FUNCTION(bfloat16, 1, name, prefix, infix, bf16, args)
#else
#define HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)
#endif

// Used for conversion instructions if HWY_NEON_HAVE_F16C.
#define HWY_NEON_DEF_FUNCTION_FLOAT_16_UNCONDITIONAL(name, prefix, infix, \
                                                     args)                \
  HWY_NEON_DEF_FUNCTION(float16, 8, name, prefix##q, infix, f16, args)    \
  HWY_NEON_DEF_FUNCTION(float16, 4, name, prefix, infix, f16, args)       \
  HWY_NEON_DEF_FUNCTION(float16, 2, name, prefix, infix, f16, args)       \
  HWY_NEON_DEF_FUNCTION(float16, 1, name, prefix, infix, f16, args)

// float16_t
#if HWY_HAVE_FLOAT16
#define HWY_NEON_DEF_FUNCTION_FLOAT_16(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_FLOAT_16_UNCONDITIONAL(name, prefix, infix, args)
#else
#define HWY_NEON_DEF_FUNCTION_FLOAT_16(name, prefix, infix, args)
#endif

// Enable generic functions for whichever of (f16, bf16) are not supported.
#if !HWY_HAVE_FLOAT16 && !HWY_NEON_HAVE_BFLOAT16
#define HWY_NEON_IF_EMULATED_D(D) HWY_IF_SPECIAL_FLOAT_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_SPECIAL_FLOAT_D(D)
#define HWY_NEON_IF_NOT_EMULATED_D(D) HWY_IF_NOT_SPECIAL_FLOAT_D(D)
#elif !HWY_HAVE_FLOAT16 && HWY_NEON_HAVE_BFLOAT16
#define HWY_NEON_IF_EMULATED_D(D) HWY_IF_F16_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_F16_D(D)
#define HWY_NEON_IF_NOT_EMULATED_D(D) HWY_IF_NOT_F16_D(D)
#elif HWY_HAVE_FLOAT16 && !HWY_NEON_HAVE_BFLOAT16
#define HWY_NEON_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_NEON_IF_NOT_EMULATED_D(D) HWY_IF_NOT_BF16_D(D)
#elif HWY_HAVE_FLOAT16 && HWY_NEON_HAVE_BFLOAT16
// NOTE: hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr is used instead of
// hwy::EnableIf<false>* = nullptr to avoid compiler errors since
// !hwy::IsSame<D, D>() is always false and as !hwy::IsSame<D, D>() will cause
// SFINAE to occur instead of a hard error due to a dependency on the D template
// argument
#define HWY_NEON_IF_EMULATED_D(D) hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr
#define HWY_GENERIC_IF_EMULATED_D(D) \
  hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr
#define HWY_NEON_IF_NOT_EMULATED_D(D) hwy::EnableIf<true>* = nullptr
#else
#error "Logic error, handled all four cases"
#endif

// float
#define HWY_NEON_DEF_FUNCTION_FLOAT_32(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(float32, 4, name, prefix##q, infix, f32, args) \
  HWY_NEON_DEF_FUNCTION(float32, 2, name, prefix, infix, f32, args)    \
  HWY_NEON_DEF_FUNCTION(float32, 1, name, prefix, infix, f32, args)

// double
#if HWY_HAVE_FLOAT64
#define HWY_NEON_DEF_FUNCTION_FLOAT_64(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(float64, 2, name, prefix##q, infix, f64, args) \
  HWY_NEON_DEF_FUNCTION(float64, 1, name, prefix, infix, f64, args)
#else
#define HWY_NEON_DEF_FUNCTION_FLOAT_64(name, prefix, infix, args)
#endif

// Helper macros to define for more than one type.
// uint8_t, uint16_t and uint32_t
#define HWY_NEON_DEF_FUNCTION_UINT_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_UINT_8(name, prefix, infix, args)             \
  HWY_NEON_DEF_FUNCTION_UINT_16(name, prefix, infix, args)            \
  HWY_NEON_DEF_FUNCTION_UINT_32(name, prefix, infix, args)

// int8_t, int16_t and int32_t
#define HWY_NEON_DEF_FUNCTION_INT_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INT_8(name, prefix, infix, args)             \
  HWY_NEON_DEF_FUNCTION_INT_16(name, prefix, infix, args)            \
  HWY_NEON_DEF_FUNCTION_INT_32(name, prefix, infix, args)

// uint8_t, uint16_t, uint32_t and uint64_t
#define HWY_NEON_DEF_FUNCTION_UINTS(name, prefix, infix, args)  \
  HWY_NEON_DEF_FUNCTION_UINT_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_UINT_64(name, prefix, infix, args)

// int8_t, int16_t, int32_t and int64_t
#define HWY_NEON_DEF_FUNCTION_INTS(name, prefix, infix, args)  \
  HWY_NEON_DEF_FUNCTION_INT_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INT_64(name, prefix, infix, args)

// All int*_t and uint*_t up to 64
#define HWY_NEON_DEF_FUNCTION_INTS_UINTS(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INTS(name, prefix, infix, args)             \
  HWY_NEON_DEF_FUNCTION_UINTS(name, prefix, infix, args)

#define HWY_NEON_DEF_FUNCTION_FLOAT_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_FLOAT_16(name, prefix, infix, args)          \
  HWY_NEON_DEF_FUNCTION_FLOAT_32(name, prefix, infix, args)

#define HWY_NEON_DEF_FUNCTION_ALL_FLOATS(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_FLOAT_16_32(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION_FLOAT_64(name, prefix, infix, args)

// All previous types.
#define HWY_NEON_DEF_FUNCTION_ALL_TYPES(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INTS_UINTS(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION_ALL_FLOATS(name, prefix, infix, args)

#define HWY_NEON_DEF_FUNCTION_UI_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_UINT_8_16_32(name, prefix, infix, args)     \
  HWY_NEON_DEF_FUNCTION_INT_8_16_32(name, prefix, infix, args)

#define HWY_NEON_DEF_FUNCTION_UIF_8_16_32(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_UI_8_16_32(name, prefix, infix, args)        \
  HWY_NEON_DEF_FUNCTION_FLOAT_16_32(name, prefix, infix, args)

#define HWY_NEON_DEF_FUNCTION_UIF_64(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_UINT_64(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION_INT_64(name, prefix, infix, args)       \
  HWY_NEON_DEF_FUNCTION_FLOAT_64(name, prefix, infix, args)

// For vzip1/2
#define HWY_NEON_DEF_FUNCTION_FULL_UI_64(name, prefix, infix, args)   \
  HWY_NEON_DEF_FUNCTION(uint64, 2, name, prefix##q, infix, u64, args) \
  HWY_NEON_DEF_FUNCTION(int64, 2, name, prefix##q, infix, s64, args)
#define HWY_NEON_DEF_FUNCTION_FULL_UIF_64(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_FULL_UI_64(name, prefix, infix, args)        \
  HWY_NEON_DEF_FUNCTION(float64, 2, name, prefix##q, infix, f64, args)

// For eor3q, which is only defined for full vectors.
#define HWY_NEON_DEF_FUNCTION_FULL_UI(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION(uint8, 16, name, prefix##q, infix, u8, args)  \
  HWY_NEON_DEF_FUNCTION(uint16, 8, name, prefix##q, infix, u16, args) \
  HWY_NEON_DEF_FUNCTION(uint32, 4, name, prefix##q, infix, u32, args) \
  HWY_NEON_DEF_FUNCTION(int8, 16, name, prefix##q, infix, s8, args)   \
  HWY_NEON_DEF_FUNCTION(int16, 8, name, prefix##q, infix, s16, args)  \
  HWY_NEON_DEF_FUNCTION(int32, 4, name, prefix##q, infix, s32, args)  \
  HWY_NEON_DEF_FUNCTION_FULL_UI_64(name, prefix, infix, args)
// Emulation of some intrinsics on armv7.
#if HWY_ARCH_ARM_V7
#define vuzp1_s8(x, y) vuzp_s8(x, y).val[0]
#define vuzp1_u8(x, y) vuzp_u8(x, y).val[0]
#define vuzp1_s16(x, y) vuzp_s16(x, y).val[0]
#define vuzp1_u16(x, y) vuzp_u16(x, y).val[0]
#define vuzp1_s32(x, y) vuzp_s32(x, y).val[0]
#define vuzp1_u32(x, y) vuzp_u32(x, y).val[0]
#define vuzp1_f32(x, y) vuzp_f32(x, y).val[0]
#define vuzp1q_s8(x, y) vuzpq_s8(x, y).val[0]
#define vuzp1q_u8(x, y) vuzpq_u8(x, y).val[0]
#define vuzp1q_s16(x, y) vuzpq_s16(x, y).val[0]
#define vuzp1q_u16(x, y) vuzpq_u16(x, y).val[0]
#define vuzp1q_s32(x, y) vuzpq_s32(x, y).val[0]
#define vuzp1q_u32(x, y) vuzpq_u32(x, y).val[0]
#define vuzp1q_f32(x, y) vuzpq_f32(x, y).val[0]
#define vuzp2_s8(x, y) vuzp_s8(x, y).val[1]
#define vuzp2_u8(x, y) vuzp_u8(x, y).val[1]
#define vuzp2_s16(x, y) vuzp_s16(x, y).val[1]
#define vuzp2_u16(x, y) vuzp_u16(x, y).val[1]
#define vuzp2_s32(x, y) vuzp_s32(x, y).val[1]
#define vuzp2_u32(x, y) vuzp_u32(x, y).val[1]
#define vuzp2_f32(x, y) vuzp_f32(x, y).val[1]
#define vuzp2q_s8(x, y) vuzpq_s8(x, y).val[1]
#define vuzp2q_u8(x, y) vuzpq_u8(x, y).val[1]
#define vuzp2q_s16(x, y) vuzpq_s16(x, y).val[1]
#define vuzp2q_u16(x, y) vuzpq_u16(x, y).val[1]
#define vuzp2q_s32(x, y) vuzpq_s32(x, y).val[1]
#define vuzp2q_u32(x, y) vuzpq_u32(x, y).val[1]
#define vuzp2q_f32(x, y) vuzpq_f32(x, y).val[1]
#define vzip1_s8(x, y) vzip_s8(x, y).val[0]
#define vzip1_u8(x, y) vzip_u8(x, y).val[0]
#define vzip1_s16(x, y) vzip_s16(x, y).val[0]
#define vzip1_u16(x, y) vzip_u16(x, y).val[0]
#define vzip1_f32(x, y) vzip_f32(x, y).val[0]
#define vzip1_u32(x, y) vzip_u32(x, y).val[0]
#define vzip1_s32(x, y) vzip_s32(x, y).val[0]
#define vzip1q_s8(x, y) vzipq_s8(x, y).val[0]
#define vzip1q_u8(x, y) vzipq_u8(x, y).val[0]
#define vzip1q_s16(x, y) vzipq_s16(x, y).val[0]
#define vzip1q_u16(x, y) vzipq_u16(x, y).val[0]
#define vzip1q_s32(x, y) vzipq_s32(x, y).val[0]
#define vzip1q_u32(x, y) vzipq_u32(x, y).val[0]
#define vzip1q_f32(x, y) vzipq_f32(x, y).val[0]
#define vzip2_s8(x, y) vzip_s8(x, y).val[1]
#define vzip2_u8(x, y) vzip_u8(x, y).val[1]
#define vzip2_s16(x, y) vzip_s16(x, y).val[1]
#define vzip2_u16(x, y) vzip_u16(x, y).val[1]
#define vzip2_s32(x, y) vzip_s32(x, y).val[1]
#define vzip2_u32(x, y) vzip_u32(x, y).val[1]
#define vzip2_f32(x, y) vzip_f32(x, y).val[1]
#define vzip2q_s8(x, y) vzipq_s8(x, y).val[1]
#define vzip2q_u8(x, y) vzipq_u8(x, y).val[1]
#define vzip2q_s16(x, y) vzipq_s16(x, y).val[1]
#define vzip2q_u16(x, y) vzipq_u16(x, y).val[1]
#define vzip2q_s32(x, y) vzipq_s32(x, y).val[1]
#define vzip2q_u32(x, y) vzipq_u32(x, y).val[1]
#define vzip2q_f32(x, y) vzipq_f32(x, y).val[1]
#endif

// Wrappers over uint8x16x2_t etc. so we can define StoreInterleaved2
// overloads for all vector types, even those (bfloat16_t) where the
// underlying vector is the same as others (uint16_t).
template <typename T, size_t N>
struct Tuple2;
template <typename T, size_t N>
struct Tuple3;
template <typename T, size_t N>
struct Tuple4;

template <>
struct Tuple2<uint8_t, 16> {
  uint8x16x2_t raw;
};
template <size_t N>
struct Tuple2<uint8_t, N> {
  uint8x8x2_t raw;
};
template <>
struct Tuple2<int8_t, 16> {
  int8x16x2_t raw;
};
template <size_t N>
struct Tuple2<int8_t, N> {
  int8x8x2_t raw;
};
template <>
struct Tuple2<uint16_t, 8> {
  uint16x8x2_t raw;
};
template <size_t N>
struct Tuple2<uint16_t, N> {
  uint16x4x2_t raw;
};
template <>
struct Tuple2<int16_t, 8> {
  int16x8x2_t raw;
};
template <size_t N>
struct Tuple2<int16_t, N> {
  int16x4x2_t raw;
};
template <>
struct Tuple2<uint32_t, 4> {
  uint32x4x2_t raw;
};
template <size_t N>
struct Tuple2<uint32_t, N> {
  uint32x2x2_t raw;
};
template <>
struct Tuple2<int32_t, 4> {
  int32x4x2_t raw;
};
template <size_t N>
struct Tuple2<int32_t, N> {
  int32x2x2_t raw;
};
template <>
struct Tuple2<uint64_t, 2> {
  uint64x2x2_t raw;
};
template <size_t N>
struct Tuple2<uint64_t, N> {
  uint64x1x2_t raw;
};
template <>
struct Tuple2<int64_t, 2> {
  int64x2x2_t raw;
};
template <size_t N>
struct Tuple2<int64_t, N> {
  int64x1x2_t raw;
};

template <>
struct Tuple2<float32_t, 4> {
  float32x4x2_t raw;
};
template <size_t N>
struct Tuple2<float32_t, N> {
  float32x2x2_t raw;
};
#if HWY_HAVE_FLOAT64
template <>
struct Tuple2<float64_t, 2> {
  float64x2x2_t raw;
};
template <size_t N>
struct Tuple2<float64_t, N> {
  float64x1x2_t raw;
};
#endif  // HWY_HAVE_FLOAT64

template <>
struct Tuple3<uint8_t, 16> {
  uint8x16x3_t raw;
};
template <size_t N>
struct Tuple3<uint8_t, N> {
  uint8x8x3_t raw;
};
template <>
struct Tuple3<int8_t, 16> {
  int8x16x3_t raw;
};
template <size_t N>
struct Tuple3<int8_t, N> {
  int8x8x3_t raw;
};
template <>
struct Tuple3<uint16_t, 8> {
  uint16x8x3_t raw;
};
template <size_t N>
struct Tuple3<uint16_t, N> {
  uint16x4x3_t raw;
};
template <>
struct Tuple3<int16_t, 8> {
  int16x8x3_t raw;
};
template <size_t N>
struct Tuple3<int16_t, N> {
  int16x4x3_t raw;
};
template <>
struct Tuple3<uint32_t, 4> {
  uint32x4x3_t raw;
};
template <size_t N>
struct Tuple3<uint32_t, N> {
  uint32x2x3_t raw;
};
template <>
struct Tuple3<int32_t, 4> {
  int32x4x3_t raw;
};
template <size_t N>
struct Tuple3<int32_t, N> {
  int32x2x3_t raw;
};
template <>
struct Tuple3<uint64_t, 2> {
  uint64x2x3_t raw;
};
template <size_t N>
struct Tuple3<uint64_t, N> {
  uint64x1x3_t raw;
};
template <>
struct Tuple3<int64_t, 2> {
  int64x2x3_t raw;
};
template <size_t N>
struct Tuple3<int64_t, N> {
  int64x1x3_t raw;
};

template <>
struct Tuple3<float32_t, 4> {
  float32x4x3_t raw;
};
template <size_t N>
struct Tuple3<float32_t, N> {
  float32x2x3_t raw;
};
#if HWY_HAVE_FLOAT64
template <>
struct Tuple3<float64_t, 2> {
  float64x2x3_t raw;
};
template <size_t N>
struct Tuple3<float64_t, N> {
  float64x1x3_t raw;
};
#endif  // HWY_HAVE_FLOAT64

template <>
struct Tuple4<uint8_t, 16> {
  uint8x16x4_t raw;
};
template <size_t N>
struct Tuple4<uint8_t, N> {
  uint8x8x4_t raw;
};
template <>
struct Tuple4<int8_t, 16> {
  int8x16x4_t raw;
};
template <size_t N>
struct Tuple4<int8_t, N> {
  int8x8x4_t raw;
};
template <>
struct Tuple4<uint16_t, 8> {
  uint16x8x4_t raw;
};
template <size_t N>
struct Tuple4<uint16_t, N> {
  uint16x4x4_t raw;
};
template <>
struct Tuple4<int16_t, 8> {
  int16x8x4_t raw;
};
template <size_t N>
struct Tuple4<int16_t, N> {
  int16x4x4_t raw;
};
template <>
struct Tuple4<uint32_t, 4> {
  uint32x4x4_t raw;
};
template <size_t N>
struct Tuple4<uint32_t, N> {
  uint32x2x4_t raw;
};
template <>
struct Tuple4<int32_t, 4> {
  int32x4x4_t raw;
};
template <size_t N>
struct Tuple4<int32_t, N> {
  int32x2x4_t raw;
};
template <>
struct Tuple4<uint64_t, 2> {
  uint64x2x4_t raw;
};
template <size_t N>
struct Tuple4<uint64_t, N> {
  uint64x1x4_t raw;
};
template <>
struct Tuple4<int64_t, 2> {
  int64x2x4_t raw;
};
template <size_t N>
struct Tuple4<int64_t, N> {
  int64x1x4_t raw;
};

template <>
struct Tuple4<float32_t, 4> {
  float32x4x4_t raw;
};
template <size_t N>
struct Tuple4<float32_t, N> {
  float32x2x4_t raw;
};
#if HWY_HAVE_FLOAT64
template <>
struct Tuple4<float64_t, 2> {
  float64x2x4_t raw;
};
template <size_t N>
struct Tuple4<float64_t, N> {
  float64x1x4_t raw;
};
#endif  // HWY_HAVE_FLOAT64

template <typename T, size_t N>
struct Raw128;

template <>
struct Raw128<uint8_t, 16> {
  using type = uint8x16_t;
};
template <size_t N>
struct Raw128<uint8_t, N> {
  using type = uint8x8_t;
};

template <>
struct Raw128<uint16_t, 8> {
  using type = uint16x8_t;
};
template <size_t N>
struct Raw128<uint16_t, N> {
  using type = uint16x4_t;
};

template <>
struct Raw128<uint32_t, 4> {
  using type = uint32x4_t;
};
template <size_t N>
struct Raw128<uint32_t, N> {
  using type = uint32x2_t;
};

template <>
struct Raw128<uint64_t, 2> {
  using type = uint64x2_t;
};
template <>
struct Raw128<uint64_t, 1> {
  using type = uint64x1_t;
};

template <>
struct Raw128<int8_t, 16> {
  using type = int8x16_t;
};
template <size_t N>
struct Raw128<int8_t, N> {
  using type = int8x8_t;
};

template <>
struct Raw128<int16_t, 8> {
  using type = int16x8_t;
};
template <size_t N>
struct Raw128<int16_t, N> {
  using type = int16x4_t;
};

template <>
struct Raw128<int32_t, 4> {
  using type = int32x4_t;
};
template <size_t N>
struct Raw128<int32_t, N> {
  using type = int32x2_t;
};

template <>
struct Raw128<int64_t, 2> {
  using type = int64x2_t;
};
template <>
struct Raw128<int64_t, 1> {
  using type = int64x1_t;
};

template <>
struct Raw128<float, 4> {
  using type = float32x4_t;
};
template <size_t N>
struct Raw128<float, N> {
  using type = float32x2_t;
};

#if HWY_HAVE_FLOAT64
template <>
struct Raw128<double, 2> {
  using type = float64x2_t;
};
template <>
struct Raw128<double, 1> {
  using type = float64x1_t;
};
#endif  // HWY_HAVE_FLOAT64

#if HWY_NEON_HAVE_F16C

template <>
struct Tuple2<float16_t, 8> {
  float16x8x2_t raw;
};
template <size_t N>
struct Tuple2<float16_t, N> {
  float16x4x2_t raw;
};

template <>
struct Tuple3<float16_t, 8> {
  float16x8x3_t raw;
};
template <size_t N>
struct Tuple3<float16_t, N> {
  float16x4x3_t raw;
};

template <>
struct Tuple4<float16_t, 8> {
  float16x8x4_t raw;
};
template <size_t N>
struct Tuple4<float16_t, N> {
  float16x4x4_t raw;
};

template <>
struct Raw128<float16_t, 8> {
  using type = float16x8_t;
};
template <size_t N>
struct Raw128<float16_t, N> {
  using type = float16x4_t;
};

#else  // !HWY_NEON_HAVE_F16C

template <size_t N>
struct Tuple2<float16_t, N> : public Tuple2<uint16_t, N> {};
template <size_t N>
struct Tuple3<float16_t, N> : public Tuple3<uint16_t, N> {};
template <size_t N>
struct Tuple4<float16_t, N> : public Tuple4<uint16_t, N> {};
template <size_t N>
struct Raw128<float16_t, N> : public Raw128<uint16_t, N> {};

#endif  // HWY_NEON_HAVE_F16C

#if HWY_NEON_HAVE_BFLOAT16

template <>
struct Tuple2<bfloat16_t, 8> {
  bfloat16x8x2_t raw;
};
template <size_t N>
struct Tuple2<bfloat16_t, N> {
  bfloat16x4x2_t raw;
};

template <>
struct Tuple3<bfloat16_t, 8> {
  bfloat16x8x3_t raw;
};
template <size_t N>
struct Tuple3<bfloat16_t, N> {
  bfloat16x4x3_t raw;
};

template <>
struct Tuple4<bfloat16_t, 8> {
  bfloat16x8x4_t raw;
};
template <size_t N>
struct Tuple4<bfloat16_t, N> {
  bfloat16x4x4_t raw;
};

template <>
struct Raw128<bfloat16_t, 8> {
  using type = bfloat16x8_t;
};
template <size_t N>
struct Raw128<bfloat16_t, N> {
  using type = bfloat16x4_t;
};

#else  // !HWY_NEON_HAVE_BFLOAT16

template <size_t N>
struct Tuple2<bfloat16_t, N> : public Tuple2<uint16_t, N> {};
template <size_t N>
struct Tuple3<bfloat16_t, N> : public Tuple3<uint16_t, N> {};
template <size_t N>
struct Tuple4<bfloat16_t, N> : public Tuple4<uint16_t, N> {};
template <size_t N>
struct Raw128<bfloat16_t, N> : public Raw128<uint16_t, N> {};

#endif  // HWY_NEON_HAVE_BFLOAT16

}  // namespace detail

template <typename T, size_t N = 16 / sizeof(T)>
class Vec128 {
 public:
  using Raw = typename detail::Raw128<T, N>::type;
  using PrivateT = T;                     // only for DFromV
  static constexpr size_t kPrivateN = N;  // only for DFromV

  HWY_INLINE Vec128() {}
  Vec128(const Vec128&) = default;
  Vec128& operator=(const Vec128&) = default;
  HWY_INLINE explicit Vec128(const Raw raw) : raw(raw) {}

  // Compound assignment. Only usable if there is a corresponding non-member
  // binary operator overload. For example, only f32 and f64 support division.
  HWY_INLINE Vec128& operator*=(const Vec128 other) {
    return *this = (*this * other);
  }
  HWY_INLINE Vec128& operator/=(const Vec128 other) {
    return *this = (*this / other);
  }
  HWY_INLINE Vec128& operator+=(const Vec128 other) {
    return *this = (*this + other);
  }
  HWY_INLINE Vec128& operator-=(const Vec128 other) {
    return *this = (*this - other);
  }
  HWY_INLINE Vec128& operator%=(const Vec128 other) {
    return *this = (*this % other);
  }
  HWY_INLINE Vec128& operator&=(const Vec128 other) {
    return *this = (*this & other);
  }
  HWY_INLINE Vec128& operator|=(const Vec128 other) {
    return *this = (*this | other);
  }
  HWY_INLINE Vec128& operator^=(const Vec128 other) {
    return *this = (*this ^ other);
  }

  Raw raw;
};

template <typename T>
using Vec64 = Vec128<T, 8 / sizeof(T)>;

template <typename T>
using Vec32 = Vec128<T, 4 / sizeof(T)>;

template <typename T>
using Vec16 = Vec128<T, 2 / sizeof(T)>;

// FF..FF or 0.
template <typename T, size_t N = 16 / sizeof(T)>
class Mask128 {
 public:
  // Arm C Language Extensions return and expect unsigned type.
  using Raw = typename detail::Raw128<MakeUnsigned<T>, N>::type;

  using PrivateT = T;                     // only for DFromM
  static constexpr size_t kPrivateN = N;  // only for DFromM

  HWY_INLINE Mask128() {}
  Mask128(const Mask128&) = default;
  Mask128& operator=(const Mask128&) = default;
  HWY_INLINE explicit Mask128(const Raw raw) : raw(raw) {}

  Raw raw;
};

template <typename T>
using Mask64 = Mask128<T, 8 / sizeof(T)>;

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class M>
using DFromM = Simd<typename M::PrivateT, M::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ Set

namespace detail {
// We want to route any combination of N/kPow2 to the intrinsics depending on
// whether the requested size is <= 64 bits or 128. HWY_NEON_BUILD_TPL is
// unconditional and currently does not accept inputs (such as whether the
// vector is 64 or 128-bit). Thus we are not able to use HWY_IF_V_SIZE_D for
// SFINAE. We instead define a private NativeSet which receives a Simd<> whose
// kPow2 has already been folded into its N.
#define HWY_NEON_BUILD_TPL_HWY_SET
#define HWY_NEON_BUILD_RET_HWY_SET(type, size) Vec128<type##_t, size>
#define HWY_NEON_BUILD_PARAM_HWY_SET(type, size) \
  Simd<type##_t, size, 0> /* tag */, type##_t t
#define HWY_NEON_BUILD_ARG_HWY_SET t

HWY_NEON_DEF_FUNCTION_ALL_TYPES(NativeSet, vdup, _n_, HWY_SET)
#if !HWY_HAVE_FLOAT16 && HWY_NEON_HAVE_F16C
HWY_NEON_DEF_FUNCTION_FLOAT_16_UNCONDITIONAL(NativeSet, vdup, _n_, HWY_SET)
#endif
HWY_NEON_DEF_FUNCTION_BFLOAT_16(NativeSet, vdup, _n_, HWY_SET)

template <class D, HWY_NEON_IF_EMULATED_D(D)>
HWY_API Vec128<TFromD<D>, MaxLanes(D())> NativeSet(D d, TFromD<D> t) {
  const uint16_t tu = BitCastScalar<uint16_t>(t);
  return Vec128<TFromD<D>, d.MaxLanes()>(Set(RebindToUnsigned<D>(), tu).raw);
}

#undef HWY_NEON_BUILD_TPL_HWY_SET
#undef HWY_NEON_BUILD_RET_HWY_SET
#undef HWY_NEON_BUILD_PARAM_HWY_SET
#undef HWY_NEON_BUILD_ARG_HWY_SET

}  // namespace detail

// Full vector. Cannot yet use VFromD because that is defined in terms of Set.
// Do not use a typename T = TFromD<D> argument because T will be deduced from
// the actual argument type, which can differ from TFromD<D>.
template <class D, HWY_IF_V_SIZE_D(D, 16), typename T>
HWY_INLINE Vec128<TFromD<D>> Set(D /* tag */, T t) {
  return detail::NativeSet(Full128<TFromD<D>>(), static_cast<TFromD<D>>(t));
}

// Partial vector: create 64-bit and return wrapper.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), typename T>
HWY_API Vec128<TFromD<D>, MaxLanes(D())> Set(D /* tag */, T t) {
  const Full64<TFromD<D>> dfull;
  return Vec128<TFromD<D>, MaxLanes(D())>(
      detail::NativeSet(dfull, static_cast<TFromD<D>>(t)).raw);
}

template <class D>
using VFromD = decltype(Set(D(), TFromD<D>()));

template <class D>
HWY_API VFromD<D> Zero(D d) {
  // Default ctor also works for bfloat16_t and float16_t.
  return Set(d, TFromD<D>{});
}

HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wuninitialized")
#if HWY_COMPILER_GCC_ACTUAL
HWY_DIAGNOSTICS_OFF(disable : 4701, ignored "-Wmaybe-uninitialized")
#endif

template <class D>
HWY_API VFromD<D> Undefined(D /*tag*/) {
#if HWY_HAS_BUILTIN(__builtin_nondeterministic_value)
  return VFromD<D>{__builtin_nondeterministic_value(Zero(D()).raw)};
#else
  VFromD<D> v;
  return v;
#endif
}

HWY_DIAGNOSTICS(pop)

#if !HWY_COMPILER_GCC && !HWY_COMPILER_CLANGCL
namespace detail {

#pragma pack(push, 1)

template <class T>
struct alignas(8) Vec64ValsWrapper {
  static_assert(sizeof(T) >= 1, "sizeof(T) >= 1 must be true");
  static_assert(sizeof(T) <= 8, "sizeof(T) <= 8 must be true");
  T vals[8 / sizeof(T)];
};

#pragma pack(pop)

}  // namespace detail
#endif  // !HWY_COMPILER_GCC && !HWY_COMPILER_CLANGCL

template <class D, HWY_IF_UI8_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> /*t8*/, TFromD<D> /*t9*/,
                                      TFromD<D> /*t10*/, TFromD<D> /*t11*/,
                                      TFromD<D> /*t12*/, TFromD<D> /*t13*/,
                                      TFromD<D> /*t14*/, TFromD<D> /*t15*/) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int8_t GccI8RawVectType __attribute__((__vector_size__(8)));
  (void)d;
  const GccI8RawVectType raw = {
      static_cast<int8_t>(t0), static_cast<int8_t>(t1), static_cast<int8_t>(t2),
      static_cast<int8_t>(t3), static_cast<int8_t>(t4), static_cast<int8_t>(t5),
      static_cast<int8_t>(t6), static_cast<int8_t>(t7)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  return ResizeBitCast(
      d, Set(Full64<uint64_t>(),
             BitCastScalar<uint64_t>(detail::Vec64ValsWrapper<TFromD<D>>{
                 {t0, t1, t2, t3, t4, t5, t6, t7}})));
#endif
}

template <class D, HWY_IF_UI16_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3,
                                      TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                      TFromD<D> /*t6*/, TFromD<D> /*t7*/) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int16_t GccI16RawVectType __attribute__((__vector_size__(8)));
  (void)d;
  const GccI16RawVectType raw = {
      static_cast<int16_t>(t0), static_cast<int16_t>(t1),
      static_cast<int16_t>(t2), static_cast<int16_t>(t3)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  return ResizeBitCast(
      d, Set(Full64<uint64_t>(),
             BitCastScalar<uint64_t>(
                 detail::Vec64ValsWrapper<TFromD<D>>{{t0, t1, t2, t3}})));
#endif
}

template <class D, HWY_IF_UI32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> /*t2*/, TFromD<D> /*t3*/) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int32_t GccI32RawVectType __attribute__((__vector_size__(8)));
  (void)d;
  const GccI32RawVectType raw = {static_cast<int32_t>(t0),
                                 static_cast<int32_t>(t1)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  return ResizeBitCast(d,
                       Set(Full64<uint64_t>(),
                           BitCastScalar<uint64_t>(
                               detail::Vec64ValsWrapper<TFromD<D>>{{t0, t1}})));
#endif
}

template <class D, HWY_IF_F32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> /*t2*/, TFromD<D> /*t3*/) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef float GccF32RawVectType __attribute__((__vector_size__(8)));
  (void)d;
  const GccF32RawVectType raw = {t0, t1};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  return ResizeBitCast(d,
                       Set(Full64<uint64_t>(),
                           BitCastScalar<uint64_t>(
                               detail::Vec64ValsWrapper<TFromD<D>>{{t0, t1}})));
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_V_SIZE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> /*t1*/) {
  return Set(d, t0);
}

template <class D, HWY_IF_UI8_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int8_t GccI8RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccI8RawVectType raw = {
      static_cast<int8_t>(t0),  static_cast<int8_t>(t1),
      static_cast<int8_t>(t2),  static_cast<int8_t>(t3),
      static_cast<int8_t>(t4),  static_cast<int8_t>(t5),
      static_cast<int8_t>(t6),  static_cast<int8_t>(t7),
      static_cast<int8_t>(t8),  static_cast<int8_t>(t9),
      static_cast<int8_t>(t10), static_cast<int8_t>(t11),
      static_cast<int8_t>(t12), static_cast<int8_t>(t13),
      static_cast<int8_t>(t14), static_cast<int8_t>(t15)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d,
                 Dup128VecFromValues(dh, t8, t9, t10, t11, t12, t13, t14, t15,
                                     t8, t9, t10, t11, t12, t13, t14, t15),
                 Dup128VecFromValues(dh, t0, t1, t2, t3, t4, t5, t6, t7, t0, t1,
                                     t2, t3, t4, t5, t6, t7));
#endif
}

template <class D, HWY_IF_UI16_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int16_t GccI16RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccI16RawVectType raw = {
      static_cast<int16_t>(t0), static_cast<int16_t>(t1),
      static_cast<int16_t>(t2), static_cast<int16_t>(t3),
      static_cast<int16_t>(t4), static_cast<int16_t>(t5),
      static_cast<int16_t>(t6), static_cast<int16_t>(t7)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d, Dup128VecFromValues(dh, t4, t5, t6, t7, t4, t5, t6, t7),
                 Dup128VecFromValues(dh, t0, t1, t2, t3, t0, t1, t2, t3));
#endif
}

template <class D, HWY_IF_UI32_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int32_t GccI32RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccI32RawVectType raw = {
      static_cast<int32_t>(t0), static_cast<int32_t>(t1),
      static_cast<int32_t>(t2), static_cast<int32_t>(t3)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d, Dup128VecFromValues(dh, t2, t3, t2, t3),
                 Dup128VecFromValues(dh, t0, t1, t0, t1));
#endif
}

template <class D, HWY_IF_F32_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccF32RawVectType raw = {t0, t1, t2, t3};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d, Dup128VecFromValues(dh, t2, t3, t2, t3),
                 Dup128VecFromValues(dh, t0, t1, t0, t1));
#endif
}

template <class D, HWY_IF_UI64_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef int64_t GccI64RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccI64RawVectType raw = {static_cast<int64_t>(t0),
                                 static_cast<int64_t>(t1)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d, Set(dh, t1), Set(dh, t0));
#endif
}

#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_F64_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1) {
#if HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL
  typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccF64RawVectType raw = {t0, t1};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
#else
  const Half<decltype(d)> dh;
  return Combine(d, Set(dh, t1), Set(dh, t0));
#endif
}
#endif

// Generic for all vector lengths
template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  const RebindToSigned<decltype(d)> di;
  return BitCast(d,
                 Dup128VecFromValues(
                     di, BitCastScalar<int16_t>(t0), BitCastScalar<int16_t>(t1),
                     BitCastScalar<int16_t>(t2), BitCastScalar<int16_t>(t3),
                     BitCastScalar<int16_t>(t4), BitCastScalar<int16_t>(t5),
                     BitCastScalar<int16_t>(t6), BitCastScalar<int16_t>(t7)));
}

#if (HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL) && HWY_NEON_HAVE_F16C
template <class D, HWY_IF_F16_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3,
                                      TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                      TFromD<D> /*t6*/, TFromD<D> /*t7*/) {
  typedef __fp16 GccF16RawVectType __attribute__((__vector_size__(8)));
  (void)d;
  const GccF16RawVectType raw = {
      static_cast<__fp16>(t0), static_cast<__fp16>(t1), static_cast<__fp16>(t2),
      static_cast<__fp16>(t3)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
}
template <class D, HWY_IF_F16_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  typedef __fp16 GccF16RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  const GccF16RawVectType raw = {
      static_cast<__fp16>(t0), static_cast<__fp16>(t1), static_cast<__fp16>(t2),
      static_cast<__fp16>(t3), static_cast<__fp16>(t4), static_cast<__fp16>(t5),
      static_cast<__fp16>(t6), static_cast<__fp16>(t7)};
  return VFromD<D>(reinterpret_cast<typename VFromD<D>::Raw>(raw));
}
#else
// Generic for all vector lengths if MSVC or !HWY_NEON_HAVE_F16C
template <class D, HWY_IF_F16_D(D)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  const RebindToSigned<decltype(d)> di;
  return BitCast(d,
                 Dup128VecFromValues(
                     di, BitCastScalar<int16_t>(t0), BitCastScalar<int16_t>(t1),
                     BitCastScalar<int16_t>(t2), BitCastScalar<int16_t>(t3),
                     BitCastScalar<int16_t>(t4), BitCastScalar<int16_t>(t5),
                     BitCastScalar<int16_t>(t6), BitCastScalar<int16_t>(t7)));
}
#endif  // (HWY_COMPILER_GCC || HWY_COMPILER_CLANGCL) && HWY_NEON_HAVE_F16C

namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<D> Iota0(D d) {
  return Dup128VecFromValues(
      d, TFromD<D>{0}, TFromD<D>{1}, TFromD<D>{2}, TFromD<D>{3}, TFromD<D>{4},
      TFromD<D>{5}, TFromD<D>{6}, TFromD<D>{7}, TFromD<D>{8}, TFromD<D>{9},
      TFromD<D>{10}, TFromD<D>{11}, TFromD<D>{12}, TFromD<D>{13}, TFromD<D>{14},
      TFromD<D>{15});
}

template <class D, HWY_IF_UI16_D(D)>
HWY_INLINE VFromD<D> Iota0(D d) {
  return Dup128VecFromValues(d, TFromD<D>{0}, TFromD<D>{1}, TFromD<D>{2},
                             TFromD<D>{3}, TFromD<D>{4}, TFromD<D>{5},
                             TFromD<D>{6}, TFromD<D>{7});
}

template <class D, HWY_IF_F16_D(D)>
HWY_INLINE VFromD<D> Iota0(D d) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Dup128VecFromValues(du, uint16_t{0}, uint16_t{0x3C00},
                                        uint16_t{0x4000}, uint16_t{0x4200},
                                        uint16_t{0x4400}, uint16_t{0x4500},
                                        uint16_t{0x4600}, uint16_t{0x4700}));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<D> Iota0(D d) {
  return Dup128VecFromValues(d, TFromD<D>{0}, TFromD<D>{1}, TFromD<D>{2},
                             TFromD<D>{3});
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<D> Iota0(D d) {
  return Dup128VecFromValues(d, TFromD<D>{0}, TFromD<D>{1});
}

#if HWY_COMPILER_MSVC
template <class V, HWY_IF_V_SIZE_LE_V(V, 4)>
static HWY_INLINE V MaskOutIota(V v) {
  constexpr size_t kVecSizeInBytes = HWY_MAX_LANES_V(V) * sizeof(TFromV<V>);
  constexpr uint64_t kU64MaskOutMask =
      hwy::LimitsMax<hwy::UnsignedFromSize<kVecSizeInBytes>>();

  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  using VU8 = VFromD<decltype(du8)>;
  const auto mask_out_mask =
      BitCast(d, VU8(vreinterpret_u8_u64(vdup_n_u64(kU64MaskOutMask))));
  return v & mask_out_mask;
}
template <class V, HWY_IF_V_SIZE_GT_V(V, 4)>
static HWY_INLINE V MaskOutIota(V v) {
  return v;
}
#endif

}  // namespace detail

template <class D, typename T2>
HWY_API VFromD<D> Iota(D d, const T2 first) {
  const auto result_iota =
      detail::Iota0(d) + Set(d, static_cast<TFromD<D>>(first));
#if HWY_COMPILER_MSVC
  return detail::MaskOutIota(result_iota);
#else
  return result_iota;
#endif
}

// ------------------------------ Combine

// Full result
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> Combine(D /* tag */, Vec64<uint8_t> hi,
                                Vec64<uint8_t> lo) {
  return Vec128<uint8_t>(vcombine_u8(lo.raw, hi.raw));
}
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> Combine(D /* tag */, Vec64<uint16_t> hi,
                                 Vec64<uint16_t> lo) {
  return Vec128<uint16_t>(vcombine_u16(lo.raw, hi.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> Combine(D /* tag */, Vec64<uint32_t> hi,
                                 Vec64<uint32_t> lo) {
  return Vec128<uint32_t>(vcombine_u32(lo.raw, hi.raw));
}
template <class D, HWY_IF_U64_D(D)>
HWY_API Vec128<uint64_t> Combine(D /* tag */, Vec64<uint64_t> hi,
                                 Vec64<uint64_t> lo) {
  return Vec128<uint64_t>(vcombine_u64(lo.raw, hi.raw));
}

template <class D, HWY_IF_I8_D(D)>
HWY_API Vec128<int8_t> Combine(D /* tag */, Vec64<int8_t> hi,
                               Vec64<int8_t> lo) {
  return Vec128<int8_t>(vcombine_s8(lo.raw, hi.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> Combine(D /* tag */, Vec64<int16_t> hi,
                                Vec64<int16_t> lo) {
  return Vec128<int16_t>(vcombine_s16(lo.raw, hi.raw));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> Combine(D /* tag */, Vec64<int32_t> hi,
                                Vec64<int32_t> lo) {
  return Vec128<int32_t>(vcombine_s32(lo.raw, hi.raw));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> Combine(D /* tag */, Vec64<int64_t> hi,
                                Vec64<int64_t> lo) {
  return Vec128<int64_t>(vcombine_s64(lo.raw, hi.raw));
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> Combine(D, Vec64<float16_t> hi, Vec64<float16_t> lo) {
  return Vec128<float16_t>(vcombine_f16(lo.raw, hi.raw));
}
#endif  // HWY_HAVE_FLOAT16

#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> Combine(D, Vec64<bfloat16_t> hi, Vec64<bfloat16_t> lo) {
  return VFromD<D>(vcombine_bf16(lo.raw, hi.raw));
}
#endif  // HWY_NEON_HAVE_BFLOAT16

template <class D, class DH = Half<D>, HWY_NEON_IF_EMULATED_D(D)>
HWY_API VFromD<D> Combine(D d, VFromD<DH> hi, VFromD<DH> lo) {
  const RebindToUnsigned<D> du;
  const Half<decltype(du)> duh;
  return BitCast(d, Combine(du, BitCast(duh, hi), BitCast(duh, lo)));
}

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec128<float> Combine(D /* tag */, Vec64<float> hi, Vec64<float> lo) {
  return Vec128<float>(vcombine_f32(lo.raw, hi.raw));
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> Combine(D /* tag */, Vec64<double> hi,
                               Vec64<double> lo) {
  return Vec128<double>(vcombine_f64(lo.raw, hi.raw));
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ BitCast

namespace detail {

// Converts from Vec128<T, N> to Vec128<uint8_t, N * sizeof(T)> using the
// vreinterpret*_u8_*() set of functions.
#define HWY_NEON_BUILD_TPL_HWY_CAST_TO_U8
#define HWY_NEON_BUILD_RET_HWY_CAST_TO_U8(type, size) \
  Vec128<uint8_t, size * sizeof(type##_t)>
#define HWY_NEON_BUILD_PARAM_HWY_CAST_TO_U8(type, size) Vec128<type##_t, size> v
#define HWY_NEON_BUILD_ARG_HWY_CAST_TO_U8 v.raw

// Special case of u8 to u8 since vreinterpret*_u8_u8 is obviously not defined.
template <size_t N>
HWY_INLINE Vec128<uint8_t, N> BitCastToByte(Vec128<uint8_t, N> v) {
  return v;
}

HWY_NEON_DEF_FUNCTION_ALL_FLOATS(BitCastToByte, vreinterpret, _u8_,
                                 HWY_CAST_TO_U8)
HWY_NEON_DEF_FUNCTION_BFLOAT_16(BitCastToByte, vreinterpret, _u8_,
                                HWY_CAST_TO_U8)

HWY_NEON_DEF_FUNCTION_INTS(BitCastToByte, vreinterpret, _u8_, HWY_CAST_TO_U8)
HWY_NEON_DEF_FUNCTION_UINT_16(BitCastToByte, vreinterpret, _u8_, HWY_CAST_TO_U8)
HWY_NEON_DEF_FUNCTION_UINT_32(BitCastToByte, vreinterpret, _u8_, HWY_CAST_TO_U8)
HWY_NEON_DEF_FUNCTION_UINT_64(BitCastToByte, vreinterpret, _u8_, HWY_CAST_TO_U8)

#if !HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_F16C
HWY_NEON_DEF_FUNCTION_FLOAT_16_UNCONDITIONAL(BitCastToByte, vreinterpret, _u8_,
                                             HWY_CAST_TO_U8)
#else
template <size_t N>
HWY_INLINE Vec128<uint8_t, N * 2> BitCastToByte(Vec128<float16_t, N> v) {
  return BitCastToByte(Vec128<uint16_t, N>(v.raw));
}
#endif  // HWY_NEON_HAVE_F16C
#endif  // !HWY_HAVE_FLOAT16

#if !HWY_NEON_HAVE_BFLOAT16
template <size_t N>
HWY_INLINE Vec128<uint8_t, N * 2> BitCastToByte(Vec128<bfloat16_t, N> v) {
  return BitCastToByte(Vec128<uint16_t, N>(v.raw));
}
#endif  // !HWY_NEON_HAVE_BFLOAT16

#undef HWY_NEON_BUILD_TPL_HWY_CAST_TO_U8
#undef HWY_NEON_BUILD_RET_HWY_CAST_TO_U8
#undef HWY_NEON_BUILD_PARAM_HWY_CAST_TO_U8
#undef HWY_NEON_BUILD_ARG_HWY_CAST_TO_U8

template <class D, HWY_IF_U8_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */, VFromD<D> v) {
  return v;
}

// 64-bit or less:

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I8_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<RebindToUnsigned<D>> v) {
  return VFromD<D>(vreinterpret_s8_u8(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>(vreinterpret_u16_u8(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>(vreinterpret_s16_u8(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>(vreinterpret_u32_u8(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>(vreinterpret_s32_u8(v.raw));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U64_D(D)>
HWY_INLINE Vec64<uint64_t> BitCastFromByte(D /* tag */, Vec64<uint8_t> v) {
  return Vec64<uint64_t>(vreinterpret_u64_u8(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I64_D(D)>
HWY_INLINE Vec64<int64_t> BitCastFromByte(D /* tag */, Vec64<uint8_t> v) {
  return Vec64<int64_t>(vreinterpret_s64_u8(v.raw));
}

// Cannot use HWY_NEON_IF_EMULATED_D due to the extra HWY_NEON_HAVE_F16C.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D, VFromD<Repartition<uint8_t, D>> v) {
#if HWY_HAVE_FLOAT16 || HWY_NEON_HAVE_F16C
  return VFromD<D>(vreinterpret_f16_u8(v.raw));
#else
  const RebindToUnsigned<D> du;
  return VFromD<D>(BitCastFromByte(du, v).raw);
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D, VFromD<Repartition<uint8_t, D>> v) {
#if HWY_NEON_HAVE_BFLOAT16
  return VFromD<D>(vreinterpret_bf16_u8(v.raw));
#else
  const RebindToUnsigned<D> du;
  return VFromD<D>(BitCastFromByte(du, v).raw);
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>(vreinterpret_f32_u8(v.raw));
}

#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F64_D(D)>
HWY_INLINE Vec64<double> BitCastFromByte(D /* tag */, Vec64<uint8_t> v) {
  return Vec64<double>(vreinterpret_f64_u8(v.raw));
}
#endif  // HWY_HAVE_FLOAT64

// 128-bit full:

template <class D, HWY_IF_I8_D(D)>
HWY_INLINE Vec128<int8_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<int8_t>(vreinterpretq_s8_u8(v.raw));
}
template <class D, HWY_IF_U16_D(D)>
HWY_INLINE Vec128<uint16_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<uint16_t>(vreinterpretq_u16_u8(v.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_INLINE Vec128<int16_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<int16_t>(vreinterpretq_s16_u8(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_INLINE Vec128<uint32_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<uint32_t>(vreinterpretq_u32_u8(v.raw));
}
template <class D, HWY_IF_I32_D(D)>
HWY_INLINE Vec128<int32_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<int32_t>(vreinterpretq_s32_u8(v.raw));
}
template <class D, HWY_IF_U64_D(D)>
HWY_INLINE Vec128<uint64_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<uint64_t>(vreinterpretq_u64_u8(v.raw));
}
template <class D, HWY_IF_I64_D(D)>
HWY_INLINE Vec128<int64_t> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<int64_t>(vreinterpretq_s64_u8(v.raw));
}

template <class D, HWY_IF_F32_D(D)>
HWY_INLINE Vec128<float> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<float>(vreinterpretq_f32_u8(v.raw));
}

#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_F64_D(D)>
HWY_INLINE Vec128<double> BitCastFromByte(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<double>(vreinterpretq_f64_u8(v.raw));
}
#endif  // HWY_HAVE_FLOAT64

// Cannot use HWY_NEON_IF_EMULATED_D due to the extra HWY_NEON_HAVE_F16C.
template <class D, HWY_IF_F16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D, Vec128<uint8_t> v) {
#if HWY_HAVE_FLOAT16 || HWY_NEON_HAVE_F16C
  return VFromD<D>(vreinterpretq_f16_u8(v.raw));
#else
  return VFromD<D>(BitCastFromByte(RebindToUnsigned<D>(), v).raw);
#endif
}

template <class D, HWY_IF_BF16_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D, Vec128<uint8_t> v) {
#if HWY_NEON_HAVE_BFLOAT16
  return VFromD<D>(vreinterpretq_bf16_u8(v.raw));
#else
  return VFromD<D>(BitCastFromByte(RebindToUnsigned<D>(), v).raw);
#endif
}

}  // namespace detail

template <class D, class FromT>
HWY_API VFromD<D> BitCast(D d,
                          Vec128<FromT, Repartition<FromT, D>().MaxLanes()> v) {
  return detail::BitCastFromByte(d, detail::BitCastToByte(v));
}

// ------------------------------ ResizeBitCast

// <= 8 byte vector to <= 8 byte vector
template <class D, class FromV, HWY_IF_V_SIZE_LE_V(FromV, 8),
          HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  const Repartition<uint8_t, decltype(d)> du8;
  return BitCast(d, VFromD<decltype(du8)>{detail::BitCastToByte(v).raw});
}

// 16-byte vector to 16-byte vector: same as BitCast
template <class D, class FromV, HWY_IF_V_SIZE_V(FromV, 16),
          HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  return BitCast(d, v);
}

// 16-byte vector to <= 8-byte vector
template <class D, class FromV, HWY_IF_V_SIZE_V(FromV, 16),
          HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  const DFromV<decltype(v)> d_from;
  const Half<decltype(d_from)> dh_from;
  return ResizeBitCast(d, LowerHalf(dh_from, v));
}

// <= 8-bit vector to 16-byte vector
template <class D, class FromV, HWY_IF_V_SIZE_LE_V(FromV, 8),
          HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  const Full64<TFromV<FromV>> d_full64_from;
  const Full128<TFromV<FromV>> d_full128_from;
  return BitCast(d, Combine(d_full128_from, Zero(d_full64_from),
                            ResizeBitCast(d_full64_from, v)));
}

// ------------------------------ GetLane

namespace detail {
#define HWY_NEON_BUILD_TPL_HWY_GET template <size_t kLane>
#define HWY_NEON_BUILD_RET_HWY_GET(type, size) type##_t
#define HWY_NEON_BUILD_PARAM_HWY_GET(type, size) Vec128<type##_t, size> v
#define HWY_NEON_BUILD_ARG_HWY_GET v.raw, kLane

HWY_NEON_DEF_FUNCTION_ALL_TYPES(GetLane, vget, _lane_, HWY_GET)
HWY_NEON_DEF_FUNCTION_BFLOAT_16(GetLane, vget, _lane_, HWY_GET)

template <size_t kLane, class V, HWY_NEON_IF_EMULATED_D(DFromV<V>)>
static HWY_INLINE HWY_MAYBE_UNUSED TFromV<V> GetLane(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCastScalar<TFromV<V>>(GetLane<kLane>(BitCast(du, v)));
}

#undef HWY_NEON_BUILD_TPL_HWY_GET
#undef HWY_NEON_BUILD_RET_HWY_GET
#undef HWY_NEON_BUILD_PARAM_HWY_GET
#undef HWY_NEON_BUILD_ARG_HWY_GET

}  // namespace detail

template <class V>
HWY_API TFromV<V> GetLane(const V v) {
  return detail::GetLane<0>(v);
}

// ------------------------------ ExtractLane

// Requires one overload per vector length because GetLane<3> is a compile error
// if v is a uint32x2_t.
template <typename T>
HWY_API T ExtractLane(const Vec128<T, 1> v, size_t i) {
  HWY_DASSERT(i == 0);
  (void)i;
  return detail::GetLane<0>(v);
}

template <typename T>
HWY_API T ExtractLane(const Vec128<T, 2> v, size_t i) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::GetLane<0>(v);
      case 1:
        return detail::GetLane<1>(v);
    }
  }
#endif
  alignas(16) T lanes[2];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[i];
}

template <typename T>
HWY_API T ExtractLane(const Vec128<T, 4> v, size_t i) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::GetLane<0>(v);
      case 1:
        return detail::GetLane<1>(v);
      case 2:
        return detail::GetLane<2>(v);
      case 3:
        return detail::GetLane<3>(v);
    }
  }
#endif
  alignas(16) T lanes[4];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[i];
}

template <typename T>
HWY_API T ExtractLane(const Vec128<T, 8> v, size_t i) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::GetLane<0>(v);
      case 1:
        return detail::GetLane<1>(v);
      case 2:
        return detail::GetLane<2>(v);
      case 3:
        return detail::GetLane<3>(v);
      case 4:
        return detail::GetLane<4>(v);
      case 5:
        return detail::GetLane<5>(v);
      case 6:
        return detail::GetLane<6>(v);
      case 7:
        return detail::GetLane<7>(v);
    }
  }
#endif
  alignas(16) T lanes[8];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[i];
}

template <typename T>
HWY_API T ExtractLane(const Vec128<T, 16> v, size_t i) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::GetLane<0>(v);
      case 1:
        return detail::GetLane<1>(v);
      case 2:
        return detail::GetLane<2>(v);
      case 3:
        return detail::GetLane<3>(v);
      case 4:
        return detail::GetLane<4>(v);
      case 5:
        return detail::GetLane<5>(v);
      case 6:
        return detail::GetLane<6>(v);
      case 7:
        return detail::GetLane<7>(v);
      case 8:
        return detail::GetLane<8>(v);
      case 9:
        return detail::GetLane<9>(v);
      case 10:
        return detail::GetLane<10>(v);
      case 11:
        return detail::GetLane<11>(v);
      case 12:
        return detail::GetLane<12>(v);
      case 13:
        return detail::GetLane<13>(v);
      case 14:
        return detail::GetLane<14>(v);
      case 15:
        return detail::GetLane<15>(v);
    }
  }
#endif
  alignas(16) T lanes[16];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[i];
}

// ------------------------------ InsertLane

namespace detail {
#define HWY_NEON_BUILD_TPL_HWY_INSERT template <size_t kLane>
#define HWY_NEON_BUILD_RET_HWY_INSERT(type, size) Vec128<type##_t, size>
#define HWY_NEON_BUILD_PARAM_HWY_INSERT(type, size) \
  Vec128<type##_t, size> v, type##_t t
#define HWY_NEON_BUILD_ARG_HWY_INSERT t, v.raw, kLane

HWY_NEON_DEF_FUNCTION_ALL_TYPES(InsertLane, vset, _lane_, HWY_INSERT)
HWY_NEON_DEF_FUNCTION_BFLOAT_16(InsertLane, vset, _lane_, HWY_INSERT)

#undef HWY_NEON_BUILD_TPL_HWY_INSERT
#undef HWY_NEON_BUILD_RET_HWY_INSERT
#undef HWY_NEON_BUILD_PARAM_HWY_INSERT
#undef HWY_NEON_BUILD_ARG_HWY_INSERT

template <size_t kLane, class V, class D = DFromV<V>, HWY_NEON_IF_EMULATED_D(D)>
HWY_API V InsertLane(const V v, TFromD<D> t) {
  const D d;
  const RebindToUnsigned<D> du;
  const uint16_t tu = BitCastScalar<uint16_t>(t);
  return BitCast(d, InsertLane<kLane>(BitCast(du, v), tu));
}

}  // namespace detail

// Requires one overload per vector length because InsertLane<3> may be a
// compile error.

template <typename T>
HWY_API Vec128<T, 1> InsertLane(const Vec128<T, 1> v, size_t i, T t) {
  HWY_DASSERT(i == 0);
  (void)i;
  return Set(DFromV<decltype(v)>(), t);
}

template <typename T>
HWY_API Vec128<T, 2> InsertLane(const Vec128<T, 2> v, size_t i, T t) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::InsertLane<0>(v, t);
      case 1:
        return detail::InsertLane<1>(v, t);
    }
  }
#endif
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[2];
  Store(v, d, lanes);
  lanes[i] = t;
  return Load(d, lanes);
}

template <typename T>
HWY_API Vec128<T, 4> InsertLane(const Vec128<T, 4> v, size_t i, T t) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::InsertLane<0>(v, t);
      case 1:
        return detail::InsertLane<1>(v, t);
      case 2:
        return detail::InsertLane<2>(v, t);
      case 3:
        return detail::InsertLane<3>(v, t);
    }
  }
#endif
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[4];
  Store(v, d, lanes);
  lanes[i] = t;
  return Load(d, lanes);
}

template <typename T>
HWY_API Vec128<T, 8> InsertLane(const Vec128<T, 8> v, size_t i, T t) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::InsertLane<0>(v, t);
      case 1:
        return detail::InsertLane<1>(v, t);
      case 2:
        return detail::InsertLane<2>(v, t);
      case 3:
        return detail::InsertLane<3>(v, t);
      case 4:
        return detail::InsertLane<4>(v, t);
      case 5:
        return detail::InsertLane<5>(v, t);
      case 6:
        return detail::InsertLane<6>(v, t);
      case 7:
        return detail::InsertLane<7>(v, t);
    }
  }
#endif
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[8];
  Store(v, d, lanes);
  lanes[i] = t;
  return Load(d, lanes);
}

template <typename T>
HWY_API Vec128<T, 16> InsertLane(const Vec128<T, 16> v, size_t i, T t) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::InsertLane<0>(v, t);
      case 1:
        return detail::InsertLane<1>(v, t);
      case 2:
        return detail::InsertLane<2>(v, t);
      case 3:
        return detail::InsertLane<3>(v, t);
      case 4:
        return detail::InsertLane<4>(v, t);
      case 5:
        return detail::InsertLane<5>(v, t);
      case 6:
        return detail::InsertLane<6>(v, t);
      case 7:
        return detail::InsertLane<7>(v, t);
      case 8:
        return detail::InsertLane<8>(v, t);
      case 9:
        return detail::InsertLane<9>(v, t);
      case 10:
        return detail::InsertLane<10>(v, t);
      case 11:
        return detail::InsertLane<11>(v, t);
      case 12:
        return detail::InsertLane<12>(v, t);
      case 13:
        return detail::InsertLane<13>(v, t);
      case 14:
        return detail::InsertLane<14>(v, t);
      case 15:
        return detail::InsertLane<15>(v, t);
    }
  }
#endif
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[16];
  Store(v, d, lanes);
  lanes[i] = t;
  return Load(d, lanes);
}

// ================================================== ARITHMETIC

// ------------------------------ Addition
HWY_NEON_DEF_FUNCTION_ALL_TYPES(operator+, vadd, _, 2)

// ------------------------------ Subtraction
HWY_NEON_DEF_FUNCTION_ALL_TYPES(operator-, vsub, _, 2)

// ------------------------------ SumsOf8

HWY_API Vec128<uint64_t> SumsOf8(const Vec128<uint8_t> v) {
  return Vec128<uint64_t>(vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(v.raw))));
}
HWY_API Vec64<uint64_t> SumsOf8(const Vec64<uint8_t> v) {
  return Vec64<uint64_t>(vpaddl_u32(vpaddl_u16(vpaddl_u8(v.raw))));
}
HWY_API Vec128<int64_t> SumsOf8(const Vec128<int8_t> v) {
  return Vec128<int64_t>(vpaddlq_s32(vpaddlq_s16(vpaddlq_s8(v.raw))));
}
HWY_API Vec64<int64_t> SumsOf8(const Vec64<int8_t> v) {
  return Vec64<int64_t>(vpaddl_s32(vpaddl_s16(vpaddl_s8(v.raw))));
}

// ------------------------------ SumsOf2
namespace detail {

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_s8(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_s8(v.raw));
}

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_u8(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_u8(v.raw));
}

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_s16(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_s16(v.raw));
}

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_u16(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_u16(v.raw));
}

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_s32(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_s32(v.raw));
}

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddl_u32(v.raw));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  return VFromD<RepartitionToWide<DFromV<V>>>(vpaddlq_u32(v.raw));
}

}  // namespace detail

// ------------------------------ SaturatedAdd

#ifdef HWY_NATIVE_I32_SATURATED_ADDSUB
#undef HWY_NATIVE_I32_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I32_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_U32_SATURATED_ADDSUB
#undef HWY_NATIVE_U32_SATURATED_ADDSUB
#else
#define HWY_NATIVE_U32_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_I64_SATURATED_ADDSUB
#undef HWY_NATIVE_I64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I64_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_U64_SATURATED_ADDSUB
#undef HWY_NATIVE_U64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_U64_SATURATED_ADDSUB
#endif

// Returns a + b clamped to the destination range.
HWY_NEON_DEF_FUNCTION_INTS_UINTS(SaturatedAdd, vqadd, _, 2)

// ------------------------------ SaturatedSub

// Returns a - b clamped to the destination range.
HWY_NEON_DEF_FUNCTION_INTS_UINTS(SaturatedSub, vqsub, _, 2)

// ------------------------------ Average

// Returns (a + b + 1) / 2

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI32
#undef HWY_NATIVE_AVERAGE_ROUND_UI32
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI32
#endif

HWY_NEON_DEF_FUNCTION_UI_8_16_32(AverageRound, vrhadd, _, 2)

// ------------------------------ Neg

HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Neg, vneg, _, 1)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(Neg, vneg, _, 1)  // i64 implemented below

#if !HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Neg(const Vec128<float16_t, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  return BitCast(d, Xor(BitCast(du, v), Set(du, SignMask<TU>())));
}
#endif  // !HWY_HAVE_FLOAT16

// There is no vneg for bf16, but we can cast to f16 (emulated or native).
template <size_t N>
HWY_API Vec128<bfloat16_t, N> Neg(const Vec128<bfloat16_t, N> v) {
  const DFromV<decltype(v)> d;
  const Rebind<float16_t, decltype(d)> df16;
  return BitCast(d, Neg(BitCast(df16, v)));
}

HWY_API Vec64<int64_t> Neg(const Vec64<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec64<int64_t>(vneg_s64(v.raw));
#else
  return Zero(DFromV<decltype(v)>()) - v;
#endif
}

HWY_API Vec128<int64_t> Neg(const Vec128<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec128<int64_t>(vnegq_s64(v.raw));
#else
  return Zero(DFromV<decltype(v)>()) - v;
#endif
}

// ------------------------------ SaturatedNeg
#ifdef HWY_NATIVE_SATURATED_NEG_8_16_32
#undef HWY_NATIVE_SATURATED_NEG_8_16_32
#else
#define HWY_NATIVE_SATURATED_NEG_8_16_32
#endif

HWY_NEON_DEF_FUNCTION_INT_8_16_32(SaturatedNeg, vqneg, _, 1)

#if HWY_ARCH_ARM_A64
#ifdef HWY_NATIVE_SATURATED_NEG_64
#undef HWY_NATIVE_SATURATED_NEG_64
#else
#define HWY_NATIVE_SATURATED_NEG_64
#endif

HWY_API Vec64<int64_t> SaturatedNeg(const Vec64<int64_t> v) {
  return Vec64<int64_t>(vqneg_s64(v.raw));
}

HWY_API Vec128<int64_t> SaturatedNeg(const Vec128<int64_t> v) {
  return Vec128<int64_t>(vqnegq_s64(v.raw));
}
#endif

// ------------------------------ ShiftLeft

#ifdef HWY_NATIVE_ROUNDING_SHR
#undef HWY_NATIVE_ROUNDING_SHR
#else
#define HWY_NATIVE_ROUNDING_SHR
#endif

// Customize HWY_NEON_DEF_FUNCTION to special-case count=0 (not supported).
#pragma push_macro("HWY_NEON_DEF_FUNCTION")
#undef HWY_NEON_DEF_FUNCTION
#define HWY_NEON_DEF_FUNCTION(type, size, name, prefix, infix, suffix, args)   \
  template <int kBits>                                                         \
  HWY_API Vec128<type##_t, size> name(const Vec128<type##_t, size> v) {        \
    return kBits == 0 ? v                                                      \
                      : Vec128<type##_t, size>(HWY_NEON_EVAL(                  \
                            prefix##infix##suffix, v.raw, HWY_MAX(1, kBits))); \
  }

HWY_NEON_DEF_FUNCTION_INTS_UINTS(ShiftLeft, vshl, _n_, ignored)

HWY_NEON_DEF_FUNCTION_UINTS(ShiftRight, vshr, _n_, ignored)
HWY_NEON_DEF_FUNCTION_INTS(ShiftRight, vshr, _n_, ignored)
HWY_NEON_DEF_FUNCTION_UINTS(RoundingShiftRight, vrshr, _n_, ignored)
HWY_NEON_DEF_FUNCTION_INTS(RoundingShiftRight, vrshr, _n_, ignored)

#pragma pop_macro("HWY_NEON_DEF_FUNCTION")

// ------------------------------ RotateRight (ShiftRight, Or)
template <int kBits, typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> RotateRight(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  constexpr size_t kSizeInBits = sizeof(T) * 8;
  static_assert(0 <= kBits && kBits < kSizeInBits, "Invalid shift count");
  if (kBits == 0) return v;

  return Or(BitCast(d, ShiftRight<kBits>(BitCast(du, v))),
            ShiftLeft<HWY_MIN(kSizeInBits - 1, kSizeInBits - kBits)>(v));
}

// NOTE: vxarq_u64 can be applied to uint64_t, but we do not yet have a
// mechanism for checking for extensions to Armv8.

// ------------------------------ Shl

HWY_API Vec128<uint8_t> operator<<(Vec128<uint8_t> v, Vec128<uint8_t> bits) {
  return Vec128<uint8_t>(vshlq_u8(v.raw, vreinterpretq_s8_u8(bits.raw)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8)>
HWY_API Vec128<uint8_t, N> operator<<(Vec128<uint8_t, N> v,
                                      Vec128<uint8_t, N> bits) {
  return Vec128<uint8_t, N>(vshl_u8(v.raw, vreinterpret_s8_u8(bits.raw)));
}

HWY_API Vec128<uint16_t> operator<<(Vec128<uint16_t> v, Vec128<uint16_t> bits) {
  return Vec128<uint16_t>(vshlq_u16(v.raw, vreinterpretq_s16_u16(bits.raw)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8)>
HWY_API Vec128<uint16_t, N> operator<<(Vec128<uint16_t, N> v,
                                       Vec128<uint16_t, N> bits) {
  return Vec128<uint16_t, N>(vshl_u16(v.raw, vreinterpret_s16_u16(bits.raw)));
}

HWY_API Vec128<uint32_t> operator<<(Vec128<uint32_t> v, Vec128<uint32_t> bits) {
  return Vec128<uint32_t>(vshlq_u32(v.raw, vreinterpretq_s32_u32(bits.raw)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8)>
HWY_API Vec128<uint32_t, N> operator<<(Vec128<uint32_t, N> v,
                                       Vec128<uint32_t, N> bits) {
  return Vec128<uint32_t, N>(vshl_u32(v.raw, vreinterpret_s32_u32(bits.raw)));
}

HWY_API Vec128<uint64_t> operator<<(Vec128<uint64_t> v, Vec128<uint64_t> bits) {
  return Vec128<uint64_t>(vshlq_u64(v.raw, vreinterpretq_s64_u64(bits.raw)));
}
HWY_API Vec64<uint64_t> operator<<(Vec64<uint64_t> v, Vec64<uint64_t> bits) {
  return Vec64<uint64_t>(vshl_u64(v.raw, vreinterpret_s64_u64(bits.raw)));
}

HWY_API Vec128<int8_t> operator<<(Vec128<int8_t> v, Vec128<int8_t> bits) {
  return Vec128<int8_t>(vshlq_s8(v.raw, bits.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8)>
HWY_API Vec128<int8_t, N> operator<<(Vec128<int8_t, N> v,
                                     Vec128<int8_t, N> bits) {
  return Vec128<int8_t, N>(vshl_s8(v.raw, bits.raw));
}

HWY_API Vec128<int16_t> operator<<(Vec128<int16_t> v, Vec128<int16_t> bits) {
  return Vec128<int16_t>(vshlq_s16(v.raw, bits.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> operator<<(Vec128<int16_t, N> v,
                                      Vec128<int16_t, N> bits) {
  return Vec128<int16_t, N>(vshl_s16(v.raw, bits.raw));
}

HWY_API Vec128<int32_t> operator<<(Vec128<int32_t> v, Vec128<int32_t> bits) {
  return Vec128<int32_t>(vshlq_s32(v.raw, bits.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8)>
HWY_API Vec128<int32_t, N> operator<<(Vec128<int32_t, N> v,
                                      Vec128<int32_t, N> bits) {
  return Vec128<int32_t, N>(vshl_s32(v.raw, bits.raw));
}

HWY_API Vec128<int64_t> operator<<(Vec128<int64_t> v, Vec128<int64_t> bits) {
  return Vec128<int64_t>(vshlq_s64(v.raw, bits.raw));
}
HWY_API Vec64<int64_t> operator<<(Vec64<int64_t> v, Vec64<int64_t> bits) {
  return Vec64<int64_t>(vshl_s64(v.raw, bits.raw));
}

// ------------------------------ Shr (Neg)

HWY_API Vec128<uint8_t> operator>>(Vec128<uint8_t> v, Vec128<uint8_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int8x16_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint8_t>(vshlq_u8(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8)>
HWY_API Vec128<uint8_t, N> operator>>(Vec128<uint8_t, N> v,
                                      Vec128<uint8_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int8x8_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint8_t, N>(vshl_u8(v.raw, neg_bits));
}

HWY_API Vec128<uint16_t> operator>>(Vec128<uint16_t> v, Vec128<uint16_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int16x8_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint16_t>(vshlq_u16(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8)>
HWY_API Vec128<uint16_t, N> operator>>(Vec128<uint16_t, N> v,
                                       Vec128<uint16_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int16x4_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint16_t, N>(vshl_u16(v.raw, neg_bits));
}

HWY_API Vec128<uint32_t> operator>>(Vec128<uint32_t> v, Vec128<uint32_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int32x4_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint32_t>(vshlq_u32(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8)>
HWY_API Vec128<uint32_t, N> operator>>(Vec128<uint32_t, N> v,
                                       Vec128<uint32_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int32x2_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint32_t, N>(vshl_u32(v.raw, neg_bits));
}

HWY_API Vec128<uint64_t> operator>>(Vec128<uint64_t> v, Vec128<uint64_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int64x2_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint64_t>(vshlq_u64(v.raw, neg_bits));
}
HWY_API Vec64<uint64_t> operator>>(Vec64<uint64_t> v, Vec64<uint64_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int64x1_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec64<uint64_t>(vshl_u64(v.raw, neg_bits));
}

HWY_API Vec128<int8_t> operator>>(Vec128<int8_t> v, Vec128<int8_t> bits) {
  return Vec128<int8_t>(vshlq_s8(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8)>
HWY_API Vec128<int8_t, N> operator>>(Vec128<int8_t, N> v,
                                     Vec128<int8_t, N> bits) {
  return Vec128<int8_t, N>(vshl_s8(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int16_t> operator>>(Vec128<int16_t> v, Vec128<int16_t> bits) {
  return Vec128<int16_t>(vshlq_s16(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> operator>>(Vec128<int16_t, N> v,
                                      Vec128<int16_t, N> bits) {
  return Vec128<int16_t, N>(vshl_s16(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int32_t> operator>>(Vec128<int32_t> v, Vec128<int32_t> bits) {
  return Vec128<int32_t>(vshlq_s32(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8)>
HWY_API Vec128<int32_t, N> operator>>(Vec128<int32_t, N> v,
                                      Vec128<int32_t, N> bits) {
  return Vec128<int32_t, N>(vshl_s32(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int64_t> operator>>(Vec128<int64_t> v, Vec128<int64_t> bits) {
  return Vec128<int64_t>(vshlq_s64(v.raw, Neg(bits).raw));
}
HWY_API Vec64<int64_t> operator>>(Vec64<int64_t> v, Vec64<int64_t> bits) {
  return Vec64<int64_t>(vshl_s64(v.raw, Neg(bits).raw));
}

// ------------------------------ RoundingShr (Neg)

HWY_API Vec128<uint8_t> RoundingShr(Vec128<uint8_t> v, Vec128<uint8_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int8x16_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint8_t>(vrshlq_u8(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8)>
HWY_API Vec128<uint8_t, N> RoundingShr(Vec128<uint8_t, N> v,
                                       Vec128<uint8_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int8x8_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint8_t, N>(vrshl_u8(v.raw, neg_bits));
}

HWY_API Vec128<uint16_t> RoundingShr(Vec128<uint16_t> v,
                                     Vec128<uint16_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int16x8_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint16_t>(vrshlq_u16(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8)>
HWY_API Vec128<uint16_t, N> RoundingShr(Vec128<uint16_t, N> v,
                                        Vec128<uint16_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int16x4_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint16_t, N>(vrshl_u16(v.raw, neg_bits));
}

HWY_API Vec128<uint32_t> RoundingShr(Vec128<uint32_t> v,
                                     Vec128<uint32_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int32x4_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint32_t>(vrshlq_u32(v.raw, neg_bits));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8)>
HWY_API Vec128<uint32_t, N> RoundingShr(Vec128<uint32_t, N> v,
                                        Vec128<uint32_t, N> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int32x2_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint32_t, N>(vrshl_u32(v.raw, neg_bits));
}

HWY_API Vec128<uint64_t> RoundingShr(Vec128<uint64_t> v,
                                     Vec128<uint64_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int64x2_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec128<uint64_t>(vrshlq_u64(v.raw, neg_bits));
}
HWY_API Vec64<uint64_t> RoundingShr(Vec64<uint64_t> v, Vec64<uint64_t> bits) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  const int64x1_t neg_bits = Neg(BitCast(di, bits)).raw;
  return Vec64<uint64_t>(vrshl_u64(v.raw, neg_bits));
}

HWY_API Vec128<int8_t> RoundingShr(Vec128<int8_t> v, Vec128<int8_t> bits) {
  return Vec128<int8_t>(vrshlq_s8(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8)>
HWY_API Vec128<int8_t, N> RoundingShr(Vec128<int8_t, N> v,
                                      Vec128<int8_t, N> bits) {
  return Vec128<int8_t, N>(vrshl_s8(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int16_t> RoundingShr(Vec128<int16_t> v, Vec128<int16_t> bits) {
  return Vec128<int16_t>(vrshlq_s16(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> RoundingShr(Vec128<int16_t, N> v,
                                       Vec128<int16_t, N> bits) {
  return Vec128<int16_t, N>(vrshl_s16(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int32_t> RoundingShr(Vec128<int32_t> v, Vec128<int32_t> bits) {
  return Vec128<int32_t>(vrshlq_s32(v.raw, Neg(bits).raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8)>
HWY_API Vec128<int32_t, N> RoundingShr(Vec128<int32_t, N> v,
                                       Vec128<int32_t, N> bits) {
  return Vec128<int32_t, N>(vrshl_s32(v.raw, Neg(bits).raw));
}

HWY_API Vec128<int64_t> RoundingShr(Vec128<int64_t> v, Vec128<int64_t> bits) {
  return Vec128<int64_t>(vrshlq_s64(v.raw, Neg(bits).raw));
}
HWY_API Vec64<int64_t> RoundingShr(Vec64<int64_t> v, Vec64<int64_t> bits) {
  return Vec64<int64_t>(vrshl_s64(v.raw, Neg(bits).raw));
}

// ------------------------------ ShiftLeftSame (Shl)

template <typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftSame(const Vec128<T, N> v, int bits) {
  return v << Set(DFromV<decltype(v)>(), static_cast<T>(bits));
}
template <typename T, size_t N>
HWY_API Vec128<T, N> ShiftRightSame(const Vec128<T, N> v, int bits) {
  return v >> Set(DFromV<decltype(v)>(), static_cast<T>(bits));
}

// ------------------------------ RoundingShiftRightSame (RoundingShr)

template <typename T, size_t N>
HWY_API Vec128<T, N> RoundingShiftRightSame(const Vec128<T, N> v, int bits) {
  return RoundingShr(v, Set(DFromV<decltype(v)>(), static_cast<T>(bits)));
}

// ------------------------------ Int/float multiplication

// Per-target flag to prevent generic_ops-inl.h from defining 8-bit operator*.
#ifdef HWY_NATIVE_MUL_8
#undef HWY_NATIVE_MUL_8
#else
#define HWY_NATIVE_MUL_8
#endif

// All except ui64
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(operator*, vmul, _, 2)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(operator*, vmul, _, 2)
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(operator*, vmul, _, 2)

// ------------------------------ Integer multiplication

// Returns the upper sizeof(T)*8 bits of a * b in each lane.
HWY_API Vec128<int8_t> MulHigh(Vec128<int8_t> a, Vec128<int8_t> b) {
  int16x8_t rlo = vmull_s8(vget_low_s8(a.raw), vget_low_s8(b.raw));
#if HWY_ARCH_ARM_A64
  int16x8_t rhi = vmull_high_s8(a.raw, b.raw);
#else
  int16x8_t rhi = vmull_s8(vget_high_s8(a.raw), vget_high_s8(b.raw));
#endif
  return Vec128<int8_t>(
      vuzp2q_s8(vreinterpretq_s8_s16(rlo), vreinterpretq_s8_s16(rhi)));
}
HWY_API Vec128<uint8_t> MulHigh(Vec128<uint8_t> a, Vec128<uint8_t> b) {
  uint16x8_t rlo = vmull_u8(vget_low_u8(a.raw), vget_low_u8(b.raw));
#if HWY_ARCH_ARM_A64
  uint16x8_t rhi = vmull_high_u8(a.raw, b.raw);
#else
  uint16x8_t rhi = vmull_u8(vget_high_u8(a.raw), vget_high_u8(b.raw));
#endif
  return Vec128<uint8_t>(
      vuzp2q_u8(vreinterpretq_u8_u16(rlo), vreinterpretq_u8_u16(rhi)));
}

template <size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8)>
HWY_API Vec128<int8_t, N> MulHigh(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
  int8x16_t hi_lo = vreinterpretq_s8_s16(vmull_s8(a.raw, b.raw));
  return Vec128<int8_t, N>(vget_low_s8(vuzp2q_s8(hi_lo, hi_lo)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8)>
HWY_API Vec128<uint8_t, N> MulHigh(Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  uint8x16_t hi_lo = vreinterpretq_u8_u16(vmull_u8(a.raw, b.raw));
  return Vec128<uint8_t, N>(vget_low_u8(vuzp2q_u8(hi_lo, hi_lo)));
}

HWY_API Vec128<int16_t> MulHigh(Vec128<int16_t> a, Vec128<int16_t> b) {
  int32x4_t rlo = vmull_s16(vget_low_s16(a.raw), vget_low_s16(b.raw));
#if HWY_ARCH_ARM_A64
  int32x4_t rhi = vmull_high_s16(a.raw, b.raw);
#else
  int32x4_t rhi = vmull_s16(vget_high_s16(a.raw), vget_high_s16(b.raw));
#endif
  return Vec128<int16_t>(
      vuzp2q_s16(vreinterpretq_s16_s32(rlo), vreinterpretq_s16_s32(rhi)));
}
HWY_API Vec128<uint16_t> MulHigh(Vec128<uint16_t> a, Vec128<uint16_t> b) {
  uint32x4_t rlo = vmull_u16(vget_low_u16(a.raw), vget_low_u16(b.raw));
#if HWY_ARCH_ARM_A64
  uint32x4_t rhi = vmull_high_u16(a.raw, b.raw);
#else
  uint32x4_t rhi = vmull_u16(vget_high_u16(a.raw), vget_high_u16(b.raw));
#endif
  return Vec128<uint16_t>(
      vuzp2q_u16(vreinterpretq_u16_u32(rlo), vreinterpretq_u16_u32(rhi)));
}

template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> MulHigh(Vec128<int16_t, N> a, Vec128<int16_t, N> b) {
  int16x8_t hi_lo = vreinterpretq_s16_s32(vmull_s16(a.raw, b.raw));
  return Vec128<int16_t, N>(vget_low_s16(vuzp2q_s16(hi_lo, hi_lo)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8)>
HWY_API Vec128<uint16_t, N> MulHigh(Vec128<uint16_t, N> a,
                                    Vec128<uint16_t, N> b) {
  uint16x8_t hi_lo = vreinterpretq_u16_u32(vmull_u16(a.raw, b.raw));
  return Vec128<uint16_t, N>(vget_low_u16(vuzp2q_u16(hi_lo, hi_lo)));
}

HWY_API Vec128<int32_t> MulHigh(Vec128<int32_t> a, Vec128<int32_t> b) {
  int64x2_t rlo = vmull_s32(vget_low_s32(a.raw), vget_low_s32(b.raw));
#if HWY_ARCH_ARM_A64
  int64x2_t rhi = vmull_high_s32(a.raw, b.raw);
#else
  int64x2_t rhi = vmull_s32(vget_high_s32(a.raw), vget_high_s32(b.raw));
#endif
  return Vec128<int32_t>(
      vuzp2q_s32(vreinterpretq_s32_s64(rlo), vreinterpretq_s32_s64(rhi)));
}
HWY_API Vec128<uint32_t> MulHigh(Vec128<uint32_t> a, Vec128<uint32_t> b) {
  uint64x2_t rlo = vmull_u32(vget_low_u32(a.raw), vget_low_u32(b.raw));
#if HWY_ARCH_ARM_A64
  uint64x2_t rhi = vmull_high_u32(a.raw, b.raw);
#else
  uint64x2_t rhi = vmull_u32(vget_high_u32(a.raw), vget_high_u32(b.raw));
#endif
  return Vec128<uint32_t>(
      vuzp2q_u32(vreinterpretq_u32_u64(rlo), vreinterpretq_u32_u64(rhi)));
}

template <size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8)>
HWY_API Vec128<int32_t, N> MulHigh(Vec128<int32_t, N> a, Vec128<int32_t, N> b) {
  int32x4_t hi_lo = vreinterpretq_s32_s64(vmull_s32(a.raw, b.raw));
  return Vec128<int32_t, N>(vget_low_s32(vuzp2q_s32(hi_lo, hi_lo)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8)>
HWY_API Vec128<uint32_t, N> MulHigh(Vec128<uint32_t, N> a,
                                    Vec128<uint32_t, N> b) {
  uint32x4_t hi_lo = vreinterpretq_u32_u64(vmull_u32(a.raw, b.raw));
  return Vec128<uint32_t, N>(vget_low_u32(vuzp2q_u32(hi_lo, hi_lo)));
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulHigh(Vec128<T> a, Vec128<T> b) {
  T hi_0;
  T hi_1;

  Mul128(GetLane(a), GetLane(b), &hi_0);
  Mul128(detail::GetLane<1>(a), detail::GetLane<1>(b), &hi_1);

  return Dup128VecFromValues(Full128<T>(), hi_0, hi_1);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec64<T> MulHigh(Vec64<T> a, Vec64<T> b) {
  T hi;
  Mul128(GetLane(a), GetLane(b), &hi);
  return Set(Full64<T>(), hi);
}

HWY_API Vec128<int16_t> MulFixedPoint15(Vec128<int16_t> a, Vec128<int16_t> b) {
  return Vec128<int16_t>(vqrdmulhq_s16(a.raw, b.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> MulFixedPoint15(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>(vqrdmulh_s16(a.raw, b.raw));
}

// ------------------------------ Floating-point division

// Emulate missing intrinsic
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
HWY_INLINE float64x1_t vrecpe_f64(float64x1_t raw) {
  const CappedTag<double, 1> d;
  const Twice<decltype(d)> dt;
  using VT = VFromD<decltype(dt)>;
  return LowerHalf(d, VT(vrecpeq_f64(Combine(dt, v, v).raw))).raw;
}
#endif

// Approximate reciprocal
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(ApproximateReciprocal, vrecpe, _, 1)

#if HWY_HAVE_FLOAT64
#ifdef HWY_NATIVE_F64_APPROX_RECIP
#undef HWY_NATIVE_F64_APPROX_RECIP
#else
#define HWY_NATIVE_F64_APPROX_RECIP
#endif

HWY_NEON_DEF_FUNCTION_ALL_FLOATS(operator/, vdiv, _, 2)
#else   // !HWY_HAVE_FLOAT64
namespace detail {
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(ReciprocalNewtonRaphsonStep, vrecps, _, 2)
}  // namespace detail

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> operator/(Vec128<T, N> a, Vec128<T, N> b) {
  auto x = ApproximateReciprocal(b);
  x *= detail::ReciprocalNewtonRaphsonStep(x, b);
  x *= detail::ReciprocalNewtonRaphsonStep(x, b);
  x *= detail::ReciprocalNewtonRaphsonStep(x, b);
  return a * x;
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ Absolute value of difference.

HWY_NEON_DEF_FUNCTION_ALL_FLOATS(AbsDiff, vabd, _, 2)
HWY_NEON_DEF_FUNCTION_UI_8_16_32(AbsDiff, vabd, _, 2)  // no UI64

#ifdef HWY_NATIVE_INTEGER_ABS_DIFF
#undef HWY_NATIVE_INTEGER_ABS_DIFF
#else
#define HWY_NATIVE_INTEGER_ABS_DIFF
#endif

// ------------------------------ Integer multiply-add

// Per-target flag to prevent generic_ops-inl.h from defining int MulAdd.
#ifdef HWY_NATIVE_INT_FMA
#undef HWY_NATIVE_INT_FMA
#else
#define HWY_NATIVE_INT_FMA
#endif

// Wrappers for changing argument order to what intrinsics expect.
namespace detail {
// All except ui64
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(MulAdd, vmla, _, 3)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(MulAdd, vmla, _, 3)
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(NegMulAdd, vmls, _, 3)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(NegMulAdd, vmls, _, 3)
}  // namespace detail

template <typename T, size_t N, HWY_IF_NOT_FLOAT(T), HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Vec128<T, N> MulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> add) {
  return detail::MulAdd(add, mul, x);
}

template <typename T, size_t N, HWY_IF_NOT_FLOAT(T), HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Vec128<T, N> NegMulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> add) {
  return detail::NegMulAdd(add, mul, x);
}

// 64-bit integer
template <typename T, size_t N, HWY_IF_NOT_FLOAT(T), HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> MulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> add) {
  return Add(Mul(mul, x), add);
}

template <typename T, size_t N, HWY_IF_NOT_FLOAT(T), HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> NegMulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> add) {
  return Sub(add, Mul(mul, x));
}

// ------------------------------ Floating-point multiply-add variants

namespace detail {

#if HWY_NATIVE_FMA
// Wrappers for changing argument order to what intrinsics expect.
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(MulAdd, vfma, _, 3)
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(NegMulAdd, vfms, _, 3)
#else
// Emulate. Matches intrinsics arg order.
template <size_t N>
HWY_API Vec128<float, N> MulAdd(Vec128<float, N> add, Vec128<float, N> mul,
                                Vec128<float, N> x) {
  return mul * x + add;
}

template <size_t N>
HWY_API Vec128<float, N> NegMulAdd(Vec128<float, N> add, Vec128<float, N> mul,
                                   Vec128<float, N> x) {
  return add - mul * x;
}

#endif  // HWY_NATIVE_FMA
}  // namespace detail

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> add) {
  return detail::MulAdd(add, mul, x);
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> add) {
  return detail::NegMulAdd(add, mul, x);
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulSub(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> sub) {
  return MulAdd(mul, x, Neg(sub));
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulSub(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> sub) {
  return Neg(MulAdd(mul, x, sub));
}

// ------------------------------ Floating-point square root (IfThenZeroElse)

// Emulate missing intrinsic
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 490
HWY_INLINE float64x1_t vrsqrte_f64(float64x1_t raw) {
  const CappedTag<double, 1> d;
  const Twice<decltype(d)> dt;
  using VT = VFromD<decltype(dt)>;
  const VFromD<decltype(d)> v(raw);
  return LowerHalf(d, VT(vrsqrteq_f64(Combine(dt, v, v).raw))).raw;
}
#endif

// Approximate reciprocal square root
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(ApproximateReciprocalSqrt, vrsqrte, _, 1)

#if HWY_HAVE_FLOAT64
#ifdef HWY_NATIVE_F64_APPROX_RSQRT
#undef HWY_NATIVE_F64_APPROX_RSQRT
#else
#define HWY_NATIVE_F64_APPROX_RSQRT
#endif

// Full precision square root
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Sqrt, vsqrt, _, 1)
#else   // !HWY_HAVE_FLOAT64
namespace detail {
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(ReciprocalSqrtStep, vrsqrts, _, 2)
}  // namespace detail

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Sqrt(const Vec128<T, N> v) {
  auto recip = ApproximateReciprocalSqrt(v);

  recip *= detail::ReciprocalSqrtStep(v * recip, recip);
  recip *= detail::ReciprocalSqrtStep(v * recip, recip);
  recip *= detail::ReciprocalSqrtStep(v * recip, recip);

  const auto root = v * recip;
  return IfThenZeroElse(v == Zero(Simd<T, N, 0>()), root);
}
#endif  // HWY_HAVE_FLOAT64

// ================================================== LOGICAL

// ------------------------------ Not

// There is no 64-bit vmvn, so cast instead of using HWY_NEON_DEF_FUNCTION.
template <typename T>
HWY_API Vec128<T> Not(const Vec128<T> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Vec128<uint8_t>(vmvnq_u8(BitCast(d8, v).raw)));
}
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<T, N> Not(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  using V8 = decltype(Zero(d8));
  return BitCast(d, V8(vmvn_u8(BitCast(d8, v).raw)));
}

// ------------------------------ And
HWY_NEON_DEF_FUNCTION_INTS_UINTS(And, vand, _, 2)

// Uses the u32/64 defined above.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> And(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, BitCast(du, a) & BitCast(du, b));
}

// ------------------------------ AndNot

namespace detail {
// reversed_andnot returns a & ~b.
HWY_NEON_DEF_FUNCTION_INTS_UINTS(reversed_andnot, vbic, _, 2)
}  // namespace detail

// Returns ~not_mask & mask.
template <typename T, size_t N, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec128<T, N> AndNot(const Vec128<T, N> not_mask,
                            const Vec128<T, N> mask) {
  return detail::reversed_andnot(mask, not_mask);
}

// Uses the u32/64 defined above.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> AndNot(const Vec128<T, N> not_mask,
                            const Vec128<T, N> mask) {
  const DFromV<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;
  VFromD<decltype(du)> ret =
      detail::reversed_andnot(BitCast(du, mask), BitCast(du, not_mask));
  return BitCast(d, ret);
}

// ------------------------------ Or

HWY_NEON_DEF_FUNCTION_INTS_UINTS(Or, vorr, _, 2)

// Uses the u32/64 defined above.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Or(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, BitCast(du, a) | BitCast(du, b));
}

// ------------------------------ Xor

HWY_NEON_DEF_FUNCTION_INTS_UINTS(Xor, veor, _, 2)

// Uses the u32/64 defined above.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Xor(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, BitCast(du, a) ^ BitCast(du, b));
}

// ------------------------------ Xor3
#if HWY_ARCH_ARM_A64 && defined(__ARM_FEATURE_SHA3)
HWY_NEON_DEF_FUNCTION_FULL_UI(Xor3, veor3, _, 3)

// Half vectors are not natively supported. Two Xor are likely more efficient
// than Combine to 128-bit.
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8), HWY_IF_NOT_FLOAT(T)>
HWY_API Vec128<T, N> Xor3(Vec128<T, N> x1, Vec128<T, N> x2, Vec128<T, N> x3) {
  return Xor(x1, Xor(x2, x3));
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Xor3(const Vec128<T, N> x1, const Vec128<T, N> x2,
                          const Vec128<T, N> x3) {
  const DFromV<decltype(x1)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Xor3(BitCast(du, x1), BitCast(du, x2), BitCast(du, x3)));
}

#else
template <typename T, size_t N>
HWY_API Vec128<T, N> Xor3(Vec128<T, N> x1, Vec128<T, N> x2, Vec128<T, N> x3) {
  return Xor(x1, Xor(x2, x3));
}
#endif

// ------------------------------ Or3
template <typename T, size_t N>
HWY_API Vec128<T, N> Or3(Vec128<T, N> o1, Vec128<T, N> o2, Vec128<T, N> o3) {
  return Or(o1, Or(o2, o3));
}

// ------------------------------ OrAnd
template <typename T, size_t N>
HWY_API Vec128<T, N> OrAnd(Vec128<T, N> o, Vec128<T, N> a1, Vec128<T, N> a2) {
  return Or(o, And(a1, a2));
}

// ------------------------------ IfVecThenElse
template <typename T, size_t N>
HWY_API Vec128<T, N> IfVecThenElse(Vec128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  return IfThenElse(MaskFromVec(mask), yes, no);
}

// ------------------------------ BitwiseIfThenElse

#ifdef HWY_NATIVE_BITWISE_IF_THEN_ELSE
#undef HWY_NATIVE_BITWISE_IF_THEN_ELSE
#else
#define HWY_NATIVE_BITWISE_IF_THEN_ELSE
#endif

template <class V>
HWY_API V BitwiseIfThenElse(V mask, V yes, V no) {
  return IfVecThenElse(mask, yes, no);
}

// ------------------------------ Operator overloads (internal-only if float)

template <typename T, size_t N>
HWY_API Vec128<T, N> operator&(const Vec128<T, N> a, const Vec128<T, N> b) {
  return And(a, b);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> operator|(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Or(a, b);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> operator^(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Xor(a, b);
}

// ------------------------------ I64/U64 AbsDiff

template <size_t N>
HWY_API Vec128<int64_t, N> AbsDiff(const Vec128<int64_t, N> a,
                                   const Vec128<int64_t, N> b) {
  return Max(a, b) - Min(a, b);
}

template <size_t N>
HWY_API Vec128<uint64_t, N> AbsDiff(const Vec128<uint64_t, N> a,
                                    const Vec128<uint64_t, N> b) {
  return Or(SaturatedSub(a, b), SaturatedSub(b, a));
}

// ------------------------------ PopulationCount

#ifdef HWY_NATIVE_POPCNT
#undef HWY_NATIVE_POPCNT
#else
#define HWY_NATIVE_POPCNT
#endif

namespace detail {

template <typename T>
HWY_INLINE Vec128<T> PopulationCount(hwy::SizeTag<1> /* tag */, Vec128<T> v) {
  const Full128<uint8_t> d8;
  return Vec128<T>(vcntq_u8(BitCast(d8, v).raw));
}
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<1> /* tag */,
                                        Vec128<T, N> v) {
  const Simd<uint8_t, N, 0> d8;
  return Vec128<T, N>(vcnt_u8(BitCast(d8, v).raw));
}

// NEON lacks popcount for lane sizes > 1, so take pairwise sums of the bytes.
template <typename T>
HWY_INLINE Vec128<T> PopulationCount(hwy::SizeTag<2> /* tag */, Vec128<T> v) {
  const Full128<uint8_t> d8;
  const uint8x16_t bytes = vcntq_u8(BitCast(d8, v).raw);
  return Vec128<T>(vpaddlq_u8(bytes));
}
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<2> /* tag */,
                                        Vec128<T, N> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d8;
  const uint8x8_t bytes = vcnt_u8(BitCast(d8, v).raw);
  return Vec128<T, N>(vpaddl_u8(bytes));
}

template <typename T>
HWY_INLINE Vec128<T> PopulationCount(hwy::SizeTag<4> /* tag */, Vec128<T> v) {
  const Full128<uint8_t> d8;
  const uint8x16_t bytes = vcntq_u8(BitCast(d8, v).raw);
  return Vec128<T>(vpaddlq_u16(vpaddlq_u8(bytes)));
}
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<4> /* tag */,
                                        Vec128<T, N> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d8;
  const uint8x8_t bytes = vcnt_u8(BitCast(d8, v).raw);
  return Vec128<T, N>(vpaddl_u16(vpaddl_u8(bytes)));
}

template <typename T>
HWY_INLINE Vec128<T> PopulationCount(hwy::SizeTag<8> /* tag */, Vec128<T> v) {
  const Full128<uint8_t> d8;
  const uint8x16_t bytes = vcntq_u8(BitCast(d8, v).raw);
  return Vec128<T>(vpaddlq_u32(vpaddlq_u16(vpaddlq_u8(bytes))));
}
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<8> /* tag */,
                                        Vec128<T, N> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d8;
  const uint8x8_t bytes = vcnt_u8(BitCast(d8, v).raw);
  return Vec128<T, N>(vpaddl_u32(vpaddl_u16(vpaddl_u8(bytes))));
}

}  // namespace detail

template <typename T, size_t N, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec128<T, N> PopulationCount(Vec128<T, N> v) {
  return detail::PopulationCount(hwy::SizeTag<sizeof(T)>(), v);
}

// ================================================== SIGN

// ------------------------------ Abs
// i64 is implemented after BroadcastSignBit.
HWY_NEON_DEF_FUNCTION_INT_8_16_32(Abs, vabs, _, 1)
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Abs, vabs, _, 1)

// ------------------------------ SaturatedAbs
#ifdef HWY_NATIVE_SATURATED_ABS
#undef HWY_NATIVE_SATURATED_ABS
#else
#define HWY_NATIVE_SATURATED_ABS
#endif

HWY_NEON_DEF_FUNCTION_INT_8_16_32(SaturatedAbs, vqabs, _, 1)

// ------------------------------ CopySign
template <typename T, size_t N>
HWY_API Vec128<T, N> CopySign(Vec128<T, N> magn, Vec128<T, N> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const DFromV<decltype(magn)> d;
  return BitwiseIfThenElse(SignBit(d), sign, magn);
}

// ------------------------------ CopySignToAbs
template <typename T, size_t N>
HWY_API Vec128<T, N> CopySignToAbs(Vec128<T, N> abs, Vec128<T, N> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const DFromV<decltype(abs)> d;
  return OrAnd(abs, SignBit(d), sign);
}

// ------------------------------ BroadcastSignBit

template <typename T, size_t N, HWY_IF_SIGNED(T)>
HWY_API Vec128<T, N> BroadcastSignBit(const Vec128<T, N> v) {
  return ShiftRight<sizeof(T) * 8 - 1>(v);
}

// ================================================== MASK

// ------------------------------ To/from vector

// Mask and Vec have the same representation (true = FF..FF).
template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(const Vec128<T, N> v) {
  const Simd<MakeUnsigned<T>, N, 0> du;
  return Mask128<T, N>(BitCast(du, v).raw);
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <class D>
HWY_API VFromD<D> VecFromMask(D d, const MFromD<D> m) {
  // Raw type of masks is unsigned.
  const RebindToUnsigned<D> du;
  return BitCast(d, VFromD<decltype(du)>(m.raw));
}

// ------------------------------ RebindMask (MaskFromVec)

template <typename TFrom, size_t NFrom, class DTo>
HWY_API MFromD<DTo> RebindMask(DTo /* tag */, Mask128<TFrom, NFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TFromD<DTo>), "Must have same size");
  return MFromD<DTo>(m.raw);
}

// ------------------------------ IfThenElse

#define HWY_NEON_BUILD_TPL_HWY_IF
#define HWY_NEON_BUILD_RET_HWY_IF(type, size) Vec128<type##_t, size>
#define HWY_NEON_BUILD_PARAM_HWY_IF(type, size)                         \
  const Mask128<type##_t, size> mask, const Vec128<type##_t, size> yes, \
      const Vec128<type##_t, size> no
#define HWY_NEON_BUILD_ARG_HWY_IF mask.raw, yes.raw, no.raw

HWY_NEON_DEF_FUNCTION_ALL_TYPES(IfThenElse, vbsl, _, HWY_IF)

#if HWY_HAVE_FLOAT16
#define HWY_NEON_IF_EMULATED_IF_THEN_ELSE(V) HWY_IF_BF16(TFromV<V>)
#else
#define HWY_NEON_IF_EMULATED_IF_THEN_ELSE(V) HWY_IF_SPECIAL_FLOAT_V(V)
#endif

template <class V, HWY_NEON_IF_EMULATED_IF_THEN_ELSE(V)>
HWY_API V IfThenElse(MFromD<DFromV<V>> mask, V yes, V no) {
  const DFromV<decltype(yes)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, IfThenElse(RebindMask(du, mask), BitCast(du, yes), BitCast(du, no)));
}

#undef HWY_NEON_IF_EMULATED_IF_THEN_ELSE
#undef HWY_NEON_BUILD_TPL_HWY_IF
#undef HWY_NEON_BUILD_RET_HWY_IF
#undef HWY_NEON_BUILD_PARAM_HWY_IF
#undef HWY_NEON_BUILD_ARG_HWY_IF

// mask ? yes : 0
template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> IfThenElseZero(Mask128<T, N> mask, Vec128<T, N> yes) {
  return yes & VecFromMask(DFromV<decltype(yes)>(), mask);
}
template <typename T, size_t N, HWY_IF_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> IfThenElseZero(Mask128<T, N> mask, Vec128<T, N> yes) {
  const DFromV<decltype(yes)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, IfThenElseZero(RebindMask(du, mask), BitCast(du, yes)));
}

// mask ? 0 : no
template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> IfThenZeroElse(Mask128<T, N> mask, Vec128<T, N> no) {
  return AndNot(VecFromMask(DFromV<decltype(no)>(), mask), no);
}
template <typename T, size_t N, HWY_IF_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> IfThenZeroElse(Mask128<T, N> mask, Vec128<T, N> no) {
  const DFromV<decltype(no)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, IfThenZeroElse(RebindMask(du, mask), BitCast(du, no)));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  static_assert(IsSigned<T>(), "Only works for signed/float");
  const DFromV<decltype(no)> d;
  const RebindToSigned<decltype(d)> di;

  Mask128<T, N> m = MaskFromVec(BitCast(d, BroadcastSignBit(BitCast(di, v))));
  return IfThenElse(m, yes, no);
}

// ------------------------------ Mask logical

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(const Mask128<T, N> m) {
  return MaskFromVec(Not(VecFromMask(DFromM<decltype(m)>(), m)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> And(const Mask128<T, N> a, Mask128<T, N> b) {
  const DFromM<decltype(a)> d;
  return MaskFromVec(And(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> AndNot(const Mask128<T, N> a, Mask128<T, N> b) {
  const DFromM<decltype(a)> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Or(const Mask128<T, N> a, Mask128<T, N> b) {
  const DFromM<decltype(a)> d;
  return MaskFromVec(Or(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Xor(const Mask128<T, N> a, Mask128<T, N> b) {
  const DFromM<decltype(a)> d;
  return MaskFromVec(Xor(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> ExclusiveNeither(const Mask128<T, N> a, Mask128<T, N> b) {
  const DFromM<decltype(a)> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), Not(VecFromMask(d, b))));
}

// ================================================== COMPARE

// Comparisons fill a lane with 1-bits if the condition is true, else 0.

// ------------------------------ Shuffle2301 (for i64 compares)

// Swap 32-bit halves in 64-bits
HWY_API Vec64<uint32_t> Shuffle2301(const Vec64<uint32_t> v) {
  return Vec64<uint32_t>(vrev64_u32(v.raw));
}
HWY_API Vec64<int32_t> Shuffle2301(const Vec64<int32_t> v) {
  return Vec64<int32_t>(vrev64_s32(v.raw));
}
HWY_API Vec64<float> Shuffle2301(const Vec64<float> v) {
  return Vec64<float>(vrev64_f32(v.raw));
}
HWY_API Vec128<uint32_t> Shuffle2301(const Vec128<uint32_t> v) {
  return Vec128<uint32_t>(vrev64q_u32(v.raw));
}
HWY_API Vec128<int32_t> Shuffle2301(const Vec128<int32_t> v) {
  return Vec128<int32_t>(vrev64q_s32(v.raw));
}
HWY_API Vec128<float> Shuffle2301(const Vec128<float> v) {
  return Vec128<float>(vrev64q_f32(v.raw));
}

#define HWY_NEON_BUILD_TPL_HWY_COMPARE
#define HWY_NEON_BUILD_RET_HWY_COMPARE(type, size) Mask128<type##_t, size>
#define HWY_NEON_BUILD_PARAM_HWY_COMPARE(type, size) \
  const Vec128<type##_t, size> a, const Vec128<type##_t, size> b
#define HWY_NEON_BUILD_ARG_HWY_COMPARE a.raw, b.raw

// ------------------------------ Equality
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(operator==, vceq, _, HWY_COMPARE)
#if HWY_ARCH_ARM_A64
HWY_NEON_DEF_FUNCTION_INTS_UINTS(operator==, vceq, _, HWY_COMPARE)
#else
// No 64-bit comparisons on armv7: emulate them below, after Shuffle2301.
HWY_NEON_DEF_FUNCTION_INT_8_16_32(operator==, vceq, _, HWY_COMPARE)
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(operator==, vceq, _, HWY_COMPARE)
#endif

// ------------------------------ Strict inequality (signed, float)
#if HWY_ARCH_ARM_A64
HWY_NEON_DEF_FUNCTION_INTS_UINTS(operator<, vclt, _, HWY_COMPARE)
#else
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(operator<, vclt, _, HWY_COMPARE)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(operator<, vclt, _, HWY_COMPARE)
#endif
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(operator<, vclt, _, HWY_COMPARE)

// ------------------------------ Weak inequality (float)
#if HWY_ARCH_ARM_A64
HWY_NEON_DEF_FUNCTION_INTS_UINTS(operator<=, vcle, _, HWY_COMPARE)
#else
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(operator<=, vcle, _, HWY_COMPARE)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(operator<=, vcle, _, HWY_COMPARE)
#endif
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(operator<=, vcle, _, HWY_COMPARE)

#undef HWY_NEON_BUILD_TPL_HWY_COMPARE
#undef HWY_NEON_BUILD_RET_HWY_COMPARE
#undef HWY_NEON_BUILD_PARAM_HWY_COMPARE
#undef HWY_NEON_BUILD_ARG_HWY_COMPARE

// ------------------------------ Armv7 i64 compare (Shuffle2301, Eq)

#if HWY_ARCH_ARM_V7

template <size_t N>
HWY_API Mask128<int64_t, N> operator==(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  const Simd<int32_t, N * 2, 0> d32;
  const Simd<int64_t, N, 0> d64;
  const auto cmp32 = VecFromMask(d32, Eq(BitCast(d32, a), BitCast(d32, b)));
  const auto cmp64 = cmp32 & Shuffle2301(cmp32);
  return MaskFromVec(BitCast(d64, cmp64));
}

template <size_t N>
HWY_API Mask128<uint64_t, N> operator==(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
  const Simd<uint32_t, N * 2, 0> d32;
  const Simd<uint64_t, N, 0> d64;
  const auto cmp32 = VecFromMask(d32, Eq(BitCast(d32, a), BitCast(d32, b)));
  const auto cmp64 = cmp32 & Shuffle2301(cmp32);
  return MaskFromVec(BitCast(d64, cmp64));
}

HWY_API Mask128<int64_t> operator<(const Vec128<int64_t> a,
                                   const Vec128<int64_t> b) {
  const int64x2_t sub = vqsubq_s64(a.raw, b.raw);
  return MaskFromVec(BroadcastSignBit(Vec128<int64_t>(sub)));
}
HWY_API Mask128<int64_t, 1> operator<(const Vec64<int64_t> a,
                                      const Vec64<int64_t> b) {
  const int64x1_t sub = vqsub_s64(a.raw, b.raw);
  return MaskFromVec(BroadcastSignBit(Vec64<int64_t>(sub)));
}

template <size_t N>
HWY_API Mask128<uint64_t, N> operator<(const Vec128<uint64_t, N> a,
                                       const Vec128<uint64_t, N> b) {
  const DFromV<decltype(a)> du;
  const RebindToSigned<decltype(du)> di;
  const Vec128<uint64_t, N> msb = AndNot(a, b) | AndNot(a ^ b, a - b);
  return MaskFromVec(BitCast(du, BroadcastSignBit(BitCast(di, msb))));
}

template <size_t N>
HWY_API Mask128<int64_t, N> operator<=(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  return Not(b < a);
}

template <size_t N>
HWY_API Mask128<uint64_t, N> operator<=(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
  return Not(b < a);
}

#endif

// ------------------------------ operator!= (operator==)

// Customize HWY_NEON_DEF_FUNCTION to call 2 functions.
#pragma push_macro("HWY_NEON_DEF_FUNCTION")
#undef HWY_NEON_DEF_FUNCTION
// This cannot have _any_ template argument (in x86_128 we can at least have N
// as an argument), otherwise it is not more specialized than rewritten
// operator== in C++20, leading to compile errors.
#define HWY_NEON_DEF_FUNCTION(type, size, name, prefix, infix, suffix, args) \
  HWY_API Mask128<type##_t, size> name(Vec128<type##_t, size> a,             \
                                       Vec128<type##_t, size> b) {           \
    return Not(a == b);                                                      \
  }

HWY_NEON_DEF_FUNCTION_ALL_TYPES(operator!=, ignored, ignored, ignored)

#pragma pop_macro("HWY_NEON_DEF_FUNCTION")

// ------------------------------ Reversed comparisons

template <typename T, size_t N>
HWY_API Mask128<T, N> operator>(Vec128<T, N> a, Vec128<T, N> b) {
  return operator<(b, a);
}
template <typename T, size_t N>
HWY_API Mask128<T, N> operator>=(Vec128<T, N> a, Vec128<T, N> b) {
  return operator<=(b, a);
}

// ------------------------------ FirstN (Iota, Lt)

template <class D>
HWY_API MFromD<D> FirstN(D d, size_t num) {
  const RebindToSigned<decltype(d)> di;  // Signed comparisons are cheaper.
  using TI = TFromD<decltype(di)>;
  return RebindMask(d, detail::Iota0(di) < Set(di, static_cast<TI>(num)));
}

// ------------------------------ TestBit (Eq)

#define HWY_NEON_BUILD_TPL_HWY_TESTBIT
#define HWY_NEON_BUILD_RET_HWY_TESTBIT(type, size) Mask128<type##_t, size>
#define HWY_NEON_BUILD_PARAM_HWY_TESTBIT(type, size) \
  Vec128<type##_t, size> v, Vec128<type##_t, size> bit
#define HWY_NEON_BUILD_ARG_HWY_TESTBIT v.raw, bit.raw

#if HWY_ARCH_ARM_A64
HWY_NEON_DEF_FUNCTION_INTS_UINTS(TestBit, vtst, _, HWY_TESTBIT)
#else
// No 64-bit versions on armv7
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(TestBit, vtst, _, HWY_TESTBIT)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(TestBit, vtst, _, HWY_TESTBIT)

template <size_t N>
HWY_API Mask128<uint64_t, N> TestBit(Vec128<uint64_t, N> v,
                                     Vec128<uint64_t, N> bit) {
  return (v & bit) == bit;
}
template <size_t N>
HWY_API Mask128<int64_t, N> TestBit(Vec128<int64_t, N> v,
                                    Vec128<int64_t, N> bit) {
  return (v & bit) == bit;
}

#endif
#undef HWY_NEON_BUILD_TPL_HWY_TESTBIT
#undef HWY_NEON_BUILD_RET_HWY_TESTBIT
#undef HWY_NEON_BUILD_PARAM_HWY_TESTBIT
#undef HWY_NEON_BUILD_ARG_HWY_TESTBIT

// ------------------------------ Abs i64 (IfThenElse, BroadcastSignBit)
HWY_API Vec128<int64_t> Abs(const Vec128<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec128<int64_t>(vabsq_s64(v.raw));
#else
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfThenElse(MaskFromVec(BroadcastSignBit(v)), zero - v, v);
#endif
}
HWY_API Vec64<int64_t> Abs(const Vec64<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec64<int64_t>(vabs_s64(v.raw));
#else
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfThenElse(MaskFromVec(BroadcastSignBit(v)), zero - v, v);
#endif
}

HWY_API Vec128<int64_t> SaturatedAbs(const Vec128<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec128<int64_t>(vqabsq_s64(v.raw));
#else
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfThenElse(MaskFromVec(BroadcastSignBit(v)), SaturatedSub(zero, v), v);
#endif
}
HWY_API Vec64<int64_t> SaturatedAbs(const Vec64<int64_t> v) {
#if HWY_ARCH_ARM_A64
  return Vec64<int64_t>(vqabs_s64(v.raw));
#else
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfThenElse(MaskFromVec(BroadcastSignBit(v)), SaturatedSub(zero, v), v);
#endif
}

// ------------------------------ Min (IfThenElse, BroadcastSignBit)

// Unsigned
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(Min, vmin, _, 2)

template <size_t N>
HWY_API Vec128<uint64_t, N> Min(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
#if HWY_ARCH_ARM_A64
  return IfThenElse(b < a, b, a);
#else
  const DFromV<decltype(a)> du;
  const RebindToSigned<decltype(du)> di;
  return BitCast(du, BitCast(di, a) - BitCast(di, SaturatedSub(a, b)));
#endif
}

// Signed
HWY_NEON_DEF_FUNCTION_INT_8_16_32(Min, vmin, _, 2)

template <size_t N>
HWY_API Vec128<int64_t, N> Min(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
#if HWY_ARCH_ARM_A64
  return IfThenElse(b < a, b, a);
#else
  const Vec128<int64_t, N> sign = SaturatedSub(a, b);
  return IfThenElse(MaskFromVec(BroadcastSignBit(sign)), a, b);
#endif
}

// Float: IEEE minimumNumber on v8
#if HWY_ARCH_ARM_A64

HWY_NEON_DEF_FUNCTION_FLOAT_16_32(Min, vminnm, _, 2)

// GCC 6.5 and earlier are missing the 64-bit (non-q) intrinsic, so define
// in terms of the 128-bit intrinsic.
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
namespace detail {

template <class V, HWY_IF_V_SIZE_V(V, 8), HWY_IF_T_SIZE_V(V, 8)>
HWY_INLINE V F64Vec64Min(V a, V b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return LowerHalf(d, Min(ZeroExtendVector(dt, a), ZeroExtendVector(dt, b)));
}

}  // namespace detail
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700

HWY_API Vec64<double> Min(Vec64<double> a, Vec64<double> b) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
  return detail::F64Vec64Min(a, b);
#else
  return Vec64<double>(vminnm_f64(a.raw, b.raw));
#endif
}

HWY_API Vec128<double> Min(Vec128<double> a, Vec128<double> b) {
  return Vec128<double>(vminnmq_f64(a.raw, b.raw));
}

#else
// Armv7: NaN if any is NaN.
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Min, vmin, _, 2)
#endif  // HWY_ARCH_ARM_A64

// ------------------------------ Max (IfThenElse, BroadcastSignBit)

// Unsigned (no u64)
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(Max, vmax, _, 2)

template <size_t N>
HWY_API Vec128<uint64_t, N> Max(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
#if HWY_ARCH_ARM_A64
  return IfThenElse(b < a, a, b);
#else
  const DFromV<decltype(a)> du;
  const RebindToSigned<decltype(du)> di;
  return BitCast(du, BitCast(di, b) + BitCast(di, SaturatedSub(a, b)));
#endif
}

// Signed (no i64)
HWY_NEON_DEF_FUNCTION_INT_8_16_32(Max, vmax, _, 2)

template <size_t N>
HWY_API Vec128<int64_t, N> Max(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
#if HWY_ARCH_ARM_A64
  return IfThenElse(b < a, a, b);
#else
  const Vec128<int64_t, N> sign = SaturatedSub(a, b);
  return IfThenElse(MaskFromVec(BroadcastSignBit(sign)), b, a);
#endif
}

// Float: IEEE minimumNumber on v8
#if HWY_ARCH_ARM_A64

HWY_NEON_DEF_FUNCTION_FLOAT_16_32(Max, vmaxnm, _, 2)

// GCC 6.5 and earlier are missing the 64-bit (non-q) intrinsic, so define
// in terms of the 128-bit intrinsic.
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
namespace detail {

template <class V, HWY_IF_V_SIZE_V(V, 8), HWY_IF_T_SIZE_V(V, 8)>
HWY_INLINE V F64Vec64Max(V a, V b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return LowerHalf(d, Max(ZeroExtendVector(dt, a), ZeroExtendVector(dt, b)));
}

}  // namespace detail
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700

HWY_API Vec64<double> Max(Vec64<double> a, Vec64<double> b) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
  return detail::F64Vec64Max(a, b);
#else
  return Vec64<double>(vmaxnm_f64(a.raw, b.raw));
#endif
}

HWY_API Vec128<double> Max(Vec128<double> a, Vec128<double> b) {
  return Vec128<double>(vmaxnmq_f64(a.raw, b.raw));
}

#else
// Armv7: NaN if any is NaN.
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Max, vmax, _, 2)
#endif  // HWY_ARCH_ARM_A64

// ================================================== MEMORY

// ------------------------------ Load 128

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> LoadU(D /* tag */,
                              const uint8_t* HWY_RESTRICT unaligned) {
  return Vec128<uint8_t>(vld1q_u8(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> LoadU(D /* tag */,
                               const uint16_t* HWY_RESTRICT unaligned) {
  return Vec128<uint16_t>(vld1q_u16(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> LoadU(D /* tag */,
                               const uint32_t* HWY_RESTRICT unaligned) {
  return Vec128<uint32_t>(vld1q_u32(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API Vec128<uint64_t> LoadU(D /* tag */,
                               const uint64_t* HWY_RESTRICT unaligned) {
  return Vec128<uint64_t>(vld1q_u64(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I8_D(D)>
HWY_API Vec128<int8_t> LoadU(D /* tag */,
                             const int8_t* HWY_RESTRICT unaligned) {
  return Vec128<int8_t>(vld1q_s8(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> LoadU(D /* tag */,
                              const int16_t* HWY_RESTRICT unaligned) {
  return Vec128<int16_t>(vld1q_s16(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> LoadU(D /* tag */,
                              const int32_t* HWY_RESTRICT unaligned) {
  return Vec128<int32_t>(vld1q_s32(unaligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> LoadU(D /* tag */,
                              const int64_t* HWY_RESTRICT unaligned) {
  return Vec128<int64_t>(vld1q_s64(unaligned));
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> LoadU(D /* tag */,
                                const float16_t* HWY_RESTRICT unaligned) {
  return Vec128<float16_t>(vld1q_f16(detail::NativeLanePointer(unaligned)));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_BF16_D(D)>
HWY_API Vec128<bfloat16_t> LoadU(D /* tag */,
                                 const bfloat16_t* HWY_RESTRICT unaligned) {
  return Vec128<bfloat16_t>(vld1q_bf16(detail::NativeLanePointer(unaligned)));
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<float> LoadU(D /* tag */, const float* HWY_RESTRICT unaligned) {
  return Vec128<float>(vld1q_f32(unaligned));
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<double> LoadU(D /* tag */,
                             const double* HWY_RESTRICT unaligned) {
  return Vec128<double>(vld1q_f64(unaligned));
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ Load 64

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> LoadU(D /* tag */, const uint8_t* HWY_RESTRICT p) {
  return Vec64<uint8_t>(vld1_u8(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> LoadU(D /* tag */, const uint16_t* HWY_RESTRICT p) {
  return Vec64<uint16_t>(vld1_u16(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> LoadU(D /* tag */, const uint32_t* HWY_RESTRICT p) {
  return Vec64<uint32_t>(vld1_u32(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U64_D(D)>
HWY_API Vec64<uint64_t> LoadU(D /* tag */, const uint64_t* HWY_RESTRICT p) {
  return Vec64<uint64_t>(vld1_u64(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API Vec64<int8_t> LoadU(D /* tag */, const int8_t* HWY_RESTRICT p) {
  return Vec64<int8_t>(vld1_s8(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> LoadU(D /* tag */, const int16_t* HWY_RESTRICT p) {
  return Vec64<int16_t>(vld1_s16(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> LoadU(D /* tag */, const int32_t* HWY_RESTRICT p) {
  return Vec64<int32_t>(vld1_s32(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I64_D(D)>
HWY_API Vec64<int64_t> LoadU(D /* tag */, const int64_t* HWY_RESTRICT p) {
  return Vec64<int64_t>(vld1_s64(p));
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API Vec64<float16_t> LoadU(D /* tag */, const float16_t* HWY_RESTRICT p) {
  return Vec64<float16_t>(vld1_f16(detail::NativeLanePointer(p)));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_API Vec64<bfloat16_t> LoadU(D /* tag */, const bfloat16_t* HWY_RESTRICT p) {
  return Vec64<bfloat16_t>(vld1_bf16(detail::NativeLanePointer(p)));
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> LoadU(D /* tag */, const float* HWY_RESTRICT p) {
  return Vec64<float>(vld1_f32(p));
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F64_D(D)>
HWY_API Vec64<double> LoadU(D /* tag */, const double* HWY_RESTRICT p) {
  return Vec64<double>(vld1_f64(p));
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ Load 32

// Actual 32-bit broadcast load - used to implement the other lane types
// because reinterpret_cast of the pointer leads to incorrect codegen on GCC.
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> LoadU(D /*tag*/, const uint32_t* HWY_RESTRICT p) {
  return Vec32<uint32_t>(vld1_dup_u32(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I32_D(D)>
HWY_API Vec32<int32_t> LoadU(D /*tag*/, const int32_t* HWY_RESTRICT p) {
  return Vec32<int32_t>(vld1_dup_s32(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API Vec32<float> LoadU(D /*tag*/, const float* HWY_RESTRICT p) {
  return Vec32<float>(vld1_dup_f32(p));
}

// {u,i}{8,16}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_T_SIZE_LE_D(D, 2),
          HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf;
  CopyBytes<4>(p, &buf);
  return BitCast(d, LoadU(d32, &buf));
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F16_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf;
  CopyBytes<4>(p, &buf);
  return BitCast(d, LoadU(d32, &buf));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf;
  CopyBytes<4>(p, &buf);
  return BitCast(d, LoadU(d32, &buf));
}
#endif  // HWY_NEON_HAVE_BFLOAT16

// ------------------------------ Load 16

// Actual 16-bit broadcast load - used to implement the other lane types
// because reinterpret_cast of the pointer leads to incorrect codegen on GCC.
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_U16_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const uint16_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_u16(p));
}
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_I16_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const int16_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_s16(p));
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_F16_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const float16_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_f16(detail::NativeLanePointer(p)));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const bfloat16_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_bf16(detail::NativeLanePointer(p)));
}
#endif  // HWY_NEON_HAVE_BFLOAT16

// 8-bit x2
template <class D, HWY_IF_LANES_D(D, 2), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const Repartition<uint16_t, decltype(d)> d16;
  uint16_t buf;
  CopyBytes<2>(p, &buf);
  return BitCast(d, LoadU(d16, &buf));
}

// ------------------------------ Load 8
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_U8_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const uint8_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_u8(p));
}
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_I8_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const int8_t* HWY_RESTRICT p) {
  return VFromD<D>(vld1_dup_s8(p));
}

// ------------------------------ Load misc

template <class D, HWY_NEON_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, LoadU(du, detail::U16LanePointer(p)));
}

// On Arm, Load is the same as LoadU.
template <class D>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}

template <class D>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT aligned) {
  return IfThenElseZero(m, Load(d, aligned));
}

template <class D>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT aligned) {
  return IfThenElse(m, Load(d, aligned), v);
}

// 128-bit SIMD => nothing to duplicate, same as an unaligned load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}

// ------------------------------ Store 128

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API void StoreU(Vec128<uint8_t> v, D /* tag */,
                    uint8_t* HWY_RESTRICT unaligned) {
  vst1q_u8(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API void StoreU(Vec128<uint16_t> v, D /* tag */,
                    uint16_t* HWY_RESTRICT unaligned) {
  vst1q_u16(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API void StoreU(Vec128<uint32_t> v, D /* tag */,
                    uint32_t* HWY_RESTRICT unaligned) {
  vst1q_u32(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API void StoreU(Vec128<uint64_t> v, D /* tag */,
                    uint64_t* HWY_RESTRICT unaligned) {
  vst1q_u64(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I8_D(D)>
HWY_API void StoreU(Vec128<int8_t> v, D /* tag */,
                    int8_t* HWY_RESTRICT unaligned) {
  vst1q_s8(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API void StoreU(Vec128<int16_t> v, D /* tag */,
                    int16_t* HWY_RESTRICT unaligned) {
  vst1q_s16(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API void StoreU(Vec128<int32_t> v, D /* tag */,
                    int32_t* HWY_RESTRICT unaligned) {
  vst1q_s32(unaligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API void StoreU(Vec128<int64_t> v, D /* tag */,
                    int64_t* HWY_RESTRICT unaligned) {
  vst1q_s64(unaligned, v.raw);
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API void StoreU(Vec128<float16_t> v, D /* tag */,
                    float16_t* HWY_RESTRICT unaligned) {
  vst1q_f16(detail::NativeLanePointer(unaligned), v.raw);
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_BF16_D(D)>
HWY_API void StoreU(Vec128<bfloat16_t> v, D /* tag */,
                    bfloat16_t* HWY_RESTRICT unaligned) {
  vst1q_bf16(detail::NativeLanePointer(unaligned), v.raw);
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API void StoreU(Vec128<float> v, D /* tag */,
                    float* HWY_RESTRICT unaligned) {
  vst1q_f32(unaligned, v.raw);
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API void StoreU(Vec128<double> v, D /* tag */,
                    double* HWY_RESTRICT unaligned) {
  vst1q_f64(unaligned, v.raw);
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ Store 64

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API void StoreU(Vec64<uint8_t> v, D /* tag */, uint8_t* HWY_RESTRICT p) {
  vst1_u8(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API void StoreU(Vec64<uint16_t> v, D /* tag */, uint16_t* HWY_RESTRICT p) {
  vst1_u16(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API void StoreU(Vec64<uint32_t> v, D /* tag */, uint32_t* HWY_RESTRICT p) {
  vst1_u32(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U64_D(D)>
HWY_API void StoreU(Vec64<uint64_t> v, D /* tag */, uint64_t* HWY_RESTRICT p) {
  vst1_u64(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API void StoreU(Vec64<int8_t> v, D /* tag */, int8_t* HWY_RESTRICT p) {
  vst1_s8(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API void StoreU(Vec64<int16_t> v, D /* tag */, int16_t* HWY_RESTRICT p) {
  vst1_s16(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API void StoreU(Vec64<int32_t> v, D /* tag */, int32_t* HWY_RESTRICT p) {
  vst1_s32(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I64_D(D)>
HWY_API void StoreU(Vec64<int64_t> v, D /* tag */, int64_t* HWY_RESTRICT p) {
  vst1_s64(p, v.raw);
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API void StoreU(Vec64<float16_t> v, D /* tag */,
                    float16_t* HWY_RESTRICT p) {
  vst1_f16(detail::NativeLanePointer(p), v.raw);
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_API void StoreU(Vec64<bfloat16_t> v, D /* tag */,
                    bfloat16_t* HWY_RESTRICT p) {
  vst1_bf16(detail::NativeLanePointer(p), v.raw);
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API void StoreU(Vec64<float> v, D /* tag */, float* HWY_RESTRICT p) {
  vst1_f32(p, v.raw);
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F64_D(D)>
HWY_API void StoreU(Vec64<double> v, D /* tag */, double* HWY_RESTRICT p) {
  vst1_f64(p, v.raw);
}
#endif  // HWY_HAVE_FLOAT64

// ------------------------------ Store 32

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U32_D(D)>
HWY_API void StoreU(Vec32<uint32_t> v, D, uint32_t* HWY_RESTRICT p) {
  vst1_lane_u32(p, v.raw, 0);
}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I32_D(D)>
HWY_API void StoreU(Vec32<int32_t> v, D, int32_t* HWY_RESTRICT p) {
  vst1_lane_s32(p, v.raw, 0);
}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API void StoreU(Vec32<float> v, D, float* HWY_RESTRICT p) {
  vst1_lane_f32(p, v.raw, 0);
}

// {u,i}{8,16}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_T_SIZE_LE_D(D, 2),
          HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf = GetLane(BitCast(d32, v));
  CopyBytes<4>(&buf, p);
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F16_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf = GetLane(BitCast(d32, v));
  CopyBytes<4>(&buf, p);
}
#endif
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_BF16_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Repartition<uint32_t, decltype(d)> d32;
  uint32_t buf = GetLane(BitCast(d32, v));
  CopyBytes<4>(&buf, p);
}
#endif  // HWY_NEON_HAVE_BFLOAT16

// ------------------------------ Store 16

template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_U16_D(D)>
HWY_API void StoreU(Vec16<uint16_t> v, D, uint16_t* HWY_RESTRICT p) {
  vst1_lane_u16(p, v.raw, 0);
}
template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_I16_D(D)>
HWY_API void StoreU(Vec16<int16_t> v, D, int16_t* HWY_RESTRICT p) {
  vst1_lane_s16(p, v.raw, 0);
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_F16_D(D)>
HWY_API void StoreU(Vec16<float16_t> v, D, float16_t* HWY_RESTRICT p) {
  vst1_lane_f16(detail::NativeLanePointer(p), v.raw, 0);
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_BF16_D(D)>
HWY_API void StoreU(Vec16<bfloat16_t> v, D, bfloat16_t* HWY_RESTRICT p) {
  vst1_lane_bf16(detail::NativeLanePointer(p), v.raw, 0);
}
#endif  // HWY_NEON_HAVE_BFLOAT16

template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_T_SIZE_D(D, 1)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const Repartition<uint16_t, decltype(d)> d16;
  const uint16_t buf = GetLane(BitCast(d16, v));
  CopyBytes<2>(&buf, p);
}

// ------------------------------ Store 8

template <class D, HWY_IF_V_SIZE_D(D, 1), HWY_IF_U8_D(D)>
HWY_API void StoreU(Vec128<uint8_t, 1> v, D, uint8_t* HWY_RESTRICT p) {
  vst1_lane_u8(p, v.raw, 0);
}
template <class D, HWY_IF_V_SIZE_D(D, 1), HWY_IF_I8_D(D)>
HWY_API void StoreU(Vec128<int8_t, 1> v, D, int8_t* HWY_RESTRICT p) {
  vst1_lane_s8(p, v.raw, 0);
}

// ------------------------------ Store misc

template <class D, HWY_NEON_IF_EMULATED_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return StoreU(BitCast(du, v), du, detail::U16LanePointer(p));
}

HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_GCC_ACTUAL
HWY_DIAGNOSTICS_OFF(disable : 4701, ignored "-Wmaybe-uninitialized")
#endif

// On Arm, Store is the same as StoreU.
template <class D>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  StoreU(v, d, aligned);
}

HWY_DIAGNOSTICS(pop)

template <class D>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  // Treat as unsigned so that we correctly support float16.
  const RebindToUnsigned<decltype(d)> du;
  const auto blended =
      IfThenElse(RebindMask(du, m), BitCast(du, v), BitCast(du, LoadU(d, p)));
  StoreU(BitCast(d, blended), d, p);
}

// ------------------------------ Non-temporal stores

// Same as aligned stores on non-x86.

template <class D>
HWY_API void Stream(const VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
#if HWY_ARCH_ARM_A64
#if HWY_COMPILER_GCC
  __builtin_prefetch(aligned, 1, 0);
#elif HWY_COMPILER_MSVC
  __prefetch2(aligned, 0x11);
#endif
#endif
  Store(v, d, aligned);
}

// ================================================== CONVERT

// ------------------------------ ConvertTo

#if HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16

// TODO(janwas): use macro generator instead of handwritten
template <class D, HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> ConvertTo(D /* tag */, Vec128<int16_t> v) {
  return Vec128<float16_t>(vcvtq_f16_s16(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>(vcvt_f16_s16(v.raw));
}

template <class D, HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> ConvertTo(D /* tag */, Vec128<uint16_t> v) {
  return Vec128<float16_t>(vcvtq_f16_u16(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>(vcvt_f16_u16(v.raw));
}

#endif  // HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec128<float> ConvertTo(D /* tag */, Vec128<int32_t> v) {
  return Vec128<float>(vcvtq_f32_s32(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<RebindToSigned<D>> v) {
  return VFromD<D>(vcvt_f32_s32(v.raw));
}

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec128<float> ConvertTo(D /* tag */, Vec128<uint32_t> v) {
  return Vec128<float>(vcvtq_f32_u32(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<RebindToUnsigned<D>> v) {
  return VFromD<D>(vcvt_f32_u32(v.raw));
}

#if HWY_HAVE_FLOAT64

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> ConvertTo(D /* tag */, Vec128<int64_t> v) {
  return Vec128<double>(vcvtq_f64_s64(v.raw));
}
template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> ConvertTo(D /* tag */, Vec64<int64_t> v) {
// GCC 6.5 and earlier are missing the 64-bit (non-q) intrinsic.
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
  return Set(Full64<double>(), static_cast<double>(GetLane(v)));
#else
  return Vec64<double>(vcvt_f64_s64(v.raw));
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> ConvertTo(D /* tag */, Vec128<uint64_t> v) {
  return Vec128<double>(vcvtq_f64_u64(v.raw));
}
template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> ConvertTo(D /* tag */, Vec64<uint64_t> v) {
  // GCC 6.5 and earlier are missing the 64-bit (non-q) intrinsic.
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
  return Set(Full64<double>(), static_cast<double>(GetLane(v)));
#else
  return Vec64<double>(vcvt_f64_u64(v.raw));
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
}

#endif  // HWY_HAVE_FLOAT64

namespace detail {
// Truncates (rounds toward zero).
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_INLINE Vec128<int32_t> ConvertFToI(D /* tag */, Vec128<float> v) {
#if HWY_COMPILER_CLANG && \
    ((HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200) || HWY_ARCH_ARM_V7)
  // If compiling for AArch64 NEON with Clang 11 or earlier or if compiling for
  // Armv7 NEON, use inline assembly to avoid undefined behavior if v[i] is
  // outside of the range of an int32_t.

  int32x4_t raw_result;
  __asm__(
#if HWY_ARCH_ARM_A64
      "fcvtzs %0.4s, %1.4s"
#else
      "vcvt.s32.f32 %0, %1"
#endif
      : "=w"(raw_result)
      : "w"(v.raw));
  return Vec128<int32_t>(raw_result);
#else
  return Vec128<int32_t>(vcvtq_s32_f32(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_INLINE VFromD<D> ConvertFToI(D /* tag */, VFromD<RebindToFloat<D>> v) {
#if HWY_COMPILER_CLANG && \
    ((HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200) || HWY_ARCH_ARM_V7)
  // If compiling for AArch64 NEON with Clang 11 or earlier or if compiling for
  // Armv7 NEON, use inline assembly to avoid undefined behavior if v[i] is
  // outside of the range of an int32_t.

  int32x2_t raw_result;
  __asm__(
#if HWY_ARCH_ARM_A64
      "fcvtzs %0.2s, %1.2s"
#else
      "vcvt.s32.f32 %0, %1"
#endif
      : "=w"(raw_result)
      : "w"(v.raw));
  return VFromD<D>(raw_result);
#else
  return VFromD<D>(vcvt_s32_f32(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_INLINE Vec128<uint32_t> ConvertFToU(D /* tag */, Vec128<float> v) {
#if HWY_COMPILER_CLANG && \
    ((HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200) || HWY_ARCH_ARM_V7)
  // If compiling for AArch64 NEON with Clang 11 or earlier or if compiling for
  // Armv7 NEON, use inline assembly to avoid undefined behavior if v[i] is
  // outside of the range of an uint32_t.

  uint32x4_t raw_result;
  __asm__(
#if HWY_ARCH_ARM_A64
      "fcvtzu %0.4s, %1.4s"
#else
      "vcvt.u32.f32 %0, %1"
#endif
      : "=w"(raw_result)
      : "w"(v.raw));
  return Vec128<uint32_t>(raw_result);
#else
  return Vec128<uint32_t>(vcvtq_u32_f32(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_INLINE VFromD<D> ConvertFToU(D /* tag */, VFromD<RebindToFloat<D>> v) {
#if HWY_COMPILER_CLANG && \
    ((HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200) || HWY_ARCH_ARM_V7)
  // If compiling for AArch64 NEON with Clang 11 or earlier or if compiling for
  // Armv7 NEON, use inline assembly to avoid undefined behavior if v[i] is
  // outside of the range of an uint32_t.

  uint32x2_t raw_result;
  __asm__(
#if HWY_ARCH_ARM_A64
      "fcvtzu %0.2s, %1.2s"
#else
      "vcvt.u32.f32 %0, %1"
#endif
      : "=w"(raw_result)
      : "w"(v.raw));
  return VFromD<D>(raw_result);
#else
  return VFromD<D>(vcvt_u32_f32(v.raw));
#endif
}

#if HWY_HAVE_FLOAT64

// Truncates (rounds toward zero).
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_INLINE Vec128<int64_t> ConvertFToI(D /* tag */, Vec128<double> v) {
#if HWY_COMPILER_CLANG && HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an int64_t.
  int64x2_t raw_result;
  __asm__("fcvtzs %0.2d, %1.2d" : "=w"(raw_result) : "w"(v.raw));
  return Vec128<int64_t>(raw_result);
#else
  return Vec128<int64_t>(vcvtq_s64_f64(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I64_D(D)>
HWY_INLINE Vec64<int64_t> ConvertFToI(D /* tag */, Vec64<double> v) {
#if HWY_ARCH_ARM_A64 &&                                            \
    ((HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700) || \
     (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200))
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an int64_t.
  // If compiling for AArch64 NEON with GCC 6 or earlier, use inline assembly to
  // work around the missing vcvt_s64_f64 intrinsic.
  int64x1_t raw_result;
  __asm__("fcvtzs %d0, %d1" : "=w"(raw_result) : "w"(v.raw));
  return Vec64<int64_t>(raw_result);
#else
  return Vec64<int64_t>(vcvt_s64_f64(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U64_D(D)>
HWY_INLINE Vec128<uint64_t> ConvertFToU(D /* tag */, Vec128<double> v) {
#if HWY_COMPILER_CLANG && HWY_ARCH_ARM_A64 && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an uint64_t.
  uint64x2_t raw_result;
  __asm__("fcvtzu %0.2d, %1.2d" : "=w"(raw_result) : "w"(v.raw));
  return Vec128<uint64_t>(raw_result);
#else
  return Vec128<uint64_t>(vcvtq_u64_f64(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U64_D(D)>
HWY_INLINE Vec64<uint64_t> ConvertFToU(D /* tag */, Vec64<double> v) {
#if HWY_ARCH_ARM_A64 &&                                            \
    ((HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700) || \
     (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200))
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an uint64_t.

  // Inline assembly is also used if compiling for AArch64 NEON with GCC 6 or
  // earlier to work around the issue of the missing vcvt_u64_f64 intrinsic.
  uint64x1_t raw_result;
  __asm__("fcvtzu %d0, %d1" : "=w"(raw_result) : "w"(v.raw));
  return Vec64<uint64_t>(raw_result);
#else
  return Vec64<uint64_t>(vcvt_u64_f64(v.raw));
#endif
}

#endif  // HWY_HAVE_FLOAT64

#if HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16

// Truncates (rounds toward zero).
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_INLINE Vec128<int16_t> ConvertFToI(D /* tag */, Vec128<float16_t> v) {
#if HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an int16_t.
  int16x8_t raw_result;
  __asm__("fcvtzs %0.8h, %1.8h" : "=w"(raw_result) : "w"(v.raw));
  return Vec128<int16_t>(raw_result);
#else
  return Vec128<int16_t>(vcvtq_s16_f16(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_INLINE VFromD<D> ConvertFToI(D /* tag */, VFromD<Rebind<float16_t, D>> v) {
#if HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an int16_t.
  int16x4_t raw_result;
  __asm__("fcvtzs %0.4h, %1.4h" : "=w"(raw_result) : "w"(v.raw));
  return VFromD<D>(raw_result);
#else
  return VFromD<D>(vcvt_s16_f16(v.raw));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_INLINE Vec128<uint16_t> ConvertFToU(D /* tag */, Vec128<float16_t> v) {
#if HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an uint16_t.
  uint16x8_t raw_result;
  __asm__("fcvtzu %0.8h, %1.8h" : "=w"(raw_result) : "w"(v.raw));
  return Vec128<uint16_t>(raw_result);
#else
  return Vec128<uint16_t>(vcvtq_u16_f16(v.raw));
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_INLINE VFromD<D> ConvertFToU(D /* tag */, VFromD<Rebind<float16_t, D>> v) {
#if HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200
  // If compiling for AArch64 NEON with Clang 11 or earlier, use inline assembly
  // to avoid undefined behavior if v[i] is outside of the range of an uint16_t.
  uint16x4_t raw_result;
  __asm__("fcvtzu %0.4h, %1.4h" : "=w"(raw_result) : "w"(v.raw));
  return VFromD<D>(raw_result);
#else
  return VFromD<D>(vcvt_u16_f16(v.raw));
#endif
}

#endif  // HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16
}  // namespace detail

template <class D, HWY_IF_SIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(
              D, (1 << 4) |
                     ((HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16) ? (1 << 2) : 0) |
                     (HWY_HAVE_FLOAT64 ? (1 << 8) : 0))>
HWY_API VFromD<D> ConvertTo(D di, VFromD<RebindToFloat<D>> v) {
  return detail::ConvertFToI(di, v);
}

template <class D, HWY_IF_UNSIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(
              D, (1 << 4) |
                     ((HWY_ARCH_ARM_A64 && HWY_HAVE_FLOAT16) ? (1 << 2) : 0) |
                     (HWY_HAVE_FLOAT64 ? (1 << 8) : 0))>
HWY_API VFromD<D> ConvertTo(D du, VFromD<RebindToFloat<D>> v) {
  return detail::ConvertFToU(du, v);
}

// ------------------------------ PromoteTo (ConvertTo)

// Unsigned: zero-extend to full vector.
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> PromoteTo(D /* tag */, Vec64<uint8_t> v) {
  return Vec128<uint16_t>(vmovl_u8(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> PromoteTo(D /* tag */, Vec32<uint8_t> v) {
  uint16x8_t a = vmovl_u8(v.raw);
  return Vec128<uint32_t>(vmovl_u16(vget_low_u16(a)));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> PromoteTo(D /* tag */, Vec64<uint16_t> v) {
  return Vec128<uint32_t>(vmovl_u16(v.raw));
}
template <class D, HWY_IF_U64_D(D)>
HWY_API Vec128<uint64_t> PromoteTo(D /* tag */, Vec64<uint32_t> v) {
  return Vec128<uint64_t>(vmovl_u32(v.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> PromoteTo(D d, Vec64<uint8_t> v) {
  return BitCast(d, Vec128<uint16_t>(vmovl_u8(v.raw)));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteTo(D d, Vec32<uint8_t> v) {
  uint16x8_t a = vmovl_u8(v.raw);
  return BitCast(d, Vec128<uint32_t>(vmovl_u16(vget_low_u16(a))));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteTo(D d, Vec64<uint16_t> v) {
  return BitCast(d, Vec128<uint32_t>(vmovl_u16(v.raw)));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> PromoteTo(D d, Vec64<uint32_t> v) {
  return BitCast(d, Vec128<uint64_t>(vmovl_u32(v.raw)));
}

// Unsigned: zero-extend to half vector.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>(vget_low_u16(vmovl_u8(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>(vget_low_u32(vmovl_u16(vget_low_u16(vmovl_u8(v.raw)))));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>(vget_low_u32(vmovl_u16(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>(vget_low_u64(vmovl_u32(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint8_t, D>> v) {
  using VU16 = VFromD<RebindToUnsigned<D>>;
  return BitCast(d, VU16(vget_low_u16(vmovl_u8(v.raw))));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  const uint32x4_t u32 = vmovl_u16(vget_low_u16(vmovl_u8(v.raw)));
  return VFromD<D>(vget_low_s32(vreinterpretq_s32_u32(u32)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>(vget_low_s32(vreinterpretq_s32_u32(vmovl_u16(v.raw))));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint32_t, D>> v) {
  using DU = RebindToUnsigned<D>;
  return BitCast(d, VFromD<DU>(vget_low_u64(vmovl_u32(v.raw))));
}

// U8/U16 to U64/I64: First, zero-extend to U32, and then zero-extend to
// TFromD<D>
template <class D, class V, HWY_IF_UI64_D(D),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V)), HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> PromoteTo(D d, V v) {
  const Rebind<uint32_t, decltype(d)> du32;
  return PromoteTo(d, PromoteTo(du32, v));
}

// Signed: replicate sign bit to full vector.
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> PromoteTo(D /* tag */, Vec64<int8_t> v) {
  return Vec128<int16_t>(vmovl_s8(v.raw));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteTo(D /* tag */, Vec32<int8_t> v) {
  int16x8_t a = vmovl_s8(v.raw);
  return Vec128<int32_t>(vmovl_s16(vget_low_s16(a)));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteTo(D /* tag */, Vec64<int16_t> v) {
  return Vec128<int32_t>(vmovl_s16(v.raw));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> PromoteTo(D /* tag */, Vec64<int32_t> v) {
  return Vec128<int64_t>(vmovl_s32(v.raw));
}

// Signed: replicate sign bit to half vector.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
  return VFromD<D>(vget_low_s16(vmovl_s8(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
  return VFromD<D>(vget_low_s32(vmovl_s16(vget_low_s16(vmovl_s8(v.raw)))));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>(vget_low_s32(vmovl_s16(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>(vget_low_s64(vmovl_s32(v.raw)));
}

// I8/I16 to I64: First, promote to I32, and then promote to I64
template <class D, class V, HWY_IF_I64_D(D),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V)), HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> PromoteTo(D d, V v) {
  const Rebind<int32_t, decltype(d)> di32;
  return PromoteTo(d, PromoteTo(di32, v));
}

#if HWY_NEON_HAVE_F16C

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec128<float> PromoteTo(D /* tag */, Vec64<float16_t> v) {
  return Vec128<float>(vcvt_f32_f16(v.raw));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<float16_t, D>> v) {
  return VFromD<D>(vget_low_f32(vcvt_f32_f16(v.raw)));
}

#endif  // HWY_NEON_HAVE_F16C

#if HWY_HAVE_FLOAT64

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteTo(D /* tag */, Vec64<float> v) {
  return Vec128<double>(vcvt_f64_f32(v.raw));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> PromoteTo(D /* tag */, Vec32<float> v) {
  return Vec64<double>(vget_low_f64(vcvt_f64_f32(v.raw)));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteTo(D /* tag */, Vec64<int32_t> v) {
  const int64x2_t i64 = vmovl_s32(v.raw);
  return Vec128<double>(vcvtq_f64_s64(i64));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> PromoteTo(D d, Vec32<int32_t> v) {
  return ConvertTo(d, Vec64<int64_t>(vget_low_s64(vmovl_s32(v.raw))));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteTo(D /* tag */, Vec64<uint32_t> v) {
  const uint64x2_t u64 = vmovl_u32(v.raw);
  return Vec128<double>(vcvtq_f64_u64(u64));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> PromoteTo(D d, Vec32<uint32_t> v) {
  return ConvertTo(d, Vec64<uint64_t>(vget_low_u64(vmovl_u32(v.raw))));
}

template <class D, HWY_IF_UI64_D(D)>
HWY_API VFromD<D> PromoteTo(D d64, VFromD<Rebind<float, D>> v) {
  const RebindToFloat<decltype(d64)> df64;
  return ConvertTo(d64, PromoteTo(df64, v));
}

#else  // !HWY_HAVE_FLOAT64

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D di64, VFromD<Rebind<float, D>> v) {
  const Rebind<int32_t, decltype(di64)> di32;
  const RebindToFloat<decltype(di32)> df32;
  const RebindToUnsigned<decltype(di32)> du32;
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;

  const auto exponent_adj = BitCast(
      du32,
      Min(SaturatedSub(BitCast(du32_as_du8, ShiftRight<23>(BitCast(du32, v))),
                       BitCast(du32_as_du8, Set(du32, uint32_t{157}))),
          BitCast(du32_as_du8, Set(du32, uint32_t{32}))));
  const auto adj_v =
      BitCast(df32, BitCast(du32, v) - ShiftLeft<23>(exponent_adj));

  const auto f32_to_i32_result = ConvertTo(di32, adj_v);
  const auto lo64_or_mask = PromoteTo(
      di64,
      BitCast(du32, VecFromMask(di32, Eq(f32_to_i32_result,
                                         Set(di32, LimitsMax<int32_t>())))));

  return Or(PromoteTo(di64, BitCast(di32, f32_to_i32_result))
                << PromoteTo(di64, exponent_adj),
            lo64_or_mask);
}

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D du64, VFromD<Rebind<float, D>> v) {
  const Rebind<uint32_t, decltype(du64)> du32;
  const RebindToFloat<decltype(du32)> df32;
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;

  const auto exponent_adj = BitCast(
      du32,
      Min(SaturatedSub(BitCast(du32_as_du8, ShiftRight<23>(BitCast(du32, v))),
                       BitCast(du32_as_du8, Set(du32, uint32_t{158}))),
          BitCast(du32_as_du8, Set(du32, uint32_t{32}))));

  const auto adj_v =
      BitCast(df32, BitCast(du32, v) - ShiftLeft<23>(exponent_adj));
  const auto f32_to_u32_result = ConvertTo(du32, adj_v);
  const auto lo32_or_mask = PromoteTo(
      du64,
      VecFromMask(du32, f32_to_u32_result == Set(du32, LimitsMax<uint32_t>())));

  return Or(PromoteTo(du64, f32_to_u32_result) << PromoteTo(du64, exponent_adj),
            lo32_or_mask);
}

#ifdef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#endif

template <class D, HWY_IF_UI64_D(D)>
HWY_API VFromD<D> PromoteInRangeTo(D d64, VFromD<Rebind<float, D>> v) {
  const Rebind<MakeNarrow<TFromD<D>>, decltype(d64)> d32;
  const RebindToFloat<decltype(d32)> df32;
  const RebindToUnsigned<decltype(d32)> du32;
  const Repartition<uint8_t, decltype(d32)> du32_as_du8;

  constexpr uint32_t kExpAdjDecr =
      0xFFFFFF9Du + static_cast<uint32_t>(!IsSigned<TFromD<D>>());

  const auto exponent_adj = BitCast(
      du32, SaturatedSub(BitCast(du32_as_du8, ShiftRight<23>(BitCast(du32, v))),
                         BitCast(du32_as_du8, Set(du32, kExpAdjDecr))));
  const auto adj_v =
      BitCast(df32, BitCast(du32, v) - ShiftLeft<23>(exponent_adj));

  return PromoteTo(d64, ConvertTo(d32, adj_v)) << PromoteTo(d64, exponent_adj);
}

#endif  // HWY_HAVE_FLOAT64

// ------------------------------ PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ------------------------------ PromoteUpperTo

#if HWY_ARCH_ARM_A64

// Per-target flag to prevent generic_ops-inl.h from defining PromoteUpperTo.
#ifdef HWY_NATIVE_PROMOTE_UPPER_TO
#undef HWY_NATIVE_PROMOTE_UPPER_TO
#else
#define HWY_NATIVE_PROMOTE_UPPER_TO
#endif

// Unsigned: zero-extend to full vector.
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> PromoteUpperTo(D /* tag */, Vec128<uint8_t> v) {
  return Vec128<uint16_t>(vmovl_high_u8(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> PromoteUpperTo(D /* tag */, Vec128<uint16_t> v) {
  return Vec128<uint32_t>(vmovl_high_u16(v.raw));
}
template <class D, HWY_IF_U64_D(D)>
HWY_API Vec128<uint64_t> PromoteUpperTo(D /* tag */, Vec128<uint32_t> v) {
  return Vec128<uint64_t>(vmovl_high_u32(v.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> PromoteUpperTo(D d, Vec128<uint8_t> v) {
  return BitCast(d, Vec128<uint16_t>(vmovl_high_u8(v.raw)));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteUpperTo(D d, Vec128<uint16_t> v) {
  return BitCast(d, Vec128<uint32_t>(vmovl_high_u16(v.raw)));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> PromoteUpperTo(D d, Vec128<uint32_t> v) {
  return BitCast(d, Vec128<uint64_t>(vmovl_high_u32(v.raw)));
}

// Signed: replicate sign bit to full vector.
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> PromoteUpperTo(D /* tag */, Vec128<int8_t> v) {
  return Vec128<int16_t>(vmovl_high_s8(v.raw));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> PromoteUpperTo(D /* tag */, Vec128<int16_t> v) {
  return Vec128<int32_t>(vmovl_high_s16(v.raw));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec128<int64_t> PromoteUpperTo(D /* tag */, Vec128<int32_t> v) {
  return Vec128<int64_t>(vmovl_high_s32(v.raw));
}

#if HWY_NEON_HAVE_F16C

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec128<float> PromoteUpperTo(D /* tag */, Vec128<float16_t> v) {
  return Vec128<float>(vcvt_high_f32_f16(v.raw));
}

#endif  // HWY_NEON_HAVE_F16C

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df32, VFromD<Repartition<bfloat16_t, D>> v) {
  const Repartition<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteUpperTo(di32, BitCast(du16, v))));
}

#if HWY_HAVE_FLOAT64

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteUpperTo(D /* tag */, Vec128<float> v) {
  return Vec128<double>(vcvt_high_f64_f32(v.raw));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteUpperTo(D /* tag */, Vec128<int32_t> v) {
  const int64x2_t i64 = vmovl_high_s32(v.raw);
  return Vec128<double>(vcvtq_f64_s64(i64));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API Vec128<double> PromoteUpperTo(D /* tag */, Vec128<uint32_t> v) {
  const uint64x2_t u64 = vmovl_high_u32(v.raw);
  return Vec128<double>(vcvtq_f64_u64(u64));
}

#endif  // HWY_HAVE_FLOAT64

template <class D, HWY_IF_UI64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D d64, Vec128<float> v) {
#if HWY_HAVE_FLOAT64
  const RebindToFloat<decltype(d64)> df64;
  return ConvertTo(d64, PromoteUpperTo(df64, v));
#else
  const Rebind<float, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
#endif
}

// Generic version for <=64 bit input/output (_high is only for full vectors).
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), class V>
HWY_API VFromD<D> PromoteUpperTo(D d, V v) {
  const Rebind<TFromV<V>, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
}

#endif  // HWY_ARCH_ARM_A64

// ------------------------------ DemoteTo (ConvertTo)

// From full vector to half or quarter
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> DemoteTo(D /* tag */, Vec128<int32_t> v) {
  return Vec64<uint16_t>(vqmovun_s32(v.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> DemoteTo(D /* tag */, Vec128<int32_t> v) {
  return Vec64<int16_t>(vqmovn_s32(v.raw));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec32<uint8_t> DemoteTo(D /* tag */, Vec128<int32_t> v) {
  const uint16x4_t a = vqmovun_s32(v.raw);
  return Vec32<uint8_t>(vqmovn_u16(vcombine_u16(a, a)));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> DemoteTo(D /* tag */, Vec128<int16_t> v) {
  return Vec64<uint8_t>(vqmovun_s16(v.raw));
}
template <class D, HWY_IF_I8_D(D)>
HWY_API Vec32<int8_t> DemoteTo(D /* tag */, Vec128<int32_t> v) {
  const int16x4_t a = vqmovn_s32(v.raw);
  return Vec32<int8_t>(vqmovn_s16(vcombine_s16(a, a)));
}
template <class D, HWY_IF_I8_D(D)>
HWY_API Vec64<int8_t> DemoteTo(D /* tag */, Vec128<int16_t> v) {
  return Vec64<int8_t>(vqmovn_s16(v.raw));
}
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> DemoteTo(D /* tag */, Vec128<uint32_t> v) {
  return Vec64<uint16_t>(vqmovn_u32(v.raw));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec32<uint8_t> DemoteTo(D /* tag */, Vec128<uint32_t> v) {
  const uint16x4_t a = vqmovn_u32(v.raw);
  return Vec32<uint8_t>(vqmovn_u16(vcombine_u16(a, a)));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> DemoteTo(D /* tag */, Vec128<uint16_t> v) {
  return Vec64<uint8_t>(vqmovn_u16(v.raw));
}

// From half vector to partial half
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>(vqmovun_s32(vcombine_s32(v.raw, v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>(vqmovn_s32(vcombine_s32(v.raw, v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const uint16x4_t a = vqmovun_s32(vcombine_s32(v.raw, v.raw));
  return VFromD<D>(vqmovn_u16(vcombine_u16(a, a)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>(vqmovun_s16(vcombine_s16(v.raw, v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const int16x4_t a = vqmovn_s32(vcombine_s32(v.raw, v.raw));
  return VFromD<D>(vqmovn_s16(vcombine_s16(a, a)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>(vqmovn_s16(vcombine_s16(v.raw, v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>(vqmovn_u32(vcombine_u32(v.raw, v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const uint16x4_t a = vqmovn_u32(vcombine_u32(v.raw, v.raw));
  return VFromD<D>(vqmovn_u16(vcombine_u16(a, a)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>(vqmovn_u16(vcombine_u16(v.raw, v.raw)));
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> DemoteTo(D /* tag */, Vec128<int64_t> v) {
  return Vec64<int32_t>(vqmovn_s64(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> DemoteTo(D /* tag */, Vec128<int64_t> v) {
  return Vec64<uint32_t>(vqmovun_s64(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> DemoteTo(D /* tag */, Vec128<uint64_t> v) {
  return Vec64<uint32_t>(vqmovn_u64(v.raw));
}
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_SIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D d, Vec128<int64_t> v) {
  const Rebind<int32_t, D> di32;
  return DemoteTo(d, DemoteTo(di32, v));
}
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D d, Vec128<int64_t> v) {
  const Rebind<uint32_t, D> du32;
  return DemoteTo(d, DemoteTo(du32, v));
}
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D d, Vec128<uint64_t> v) {
  const Rebind<uint32_t, D> du32;
  return DemoteTo(d, DemoteTo(du32, v));
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec32<int32_t> DemoteTo(D /* tag */, Vec64<int64_t> v) {
  return Vec32<int32_t>(vqmovn_s64(vcombine_s64(v.raw, v.raw)));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> DemoteTo(D /* tag */, Vec64<int64_t> v) {
  return Vec32<uint32_t>(vqmovun_s64(vcombine_s64(v.raw, v.raw)));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> DemoteTo(D /* tag */, Vec64<uint64_t> v) {
  return Vec32<uint32_t>(vqmovn_u64(vcombine_u64(v.raw, v.raw)));
}
template <class D, HWY_IF_SIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> DemoteTo(D d, Vec64<int64_t> v) {
  const Rebind<int32_t, D> di32;
  return DemoteTo(d, DemoteTo(di32, v));
}
template <class D, HWY_IF_UNSIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> DemoteTo(D d, Vec64<int64_t> v) {
  const Rebind<uint32_t, D> du32;
  return DemoteTo(d, DemoteTo(du32, v));
}
template <class D, HWY_IF_LANES_D(D, 1), HWY_IF_UNSIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> DemoteTo(D d, Vec64<uint64_t> v) {
  const Rebind<uint32_t, D> du32;
  return DemoteTo(d, DemoteTo(du32, v));
}

#if HWY_NEON_HAVE_F16C

// We already toggled HWY_NATIVE_F16C above.

template <class D, HWY_IF_F16_D(D)>
HWY_API Vec64<float16_t> DemoteTo(D /* tag */, Vec128<float> v) {
  return Vec64<float16_t>{vcvt_f16_f32(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return VFromD<D>(vcvt_f16_f32(vcombine_f32(v.raw, v.raw)));
}

#endif  // HWY_NEON_HAVE_F16C

#if HWY_NEON_HAVE_F32_TO_BF16C
#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

namespace detail {
#if HWY_NEON_HAVE_BFLOAT16
// If HWY_NEON_HAVE_BFLOAT16 is true, detail::Vec128<bfloat16_t, N>::type is
// bfloat16x4_t or bfloat16x8_t.
static HWY_INLINE bfloat16x4_t BitCastFromRawNeonBF16(bfloat16x4_t raw) {
  return raw;
}
#else
// If HWY_NEON_HAVE_F32_TO_BF16C && !HWY_NEON_HAVE_BFLOAT16 is true,
// detail::Vec128<bfloat16_t, N>::type is uint16x4_t or uint16x8_t vector to
// work around compiler bugs that are there with GCC 13 or earlier or Clang 16
// or earlier on AArch64.

// The bfloat16x4_t vector returned by vcvt_bf16_f32 needs to be bitcasted to
// an uint16x4_t vector if HWY_NEON_HAVE_F32_TO_BF16C &&
// !HWY_NEON_HAVE_BFLOAT16 is true.
static HWY_INLINE uint16x4_t BitCastFromRawNeonBF16(bfloat16x4_t raw) {
  return vreinterpret_u16_bf16(raw);
}
#endif
}  // namespace detail

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> DemoteTo(D /*dbf16*/, VFromD<Rebind<float, D>> v) {
  return VFromD<D>(detail::BitCastFromRawNeonBF16(vcvt_bf16_f32(v.raw)));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> DemoteTo(D /*dbf16*/, VFromD<Rebind<float, D>> v) {
  return VFromD<D>(detail::BitCastFromRawNeonBF16(
      vcvt_bf16_f32(vcombine_f32(v.raw, v.raw))));
}
#endif  // HWY_NEON_HAVE_F32_TO_BF16C

#if HWY_HAVE_FLOAT64

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec64<float> DemoteTo(D /* tag */, Vec128<double> v) {
  return Vec64<float>(vcvt_f32_f64(v.raw));
}
template <class D, HWY_IF_F32_D(D)>
HWY_API Vec32<float> DemoteTo(D /* tag */, Vec64<double> v) {
  return Vec32<float>(vcvt_f32_f64(vcombine_f64(v.raw, v.raw)));
}

template <class D, HWY_IF_UI32_D(D)>
HWY_API VFromD<D> DemoteTo(D d32, VFromD<Rebind<double, D>> v) {
  const Rebind<MakeWide<TFromD<D>>, D> d64;
  return DemoteTo(d32, ConvertTo(d64, v));
}

#endif  // HWY_HAVE_FLOAT64

template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D df32, VFromD<Rebind<int64_t, D>> v) {
  const Rebind<int64_t, decltype(df32)> di64;
  const RebindToUnsigned<decltype(di64)> du64;

#if HWY_ARCH_ARM_A64
  const RebindToFloat<decltype(du64)> df64;

  const auto k2p64_63 = Set(df64, 27670116110564327424.0);
  const auto f64_hi52 =
      Xor(BitCast(df64, ShiftRight<12>(BitCast(du64, v))), k2p64_63) - k2p64_63;
  const auto f64_lo12 =
      ConvertTo(df64, And(BitCast(du64, v), Set(du64, uint64_t{0x00000FFF})));

  const auto f64_sum = f64_hi52 + f64_lo12;
  const auto f64_carry = (f64_hi52 - f64_sum) + f64_lo12;

  const auto f64_sum_is_inexact =
      ShiftRight<63>(BitCast(du64, VecFromMask(df64, f64_carry != Zero(df64))));
  const auto f64_bits_decrement =
      And(ShiftRight<63>(BitCast(du64, Xor(f64_sum, f64_carry))),
          f64_sum_is_inexact);

  const auto adj_f64_val = BitCast(
      df64,
      Or(BitCast(du64, f64_sum) - f64_bits_decrement, f64_sum_is_inexact));

  return DemoteTo(df32, adj_f64_val);
#else
  const RebindToUnsigned<decltype(df32)> du32;
  const auto hi23 = TruncateTo(du32, ShiftRight<41>(BitCast(du64, v)));
  const auto mid23 = And(TruncateTo(du32, ShiftRight<18>(BitCast(du64, v))),
                         Set(du32, uint32_t{0x007FFFFFu}));
  const auto lo18 =
      And(TruncateTo(du32, BitCast(du64, v)), Set(du32, uint32_t{0x0003FFFFu}));

  const auto k2p41_f32 = Set(df32, 2199023255552.0f);
  const auto k2p64_63_f32 = Set(df32, 27670116110564327424.0f);

  const auto hi23_f32 =
      BitCast(df32, Xor(hi23, BitCast(du32, k2p64_63_f32))) - k2p64_63_f32;
  const auto mid23_f32 =
      BitCast(df32, Or(mid23, BitCast(du32, k2p41_f32))) - k2p41_f32;
  const auto lo18_f32 = ConvertTo(df32, lo18);

  const auto s_hi46 = hi23_f32 + mid23_f32;
  const auto c_hi46 = (hi23_f32 - s_hi46) + mid23_f32;

  auto s_lo = c_hi46 + lo18_f32;
  const auto c_lo = (c_hi46 - s_lo) + lo18_f32;

  const auto s_lo_inexact_mask =
      VecFromMask(du32, RebindMask(du32, c_lo != Zero(df32)));
  const auto s_lo_mag_adj = ShiftRight<31>(
      And(s_lo_inexact_mask, Xor(BitCast(du32, s_lo), BitCast(du32, c_lo))));

  s_lo = BitCast(df32, BitCast(du32, s_lo) - s_lo_mag_adj);
  s_lo =
      BitCast(df32, Or(BitCast(du32, s_lo), ShiftRight<31>(s_lo_inexact_mask)));
  return s_hi46 + s_lo;
#endif
}

template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D df32, VFromD<Rebind<uint64_t, D>> v) {
#if HWY_ARCH_ARM_A64
  const Rebind<uint64_t, decltype(df32)> du64;
  const RebindToFloat<decltype(du64)> df64;

  const auto k2p64 = Set(df64, 18446744073709551616.0);
  const auto f64_hi52 = Or(BitCast(df64, ShiftRight<12>(v)), k2p64) - k2p64;
  const auto f64_lo12 =
      ConvertTo(df64, And(v, Set(du64, uint64_t{0x00000FFF})));

  const auto f64_sum = f64_hi52 + f64_lo12;
  const auto f64_carry = (f64_hi52 - f64_sum) + f64_lo12;
  const auto f64_sum_is_inexact =
      ShiftRight<63>(BitCast(du64, VecFromMask(df64, f64_carry != Zero(df64))));

  const auto adj_f64_val = BitCast(
      df64,
      Or(BitCast(du64, f64_sum) - ShiftRight<63>(BitCast(du64, f64_carry)),
         f64_sum_is_inexact));

  return DemoteTo(df32, adj_f64_val);
#else
  const RebindToUnsigned<decltype(df32)> du32;

  const auto hi23 = TruncateTo(du32, ShiftRight<41>(v));
  const auto mid23 = And(TruncateTo(du32, ShiftRight<18>(v)),
                         Set(du32, uint32_t{0x007FFFFFu}));
  const auto lo18 = And(TruncateTo(du32, v), Set(du32, uint32_t{0x0003FFFFu}));

  const auto k2p41_f32 = Set(df32, 2199023255552.0f);
  const auto k2p64_f32 = Set(df32, 18446744073709551616.0f);

  const auto hi23_f32 =
      BitCast(df32, Or(hi23, BitCast(du32, k2p64_f32))) - k2p64_f32;
  const auto mid23_f32 =
      BitCast(df32, Or(mid23, BitCast(du32, k2p41_f32))) - k2p41_f32;
  const auto lo18_f32 = ConvertTo(df32, lo18);

  const auto s_hi46 = hi23_f32 + mid23_f32;
  const auto c_hi46 = (hi23_f32 - s_hi46) + mid23_f32;

  auto s_lo = c_hi46 + lo18_f32;
  const auto c_lo = (c_hi46 - s_lo) + lo18_f32;

  const auto s_lo_inexact_mask =
      VecFromMask(du32, RebindMask(du32, c_lo != Zero(df32)));
  const auto s_lo_mag_adj = ShiftRight<31>(
      And(s_lo_inexact_mask, Xor(BitCast(du32, s_lo), BitCast(du32, c_lo))));

  s_lo = BitCast(df32, BitCast(du32, s_lo) - s_lo_mag_adj);
  s_lo =
      BitCast(df32, Or(BitCast(du32, s_lo), ShiftRight<31>(s_lo_inexact_mask)));
  return s_hi46 + s_lo;
#endif
}

HWY_API Vec32<uint8_t> U8FromU32(Vec128<uint32_t> v) {
  const uint8x16_t org_v = detail::BitCastToByte(v).raw;
  const uint8x16_t w = vuzp1q_u8(org_v, org_v);
  return Vec32<uint8_t>(vget_low_u8(vuzp1q_u8(w, w)));
}
template <size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8)>
HWY_API Vec128<uint8_t, N> U8FromU32(Vec128<uint32_t, N> v) {
  const uint8x8_t org_v = detail::BitCastToByte(v).raw;
  const uint8x8_t w = vuzp1_u8(org_v, org_v);
  return Vec128<uint8_t, N>(vuzp1_u8(w, w));
}

// ------------------------------ Round (IfThenElse, mask, logical)

#if HWY_ARCH_ARM_A64
// Toward nearest integer
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Round, vrndn, _, 1)

// Toward zero, aka truncate
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Trunc, vrnd, _, 1)

// Toward +infinity, aka ceiling
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Ceil, vrndp, _, 1)

// Toward -infinity, aka floor
HWY_NEON_DEF_FUNCTION_ALL_FLOATS(Floor, vrndm, _, 1)
#else

// ------------------------------ Trunc

// Armv7 only supports truncation to integer. We can either convert back to
// float (3 floating-point and 2 logic operations) or manipulate the binary32
// representation, clearing the lowest 23-exp mantissa bits. This requires 9
// integer operations and 3 constants, which is likely more expensive.

namespace detail {

// The original value is already the desired result if NaN or the magnitude is
// large (i.e. the value is already an integer).
template <size_t N>
HWY_INLINE Mask128<float, N> UseInt(const Vec128<float, N> v) {
  return Abs(v) < Set(Simd<float, N, 0>(), MantissaEnd<float>());
}

}  // namespace detail

template <size_t N>
HWY_API Vec128<float, N> Trunc(const Vec128<float, N> v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), int_f, v);
}

template <size_t N>
HWY_API Vec128<float, N> Round(const Vec128<float, N> v) {
  const DFromV<decltype(v)> df;

  // Armv7 also lacks a native NearestInt, but we can instead rely on rounding
  // (we assume the current mode is nearest-even) after addition with a large
  // value such that no mantissa bits remain. We may need a compiler flag for
  // precise floating-point to prevent this from being "optimized" out.
  const auto max = Set(df, MantissaEnd<float>());
  const auto large = CopySignToAbs(max, v);
  const auto added = large + v;
  const auto rounded = added - large;

  // Keep original if NaN or the magnitude is large (already an int).
  return IfThenElse(Abs(v) < max, rounded, v);
}

template <size_t N>
HWY_API Vec128<float, N> Ceil(const Vec128<float, N> v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a positive non-integer ends up smaller; if so, add 1.
  const auto neg1 = ConvertTo(df, VecFromMask(di, RebindMask(di, int_f < v)));

  return IfThenElse(detail::UseInt(v), int_f - neg1, v);
}

template <size_t N>
HWY_API Vec128<float, N> Floor(const Vec128<float, N> v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a negative non-integer ends up larger; if so, subtract 1.
  const auto neg1 = ConvertTo(df, VecFromMask(di, RebindMask(di, int_f > v)));

  return IfThenElse(detail::UseInt(v), int_f + neg1, v);
}

#endif

// ------------------------------ CeilInt/FloorInt
#if HWY_ARCH_ARM_A64

#ifdef HWY_NATIVE_CEIL_FLOOR_INT
#undef HWY_NATIVE_CEIL_FLOOR_INT
#else
#define HWY_NATIVE_CEIL_FLOOR_INT
#endif

#if HWY_HAVE_FLOAT16
HWY_API Vec128<int16_t> CeilInt(const Vec128<float16_t> v) {
  return Vec128<int16_t>(vcvtpq_s16_f16(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(float16_t, N, 8)>
HWY_API Vec128<int16_t, N> CeilInt(const Vec128<float16_t, N> v) {
  return Vec128<int16_t, N>(vcvtp_s16_f16(v.raw));
}

HWY_API Vec128<int16_t> FloorInt(const Vec128<float16_t> v) {
  return Vec128<int16_t>(vcvtmq_s16_f16(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(float16_t, N, 8)>
HWY_API Vec128<int16_t, N> FloorInt(const Vec128<float16_t, N> v) {
  return Vec128<int16_t, N>(vcvtm_s16_f16(v.raw));
}
#endif  // HWY_HAVE_FLOAT16

HWY_API Vec128<int32_t> CeilInt(const Vec128<float> v) {
  return Vec128<int32_t>(vcvtpq_s32_f32(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(float, N, 8)>
HWY_API Vec128<int32_t, N> CeilInt(const Vec128<float, N> v) {
  return Vec128<int32_t, N>(vcvtp_s32_f32(v.raw));
}

HWY_API Vec128<int64_t> CeilInt(const Vec128<double> v) {
  return Vec128<int64_t>(vcvtpq_s64_f64(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(double, N, 8)>
HWY_API Vec128<int64_t, N> CeilInt(const Vec128<double, N> v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 610
  // Workaround for missing vcvtp_s64_f64 intrinsic
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const Twice<decltype(d)> dt;
  return LowerHalf(di, CeilInt(Combine(dt, v, v)));
#else
  return Vec128<int64_t, N>(vcvtp_s64_f64(v.raw));
#endif
}

HWY_API Vec128<int32_t> FloorInt(const Vec128<float> v) {
  return Vec128<int32_t>(vcvtmq_s32_f32(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(float, N, 8)>
HWY_API Vec128<int32_t, N> FloorInt(const Vec128<float, N> v) {
  return Vec128<int32_t, N>(vcvtm_s32_f32(v.raw));
}

HWY_API Vec128<int64_t> FloorInt(const Vec128<double> v) {
  return Vec128<int64_t>(vcvtmq_s64_f64(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(double, N, 8)>
HWY_API Vec128<int64_t, N> FloorInt(const Vec128<double, N> v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 610
  // Workaround for missing vcvtm_s64_f64 intrinsic
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const Twice<decltype(d)> dt;
  return LowerHalf(di, FloorInt(Combine(dt, v, v)));
#else
  return Vec128<int64_t, N>(vcvtm_s64_f64(v.raw));
#endif
}

#endif  // HWY_ARCH_ARM_A64

// ------------------------------ NearestInt (Round)

#if HWY_HAVE_FLOAT16
HWY_API Vec128<int16_t> NearestInt(const Vec128<float16_t> v) {
  return Vec128<int16_t>(vcvtnq_s16_f16(v.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(float16_t, N, 8)>
HWY_API Vec128<int16_t, N> NearestInt(const Vec128<float16_t, N> v) {
  return Vec128<int16_t, N>(vcvtn_s16_f16(v.raw));
}
#endif

#if HWY_ARCH_ARM_A64

HWY_API Vec128<int32_t> NearestInt(const Vec128<float> v) {
  return Vec128<int32_t>(vcvtnq_s32_f32(v.raw));
}
template <size_t N, HWY_IF_V_SIZE_LE(float, N, 8)>
HWY_API Vec128<int32_t, N> NearestInt(const Vec128<float, N> v) {
  return Vec128<int32_t, N>(vcvtn_s32_f32(v.raw));
}

HWY_API Vec128<int64_t> NearestInt(const Vec128<double> v) {
  return Vec128<int64_t>(vcvtnq_s64_f64(v.raw));
}

template <size_t N, HWY_IF_V_SIZE_LE(double, N, 8)>
HWY_API Vec128<int64_t, N> NearestInt(const Vec128<double, N> v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 610
  // Workaround for missing vcvtn_s64_f64 intrinsic
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const Twice<decltype(d)> dt;
  return LowerHalf(di, NearestInt(Combine(dt, v, v)));
#else
  return Vec128<int64_t, N>(vcvtn_s64_f64(v.raw));
#endif
}

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 di32,
                                        VFromD<Rebind<double, DI32>> v) {
  return DemoteTo(di32, NearestInt(v));
}

#else

template <size_t N>
HWY_API Vec128<int32_t, N> NearestInt(const Vec128<float, N> v) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  return ConvertTo(di, Round(v));
}

#endif

// ------------------------------ Floating-point classification

#if !HWY_COMPILER_CLANG || HWY_COMPILER_CLANG > 1801 || HWY_ARCH_ARM_V7
template <typename T, size_t N>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  return v != v;
}
#else
// Clang up to 18.1 generates less efficient code than the expected FCMEQ, see
// https://github.com/numpy/numpy/issues/27313 and
// https://github.com/numpy/numpy/pull/22954/files and
// https://github.com/llvm/llvm-project/issues/59855

#if HWY_HAVE_FLOAT16
template <typename T, size_t N, HWY_IF_F16(T), HWY_IF_V_SIZE(T, N, 16)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %0.8h, %1.8h, %1.8h" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}
template <typename T, size_t N, HWY_IF_F16(T), HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %0.4h, %1.4h, %1.4h" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}
#endif  // HWY_HAVE_FLOAT16

template <typename T, size_t N, HWY_IF_F32(T), HWY_IF_V_SIZE(T, N, 16)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %0.4s, %1.4s, %1.4s" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}
template <typename T, size_t N, HWY_IF_F32(T), HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %0.2s, %1.2s, %1.2s" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}

#if HWY_HAVE_FLOAT64
template <typename T, size_t N, HWY_IF_F64(T), HWY_IF_V_SIZE(T, N, 16)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %0.2d, %1.2d, %1.2d" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}
template <typename T, size_t N, HWY_IF_F64(T), HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  typename Mask128<T, N>::Raw ret;
  __asm__ volatile("fcmeq %d0, %d1, %d1" : "=w"(ret) : "w"(v.raw));
  return Not(Mask128<T, N>(ret));
}
#endif  // HWY_HAVE_FLOAT64

#endif  // HWY_COMPILER_CLANG

// ================================================== SWIZZLE

// ------------------------------ LowerHalf

// <= 64 bit: just return different type
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<T, N / 2> LowerHalf(Vec128<T, N> v) {
  return Vec128<T, N / 2>(v.raw);
}

HWY_API Vec64<uint8_t> LowerHalf(Vec128<uint8_t> v) {
  return Vec64<uint8_t>(vget_low_u8(v.raw));
}
HWY_API Vec64<uint16_t> LowerHalf(Vec128<uint16_t> v) {
  return Vec64<uint16_t>(vget_low_u16(v.raw));
}
HWY_API Vec64<uint32_t> LowerHalf(Vec128<uint32_t> v) {
  return Vec64<uint32_t>(vget_low_u32(v.raw));
}
HWY_API Vec64<uint64_t> LowerHalf(Vec128<uint64_t> v) {
  return Vec64<uint64_t>(vget_low_u64(v.raw));
}
HWY_API Vec64<int8_t> LowerHalf(Vec128<int8_t> v) {
  return Vec64<int8_t>(vget_low_s8(v.raw));
}
HWY_API Vec64<int16_t> LowerHalf(Vec128<int16_t> v) {
  return Vec64<int16_t>(vget_low_s16(v.raw));
}
HWY_API Vec64<int32_t> LowerHalf(Vec128<int32_t> v) {
  return Vec64<int32_t>(vget_low_s32(v.raw));
}
HWY_API Vec64<int64_t> LowerHalf(Vec128<int64_t> v) {
  return Vec64<int64_t>(vget_low_s64(v.raw));
}
HWY_API Vec64<float> LowerHalf(Vec128<float> v) {
  return Vec64<float>(vget_low_f32(v.raw));
}
#if HWY_HAVE_FLOAT16
HWY_API Vec64<float16_t> LowerHalf(Vec128<float16_t> v) {
  return Vec64<float16_t>(vget_low_f16(v.raw));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
HWY_API Vec64<bfloat16_t> LowerHalf(Vec128<bfloat16_t> v) {
  return Vec64<bfloat16_t>(vget_low_bf16(v.raw));
}
#endif  // HWY_NEON_HAVE_BFLOAT16
#if HWY_HAVE_FLOAT64
HWY_API Vec64<double> LowerHalf(Vec128<double> v) {
  return Vec64<double>(vget_low_f64(v.raw));
}
#endif  // HWY_HAVE_FLOAT64

template <class V, HWY_NEON_IF_EMULATED_D(DFromV<V>), HWY_IF_V_SIZE_V(V, 16)>
HWY_API VFromD<Half<DFromV<V>>> LowerHalf(V v) {
  const Full128<uint16_t> du;
  const Half<DFromV<V>> dh;
  return BitCast(dh, LowerHalf(BitCast(du, v)));
}

template <class DH>
HWY_API VFromD<DH> LowerHalf(DH /* tag */, VFromD<Twice<DH>> v) {
  return LowerHalf(v);
}

// ------------------------------ CombineShiftRightBytes

// 128-bit
template <int kBytes, class D, typename T = TFromD<D>>
HWY_API Vec128<T> CombineShiftRightBytes(D d, Vec128<T> hi, Vec128<T> lo) {
  static_assert(0 < kBytes && kBytes < 16, "kBytes must be in [1, 15]");
  const Repartition<uint8_t, decltype(d)> d8;
  uint8x16_t v8 = vextq_u8(BitCast(d8, lo).raw, BitCast(d8, hi).raw, kBytes);
  return BitCast(d, Vec128<uint8_t>(v8));
}

// 64-bit
template <int kBytes, class D, typename T = TFromD<D>>
HWY_API Vec64<T> CombineShiftRightBytes(D d, Vec64<T> hi, Vec64<T> lo) {
  static_assert(0 < kBytes && kBytes < 8, "kBytes must be in [1, 7]");
  const Repartition<uint8_t, decltype(d)> d8;
  uint8x8_t v8 = vext_u8(BitCast(d8, lo).raw, BitCast(d8, hi).raw, kBytes);
  return BitCast(d, VFromD<decltype(d8)>(v8));
}

// <= 32-bit defined after ShiftLeftBytes.

// ------------------------------ Shift vector by constant #bytes

namespace detail {

// Partially specialize because kBytes = 0 and >= size are compile errors;
// callers replace the latter with 0xFF for easier specialization.
template <int kBytes>
struct ShiftLeftBytesT {
  // Full
  template <class T>
  HWY_INLINE Vec128<T> operator()(const Vec128<T> v) {
    const Full128<T> d;
    return CombineShiftRightBytes<16 - kBytes>(d, v, Zero(d));
  }

  // Partial
  template <class T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
  HWY_INLINE Vec128<T, N> operator()(const Vec128<T, N> v) {
    // Expand to 64-bit so we only use the native EXT instruction.
    const Full64<T> d64;
    const auto zero64 = Zero(d64);
    const decltype(zero64) v64(v.raw);
    return Vec128<T, N>(
        CombineShiftRightBytes<8 - kBytes>(d64, v64, zero64).raw);
  }
};
template <>
struct ShiftLeftBytesT<0> {
  template <class T, size_t N>
  HWY_INLINE Vec128<T, N> operator()(const Vec128<T, N> v) {
    return v;
  }
};
template <>
struct ShiftLeftBytesT<0xFF> {
  template <class T, size_t N>
  HWY_INLINE Vec128<T, N> operator()(const Vec128<T, N> v) {
    return Xor(v, v);
  }
};

template <int kBytes>
struct ShiftRightBytesT {
  template <class T, size_t N>
  HWY_INLINE Vec128<T, N> operator()(Vec128<T, N> v) {
    const DFromV<decltype(v)> d;
    // For < 64-bit vectors, zero undefined lanes so we shift in zeros.
    if (d.MaxBytes() < 8) {
      constexpr size_t kReg = d.MaxBytes() == 16 ? 16 : 8;
      const Simd<T, kReg / sizeof(T), 0> dreg;
      v = Vec128<T, N>(
          IfThenElseZero(FirstN(dreg, N), VFromD<decltype(dreg)>(v.raw)).raw);
    }
    return CombineShiftRightBytes<kBytes>(d, Zero(d), v);
  }
};
template <>
struct ShiftRightBytesT<0> {
  template <class T, size_t N>
  HWY_INLINE Vec128<T, N> operator()(const Vec128<T, N> v) {
    return v;
  }
};
template <>
struct ShiftRightBytesT<0xFF> {
  template <class T, size_t N>
  HWY_INLINE Vec128<T, N> operator()(const Vec128<T, N> v) {
    return Xor(v, v);
  }
};

}  // namespace detail

template <int kBytes, class D>
HWY_API VFromD<D> ShiftLeftBytes(D d, VFromD<D> v) {
  return detail::ShiftLeftBytesT<(kBytes >= d.MaxBytes() ? 0xFF : kBytes)>()(v);
}

template <int kBytes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftBytes(Vec128<T, N> v) {
  return ShiftLeftBytes<kBytes>(DFromV<decltype(v)>(), v);
}

template <int kLanes, class D>
HWY_API VFromD<D> ShiftLeftLanes(D d, VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftLeftBytes<kLanes * sizeof(TFromD<D>)>(BitCast(d8, v)));
}

template <int kLanes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftLanes(Vec128<T, N> v) {
  return ShiftLeftLanes<kLanes>(DFromV<decltype(v)>(), v);
}

// 0x01..0F, kBytes = 1 => 0x0001..0E
template <int kBytes, class D>
HWY_API VFromD<D> ShiftRightBytes(D d, VFromD<D> v) {
  return detail::ShiftRightBytesT<(kBytes >= d.MaxBytes() ? 0xFF : kBytes)>()(
      v);
}

template <int kLanes, class D>
HWY_API VFromD<D> ShiftRightLanes(D d, VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(
      d, ShiftRightBytes<kLanes * sizeof(TFromD<D>)>(d8, BitCast(d8, v)));
}

// Calls ShiftLeftBytes
template <int kBytes, class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  constexpr size_t kSize = d.MaxBytes();
  static_assert(0 < kBytes && kBytes < kSize, "kBytes invalid");
  const Repartition<uint8_t, decltype(d)> d8;
  const Full64<uint8_t> d_full8;
  const Repartition<TFromD<D>, decltype(d_full8)> d_full;
  using V64 = VFromD<decltype(d_full8)>;
  const V64 hi64(BitCast(d8, hi).raw);
  // Move into most-significant bytes
  const V64 lo64 = ShiftLeftBytes<8 - kSize>(V64(BitCast(d8, lo).raw));
  const V64 r = CombineShiftRightBytes<8 - kSize + kBytes>(d_full8, hi64, lo64);
  // After casting to full 64-bit vector of correct type, shrink to 32-bit
  return VFromD<D>(BitCast(d_full, r).raw);
}

// ------------------------------ UpperHalf (ShiftRightBytes)

// Full input
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> UpperHalf(D /* tag */, Vec128<uint8_t> v) {
  return Vec64<uint8_t>(vget_high_u8(v.raw));
}
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> UpperHalf(D /* tag */, Vec128<uint16_t> v) {
  return Vec64<uint16_t>(vget_high_u16(v.raw));
}
template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> UpperHalf(D /* tag */, Vec128<uint32_t> v) {
  return Vec64<uint32_t>(vget_high_u32(v.raw));
}
template <class D, HWY_IF_U64_D(D)>
HWY_API Vec64<uint64_t> UpperHalf(D /* tag */, Vec128<uint64_t> v) {
  return Vec64<uint64_t>(vget_high_u64(v.raw));
}
template <class D, HWY_IF_I8_D(D)>
HWY_API Vec64<int8_t> UpperHalf(D /* tag */, Vec128<int8_t> v) {
  return Vec64<int8_t>(vget_high_s8(v.raw));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> UpperHalf(D /* tag */, Vec128<int16_t> v) {
  return Vec64<int16_t>(vget_high_s16(v.raw));
}
template <class D, HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> UpperHalf(D /* tag */, Vec128<int32_t> v) {
  return Vec64<int32_t>(vget_high_s32(v.raw));
}
template <class D, HWY_IF_I64_D(D)>
HWY_API Vec64<int64_t> UpperHalf(D /* tag */, Vec128<int64_t> v) {
  return Vec64<int64_t>(vget_high_s64(v.raw));
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_F16_D(D)>
HWY_API Vec64<float16_t> UpperHalf(D /* tag */, Vec128<float16_t> v) {
  return Vec64<float16_t>(vget_high_f16(v.raw));
}
#endif
#if HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_BF16_D(D)>
HWY_API Vec64<bfloat16_t> UpperHalf(D /* tag */, Vec128<bfloat16_t> v) {
  return Vec64<bfloat16_t>(vget_high_bf16(v.raw));
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <class D, HWY_IF_F32_D(D)>
HWY_API Vec64<float> UpperHalf(D /* tag */, Vec128<float> v) {
  return Vec64<float>(vget_high_f32(v.raw));
}
#if HWY_HAVE_FLOAT64
template <class D, HWY_IF_F64_D(D)>
HWY_API Vec64<double> UpperHalf(D /* tag */, Vec128<double> v) {
  return Vec64<double>(vget_high_f64(v.raw));
}
#endif  // HWY_HAVE_FLOAT64

template <class D, HWY_NEON_IF_EMULATED_D(D), HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> UpperHalf(D dh, VFromD<Twice<D>> v) {
  const RebindToUnsigned<Twice<decltype(dh)>> du;
  const Half<decltype(du)> duh;
  return BitCast(dh, UpperHalf(duh, BitCast(du, v)));
}

// Partial
template <class DH, HWY_IF_V_SIZE_LE_D(DH, 4)>
HWY_API VFromD<DH> UpperHalf(DH dh, VFromD<Twice<DH>> v) {
  const Twice<DH> d;
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> upper =
      ShiftRightBytes<dh.MaxBytes()>(du, BitCast(du, v));
  return VFromD<DH>(BitCast(d, upper).raw);
}

// ------------------------------ Broadcast/splat any lane

template <int kLane, typename T>
HWY_API Vec128<T, 1> Broadcast(Vec128<T, 1> v) {
  return v;
}

#if HWY_ARCH_ARM_A64
// Unsigned
template <int kLane>
HWY_API Vec128<uint8_t> Broadcast(Vec128<uint8_t> v) {
  static_assert(0 <= kLane && kLane < 16, "Invalid lane");
  return Vec128<uint8_t>(vdupq_laneq_u8(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint8_t, N> Broadcast(Vec128<uint8_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint8_t, N>(vdup_lane_u8(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint16_t> Broadcast(Vec128<uint16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<uint16_t>(vdupq_laneq_u16(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint16_t, N> Broadcast(Vec128<uint16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint16_t, N>(vdup_lane_u16(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint32_t> Broadcast(Vec128<uint32_t> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<uint32_t>(vdupq_laneq_u32(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint32_t, N> Broadcast(Vec128<uint32_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint32_t, N>(vdup_lane_u32(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint64_t> Broadcast(Vec128<uint64_t> v) {
  static_assert(0 <= kLane && kLane < 2, "Invalid lane");
  return Vec128<uint64_t>(vdupq_laneq_u64(v.raw, kLane));
}

// Signed
template <int kLane>
HWY_API Vec128<int8_t> Broadcast(Vec128<int8_t> v) {
  static_assert(0 <= kLane && kLane < 16, "Invalid lane");
  return Vec128<int8_t>(vdupq_laneq_s8(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int8_t, N> Broadcast(Vec128<int8_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int8_t, N>(vdup_lane_s8(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int16_t> Broadcast(Vec128<int16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<int16_t>(vdupq_laneq_s16(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int16_t, N> Broadcast(Vec128<int16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int16_t, N>(vdup_lane_s16(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int32_t> Broadcast(Vec128<int32_t> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<int32_t>(vdupq_laneq_s32(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int32_t, N> Broadcast(Vec128<int32_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int32_t, N>(vdup_lane_s32(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int64_t> Broadcast(Vec128<int64_t> v) {
  static_assert(0 <= kLane && kLane < 2, "Invalid lane");
  return Vec128<int64_t>(vdupq_laneq_s64(v.raw, kLane));
}

// Float
#if HWY_HAVE_FLOAT16
template <int kLane>
HWY_API Vec128<float16_t> Broadcast(Vec128<float16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<float16_t>(vdupq_laneq_f16(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(float16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float16_t, N> Broadcast(Vec128<float16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<float16_t, N>(vdup_lane_f16(v.raw, kLane));
}
#endif  // HWY_HAVE_FLOAT16

#if HWY_NEON_HAVE_BFLOAT16
template <int kLane>
HWY_API Vec128<bfloat16_t> Broadcast(Vec128<bfloat16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<bfloat16_t>(vdupq_laneq_bf16(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(bfloat16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<bfloat16_t, N> Broadcast(Vec128<bfloat16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<bfloat16_t, N>(vdup_lane_bf16(v.raw, kLane));
}
#endif  // HWY_NEON_HAVE_BFLOAT16

template <int kLane>
HWY_API Vec128<float> Broadcast(Vec128<float> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<float>(vdupq_laneq_f32(v.raw, kLane));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(float, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float, N> Broadcast(Vec128<float, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<float, N>(vdup_lane_f32(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<double> Broadcast(Vec128<double> v) {
  static_assert(0 <= kLane && kLane < 2, "Invalid lane");
  return Vec128<double>(vdupq_laneq_f64(v.raw, kLane));
}

#else  // !HWY_ARCH_ARM_A64
// No vdupq_laneq_* on armv7: use vgetq_lane_* + vdupq_n_*.

// Unsigned
template <int kLane>
HWY_API Vec128<uint8_t> Broadcast(Vec128<uint8_t> v) {
  static_assert(0 <= kLane && kLane < 16, "Invalid lane");
  return Vec128<uint8_t>(vdupq_n_u8(vgetq_lane_u8(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint8_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint8_t, N> Broadcast(Vec128<uint8_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint8_t, N>(vdup_lane_u8(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint16_t> Broadcast(Vec128<uint16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<uint16_t>(vdupq_n_u16(vgetq_lane_u16(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint16_t, N> Broadcast(Vec128<uint16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint16_t, N>(vdup_lane_u16(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint32_t> Broadcast(Vec128<uint32_t> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<uint32_t>(vdupq_n_u32(vgetq_lane_u32(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(uint32_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<uint32_t, N> Broadcast(Vec128<uint32_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<uint32_t, N>(vdup_lane_u32(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<uint64_t> Broadcast(Vec128<uint64_t> v) {
  static_assert(0 <= kLane && kLane < 2, "Invalid lane");
  return Vec128<uint64_t>(vdupq_n_u64(vgetq_lane_u64(v.raw, kLane)));
}

// Signed
template <int kLane>
HWY_API Vec128<int8_t> Broadcast(Vec128<int8_t> v) {
  static_assert(0 <= kLane && kLane < 16, "Invalid lane");
  return Vec128<int8_t>(vdupq_n_s8(vgetq_lane_s8(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int8_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int8_t, N> Broadcast(Vec128<int8_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int8_t, N>(vdup_lane_s8(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int16_t> Broadcast(Vec128<int16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<int16_t>(vdupq_n_s16(vgetq_lane_s16(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int16_t, N> Broadcast(Vec128<int16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int16_t, N>(vdup_lane_s16(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int32_t> Broadcast(Vec128<int32_t> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<int32_t>(vdupq_n_s32(vgetq_lane_s32(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(int32_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int32_t, N> Broadcast(Vec128<int32_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<int32_t, N>(vdup_lane_s32(v.raw, kLane));
}
template <int kLane>
HWY_API Vec128<int64_t> Broadcast(Vec128<int64_t> v) {
  static_assert(0 <= kLane && kLane < 2, "Invalid lane");
  return Vec128<int64_t>(vdupq_n_s64(vgetq_lane_s64(v.raw, kLane)));
}

// Float
#if HWY_HAVE_FLOAT16
template <int kLane>
HWY_API Vec128<float16_t> Broadcast(Vec128<float16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<float16_t>(vdupq_n_f16(vgetq_lane_f16(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(float16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float16_t, N> Broadcast(Vec128<float16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<float16_t, N>(vdup_lane_f16(v.raw, kLane));
}
#endif  // HWY_HAVE_FLOAT16
#if HWY_NEON_HAVE_BFLOAT16
template <int kLane>
HWY_API Vec128<bfloat16_t> Broadcast(Vec128<bfloat16_t> v) {
  static_assert(0 <= kLane && kLane < 8, "Invalid lane");
  return Vec128<bfloat16_t>(vdupq_n_bf16(vgetq_lane_bf16(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(bfloat16_t, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<bfloat16_t, N> Broadcast(Vec128<bfloat16_t, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<bfloat16_t, N>(vdup_lane_bf16(v.raw, kLane));
}
#endif  // HWY_NEON_HAVE_BFLOAT16
template <int kLane>
HWY_API Vec128<float> Broadcast(Vec128<float> v) {
  static_assert(0 <= kLane && kLane < 4, "Invalid lane");
  return Vec128<float>(vdupq_n_f32(vgetq_lane_f32(v.raw, kLane)));
}
template <int kLane, size_t N, HWY_IF_V_SIZE_LE(float, N, 8),
          HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float, N> Broadcast(Vec128<float, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<float, N>(vdup_lane_f32(v.raw, kLane));
}

#endif  // HWY_ARCH_ARM_A64

template <int kLane, typename V, HWY_NEON_IF_EMULATED_D(DFromV<V>),
          HWY_IF_LANES_GT_D(DFromV<V>, 1)>
HWY_API V Broadcast(V v) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Broadcast<kLane>(BitCast(du, v)));
}

// ------------------------------ TableLookupLanes

// Returned by SetTableIndices for use by TableLookupLanes.
template <typename T, size_t N>
struct Indices128 {
  typename detail::Raw128<T, N>::type raw;
};

namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  return Iota(d8, 0);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kBroadcastLaneBytes[16] = {
      0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
  return Load(d8, kBroadcastLaneBytes);
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kBroadcastLaneBytes[16] = {
      0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
  return Load(d8, kBroadcastLaneBytes);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kBroadcastLaneBytes[16] = {
      0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8};
  return Load(d8, kBroadcastLaneBytes);
}

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  return Zero(d8);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kByteOffsets[16] = {
      0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};
  return Load(d8, kByteOffsets);
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kByteOffsets[16] = {
      0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
  return Load(d8, kByteOffsets);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kByteOffsets[16] = {
      0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7};
  return Load(d8, kByteOffsets);
}

}  // namespace detail

template <class D, typename TI, HWY_IF_T_SIZE_D(D, 1)>
HWY_API Indices128<TFromD<D>, MaxLanes(D())> IndicesFromVec(
    D d, Vec128<TI, MaxLanes(D())> vec) {
  using T = TFromD<D>;
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  HWY_DASSERT(AllTrue(
      du, Lt(BitCast(du, vec), Set(du, static_cast<TU>(MaxLanes(d) * 2)))));
#endif

  (void)d;
  return Indices128<TFromD<D>, MaxLanes(D())>{BitCast(d, vec).raw};
}

template <class D, typename TI,
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API Indices128<TFromD<D>, MaxLanes(D())> IndicesFromVec(
    D d, Vec128<TI, MaxLanes(D())> vec) {
  using T = TFromD<D>;
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  HWY_DASSERT(AllTrue(
      du, Lt(BitCast(du, vec), Set(du, static_cast<TU>(MaxLanes(d) * 2)))));
#endif

  const Repartition<uint8_t, decltype(d)> d8;
  using V8 = VFromD<decltype(d8)>;

  // Broadcast each lane index to all bytes of T and shift to bytes
  const V8 lane_indices = TableLookupBytes(
      BitCast(d8, vec), detail::IndicesFromVecBroadcastLaneBytes(d));
  constexpr int kIndexShiftAmt = static_cast<int>(FloorLog2(sizeof(T)));
  const V8 byte_indices = ShiftLeft<kIndexShiftAmt>(lane_indices);
  const V8 sum = Add(byte_indices, detail::IndicesFromVecByteOffsets(d));
  return Indices128<TFromD<D>, MaxLanes(D())>{BitCast(d, sum).raw};
}

template <class D, typename TI>
HWY_API Indices128<TFromD<D>, MaxLanes(D())> SetTableIndices(D d,
                                                             const TI* idx) {
  const Rebind<TI, decltype(d)> di;
  return IndicesFromVec(d, LoadU(di, idx));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  return BitCast(
      d, TableLookupBytes(BitCast(di, v), BitCast(di, Vec128<T, N>{idx.raw})));
}

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 4)>
HWY_API Vec128<T, N> TwoTablesLookupLanes(Vec128<T, N> a, Vec128<T, N> b,
                                          Indices128<T, N> idx) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
// TableLookupLanes currently requires table and index vectors to be the same
// size, though a half-length index vector would be sufficient here.
#if HWY_IS_MSAN
  const Vec128<T, N> idx_vec{idx.raw};
  const Indices128<T, N * 2> idx2{Combine(dt, idx_vec, idx_vec).raw};
#else
  // We only keep LowerHalf of the result, which is valid in idx.
  const Indices128<T, N * 2> idx2{idx.raw};
#endif
  return LowerHalf(d, TableLookupLanes(Combine(dt, b, a), idx2));
}

template <typename T>
HWY_API Vec64<T> TwoTablesLookupLanes(Vec64<T> a, Vec64<T> b,
                                      Indices128<T, 8 / sizeof(T)> idx) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const auto a_u8 = BitCast(du8, a);
  const auto b_u8 = BitCast(du8, b);
  const auto idx_u8 = BitCast(du8, Vec64<T>{idx.raw});

#if HWY_ARCH_ARM_A64
  const Twice<decltype(du8)> dt_u8;
  return BitCast(
      d, Vec64<uint8_t>{vqtbl1_u8(Combine(dt_u8, b_u8, a_u8).raw, idx_u8.raw)});
#else
  detail::Tuple2<uint8_t, du8.MaxLanes()> tup = {{{a_u8.raw, b_u8.raw}}};
  return BitCast(d, Vec64<uint8_t>{vtbl2_u8(tup.raw, idx_u8.raw)});
#endif
}

template <typename T>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T, 16 / sizeof(T)> idx) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const auto a_u8 = BitCast(du8, a);
  const auto b_u8 = BitCast(du8, b);
  const auto idx_u8 = BitCast(du8, Vec128<T>{idx.raw});

#if HWY_ARCH_ARM_A64
  detail::Tuple2<uint8_t, du8.MaxLanes()> tup = {{{a_u8.raw, b_u8.raw}}};
  return BitCast(d, Vec128<uint8_t>{vqtbl2q_u8(tup.raw, idx_u8.raw)});
#else
  const Half<decltype(d)> dh;
  const Repartition<uint8_t, decltype(dh)> dh_u8;
  const auto a_lo_u8 = LowerHalf(dh_u8, a_u8);
  const auto a_hi_u8 = UpperHalf(dh_u8, a_u8);
  const auto b_lo_u8 = LowerHalf(dh_u8, b_u8);
  const auto b_hi_u8 = UpperHalf(dh_u8, b_u8);
  const auto idx_lo_u8 = LowerHalf(dh_u8, idx_u8);
  const auto idx_hi_u8 = UpperHalf(dh_u8, idx_u8);

  detail::Tuple4<uint8_t, dh_u8.MaxLanes()> tup = {
      {{a_lo_u8.raw, a_hi_u8.raw, b_lo_u8.raw, b_hi_u8.raw}}};
  const auto lo_result =
      BitCast(dh, Vec64<uint8_t>{vtbl4_u8(tup.raw, idx_lo_u8.raw)});
  const auto hi_result =
      BitCast(dh, Vec64<uint8_t>{vtbl4_u8(tup.raw, idx_hi_u8.raw)});
  return Combine(d, hi_result, lo_result);
#endif
}

// ------------------------------ Reverse2 (CombineShiftRightBytes)

// Per-target flag to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev16_u8(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> Reverse2(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint8_t>(vrev16q_u8(BitCast(du, v).raw)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev32_u16(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> Reverse2(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint16_t>(vrev32q_u16(BitCast(du, v).raw)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev64_u32(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Reverse2(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint32_t>(vrev64q_u32(BitCast(du, v).raw)));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  return CombineShiftRightBytes<8>(d, v, v);
}

// ------------------------------ Reverse4 (Reverse2)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev32_u8(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> Reverse4(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint8_t>(vrev32q_u8(BitCast(du, v).raw)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev64_u16(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> Reverse4(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint16_t>(vrev64q_u16(BitCast(du, v).raw)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  const RepartitionToWide<RebindToUnsigned<decltype(d)>> duw;
  return BitCast(d, Reverse2(duw, BitCast(duw, Reverse2(d, v))));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse4(D /* tag */, VFromD<D>) {
  HWY_ASSERT(0);  // don't have 8 u64 lanes
}

// ------------------------------ Reverse8 (Reverse2, Reverse4)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse8(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>(vrev64_u8(BitCast(du, v).raw)));
}
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> Reverse8(D d, Vec128<T> v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Vec128<uint8_t>(vrev64q_u8(BitCast(du, v).raw)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse8(D d, VFromD<D> v) {
  const Repartition<uint64_t, decltype(d)> du64;
  return BitCast(d, Reverse2(du64, BitCast(du64, Reverse4(d, v))));
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API VFromD<D> Reverse8(D, VFromD<D>) {
  HWY_ASSERT(0);  // don't have 8 lanes if larger than 16-bit
}

// ------------------------------ Reverse (Reverse2, Reverse4, Reverse8)

template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 1)>
HWY_API Vec128<T, 1> Reverse(D /* tag */, Vec128<T, 1> v) {
  return v;
}

template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 2)>
HWY_API Vec128<T, 2> Reverse(D d, Vec128<T, 2> v) {
  return Reverse2(d, v);
}

template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 4)>
HWY_API Vec128<T, 4> Reverse(D d, Vec128<T, 4> v) {
  return Reverse4(d, v);
}

template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 8)>
HWY_API Vec128<T, 8> Reverse(D d, Vec128<T, 8> v) {
  return Reverse8(d, v);
}

template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 16)>
HWY_API Vec128<T> Reverse(D d, Vec128<T> v) {
  const Repartition<uint64_t, decltype(d)> du64;
  return BitCast(d, Reverse2(du64, BitCast(du64, Reverse8(d, v))));
}

// ------------------------------ ReverseBits

#if HWY_ARCH_ARM_A64

#ifdef HWY_NATIVE_REVERSE_BITS_UI8
#undef HWY_NATIVE_REVERSE_BITS_UI8
#else
#define HWY_NATIVE_REVERSE_BITS_UI8
#endif

HWY_NEON_DEF_FUNCTION_INT_8(ReverseBits, vrbit, _, 1)
HWY_NEON_DEF_FUNCTION_UINT_8(ReverseBits, vrbit, _, 1)

#endif  // HWY_ARCH_ARM_A64

// ------------------------------ Other shuffles (TableLookupBytes)

// Notation: let Vec128<int32_t> have lanes 3,2,1,0 (0 is least-significant).
// Shuffle0321 rotates one lane to the right (the previous least-significant
// lane is now most-significant). These could also be implemented via
// CombineShiftRightBytes but the shuffle_abcd notation is more convenient.

// Swap 64-bit halves
template <typename T>
HWY_API Vec128<T> Shuffle1032(Vec128<T> v) {
  return CombineShiftRightBytes<8>(DFromV<decltype(v)>(), v, v);
}
template <typename T>
HWY_API Vec128<T> Shuffle01(Vec128<T> v) {
  return CombineShiftRightBytes<8>(DFromV<decltype(v)>(), v, v);
}

// Rotate right 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle0321(Vec128<T> v) {
  return CombineShiftRightBytes<4>(DFromV<decltype(v)>(), v, v);
}

// Rotate left 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle2103(Vec128<T> v) {
  return CombineShiftRightBytes<12>(DFromV<decltype(v)>(), v, v);
}

// Reverse
template <typename T>
HWY_API Vec128<T> Shuffle0123(Vec128<T> v) {
  return Reverse4(DFromV<decltype(v)>(), v);
}

// ------------------------------ InterleaveLower

// Interleaves lanes from halves of the 128-bit blocks of "a" (which provides
// the least-significant lane) and "b". To concatenate two half-width integers
// into one, use ZipLower/Upper instead (also works with scalar).
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(InterleaveLower, vzip1, _, 2)
#if HWY_ARCH_ARM_A64
// N=1 makes no sense (in that case, there would be no upper/lower).
HWY_NEON_DEF_FUNCTION_FULL_UIF_64(InterleaveLower, vzip1, _, 2)
#else
// Emulated version for Armv7.
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> InterleaveLower(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  return CombineShiftRightBytes<8>(d, b, Shuffle01(a));
}
#endif

#if !HWY_HAVE_FLOAT16
template <size_t N, HWY_IF_V_SIZE_GT(float16_t, N, 4)>
HWY_API Vec128<float16_t, N> InterleaveLower(Vec128<float16_t, N> a,
                                             Vec128<float16_t, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, InterleaveLower(BitCast(du, a), BitCast(du, b)));
}
#endif  // !HWY_HAVE_FLOAT16

// < 64 bit parts
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 4)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>(InterleaveLower(Vec64<T>(a.raw), Vec64<T>(b.raw)).raw);
}

// Additional overload for the optional Simd<> tag.
template <class D>
HWY_API VFromD<D> InterleaveLower(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveUpper (UpperHalf)

// All functions inside detail lack the required D parameter.
namespace detail {
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(InterleaveUpper, vzip2, _, 2)

#if HWY_ARCH_ARM_A64
// N=1 makes no sense (in that case, there would be no upper/lower).
HWY_NEON_DEF_FUNCTION_FULL_UIF_64(InterleaveUpper, vzip2, _, 2)
#else
// Emulated version for Armv7.
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> InterleaveUpper(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  return CombineShiftRightBytes<8>(d, Shuffle01(b), a);
}
#endif
}  // namespace detail

// Full register
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return detail::InterleaveUpper(a, b);
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> InterleaveUpper(D d, VFromD<D> a, VFromD<D> b) {
  const Half<decltype(d)> d2;
  const VFromD<D> a2(UpperHalf(d2, a).raw);
  const VFromD<D> b2(UpperHalf(d2, b).raw);
  return InterleaveLower(d, a2, b2);
}

// ------------------------------ ZipLower/ZipUpper (InterleaveLower)

// Same as Interleave*, except that the return lanes are double-width integers;
// this is necessary because the single-lane scalar cannot return two values.
template <class V, class DW = RepartitionToWide<DFromV<V>>>
HWY_API VFromD<DW> ZipLower(V a, V b) {
  return BitCast(DW(), InterleaveLower(a, b));
}
template <class V, class D = DFromV<V>, class DW = RepartitionToWide<D>>
HWY_API VFromD<DW> ZipLower(DW dw, V a, V b) {
  return BitCast(dw, InterleaveLower(D(), a, b));
}

template <class V, class D = DFromV<V>, class DW = RepartitionToWide<D>>
HWY_API VFromD<DW> ZipUpper(DW dw, V a, V b) {
  return BitCast(dw, InterleaveUpper(D(), a, b));
}

// ------------------------------ Per4LaneBlockShuffle
namespace detail {

#if HWY_COMPILER_GCC || HWY_COMPILER_CLANG

#ifdef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#undef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#else
#define HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_INLINE VFromD<D> Per4LaneBlkShufDupSet4xU32(D d, const uint32_t /*x3*/,
                                                const uint32_t /*x2*/,
                                                const uint32_t x1,
                                                const uint32_t x0) {
  typedef uint32_t GccU32RawVectType __attribute__((__vector_size__(8)));
  const GccU32RawVectType raw = {x0, x1};
  return ResizeBitCast(d, Vec64<uint32_t>(reinterpret_cast<uint32x2_t>(raw)));
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_INLINE VFromD<D> Per4LaneBlkShufDupSet4xU32(D d, const uint32_t x3,
                                                const uint32_t x2,
                                                const uint32_t x1,
                                                const uint32_t x0) {
  typedef uint32_t GccU32RawVectType __attribute__((__vector_size__(16)));
  const GccU32RawVectType raw = {x0, x1, x2, x3};
  return ResizeBitCast(d, Vec128<uint32_t>(reinterpret_cast<uint32x4_t>(raw)));
}
#endif  // HWY_COMPILER_GCC || HWY_COMPILER_CLANG

template <size_t kLaneSize, size_t kVectSize, class V,
          HWY_IF_LANES_GT_D(DFromV<V>, 4)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<0x88> /*idx_3210_tag*/,
                                  hwy::SizeTag<kLaneSize> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;

  const auto evens = BitCast(dw, ConcatEven(d, v, v));
  return BitCast(d, InterleaveLower(dw, evens, evens));
}

template <size_t kLaneSize, size_t kVectSize, class V,
          HWY_IF_LANES_GT_D(DFromV<V>, 4)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<0xDD> /*idx_3210_tag*/,
                                  hwy::SizeTag<kLaneSize> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;

  const auto odds = BitCast(dw, ConcatOdd(d, v, v));
  return BitCast(d, InterleaveLower(dw, odds, odds));
}

template <class V>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<0xFA> /*idx_3210_tag*/,
                                  hwy::SizeTag<2> /*lane_size_tag*/,
                                  hwy::SizeTag<8> /*vect_size_tag*/, V v) {
  const DFromV<decltype(v)> d;
  return InterleaveUpper(d, v, v);
}

}  // namespace detail

// ------------------------------ SlideUpLanes

namespace detail {

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V SlideUpLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  using TU = UnsignedFromSize<d.MaxBytes()>;
  const Repartition<TU, decltype(d)> du;
  return BitCast(d, BitCast(du, v) << Set(
                        du, static_cast<TU>(amt * sizeof(TFromV<V>) * 8)));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideUpLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const auto idx =
      Iota(du8, static_cast<uint8_t>(size_t{0} - amt * sizeof(TFromV<V>)));
  return BitCast(d, TableLookupBytesOr0(BitCast(du8, v), idx));
}

}  // namespace detail

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> SlideUpLanes(D /*d*/, VFromD<D> v, size_t /*amt*/) {
  return v;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 2)>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftLeftLanes<1>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideUpLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 4)>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftLeftLanes<1>(d, v);
      case 2:
        return ShiftLeftLanes<2>(d, v);
      case 3:
        return ShiftLeftLanes<3>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideUpLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 8)>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftLeftLanes<1>(d, v);
      case 2:
        return ShiftLeftLanes<2>(d, v);
      case 3:
        return ShiftLeftLanes<3>(d, v);
      case 4:
        return ShiftLeftLanes<4>(d, v);
      case 5:
        return ShiftLeftLanes<5>(d, v);
      case 6:
        return ShiftLeftLanes<6>(d, v);
      case 7:
        return ShiftLeftLanes<7>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideUpLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_LANES_D(D, 16)>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftLeftLanes<1>(d, v);
      case 2:
        return ShiftLeftLanes<2>(d, v);
      case 3:
        return ShiftLeftLanes<3>(d, v);
      case 4:
        return ShiftLeftLanes<4>(d, v);
      case 5:
        return ShiftLeftLanes<5>(d, v);
      case 6:
        return ShiftLeftLanes<6>(d, v);
      case 7:
        return ShiftLeftLanes<7>(d, v);
      case 8:
        return ShiftLeftLanes<8>(d, v);
      case 9:
        return ShiftLeftLanes<9>(d, v);
      case 10:
        return ShiftLeftLanes<10>(d, v);
      case 11:
        return ShiftLeftLanes<11>(d, v);
      case 12:
        return ShiftLeftLanes<12>(d, v);
      case 13:
        return ShiftLeftLanes<13>(d, v);
      case 14:
        return ShiftLeftLanes<14>(d, v);
      case 15:
        return ShiftLeftLanes<15>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideUpLanes(v, amt);
}

// ------------------------------ SlideDownLanes

namespace detail {

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V SlideDownLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  using TU = UnsignedFromSize<d.MaxBytes()>;
  const Repartition<TU, decltype(d)> du;
  return BitCast(d,
                 BitCast(du, v) << Set(
                     du, static_cast<TU>(TU{0} - amt * sizeof(TFromV<V>) * 8)));
}

template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideDownLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<int8_t, decltype(d)> di8;
  auto idx = Iota(di8, static_cast<int8_t>(amt * sizeof(TFromV<V>)));
  idx = Or(idx, VecFromMask(di8, idx > Set(di8, int8_t{15})));
  return BitCast(d, TableLookupBytesOr0(BitCast(di8, v), idx));
}

}  // namespace detail

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> SlideDownLanes(D /*d*/, VFromD<D> v, size_t /*amt*/) {
  return v;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 2)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftRightLanes<1>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideDownLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 4)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftRightLanes<1>(d, v);
      case 2:
        return ShiftRightLanes<2>(d, v);
      case 3:
        return ShiftRightLanes<3>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideDownLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_LANES_D(D, 8)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftRightLanes<1>(d, v);
      case 2:
        return ShiftRightLanes<2>(d, v);
      case 3:
        return ShiftRightLanes<3>(d, v);
      case 4:
        return ShiftRightLanes<4>(d, v);
      case 5:
        return ShiftRightLanes<5>(d, v);
      case 6:
        return ShiftRightLanes<6>(d, v);
      case 7:
        return ShiftRightLanes<7>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideDownLanes(v, amt);
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_LANES_D(D, 16)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(amt)) {
    switch (amt) {
      case 0:
        return v;
      case 1:
        return ShiftRightLanes<1>(d, v);
      case 2:
        return ShiftRightLanes<2>(d, v);
      case 3:
        return ShiftRightLanes<3>(d, v);
      case 4:
        return ShiftRightLanes<4>(d, v);
      case 5:
        return ShiftRightLanes<5>(d, v);
      case 6:
        return ShiftRightLanes<6>(d, v);
      case 7:
        return ShiftRightLanes<7>(d, v);
      case 8:
        return ShiftRightLanes<8>(d, v);
      case 9:
        return ShiftRightLanes<9>(d, v);
      case 10:
        return ShiftRightLanes<10>(d, v);
      case 11:
        return ShiftRightLanes<11>(d, v);
      case 12:
        return ShiftRightLanes<12>(d, v);
      case 13:
        return ShiftRightLanes<13>(d, v);
      case 14:
        return ShiftRightLanes<14>(d, v);
      case 15:
        return ShiftRightLanes<15>(d, v);
    }
  }
#else
  (void)d;
#endif

  return detail::SlideDownLanes(v, amt);
}

// ------------------------------- WidenHighMulAdd

#ifdef HWY_NATIVE_WIDEN_HIGH_MUL_ADD
#undef HWY_NATIVE_WIDEN_HIGH_MUL_ADD
#else
#define HWY_NATIVE_WIDEN_HIGH_MUL_ADD
#endif

namespace detail {

template<class D, HWY_IF_U64_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 2)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                   VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<uint64_t>(vmlal_high_u32(add.raw, mul.raw, x.raw));
#else
  const Full64<uint32_t> dh;
  return Vec128<uint64_t>(
      vmlal_u32(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_U64_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_LE_D(DN, 2)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<uint64_t> mulResult = Vec128<uint64_t>(vmull_u32(mul.raw, x.raw));
  return UpperHalf(d, mulResult) + add;
}

template<class D, HWY_IF_I64_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 2)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                   VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<int64_t>(vmlal_high_s32(add.raw, mul.raw, x.raw));
#else
  const Full64<int32_t> dh;
  return Vec128<int64_t>(
      vmlal_s32(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_I64_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_LE_D(DN, 2)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<int64_t> mulResult = Vec128<int64_t>(vmull_s32(mul.raw, x.raw));
  return UpperHalf(d, mulResult) + add;
}

template<class D, HWY_IF_I32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<int32_t>(vmlal_high_s16(add.raw, mul.raw, x.raw));
#else
  const Full64<int16_t> dh;
  return Vec128<int32_t>(
      vmlal_s16(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_I32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<int32_t> widen = Vec128<int32_t>(vmull_s16(mul.raw, x.raw));
  Vec64<int32_t> hi = UpperHalf(d, widen);
  return hi + add;
}

template<class D, HWY_IF_I32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 2)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<int32_t> widen = Vec128<int32_t>(vmull_s16(mul.raw, x.raw));
  Vec32<int32_t> hi = UpperHalf(d, Vec64<int32_t>(vget_high_s32(widen.raw)));
  return hi + add;
}

template<class D, HWY_IF_U32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                   VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<uint32_t>(vmlal_high_u16(add.raw, mul.raw, x.raw));
#else
  const Full64<uint16_t> dh;
  return Vec128<uint32_t>(
      vmlal_u16(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_U32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                   VFromD<DN> x, VFromD<D> add) {
  Vec128<uint32_t> widen = Vec128<uint32_t>(vmull_u16(mul.raw, x.raw));
  VFromD<D> hi = UpperHalf(d, widen);
  return hi + add;
}

template<class D, HWY_IF_U32_D(D), HWY_IF_LANES_D(D, 1),
         class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<uint32_t> widen = Vec128<uint32_t>(vmull_u16(mul.raw, x.raw));
  VFromD<D> hi = UpperHalf(d, Vec64<uint32_t>(vget_high_u32(widen.raw)));
  return hi + add;
}

template<class D, HWY_IF_U16_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 8)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                   VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<uint16_t>(vmlal_high_u8(add.raw, mul.raw, x.raw));
#else
  const Full64<uint8_t> dh;
  return Vec128<uint16_t>(
      vmlal_u8(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_U16_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 8)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<uint16_t> widen = Vec128<uint16_t>(vmull_u8(mul.raw, x.raw));
  VFromD<D> hi = UpperHalf(d, widen);
  return hi + add;
}

template<class D, HWY_IF_U16(TFromD<D>), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_LE_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<uint16_t> widen = Vec128<uint16_t>(vmull_u8(mul.raw, x.raw));
  const Twice<decltype(d)> d16F;
  VFromD<D> hi = UpperHalf(d, VFromD<decltype(d16F)>(vget_high_u16(widen.raw)));
  return hi + add;
}

template<class D, HWY_IF_I16_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_GT_D(DN, 8)>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
#if HWY_ARCH_ARM_A64
  return Vec128<int16_t>(vmlal_high_s8(add.raw, mul.raw, x.raw));
#else
  const Full64<int8_t> dh;
  return Vec128<int16_t>(
      vmlal_s8(add.raw, UpperHalf(dh, mul).raw, UpperHalf(dh, x).raw));
#endif
}

template<class D, HWY_IF_I16_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 8)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<int16_t> widen = Vec128<int16_t>(vmull_s8(mul.raw, x.raw));
  VFromD<D> hi = UpperHalf(d, widen);
  return hi + add;
}

template<class D, HWY_IF_I16_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_LE_D(DN, 4)>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  Vec128<int16_t> widen = Vec128<int16_t>(vmull_s8(mul.raw, x.raw));
  const Twice<decltype(d)> d16F;
  VFromD<D> hi = UpperHalf(d, VFromD<decltype(d16F)>(vget_high_s16(widen.raw)));
  return hi + add;
}

#if 0
#if HWY_HAVE_FLOAT16
template<class D, HWY_IF_F32_D(D), HWY_IF_LANES_D(D, 4),
         class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  return VFromD<D>(vfmlalq_high_f16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_F32_D(D), HWY_IF_LANES_D(D, 2),
         class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenHighMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  return Vec64<float32_t>(vfmlal_high_f16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_F32_D(D), HWY_IF_LANES_D(D, 1),
         class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenHighMulAdd(D d, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  return MulAdd(add, PromoteUpperTo(d, mul), PromoteUpperTo(d, x));
}
#endif
#endif

}  // namespace detail

// ------------------------------- WidenMulAdd

#ifdef HWY_NATIVE_WIDEN_MUL_ADD
#undef HWY_NATIVE_WIDEN_MUL_ADD
#else
#define HWY_NATIVE_WIDEN_MUL_ADD
#endif

namespace detail {

template<class D, HWY_IF_U16_D(D), HWY_IF_LANES_GT_D(D, 4),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  return Vec128<uint16_t>(vmlal_u8(add.raw, mul.raw, x.raw));
}

template <class D, HWY_IF_U16_D(D), HWY_IF_LANES_LE_D(D, 4),
          class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D d, VFromD<DN> mul, VFromD<DN> x,
                              VFromD<D> add) {
  return MulAdd(add, PromoteTo(d, mul), PromoteTo(d, x));
}

template<class D, HWY_IF_I16_D(D), HWY_IF_LANES_GT_D(D, 4),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  return VFromD<D>(vmlal_s8(add.raw, mul.raw, x.raw));
}

template <class D, HWY_IF_I16_D(D), HWY_IF_LANES_LE_D(D, 4),
          class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D d, VFromD<DN> mul, VFromD<DN> x,
                              VFromD<D> add) {
  return MulAdd(add, PromoteTo(d, mul), PromoteTo(d, x));
}

template<class D, HWY_IF_I32_D(D),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_GT_D(DN, 2)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  return Vec128<int32_t>(vmlal_s16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_I32_D(D),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_D(DN, 2)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  Vec128<int32_t> mulRs = Vec128<int32_t>(vmull_s16(mul.raw, x.raw));
  const VFromD<D> mul10 = LowerHalf(mulRs);
  return add + mul10;
}

template<class D, HWY_IF_I32_D(D),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                               VFromD<DN> x, VFromD<D> add) {
  Vec64<int32_t> mulRs = LowerHalf(Vec128<int32_t>(vmull_s16(mul.raw, x.raw)));
  const Vec32<int32_t> mul10(LowerHalf(mulRs));
  return add + mul10;
}

template<class D, HWY_IF_U32_D(D), HWY_IF_LANES_GT_D(D, 2),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  return Vec128<uint32_t>(vmlal_u16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_U32_D(D), HWY_IF_LANES_D(D, 2),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  Vec128<uint32_t> mulRs = Vec128<uint32_t>(vmull_u16(mul.raw, x.raw));
  const Vec64<uint32_t> mul10(LowerHalf(mulRs));
  return add + mul10;
}

template<class D, HWY_IF_U32_D(D), HWY_IF_LANES_D(D, 1),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  Vec64<uint32_t> mulRs =
      LowerHalf(Vec128<uint32_t>(vmull_u16(mul.raw, x.raw)));
  const Vec32<uint32_t> mul10(LowerHalf(mulRs));
  return add + mul10;
}

template<class D, HWY_IF_I64_D(D), class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_D(DN, 2)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                               VFromD<DN> x, VFromD<D> add) {
  return VFromD<D>(vmlal_s32(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_I64_D(D), HWY_IF_LANES_D(D, 1),
         class DN = Rebind<MakeNarrow<TFromD<D>>, D>>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  Vec128<int64_t> mulRs = Vec128<int64_t>(vmull_s32(mul.raw, x.raw));
  const VFromD<D> mul10(LowerHalf(mulRs));
  return add + mul10;
}

template<class D, HWY_IF_U64_D(D), class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_D(DN, 2)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  return VFromD<D>(vmlal_u32(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_U64_D(D), class DN = Rebind<MakeNarrow<TFromD<D>>, D>,
         HWY_IF_LANES_D(DN, 1)>
HWY_API VFromD<D> WidenMulAdd(D /* tag */, VFromD<DN> mul,
                              VFromD<DN> x, VFromD<D> add) {
  Vec128<uint64_t> mulRs = Vec128<uint64_t>(vmull_u32(mul.raw, x.raw));
  const VFromD<D> mul10(LowerHalf(mulRs));
  return add + mul10;
}

#if 0
#if HWY_HAVE_FLOAT16
template<class D, HWY_IF_F32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(D, 4)>
HWY_API VFromD<D> WidenLowMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  return VFromD<D>(vfmlalq_low_f16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_F32_D(D), class DN = RepartitionToNarrow<D>,
         HWY_IF_LANES_D(DN, 4)>
HWY_API VFromD<D> WidenLowMulAdd(D /* tag */, VFromD<DN> mul,
                                  VFromD<DN> x, VFromD<D> add) {
  return Vec64<float32_t>(vfmlal_low_f16(add.raw, mul.raw, x.raw));
}

template<class D, HWY_IF_F32_D(D), HWY_IF_LANES_D(D, 1),
         class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenLowMulAdd(D d, VFromD<DN> mul,
                                 VFromD<DN> x, VFromD<D> add) {
  return MulAdd(add, PromoteLowerTo(d, mul), PromoteLowerTo(d, x));
}
#endif
#endif

}  // namespace detail

// ------------------------------ WidenMulAccumulate

#ifdef HWY_NATIVE_WIDEN_MUL_ACCUMULATE
#undef HWY_NATIVE_WIDEN_MUL_ACCUMULATE
#else
#define HWY_NATIVE_WIDEN_MUL_ACCUMULATE
#endif

template<class D, HWY_IF_INTEGER(TFromD<D>), class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenMulAccumulate(D d, VFromD<DN> mul, VFromD<DN> x,
                                     VFromD<D> low, VFromD<D>& high) {
  high = detail::WidenHighMulAdd(d, mul, x, high);
  return detail::WidenMulAdd(d, LowerHalf(mul), LowerHalf(x), low);
}

#if 0
#ifdef HWY_NATIVE_WIDEN_MUL_ACCUMULATE_F16
#undef HWY_NATIVE_WIDEN_MUL_ACCUMULATE_F16
#else
#define HWY_NATIVE_WIDEN_MUL_ACCUMULATE_F16
#endif

#if HWY_HAVE_FLOAT16

template<class D, HWY_IF_F32_D(D), class DN = RepartitionToNarrow<D>>
HWY_API VFromD<D> WidenMulAccumulate(D d, VFromD<DN> mul, VFromD<DN> x,
                                     VFromD<D> low, VFromD<D>& high) {
  high = detail::WidenHighMulAdd(d, mul, x, high);
  return detail::WidenLowMulAdd(d, mul, x, low);
}

#endif
#endif

// ------------------------------ SatWidenMulAccumFixedPoint

#ifdef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#undef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#else
#define HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#endif

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_D(DI32, 16)>
HWY_API VFromD<DI32> SatWidenMulAccumFixedPoint(DI32 /*di32*/,
                                                VFromD<Rebind<int16_t, DI32>> a,
                                                VFromD<Rebind<int16_t, DI32>> b,
                                                VFromD<DI32> sum) {
  return VFromD<DI32>(vqdmlal_s16(sum.raw, a.raw, b.raw));
}

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 8)>
HWY_API VFromD<DI32> SatWidenMulAccumFixedPoint(DI32 di32,
                                                VFromD<Rebind<int16_t, DI32>> a,
                                                VFromD<Rebind<int16_t, DI32>> b,
                                                VFromD<DI32> sum) {
  const Full128<TFromD<DI32>> di32_full;
  const Rebind<int16_t, decltype(di32_full)> di16_full64;
  return ResizeBitCast(
      di32, SatWidenMulAccumFixedPoint(di32_full, ResizeBitCast(di16_full64, a),
                                       ResizeBitCast(di16_full64, b),
                                       ResizeBitCast(di32_full, sum)));
}

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

#if HWY_NEON_HAVE_F32_TO_BF16C

#ifdef HWY_NATIVE_MUL_EVEN_BF16
#undef HWY_NATIVE_MUL_EVEN_BF16
#else
#define HWY_NATIVE_MUL_EVEN_BF16
#endif

#ifdef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#undef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#else
#define HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#endif

namespace detail {
#if HWY_NEON_HAVE_BFLOAT16
// If HWY_NEON_HAVE_BFLOAT16 is true, detail::Vec128<bfloat16_t, N>::type is
// bfloat16x4_t or bfloat16x8_t.
static HWY_INLINE bfloat16x4_t BitCastToRawNeonBF16(bfloat16x4_t raw) {
  return raw;
}
static HWY_INLINE bfloat16x8_t BitCastToRawNeonBF16(bfloat16x8_t raw) {
  return raw;
}
#else
// If HWY_NEON_HAVE_F32_TO_BF16C && !HWY_NEON_HAVE_BFLOAT16 is true,
// detail::Vec128<bfloat16_t, N>::type is uint16x4_t or uint16x8_t vector to
// work around compiler bugs that are there with GCC 13 or earlier or Clang 16
// or earlier on AArch64.

// The uint16x4_t or uint16x8_t vector neets to be bitcasted to a bfloat16x4_t
// or a bfloat16x8_t vector for the vbfdot_f32 and vbfdotq_f32 intrinsics if
// HWY_NEON_HAVE_F32_TO_BF16C && !HWY_NEON_HAVE_BFLOAT16 is true
static HWY_INLINE bfloat16x4_t BitCastToRawNeonBF16(uint16x4_t raw) {
  return vreinterpret_bf16_u16(raw);
}
static HWY_INLINE bfloat16x8_t BitCastToRawNeonBF16(uint16x8_t raw) {
  return vreinterpretq_bf16_u16(raw);
}
#endif
}  // namespace detail

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API Vec128<float> MulEvenAdd(D /*d32*/, Vec128<bfloat16_t> a,
                                 Vec128<bfloat16_t> b, const Vec128<float> c) {
  return Vec128<float>(vbfmlalbq_f32(c.raw, detail::BitCastToRawNeonBF16(a.raw),
                                   detail::BitCastToRawNeonBF16(b.raw)));
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API Vec128<float> MulOddAdd(D /*d32*/, Vec128<bfloat16_t> a,
                                 Vec128<bfloat16_t> b, const Vec128<float> c) {
  return Vec128<float>(vbfmlaltq_f32(c.raw, detail::BitCastToRawNeonBF16(a.raw),
                                   detail::BitCastToRawNeonBF16(b.raw)));
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API Vec128<float> ReorderWidenMulAccumulate(D /*d32*/, Vec128<bfloat16_t> a,
                                                Vec128<bfloat16_t> b,
                                                const Vec128<float> sum0,
                                                Vec128<float>& /*sum1*/) {
  return Vec128<float>(vbfdotq_f32(sum0.raw,
                                   detail::BitCastToRawNeonBF16(a.raw),
                                   detail::BitCastToRawNeonBF16(b.raw)));
}

// There is no non-q version of these instructions.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> MulEvenAdd(D d32, VFromD<Repartition<bfloat16_t, D>> a,
                             VFromD<Repartition<bfloat16_t, D>> b,
                             const VFromD<D> c) {
  const Full128<float> d32f;
  const Full128<bfloat16_t> d16f;
  return ResizeBitCast(
      d32, MulEvenAdd(d32f, ResizeBitCast(d16f, a), ResizeBitCast(d16f, b),
                      ResizeBitCast(d32f, c)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> MulOddAdd(D d32, VFromD<Repartition<bfloat16_t, D>> a,
                            VFromD<Repartition<bfloat16_t, D>> b,
                            const VFromD<D> c) {
  const Full128<float> d32f;
  const Full128<bfloat16_t> d16f;
  return ResizeBitCast(
      d32, MulOddAdd(d32f, ResizeBitCast(d16f, a), ResizeBitCast(d16f, b),
                     ResizeBitCast(d32f, c)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderWidenMulAccumulate(
    D /*d32*/, VFromD<Repartition<bfloat16_t, D>> a,
    VFromD<Repartition<bfloat16_t, D>> b, const VFromD<D> sum0,
    VFromD<D>& /*sum1*/) {
  return VFromD<D>(vbfdot_f32(sum0.raw, detail::BitCastToRawNeonBF16(a.raw),
                              detail::BitCastToRawNeonBF16(b.raw)));
}

#endif  // HWY_NEON_HAVE_F32_TO_BF16C

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> ReorderWidenMulAccumulate(D /*d32*/, Vec128<int16_t> a,
                                                  Vec128<int16_t> b,
                                                  const Vec128<int32_t> sum0,
                                                  Vec128<int32_t>& sum1) {
#if HWY_ARCH_ARM_A64
  sum1 = Vec128<int32_t>(vmlal_high_s16(sum1.raw, a.raw, b.raw));
#else
  const Full64<int16_t> dh;
  sum1 = Vec128<int32_t>(
      vmlal_s16(sum1.raw, UpperHalf(dh, a).raw, UpperHalf(dh, b).raw));
#endif
  return Vec128<int32_t>(
      vmlal_s16(sum0.raw, LowerHalf(a).raw, LowerHalf(b).raw));
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> ReorderWidenMulAccumulate(D d32, Vec64<int16_t> a,
                                                 Vec64<int16_t> b,
                                                 const Vec64<int32_t> sum0,
                                                 Vec64<int32_t>& sum1) {
  // vmlal writes into the upper half, which the caller cannot use, so
  // split into two halves.
  const Vec128<int32_t> mul_3210(vmull_s16(a.raw, b.raw));
  const Vec64<int32_t> mul_32 = UpperHalf(d32, mul_3210);
  sum1 += mul_32;
  return sum0 + LowerHalf(mul_3210);
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec32<int32_t> ReorderWidenMulAccumulate(D d32, Vec32<int16_t> a,
                                                 Vec32<int16_t> b,
                                                 const Vec32<int32_t> sum0,
                                                 Vec32<int32_t>& sum1) {
  const Vec128<int32_t> mul_xx10(vmull_s16(a.raw, b.raw));
  const Vec64<int32_t> mul_10(LowerHalf(mul_xx10));
  const Vec32<int32_t> mul0 = LowerHalf(d32, mul_10);
  const Vec32<int32_t> mul1 = UpperHalf(d32, mul_10);
  sum1 += mul1;
  return sum0 + mul0;
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> ReorderWidenMulAccumulate(D /*d32*/,
                                                   Vec128<uint16_t> a,
                                                   Vec128<uint16_t> b,
                                                   const Vec128<uint32_t> sum0,
                                                   Vec128<uint32_t>& sum1) {
#if HWY_ARCH_ARM_A64
  sum1 = Vec128<uint32_t>(vmlal_high_u16(sum1.raw, a.raw, b.raw));
#else
  const Full64<uint16_t> dh;
  sum1 = Vec128<uint32_t>(
      vmlal_u16(sum1.raw, UpperHalf(dh, a).raw, UpperHalf(dh, b).raw));
#endif
  return Vec128<uint32_t>(
      vmlal_u16(sum0.raw, LowerHalf(a).raw, LowerHalf(b).raw));
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> ReorderWidenMulAccumulate(D d32, Vec64<uint16_t> a,
                                                  Vec64<uint16_t> b,
                                                  const Vec64<uint32_t> sum0,
                                                  Vec64<uint32_t>& sum1) {
  // vmlal writes into the upper half, which the caller cannot use, so
  // split into two halves.
  const Vec128<uint32_t> mul_3210(vmull_u16(a.raw, b.raw));
  const Vec64<uint32_t> mul_32 = UpperHalf(d32, mul_3210);
  sum1 += mul_32;
  return sum0 + LowerHalf(mul_3210);
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> ReorderWidenMulAccumulate(D du32, Vec32<uint16_t> a,
                                                  Vec32<uint16_t> b,
                                                  const Vec32<uint32_t> sum0,
                                                  Vec32<uint32_t>& sum1) {
  const Vec128<uint32_t> mul_xx10(vmull_u16(a.raw, b.raw));
  const Vec64<uint32_t> mul_10(LowerHalf(mul_xx10));
  const Vec32<uint32_t> mul0 = LowerHalf(du32, mul_10);
  const Vec32<uint32_t> mul1 = UpperHalf(du32, mul_10);
  sum1 += mul1;
  return sum0 + mul0;
}

// ------------------------------ Combine partial (InterleaveLower)
// < 64bit input, <= 64 bit result
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Combine(D d, VFromD<Half<D>> hi, VFromD<Half<D>> lo) {
  // First double N (only lower halves will be used).
  const VFromD<D> hi2(hi.raw);
  const VFromD<D> lo2(lo.raw);
  // Repartition to two unsigned lanes (each the size of the valid input).
  const Simd<UnsignedFromSize<d.MaxBytes() / 2>, 2, 0> du;
  return BitCast(d, InterleaveLower(BitCast(du, lo2), BitCast(du, hi2)));
}

// ------------------------------ RearrangeToOddPlusEven (Combine)

template <size_t N>
HWY_API Vec128<float, N> RearrangeToOddPlusEven(Vec128<float, N> sum0,
                                                Vec128<float, N> sum1) {
#if HWY_NEON_HAVE_BFLOAT16
  (void)sum1;  // unused by bf16 ReorderWidenMulAccumulate
  return sum0;
#else
  return Add(sum0, sum1);
#endif
}

HWY_API Vec128<int32_t> RearrangeToOddPlusEven(Vec128<int32_t> sum0,
                                               Vec128<int32_t> sum1) {
// vmlal_s16 multiplied the lower half into sum0 and upper into sum1.
#if HWY_ARCH_ARM_A64  // pairwise sum is available and what we want
  return Vec128<int32_t>(vpaddq_s32(sum0.raw, sum1.raw));
#else
  const Full128<int32_t> d;
  const Half<decltype(d)> d64;
  const Vec64<int32_t> hi(
      vpadd_s32(LowerHalf(d64, sum1).raw, UpperHalf(d64, sum1).raw));
  const Vec64<int32_t> lo(
      vpadd_s32(LowerHalf(d64, sum0).raw, UpperHalf(d64, sum0).raw));
  return Combine(Full128<int32_t>(), hi, lo);
#endif
}

HWY_API Vec64<int32_t> RearrangeToOddPlusEven(Vec64<int32_t> sum0,
                                              Vec64<int32_t> sum1) {
  // vmlal_s16 multiplied the lower half into sum0 and upper into sum1.
  return Vec64<int32_t>(vpadd_s32(sum0.raw, sum1.raw));
}

HWY_API Vec32<int32_t> RearrangeToOddPlusEven(Vec32<int32_t> sum0,
                                              Vec32<int32_t> sum1) {
  // Only one widened sum per register, so add them for sum of odd and even.
  return sum0 + sum1;
}

HWY_API Vec128<uint32_t> RearrangeToOddPlusEven(Vec128<uint32_t> sum0,
                                                Vec128<uint32_t> sum1) {
// vmlal_s16 multiplied the lower half into sum0 and upper into sum1.
#if HWY_ARCH_ARM_A64  // pairwise sum is available and what we want
  return Vec128<uint32_t>(vpaddq_u32(sum0.raw, sum1.raw));
#else
  const Full128<uint32_t> d;
  const Half<decltype(d)> d64;
  const Vec64<uint32_t> hi(
      vpadd_u32(LowerHalf(d64, sum1).raw, UpperHalf(d64, sum1).raw));
  const Vec64<uint32_t> lo(
      vpadd_u32(LowerHalf(d64, sum0).raw, UpperHalf(d64, sum0).raw));
  return Combine(Full128<uint32_t>(), hi, lo);
#endif
}

HWY_API Vec64<uint32_t> RearrangeToOddPlusEven(Vec64<uint32_t> sum0,
                                               Vec64<uint32_t> sum1) {
  // vmlal_u16 multiplied the lower half into sum0 and upper into sum1.
  return Vec64<uint32_t>(vpadd_u32(sum0.raw, sum1.raw));
}

HWY_API Vec32<uint32_t> RearrangeToOddPlusEven(Vec32<uint32_t> sum0,
                                               Vec32<uint32_t> sum1) {
  // Only one widened sum per register, so add them for sum of odd and even.
  return sum0 + sum1;
}

// ------------------------------ SumOfMulQuadAccumulate

#if HWY_TARGET == HWY_NEON_BF16

#ifdef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 8)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(DI32 /*di32*/,
                                            VFromD<Repartition<int8_t, DI32>> a,
                                            VFromD<Repartition<int8_t, DI32>> b,
                                            VFromD<DI32> sum) {
  return VFromD<DI32>(vdot_s32(sum.raw, a.raw, b.raw));
}

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_D(DI32, 16)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(DI32 /*di32*/,
                                            VFromD<Repartition<int8_t, DI32>> a,
                                            VFromD<Repartition<int8_t, DI32>> b,
                                            VFromD<DI32> sum) {
  return VFromD<DI32>(vdotq_s32(sum.raw, a.raw, b.raw));
}

#ifdef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#endif

template <class DU32, HWY_IF_U32_D(DU32), HWY_IF_V_SIZE_LE_D(DU32, 8)>
HWY_API VFromD<DU32> SumOfMulQuadAccumulate(
    DU32 /*du32*/, VFromD<Repartition<uint8_t, DU32>> a,
    VFromD<Repartition<uint8_t, DU32>> b, VFromD<DU32> sum) {
  return VFromD<DU32>(vdot_u32(sum.raw, a.raw, b.raw));
}

template <class DU32, HWY_IF_U32_D(DU32), HWY_IF_V_SIZE_D(DU32, 16)>
HWY_API VFromD<DU32> SumOfMulQuadAccumulate(
    DU32 /*du32*/, VFromD<Repartition<uint8_t, DU32>> a,
    VFromD<Repartition<uint8_t, DU32>> b, VFromD<DU32> sum) {
  return VFromD<DU32>(vdotq_u32(sum.raw, a.raw, b.raw));
}

#ifdef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(
    DI32 di32, VFromD<Repartition<uint8_t, DI32>> a_u,
    VFromD<Repartition<int8_t, DI32>> b_i, VFromD<DI32> sum) {
  // TODO: use vusdot[q]_s32 on NEON targets that require support for NEON I8MM

  const RebindToUnsigned<decltype(di32)> du32;
  const Repartition<uint8_t, decltype(di32)> du8;

  const auto b_u = BitCast(du8, b_i);
  const auto result_sum0 =
      SumOfMulQuadAccumulate(du32, a_u, b_u, BitCast(du32, sum));
  const auto result_sum1 = ShiftLeft<8>(
      SumOfMulQuadAccumulate(du32, a_u, ShiftRight<7>(b_u), Zero(du32)));

  return BitCast(di32, Sub(result_sum0, result_sum1));
}

#endif  // HWY_TARGET == HWY_NEON_BF16

// ------------------------------ WidenMulPairwiseAdd

#if HWY_NEON_HAVE_F32_TO_BF16C

template <class DF, HWY_IF_V_SIZE_D(DF, 16)>
HWY_API Vec128<float> WidenMulPairwiseAdd(DF df, Vec128<bfloat16_t> a,
                                          Vec128<bfloat16_t> b) {
  return Vec128<float>(vbfdotq_f32(Zero(df).raw,
                                   detail::BitCastToRawNeonBF16(a.raw),
                                   detail::BitCastToRawNeonBF16(b.raw)));
}

template <class DF, HWY_IF_V_SIZE_LE_D(DF, 8)>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df,
                                       VFromD<Repartition<bfloat16_t, DF>> a,
                                       VFromD<Repartition<bfloat16_t, DF>> b) {
  return VFromD<DF>(vbfdot_f32(Zero(df).raw,
                               detail::BitCastToRawNeonBF16(a.raw),
                               detail::BitCastToRawNeonBF16(b.raw)));
}

#else
template <class DF, HWY_IF_F32_D(DF)>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df,
                                       VFromD<Repartition<bfloat16_t, DF>> a,
                                       VFromD<Repartition<bfloat16_t, DF>> b) {
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
}
#endif  // HWY_NEON_HAVE_F32_TO_BF16C

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> WidenMulPairwiseAdd(D /*d32*/, Vec128<int16_t> a,
                                            Vec128<int16_t> b) {
  Vec128<int32_t> sum1;
#if HWY_ARCH_ARM_A64
  sum1 = Vec128<int32_t>(vmull_high_s16(a.raw, b.raw));
#else
  const Full64<int16_t> dh;
  sum1 = Vec128<int32_t>(vmull_s16(UpperHalf(dh, a).raw, UpperHalf(dh, b).raw));
#endif
  Vec128<int32_t> sum0 =
      Vec128<int32_t>(vmull_s16(LowerHalf(a).raw, LowerHalf(b).raw));
  return RearrangeToOddPlusEven(sum0, sum1);
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> WidenMulPairwiseAdd(D d32, Vec64<int16_t> a,
                                           Vec64<int16_t> b) {
  // vmlal writes into the upper half, which the caller cannot use, so
  // split into two halves.
  const Vec128<int32_t> mul_3210(vmull_s16(a.raw, b.raw));
  const Vec64<int32_t> mul0 = LowerHalf(mul_3210);
  const Vec64<int32_t> mul1 = UpperHalf(d32, mul_3210);
  return RearrangeToOddPlusEven(mul0, mul1);
}

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec32<int32_t> WidenMulPairwiseAdd(D d32, Vec32<int16_t> a,
                                           Vec32<int16_t> b) {
  const Vec128<int32_t> mul_xx10(vmull_s16(a.raw, b.raw));
  const Vec64<int32_t> mul_10(LowerHalf(mul_xx10));
  const Vec32<int32_t> mul0 = LowerHalf(d32, mul_10);
  const Vec32<int32_t> mul1 = UpperHalf(d32, mul_10);
  return RearrangeToOddPlusEven(mul0, mul1);
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> WidenMulPairwiseAdd(D /*d32*/, Vec128<uint16_t> a,
                                             Vec128<uint16_t> b) {
  Vec128<uint32_t> sum1;
#if HWY_ARCH_ARM_A64
  sum1 = Vec128<uint32_t>(vmull_high_u16(a.raw, b.raw));
#else
  const Full64<uint16_t> dh;
  sum1 =
      Vec128<uint32_t>(vmull_u16(UpperHalf(dh, a).raw, UpperHalf(dh, b).raw));
#endif
  Vec128<uint32_t> sum0 =
      Vec128<uint32_t>(vmull_u16(LowerHalf(a).raw, LowerHalf(b).raw));
  return RearrangeToOddPlusEven(sum0, sum1);
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> WidenMulPairwiseAdd(D d32, Vec64<uint16_t> a,
                                            Vec64<uint16_t> b) {
  // vmlal writes into the upper half, which the caller cannot use, so
  // split into two halves.
  const Vec128<uint32_t> mul_3210(vmull_u16(a.raw, b.raw));
  const Vec64<uint32_t> mul0 = LowerHalf(mul_3210);
  const Vec64<uint32_t> mul1 = UpperHalf(d32, mul_3210);
  return RearrangeToOddPlusEven(mul0, mul1);
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> WidenMulPairwiseAdd(D d32, Vec32<uint16_t> a,
                                            Vec32<uint16_t> b) {
  const Vec128<uint32_t> mul_xx10(vmull_u16(a.raw, b.raw));
  const Vec64<uint32_t> mul_10(LowerHalf(mul_xx10));
  const Vec32<uint32_t> mul0 = LowerHalf(d32, mul_10);
  const Vec32<uint32_t> mul1 = UpperHalf(d32, mul_10);
  return RearrangeToOddPlusEven(mul0, mul1);
}

// ------------------------------ ZeroExtendVector (Combine)

template <class D>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  return Combine(d, Zero(Half<decltype(d)>()), lo);
}

// ------------------------------ ConcatLowerLower

// 64 or 128-bit input: just interleave
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> ConcatLowerLower(D d, VFromD<D> hi, VFromD<D> lo) {
  // Treat half-width input as a single lane and interleave them.
  const Repartition<UnsignedFromSize<d.MaxBytes() / 2>, decltype(d)> du;
  return BitCast(d, InterleaveLower(BitCast(du, lo), BitCast(du, hi)));
}

namespace detail {
#if HWY_ARCH_ARM_A64
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(InterleaveEven, vtrn1, _, 2)
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(InterleaveOdd, vtrn2, _, 2)
#else

// vtrn returns a struct with even and odd result.
#define HWY_NEON_BUILD_TPL_HWY_TRN
#define HWY_NEON_BUILD_RET_HWY_TRN(type, size) type##x##size##x2_t
// Pass raw args so we can accept uint16x2 args, for which there is no
// corresponding uint16x2x2 return type.
#define HWY_NEON_BUILD_PARAM_HWY_TRN(TYPE, size) \
  Raw128<TYPE##_t, size>::type a, Raw128<TYPE##_t, size>::type b
#define HWY_NEON_BUILD_ARG_HWY_TRN a, b

// Cannot use UINT8 etc. type macros because the x2_t tuples are only defined
// for full and half vectors.
HWY_NEON_DEF_FUNCTION(uint8, 16, InterleaveEvenOdd, vtrnq, _, u8, HWY_TRN)
HWY_NEON_DEF_FUNCTION(uint8, 8, InterleaveEvenOdd, vtrn, _, u8, HWY_TRN)
HWY_NEON_DEF_FUNCTION(uint16, 8, InterleaveEvenOdd, vtrnq, _, u16, HWY_TRN)
HWY_NEON_DEF_FUNCTION(uint16, 4, InterleaveEvenOdd, vtrn, _, u16, HWY_TRN)
HWY_NEON_DEF_FUNCTION(uint32, 4, InterleaveEvenOdd, vtrnq, _, u32, HWY_TRN)
HWY_NEON_DEF_FUNCTION(uint32, 2, InterleaveEvenOdd, vtrn, _, u32, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int8, 16, InterleaveEvenOdd, vtrnq, _, s8, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int8, 8, InterleaveEvenOdd, vtrn, _, s8, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int16, 8, InterleaveEvenOdd, vtrnq, _, s16, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int16, 4, InterleaveEvenOdd, vtrn, _, s16, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int32, 4, InterleaveEvenOdd, vtrnq, _, s32, HWY_TRN)
HWY_NEON_DEF_FUNCTION(int32, 2, InterleaveEvenOdd, vtrn, _, s32, HWY_TRN)
HWY_NEON_DEF_FUNCTION(float32, 4, InterleaveEvenOdd, vtrnq, _, f32, HWY_TRN)
HWY_NEON_DEF_FUNCTION(float32, 2, InterleaveEvenOdd, vtrn, _, f32, HWY_TRN)

#undef HWY_NEON_BUILD_TPL_HWY_TRN
#undef HWY_NEON_BUILD_RET_HWY_TRN
#undef HWY_NEON_BUILD_PARAM_HWY_TRN
#undef HWY_NEON_BUILD_ARG_HWY_TRN

#endif  // HWY_ARCH_ARM_A64
}  // namespace detail

// <= 32-bit input/output
template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> ConcatLowerLower(D d, VFromD<D> hi, VFromD<D> lo) {
  // Treat half-width input as two lanes and take every second one.
  const Repartition<UnsignedFromSize<d.MaxBytes() / 2>, decltype(d)> du;
#if HWY_ARCH_ARM_A64
  return BitCast(d, detail::InterleaveEven(BitCast(du, lo), BitCast(du, hi)));
#else
  using VU = VFromD<decltype(du)>;
  return BitCast(
      d, VU(detail::InterleaveEvenOdd(BitCast(du, lo).raw, BitCast(du, hi).raw)
                .val[0]));
#endif
}

// ------------------------------ ConcatUpperUpper

// 64 or 128-bit input: just interleave
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> ConcatUpperUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  // Treat half-width input as a single lane and interleave them.
  const Repartition<UnsignedFromSize<d.MaxBytes() / 2>, decltype(d)> du;
  return BitCast(d, InterleaveUpper(du, BitCast(du, lo), BitCast(du, hi)));
}

// <= 32-bit input/output
template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> ConcatUpperUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  // Treat half-width input as two lanes and take every second one.
  const Repartition<UnsignedFromSize<d.MaxBytes() / 2>, decltype(d)> du;
#if HWY_ARCH_ARM_A64
  return BitCast(d, detail::InterleaveOdd(BitCast(du, lo), BitCast(du, hi)));
#else
  using VU = VFromD<decltype(du)>;
  return BitCast(
      d, VU(detail::InterleaveEvenOdd(BitCast(du, lo).raw, BitCast(du, hi).raw)
                .val[1]));
#endif
}

// ------------------------------ ConcatLowerUpper (ShiftLeftBytes)

// 64 or 128-bit input: extract from concatenated
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> ConcatLowerUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  return CombineShiftRightBytes<d.MaxBytes() / 2>(d, hi, lo);
}

// <= 32-bit input/output
template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> ConcatLowerUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  constexpr size_t kSize = d.MaxBytes();
  const Repartition<uint8_t, decltype(d)> d8;
  const Full64<uint8_t> d8x8;
  const Full64<TFromD<D>> d64;
  using V8x8 = VFromD<decltype(d8x8)>;
  const V8x8 hi8x8(BitCast(d8, hi).raw);
  // Move into most-significant bytes
  const V8x8 lo8x8 = ShiftLeftBytes<8 - kSize>(V8x8(BitCast(d8, lo).raw));
  const V8x8 r = CombineShiftRightBytes<8 - kSize / 2>(d8x8, hi8x8, lo8x8);
  // Back to original lane type, then shrink N.
  return VFromD<D>(BitCast(d64, r).raw);
}

// ------------------------------ ConcatUpperLower

// Works for all N.
template <class D>
HWY_API VFromD<D> ConcatUpperLower(D d, VFromD<D> hi, VFromD<D> lo) {
  return IfThenElse(FirstN(d, Lanes(d) / 2), lo, hi);
}

// ------------------------------ ConcatOdd (InterleaveUpper)

namespace detail {
// There is no vuzpq_u64.
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(ConcatEven, vuzp1, _, 2)
HWY_NEON_DEF_FUNCTION_UIF_8_16_32(ConcatOdd, vuzp2, _, 2)

#if !HWY_HAVE_FLOAT16
template <size_t N>
HWY_INLINE Vec128<float16_t, N> ConcatEven(Vec128<float16_t, N> hi,
                                           Vec128<float16_t, N> lo) {
  const DFromV<decltype(hi)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, ConcatEven(BitCast(du, hi), BitCast(du, lo)));
}
template <size_t N>
HWY_INLINE Vec128<float16_t, N> ConcatOdd(Vec128<float16_t, N> hi,
                                          Vec128<float16_t, N> lo) {
  const DFromV<decltype(hi)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, ConcatOdd(BitCast(du, hi), BitCast(du, lo)));
}
#endif  // !HWY_HAVE_FLOAT16
}  // namespace detail

// Full/half vector
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> ConcatOdd(D /* tag */, VFromD<D> hi, VFromD<D> lo) {
  return detail::ConcatOdd(lo, hi);
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatOdd(D d, Vec32<T> hi, Vec32<T> lo) {
  const Twice<decltype(d)> d2;
  const Repartition<uint16_t, decltype(d2)> dw2;
  const VFromD<decltype(d2)> hi2(hi.raw);
  const VFromD<decltype(d2)> lo2(lo.raw);
  const VFromD<decltype(dw2)> Hx1Lx1 = BitCast(dw2, ConcatOdd(d2, hi2, lo2));
  // Compact into two pairs of u8, skipping the invalid x lanes. Could also use
  // vcopy_lane_u16, but that's A64-only.
  return Vec32<T>(BitCast(d2, ConcatEven(dw2, Hx1Lx1, Hx1Lx1)).raw);
}

// Any type x2
template <class D, HWY_IF_LANES_D(D, 2), typename T = TFromD<D>>
HWY_API Vec128<T, 2> ConcatOdd(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveUpper(d, lo, hi);
}

// ------------------------------ ConcatEven (InterleaveLower)

// Full/half vector
template <class D, HWY_IF_V_SIZE_GT_D(D, 4)>
HWY_API VFromD<D> ConcatEven(D /* tag */, VFromD<D> hi, VFromD<D> lo) {
  return detail::ConcatEven(lo, hi);
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatEven(D d, Vec32<T> hi, Vec32<T> lo) {
  const Twice<decltype(d)> d2;
  const Repartition<uint16_t, decltype(d2)> dw2;
  const VFromD<decltype(d2)> hi2(hi.raw);
  const VFromD<decltype(d2)> lo2(lo.raw);
  const VFromD<decltype(dw2)> Hx0Lx0 = BitCast(dw2, ConcatEven(d2, hi2, lo2));
  // Compact into two pairs of u8, skipping the invalid x lanes. Could also use
  // vcopy_lane_u16, but that's A64-only.
  return Vec32<T>(BitCast(d2, ConcatEven(dw2, Hx0Lx0, Hx0Lx0)).raw);
}

// Any type x2
template <class D, HWY_IF_LANES_D(D, 2), typename T = TFromD<D>>
HWY_API Vec128<T, 2> ConcatEven(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveLower(d, lo, hi);
}

// ------------------------------ DupEven (InterleaveLower)

template <typename T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
#if HWY_ARCH_ARM_A64
  return detail::InterleaveEven(v, v);
#else
  return Vec128<T, N>(detail::InterleaveEvenOdd(v.raw, v.raw).val[0]);
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  return InterleaveLower(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ DupOdd (InterleaveUpper)

template <typename T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
#if HWY_ARCH_ARM_A64
  return detail::InterleaveOdd(v, v);
#else
  return Vec128<T, N>(detail::InterleaveEvenOdd(v.raw, v.raw).val[1]);
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return InterleaveUpper(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ OddEven (IfThenElse)

template <typename T, size_t N>
HWY_API Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t kBytes[16] = {
      ((0 / sizeof(T)) & 1) ? 0 : 0xFF,  ((1 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((2 / sizeof(T)) & 1) ? 0 : 0xFF,  ((3 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((4 / sizeof(T)) & 1) ? 0 : 0xFF,  ((5 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((6 / sizeof(T)) & 1) ? 0 : 0xFF,  ((7 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((8 / sizeof(T)) & 1) ? 0 : 0xFF,  ((9 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((10 / sizeof(T)) & 1) ? 0 : 0xFF, ((11 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((12 / sizeof(T)) & 1) ? 0 : 0xFF, ((13 / sizeof(T)) & 1) ? 0 : 0xFF,
      ((14 / sizeof(T)) & 1) ? 0 : 0xFF, ((15 / sizeof(T)) & 1) ? 0 : 0xFF,
  };
  const auto vec = BitCast(d, Load(d8, kBytes));
  return IfThenElse(MaskFromVec(vec), b, a);
}

// ------------------------------ InterleaveEven
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
#if HWY_ARCH_ARM_A64
  return detail::InterleaveEven(a, b);
#else
  return VFromD<D>(detail::InterleaveEvenOdd(a.raw, b.raw).val[0]);
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveOdd
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
#if HWY_ARCH_ARM_A64
  return detail::InterleaveOdd(a, b);
#else
  return VFromD<D>(detail::InterleaveEvenOdd(a.raw, b.raw).val[1]);
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  return InterleaveUpper(d, a, b);
}

// ------------------------------ OddEvenBlocks
template <typename T, size_t N>
HWY_API Vec128<T, N> OddEvenBlocks(Vec128<T, N> /* odd */, Vec128<T, N> even) {
  return even;
}

// ------------------------------ SwapAdjacentBlocks
template <typename T, size_t N>
HWY_API Vec128<T, N> SwapAdjacentBlocks(Vec128<T, N> v) {
  return v;
}

// ------------------------------ ReverseBlocks
// Single block: no change
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ReverseBlocks(D /* tag */, VFromD<D> v) {
  return v;
}

// ------------------------------ ReorderDemote2To (OddEven)

#if HWY_NEON_HAVE_F32_TO_BF16C
template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dbf16, VFromD<Repartition<float, D>> a,
                                   VFromD<Repartition<float, D>> b) {
  const Half<decltype(dbf16)> dh_bf16;
  return Combine(dbf16, DemoteTo(dh_bf16, b), DemoteTo(dh_bf16, a));
}
#endif  // HWY_NEON_HAVE_F32_TO_BF16C

template <class D, HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> ReorderDemote2To(D d32, Vec128<int64_t> a,
                                         Vec128<int64_t> b) {
  const Vec64<int32_t> a32(vqmovn_s64(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d32;
  return Vec128<int32_t>(vqmovn_high_s64(a32.raw, b.raw));
#else
  const Vec64<int32_t> b32(vqmovn_s64(b.raw));
  return Combine(d32, b32, a32);
#endif
}

template <class D, HWY_IF_I32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d32, VFromD<Repartition<int64_t, D>> a,
                                   VFromD<Repartition<int64_t, D>> b) {
  const Rebind<int64_t, decltype(d32)> dt;
  return DemoteTo(d32, Combine(dt, b, a));
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> ReorderDemote2To(D d32, Vec128<int64_t> a,
                                          Vec128<int64_t> b) {
  const Vec64<uint32_t> a32(vqmovun_s64(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d32;
  return Vec128<uint32_t>(vqmovun_high_s64(a32.raw, b.raw));
#else
  const Vec64<uint32_t> b32(vqmovun_s64(b.raw));
  return Combine(d32, b32, a32);
#endif
}

template <class D, HWY_IF_U32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d32, VFromD<Repartition<int64_t, D>> a,
                                   VFromD<Repartition<int64_t, D>> b) {
  const Rebind<int64_t, decltype(d32)> dt;
  return DemoteTo(d32, Combine(dt, b, a));
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> ReorderDemote2To(D d32, Vec128<uint64_t> a,
                                          Vec128<uint64_t> b) {
  const Vec64<uint32_t> a32(vqmovn_u64(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d32;
  return Vec128<uint32_t>(vqmovn_high_u64(a32.raw, b.raw));
#else
  const Vec64<uint32_t> b32(vqmovn_u64(b.raw));
  return Combine(d32, b32, a32);
#endif
}

template <class D, HWY_IF_U32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d32, VFromD<Repartition<uint64_t, D>> a,
                                   VFromD<Repartition<uint64_t, D>> b) {
  const Rebind<uint64_t, decltype(d32)> dt;
  return DemoteTo(d32, Combine(dt, b, a));
}

template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> ReorderDemote2To(D d16, Vec128<int32_t> a,
                                         Vec128<int32_t> b) {
  const Vec64<int16_t> a16(vqmovn_s32(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d16;
  return Vec128<int16_t>(vqmovn_high_s32(a16.raw, b.raw));
#else
  const Vec64<int16_t> b16(vqmovn_s32(b.raw));
  return Combine(d16, b16, a16);
#endif
}

template <class D, HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> ReorderDemote2To(D /*d16*/, Vec64<int32_t> a,
                                        Vec64<int32_t> b) {
  const Full128<int32_t> d32;
  const Vec128<int32_t> ab = Combine(d32, b, a);
  return Vec64<int16_t>(vqmovn_s32(ab.raw));
}

template <class D, HWY_IF_I16_D(D)>
HWY_API Vec32<int16_t> ReorderDemote2To(D /*d16*/, Vec32<int32_t> a,
                                        Vec32<int32_t> b) {
  const Full128<int32_t> d32;
  const Vec64<int32_t> ab(vzip1_s32(a.raw, b.raw));
  return Vec32<int16_t>(vqmovn_s32(Combine(d32, ab, ab).raw));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> ReorderDemote2To(D d16, Vec128<int32_t> a,
                                          Vec128<int32_t> b) {
  const Vec64<uint16_t> a16(vqmovun_s32(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d16;
  return Vec128<uint16_t>(vqmovun_high_s32(a16.raw, b.raw));
#else
  const Vec64<uint16_t> b16(vqmovun_s32(b.raw));
  return Combine(d16, b16, a16);
#endif
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> ReorderDemote2To(D /*d16*/, Vec64<int32_t> a,
                                         Vec64<int32_t> b) {
  const Full128<int32_t> d32;
  const Vec128<int32_t> ab = Combine(d32, b, a);
  return Vec64<uint16_t>(vqmovun_s32(ab.raw));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> ReorderDemote2To(D /*d16*/, Vec32<int32_t> a,
                                         Vec32<int32_t> b) {
  const Full128<int32_t> d32;
  const Vec64<int32_t> ab(vzip1_s32(a.raw, b.raw));
  return Vec32<uint16_t>(vqmovun_s32(Combine(d32, ab, ab).raw));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> ReorderDemote2To(D d16, Vec128<uint32_t> a,
                                          Vec128<uint32_t> b) {
  const Vec64<uint16_t> a16(vqmovn_u32(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d16;
  return Vec128<uint16_t>(vqmovn_high_u32(a16.raw, b.raw));
#else
  const Vec64<uint16_t> b16(vqmovn_u32(b.raw));
  return Combine(d16, b16, a16);
#endif
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> ReorderDemote2To(D /*d16*/, Vec64<uint32_t> a,
                                         Vec64<uint32_t> b) {
  const Full128<uint32_t> d32;
  const Vec128<uint32_t> ab = Combine(d32, b, a);
  return Vec64<uint16_t>(vqmovn_u32(ab.raw));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> ReorderDemote2To(D /*d16*/, Vec32<uint32_t> a,
                                         Vec32<uint32_t> b) {
  const Full128<uint32_t> d32;
  const Vec64<uint32_t> ab(vzip1_u32(a.raw, b.raw));
  return Vec32<uint16_t>(vqmovn_u32(Combine(d32, ab, ab).raw));
}

template <class D, HWY_IF_I8_D(D)>
HWY_API Vec128<int8_t> ReorderDemote2To(D d8, Vec128<int16_t> a,
                                        Vec128<int16_t> b) {
  const Vec64<int8_t> a8(vqmovn_s16(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d8;
  return Vec128<int8_t>(vqmovn_high_s16(a8.raw, b.raw));
#else
  const Vec64<int8_t> b8(vqmovn_s16(b.raw));
  return Combine(d8, b8, a8);
#endif
}

template <class D, HWY_IF_I8_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d8, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const Rebind<int16_t, decltype(d8)> dt;
  return DemoteTo(d8, Combine(dt, b, a));
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> ReorderDemote2To(D d8, Vec128<int16_t> a,
                                         Vec128<int16_t> b) {
  const Vec64<uint8_t> a8(vqmovun_s16(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d8;
  return Vec128<uint8_t>(vqmovun_high_s16(a8.raw, b.raw));
#else
  const Vec64<uint8_t> b8(vqmovun_s16(b.raw));
  return Combine(d8, b8, a8);
#endif
}

template <class D, HWY_IF_U8_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d8, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const Rebind<int16_t, decltype(d8)> dt;
  return DemoteTo(d8, Combine(dt, b, a));
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> ReorderDemote2To(D d8, Vec128<uint16_t> a,
                                         Vec128<uint16_t> b) {
  const Vec64<uint8_t> a8(vqmovn_u16(a.raw));
#if HWY_ARCH_ARM_A64
  (void)d8;
  return Vec128<uint8_t>(vqmovn_high_u16(a8.raw, b.raw));
#else
  const Vec64<uint8_t> b8(vqmovn_u16(b.raw));
  return Combine(d8, b8, a8);
#endif
}

template <class D, HWY_IF_U8_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ReorderDemote2To(D d8, VFromD<Repartition<uint16_t, D>> a,
                                   VFromD<Repartition<uint16_t, D>> b) {
  const Rebind<uint16_t, decltype(d8)> dt;
  return DemoteTo(d8, Combine(dt, b, a));
}

template <class D, class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D d, V a, V b) {
  return ReorderDemote2To(d, a, b);
}

#if HWY_NEON_HAVE_F32_TO_BF16C
template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> OrderedDemote2To(D dbf16, VFromD<Repartition<float, D>> a,
                                   VFromD<Repartition<float, D>> b) {
  return ReorderDemote2To(dbf16, a, b);
}
#endif  // HWY_NEON_HAVE_F32_TO_BF16C

// ================================================== CRYPTO

// (aarch64 or Arm7) and (__ARM_FEATURE_AES or HWY_HAVE_RUNTIME_DISPATCH).
// Otherwise, rely on generic_ops-inl.h to emulate AESRound / CLMul*.
#if HWY_TARGET != HWY_NEON_WITHOUT_AES

#ifdef HWY_NATIVE_AES
#undef HWY_NATIVE_AES
#else
#define HWY_NATIVE_AES
#endif

HWY_API Vec128<uint8_t> AESRound(Vec128<uint8_t> state,
                                 Vec128<uint8_t> round_key) {
  // NOTE: it is important that AESE and AESMC be consecutive instructions so
  // they can be fused. AESE includes AddRoundKey, which is a different ordering
  // than the AES-NI semantics we adopted, so XOR by 0 and later with the actual
  // round key (the compiler will hopefully optimize this for multiple rounds).
  return Vec128<uint8_t>(vaesmcq_u8(vaeseq_u8(state.raw, vdupq_n_u8(0)))) ^
         round_key;
}

HWY_API Vec128<uint8_t> AESLastRound(Vec128<uint8_t> state,
                                     Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>(vaeseq_u8(state.raw, vdupq_n_u8(0))) ^ round_key;
}

HWY_API Vec128<uint8_t> AESInvMixColumns(Vec128<uint8_t> state) {
  return Vec128<uint8_t>{vaesimcq_u8(state.raw)};
}

HWY_API Vec128<uint8_t> AESRoundInv(Vec128<uint8_t> state,
                                    Vec128<uint8_t> round_key) {
  // NOTE: it is important that AESD and AESIMC be consecutive instructions so
  // they can be fused. AESD includes AddRoundKey, which is a different ordering
  // than the AES-NI semantics we adopted, so XOR by 0 and later with the actual
  // round key (the compiler will hopefully optimize this for multiple rounds).
  return Vec128<uint8_t>(vaesimcq_u8(vaesdq_u8(state.raw, vdupq_n_u8(0)))) ^
         round_key;
}

HWY_API Vec128<uint8_t> AESLastRoundInv(Vec128<uint8_t> state,
                                        Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>(vaesdq_u8(state.raw, vdupq_n_u8(0))) ^ round_key;
}

HWY_API Vec128<uint64_t> CLMulLower(Vec128<uint64_t> a, Vec128<uint64_t> b) {
  return Vec128<uint64_t>((uint64x2_t)vmull_p64(GetLane(a), GetLane(b)));
}

HWY_API Vec128<uint64_t> CLMulUpper(Vec128<uint64_t> a, Vec128<uint64_t> b) {
  return Vec128<uint64_t>(
      (uint64x2_t)vmull_high_p64((poly64x2_t)a.raw, (poly64x2_t)b.raw));
}

#endif  // HWY_TARGET != HWY_NEON_WITHOUT_AES

// ================================================== MISC

template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D df32, VFromD<Rebind<bfloat16_t, D>> v) {
  const Rebind<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteTo(di32, BitCast(du16, v))));
}

// ------------------------------ Truncations

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_UNSIGNED(TFrom), HWY_IF_UNSIGNED(TTo),
          hwy::EnableIf<(sizeof(TTo) < sizeof(TFrom))>* = nullptr>
HWY_API Vec128<TTo, 1> TruncateTo(DTo /* tag */, Vec128<TFrom, 1> v) {
  const Repartition<TTo, DFromV<decltype(v)>> d;
  return Vec128<TTo, 1>{BitCast(d, v).raw};
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec16<uint8_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  const auto v3 = detail::ConcatEven(v2, v2);
  const auto v4 = detail::ConcatEven(v3, v3);
  return LowerHalf(LowerHalf(LowerHalf(v4)));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Repartition<uint16_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  const auto v3 = detail::ConcatEven(v2, v2);
  return LowerHalf(LowerHalf(v3));
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Repartition<uint32_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  return LowerHalf(v2);
}

template <class D, HWY_IF_U8_D(D), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  const auto v3 = detail::ConcatEven(v2, v2);
  return LowerHalf(LowerHalf(v3));
}

template <class D, HWY_IF_U16_D(D), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const Repartition<uint16_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  return LowerHalf(v2);
}

template <class D, HWY_IF_U8_D(D), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = detail::ConcatEven(v1, v1);
  return LowerHalf(v2);
}

// ------------------------------ MulEven (ConcatEven)

// Multiplies even lanes (0, 2 ..) and places the double-wide result into
// even and the upper half into its odd neighbor lane.
HWY_API Vec128<int16_t> MulEven(Vec128<int8_t> a, Vec128<int8_t> b) {
  const DFromV<decltype(a)> d;
  int8x16_t a_packed = ConcatEven(d, a, a).raw;
  int8x16_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int16_t>(
      vmull_s8(vget_low_s8(a_packed), vget_low_s8(b_packed)));
}
HWY_API Vec128<uint16_t> MulEven(Vec128<uint8_t> a, Vec128<uint8_t> b) {
  const DFromV<decltype(a)> d;
  uint8x16_t a_packed = ConcatEven(d, a, a).raw;
  uint8x16_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint16_t>(
      vmull_u8(vget_low_u8(a_packed), vget_low_u8(b_packed)));
}
HWY_API Vec128<int32_t> MulEven(Vec128<int16_t> a, Vec128<int16_t> b) {
  const DFromV<decltype(a)> d;
  int16x8_t a_packed = ConcatEven(d, a, a).raw;
  int16x8_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int32_t>(
      vmull_s16(vget_low_s16(a_packed), vget_low_s16(b_packed)));
}
HWY_API Vec128<uint32_t> MulEven(Vec128<uint16_t> a, Vec128<uint16_t> b) {
  const DFromV<decltype(a)> d;
  uint16x8_t a_packed = ConcatEven(d, a, a).raw;
  uint16x8_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint32_t>(
      vmull_u16(vget_low_u16(a_packed), vget_low_u16(b_packed)));
}
HWY_API Vec128<int64_t> MulEven(Vec128<int32_t> a, Vec128<int32_t> b) {
  const DFromV<decltype(a)> d;
  int32x4_t a_packed = ConcatEven(d, a, a).raw;
  int32x4_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int64_t>(
      vmull_s32(vget_low_s32(a_packed), vget_low_s32(b_packed)));
}
HWY_API Vec128<uint64_t> MulEven(Vec128<uint32_t> a, Vec128<uint32_t> b) {
  const DFromV<decltype(a)> d;
  uint32x4_t a_packed = ConcatEven(d, a, a).raw;
  uint32x4_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint64_t>(
      vmull_u32(vget_low_u32(a_packed), vget_low_u32(b_packed)));
}

template <size_t N>
HWY_API Vec128<int16_t, (N + 1) / 2> MulEven(Vec128<int8_t, N> a,
                                             Vec128<int8_t, N> b) {
  const DFromV<decltype(a)> d;
  int8x8_t a_packed = ConcatEven(d, a, a).raw;
  int8x8_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int16_t, (N + 1) / 2>(
      vget_low_s16(vmull_s8(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint16_t, (N + 1) / 2> MulEven(Vec128<uint8_t, N> a,
                                              Vec128<uint8_t, N> b) {
  const DFromV<decltype(a)> d;
  uint8x8_t a_packed = ConcatEven(d, a, a).raw;
  uint8x8_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint16_t, (N + 1) / 2>(
      vget_low_u16(vmull_u8(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<int32_t, (N + 1) / 2> MulEven(Vec128<int16_t, N> a,
                                             Vec128<int16_t, N> b) {
  const DFromV<decltype(a)> d;
  int16x4_t a_packed = ConcatEven(d, a, a).raw;
  int16x4_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int32_t, (N + 1) / 2>(
      vget_low_s32(vmull_s16(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint32_t, (N + 1) / 2> MulEven(Vec128<uint16_t, N> a,
                                              Vec128<uint16_t, N> b) {
  const DFromV<decltype(a)> d;
  uint16x4_t a_packed = ConcatEven(d, a, a).raw;
  uint16x4_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint32_t, (N + 1) / 2>(
      vget_low_u32(vmull_u16(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<int64_t, (N + 1) / 2> MulEven(Vec128<int32_t, N> a,
                                             Vec128<int32_t, N> b) {
  const DFromV<decltype(a)> d;
  int32x2_t a_packed = ConcatEven(d, a, a).raw;
  int32x2_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<int64_t, (N + 1) / 2>(
      vget_low_s64(vmull_s32(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint64_t, (N + 1) / 2> MulEven(Vec128<uint32_t, N> a,
                                              Vec128<uint32_t, N> b) {
  const DFromV<decltype(a)> d;
  uint32x2_t a_packed = ConcatEven(d, a, a).raw;
  uint32x2_t b_packed = ConcatEven(d, b, b).raw;
  return Vec128<uint64_t, (N + 1) / 2>(
      vget_low_u64(vmull_u32(a_packed, b_packed)));
}

template <class T, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T> MulEven(Vec128<T> a, Vec128<T> b) {
  T hi;
  T lo = Mul128(GetLane(a), GetLane(b), &hi);
  return Dup128VecFromValues(Full128<T>(), lo, hi);
}

// Multiplies odd lanes (1, 3 ..) and places the double-wide result into
// even and the upper half into its odd neighbor lane.
HWY_API Vec128<int16_t> MulOdd(Vec128<int8_t> a, Vec128<int8_t> b) {
  const DFromV<decltype(a)> d;
  int8x16_t a_packed = ConcatOdd(d, a, a).raw;
  int8x16_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int16_t>(
      vmull_s8(vget_low_s8(a_packed), vget_low_s8(b_packed)));
}
HWY_API Vec128<uint16_t> MulOdd(Vec128<uint8_t> a, Vec128<uint8_t> b) {
  const DFromV<decltype(a)> d;
  uint8x16_t a_packed = ConcatOdd(d, a, a).raw;
  uint8x16_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint16_t>(
      vmull_u8(vget_low_u8(a_packed), vget_low_u8(b_packed)));
}
HWY_API Vec128<int32_t> MulOdd(Vec128<int16_t> a, Vec128<int16_t> b) {
  const DFromV<decltype(a)> d;
  int16x8_t a_packed = ConcatOdd(d, a, a).raw;
  int16x8_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int32_t>(
      vmull_s16(vget_low_s16(a_packed), vget_low_s16(b_packed)));
}
HWY_API Vec128<uint32_t> MulOdd(Vec128<uint16_t> a, Vec128<uint16_t> b) {
  const DFromV<decltype(a)> d;
  uint16x8_t a_packed = ConcatOdd(d, a, a).raw;
  uint16x8_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint32_t>(
      vmull_u16(vget_low_u16(a_packed), vget_low_u16(b_packed)));
}
HWY_API Vec128<int64_t> MulOdd(Vec128<int32_t> a, Vec128<int32_t> b) {
  const DFromV<decltype(a)> d;
  int32x4_t a_packed = ConcatOdd(d, a, a).raw;
  int32x4_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int64_t>(
      vmull_s32(vget_low_s32(a_packed), vget_low_s32(b_packed)));
}
HWY_API Vec128<uint64_t> MulOdd(Vec128<uint32_t> a, Vec128<uint32_t> b) {
  const DFromV<decltype(a)> d;
  uint32x4_t a_packed = ConcatOdd(d, a, a).raw;
  uint32x4_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint64_t>(
      vmull_u32(vget_low_u32(a_packed), vget_low_u32(b_packed)));
}

template <size_t N>
HWY_API Vec128<int16_t, (N + 1) / 2> MulOdd(Vec128<int8_t, N> a,
                                            Vec128<int8_t, N> b) {
  const DFromV<decltype(a)> d;
  int8x8_t a_packed = ConcatOdd(d, a, a).raw;
  int8x8_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int16_t, (N + 1) / 2>(
      vget_low_s16(vmull_s8(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint16_t, (N + 1) / 2> MulOdd(Vec128<uint8_t, N> a,
                                             Vec128<uint8_t, N> b) {
  const DFromV<decltype(a)> d;
  uint8x8_t a_packed = ConcatOdd(d, a, a).raw;
  uint8x8_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint16_t, (N + 1) / 2>(
      vget_low_u16(vmull_u8(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<int32_t, (N + 1) / 2> MulOdd(Vec128<int16_t, N> a,
                                            Vec128<int16_t, N> b) {
  const DFromV<decltype(a)> d;
  int16x4_t a_packed = ConcatOdd(d, a, a).raw;
  int16x4_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int32_t, (N + 1) / 2>(
      vget_low_s32(vmull_s16(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint32_t, (N + 1) / 2> MulOdd(Vec128<uint16_t, N> a,
                                             Vec128<uint16_t, N> b) {
  const DFromV<decltype(a)> d;
  uint16x4_t a_packed = ConcatOdd(d, a, a).raw;
  uint16x4_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint32_t, (N + 1) / 2>(
      vget_low_u32(vmull_u16(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<int64_t, (N + 1) / 2> MulOdd(Vec128<int32_t, N> a,
                                            Vec128<int32_t, N> b) {
  const DFromV<decltype(a)> d;
  int32x2_t a_packed = ConcatOdd(d, a, a).raw;
  int32x2_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<int64_t, (N + 1) / 2>(
      vget_low_s64(vmull_s32(a_packed, b_packed)));
}
template <size_t N>
HWY_API Vec128<uint64_t, (N + 1) / 2> MulOdd(Vec128<uint32_t, N> a,
                                             Vec128<uint32_t, N> b) {
  const DFromV<decltype(a)> d;
  uint32x2_t a_packed = ConcatOdd(d, a, a).raw;
  uint32x2_t b_packed = ConcatOdd(d, b, b).raw;
  return Vec128<uint64_t, (N + 1) / 2>(
      vget_low_u64(vmull_u32(a_packed, b_packed)));
}

template <class T, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T> MulOdd(Vec128<T> a, Vec128<T> b) {
  T hi;
  T lo = Mul128(detail::GetLane<1>(a), detail::GetLane<1>(b), &hi);
  return Dup128VecFromValues(Full128<T>(), lo, hi);
}

// ------------------------------ TableLookupBytes (Combine, LowerHalf)

// Both full
template <typename T, typename TI>
HWY_API Vec128<TI> TableLookupBytes(Vec128<T> bytes, Vec128<TI> from) {
  const DFromV<decltype(from)> d;
  const Repartition<uint8_t, decltype(d)> d8;
#if HWY_ARCH_ARM_A64
  return BitCast(d, Vec128<uint8_t>(vqtbl1q_u8(BitCast(d8, bytes).raw,
                                               BitCast(d8, from).raw)));
#else
  uint8x16_t table0 = BitCast(d8, bytes).raw;
  uint8x8x2_t table;
  table.val[0] = vget_low_u8(table0);
  table.val[1] = vget_high_u8(table0);
  uint8x16_t idx = BitCast(d8, from).raw;
  uint8x8_t low = vtbl2_u8(table, vget_low_u8(idx));
  uint8x8_t hi = vtbl2_u8(table, vget_high_u8(idx));
  return BitCast(d, Vec128<uint8_t>(vcombine_u8(low, hi)));
#endif
}

// Partial index vector
template <typename T, typename TI, size_t NI, HWY_IF_V_SIZE_LE(TI, NI, 8)>
HWY_API Vec128<TI, NI> TableLookupBytes(Vec128<T> bytes, Vec128<TI, NI> from) {
  const Full128<TI> d_full;
  const Vec64<TI> from64(from.raw);
  const auto idx_full = Combine(d_full, from64, from64);
  const auto out_full = TableLookupBytes(bytes, idx_full);
  return Vec128<TI, NI>(LowerHalf(Half<decltype(d_full)>(), out_full).raw);
}

// Partial table vector
template <typename T, size_t N, typename TI, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<TI> TableLookupBytes(Vec128<T, N> bytes, Vec128<TI> from) {
  const Full128<T> d_full;
  return TableLookupBytes(Combine(d_full, bytes, bytes), from);
}

// Partial both
template <typename T, size_t N, typename TI, size_t NI,
          HWY_IF_V_SIZE_LE(T, N, 8), HWY_IF_V_SIZE_LE(TI, NI, 8)>
HWY_API Vec128<TI, NI> TableLookupBytes(Vec128<T, N> bytes,
                                        Vec128<TI, NI> from) {
  const DFromV<decltype(bytes)> d;
  const Simd<TI, NI, 0> d_idx;
  const Repartition<uint8_t, decltype(d_idx)> d_idx8;
  // uint8x8
  const auto bytes8 = BitCast(Repartition<uint8_t, decltype(d)>(), bytes);
  const auto from8 = BitCast(d_idx8, from);
  const VFromD<decltype(d_idx8)> v8(vtbl1_u8(bytes8.raw, from8.raw));
  return BitCast(d_idx, v8);
}

// For all vector widths; Arm anyway zeroes if >= 0x10.
template <class V, class VI>
HWY_API VI TableLookupBytesOr0(V bytes, VI from) {
  return TableLookupBytes(bytes, from);
}

// ---------------------------- AESKeyGenAssist (AESLastRound, TableLookupBytes)

#if HWY_TARGET != HWY_NEON_WITHOUT_AES
template <uint8_t kRcon>
HWY_API Vec128<uint8_t> AESKeyGenAssist(Vec128<uint8_t> v) {
  alignas(16) static constexpr uint8_t kRconXorMask[16] = {
      0, kRcon, 0, 0, 0, 0, 0, 0, 0, kRcon, 0, 0, 0, 0, 0, 0};
  alignas(16) static constexpr uint8_t kRotWordShuffle[16] = {
      0, 13, 10, 7, 1, 14, 11, 4, 8, 5, 2, 15, 9, 6, 3, 12};
  const DFromV<decltype(v)> d;
  const Repartition<uint32_t, decltype(d)> du32;
  const auto w13 = BitCast(d, DupOdd(BitCast(du32, v)));
  const auto sub_word_result = AESLastRound(w13, Load(d, kRconXorMask));
  return TableLookupBytes(sub_word_result, Load(d, kRotWordShuffle));
}
#endif  // HWY_TARGET != HWY_NEON_WITHOUT_AES

// ------------------------------ Scatter in generic_ops-inl.h
// ------------------------------ Gather in generic_ops-inl.h

// ------------------------------ Reductions

// On Armv8 we define ReduceSum and generic_ops defines SumOfLanes via Set.
#if HWY_ARCH_ARM_A64

#ifdef HWY_NATIVE_REDUCE_SCALAR
#undef HWY_NATIVE_REDUCE_SCALAR
#else
#define HWY_NATIVE_REDUCE_SCALAR
#endif

// TODO(janwas): use normal HWY_NEON_DEF, then FULL type list.
#define HWY_NEON_DEF_REDUCTION(type, size, name, prefix, infix, suffix) \
  template <class D, HWY_IF_LANES_D(D, size)>                           \
  HWY_API type##_t name(D /* tag */, Vec128<type##_t, size> v) {        \
    return HWY_NEON_EVAL(prefix##infix##suffix, v.raw);                 \
  }

// Excludes u64/s64 (missing minv/maxv) and f16 (missing addv).
#define HWY_NEON_DEF_REDUCTION_CORE_TYPES(name, prefix)       \
  HWY_NEON_DEF_REDUCTION(uint8, 8, name, prefix, _, u8)       \
  HWY_NEON_DEF_REDUCTION(uint8, 16, name, prefix##q, _, u8)   \
  HWY_NEON_DEF_REDUCTION(uint16, 4, name, prefix, _, u16)     \
  HWY_NEON_DEF_REDUCTION(uint16, 8, name, prefix##q, _, u16)  \
  HWY_NEON_DEF_REDUCTION(uint32, 2, name, prefix, _, u32)     \
  HWY_NEON_DEF_REDUCTION(uint32, 4, name, prefix##q, _, u32)  \
  HWY_NEON_DEF_REDUCTION(int8, 8, name, prefix, _, s8)        \
  HWY_NEON_DEF_REDUCTION(int8, 16, name, prefix##q, _, s8)    \
  HWY_NEON_DEF_REDUCTION(int16, 4, name, prefix, _, s16)      \
  HWY_NEON_DEF_REDUCTION(int16, 8, name, prefix##q, _, s16)   \
  HWY_NEON_DEF_REDUCTION(int32, 2, name, prefix, _, s32)      \
  HWY_NEON_DEF_REDUCTION(int32, 4, name, prefix##q, _, s32)   \
  HWY_NEON_DEF_REDUCTION(float32, 2, name, prefix, _, f32)    \
  HWY_NEON_DEF_REDUCTION(float32, 4, name, prefix##q, _, f32) \
  HWY_NEON_DEF_REDUCTION(float64, 2, name, prefix##q, _, f64)

// Different interface than HWY_NEON_DEF_FUNCTION_FULL_UI_64.
#define HWY_NEON_DEF_REDUCTION_UI64(name, prefix)            \
  HWY_NEON_DEF_REDUCTION(uint64, 2, name, prefix##q, _, u64) \
  HWY_NEON_DEF_REDUCTION(int64, 2, name, prefix##q, _, s64)

#if HWY_HAVE_FLOAT16
#define HWY_NEON_DEF_REDUCTION_F16(name, prefix)           \
  HWY_NEON_DEF_REDUCTION(float16, 4, name, prefix, _, f16) \
  HWY_NEON_DEF_REDUCTION(float16, 8, name, prefix##q, _, f16)
#else
#define HWY_NEON_DEF_REDUCTION_F16(name, prefix)
#endif

HWY_NEON_DEF_REDUCTION_CORE_TYPES(ReduceMin, vminv)
HWY_NEON_DEF_REDUCTION_CORE_TYPES(ReduceMax, vmaxv)
HWY_NEON_DEF_REDUCTION_F16(ReduceMin, vminv)
HWY_NEON_DEF_REDUCTION_F16(ReduceMax, vmaxv)

HWY_NEON_DEF_REDUCTION_CORE_TYPES(ReduceSum, vaddv)
HWY_NEON_DEF_REDUCTION_UI64(ReduceSum, vaddv)

// Emulate missing UI64 and partial N=2.
template <class D, HWY_IF_LANES_D(D, 2),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API TFromD<D> ReduceSum(D /* tag */, VFromD<D> v10) {
  return GetLane(v10) + ExtractLane(v10, 1);
}

template <class D, HWY_IF_LANES_D(D, 2), HWY_IF_NOT_FLOAT_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 8))>
HWY_API TFromD<D> ReduceMin(D /* tag */, VFromD<D> v10) {
  return HWY_MIN(GetLane(v10), ExtractLane(v10, 1));
}

template <class D, HWY_IF_LANES_D(D, 2), HWY_IF_NOT_FLOAT_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 8))>
HWY_API TFromD<D> ReduceMax(D /* tag */, VFromD<D> v10) {
  return HWY_MAX(GetLane(v10), ExtractLane(v10, 1));
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_LANES_D(D, 2), HWY_IF_F16_D(D)>
HWY_API float16_t ReduceMin(D d, VFromD<D> v10) {
  return GetLane(Min(v10, Reverse2(d, v10)));
}

template <class D, HWY_IF_LANES_D(D, 2), HWY_IF_F16_D(D)>
HWY_API float16_t ReduceMax(D d, VFromD<D> v10) {
  return GetLane(Max(v10, Reverse2(d, v10)));
}

template <class D, HWY_IF_F16_D(D), HWY_IF_V_SIZE_D(D, 8)>
HWY_API float16_t ReduceSum(D /* tag */, VFromD<D> v) {
  const float16x4_t x2 = vpadd_f16(v.raw, v.raw);
  return GetLane(VFromD<D>(vpadd_f16(x2, x2)));
}
template <class D, HWY_IF_F16_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API float16_t ReduceSum(D d, VFromD<D> v) {
  const Half<decltype(d)> dh;
  return ReduceSum(dh, LowerHalf(dh, VFromD<D>(vpaddq_f16(v.raw, v.raw))));
}
#endif  // HWY_HAVE_FLOAT16

#undef HWY_NEON_DEF_REDUCTION_CORE_TYPES
#undef HWY_NEON_DEF_REDUCTION_F16
#undef HWY_NEON_DEF_REDUCTION_UI64
#undef HWY_NEON_DEF_REDUCTION

// ------------------------------ SumOfLanes

template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> SumOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceSum(d, v));
}
template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> MinOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceMin(d, v));
}
template <class D, HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> MaxOfLanes(D d, VFromD<D> v) {
  return Set(d, ReduceMax(d, v));
}

// On Armv7 we define SumOfLanes and generic_ops defines ReduceSum via GetLane.
#else  // !HWY_ARCH_ARM_A64

// Armv7 lacks N=2 and 8-bit x4, so enable generic versions of those.
#undef HWY_IF_SUM_OF_LANES_D
#define HWY_IF_SUM_OF_LANES_D(D)                                        \
  hwy::EnableIf<(HWY_MAX_LANES_D(D) == 2) ||                            \
                (sizeof(TFromD<D>) == 1 && HWY_MAX_LANES_D(D) == 4)>* = \
      nullptr
#undef HWY_IF_MINMAX_OF_LANES_D
#define HWY_IF_MINMAX_OF_LANES_D(D)                                     \
  hwy::EnableIf<(HWY_MAX_LANES_D(D) == 2) ||                            \
                (sizeof(TFromD<D>) == 1 && HWY_MAX_LANES_D(D) == 4)>* = \
      nullptr

// For arm7, we implement reductions using a series of pairwise operations. This
// produces the full vector result, so we express Reduce* in terms of *OfLanes.
#define HWY_NEON_BUILD_TYPE_T(type, size) type##x##size##_t
#define HWY_NEON_DEF_PAIRWISE_REDUCTION(type, size, name, prefix, suffix)    \
  template <class D, HWY_IF_LANES_D(D, size)>                                \
  HWY_API Vec128<type##_t, size> name##OfLanes(D /* d */,                    \
                                               Vec128<type##_t, size> v) {   \
    HWY_NEON_BUILD_TYPE_T(type, size) tmp = prefix##_##suffix(v.raw, v.raw); \
    if ((size / 2) > 1) tmp = prefix##_##suffix(tmp, tmp);                   \
    if ((size / 4) > 1) tmp = prefix##_##suffix(tmp, tmp);                   \
    return Vec128<type##_t, size>(tmp);                                      \
  }

// For the wide versions, the pairwise operations produce a half-length vector.
// We produce that `tmp` and then Combine.
#define HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(type, size, half, name, prefix, \
                                             suffix)                         \
  template <class D, HWY_IF_LANES_D(D, size)>                                \
  HWY_API Vec128<type##_t, size> name##OfLanes(D /* d */,                    \
                                               Vec128<type##_t, size> v) {   \
    HWY_NEON_BUILD_TYPE_T(type, half) tmp;                                   \
    tmp = prefix##_##suffix(vget_high_##suffix(v.raw),                       \
                            vget_low_##suffix(v.raw));                       \
    if ((size / 2) > 1) tmp = prefix##_##suffix(tmp, tmp);                   \
    if ((size / 4) > 1) tmp = prefix##_##suffix(tmp, tmp);                   \
    if ((size / 8) > 1) tmp = prefix##_##suffix(tmp, tmp);                   \
    return Vec128<type##_t, size>(vcombine_##suffix(tmp, tmp));              \
  }

#define HWY_NEON_DEF_PAIRWISE_REDUCTIONS(name, prefix)                  \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(uint32, 2, name, prefix, u32)         \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(uint16, 4, name, prefix, u16)         \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(uint8, 8, name, prefix, u8)           \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(int32, 2, name, prefix, s32)          \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(int16, 4, name, prefix, s16)          \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(int8, 8, name, prefix, s8)            \
  HWY_NEON_DEF_PAIRWISE_REDUCTION(float32, 2, name, prefix, f32)        \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(uint32, 4, 2, name, prefix, u32) \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(uint16, 8, 4, name, prefix, u16) \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(uint8, 16, 8, name, prefix, u8)  \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(int32, 4, 2, name, prefix, s32)  \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(int16, 8, 4, name, prefix, s16)  \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(int8, 16, 8, name, prefix, s8)   \
  HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION(float32, 4, 2, name, prefix, f32)

HWY_NEON_DEF_PAIRWISE_REDUCTIONS(Sum, vpadd)
HWY_NEON_DEF_PAIRWISE_REDUCTIONS(Min, vpmin)
HWY_NEON_DEF_PAIRWISE_REDUCTIONS(Max, vpmax)

#undef HWY_NEON_DEF_PAIRWISE_REDUCTIONS
#undef HWY_NEON_DEF_WIDE_PAIRWISE_REDUCTION
#undef HWY_NEON_DEF_PAIRWISE_REDUCTION
#undef HWY_NEON_BUILD_TYPE_T

// GetLane(SumsOf4(v)) is more efficient on ArmV7 NEON than the default
// N=4 I8/U8 ReduceSum implementation in generic_ops-inl.h
#ifdef HWY_NATIVE_REDUCE_SUM_4_UI8
#undef HWY_NATIVE_REDUCE_SUM_4_UI8
#else
#define HWY_NATIVE_REDUCE_SUM_4_UI8
#endif

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_UI8_D(D)>
HWY_API TFromD<D> ReduceSum(D /*d*/, VFromD<D> v) {
  return static_cast<TFromD<D>>(GetLane(SumsOf4(v)));
}

#endif  // HWY_ARCH_ARM_A64

// ------------------------------ LoadMaskBits (TestBit)

namespace detail {

// Helper function to set 64 bits and potentially return a smaller vector. The
// overload is required to call the q vs non-q intrinsics. Note that 8-bit
// LoadMaskBits only requires 16 bits, but 64 avoids casting.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_INLINE VFromD<D> Set64(D /* tag */, uint64_t mask_bits) {
  const auto v64 = Vec64<uint64_t>(vdup_n_u64(mask_bits));
  return VFromD<D>(BitCast(Full64<TFromD<D>>(), v64).raw);
}
template <typename T>
HWY_INLINE Vec128<T> Set64(Full128<T> d, uint64_t mask_bits) {
  return BitCast(d, Vec128<uint64_t>(vdupq_n_u64(mask_bits)));
}

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  // Easier than Set(), which would require an >8-bit type, which would not
  // compile for T=uint8_t, N=1.
  const auto vmask_bits = Set64(du, mask_bits);

  // Replicate bytes 8x such that each byte contains the bit that governs it.
  alignas(16) static constexpr uint8_t kRep8[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                    1, 1, 1, 1, 1, 1, 1, 1};
  const auto rep8 = TableLookupBytes(vmask_bits, Load(du, kRep8));

  alignas(16) static constexpr uint8_t kBit[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                                   1, 2, 4, 8, 16, 32, 64, 128};
  return RebindMask(d, TestBit(rep8, LoadDup128(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint16_t kBit[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  const auto vmask_bits = Set(du, static_cast<uint16_t>(mask_bits));
  return RebindMask(d, TestBit(vmask_bits, Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint32_t kBit[8] = {1, 2, 4, 8};
  const auto vmask_bits = Set(du, static_cast<uint32_t>(mask_bits));
  return RebindMask(d, TestBit(vmask_bits, Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint64_t kBit[8] = {1, 2};
  return RebindMask(d, TestBit(Set(du, mask_bits), Load(du, kBit)));
}

}  // namespace detail

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  uint64_t mask_bits = 0;
  CopyBytes<(d.MaxLanes() + 7) / 8>(bits, &mask_bits);
  return detail::LoadMaskBits(d, mask_bits);
}

// ------------------------------ Dup128MaskFromMaskBits

template <class D>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;
  return detail::LoadMaskBits(d, mask_bits);
}

// ------------------------------ Mask

namespace detail {

// Returns mask[i]? 0xF : 0 in each nibble. This is more efficient than
// BitsFromMask for use in (partial) CountTrue, FindFirstTrue and AllFalse.
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_INLINE uint64_t NibblesFromMask(D d, MFromD<D> mask) {
  const Full128<uint16_t> du16;
  const Vec128<uint16_t> vu16 = BitCast(du16, VecFromMask(d, mask));
  const Vec64<uint8_t> nib(vshrn_n_u16(vu16.raw, 4));
  return GetLane(BitCast(Full64<uint64_t>(), nib));
}

template <class D, HWY_IF_V_SIZE_D(D, 8)>
HWY_INLINE uint64_t NibblesFromMask(D d, MFromD<D> mask) {
  // There is no vshrn_n_u16 for uint16x4, so zero-extend.
  const Twice<decltype(d)> d2;
  const VFromD<decltype(d2)> v128 = ZeroExtendVector(d2, VecFromMask(d, mask));
  // No need to mask, upper half is zero thanks to ZeroExtendVector.
  return NibblesFromMask(d2, MaskFromVec(v128));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_INLINE uint64_t NibblesFromMask(D d, MFromD<D> mask) {
  const Mask64<TFromD<D>> mask64(mask.raw);
  const uint64_t nib = NibblesFromMask(Full64<TFromD<D>>(), mask64);
  // Clear nibbles from upper half of 64-bits
  return nib & ((1ull << (d.MaxBytes() * 4)) - 1);
}

template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/, Mask128<T> mask) {
  alignas(16) static constexpr uint8_t kSliceLanes[16] = {
      1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80, 1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80,
  };
  const Full128<uint8_t> du;
  const Vec128<uint8_t> values =
      BitCast(du, VecFromMask(Full128<T>(), mask)) & Load(du, kSliceLanes);

#if HWY_ARCH_ARM_A64
  // Can't vaddv - we need two separate bytes (16 bits).
  const uint8x8_t x2 = vget_low_u8(vpaddq_u8(values.raw, values.raw));
  const uint8x8_t x4 = vpadd_u8(x2, x2);
  const uint8x8_t x8 = vpadd_u8(x4, x4);
  return vget_lane_u64(vreinterpret_u64_u8(x8), 0) & 0xFFFF;
#else
  // Don't have vpaddq, so keep doubling lane size.
  const uint16x8_t x2 = vpaddlq_u8(values.raw);
  const uint32x4_t x4 = vpaddlq_u16(x2);
  const uint64x2_t x8 = vpaddlq_u32(x4);
  return (vgetq_lane_u64(x8, 1) << 8) | vgetq_lane_u64(x8, 0);
#endif
}

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/, Mask128<T, N> mask) {
  // Upper lanes of partial loads are undefined. OnlyActive will fix this if
  // we load all kSliceLanes so the upper lanes do not pollute the valid bits.
  alignas(8) static constexpr uint8_t kSliceLanes[8] = {1,    2,    4,    8,
                                                        0x10, 0x20, 0x40, 0x80};
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Vec128<uint8_t, N> slice(Load(Full64<uint8_t>(), kSliceLanes).raw);
  const Vec128<uint8_t, N> values = BitCast(du, VecFromMask(d, mask)) & slice;

#if HWY_ARCH_ARM_A64
  return vaddv_u8(values.raw);
#else
  const uint16x4_t x2 = vpaddl_u8(values.raw);
  const uint32x2_t x4 = vpaddl_u16(x2);
  const uint64x1_t x8 = vpaddl_u32(x4);
  return vget_lane_u64(x8, 0);
#endif
}

template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<2> /*tag*/, Mask128<T> mask) {
  alignas(16) static constexpr uint16_t kSliceLanes[8] = {
      1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80};
  const Full128<T> d;
  const Full128<uint16_t> du;
  const Vec128<uint16_t> values =
      BitCast(du, VecFromMask(d, mask)) & Load(du, kSliceLanes);
#if HWY_ARCH_ARM_A64
  return vaddvq_u16(values.raw);
#else
  const uint32x4_t x2 = vpaddlq_u16(values.raw);
  const uint64x2_t x4 = vpaddlq_u32(x2);
  return vgetq_lane_u64(x4, 0) + vgetq_lane_u64(x4, 1);
#endif
}

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<2> /*tag*/, Mask128<T, N> mask) {
  // Upper lanes of partial loads are undefined. OnlyActive will fix this if
  // we load all kSliceLanes so the upper lanes do not pollute the valid bits.
  alignas(8) static constexpr uint16_t kSliceLanes[4] = {1, 2, 4, 8};
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Vec128<uint16_t, N> slice(Load(Full64<uint16_t>(), kSliceLanes).raw);
  const Vec128<uint16_t, N> values = BitCast(du, VecFromMask(d, mask)) & slice;
#if HWY_ARCH_ARM_A64
  return vaddv_u16(values.raw);
#else
  const uint32x2_t x2 = vpaddl_u16(values.raw);
  const uint64x1_t x4 = vpaddl_u32(x2);
  return vget_lane_u64(x4, 0);
#endif
}

template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<4> /*tag*/, Mask128<T> mask) {
  alignas(16) static constexpr uint32_t kSliceLanes[4] = {1, 2, 4, 8};
  const Full128<T> d;
  const Full128<uint32_t> du;
  const Vec128<uint32_t> values =
      BitCast(du, VecFromMask(d, mask)) & Load(du, kSliceLanes);
#if HWY_ARCH_ARM_A64
  return vaddvq_u32(values.raw);
#else
  const uint64x2_t x2 = vpaddlq_u32(values.raw);
  return vgetq_lane_u64(x2, 0) + vgetq_lane_u64(x2, 1);
#endif
}

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<4> /*tag*/, Mask128<T, N> mask) {
  // Upper lanes of partial loads are undefined. OnlyActive will fix this if
  // we load all kSliceLanes so the upper lanes do not pollute the valid bits.
  alignas(8) static constexpr uint32_t kSliceLanes[2] = {1, 2};
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Vec128<uint32_t, N> slice(Load(Full64<uint32_t>(), kSliceLanes).raw);
  const Vec128<uint32_t, N> values = BitCast(du, VecFromMask(d, mask)) & slice;
#if HWY_ARCH_ARM_A64
  return vaddv_u32(values.raw);
#else
  const uint64x1_t x2 = vpaddl_u32(values.raw);
  return vget_lane_u64(x2, 0);
#endif
}

template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<8> /*tag*/, Mask128<T> m) {
  alignas(16) static constexpr uint64_t kSliceLanes[2] = {1, 2};
  const Full128<T> d;
  const Full128<uint64_t> du;
  const Vec128<uint64_t> values =
      BitCast(du, VecFromMask(d, m)) & Load(du, kSliceLanes);
#if HWY_ARCH_ARM_A64
  return vaddvq_u64(values.raw);
#else
  return vgetq_lane_u64(values.raw, 0) + vgetq_lane_u64(values.raw, 1);
#endif
}

template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<8> /*tag*/, Mask128<T, 1> m) {
  const Full64<T> d;
  const Full64<uint64_t> du;
  const Vec64<uint64_t> values = BitCast(du, VecFromMask(d, m)) & Set(du, 1);
  return vget_lane_u64(values.raw, 0);
}

// Returns the lowest N for the BitsFromMask result.
template <typename T, size_t N>
constexpr uint64_t OnlyActive(uint64_t bits) {
  return ((N * sizeof(T)) >= 8) ? bits : (bits & ((1ull << N) - 1));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(Mask128<T, N> mask) {
  return OnlyActive<T, N>(BitsFromMask(hwy::SizeTag<sizeof(T)>(), mask));
}

// Returns number of lanes whose mask is set.
//
// Masks are either FF..FF or 0. Unfortunately there is no reduce-sub op
// ("vsubv"). ANDing with 1 would work but requires a constant. Negating also
// changes each lane to 1 (if mask set) or 0.
// NOTE: PopCount also operates on vectors, so we still have to do horizontal
// sums separately. We specialize CountTrue for full vectors (negating instead
// of PopCount because it avoids an extra shift), and use PopCount of
// NibblesFromMask for partial vectors.

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<1> /*tag*/, Mask128<T> mask) {
  const Full128<int8_t> di;
  const int8x16_t ones =
      vnegq_s8(BitCast(di, VecFromMask(Full128<T>(), mask)).raw);

#if HWY_ARCH_ARM_A64
  return static_cast<size_t>(vaddvq_s8(ones));
#else
  const int16x8_t x2 = vpaddlq_s8(ones);
  const int32x4_t x4 = vpaddlq_s16(x2);
  const int64x2_t x8 = vpaddlq_s32(x4);
  return static_cast<size_t>(vgetq_lane_s64(x8, 0) + vgetq_lane_s64(x8, 1));
#endif
}
template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<2> /*tag*/, Mask128<T> mask) {
  const Full128<int16_t> di;
  const int16x8_t ones =
      vnegq_s16(BitCast(di, VecFromMask(Full128<T>(), mask)).raw);

#if HWY_ARCH_ARM_A64
  return static_cast<size_t>(vaddvq_s16(ones));
#else
  const int32x4_t x2 = vpaddlq_s16(ones);
  const int64x2_t x4 = vpaddlq_s32(x2);
  return static_cast<size_t>(vgetq_lane_s64(x4, 0) + vgetq_lane_s64(x4, 1));
#endif
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<4> /*tag*/, Mask128<T> mask) {
  const Full128<int32_t> di;
  const int32x4_t ones =
      vnegq_s32(BitCast(di, VecFromMask(Full128<T>(), mask)).raw);

#if HWY_ARCH_ARM_A64
  return static_cast<size_t>(vaddvq_s32(ones));
#else
  const int64x2_t x2 = vpaddlq_s32(ones);
  return static_cast<size_t>(vgetq_lane_s64(x2, 0) + vgetq_lane_s64(x2, 1));
#endif
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<8> /*tag*/, Mask128<T> mask) {
#if HWY_ARCH_ARM_A64
  const Full128<int64_t> di;
  const int64x2_t ones =
      vnegq_s64(BitCast(di, VecFromMask(Full128<T>(), mask)).raw);
  return static_cast<size_t>(vaddvq_s64(ones));
#else
  const Full128<uint64_t> du;
  const auto mask_u = VecFromMask(du, RebindMask(du, mask));
  const uint64x2_t ones = vshrq_n_u64(mask_u.raw, 63);
  return static_cast<size_t>(vgetq_lane_u64(ones, 0) + vgetq_lane_u64(ones, 1));
#endif
}

}  // namespace detail

// Full
template <class D, typename T = TFromD<D>>
HWY_API size_t CountTrue(D /* tag */, Mask128<T> mask) {
  return detail::CountTrue(hwy::SizeTag<sizeof(T)>(), mask);
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API size_t CountTrue(D d, MFromD<D> mask) {
  constexpr int kDiv = 4 * sizeof(TFromD<D>);
  return PopCount(detail::NibblesFromMask(d, mask)) / kDiv;
}

template <class D>
HWY_API size_t FindKnownFirstTrue(D d, MFromD<D> mask) {
  const uint64_t nib = detail::NibblesFromMask(d, mask);
  constexpr size_t kDiv = 4 * sizeof(TFromD<D>);
  return Num0BitsBelowLS1Bit_Nonzero64(nib) / kDiv;
}

template <class D>
HWY_API intptr_t FindFirstTrue(D d, MFromD<D> mask) {
  const uint64_t nib = detail::NibblesFromMask(d, mask);
  if (nib == 0) return -1;
  constexpr size_t kDiv = 4 * sizeof(TFromD<D>);
  return static_cast<intptr_t>(Num0BitsBelowLS1Bit_Nonzero64(nib) / kDiv);
}

template <class D>
HWY_API size_t FindKnownLastTrue(D d, MFromD<D> mask) {
  const uint64_t nib = detail::NibblesFromMask(d, mask);
  constexpr size_t kDiv = 4 * sizeof(TFromD<D>);
  return (63 - Num0BitsAboveMS1Bit_Nonzero64(nib)) / kDiv;
}

template <class D>
HWY_API intptr_t FindLastTrue(D d, MFromD<D> mask) {
  const uint64_t nib = detail::NibblesFromMask(d, mask);
  if (nib == 0) return -1;
  constexpr size_t kDiv = 4 * sizeof(TFromD<D>);
  return static_cast<intptr_t>((63 - Num0BitsAboveMS1Bit_Nonzero64(nib)) /
                               kDiv);
}

// `p` points to at least 8 writable bytes.
template <class D>
HWY_API size_t StoreMaskBits(D d, MFromD<D> mask, uint8_t* bits) {
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  const size_t kNumBytes = (d.MaxLanes() + 7) / 8;
  CopyBytes<kNumBytes>(&mask_bits, bits);
  return kNumBytes;
}

template <class D>
HWY_API bool AllFalse(D d, MFromD<D> m) {
  return detail::NibblesFromMask(d, m) == 0;
}

// Full
template <class D, typename T = TFromD<D>>
HWY_API bool AllTrue(D d, Mask128<T> m) {
  return detail::NibblesFromMask(d, m) == ~0ull;
}
// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API bool AllTrue(D d, MFromD<D> m) {
  return detail::NibblesFromMask(d, m) == (1ull << (d.MaxBytes() * 4)) - 1;
}

// ------------------------------ Compress

template <typename T>
struct CompressIsPartition {
  enum { value = (sizeof(T) != 1) };
};

namespace detail {

// Load 8 bytes, replicate into upper half so ZipLower can use the lower half.
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_INLINE Vec128<uint8_t> Load8Bytes(D /*tag*/, const uint8_t* bytes) {
  return Vec128<uint8_t>(vreinterpretq_u8_u64(
      vld1q_dup_u64(HWY_RCAST_ALIGNED(const uint64_t*, bytes))));
}

// Load 8 bytes and return half-reg with N <= 8 bytes.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_INLINE VFromD<D> Load8Bytes(D d, const uint8_t* bytes) {
  return Load(d, bytes);
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromBits(hwy::SizeTag<2> /*tag*/,
                                    uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  const Simd<uint16_t, N, 0> du;

  // NEON does not provide an equivalent of AVX2 permutevar, so we need byte
  // indices for VTBL (one vector's worth for each of 256 combinations of
  // 8 mask bits). Loading them directly would require 4 KiB. We can instead
  // store lane indices and convert to byte indices (2*lane + 0..1), with the
  // doubling baked into the table. AVX2 Compress32 stores eight 4-bit lane
  // indices (total 1 KiB), broadcasts them into each 32-bit lane and shifts.
  // Here, 16-bit lanes are too narrow to hold all bits, and unpacking nibbles
  // is likely more costly than the higher cache footprint from storing bytes.
  alignas(16) static constexpr uint8_t table[256 * 8] = {
      // PrintCompress16x8Tables
      0,  2,  4,  6,  8,  10, 12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      2,  0,  4,  6,  8,  10, 12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      4,  0,  2,  6,  8,  10, 12, 14, /**/ 0, 4,  2,  6,  8,  10, 12, 14,  //
      2,  4,  0,  6,  8,  10, 12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      6,  0,  2,  4,  8,  10, 12, 14, /**/ 0, 6,  2,  4,  8,  10, 12, 14,  //
      2,  6,  0,  4,  8,  10, 12, 14, /**/ 0, 2,  6,  4,  8,  10, 12, 14,  //
      4,  6,  0,  2,  8,  10, 12, 14, /**/ 0, 4,  6,  2,  8,  10, 12, 14,  //
      2,  4,  6,  0,  8,  10, 12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      8,  0,  2,  4,  6,  10, 12, 14, /**/ 0, 8,  2,  4,  6,  10, 12, 14,  //
      2,  8,  0,  4,  6,  10, 12, 14, /**/ 0, 2,  8,  4,  6,  10, 12, 14,  //
      4,  8,  0,  2,  6,  10, 12, 14, /**/ 0, 4,  8,  2,  6,  10, 12, 14,  //
      2,  4,  8,  0,  6,  10, 12, 14, /**/ 0, 2,  4,  8,  6,  10, 12, 14,  //
      6,  8,  0,  2,  4,  10, 12, 14, /**/ 0, 6,  8,  2,  4,  10, 12, 14,  //
      2,  6,  8,  0,  4,  10, 12, 14, /**/ 0, 2,  6,  8,  4,  10, 12, 14,  //
      4,  6,  8,  0,  2,  10, 12, 14, /**/ 0, 4,  6,  8,  2,  10, 12, 14,  //
      2,  4,  6,  8,  0,  10, 12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      10, 0,  2,  4,  6,  8,  12, 14, /**/ 0, 10, 2,  4,  6,  8,  12, 14,  //
      2,  10, 0,  4,  6,  8,  12, 14, /**/ 0, 2,  10, 4,  6,  8,  12, 14,  //
      4,  10, 0,  2,  6,  8,  12, 14, /**/ 0, 4,  10, 2,  6,  8,  12, 14,  //
      2,  4,  10, 0,  6,  8,  12, 14, /**/ 0, 2,  4,  10, 6,  8,  12, 14,  //
      6,  10, 0,  2,  4,  8,  12, 14, /**/ 0, 6,  10, 2,  4,  8,  12, 14,  //
      2,  6,  10, 0,  4,  8,  12, 14, /**/ 0, 2,  6,  10, 4,  8,  12, 14,  //
      4,  6,  10, 0,  2,  8,  12, 14, /**/ 0, 4,  6,  10, 2,  8,  12, 14,  //
      2,  4,  6,  10, 0,  8,  12, 14, /**/ 0, 2,  4,  6,  10, 8,  12, 14,  //
      8,  10, 0,  2,  4,  6,  12, 14, /**/ 0, 8,  10, 2,  4,  6,  12, 14,  //
      2,  8,  10, 0,  4,  6,  12, 14, /**/ 0, 2,  8,  10, 4,  6,  12, 14,  //
      4,  8,  10, 0,  2,  6,  12, 14, /**/ 0, 4,  8,  10, 2,  6,  12, 14,  //
      2,  4,  8,  10, 0,  6,  12, 14, /**/ 0, 2,  4,  8,  10, 6,  12, 14,  //
      6,  8,  10, 0,  2,  4,  12, 14, /**/ 0, 6,  8,  10, 2,  4,  12, 14,  //
      2,  6,  8,  10, 0,  4,  12, 14, /**/ 0, 2,  6,  8,  10, 4,  12, 14,  //
      4,  6,  8,  10, 0,  2,  12, 14, /**/ 0, 4,  6,  8,  10, 2,  12, 14,  //
      2,  4,  6,  8,  10, 0,  12, 14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      12, 0,  2,  4,  6,  8,  10, 14, /**/ 0, 12, 2,  4,  6,  8,  10, 14,  //
      2,  12, 0,  4,  6,  8,  10, 14, /**/ 0, 2,  12, 4,  6,  8,  10, 14,  //
      4,  12, 0,  2,  6,  8,  10, 14, /**/ 0, 4,  12, 2,  6,  8,  10, 14,  //
      2,  4,  12, 0,  6,  8,  10, 14, /**/ 0, 2,  4,  12, 6,  8,  10, 14,  //
      6,  12, 0,  2,  4,  8,  10, 14, /**/ 0, 6,  12, 2,  4,  8,  10, 14,  //
      2,  6,  12, 0,  4,  8,  10, 14, /**/ 0, 2,  6,  12, 4,  8,  10, 14,  //
      4,  6,  12, 0,  2,  8,  10, 14, /**/ 0, 4,  6,  12, 2,  8,  10, 14,  //
      2,  4,  6,  12, 0,  8,  10, 14, /**/ 0, 2,  4,  6,  12, 8,  10, 14,  //
      8,  12, 0,  2,  4,  6,  10, 14, /**/ 0, 8,  12, 2,  4,  6,  10, 14,  //
      2,  8,  12, 0,  4,  6,  10, 14, /**/ 0, 2,  8,  12, 4,  6,  10, 14,  //
      4,  8,  12, 0,  2,  6,  10, 14, /**/ 0, 4,  8,  12, 2,  6,  10, 14,  //
      2,  4,  8,  12, 0,  6,  10, 14, /**/ 0, 2,  4,  8,  12, 6,  10, 14,  //
      6,  8,  12, 0,  2,  4,  10, 14, /**/ 0, 6,  8,  12, 2,  4,  10, 14,  //
      2,  6,  8,  12, 0,  4,  10, 14, /**/ 0, 2,  6,  8,  12, 4,  10, 14,  //
      4,  6,  8,  12, 0,  2,  10, 14, /**/ 0, 4,  6,  8,  12, 2,  10, 14,  //
      2,  4,  6,  8,  12, 0,  10, 14, /**/ 0, 2,  4,  6,  8,  12, 10, 14,  //
      10, 12, 0,  2,  4,  6,  8,  14, /**/ 0, 10, 12, 2,  4,  6,  8,  14,  //
      2,  10, 12, 0,  4,  6,  8,  14, /**/ 0, 2,  10, 12, 4,  6,  8,  14,  //
      4,  10, 12, 0,  2,  6,  8,  14, /**/ 0, 4,  10, 12, 2,  6,  8,  14,  //
      2,  4,  10, 12, 0,  6,  8,  14, /**/ 0, 2,  4,  10, 12, 6,  8,  14,  //
      6,  10, 12, 0,  2,  4,  8,  14, /**/ 0, 6,  10, 12, 2,  4,  8,  14,  //
      2,  6,  10, 12, 0,  4,  8,  14, /**/ 0, 2,  6,  10, 12, 4,  8,  14,  //
      4,  6,  10, 12, 0,  2,  8,  14, /**/ 0, 4,  6,  10, 12, 2,  8,  14,  //
      2,  4,  6,  10, 12, 0,  8,  14, /**/ 0, 2,  4,  6,  10, 12, 8,  14,  //
      8,  10, 12, 0,  2,  4,  6,  14, /**/ 0, 8,  10, 12, 2,  4,  6,  14,  //
      2,  8,  10, 12, 0,  4,  6,  14, /**/ 0, 2,  8,  10, 12, 4,  6,  14,  //
      4,  8,  10, 12, 0,  2,  6,  14, /**/ 0, 4,  8,  10, 12, 2,  6,  14,  //
      2,  4,  8,  10, 12, 0,  6,  14, /**/ 0, 2,  4,  8,  10, 12, 6,  14,  //
      6,  8,  10, 12, 0,  2,  4,  14, /**/ 0, 6,  8,  10, 12, 2,  4,  14,  //
      2,  6,  8,  10, 12, 0,  4,  14, /**/ 0, 2,  6,  8,  10, 12, 4,  14,  //
      4,  6,  8,  10, 12, 0,  2,  14, /**/ 0, 4,  6,  8,  10, 12, 2,  14,  //
      2,  4,  6,  8,  10, 12, 0,  14, /**/ 0, 2,  4,  6,  8,  10, 12, 14,  //
      14, 0,  2,  4,  6,  8,  10, 12, /**/ 0, 14, 2,  4,  6,  8,  10, 12,  //
      2,  14, 0,  4,  6,  8,  10, 12, /**/ 0, 2,  14, 4,  6,  8,  10, 12,  //
      4,  14, 0,  2,  6,  8,  10, 12, /**/ 0, 4,  14, 2,  6,  8,  10, 12,  //
      2,  4,  14, 0,  6,  8,  10, 12, /**/ 0, 2,  4,  14, 6,  8,  10, 12,  //
      6,  14, 0,  2,  4,  8,  10, 12, /**/ 0, 6,  14, 2,  4,  8,  10, 12,  //
      2,  6,  14, 0,  4,  8,  10, 12, /**/ 0, 2,  6,  14, 4,  8,  10, 12,  //
      4,  6,  14, 0,  2,  8,  10, 12, /**/ 0, 4,  6,  14, 2,  8,  10, 12,  //
      2,  4,  6,  14, 0,  8,  10, 12, /**/ 0, 2,  4,  6,  14, 8,  10, 12,  //
      8,  14, 0,  2,  4,  6,  10, 12, /**/ 0, 8,  14, 2,  4,  6,  10, 12,  //
      2,  8,  14, 0,  4,  6,  10, 12, /**/ 0, 2,  8,  14, 4,  6,  10, 12,  //
      4,  8,  14, 0,  2,  6,  10, 12, /**/ 0, 4,  8,  14, 2,  6,  10, 12,  //
      2,  4,  8,  14, 0,  6,  10, 12, /**/ 0, 2,  4,  8,  14, 6,  10, 12,  //
      6,  8,  14, 0,  2,  4,  10, 12, /**/ 0, 6,  8,  14, 2,  4,  10, 12,  //
      2,  6,  8,  14, 0,  4,  10, 12, /**/ 0, 2,  6,  8,  14, 4,  10, 12,  //
      4,  6,  8,  14, 0,  2,  10, 12, /**/ 0, 4,  6,  8,  14, 2,  10, 12,  //
      2,  4,  6,  8,  14, 0,  10, 12, /**/ 0, 2,  4,  6,  8,  14, 10, 12,  //
      10, 14, 0,  2,  4,  6,  8,  12, /**/ 0, 10, 14, 2,  4,  6,  8,  12,  //
      2,  10, 14, 0,  4,  6,  8,  12, /**/ 0, 2,  10, 14, 4,  6,  8,  12,  //
      4,  10, 14, 0,  2,  6,  8,  12, /**/ 0, 4,  10, 14, 2,  6,  8,  12,  //
      2,  4,  10, 14, 0,  6,  8,  12, /**/ 0, 2,  4,  10, 14, 6,  8,  12,  //
      6,  10, 14, 0,  2,  4,  8,  12, /**/ 0, 6,  10, 14, 2,  4,  8,  12,  //
      2,  6,  10, 14, 0,  4,  8,  12, /**/ 0, 2,  6,  10, 14, 4,  8,  12,  //
      4,  6,  10, 14, 0,  2,  8,  12, /**/ 0, 4,  6,  10, 14, 2,  8,  12,  //
      2,  4,  6,  10, 14, 0,  8,  12, /**/ 0, 2,  4,  6,  10, 14, 8,  12,  //
      8,  10, 14, 0,  2,  4,  6,  12, /**/ 0, 8,  10, 14, 2,  4,  6,  12,  //
      2,  8,  10, 14, 0,  4,  6,  12, /**/ 0, 2,  8,  10, 14, 4,  6,  12,  //
      4,  8,  10, 14, 0,  2,  6,  12, /**/ 0, 4,  8,  10, 14, 2,  6,  12,  //
      2,  4,  8,  10, 14, 0,  6,  12, /**/ 0, 2,  4,  8,  10, 14, 6,  12,  //
      6,  8,  10, 14, 0,  2,  4,  12, /**/ 0, 6,  8,  10, 14, 2,  4,  12,  //
      2,  6,  8,  10, 14, 0,  4,  12, /**/ 0, 2,  6,  8,  10, 14, 4,  12,  //
      4,  6,  8,  10, 14, 0,  2,  12, /**/ 0, 4,  6,  8,  10, 14, 2,  12,  //
      2,  4,  6,  8,  10, 14, 0,  12, /**/ 0, 2,  4,  6,  8,  10, 14, 12,  //
      12, 14, 0,  2,  4,  6,  8,  10, /**/ 0, 12, 14, 2,  4,  6,  8,  10,  //
      2,  12, 14, 0,  4,  6,  8,  10, /**/ 0, 2,  12, 14, 4,  6,  8,  10,  //
      4,  12, 14, 0,  2,  6,  8,  10, /**/ 0, 4,  12, 14, 2,  6,  8,  10,  //
      2,  4,  12, 14, 0,  6,  8,  10, /**/ 0, 2,  4,  12, 14, 6,  8,  10,  //
      6,  12, 14, 0,  2,  4,  8,  10, /**/ 0, 6,  12, 14, 2,  4,  8,  10,  //
      2,  6,  12, 14, 0,  4,  8,  10, /**/ 0, 2,  6,  12, 14, 4,  8,  10,  //
      4,  6,  12, 14, 0,  2,  8,  10, /**/ 0, 4,  6,  12, 14, 2,  8,  10,  //
      2,  4,  6,  12, 14, 0,  8,  10, /**/ 0, 2,  4,  6,  12, 14, 8,  10,  //
      8,  12, 14, 0,  2,  4,  6,  10, /**/ 0, 8,  12, 14, 2,  4,  6,  10,  //
      2,  8,  12, 14, 0,  4,  6,  10, /**/ 0, 2,  8,  12, 14, 4,  6,  10,  //
      4,  8,  12, 14, 0,  2,  6,  10, /**/ 0, 4,  8,  12, 14, 2,  6,  10,  //
      2,  4,  8,  12, 14, 0,  6,  10, /**/ 0, 2,  4,  8,  12, 14, 6,  10,  //
      6,  8,  12, 14, 0,  2,  4,  10, /**/ 0, 6,  8,  12, 14, 2,  4,  10,  //
      2,  6,  8,  12, 14, 0,  4,  10, /**/ 0, 2,  6,  8,  12, 14, 4,  10,  //
      4,  6,  8,  12, 14, 0,  2,  10, /**/ 0, 4,  6,  8,  12, 14, 2,  10,  //
      2,  4,  6,  8,  12, 14, 0,  10, /**/ 0, 2,  4,  6,  8,  12, 14, 10,  //
      10, 12, 14, 0,  2,  4,  6,  8,  /**/ 0, 10, 12, 14, 2,  4,  6,  8,   //
      2,  10, 12, 14, 0,  4,  6,  8,  /**/ 0, 2,  10, 12, 14, 4,  6,  8,   //
      4,  10, 12, 14, 0,  2,  6,  8,  /**/ 0, 4,  10, 12, 14, 2,  6,  8,   //
      2,  4,  10, 12, 14, 0,  6,  8,  /**/ 0, 2,  4,  10, 12, 14, 6,  8,   //
      6,  10, 12, 14, 0,  2,  4,  8,  /**/ 0, 6,  10, 12, 14, 2,  4,  8,   //
      2,  6,  10, 12, 14, 0,  4,  8,  /**/ 0, 2,  6,  10, 12, 14, 4,  8,   //
      4,  6,  10, 12, 14, 0,  2,  8,  /**/ 0, 4,  6,  10, 12, 14, 2,  8,   //
      2,  4,  6,  10, 12, 14, 0,  8,  /**/ 0, 2,  4,  6,  10, 12, 14, 8,   //
      8,  10, 12, 14, 0,  2,  4,  6,  /**/ 0, 8,  10, 12, 14, 2,  4,  6,   //
      2,  8,  10, 12, 14, 0,  4,  6,  /**/ 0, 2,  8,  10, 12, 14, 4,  6,   //
      4,  8,  10, 12, 14, 0,  2,  6,  /**/ 0, 4,  8,  10, 12, 14, 2,  6,   //
      2,  4,  8,  10, 12, 14, 0,  6,  /**/ 0, 2,  4,  8,  10, 12, 14, 6,   //
      6,  8,  10, 12, 14, 0,  2,  4,  /**/ 0, 6,  8,  10, 12, 14, 2,  4,   //
      2,  6,  8,  10, 12, 14, 0,  4,  /**/ 0, 2,  6,  8,  10, 12, 14, 4,   //
      4,  6,  8,  10, 12, 14, 0,  2,  /**/ 0, 4,  6,  8,  10, 12, 14, 2,   //
      2,  4,  6,  8,  10, 12, 14, 0,  /**/ 0, 2,  4,  6,  8,  10, 12, 14};

  const Vec128<uint8_t, 2 * N> byte_idx = Load8Bytes(d8, table + mask_bits * 8);
  const Vec128<uint16_t, N> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromNotBits(hwy::SizeTag<2> /*tag*/,
                                       uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  const Simd<uint16_t, N, 0> du;

  // NEON does not provide an equivalent of AVX2 permutevar, so we need byte
  // indices for VTBL (one vector's worth for each of 256 combinations of
  // 8 mask bits). Loading them directly would require 4 KiB. We can instead
  // store lane indices and convert to byte indices (2*lane + 0..1), with the
  // doubling baked into the table. AVX2 Compress32 stores eight 4-bit lane
  // indices (total 1 KiB), broadcasts them into each 32-bit lane and shifts.
  // Here, 16-bit lanes are too narrow to hold all bits, and unpacking nibbles
  // is likely more costly than the higher cache footprint from storing bytes.
  alignas(16) static constexpr uint8_t table[256 * 8] = {
      // PrintCompressNot16x8Tables
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  6,  8,  10, 12, 14, 0,   //
      0, 4,  6,  8,  10, 12, 14, 2,  /**/ 4,  6,  8,  10, 12, 14, 0,  2,   //
      0, 2,  6,  8,  10, 12, 14, 4,  /**/ 2,  6,  8,  10, 12, 14, 0,  4,   //
      0, 6,  8,  10, 12, 14, 2,  4,  /**/ 6,  8,  10, 12, 14, 0,  2,  4,   //
      0, 2,  4,  8,  10, 12, 14, 6,  /**/ 2,  4,  8,  10, 12, 14, 0,  6,   //
      0, 4,  8,  10, 12, 14, 2,  6,  /**/ 4,  8,  10, 12, 14, 0,  2,  6,   //
      0, 2,  8,  10, 12, 14, 4,  6,  /**/ 2,  8,  10, 12, 14, 0,  4,  6,   //
      0, 8,  10, 12, 14, 2,  4,  6,  /**/ 8,  10, 12, 14, 0,  2,  4,  6,   //
      0, 2,  4,  6,  10, 12, 14, 8,  /**/ 2,  4,  6,  10, 12, 14, 0,  8,   //
      0, 4,  6,  10, 12, 14, 2,  8,  /**/ 4,  6,  10, 12, 14, 0,  2,  8,   //
      0, 2,  6,  10, 12, 14, 4,  8,  /**/ 2,  6,  10, 12, 14, 0,  4,  8,   //
      0, 6,  10, 12, 14, 2,  4,  8,  /**/ 6,  10, 12, 14, 0,  2,  4,  8,   //
      0, 2,  4,  10, 12, 14, 6,  8,  /**/ 2,  4,  10, 12, 14, 0,  6,  8,   //
      0, 4,  10, 12, 14, 2,  6,  8,  /**/ 4,  10, 12, 14, 0,  2,  6,  8,   //
      0, 2,  10, 12, 14, 4,  6,  8,  /**/ 2,  10, 12, 14, 0,  4,  6,  8,   //
      0, 10, 12, 14, 2,  4,  6,  8,  /**/ 10, 12, 14, 0,  2,  4,  6,  8,   //
      0, 2,  4,  6,  8,  12, 14, 10, /**/ 2,  4,  6,  8,  12, 14, 0,  10,  //
      0, 4,  6,  8,  12, 14, 2,  10, /**/ 4,  6,  8,  12, 14, 0,  2,  10,  //
      0, 2,  6,  8,  12, 14, 4,  10, /**/ 2,  6,  8,  12, 14, 0,  4,  10,  //
      0, 6,  8,  12, 14, 2,  4,  10, /**/ 6,  8,  12, 14, 0,  2,  4,  10,  //
      0, 2,  4,  8,  12, 14, 6,  10, /**/ 2,  4,  8,  12, 14, 0,  6,  10,  //
      0, 4,  8,  12, 14, 2,  6,  10, /**/ 4,  8,  12, 14, 0,  2,  6,  10,  //
      0, 2,  8,  12, 14, 4,  6,  10, /**/ 2,  8,  12, 14, 0,  4,  6,  10,  //
      0, 8,  12, 14, 2,  4,  6,  10, /**/ 8,  12, 14, 0,  2,  4,  6,  10,  //
      0, 2,  4,  6,  12, 14, 8,  10, /**/ 2,  4,  6,  12, 14, 0,  8,  10,  //
      0, 4,  6,  12, 14, 2,  8,  10, /**/ 4,  6,  12, 14, 0,  2,  8,  10,  //
      0, 2,  6,  12, 14, 4,  8,  10, /**/ 2,  6,  12, 14, 0,  4,  8,  10,  //
      0, 6,  12, 14, 2,  4,  8,  10, /**/ 6,  12, 14, 0,  2,  4,  8,  10,  //
      0, 2,  4,  12, 14, 6,  8,  10, /**/ 2,  4,  12, 14, 0,  6,  8,  10,  //
      0, 4,  12, 14, 2,  6,  8,  10, /**/ 4,  12, 14, 0,  2,  6,  8,  10,  //
      0, 2,  12, 14, 4,  6,  8,  10, /**/ 2,  12, 14, 0,  4,  6,  8,  10,  //
      0, 12, 14, 2,  4,  6,  8,  10, /**/ 12, 14, 0,  2,  4,  6,  8,  10,  //
      0, 2,  4,  6,  8,  10, 14, 12, /**/ 2,  4,  6,  8,  10, 14, 0,  12,  //
      0, 4,  6,  8,  10, 14, 2,  12, /**/ 4,  6,  8,  10, 14, 0,  2,  12,  //
      0, 2,  6,  8,  10, 14, 4,  12, /**/ 2,  6,  8,  10, 14, 0,  4,  12,  //
      0, 6,  8,  10, 14, 2,  4,  12, /**/ 6,  8,  10, 14, 0,  2,  4,  12,  //
      0, 2,  4,  8,  10, 14, 6,  12, /**/ 2,  4,  8,  10, 14, 0,  6,  12,  //
      0, 4,  8,  10, 14, 2,  6,  12, /**/ 4,  8,  10, 14, 0,  2,  6,  12,  //
      0, 2,  8,  10, 14, 4,  6,  12, /**/ 2,  8,  10, 14, 0,  4,  6,  12,  //
      0, 8,  10, 14, 2,  4,  6,  12, /**/ 8,  10, 14, 0,  2,  4,  6,  12,  //
      0, 2,  4,  6,  10, 14, 8,  12, /**/ 2,  4,  6,  10, 14, 0,  8,  12,  //
      0, 4,  6,  10, 14, 2,  8,  12, /**/ 4,  6,  10, 14, 0,  2,  8,  12,  //
      0, 2,  6,  10, 14, 4,  8,  12, /**/ 2,  6,  10, 14, 0,  4,  8,  12,  //
      0, 6,  10, 14, 2,  4,  8,  12, /**/ 6,  10, 14, 0,  2,  4,  8,  12,  //
      0, 2,  4,  10, 14, 6,  8,  12, /**/ 2,  4,  10, 14, 0,  6,  8,  12,  //
      0, 4,  10, 14, 2,  6,  8,  12, /**/ 4,  10, 14, 0,  2,  6,  8,  12,  //
      0, 2,  10, 14, 4,  6,  8,  12, /**/ 2,  10, 14, 0,  4,  6,  8,  12,  //
      0, 10, 14, 2,  4,  6,  8,  12, /**/ 10, 14, 0,  2,  4,  6,  8,  12,  //
      0, 2,  4,  6,  8,  14, 10, 12, /**/ 2,  4,  6,  8,  14, 0,  10, 12,  //
      0, 4,  6,  8,  14, 2,  10, 12, /**/ 4,  6,  8,  14, 0,  2,  10, 12,  //
      0, 2,  6,  8,  14, 4,  10, 12, /**/ 2,  6,  8,  14, 0,  4,  10, 12,  //
      0, 6,  8,  14, 2,  4,  10, 12, /**/ 6,  8,  14, 0,  2,  4,  10, 12,  //
      0, 2,  4,  8,  14, 6,  10, 12, /**/ 2,  4,  8,  14, 0,  6,  10, 12,  //
      0, 4,  8,  14, 2,  6,  10, 12, /**/ 4,  8,  14, 0,  2,  6,  10, 12,  //
      0, 2,  8,  14, 4,  6,  10, 12, /**/ 2,  8,  14, 0,  4,  6,  10, 12,  //
      0, 8,  14, 2,  4,  6,  10, 12, /**/ 8,  14, 0,  2,  4,  6,  10, 12,  //
      0, 2,  4,  6,  14, 8,  10, 12, /**/ 2,  4,  6,  14, 0,  8,  10, 12,  //
      0, 4,  6,  14, 2,  8,  10, 12, /**/ 4,  6,  14, 0,  2,  8,  10, 12,  //
      0, 2,  6,  14, 4,  8,  10, 12, /**/ 2,  6,  14, 0,  4,  8,  10, 12,  //
      0, 6,  14, 2,  4,  8,  10, 12, /**/ 6,  14, 0,  2,  4,  8,  10, 12,  //
      0, 2,  4,  14, 6,  8,  10, 12, /**/ 2,  4,  14, 0,  6,  8,  10, 12,  //
      0, 4,  14, 2,  6,  8,  10, 12, /**/ 4,  14, 0,  2,  6,  8,  10, 12,  //
      0, 2,  14, 4,  6,  8,  10, 12, /**/ 2,  14, 0,  4,  6,  8,  10, 12,  //
      0, 14, 2,  4,  6,  8,  10, 12, /**/ 14, 0,  2,  4,  6,  8,  10, 12,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  6,  8,  10, 12, 0,  14,  //
      0, 4,  6,  8,  10, 12, 2,  14, /**/ 4,  6,  8,  10, 12, 0,  2,  14,  //
      0, 2,  6,  8,  10, 12, 4,  14, /**/ 2,  6,  8,  10, 12, 0,  4,  14,  //
      0, 6,  8,  10, 12, 2,  4,  14, /**/ 6,  8,  10, 12, 0,  2,  4,  14,  //
      0, 2,  4,  8,  10, 12, 6,  14, /**/ 2,  4,  8,  10, 12, 0,  6,  14,  //
      0, 4,  8,  10, 12, 2,  6,  14, /**/ 4,  8,  10, 12, 0,  2,  6,  14,  //
      0, 2,  8,  10, 12, 4,  6,  14, /**/ 2,  8,  10, 12, 0,  4,  6,  14,  //
      0, 8,  10, 12, 2,  4,  6,  14, /**/ 8,  10, 12, 0,  2,  4,  6,  14,  //
      0, 2,  4,  6,  10, 12, 8,  14, /**/ 2,  4,  6,  10, 12, 0,  8,  14,  //
      0, 4,  6,  10, 12, 2,  8,  14, /**/ 4,  6,  10, 12, 0,  2,  8,  14,  //
      0, 2,  6,  10, 12, 4,  8,  14, /**/ 2,  6,  10, 12, 0,  4,  8,  14,  //
      0, 6,  10, 12, 2,  4,  8,  14, /**/ 6,  10, 12, 0,  2,  4,  8,  14,  //
      0, 2,  4,  10, 12, 6,  8,  14, /**/ 2,  4,  10, 12, 0,  6,  8,  14,  //
      0, 4,  10, 12, 2,  6,  8,  14, /**/ 4,  10, 12, 0,  2,  6,  8,  14,  //
      0, 2,  10, 12, 4,  6,  8,  14, /**/ 2,  10, 12, 0,  4,  6,  8,  14,  //
      0, 10, 12, 2,  4,  6,  8,  14, /**/ 10, 12, 0,  2,  4,  6,  8,  14,  //
      0, 2,  4,  6,  8,  12, 10, 14, /**/ 2,  4,  6,  8,  12, 0,  10, 14,  //
      0, 4,  6,  8,  12, 2,  10, 14, /**/ 4,  6,  8,  12, 0,  2,  10, 14,  //
      0, 2,  6,  8,  12, 4,  10, 14, /**/ 2,  6,  8,  12, 0,  4,  10, 14,  //
      0, 6,  8,  12, 2,  4,  10, 14, /**/ 6,  8,  12, 0,  2,  4,  10, 14,  //
      0, 2,  4,  8,  12, 6,  10, 14, /**/ 2,  4,  8,  12, 0,  6,  10, 14,  //
      0, 4,  8,  12, 2,  6,  10, 14, /**/ 4,  8,  12, 0,  2,  6,  10, 14,  //
      0, 2,  8,  12, 4,  6,  10, 14, /**/ 2,  8,  12, 0,  4,  6,  10, 14,  //
      0, 8,  12, 2,  4,  6,  10, 14, /**/ 8,  12, 0,  2,  4,  6,  10, 14,  //
      0, 2,  4,  6,  12, 8,  10, 14, /**/ 2,  4,  6,  12, 0,  8,  10, 14,  //
      0, 4,  6,  12, 2,  8,  10, 14, /**/ 4,  6,  12, 0,  2,  8,  10, 14,  //
      0, 2,  6,  12, 4,  8,  10, 14, /**/ 2,  6,  12, 0,  4,  8,  10, 14,  //
      0, 6,  12, 2,  4,  8,  10, 14, /**/ 6,  12, 0,  2,  4,  8,  10, 14,  //
      0, 2,  4,  12, 6,  8,  10, 14, /**/ 2,  4,  12, 0,  6,  8,  10, 14,  //
      0, 4,  12, 2,  6,  8,  10, 14, /**/ 4,  12, 0,  2,  6,  8,  10, 14,  //
      0, 2,  12, 4,  6,  8,  10, 14, /**/ 2,  12, 0,  4,  6,  8,  10, 14,  //
      0, 12, 2,  4,  6,  8,  10, 14, /**/ 12, 0,  2,  4,  6,  8,  10, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  6,  8,  10, 0,  12, 14,  //
      0, 4,  6,  8,  10, 2,  12, 14, /**/ 4,  6,  8,  10, 0,  2,  12, 14,  //
      0, 2,  6,  8,  10, 4,  12, 14, /**/ 2,  6,  8,  10, 0,  4,  12, 14,  //
      0, 6,  8,  10, 2,  4,  12, 14, /**/ 6,  8,  10, 0,  2,  4,  12, 14,  //
      0, 2,  4,  8,  10, 6,  12, 14, /**/ 2,  4,  8,  10, 0,  6,  12, 14,  //
      0, 4,  8,  10, 2,  6,  12, 14, /**/ 4,  8,  10, 0,  2,  6,  12, 14,  //
      0, 2,  8,  10, 4,  6,  12, 14, /**/ 2,  8,  10, 0,  4,  6,  12, 14,  //
      0, 8,  10, 2,  4,  6,  12, 14, /**/ 8,  10, 0,  2,  4,  6,  12, 14,  //
      0, 2,  4,  6,  10, 8,  12, 14, /**/ 2,  4,  6,  10, 0,  8,  12, 14,  //
      0, 4,  6,  10, 2,  8,  12, 14, /**/ 4,  6,  10, 0,  2,  8,  12, 14,  //
      0, 2,  6,  10, 4,  8,  12, 14, /**/ 2,  6,  10, 0,  4,  8,  12, 14,  //
      0, 6,  10, 2,  4,  8,  12, 14, /**/ 6,  10, 0,  2,  4,  8,  12, 14,  //
      0, 2,  4,  10, 6,  8,  12, 14, /**/ 2,  4,  10, 0,  6,  8,  12, 14,  //
      0, 4,  10, 2,  6,  8,  12, 14, /**/ 4,  10, 0,  2,  6,  8,  12, 14,  //
      0, 2,  10, 4,  6,  8,  12, 14, /**/ 2,  10, 0,  4,  6,  8,  12, 14,  //
      0, 10, 2,  4,  6,  8,  12, 14, /**/ 10, 0,  2,  4,  6,  8,  12, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  6,  8,  0,  10, 12, 14,  //
      0, 4,  6,  8,  2,  10, 12, 14, /**/ 4,  6,  8,  0,  2,  10, 12, 14,  //
      0, 2,  6,  8,  4,  10, 12, 14, /**/ 2,  6,  8,  0,  4,  10, 12, 14,  //
      0, 6,  8,  2,  4,  10, 12, 14, /**/ 6,  8,  0,  2,  4,  10, 12, 14,  //
      0, 2,  4,  8,  6,  10, 12, 14, /**/ 2,  4,  8,  0,  6,  10, 12, 14,  //
      0, 4,  8,  2,  6,  10, 12, 14, /**/ 4,  8,  0,  2,  6,  10, 12, 14,  //
      0, 2,  8,  4,  6,  10, 12, 14, /**/ 2,  8,  0,  4,  6,  10, 12, 14,  //
      0, 8,  2,  4,  6,  10, 12, 14, /**/ 8,  0,  2,  4,  6,  10, 12, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  6,  0,  8,  10, 12, 14,  //
      0, 4,  6,  2,  8,  10, 12, 14, /**/ 4,  6,  0,  2,  8,  10, 12, 14,  //
      0, 2,  6,  4,  8,  10, 12, 14, /**/ 2,  6,  0,  4,  8,  10, 12, 14,  //
      0, 6,  2,  4,  8,  10, 12, 14, /**/ 6,  0,  2,  4,  8,  10, 12, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  4,  0,  6,  8,  10, 12, 14,  //
      0, 4,  2,  6,  8,  10, 12, 14, /**/ 4,  0,  2,  6,  8,  10, 12, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 2,  0,  4,  6,  8,  10, 12, 14,  //
      0, 2,  4,  6,  8,  10, 12, 14, /**/ 0,  2,  4,  6,  8,  10, 12, 14};

  const Vec128<uint8_t, 2 * N> byte_idx = Load8Bytes(d8, table + mask_bits * 8);
  const Vec128<uint16_t, N> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromBits(hwy::SizeTag<4> /*tag*/,
                                    uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 16);

  // There are only 4 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[16 * 16] = {
      // PrintCompress32x4Tables
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  //
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  //
      4,  5,  6,  7,  0,  1,  2,  3,  8,  9,  10, 11, 12, 13, 14, 15,  //
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  //
      8,  9,  10, 11, 0,  1,  2,  3,  4,  5,  6,  7,  12, 13, 14, 15,  //
      0,  1,  2,  3,  8,  9,  10, 11, 4,  5,  6,  7,  12, 13, 14, 15,  //
      4,  5,  6,  7,  8,  9,  10, 11, 0,  1,  2,  3,  12, 13, 14, 15,  //
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,  //
      12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,  //
      0,  1,  2,  3,  12, 13, 14, 15, 4,  5,  6,  7,  8,  9,  10, 11,  //
      4,  5,  6,  7,  12, 13, 14, 15, 0,  1,  2,  3,  8,  9,  10, 11,  //
      0,  1,  2,  3,  4,  5,  6,  7,  12, 13, 14, 15, 8,  9,  10, 11,  //
      8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,   //
      0,  1,  2,  3,  8,  9,  10, 11, 12, 13, 14, 15, 4,  5,  6,  7,   //
      4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,   //
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15};
  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromNotBits(hwy::SizeTag<4> /*tag*/,
                                       uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 16);

  // There are only 4 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[16 * 16] = {
      // PrintCompressNot32x4Tables
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 4,  5,
      6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  0,  1,  2,  3,
      8,  9,  10, 11, 12, 13, 14, 15, 4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
      14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  0,  1,  2,  3,  4,  5,  6,  7,
      12, 13, 14, 15, 8,  9,  10, 11, 4,  5,  6,  7,  12, 13, 14, 15, 0,  1,
      2,  3,  8,  9,  10, 11, 0,  1,  2,  3,  12, 13, 14, 15, 4,  5,  6,  7,
      8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
      10, 11, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
      4,  5,  6,  7,  8,  9,  10, 11, 0,  1,  2,  3,  12, 13, 14, 15, 0,  1,
      2,  3,  8,  9,  10, 11, 4,  5,  6,  7,  12, 13, 14, 15, 8,  9,  10, 11,
      0,  1,  2,  3,  4,  5,  6,  7,  12, 13, 14, 15, 0,  1,  2,  3,  4,  5,
      6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 4,  5,  6,  7,  0,  1,  2,  3,
      8,  9,  10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,
      10, 11, 12, 13, 14, 15, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
      12, 13, 14, 15};
  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

#if HWY_HAVE_INTEGER64 || HWY_HAVE_FLOAT64

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromBits(hwy::SizeTag<8> /*tag*/,
                                    uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[64] = {
      // PrintCompress64x2Tables
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IdxFromNotBits(hwy::SizeTag<8> /*tag*/,
                                       uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[4 * 16] = {
      // PrintCompressNot64x2Tables
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

#endif

// Helper function called by both Compress and CompressStore - avoids a
// redundant BitsFromMask in the latter.
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Compress(Vec128<T, N> v, uint64_t mask_bits) {
  const auto idx =
      detail::IdxFromBits<T, N>(hwy::SizeTag<sizeof(T)>(), mask_bits);
  using D = DFromV<decltype(v)>;
  const RebindToSigned<D> di;
  return BitCast(D(), TableLookupBytes(BitCast(di, v), BitCast(di, idx)));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> CompressNot(Vec128<T, N> v, uint64_t mask_bits) {
  const auto idx =
      detail::IdxFromNotBits<T, N>(hwy::SizeTag<sizeof(T)>(), mask_bits);
  using D = DFromV<decltype(v)>;
  const RebindToSigned<D> di;
  return BitCast(D(), TableLookupBytes(BitCast(di, v), BitCast(di, idx)));
}

}  // namespace detail

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> Compress(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

// Two lanes: conditional swap
template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> Compress(Vec128<T, N> v, Mask128<T, N> mask) {
  // If mask[1] = 1 and mask[0] = 0, then swap both halves, else keep.
  const DFromV<decltype(v)> d;
  const Vec128<T, N> m = VecFromMask(d, mask);
  const Vec128<T, N> maskL = DupEven(m);
  const Vec128<T, N> maskH = DupOdd(m);
  const Vec128<T, N> swap = AndNot(maskL, maskH);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

// General case, 2 or 4 byte lanes
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> Compress(Vec128<T, N> v, Mask128<T, N> mask) {
  return detail::Compress(v, detail::BitsFromMask(mask));
}

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> CompressNot(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

// Two lanes: conditional swap
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> CompressNot(Vec128<T> v, Mask128<T> mask) {
  // If mask[1] = 0 and mask[0] = 1, then swap both halves, else keep.
  const DFromV<decltype(v)> d;
  const Vec128<T> m = VecFromMask(d, mask);
  const Vec128<T> maskL = DupEven(m);
  const Vec128<T> maskH = DupOdd(m);
  const Vec128<T> swap = AndNot(maskH, maskL);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

// General case, 2 or 4 byte lanes
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> CompressNot(Vec128<T, N> v, Mask128<T, N> mask) {
  // For partial vectors, we cannot pull the Not() into the table because
  // BitsFromMask clears the upper bits.
  if (N < 16 / sizeof(T)) {
    return detail::Compress(v, detail::BitsFromMask(Not(mask)));
  }
  return detail::CompressNot(v, detail::BitsFromMask(mask));
}

// ------------------------------ CompressBlocksNot
HWY_API Vec128<uint64_t> CompressBlocksNot(Vec128<uint64_t> v,
                                           Mask128<uint64_t> /* m */) {
  return v;
}

// ------------------------------ CompressBits

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_INLINE Vec128<T, N> CompressBits(Vec128<T, N> v,
                                     const uint8_t* HWY_RESTRICT bits) {
  uint64_t mask_bits = 0;
  constexpr size_t kNumBytes = (N + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (N < 8) {
    mask_bits &= (1ull << N) - 1;
  }

  return detail::Compress(v, mask_bits);
}

// ------------------------------ CompressStore
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(VFromD<D> v, MFromD<D> mask, D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  StoreU(detail::Compress(v, mask_bits), d, unaligned);
  return PopCount(mask_bits);
}

// ------------------------------ CompressBlendedStore
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;  // so we can support fp16/bf16
  const uint64_t mask_bits = detail::BitsFromMask(m);
  const size_t count = PopCount(mask_bits);
  const MFromD<D> store_mask = RebindMask(d, FirstN(du, count));
  const VFromD<decltype(du)> compressed =
      detail::Compress(BitCast(du, v), mask_bits);
  BlendedStore(BitCast(d, compressed), store_mask, d, unaligned);
  return count;
}

// ------------------------------ CompressBitsStore

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  uint64_t mask_bits = 0;
  constexpr size_t kNumBytes = (d.MaxLanes() + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (d.MaxLanes() < 8) {
    mask_bits &= (1ull << d.MaxLanes()) - 1;
  }

  StoreU(detail::Compress(v, mask_bits), d, unaligned);
  return PopCount(mask_bits);
}

// ------------------------------ LoadInterleaved2

// Per-target flag to prevent generic_ops-inl.h from defining LoadInterleaved2.
#ifdef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_INTERLEAVED
#endif

namespace detail {

#define HWY_NEON_BUILD_TPL_HWY_LOAD_INT
#define HWY_NEON_BUILD_ARG_HWY_LOAD_INT from

#if HWY_ARCH_ARM_A64
#define HWY_IF_LOAD_INT(D) \
  HWY_IF_V_SIZE_GT_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D)
#define HWY_NEON_DEF_FUNCTION_LOAD_INT(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_ALL_TYPES(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)
#else
// Exclude 64x2 and f64x1, which are only supported on aarch64; also exclude any
// emulated types.
#define HWY_IF_LOAD_INT(D)                                                 \
  HWY_IF_V_SIZE_GT_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),                 \
      hwy::EnableIf<(HWY_MAX_LANES_D(D) == 1 || sizeof(TFromD<D>) < 8)>* = \
          nullptr
#define HWY_NEON_DEF_FUNCTION_LOAD_INT(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INT_8_16_32(name, prefix, infix, args)    \
  HWY_NEON_DEF_FUNCTION_UINT_8_16_32(name, prefix, infix, args)   \
  HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)      \
  HWY_NEON_DEF_FUNCTION_FLOAT_16_32(name, prefix, infix, args)    \
  HWY_NEON_DEF_FUNCTION(int64, 1, name, prefix, infix, s64, args) \
  HWY_NEON_DEF_FUNCTION(uint64, 1, name, prefix, infix, u64, args)
#endif  // HWY_ARCH_ARM_A64

// Must return raw tuple because Tuple2 lack a ctor, and we cannot use
// brace-initialization in HWY_NEON_DEF_FUNCTION because some functions return
// void.
#define HWY_NEON_BUILD_RET_HWY_LOAD_INT(type, size) \
  decltype(Tuple2<type##_t, size>().raw)
// Tuple tag arg allows overloading (cannot just overload on return type)
#define HWY_NEON_BUILD_PARAM_HWY_LOAD_INT(type, size) \
  const NativeLaneType<type##_t>*from, Tuple2<type##_t, size>
HWY_NEON_DEF_FUNCTION_LOAD_INT(LoadInterleaved2, vld2, _, HWY_LOAD_INT)
#undef HWY_NEON_BUILD_RET_HWY_LOAD_INT
#undef HWY_NEON_BUILD_PARAM_HWY_LOAD_INT

#define HWY_NEON_BUILD_RET_HWY_LOAD_INT(type, size) \
  decltype(Tuple3<type##_t, size>().raw)
#define HWY_NEON_BUILD_PARAM_HWY_LOAD_INT(type, size) \
  const NativeLaneType<type##_t>*from, Tuple3<type##_t, size>
HWY_NEON_DEF_FUNCTION_LOAD_INT(LoadInterleaved3, vld3, _, HWY_LOAD_INT)
#undef HWY_NEON_BUILD_PARAM_HWY_LOAD_INT
#undef HWY_NEON_BUILD_RET_HWY_LOAD_INT

#define HWY_NEON_BUILD_RET_HWY_LOAD_INT(type, size) \
  decltype(Tuple4<type##_t, size>().raw)
#define HWY_NEON_BUILD_PARAM_HWY_LOAD_INT(type, size) \
  const NativeLaneType<type##_t>*from, Tuple4<type##_t, size>
HWY_NEON_DEF_FUNCTION_LOAD_INT(LoadInterleaved4, vld4, _, HWY_LOAD_INT)
#undef HWY_NEON_BUILD_PARAM_HWY_LOAD_INT
#undef HWY_NEON_BUILD_RET_HWY_LOAD_INT

#undef HWY_NEON_DEF_FUNCTION_LOAD_INT
#undef HWY_NEON_BUILD_TPL_HWY_LOAD_INT
#undef HWY_NEON_BUILD_ARG_HWY_LOAD_INT

}  // namespace detail

template <class D, HWY_IF_LOAD_INT(D), typename T = TFromD<D>>
HWY_API void LoadInterleaved2(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1) {
  auto raw = detail::LoadInterleaved2(detail::NativeLanePointer(unaligned),
                                      detail::Tuple2<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
}

// <= 32 bits: avoid loading more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void LoadInterleaved2(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1) {
  // The smallest vector registers are 64-bits and we want space for two.
  alignas(16) T buf[2 * 8 / sizeof(T)] = {};
  CopyBytes<d.MaxBytes() * 2>(unaligned, buf);
  auto raw = detail::LoadInterleaved2(detail::NativeLanePointer(buf),
                                      detail::Tuple2<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved2(D d, T* HWY_RESTRICT unaligned, Vec128<T>& v0,
                              Vec128<T>& v1) {
  const Half<decltype(d)> dh;
  VFromD<decltype(dh)> v00, v10, v01, v11;
  LoadInterleaved2(dh, detail::NativeLanePointer(unaligned), v00, v10);
  LoadInterleaved2(dh, detail::NativeLanePointer(unaligned + 2), v01, v11);
  v0 = Combine(d, v01, v00);
  v1 = Combine(d, v11, v10);
}
#endif  // HWY_ARCH_ARM_V7

// ------------------------------ LoadInterleaved3

template <class D, HWY_IF_LOAD_INT(D), typename T = TFromD<D>>
HWY_API void LoadInterleaved3(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2) {
  auto raw = detail::LoadInterleaved3(detail::NativeLanePointer(unaligned),
                                      detail::Tuple3<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
  v2 = VFromD<D>(raw.val[2]);
}

// <= 32 bits: avoid writing more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void LoadInterleaved3(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2) {
  // The smallest vector registers are 64-bits and we want space for three.
  alignas(16) T buf[3 * 8 / sizeof(T)] = {};
  CopyBytes<d.MaxBytes() * 3>(unaligned, buf);
  auto raw = detail::LoadInterleaved3(detail::NativeLanePointer(buf),
                                      detail::Tuple3<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
  v2 = VFromD<D>(raw.val[2]);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved3(D d, const TFromD<D>* HWY_RESTRICT unaligned,
                              Vec128<T>& v0, Vec128<T>& v1, Vec128<T>& v2) {
  const Half<decltype(d)> dh;
  VFromD<decltype(dh)> v00, v10, v20, v01, v11, v21;
  LoadInterleaved3(dh, detail::NativeLanePointer(unaligned), v00, v10, v20);
  LoadInterleaved3(dh, detail::NativeLanePointer(unaligned + 3), v01, v11, v21);
  v0 = Combine(d, v01, v00);
  v1 = Combine(d, v11, v10);
  v2 = Combine(d, v21, v20);
}
#endif  // HWY_ARCH_ARM_V7

// ------------------------------ LoadInterleaved4

template <class D, HWY_IF_LOAD_INT(D), typename T = TFromD<D>>
HWY_API void LoadInterleaved4(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2,
                              VFromD<D>& v3) {
  auto raw = detail::LoadInterleaved4(detail::NativeLanePointer(unaligned),
                                      detail::Tuple4<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
  v2 = VFromD<D>(raw.val[2]);
  v3 = VFromD<D>(raw.val[3]);
}

// <= 32 bits: avoid writing more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void LoadInterleaved4(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2,
                              VFromD<D>& v3) {
  alignas(16) T buf[4 * 8 / sizeof(T)] = {};
  CopyBytes<d.MaxBytes() * 4>(unaligned, buf);
  auto raw = detail::LoadInterleaved4(detail::NativeLanePointer(buf),
                                      detail::Tuple4<T, d.MaxLanes()>());
  v0 = VFromD<D>(raw.val[0]);
  v1 = VFromD<D>(raw.val[1]);
  v2 = VFromD<D>(raw.val[2]);
  v3 = VFromD<D>(raw.val[3]);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void LoadInterleaved4(D d, const T* HWY_RESTRICT unaligned,
                              Vec128<T>& v0, Vec128<T>& v1, Vec128<T>& v2,
                              Vec128<T>& v3) {
  const Half<decltype(d)> dh;
  VFromD<decltype(dh)> v00, v10, v20, v30, v01, v11, v21, v31;
  LoadInterleaved4(dh, detail::NativeLanePointer(unaligned), v00, v10, v20,
                   v30);
  LoadInterleaved4(dh, detail::NativeLanePointer(unaligned + 4), v01, v11, v21,
                   v31);
  v0 = Combine(d, v01, v00);
  v1 = Combine(d, v11, v10);
  v2 = Combine(d, v21, v20);
  v3 = Combine(d, v31, v30);
}
#endif  // HWY_ARCH_ARM_V7

#undef HWY_IF_LOAD_INT

// ------------------------------ StoreInterleaved2

namespace detail {
#define HWY_NEON_BUILD_TPL_HWY_STORE_INT
#define HWY_NEON_BUILD_RET_HWY_STORE_INT(type, size) void
#define HWY_NEON_BUILD_ARG_HWY_STORE_INT to, tup.raw

#if HWY_ARCH_ARM_A64
#define HWY_IF_STORE_INT(D) \
  HWY_IF_V_SIZE_GT_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D)
#define HWY_NEON_DEF_FUNCTION_STORE_INT(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_ALL_TYPES(name, prefix, infix, args)       \
  HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)
#else
// Exclude 64x2 and f64x1, which are only supported on aarch64; also exclude any
// emulated types.
#define HWY_IF_STORE_INT(D)                                                \
  HWY_IF_V_SIZE_GT_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),                 \
      hwy::EnableIf<(HWY_MAX_LANES_D(D) == 1 || sizeof(TFromD<D>) < 8)>* = \
          nullptr
#define HWY_NEON_DEF_FUNCTION_STORE_INT(name, prefix, infix, args) \
  HWY_NEON_DEF_FUNCTION_INT_8_16_32(name, prefix, infix, args)     \
  HWY_NEON_DEF_FUNCTION_UINT_8_16_32(name, prefix, infix, args)    \
  HWY_NEON_DEF_FUNCTION_BFLOAT_16(name, prefix, infix, args)       \
  HWY_NEON_DEF_FUNCTION_FLOAT_16_32(name, prefix, infix, args)     \
  HWY_NEON_DEF_FUNCTION(int64, 1, name, prefix, infix, s64, args)  \
  HWY_NEON_DEF_FUNCTION(uint64, 1, name, prefix, infix, u64, args)
#endif  // HWY_ARCH_ARM_A64

#define HWY_NEON_BUILD_PARAM_HWY_STORE_INT(type, size) \
  Tuple2<type##_t, size> tup, NativeLaneType<type##_t>*to
HWY_NEON_DEF_FUNCTION_STORE_INT(StoreInterleaved2, vst2, _, HWY_STORE_INT)
#undef HWY_NEON_BUILD_PARAM_HWY_STORE_INT

#define HWY_NEON_BUILD_PARAM_HWY_STORE_INT(type, size) \
  Tuple3<type##_t, size> tup, NativeLaneType<type##_t>*to
HWY_NEON_DEF_FUNCTION_STORE_INT(StoreInterleaved3, vst3, _, HWY_STORE_INT)
#undef HWY_NEON_BUILD_PARAM_HWY_STORE_INT

#define HWY_NEON_BUILD_PARAM_HWY_STORE_INT(type, size) \
  Tuple4<type##_t, size> tup, NativeLaneType<type##_t>*to
HWY_NEON_DEF_FUNCTION_STORE_INT(StoreInterleaved4, vst4, _, HWY_STORE_INT)
#undef HWY_NEON_BUILD_PARAM_HWY_STORE_INT

#undef HWY_NEON_DEF_FUNCTION_STORE_INT
#undef HWY_NEON_BUILD_TPL_HWY_STORE_INT
#undef HWY_NEON_BUILD_RET_HWY_STORE_INT
#undef HWY_NEON_BUILD_ARG_HWY_STORE_INT
}  // namespace detail

template <class D, HWY_IF_STORE_INT(D), typename T = TFromD<D>>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  detail::Tuple2<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw}}};
  detail::StoreInterleaved2(tup, detail::NativeLanePointer(unaligned));
}

// <= 32 bits: avoid writing more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  alignas(16) T buf[2 * 8 / sizeof(T)];
  detail::Tuple2<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw}}};
  detail::StoreInterleaved2(tup, detail::NativeLanePointer(buf));
  CopyBytes<d.MaxBytes() * 2>(buf, unaligned);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved2(Vec128<T> v0, Vec128<T> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  const Half<decltype(d)> dh;
  StoreInterleaved2(LowerHalf(dh, v0), LowerHalf(dh, v1), dh,
                    detail::NativeLanePointer(unaligned));
  StoreInterleaved2(UpperHalf(dh, v0), UpperHalf(dh, v1), dh,
                    detail::NativeLanePointer(unaligned + 2));
}
#endif  // HWY_ARCH_ARM_V7

// ------------------------------ StoreInterleaved3

template <class D, HWY_IF_STORE_INT(D), typename T = TFromD<D>>
HWY_API void StoreInterleaved3(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2, D d,
                               T* HWY_RESTRICT unaligned) {
  detail::Tuple3<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw, v2.raw}}};
  detail::StoreInterleaved3(tup, detail::NativeLanePointer(unaligned));
}

// <= 32 bits: avoid writing more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void StoreInterleaved3(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2, D d,
                               T* HWY_RESTRICT unaligned) {
  alignas(16) T buf[3 * 8 / sizeof(T)];
  detail::Tuple3<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw, v2.raw}}};
  detail::StoreInterleaved3(tup, detail::NativeLanePointer(buf));
  CopyBytes<d.MaxBytes() * 3>(buf, unaligned);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved3(Vec128<T> v0, Vec128<T> v1, Vec128<T> v2, D d,
                               T* HWY_RESTRICT unaligned) {
  const Half<decltype(d)> dh;
  StoreInterleaved3(LowerHalf(dh, v0), LowerHalf(dh, v1), LowerHalf(dh, v2), dh,
                    detail::NativeLanePointer(unaligned));
  StoreInterleaved3(UpperHalf(dh, v0), UpperHalf(dh, v1), UpperHalf(dh, v2), dh,
                    detail::NativeLanePointer(unaligned + 3));
}
#endif  // HWY_ARCH_ARM_V7

// ------------------------------ StoreInterleaved4

template <class D, HWY_IF_STORE_INT(D), typename T = TFromD<D>>
HWY_API void StoreInterleaved4(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2,
                               VFromD<D> v3, D d, T* HWY_RESTRICT unaligned) {
  detail::Tuple4<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw, v2.raw, v3.raw}}};
  detail::StoreInterleaved4(tup, detail::NativeLanePointer(unaligned));
}

// <= 32 bits: avoid writing more than N bytes by copying to buffer
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_NEON_IF_NOT_EMULATED_D(D),
          typename T = TFromD<D>>
HWY_API void StoreInterleaved4(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2,
                               VFromD<D> v3, D d, T* HWY_RESTRICT unaligned) {
  alignas(16) T buf[4 * 8 / sizeof(T)];
  detail::Tuple4<T, d.MaxLanes()> tup = {{{v0.raw, v1.raw, v2.raw, v3.raw}}};
  detail::StoreInterleaved4(tup, detail::NativeLanePointer(buf));
  CopyBytes<d.MaxBytes() * 4>(buf, unaligned);
}

#if HWY_ARCH_ARM_V7
// 64x2: split into two 64x1
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8),
          HWY_NEON_IF_NOT_EMULATED_D(D)>
HWY_API void StoreInterleaved4(Vec128<T> v0, Vec128<T> v1, Vec128<T> v2,
                               Vec128<T> v3, D d, T* HWY_RESTRICT unaligned) {
  const Half<decltype(d)> dh;
  StoreInterleaved4(LowerHalf(dh, v0), LowerHalf(dh, v1), LowerHalf(dh, v2),
                    LowerHalf(dh, v3), dh,
                    detail::NativeLanePointer(unaligned));
  StoreInterleaved4(UpperHalf(dh, v0), UpperHalf(dh, v1), UpperHalf(dh, v2),
                    UpperHalf(dh, v3), dh,
                    detail::NativeLanePointer(unaligned + 4));
}
#endif  // HWY_ARCH_ARM_V7

#undef HWY_IF_STORE_INT

// Fall back on generic Load/StoreInterleaved[234] for any emulated types.
// Requires HWY_GENERIC_IF_EMULATED_D mirrors HWY_NEON_IF_EMULATED_D.

// ------------------------------ Additional mask logical operations
template <class T>
HWY_API Mask128<T, 1> SetAtOrAfterFirst(Mask128<T, 1> mask) {
  return mask;
}
template <class T>
HWY_API Mask128<T, 2> SetAtOrAfterFirst(Mask128<T, 2> mask) {
  const FixedTag<T, 2> d;
  const auto vmask = VecFromMask(d, mask);
  return MaskFromVec(Or(vmask, InterleaveLower(vmask, vmask)));
}
template <class T, size_t N, HWY_IF_LANES_GT(N, 2), HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Mask128<T, N> SetAtOrAfterFirst(Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  const auto vmask = VecFromMask(d, mask);
  const auto neg_vmask =
      ResizeBitCast(d, Neg(ResizeBitCast(Full64<int64_t>(), vmask)));
  return MaskFromVec(Or(vmask, neg_vmask));
}
template <class T, HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Mask128<T> SetAtOrAfterFirst(Mask128<T> mask) {
  const Full128<T> d;
  const Repartition<int64_t, decltype(d)> di64;

  auto vmask = BitCast(di64, VecFromMask(d, mask));
  vmask = Or(vmask, Neg(vmask));

  // Copy the sign bit of the first int64_t lane to the second int64_t lane
  const auto vmask2 = BroadcastSignBit(InterleaveLower(Zero(di64), vmask));
  return MaskFromVec(BitCast(d, Or(vmask, vmask2)));
}

template <class T, size_t N>
HWY_API Mask128<T, N> SetBeforeFirst(Mask128<T, N> mask) {
  return Not(SetAtOrAfterFirst(mask));
}

template <class T>
HWY_API Mask128<T, 1> SetOnlyFirst(Mask128<T, 1> mask) {
  return mask;
}
template <class T>
HWY_API Mask128<T, 2> SetOnlyFirst(Mask128<T, 2> mask) {
  const FixedTag<T, 2> d;
  const RebindToSigned<decltype(d)> di;

  const auto vmask = BitCast(di, VecFromMask(d, mask));
  const auto zero = Zero(di);
  const auto vmask2 = VecFromMask(di, InterleaveLower(zero, vmask) == zero);
  return MaskFromVec(BitCast(d, And(vmask, vmask2)));
}
template <class T, size_t N, HWY_IF_LANES_GT(N, 2), HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Mask128<T, N> SetOnlyFirst(Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  const RebindToSigned<decltype(d)> di;

  const auto vmask = ResizeBitCast(Full64<int64_t>(), VecFromMask(d, mask));
  const auto only_first_vmask =
      BitCast(d, Neg(ResizeBitCast(di, And(vmask, Neg(vmask)))));
  return MaskFromVec(only_first_vmask);
}
template <class T, HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Mask128<T> SetOnlyFirst(Mask128<T> mask) {
  const Full128<T> d;
  const RebindToSigned<decltype(d)> di;
  const Repartition<int64_t, decltype(d)> di64;

  const auto zero = Zero(di64);
  const auto vmask = BitCast(di64, VecFromMask(d, mask));
  const auto vmask2 = VecFromMask(di64, InterleaveLower(zero, vmask) == zero);
  const auto only_first_vmask = Neg(BitCast(di, And(vmask, Neg(vmask))));
  return MaskFromVec(BitCast(d, And(only_first_vmask, BitCast(di, vmask2))));
}

template <class T>
HWY_API Mask128<T, 1> SetAtOrBeforeFirst(Mask128<T, 1> /*mask*/) {
  const FixedTag<T, 1> d;
  const RebindToSigned<decltype(d)> di;
  using TI = MakeSigned<T>;

  return RebindMask(d, MaskFromVec(Set(di, TI(-1))));
}
template <class T, size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Mask128<T, N> SetAtOrBeforeFirst(Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  return SetBeforeFirst(MaskFromVec(ShiftLeftLanes<1>(VecFromMask(d, mask))));
}

// ------------------------------ Lt128

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Lt128(D d, VFromD<D> a, VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "T must be u64");
  // Truth table of Eq and Lt for Hi and Lo u64.
  // (removed lines with (=H && cH) or (=L && cL) - cannot both be true)
  // =H =L cH cL  | out = cH | (=H & cL)
  //  0  0  0  0  |  0
  //  0  0  0  1  |  0
  //  0  0  1  0  |  1
  //  0  0  1  1  |  1
  //  0  1  0  0  |  0
  //  0  1  0  1  |  0
  //  0  1  1  0  |  1
  //  1  0  0  0  |  0
  //  1  0  0  1  |  1
  //  1  1  0  0  |  0
  const MFromD<D> eqHL = Eq(a, b);
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  // We need to bring cL to the upper lane/bit corresponding to cH. Comparing
  // the result of InterleaveUpper/Lower requires 9 ops, whereas shifting the
  // comparison result leftwards requires only 4. IfThenElse compiles to the
  // same code as OrAnd().
  const VFromD<D> ltLx = DupEven(ltHL);
  const VFromD<D> outHx = IfThenElse(eqHL, ltLx, ltHL);
  return MaskFromVec(DupOdd(outHx));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Lt128Upper(D d, VFromD<D> a, VFromD<D> b) {
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  return MaskFromVec(InterleaveUpper(d, ltHL, ltHL));
}

// ------------------------------ Eq128

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Eq128(D d, VFromD<D> a, VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "T must be u64");
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  return MaskFromVec(And(Reverse2(d, eqHL), eqHL));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Eq128Upper(D d, VFromD<D> a, VFromD<D> b) {
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  return MaskFromVec(InterleaveUpper(d, eqHL, eqHL));
}

// ------------------------------ Ne128

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Ne128(D d, VFromD<D> a, VFromD<D> b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "T must be u64");
  const VFromD<D> neHL = VecFromMask(d, Ne(a, b));
  return MaskFromVec(Or(Reverse2(d, neHL), neHL));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Ne128Upper(D d, VFromD<D> a, VFromD<D> b) {
  const VFromD<D> neHL = VecFromMask(d, Ne(a, b));
  return MaskFromVec(InterleaveUpper(d, neHL, neHL));
}

// ------------------------------ Min128, Max128 (Lt128)

// Without a native OddEven, it seems infeasible to go faster than Lt128.
template <class D>
HWY_INLINE VFromD<D> Min128(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128(d, a, b), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Max128(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128(d, b, a), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Min128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, a, b), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Max128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, b, a), a, b);
}

// -------------------- LeadingZeroCount, TrailingZeroCount, HighestSetBitIndex

#ifdef HWY_NATIVE_LEADING_ZERO_COUNT
#undef HWY_NATIVE_LEADING_ZERO_COUNT
#else
#define HWY_NATIVE_LEADING_ZERO_COUNT
#endif

HWY_NEON_DEF_FUNCTION_INT_8_16_32(LeadingZeroCount, vclz, _, 1)
HWY_NEON_DEF_FUNCTION_UINT_8_16_32(LeadingZeroCount, vclz, _, 1)

template <class V, HWY_IF_UI64_D(DFromV<V>)>
HWY_API V LeadingZeroCount(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint32_t, decltype(d)> du32;

  const auto v_k32 = BitCast(du32, Set(du, 32));
  const auto v_u32_lzcnt = LeadingZeroCount(BitCast(du32, v)) + v_k32;
  const auto v_u32_lo_lzcnt =
      And(v_u32_lzcnt, BitCast(du32, Set(du, 0xFFFFFFFFu)));
  const auto v_u32_hi_lzcnt =
      BitCast(du32, ShiftRight<32>(BitCast(du, v_u32_lzcnt)));

  return BitCast(
      d, IfThenElse(v_u32_hi_lzcnt == v_k32, v_u32_lo_lzcnt, v_u32_hi_lzcnt));
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V HighestSetBitIndex(V v) {
  const DFromV<decltype(v)> d;
  using T = TFromD<decltype(d)>;
  return BitCast(d, Set(d, T{sizeof(T) * 8 - 1}) - LeadingZeroCount(v));
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V), HWY_IF_T_SIZE_V(V, 1)>
HWY_API V TrailingZeroCount(V v) {
  return LeadingZeroCount(ReverseBits(v));
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API V TrailingZeroCount(V v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  return LeadingZeroCount(
      ReverseLaneBytes(BitCast(d, ReverseBits(BitCast(du8, v)))));
}

namespace detail {  // for code folding
#if HWY_ARCH_ARM_V7
#undef vuzp1_s8
#undef vuzp1_u8
#undef vuzp1_s16
#undef vuzp1_u16
#undef vuzp1_s32
#undef vuzp1_u32
#undef vuzp1_f32
#undef vuzp1q_s8
#undef vuzp1q_u8
#undef vuzp1q_s16
#undef vuzp1q_u16
#undef vuzp1q_s32
#undef vuzp1q_u32
#undef vuzp1q_f32
#undef vuzp2_s8
#undef vuzp2_u8
#undef vuzp2_s16
#undef vuzp2_u16
#undef vuzp2_s32
#undef vuzp2_u32
#undef vuzp2_f32
#undef vuzp2q_s8
#undef vuzp2q_u8
#undef vuzp2q_s16
#undef vuzp2q_u16
#undef vuzp2q_s32
#undef vuzp2q_u32
#undef vuzp2q_f32
#undef vzip1_s8
#undef vzip1_u8
#undef vzip1_s16
#undef vzip1_u16
#undef vzip1_s32
#undef vzip1_u32
#undef vzip1_f32
#undef vzip1q_s8
#undef vzip1q_u8
#undef vzip1q_s16
#undef vzip1q_u16
#undef vzip1q_s32
#undef vzip1q_u32
#undef vzip1q_f32
#undef vzip2_s8
#undef vzip2_u8
#undef vzip2_s16
#undef vzip2_u16
#undef vzip2_s32
#undef vzip2_u32
#undef vzip2_f32
#undef vzip2q_s8
#undef vzip2q_u8
#undef vzip2q_s16
#undef vzip2q_u16
#undef vzip2q_s32
#undef vzip2q_u32
#undef vzip2q_f32
#endif

#undef HWY_NEON_BUILD_ARG_1
#undef HWY_NEON_BUILD_ARG_2
#undef HWY_NEON_BUILD_ARG_3
#undef HWY_NEON_BUILD_PARAM_1
#undef HWY_NEON_BUILD_PARAM_2
#undef HWY_NEON_BUILD_PARAM_3
#undef HWY_NEON_BUILD_RET_1
#undef HWY_NEON_BUILD_RET_2
#undef HWY_NEON_BUILD_RET_3
#undef HWY_NEON_BUILD_TPL_1
#undef HWY_NEON_BUILD_TPL_2
#undef HWY_NEON_BUILD_TPL_3
#undef HWY_NEON_DEF_FUNCTION
#undef HWY_NEON_DEF_FUNCTION_ALL_FLOATS
#undef HWY_NEON_DEF_FUNCTION_ALL_TYPES
#undef HWY_NEON_DEF_FUNCTION_BFLOAT_16
#undef HWY_NEON_DEF_FUNCTION_FLOAT_16
#undef HWY_NEON_DEF_FUNCTION_FLOAT_16_32
#undef HWY_NEON_DEF_FUNCTION_FLOAT_32
#undef HWY_NEON_DEF_FUNCTION_FLOAT_64
#undef HWY_NEON_DEF_FUNCTION_FULL_UI
#undef HWY_NEON_DEF_FUNCTION_FULL_UI_64
#undef HWY_NEON_DEF_FUNCTION_FULL_UIF_64
#undef HWY_NEON_DEF_FUNCTION_INT_16
#undef HWY_NEON_DEF_FUNCTION_INT_32
#undef HWY_NEON_DEF_FUNCTION_INT_64
#undef HWY_NEON_DEF_FUNCTION_INT_8
#undef HWY_NEON_DEF_FUNCTION_INT_8_16_32
#undef HWY_NEON_DEF_FUNCTION_INTS
#undef HWY_NEON_DEF_FUNCTION_INTS_UINTS
#undef HWY_NEON_DEF_FUNCTION_UI_8_16_32
#undef HWY_NEON_DEF_FUNCTION_UIF_64
#undef HWY_NEON_DEF_FUNCTION_UIF_8_16_32
#undef HWY_NEON_DEF_FUNCTION_UINT_16
#undef HWY_NEON_DEF_FUNCTION_UINT_32
#undef HWY_NEON_DEF_FUNCTION_UINT_64
#undef HWY_NEON_DEF_FUNCTION_UINT_8
#undef HWY_NEON_DEF_FUNCTION_UINT_8_16_32
#undef HWY_NEON_DEF_FUNCTION_UINTS
#undef HWY_NEON_EVAL
#undef HWY_NEON_IF_EMULATED_D
#undef HWY_NEON_IF_NOT_EMULATED_D
}  // namespace detail

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
