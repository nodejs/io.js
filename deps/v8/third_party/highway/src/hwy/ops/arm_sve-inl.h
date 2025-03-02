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

// Arm SVE[2] vectors (length not known at compile time).
// External include guard in highway.h - see comment there.

#include <arm_sve.h>

#include "hwy/ops/shared-inl.h"

// Arm C215 declares that SVE vector lengths will always be a power of two.
// We default to relying on this, which makes some operations more efficient.
// You can still opt into fixups by setting this to 0 (unsupported).
#ifndef HWY_SVE_IS_POW2
#define HWY_SVE_IS_POW2 1
#endif

#if HWY_TARGET == HWY_SVE2 || HWY_TARGET == HWY_SVE2_128
#define HWY_SVE_HAVE_2 1
#else
#define HWY_SVE_HAVE_2 0
#endif

// If 1, both __bf16 and a limited set of *_bf16 SVE intrinsics are available:
// create/get/set/dup, ld/st, sel, rev, trn, uzp, zip.
#if HWY_ARM_HAVE_SCALAR_BF16_TYPE && defined(__ARM_FEATURE_SVE_BF16)
#define HWY_SVE_HAVE_BF16_FEATURE 1
#else
#define HWY_SVE_HAVE_BF16_FEATURE 0
#endif

// HWY_SVE_HAVE_BF16_VEC is defined to 1 if the SVE svbfloat16_t vector type
// is supported, even if HWY_SVE_HAVE_BF16_FEATURE (= intrinsics) is 0.
#if HWY_SVE_HAVE_BF16_FEATURE ||                                       \
    (HWY_COMPILER_CLANG >= 1200 && defined(__ARM_FEATURE_SVE_BF16)) || \
    HWY_COMPILER_GCC_ACTUAL >= 1000
#define HWY_SVE_HAVE_BF16_VEC 1
#else
#define HWY_SVE_HAVE_BF16_VEC 0
#endif

// HWY_SVE_HAVE_F32_TO_BF16C is defined to 1 if the SVE svcvt_bf16_f32_x
// and svcvtnt_bf16_f32_x intrinsics are available, even if the __bf16 type
// is disabled
#if HWY_SVE_HAVE_BF16_VEC && defined(__ARM_FEATURE_SVE_BF16)
#define HWY_SVE_HAVE_F32_TO_BF16C 1
#else
#define HWY_SVE_HAVE_F32_TO_BF16C 0
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

template <class V>
struct DFromV_t {};  // specialized in macros
template <class V>
using DFromV = typename DFromV_t<RemoveConst<V>>::type;

template <class V>
using TFromV = TFromD<DFromV<V>>;

// ================================================== MACROS

// Generate specializations and function definitions using X macros. Although
// harder to read and debug, writing everything manually is too bulky.

namespace detail {  // for code folding

// Args: BASE, CHAR, BITS, HALF, NAME, OP

// Unsigned:
#define HWY_SVE_FOREACH_U08(X_MACRO, NAME, OP) X_MACRO(uint, u, 8, 8, NAME, OP)
#define HWY_SVE_FOREACH_U16(X_MACRO, NAME, OP) X_MACRO(uint, u, 16, 8, NAME, OP)
#define HWY_SVE_FOREACH_U32(X_MACRO, NAME, OP) \
  X_MACRO(uint, u, 32, 16, NAME, OP)
#define HWY_SVE_FOREACH_U64(X_MACRO, NAME, OP) \
  X_MACRO(uint, u, 64, 32, NAME, OP)

// Signed:
#define HWY_SVE_FOREACH_I08(X_MACRO, NAME, OP) X_MACRO(int, s, 8, 8, NAME, OP)
#define HWY_SVE_FOREACH_I16(X_MACRO, NAME, OP) X_MACRO(int, s, 16, 8, NAME, OP)
#define HWY_SVE_FOREACH_I32(X_MACRO, NAME, OP) X_MACRO(int, s, 32, 16, NAME, OP)
#define HWY_SVE_FOREACH_I64(X_MACRO, NAME, OP) X_MACRO(int, s, 64, 32, NAME, OP)

// Float:
#define HWY_SVE_FOREACH_F16(X_MACRO, NAME, OP) \
  X_MACRO(float, f, 16, 16, NAME, OP)
#define HWY_SVE_FOREACH_F32(X_MACRO, NAME, OP) \
  X_MACRO(float, f, 32, 16, NAME, OP)
#define HWY_SVE_FOREACH_F64(X_MACRO, NAME, OP) \
  X_MACRO(float, f, 64, 32, NAME, OP)

#define HWY_SVE_FOREACH_BF16_UNCONDITIONAL(X_MACRO, NAME, OP) \
  X_MACRO(bfloat, bf, 16, 16, NAME, OP)

#if HWY_SVE_HAVE_BF16_FEATURE
#define HWY_SVE_FOREACH_BF16(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_BF16_UNCONDITIONAL(X_MACRO, NAME, OP)
// We have both f16 and bf16, so nothing is emulated.

// NOTE: hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr is used instead of
// hwy::EnableIf<false>* = nullptr to avoid compiler errors since
// !hwy::IsSame<D, D>() is always false and as !hwy::IsSame<D, D>() will cause
// SFINAE to occur instead of a hard error due to a dependency on the D template
// argument
#define HWY_SVE_IF_EMULATED_D(D) hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr
#define HWY_GENERIC_IF_EMULATED_D(D) \
  hwy::EnableIf<!hwy::IsSame<D, D>()>* = nullptr
#define HWY_SVE_IF_NOT_EMULATED_D(D) hwy::EnableIf<true>* = nullptr
#else
#define HWY_SVE_FOREACH_BF16(X_MACRO, NAME, OP)
#define HWY_SVE_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_GENERIC_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#define HWY_SVE_IF_NOT_EMULATED_D(D) HWY_IF_NOT_BF16_D(D)
#endif  // HWY_SVE_HAVE_BF16_FEATURE

// For all element sizes:
#define HWY_SVE_FOREACH_U(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U08(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_U16(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_U32(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_U64(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_I(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_I08(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_I16(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_I32(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_I64(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_F3264(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_F32(X_MACRO, NAME, OP)         \
  HWY_SVE_FOREACH_F64(X_MACRO, NAME, OP)

// HWY_SVE_FOREACH_F does not include HWY_SVE_FOREACH_BF16 because SVE lacks
// bf16 overloads for some intrinsics (especially less-common arithmetic).
// However, this does include f16 because SVE supports it unconditionally.
#define HWY_SVE_FOREACH_F(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_F16(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_F3264(X_MACRO, NAME, OP)

// Commonly used type categories for a given element size:
#define HWY_SVE_FOREACH_UI08(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U08(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_I08(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_UI16(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U16(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_I16(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_UI32(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U32(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_I32(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_UI64(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U64(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_I64(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_UIF3264(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_UI32(X_MACRO, NAME, OP)          \
  HWY_SVE_FOREACH_UI64(X_MACRO, NAME, OP)          \
  HWY_SVE_FOREACH_F3264(X_MACRO, NAME, OP)

// Commonly used type categories:
#define HWY_SVE_FOREACH_UI(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_I(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH_IF(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_I(X_MACRO, NAME, OP)        \
  HWY_SVE_FOREACH_F(X_MACRO, NAME, OP)

#define HWY_SVE_FOREACH(X_MACRO, NAME, OP) \
  HWY_SVE_FOREACH_U(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_I(X_MACRO, NAME, OP)     \
  HWY_SVE_FOREACH_F(X_MACRO, NAME, OP)

// Assemble types for use in x-macros
#define HWY_SVE_T(BASE, BITS) BASE##BITS##_t
#define HWY_SVE_D(BASE, BITS, N, POW2) Simd<HWY_SVE_T(BASE, BITS), N, POW2>
#define HWY_SVE_V(BASE, BITS) sv##BASE##BITS##_t
#define HWY_SVE_TUPLE(BASE, BITS, MUL) sv##BASE##BITS##x##MUL##_t

}  // namespace detail

#define HWY_SPECIALIZE(BASE, CHAR, BITS, HALF, NAME, OP) \
  template <>                                            \
  struct DFromV_t<HWY_SVE_V(BASE, BITS)> {               \
    using type = ScalableTag<HWY_SVE_T(BASE, BITS)>;     \
  };

HWY_SVE_FOREACH(HWY_SPECIALIZE, _, _)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SPECIALIZE, _, _)
#endif
#undef HWY_SPECIALIZE

// Note: _x (don't-care value for inactive lanes) avoids additional MOVPRFX
// instructions, and we anyway only use it when the predicate is ptrue.

// vector = f(vector), e.g. Not
#define HWY_SVE_RETV_ARGPV(BASE, CHAR, BITS, HALF, NAME, OP)    \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v);   \
  }
#define HWY_SVE_RETV_ARGV(BASE, CHAR, BITS, HALF, NAME, OP)     \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_##CHAR##BITS(v);                            \
  }

// vector = f(vector, scalar), e.g. detail::AddN
#define HWY_SVE_RETV_ARGPVN(BASE, CHAR, BITS, HALF, NAME, OP)    \
  HWY_API HWY_SVE_V(BASE, BITS)                                  \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_T(BASE, BITS) b) {   \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), a, b); \
  }
#define HWY_SVE_RETV_ARGVN(BASE, CHAR, BITS, HALF, NAME, OP)   \
  HWY_API HWY_SVE_V(BASE, BITS)                                \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_T(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS(a, b);                        \
  }

// vector = f(vector, vector), e.g. Add
#define HWY_SVE_RETV_ARGVV(BASE, CHAR, BITS, HALF, NAME, OP)   \
  HWY_API HWY_SVE_V(BASE, BITS)                                \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS(a, b);                        \
  }
// All-true mask
#define HWY_SVE_RETV_ARGPVV(BASE, CHAR, BITS, HALF, NAME, OP)    \
  HWY_API HWY_SVE_V(BASE, BITS)                                  \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) {   \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), a, b); \
  }
// User-specified mask. Mask=false value is undefined and must be set by caller
// because SVE instructions take it from one of the two inputs, whereas
// AVX-512, RVV and Highway allow a third argument.
#define HWY_SVE_RETV_ARGMVV(BASE, CHAR, BITS, HALF, NAME, OP)              \
  HWY_API HWY_SVE_V(BASE, BITS)                                            \
      NAME(svbool_t m, HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS##_x(m, a, b);                             \
  }

#define HWY_SVE_RETV_ARGVVV(BASE, CHAR, BITS, HALF, NAME, OP) \
  HWY_API HWY_SVE_V(BASE, BITS)                               \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b,  \
           HWY_SVE_V(BASE, BITS) c) {                         \
    return sv##OP##_##CHAR##BITS(a, b, c);                    \
  }

// ------------------------------ Lanes

namespace detail {

// Returns actual lanes of a hardware vector without rounding to a power of two.
template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE size_t AllHardwareLanes() {
  return svcntb_pat(SV_ALL);
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE size_t AllHardwareLanes() {
  return svcnth_pat(SV_ALL);
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE size_t AllHardwareLanes() {
  return svcntw_pat(SV_ALL);
}
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE size_t AllHardwareLanes() {
  return svcntd_pat(SV_ALL);
}

// All-true mask from a macro

#if HWY_SVE_IS_POW2
#define HWY_SVE_ALL_PTRUE(BITS) svptrue_b##BITS()
#define HWY_SVE_PTRUE(BITS) svptrue_b##BITS()
#else
#define HWY_SVE_ALL_PTRUE(BITS) svptrue_pat_b##BITS(SV_ALL)
#define HWY_SVE_PTRUE(BITS) svptrue_pat_b##BITS(SV_POW2)
#endif  // HWY_SVE_IS_POW2

}  // namespace detail

#if HWY_HAVE_SCALABLE

// Returns actual number of lanes after capping by N and shifting. May return 0
// (e.g. for "1/8th" of a u32x4 - would be 1 for 1/8th of u32x8).
template <typename T, size_t N, int kPow2>
HWY_API size_t Lanes(Simd<T, N, kPow2> d) {
  const size_t actual = detail::AllHardwareLanes<T>();
  constexpr size_t kMaxLanes = MaxLanes(d);
  constexpr int kClampedPow2 = HWY_MIN(kPow2, 0);
  // Common case of full vectors: avoid any extra instructions.
  if (detail::IsFull(d)) return actual;
  return HWY_MIN(detail::ScaleByPower(actual, kClampedPow2), kMaxLanes);
}

#endif  // HWY_HAVE_SCALABLE

// ================================================== MASK INIT

// One mask bit per byte; only the one belonging to the lowest byte is valid.

// ------------------------------ FirstN
#define HWY_SVE_FIRSTN(BASE, CHAR, BITS, HALF, NAME, OP)                       \
  template <size_t N, int kPow2>                                               \
  HWY_API svbool_t NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d, size_t count) {     \
    const size_t limit = detail::IsFull(d) ? count : HWY_MIN(Lanes(d), count); \
    return sv##OP##_b##BITS##_u32(uint32_t{0}, static_cast<uint32_t>(limit));  \
  }
HWY_SVE_FOREACH(HWY_SVE_FIRSTN, FirstN, whilelt)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_FIRSTN, FirstN, whilelt)
#endif

template <class D, HWY_SVE_IF_EMULATED_D(D)>
svbool_t FirstN(D /* tag */, size_t count) {
  return FirstN(RebindToUnsigned<D>(), count);
}

#undef HWY_SVE_FIRSTN

template <class D>
using MFromD = svbool_t;

namespace detail {

#define HWY_SVE_WRAP_PTRUE(BASE, CHAR, BITS, HALF, NAME, OP)            \
  template <size_t N, int kPow2>                                        \
  HWY_API svbool_t NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */) {      \
    return HWY_SVE_PTRUE(BITS);                                         \
  }                                                                     \
  template <size_t N, int kPow2>                                        \
  HWY_API svbool_t All##NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */) { \
    return HWY_SVE_ALL_PTRUE(BITS);                                     \
  }

HWY_SVE_FOREACH(HWY_SVE_WRAP_PTRUE, PTrue, ptrue)  // return all-true
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_WRAP_PTRUE, PTrue, ptrue)
#undef HWY_SVE_WRAP_PTRUE

HWY_API svbool_t PFalse() { return svpfalse_b(); }

// Returns all-true if d is HWY_FULL or FirstN(N) after capping N.
//
// This is used in functions that load/store memory; other functions (e.g.
// arithmetic) can ignore d and use PTrue instead.
template <class D>
svbool_t MakeMask(D d) {
  return IsFull(d) ? PTrue(d) : FirstN(d, Lanes(d));
}

}  // namespace detail

#ifdef HWY_NATIVE_MASK_FALSE
#undef HWY_NATIVE_MASK_FALSE
#else
#define HWY_NATIVE_MASK_FALSE
#endif

template <class D>
HWY_API svbool_t MaskFalse(const D /*d*/) {
  return detail::PFalse();
}

// ================================================== INIT

// ------------------------------ Set
// vector = f(d, scalar), e.g. Set
#define HWY_SVE_SET(BASE, CHAR, BITS, HALF, NAME, OP)                         \
  template <size_t N, int kPow2>                                              \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, \
                                     HWY_SVE_T(BASE, BITS) arg) {             \
    return sv##OP##_##CHAR##BITS(arg);                                        \
  }

HWY_SVE_FOREACH(HWY_SVE_SET, Set, dup_n)
#if HWY_SVE_HAVE_BF16_FEATURE  // for if-elif chain
HWY_SVE_FOREACH_BF16(HWY_SVE_SET, Set, dup_n)
#elif HWY_SVE_HAVE_BF16_VEC
// Required for Zero and VFromD
template <class D, HWY_IF_BF16_D(D)>
HWY_API svbfloat16_t Set(D d, bfloat16_t arg) {
  return svreinterpret_bf16_u16(
      Set(RebindToUnsigned<decltype(d)>(), BitCastScalar<uint16_t>(arg)));
}
#else   // neither bf16 feature nor vector: emulate with u16
// Required for Zero and VFromD
template <class D, HWY_IF_BF16_D(D)>
HWY_API svuint16_t Set(D d, bfloat16_t arg) {
  const RebindToUnsigned<decltype(d)> du;
  return Set(du, BitCastScalar<uint16_t>(arg));
}
#endif  // HWY_SVE_HAVE_BF16_FEATURE
#undef HWY_SVE_SET

template <class D>
using VFromD = decltype(Set(D(), TFromD<D>()));

using VBF16 = VFromD<ScalableTag<bfloat16_t>>;

// ------------------------------ Zero

template <class D>
VFromD<D> Zero(D d) {
  // Cast to support bfloat16_t.
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Set(du, 0));
}

// ------------------------------ BitCast

namespace detail {

// u8: no change
#define HWY_SVE_CAST_NOP(BASE, CHAR, BITS, HALF, NAME, OP)                \
  HWY_API HWY_SVE_V(BASE, BITS) BitCastToByte(HWY_SVE_V(BASE, BITS) v) {  \
    return v;                                                             \
  }                                                                       \
  template <size_t N, int kPow2>                                          \
  HWY_API HWY_SVE_V(BASE, BITS) BitCastFromByte(                          \
      HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, HWY_SVE_V(BASE, BITS) v) { \
    return v;                                                             \
  }

// All other types
#define HWY_SVE_CAST(BASE, CHAR, BITS, HALF, NAME, OP)                        \
  HWY_INLINE svuint8_t BitCastToByte(HWY_SVE_V(BASE, BITS) v) {               \
    return sv##OP##_u8_##CHAR##BITS(v);                                       \
  }                                                                           \
  template <size_t N, int kPow2>                                              \
  HWY_INLINE HWY_SVE_V(BASE, BITS)                                            \
      BitCastFromByte(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, svuint8_t v) { \
    return sv##OP##_##CHAR##BITS##_u8(v);                                     \
  }

// U08 is special-cased, hence do not use FOREACH.
HWY_SVE_FOREACH_U08(HWY_SVE_CAST_NOP, _, _)
HWY_SVE_FOREACH_I08(HWY_SVE_CAST, _, reinterpret)
HWY_SVE_FOREACH_UI16(HWY_SVE_CAST, _, reinterpret)
HWY_SVE_FOREACH_UI32(HWY_SVE_CAST, _, reinterpret)
HWY_SVE_FOREACH_UI64(HWY_SVE_CAST, _, reinterpret)
HWY_SVE_FOREACH_F(HWY_SVE_CAST, _, reinterpret)

#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CAST, _, reinterpret)
#else   // !(HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC)
template <class V, HWY_SVE_IF_EMULATED_D(DFromV<V>)>
HWY_INLINE svuint8_t BitCastToByte(V v) {
  const RebindToUnsigned<DFromV<V>> du;
  return BitCastToByte(BitCast(du, v));
}

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_INLINE VFromD<D> BitCastFromByte(D d, svuint8_t v) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCastFromByte(du, v);
}
#endif  // HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC

#undef HWY_SVE_CAST_NOP
#undef HWY_SVE_CAST

}  // namespace detail

template <class D, class FromV>
HWY_API VFromD<D> BitCast(D d, FromV v) {
  return detail::BitCastFromByte(d, detail::BitCastToByte(v));
}

// ------------------------------ Undefined

#define HWY_SVE_UNDEFINED(BASE, CHAR, BITS, HALF, NAME, OP) \
  template <size_t N, int kPow2>                            \
  HWY_API HWY_SVE_V(BASE, BITS)                             \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */) {       \
    return sv##OP##_##CHAR##BITS();                         \
  }

HWY_SVE_FOREACH(HWY_SVE_UNDEFINED, Undefined, undef)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_UNDEFINED, Undefined, undef)
#endif

template <class D, HWY_SVE_IF_EMULATED_D(D)>
VFromD<D> Undefined(D d) {
  const RebindToUnsigned<D> du;
  return BitCast(d, Undefined(du));
}

// ------------------------------ Tuple

// tuples = f(d, v..), e.g. Create2
#define HWY_SVE_CREATE(BASE, CHAR, BITS, HALF, NAME, OP)                 \
  template <size_t N, int kPow2>                                         \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 2)                                   \
      NAME##2(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */,                   \
              HWY_SVE_V(BASE, BITS) v0, HWY_SVE_V(BASE, BITS) v1) {      \
    return sv##OP##2_##CHAR##BITS(v0, v1);                               \
  }                                                                      \
  template <size_t N, int kPow2>                                         \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 3) NAME##3(                          \
      HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, HWY_SVE_V(BASE, BITS) v0, \
      HWY_SVE_V(BASE, BITS) v1, HWY_SVE_V(BASE, BITS) v2) {              \
    return sv##OP##3_##CHAR##BITS(v0, v1, v2);                           \
  }                                                                      \
  template <size_t N, int kPow2>                                         \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 4)                                   \
      NAME##4(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */,                   \
              HWY_SVE_V(BASE, BITS) v0, HWY_SVE_V(BASE, BITS) v1,        \
              HWY_SVE_V(BASE, BITS) v2, HWY_SVE_V(BASE, BITS) v3) {      \
    return sv##OP##4_##CHAR##BITS(v0, v1, v2, v3);                       \
  }

HWY_SVE_FOREACH(HWY_SVE_CREATE, Create, create)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CREATE, Create, create)
#endif
#undef HWY_SVE_CREATE

template <class D>
using Vec2 = decltype(Create2(D(), Zero(D()), Zero(D())));
template <class D>
using Vec3 = decltype(Create3(D(), Zero(D()), Zero(D()), Zero(D())));
template <class D>
using Vec4 = decltype(Create4(D(), Zero(D()), Zero(D()), Zero(D()), Zero(D())));

#define HWY_SVE_GET(BASE, CHAR, BITS, HALF, NAME, OP)                         \
  template <size_t kIndex>                                                    \
  HWY_API HWY_SVE_V(BASE, BITS) NAME##2(HWY_SVE_TUPLE(BASE, BITS, 2) tuple) { \
    return sv##OP##2_##CHAR##BITS(tuple, kIndex);                             \
  }                                                                           \
  template <size_t kIndex>                                                    \
  HWY_API HWY_SVE_V(BASE, BITS) NAME##3(HWY_SVE_TUPLE(BASE, BITS, 3) tuple) { \
    return sv##OP##3_##CHAR##BITS(tuple, kIndex);                             \
  }                                                                           \
  template <size_t kIndex>                                                    \
  HWY_API HWY_SVE_V(BASE, BITS) NAME##4(HWY_SVE_TUPLE(BASE, BITS, 4) tuple) { \
    return sv##OP##4_##CHAR##BITS(tuple, kIndex);                             \
  }

HWY_SVE_FOREACH(HWY_SVE_GET, Get, get)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_GET, Get, get)
#endif
#undef HWY_SVE_GET

#define HWY_SVE_SET(BASE, CHAR, BITS, HALF, NAME, OP)                          \
  template <size_t kIndex>                                                     \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 2)                                         \
      NAME##2(HWY_SVE_TUPLE(BASE, BITS, 2) tuple, HWY_SVE_V(BASE, BITS) vec) { \
    return sv##OP##2_##CHAR##BITS(tuple, kIndex, vec);                         \
  }                                                                            \
  template <size_t kIndex>                                                     \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 3)                                         \
      NAME##3(HWY_SVE_TUPLE(BASE, BITS, 3) tuple, HWY_SVE_V(BASE, BITS) vec) { \
    return sv##OP##3_##CHAR##BITS(tuple, kIndex, vec);                         \
  }                                                                            \
  template <size_t kIndex>                                                     \
  HWY_API HWY_SVE_TUPLE(BASE, BITS, 4)                                         \
      NAME##4(HWY_SVE_TUPLE(BASE, BITS, 4) tuple, HWY_SVE_V(BASE, BITS) vec) { \
    return sv##OP##4_##CHAR##BITS(tuple, kIndex, vec);                         \
  }

HWY_SVE_FOREACH(HWY_SVE_SET, Set, set)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_SET, Set, set)
#endif
#undef HWY_SVE_SET

// ------------------------------ ResizeBitCast

// Same as BitCast on SVE
template <class D, class FromV>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  return BitCast(d, v);
}

// ------------------------------ Dup128VecFromValues

template <class D, HWY_IF_I8_D(D)>
HWY_API svint8_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                     TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                     TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                     TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                     TFromD<D> t11, TFromD<D> t12,
                                     TFromD<D> t13, TFromD<D> t14,
                                     TFromD<D> t15) {
  return svdupq_n_s8(t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13,
                     t14, t15);
}

template <class D, HWY_IF_U8_D(D)>
HWY_API svuint8_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  return svdupq_n_u8(t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13,
                     t14, t15);
}

template <class D, HWY_IF_I16_D(D)>
HWY_API svint16_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  return svdupq_n_s16(t0, t1, t2, t3, t4, t5, t6, t7);
}

template <class D, HWY_IF_U16_D(D)>
HWY_API svuint16_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                       TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                       TFromD<D> t5, TFromD<D> t6,
                                       TFromD<D> t7) {
  return svdupq_n_u16(t0, t1, t2, t3, t4, t5, t6, t7);
}

template <class D, HWY_IF_F16_D(D)>
HWY_API svfloat16_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                        TFromD<D> t2, TFromD<D> t3,
                                        TFromD<D> t4, TFromD<D> t5,
                                        TFromD<D> t6, TFromD<D> t7) {
  return svdupq_n_f16(t0, t1, t2, t3, t4, t5, t6, t7);
}

template <class D, HWY_IF_BF16_D(D)>
HWY_API VBF16 Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1, TFromD<D> t2,
                                  TFromD<D> t3, TFromD<D> t4, TFromD<D> t5,
                                  TFromD<D> t6, TFromD<D> t7) {
#if HWY_SVE_HAVE_BF16_FEATURE
  (void)d;
  return svdupq_n_bf16(t0, t1, t2, t3, t4, t5, t6, t7);
#else
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, Dup128VecFromValues(
             du, BitCastScalar<uint16_t>(t0), BitCastScalar<uint16_t>(t1),
             BitCastScalar<uint16_t>(t2), BitCastScalar<uint16_t>(t3),
             BitCastScalar<uint16_t>(t4), BitCastScalar<uint16_t>(t5),
             BitCastScalar<uint16_t>(t6), BitCastScalar<uint16_t>(t7)));
#endif
}

template <class D, HWY_IF_I32_D(D)>
HWY_API svint32_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return svdupq_n_s32(t0, t1, t2, t3);
}

template <class D, HWY_IF_U32_D(D)>
HWY_API svuint32_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                       TFromD<D> t2, TFromD<D> t3) {
  return svdupq_n_u32(t0, t1, t2, t3);
}

template <class D, HWY_IF_F32_D(D)>
HWY_API svfloat32_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                        TFromD<D> t2, TFromD<D> t3) {
  return svdupq_n_f32(t0, t1, t2, t3);
}

template <class D, HWY_IF_I64_D(D)>
HWY_API svint64_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return svdupq_n_s64(t0, t1);
}

template <class D, HWY_IF_U64_D(D)>
HWY_API svuint64_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return svdupq_n_u64(t0, t1);
}

template <class D, HWY_IF_F64_D(D)>
HWY_API svfloat64_t Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return svdupq_n_f64(t0, t1);
}

// ================================================== LOGICAL

// detail::*N() functions accept a scalar argument to avoid extra Set().

// ------------------------------ Not
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPV, Not, not )  // NOLINT

// ------------------------------ And

namespace detail {
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN, AndN, and_n)
}  // namespace detail

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, And, and)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V And(const V a, const V b) {
  const DFromV<V> df;
  const RebindToUnsigned<decltype(df)> du;
  return BitCast(df, And(BitCast(du, a), BitCast(du, b)));
}

// ------------------------------ Or

namespace detail {
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN, OrN, orr_n)
}  // namespace detail

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, Or, orr)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Or(const V a, const V b) {
  const DFromV<V> df;
  const RebindToUnsigned<decltype(df)> du;
  return BitCast(df, Or(BitCast(du, a), BitCast(du, b)));
}

// ------------------------------ Xor

namespace detail {
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN, XorN, eor_n)
}  // namespace detail

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, Xor, eor)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Xor(const V a, const V b) {
  const DFromV<V> df;
  const RebindToUnsigned<decltype(df)> du;
  return BitCast(df, Xor(BitCast(du, a), BitCast(du, b)));
}

// ------------------------------ AndNot

namespace detail {
#define HWY_SVE_RETV_ARGPVN_SWAP(BASE, CHAR, BITS, HALF, NAME, OP) \
  HWY_API HWY_SVE_V(BASE, BITS)                                    \
      NAME(HWY_SVE_T(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) {     \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), b, a);   \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN_SWAP, AndNotN, bic_n)
#undef HWY_SVE_RETV_ARGPVN_SWAP
}  // namespace detail

#define HWY_SVE_RETV_ARGPVV_SWAP(BASE, CHAR, BITS, HALF, NAME, OP) \
  HWY_API HWY_SVE_V(BASE, BITS)                                    \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) {     \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), b, a);   \
  }
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV_SWAP, AndNot, bic)
#undef HWY_SVE_RETV_ARGPVV_SWAP

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V AndNot(const V a, const V b) {
  const DFromV<V> df;
  const RebindToUnsigned<decltype(df)> du;
  return BitCast(df, AndNot(BitCast(du, a), BitCast(du, b)));
}

// ------------------------------ Xor3

#if HWY_SVE_HAVE_2

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGVVV, Xor3, eor3)

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V Xor3(const V x1, const V x2, const V x3) {
  const DFromV<V> df;
  const RebindToUnsigned<decltype(df)> du;
  return BitCast(df, Xor3(BitCast(du, x1), BitCast(du, x2), BitCast(du, x3)));
}

#else
template <class V>
HWY_API V Xor3(V x1, V x2, V x3) {
  return Xor(x1, Xor(x2, x3));
}
#endif

// ------------------------------ Or3
template <class V>
HWY_API V Or3(V o1, V o2, V o3) {
  return Or(o1, Or(o2, o3));
}

// ------------------------------ OrAnd
template <class V>
HWY_API V OrAnd(const V o, const V a1, const V a2) {
  return Or(o, And(a1, a2));
}

// ------------------------------ PopulationCount

#ifdef HWY_NATIVE_POPCNT
#undef HWY_NATIVE_POPCNT
#else
#define HWY_NATIVE_POPCNT
#endif

// Need to return original type instead of unsigned.
#define HWY_SVE_POPCNT(BASE, CHAR, BITS, HALF, NAME, OP)               \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) {        \
    return BitCast(DFromV<decltype(v)>(),                              \
                   sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v)); \
  }
HWY_SVE_FOREACH_UI(HWY_SVE_POPCNT, PopulationCount, cnt)
#undef HWY_SVE_POPCNT

// ================================================== SIGN

// ------------------------------ Neg
HWY_SVE_FOREACH_IF(HWY_SVE_RETV_ARGPV, Neg, neg)

HWY_API VBF16 Neg(VBF16 v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  return BitCast(d, Xor(BitCast(du, v), Set(du, SignMask<TU>())));
}

// ------------------------------ SaturatedNeg
#if HWY_SVE_HAVE_2
#ifdef HWY_NATIVE_SATURATED_NEG_8_16_32
#undef HWY_NATIVE_SATURATED_NEG_8_16_32
#else
#define HWY_NATIVE_SATURATED_NEG_8_16_32
#endif

#ifdef HWY_NATIVE_SATURATED_NEG_64
#undef HWY_NATIVE_SATURATED_NEG_64
#else
#define HWY_NATIVE_SATURATED_NEG_64
#endif

HWY_SVE_FOREACH_I(HWY_SVE_RETV_ARGPV, SaturatedNeg, qneg)
#endif  // HWY_SVE_HAVE_2

// ------------------------------ Abs
HWY_SVE_FOREACH_IF(HWY_SVE_RETV_ARGPV, Abs, abs)

// ------------------------------ SaturatedAbs
#if HWY_SVE_HAVE_2
#ifdef HWY_NATIVE_SATURATED_ABS
#undef HWY_NATIVE_SATURATED_ABS
#else
#define HWY_NATIVE_SATURATED_ABS
#endif

HWY_SVE_FOREACH_I(HWY_SVE_RETV_ARGPV, SaturatedAbs, qabs)
#endif  // HWY_SVE_HAVE_2

// ================================================== ARITHMETIC

// Per-target flags to prevent generic_ops-inl.h defining Add etc.
#ifdef HWY_NATIVE_OPERATOR_REPLACEMENTS
#undef HWY_NATIVE_OPERATOR_REPLACEMENTS
#else
#define HWY_NATIVE_OPERATOR_REPLACEMENTS
#endif

// ------------------------------ Add

namespace detail {
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVN, AddN, add_n)
}  // namespace detail

HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVV, Add, add)

// ------------------------------ Sub

namespace detail {
// Can't use HWY_SVE_RETV_ARGPVN because caller wants to specify pg.
#define HWY_SVE_RETV_ARGPVN_MASK(BASE, CHAR, BITS, HALF, NAME, OP)          \
  HWY_API HWY_SVE_V(BASE, BITS)                                             \
      NAME(svbool_t pg, HWY_SVE_V(BASE, BITS) a, HWY_SVE_T(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS##_z(pg, a, b);                             \
  }

HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVN_MASK, SubN, sub_n)
#undef HWY_SVE_RETV_ARGPVN_MASK
}  // namespace detail

HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVV, Sub, sub)

// ------------------------------ SumsOf8
HWY_API svuint64_t SumsOf8(const svuint8_t v) {
  const ScalableTag<uint32_t> du32;
  const ScalableTag<uint64_t> du64;
  const svbool_t pg = detail::PTrue(du64);

  const svuint32_t sums_of_4 = svdot_n_u32(Zero(du32), v, 1);
  // Compute pairwise sum of u32 and extend to u64.

#if HWY_SVE_HAVE_2
  return svadalp_u64_x(pg, Zero(du64), sums_of_4);
#else
  const svuint64_t hi = svlsr_n_u64_x(pg, BitCast(du64, sums_of_4), 32);
  // Isolate the lower 32 bits (to be added to the upper 32 and zero-extended)
  const svuint64_t lo = svextw_u64_x(pg, BitCast(du64, sums_of_4));
  return Add(hi, lo);
#endif
}

HWY_API svint64_t SumsOf8(const svint8_t v) {
  const ScalableTag<int32_t> di32;
  const ScalableTag<int64_t> di64;
  const svbool_t pg = detail::PTrue(di64);

  const svint32_t sums_of_4 = svdot_n_s32(Zero(di32), v, 1);
#if HWY_SVE_HAVE_2
  return svadalp_s64_x(pg, Zero(di64), sums_of_4);
#else
  const svint64_t hi = svasr_n_s64_x(pg, BitCast(di64, sums_of_4), 32);
  // Isolate the lower 32 bits (to be added to the upper 32 and sign-extended)
  const svint64_t lo = svextw_s64_x(pg, BitCast(di64, sums_of_4));
  return Add(hi, lo);
#endif
}

// ------------------------------ SumsOf2
#if HWY_SVE_HAVE_2
namespace detail {

HWY_INLINE svint16_t SumsOf2(hwy::SignedTag /*type_tag*/,
                             hwy::SizeTag<1> /*lane_size_tag*/, svint8_t v) {
  const ScalableTag<int16_t> di16;
  const svbool_t pg = detail::PTrue(di16);
  return svadalp_s16_x(pg, Zero(di16), v);
}

HWY_INLINE svuint16_t SumsOf2(hwy::UnsignedTag /*type_tag*/,
                              hwy::SizeTag<1> /*lane_size_tag*/, svuint8_t v) {
  const ScalableTag<uint16_t> du16;
  const svbool_t pg = detail::PTrue(du16);
  return svadalp_u16_x(pg, Zero(du16), v);
}

HWY_INLINE svint32_t SumsOf2(hwy::SignedTag /*type_tag*/,
                             hwy::SizeTag<2> /*lane_size_tag*/, svint16_t v) {
  const ScalableTag<int32_t> di32;
  const svbool_t pg = detail::PTrue(di32);
  return svadalp_s32_x(pg, Zero(di32), v);
}

HWY_INLINE svuint32_t SumsOf2(hwy::UnsignedTag /*type_tag*/,
                              hwy::SizeTag<2> /*lane_size_tag*/, svuint16_t v) {
  const ScalableTag<uint32_t> du32;
  const svbool_t pg = detail::PTrue(du32);
  return svadalp_u32_x(pg, Zero(du32), v);
}

HWY_INLINE svint64_t SumsOf2(hwy::SignedTag /*type_tag*/,
                             hwy::SizeTag<4> /*lane_size_tag*/, svint32_t v) {
  const ScalableTag<int64_t> di64;
  const svbool_t pg = detail::PTrue(di64);
  return svadalp_s64_x(pg, Zero(di64), v);
}

HWY_INLINE svuint64_t SumsOf2(hwy::UnsignedTag /*type_tag*/,
                              hwy::SizeTag<4> /*lane_size_tag*/, svuint32_t v) {
  const ScalableTag<uint64_t> du64;
  const svbool_t pg = detail::PTrue(du64);
  return svadalp_u64_x(pg, Zero(du64), v);
}

}  // namespace detail
#endif  // HWY_SVE_HAVE_2

// ------------------------------ SumsOf4
namespace detail {

HWY_INLINE svint32_t SumsOf4(hwy::SignedTag /*type_tag*/,
                             hwy::SizeTag<1> /*lane_size_tag*/, svint8_t v) {
  return svdot_n_s32(Zero(ScalableTag<int32_t>()), v, 1);
}

HWY_INLINE svuint32_t SumsOf4(hwy::UnsignedTag /*type_tag*/,
                              hwy::SizeTag<1> /*lane_size_tag*/, svuint8_t v) {
  return svdot_n_u32(Zero(ScalableTag<uint32_t>()), v, 1);
}

HWY_INLINE svint64_t SumsOf4(hwy::SignedTag /*type_tag*/,
                             hwy::SizeTag<2> /*lane_size_tag*/, svint16_t v) {
  return svdot_n_s64(Zero(ScalableTag<int64_t>()), v, 1);
}

HWY_INLINE svuint64_t SumsOf4(hwy::UnsignedTag /*type_tag*/,
                              hwy::SizeTag<2> /*lane_size_tag*/, svuint16_t v) {
  return svdot_n_u64(Zero(ScalableTag<uint64_t>()), v, 1);
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

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGVV, SaturatedAdd, qadd)

// ------------------------------ SaturatedSub

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGVV, SaturatedSub, qsub)

// ------------------------------ AbsDiff
#ifdef HWY_NATIVE_INTEGER_ABS_DIFF
#undef HWY_NATIVE_INTEGER_ABS_DIFF
#else
#define HWY_NATIVE_INTEGER_ABS_DIFF
#endif

HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVV, AbsDiff, abd)

// ------------------------------ ShiftLeft[Same]

#define HWY_SVE_SHIFT_N(BASE, CHAR, BITS, HALF, NAME, OP)                  \
  template <int kBits>                                                     \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) {            \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v, kBits);       \
  }                                                                        \
  HWY_API HWY_SVE_V(BASE, BITS)                                            \
      NAME##Same(HWY_SVE_V(BASE, BITS) v, int bits) {                      \
    return sv##OP##_##CHAR##BITS##_x(                                      \
        HWY_SVE_PTRUE(BITS), v, static_cast<HWY_SVE_T(uint, BITS)>(bits)); \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_SHIFT_N, ShiftLeft, lsl_n)

// ------------------------------ ShiftRight[Same]

HWY_SVE_FOREACH_U(HWY_SVE_SHIFT_N, ShiftRight, lsr_n)
HWY_SVE_FOREACH_I(HWY_SVE_SHIFT_N, ShiftRight, asr_n)

#undef HWY_SVE_SHIFT_N

// ------------------------------ RotateRight

#if HWY_SVE_HAVE_2

#define HWY_SVE_ROTATE_RIGHT_N(BASE, CHAR, BITS, HALF, NAME, OP) \
  template <int kBits>                                           \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) {  \
    if (kBits == 0) return v;                                    \
    return sv##OP##_##CHAR##BITS(v, Zero(DFromV<decltype(v)>()), \
                                 HWY_MAX(kBits, 1));             \
  }

HWY_SVE_FOREACH_U(HWY_SVE_ROTATE_RIGHT_N, RotateRight, xar_n)
HWY_SVE_FOREACH_I(HWY_SVE_ROTATE_RIGHT_N, RotateRight, xar_n)

#undef HWY_SVE_ROTATE_RIGHT_N

#else  // !HWY_SVE_HAVE_2
template <int kBits, class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V RotateRight(const V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  constexpr size_t kSizeInBits = sizeof(TFromV<V>) * 8;
  static_assert(0 <= kBits && kBits < kSizeInBits, "Invalid shift count");
  if (kBits == 0) return v;

  return Or(BitCast(d, ShiftRight<kBits>(BitCast(du, v))),
            ShiftLeft<HWY_MIN(kSizeInBits - 1, kSizeInBits - kBits)>(v));
}
#endif

// ------------------------------ Shl/r

#define HWY_SVE_SHIFT(BASE, CHAR, BITS, HALF, NAME, OP)           \
  HWY_API HWY_SVE_V(BASE, BITS)                                   \
      NAME(HWY_SVE_V(BASE, BITS) v, HWY_SVE_V(BASE, BITS) bits) { \
    const RebindToUnsigned<DFromV<decltype(v)>> du;               \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v,      \
                                     BitCast(du, bits));          \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_SHIFT, Shl, lsl)

HWY_SVE_FOREACH_U(HWY_SVE_SHIFT, Shr, lsr)
HWY_SVE_FOREACH_I(HWY_SVE_SHIFT, Shr, asr)

#undef HWY_SVE_SHIFT

// ------------------------------ RoundingShiftLeft[Same]/RoundingShr

#if HWY_SVE_HAVE_2

#ifdef HWY_NATIVE_ROUNDING_SHR
#undef HWY_NATIVE_ROUNDING_SHR
#else
#define HWY_NATIVE_ROUNDING_SHR
#endif

#define HWY_SVE_ROUNDING_SHR_N(BASE, CHAR, BITS, HALF, NAME, OP)           \
  template <int kBits>                                                     \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) {            \
    HWY_IF_CONSTEXPR(kBits == 0) { return v; }                             \
                                                                           \
    return sv##OP##_##CHAR##BITS##_x(                                      \
        HWY_SVE_PTRUE(BITS), v, static_cast<uint64_t>(HWY_MAX(kBits, 1))); \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_ROUNDING_SHR_N, RoundingShiftRight, rshr_n)

#undef HWY_SVE_ROUNDING_SHR_N

#define HWY_SVE_ROUNDING_SHR(BASE, CHAR, BITS, HALF, NAME, OP)    \
  HWY_API HWY_SVE_V(BASE, BITS)                                   \
      NAME(HWY_SVE_V(BASE, BITS) v, HWY_SVE_V(BASE, BITS) bits) { \
    const RebindToSigned<DFromV<decltype(v)>> di;                 \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v,      \
                                     Neg(BitCast(di, bits)));     \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_ROUNDING_SHR, RoundingShr, rshl)

#undef HWY_SVE_ROUNDING_SHR

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V RoundingShiftRightSame(V v, int bits) {
  const DFromV<V> d;
  using T = TFromD<decltype(d)>;
  return RoundingShr(v, Set(d, static_cast<T>(bits)));
}

#endif  // HWY_SVE_HAVE_2

// ------------------------------ Min/Max

HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, Min, min)
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, Max, max)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPVV, Min, minnm)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPVV, Max, maxnm)

namespace detail {
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN, MinN, min_n)
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVN, MaxN, max_n)
}  // namespace detail

// ------------------------------ Mul

// Per-target flags to prevent generic_ops-inl.h defining 8/64-bit operator*.
#ifdef HWY_NATIVE_MUL_8
#undef HWY_NATIVE_MUL_8
#else
#define HWY_NATIVE_MUL_8
#endif
#ifdef HWY_NATIVE_MUL_64
#undef HWY_NATIVE_MUL_64
#else
#define HWY_NATIVE_MUL_64
#endif

HWY_SVE_FOREACH(HWY_SVE_RETV_ARGPVV, Mul, mul)

// ------------------------------ MulHigh
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, MulHigh, mulh)

// ------------------------------ MulFixedPoint15
HWY_API svint16_t MulFixedPoint15(svint16_t a, svint16_t b) {
#if HWY_SVE_HAVE_2
  return svqrdmulh_s16(a, b);
#else
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;

  const svuint16_t lo = BitCast(du, Mul(a, b));
  const svint16_t hi = MulHigh(a, b);
  // We want (lo + 0x4000) >> 15, but that can overflow, and if it does we must
  // carry that into the result. Instead isolate the top two bits because only
  // they can influence the result.
  const svuint16_t lo_top2 = ShiftRight<14>(lo);
  // Bits 11: add 2, 10: add 1, 01: add 1, 00: add 0.
  const svuint16_t rounding = ShiftRight<1>(detail::AddN(lo_top2, 1));
  return Add(Add(hi, hi), BitCast(d, rounding));
#endif
}

// ------------------------------ Div
#ifdef HWY_NATIVE_INT_DIV
#undef HWY_NATIVE_INT_DIV
#else
#define HWY_NATIVE_INT_DIV
#endif

HWY_SVE_FOREACH_UI32(HWY_SVE_RETV_ARGPVV, Div, div)
HWY_SVE_FOREACH_UI64(HWY_SVE_RETV_ARGPVV, Div, div)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPVV, Div, div)

// ------------------------------ ApproximateReciprocal
#ifdef HWY_NATIVE_F64_APPROX_RECIP
#undef HWY_NATIVE_F64_APPROX_RECIP
#else
#define HWY_NATIVE_F64_APPROX_RECIP
#endif

HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGV, ApproximateReciprocal, recpe)

// ------------------------------ Sqrt
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPV, Sqrt, sqrt)

// ------------------------------ ApproximateReciprocalSqrt
#ifdef HWY_NATIVE_F64_APPROX_RSQRT
#undef HWY_NATIVE_F64_APPROX_RSQRT
#else
#define HWY_NATIVE_F64_APPROX_RSQRT
#endif

HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGV, ApproximateReciprocalSqrt, rsqrte)

// ------------------------------ MulAdd

// Per-target flag to prevent generic_ops-inl.h from defining int MulAdd.
#ifdef HWY_NATIVE_INT_FMA
#undef HWY_NATIVE_INT_FMA
#else
#define HWY_NATIVE_INT_FMA
#endif

#define HWY_SVE_FMA(BASE, CHAR, BITS, HALF, NAME, OP)                   \
  HWY_API HWY_SVE_V(BASE, BITS)                                         \
      NAME(HWY_SVE_V(BASE, BITS) mul, HWY_SVE_V(BASE, BITS) x,          \
           HWY_SVE_V(BASE, BITS) add) {                                 \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), x, mul, add); \
  }

HWY_SVE_FOREACH(HWY_SVE_FMA, MulAdd, mad)

// ------------------------------ NegMulAdd
HWY_SVE_FOREACH(HWY_SVE_FMA, NegMulAdd, msb)

// ------------------------------ MulSub
HWY_SVE_FOREACH_F(HWY_SVE_FMA, MulSub, nmsb)

// ------------------------------ NegMulSub
HWY_SVE_FOREACH_F(HWY_SVE_FMA, NegMulSub, nmad)

#undef HWY_SVE_FMA

// ------------------------------ Round etc.

HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPV, Round, rintn)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPV, Floor, rintm)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPV, Ceil, rintp)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGPV, Trunc, rintz)

// ================================================== MASK

// ------------------------------ RebindMask
template <class D, typename MFrom>
HWY_API svbool_t RebindMask(const D /*d*/, const MFrom mask) {
  return mask;
}

// ------------------------------ Mask logical

HWY_API svbool_t Not(svbool_t m) {
  // We don't know the lane type, so assume 8-bit. For larger types, this will
  // de-canonicalize the predicate, i.e. set bits to 1 even though they do not
  // correspond to the lowest byte in the lane. Arm says such bits are ignored.
  return svnot_b_z(HWY_SVE_PTRUE(8), m);
}
HWY_API svbool_t And(svbool_t a, svbool_t b) {
  return svand_b_z(b, b, a);  // same order as AndNot for consistency
}
HWY_API svbool_t AndNot(svbool_t a, svbool_t b) {
  return svbic_b_z(b, b, a);  // reversed order like NEON
}
HWY_API svbool_t Or(svbool_t a, svbool_t b) {
  return svsel_b(a, a, b);  // a ? true : b
}
HWY_API svbool_t Xor(svbool_t a, svbool_t b) {
  return svsel_b(a, svnand_b_z(a, a, b), b);  // a ? !(a & b) : b.
}

HWY_API svbool_t ExclusiveNeither(svbool_t a, svbool_t b) {
  return svnor_b_z(HWY_SVE_PTRUE(8), a, b);  // !a && !b, undefined if a && b.
}

// ------------------------------ CountTrue

#define HWY_SVE_COUNT_TRUE(BASE, CHAR, BITS, HALF, NAME, OP)           \
  template <size_t N, int kPow2>                                       \
  HWY_API size_t NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d, svbool_t m) { \
    return sv##OP##_b##BITS(detail::MakeMask(d), m);                   \
  }

HWY_SVE_FOREACH(HWY_SVE_COUNT_TRUE, CountTrue, cntp)
#undef HWY_SVE_COUNT_TRUE

// For 16-bit Compress: full vector, not limited to SV_POW2.
namespace detail {

#define HWY_SVE_COUNT_TRUE_FULL(BASE, CHAR, BITS, HALF, NAME, OP)            \
  template <size_t N, int kPow2>                                             \
  HWY_API size_t NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, svbool_t m) { \
    return sv##OP##_b##BITS(svptrue_b##BITS(), m);                           \
  }

HWY_SVE_FOREACH(HWY_SVE_COUNT_TRUE_FULL, CountTrueFull, cntp)
#undef HWY_SVE_COUNT_TRUE_FULL

}  // namespace detail

// ------------------------------ AllFalse
template <class D>
HWY_API bool AllFalse(D d, svbool_t m) {
  return !svptest_any(detail::MakeMask(d), m);
}

// ------------------------------ AllTrue
template <class D>
HWY_API bool AllTrue(D d, svbool_t m) {
  return CountTrue(d, m) == Lanes(d);
}

// ------------------------------ FindFirstTrue
template <class D>
HWY_API intptr_t FindFirstTrue(D d, svbool_t m) {
  return AllFalse(d, m) ? intptr_t{-1}
                        : static_cast<intptr_t>(
                              CountTrue(d, svbrkb_b_z(detail::MakeMask(d), m)));
}

// ------------------------------ FindKnownFirstTrue
template <class D>
HWY_API size_t FindKnownFirstTrue(D d, svbool_t m) {
  return CountTrue(d, svbrkb_b_z(detail::MakeMask(d), m));
}

// ------------------------------ IfThenElse
#define HWY_SVE_IF_THEN_ELSE(BASE, CHAR, BITS, HALF, NAME, OP)                \
  HWY_API HWY_SVE_V(BASE, BITS)                                               \
      NAME(svbool_t m, HWY_SVE_V(BASE, BITS) yes, HWY_SVE_V(BASE, BITS) no) { \
    return sv##OP##_##CHAR##BITS(m, yes, no);                                 \
  }

HWY_SVE_FOREACH(HWY_SVE_IF_THEN_ELSE, IfThenElse, sel)
HWY_SVE_FOREACH_BF16(HWY_SVE_IF_THEN_ELSE, IfThenElse, sel)
#undef HWY_SVE_IF_THEN_ELSE

template <class V, class D = DFromV<V>, HWY_SVE_IF_EMULATED_D(D)>
HWY_API V IfThenElse(const svbool_t mask, V yes, V no) {
  const RebindToUnsigned<D> du;
  return BitCast(
      D(), IfThenElse(RebindMask(du, mask), BitCast(du, yes), BitCast(du, no)));
}

// ------------------------------ IfThenElseZero

template <class V, class D = DFromV<V>, HWY_SVE_IF_NOT_EMULATED_D(D)>
HWY_API V IfThenElseZero(const svbool_t mask, const V yes) {
  return IfThenElse(mask, yes, Zero(D()));
}

template <class V, class D = DFromV<V>, HWY_SVE_IF_EMULATED_D(D)>
HWY_API V IfThenElseZero(const svbool_t mask, V yes) {
  const RebindToUnsigned<D> du;
  return BitCast(D(), IfThenElseZero(RebindMask(du, mask), BitCast(du, yes)));
}

// ------------------------------ IfThenZeroElse

template <class V, class D = DFromV<V>, HWY_SVE_IF_NOT_EMULATED_D(D)>
HWY_API V IfThenZeroElse(const svbool_t mask, const V no) {
  return IfThenElse(mask, Zero(D()), no);
}

template <class V, class D = DFromV<V>, HWY_SVE_IF_EMULATED_D(D)>
HWY_API V IfThenZeroElse(const svbool_t mask, V no) {
  const RebindToUnsigned<D> du;
  return BitCast(D(), IfThenZeroElse(RebindMask(du, mask), BitCast(du, no)));
}

// ------------------------------ Additional mask logical operations
HWY_API svbool_t SetBeforeFirst(svbool_t m) {
  // We don't know the lane type, so assume 8-bit. For larger types, this will
  // de-canonicalize the predicate, i.e. set bits to 1 even though they do not
  // correspond to the lowest byte in the lane. Arm says such bits are ignored.
  return svbrkb_b_z(HWY_SVE_PTRUE(8), m);
}

HWY_API svbool_t SetAtOrBeforeFirst(svbool_t m) {
  // We don't know the lane type, so assume 8-bit. For larger types, this will
  // de-canonicalize the predicate, i.e. set bits to 1 even though they do not
  // correspond to the lowest byte in the lane. Arm says such bits are ignored.
  return svbrka_b_z(HWY_SVE_PTRUE(8), m);
}

HWY_API svbool_t SetOnlyFirst(svbool_t m) { return svbrka_b_z(m, m); }

HWY_API svbool_t SetAtOrAfterFirst(svbool_t m) {
  return Not(SetBeforeFirst(m));
}

// ------------------------------ PromoteMaskTo

#ifdef HWY_NATIVE_PROMOTE_MASK_TO
#undef HWY_NATIVE_PROMOTE_MASK_TO
#else
#define HWY_NATIVE_PROMOTE_MASK_TO
#endif

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_D(DTo, sizeof(TFromD<DFrom>) * 2)>
HWY_API svbool_t PromoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/, svbool_t m) {
  return svunpklo_b(m);
}

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_GT_D(DTo, sizeof(TFromD<DFrom>) * 2)>
HWY_API svbool_t PromoteMaskTo(DTo d_to, DFrom d_from, svbool_t m) {
  using TFrom = TFromD<DFrom>;
  using TWFrom = MakeWide<MakeUnsigned<TFrom>>;
  static_assert(sizeof(TWFrom) > sizeof(TFrom),
                "sizeof(TWFrom) > sizeof(TFrom) must be true");

  const Rebind<TWFrom, decltype(d_from)> dw_from;
  return PromoteMaskTo(d_to, dw_from, PromoteMaskTo(dw_from, d_from, m));
}

// ------------------------------ DemoteMaskTo

#ifdef HWY_NATIVE_DEMOTE_MASK_TO
#undef HWY_NATIVE_DEMOTE_MASK_TO
#else
#define HWY_NATIVE_DEMOTE_MASK_TO
#endif

template <class DTo, class DFrom, HWY_IF_T_SIZE_D(DTo, 1),
          HWY_IF_T_SIZE_D(DFrom, 2)>
HWY_API svbool_t DemoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/, svbool_t m) {
  return svuzp1_b8(m, m);
}

template <class DTo, class DFrom, HWY_IF_T_SIZE_D(DTo, 2),
          HWY_IF_T_SIZE_D(DFrom, 4)>
HWY_API svbool_t DemoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/, svbool_t m) {
  return svuzp1_b16(m, m);
}

template <class DTo, class DFrom, HWY_IF_T_SIZE_D(DTo, 4),
          HWY_IF_T_SIZE_D(DFrom, 8)>
HWY_API svbool_t DemoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/, svbool_t m) {
  return svuzp1_b32(m, m);
}

template <class DTo, class DFrom,
          HWY_IF_T_SIZE_LE_D(DTo, sizeof(TFromD<DFrom>) / 4)>
HWY_API svbool_t DemoteMaskTo(DTo d_to, DFrom d_from, svbool_t m) {
  using TFrom = TFromD<DFrom>;
  using TNFrom = MakeNarrow<MakeUnsigned<TFrom>>;
  static_assert(sizeof(TNFrom) < sizeof(TFrom),
                "sizeof(TNFrom) < sizeof(TFrom) must be true");

  const Rebind<TNFrom, decltype(d_from)> dn_from;
  return DemoteMaskTo(d_to, dn_from, DemoteMaskTo(dn_from, d_from, m));
}

// ------------------------------ LowerHalfOfMask
#ifdef HWY_NATIVE_LOWER_HALF_OF_MASK
#undef HWY_NATIVE_LOWER_HALF_OF_MASK
#else
#define HWY_NATIVE_LOWER_HALF_OF_MASK
#endif

template <class D>
HWY_API svbool_t LowerHalfOfMask(D /*d*/, svbool_t m) {
  return m;
}

// ------------------------------ MaskedAddOr etc. (IfThenElse)

#ifdef HWY_NATIVE_MASKED_ARITH
#undef HWY_NATIVE_MASKED_ARITH
#else
#define HWY_NATIVE_MASKED_ARITH
#endif

namespace detail {
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGMVV, MaskedMin, min)
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGMVV, MaskedMax, max)
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGMVV, MaskedAdd, add)
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGMVV, MaskedSub, sub)
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGMVV, MaskedMul, mul)
HWY_SVE_FOREACH_F(HWY_SVE_RETV_ARGMVV, MaskedDiv, div)
HWY_SVE_FOREACH_UI32(HWY_SVE_RETV_ARGMVV, MaskedDiv, div)
HWY_SVE_FOREACH_UI64(HWY_SVE_RETV_ARGMVV, MaskedDiv, div)
#if HWY_SVE_HAVE_2
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGMVV, MaskedSatAdd, qadd)
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGMVV, MaskedSatSub, qsub)
#endif
}  // namespace detail

template <class V, class M>
HWY_API V MaskedMinOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedMin(m, a, b), no);
}

template <class V, class M>
HWY_API V MaskedMaxOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedMax(m, a, b), no);
}

template <class V, class M>
HWY_API V MaskedAddOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedAdd(m, a, b), no);
}

template <class V, class M>
HWY_API V MaskedSubOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedSub(m, a, b), no);
}

template <class V, class M>
HWY_API V MaskedMulOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedMul(m, a, b), no);
}

template <class V, class M,
          HWY_IF_T_SIZE_ONE_OF_V(
              V, (hwy::IsSame<TFromV<V>, hwy::float16_t>() ? (1 << 2) : 0) |
                     (1 << 4) | (1 << 8))>
HWY_API V MaskedDivOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedDiv(m, a, b), no);
}

// I8/U8/I16/U16 MaskedDivOr is implemented after I8/U8/I16/U16 Div

#if HWY_SVE_HAVE_2
template <class V, class M>
HWY_API V MaskedSatAddOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedSatAdd(m, a, b), no);
}

template <class V, class M>
HWY_API V MaskedSatSubOr(V no, M m, V a, V b) {
  return IfThenElse(m, detail::MaskedSatSub(m, a, b), no);
}
#else
template <class V, class M>
HWY_API V MaskedSatAddOr(V no, M m, V a, V b) {
  return IfThenElse(m, SaturatedAdd(a, b), no);
}

template <class V, class M>
HWY_API V MaskedSatSubOr(V no, M m, V a, V b) {
  return IfThenElse(m, SaturatedSub(a, b), no);
}
#endif

// ================================================== COMPARE

// mask = f(vector, vector)
#define HWY_SVE_COMPARE(BASE, CHAR, BITS, HALF, NAME, OP)                   \
  HWY_API svbool_t NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS(HWY_SVE_PTRUE(BITS), a, b);                \
  }
#define HWY_SVE_COMPARE_N(BASE, CHAR, BITS, HALF, NAME, OP)                 \
  HWY_API svbool_t NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_T(BASE, BITS) b) { \
    return sv##OP##_##CHAR##BITS(HWY_SVE_PTRUE(BITS), a, b);                \
  }

// ------------------------------ Eq
HWY_SVE_FOREACH(HWY_SVE_COMPARE, Eq, cmpeq)
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, EqN, cmpeq_n)
}  // namespace detail

// ------------------------------ Ne
HWY_SVE_FOREACH(HWY_SVE_COMPARE, Ne, cmpne)
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, NeN, cmpne_n)
}  // namespace detail

// ------------------------------ Lt
HWY_SVE_FOREACH(HWY_SVE_COMPARE, Lt, cmplt)
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, LtN, cmplt_n)
}  // namespace detail

// ------------------------------ Le
HWY_SVE_FOREACH(HWY_SVE_COMPARE, Le, cmple)
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, LeN, cmple_n)
}  // namespace detail

// ------------------------------ Gt/Ge (swapped order)
template <class V>
HWY_API svbool_t Gt(const V a, const V b) {
  return Lt(b, a);
}
template <class V>
HWY_API svbool_t Ge(const V a, const V b) {
  return Le(b, a);
}
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, GeN, cmpge_n)
HWY_SVE_FOREACH(HWY_SVE_COMPARE_N, GtN, cmpgt_n)
}  // namespace detail

#undef HWY_SVE_COMPARE
#undef HWY_SVE_COMPARE_N

// ------------------------------ TestBit
template <class V>
HWY_API svbool_t TestBit(const V a, const V bit) {
  return detail::NeN(And(a, bit), 0);
}

// ------------------------------ MaskFromVec (Ne)
template <class V>
HWY_API svbool_t MaskFromVec(const V v) {
  using T = TFromV<V>;
  return detail::NeN(v, ConvertScalarTo<T>(0));
}

// ------------------------------ VecFromMask
template <class D>
HWY_API VFromD<D> VecFromMask(const D d, svbool_t mask) {
  const RebindToSigned<D> di;
  // This generates MOV imm, whereas svdup_n_s8_z generates MOV scalar, which
  // requires an extra instruction plus M0 pipeline.
  return BitCast(d, IfThenElseZero(mask, Set(di, -1)));
}

// ------------------------------ IsNegative (Lt)
#ifdef HWY_NATIVE_IS_NEGATIVE
#undef HWY_NATIVE_IS_NEGATIVE
#else
#define HWY_NATIVE_IS_NEGATIVE
#endif

template <class V, HWY_IF_NOT_UNSIGNED_V(V)>
HWY_API svbool_t IsNegative(V v) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  using TI = TFromD<decltype(di)>;

  return detail::LtN(BitCast(di, v), static_cast<TI>(0));
}

// ------------------------------ IfVecThenElse (MaskFromVec, IfThenElse)

#if HWY_SVE_HAVE_2

#define HWY_SVE_IF_VEC(BASE, CHAR, BITS, HALF, NAME, OP)          \
  HWY_API HWY_SVE_V(BASE, BITS)                                   \
      NAME(HWY_SVE_V(BASE, BITS) mask, HWY_SVE_V(BASE, BITS) yes, \
           HWY_SVE_V(BASE, BITS) no) {                            \
    return sv##OP##_##CHAR##BITS(yes, no, mask);                  \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_IF_VEC, IfVecThenElse, bsl)
#undef HWY_SVE_IF_VEC

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V IfVecThenElse(const V mask, const V yes, const V no) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, IfVecThenElse(BitCast(du, mask), BitCast(du, yes), BitCast(du, no)));
}

#else

template <class V>
HWY_API V IfVecThenElse(const V mask, const V yes, const V no) {
  return Or(And(mask, yes), AndNot(mask, no));
}

#endif  // HWY_SVE_HAVE_2

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

// ------------------------------ CopySign (BitwiseIfThenElse)
template <class V>
HWY_API V CopySign(const V magn, const V sign) {
  const DFromV<decltype(magn)> d;
  return BitwiseIfThenElse(SignBit(d), sign, magn);
}

// ------------------------------ CopySignToAbs
template <class V>
HWY_API V CopySignToAbs(const V abs, const V sign) {
#if HWY_SVE_HAVE_2  // CopySign is more efficient than OrAnd
  return CopySign(abs, sign);
#else
  const DFromV<V> d;
  return OrAnd(abs, SignBit(d), sign);
#endif
}

// ------------------------------ Floating-point classification (Ne)

template <class V>
HWY_API svbool_t IsNaN(const V v) {
  return Ne(v, v);  // could also use cmpuo
}

// Per-target flag to prevent generic_ops-inl.h from defining IsInf / IsFinite.
// We use a fused Set/comparison for IsFinite.
#ifdef HWY_NATIVE_ISINF
#undef HWY_NATIVE_ISINF
#else
#define HWY_NATIVE_ISINF
#endif

template <class V>
HWY_API svbool_t IsInf(const V v) {
  using T = TFromV<V>;
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;

  // 'Shift left' to clear the sign bit
  const VFromD<decltype(du)> vu = BitCast(du, v);
  const VFromD<decltype(du)> v2 = Add(vu, vu);
  // Check for exponent=max and mantissa=0.
  const VFromD<decltype(di)> max2 = Set(di, hwy::MaxExponentTimes2<T>());
  return RebindMask(d, Eq(v2, BitCast(du, max2)));
}

// Returns whether normal/subnormal/zero.
template <class V>
HWY_API svbool_t IsFinite(const V v) {
  using T = TFromV<V>;
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;  // cheaper than unsigned comparison
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, then right so we can compare with the
  // max exponent (cannot compare with MaxExponentTimes2 directly because it is
  // negative and non-negative floats would be greater).
  const VFromD<decltype(di)> exp =
      BitCast(di, ShiftRight<hwy::MantissaBits<T>() + 1>(Add(vu, vu)));
  return RebindMask(d, detail::LtN(exp, hwy::MaxExponentField<T>()));
}

// ================================================== MEMORY

// ------------------------------ LoadU/MaskedLoad/LoadDup128/StoreU/Stream

#define HWY_SVE_MEM(BASE, CHAR, BITS, HALF, NAME, OP)                         \
  template <size_t N, int kPow2>                                              \
  HWY_API HWY_SVE_V(BASE, BITS)                                               \
      LoadU(HWY_SVE_D(BASE, BITS, N, kPow2) d,                                \
            const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {                   \
    return svld1_##CHAR##BITS(detail::MakeMask(d),                            \
                              detail::NativeLanePointer(p));                  \
  }                                                                           \
  template <size_t N, int kPow2>                                              \
  HWY_API HWY_SVE_V(BASE, BITS)                                               \
      MaskedLoad(svbool_t m, HWY_SVE_D(BASE, BITS, N, kPow2) /* d */,         \
                 const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {              \
    return svld1_##CHAR##BITS(m, detail::NativeLanePointer(p));               \
  }                                                                           \
  template <size_t N, int kPow2>                                              \
  HWY_API void StoreU(HWY_SVE_V(BASE, BITS) v,                                \
                      HWY_SVE_D(BASE, BITS, N, kPow2) d,                      \
                      HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {               \
    svst1_##CHAR##BITS(detail::MakeMask(d), detail::NativeLanePointer(p), v); \
  }                                                                           \
  template <size_t N, int kPow2>                                              \
  HWY_API void Stream(HWY_SVE_V(BASE, BITS) v,                                \
                      HWY_SVE_D(BASE, BITS, N, kPow2) d,                      \
                      HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {               \
    svstnt1_##CHAR##BITS(detail::MakeMask(d), detail::NativeLanePointer(p),   \
                         v);                                                  \
  }                                                                           \
  template <size_t N, int kPow2>                                              \
  HWY_API void BlendedStore(HWY_SVE_V(BASE, BITS) v, svbool_t m,              \
                            HWY_SVE_D(BASE, BITS, N, kPow2) /* d */,          \
                            HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {         \
    svst1_##CHAR##BITS(m, detail::NativeLanePointer(p), v);                   \
  }

HWY_SVE_FOREACH(HWY_SVE_MEM, _, _)
HWY_SVE_FOREACH_BF16(HWY_SVE_MEM, _, _)

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, LoadU(du, detail::U16LanePointer(p)));
}

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  StoreU(BitCast(du, v), du, detail::U16LanePointer(p));
}

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d,
                 MaskedLoad(RebindMask(du, m), du, detail::U16LanePointer(p)));
}

// MaskedLoadOr is generic and does not require emulation.

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  BlendedStore(BitCast(du, v), RebindMask(du, m), du,
               detail::U16LanePointer(p));
}

#undef HWY_SVE_MEM

#if HWY_TARGET != HWY_SVE2_128
namespace detail {
#define HWY_SVE_LOAD_DUP128(BASE, CHAR, BITS, HALF, NAME, OP)   \
  template <size_t N, int kPow2>                                \
  HWY_API HWY_SVE_V(BASE, BITS)                                 \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */,             \
           const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT p) {      \
    /* All-true predicate to load all 128 bits. */              \
    return sv##OP##_##CHAR##BITS(HWY_SVE_PTRUE(8),              \
                                 detail::NativeLanePointer(p)); \
  }

HWY_SVE_FOREACH(HWY_SVE_LOAD_DUP128, LoadDupFull128, ld1rq)
HWY_SVE_FOREACH_BF16(HWY_SVE_LOAD_DUP128, LoadDupFull128, ld1rq)

template <class D, HWY_SVE_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadDupFull128(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, LoadDupFull128(du, detail::U16LanePointer(p)));
}

}  // namespace detail
#endif  // HWY_TARGET != HWY_SVE2_128

#if HWY_TARGET == HWY_SVE2_128
// On the HWY_SVE2_128 target, LoadDup128 is the same as LoadU since vectors
// cannot exceed 16 bytes on the HWY_SVE2_128 target.
template <class D>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}
#else  // HWY_TARGET != HWY_SVE2_128
// If D().MaxBytes() <= 16 is true, simply do a LoadU operation.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}

// If D().MaxBytes() > 16 is true, need to load the vector using ld1rq
template <class D, HWY_IF_V_SIZE_GT_D(D, 16)>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return detail::LoadDupFull128(d, p);
}

#endif  // HWY_TARGET != HWY_SVE2_128

// ------------------------------ Load/Store

// SVE only requires lane alignment, not natural alignment of the entire
// vector, so Load/Store are the same as LoadU/StoreU.
template <class D>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}

template <class V, class D>
HWY_API void Store(const V v, D d, TFromD<D>* HWY_RESTRICT p) {
  StoreU(v, d, p);
}

// ------------------------------ MaskedLoadOr

// SVE MaskedLoad hard-codes zero, so this requires an extra blend.
template <class D>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElse(m, MaskedLoad(m, d, p), v);
}

// ------------------------------ ScatterOffset/Index

#ifdef HWY_NATIVE_SCATTER
#undef HWY_NATIVE_SCATTER
#else
#define HWY_NATIVE_SCATTER
#endif

#define HWY_SVE_SCATTER_OFFSET(BASE, CHAR, BITS, HALF, NAME, OP)             \
  template <size_t N, int kPow2>                                             \
  HWY_API void NAME(HWY_SVE_V(BASE, BITS) v,                                 \
                    HWY_SVE_D(BASE, BITS, N, kPow2) d,                       \
                    HWY_SVE_T(BASE, BITS) * HWY_RESTRICT base,               \
                    HWY_SVE_V(int, BITS) offset) {                           \
    sv##OP##_s##BITS##offset_##CHAR##BITS(detail::MakeMask(d), base, offset, \
                                          v);                                \
  }

#define HWY_SVE_MASKED_SCATTER_INDEX(BASE, CHAR, BITS, HALF, NAME, OP) \
  template <size_t N, int kPow2>                                       \
  HWY_API void NAME(HWY_SVE_V(BASE, BITS) v, svbool_t m,               \
                    HWY_SVE_D(BASE, BITS, N, kPow2) /*d*/,             \
                    HWY_SVE_T(BASE, BITS) * HWY_RESTRICT base,         \
                    HWY_SVE_V(int, BITS) indices) {                    \
    sv##OP##_s##BITS##index_##CHAR##BITS(m, base, indices, v);         \
  }

HWY_SVE_FOREACH_UIF3264(HWY_SVE_SCATTER_OFFSET, ScatterOffset, st1_scatter)
HWY_SVE_FOREACH_UIF3264(HWY_SVE_MASKED_SCATTER_INDEX, MaskedScatterIndex,
                        st1_scatter)
#undef HWY_SVE_SCATTER_OFFSET
#undef HWY_SVE_MASKED_SCATTER_INDEX

template <class D>
HWY_API void ScatterIndex(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p,
                          VFromD<RebindToSigned<D>> indices) {
  MaskedScatterIndex(v, detail::MakeMask(d), d, p, indices);
}

// ------------------------------ GatherOffset/Index

#ifdef HWY_NATIVE_GATHER
#undef HWY_NATIVE_GATHER
#else
#define HWY_NATIVE_GATHER
#endif

#define HWY_SVE_GATHER_OFFSET(BASE, CHAR, BITS, HALF, NAME, OP)             \
  template <size_t N, int kPow2>                                            \
  HWY_API HWY_SVE_V(BASE, BITS)                                             \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d,                               \
           const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT base,                 \
           HWY_SVE_V(int, BITS) offset) {                                   \
    return sv##OP##_s##BITS##offset_##CHAR##BITS(detail::MakeMask(d), base, \
                                                 offset);                   \
  }
#define HWY_SVE_MASKED_GATHER_INDEX(BASE, CHAR, BITS, HALF, NAME, OP) \
  template <size_t N, int kPow2>                                      \
  HWY_API HWY_SVE_V(BASE, BITS)                                       \
      NAME(svbool_t m, HWY_SVE_D(BASE, BITS, N, kPow2) d,             \
           const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT base,           \
           HWY_SVE_V(int, BITS) indices) {                            \
    const RebindToSigned<decltype(d)> di;                             \
    (void)di; /* for HWY_DASSERT */                                   \
    HWY_DASSERT(AllFalse(di, Lt(indices, Zero(di))));                 \
    return sv##OP##_s##BITS##index_##CHAR##BITS(m, base, indices);    \
  }

HWY_SVE_FOREACH_UIF3264(HWY_SVE_GATHER_OFFSET, GatherOffset, ld1_gather)
HWY_SVE_FOREACH_UIF3264(HWY_SVE_MASKED_GATHER_INDEX, MaskedGatherIndex,
                        ld1_gather)
#undef HWY_SVE_GATHER_OFFSET
#undef HWY_SVE_MASKED_GATHER_INDEX

template <class D>
HWY_API VFromD<D> MaskedGatherIndexOr(VFromD<D> no, svbool_t m, D d,
                                      const TFromD<D>* HWY_RESTRICT p,
                                      VFromD<RebindToSigned<D>> indices) {
  return IfThenElse(m, MaskedGatherIndex(m, d, p, indices), no);
}

template <class D>
HWY_API VFromD<D> GatherIndex(D d, const TFromD<D>* HWY_RESTRICT p,
                              VFromD<RebindToSigned<D>> indices) {
  return MaskedGatherIndex(detail::MakeMask(d), d, p, indices);
}

// ------------------------------ LoadInterleaved2

// Per-target flag to prevent generic_ops-inl.h from defining LoadInterleaved2.
#ifdef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_INTERLEAVED
#endif

#define HWY_SVE_LOAD2(BASE, CHAR, BITS, HALF, NAME, OP)                       \
  template <size_t N, int kPow2>                                              \
  HWY_API void NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d,                        \
                    const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned,     \
                    HWY_SVE_V(BASE, BITS) & v0, HWY_SVE_V(BASE, BITS) & v1) { \
    const HWY_SVE_TUPLE(BASE, BITS, 2) tuple = sv##OP##_##CHAR##BITS(         \
        detail::MakeMask(d), detail::NativeLanePointer(unaligned));           \
    v0 = svget2(tuple, 0);                                                    \
    v1 = svget2(tuple, 1);                                                    \
  }
HWY_SVE_FOREACH(HWY_SVE_LOAD2, LoadInterleaved2, ld2)
HWY_SVE_FOREACH_BF16(HWY_SVE_LOAD2, LoadInterleaved2, ld2)

#undef HWY_SVE_LOAD2

// ------------------------------ LoadInterleaved3

#define HWY_SVE_LOAD3(BASE, CHAR, BITS, HALF, NAME, OP)                     \
  template <size_t N, int kPow2>                                            \
  HWY_API void NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d,                      \
                    const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned,   \
                    HWY_SVE_V(BASE, BITS) & v0, HWY_SVE_V(BASE, BITS) & v1, \
                    HWY_SVE_V(BASE, BITS) & v2) {                           \
    const HWY_SVE_TUPLE(BASE, BITS, 3) tuple = sv##OP##_##CHAR##BITS(       \
        detail::MakeMask(d), detail::NativeLanePointer(unaligned));         \
    v0 = svget3(tuple, 0);                                                  \
    v1 = svget3(tuple, 1);                                                  \
    v2 = svget3(tuple, 2);                                                  \
  }
HWY_SVE_FOREACH(HWY_SVE_LOAD3, LoadInterleaved3, ld3)
HWY_SVE_FOREACH_BF16(HWY_SVE_LOAD3, LoadInterleaved3, ld3)

#undef HWY_SVE_LOAD3

// ------------------------------ LoadInterleaved4

#define HWY_SVE_LOAD4(BASE, CHAR, BITS, HALF, NAME, OP)                       \
  template <size_t N, int kPow2>                                              \
  HWY_API void NAME(HWY_SVE_D(BASE, BITS, N, kPow2) d,                        \
                    const HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned,     \
                    HWY_SVE_V(BASE, BITS) & v0, HWY_SVE_V(BASE, BITS) & v1,   \
                    HWY_SVE_V(BASE, BITS) & v2, HWY_SVE_V(BASE, BITS) & v3) { \
    const HWY_SVE_TUPLE(BASE, BITS, 4) tuple = sv##OP##_##CHAR##BITS(         \
        detail::MakeMask(d), detail::NativeLanePointer(unaligned));           \
    v0 = svget4(tuple, 0);                                                    \
    v1 = svget4(tuple, 1);                                                    \
    v2 = svget4(tuple, 2);                                                    \
    v3 = svget4(tuple, 3);                                                    \
  }
HWY_SVE_FOREACH(HWY_SVE_LOAD4, LoadInterleaved4, ld4)
HWY_SVE_FOREACH_BF16(HWY_SVE_LOAD4, LoadInterleaved4, ld4)

#undef HWY_SVE_LOAD4

// ------------------------------ StoreInterleaved2

#define HWY_SVE_STORE2(BASE, CHAR, BITS, HALF, NAME, OP)                \
  template <size_t N, int kPow2>                                        \
  HWY_API void NAME(HWY_SVE_V(BASE, BITS) v0, HWY_SVE_V(BASE, BITS) v1, \
                    HWY_SVE_D(BASE, BITS, N, kPow2) d,                  \
                    HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned) {   \
    sv##OP##_##CHAR##BITS(detail::MakeMask(d),                          \
                          detail::NativeLanePointer(unaligned),         \
                          Create2(d, v0, v1));                          \
  }
HWY_SVE_FOREACH(HWY_SVE_STORE2, StoreInterleaved2, st2)
HWY_SVE_FOREACH_BF16(HWY_SVE_STORE2, StoreInterleaved2, st2)

#undef HWY_SVE_STORE2

// ------------------------------ StoreInterleaved3

#define HWY_SVE_STORE3(BASE, CHAR, BITS, HALF, NAME, OP)                \
  template <size_t N, int kPow2>                                        \
  HWY_API void NAME(HWY_SVE_V(BASE, BITS) v0, HWY_SVE_V(BASE, BITS) v1, \
                    HWY_SVE_V(BASE, BITS) v2,                           \
                    HWY_SVE_D(BASE, BITS, N, kPow2) d,                  \
                    HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned) {   \
    sv##OP##_##CHAR##BITS(detail::MakeMask(d),                          \
                          detail::NativeLanePointer(unaligned),         \
                          Create3(d, v0, v1, v2));                      \
  }
HWY_SVE_FOREACH(HWY_SVE_STORE3, StoreInterleaved3, st3)
HWY_SVE_FOREACH_BF16(HWY_SVE_STORE3, StoreInterleaved3, st3)

#undef HWY_SVE_STORE3

// ------------------------------ StoreInterleaved4

#define HWY_SVE_STORE4(BASE, CHAR, BITS, HALF, NAME, OP)                \
  template <size_t N, int kPow2>                                        \
  HWY_API void NAME(HWY_SVE_V(BASE, BITS) v0, HWY_SVE_V(BASE, BITS) v1, \
                    HWY_SVE_V(BASE, BITS) v2, HWY_SVE_V(BASE, BITS) v3, \
                    HWY_SVE_D(BASE, BITS, N, kPow2) d,                  \
                    HWY_SVE_T(BASE, BITS) * HWY_RESTRICT unaligned) {   \
    sv##OP##_##CHAR##BITS(detail::MakeMask(d),                          \
                          detail::NativeLanePointer(unaligned),         \
                          Create4(d, v0, v1, v2, v3));                  \
  }
HWY_SVE_FOREACH(HWY_SVE_STORE4, StoreInterleaved4, st4)
HWY_SVE_FOREACH_BF16(HWY_SVE_STORE4, StoreInterleaved4, st4)

#undef HWY_SVE_STORE4

// Fall back on generic Load/StoreInterleaved[234] for any emulated types.
// Requires HWY_GENERIC_IF_EMULATED_D mirrors HWY_SVE_IF_EMULATED_D.

// ================================================== CONVERT

// ------------------------------ PromoteTo

// Same sign
#define HWY_SVE_PROMOTE_TO(BASE, CHAR, BITS, HALF, NAME, OP)                \
  template <size_t N, int kPow2>                                            \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(                                       \
      HWY_SVE_D(BASE, BITS, N, kPow2) /* tag */, HWY_SVE_V(BASE, HALF) v) { \
    return sv##OP##_##CHAR##BITS(v);                                        \
  }

HWY_SVE_FOREACH_UI16(HWY_SVE_PROMOTE_TO, PromoteTo, unpklo)
HWY_SVE_FOREACH_UI32(HWY_SVE_PROMOTE_TO, PromoteTo, unpklo)
HWY_SVE_FOREACH_UI64(HWY_SVE_PROMOTE_TO, PromoteTo, unpklo)

// 2x
template <size_t N, int kPow2>
HWY_API svuint32_t PromoteTo(Simd<uint32_t, N, kPow2> dto, svuint8_t vfrom) {
  const RepartitionToWide<DFromV<decltype(vfrom)>> d2;
  return PromoteTo(dto, PromoteTo(d2, vfrom));
}
template <size_t N, int kPow2>
HWY_API svint32_t PromoteTo(Simd<int32_t, N, kPow2> dto, svint8_t vfrom) {
  const RepartitionToWide<DFromV<decltype(vfrom)>> d2;
  return PromoteTo(dto, PromoteTo(d2, vfrom));
}
template <size_t N, int kPow2>
HWY_API svuint64_t PromoteTo(Simd<uint64_t, N, kPow2> dto, svuint16_t vfrom) {
  const RepartitionToWide<DFromV<decltype(vfrom)>> d2;
  return PromoteTo(dto, PromoteTo(d2, vfrom));
}
template <size_t N, int kPow2>
HWY_API svint64_t PromoteTo(Simd<int64_t, N, kPow2> dto, svint16_t vfrom) {
  const RepartitionToWide<DFromV<decltype(vfrom)>> d2;
  return PromoteTo(dto, PromoteTo(d2, vfrom));
}

// 3x
template <size_t N, int kPow2>
HWY_API svuint64_t PromoteTo(Simd<uint64_t, N, kPow2> dto, svuint8_t vfrom) {
  const RepartitionToNarrow<decltype(dto)> d4;
  const RepartitionToNarrow<decltype(d4)> d2;
  return PromoteTo(dto, PromoteTo(d4, PromoteTo(d2, vfrom)));
}
template <size_t N, int kPow2>
HWY_API svint64_t PromoteTo(Simd<int64_t, N, kPow2> dto, svint8_t vfrom) {
  const RepartitionToNarrow<decltype(dto)> d4;
  const RepartitionToNarrow<decltype(d4)> d2;
  return PromoteTo(dto, PromoteTo(d4, PromoteTo(d2, vfrom)));
}

// Sign change
template <class D, class V, HWY_IF_SIGNED_D(D), HWY_IF_UNSIGNED_V(V),
          HWY_IF_LANES_GT(sizeof(TFromD<D>), sizeof(TFromV<V>))>
HWY_API VFromD<D> PromoteTo(D di, V v) {
  const RebindToUnsigned<decltype(di)> du;
  return BitCast(di, PromoteTo(du, v));
}

// ------------------------------ PromoteTo F

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif

// Unlike Highway's ZipLower, this returns the same type.
namespace detail {
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGVV, ZipLowerSame, zip1)
}  // namespace detail

template <size_t N, int kPow2>
HWY_API svfloat32_t PromoteTo(Simd<float32_t, N, kPow2> /* d */,
                              const svfloat16_t v) {
  // svcvt* expects inputs in even lanes, whereas Highway wants lower lanes, so
  // first replicate each lane once.
  const svfloat16_t vv = detail::ZipLowerSame(v, v);
  return svcvt_f32_f16_x(detail::PTrue(Simd<float16_t, N, kPow2>()), vv);
}

#ifdef HWY_NATIVE_PROMOTE_F16_TO_F64
#undef HWY_NATIVE_PROMOTE_F16_TO_F64
#else
#define HWY_NATIVE_PROMOTE_F16_TO_F64
#endif

template <size_t N, int kPow2>
HWY_API svfloat64_t PromoteTo(Simd<float64_t, N, kPow2> /* d */,
                              const svfloat16_t v) {
  // svcvt* expects inputs in even lanes, whereas Highway wants lower lanes, so
  // first replicate each lane once.
  const svfloat16_t vv = detail::ZipLowerSame(v, v);
  return svcvt_f64_f16_x(detail::PTrue(Simd<float16_t, N, kPow2>()),
                         detail::ZipLowerSame(vv, vv));
}

template <size_t N, int kPow2>
HWY_API svfloat64_t PromoteTo(Simd<float64_t, N, kPow2> /* d */,
                              const svfloat32_t v) {
  const svfloat32_t vv = detail::ZipLowerSame(v, v);
  return svcvt_f64_f32_x(detail::PTrue(Simd<float32_t, N, kPow2>()), vv);
}

template <size_t N, int kPow2>
HWY_API svfloat64_t PromoteTo(Simd<float64_t, N, kPow2> /* d */,
                              const svint32_t v) {
  const svint32_t vv = detail::ZipLowerSame(v, v);
  return svcvt_f64_s32_x(detail::PTrue(Simd<int32_t, N, kPow2>()), vv);
}

template <size_t N, int kPow2>
HWY_API svfloat64_t PromoteTo(Simd<float64_t, N, kPow2> /* d */,
                              const svuint32_t v) {
  const svuint32_t vv = detail::ZipLowerSame(v, v);
  return svcvt_f64_u32_x(detail::PTrue(Simd<uint32_t, N, kPow2>()), vv);
}

template <size_t N, int kPow2>
HWY_API svint64_t PromoteTo(Simd<int64_t, N, kPow2> /* d */,
                            const svfloat32_t v) {
  const svfloat32_t vv = detail::ZipLowerSame(v, v);
  return svcvt_s64_f32_x(detail::PTrue(Simd<float, N, kPow2>()), vv);
}

template <size_t N, int kPow2>
HWY_API svuint64_t PromoteTo(Simd<uint64_t, N, kPow2> /* d */,
                             const svfloat32_t v) {
  const svfloat32_t vv = detail::ZipLowerSame(v, v);
  return svcvt_u64_f32_x(detail::PTrue(Simd<float, N, kPow2>()), vv);
}

// ------------------------------ PromoteUpperTo

namespace detail {
HWY_SVE_FOREACH_UI16(HWY_SVE_PROMOTE_TO, PromoteUpperTo, unpkhi)
HWY_SVE_FOREACH_UI32(HWY_SVE_PROMOTE_TO, PromoteUpperTo, unpkhi)
HWY_SVE_FOREACH_UI64(HWY_SVE_PROMOTE_TO, PromoteUpperTo, unpkhi)
#undef HWY_SVE_PROMOTE_TO
}  // namespace detail

#ifdef HWY_NATIVE_PROMOTE_UPPER_TO
#undef HWY_NATIVE_PROMOTE_UPPER_TO
#else
#define HWY_NATIVE_PROMOTE_UPPER_TO
#endif

// Unsigned->Unsigned or Signed->Signed
template <class D, class V, typename TD = TFromD<D>, typename TV = TFromV<V>,
          hwy::EnableIf<IsInteger<TD>() && IsInteger<TV>() &&
                        (IsSigned<TD>() == IsSigned<TV>())>* = nullptr>
HWY_API VFromD<D> PromoteUpperTo(D d, V v) {
  if (detail::IsFull(d)) {
    return detail::PromoteUpperTo(d, v);
  }
  const Rebind<TFromV<V>, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
}

// Differing signs or either is float
template <class D, class V, typename TD = TFromD<D>, typename TV = TFromV<V>,
          hwy::EnableIf<!IsInteger<TD>() || !IsInteger<TV>() ||
                        (IsSigned<TD>() != IsSigned<TV>())>* = nullptr>
HWY_API VFromD<D> PromoteUpperTo(D d, V v) {
  // Lanes(d) may differ from Lanes(DFromV<V>()). Use the lane type from V
  // because it cannot be deduced from D (could be either bf16 or f16).
  const Rebind<TFromV<V>, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
}

// ------------------------------ DemoteTo U

namespace detail {

// Saturates unsigned vectors to half/quarter-width TN.
template <typename TN, class VU>
VU SaturateU(VU v) {
  return detail::MinN(v, static_cast<TFromV<VU>>(LimitsMax<TN>()));
}

// Saturates unsigned vectors to half/quarter-width TN.
template <typename TN, class VI>
VI SaturateI(VI v) {
  return detail::MinN(detail::MaxN(v, LimitsMin<TN>()), LimitsMax<TN>());
}

}  // namespace detail

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svint16_t v) {
#if HWY_SVE_HAVE_2
  const svuint8_t vn = BitCast(dn, svqxtunb_s16(v));
#else
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint16_t clamped = BitCast(du, detail::MaxN(v, 0));
  // Saturate to unsigned-max and halve the width.
  const svuint8_t vn = BitCast(dn, detail::SaturateU<TN>(clamped));
#endif
  return svuzp1_u8(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint16_t DemoteTo(Simd<uint16_t, N, kPow2> dn, const svint32_t v) {
#if HWY_SVE_HAVE_2
  const svuint16_t vn = BitCast(dn, svqxtunb_s32(v));
#else
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint32_t clamped = BitCast(du, detail::MaxN(v, 0));
  // Saturate to unsigned-max and halve the width.
  const svuint16_t vn = BitCast(dn, detail::SaturateU<TN>(clamped));
#endif
  return svuzp1_u16(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svint32_t v) {
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const RepartitionToNarrow<decltype(du)> d2;
#if HWY_SVE_HAVE_2
  const svuint16_t cast16 = BitCast(d2, svqxtnb_u16(svqxtunb_s32(v)));
#else
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint32_t clamped = BitCast(du, detail::MaxN(v, 0));
  // Saturate to unsigned-max and quarter the width.
  const svuint16_t cast16 = BitCast(d2, detail::SaturateU<TN>(clamped));
#endif
  const svuint8_t x2 = BitCast(dn, svuzp1_u16(cast16, cast16));
  return svuzp1_u8(x2, x2);
}

HWY_API svuint8_t U8FromU32(const svuint32_t v) {
  const DFromV<svuint32_t> du32;
  const RepartitionToNarrow<decltype(du32)> du16;
  const RepartitionToNarrow<decltype(du16)> du8;

  const svuint16_t cast16 = BitCast(du16, v);
  const svuint16_t x2 = svuzp1_u16(cast16, cast16);
  const svuint8_t cast8 = BitCast(du8, x2);
  return svuzp1_u8(cast8, cast8);
}

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svuint16_t v) {
#if HWY_SVE_HAVE_2
  const svuint8_t vn = BitCast(dn, svqxtnb_u16(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint8_t vn = BitCast(dn, detail::SaturateU<TN>(v));
#endif
  return svuzp1_u8(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint16_t DemoteTo(Simd<uint16_t, N, kPow2> dn, const svuint32_t v) {
#if HWY_SVE_HAVE_2
  const svuint16_t vn = BitCast(dn, svqxtnb_u32(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint16_t vn = BitCast(dn, detail::SaturateU<TN>(v));
#endif
  return svuzp1_u16(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svuint32_t v) {
  using TN = TFromD<decltype(dn)>;
  return U8FromU32(detail::SaturateU<TN>(v));
}

// ------------------------------ Truncations

template <size_t N, int kPow2>
HWY_API svuint8_t TruncateTo(Simd<uint8_t, N, kPow2> /* tag */,
                             const svuint64_t v) {
  const DFromV<svuint8_t> d;
  const svuint8_t v1 = BitCast(d, v);
  const svuint8_t v2 = svuzp1_u8(v1, v1);
  const svuint8_t v3 = svuzp1_u8(v2, v2);
  return svuzp1_u8(v3, v3);
}

template <size_t N, int kPow2>
HWY_API svuint16_t TruncateTo(Simd<uint16_t, N, kPow2> /* tag */,
                              const svuint64_t v) {
  const DFromV<svuint16_t> d;
  const svuint16_t v1 = BitCast(d, v);
  const svuint16_t v2 = svuzp1_u16(v1, v1);
  return svuzp1_u16(v2, v2);
}

template <size_t N, int kPow2>
HWY_API svuint32_t TruncateTo(Simd<uint32_t, N, kPow2> /* tag */,
                              const svuint64_t v) {
  const DFromV<svuint32_t> d;
  const svuint32_t v1 = BitCast(d, v);
  return svuzp1_u32(v1, v1);
}

template <size_t N, int kPow2>
HWY_API svuint8_t TruncateTo(Simd<uint8_t, N, kPow2> /* tag */,
                             const svuint32_t v) {
  const DFromV<svuint8_t> d;
  const svuint8_t v1 = BitCast(d, v);
  const svuint8_t v2 = svuzp1_u8(v1, v1);
  return svuzp1_u8(v2, v2);
}

template <size_t N, int kPow2>
HWY_API svuint16_t TruncateTo(Simd<uint16_t, N, kPow2> /* tag */,
                              const svuint32_t v) {
  const DFromV<svuint16_t> d;
  const svuint16_t v1 = BitCast(d, v);
  return svuzp1_u16(v1, v1);
}

template <size_t N, int kPow2>
HWY_API svuint8_t TruncateTo(Simd<uint8_t, N, kPow2> /* tag */,
                             const svuint16_t v) {
  const DFromV<svuint8_t> d;
  const svuint8_t v1 = BitCast(d, v);
  return svuzp1_u8(v1, v1);
}

// ------------------------------ DemoteTo I

template <size_t N, int kPow2>
HWY_API svint8_t DemoteTo(Simd<int8_t, N, kPow2> dn, const svint16_t v) {
#if HWY_SVE_HAVE_2
  const svint8_t vn = BitCast(dn, svqxtnb_s16(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svint8_t vn = BitCast(dn, detail::SaturateI<TN>(v));
#endif
  return svuzp1_s8(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svint16_t DemoteTo(Simd<int16_t, N, kPow2> dn, const svint32_t v) {
#if HWY_SVE_HAVE_2
  const svint16_t vn = BitCast(dn, svqxtnb_s32(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svint16_t vn = BitCast(dn, detail::SaturateI<TN>(v));
#endif
  return svuzp1_s16(vn, vn);
}

template <size_t N, int kPow2>
HWY_API svint8_t DemoteTo(Simd<int8_t, N, kPow2> dn, const svint32_t v) {
  const RepartitionToWide<decltype(dn)> d2;
#if HWY_SVE_HAVE_2
  const svint16_t cast16 = BitCast(d2, svqxtnb_s16(svqxtnb_s32(v)));
#else
  using TN = TFromD<decltype(dn)>;
  const svint16_t cast16 = BitCast(d2, detail::SaturateI<TN>(v));
#endif
  const svint8_t v2 = BitCast(dn, svuzp1_s16(cast16, cast16));
  return BitCast(dn, svuzp1_s8(v2, v2));
}

// ------------------------------ I64/U64 DemoteTo

template <size_t N, int kPow2>
HWY_API svint32_t DemoteTo(Simd<int32_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
  const RebindToUnsigned<decltype(dn)> dn_u;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtnb_s64(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateI<TN>(v));
#endif
  return BitCast(dn, TruncateTo(dn_u, vn));
}

template <size_t N, int kPow2>
HWY_API svint16_t DemoteTo(Simd<int16_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
  const RebindToUnsigned<decltype(dn)> dn_u;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtnb_s32(svqxtnb_s64(v)));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateI<TN>(v));
#endif
  return BitCast(dn, TruncateTo(dn_u, vn));
}

template <size_t N, int kPow2>
HWY_API svint8_t DemoteTo(Simd<int8_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
  const RebindToUnsigned<decltype(dn)> dn_u;
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateI<TN>(v));
  return BitCast(dn, TruncateTo(dn_u, vn));
}

template <size_t N, int kPow2>
HWY_API svuint32_t DemoteTo(Simd<uint32_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtunb_s64(v));
#else
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint64_t clamped = BitCast(du64, detail::MaxN(v, 0));
  // Saturate to unsigned-max
  const svuint64_t vn = detail::SaturateU<TN>(clamped);
#endif
  return TruncateTo(dn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint16_t DemoteTo(Simd<uint16_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtnb_u32(svqxtunb_s64(v)));
#else
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint64_t clamped = BitCast(du64, detail::MaxN(v, 0));
  // Saturate to unsigned-max
  const svuint64_t vn = detail::SaturateU<TN>(clamped);
#endif
  return TruncateTo(dn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
  using TN = TFromD<decltype(dn)>;
  // First clamp negative numbers to zero and cast to unsigned.
  const svuint64_t clamped = BitCast(du64, detail::MaxN(v, 0));
  // Saturate to unsigned-max
  const svuint64_t vn = detail::SaturateU<TN>(clamped);
  return TruncateTo(dn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint32_t DemoteTo(Simd<uint32_t, N, kPow2> dn, const svuint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtnb_u64(v));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateU<TN>(v));
#endif
  return TruncateTo(dn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint16_t DemoteTo(Simd<uint16_t, N, kPow2> dn, const svuint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
#if HWY_SVE_HAVE_2
  const svuint64_t vn = BitCast(du64, svqxtnb_u32(svqxtnb_u64(v)));
#else
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateU<TN>(v));
#endif
  return TruncateTo(dn, vn);
}

template <size_t N, int kPow2>
HWY_API svuint8_t DemoteTo(Simd<uint8_t, N, kPow2> dn, const svuint64_t v) {
  const Rebind<uint64_t, decltype(dn)> du64;
  using TN = TFromD<decltype(dn)>;
  const svuint64_t vn = BitCast(du64, detail::SaturateU<TN>(v));
  return TruncateTo(dn, vn);
}

// ------------------------------ Unsigned to signed demotions

// Disable the default unsigned to signed DemoteTo/ReorderDemote2To
// implementations in generic_ops-inl.h on SVE/SVE2 as the SVE/SVE2 targets have
// target-specific implementations of the unsigned to signed DemoteTo and
// ReorderDemote2To ops

// NOTE: hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr is used instead of
// hwy::EnableIf<false>* = nullptr to avoid compiler errors since
// !hwy::IsSame<V, V>() is always false and as !hwy::IsSame<V, V>() will cause
// SFINAE to occur instead of a hard error due to a dependency on the V template
// argument
#undef HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V
#define HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V(V) \
  hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr

template <class D, class V, HWY_IF_SIGNED_D(D), HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_LE_D(D, sizeof(TFromV<V>) - 1)>
HWY_API VFromD<D> DemoteTo(D dn, V v) {
  const RebindToUnsigned<D> dn_u;
  return BitCast(dn, TruncateTo(dn_u, detail::SaturateU<TFromD<D>>(v)));
}

// ------------------------------ ConcatEven/ConcatOdd

// WARNING: the upper half of these needs fixing up (uzp1/uzp2 use the
// full vector length, not rounded down to a power of two as we require).
namespace detail {

#define HWY_SVE_CONCAT_EVERY_SECOND(BASE, CHAR, BITS, HALF, NAME, OP) \
  HWY_INLINE HWY_SVE_V(BASE, BITS)                                    \
      NAME(HWY_SVE_V(BASE, BITS) hi, HWY_SVE_V(BASE, BITS) lo) {      \
    return sv##OP##_##CHAR##BITS(lo, hi);                             \
  }
HWY_SVE_FOREACH(HWY_SVE_CONCAT_EVERY_SECOND, ConcatEvenFull, uzp1)
HWY_SVE_FOREACH(HWY_SVE_CONCAT_EVERY_SECOND, ConcatOddFull, uzp2)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CONCAT_EVERY_SECOND, ConcatEvenFull,
                                   uzp1)
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CONCAT_EVERY_SECOND, ConcatOddFull,
                                   uzp2)
#endif  // HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
#if defined(__ARM_FEATURE_SVE_MATMUL_FP64)
HWY_SVE_FOREACH(HWY_SVE_CONCAT_EVERY_SECOND, ConcatEvenBlocks, uzp1q)
HWY_SVE_FOREACH(HWY_SVE_CONCAT_EVERY_SECOND, ConcatOddBlocks, uzp2q)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CONCAT_EVERY_SECOND,
                                   ConcatEvenBlocks, uzp1q)
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_CONCAT_EVERY_SECOND, ConcatOddBlocks,
                                   uzp2q)
#endif  // HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
#endif  // defined(__ARM_FEATURE_SVE_MATMUL_FP64)
#undef HWY_SVE_CONCAT_EVERY_SECOND

// Used to slide up / shift whole register left; mask indicates which range
// to take from lo, and the rest is filled from hi starting at its lowest.
#define HWY_SVE_SPLICE(BASE, CHAR, BITS, HALF, NAME, OP)                   \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(                                      \
      HWY_SVE_V(BASE, BITS) hi, HWY_SVE_V(BASE, BITS) lo, svbool_t mask) { \
    return sv##OP##_##CHAR##BITS(mask, lo, hi);                            \
  }
HWY_SVE_FOREACH(HWY_SVE_SPLICE, Splice, splice)
#if HWY_SVE_HAVE_BF16_FEATURE
HWY_SVE_FOREACH_BF16(HWY_SVE_SPLICE, Splice, splice)
#else
template <class V, HWY_IF_BF16_D(DFromV<V>)>
HWY_INLINE V Splice(V hi, V lo, svbool_t mask) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Splice(BitCast(du, hi), BitCast(du, lo), mask));
}
#endif  // HWY_SVE_HAVE_BF16_FEATURE
#undef HWY_SVE_SPLICE

}  // namespace detail

template <class D>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_SVE_IS_POW2
  if (detail::IsFull(d)) return detail::ConcatOddFull(hi, lo);
#endif
  const VFromD<D> hi_odd = detail::ConcatOddFull(hi, hi);
  const VFromD<D> lo_odd = detail::ConcatOddFull(lo, lo);
  return detail::Splice(hi_odd, lo_odd, FirstN(d, Lanes(d) / 2));
}

template <class D>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_SVE_IS_POW2
  if (detail::IsFull(d)) return detail::ConcatEvenFull(hi, lo);
#endif
  const VFromD<D> hi_odd = detail::ConcatEvenFull(hi, hi);
  const VFromD<D> lo_odd = detail::ConcatEvenFull(lo, lo);
  return detail::Splice(hi_odd, lo_odd, FirstN(d, Lanes(d) / 2));
}

// ------------------------------ PromoteEvenTo/PromoteOddTo

// Signed to signed PromoteEvenTo: 1 instruction instead of 2 in generic-inl.h.
// Might as well also enable unsigned to unsigned, though it is just an And.
namespace detail {
HWY_SVE_FOREACH_UI16(HWY_SVE_RETV_ARGPV, NativePromoteEvenTo, extb)
HWY_SVE_FOREACH_UI32(HWY_SVE_RETV_ARGPV, NativePromoteEvenTo, exth)
HWY_SVE_FOREACH_UI64(HWY_SVE_RETV_ARGPV, NativePromoteEvenTo, extw)
}  // namespace detail

#include "hwy/ops/inside-inl.h"

// ------------------------------ DemoteTo F

// We already toggled HWY_NATIVE_F16C above.

template <size_t N, int kPow2>
HWY_API svfloat16_t DemoteTo(Simd<float16_t, N, kPow2> d, const svfloat32_t v) {
  const svfloat16_t in_even = svcvt_f16_f32_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

#ifdef HWY_NATIVE_DEMOTE_F64_TO_F16
#undef HWY_NATIVE_DEMOTE_F64_TO_F16
#else
#define HWY_NATIVE_DEMOTE_F64_TO_F16
#endif

template <size_t N, int kPow2>
HWY_API svfloat16_t DemoteTo(Simd<float16_t, N, kPow2> d, const svfloat64_t v) {
  const svfloat16_t in_lo16 = svcvt_f16_f64_x(detail::PTrue(d), v);
  const svfloat16_t in_even = detail::ConcatEvenFull(in_lo16, in_lo16);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

#if !HWY_SVE_HAVE_F32_TO_BF16C
namespace detail {

// Round a F32 value to the nearest BF16 value, with the result returned as the
// rounded F32 value bitcasted to an U32

// RoundF32ForDemoteToBF16 also converts NaN values to QNaN values to prevent
// NaN F32 values from being converted to an infinity
HWY_INLINE svuint32_t RoundF32ForDemoteToBF16(svfloat32_t v) {
  const DFromV<decltype(v)> df32;
  const RebindToUnsigned<decltype(df32)> du32;

  const auto is_non_nan = Eq(v, v);
  const auto bits32 = BitCast(du32, v);

  const auto round_incr =
      detail::AddN(detail::AndN(ShiftRight<16>(bits32), 1u), 0x7FFFu);
  return MaskedAddOr(detail::OrN(bits32, 0x00400000u), is_non_nan, bits32,
                     round_incr);
}

}  // namespace detail
#endif  // !HWY_SVE_HAVE_F32_TO_BF16C

template <size_t N, int kPow2>
HWY_API VBF16 DemoteTo(Simd<bfloat16_t, N, kPow2> dbf16, svfloat32_t v) {
#if HWY_SVE_HAVE_F32_TO_BF16C
  const VBF16 in_even = svcvt_bf16_f32_x(detail::PTrue(dbf16), v);
  return detail::ConcatEvenFull(in_even, in_even);
#else
  const svuint16_t in_odd =
      BitCast(ScalableTag<uint16_t>(), detail::RoundF32ForDemoteToBF16(v));
  return BitCast(dbf16, detail::ConcatOddFull(in_odd, in_odd));  // lower half
#endif
}

template <size_t N, int kPow2>
HWY_API svfloat32_t DemoteTo(Simd<float32_t, N, kPow2> d, const svfloat64_t v) {
  const svfloat32_t in_even = svcvt_f32_f64_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

template <size_t N, int kPow2>
HWY_API svint32_t DemoteTo(Simd<int32_t, N, kPow2> d, const svfloat64_t v) {
  const svint32_t in_even = svcvt_s32_f64_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

template <size_t N, int kPow2>
HWY_API svuint32_t DemoteTo(Simd<uint32_t, N, kPow2> d, const svfloat64_t v) {
  const svuint32_t in_even = svcvt_u32_f64_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

template <size_t N, int kPow2>
HWY_API svfloat32_t DemoteTo(Simd<float, N, kPow2> d, const svint64_t v) {
  const svfloat32_t in_even = svcvt_f32_s64_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

template <size_t N, int kPow2>
HWY_API svfloat32_t DemoteTo(Simd<float, N, kPow2> d, const svuint64_t v) {
  const svfloat32_t in_even = svcvt_f32_u64_x(detail::PTrue(d), v);
  return detail::ConcatEvenFull(in_even,
                                in_even);  // lower half
}

// ------------------------------ ConvertTo F

#define HWY_SVE_CONVERT(BASE, CHAR, BITS, HALF, NAME, OP)                      \
  /* Float from signed */                                                      \
  template <size_t N, int kPow2>                                               \
  HWY_API HWY_SVE_V(BASE, BITS)                                                \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, HWY_SVE_V(int, BITS) v) {  \
    return sv##OP##_##CHAR##BITS##_s##BITS##_x(HWY_SVE_PTRUE(BITS), v);        \
  }                                                                            \
  /* Float from unsigned */                                                    \
  template <size_t N, int kPow2>                                               \
  HWY_API HWY_SVE_V(BASE, BITS)                                                \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, HWY_SVE_V(uint, BITS) v) { \
    return sv##OP##_##CHAR##BITS##_u##BITS##_x(HWY_SVE_PTRUE(BITS), v);        \
  }                                                                            \
  /* Signed from float, rounding toward zero */                                \
  template <size_t N, int kPow2>                                               \
  HWY_API HWY_SVE_V(int, BITS)                                                 \
      NAME(HWY_SVE_D(int, BITS, N, kPow2) /* d */, HWY_SVE_V(BASE, BITS) v) {  \
    return sv##OP##_s##BITS##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v);        \
  }                                                                            \
  /* Unsigned from float, rounding toward zero */                              \
  template <size_t N, int kPow2>                                               \
  HWY_API HWY_SVE_V(uint, BITS)                                                \
      NAME(HWY_SVE_D(uint, BITS, N, kPow2) /* d */, HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_u##BITS##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), v);        \
  }

HWY_SVE_FOREACH_F(HWY_SVE_CONVERT, ConvertTo, cvt)
#undef HWY_SVE_CONVERT

// ------------------------------ NearestInt (Round, ConvertTo)
template <class VF, class DI = RebindToSigned<DFromV<VF>>>
HWY_API VFromD<DI> NearestInt(VF v) {
  // No single instruction, round then truncate.
  return ConvertTo(DI(), Round(v));
}

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 di32,
                                        VFromD<Rebind<double, DI32>> v) {
  // No single instruction, round then demote.
  return DemoteTo(di32, Round(v));
}

// ------------------------------ Iota (Add, ConvertTo)

#define HWY_SVE_IOTA(BASE, CHAR, BITS, HALF, NAME, OP)          \
  template <size_t N, int kPow2, typename T2>                   \
  HWY_API HWY_SVE_V(BASE, BITS)                                 \
      NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /* d */, T2 first) { \
    return sv##OP##_##CHAR##BITS(                               \
        ConvertScalarTo<HWY_SVE_T(BASE, BITS)>(first), 1);      \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_IOTA, Iota, index)
#undef HWY_SVE_IOTA

template <class D, typename T2, HWY_IF_FLOAT_D(D)>
HWY_API VFromD<D> Iota(const D d, T2 first) {
  const RebindToSigned<D> di;
  return detail::AddN(ConvertTo(d, Iota(di, 0)),
                      ConvertScalarTo<TFromD<D>>(first));
}

// ------------------------------ InterleaveLower

template <class D, class V>
HWY_API V InterleaveLower(D d, const V a, const V b) {
  static_assert(IsSame<TFromD<D>, TFromV<V>>(), "D/V mismatch");
#if HWY_TARGET == HWY_SVE2_128
  (void)d;
  return detail::ZipLowerSame(a, b);
#else
  // Move lower halves of blocks to lower half of vector.
  const Repartition<uint64_t, decltype(d)> d64;
  const auto a64 = BitCast(d64, a);
  const auto b64 = BitCast(d64, b);
  const auto a_blocks = detail::ConcatEvenFull(a64, a64);  // lower half
  const auto b_blocks = detail::ConcatEvenFull(b64, b64);
  return detail::ZipLowerSame(BitCast(d, a_blocks), BitCast(d, b_blocks));
#endif
}

template <class V>
HWY_API V InterleaveLower(const V a, const V b) {
  return InterleaveLower(DFromV<V>(), a, b);
}

// ------------------------------ InterleaveUpper

// Only use zip2 if vector are a powers of two, otherwise getting the actual
// "upper half" requires MaskUpperHalf.
namespace detail {
// Unlike Highway's ZipUpper, this returns the same type.
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGVV, ZipUpperSame, zip2)
}  // namespace detail

// Full vector: guaranteed to have at least one block
template <class D, class V = VFromD<D>,
          hwy::EnableIf<detail::IsFull(D())>* = nullptr>
HWY_API V InterleaveUpper(D d, const V a, const V b) {
#if HWY_TARGET == HWY_SVE2_128
  (void)d;
  return detail::ZipUpperSame(a, b);
#else
  // Move upper halves of blocks to lower half of vector.
  const Repartition<uint64_t, decltype(d)> d64;
  const auto a64 = BitCast(d64, a);
  const auto b64 = BitCast(d64, b);
  const auto a_blocks = detail::ConcatOddFull(a64, a64);  // lower half
  const auto b_blocks = detail::ConcatOddFull(b64, b64);
  return detail::ZipLowerSame(BitCast(d, a_blocks), BitCast(d, b_blocks));
#endif
}

// Capped/fraction: need runtime check
template <class D, class V = VFromD<D>,
          hwy::EnableIf<!detail::IsFull(D())>* = nullptr>
HWY_API V InterleaveUpper(D d, const V a, const V b) {
  // Less than one block: treat as capped
  if (Lanes(d) * sizeof(TFromD<D>) < 16) {
    const Half<decltype(d)> d2;
    return InterleaveLower(d, UpperHalf(d2, a), UpperHalf(d2, b));
  }
  return InterleaveUpper(DFromV<V>(), a, b);
}

// ------------------------------ InterleaveWholeLower
#ifdef HWY_NATIVE_INTERLEAVE_WHOLE
#undef HWY_NATIVE_INTERLEAVE_WHOLE
#else
#define HWY_NATIVE_INTERLEAVE_WHOLE
#endif

template <class D>
HWY_API VFromD<D> InterleaveWholeLower(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return detail::ZipLowerSame(a, b);
}

// ------------------------------ InterleaveWholeUpper

template <class D>
HWY_API VFromD<D> InterleaveWholeUpper(D d, VFromD<D> a, VFromD<D> b) {
  if (HWY_SVE_IS_POW2 && detail::IsFull(d)) {
    return detail::ZipUpperSame(a, b);
  }

  const Half<decltype(d)> d2;
  return InterleaveWholeLower(d, UpperHalf(d2, a), UpperHalf(d2, b));
}

// ------------------------------ Per4LaneBlockShuffle

namespace detail {

template <size_t kLaneSize, size_t kVectSize, class V,
          HWY_IF_NOT_T_SIZE_V(V, 8)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<0x88> /*idx_3210_tag*/,
                                  hwy::SizeTag<kLaneSize> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;

  const auto evens = BitCast(dw, ConcatEvenFull(v, v));
  return BitCast(d, ZipLowerSame(evens, evens));
}

template <size_t kLaneSize, size_t kVectSize, class V,
          HWY_IF_NOT_T_SIZE_V(V, 8)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<0xDD> /*idx_3210_tag*/,
                                  hwy::SizeTag<kLaneSize> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;

  const auto odds = BitCast(dw, ConcatOddFull(v, v));
  return BitCast(d, ZipLowerSame(odds, odds));
}

}  // namespace detail

// ================================================== COMBINE

namespace detail {

#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
template <class D, HWY_IF_T_SIZE_D(D, 1)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 32:
      return svptrue_pat_b8(SV_VL16);
    case 16:
      return svptrue_pat_b8(SV_VL8);
    case 8:
      return svptrue_pat_b8(SV_VL4);
    case 4:
      return svptrue_pat_b8(SV_VL2);
    default:
      return svptrue_pat_b8(SV_VL1);
  }
}
template <class D, HWY_IF_T_SIZE_D(D, 2)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 16:
      return svptrue_pat_b16(SV_VL8);
    case 8:
      return svptrue_pat_b16(SV_VL4);
    case 4:
      return svptrue_pat_b16(SV_VL2);
    default:
      return svptrue_pat_b16(SV_VL1);
  }
}
template <class D, HWY_IF_T_SIZE_D(D, 4)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 8:
      return svptrue_pat_b32(SV_VL4);
    case 4:
      return svptrue_pat_b32(SV_VL2);
    default:
      return svptrue_pat_b32(SV_VL1);
  }
}
template <class D, HWY_IF_T_SIZE_D(D, 8)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 4:
      return svptrue_pat_b64(SV_VL2);
    default:
      return svptrue_pat_b64(SV_VL1);
  }
}
#endif
#if HWY_TARGET == HWY_SVE2_128 || HWY_IDE
template <class D, HWY_IF_T_SIZE_D(D, 1)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 16:
      return svptrue_pat_b8(SV_VL8);
    case 8:
      return svptrue_pat_b8(SV_VL4);
    case 4:
      return svptrue_pat_b8(SV_VL2);
    case 2:
    case 1:
    default:
      return svptrue_pat_b8(SV_VL1);
  }
}
template <class D, HWY_IF_T_SIZE_D(D, 2)>
svbool_t MaskLowerHalf(D d) {
  switch (Lanes(d)) {
    case 8:
      return svptrue_pat_b16(SV_VL4);
    case 4:
      return svptrue_pat_b16(SV_VL2);
    case 2:
    case 1:
    default:
      return svptrue_pat_b16(SV_VL1);
  }
}
template <class D, HWY_IF_T_SIZE_D(D, 4)>
svbool_t MaskLowerHalf(D d) {
  return svptrue_pat_b32(Lanes(d) == 4 ? SV_VL2 : SV_VL1);
}
template <class D, HWY_IF_T_SIZE_D(D, 8)>
svbool_t MaskLowerHalf(D /*d*/) {
  return svptrue_pat_b64(SV_VL1);
}
#endif  // HWY_TARGET == HWY_SVE2_128
#if HWY_TARGET != HWY_SVE_256 && HWY_TARGET != HWY_SVE2_128
template <class D>
svbool_t MaskLowerHalf(D d) {
  return FirstN(d, Lanes(d) / 2);
}
#endif

template <class D>
svbool_t MaskUpperHalf(D d) {
  // TODO(janwas): WHILEGE on SVE2
  if (HWY_SVE_IS_POW2 && IsFull(d)) {
    return Not(MaskLowerHalf(d));
  }

  // For Splice to work as intended, make sure bits above Lanes(d) are zero.
  return AndNot(MaskLowerHalf(d), detail::MakeMask(d));
}

// Right-shift vector pair by constexpr; can be used to slide down (=N) or up
// (=Lanes()-N).
#define HWY_SVE_EXT(BASE, CHAR, BITS, HALF, NAME, OP)            \
  template <size_t kIndex>                                       \
  HWY_API HWY_SVE_V(BASE, BITS)                                  \
      NAME(HWY_SVE_V(BASE, BITS) hi, HWY_SVE_V(BASE, BITS) lo) { \
    return sv##OP##_##CHAR##BITS(lo, hi, kIndex);                \
  }
HWY_SVE_FOREACH(HWY_SVE_EXT, Ext, ext)
#undef HWY_SVE_EXT

}  // namespace detail

// ------------------------------ ConcatUpperLower
template <class D, class V>
HWY_API V ConcatUpperLower(const D d, const V hi, const V lo) {
  return IfThenElse(detail::MaskLowerHalf(d), lo, hi);
}

// ------------------------------ ConcatLowerLower
template <class D, class V>
HWY_API V ConcatLowerLower(const D d, const V hi, const V lo) {
  if (detail::IsFull(d)) {
#if defined(__ARM_FEATURE_SVE_MATMUL_FP64) && HWY_TARGET == HWY_SVE_256
    return detail::ConcatEvenBlocks(hi, lo);
#endif
#if HWY_TARGET == HWY_SVE2_128
    const Repartition<uint64_t, D> du64;
    const auto lo64 = BitCast(du64, lo);
    return BitCast(d, InterleaveLower(du64, lo64, BitCast(du64, hi)));
#endif
  }
  return detail::Splice(hi, lo, detail::MaskLowerHalf(d));
}

// ------------------------------ ConcatLowerUpper
template <class D, class V>
HWY_API V ConcatLowerUpper(const D d, const V hi, const V lo) {
#if HWY_TARGET == HWY_SVE_256 || HWY_TARGET == HWY_SVE2_128  // constexpr Lanes
  if (detail::IsFull(d)) {
    return detail::Ext<Lanes(d) / 2>(hi, lo);
  }
#endif
  return detail::Splice(hi, lo, detail::MaskUpperHalf(d));
}

// ------------------------------ ConcatUpperUpper
template <class D, class V>
HWY_API V ConcatUpperUpper(const D d, const V hi, const V lo) {
  if (detail::IsFull(d)) {
#if defined(__ARM_FEATURE_SVE_MATMUL_FP64) && HWY_TARGET == HWY_SVE_256
    return detail::ConcatOddBlocks(hi, lo);
#endif
#if HWY_TARGET == HWY_SVE2_128
    const Repartition<uint64_t, D> du64;
    const auto lo64 = BitCast(du64, lo);
    return BitCast(d, InterleaveUpper(du64, lo64, BitCast(du64, hi)));
#endif
  }
  const svbool_t mask_upper = detail::MaskUpperHalf(d);
  const V lo_upper = detail::Splice(lo, lo, mask_upper);
  return IfThenElse(mask_upper, hi, lo_upper);
}

// ------------------------------ Combine
template <class D, class V2>
HWY_API VFromD<D> Combine(const D d, const V2 hi, const V2 lo) {
  return ConcatLowerLower(d, hi, lo);
}

// ------------------------------ ZeroExtendVector
template <class D, class V>
HWY_API V ZeroExtendVector(const D d, const V lo) {
  return Combine(d, Zero(Half<D>()), lo);
}

// ------------------------------ Lower/UpperHalf

template <class D2, class V>
HWY_API V LowerHalf(D2 /* tag */, const V v) {
  return v;
}

template <class V>
HWY_API V LowerHalf(const V v) {
  return v;
}

template <class DH, class V>
HWY_API V UpperHalf(const DH dh, const V v) {
  const Twice<decltype(dh)> d;
  // Cast so that we support bfloat16_t.
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> vu = BitCast(du, v);
#if HWY_TARGET == HWY_SVE_256 || HWY_TARGET == HWY_SVE2_128  // constexpr Lanes
  return BitCast(d, detail::Ext<Lanes(dh)>(vu, vu));
#else
  const MFromD<decltype(du)> mask = detail::MaskUpperHalf(du);
  return BitCast(d, detail::Splice(vu, vu, mask));
#endif
}

// ================================================== REDUCE

#ifdef HWY_NATIVE_REDUCE_SCALAR
#undef HWY_NATIVE_REDUCE_SCALAR
#else
#define HWY_NATIVE_REDUCE_SCALAR
#endif

// These return T, suitable for ReduceSum.
namespace detail {
#define HWY_SVE_REDUCE_ADD(BASE, CHAR, BITS, HALF, NAME, OP)                   \
  HWY_API HWY_SVE_T(BASE, BITS) NAME(svbool_t pg, HWY_SVE_V(BASE, BITS) v) {   \
    /* The intrinsic returns [u]int64_t; truncate to T so we can broadcast. */ \
    using T = HWY_SVE_T(BASE, BITS);                                           \
    using TU = MakeUnsigned<T>;                                                \
    constexpr uint64_t kMask = LimitsMax<TU>();                                \
    return static_cast<T>(static_cast<TU>(                                     \
        static_cast<uint64_t>(sv##OP##_##CHAR##BITS(pg, v)) & kMask));         \
  }

#define HWY_SVE_REDUCE(BASE, CHAR, BITS, HALF, NAME, OP)                     \
  HWY_API HWY_SVE_T(BASE, BITS) NAME(svbool_t pg, HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_##CHAR##BITS(pg, v);                                     \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_REDUCE_ADD, SumOfLanesM, addv)
HWY_SVE_FOREACH_F(HWY_SVE_REDUCE, SumOfLanesM, addv)

HWY_SVE_FOREACH_UI(HWY_SVE_REDUCE, MinOfLanesM, minv)
HWY_SVE_FOREACH_UI(HWY_SVE_REDUCE, MaxOfLanesM, maxv)
// NaN if all are
HWY_SVE_FOREACH_F(HWY_SVE_REDUCE, MinOfLanesM, minnmv)
HWY_SVE_FOREACH_F(HWY_SVE_REDUCE, MaxOfLanesM, maxnmv)

#undef HWY_SVE_REDUCE
#undef HWY_SVE_REDUCE_ADD
}  // namespace detail

// detail::SumOfLanesM, detail::MinOfLanesM, and detail::MaxOfLanesM is more
// efficient for N=4 I8/U8 reductions on SVE than the default implementations
// of the N=4 I8/U8 ReduceSum/ReduceMin/ReduceMax operations in
// generic_ops-inl.h
#undef HWY_IF_REDUCE_D
#define HWY_IF_REDUCE_D(D) hwy::EnableIf<HWY_MAX_LANES_D(D) != 1>* = nullptr

#ifdef HWY_NATIVE_REDUCE_SUM_4_UI8
#undef HWY_NATIVE_REDUCE_SUM_4_UI8
#else
#define HWY_NATIVE_REDUCE_SUM_4_UI8
#endif

#ifdef HWY_NATIVE_REDUCE_MINMAX_4_UI8
#undef HWY_NATIVE_REDUCE_MINMAX_4_UI8
#else
#define HWY_NATIVE_REDUCE_MINMAX_4_UI8
#endif

template <class D, HWY_IF_REDUCE_D(D)>
HWY_API TFromD<D> ReduceSum(D d, VFromD<D> v) {
  return detail::SumOfLanesM(detail::MakeMask(d), v);
}

template <class D, HWY_IF_REDUCE_D(D)>
HWY_API TFromD<D> ReduceMin(D d, VFromD<D> v) {
  return detail::MinOfLanesM(detail::MakeMask(d), v);
}

template <class D, HWY_IF_REDUCE_D(D)>
HWY_API TFromD<D> ReduceMax(D d, VFromD<D> v) {
  return detail::MaxOfLanesM(detail::MakeMask(d), v);
}

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

// ================================================== SWIZZLE

// ------------------------------ GetLane

namespace detail {
#define HWY_SVE_GET_LANE(BASE, CHAR, BITS, HALF, NAME, OP) \
  HWY_INLINE HWY_SVE_T(BASE, BITS)                         \
      NAME(HWY_SVE_V(BASE, BITS) v, svbool_t mask) {       \
    return sv##OP##_##CHAR##BITS(mask, v);                 \
  }

HWY_SVE_FOREACH(HWY_SVE_GET_LANE, GetLaneM, lasta)
HWY_SVE_FOREACH(HWY_SVE_GET_LANE, ExtractLastMatchingLaneM, lastb)
#undef HWY_SVE_GET_LANE
}  // namespace detail

template <class V>
HWY_API TFromV<V> GetLane(V v) {
  return detail::GetLaneM(v, detail::PFalse());
}

// ------------------------------ ExtractLane
template <class V>
HWY_API TFromV<V> ExtractLane(V v, size_t i) {
  return detail::GetLaneM(v, FirstN(DFromV<V>(), i));
}

// ------------------------------ InsertLane (IfThenElse)
template <class V, typename T>
HWY_API V InsertLane(const V v, size_t i, T t) {
  static_assert(sizeof(TFromV<V>) == sizeof(T), "Lane size mismatch");
  const DFromV<V> d;
  const RebindToSigned<decltype(d)> di;
  using TI = TFromD<decltype(di)>;
  const svbool_t is_i = detail::EqN(Iota(di, 0), static_cast<TI>(i));
  return IfThenElse(RebindMask(d, is_i),
                    Set(d, hwy::ConvertScalarTo<TFromV<V>>(t)), v);
}

// ------------------------------ DupEven

namespace detail {
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGVV, InterleaveEven, trn1)
}  // namespace detail

template <class V>
HWY_API V DupEven(const V v) {
  return detail::InterleaveEven(v, v);
}

// ------------------------------ DupOdd

namespace detail {
HWY_SVE_FOREACH(HWY_SVE_RETV_ARGVV, InterleaveOdd, trn2)
}  // namespace detail

template <class V>
HWY_API V DupOdd(const V v) {
  return detail::InterleaveOdd(v, v);
}

// ------------------------------ OddEven

#if HWY_SVE_HAVE_2

#define HWY_SVE_ODD_EVEN(BASE, CHAR, BITS, HALF, NAME, OP)          \
  HWY_API HWY_SVE_V(BASE, BITS)                                     \
      NAME(HWY_SVE_V(BASE, BITS) odd, HWY_SVE_V(BASE, BITS) even) { \
    return sv##OP##_##CHAR##BITS(even, odd, /*xor=*/0);             \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_ODD_EVEN, OddEven, eortb_n)
#undef HWY_SVE_ODD_EVEN

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V OddEven(const V odd, const V even) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, OddEven(BitCast(du, odd), BitCast(du, even)));
}

#else

template <class V>
HWY_API V OddEven(const V odd, const V even) {
  const auto odd_in_even = detail::Ext<1>(odd, odd);
  return detail::InterleaveEven(even, odd_in_even);
}

#endif  // HWY_TARGET

// ------------------------------ InterleaveEven
template <class D>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return detail::InterleaveEven(a, b);
}

// ------------------------------ InterleaveOdd
template <class D>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return detail::InterleaveOdd(a, b);
}

// ------------------------------ OddEvenBlocks
template <class V>
HWY_API V OddEvenBlocks(const V odd, const V even) {
  const DFromV<V> d;
#if HWY_TARGET == HWY_SVE_256
  return ConcatUpperLower(d, odd, even);
#elif HWY_TARGET == HWY_SVE2_128
  (void)odd;
  (void)d;
  return even;
#else
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  constexpr size_t kShift = CeilLog2(16 / sizeof(TU));
  const auto idx_block = ShiftRight<kShift>(Iota(du, 0));
  const auto lsb = detail::AndN(idx_block, static_cast<TU>(1));
  const svbool_t is_even = detail::EqN(lsb, static_cast<TU>(0));
  return IfThenElse(is_even, even, odd);
#endif
}

// ------------------------------ TableLookupLanes

template <class D, class VI>
HWY_API VFromD<RebindToUnsigned<D>> IndicesFromVec(D d, VI vec) {
  using TI = TFromV<VI>;
  static_assert(sizeof(TFromD<D>) == sizeof(TI), "Index/lane size mismatch");
  const RebindToUnsigned<D> du;
  const auto indices = BitCast(du, vec);
#if HWY_IS_DEBUG_BUILD
  using TU = MakeUnsigned<TI>;
  const size_t twice_max_lanes = Lanes(d) * 2;
  HWY_DASSERT(AllTrue(
      du, Eq(indices,
             detail::AndN(indices, static_cast<TU>(twice_max_lanes - 1)))));
#else
  (void)d;
#endif
  return indices;
}

template <class D, typename TI>
HWY_API VFromD<RebindToUnsigned<D>> SetTableIndices(D d, const TI* idx) {
  static_assert(sizeof(TFromD<D>) == sizeof(TI), "Index size must match lane");
  return IndicesFromVec(d, LoadU(Rebind<TI, D>(), idx));
}

#define HWY_SVE_TABLE(BASE, CHAR, BITS, HALF, NAME, OP)          \
  HWY_API HWY_SVE_V(BASE, BITS)                                  \
      NAME(HWY_SVE_V(BASE, BITS) v, HWY_SVE_V(uint, BITS) idx) { \
    return sv##OP##_##CHAR##BITS(v, idx);                        \
  }

HWY_SVE_FOREACH(HWY_SVE_TABLE, TableLookupLanes, tbl)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_TABLE, TableLookupLanes, tbl)
#endif
#undef HWY_SVE_TABLE

#if HWY_SVE_HAVE_2
namespace detail {
#define HWY_SVE_TABLE2(BASE, CHAR, BITS, HALF, NAME, OP)                    \
  HWY_API HWY_SVE_V(BASE, BITS)                                             \
      NAME(HWY_SVE_TUPLE(BASE, BITS, 2) tuple, HWY_SVE_V(uint, BITS) idx) { \
    return sv##OP##_##CHAR##BITS(tuple, idx);                               \
  }

HWY_SVE_FOREACH(HWY_SVE_TABLE2, NativeTwoTableLookupLanes, tbl2)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_TABLE2, NativeTwoTableLookupLanes,
                                   tbl2)
#endif
#undef HWY_SVE_TABLE
}  // namespace detail
#endif  // HWY_SVE_HAVE_2

template <class D>
HWY_API VFromD<D> TwoTablesLookupLanes(D d, VFromD<D> a, VFromD<D> b,
                                       VFromD<RebindToUnsigned<D>> idx) {
  // SVE2 has an instruction for this, but it only works for full 2^n vectors.
#if HWY_SVE_HAVE_2 && HWY_SVE_IS_POW2
  if (detail::IsFull(d)) {
    return detail::NativeTwoTableLookupLanes(Create2(d, a, b), idx);
  }
#endif
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;

  const size_t num_of_lanes = Lanes(d);
  const auto idx_mod = detail::AndN(idx, static_cast<TU>(num_of_lanes - 1));
  const auto sel_a_mask = Eq(idx, idx_mod);

  const auto a_lookup_result = TableLookupLanes(a, idx_mod);
  const auto b_lookup_result = TableLookupLanes(b, idx_mod);
  return IfThenElse(sel_a_mask, a_lookup_result, b_lookup_result);
}

template <class V>
HWY_API V TwoTablesLookupLanes(V a, V b,
                               VFromD<RebindToUnsigned<DFromV<V>>> idx) {
  const DFromV<decltype(a)> d;
  return TwoTablesLookupLanes(d, a, b, idx);
}

// ------------------------------ SwapAdjacentBlocks (TableLookupLanes)

namespace detail {

template <typename T, size_t N, int kPow2>
constexpr size_t LanesPerBlock(Simd<T, N, kPow2> d) {
  // We might have a capped vector smaller than a block, so honor that.
  return HWY_MIN(16 / sizeof(T), MaxLanes(d));
}

}  // namespace detail

template <class V>
HWY_API V SwapAdjacentBlocks(const V v) {
  const DFromV<V> d;
#if HWY_TARGET == HWY_SVE_256
  return ConcatLowerUpper(d, v, v);
#elif HWY_TARGET == HWY_SVE2_128
  (void)d;
  return v;
#else
  const RebindToUnsigned<decltype(d)> du;
  constexpr auto kLanesPerBlock =
      static_cast<TFromD<decltype(du)>>(detail::LanesPerBlock(d));
  const VFromD<decltype(du)> idx = detail::XorN(Iota(du, 0), kLanesPerBlock);
  return TableLookupLanes(v, idx);
#endif
}

// ------------------------------ Reverse

namespace detail {

#define HWY_SVE_REVERSE(BASE, CHAR, BITS, HALF, NAME, OP)       \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_##CHAR##BITS(v);                            \
  }

HWY_SVE_FOREACH(HWY_SVE_REVERSE, ReverseFull, rev)
#if HWY_SVE_HAVE_BF16_FEATURE || HWY_SVE_HAVE_BF16_VEC
HWY_SVE_FOREACH_BF16_UNCONDITIONAL(HWY_SVE_REVERSE, ReverseFull, rev)
#endif
#undef HWY_SVE_REVERSE

}  // namespace detail

template <class D, class V>
HWY_API V Reverse(D d, V v) {
  using T = TFromD<D>;
  const auto reversed = detail::ReverseFull(v);
  if (HWY_SVE_IS_POW2 && detail::IsFull(d)) return reversed;
  // Shift right to remove extra (non-pow2 and remainder) lanes.
  // TODO(janwas): on SVE2, use WHILEGE.
  // Avoids FirstN truncating to the return vector size. Must also avoid Not
  // because that is limited to SV_POW2.
  const ScalableTag<T> dfull;
  const svbool_t all_true = detail::AllPTrue(dfull);
  const size_t all_lanes = detail::AllHardwareLanes<T>();
  const size_t want_lanes = Lanes(d);
  HWY_DASSERT(want_lanes <= all_lanes);
  const svbool_t mask =
      svnot_b_z(all_true, FirstN(dfull, all_lanes - want_lanes));
  return detail::Splice(reversed, reversed, mask);
}

// ------------------------------ Reverse2

// Per-target flag to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;
  return BitCast(d, svrevb_u16_x(detail::PTrue(d), BitCast(dw, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;
  return BitCast(d, svrevh_u32_x(detail::PTrue(d), BitCast(dw, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(du)> dw;
  return BitCast(d, svrevw_u64_x(detail::PTrue(d), BitCast(dw, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {  // 3210
#if HWY_TARGET == HWY_SVE2_128
  if (detail::IsFull(d)) {
    return detail::Ext<1>(v, v);
  }
#endif
  (void)d;
  const auto odd_in_even = detail::Ext<1>(v, v);  // x321
  return detail::InterleaveEven(odd_in_even, v);  // 2301
}

// ------------------------------ Reverse4 (TableLookupLanes)

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWideX2<decltype(du)> du32;
  return BitCast(d, svrevb_u32_x(detail::PTrue(d), BitCast(du32, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWideX2<decltype(du)> du64;
  return BitCast(d, svrevh_u64_x(detail::PTrue(d), BitCast(du64, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  if (HWY_TARGET == HWY_SVE2_128 && detail::IsFull(d)) {
    return detail::ReverseFull(v);
  }
  // TODO(janwas): is this approach faster than Shuffle0123?
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = detail::XorN(Iota(du, 0), 3);
  return TableLookupLanes(v, idx);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse4(D d, const VFromD<D> v) {
  if (HWY_TARGET == HWY_SVE_256 && detail::IsFull(d)) {
    return detail::ReverseFull(v);
  }
  // TODO(janwas): is this approach faster than Shuffle0123?
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = detail::XorN(Iota(du, 0), 3);
  return TableLookupLanes(v, idx);
}

// ------------------------------ Reverse8 (TableLookupLanes)

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
  const Repartition<uint64_t, decltype(d)> du64;
  return BitCast(d, svrevb_u64_x(detail::PTrue(d), BitCast(du64, v)));
}

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  const auto idx = detail::XorN(Iota(du, 0), 7);
  return TableLookupLanes(v, idx);
}

// ------------------------------- ReverseBits

#ifdef HWY_NATIVE_REVERSE_BITS_UI8
#undef HWY_NATIVE_REVERSE_BITS_UI8
#else
#define HWY_NATIVE_REVERSE_BITS_UI8
#endif

#ifdef HWY_NATIVE_REVERSE_BITS_UI16_32_64
#undef HWY_NATIVE_REVERSE_BITS_UI16_32_64
#else
#define HWY_NATIVE_REVERSE_BITS_UI16_32_64
#endif

#define HWY_SVE_REVERSE_BITS(BASE, CHAR, BITS, HALF, NAME, OP)  \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) { \
    const DFromV<decltype(v)> d;                                \
    return sv##OP##_##CHAR##BITS##_x(detail::PTrue(d), v);      \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_REVERSE_BITS, ReverseBits, rbit)
#undef HWY_SVE_REVERSE_BITS

// ------------------------------ SlideUpLanes

template <class D>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
  return detail::Splice(v, Zero(d), FirstN(d, amt));
}

// ------------------------------ Slide1Up

#ifdef HWY_NATIVE_SLIDE1_UP_DOWN
#undef HWY_NATIVE_SLIDE1_UP_DOWN
#else
#define HWY_NATIVE_SLIDE1_UP_DOWN
#endif

template <class D>
HWY_API VFromD<D> Slide1Up(D d, VFromD<D> v) {
  return SlideUpLanes(d, v, 1);
}

// ------------------------------ SlideDownLanes (TableLookupLanes)

template <class D>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  const auto idx = Iota(du, static_cast<TU>(amt));
  return IfThenElseZero(FirstN(d, Lanes(d) - amt), TableLookupLanes(v, idx));
}

// ------------------------------ Slide1Down

template <class D>
HWY_API VFromD<D> Slide1Down(D d, VFromD<D> v) {
  return SlideDownLanes(d, v, 1);
}

// ------------------------------ Block insert/extract/broadcast ops
#if HWY_TARGET != HWY_SVE2_128

#ifdef HWY_NATIVE_BLK_INSERT_EXTRACT
#undef HWY_NATIVE_BLK_INSERT_EXTRACT
#else
#define HWY_NATIVE_BLK_INSERT_EXTRACT
#endif

template <int kBlockIdx, class V>
HWY_API V InsertBlock(V v, V blk_to_insert) {
  const DFromV<decltype(v)> d;
  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");

#if HWY_TARGET == HWY_SVE_256
  return (kBlockIdx == 0) ? ConcatUpperLower(d, v, blk_to_insert)
                          : ConcatLowerLower(d, blk_to_insert, v);
#else
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(d);

  constexpr size_t kBlockOffset =
      static_cast<size_t>(kBlockIdx) * kLanesPerBlock;
  const auto splice_mask = FirstN(d, kBlockOffset);
  const auto sel_lo_mask = FirstN(d, kBlockOffset + kLanesPerBlock);

  const auto splice_result = detail::Splice(blk_to_insert, v, splice_mask);
  return IfThenElse(sel_lo_mask, splice_result, v);
#endif
}

template <int kBlockIdx, class V>
HWY_API V ExtractBlock(V v) {
  const DFromV<decltype(v)> d;
  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");

  if (kBlockIdx == 0) return v;

#if HWY_TARGET == HWY_SVE_256
  return UpperHalf(Half<decltype(d)>(), v);
#else
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(d);
  constexpr size_t kBlockOffset =
      static_cast<size_t>(kBlockIdx) * kLanesPerBlock;
  const auto splice_mask =
      RebindMask(d, detail::LtN(Iota(du, static_cast<TU>(0u - kBlockOffset)),
                                static_cast<TU>(kLanesPerBlock)));
  return detail::Splice(v, v, splice_mask);
#endif
}

template <int kBlockIdx, class V>
HWY_API V BroadcastBlock(V v) {
  const DFromV<decltype(v)> d;
  static_assert(0 <= kBlockIdx && kBlockIdx < d.MaxBlocks(),
                "Invalid block index");

  const RebindToUnsigned<decltype(d)> du;  // for bfloat16_t
  using VU = VFromD<decltype(du)>;
  const VU vu = BitCast(du, v);

#if HWY_TARGET == HWY_SVE_256
  return BitCast(d, (kBlockIdx == 0) ? ConcatLowerLower(du, vu, vu)
                                     : ConcatUpperUpper(du, vu, vu));
#else
  using TU = TFromD<decltype(du)>;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(d);
  constexpr size_t kBlockOffset =
      static_cast<size_t>(kBlockIdx) * kLanesPerBlock;

  const VU idx = detail::AddN(
      detail::AndN(Iota(du, TU{0}), static_cast<TU>(kLanesPerBlock - 1)),
      static_cast<TU>(kBlockOffset));
  return BitCast(d, TableLookupLanes(vu, idx));
#endif
}

#endif  // HWY_TARGET != HWY_SVE2_128

// ------------------------------ Compress (PromoteTo)

template <typename T>
struct CompressIsPartition {
#if HWY_TARGET == HWY_SVE_256 || HWY_TARGET == HWY_SVE2_128
  // Optimization for 64-bit lanes (could also be applied to 32-bit, but that
  // requires a larger table).
  enum { value = (sizeof(T) == 8) };
#else
  enum { value = 0 };
#endif  // HWY_TARGET == HWY_SVE_256
};

#define HWY_SVE_COMPRESS(BASE, CHAR, BITS, HALF, NAME, OP)                     \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v, svbool_t mask) { \
    return sv##OP##_##CHAR##BITS(mask, v);                                     \
  }

#if HWY_TARGET == HWY_SVE_256 || HWY_TARGET == HWY_SVE2_128
HWY_SVE_FOREACH_UI32(HWY_SVE_COMPRESS, Compress, compact)
HWY_SVE_FOREACH_F32(HWY_SVE_COMPRESS, Compress, compact)
#else
HWY_SVE_FOREACH_UIF3264(HWY_SVE_COMPRESS, Compress, compact)
#endif
#undef HWY_SVE_COMPRESS

#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_API V Compress(V v, svbool_t mask) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du64;

  // Convert mask into bitfield via horizontal sum (faster than ORV) of masked
  // bits 1, 2, 4, 8. Pre-multiply by N so we can use it as an offset for
  // SetTableIndices.
  const svuint64_t bits = Shl(Set(du64, 1), Iota(du64, 2));
  const size_t offset = detail::SumOfLanesM(mask, bits);

  // See CompressIsPartition.
  alignas(16) static constexpr uint64_t table[4 * 16] = {
      // PrintCompress64x4Tables
      0, 1, 2, 3, 0, 1, 2, 3, 1, 0, 2, 3, 0, 1, 2, 3, 2, 0, 1, 3, 0, 2,
      1, 3, 1, 2, 0, 3, 0, 1, 2, 3, 3, 0, 1, 2, 0, 3, 1, 2, 1, 3, 0, 2,
      0, 1, 3, 2, 2, 3, 0, 1, 0, 2, 3, 1, 1, 2, 3, 0, 0, 1, 2, 3};
  return TableLookupLanes(v, SetTableIndices(d, table + offset));
}

#endif  // HWY_TARGET == HWY_SVE_256
#if HWY_TARGET == HWY_SVE2_128 || HWY_IDE
template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_API V Compress(V v, svbool_t mask) {
  // If mask == 10: swap via splice. A mask of 00 or 11 leaves v unchanged, 10
  // swaps upper/lower (the lower half is set to the upper half, and the
  // remaining upper half is filled from the lower half of the second v), and
  // 01 is invalid because it would ConcatLowerLower. zip1 and AndNot keep 10
  // unchanged and map everything else to 00.
  const svbool_t maskLL = svzip1_b64(mask, mask);  // broadcast lower lane
  return detail::Splice(v, v, AndNot(maskLL, mask));
}

#endif  // HWY_TARGET == HWY_SVE2_128

template <class V, HWY_IF_T_SIZE_V(V, 2)>
HWY_API V Compress(V v, svbool_t mask16) {
  static_assert(!IsSame<V, svfloat16_t>(), "Must use overload");
  const DFromV<V> d16;

  // Promote vector and mask to 32-bit
  const RepartitionToWide<decltype(d16)> dw;
  const auto v32L = PromoteTo(dw, v);
  const auto v32H = detail::PromoteUpperTo(dw, v);
  const svbool_t mask32L = svunpklo_b(mask16);
  const svbool_t mask32H = svunpkhi_b(mask16);

  const auto compressedL = Compress(v32L, mask32L);
  const auto compressedH = Compress(v32H, mask32H);

  // Demote to 16-bit (already in range) - separately so we can splice
  const V evenL = BitCast(d16, compressedL);
  const V evenH = BitCast(d16, compressedH);
  const V v16L = detail::ConcatEvenFull(evenL, evenL);  // lower half
  const V v16H = detail::ConcatEvenFull(evenH, evenH);

  // We need to combine two vectors of non-constexpr length, so the only option
  // is Splice, which requires us to synthesize a mask. NOTE: this function uses
  // full vectors (SV_ALL instead of SV_POW2), hence we need unmasked svcnt.
  const size_t countL = detail::CountTrueFull(dw, mask32L);
  const auto compressed_maskL = FirstN(d16, countL);
  return detail::Splice(v16H, v16L, compressed_maskL);
}

// Must treat float16_t as integers so we can ConcatEven.
HWY_API svfloat16_t Compress(svfloat16_t v, svbool_t mask16) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;
  return BitCast(df, Compress(BitCast(di, v), mask16));
}

// ------------------------------ CompressNot

// 2 or 4 bytes
template <class V, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 2) | (1 << 4))>
HWY_API V CompressNot(V v, const svbool_t mask) {
  return Compress(v, Not(mask));
}

template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_API V CompressNot(V v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE2_128 || HWY_IDE
  // If mask == 01: swap via splice. A mask of 00 or 11 leaves v unchanged, 10
  // swaps upper/lower (the lower half is set to the upper half, and the
  // remaining upper half is filled from the lower half of the second v), and
  // 01 is invalid because it would ConcatLowerLower. zip1 and AndNot map
  // 01 to 10, and everything else to 00.
  const svbool_t maskLL = svzip1_b64(mask, mask);  // broadcast lower lane
  return detail::Splice(v, v, AndNot(mask, maskLL));
#endif
#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du64;

  // Convert mask into bitfield via horizontal sum (faster than ORV) of masked
  // bits 1, 2, 4, 8. Pre-multiply by N so we can use it as an offset for
  // SetTableIndices.
  const svuint64_t bits = Shl(Set(du64, 1), Iota(du64, 2));
  const size_t offset = detail::SumOfLanesM(mask, bits);

  // See CompressIsPartition.
  alignas(16) static constexpr uint64_t table[4 * 16] = {
      // PrintCompressNot64x4Tables
      0, 1, 2, 3, 1, 2, 3, 0, 0, 2, 3, 1, 2, 3, 0, 1, 0, 1, 3, 2, 1, 3,
      0, 2, 0, 3, 1, 2, 3, 0, 1, 2, 0, 1, 2, 3, 1, 2, 0, 3, 0, 2, 1, 3,
      2, 0, 1, 3, 0, 1, 2, 3, 1, 0, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};
  return TableLookupLanes(v, SetTableIndices(d, table + offset));
#endif  // HWY_TARGET == HWY_SVE_256

  return Compress(v, Not(mask));
}

// ------------------------------ CompressBlocksNot
HWY_API svuint64_t CompressBlocksNot(svuint64_t v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE2_128
  (void)mask;
  return v;
#endif
#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
  uint64_t bits = 0;           // predicate reg is 32-bit
  CopyBytes<4>(&mask, &bits);  // not same size - 64-bit more efficient
  // Concatenate LSB for upper and lower blocks, pre-scale by 4 for table idx.
  const size_t offset = ((bits & 1) ? 4u : 0u) + ((bits & 0x10000) ? 8u : 0u);
  // See CompressIsPartition. Manually generated; flip halves if mask = [0, 1].
  alignas(16) static constexpr uint64_t table[4 * 4] = {0, 1, 2, 3, 2, 3, 0, 1,
                                                        0, 1, 2, 3, 0, 1, 2, 3};
  const ScalableTag<uint64_t> d;
  return TableLookupLanes(v, SetTableIndices(d, table + offset));
#endif

  return CompressNot(v, mask);
}

// ------------------------------ CompressStore
template <class V, class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(const V v, const svbool_t mask, const D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  StoreU(Compress(v, mask), d, unaligned);
  return CountTrue(d, mask);
}

// ------------------------------ CompressBlendedStore
template <class V, class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(const V v, const svbool_t mask, const D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const size_t count = CountTrue(d, mask);
  const svbool_t store_mask = FirstN(d, count);
  BlendedStore(Compress(v, mask), store_mask, d, unaligned);
  return count;
}

// ================================================== MASK (2)

// ------------------------------ FindKnownLastTrue
template <class D>
HWY_API size_t FindKnownLastTrue(D d, svbool_t m) {
  const RebindToUnsigned<decltype(d)> du;
  return static_cast<size_t>(detail::ExtractLastMatchingLaneM(
      Iota(du, 0), And(m, detail::MakeMask(d))));
}

// ------------------------------ FindLastTrue
template <class D>
HWY_API intptr_t FindLastTrue(D d, svbool_t m) {
  return AllFalse(d, m) ? intptr_t{-1}
                        : static_cast<intptr_t>(FindKnownLastTrue(d, m));
}

// ================================================== BLOCKWISE

// ------------------------------ CombineShiftRightBytes

// Prevent accidentally using these for 128-bit vectors - should not be
// necessary.
#if HWY_TARGET != HWY_SVE2_128
namespace detail {

// For x86-compatible behaviour mandated by Highway API: TableLookupBytes
// offsets are implicitly relative to the start of their 128-bit block.
template <class D, class V>
HWY_INLINE V OffsetsOf128BitBlocks(const D d, const V iota0) {
  using T = MakeUnsigned<TFromD<D>>;
  return detail::AndNotN(static_cast<T>(LanesPerBlock(d) - 1), iota0);
}

template <size_t kLanes, class D, HWY_IF_T_SIZE_D(D, 1)>
svbool_t FirstNPerBlock(D d) {
  const RebindToUnsigned<decltype(d)> du;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(du);
  const svuint8_t idx_mod =
      svdupq_n_u8(0 % kLanesPerBlock, 1 % kLanesPerBlock, 2 % kLanesPerBlock,
                  3 % kLanesPerBlock, 4 % kLanesPerBlock, 5 % kLanesPerBlock,
                  6 % kLanesPerBlock, 7 % kLanesPerBlock, 8 % kLanesPerBlock,
                  9 % kLanesPerBlock, 10 % kLanesPerBlock, 11 % kLanesPerBlock,
                  12 % kLanesPerBlock, 13 % kLanesPerBlock, 14 % kLanesPerBlock,
                  15 % kLanesPerBlock);
  return detail::LtN(BitCast(du, idx_mod), kLanes);
}
template <size_t kLanes, class D, HWY_IF_T_SIZE_D(D, 2)>
svbool_t FirstNPerBlock(D d) {
  const RebindToUnsigned<decltype(d)> du;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(du);
  const svuint16_t idx_mod =
      svdupq_n_u16(0 % kLanesPerBlock, 1 % kLanesPerBlock, 2 % kLanesPerBlock,
                   3 % kLanesPerBlock, 4 % kLanesPerBlock, 5 % kLanesPerBlock,
                   6 % kLanesPerBlock, 7 % kLanesPerBlock);
  return detail::LtN(BitCast(du, idx_mod), kLanes);
}
template <size_t kLanes, class D, HWY_IF_T_SIZE_D(D, 4)>
svbool_t FirstNPerBlock(D d) {
  const RebindToUnsigned<decltype(d)> du;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(du);
  const svuint32_t idx_mod =
      svdupq_n_u32(0 % kLanesPerBlock, 1 % kLanesPerBlock, 2 % kLanesPerBlock,
                   3 % kLanesPerBlock);
  return detail::LtN(BitCast(du, idx_mod), kLanes);
}
template <size_t kLanes, class D, HWY_IF_T_SIZE_D(D, 8)>
svbool_t FirstNPerBlock(D d) {
  const RebindToUnsigned<decltype(d)> du;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(du);
  const svuint64_t idx_mod =
      svdupq_n_u64(0 % kLanesPerBlock, 1 % kLanesPerBlock);
  return detail::LtN(BitCast(du, idx_mod), kLanes);
}

}  // namespace detail
#endif  // HWY_TARGET != HWY_SVE2_128

template <size_t kBytes, class D, class V = VFromD<D>>
HWY_API V CombineShiftRightBytes(const D d, const V hi, const V lo) {
  const Repartition<uint8_t, decltype(d)> d8;
  const auto hi8 = BitCast(d8, hi);
  const auto lo8 = BitCast(d8, lo);
#if HWY_TARGET == HWY_SVE2_128
  return BitCast(d, detail::Ext<kBytes>(hi8, lo8));
#else
  const auto hi_up = detail::Splice(hi8, hi8, FirstN(d8, 16 - kBytes));
  const auto lo_down = detail::Ext<kBytes>(lo8, lo8);
  const svbool_t is_lo = detail::FirstNPerBlock<16 - kBytes>(d8);
  return BitCast(d, IfThenElse(is_lo, lo_down, hi_up));
#endif
}

// ------------------------------ Shuffle2301
template <class V>
HWY_API V Shuffle2301(const V v) {
  const DFromV<V> d;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  return Reverse2(d, v);
}

// ------------------------------ Shuffle2103
template <class V>
HWY_API V Shuffle2103(const V v) {
  const DFromV<V> d;
  const Repartition<uint8_t, decltype(d)> d8;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  const svuint8_t v8 = BitCast(d8, v);
  return BitCast(d, CombineShiftRightBytes<12>(d8, v8, v8));
}

// ------------------------------ Shuffle0321
template <class V>
HWY_API V Shuffle0321(const V v) {
  const DFromV<V> d;
  const Repartition<uint8_t, decltype(d)> d8;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  const svuint8_t v8 = BitCast(d8, v);
  return BitCast(d, CombineShiftRightBytes<4>(d8, v8, v8));
}

// ------------------------------ Shuffle1032
template <class V>
HWY_API V Shuffle1032(const V v) {
  const DFromV<V> d;
  const Repartition<uint8_t, decltype(d)> d8;
  static_assert(sizeof(TFromD<decltype(d)>) == 4, "Defined for 32-bit types");
  const svuint8_t v8 = BitCast(d8, v);
  return BitCast(d, CombineShiftRightBytes<8>(d8, v8, v8));
}

// ------------------------------ Shuffle01
template <class V>
HWY_API V Shuffle01(const V v) {
  const DFromV<V> d;
  const Repartition<uint8_t, decltype(d)> d8;
  static_assert(sizeof(TFromD<decltype(d)>) == 8, "Defined for 64-bit types");
  const svuint8_t v8 = BitCast(d8, v);
  return BitCast(d, CombineShiftRightBytes<8>(d8, v8, v8));
}

// ------------------------------ Shuffle0123
template <class V>
HWY_API V Shuffle0123(const V v) {
  return Shuffle2301(Shuffle1032(v));
}

// ------------------------------ ReverseBlocks (Reverse, Shuffle01)
template <class D, class V = VFromD<D>>
HWY_API V ReverseBlocks(D d, V v) {
#if HWY_TARGET == HWY_SVE_256
  if (detail::IsFull(d)) {
    return SwapAdjacentBlocks(v);
  } else if (detail::IsFull(Twice<D>())) {
    return v;
  }
#elif HWY_TARGET == HWY_SVE2_128
  (void)d;
  return v;
#endif
  const Repartition<uint64_t, D> du64;
  return BitCast(d, Shuffle01(Reverse(du64, BitCast(du64, v))));
}

// ------------------------------ TableLookupBytes

template <class V, class VI>
HWY_API VI TableLookupBytes(const V v, const VI idx) {
  const DFromV<VI> d;
  const Repartition<uint8_t, decltype(d)> du8;
#if HWY_TARGET == HWY_SVE2_128
  return BitCast(d, TableLookupLanes(BitCast(du8, v), BitCast(du8, idx)));
#else
  const auto offsets128 = detail::OffsetsOf128BitBlocks(du8, Iota(du8, 0));
  const auto idx8 = Add(BitCast(du8, idx), offsets128);
  return BitCast(d, TableLookupLanes(BitCast(du8, v), idx8));
#endif
}

template <class V, class VI>
HWY_API VI TableLookupBytesOr0(const V v, const VI idx) {
  const DFromV<VI> d;
  // Mask size must match vector type, so cast everything to this type.
  const Repartition<int8_t, decltype(d)> di8;

  auto idx8 = BitCast(di8, idx);
  const auto msb = detail::LtN(idx8, 0);

  const auto lookup = TableLookupBytes(BitCast(di8, v), idx8);
  return BitCast(d, IfThenZeroElse(msb, lookup));
}

// ------------------------------ Broadcast

#ifdef HWY_NATIVE_BROADCASTLANE
#undef HWY_NATIVE_BROADCASTLANE
#else
#define HWY_NATIVE_BROADCASTLANE
#endif

namespace detail {
#define HWY_SVE_BROADCAST(BASE, CHAR, BITS, HALF, NAME, OP)        \
  template <int kLane>                                             \
  HWY_INLINE HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) { \
    return sv##OP##_##CHAR##BITS(v, kLane);                        \
  }

HWY_SVE_FOREACH(HWY_SVE_BROADCAST, BroadcastLane, dup_lane)
#undef HWY_SVE_BROADCAST
}  // namespace detail

template <int kLane, class V>
HWY_API V Broadcast(const V v) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(du);
  static_assert(0 <= kLane && kLane < kLanesPerBlock, "Invalid lane");
#if HWY_TARGET == HWY_SVE2_128
  return detail::BroadcastLane<kLane>(v);
#else
  auto idx = detail::OffsetsOf128BitBlocks(du, Iota(du, 0));
  if (kLane != 0) {
    idx = detail::AddN(idx, kLane);
  }
  return TableLookupLanes(v, idx);
#endif
}

template <int kLane, class V>
HWY_API V BroadcastLane(const V v) {
  static_assert(0 <= kLane && kLane < HWY_MAX_LANES_V(V), "Invalid lane");
  return detail::BroadcastLane<kLane>(v);
}

// ------------------------------ ShiftLeftLanes

template <size_t kLanes, class D, class V = VFromD<D>>
HWY_API V ShiftLeftLanes(D d, const V v) {
  const auto zero = Zero(d);
  const auto shifted = detail::Splice(v, zero, FirstN(d, kLanes));
#if HWY_TARGET == HWY_SVE2_128
  return shifted;
#else
  // Match x86 semantics by zeroing lower lanes in 128-bit blocks
  return IfThenElse(detail::FirstNPerBlock<kLanes>(d), zero, shifted);
#endif
}

template <size_t kLanes, class V>
HWY_API V ShiftLeftLanes(const V v) {
  return ShiftLeftLanes<kLanes>(DFromV<V>(), v);
}

// ------------------------------ ShiftRightLanes
template <size_t kLanes, class D, class V = VFromD<D>>
HWY_API V ShiftRightLanes(D d, V v) {
  // For capped/fractional vectors, clear upper lanes so we shift in zeros.
  if (!detail::IsFull(d)) {
    v = IfThenElseZero(detail::MakeMask(d), v);
  }

#if HWY_TARGET == HWY_SVE2_128
  return detail::Ext<kLanes>(Zero(d), v);
#else
  const auto shifted = detail::Ext<kLanes>(v, v);
  // Match x86 semantics by zeroing upper lanes in 128-bit blocks
  constexpr size_t kLanesPerBlock = detail::LanesPerBlock(d);
  const svbool_t mask = detail::FirstNPerBlock<kLanesPerBlock - kLanes>(d);
  return IfThenElseZero(mask, shifted);
#endif
}

// ------------------------------ ShiftLeftBytes

template <int kBytes, class D, class V = VFromD<D>>
HWY_API V ShiftLeftBytes(const D d, const V v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftLeftLanes<kBytes>(BitCast(d8, v)));
}

template <int kBytes, class V>
HWY_API V ShiftLeftBytes(const V v) {
  return ShiftLeftBytes<kBytes>(DFromV<V>(), v);
}

// ------------------------------ ShiftRightBytes
template <int kBytes, class D, class V = VFromD<D>>
HWY_API V ShiftRightBytes(const D d, const V v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftRightLanes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ ZipLower

template <class V, class DW = RepartitionToWide<DFromV<V>>>
HWY_API VFromD<DW> ZipLower(DW dw, V a, V b) {
  const RepartitionToNarrow<DW> dn;
  static_assert(IsSame<TFromD<decltype(dn)>, TFromV<V>>(), "D/V mismatch");
  return BitCast(dw, InterleaveLower(dn, a, b));
}
template <class V, class D = DFromV<V>, class DW = RepartitionToWide<D>>
HWY_API VFromD<DW> ZipLower(const V a, const V b) {
  return BitCast(DW(), InterleaveLower(D(), a, b));
}

// ------------------------------ ZipUpper
template <class V, class DW = RepartitionToWide<DFromV<V>>>
HWY_API VFromD<DW> ZipUpper(DW dw, V a, V b) {
  const RepartitionToNarrow<DW> dn;
  static_assert(IsSame<TFromD<decltype(dn)>, TFromV<V>>(), "D/V mismatch");
  return BitCast(dw, InterleaveUpper(dn, a, b));
}

// ================================================== Ops with dependencies

// ------------------------------ AddSub (Reverse2)

// NOTE: svcadd_f*_x(HWY_SVE_PTRUE(BITS), a, b, 90) computes a[i] - b[i + 1] in
// the even lanes and a[i] + b[i - 1] in the odd lanes.

#define HWY_SVE_ADDSUB_F(BASE, CHAR, BITS, HALF, NAME, OP)                   \
  HWY_API HWY_SVE_V(BASE, BITS)                                              \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) {               \
    const DFromV<decltype(b)> d;                                             \
    return sv##OP##_##CHAR##BITS##_x(HWY_SVE_PTRUE(BITS), a, Reverse2(d, b), \
                                     90);                                    \
  }

HWY_SVE_FOREACH_F(HWY_SVE_ADDSUB_F, AddSub, cadd)

#undef HWY_SVE_ADDSUB_F

// NOTE: svcadd_s*(a, b, 90) and svcadd_u*(a, b, 90) compute a[i] - b[i + 1] in
// the even lanes and a[i] + b[i - 1] in the odd lanes.

#if HWY_SVE_HAVE_2
#define HWY_SVE_ADDSUB_UI(BASE, CHAR, BITS, HALF, NAME, OP)    \
  HWY_API HWY_SVE_V(BASE, BITS)                                \
      NAME(HWY_SVE_V(BASE, BITS) a, HWY_SVE_V(BASE, BITS) b) { \
    const DFromV<decltype(b)> d;                               \
    return sv##OP##_##CHAR##BITS(a, Reverse2(d, b), 90);       \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_ADDSUB_UI, AddSub, cadd)

#undef HWY_SVE_ADDSUB_UI

// Disable the default implementation of AddSub in generic_ops-inl.h on SVE2
#undef HWY_IF_ADDSUB_V
#define HWY_IF_ADDSUB_V(V)         \
  HWY_IF_LANES_GT_D(DFromV<V>, 1), \
      hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr

#else  // !HWY_SVE_HAVE_2

// Disable the default implementation of AddSub in generic_ops-inl.h for
// floating-point vectors on SVE, but enable the default implementation of
// AddSub in generic_ops-inl.h for integer vectors on SVE that do not support
// SVE2
#undef HWY_IF_ADDSUB_V
#define HWY_IF_ADDSUB_V(V) \
  HWY_IF_LANES_GT_D(DFromV<V>, 1), HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)

#endif  // HWY_SVE_HAVE_2

// ------------------------------ MulAddSub (AddSub)

template <class V, HWY_IF_LANES_GT_D(DFromV<V>, 1), HWY_IF_FLOAT_V(V)>
HWY_API V MulAddSub(V mul, V x, V sub_or_add) {
  using T = TFromV<V>;

  const DFromV<V> d;
  const T neg_zero = ConvertScalarTo<T>(-0.0f);

  return MulAdd(mul, x, AddSub(Set(d, neg_zero), sub_or_add));
}

#if HWY_SVE_HAVE_2

// Disable the default implementation of MulAddSub in generic_ops-inl.h on SVE2
#undef HWY_IF_MULADDSUB_V
#define HWY_IF_MULADDSUB_V(V)      \
  HWY_IF_LANES_GT_D(DFromV<V>, 1), \
      hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr

template <class V, HWY_IF_LANES_GT_D(DFromV<V>, 1),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V MulAddSub(V mul, V x, V sub_or_add) {
  const DFromV<V> d;
  return MulAdd(mul, x, AddSub(Zero(d), sub_or_add));
}

#else  // !HWY_SVE_HAVE_2

// Disable the default implementation of MulAddSub in generic_ops-inl.h for
// floating-point vectors on SVE, but enable the default implementation of
// AddSub in generic_ops-inl.h for integer vectors on SVE targets that do not
// support SVE2
#undef HWY_IF_MULADDSUB_V
#define HWY_IF_MULADDSUB_V(V) \
  HWY_IF_LANES_GT_D(DFromV<V>, 1), HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)

#endif  // HWY_SVE_HAVE_2

// ------------------------------ PromoteTo bfloat16 (ZipLower)
template <size_t N, int kPow2>
HWY_API svfloat32_t PromoteTo(Simd<float32_t, N, kPow2> df32, VBF16 v) {
  const ScalableTag<uint16_t> du16;
  return BitCast(df32, detail::ZipLowerSame(svdup_n_u16(0), BitCast(du16, v)));
}

// ------------------------------ PromoteEvenTo/PromoteOddTo (ConcatOddFull)

namespace detail {

// Signed to signed PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<2> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   svint8_t v) {
  return svextb_s16_x(detail::PTrue(d_to), BitCast(d_to, v));
}

template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<4> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   svint16_t v) {
  return svexth_s32_x(detail::PTrue(d_to), BitCast(d_to, v));
}

template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   svint32_t v) {
  return svextw_s64_x(detail::PTrue(d_to), BitCast(d_to, v));
}

// F16->F32 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<4> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   svfloat16_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_f32_f16_x(detail::PTrue(d_from), v);
}

// F32->F64 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   svfloat32_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_f64_f32_x(detail::PTrue(d_from), v);
}

// I32->F64 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   svint32_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_f64_s32_x(detail::PTrue(d_from), v);
}

// U32->F64 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::UnsignedTag /*from_type_tag*/, D d_to,
                                   svuint32_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_f64_u32_x(detail::PTrue(d_from), v);
}

// F32->I64 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   svfloat32_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_s64_f32_x(detail::PTrue(d_from), v);
}

// F32->U64 PromoteEvenTo
template <class D>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::UnsignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   svfloat32_t v) {
  const Repartition<float, decltype(d_to)> d_from;
  return svcvt_u64_f32_x(detail::PTrue(d_from), v);
}

// F16->F32 PromoteOddTo
template <class D>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::FloatTag to_type_tag,
                                  hwy::SizeTag<4> to_lane_size_tag,
                                  hwy::FloatTag from_type_tag, D d_to,
                                  svfloat16_t v) {
  return PromoteEvenTo(to_type_tag, to_lane_size_tag, from_type_tag, d_to,
                       DupOdd(v));
}

// I32/U32/F32->F64 PromoteOddTo
template <class FromTypeTag, class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::FloatTag to_type_tag,
                                  hwy::SizeTag<8> to_lane_size_tag,
                                  FromTypeTag from_type_tag, D d_to, V v) {
  return PromoteEvenTo(to_type_tag, to_lane_size_tag, from_type_tag, d_to,
                       DupOdd(v));
}

// F32->I64/U64 PromoteOddTo
template <class ToTypeTag, class D, HWY_IF_UI64_D(D)>
HWY_INLINE VFromD<D> PromoteOddTo(ToTypeTag to_type_tag,
                                  hwy::SizeTag<8> to_lane_size_tag,
                                  hwy::FloatTag from_type_tag, D d_to,
                                  svfloat32_t v) {
  return PromoteEvenTo(to_type_tag, to_lane_size_tag, from_type_tag, d_to,
                       DupOdd(v));
}

}  // namespace detail

// ------------------------------ ReorderDemote2To (OddEven)

template <size_t N, int kPow2>
HWY_API VBF16 ReorderDemote2To(Simd<bfloat16_t, N, kPow2> dbf16, svfloat32_t a,
                               svfloat32_t b) {
#if HWY_SVE_HAVE_F32_TO_BF16C
  const VBF16 b_in_even = svcvt_bf16_f32_x(detail::PTrue(dbf16), b);
  return svcvtnt_bf16_f32_x(b_in_even, detail::PTrue(dbf16), a);
#else
  (void)dbf16;
  const auto a_in_odd =
      BitCast(ScalableTag<uint16_t>(), detail::RoundF32ForDemoteToBF16(a));
  const auto b_in_odd =
      BitCast(ScalableTag<uint16_t>(), detail::RoundF32ForDemoteToBF16(b));
  return BitCast(dbf16, detail::InterleaveOdd(b_in_odd, a_in_odd));
#endif
}

template <size_t N, int kPow2>
HWY_API svint16_t ReorderDemote2To(Simd<int16_t, N, kPow2> d16, svint32_t a,
                                   svint32_t b) {
#if HWY_SVE_HAVE_2
  (void)d16;
  const svint16_t a_in_even = svqxtnb_s32(a);
  return svqxtnt_s32(a_in_even, b);
#else
  const svint16_t a16 = BitCast(d16, detail::SaturateI<int16_t>(a));
  const svint16_t b16 = BitCast(d16, detail::SaturateI<int16_t>(b));
  return detail::InterleaveEven(a16, b16);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint16_t ReorderDemote2To(Simd<uint16_t, N, kPow2> d16, svint32_t a,
                                    svint32_t b) {
#if HWY_SVE_HAVE_2
  (void)d16;
  const svuint16_t a_in_even = svqxtunb_s32(a);
  return svqxtunt_s32(a_in_even, b);
#else
  const Repartition<uint32_t, decltype(d16)> du32;
  const svuint32_t clamped_a = BitCast(du32, detail::MaxN(a, 0));
  const svuint32_t clamped_b = BitCast(du32, detail::MaxN(b, 0));
  const svuint16_t a16 = BitCast(d16, detail::SaturateU<uint16_t>(clamped_a));
  const svuint16_t b16 = BitCast(d16, detail::SaturateU<uint16_t>(clamped_b));
  return detail::InterleaveEven(a16, b16);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint16_t ReorderDemote2To(Simd<uint16_t, N, kPow2> d16, svuint32_t a,
                                    svuint32_t b) {
#if HWY_SVE_HAVE_2
  (void)d16;
  const svuint16_t a_in_even = svqxtnb_u32(a);
  return svqxtnt_u32(a_in_even, b);
#else
  const svuint16_t a16 = BitCast(d16, detail::SaturateU<uint16_t>(a));
  const svuint16_t b16 = BitCast(d16, detail::SaturateU<uint16_t>(b));
  return detail::InterleaveEven(a16, b16);
#endif
}

template <size_t N, int kPow2>
HWY_API svint8_t ReorderDemote2To(Simd<int8_t, N, kPow2> d8, svint16_t a,
                                  svint16_t b) {
#if HWY_SVE_HAVE_2
  (void)d8;
  const svint8_t a_in_even = svqxtnb_s16(a);
  return svqxtnt_s16(a_in_even, b);
#else
  const svint8_t a8 = BitCast(d8, detail::SaturateI<int8_t>(a));
  const svint8_t b8 = BitCast(d8, detail::SaturateI<int8_t>(b));
  return detail::InterleaveEven(a8, b8);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint8_t ReorderDemote2To(Simd<uint8_t, N, kPow2> d8, svint16_t a,
                                   svint16_t b) {
#if HWY_SVE_HAVE_2
  (void)d8;
  const svuint8_t a_in_even = svqxtunb_s16(a);
  return svqxtunt_s16(a_in_even, b);
#else
  const Repartition<uint16_t, decltype(d8)> du16;
  const svuint16_t clamped_a = BitCast(du16, detail::MaxN(a, 0));
  const svuint16_t clamped_b = BitCast(du16, detail::MaxN(b, 0));
  const svuint8_t a8 = BitCast(d8, detail::SaturateU<uint8_t>(clamped_a));
  const svuint8_t b8 = BitCast(d8, detail::SaturateU<uint8_t>(clamped_b));
  return detail::InterleaveEven(a8, b8);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint8_t ReorderDemote2To(Simd<uint8_t, N, kPow2> d8, svuint16_t a,
                                   svuint16_t b) {
#if HWY_SVE_HAVE_2
  (void)d8;
  const svuint8_t a_in_even = svqxtnb_u16(a);
  return svqxtnt_u16(a_in_even, b);
#else
  const svuint8_t a8 = BitCast(d8, detail::SaturateU<uint8_t>(a));
  const svuint8_t b8 = BitCast(d8, detail::SaturateU<uint8_t>(b));
  return detail::InterleaveEven(a8, b8);
#endif
}

template <size_t N, int kPow2>
HWY_API svint32_t ReorderDemote2To(Simd<int32_t, N, kPow2> d32, svint64_t a,
                                   svint64_t b) {
#if HWY_SVE_HAVE_2
  (void)d32;
  const svint32_t a_in_even = svqxtnb_s64(a);
  return svqxtnt_s64(a_in_even, b);
#else
  const svint32_t a32 = BitCast(d32, detail::SaturateI<int32_t>(a));
  const svint32_t b32 = BitCast(d32, detail::SaturateI<int32_t>(b));
  return detail::InterleaveEven(a32, b32);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint32_t ReorderDemote2To(Simd<uint32_t, N, kPow2> d32, svint64_t a,
                                    svint64_t b) {
#if HWY_SVE_HAVE_2
  (void)d32;
  const svuint32_t a_in_even = svqxtunb_s64(a);
  return svqxtunt_s64(a_in_even, b);
#else
  const Repartition<uint64_t, decltype(d32)> du64;
  const svuint64_t clamped_a = BitCast(du64, detail::MaxN(a, 0));
  const svuint64_t clamped_b = BitCast(du64, detail::MaxN(b, 0));
  const svuint32_t a32 = BitCast(d32, detail::SaturateU<uint32_t>(clamped_a));
  const svuint32_t b32 = BitCast(d32, detail::SaturateU<uint32_t>(clamped_b));
  return detail::InterleaveEven(a32, b32);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint32_t ReorderDemote2To(Simd<uint32_t, N, kPow2> d32, svuint64_t a,
                                    svuint64_t b) {
#if HWY_SVE_HAVE_2
  (void)d32;
  const svuint32_t a_in_even = svqxtnb_u64(a);
  return svqxtnt_u64(a_in_even, b);
#else
  const svuint32_t a32 = BitCast(d32, detail::SaturateU<uint32_t>(a));
  const svuint32_t b32 = BitCast(d32, detail::SaturateU<uint32_t>(b));
  return detail::InterleaveEven(a32, b32);
#endif
}

template <class D, class V, HWY_IF_SIGNED_D(D), HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_D(D, sizeof(TFromV<V>) / 2)>
HWY_API VFromD<D> ReorderDemote2To(D dn, V a, V b) {
  const auto clamped_a = BitCast(dn, detail::SaturateU<TFromD<D>>(a));
  const auto clamped_b = BitCast(dn, detail::SaturateU<TFromD<D>>(b));
  return detail::InterleaveEven(clamped_a, clamped_b);
}

template <class D, class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<D>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D dn, V a, V b) {
  const Half<decltype(dn)> dnh;
  const auto demoted_a = DemoteTo(dnh, a);
  const auto demoted_b = DemoteTo(dnh, b);
  return Combine(dn, demoted_b, demoted_a);
}

template <size_t N, int kPow2>
HWY_API VBF16 OrderedDemote2To(Simd<bfloat16_t, N, kPow2> dbf16, svfloat32_t a,
                               svfloat32_t b) {
#if HWY_SVE_HAVE_F32_TO_BF16C
  (void)dbf16;
  const VBF16 a_in_even = svcvt_bf16_f32_x(detail::PTrue(dbf16), a);
  const VBF16 b_in_even = svcvt_bf16_f32_x(detail::PTrue(dbf16), b);
  return ConcatEven(dbf16, b_in_even, a_in_even);
#else
  const RebindToUnsigned<decltype(dbf16)> du16;
  const svuint16_t a_in_odd = BitCast(du16, detail::RoundF32ForDemoteToBF16(a));
  const svuint16_t b_in_odd = BitCast(du16, detail::RoundF32ForDemoteToBF16(b));
  return BitCast(dbf16, ConcatOdd(du16, b_in_odd, a_in_odd));  // lower half
#endif
}

// ------------------------------ I8/U8/I16/U16 Div

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API V Div(V a, V b) {
  const DFromV<decltype(a)> d;
  const Half<decltype(d)> dh;
  const RepartitionToWide<decltype(d)> dw;

  const auto q_lo =
      Div(PromoteTo(dw, LowerHalf(dh, a)), PromoteTo(dw, LowerHalf(dh, b)));
  const auto q_hi = Div(PromoteUpperTo(dw, a), PromoteUpperTo(dw, b));

  return OrderedDemote2To(d, q_lo, q_hi);
}

// ------------------------------ I8/U8/I16/U16 MaskedDivOr
template <class V, class M, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V MaskedDivOr(V no, M m, V a, V b) {
  return IfThenElse(m, Div(a, b), no);
}

// ------------------------------ Mod (Div, NegMulAdd)
template <class V>
HWY_API V Mod(V a, V b) {
  return NegMulAdd(Div(a, b), b, a);
}

// ------------------------------ MaskedModOr (Mod)
template <class V, class M>
HWY_API V MaskedModOr(V no, M m, V a, V b) {
  return IfThenElse(m, Mod(a, b), no);
}

// ------------------------------ BroadcastSignBit (ShiftRight)
template <class V>
HWY_API V BroadcastSignBit(const V v) {
  return ShiftRight<sizeof(TFromV<V>) * 8 - 1>(v);
}

// ------------------------------ IfNegativeThenElse (BroadcastSignBit)
template <class V>
HWY_API V IfNegativeThenElse(V v, V yes, V no) {
  static_assert(IsSigned<TFromV<V>>(), "Only works for signed/float");
  return IfThenElse(IsNegative(v), yes, no);
}

// ------------------------------ AverageRound (ShiftRight)

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI32
#undef HWY_NATIVE_AVERAGE_ROUND_UI32
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI32
#endif

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI64
#undef HWY_NATIVE_AVERAGE_ROUND_UI64
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI64
#endif

#if HWY_SVE_HAVE_2
HWY_SVE_FOREACH_UI(HWY_SVE_RETV_ARGPVV, AverageRound, rhadd)
#else
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V AverageRound(const V a, const V b) {
  return Add(Add(ShiftRight<1>(a), ShiftRight<1>(b)),
             detail::AndN(Or(a, b), 1));
}
#endif  // HWY_SVE_HAVE_2

// ------------------------------ LoadMaskBits (TestBit)

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE svbool_t LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  // TODO(janwas): with SVE2.1, load to vector, then PMOV
  const RebindToUnsigned<D> du;
  const svuint8_t iota = Iota(du, 0);

  // Load correct number of bytes (bits/8) with 7 zeros after each.
  const svuint8_t bytes = BitCast(du, svld1ub_u64(detail::PTrue(d), bits));
  // Replicate bytes 8x such that each byte contains the bit that governs it.
  const svuint8_t rep8 = svtbl_u8(bytes, detail::AndNotN(7, iota));

  const svuint8_t bit =
      svdupq_n_u8(1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128);
  return TestBit(rep8, bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE svbool_t LoadMaskBits(D /* tag */,
                                 const uint8_t* HWY_RESTRICT bits) {
  const RebindToUnsigned<D> du;
  const Repartition<uint8_t, D> du8;

  // There may be up to 128 bits; avoid reading past the end.
  const svuint8_t bytes = svld1(FirstN(du8, (Lanes(du) + 7) / 8), bits);

  // Replicate bytes 16x such that each lane contains the bit that governs it.
  const svuint8_t rep16 = svtbl_u8(bytes, ShiftRight<4>(Iota(du8, 0)));

  const svuint16_t bit = svdupq_n_u16(1, 2, 4, 8, 16, 32, 64, 128);
  return TestBit(BitCast(du, rep16), bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE svbool_t LoadMaskBits(D /* tag */,
                                 const uint8_t* HWY_RESTRICT bits) {
  const RebindToUnsigned<D> du;
  const Repartition<uint8_t, D> du8;

  // Upper bound = 2048 bits / 32 bit = 64 bits; at least 8 bytes are readable,
  // so we can skip computing the actual length (Lanes(du)+7)/8.
  const svuint8_t bytes = svld1(FirstN(du8, 8), bits);

  // Replicate bytes 32x such that each lane contains the bit that governs it.
  const svuint8_t rep32 = svtbl_u8(bytes, ShiftRight<5>(Iota(du8, 0)));

  // 1, 2, 4, 8, 16, 32, 64, 128,  1, 2 ..
  const svuint32_t bit = Shl(Set(du, 1), detail::AndN(Iota(du, 0), 7));

  return TestBit(BitCast(du, rep32), bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE svbool_t LoadMaskBits(D /* tag */,
                                 const uint8_t* HWY_RESTRICT bits) {
  const RebindToUnsigned<D> du;

  // Max 2048 bits = 32 lanes = 32 input bits; replicate those into each lane.
  // The "at least 8 byte" guarantee in quick_reference ensures this is safe.
  uint32_t mask_bits;
  CopyBytes<4>(bits, &mask_bits);  // copy from bytes
  const auto vbits = Set(du, mask_bits);

  // 2 ^ {0,1, .., 31}, will not have more lanes than that.
  const svuint64_t bit = Shl(Set(du, 1), Iota(du, 0));

  return TestBit(vbits, bit);
}

// ------------------------------ Dup128MaskFromMaskBits

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  const RebindToUnsigned<decltype(d)> du;

  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;

  // Replicate the lower 8 bits of mask_bits to each u8 lane
  const svuint8_t bytes = BitCast(du, Set(du, static_cast<uint8_t>(mask_bits)));

  const svuint8_t bit =
      svdupq_n_u8(1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128);
  return TestBit(bytes, bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_V_SIZE_GT_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint16_t, decltype(du)> du16;

  // Replicate the lower 16 bits of mask_bits to each u16 lane of a u16 vector,
  // and then bitcast the replicated mask_bits to a u8 vector
  const svuint8_t bytes =
      BitCast(du, Set(du16, static_cast<uint16_t>(mask_bits)));
  // Replicate bytes 8x such that each byte contains the bit that governs it.
  const svuint8_t rep8 = svtbl_u8(bytes, ShiftRight<3>(Iota(du, 0)));

  const svuint8_t bit =
      svdupq_n_u8(1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128);
  return TestBit(rep8, bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint8_t, decltype(d)> du8;

  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;

  // Set all of the u8 lanes of bytes to the lower 8 bits of mask_bits
  const svuint8_t bytes = Set(du8, static_cast<uint8_t>(mask_bits));

  const svuint16_t bit = svdupq_n_u16(1, 2, 4, 8, 16, 32, 64, 128);
  return TestBit(BitCast(du, bytes), bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint8_t, decltype(d)> du8;

  constexpr size_t kN = MaxLanes(d);
  if (kN < 4) mask_bits &= (1u << kN) - 1;

  // Set all of the u8 lanes of bytes to the lower 8 bits of mask_bits
  const svuint8_t bytes = Set(du8, static_cast<uint8_t>(mask_bits));

  const svuint32_t bit = svdupq_n_u32(1, 2, 4, 8);
  return TestBit(BitCast(du, bytes), bit);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint8_t, decltype(d)> du8;

  if (MaxLanes(d) < 2) mask_bits &= 1u;

  // Set all of the u8 lanes of bytes to the lower 8 bits of mask_bits
  const svuint8_t bytes = Set(du8, static_cast<uint8_t>(mask_bits));

  const svuint64_t bit = svdupq_n_u64(1, 2);
  return TestBit(BitCast(du, bytes), bit);
}

// ------------------------------ StoreMaskBits

namespace detail {

// For each mask lane (governing lane type T), store 1 or 0 in BYTE lanes.
template <class T, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE svuint8_t BoolFromMask(svbool_t m) {
  return svdup_n_u8_z(m, 1);
}
template <class T, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE svuint8_t BoolFromMask(svbool_t m) {
  const ScalableTag<uint8_t> d8;
  const svuint8_t b16 = BitCast(d8, svdup_n_u16_z(m, 1));
  return detail::ConcatEvenFull(b16, b16);  // lower half
}
template <class T, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE svuint8_t BoolFromMask(svbool_t m) {
  return U8FromU32(svdup_n_u32_z(m, 1));
}
template <class T, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE svuint8_t BoolFromMask(svbool_t m) {
  const ScalableTag<uint32_t> d32;
  const svuint32_t b64 = BitCast(d32, svdup_n_u64_z(m, 1));
  return U8FromU32(detail::ConcatEvenFull(b64, b64));  // lower half
}

// Compacts groups of 8 u8 into 8 contiguous bits in a 64-bit lane.
HWY_INLINE svuint64_t BitsFromBool(svuint8_t x) {
  const ScalableTag<uint8_t> d8;
  const ScalableTag<uint16_t> d16;
  const ScalableTag<uint32_t> d32;
  const ScalableTag<uint64_t> d64;
  // TODO(janwas): could use SVE2 BDEP, but it's optional.
  x = Or(x, BitCast(d8, ShiftRight<7>(BitCast(d16, x))));
  x = Or(x, BitCast(d8, ShiftRight<14>(BitCast(d32, x))));
  x = Or(x, BitCast(d8, ShiftRight<28>(BitCast(d64, x))));
  return BitCast(d64, x);
}

}  // namespace detail

// `p` points to at least 8 writable bytes.
// TODO(janwas): specialize for HWY_SVE_256
// TODO(janwas): with SVE2.1, use PMOV to store to vector, then StoreU
template <class D>
HWY_API size_t StoreMaskBits(D d, svbool_t m, uint8_t* bits) {
  svuint64_t bits_in_u64 =
      detail::BitsFromBool(detail::BoolFromMask<TFromD<D>>(m));

  const size_t num_bits = Lanes(d);
  const size_t num_bytes = (num_bits + 8 - 1) / 8;  // Round up, see below

  // Truncate each u64 to 8 bits and store to u8.
  svst1b_u64(FirstN(ScalableTag<uint64_t>(), num_bytes), bits, bits_in_u64);

  // Non-full byte, need to clear the undefined upper bits. Can happen for
  // capped/fractional vectors or large T and small hardware vectors.
  if (num_bits < 8) {
    const int mask = static_cast<int>((1ull << num_bits) - 1);
    bits[0] = static_cast<uint8_t>(bits[0] & mask);
  }
  // Else: we wrote full bytes because num_bits is a power of two >= 8.

  return num_bytes;
}

// ------------------------------ CompressBits (LoadMaskBits)
template <class V, HWY_IF_NOT_T_SIZE_V(V, 1)>
HWY_INLINE V CompressBits(V v, const uint8_t* HWY_RESTRICT bits) {
  return Compress(v, LoadMaskBits(DFromV<V>(), bits));
}

// ------------------------------ CompressBitsStore (LoadMaskBits)
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  return CompressStore(v, LoadMaskBits(d, bits), d, unaligned);
}

// ------------------------------ Expand (StoreMaskBits)

#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

namespace detail {

HWY_INLINE svuint8_t IndicesForExpandFromBits(uint64_t mask_bits) {
  const CappedTag<uint8_t, 8> du8;
  alignas(16) static constexpr uint8_t table[8 * 256] = {
      // PrintExpand8x8Tables
      128, 128, 128, 128, 128, 128, 128, 128,  //
      0,   128, 128, 128, 128, 128, 128, 128,  //
      128, 0,   128, 128, 128, 128, 128, 128,  //
      0,   1,   128, 128, 128, 128, 128, 128,  //
      128, 128, 0,   128, 128, 128, 128, 128,  //
      0,   128, 1,   128, 128, 128, 128, 128,  //
      128, 0,   1,   128, 128, 128, 128, 128,  //
      0,   1,   2,   128, 128, 128, 128, 128,  //
      128, 128, 128, 0,   128, 128, 128, 128,  //
      0,   128, 128, 1,   128, 128, 128, 128,  //
      128, 0,   128, 1,   128, 128, 128, 128,  //
      0,   1,   128, 2,   128, 128, 128, 128,  //
      128, 128, 0,   1,   128, 128, 128, 128,  //
      0,   128, 1,   2,   128, 128, 128, 128,  //
      128, 0,   1,   2,   128, 128, 128, 128,  //
      0,   1,   2,   3,   128, 128, 128, 128,  //
      128, 128, 128, 128, 0,   128, 128, 128,  //
      0,   128, 128, 128, 1,   128, 128, 128,  //
      128, 0,   128, 128, 1,   128, 128, 128,  //
      0,   1,   128, 128, 2,   128, 128, 128,  //
      128, 128, 0,   128, 1,   128, 128, 128,  //
      0,   128, 1,   128, 2,   128, 128, 128,  //
      128, 0,   1,   128, 2,   128, 128, 128,  //
      0,   1,   2,   128, 3,   128, 128, 128,  //
      128, 128, 128, 0,   1,   128, 128, 128,  //
      0,   128, 128, 1,   2,   128, 128, 128,  //
      128, 0,   128, 1,   2,   128, 128, 128,  //
      0,   1,   128, 2,   3,   128, 128, 128,  //
      128, 128, 0,   1,   2,   128, 128, 128,  //
      0,   128, 1,   2,   3,   128, 128, 128,  //
      128, 0,   1,   2,   3,   128, 128, 128,  //
      0,   1,   2,   3,   4,   128, 128, 128,  //
      128, 128, 128, 128, 128, 0,   128, 128,  //
      0,   128, 128, 128, 128, 1,   128, 128,  //
      128, 0,   128, 128, 128, 1,   128, 128,  //
      0,   1,   128, 128, 128, 2,   128, 128,  //
      128, 128, 0,   128, 128, 1,   128, 128,  //
      0,   128, 1,   128, 128, 2,   128, 128,  //
      128, 0,   1,   128, 128, 2,   128, 128,  //
      0,   1,   2,   128, 128, 3,   128, 128,  //
      128, 128, 128, 0,   128, 1,   128, 128,  //
      0,   128, 128, 1,   128, 2,   128, 128,  //
      128, 0,   128, 1,   128, 2,   128, 128,  //
      0,   1,   128, 2,   128, 3,   128, 128,  //
      128, 128, 0,   1,   128, 2,   128, 128,  //
      0,   128, 1,   2,   128, 3,   128, 128,  //
      128, 0,   1,   2,   128, 3,   128, 128,  //
      0,   1,   2,   3,   128, 4,   128, 128,  //
      128, 128, 128, 128, 0,   1,   128, 128,  //
      0,   128, 128, 128, 1,   2,   128, 128,  //
      128, 0,   128, 128, 1,   2,   128, 128,  //
      0,   1,   128, 128, 2,   3,   128, 128,  //
      128, 128, 0,   128, 1,   2,   128, 128,  //
      0,   128, 1,   128, 2,   3,   128, 128,  //
      128, 0,   1,   128, 2,   3,   128, 128,  //
      0,   1,   2,   128, 3,   4,   128, 128,  //
      128, 128, 128, 0,   1,   2,   128, 128,  //
      0,   128, 128, 1,   2,   3,   128, 128,  //
      128, 0,   128, 1,   2,   3,   128, 128,  //
      0,   1,   128, 2,   3,   4,   128, 128,  //
      128, 128, 0,   1,   2,   3,   128, 128,  //
      0,   128, 1,   2,   3,   4,   128, 128,  //
      128, 0,   1,   2,   3,   4,   128, 128,  //
      0,   1,   2,   3,   4,   5,   128, 128,  //
      128, 128, 128, 128, 128, 128, 0,   128,  //
      0,   128, 128, 128, 128, 128, 1,   128,  //
      128, 0,   128, 128, 128, 128, 1,   128,  //
      0,   1,   128, 128, 128, 128, 2,   128,  //
      128, 128, 0,   128, 128, 128, 1,   128,  //
      0,   128, 1,   128, 128, 128, 2,   128,  //
      128, 0,   1,   128, 128, 128, 2,   128,  //
      0,   1,   2,   128, 128, 128, 3,   128,  //
      128, 128, 128, 0,   128, 128, 1,   128,  //
      0,   128, 128, 1,   128, 128, 2,   128,  //
      128, 0,   128, 1,   128, 128, 2,   128,  //
      0,   1,   128, 2,   128, 128, 3,   128,  //
      128, 128, 0,   1,   128, 128, 2,   128,  //
      0,   128, 1,   2,   128, 128, 3,   128,  //
      128, 0,   1,   2,   128, 128, 3,   128,  //
      0,   1,   2,   3,   128, 128, 4,   128,  //
      128, 128, 128, 128, 0,   128, 1,   128,  //
      0,   128, 128, 128, 1,   128, 2,   128,  //
      128, 0,   128, 128, 1,   128, 2,   128,  //
      0,   1,   128, 128, 2,   128, 3,   128,  //
      128, 128, 0,   128, 1,   128, 2,   128,  //
      0,   128, 1,   128, 2,   128, 3,   128,  //
      128, 0,   1,   128, 2,   128, 3,   128,  //
      0,   1,   2,   128, 3,   128, 4,   128,  //
      128, 128, 128, 0,   1,   128, 2,   128,  //
      0,   128, 128, 1,   2,   128, 3,   128,  //
      128, 0,   128, 1,   2,   128, 3,   128,  //
      0,   1,   128, 2,   3,   128, 4,   128,  //
      128, 128, 0,   1,   2,   128, 3,   128,  //
      0,   128, 1,   2,   3,   128, 4,   128,  //
      128, 0,   1,   2,   3,   128, 4,   128,  //
      0,   1,   2,   3,   4,   128, 5,   128,  //
      128, 128, 128, 128, 128, 0,   1,   128,  //
      0,   128, 128, 128, 128, 1,   2,   128,  //
      128, 0,   128, 128, 128, 1,   2,   128,  //
      0,   1,   128, 128, 128, 2,   3,   128,  //
      128, 128, 0,   128, 128, 1,   2,   128,  //
      0,   128, 1,   128, 128, 2,   3,   128,  //
      128, 0,   1,   128, 128, 2,   3,   128,  //
      0,   1,   2,   128, 128, 3,   4,   128,  //
      128, 128, 128, 0,   128, 1,   2,   128,  //
      0,   128, 128, 1,   128, 2,   3,   128,  //
      128, 0,   128, 1,   128, 2,   3,   128,  //
      0,   1,   128, 2,   128, 3,   4,   128,  //
      128, 128, 0,   1,   128, 2,   3,   128,  //
      0,   128, 1,   2,   128, 3,   4,   128,  //
      128, 0,   1,   2,   128, 3,   4,   128,  //
      0,   1,   2,   3,   128, 4,   5,   128,  //
      128, 128, 128, 128, 0,   1,   2,   128,  //
      0,   128, 128, 128, 1,   2,   3,   128,  //
      128, 0,   128, 128, 1,   2,   3,   128,  //
      0,   1,   128, 128, 2,   3,   4,   128,  //
      128, 128, 0,   128, 1,   2,   3,   128,  //
      0,   128, 1,   128, 2,   3,   4,   128,  //
      128, 0,   1,   128, 2,   3,   4,   128,  //
      0,   1,   2,   128, 3,   4,   5,   128,  //
      128, 128, 128, 0,   1,   2,   3,   128,  //
      0,   128, 128, 1,   2,   3,   4,   128,  //
      128, 0,   128, 1,   2,   3,   4,   128,  //
      0,   1,   128, 2,   3,   4,   5,   128,  //
      128, 128, 0,   1,   2,   3,   4,   128,  //
      0,   128, 1,   2,   3,   4,   5,   128,  //
      128, 0,   1,   2,   3,   4,   5,   128,  //
      0,   1,   2,   3,   4,   5,   6,   128,  //
      128, 128, 128, 128, 128, 128, 128, 0,    //
      0,   128, 128, 128, 128, 128, 128, 1,    //
      128, 0,   128, 128, 128, 128, 128, 1,    //
      0,   1,   128, 128, 128, 128, 128, 2,    //
      128, 128, 0,   128, 128, 128, 128, 1,    //
      0,   128, 1,   128, 128, 128, 128, 2,    //
      128, 0,   1,   128, 128, 128, 128, 2,    //
      0,   1,   2,   128, 128, 128, 128, 3,    //
      128, 128, 128, 0,   128, 128, 128, 1,    //
      0,   128, 128, 1,   128, 128, 128, 2,    //
      128, 0,   128, 1,   128, 128, 128, 2,    //
      0,   1,   128, 2,   128, 128, 128, 3,    //
      128, 128, 0,   1,   128, 128, 128, 2,    //
      0,   128, 1,   2,   128, 128, 128, 3,    //
      128, 0,   1,   2,   128, 128, 128, 3,    //
      0,   1,   2,   3,   128, 128, 128, 4,    //
      128, 128, 128, 128, 0,   128, 128, 1,    //
      0,   128, 128, 128, 1,   128, 128, 2,    //
      128, 0,   128, 128, 1,   128, 128, 2,    //
      0,   1,   128, 128, 2,   128, 128, 3,    //
      128, 128, 0,   128, 1,   128, 128, 2,    //
      0,   128, 1,   128, 2,   128, 128, 3,    //
      128, 0,   1,   128, 2,   128, 128, 3,    //
      0,   1,   2,   128, 3,   128, 128, 4,    //
      128, 128, 128, 0,   1,   128, 128, 2,    //
      0,   128, 128, 1,   2,   128, 128, 3,    //
      128, 0,   128, 1,   2,   128, 128, 3,    //
      0,   1,   128, 2,   3,   128, 128, 4,    //
      128, 128, 0,   1,   2,   128, 128, 3,    //
      0,   128, 1,   2,   3,   128, 128, 4,    //
      128, 0,   1,   2,   3,   128, 128, 4,    //
      0,   1,   2,   3,   4,   128, 128, 5,    //
      128, 128, 128, 128, 128, 0,   128, 1,    //
      0,   128, 128, 128, 128, 1,   128, 2,    //
      128, 0,   128, 128, 128, 1,   128, 2,    //
      0,   1,   128, 128, 128, 2,   128, 3,    //
      128, 128, 0,   128, 128, 1,   128, 2,    //
      0,   128, 1,   128, 128, 2,   128, 3,    //
      128, 0,   1,   128, 128, 2,   128, 3,    //
      0,   1,   2,   128, 128, 3,   128, 4,    //
      128, 128, 128, 0,   128, 1,   128, 2,    //
      0,   128, 128, 1,   128, 2,   128, 3,    //
      128, 0,   128, 1,   128, 2,   128, 3,    //
      0,   1,   128, 2,   128, 3,   128, 4,    //
      128, 128, 0,   1,   128, 2,   128, 3,    //
      0,   128, 1,   2,   128, 3,   128, 4,    //
      128, 0,   1,   2,   128, 3,   128, 4,    //
      0,   1,   2,   3,   128, 4,   128, 5,    //
      128, 128, 128, 128, 0,   1,   128, 2,    //
      0,   128, 128, 128, 1,   2,   128, 3,    //
      128, 0,   128, 128, 1,   2,   128, 3,    //
      0,   1,   128, 128, 2,   3,   128, 4,    //
      128, 128, 0,   128, 1,   2,   128, 3,    //
      0,   128, 1,   128, 2,   3,   128, 4,    //
      128, 0,   1,   128, 2,   3,   128, 4,    //
      0,   1,   2,   128, 3,   4,   128, 5,    //
      128, 128, 128, 0,   1,   2,   128, 3,    //
      0,   128, 128, 1,   2,   3,   128, 4,    //
      128, 0,   128, 1,   2,   3,   128, 4,    //
      0,   1,   128, 2,   3,   4,   128, 5,    //
      128, 128, 0,   1,   2,   3,   128, 4,    //
      0,   128, 1,   2,   3,   4,   128, 5,    //
      128, 0,   1,   2,   3,   4,   128, 5,    //
      0,   1,   2,   3,   4,   5,   128, 6,    //
      128, 128, 128, 128, 128, 128, 0,   1,    //
      0,   128, 128, 128, 128, 128, 1,   2,    //
      128, 0,   128, 128, 128, 128, 1,   2,    //
      0,   1,   128, 128, 128, 128, 2,   3,    //
      128, 128, 0,   128, 128, 128, 1,   2,    //
      0,   128, 1,   128, 128, 128, 2,   3,    //
      128, 0,   1,   128, 128, 128, 2,   3,    //
      0,   1,   2,   128, 128, 128, 3,   4,    //
      128, 128, 128, 0,   128, 128, 1,   2,    //
      0,   128, 128, 1,   128, 128, 2,   3,    //
      128, 0,   128, 1,   128, 128, 2,   3,    //
      0,   1,   128, 2,   128, 128, 3,   4,    //
      128, 128, 0,   1,   128, 128, 2,   3,    //
      0,   128, 1,   2,   128, 128, 3,   4,    //
      128, 0,   1,   2,   128, 128, 3,   4,    //
      0,   1,   2,   3,   128, 128, 4,   5,    //
      128, 128, 128, 128, 0,   128, 1,   2,    //
      0,   128, 128, 128, 1,   128, 2,   3,    //
      128, 0,   128, 128, 1,   128, 2,   3,    //
      0,   1,   128, 128, 2,   128, 3,   4,    //
      128, 128, 0,   128, 1,   128, 2,   3,    //
      0,   128, 1,   128, 2,   128, 3,   4,    //
      128, 0,   1,   128, 2,   128, 3,   4,    //
      0,   1,   2,   128, 3,   128, 4,   5,    //
      128, 128, 128, 0,   1,   128, 2,   3,    //
      0,   128, 128, 1,   2,   128, 3,   4,    //
      128, 0,   128, 1,   2,   128, 3,   4,    //
      0,   1,   128, 2,   3,   128, 4,   5,    //
      128, 128, 0,   1,   2,   128, 3,   4,    //
      0,   128, 1,   2,   3,   128, 4,   5,    //
      128, 0,   1,   2,   3,   128, 4,   5,    //
      0,   1,   2,   3,   4,   128, 5,   6,    //
      128, 128, 128, 128, 128, 0,   1,   2,    //
      0,   128, 128, 128, 128, 1,   2,   3,    //
      128, 0,   128, 128, 128, 1,   2,   3,    //
      0,   1,   128, 128, 128, 2,   3,   4,    //
      128, 128, 0,   128, 128, 1,   2,   3,    //
      0,   128, 1,   128, 128, 2,   3,   4,    //
      128, 0,   1,   128, 128, 2,   3,   4,    //
      0,   1,   2,   128, 128, 3,   4,   5,    //
      128, 128, 128, 0,   128, 1,   2,   3,    //
      0,   128, 128, 1,   128, 2,   3,   4,    //
      128, 0,   128, 1,   128, 2,   3,   4,    //
      0,   1,   128, 2,   128, 3,   4,   5,    //
      128, 128, 0,   1,   128, 2,   3,   4,    //
      0,   128, 1,   2,   128, 3,   4,   5,    //
      128, 0,   1,   2,   128, 3,   4,   5,    //
      0,   1,   2,   3,   128, 4,   5,   6,    //
      128, 128, 128, 128, 0,   1,   2,   3,    //
      0,   128, 128, 128, 1,   2,   3,   4,    //
      128, 0,   128, 128, 1,   2,   3,   4,    //
      0,   1,   128, 128, 2,   3,   4,   5,    //
      128, 128, 0,   128, 1,   2,   3,   4,    //
      0,   128, 1,   128, 2,   3,   4,   5,    //
      128, 0,   1,   128, 2,   3,   4,   5,    //
      0,   1,   2,   128, 3,   4,   5,   6,    //
      128, 128, 128, 0,   1,   2,   3,   4,    //
      0,   128, 128, 1,   2,   3,   4,   5,    //
      128, 0,   128, 1,   2,   3,   4,   5,    //
      0,   1,   128, 2,   3,   4,   5,   6,    //
      128, 128, 0,   1,   2,   3,   4,   5,    //
      0,   128, 1,   2,   3,   4,   5,   6,    //
      128, 0,   1,   2,   3,   4,   5,   6,    //
      0,   1,   2,   3,   4,   5,   6,   7};
  return Load(du8, table + mask_bits * 8);
}

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE svuint8_t LaneIndicesFromByteIndices(D, svuint8_t idx) {
  return idx;
}
template <class D, class DU = RebindToUnsigned<D>, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<DU> LaneIndicesFromByteIndices(D, svuint8_t idx) {
  return PromoteTo(DU(), idx);
}

// General case when we don't know the vector size, 8 elements at a time.
template <class V>
HWY_INLINE V ExpandLoop(V v, svbool_t mask) {
  const DFromV<V> d;
  using T = TFromV<V>;
  uint8_t mask_bytes[256 / 8];
  StoreMaskBits(d, mask, mask_bytes);

  // ShiftLeftLanes is expensive, so we're probably better off storing to memory
  // and loading the final result.
  alignas(16) T out[2 * MaxLanes(d)];

  svbool_t next = svpfalse_b();
  size_t input_consumed = 0;
  const V iota = Iota(d, 0);
  for (size_t i = 0; i < Lanes(d); i += 8) {
    uint64_t mask_bits = mask_bytes[i / 8];

    // We want to skip past the v lanes already consumed. There is no
    // instruction for variable-shift-reg, but we can splice.
    const V vH = detail::Splice(v, v, next);
    input_consumed += PopCount(mask_bits);
    next = detail::GeN(iota, ConvertScalarTo<T>(input_consumed));

    const auto idx = detail::LaneIndicesFromByteIndices(
        d, detail::IndicesForExpandFromBits(mask_bits));
    const V expand = TableLookupLanes(vH, idx);
    StoreU(expand, d, out + i);
  }
  return LoadU(d, out);
}

}  // namespace detail

template <class V, HWY_IF_T_SIZE_V(V, 1)>
HWY_API V Expand(V v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE2_128 || HWY_IDE
  const DFromV<V> d;
  uint8_t mask_bytes[256 / 8];
  StoreMaskBits(d, mask, mask_bytes);
  const uint64_t maskL = mask_bytes[0];
  const uint64_t maskH = mask_bytes[1];

  // We want to skip past the v bytes already consumed by expandL. There is no
  // instruction for shift-reg by variable bytes, but we can splice. Instead of
  // GeN, Not(FirstN()) would also work.
  using T = TFromV<V>;
  const T countL = static_cast<T>(PopCount(maskL));
  const V vH = detail::Splice(v, v, detail::GeN(Iota(d, 0), countL));

  const svuint8_t idxL = detail::IndicesForExpandFromBits(maskL);
  const svuint8_t idxH = detail::IndicesForExpandFromBits(maskH);
  return Combine(d, TableLookupLanes(vH, idxH), TableLookupLanes(v, idxL));
#else
  return detail::ExpandLoop(v, mask);
#endif
}

template <class V, HWY_IF_T_SIZE_V(V, 2)>
HWY_API V Expand(V v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE2_128 || HWY_IDE  // 16x8
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du16;
  const Rebind<uint8_t, decltype(d)> du8;
  // Convert mask into bitfield via horizontal sum (faster than ORV) of 8 bits.
  // Pre-multiply by N so we can use it as an offset for Load.
  const svuint16_t bits = Shl(Set(du16, 1), Iota(du16, 3));
  const size_t offset = detail::SumOfLanesM(mask, bits);

  // Storing as 8-bit reduces table size from 4 KiB to 2 KiB. We cannot apply
  // the nibble trick used below because not all indices fit within one lane.
  alignas(16) static constexpr uint8_t table[8 * 256] = {
      // PrintExpand16x8LaneTables
      255, 255, 255, 255, 255, 255, 255, 255,  //
      0,   255, 255, 255, 255, 255, 255, 255,  //
      255, 0,   255, 255, 255, 255, 255, 255,  //
      0,   1,   255, 255, 255, 255, 255, 255,  //
      255, 255, 0,   255, 255, 255, 255, 255,  //
      0,   255, 1,   255, 255, 255, 255, 255,  //
      255, 0,   1,   255, 255, 255, 255, 255,  //
      0,   1,   2,   255, 255, 255, 255, 255,  //
      255, 255, 255, 0,   255, 255, 255, 255,  //
      0,   255, 255, 1,   255, 255, 255, 255,  //
      255, 0,   255, 1,   255, 255, 255, 255,  //
      0,   1,   255, 2,   255, 255, 255, 255,  //
      255, 255, 0,   1,   255, 255, 255, 255,  //
      0,   255, 1,   2,   255, 255, 255, 255,  //
      255, 0,   1,   2,   255, 255, 255, 255,  //
      0,   1,   2,   3,   255, 255, 255, 255,  //
      255, 255, 255, 255, 0,   255, 255, 255,  //
      0,   255, 255, 255, 1,   255, 255, 255,  //
      255, 0,   255, 255, 1,   255, 255, 255,  //
      0,   1,   255, 255, 2,   255, 255, 255,  //
      255, 255, 0,   255, 1,   255, 255, 255,  //
      0,   255, 1,   255, 2,   255, 255, 255,  //
      255, 0,   1,   255, 2,   255, 255, 255,  //
      0,   1,   2,   255, 3,   255, 255, 255,  //
      255, 255, 255, 0,   1,   255, 255, 255,  //
      0,   255, 255, 1,   2,   255, 255, 255,  //
      255, 0,   255, 1,   2,   255, 255, 255,  //
      0,   1,   255, 2,   3,   255, 255, 255,  //
      255, 255, 0,   1,   2,   255, 255, 255,  //
      0,   255, 1,   2,   3,   255, 255, 255,  //
      255, 0,   1,   2,   3,   255, 255, 255,  //
      0,   1,   2,   3,   4,   255, 255, 255,  //
      255, 255, 255, 255, 255, 0,   255, 255,  //
      0,   255, 255, 255, 255, 1,   255, 255,  //
      255, 0,   255, 255, 255, 1,   255, 255,  //
      0,   1,   255, 255, 255, 2,   255, 255,  //
      255, 255, 0,   255, 255, 1,   255, 255,  //
      0,   255, 1,   255, 255, 2,   255, 255,  //
      255, 0,   1,   255, 255, 2,   255, 255,  //
      0,   1,   2,   255, 255, 3,   255, 255,  //
      255, 255, 255, 0,   255, 1,   255, 255,  //
      0,   255, 255, 1,   255, 2,   255, 255,  //
      255, 0,   255, 1,   255, 2,   255, 255,  //
      0,   1,   255, 2,   255, 3,   255, 255,  //
      255, 255, 0,   1,   255, 2,   255, 255,  //
      0,   255, 1,   2,   255, 3,   255, 255,  //
      255, 0,   1,   2,   255, 3,   255, 255,  //
      0,   1,   2,   3,   255, 4,   255, 255,  //
      255, 255, 255, 255, 0,   1,   255, 255,  //
      0,   255, 255, 255, 1,   2,   255, 255,  //
      255, 0,   255, 255, 1,   2,   255, 255,  //
      0,   1,   255, 255, 2,   3,   255, 255,  //
      255, 255, 0,   255, 1,   2,   255, 255,  //
      0,   255, 1,   255, 2,   3,   255, 255,  //
      255, 0,   1,   255, 2,   3,   255, 255,  //
      0,   1,   2,   255, 3,   4,   255, 255,  //
      255, 255, 255, 0,   1,   2,   255, 255,  //
      0,   255, 255, 1,   2,   3,   255, 255,  //
      255, 0,   255, 1,   2,   3,   255, 255,  //
      0,   1,   255, 2,   3,   4,   255, 255,  //
      255, 255, 0,   1,   2,   3,   255, 255,  //
      0,   255, 1,   2,   3,   4,   255, 255,  //
      255, 0,   1,   2,   3,   4,   255, 255,  //
      0,   1,   2,   3,   4,   5,   255, 255,  //
      255, 255, 255, 255, 255, 255, 0,   255,  //
      0,   255, 255, 255, 255, 255, 1,   255,  //
      255, 0,   255, 255, 255, 255, 1,   255,  //
      0,   1,   255, 255, 255, 255, 2,   255,  //
      255, 255, 0,   255, 255, 255, 1,   255,  //
      0,   255, 1,   255, 255, 255, 2,   255,  //
      255, 0,   1,   255, 255, 255, 2,   255,  //
      0,   1,   2,   255, 255, 255, 3,   255,  //
      255, 255, 255, 0,   255, 255, 1,   255,  //
      0,   255, 255, 1,   255, 255, 2,   255,  //
      255, 0,   255, 1,   255, 255, 2,   255,  //
      0,   1,   255, 2,   255, 255, 3,   255,  //
      255, 255, 0,   1,   255, 255, 2,   255,  //
      0,   255, 1,   2,   255, 255, 3,   255,  //
      255, 0,   1,   2,   255, 255, 3,   255,  //
      0,   1,   2,   3,   255, 255, 4,   255,  //
      255, 255, 255, 255, 0,   255, 1,   255,  //
      0,   255, 255, 255, 1,   255, 2,   255,  //
      255, 0,   255, 255, 1,   255, 2,   255,  //
      0,   1,   255, 255, 2,   255, 3,   255,  //
      255, 255, 0,   255, 1,   255, 2,   255,  //
      0,   255, 1,   255, 2,   255, 3,   255,  //
      255, 0,   1,   255, 2,   255, 3,   255,  //
      0,   1,   2,   255, 3,   255, 4,   255,  //
      255, 255, 255, 0,   1,   255, 2,   255,  //
      0,   255, 255, 1,   2,   255, 3,   255,  //
      255, 0,   255, 1,   2,   255, 3,   255,  //
      0,   1,   255, 2,   3,   255, 4,   255,  //
      255, 255, 0,   1,   2,   255, 3,   255,  //
      0,   255, 1,   2,   3,   255, 4,   255,  //
      255, 0,   1,   2,   3,   255, 4,   255,  //
      0,   1,   2,   3,   4,   255, 5,   255,  //
      255, 255, 255, 255, 255, 0,   1,   255,  //
      0,   255, 255, 255, 255, 1,   2,   255,  //
      255, 0,   255, 255, 255, 1,   2,   255,  //
      0,   1,   255, 255, 255, 2,   3,   255,  //
      255, 255, 0,   255, 255, 1,   2,   255,  //
      0,   255, 1,   255, 255, 2,   3,   255,  //
      255, 0,   1,   255, 255, 2,   3,   255,  //
      0,   1,   2,   255, 255, 3,   4,   255,  //
      255, 255, 255, 0,   255, 1,   2,   255,  //
      0,   255, 255, 1,   255, 2,   3,   255,  //
      255, 0,   255, 1,   255, 2,   3,   255,  //
      0,   1,   255, 2,   255, 3,   4,   255,  //
      255, 255, 0,   1,   255, 2,   3,   255,  //
      0,   255, 1,   2,   255, 3,   4,   255,  //
      255, 0,   1,   2,   255, 3,   4,   255,  //
      0,   1,   2,   3,   255, 4,   5,   255,  //
      255, 255, 255, 255, 0,   1,   2,   255,  //
      0,   255, 255, 255, 1,   2,   3,   255,  //
      255, 0,   255, 255, 1,   2,   3,   255,  //
      0,   1,   255, 255, 2,   3,   4,   255,  //
      255, 255, 0,   255, 1,   2,   3,   255,  //
      0,   255, 1,   255, 2,   3,   4,   255,  //
      255, 0,   1,   255, 2,   3,   4,   255,  //
      0,   1,   2,   255, 3,   4,   5,   255,  //
      255, 255, 255, 0,   1,   2,   3,   255,  //
      0,   255, 255, 1,   2,   3,   4,   255,  //
      255, 0,   255, 1,   2,   3,   4,   255,  //
      0,   1,   255, 2,   3,   4,   5,   255,  //
      255, 255, 0,   1,   2,   3,   4,   255,  //
      0,   255, 1,   2,   3,   4,   5,   255,  //
      255, 0,   1,   2,   3,   4,   5,   255,  //
      0,   1,   2,   3,   4,   5,   6,   255,  //
      255, 255, 255, 255, 255, 255, 255, 0,    //
      0,   255, 255, 255, 255, 255, 255, 1,    //
      255, 0,   255, 255, 255, 255, 255, 1,    //
      0,   1,   255, 255, 255, 255, 255, 2,    //
      255, 255, 0,   255, 255, 255, 255, 1,    //
      0,   255, 1,   255, 255, 255, 255, 2,    //
      255, 0,   1,   255, 255, 255, 255, 2,    //
      0,   1,   2,   255, 255, 255, 255, 3,    //
      255, 255, 255, 0,   255, 255, 255, 1,    //
      0,   255, 255, 1,   255, 255, 255, 2,    //
      255, 0,   255, 1,   255, 255, 255, 2,    //
      0,   1,   255, 2,   255, 255, 255, 3,    //
      255, 255, 0,   1,   255, 255, 255, 2,    //
      0,   255, 1,   2,   255, 255, 255, 3,    //
      255, 0,   1,   2,   255, 255, 255, 3,    //
      0,   1,   2,   3,   255, 255, 255, 4,    //
      255, 255, 255, 255, 0,   255, 255, 1,    //
      0,   255, 255, 255, 1,   255, 255, 2,    //
      255, 0,   255, 255, 1,   255, 255, 2,    //
      0,   1,   255, 255, 2,   255, 255, 3,    //
      255, 255, 0,   255, 1,   255, 255, 2,    //
      0,   255, 1,   255, 2,   255, 255, 3,    //
      255, 0,   1,   255, 2,   255, 255, 3,    //
      0,   1,   2,   255, 3,   255, 255, 4,    //
      255, 255, 255, 0,   1,   255, 255, 2,    //
      0,   255, 255, 1,   2,   255, 255, 3,    //
      255, 0,   255, 1,   2,   255, 255, 3,    //
      0,   1,   255, 2,   3,   255, 255, 4,    //
      255, 255, 0,   1,   2,   255, 255, 3,    //
      0,   255, 1,   2,   3,   255, 255, 4,    //
      255, 0,   1,   2,   3,   255, 255, 4,    //
      0,   1,   2,   3,   4,   255, 255, 5,    //
      255, 255, 255, 255, 255, 0,   255, 1,    //
      0,   255, 255, 255, 255, 1,   255, 2,    //
      255, 0,   255, 255, 255, 1,   255, 2,    //
      0,   1,   255, 255, 255, 2,   255, 3,    //
      255, 255, 0,   255, 255, 1,   255, 2,    //
      0,   255, 1,   255, 255, 2,   255, 3,    //
      255, 0,   1,   255, 255, 2,   255, 3,    //
      0,   1,   2,   255, 255, 3,   255, 4,    //
      255, 255, 255, 0,   255, 1,   255, 2,    //
      0,   255, 255, 1,   255, 2,   255, 3,    //
      255, 0,   255, 1,   255, 2,   255, 3,    //
      0,   1,   255, 2,   255, 3,   255, 4,    //
      255, 255, 0,   1,   255, 2,   255, 3,    //
      0,   255, 1,   2,   255, 3,   255, 4,    //
      255, 0,   1,   2,   255, 3,   255, 4,    //
      0,   1,   2,   3,   255, 4,   255, 5,    //
      255, 255, 255, 255, 0,   1,   255, 2,    //
      0,   255, 255, 255, 1,   2,   255, 3,    //
      255, 0,   255, 255, 1,   2,   255, 3,    //
      0,   1,   255, 255, 2,   3,   255, 4,    //
      255, 255, 0,   255, 1,   2,   255, 3,    //
      0,   255, 1,   255, 2,   3,   255, 4,    //
      255, 0,   1,   255, 2,   3,   255, 4,    //
      0,   1,   2,   255, 3,   4,   255, 5,    //
      255, 255, 255, 0,   1,   2,   255, 3,    //
      0,   255, 255, 1,   2,   3,   255, 4,    //
      255, 0,   255, 1,   2,   3,   255, 4,    //
      0,   1,   255, 2,   3,   4,   255, 5,    //
      255, 255, 0,   1,   2,   3,   255, 4,    //
      0,   255, 1,   2,   3,   4,   255, 5,    //
      255, 0,   1,   2,   3,   4,   255, 5,    //
      0,   1,   2,   3,   4,   5,   255, 6,    //
      255, 255, 255, 255, 255, 255, 0,   1,    //
      0,   255, 255, 255, 255, 255, 1,   2,    //
      255, 0,   255, 255, 255, 255, 1,   2,    //
      0,   1,   255, 255, 255, 255, 2,   3,    //
      255, 255, 0,   255, 255, 255, 1,   2,    //
      0,   255, 1,   255, 255, 255, 2,   3,    //
      255, 0,   1,   255, 255, 255, 2,   3,    //
      0,   1,   2,   255, 255, 255, 3,   4,    //
      255, 255, 255, 0,   255, 255, 1,   2,    //
      0,   255, 255, 1,   255, 255, 2,   3,    //
      255, 0,   255, 1,   255, 255, 2,   3,    //
      0,   1,   255, 2,   255, 255, 3,   4,    //
      255, 255, 0,   1,   255, 255, 2,   3,    //
      0,   255, 1,   2,   255, 255, 3,   4,    //
      255, 0,   1,   2,   255, 255, 3,   4,    //
      0,   1,   2,   3,   255, 255, 4,   5,    //
      255, 255, 255, 255, 0,   255, 1,   2,    //
      0,   255, 255, 255, 1,   255, 2,   3,    //
      255, 0,   255, 255, 1,   255, 2,   3,    //
      0,   1,   255, 255, 2,   255, 3,   4,    //
      255, 255, 0,   255, 1,   255, 2,   3,    //
      0,   255, 1,   255, 2,   255, 3,   4,    //
      255, 0,   1,   255, 2,   255, 3,   4,    //
      0,   1,   2,   255, 3,   255, 4,   5,    //
      255, 255, 255, 0,   1,   255, 2,   3,    //
      0,   255, 255, 1,   2,   255, 3,   4,    //
      255, 0,   255, 1,   2,   255, 3,   4,    //
      0,   1,   255, 2,   3,   255, 4,   5,    //
      255, 255, 0,   1,   2,   255, 3,   4,    //
      0,   255, 1,   2,   3,   255, 4,   5,    //
      255, 0,   1,   2,   3,   255, 4,   5,    //
      0,   1,   2,   3,   4,   255, 5,   6,    //
      255, 255, 255, 255, 255, 0,   1,   2,    //
      0,   255, 255, 255, 255, 1,   2,   3,    //
      255, 0,   255, 255, 255, 1,   2,   3,    //
      0,   1,   255, 255, 255, 2,   3,   4,    //
      255, 255, 0,   255, 255, 1,   2,   3,    //
      0,   255, 1,   255, 255, 2,   3,   4,    //
      255, 0,   1,   255, 255, 2,   3,   4,    //
      0,   1,   2,   255, 255, 3,   4,   5,    //
      255, 255, 255, 0,   255, 1,   2,   3,    //
      0,   255, 255, 1,   255, 2,   3,   4,    //
      255, 0,   255, 1,   255, 2,   3,   4,    //
      0,   1,   255, 2,   255, 3,   4,   5,    //
      255, 255, 0,   1,   255, 2,   3,   4,    //
      0,   255, 1,   2,   255, 3,   4,   5,    //
      255, 0,   1,   2,   255, 3,   4,   5,    //
      0,   1,   2,   3,   255, 4,   5,   6,    //
      255, 255, 255, 255, 0,   1,   2,   3,    //
      0,   255, 255, 255, 1,   2,   3,   4,    //
      255, 0,   255, 255, 1,   2,   3,   4,    //
      0,   1,   255, 255, 2,   3,   4,   5,    //
      255, 255, 0,   255, 1,   2,   3,   4,    //
      0,   255, 1,   255, 2,   3,   4,   5,    //
      255, 0,   1,   255, 2,   3,   4,   5,    //
      0,   1,   2,   255, 3,   4,   5,   6,    //
      255, 255, 255, 0,   1,   2,   3,   4,    //
      0,   255, 255, 1,   2,   3,   4,   5,    //
      255, 0,   255, 1,   2,   3,   4,   5,    //
      0,   1,   255, 2,   3,   4,   5,   6,    //
      255, 255, 0,   1,   2,   3,   4,   5,    //
      0,   255, 1,   2,   3,   4,   5,   6,    //
      255, 0,   1,   2,   3,   4,   5,   6,    //
      0,   1,   2,   3,   4,   5,   6,   7};
  const svuint16_t indices = PromoteTo(du16, Load(du8, table + offset));
  return TableLookupLanes(v, indices);  // already zeros mask=false lanes
#else
  return detail::ExpandLoop(v, mask);
#endif
}

template <class V, HWY_IF_T_SIZE_V(V, 4)>
HWY_API V Expand(V v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE_256 || HWY_IDE  // 32x8
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du32;
  // Convert mask into bitfield via horizontal sum (faster than ORV).
  const svuint32_t bits = Shl(Set(du32, 1), Iota(du32, 0));
  const size_t code = detail::SumOfLanesM(mask, bits);

  alignas(16) constexpr uint32_t packed_array[256] = {
      // PrintExpand32x8.
      0xffffffff, 0xfffffff0, 0xffffff0f, 0xffffff10, 0xfffff0ff, 0xfffff1f0,
      0xfffff10f, 0xfffff210, 0xffff0fff, 0xffff1ff0, 0xffff1f0f, 0xffff2f10,
      0xffff10ff, 0xffff21f0, 0xffff210f, 0xffff3210, 0xfff0ffff, 0xfff1fff0,
      0xfff1ff0f, 0xfff2ff10, 0xfff1f0ff, 0xfff2f1f0, 0xfff2f10f, 0xfff3f210,
      0xfff10fff, 0xfff21ff0, 0xfff21f0f, 0xfff32f10, 0xfff210ff, 0xfff321f0,
      0xfff3210f, 0xfff43210, 0xff0fffff, 0xff1ffff0, 0xff1fff0f, 0xff2fff10,
      0xff1ff0ff, 0xff2ff1f0, 0xff2ff10f, 0xff3ff210, 0xff1f0fff, 0xff2f1ff0,
      0xff2f1f0f, 0xff3f2f10, 0xff2f10ff, 0xff3f21f0, 0xff3f210f, 0xff4f3210,
      0xff10ffff, 0xff21fff0, 0xff21ff0f, 0xff32ff10, 0xff21f0ff, 0xff32f1f0,
      0xff32f10f, 0xff43f210, 0xff210fff, 0xff321ff0, 0xff321f0f, 0xff432f10,
      0xff3210ff, 0xff4321f0, 0xff43210f, 0xff543210, 0xf0ffffff, 0xf1fffff0,
      0xf1ffff0f, 0xf2ffff10, 0xf1fff0ff, 0xf2fff1f0, 0xf2fff10f, 0xf3fff210,
      0xf1ff0fff, 0xf2ff1ff0, 0xf2ff1f0f, 0xf3ff2f10, 0xf2ff10ff, 0xf3ff21f0,
      0xf3ff210f, 0xf4ff3210, 0xf1f0ffff, 0xf2f1fff0, 0xf2f1ff0f, 0xf3f2ff10,
      0xf2f1f0ff, 0xf3f2f1f0, 0xf3f2f10f, 0xf4f3f210, 0xf2f10fff, 0xf3f21ff0,
      0xf3f21f0f, 0xf4f32f10, 0xf3f210ff, 0xf4f321f0, 0xf4f3210f, 0xf5f43210,
      0xf10fffff, 0xf21ffff0, 0xf21fff0f, 0xf32fff10, 0xf21ff0ff, 0xf32ff1f0,
      0xf32ff10f, 0xf43ff210, 0xf21f0fff, 0xf32f1ff0, 0xf32f1f0f, 0xf43f2f10,
      0xf32f10ff, 0xf43f21f0, 0xf43f210f, 0xf54f3210, 0xf210ffff, 0xf321fff0,
      0xf321ff0f, 0xf432ff10, 0xf321f0ff, 0xf432f1f0, 0xf432f10f, 0xf543f210,
      0xf3210fff, 0xf4321ff0, 0xf4321f0f, 0xf5432f10, 0xf43210ff, 0xf54321f0,
      0xf543210f, 0xf6543210, 0x0fffffff, 0x1ffffff0, 0x1fffff0f, 0x2fffff10,
      0x1ffff0ff, 0x2ffff1f0, 0x2ffff10f, 0x3ffff210, 0x1fff0fff, 0x2fff1ff0,
      0x2fff1f0f, 0x3fff2f10, 0x2fff10ff, 0x3fff21f0, 0x3fff210f, 0x4fff3210,
      0x1ff0ffff, 0x2ff1fff0, 0x2ff1ff0f, 0x3ff2ff10, 0x2ff1f0ff, 0x3ff2f1f0,
      0x3ff2f10f, 0x4ff3f210, 0x2ff10fff, 0x3ff21ff0, 0x3ff21f0f, 0x4ff32f10,
      0x3ff210ff, 0x4ff321f0, 0x4ff3210f, 0x5ff43210, 0x1f0fffff, 0x2f1ffff0,
      0x2f1fff0f, 0x3f2fff10, 0x2f1ff0ff, 0x3f2ff1f0, 0x3f2ff10f, 0x4f3ff210,
      0x2f1f0fff, 0x3f2f1ff0, 0x3f2f1f0f, 0x4f3f2f10, 0x3f2f10ff, 0x4f3f21f0,
      0x4f3f210f, 0x5f4f3210, 0x2f10ffff, 0x3f21fff0, 0x3f21ff0f, 0x4f32ff10,
      0x3f21f0ff, 0x4f32f1f0, 0x4f32f10f, 0x5f43f210, 0x3f210fff, 0x4f321ff0,
      0x4f321f0f, 0x5f432f10, 0x4f3210ff, 0x5f4321f0, 0x5f43210f, 0x6f543210,
      0x10ffffff, 0x21fffff0, 0x21ffff0f, 0x32ffff10, 0x21fff0ff, 0x32fff1f0,
      0x32fff10f, 0x43fff210, 0x21ff0fff, 0x32ff1ff0, 0x32ff1f0f, 0x43ff2f10,
      0x32ff10ff, 0x43ff21f0, 0x43ff210f, 0x54ff3210, 0x21f0ffff, 0x32f1fff0,
      0x32f1ff0f, 0x43f2ff10, 0x32f1f0ff, 0x43f2f1f0, 0x43f2f10f, 0x54f3f210,
      0x32f10fff, 0x43f21ff0, 0x43f21f0f, 0x54f32f10, 0x43f210ff, 0x54f321f0,
      0x54f3210f, 0x65f43210, 0x210fffff, 0x321ffff0, 0x321fff0f, 0x432fff10,
      0x321ff0ff, 0x432ff1f0, 0x432ff10f, 0x543ff210, 0x321f0fff, 0x432f1ff0,
      0x432f1f0f, 0x543f2f10, 0x432f10ff, 0x543f21f0, 0x543f210f, 0x654f3210,
      0x3210ffff, 0x4321fff0, 0x4321ff0f, 0x5432ff10, 0x4321f0ff, 0x5432f1f0,
      0x5432f10f, 0x6543f210, 0x43210fff, 0x54321ff0, 0x54321f0f, 0x65432f10,
      0x543210ff, 0x654321f0, 0x6543210f, 0x76543210};

  // For lane i, shift the i-th 4-bit index down and mask with 0xF because
  // svtbl zeros outputs if the index is out of bounds.
  const svuint32_t packed = Set(du32, packed_array[code]);
  const svuint32_t indices = detail::AndN(Shr(packed, svindex_u32(0, 4)), 0xF);
  return TableLookupLanes(v, indices);  // already zeros mask=false lanes
#elif HWY_TARGET == HWY_SVE2_128        // 32x4
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du32;
  // Convert mask into bitfield via horizontal sum (faster than ORV).
  const svuint32_t bits = Shl(Set(du32, 1), Iota(du32, 0));
  const size_t offset = detail::SumOfLanesM(mask, bits);

  alignas(16) constexpr uint32_t packed_array[16] = {
      // PrintExpand64x4Nibble - same for 32x4.
      0x0000ffff, 0x0000fff0, 0x0000ff0f, 0x0000ff10, 0x0000f0ff, 0x0000f1f0,
      0x0000f10f, 0x0000f210, 0x00000fff, 0x00001ff0, 0x00001f0f, 0x00002f10,
      0x000010ff, 0x000021f0, 0x0000210f, 0x00003210};

  // For lane i, shift the i-th 4-bit index down and mask with 0xF because
  // svtbl zeros outputs if the index is out of bounds.
  const svuint32_t packed = Set(du32, packed_array[offset]);
  const svuint32_t indices = detail::AndN(Shr(packed, svindex_u32(0, 4)), 0xF);
  return TableLookupLanes(v, indices);  // already zeros mask=false lanes
#else
  return detail::ExpandLoop(v, mask);
#endif
}

template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_API V Expand(V v, svbool_t mask) {
#if HWY_TARGET == HWY_SVE_256 || HWY_IDE  // 64x4
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du64;

  // Convert mask into bitfield via horizontal sum (faster than ORV) of masked
  // bits 1, 2, 4, 8. Pre-multiply by N so we can use it as an offset for
  // SetTableIndices.
  const svuint64_t bits = Shl(Set(du64, 1), Iota(du64, 2));
  const size_t offset = detail::SumOfLanesM(mask, bits);

  alignas(16) static constexpr uint64_t table[4 * 16] = {
      // PrintExpand64x4Tables - small enough to store uncompressed.
      255, 255, 255, 255, 0, 255, 255, 255, 255, 0, 255, 255, 0, 1, 255, 255,
      255, 255, 0,   255, 0, 255, 1,   255, 255, 0, 1,   255, 0, 1, 2,   255,
      255, 255, 255, 0,   0, 255, 255, 1,   255, 0, 255, 1,   0, 1, 255, 2,
      255, 255, 0,   1,   0, 255, 1,   2,   255, 0, 1,   2,   0, 1, 2,   3};
  // This already zeros mask=false lanes.
  return TableLookupLanes(v, SetTableIndices(d, table + offset));
#elif HWY_TARGET == HWY_SVE2_128  // 64x2
  // Same as Compress, just zero out the mask=false lanes.
  return IfThenElseZero(mask, Compress(v, mask));
#else
  return detail::ExpandLoop(v, mask);
#endif
}

// ------------------------------ LoadExpand

template <class D>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
  return Expand(LoadU(d, unaligned), mask);
}

// ------------------------------ MulEven (InterleaveEven)

#if HWY_SVE_HAVE_2
namespace detail {
#define HWY_SVE_MUL_EVEN(BASE, CHAR, BITS, HALF, NAME, OP)     \
  HWY_API HWY_SVE_V(BASE, BITS)                                \
      NAME(HWY_SVE_V(BASE, HALF) a, HWY_SVE_V(BASE, HALF) b) { \
    return sv##OP##_##CHAR##BITS(a, b);                        \
  }

HWY_SVE_FOREACH_UI16(HWY_SVE_MUL_EVEN, MulEvenNative, mullb)
HWY_SVE_FOREACH_UI32(HWY_SVE_MUL_EVEN, MulEvenNative, mullb)
HWY_SVE_FOREACH_UI64(HWY_SVE_MUL_EVEN, MulEvenNative, mullb)
HWY_SVE_FOREACH_UI16(HWY_SVE_MUL_EVEN, MulOddNative, mullt)
HWY_SVE_FOREACH_UI32(HWY_SVE_MUL_EVEN, MulOddNative, mullt)
HWY_SVE_FOREACH_UI64(HWY_SVE_MUL_EVEN, MulOddNative, mullt)
#undef HWY_SVE_MUL_EVEN
}  // namespace detail
#endif

template <class V, class DW = RepartitionToWide<DFromV<V>>,
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<DW> MulEven(const V a, const V b) {
#if HWY_SVE_HAVE_2
  return BitCast(DW(), detail::MulEvenNative(a, b));
#else
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return BitCast(DW(), detail::InterleaveEven(lo, hi));
#endif
}

template <class V, class DW = RepartitionToWide<DFromV<V>>,
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API VFromD<DW> MulOdd(const V a, const V b) {
#if HWY_SVE_HAVE_2
  return BitCast(DW(), detail::MulOddNative(a, b));
#else
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return BitCast(DW(), detail::InterleaveOdd(lo, hi));
#endif
}

HWY_API svint64_t MulEven(const svint64_t a, const svint64_t b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return detail::InterleaveEven(lo, hi);
}

HWY_API svuint64_t MulEven(const svuint64_t a, const svuint64_t b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return detail::InterleaveEven(lo, hi);
}

HWY_API svint64_t MulOdd(const svint64_t a, const svint64_t b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return detail::InterleaveOdd(lo, hi);
}

HWY_API svuint64_t MulOdd(const svuint64_t a, const svuint64_t b) {
  const auto lo = Mul(a, b);
  const auto hi = MulHigh(a, b);
  return detail::InterleaveOdd(lo, hi);
}

// ------------------------------ WidenMulPairwiseAdd

template <size_t N, int kPow2>
HWY_API svfloat32_t WidenMulPairwiseAdd(Simd<float, N, kPow2> df, VBF16 a,
                                        VBF16 b) {
#if HWY_SVE_HAVE_F32_TO_BF16C
  const svfloat32_t even = svbfmlalb_f32(Zero(df), a, b);
  return svbfmlalt_f32(even, a, b);
#else
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
#endif  // HWY_SVE_HAVE_BF16_FEATURE
}

template <size_t N, int kPow2>
HWY_API svint32_t WidenMulPairwiseAdd(Simd<int32_t, N, kPow2> d32, svint16_t a,
                                      svint16_t b) {
#if HWY_SVE_HAVE_2
  (void)d32;
  return svmlalt_s32(svmullb_s32(a, b), a, b);
#else
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b),
                Mul(PromoteOddTo(d32, a), PromoteOddTo(d32, b)));
#endif
}

template <size_t N, int kPow2>
HWY_API svuint32_t WidenMulPairwiseAdd(Simd<uint32_t, N, kPow2> d32,
                                       svuint16_t a, svuint16_t b) {
#if HWY_SVE_HAVE_2
  (void)d32;
  return svmlalt_u32(svmullb_u32(a, b), a, b);
#else
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b),
                Mul(PromoteOddTo(d32, a), PromoteOddTo(d32, b)));
#endif
}

// ------------------------------ SatWidenMulAccumFixedPoint

#if HWY_SVE_HAVE_2

#ifdef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#undef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#else
#define HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#endif

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SatWidenMulAccumFixedPoint(DI32 /*di32*/,
                                                VFromD<Rebind<int16_t, DI32>> a,
                                                VFromD<Rebind<int16_t, DI32>> b,
                                                VFromD<DI32> sum) {
  return svqdmlalb_s32(sum, detail::ZipLowerSame(a, a),
                       detail::ZipLowerSame(b, b));
}

#endif  // HWY_SVE_HAVE_2

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

#if HWY_SVE_HAVE_BF16_FEATURE

// NOTE: we currently do not use SVE BFDOT for bf16 ReorderWidenMulAccumulate
// because, apparently unlike NEON, it uses round to odd unless the additional
// FEAT_EBF16 feature is available and enabled.
#ifdef HWY_NATIVE_MUL_EVEN_BF16
#undef HWY_NATIVE_MUL_EVEN_BF16
#else
#define HWY_NATIVE_MUL_EVEN_BF16
#endif

template <size_t N, int kPow2>
HWY_API svfloat32_t MulEvenAdd(Simd<float, N, kPow2> /* d */, VBF16 a, VBF16 b,
                               const svfloat32_t c) {
  return svbfmlalb_f32(c, a, b);
}

template <size_t N, int kPow2>
HWY_API svfloat32_t MulOddAdd(Simd<float, N, kPow2> /* d */, VBF16 a, VBF16 b,
                              const svfloat32_t c) {
  return svbfmlalt_f32(c, a, b);
}

#endif  // HWY_SVE_HAVE_BF16_FEATURE

template <size_t N, int kPow2>
HWY_API svint32_t ReorderWidenMulAccumulate(Simd<int32_t, N, kPow2> d32,
                                            svint16_t a, svint16_t b,
                                            const svint32_t sum0,
                                            svint32_t& sum1) {
#if HWY_SVE_HAVE_2
  (void)d32;
  sum1 = svmlalt_s32(sum1, a, b);
  return svmlalb_s32(sum0, a, b);
#else
  // Lane order within sum0/1 is undefined, hence we can avoid the
  // longer-latency lane-crossing PromoteTo by using PromoteEvenTo.
  sum1 = MulAdd(PromoteOddTo(d32, a), PromoteOddTo(d32, b), sum1);
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b), sum0);
#endif
}

template <size_t N, int kPow2>
HWY_API svuint32_t ReorderWidenMulAccumulate(Simd<uint32_t, N, kPow2> d32,
                                             svuint16_t a, svuint16_t b,
                                             const svuint32_t sum0,
                                             svuint32_t& sum1) {
#if HWY_SVE_HAVE_2
  (void)d32;
  sum1 = svmlalt_u32(sum1, a, b);
  return svmlalb_u32(sum0, a, b);
#else
  // Lane order within sum0/1 is undefined, hence we can avoid the
  // longer-latency lane-crossing PromoteTo by using PromoteEvenTo.
  sum1 = MulAdd(PromoteOddTo(d32, a), PromoteOddTo(d32, b), sum1);
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b), sum0);
#endif
}

// ------------------------------ RearrangeToOddPlusEven
template <class VW>
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  // sum0 is the sum of bottom/even lanes and sum1 of top/odd lanes.
  return Add(sum0, sum1);
}

// ------------------------------ SumOfMulQuadAccumulate

#ifdef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(DI32 /*di32*/, svint8_t a,
                                            svint8_t b, svint32_t sum) {
  return svdot_s32(sum, a, b);
}

#ifdef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#endif

template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API VFromD<DU32> SumOfMulQuadAccumulate(DU32 /*du32*/, svuint8_t a,
                                            svuint8_t b, svuint32_t sum) {
  return svdot_u32(sum, a, b);
}

#ifdef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(DI32 di32, svuint8_t a_u,
                                            svint8_t b_i, svint32_t sum) {
  // TODO: use svusdot_u32 on SVE targets that require support for both SVE2
  // and SVE I8MM.

  const RebindToUnsigned<decltype(di32)> du32;
  const Repartition<uint8_t, decltype(di32)> du8;

  const auto b_u = BitCast(du8, b_i);
  const auto result_sum0 = svdot_u32(BitCast(du32, sum), a_u, b_u);
  const auto result_sum1 =
      ShiftLeft<8>(svdot_u32(Zero(du32), a_u, ShiftRight<7>(b_u)));

  return BitCast(di32, Sub(result_sum0, result_sum1));
}

#ifdef HWY_NATIVE_I16_I16_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_I16_I16_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_I16_I16_SUMOFMULQUADACCUMULATE
#endif

template <class DI64, HWY_IF_I64_D(DI64)>
HWY_API VFromD<DI64> SumOfMulQuadAccumulate(DI64 /*di64*/, svint16_t a,
                                            svint16_t b, svint64_t sum) {
  return svdot_s64(sum, a, b);
}

#ifdef HWY_NATIVE_U16_U16_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U16_U16_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U16_U16_SUMOFMULQUADACCUMULATE
#endif

template <class DU64, HWY_IF_U64_D(DU64)>
HWY_API VFromD<DU64> SumOfMulQuadAccumulate(DU64 /*du64*/, svuint16_t a,
                                            svuint16_t b, svuint64_t sum) {
  return svdot_u64(sum, a, b);
}

// ------------------------------ AESRound / CLMul

// Static dispatch with -march=armv8-a+sve2+aes, or dynamic dispatch WITHOUT a
// baseline, in which case we check for AES support at runtime.
#if defined(__ARM_FEATURE_SVE2_AES) || \
    (HWY_SVE_HAVE_2 && HWY_HAVE_RUNTIME_DISPATCH && HWY_BASELINE_SVE2 == 0)

// Per-target flag to prevent generic_ops-inl.h from defining AESRound.
#ifdef HWY_NATIVE_AES
#undef HWY_NATIVE_AES
#else
#define HWY_NATIVE_AES
#endif

HWY_API svuint8_t AESRound(svuint8_t state, svuint8_t round_key) {
  // It is not clear whether E and MC fuse like they did on NEON.
  return Xor(svaesmc_u8(svaese_u8(state, svdup_n_u8(0))), round_key);
}

HWY_API svuint8_t AESLastRound(svuint8_t state, svuint8_t round_key) {
  return Xor(svaese_u8(state, svdup_n_u8(0)), round_key);
}

HWY_API svuint8_t AESInvMixColumns(svuint8_t state) {
  return svaesimc_u8(state);
}

HWY_API svuint8_t AESRoundInv(svuint8_t state, svuint8_t round_key) {
  return Xor(svaesimc_u8(svaesd_u8(state, svdup_n_u8(0))), round_key);
}

HWY_API svuint8_t AESLastRoundInv(svuint8_t state, svuint8_t round_key) {
  return Xor(svaesd_u8(state, svdup_n_u8(0)), round_key);
}

template <uint8_t kRcon>
HWY_API svuint8_t AESKeyGenAssist(svuint8_t v) {
  alignas(16) static constexpr uint8_t kRconXorMask[16] = {
      0, kRcon, 0, 0, 0, 0, 0, 0, 0, kRcon, 0, 0, 0, 0, 0, 0};
  alignas(16) static constexpr uint8_t kRotWordShuffle[16] = {
      0, 13, 10, 7, 1, 14, 11, 4, 8, 5, 2, 15, 9, 6, 3, 12};
  const DFromV<decltype(v)> d;
  const Repartition<uint32_t, decltype(d)> du32;
  const auto w13 = BitCast(d, DupOdd(BitCast(du32, v)));
  const auto sub_word_result = AESLastRound(w13, LoadDup128(d, kRconXorMask));
  return TableLookupBytes(sub_word_result, LoadDup128(d, kRotWordShuffle));
}

HWY_API svuint64_t CLMulLower(const svuint64_t a, const svuint64_t b) {
  return svpmullb_pair(a, b);
}

HWY_API svuint64_t CLMulUpper(const svuint64_t a, const svuint64_t b) {
  return svpmullt_pair(a, b);
}

#endif  // __ARM_FEATURE_SVE2_AES

// ------------------------------ Lt128

namespace detail {
#define HWY_SVE_DUP(BASE, CHAR, BITS, HALF, NAME, OP)                        \
  template <size_t N, int kPow2>                                             \
  HWY_API svbool_t NAME(HWY_SVE_D(BASE, BITS, N, kPow2) /*d*/, svbool_t m) { \
    return sv##OP##_b##BITS(m, m);                                           \
  }

HWY_SVE_FOREACH_U(HWY_SVE_DUP, DupEvenB, trn1)  // actually for bool
HWY_SVE_FOREACH_U(HWY_SVE_DUP, DupOddB, trn2)   // actually for bool
#undef HWY_SVE_DUP

#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
template <class D>
HWY_INLINE svuint64_t Lt128Vec(D d, const svuint64_t a, const svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t eqHx = Eq(a, b);  // only odd lanes used
  // Convert to vector: more pipelines can execute vector TRN* instructions
  // than the predicate version.
  const svuint64_t ltHL = VecFromMask(d, Lt(a, b));
  // Move into upper lane: ltL if the upper half is equal, otherwise ltH.
  // Requires an extra IfThenElse because INSR, EXT, TRN2 are unpredicated.
  const svuint64_t ltHx = IfThenElse(eqHx, DupEven(ltHL), ltHL);
  // Duplicate upper lane into lower.
  return DupOdd(ltHx);
}
#endif
}  // namespace detail

template <class D>
HWY_INLINE svbool_t Lt128(D d, const svuint64_t a, const svuint64_t b) {
#if HWY_TARGET == HWY_SVE_256
  return MaskFromVec(detail::Lt128Vec(d, a, b));
#else
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t eqHx = Eq(a, b);  // only odd lanes used
  const svbool_t ltHL = Lt(a, b);
  // Move into upper lane: ltL if the upper half is equal, otherwise ltH.
  const svbool_t ltHx = svsel_b(eqHx, detail::DupEvenB(d, ltHL), ltHL);
  // Duplicate upper lane into lower.
  return detail::DupOddB(d, ltHx);
#endif  // HWY_TARGET != HWY_SVE_256
}

// ------------------------------ Lt128Upper

template <class D>
HWY_INLINE svbool_t Lt128Upper(D d, svuint64_t a, svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t ltHL = Lt(a, b);
  return detail::DupOddB(d, ltHL);
}

// ------------------------------ Eq128, Ne128

#if HWY_TARGET == HWY_SVE_256 || HWY_IDE
namespace detail {

template <class D>
HWY_INLINE svuint64_t Eq128Vec(D d, const svuint64_t a, const svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  // Convert to vector: more pipelines can execute vector TRN* instructions
  // than the predicate version.
  const svuint64_t eqHL = VecFromMask(d, Eq(a, b));
  // Duplicate upper and lower.
  const svuint64_t eqHH = DupOdd(eqHL);
  const svuint64_t eqLL = DupEven(eqHL);
  return And(eqLL, eqHH);
}

template <class D>
HWY_INLINE svuint64_t Ne128Vec(D d, const svuint64_t a, const svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  // Convert to vector: more pipelines can execute vector TRN* instructions
  // than the predicate version.
  const svuint64_t neHL = VecFromMask(d, Ne(a, b));
  // Duplicate upper and lower.
  const svuint64_t neHH = DupOdd(neHL);
  const svuint64_t neLL = DupEven(neHL);
  return Or(neLL, neHH);
}

}  // namespace detail
#endif

template <class D>
HWY_INLINE svbool_t Eq128(D d, const svuint64_t a, const svuint64_t b) {
#if HWY_TARGET == HWY_SVE_256
  return MaskFromVec(detail::Eq128Vec(d, a, b));
#else
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t eqHL = Eq(a, b);
  const svbool_t eqHH = detail::DupOddB(d, eqHL);
  const svbool_t eqLL = detail::DupEvenB(d, eqHL);
  return And(eqLL, eqHH);
#endif  // HWY_TARGET != HWY_SVE_256
}

template <class D>
HWY_INLINE svbool_t Ne128(D d, const svuint64_t a, const svuint64_t b) {
#if HWY_TARGET == HWY_SVE_256
  return MaskFromVec(detail::Ne128Vec(d, a, b));
#else
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t neHL = Ne(a, b);
  const svbool_t neHH = detail::DupOddB(d, neHL);
  const svbool_t neLL = detail::DupEvenB(d, neHL);
  return Or(neLL, neHH);
#endif  // HWY_TARGET != HWY_SVE_256
}

// ------------------------------ Eq128Upper, Ne128Upper

template <class D>
HWY_INLINE svbool_t Eq128Upper(D d, svuint64_t a, svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t eqHL = Eq(a, b);
  return detail::DupOddB(d, eqHL);
}

template <class D>
HWY_INLINE svbool_t Ne128Upper(D d, svuint64_t a, svuint64_t b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
  const svbool_t neHL = Ne(a, b);
  return detail::DupOddB(d, neHL);
}

// ------------------------------ Min128, Max128 (Lt128)

template <class D>
HWY_INLINE svuint64_t Min128(D d, const svuint64_t a, const svuint64_t b) {
#if HWY_TARGET == HWY_SVE_256
  return IfVecThenElse(detail::Lt128Vec(d, a, b), a, b);
#else
  return IfThenElse(Lt128(d, a, b), a, b);
#endif
}

template <class D>
HWY_INLINE svuint64_t Max128(D d, const svuint64_t a, const svuint64_t b) {
#if HWY_TARGET == HWY_SVE_256
  return IfVecThenElse(detail::Lt128Vec(d, b, a), a, b);
#else
  return IfThenElse(Lt128(d, b, a), a, b);
#endif
}

template <class D>
HWY_INLINE svuint64_t Min128Upper(D d, const svuint64_t a, const svuint64_t b) {
  return IfThenElse(Lt128Upper(d, a, b), a, b);
}

template <class D>
HWY_INLINE svuint64_t Max128Upper(D d, const svuint64_t a, const svuint64_t b) {
  return IfThenElse(Lt128Upper(d, b, a), a, b);
}

// -------------------- LeadingZeroCount, TrailingZeroCount, HighestSetBitIndex

#ifdef HWY_NATIVE_LEADING_ZERO_COUNT
#undef HWY_NATIVE_LEADING_ZERO_COUNT
#else
#define HWY_NATIVE_LEADING_ZERO_COUNT
#endif

#define HWY_SVE_LEADING_ZERO_COUNT(BASE, CHAR, BITS, HALF, NAME, OP)   \
  HWY_API HWY_SVE_V(BASE, BITS) NAME(HWY_SVE_V(BASE, BITS) v) {        \
    const DFromV<decltype(v)> d;                                       \
    return BitCast(d, sv##OP##_##CHAR##BITS##_x(detail::PTrue(d), v)); \
  }

HWY_SVE_FOREACH_UI(HWY_SVE_LEADING_ZERO_COUNT, LeadingZeroCount, clz)
#undef HWY_SVE_LEADING_ZERO_COUNT

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V TrailingZeroCount(V v) {
  return LeadingZeroCount(ReverseBits(v));
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V HighestSetBitIndex(V v) {
  const DFromV<decltype(v)> d;
  using T = TFromD<decltype(d)>;
  return BitCast(d, Sub(Set(d, T{sizeof(T) * 8 - 1}), LeadingZeroCount(v)));
}

// ================================================== END MACROS
#undef HWY_SVE_ALL_PTRUE
#undef HWY_SVE_D
#undef HWY_SVE_FOREACH
#undef HWY_SVE_FOREACH_BF16
#undef HWY_SVE_FOREACH_BF16_UNCONDITIONAL
#undef HWY_SVE_FOREACH_F
#undef HWY_SVE_FOREACH_F16
#undef HWY_SVE_FOREACH_F32
#undef HWY_SVE_FOREACH_F3264
#undef HWY_SVE_FOREACH_F64
#undef HWY_SVE_FOREACH_I
#undef HWY_SVE_FOREACH_I08
#undef HWY_SVE_FOREACH_I16
#undef HWY_SVE_FOREACH_I32
#undef HWY_SVE_FOREACH_I64
#undef HWY_SVE_FOREACH_IF
#undef HWY_SVE_FOREACH_U
#undef HWY_SVE_FOREACH_U08
#undef HWY_SVE_FOREACH_U16
#undef HWY_SVE_FOREACH_U32
#undef HWY_SVE_FOREACH_U64
#undef HWY_SVE_FOREACH_UI
#undef HWY_SVE_FOREACH_UI08
#undef HWY_SVE_FOREACH_UI16
#undef HWY_SVE_FOREACH_UI32
#undef HWY_SVE_FOREACH_UI64
#undef HWY_SVE_FOREACH_UIF3264
#undef HWY_SVE_HAVE_2
#undef HWY_SVE_IF_EMULATED_D
#undef HWY_SVE_IF_NOT_EMULATED_D
#undef HWY_SVE_PTRUE
#undef HWY_SVE_RETV_ARGMVV
#undef HWY_SVE_RETV_ARGPV
#undef HWY_SVE_RETV_ARGPVN
#undef HWY_SVE_RETV_ARGPVV
#undef HWY_SVE_RETV_ARGV
#undef HWY_SVE_RETV_ARGVN
#undef HWY_SVE_RETV_ARGVV
#undef HWY_SVE_RETV_ARGVVV
#undef HWY_SVE_T
#undef HWY_SVE_UNDEFINED
#undef HWY_SVE_V

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
