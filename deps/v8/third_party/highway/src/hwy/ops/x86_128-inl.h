// Copyright 2019 Google LLC
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

// 128-bit vectors and SSE4 instructions, plus some AVX2 and AVX512-VL
// operations when compiling for those targets.
// External include guard in highway.h - see comment there.

// Must come before HWY_DIAGNOSTICS and HWY_COMPILER_GCC_ACTUAL
#include "hwy/base.h"

// Avoid uninitialized warnings in GCC's emmintrin.h - see
// https://github.com/google/highway/issues/710 and pull/902
HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_GCC_ACTUAL
HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wuninitialized")
HWY_DIAGNOSTICS_OFF(disable : 4701 4703 6001 26494,
                    ignored "-Wmaybe-uninitialized")
#endif

#include <emmintrin.h>
#include <stdio.h>
#if HWY_TARGET == HWY_SSSE3
#include <tmmintrin.h>  // SSSE3
#elif HWY_TARGET <= HWY_SSE4
#include <smmintrin.h>  // SSE4
#ifndef HWY_DISABLE_PCLMUL_AES
#include <wmmintrin.h>  // CLMUL
#endif
#endif

#include "hwy/ops/shared-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace detail {

// Enable generic functions for whichever of (f16, bf16) are not supported.
#if !HWY_HAVE_FLOAT16
#define HWY_X86_IF_EMULATED_D(D) HWY_IF_SPECIAL_FLOAT_D(D)
#else
#define HWY_X86_IF_EMULATED_D(D) HWY_IF_BF16_D(D)
#endif

#undef HWY_AVX3_HAVE_F32_TO_BF16C
#if HWY_TARGET <= HWY_AVX3_ZEN4 && !HWY_COMPILER_CLANGCL &&           \
    (HWY_COMPILER_GCC_ACTUAL >= 1000 || HWY_COMPILER_CLANG >= 900) && \
    !defined(HWY_AVX3_DISABLE_AVX512BF16)
#define HWY_AVX3_HAVE_F32_TO_BF16C 1
#else
#define HWY_AVX3_HAVE_F32_TO_BF16C 0
#endif

#undef HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT
#if HWY_TARGET <= HWY_AVX3 && HWY_ARCH_X86_64
#define HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT "v"
#else
#define HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT "x"
#endif

template <typename T>
struct Raw128 {
  using type = __m128i;
};
#if HWY_HAVE_FLOAT16
template <>
struct Raw128<float16_t> {
  using type = __m128h;
};
#endif  // HWY_HAVE_FLOAT16
template <>
struct Raw128<float> {
  using type = __m128;
};
template <>
struct Raw128<double> {
  using type = __m128d;
};

}  // namespace detail

template <typename T, size_t N = 16 / sizeof(T)>
class Vec128 {
  using Raw = typename detail::Raw128<T>::type;

 public:
  using PrivateT = T;                     // only for DFromV
  static constexpr size_t kPrivateN = N;  // only for DFromV

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

#if HWY_TARGET <= HWY_AVX3

namespace detail {

// Template arg: sizeof(lane type)
template <size_t size>
struct RawMask128 {};
template <>
struct RawMask128<1> {
  using type = __mmask16;
};
template <>
struct RawMask128<2> {
  using type = __mmask8;
};
template <>
struct RawMask128<4> {
  using type = __mmask8;
};
template <>
struct RawMask128<8> {
  using type = __mmask8;
};

}  // namespace detail

template <typename T, size_t N = 16 / sizeof(T)>
struct Mask128 {
  using Raw = typename detail::RawMask128<sizeof(T)>::type;

  static Mask128<T, N> FromBits(uint64_t mask_bits) {
    return Mask128<T, N>{static_cast<Raw>(mask_bits)};
  }

  Raw raw;
};

#else  // AVX2 or below

// FF..FF or 0.
template <typename T, size_t N = 16 / sizeof(T)>
struct Mask128 {
  typename detail::Raw128<T>::type raw;
};

#endif  // AVX2 or below

namespace detail {

// Returns the lowest N of the _mm_movemask* bits.
template <typename T, size_t N>
constexpr uint64_t OnlyActive(uint64_t mask_bits) {
  return ((N * sizeof(T)) == 16) ? mask_bits : mask_bits & ((1ull << N) - 1);
}

}  // namespace detail

#if HWY_TARGET <= HWY_AVX3
namespace detail {

// Used by Expand() emulation, which is required for both AVX3 and AVX2.
template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(const Mask128<T, N> mask) {
  return OnlyActive<T, N>(mask.raw);
}

}  // namespace detail
#endif  // HWY_TARGET <= HWY_AVX3

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ Zero

// Use HWY_MAX_LANES_D here because VFromD is defined in terms of Zero.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<TFromD<D>, HWY_MAX_LANES_D(D)>{_mm_setzero_si128()};
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<float16_t, HWY_MAX_LANES_D(D)>{_mm_setzero_ph()};
}
#endif  // HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<float, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<float, HWY_MAX_LANES_D(D)>{_mm_setzero_ps()};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<double, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<double, HWY_MAX_LANES_D(D)>{_mm_setzero_pd()};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_X86_IF_EMULATED_D(D)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<TFromD<D>, HWY_MAX_LANES_D(D)>{_mm_setzero_si128()};
}

// Using the existing Zero function instead of a dedicated function for
// deduction avoids having to forward-declare Vec256 here.
template <class D>
using VFromD = decltype(Zero(D()));

// ------------------------------ BitCast

namespace detail {

HWY_INLINE __m128i BitCastToInteger(__m128i v) { return v; }
#if HWY_HAVE_FLOAT16
HWY_INLINE __m128i BitCastToInteger(__m128h v) { return _mm_castph_si128(v); }
#endif  // HWY_HAVE_FLOAT16
HWY_INLINE __m128i BitCastToInteger(__m128 v) { return _mm_castps_si128(v); }
HWY_INLINE __m128i BitCastToInteger(__m128d v) { return _mm_castpd_si128(v); }

#if HWY_AVX3_HAVE_F32_TO_BF16C
HWY_INLINE __m128i BitCastToInteger(__m128bh v) {
  // Need to use reinterpret_cast on GCC/Clang or BitCastScalar on MSVC to
  // bit cast a __m128bh to a __m128i as there is currently no intrinsic
  // available (as of GCC 13 and Clang 17) that can bit cast a __m128bh vector
  // to a __m128i vector

#if HWY_COMPILER_GCC || HWY_COMPILER_CLANG
  // On GCC or Clang, use reinterpret_cast to bit cast a __m128bh to a __m128i
  return reinterpret_cast<__m128i>(v);
#else
  // On MSVC, use BitCastScalar to bit cast a __m128bh to a __m128i as MSVC does
  // not allow reinterpret_cast, static_cast, or a C-style cast to be used to
  // bit cast from one SSE/AVX vector type to a different SSE/AVX vector type
  return BitCastScalar<__m128i>(v);
#endif  // HWY_COMPILER_GCC || HWY_COMPILER_CLANG
}
#endif  // HWY_AVX3_HAVE_F32_TO_BF16C

template <typename T, size_t N>
HWY_INLINE Vec128<uint8_t, N * sizeof(T)> BitCastToByte(Vec128<T, N> v) {
  return Vec128<uint8_t, N * sizeof(T)>{BitCastToInteger(v.raw)};
}

// Cannot rely on function overloading because return types differ.
template <typename T>
struct BitCastFromInteger128 {
  HWY_INLINE __m128i operator()(__m128i v) { return v; }
};
#if HWY_HAVE_FLOAT16
template <>
struct BitCastFromInteger128<float16_t> {
  HWY_INLINE __m128h operator()(__m128i v) { return _mm_castsi128_ph(v); }
};
#endif  // HWY_HAVE_FLOAT16
template <>
struct BitCastFromInteger128<float> {
  HWY_INLINE __m128 operator()(__m128i v) { return _mm_castsi128_ps(v); }
};
template <>
struct BitCastFromInteger128<double> {
  HWY_INLINE __m128d operator()(__m128i v) { return _mm_castsi128_pd(v); }
};

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE VFromD<D> BitCastFromByte(D /* tag */,
                                     Vec128<uint8_t, D().MaxBytes()> v) {
  return VFromD<D>{BitCastFromInteger128<TFromD<D>>()(v.raw)};
}

}  // namespace detail

template <class D, typename FromT, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> BitCast(D d,
                          Vec128<FromT, Repartition<FromT, D>().MaxLanes()> v) {
  return detail::BitCastFromByte(d, detail::BitCastToByte(v));
}

// ------------------------------ Set

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{_mm_set1_epi8(static_cast<char>(t))};  // NOLINT
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI16_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{_mm_set1_epi16(static_cast<short>(t))};  // NOLINT
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{_mm_set1_epi32(static_cast<int>(t))};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{_mm_set1_epi64x(static_cast<long long>(t))};  // NOLINT
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API VFromD<D> Set(D /* tag */, float16_t t) {
  return VFromD<D>{_mm_set1_ph(t)};
}
#endif  // HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> Set(D /* tag */, float t) {
  return VFromD<D>{_mm_set1_ps(t)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> Set(D /* tag */, double t) {
  return VFromD<D>{_mm_set1_pd(t)};
}

// Generic for all vector lengths.
template <class D, HWY_X86_IF_EMULATED_D(D)>
HWY_API VFromD<D> Set(D df, TFromD<D> t) {
  const RebindToUnsigned<decltype(df)> du;
  static_assert(sizeof(TFromD<D>) == 2, "Expecting [b]f16");
  uint16_t bits;
  CopyBytes<2>(&t, &bits);
  return BitCast(df, Set(du, bits));
}

// ------------------------------ Undefined

HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wuninitialized")

// Returns a vector with uninitialized elements.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API VFromD<D> Undefined(D /* tag */) {
  // Available on Clang 6.0, GCC 6.2, ICC 16.03, MSVC 19.14. All but ICC
  // generate an XOR instruction.
  return VFromD<D>{_mm_undefined_si128()};
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API VFromD<D> Undefined(D /* tag */) {
  return VFromD<D>{_mm_undefined_ph()};
}
#endif  // HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> Undefined(D /* tag */) {
  return VFromD<D>{_mm_undefined_ps()};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> Undefined(D /* tag */) {
  return VFromD<D>{_mm_undefined_pd()};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_X86_IF_EMULATED_D(D)>
HWY_API VFromD<D> Undefined(D /* tag */) {
  return VFromD<D>{_mm_undefined_si128()};
}

HWY_DIAGNOSTICS(pop)

// ------------------------------ GetLane

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API T GetLane(const Vec128<T, N> v) {
  return static_cast<T>(_mm_cvtsi128_si32(v.raw) & 0xFF);
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API T GetLane(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const uint16_t bits =
      static_cast<uint16_t>(_mm_cvtsi128_si32(BitCast(du, v).raw) & 0xFFFF);
  return BitCastScalar<T>(bits);
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API T GetLane(const Vec128<T, N> v) {
  return static_cast<T>(_mm_cvtsi128_si32(v.raw));
}
template <size_t N>
HWY_API float GetLane(const Vec128<float, N> v) {
  return _mm_cvtss_f32(v.raw);
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API T GetLane(const Vec128<T, N> v) {
#if HWY_ARCH_X86_32
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[2];
  Store(v, d, lanes);
  return lanes[0];
#else
  return static_cast<T>(_mm_cvtsi128_si64(v.raw));
#endif
}
template <size_t N>
HWY_API double GetLane(const Vec128<double, N> v) {
  return _mm_cvtsd_f64(v.raw);
}

// ------------------------------ ResizeBitCast

template <class D, class FromV, HWY_IF_V_SIZE_LE_V(FromV, 16),
          HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  const Repartition<uint8_t, decltype(d)> du8;
  return BitCast(d, VFromD<decltype(du8)>{detail::BitCastToInteger(v.raw)});
}

// ------------------------------ Dup128VecFromValues

template <class D, HWY_IF_UI8_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  return VFromD<D>{_mm_setr_epi8(
      static_cast<char>(t0), static_cast<char>(t1), static_cast<char>(t2),
      static_cast<char>(t3), static_cast<char>(t4), static_cast<char>(t5),
      static_cast<char>(t6), static_cast<char>(t7), static_cast<char>(t8),
      static_cast<char>(t9), static_cast<char>(t10), static_cast<char>(t11),
      static_cast<char>(t12), static_cast<char>(t13), static_cast<char>(t14),
      static_cast<char>(t15))};
}

template <class D, HWY_IF_UI16_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  return VFromD<D>{
      _mm_setr_epi16(static_cast<int16_t>(t0), static_cast<int16_t>(t1),
                     static_cast<int16_t>(t2), static_cast<int16_t>(t3),
                     static_cast<int16_t>(t4), static_cast<int16_t>(t5),
                     static_cast<int16_t>(t6), static_cast<int16_t>(t7))};
}

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

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_F16_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  return VFromD<D>{_mm_setr_ph(t0, t1, t2, t3, t4, t5, t6, t7)};
}
#else
// Generic for all vector lengths if HWY_HAVE_FLOAT16 is not true
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
#endif  // HWY_HAVE_FLOAT16

template <class D, HWY_IF_UI32_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return VFromD<D>{
      _mm_setr_epi32(static_cast<int32_t>(t0), static_cast<int32_t>(t1),
                     static_cast<int32_t>(t2), static_cast<int32_t>(t3))};
}

template <class D, HWY_IF_F32_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return VFromD<D>{_mm_setr_ps(t0, t1, t2, t3)};
}

template <class D, HWY_IF_UI64_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  // Need to use _mm_set_epi64x as there is no _mm_setr_epi64x intrinsic
  // available
  return VFromD<D>{
      _mm_set_epi64x(static_cast<int64_t>(t1), static_cast<int64_t>(t0))};
}

template <class D, HWY_IF_F64_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return VFromD<D>{_mm_setr_pd(t0, t1)};
}

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
namespace detail {

template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<1> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]);
}

template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<2> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]);
}

template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<4> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]);
}

template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<8> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]) &&
         __builtin_constant_p(v[4]) && __builtin_constant_p(v[5]) &&
         __builtin_constant_p(v[6]) && __builtin_constant_p(v[7]);
}

template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<16> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]) &&
         __builtin_constant_p(v[4]) && __builtin_constant_p(v[5]) &&
         __builtin_constant_p(v[6]) && __builtin_constant_p(v[7]) &&
         __builtin_constant_p(v[8]) && __builtin_constant_p(v[9]) &&
         __builtin_constant_p(v[10]) && __builtin_constant_p(v[11]) &&
         __builtin_constant_p(v[12]) && __builtin_constant_p(v[13]) &&
         __builtin_constant_p(v[14]) && __builtin_constant_p(v[15]);
}

#if HWY_TARGET <= HWY_AVX2
template <class RawV>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantRawX86Vec(
    hwy::SizeTag<32> /* num_of_lanes_tag*/, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]) &&
         __builtin_constant_p(v[4]) && __builtin_constant_p(v[5]) &&
         __builtin_constant_p(v[6]) && __builtin_constant_p(v[7]) &&
         __builtin_constant_p(v[8]) && __builtin_constant_p(v[9]) &&
         __builtin_constant_p(v[10]) && __builtin_constant_p(v[11]) &&
         __builtin_constant_p(v[12]) && __builtin_constant_p(v[13]) &&
         __builtin_constant_p(v[14]) && __builtin_constant_p(v[15]) &&
         __builtin_constant_p(v[16]) && __builtin_constant_p(v[17]) &&
         __builtin_constant_p(v[18]) && __builtin_constant_p(v[19]) &&
         __builtin_constant_p(v[20]) && __builtin_constant_p(v[21]) &&
         __builtin_constant_p(v[22]) && __builtin_constant_p(v[23]) &&
         __builtin_constant_p(v[24]) && __builtin_constant_p(v[25]) &&
         __builtin_constant_p(v[26]) && __builtin_constant_p(v[27]) &&
         __builtin_constant_p(v[28]) && __builtin_constant_p(v[29]) &&
         __builtin_constant_p(v[30]) && __builtin_constant_p(v[31]);
}
#endif

template <size_t kNumOfLanes, class V>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantX86Vec(
    hwy::SizeTag<kNumOfLanes> num_of_lanes_tag, V v) {
  using T = TFromV<V>;
#if HWY_HAVE_FLOAT16 && HWY_HAVE_SCALAR_F16_TYPE
  using F16VecLaneT = hwy::float16_t::Native;
#else
  using F16VecLaneT = uint16_t;
#endif
  using RawVecLaneT = If<hwy::IsSame<T, hwy::float16_t>(), F16VecLaneT,
                         If<hwy::IsSame<T, hwy::bfloat16_t>(), uint16_t, T>>;

  // Suppress the -Wignored-attributes warning that is emitted by
  // RemoveCvRef<decltype(v.raw)> with GCC
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4649, ignored "-Wignored-attributes")
  typedef RawVecLaneT GccRawVec
      __attribute__((__vector_size__(sizeof(RemoveCvRef<decltype(v.raw)>))));
  HWY_DIAGNOSTICS(pop)

  return IsConstantRawX86Vec(num_of_lanes_tag,
                             reinterpret_cast<GccRawVec>(v.raw));
}

template <class TTo, class V>
static HWY_INLINE HWY_MAYBE_UNUSED bool IsConstantX86VecForF2IConv(V v) {
  constexpr size_t kNumOfLanesInRawSrcVec =
      HWY_MAX(HWY_MAX_LANES_V(V), 16 / sizeof(TFromV<V>));
  constexpr size_t kNumOfLanesInRawResultVec =
      HWY_MAX(HWY_MAX_LANES_V(V), 16 / sizeof(TTo));
  constexpr size_t kNumOfLanesToCheck =
      HWY_MIN(kNumOfLanesInRawSrcVec, kNumOfLanesInRawResultVec);

  return IsConstantX86Vec(hwy::SizeTag<kNumOfLanesToCheck>(), v);
}

}  // namespace detail
#endif  // HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD

// ================================================== LOGICAL

// ------------------------------ And

template <typename T, size_t N>
HWY_API Vec128<T, N> And(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;  // for float16_t
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>{
                        _mm_and_si128(BitCast(du, a).raw, BitCast(du, b).raw)});
}
template <size_t N>
HWY_API Vec128<float, N> And(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_and_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> And(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{_mm_and_pd(a.raw, b.raw)};
}

// ------------------------------ AndNot

// Returns ~not_mask & mask.
template <typename T, size_t N>
HWY_API Vec128<T, N> AndNot(Vec128<T, N> not_mask, Vec128<T, N> mask) {
  const DFromV<decltype(mask)> d;  // for float16_t
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>{_mm_andnot_si128(
                        BitCast(du, not_mask).raw, BitCast(du, mask).raw)});
}
template <size_t N>
HWY_API Vec128<float, N> AndNot(Vec128<float, N> not_mask,
                                Vec128<float, N> mask) {
  return Vec128<float, N>{_mm_andnot_ps(not_mask.raw, mask.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> AndNot(Vec128<double, N> not_mask,
                                 Vec128<double, N> mask) {
  return Vec128<double, N>{_mm_andnot_pd(not_mask.raw, mask.raw)};
}

// ------------------------------ Or

template <typename T, size_t N>
HWY_API Vec128<T, N> Or(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;  // for float16_t
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>{
                        _mm_or_si128(BitCast(du, a).raw, BitCast(du, b).raw)});
}

template <size_t N>
HWY_API Vec128<float, N> Or(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_or_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Or(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{_mm_or_pd(a.raw, b.raw)};
}

// ------------------------------ Xor

template <typename T, size_t N>
HWY_API Vec128<T, N> Xor(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;  // for float16_t
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>{
                        _mm_xor_si128(BitCast(du, a).raw, BitCast(du, b).raw)});
}

template <size_t N>
HWY_API Vec128<float, N> Xor(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_xor_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Xor(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{_mm_xor_pd(a.raw, b.raw)};
}

// ------------------------------ Not
template <typename T, size_t N>
HWY_API Vec128<T, N> Not(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
  const __m128i vu = BitCast(du, v).raw;
  return BitCast(d, VU{_mm_ternarylogic_epi32(vu, vu, vu, 0x55)});
#else
  return Xor(v, BitCast(d, VU{_mm_set1_epi32(-1)}));
#endif
}

// ------------------------------ Xor3
template <typename T, size_t N>
HWY_API Vec128<T, N> Xor3(Vec128<T, N> x1, Vec128<T, N> x2, Vec128<T, N> x3) {
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
  const DFromV<decltype(x1)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const __m128i ret = _mm_ternarylogic_epi64(
      BitCast(du, x1).raw, BitCast(du, x2).raw, BitCast(du, x3).raw, 0x96);
  return BitCast(d, VU{ret});
#else
  return Xor(x1, Xor(x2, x3));
#endif
}

// ------------------------------ Or3
template <typename T, size_t N>
HWY_API Vec128<T, N> Or3(Vec128<T, N> o1, Vec128<T, N> o2, Vec128<T, N> o3) {
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
  const DFromV<decltype(o1)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const __m128i ret = _mm_ternarylogic_epi64(
      BitCast(du, o1).raw, BitCast(du, o2).raw, BitCast(du, o3).raw, 0xFE);
  return BitCast(d, VU{ret});
#else
  return Or(o1, Or(o2, o3));
#endif
}

// ------------------------------ OrAnd
template <typename T, size_t N>
HWY_API Vec128<T, N> OrAnd(Vec128<T, N> o, Vec128<T, N> a1, Vec128<T, N> a2) {
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
  const DFromV<decltype(o)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const __m128i ret = _mm_ternarylogic_epi64(
      BitCast(du, o).raw, BitCast(du, a1).raw, BitCast(du, a2).raw, 0xF8);
  return BitCast(d, VU{ret});
#else
  return Or(o, And(a1, a2));
#endif
}

// ------------------------------ IfVecThenElse
template <typename T, size_t N>
HWY_API Vec128<T, N> IfVecThenElse(Vec128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
  const DFromV<decltype(no)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  return BitCast(
      d, VU{_mm_ternarylogic_epi64(BitCast(du, mask).raw, BitCast(du, yes).raw,
                                   BitCast(du, no).raw, 0xCA)});
#else
  return IfThenElse(MaskFromVec(mask), yes, no);
#endif
}

// ------------------------------ BitwiseIfThenElse
#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN

#ifdef HWY_NATIVE_BITWISE_IF_THEN_ELSE
#undef HWY_NATIVE_BITWISE_IF_THEN_ELSE
#else
#define HWY_NATIVE_BITWISE_IF_THEN_ELSE
#endif

template <class V>
HWY_API V BitwiseIfThenElse(V mask, V yes, V no) {
  return IfVecThenElse(mask, yes, no);
}

#endif

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

// ------------------------------ PopulationCount

// 8/16 require BITALG, 32/64 require VPOPCNTDQ.
#if HWY_TARGET <= HWY_AVX3_DL

#ifdef HWY_NATIVE_POPCNT
#undef HWY_NATIVE_POPCNT
#else
#define HWY_NATIVE_POPCNT
#endif

namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<1> /* tag */,
                                        Vec128<T, N> v) {
  return Vec128<T, N>{_mm_popcnt_epi8(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<2> /* tag */,
                                        Vec128<T, N> v) {
  return Vec128<T, N>{_mm_popcnt_epi16(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<4> /* tag */,
                                        Vec128<T, N> v) {
  return Vec128<T, N>{_mm_popcnt_epi32(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> PopulationCount(hwy::SizeTag<8> /* tag */,
                                        Vec128<T, N> v) {
  return Vec128<T, N>{_mm_popcnt_epi64(v.raw)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> PopulationCount(Vec128<T, N> v) {
  return detail::PopulationCount(hwy::SizeTag<sizeof(T)>(), v);
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

// ================================================== SIGN

// ------------------------------ Neg

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Neg(hwy::FloatTag /*tag*/, const Vec128<T, N> v) {
  return Xor(v, SignBit(DFromV<decltype(v)>()));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Neg(hwy::SpecialTag /*tag*/, const Vec128<T, N> v) {
  return Xor(v, SignBit(DFromV<decltype(v)>()));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Neg(hwy::SignedTag /*tag*/, const Vec128<T, N> v) {
  return Zero(DFromV<decltype(v)>()) - v;
}

}  // namespace detail

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Neg(const Vec128<T, N> v) {
  return detail::Neg(hwy::TypeTag<T>(), v);
}

// ------------------------------ Floating-point Abs
// Generic for all vector lengths
template <class V, HWY_IF_FLOAT(TFromV<V>)>
HWY_API V Abs(V v) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  using TI = TFromD<decltype(di)>;
  return v & BitCast(d, Set(di, static_cast<TI>(~SignMask<TI>())));
}

// ------------------------------ CopySign
// Generic for all vector lengths.
template <class V>
HWY_API V CopySign(const V magn, const V sign) {
  static_assert(IsFloat<TFromV<V>>(), "Only makes sense for floating-point");

  const DFromV<decltype(magn)> d;
  const auto msb = SignBit(d);

  // Truth table for msb, magn, sign | bitwise msb ? sign : mag
  //                  0    0     0   |  0
  //                  0    0     1   |  0
  //                  0    1     0   |  1
  //                  0    1     1   |  1
  //                  1    0     0   |  0
  //                  1    0     1   |  1
  //                  1    1     0   |  0
  //                  1    1     1   |  1
  return BitwiseIfThenElse(msb, sign, magn);
}

// ------------------------------ CopySignToAbs
// Generic for all vector lengths.
template <class V>
HWY_API V CopySignToAbs(const V abs, const V sign) {
  const DFromV<decltype(abs)> d;
  return OrAnd(abs, SignBit(d), sign);
}

// ================================================== MASK

#if HWY_TARGET <= HWY_AVX3
// ------------------------------ MaskFromVec

namespace detail {

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> MaskFromVec(hwy::SizeTag<1> /*tag*/,
                                     const Vec128<T, N> v) {
  return Mask128<T, N>{_mm_movepi8_mask(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> MaskFromVec(hwy::SizeTag<2> /*tag*/,
                                     const Vec128<T, N> v) {
  return Mask128<T, N>{_mm_movepi16_mask(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> MaskFromVec(hwy::SizeTag<4> /*tag*/,
                                     const Vec128<T, N> v) {
  return Mask128<T, N>{_mm_movepi32_mask(v.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> MaskFromVec(hwy::SizeTag<8> /*tag*/,
                                     const Vec128<T, N> v) {
  return Mask128<T, N>{_mm_movepi64_mask(v.raw)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(const Vec128<T, N> v) {
  return detail::MaskFromVec(hwy::SizeTag<sizeof(T)>(), v);
}
// There do not seem to be native floating-point versions of these instructions.
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float16_t, N> MaskFromVec(const Vec128<float16_t, N> v) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  return Mask128<float16_t, N>{MaskFromVec(BitCast(di, v)).raw};
}
#endif
template <size_t N>
HWY_API Mask128<float, N> MaskFromVec(const Vec128<float, N> v) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  return Mask128<float, N>{MaskFromVec(BitCast(di, v)).raw};
}
template <size_t N>
HWY_API Mask128<double, N> MaskFromVec(const Vec128<double, N> v) {
  const RebindToSigned<DFromV<decltype(v)>> di;
  return Mask128<double, N>{MaskFromVec(BitCast(di, v)).raw};
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

// ------------------------------ MaskFalse (MFromD)

#ifdef HWY_NATIVE_MASK_FALSE
#undef HWY_NATIVE_MASK_FALSE
#else
#define HWY_NATIVE_MASK_FALSE
#endif

// Generic for all vector lengths
template <class D>
HWY_API MFromD<D> MaskFalse(D /*d*/) {
  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(0)};
}

// ------------------------------ IsNegative (MFromD)
#ifdef HWY_NATIVE_IS_NEGATIVE
#undef HWY_NATIVE_IS_NEGATIVE
#else
#define HWY_NATIVE_IS_NEGATIVE
#endif

// Generic for all vector lengths
template <class V, HWY_IF_NOT_UNSIGNED_V(V)>
HWY_API MFromD<DFromV<V>> IsNegative(V v) {
  return MaskFromVec(v);
}

// ------------------------------ PromoteMaskTo (MFromD)

#ifdef HWY_NATIVE_PROMOTE_MASK_TO
#undef HWY_NATIVE_PROMOTE_MASK_TO
#else
#define HWY_NATIVE_PROMOTE_MASK_TO
#endif

// AVX3 PromoteMaskTo is generic for all vector lengths
template <class DTo, class DFrom,
          HWY_IF_T_SIZE_GT_D(DTo, sizeof(TFromD<DFrom>)),
          class DFrom_2 = Rebind<TFromD<DFrom>, DTo>,
          hwy::EnableIf<IsSame<MFromD<DFrom>, MFromD<DFrom_2>>()>* = nullptr>
HWY_API MFromD<DTo> PromoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/,
                                  MFromD<DFrom> m) {
  return MFromD<DTo>{static_cast<decltype(MFromD<DTo>().raw)>(m.raw)};
}

// ------------------------------ DemoteMaskTo (MFromD)

#ifdef HWY_NATIVE_DEMOTE_MASK_TO
#undef HWY_NATIVE_DEMOTE_MASK_TO
#else
#define HWY_NATIVE_DEMOTE_MASK_TO
#endif

// AVX3 DemoteMaskTo is generic for all vector lengths
template <class DTo, class DFrom,
          HWY_IF_T_SIZE_LE_D(DTo, sizeof(TFromD<DFrom>) - 1),
          class DFrom_2 = Rebind<TFromD<DFrom>, DTo>,
          hwy::EnableIf<IsSame<MFromD<DFrom>, MFromD<DFrom_2>>()>* = nullptr>
HWY_API MFromD<DTo> DemoteMaskTo(DTo /*d_to*/, DFrom /*d_from*/,
                                 MFromD<DFrom> m) {
  return MFromD<DTo>{static_cast<decltype(MFromD<DTo>().raw)>(m.raw)};
}

// ------------------------------ CombineMasks (MFromD)

#ifdef HWY_NATIVE_COMBINE_MASKS
#undef HWY_NATIVE_COMBINE_MASKS
#else
#define HWY_NATIVE_COMBINE_MASKS
#endif

template <class D, HWY_IF_LANES_D(D, 2)>
HWY_API MFromD<D> CombineMasks(D /*d*/, MFromD<Half<D>> hi,
                               MFromD<Half<D>> lo) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const __mmask8 combined_mask = _kor_mask8(
      _kshiftli_mask8(static_cast<__mmask8>(hi.raw), 1),
      _kand_mask8(static_cast<__mmask8>(lo.raw), static_cast<__mmask8>(1)));
#else
  const auto combined_mask =
      (static_cast<unsigned>(hi.raw) << 1) | (lo.raw & 1);
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(combined_mask)};
}

template <class D, HWY_IF_LANES_D(D, 4)>
HWY_API MFromD<D> CombineMasks(D /*d*/, MFromD<Half<D>> hi,
                               MFromD<Half<D>> lo) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const __mmask8 combined_mask = _kor_mask8(
      _kshiftli_mask8(static_cast<__mmask8>(hi.raw), 2),
      _kand_mask8(static_cast<__mmask8>(lo.raw), static_cast<__mmask8>(3)));
#else
  const auto combined_mask =
      (static_cast<unsigned>(hi.raw) << 2) | (lo.raw & 3);
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(combined_mask)};
}

template <class D, HWY_IF_LANES_D(D, 8)>
HWY_API MFromD<D> CombineMasks(D /*d*/, MFromD<Half<D>> hi,
                               MFromD<Half<D>> lo) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const __mmask8 combined_mask = _kor_mask8(
      _kshiftli_mask8(static_cast<__mmask8>(hi.raw), 4),
      _kand_mask8(static_cast<__mmask8>(lo.raw), static_cast<__mmask8>(15)));
#else
  const auto combined_mask =
      (static_cast<unsigned>(hi.raw) << 4) | (lo.raw & 15u);
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(combined_mask)};
}

template <class D, HWY_IF_LANES_D(D, 16)>
HWY_API MFromD<D> CombineMasks(D /*d*/, MFromD<Half<D>> hi,
                               MFromD<Half<D>> lo) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const __mmask16 combined_mask = _mm512_kunpackb(
      static_cast<__mmask16>(hi.raw), static_cast<__mmask16>(lo.raw));
#else
  const auto combined_mask =
      ((static_cast<unsigned>(hi.raw) << 8) | (lo.raw & 0xFFu));
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(combined_mask)};
}

// ------------------------------ LowerHalfOfMask (MFromD)

#ifdef HWY_NATIVE_LOWER_HALF_OF_MASK
#undef HWY_NATIVE_LOWER_HALF_OF_MASK
#else
#define HWY_NATIVE_LOWER_HALF_OF_MASK
#endif

// Generic for all vector lengths
template <class D>
HWY_API MFromD<D> LowerHalfOfMask(D d, MFromD<Twice<D>> m) {
  using RawM = decltype(MFromD<D>().raw);
  constexpr size_t kN = MaxLanes(d);
  constexpr size_t kNumOfBitsInRawMask = sizeof(RawM) * 8;

  MFromD<D> result_mask{static_cast<RawM>(m.raw)};

  if (kN < kNumOfBitsInRawMask) {
    result_mask =
        And(result_mask, MFromD<D>{static_cast<RawM>((1ULL << kN) - 1)});
  }

  return result_mask;
}

// ------------------------------ UpperHalfOfMask (MFromD)

#ifdef HWY_NATIVE_UPPER_HALF_OF_MASK
#undef HWY_NATIVE_UPPER_HALF_OF_MASK
#else
#define HWY_NATIVE_UPPER_HALF_OF_MASK
#endif

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API MFromD<D> UpperHalfOfMask(D /*d*/, MFromD<Twice<D>> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const auto shifted_mask = _kshiftri_mask8(static_cast<__mmask8>(m.raw), 1);
#else
  const auto shifted_mask = static_cast<unsigned>(m.raw) >> 1;
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(shifted_mask)};
}

template <class D, HWY_IF_LANES_D(D, 2)>
HWY_API MFromD<D> UpperHalfOfMask(D /*d*/, MFromD<Twice<D>> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const auto shifted_mask = _kshiftri_mask8(static_cast<__mmask8>(m.raw), 2);
#else
  const auto shifted_mask = static_cast<unsigned>(m.raw) >> 2;
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(shifted_mask)};
}

template <class D, HWY_IF_LANES_D(D, 4)>
HWY_API MFromD<D> UpperHalfOfMask(D /*d*/, MFromD<Twice<D>> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const auto shifted_mask = _kshiftri_mask8(static_cast<__mmask8>(m.raw), 4);
#else
  const auto shifted_mask = static_cast<unsigned>(m.raw) >> 4;
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(shifted_mask)};
}

template <class D, HWY_IF_LANES_D(D, 8)>
HWY_API MFromD<D> UpperHalfOfMask(D /*d*/, MFromD<Twice<D>> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  const auto shifted_mask = _kshiftri_mask16(static_cast<__mmask16>(m.raw), 8);
#else
  const auto shifted_mask = static_cast<unsigned>(m.raw) >> 8;
#endif

  return MFromD<D>{static_cast<decltype(MFromD<D>().raw)>(shifted_mask)};
}

// ------------------------------ OrderedDemote2MasksTo (MFromD, CombineMasks)

#ifdef HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#undef HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#else
#define HWY_NATIVE_ORDERED_DEMOTE_2_MASKS_TO
#endif

// Generic for all vector lengths
template <class DTo, class DFrom,
          HWY_IF_T_SIZE_D(DTo, sizeof(TFromD<DFrom>) / 2),
          class DTo_2 = Repartition<TFromD<DTo>, DFrom>,
          hwy::EnableIf<IsSame<MFromD<DTo>, MFromD<DTo_2>>()>* = nullptr>
HWY_API MFromD<DTo> OrderedDemote2MasksTo(DTo d_to, DFrom /*d_from*/,
                                          MFromD<DFrom> a, MFromD<DFrom> b) {
  using MH = MFromD<Half<DTo>>;
  using RawMH = decltype(MH().raw);

  return CombineMasks(d_to, MH{static_cast<RawMH>(b.raw)},
                      MH{static_cast<RawMH>(a.raw)});
}

// ------------------------------ Slide mask up/down
#ifdef HWY_NATIVE_SLIDE_MASK
#undef HWY_NATIVE_SLIDE_MASK
#else
#define HWY_NATIVE_SLIDE_MASK
#endif

template <class D, HWY_IF_LANES_LE_D(D, 8)>
HWY_API MFromD<D> SlideMask1Up(D d, MFromD<D> m) {
  using RawM = decltype(MFromD<D>().raw);
  constexpr size_t kN = MaxLanes(d);
  constexpr unsigned kValidLanesMask = (1u << kN) - 1u;

#if HWY_COMPILER_HAS_MASK_INTRINSICS
  MFromD<D> result_mask{
      static_cast<RawM>(_kshiftli_mask8(static_cast<__mmask8>(m.raw), 1))};

  if (kN < 8) {
    result_mask =
        And(result_mask, MFromD<D>{static_cast<RawM>(kValidLanesMask)});
  }
#else
  MFromD<D> result_mask{
      static_cast<RawM>((static_cast<unsigned>(m.raw) << 1) & kValidLanesMask)};
#endif

  return result_mask;
}

template <class D, HWY_IF_LANES_D(D, 16)>
HWY_API MFromD<D> SlideMask1Up(D /*d*/, MFromD<D> m) {
  using RawM = decltype(MFromD<D>().raw);
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return MFromD<D>{
      static_cast<RawM>(_kshiftli_mask16(static_cast<__mmask16>(m.raw), 1))};
#else
  return MFromD<D>{static_cast<RawM>(static_cast<unsigned>(m.raw) << 1)};
#endif
}

template <class D, HWY_IF_LANES_LE_D(D, 8)>
HWY_API MFromD<D> SlideMask1Down(D d, MFromD<D> m) {
  using RawM = decltype(MFromD<D>().raw);
  constexpr size_t kN = MaxLanes(d);
  constexpr unsigned kValidLanesMask = (1u << kN) - 1u;

#if HWY_COMPILER_HAS_MASK_INTRINSICS
  if (kN < 8) {
    m = And(m, MFromD<D>{static_cast<RawM>(kValidLanesMask)});
  }

  return MFromD<D>{
      static_cast<RawM>(_kshiftri_mask8(static_cast<__mmask8>(m.raw), 1))};
#else
  return MFromD<D>{
      static_cast<RawM>((static_cast<unsigned>(m.raw) & kValidLanesMask) >> 1)};
#endif
}

template <class D, HWY_IF_LANES_D(D, 16)>
HWY_API MFromD<D> SlideMask1Down(D /*d*/, MFromD<D> m) {
  using RawM = decltype(MFromD<D>().raw);
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return MFromD<D>{
      static_cast<RawM>(_kshiftri_mask16(static_cast<__mmask16>(m.raw), 1))};
#else
  return MFromD<D>{
      static_cast<RawM>((static_cast<unsigned>(m.raw) & 0xFFFFu) >> 1)};
#endif
}

// Generic for all vector lengths
template <class D>
HWY_API MFromD<D> SlideMaskUpLanes(D d, MFromD<D> m, size_t amt) {
  using RawM = decltype(MFromD<D>().raw);
  constexpr size_t kN = MaxLanes(d);
  constexpr uint64_t kValidLanesMask =
      static_cast<uint64_t>(((kN < 64) ? (1ULL << kN) : 0ULL) - 1ULL);

  return MFromD<D>{static_cast<RawM>(
      (static_cast<uint64_t>(m.raw) << (amt & 63)) & kValidLanesMask)};
}

// Generic for all vector lengths
template <class D>
HWY_API MFromD<D> SlideMaskDownLanes(D d, MFromD<D> m, size_t amt) {
  using RawM = decltype(MFromD<D>().raw);
  constexpr size_t kN = MaxLanes(d);
  constexpr uint64_t kValidLanesMask =
      static_cast<uint64_t>(((kN < 64) ? (1ULL << kN) : 0ULL) - 1ULL);

  return MFromD<D>{static_cast<RawM>(
      (static_cast<uint64_t>(m.raw) & kValidLanesMask) >> (amt & 63))};
}

// ------------------------------ VecFromMask

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> VecFromMask(const Mask128<T, N> v) {
  return Vec128<T, N>{_mm_movm_epi8(v.raw)};
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Vec128<T, N> VecFromMask(const Mask128<T, N> v) {
  return Vec128<T, N>{_mm_movm_epi16(v.raw)};
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> VecFromMask(const Mask128<T, N> v) {
  return Vec128<T, N>{_mm_movm_epi32(v.raw)};
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> VecFromMask(const Mask128<T, N> v) {
  return Vec128<T, N>{_mm_movm_epi64(v.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> VecFromMask(const Mask128<float16_t, N> v) {
  return Vec128<float16_t, N>{_mm_castsi128_ph(_mm_movm_epi16(v.raw))};
}
#endif  // HWY_HAVE_FLOAT16

template <size_t N>
HWY_API Vec128<float, N> VecFromMask(const Mask128<float, N> v) {
  return Vec128<float, N>{_mm_castsi128_ps(_mm_movm_epi32(v.raw))};
}

template <size_t N>
HWY_API Vec128<double, N> VecFromMask(const Mask128<double, N> v) {
  return Vec128<double, N>{_mm_castsi128_pd(_mm_movm_epi64(v.raw))};
}

// Generic for all vector lengths.
template <class D>
HWY_API VFromD<D> VecFromMask(D /* tag */, MFromD<D> v) {
  return VecFromMask(v);
}

// ------------------------------ RebindMask (MaskFromVec)

template <typename TFrom, size_t NFrom, class DTo, HWY_IF_V_SIZE_LE_D(DTo, 16)>
HWY_API MFromD<DTo> RebindMask(DTo /* tag */, Mask128<TFrom, NFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TFromD<DTo>), "Must have same size");
  return MFromD<DTo>{m.raw};
}

// ------------------------------ IfThenElse

namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElse(hwy::SizeTag<1> /* tag */,
                                   Mask128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_blend_epi8(mask.raw, no.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElse(hwy::SizeTag<2> /* tag */,
                                   Mask128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_blend_epi16(mask.raw, no.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElse(hwy::SizeTag<4> /* tag */,
                                   Mask128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_blend_epi32(mask.raw, no.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElse(hwy::SizeTag<8> /* tag */,
                                   Mask128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_blend_epi64(mask.raw, no.raw, yes.raw)};
}

}  // namespace detail

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  return detail::IfThenElse(hwy::SizeTag<sizeof(T)>(), mask, yes, no);
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> IfThenElse(Mask128<float16_t, N> mask,
                                        Vec128<float16_t, N> yes,
                                        Vec128<float16_t, N> no) {
  return Vec128<float16_t, N>{_mm_mask_blend_ph(mask.raw, no.raw, yes.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// Generic for all vector lengths.
template <class V, class D = DFromV<V>, HWY_X86_IF_EMULATED_D(D)>
HWY_API V IfThenElse(MFromD<D> mask, V yes, V no) {
  const RebindToUnsigned<D> du;
  return BitCast(
      D(), IfThenElse(RebindMask(du, mask), BitCast(du, yes), BitCast(du, no)));
}

template <size_t N>
HWY_API Vec128<float, N> IfThenElse(Mask128<float, N> mask,
                                    Vec128<float, N> yes, Vec128<float, N> no) {
  return Vec128<float, N>{_mm_mask_blend_ps(mask.raw, no.raw, yes.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> IfThenElse(Mask128<double, N> mask,
                                     Vec128<double, N> yes,
                                     Vec128<double, N> no) {
  return Vec128<double, N>{_mm_mask_blend_pd(mask.raw, no.raw, yes.raw)};
}

namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElseZero(hwy::SizeTag<1> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> yes) {
  return Vec128<T, N>{_mm_maskz_mov_epi8(mask.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElseZero(hwy::SizeTag<2> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> yes) {
  return Vec128<T, N>{_mm_maskz_mov_epi16(mask.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElseZero(hwy::SizeTag<4> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> yes) {
  return Vec128<T, N>{_mm_maskz_mov_epi32(mask.raw, yes.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenElseZero(hwy::SizeTag<8> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> yes) {
  return Vec128<T, N>{_mm_maskz_mov_epi64(mask.raw, yes.raw)};
}

}  // namespace detail

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> IfThenElseZero(Mask128<T, N> mask, Vec128<T, N> yes) {
  return detail::IfThenElseZero(hwy::SizeTag<sizeof(T)>(), mask, yes);
}

template <size_t N>
HWY_API Vec128<float, N> IfThenElseZero(Mask128<float, N> mask,
                                        Vec128<float, N> yes) {
  return Vec128<float, N>{_mm_maskz_mov_ps(mask.raw, yes.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> IfThenElseZero(Mask128<double, N> mask,
                                         Vec128<double, N> yes) {
  return Vec128<double, N>{_mm_maskz_mov_pd(mask.raw, yes.raw)};
}

// Generic for all vector lengths.
template <class V, class D = DFromV<V>, HWY_IF_SPECIAL_FLOAT_D(D)>
HWY_API V IfThenElseZero(MFromD<D> mask, V yes) {
  const RebindToUnsigned<D> du;
  return BitCast(D(), IfThenElseZero(RebindMask(du, mask), BitCast(du, yes)));
}

namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenZeroElse(hwy::SizeTag<1> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> no) {
  // xor_epi8/16 are missing, but we have sub, which is just as fast for u8/16.
  return Vec128<T, N>{_mm_mask_sub_epi8(no.raw, mask.raw, no.raw, no.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenZeroElse(hwy::SizeTag<2> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_sub_epi16(no.raw, mask.raw, no.raw, no.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenZeroElse(hwy::SizeTag<4> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_xor_epi32(no.raw, mask.raw, no.raw, no.raw)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> IfThenZeroElse(hwy::SizeTag<8> /* tag */,
                                       Mask128<T, N> mask, Vec128<T, N> no) {
  return Vec128<T, N>{_mm_mask_xor_epi64(no.raw, mask.raw, no.raw, no.raw)};
}

}  // namespace detail

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> IfThenZeroElse(Mask128<T, N> mask, Vec128<T, N> no) {
  return detail::IfThenZeroElse(hwy::SizeTag<sizeof(T)>(), mask, no);
}

template <size_t N>
HWY_API Vec128<float, N> IfThenZeroElse(Mask128<float, N> mask,
                                        Vec128<float, N> no) {
  return Vec128<float, N>{_mm_mask_xor_ps(no.raw, mask.raw, no.raw, no.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> IfThenZeroElse(Mask128<double, N> mask,
                                         Vec128<double, N> no) {
  return Vec128<double, N>{_mm_mask_xor_pd(no.raw, mask.raw, no.raw, no.raw)};
}

// Generic for all vector lengths.
template <class V, class D = DFromV<V>, HWY_IF_SPECIAL_FLOAT_D(D)>
HWY_API V IfThenZeroElse(MFromD<D> mask, V no) {
  const RebindToUnsigned<D> du;
  return BitCast(D(), IfThenZeroElse(RebindMask(du, mask), BitCast(du, no)));
}

// ------------------------------ Mask logical

// For Clang and GCC, mask intrinsics (KORTEST) weren't added until recently.
#if !defined(HWY_COMPILER_HAS_MASK_INTRINSICS)
#if HWY_COMPILER_MSVC != 0 || HWY_COMPILER_GCC_ACTUAL >= 700 || \
    HWY_COMPILER_CLANG >= 800
#define HWY_COMPILER_HAS_MASK_INTRINSICS 1
#else
#define HWY_COMPILER_HAS_MASK_INTRINSICS 0
#endif
#endif  // HWY_COMPILER_HAS_MASK_INTRINSICS

namespace detail {

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> And(hwy::SizeTag<1> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kand_mask16(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask16>(a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> And(hwy::SizeTag<2> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kand_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> And(hwy::SizeTag<4> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kand_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> And(hwy::SizeTag<8> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kand_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw & b.raw)};
#endif
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> AndNot(hwy::SizeTag<1> /*tag*/, const Mask128<T, N> a,
                                const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kandn_mask16(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask16>(~a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> AndNot(hwy::SizeTag<2> /*tag*/, const Mask128<T, N> a,
                                const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kandn_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> AndNot(hwy::SizeTag<4> /*tag*/, const Mask128<T, N> a,
                                const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kandn_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~a.raw & b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> AndNot(hwy::SizeTag<8> /*tag*/, const Mask128<T, N> a,
                                const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kandn_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~a.raw & b.raw)};
#endif
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Or(hwy::SizeTag<1> /*tag*/, const Mask128<T, N> a,
                            const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kor_mask16(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask16>(a.raw | b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Or(hwy::SizeTag<2> /*tag*/, const Mask128<T, N> a,
                            const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw | b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Or(hwy::SizeTag<4> /*tag*/, const Mask128<T, N> a,
                            const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw | b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Or(hwy::SizeTag<8> /*tag*/, const Mask128<T, N> a,
                            const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw | b.raw)};
#endif
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Xor(hwy::SizeTag<1> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxor_mask16(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask16>(a.raw ^ b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Xor(hwy::SizeTag<2> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw ^ b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Xor(hwy::SizeTag<4> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw ^ b.raw)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Xor(hwy::SizeTag<8> /*tag*/, const Mask128<T, N> a,
                             const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(a.raw ^ b.raw)};
#endif
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> ExclusiveNeither(hwy::SizeTag<1> /*tag*/,
                                          const Mask128<T, N> a,
                                          const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxnor_mask16(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask16>(~(a.raw ^ b.raw) & 0xFFFF)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> ExclusiveNeither(hwy::SizeTag<2> /*tag*/,
                                          const Mask128<T, N> a,
                                          const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{_kxnor_mask8(a.raw, b.raw)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~(a.raw ^ b.raw) & 0xFF)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> ExclusiveNeither(hwy::SizeTag<4> /*tag*/,
                                          const Mask128<T, N> a,
                                          const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{static_cast<__mmask8>(_kxnor_mask8(a.raw, b.raw) & 0xF)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~(a.raw ^ b.raw) & 0xF)};
#endif
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> ExclusiveNeither(hwy::SizeTag<8> /*tag*/,
                                          const Mask128<T, N> a,
                                          const Mask128<T, N> b) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{static_cast<__mmask8>(_kxnor_mask8(a.raw, b.raw) & 0x3)};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~(a.raw ^ b.raw) & 0x3)};
#endif
}

// UnmaskedNot returns ~m.raw without zeroing out any invalid bits
template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE Mask128<T, N> UnmaskedNot(const Mask128<T, N> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{static_cast<__mmask16>(_knot_mask16(m.raw))};
#else
  return Mask128<T, N>{static_cast<__mmask16>(~m.raw)};
#endif
}

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_INLINE Mask128<T, N> UnmaskedNot(const Mask128<T, N> m) {
#if HWY_COMPILER_HAS_MASK_INTRINSICS
  return Mask128<T, N>{static_cast<__mmask8>(_knot_mask8(m.raw))};
#else
  return Mask128<T, N>{static_cast<__mmask8>(~m.raw)};
#endif
}

template <typename T>
HWY_INLINE Mask128<T> Not(hwy::SizeTag<1> /*tag*/, const Mask128<T> m) {
  // sizeof(T) == 1 and N == 16: simply return ~m as all 16 bits of m are valid
  return UnmaskedNot(m);
}
template <typename T, size_t N, HWY_IF_LANES_LE(N, 8)>
HWY_INLINE Mask128<T, N> Not(hwy::SizeTag<1> /*tag*/, const Mask128<T, N> m) {
  // sizeof(T) == 1 and N <= 8: need to zero out the upper bits of ~m as there
  // are fewer than 16 valid bits in m

  // Return (~m) & ((1ull << N) - 1)
  return AndNot(hwy::SizeTag<1>(), m, Mask128<T, N>::FromBits((1ull << N) - 1));
}
template <typename T>
HWY_INLINE Mask128<T> Not(hwy::SizeTag<2> /*tag*/, const Mask128<T> m) {
  // sizeof(T) == 2 and N == 8: simply return ~m as all 8 bits of m are valid
  return UnmaskedNot(m);
}
template <typename T, size_t N, HWY_IF_LANES_LE(N, 4)>
HWY_INLINE Mask128<T, N> Not(hwy::SizeTag<2> /*tag*/, const Mask128<T, N> m) {
  // sizeof(T) == 2 and N <= 4: need to zero out the upper bits of ~m as there
  // are fewer than 8 valid bits in m

  // Return (~m) & ((1ull << N) - 1)
  return AndNot(hwy::SizeTag<2>(), m, Mask128<T, N>::FromBits((1ull << N) - 1));
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Not(hwy::SizeTag<4> /*tag*/, const Mask128<T, N> m) {
  // sizeof(T) == 4: need to zero out the upper bits of ~m as there are at most
  // 4 valid bits in m

  // Return (~m) & ((1ull << N) - 1)
  return AndNot(hwy::SizeTag<4>(), m, Mask128<T, N>::FromBits((1ull << N) - 1));
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Not(hwy::SizeTag<8> /*tag*/, const Mask128<T, N> m) {
  // sizeof(T) == 8: need to zero out the upper bits of ~m as there are at most
  // 2 valid bits in m

  // Return (~m) & ((1ull << N) - 1)
  return AndNot(hwy::SizeTag<8>(), m, Mask128<T, N>::FromBits((1ull << N) - 1));
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Mask128<T, N> And(const Mask128<T, N> a, Mask128<T, N> b) {
  return detail::And(hwy::SizeTag<sizeof(T)>(), a, b);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> AndNot(const Mask128<T, N> a, Mask128<T, N> b) {
  return detail::AndNot(hwy::SizeTag<sizeof(T)>(), a, b);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Or(const Mask128<T, N> a, Mask128<T, N> b) {
  return detail::Or(hwy::SizeTag<sizeof(T)>(), a, b);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Xor(const Mask128<T, N> a, Mask128<T, N> b) {
  return detail::Xor(hwy::SizeTag<sizeof(T)>(), a, b);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(const Mask128<T, N> m) {
  // Flip only the valid bits
  return detail::Not(hwy::SizeTag<sizeof(T)>(), m);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> ExclusiveNeither(const Mask128<T, N> a, Mask128<T, N> b) {
  return detail::ExclusiveNeither(hwy::SizeTag<sizeof(T)>(), a, b);
}

#else  // AVX2 or below

// ------------------------------ Mask

// Mask and Vec are the same (true = FF..FF).
template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(const Vec128<T, N> v) {
  return Mask128<T, N>{v.raw};
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <typename T, size_t N>
HWY_API Vec128<T, N> VecFromMask(const Mask128<T, N> v) {
  return Vec128<T, N>{v.raw};
}

// Generic for all vector lengths.
template <class D>
HWY_API VFromD<D> VecFromMask(D /* tag */, MFromD<D> v) {
  return VecFromMask(v);
}

#if HWY_TARGET >= HWY_SSSE3

// mask ? yes : no
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  const auto vmask = VecFromMask(DFromV<decltype(no)>(), mask);
  return Or(And(vmask, yes), AndNot(vmask, no));
}

#else  // HWY_TARGET < HWY_SSSE3

// mask ? yes : no
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  return Vec128<T, N>{_mm_blendv_epi8(no.raw, yes.raw, mask.raw)};
}
template <size_t N>
HWY_API Vec128<float, N> IfThenElse(Mask128<float, N> mask,
                                    Vec128<float, N> yes, Vec128<float, N> no) {
  return Vec128<float, N>{_mm_blendv_ps(no.raw, yes.raw, mask.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> IfThenElse(Mask128<double, N> mask,
                                     Vec128<double, N> yes,
                                     Vec128<double, N> no) {
  return Vec128<double, N>{_mm_blendv_pd(no.raw, yes.raw, mask.raw)};
}

#endif  // HWY_TARGET >= HWY_SSSE3

// mask ? yes : 0
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElseZero(Mask128<T, N> mask, Vec128<T, N> yes) {
  return yes & VecFromMask(DFromV<decltype(yes)>(), mask);
}

// mask ? 0 : no
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenZeroElse(Mask128<T, N> mask, Vec128<T, N> no) {
  return AndNot(VecFromMask(DFromV<decltype(no)>(), mask), no);
}

// ------------------------------ Mask logical

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(const Mask128<T, N> m) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Not(VecFromMask(d, m)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> And(const Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(And(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> AndNot(const Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Or(const Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Or(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Xor(const Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Xor(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> ExclusiveNeither(const Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), Not(VecFromMask(d, b))));
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ ShiftLeft

template <int kBits, size_t N>
HWY_API Vec128<uint16_t, N> ShiftLeft(const Vec128<uint16_t, N> v) {
  return Vec128<uint16_t, N>{_mm_slli_epi16(v.raw, kBits)};
}

template <int kBits, size_t N>
HWY_API Vec128<uint32_t, N> ShiftLeft(const Vec128<uint32_t, N> v) {
  return Vec128<uint32_t, N>{_mm_slli_epi32(v.raw, kBits)};
}

template <int kBits, size_t N>
HWY_API Vec128<uint64_t, N> ShiftLeft(const Vec128<uint64_t, N> v) {
  return Vec128<uint64_t, N>{_mm_slli_epi64(v.raw, kBits)};
}

template <int kBits, size_t N>
HWY_API Vec128<int16_t, N> ShiftLeft(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{_mm_slli_epi16(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int32_t, N> ShiftLeft(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{_mm_slli_epi32(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int64_t, N> ShiftLeft(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{_mm_slli_epi64(v.raw, kBits)};
}

#if HWY_TARGET <= HWY_AVX3_DL

namespace detail {
template <typename T, size_t N>
HWY_API Vec128<T, N> GaloisAffine(
    Vec128<T, N> v, VFromD<Repartition<uint64_t, Simd<T, N, 0>>> matrix) {
  return Vec128<T, N>{_mm_gf2p8affine_epi64_epi8(v.raw, matrix.raw, 0)};
}
}  // namespace detail

#else  // HWY_TARGET > HWY_AVX3_DL

template <int kBits, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShiftLeft(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<T, N> shifted{ShiftLeft<kBits>(Vec128<MakeWide<T>>{v.raw}).raw};
  return kBits == 1
             ? (v + v)
             : (shifted & Set(d8, static_cast<T>((0xFF << kBits) & 0xFF)));
}

#endif  // HWY_TARGET > HWY_AVX3_DL

// ------------------------------ ShiftRight

template <int kBits, size_t N>
HWY_API Vec128<uint16_t, N> ShiftRight(const Vec128<uint16_t, N> v) {
  return Vec128<uint16_t, N>{_mm_srli_epi16(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint32_t, N> ShiftRight(const Vec128<uint32_t, N> v) {
  return Vec128<uint32_t, N>{_mm_srli_epi32(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint64_t, N> ShiftRight(const Vec128<uint64_t, N> v) {
  return Vec128<uint64_t, N>{_mm_srli_epi64(v.raw, kBits)};
}

template <int kBits, size_t N>
HWY_API Vec128<int16_t, N> ShiftRight(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{_mm_srai_epi16(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int32_t, N> ShiftRight(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{_mm_srai_epi32(v.raw, kBits)};
}

#if HWY_TARGET > HWY_AVX3_DL

template <int kBits, size_t N>
HWY_API Vec128<uint8_t, N> ShiftRight(const Vec128<uint8_t, N> v) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<uint8_t, N> shifted{
      ShiftRight<kBits>(Vec128<uint16_t>{v.raw}).raw};
  return shifted & Set(d8, 0xFF >> kBits);
}

template <int kBits, size_t N>
HWY_API Vec128<int8_t, N> ShiftRight(const Vec128<int8_t, N> v) {
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const auto shifted = BitCast(di, ShiftRight<kBits>(BitCast(du, v)));
  const auto shifted_sign = BitCast(di, Set(du, 0x80 >> kBits));
  return (shifted ^ shifted_sign) - shifted_sign;
}

#endif  // HWY_TARGET > HWY_AVX3_DL

// i64 is implemented after BroadcastSignBit.

// ================================================== MEMORY (1)

// Clang static analysis claims the memory immediately after a partial vector
// store is uninitialized, and also flags the input to partial loads (at least
// for loadl_pd) as "garbage". This is a false alarm because msan does not
// raise errors. We work around this by using CopyBytes instead of intrinsics,
// but only for the analyzer to avoid potentially bad code generation.
// Unfortunately __clang_analyzer__ was not defined for clang-tidy prior to v7.
#ifndef HWY_SAFE_PARTIAL_LOAD_STORE
#if defined(__clang_analyzer__) || \
    (HWY_COMPILER_CLANG != 0 && HWY_COMPILER_CLANG < 700)
#define HWY_SAFE_PARTIAL_LOAD_STORE 1
#else
#define HWY_SAFE_PARTIAL_LOAD_STORE 0
#endif
#endif  // HWY_SAFE_PARTIAL_LOAD_STORE

// ------------------------------ Load

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API VFromD<D> Load(D /* tag */, const TFromD<D>* HWY_RESTRICT aligned) {
  return VFromD<D>{_mm_load_si128(reinterpret_cast<const __m128i*>(aligned))};
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> Load(D, const float16_t* HWY_RESTRICT aligned) {
  return Vec128<float16_t>{_mm_load_ph(aligned)};
}
#endif  // HWY_HAVE_FLOAT16
// Generic for all vector lengths greater than or equal to 16 bytes.
template <class D, HWY_IF_V_SIZE_GT_D(D, 8), HWY_X86_IF_EMULATED_D(D)>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT aligned) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Load(du, detail::U16LanePointer(aligned)));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<float> Load(D /* tag */, const float* HWY_RESTRICT aligned) {
  return Vec128<float>{_mm_load_ps(aligned)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<double> Load(D /* tag */, const double* HWY_RESTRICT aligned) {
  return Vec128<double>{_mm_load_pd(aligned)};
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API VFromD<D> LoadU(D /* tag */, const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_loadu_si128(reinterpret_cast<const __m128i*>(p))};
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API Vec128<float16_t> LoadU(D, const float16_t* HWY_RESTRICT p) {
  return Vec128<float16_t>{_mm_loadu_ph(p)};
}
#endif  // HWY_HAVE_FLOAT16
// Generic for all vector lengths greater than or equal to 16 bytes.
template <class D, HWY_IF_V_SIZE_GT_D(D, 8), HWY_X86_IF_EMULATED_D(D)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, LoadU(du, detail::U16LanePointer(p)));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<float> LoadU(D /* tag */, const float* HWY_RESTRICT p) {
  return Vec128<float>{_mm_loadu_ps(p)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<double> LoadU(D /* tag */, const double* HWY_RESTRICT p) {
  return Vec128<double>{_mm_loadu_pd(p)};
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
#if HWY_SAFE_PARTIAL_LOAD_STORE
  __m128i v = _mm_setzero_si128();
  CopyBytes<8>(p, &v);  // not same size
#else
  const __m128i v = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
#endif
  return BitCast(d, VFromD<decltype(du)>{v});
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> Load(D /* tag */, const float* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  __m128 v = _mm_setzero_ps();
  CopyBytes<8>(p, &v);  // not same size
  return Vec64<float>{v};
#else
  const __m128 hi = _mm_setzero_ps();
  return Vec64<float>{_mm_loadl_pi(hi, reinterpret_cast<const __m64*>(p))};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F64_D(D)>
HWY_API Vec64<double> Load(D /* tag */, const double* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  __m128d v = _mm_setzero_pd();
  CopyBytes<8>(p, &v);  // not same size
  return Vec64<double>{v};
#else
  return Vec64<double>{_mm_load_sd(p)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API Vec32<float> Load(D /* tag */, const float* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  __m128 v = _mm_setzero_ps();
  CopyBytes<4>(p, &v);  // not same size
  return Vec32<float>{v};
#else
  return Vec32<float>{_mm_load_ss(p)};
#endif
}

// Any <= 32 bit except <float, 1>
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  // Clang ArgumentPromotionPass seems to break this code. We can unpoison
  // before SetTableIndices -> LoadU -> Load and the memory is poisoned again.
  detail::MaybeUnpoison(p, Lanes(d));

#if HWY_SAFE_PARTIAL_LOAD_STORE
  __m128i v = Zero(Full128<TFromD<decltype(du)>>()).raw;
  CopyBytes<d.MaxBytes()>(p, &v);  // not same size as VFromD
#else
  int32_t bits = 0;
  CopyBytes<d.MaxBytes()>(p, &bits);  // not same size as VFromD
  const __m128i v = _mm_cvtsi32_si128(bits);
#endif
  return BitCast(d, VFromD<decltype(du)>{v});
}

// For < 128 bit, LoadU == Load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  return Load(d, p);
}

// 128-bit SIMD => nothing to duplicate, same as an unaligned load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return LoadU(d, p);
}

// ------------------------------ Store

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API void Store(VFromD<D> v, D /* tag */, TFromD<D>* HWY_RESTRICT aligned) {
  _mm_store_si128(reinterpret_cast<__m128i*>(aligned), v.raw);
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API void Store(Vec128<float16_t> v, D, float16_t* HWY_RESTRICT aligned) {
  _mm_store_ph(aligned, v.raw);
}
#endif  // HWY_HAVE_FLOAT16
// Generic for all vector lengths greater than or equal to 16 bytes.
template <class D, HWY_IF_V_SIZE_GT_D(D, 8), HWY_X86_IF_EMULATED_D(D)>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  const RebindToUnsigned<decltype(d)> du;
  Store(BitCast(du, v), du, reinterpret_cast<uint16_t*>(aligned));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API void Store(Vec128<float> v, D /* tag */, float* HWY_RESTRICT aligned) {
  _mm_store_ps(aligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API void Store(Vec128<double> v, D /* tag */,
                   double* HWY_RESTRICT aligned) {
  _mm_store_pd(aligned, v.raw);
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_API void StoreU(VFromD<D> v, D /* tag */, TFromD<D>* HWY_RESTRICT p) {
  _mm_storeu_si128(reinterpret_cast<__m128i*>(p), v.raw);
}
#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API void StoreU(Vec128<float16_t> v, D, float16_t* HWY_RESTRICT p) {
  _mm_storeu_ph(p, v.raw);
}
#endif  // HWY_HAVE_FLOAT16
// Generic for all vector lengths greater than or equal to 16 bytes.
template <class D, HWY_IF_V_SIZE_GT_D(D, 8), HWY_X86_IF_EMULATED_D(D)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;
  StoreU(BitCast(du, v), du, reinterpret_cast<uint16_t*>(p));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API void StoreU(Vec128<float> v, D /* tag */, float* HWY_RESTRICT p) {
  _mm_storeu_ps(p, v.raw);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API void StoreU(Vec128<double> v, D /* tag */, double* HWY_RESTRICT p) {
  _mm_storeu_pd(p, v.raw);
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  (void)d;
  CopyBytes<8>(&v, p);  // not same size
#else
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  _mm_storel_epi64(reinterpret_cast<__m128i*>(p), BitCast(du, v).raw);
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API void Store(Vec64<float> v, D /* tag */, float* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  CopyBytes<8>(&v, p);  // not same size
#else
  _mm_storel_pi(reinterpret_cast<__m64*>(p), v.raw);
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F64_D(D)>
HWY_API void Store(Vec64<double> v, D /* tag */, double* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  CopyBytes<8>(&v, p);  // not same size
#else
  _mm_storel_pd(p, v.raw);
#endif
}

// Any <= 32 bit except <float, 1>
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  CopyBytes<d.MaxBytes()>(&v, p);  // not same size
}
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API void Store(Vec32<float> v, D /* tag */, float* HWY_RESTRICT p) {
#if HWY_SAFE_PARTIAL_LOAD_STORE
  CopyBytes<4>(&v, p);  // not same size
#else
  _mm_store_ss(p, v.raw);
#endif
}

// For < 128 bit, StoreU == Store.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Store(v, d, p);
}

// ================================================== SWIZZLE (1)

// ------------------------------ TableLookupBytes
template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytes(const Vec128<T, N> bytes,
                                        const Vec128<TI, NI> from) {
  const DFromV<decltype(from)> d;
  const Repartition<uint8_t, decltype(d)> du8;

  const DFromV<decltype(bytes)> d_bytes;
  const Repartition<uint8_t, decltype(d_bytes)> du8_bytes;
#if HWY_TARGET == HWY_SSE2
#if HWY_COMPILER_GCC_ACTUAL && HWY_HAS_BUILTIN(__builtin_shuffle)
  typedef uint8_t GccU8RawVectType __attribute__((__vector_size__(16)));
  (void)d;
  (void)du8;
  (void)d_bytes;
  (void)du8_bytes;
  return Vec128<TI, NI>{reinterpret_cast<typename detail::Raw128<TI>::type>(
      __builtin_shuffle(reinterpret_cast<GccU8RawVectType>(bytes.raw),
                        reinterpret_cast<GccU8RawVectType>(from.raw)))};
#else
  const Full128<uint8_t> du8_full;

  alignas(16) uint8_t result_bytes[16];
  alignas(16) uint8_t u8_bytes[16];
  alignas(16) uint8_t from_bytes[16];

  Store(Vec128<uint8_t>{BitCast(du8_bytes, bytes).raw}, du8_full, u8_bytes);
  Store(Vec128<uint8_t>{BitCast(du8, from).raw}, du8_full, from_bytes);

  for (int i = 0; i < 16; i++) {
    result_bytes[i] = u8_bytes[from_bytes[i] & 15];
  }

  return BitCast(d, VFromD<decltype(du8)>{Load(du8_full, result_bytes).raw});
#endif
#else  // SSSE3 or newer
  return BitCast(
      d, VFromD<decltype(du8)>{_mm_shuffle_epi8(BitCast(du8_bytes, bytes).raw,
                                                BitCast(du8, from).raw)});
#endif
}

// ------------------------------ TableLookupBytesOr0
// For all vector widths; x86 anyway zeroes if >= 0x80 on SSSE3/SSE4/AVX2/AVX3
template <class V, class VI>
HWY_API VI TableLookupBytesOr0(const V bytes, const VI from) {
#if HWY_TARGET == HWY_SSE2
  const DFromV<decltype(from)> d;
  const Repartition<int8_t, decltype(d)> di8;

  const auto di8_from = BitCast(di8, from);
  return BitCast(d, IfThenZeroElse(di8_from < Zero(di8),
                                   TableLookupBytes(bytes, di8_from)));
#else
  return TableLookupBytes(bytes, from);
#endif
}

// ------------------------------ Shuffles (ShiftRight, TableLookupBytes)

// Notation: let Vec128<int32_t> have lanes 3,2,1,0 (0 is least-significant).
// Shuffle0321 rotates one lane to the right (the previous least-significant
// lane is now most-significant). These could also be implemented via
// CombineShiftRightBytes but the shuffle_abcd notation is more convenient.

// Swap 32-bit halves in 64-bit halves.
template <typename T, size_t N>
HWY_API Vec128<T, N> Shuffle2301(const Vec128<T, N> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{_mm_shuffle_epi32(v.raw, 0xB1)};
}
template <size_t N>
HWY_API Vec128<float, N> Shuffle2301(const Vec128<float, N> v) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<float, N>{_mm_shuffle_ps(v.raw, v.raw, 0xB1)};
}

// These are used by generic_ops-inl to implement LoadInterleaved3. As with
// Intel's shuffle* intrinsics and InterleaveLower, the lower half of the output
// comes from the first argument.
namespace detail {

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo2301(const Vec32<T> a, const Vec32<T> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
#if HWY_TARGET == HWY_SSE2
  Vec32<uint16_t> ba_shuffled{
      _mm_shufflelo_epi16(ba.raw, _MM_SHUFFLE(3, 0, 3, 0))};
  return BitCast(d, Or(ShiftLeft<8>(ba_shuffled), ShiftRight<8>(ba_shuffled)));
#else
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx =
      BitCast(d2, Dup128VecFromValues(d2_u, 1, 0, 7, 6, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0));
  return Vec32<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo2301(const Vec64<T> a, const Vec64<T> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
#if HWY_TARGET == HWY_SSE2
  Vec64<uint32_t> ba_shuffled{
      _mm_shuffle_epi32(ba.raw, _MM_SHUFFLE(3, 0, 3, 0))};
  return Vec64<T>{
      _mm_shufflelo_epi16(ba_shuffled.raw, _MM_SHUFFLE(2, 3, 0, 1))};
#else
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx = BitCast(
      d2,
      Dup128VecFromValues(d2_u, 0x0302, 0x0100, 0x0f0e, 0x0d0c, 0, 0, 0, 0));
  return Vec64<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo2301(const Vec128<T> a, const Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const RebindToFloat<decltype(d)> df;
  constexpr int m = _MM_SHUFFLE(2, 3, 0, 1);
  return BitCast(d, Vec128<float>{_mm_shuffle_ps(BitCast(df, a).raw,
                                                 BitCast(df, b).raw, m)});
}

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo1230(const Vec32<T> a, const Vec32<T> b) {
  const DFromV<decltype(a)> d;
#if HWY_TARGET == HWY_SSE2
  const auto zero = Zero(d);
  const Rebind<int16_t, decltype(d)> di16;
  const Vec32<int16_t> a_shuffled{_mm_shufflelo_epi16(
      _mm_unpacklo_epi8(a.raw, zero.raw), _MM_SHUFFLE(3, 0, 3, 0))};
  const Vec32<int16_t> b_shuffled{_mm_shufflelo_epi16(
      _mm_unpacklo_epi8(b.raw, zero.raw), _MM_SHUFFLE(1, 2, 1, 2))};
  const auto ba_shuffled = Combine(di16, b_shuffled, a_shuffled);
  return Vec32<T>{_mm_packus_epi16(ba_shuffled.raw, ba_shuffled.raw)};
#else
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx =
      BitCast(d2, Dup128VecFromValues(d2_u, 0, 3, 6, 5, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0));
  return Vec32<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo1230(const Vec64<T> a, const Vec64<T> b) {
  const DFromV<decltype(a)> d;
#if HWY_TARGET == HWY_SSE2
  const Vec32<T> a_shuffled{
      _mm_shufflelo_epi16(a.raw, _MM_SHUFFLE(3, 0, 3, 0))};
  const Vec32<T> b_shuffled{
      _mm_shufflelo_epi16(b.raw, _MM_SHUFFLE(1, 2, 1, 2))};
  return Combine(d, b_shuffled, a_shuffled);
#else
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx = BitCast(
      d2,
      Dup128VecFromValues(d2_u, 0x0100, 0x0706, 0x0d0c, 0x0b0a, 0, 0, 0, 0));
  return Vec64<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo1230(const Vec128<T> a, const Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const RebindToFloat<decltype(d)> df;
  constexpr int m = _MM_SHUFFLE(1, 2, 3, 0);
  return BitCast(d, Vec128<float>{_mm_shuffle_ps(BitCast(df, a).raw,
                                                 BitCast(df, b).raw, m)});
}

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo3012(const Vec32<T> a, const Vec32<T> b) {
  const DFromV<decltype(a)> d;
#if HWY_TARGET == HWY_SSE2
  const auto zero = Zero(d);
  const Rebind<int16_t, decltype(d)> di16;
  const Vec32<int16_t> a_shuffled{_mm_shufflelo_epi16(
      _mm_unpacklo_epi8(a.raw, zero.raw), _MM_SHUFFLE(1, 2, 1, 2))};
  const Vec32<int16_t> b_shuffled{_mm_shufflelo_epi16(
      _mm_unpacklo_epi8(b.raw, zero.raw), _MM_SHUFFLE(3, 0, 3, 0))};
  const auto ba_shuffled = Combine(di16, b_shuffled, a_shuffled);
  return Vec32<T>{_mm_packus_epi16(ba_shuffled.raw, ba_shuffled.raw)};
#else
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx =
      BitCast(d2, Dup128VecFromValues(d2_u, 2, 1, 4, 7, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0));
  return Vec32<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo3012(const Vec64<T> a, const Vec64<T> b) {
  const DFromV<decltype(a)> d;
#if HWY_TARGET == HWY_SSE2
  const Vec32<T> a_shuffled{
      _mm_shufflelo_epi16(a.raw, _MM_SHUFFLE(1, 2, 1, 2))};
  const Vec32<T> b_shuffled{
      _mm_shufflelo_epi16(b.raw, _MM_SHUFFLE(3, 0, 3, 0))};
  return Combine(d, b_shuffled, a_shuffled);
#else
  const Twice<decltype(d)> d2;
  const auto ba = Combine(d2, b, a);
  const RebindToUnsigned<decltype(d2)> d2_u;
  const auto shuffle_idx = BitCast(
      d2,
      Dup128VecFromValues(d2_u, 0x0504, 0x0302, 0x0908, 0x0f0e, 0, 0, 0, 0));
  return Vec64<T>{TableLookupBytes(ba, shuffle_idx).raw};
#endif
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo3012(const Vec128<T> a, const Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const RebindToFloat<decltype(d)> df;
  constexpr int m = _MM_SHUFFLE(3, 0, 1, 2);
  return BitCast(d, Vec128<float>{_mm_shuffle_ps(BitCast(df, a).raw,
                                                 BitCast(df, b).raw, m)});
}

}  // namespace detail

// Swap 64-bit halves
HWY_API Vec128<uint32_t> Shuffle1032(const Vec128<uint32_t> v) {
  return Vec128<uint32_t>{_mm_shuffle_epi32(v.raw, 0x4E)};
}
HWY_API Vec128<int32_t> Shuffle1032(const Vec128<int32_t> v) {
  return Vec128<int32_t>{_mm_shuffle_epi32(v.raw, 0x4E)};
}
HWY_API Vec128<float> Shuffle1032(const Vec128<float> v) {
  return Vec128<float>{_mm_shuffle_ps(v.raw, v.raw, 0x4E)};
}
HWY_API Vec128<uint64_t> Shuffle01(const Vec128<uint64_t> v) {
  return Vec128<uint64_t>{_mm_shuffle_epi32(v.raw, 0x4E)};
}
HWY_API Vec128<int64_t> Shuffle01(const Vec128<int64_t> v) {
  return Vec128<int64_t>{_mm_shuffle_epi32(v.raw, 0x4E)};
}
HWY_API Vec128<double> Shuffle01(const Vec128<double> v) {
  return Vec128<double>{_mm_shuffle_pd(v.raw, v.raw, 1)};
}

// Rotate right 32 bits
HWY_API Vec128<uint32_t> Shuffle0321(const Vec128<uint32_t> v) {
  return Vec128<uint32_t>{_mm_shuffle_epi32(v.raw, 0x39)};
}
HWY_API Vec128<int32_t> Shuffle0321(const Vec128<int32_t> v) {
  return Vec128<int32_t>{_mm_shuffle_epi32(v.raw, 0x39)};
}
HWY_API Vec128<float> Shuffle0321(const Vec128<float> v) {
  return Vec128<float>{_mm_shuffle_ps(v.raw, v.raw, 0x39)};
}
// Rotate left 32 bits
HWY_API Vec128<uint32_t> Shuffle2103(const Vec128<uint32_t> v) {
  return Vec128<uint32_t>{_mm_shuffle_epi32(v.raw, 0x93)};
}
HWY_API Vec128<int32_t> Shuffle2103(const Vec128<int32_t> v) {
  return Vec128<int32_t>{_mm_shuffle_epi32(v.raw, 0x93)};
}
HWY_API Vec128<float> Shuffle2103(const Vec128<float> v) {
  return Vec128<float>{_mm_shuffle_ps(v.raw, v.raw, 0x93)};
}

// Reverse
HWY_API Vec128<uint32_t> Shuffle0123(const Vec128<uint32_t> v) {
  return Vec128<uint32_t>{_mm_shuffle_epi32(v.raw, 0x1B)};
}
HWY_API Vec128<int32_t> Shuffle0123(const Vec128<int32_t> v) {
  return Vec128<int32_t>{_mm_shuffle_epi32(v.raw, 0x1B)};
}
HWY_API Vec128<float> Shuffle0123(const Vec128<float> v) {
  return Vec128<float>{_mm_shuffle_ps(v.raw, v.raw, 0x1B)};
}

// ================================================== COMPARE

#if HWY_TARGET <= HWY_AVX3

// Comparisons set a mask bit to 1 if the condition is true, else 0.

// ------------------------------ TestBit

namespace detail {

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> TestBit(hwy::SizeTag<1> /*tag*/, const Vec128<T, N> v,
                                 const Vec128<T, N> bit) {
  return Mask128<T, N>{_mm_test_epi8_mask(v.raw, bit.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> TestBit(hwy::SizeTag<2> /*tag*/, const Vec128<T, N> v,
                                 const Vec128<T, N> bit) {
  return Mask128<T, N>{_mm_test_epi16_mask(v.raw, bit.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> TestBit(hwy::SizeTag<4> /*tag*/, const Vec128<T, N> v,
                                 const Vec128<T, N> bit) {
  return Mask128<T, N>{_mm_test_epi32_mask(v.raw, bit.raw)};
}
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> TestBit(hwy::SizeTag<8> /*tag*/, const Vec128<T, N> v,
                                 const Vec128<T, N> bit) {
  return Mask128<T, N>{_mm_test_epi64_mask(v.raw, bit.raw)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Mask128<T, N> TestBit(const Vec128<T, N> v, const Vec128<T, N> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return detail::TestBit(hwy::SizeTag<sizeof(T)>(), v, bit);
}

// ------------------------------ Equality

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Mask128<T, N> operator==(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpeq_epi8_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Mask128<T, N> operator==(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpeq_epi16_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Mask128<T, N> operator==(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpeq_epi32_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Mask128<T, N> operator==(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpeq_epi64_mask(a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float16_t, N> operator==(Vec128<float16_t, N> a,
                                         Vec128<float16_t, N> b) {
  // Work around warnings in the intrinsic definitions (passing -1 as a mask).
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")
  return Mask128<float16_t, N>{_mm_cmp_ph_mask(a.raw, b.raw, _CMP_EQ_OQ)};
  HWY_DIAGNOSTICS(pop)
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float, N> operator==(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmp_ps_mask(a.raw, b.raw, _CMP_EQ_OQ)};
}

template <size_t N>
HWY_API Mask128<double, N> operator==(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmp_pd_mask(a.raw, b.raw, _CMP_EQ_OQ)};
}

// ------------------------------ Inequality

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Mask128<T, N> operator!=(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpneq_epi8_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Mask128<T, N> operator!=(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpneq_epi16_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Mask128<T, N> operator!=(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpneq_epi32_mask(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Mask128<T, N> operator!=(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Mask128<T, N>{_mm_cmpneq_epi64_mask(a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float16_t, N> operator!=(Vec128<float16_t, N> a,
                                         Vec128<float16_t, N> b) {
  // Work around warnings in the intrinsic definitions (passing -1 as a mask).
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")
  return Mask128<float16_t, N>{_mm_cmp_ph_mask(a.raw, b.raw, _CMP_NEQ_OQ)};
  HWY_DIAGNOSTICS(pop)
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float, N> operator!=(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmp_ps_mask(a.raw, b.raw, _CMP_NEQ_OQ)};
}

template <size_t N>
HWY_API Mask128<double, N> operator!=(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmp_pd_mask(a.raw, b.raw, _CMP_NEQ_OQ)};
}

// ------------------------------ Strict inequality

// Signed/float <
template <size_t N>
HWY_API Mask128<int8_t, N> operator>(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{_mm_cmpgt_epi8_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator>(Vec128<int16_t, N> a,
                                      Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{_mm_cmpgt_epi16_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator>(Vec128<int32_t, N> a,
                                      Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{_mm_cmpgt_epi32_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator>(Vec128<int64_t, N> a,
                                      Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{_mm_cmpgt_epi64_mask(a.raw, b.raw)};
}

template <size_t N>
HWY_API Mask128<uint8_t, N> operator>(Vec128<uint8_t, N> a,
                                      Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{_mm_cmpgt_epu8_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator>(Vec128<uint16_t, N> a,
                                       Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{_mm_cmpgt_epu16_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator>(Vec128<uint32_t, N> a,
                                       Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{_mm_cmpgt_epu32_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator>(Vec128<uint64_t, N> a,
                                       Vec128<uint64_t, N> b) {
  return Mask128<uint64_t, N>{_mm_cmpgt_epu64_mask(a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float16_t, N> operator>(Vec128<float16_t, N> a,
                                        Vec128<float16_t, N> b) {
  // Work around warnings in the intrinsic definitions (passing -1 as a mask).
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")
  return Mask128<float16_t, N>{_mm_cmp_ph_mask(a.raw, b.raw, _CMP_GT_OQ)};
  HWY_DIAGNOSTICS(pop)
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float, N> operator>(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmp_ps_mask(a.raw, b.raw, _CMP_GT_OQ)};
}
template <size_t N>
HWY_API Mask128<double, N> operator>(Vec128<double, N> a, Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmp_pd_mask(a.raw, b.raw, _CMP_GT_OQ)};
}

// ------------------------------ Weak inequality

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float16_t, N> operator>=(Vec128<float16_t, N> a,
                                         Vec128<float16_t, N> b) {
  // Work around warnings in the intrinsic definitions (passing -1 as a mask).
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")
  return Mask128<float16_t, N>{_mm_cmp_ph_mask(a.raw, b.raw, _CMP_GE_OQ)};
  HWY_DIAGNOSTICS(pop)
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Mask128<float, N> operator>=(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmp_ps_mask(a.raw, b.raw, _CMP_GE_OQ)};
}
template <size_t N>
HWY_API Mask128<double, N> operator>=(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmp_pd_mask(a.raw, b.raw, _CMP_GE_OQ)};
}

template <size_t N>
HWY_API Mask128<int8_t, N> operator>=(Vec128<int8_t, N> a,
                                      Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{_mm_cmpge_epi8_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator>=(Vec128<int16_t, N> a,
                                       Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{_mm_cmpge_epi16_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator>=(Vec128<int32_t, N> a,
                                       Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{_mm_cmpge_epi32_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator>=(Vec128<int64_t, N> a,
                                       Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{_mm_cmpge_epi64_mask(a.raw, b.raw)};
}

template <size_t N>
HWY_API Mask128<uint8_t, N> operator>=(Vec128<uint8_t, N> a,
                                       Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{_mm_cmpge_epu8_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator>=(Vec128<uint16_t, N> a,
                                        Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{_mm_cmpge_epu16_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator>=(Vec128<uint32_t, N> a,
                                        Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{_mm_cmpge_epu32_mask(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator>=(Vec128<uint64_t, N> a,
                                        Vec128<uint64_t, N> b) {
  return Mask128<uint64_t, N>{_mm_cmpge_epu64_mask(a.raw, b.raw)};
}

#else  // AVX2 or below

// Comparisons fill a lane with 1-bits if the condition is true, else 0.

template <class DTo, typename TFrom, size_t NFrom, HWY_IF_V_SIZE_LE_D(DTo, 16)>
HWY_API MFromD<DTo> RebindMask(DTo dto, Mask128<TFrom, NFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TFromD<DTo>), "Must have same size");
  const Simd<TFrom, NFrom, 0> d;
  return MaskFromVec(BitCast(dto, VecFromMask(d, m)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> TestBit(Vec128<T, N> v, Vec128<T, N> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return (v & bit) == bit;
}

// ------------------------------ Equality

// Unsigned
template <size_t N>
HWY_API Mask128<uint8_t, N> operator==(Vec128<uint8_t, N> a,
                                       Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{_mm_cmpeq_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator==(Vec128<uint16_t, N> a,
                                        Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{_mm_cmpeq_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator==(Vec128<uint32_t, N> a,
                                        Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{_mm_cmpeq_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator==(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  const DFromV<decltype(a)> d64;
  const RepartitionToNarrow<decltype(d64)> d32;
  const auto cmp32 = VecFromMask(d32, Eq(BitCast(d32, a), BitCast(d32, b)));
  const auto cmp64 = cmp32 & Shuffle2301(cmp32);
  return MaskFromVec(BitCast(d64, cmp64));
#else
  return Mask128<uint64_t, N>{_mm_cmpeq_epi64(a.raw, b.raw)};
#endif
}

// Signed
template <size_t N>
HWY_API Mask128<int8_t, N> operator==(Vec128<int8_t, N> a,
                                      Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{_mm_cmpeq_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator==(Vec128<int16_t, N> a,
                                       Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{_mm_cmpeq_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator==(Vec128<int32_t, N> a,
                                       Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{_mm_cmpeq_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator==(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  // Same as signed ==; avoid duplicating the SSSE3 version.
  const DFromV<decltype(a)> d;
  RebindToUnsigned<decltype(d)> du;
  return RebindMask(d, BitCast(du, a) == BitCast(du, b));
}

// Float
template <size_t N>
HWY_API Mask128<float, N> operator==(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmpeq_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator==(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmpeq_pd(a.raw, b.raw)};
}

// ------------------------------ Inequality

// This cannot have T as a template argument, otherwise it is not more
// specialized than rewritten operator== in C++20, leading to compile
// errors: https://gcc.godbolt.org/z/xsrPhPvPT.
template <size_t N>
HWY_API Mask128<uint8_t, N> operator!=(Vec128<uint8_t, N> a,
                                       Vec128<uint8_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator!=(Vec128<uint16_t, N> a,
                                        Vec128<uint16_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator!=(Vec128<uint32_t, N> a,
                                        Vec128<uint32_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator!=(Vec128<uint64_t, N> a,
                                        Vec128<uint64_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<int8_t, N> operator!=(Vec128<int8_t, N> a,
                                      Vec128<int8_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator!=(Vec128<int16_t, N> a,
                                       Vec128<int16_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator!=(Vec128<int32_t, N> a,
                                       Vec128<int32_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator!=(Vec128<int64_t, N> a,
                                       Vec128<int64_t, N> b) {
  return Not(a == b);
}

template <size_t N>
HWY_API Mask128<float, N> operator!=(Vec128<float, N> a, Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmpneq_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator!=(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmpneq_pd(a.raw, b.raw)};
}

// ------------------------------ Strict inequality

namespace detail {

template <size_t N>
HWY_INLINE Mask128<int8_t, N> Gt(hwy::SignedTag /*tag*/, Vec128<int8_t, N> a,
                                 Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{_mm_cmpgt_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_INLINE Mask128<int16_t, N> Gt(hwy::SignedTag /*tag*/, Vec128<int16_t, N> a,
                                  Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{_mm_cmpgt_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_INLINE Mask128<int32_t, N> Gt(hwy::SignedTag /*tag*/, Vec128<int32_t, N> a,
                                  Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{_mm_cmpgt_epi32(a.raw, b.raw)};
}

template <size_t N>
HWY_INLINE Mask128<int64_t, N> Gt(hwy::SignedTag /*tag*/,
                                  const Vec128<int64_t, N> a,
                                  const Vec128<int64_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  // See https://stackoverflow.com/questions/65166174/:
  const DFromV<decltype(a)> d;
  const RepartitionToNarrow<decltype(d)> d32;
  const Vec128<int64_t, N> m_eq32{Eq(BitCast(d32, a), BitCast(d32, b)).raw};
  const Vec128<int64_t, N> m_gt32{Gt(BitCast(d32, a), BitCast(d32, b)).raw};
  // If a.upper is greater, upper := true. Otherwise, if a.upper == b.upper:
  // upper := b-a (unsigned comparison result of lower). Otherwise: upper := 0.
  const __m128i upper = OrAnd(m_gt32, m_eq32, Sub(b, a)).raw;
  // Duplicate upper to lower half.
  return Mask128<int64_t, N>{_mm_shuffle_epi32(upper, _MM_SHUFFLE(3, 3, 1, 1))};
#else
  return Mask128<int64_t, N>{_mm_cmpgt_epi64(a.raw, b.raw)};  // SSE4.2
#endif
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Gt(hwy::UnsignedTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  const DFromV<decltype(a)> du;
  const RebindToSigned<decltype(du)> di;
  const Vec128<T, N> msb = Set(du, (LimitsMax<T>() >> 1) + 1);
  const auto sa = BitCast(di, Xor(a, msb));
  const auto sb = BitCast(di, Xor(b, msb));
  return RebindMask(du, Gt(hwy::SignedTag(), sa, sb));
}

template <size_t N>
HWY_INLINE Mask128<float, N> Gt(hwy::FloatTag /*tag*/, Vec128<float, N> a,
                                Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmpgt_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_INLINE Mask128<double, N> Gt(hwy::FloatTag /*tag*/, Vec128<double, N> a,
                                 Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmpgt_pd(a.raw, b.raw)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> operator>(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Gt(hwy::TypeTag<T>(), a, b);
}

// ------------------------------ Weak inequality

namespace detail {
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Ge(hwy::SignedTag tag, Vec128<T, N> a,
                            Vec128<T, N> b) {
  return Not(Gt(tag, b, a));
}

template <typename T, size_t N>
HWY_INLINE Mask128<T, N> Ge(hwy::UnsignedTag tag, Vec128<T, N> a,
                            Vec128<T, N> b) {
  return Not(Gt(tag, b, a));
}

template <size_t N>
HWY_INLINE Mask128<float, N> Ge(hwy::FloatTag /*tag*/, Vec128<float, N> a,
                                Vec128<float, N> b) {
  return Mask128<float, N>{_mm_cmpge_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_INLINE Mask128<double, N> Ge(hwy::FloatTag /*tag*/, Vec128<double, N> a,
                                 Vec128<double, N> b) {
  return Mask128<double, N>{_mm_cmpge_pd(a.raw, b.raw)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Mask128<T, N> operator>=(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Ge(hwy::TypeTag<T>(), a, b);
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Reversed comparisons

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<(Vec128<T, N> a, Vec128<T, N> b) {
  return b > a;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<=(Vec128<T, N> a, Vec128<T, N> b) {
  return b >= a;
}

// ------------------------------ Iota (Load)

namespace detail {

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_epi8(
      static_cast<char>(15), static_cast<char>(14), static_cast<char>(13),
      static_cast<char>(12), static_cast<char>(11), static_cast<char>(10),
      static_cast<char>(9), static_cast<char>(8), static_cast<char>(7),
      static_cast<char>(6), static_cast<char>(5), static_cast<char>(4),
      static_cast<char>(3), static_cast<char>(2), static_cast<char>(1),
      static_cast<char>(0))};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI16_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_epi16(int16_t{7}, int16_t{6}, int16_t{5}, int16_t{4},
                                 int16_t{3}, int16_t{2}, int16_t{1},
                                 int16_t{0})};
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_ph(float16_t{7}, float16_t{6}, float16_t{5},
                              float16_t{4}, float16_t{3}, float16_t{2},
                              float16_t{1}, float16_t{0})};
}
#endif  // HWY_HAVE_FLOAT16

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{
      _mm_set_epi32(int32_t{3}, int32_t{2}, int32_t{1}, int32_t{0})};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_epi64x(int64_t{1}, int64_t{0})};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_ps(3.0f, 2.0f, 1.0f, 0.0f)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  return VFromD<D>{_mm_set_pd(1.0, 0.0)};
}

#if HWY_COMPILER_MSVC
template <class V, HWY_IF_V_SIZE_V(V, 1)>
static HWY_INLINE V MaskOutVec128Iota(V v) {
  const V mask_out_mask{_mm_set_epi32(0, 0, 0, 0xFF)};
  return v & mask_out_mask;
}
template <class V, HWY_IF_V_SIZE_V(V, 2)>
static HWY_INLINE V MaskOutVec128Iota(V v) {
#if HWY_TARGET <= HWY_SSE4
  return V{_mm_blend_epi16(v.raw, _mm_setzero_si128(), 0xFE)};
#else
  const V mask_out_mask{_mm_set_epi32(0, 0, 0, 0xFFFF)};
  return v & mask_out_mask;
#endif
}
template <class V, HWY_IF_V_SIZE_V(V, 4)>
static HWY_INLINE V MaskOutVec128Iota(V v) {
  const DFromV<decltype(v)> d;
  const Repartition<float, decltype(d)> df;
  using VF = VFromD<decltype(df)>;
  return BitCast(d, VF{_mm_move_ss(_mm_setzero_ps(), BitCast(df, v).raw)});
}
template <class V, HWY_IF_V_SIZE_V(V, 8)>
static HWY_INLINE V MaskOutVec128Iota(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  return BitCast(d, VU{_mm_move_epi64(BitCast(du, v).raw)});
}
template <class V, HWY_IF_V_SIZE_GT_V(V, 8)>
static HWY_INLINE V MaskOutVec128Iota(V v) {
  return v;
}
#endif

}  // namespace detail

template <class D, typename T2, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Iota(D d, const T2 first) {
  const auto result_iota =
      detail::Iota0(d) + Set(d, ConvertScalarTo<TFromD<D>>(first));
#if HWY_COMPILER_MSVC
  return detail::MaskOutVec128Iota(result_iota);
#else
  return result_iota;
#endif
}

// ------------------------------ FirstN (Iota, Lt)

template <class D, class M = MFromD<D>, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API M FirstN(D d, size_t num) {
  constexpr size_t kN = MaxLanes(d);
  // For AVX3, this ensures `num` <= 255 as required by bzhi, which only looks
  // at the lower 8 bits; for AVX2 and below, this ensures `num` fits in TI.
  num = HWY_MIN(num, kN);
#if HWY_TARGET <= HWY_AVX3
#if HWY_ARCH_X86_64
  const uint64_t all = (1ull << kN) - 1;
  return M::FromBits(_bzhi_u64(all, num));
#else
  const uint32_t all = static_cast<uint32_t>((1ull << kN) - 1);
  return M::FromBits(_bzhi_u32(all, static_cast<uint32_t>(num)));
#endif  // HWY_ARCH_X86_64
#else   // HWY_TARGET > HWY_AVX3
  const RebindToSigned<decltype(d)> di;  // Signed comparisons are cheaper.
  using TI = TFromD<decltype(di)>;
  return RebindMask(d, detail::Iota0(di) < Set(di, static_cast<TI>(num)));
#endif  // HWY_TARGET <= HWY_AVX3
}

// ------------------------------ InterleaveLower

// Interleaves lanes from halves of the 128-bit blocks of "a" (which provides
// the least-significant lane) and "b". To concatenate two half-width integers
// into one, use ZipLower/Upper instead (also works with scalar).

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_unpacklo_epi8(a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;  // for float16_t
  return BitCast(
      d, VU{_mm_unpacklo_epi16(BitCast(du, a).raw, BitCast(du, b).raw)});
}
template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_unpacklo_epi32(a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_unpacklo_epi64(a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<float, N> InterleaveLower(Vec128<float, N> a,
                                         Vec128<float, N> b) {
  return Vec128<float, N>{_mm_unpacklo_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> InterleaveLower(Vec128<double, N> a,
                                          Vec128<double, N> b) {
  return Vec128<double, N>{_mm_unpacklo_pd(a.raw, b.raw)};
}

// Generic for all vector lengths.
template <class D>
HWY_API VFromD<D> InterleaveLower(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ================================================== MEMORY (2)

// ------------------------------ MaskedLoad

#if HWY_TARGET <= HWY_AVX3

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_maskz_loadu_epi8(m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d, VFromD<decltype(du)>{_mm_maskz_loadu_epi16(m.raw, p)});
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_maskz_loadu_epi32(m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_maskz_loadu_epi64(m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const float* HWY_RESTRICT p) {
  return VFromD<D>{_mm_maskz_loadu_ps(m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const double* HWY_RESTRICT p) {
  return VFromD<D>{_mm_maskz_loadu_pd(m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D /* tag */,
                               const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_mask_loadu_epi8(v.raw, m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d, VFromD<decltype(du)>{
                        _mm_mask_loadu_epi16(BitCast(du, v).raw, m.raw, p)});
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D /* tag */,
                               const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_mask_loadu_epi32(v.raw, m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D /* tag */,
                               const TFromD<D>* HWY_RESTRICT p) {
  return VFromD<D>{_mm_mask_loadu_epi64(v.raw, m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D /* tag */,
                               const float* HWY_RESTRICT p) {
  return VFromD<D>{_mm_mask_loadu_ps(v.raw, m.raw, p)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D /* tag */,
                               const double* HWY_RESTRICT p) {
  return VFromD<D>{_mm_mask_loadu_pd(v.raw, m.raw, p)};
}

#elif HWY_TARGET == HWY_AVX2

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const TFromD<D>* HWY_RESTRICT p) {
  auto p_p = reinterpret_cast<const int*>(p);  // NOLINT
  return VFromD<D>{_mm_maskload_epi32(p_p, m.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D /* tag */,
                             const TFromD<D>* HWY_RESTRICT p) {
  auto p_p = reinterpret_cast<const long long*>(p);  // NOLINT
  return VFromD<D>{_mm_maskload_epi64(p_p, m.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d, const float* HWY_RESTRICT p) {
  const RebindToSigned<decltype(d)> di;
  return VFromD<D>{_mm_maskload_ps(p, BitCast(di, VecFromMask(d, m)).raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d, const double* HWY_RESTRICT p) {
  const RebindToSigned<decltype(d)> di;
  return VFromD<D>{_mm_maskload_pd(p, BitCast(di, VecFromMask(d, m)).raw)};
}

// There is no maskload_epi8/16, so blend instead.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElseZero(m, LoadU(d, p));
}

#else  // <= SSE4

// Avoid maskmov* - its nontemporal 'hint' causes it to bypass caches (slow).
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElseZero(m, LoadU(d, p));
}

#endif

// ------------------------------ MaskedLoadOr

#if HWY_TARGET > HWY_AVX3  // else: native

// Generic for all vector lengths.
template <class D>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElse(m, LoadU(d, p), v);
}

#endif  // HWY_TARGET > HWY_AVX3

// ------------------------------ LoadN (InterleaveLower)

#if HWY_TARGET <= HWY_AVX2 && !HWY_MEM_OPS_MIGHT_FAULT

#ifdef HWY_NATIVE_LOAD_N
#undef HWY_NATIVE_LOAD_N
#else
#define HWY_NATIVE_LOAD_N
#endif

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(
                       D, (HWY_TARGET <= HWY_AVX3 ? ((1 << 1) | (1 << 2)) : 0) |
                              (1 << 4) | (1 << 8))>
HWY_API VFromD<D> LoadN(D d, const TFromD<D>* HWY_RESTRICT p,
                        size_t num_lanes) {
  const FixedTag<TFromD<D>, HWY_MAX(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>))>
      d_full;
  return ResizeBitCast(d, MaskedLoad(FirstN(d_full, num_lanes), d_full, p));
}

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(
                       D, (HWY_TARGET <= HWY_AVX3 ? ((1 << 1) | (1 << 2)) : 0) |
                              (1 << 4) | (1 << 8))>
HWY_API VFromD<D> LoadNOr(VFromD<D> no, D d, const TFromD<D>* HWY_RESTRICT p,
                          size_t num_lanes) {
  const FixedTag<TFromD<D>, HWY_MAX(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>))>
      d_full;
  return ResizeBitCast(d, MaskedLoadOr(ResizeBitCast(d_full, no),
                                       FirstN(d_full, num_lanes), d_full, p));
}

#if HWY_TARGET > HWY_AVX3
namespace detail {

// 'Leading' means the part that fits in 32-bit lanes. With 2-byte vectors,
// there are none, so return the remainder (v_trailing).
template <class D, HWY_IF_V_SIZE_LE_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadLeadingN(
    VFromD<D> /*load_mask*/, D /*d*/, const TFromD<D>* HWY_RESTRICT /*p*/,
    VFromD<D> v_trailing) {
  return v_trailing;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadLeadingNOr(
    VFromD<D> /*no*/, VFromD<D> /*load_mask*/, D /*d*/,
    const TFromD<D>* HWY_RESTRICT /*p*/, VFromD<D> v_trailing) {
  return v_trailing;
}

template <class D, HWY_IF_V_SIZE_GT_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadLeadingN(VFromD<D> load_mask, D d,
                                              const TFromD<D>* HWY_RESTRICT p,
                                              VFromD<D> v_trailing) {
  using DI32 = Repartition<int32_t, D>;
  const FixedTag<int32_t, HWY_MAX(HWY_MAX_LANES_D(DI32), 4)> di32_full;

  // ResizeBitCast of load_mask to di32 is okay below if
  // d.MaxBytes() < di32.MaxBytes() is true as any lanes of load_mask.raw past
  // the first (lowest-index) lanes of load_mask.raw will have already been
  // zeroed out by FirstN.
  return ResizeBitCast(
      d, IfNegativeThenElse(
             ResizeBitCast(di32_full, load_mask),
             MaskedLoad(MaskFromVec(ResizeBitCast(di32_full, load_mask)),
                        di32_full, reinterpret_cast<const int32_t*>(p)),
             ResizeBitCast(di32_full, v_trailing)));
}

template <class D, HWY_IF_V_SIZE_GT_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadLeadingNOr(VFromD<D> no,
                                                VFromD<D> load_mask, D d,
                                                const TFromD<D>* HWY_RESTRICT p,
                                                VFromD<D> v_trailing) {
  using DI32 = Repartition<int32_t, D>;
  const FixedTag<int32_t, HWY_MAX(HWY_MAX_LANES_D(DI32), 4)> di32_full;

  // ResizeBitCast of load_mask to di32 is okay below if
  // d.MaxBytes() < di32.MaxBytes() is true as any lanes of load_mask.raw past
  // the first (lowest-index) lanes of load_mask.raw will have already been
  // zeroed out by FirstN.
  return ResizeBitCast(
      d, IfNegativeThenElse(
             ResizeBitCast(di32_full, load_mask),
             MaskedLoadOr(ResizeBitCast(di32_full, no),
                          MaskFromVec(ResizeBitCast(di32_full, load_mask)),
                          di32_full, reinterpret_cast<const int32_t*>(p)),
             ResizeBitCast(di32_full, v_trailing)));
}

// Single lane: load or default value.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_LANES_D(D, 1)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingN(VFromD<D> /*load_mask*/, D d,
                                               const TFromD<D>* HWY_RESTRICT p,
                                               size_t num_lanes) {
  return (num_lanes > 0) ? LoadU(d, p) : Zero(d);
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_LANES_D(D, 1)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingNOr(
    VFromD<D> no, VFromD<D> /*load_mask*/, D d, const TFromD<D>* HWY_RESTRICT p,
    size_t num_lanes) {
  return (num_lanes > 0) ? LoadU(d, p) : no;
}

// Two lanes: load 1, 2, or default.
template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingN(VFromD<D> /*load_mask*/, D d,
                                               const TFromD<D>* HWY_RESTRICT p,
                                               size_t num_lanes) {
  if (num_lanes > 1) {
    return LoadU(d, p);
  } else {
    const FixedTag<TFromD<D>, 1> d1;
    return (num_lanes == 1) ? ResizeBitCast(d, LoadU(d1, p)) : Zero(d);
  }
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingNOr(
    VFromD<D> no, VFromD<D> /*load_mask*/, D d, const TFromD<D>* HWY_RESTRICT p,
    size_t num_lanes) {
  if (num_lanes > 1) {
    return LoadU(d, p);
  } else {
    if (num_lanes == 0) return no;
    // Load one, upper lane is default.
    const FixedTag<TFromD<D>, 1> d1;
    return InterleaveLower(ResizeBitCast(d, LoadU(d1, p)), no);
  }
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_GT_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingN(VFromD<D> load_mask, D d,
                                               const TFromD<D>* HWY_RESTRICT p,
                                               size_t num_lanes) {
  const size_t trailing_n = num_lanes & 3;
  if (trailing_n == 0) return Zero(d);

  VFromD<D> v_trailing = And(load_mask, Set(d, p[num_lanes - 1]));

  if ((trailing_n & 2) != 0) {
    const Repartition<int16_t, decltype(d)> di16;
    int16_t i16_bits;
    CopyBytes<sizeof(int16_t)>(p + num_lanes - trailing_n, &i16_bits);
    v_trailing = BitCast(
        d, IfNegativeThenElse(BitCast(di16, load_mask), Set(di16, i16_bits),
                              BitCast(di16, v_trailing)));
  }

  return v_trailing;
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_LANES_GT_D(D, 2)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingNOr(
    VFromD<D> no, VFromD<D> load_mask, D d, const TFromD<D>* HWY_RESTRICT p,
    size_t num_lanes) {
  const size_t trailing_n = num_lanes & 3;
  if (trailing_n == 0) return no;

  VFromD<D> v_trailing = IfVecThenElse(load_mask, Set(d, p[num_lanes - 1]), no);

  if ((trailing_n & 2) != 0) {
    const Repartition<int16_t, decltype(d)> di16;
    int16_t i16_bits;
    CopyBytes<sizeof(int16_t)>(p + num_lanes - trailing_n, &i16_bits);
    v_trailing = BitCast(
        d, IfNegativeThenElse(BitCast(di16, load_mask), Set(di16, i16_bits),
                              BitCast(di16, v_trailing)));
  }

  return v_trailing;
}

template <class D, HWY_IF_T_SIZE_D(D, 2), HWY_IF_LANES_GT_D(D, 1)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingN(VFromD<D> load_mask, D d,
                                               const TFromD<D>* HWY_RESTRICT p,
                                               size_t num_lanes) {
  if ((num_lanes & 1) != 0) {
    return And(load_mask, Set(d, p[num_lanes - 1]));
  } else {
    return Zero(d);
  }
}

template <class D, HWY_IF_T_SIZE_D(D, 2), HWY_IF_LANES_GT_D(D, 1)>
HWY_INLINE VFromD<D> AVX2UIF8Or16LoadTrailingNOr(
    VFromD<D> no, VFromD<D> load_mask, D d, const TFromD<D>* HWY_RESTRICT p,
    size_t num_lanes) {
  if ((num_lanes & 1) != 0) {
    return IfVecThenElse(load_mask, Set(d, p[num_lanes - 1]), no);
  } else {
    return no;
  }
}

}  // namespace detail

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> LoadN(D d, const TFromD<D>* HWY_RESTRICT p, size_t N) {
  const FixedTag<TFromD<D>, HWY_MAX(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>))>
      d_full;

  const VFromD<D> load_mask =
      ResizeBitCast(d, VecFromMask(d_full, FirstN(d_full, N)));
  const size_t num_lanes = HWY_MIN(N, HWY_MAX_LANES_D(D));
  const VFromD<D> v_trailing =
      detail::AVX2UIF8Or16LoadTrailingN(load_mask, d, p, num_lanes);

#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(num_lanes < (4 / sizeof(TFromD<D>))) &&
      num_lanes < (4 / sizeof(TFromD<D>))) {
    return v_trailing;
  }
#endif

  return detail::AVX2UIF8Or16LoadLeadingN(load_mask, d, p, v_trailing);
}

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> LoadNOr(VFromD<D> no, D d, const TFromD<D>* HWY_RESTRICT p,
                          size_t N) {
  const FixedTag<TFromD<D>, HWY_MAX(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>))>
      d_full;

  const VFromD<D> load_mask =
      ResizeBitCast(d, VecFromMask(d_full, FirstN(d_full, N)));
  const size_t num_lanes = HWY_MIN(N, HWY_MAX_LANES_D(D));
  const VFromD<D> v_trailing =
      detail::AVX2UIF8Or16LoadTrailingNOr(no, load_mask, d, p, num_lanes);

#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(num_lanes < (4 / sizeof(TFromD<D>))) &&
      num_lanes < (4 / sizeof(TFromD<D>))) {
    return v_trailing;
  }
#endif

  return detail::AVX2UIF8Or16LoadLeadingNOr(no, load_mask, d, p, v_trailing);
}

#endif  // HWY_TARGET > HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2 && !HWY_MEM_OPS_MIGHT_FAULT

// ------------------------------ BlendedStore

namespace detail {

// There is no maskload_epi8/16 with which we could safely implement
// BlendedStore. Manual blending is also unsafe because loading a full vector
// that crosses the array end causes asan faults. Resort to scalar code; the
// caller should instead use memcpy, assuming m is FirstN(d, n).
template <class D>
HWY_API void ScalarMaskedStore(VFromD<D> v, MFromD<D> m, D d,
                               TFromD<D>* HWY_RESTRICT p) {
  const RebindToSigned<decltype(d)> di;  // for testing mask if T=bfloat16_t.
  using TI = TFromD<decltype(di)>;
  alignas(16) TI buf[MaxLanes(d)];
  alignas(16) TI mask[MaxLanes(d)];
  Store(BitCast(di, v), di, buf);
  Store(BitCast(di, VecFromMask(d, m)), di, mask);
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    if (mask[i]) {
      CopySameSize(buf + i, p + i);
    }
  }
}
}  // namespace detail

#if HWY_TARGET <= HWY_AVX3

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D /* tag */,
                          TFromD<D>* HWY_RESTRICT p) {
  _mm_mask_storeu_epi8(p, m.raw, v.raw);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  _mm_mask_storeu_epi16(reinterpret_cast<uint16_t*>(p), RebindMask(du, m).raw,
                        BitCast(du, v).raw);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D /* tag */,
                          TFromD<D>* HWY_RESTRICT p) {
  auto pi = reinterpret_cast<int*>(p);  // NOLINT
  _mm_mask_storeu_epi32(pi, m.raw, v.raw);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D /* tag */,
                          TFromD<D>* HWY_RESTRICT p) {
  auto pi = reinterpret_cast<long long*>(p);  // NOLINT
  _mm_mask_storeu_epi64(pi, m.raw, v.raw);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D, float* HWY_RESTRICT p) {
  _mm_mask_storeu_ps(p, m.raw, v.raw);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D, double* HWY_RESTRICT p) {
  _mm_mask_storeu_pd(p, m.raw, v.raw);
}

#elif HWY_TARGET == HWY_AVX2

template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  detail::ScalarMaskedStore(v, m, d, p);
}

namespace detail {

template <class D, class V, class M, HWY_IF_UI32_D(D)>
HWY_INLINE void NativeBlendedStore(V v, M m, TFromD<D>* HWY_RESTRICT p) {
  auto pi = reinterpret_cast<int*>(p);  // NOLINT
  _mm_maskstore_epi32(pi, m.raw, v.raw);
}

template <class D, class V, class M, HWY_IF_UI64_D(D)>
HWY_INLINE void NativeBlendedStore(V v, M m, TFromD<D>* HWY_RESTRICT p) {
  auto pi = reinterpret_cast<long long*>(p);  // NOLINT
  _mm_maskstore_epi64(pi, m.raw, v.raw);
}

template <class D, class V, class M, HWY_IF_F32_D(D)>
HWY_INLINE void NativeBlendedStore(V v, M m, float* HWY_RESTRICT p) {
  _mm_maskstore_ps(p, m.raw, v.raw);
}

template <class D, class V, class M, HWY_IF_F64_D(D)>
HWY_INLINE void NativeBlendedStore(V v, M m, double* HWY_RESTRICT p) {
  _mm_maskstore_pd(p, m.raw, v.raw);
}

}  // namespace detail

template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  const RebindToSigned<decltype(d)> di;
  // For partial vectors, avoid writing other lanes by zeroing their mask.
  if (d.MaxBytes() < 16) {
    const Full128<TFromD<D>> dfull;
    const Mask128<TFromD<D>> mfull{m.raw};
    m = MFromD<D>{And(mfull, FirstN(dfull, MaxLanes(d))).raw};
  }

  // Float/double require, and unsigned ints tolerate, signed int masks.
  detail::NativeBlendedStore<D>(v, RebindMask(di, m), p);
}

#else  // <= SSE4

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  // Avoid maskmov* - its nontemporal 'hint' causes it to bypass caches (slow).
  detail::ScalarMaskedStore(v, m, d, p);
}

#endif  // SSE4

// ================================================== ARITHMETIC

// ------------------------------ Addition

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> operator+(const Vec128<uint8_t, N> a,
                                     const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_add_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> operator+(const Vec128<uint16_t, N> a,
                                      const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_add_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> operator+(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{_mm_add_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> operator+(const Vec128<uint64_t, N> a,
                                      const Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{_mm_add_epi64(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> operator+(const Vec128<int8_t, N> a,
                                    const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{_mm_add_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> operator+(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_add_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> operator+(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{_mm_add_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> operator+(const Vec128<int64_t, N> a,
                                     const Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{_mm_add_epi64(a.raw, b.raw)};
}

// Float
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> operator+(const Vec128<float16_t, N> a,
                                       const Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_add_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> operator+(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{_mm_add_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator+(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{_mm_add_pd(a.raw, b.raw)};
}

// ------------------------------ Subtraction

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> operator-(const Vec128<uint8_t, N> a,
                                     const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_sub_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> operator-(Vec128<uint16_t, N> a,
                                      Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_sub_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> operator-(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{_mm_sub_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> operator-(const Vec128<uint64_t, N> a,
                                      const Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{_mm_sub_epi64(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> operator-(const Vec128<int8_t, N> a,
                                    const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{_mm_sub_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> operator-(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_sub_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> operator-(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{_mm_sub_epi32(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> operator-(const Vec128<int64_t, N> a,
                                     const Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{_mm_sub_epi64(a.raw, b.raw)};
}

// Float
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> operator-(const Vec128<float16_t, N> a,
                                       const Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_sub_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> operator-(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{_mm_sub_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator-(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{_mm_sub_pd(a.raw, b.raw)};
}

// ------------------------------ AddSub

#if HWY_TARGET <= HWY_SSSE3

#undef HWY_IF_ADDSUB_V
#define HWY_IF_ADDSUB_V(V) \
  HWY_IF_V_SIZE_GT_V(      \
      V, ((hwy::IsFloat3264<TFromV<V>>()) ? 32 : sizeof(TFromV<V>)))

template <size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float, N> AddSub(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_addsub_ps(a.raw, b.raw)};
}
HWY_API Vec128<double> AddSub(Vec128<double> a, Vec128<double> b) {
  return Vec128<double>{_mm_addsub_pd(a.raw, b.raw)};
}
#endif  // HWY_TARGET <= HWY_SSSE3

// ------------------------------ SumsOf8
template <size_t N>
HWY_API Vec128<uint64_t, N / 8> SumsOf8(const Vec128<uint8_t, N> v) {
  return Vec128<uint64_t, N / 8>{_mm_sad_epu8(v.raw, _mm_setzero_si128())};
}

// Generic for all vector lengths
template <class V, HWY_IF_I8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWideX3<DFromV<V>>> SumsOf8(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<int64_t, decltype(d)> di64;

  // Adjust the values of v to be in the 0..255 range by adding 128 to each lane
  // of v (which is the same as an bitwise XOR of each i8 lane by 128) and then
  // bitcasting the Xor result to an u8 vector.
  const auto v_adj = BitCast(du, Xor(v, SignBit(d)));

  // Need to add -1024 to each i64 lane of the result of the SumsOf8(v_adj)
  // operation to account for the adjustment made above.
  return BitCast(di64, SumsOf8(v_adj)) + Set(di64, int64_t{-1024});
}

#ifdef HWY_NATIVE_SUMS_OF_8_ABS_DIFF
#undef HWY_NATIVE_SUMS_OF_8_ABS_DIFF
#else
#define HWY_NATIVE_SUMS_OF_8_ABS_DIFF
#endif

template <size_t N>
HWY_API Vec128<uint64_t, N / 8> SumsOf8AbsDiff(const Vec128<uint8_t, N> a,
                                               const Vec128<uint8_t, N> b) {
  return Vec128<uint64_t, N / 8>{_mm_sad_epu8(a.raw, b.raw)};
}

// Generic for all vector lengths
template <class V, HWY_IF_I8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWideX3<DFromV<V>>> SumsOf8AbsDiff(V a, V b) {
  const DFromV<V> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWideX3<decltype(d)> di64;

  // Adjust the values of a and b to be in the 0..255 range by adding 128 to
  // each lane of a and b (which is the same as an bitwise XOR of each i8 lane
  // by 128) and then bitcasting the results of the Xor operations to u8
  // vectors.
  const auto i8_msb = SignBit(d);
  const auto a_adj = BitCast(du, Xor(a, i8_msb));
  const auto b_adj = BitCast(du, Xor(b, i8_msb));

  // The result of SumsOf8AbsDiff(a_adj, b_adj) can simply be bitcasted to an
  // i64 vector as |(a[i] + 128) - (b[i] + 128)| == |a[i] - b[i]| is true
  return BitCast(di64, SumsOf8AbsDiff(a_adj, b_adj));
}

// ------------------------------ SumsOf4
#if HWY_TARGET <= HWY_AVX3
namespace detail {

template <size_t N>
HWY_INLINE Vec128<uint32_t, (N + 3) / 4> SumsOf4(
    hwy::UnsignedTag /*type_tag*/, hwy::SizeTag<1> /*lane_size_tag*/,
    Vec128<uint8_t, N> v) {
  const DFromV<decltype(v)> d;

  // _mm_maskz_dbsad_epu8 is used below as the odd uint16_t lanes need to be
  // zeroed out and the sums of the 4 consecutive lanes are already in the
  // even uint16_t lanes of the _mm_maskz_dbsad_epu8 result.
  return Vec128<uint32_t, (N + 3) / 4>{
      _mm_maskz_dbsad_epu8(static_cast<__mmask8>(0x55), v.raw, Zero(d).raw, 0)};
}

// detail::SumsOf4 for Vec128<int8_t, N> on AVX3 is implemented in x86_512-inl.h

}  // namespace detail
#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ SumsOfAdjQuadAbsDiff

#if HWY_TARGET <= HWY_SSE4
#ifdef HWY_NATIVE_SUMS_OF_ADJ_QUAD_ABS_DIFF
#undef HWY_NATIVE_SUMS_OF_ADJ_QUAD_ABS_DIFF
#else
#define HWY_NATIVE_SUMS_OF_ADJ_QUAD_ABS_DIFF
#endif

template <int kAOffset, int kBOffset, size_t N>
HWY_API Vec128<uint16_t, (N + 1) / 2> SumsOfAdjQuadAbsDiff(
    Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  static_assert(0 <= kAOffset && kAOffset <= 1,
                "kAOffset must be between 0 and 1");
  static_assert(0 <= kBOffset && kBOffset <= 3,
                "kBOffset must be between 0 and 3");
  return Vec128<uint16_t, (N + 1) / 2>{
      _mm_mpsadbw_epu8(a.raw, b.raw, (kAOffset << 2) | kBOffset)};
}

// Generic for all vector lengths
template <int kAOffset, int kBOffset, class V, HWY_IF_I8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> SumsOfAdjQuadAbsDiff(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(d)> dw;

  // Adjust the values of a and b to be in the 0..255 range by adding 128 to
  // each lane of a and b (which is the same as an bitwise XOR of each i8 lane
  // by 128) and then bitcasting the results of the Xor operations to u8
  // vectors.
  const auto i8_msb = SignBit(d);
  const auto a_adj = BitCast(du, Xor(a, i8_msb));
  const auto b_adj = BitCast(du, Xor(b, i8_msb));

  // The result of SumsOfAdjQuadAbsDiff<kAOffset, kBOffset>(a_adj, b_adj) can
  // simply be bitcasted to an i16 vector as
  // |(a[i] + 128) - (b[i] + 128)| == |a[i] - b[i]| is true.
  return BitCast(dw, SumsOfAdjQuadAbsDiff<kAOffset, kBOffset>(a_adj, b_adj));
}
#endif

// ------------------------------ SumsOfShuffledQuadAbsDiff

#if HWY_TARGET <= HWY_AVX3
#ifdef HWY_NATIVE_SUMS_OF_SHUFFLED_QUAD_ABS_DIFF
#undef HWY_NATIVE_SUMS_OF_SHUFFLED_QUAD_ABS_DIFF
#else
#define HWY_NATIVE_SUMS_OF_SHUFFLED_QUAD_ABS_DIFF
#endif

template <int kIdx3, int kIdx2, int kIdx1, int kIdx0, size_t N>
HWY_API Vec128<uint16_t, (N + 1) / 2> SumsOfShuffledQuadAbsDiff(
    Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  static_assert(0 <= kIdx0 && kIdx0 <= 3, "kIdx0 must be between 0 and 3");
  static_assert(0 <= kIdx1 && kIdx1 <= 3, "kIdx1 must be between 0 and 3");
  static_assert(0 <= kIdx2 && kIdx2 <= 3, "kIdx2 must be between 0 and 3");
  static_assert(0 <= kIdx3 && kIdx3 <= 3, "kIdx3 must be between 0 and 3");
  return Vec128<uint16_t, (N + 1) / 2>{
      _mm_dbsad_epu8(b.raw, a.raw, _MM_SHUFFLE(kIdx3, kIdx2, kIdx1, kIdx0))};
}

// Generic for all vector lengths
template <int kIdx3, int kIdx2, int kIdx1, int kIdx0, class V,
          HWY_IF_I8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> SumsOfShuffledQuadAbsDiff(V a,
                                                                       V b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(d)> dw;

  // Adjust the values of a and b to be in the 0..255 range by adding 128 to
  // each lane of a and b (which is the same as an bitwise XOR of each i8 lane
  // by 128) and then bitcasting the results of the Xor operations to u8
  // vectors.
  const auto i8_msb = SignBit(d);
  const auto a_adj = BitCast(du, Xor(a, i8_msb));
  const auto b_adj = BitCast(du, Xor(b, i8_msb));

  // The result of
  // SumsOfShuffledQuadAbsDiff<kIdx3, kIdx2, kIdx1, kIdx0>(a_adj, b_adj) can
  // simply be bitcasted to an i16 vector as
  // |(a[i] + 128) - (b[i] + 128)| == |a[i] - b[i]| is true.
  return BitCast(
      dw, SumsOfShuffledQuadAbsDiff<kIdx3, kIdx2, kIdx1, kIdx0>(a_adj, b_adj));
}
#endif

// ------------------------------ SaturatedAdd

// Returns a + b clamped to the destination range.

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> SaturatedAdd(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_adds_epu8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> SaturatedAdd(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_adds_epu16(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> SaturatedAdd(const Vec128<int8_t, N> a,
                                       const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{_mm_adds_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> SaturatedAdd(const Vec128<int16_t, N> a,
                                        const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_adds_epi16(a.raw, b.raw)};
}

#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
#ifdef HWY_NATIVE_I32_SATURATED_ADDSUB
#undef HWY_NATIVE_I32_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I32_SATURATED_ADDSUB
#endif

#ifdef HWY_NATIVE_I64_SATURATED_ADDSUB
#undef HWY_NATIVE_I64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I64_SATURATED_ADDSUB
#endif

template <size_t N>
HWY_API Vec128<int32_t, N> SaturatedAdd(Vec128<int32_t, N> a,
                                        Vec128<int32_t, N> b) {
  const DFromV<decltype(a)> d;
  const auto sum = a + b;
  const auto overflow_mask = MaskFromVec(
      Vec128<int32_t, N>{_mm_ternarylogic_epi32(a.raw, b.raw, sum.raw, 0x42)});
  const auto i32_max = Set(d, LimitsMax<int32_t>());
  const Vec128<int32_t, N> overflow_result{_mm_mask_ternarylogic_epi32(
      i32_max.raw, MaskFromVec(a).raw, i32_max.raw, i32_max.raw, 0x55)};
  return IfThenElse(overflow_mask, overflow_result, sum);
}

template <size_t N>
HWY_API Vec128<int64_t, N> SaturatedAdd(Vec128<int64_t, N> a,
                                        Vec128<int64_t, N> b) {
  const DFromV<decltype(a)> d;
  const auto sum = a + b;
  const auto overflow_mask = MaskFromVec(
      Vec128<int64_t, N>{_mm_ternarylogic_epi64(a.raw, b.raw, sum.raw, 0x42)});
  const auto i64_max = Set(d, LimitsMax<int64_t>());
  const Vec128<int64_t, N> overflow_result{_mm_mask_ternarylogic_epi64(
      i64_max.raw, MaskFromVec(a).raw, i64_max.raw, i64_max.raw, 0x55)};
  return IfThenElse(overflow_mask, overflow_result, sum);
}
#endif  // HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN

// ------------------------------ SaturatedSub

// Returns a - b clamped to the destination range.

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> SaturatedSub(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_subs_epu8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> SaturatedSub(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_subs_epu16(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> SaturatedSub(const Vec128<int8_t, N> a,
                                       const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{_mm_subs_epi8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> SaturatedSub(const Vec128<int16_t, N> a,
                                        const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_subs_epi16(a.raw, b.raw)};
}

#if HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN
template <size_t N>
HWY_API Vec128<int32_t, N> SaturatedSub(Vec128<int32_t, N> a,
                                        Vec128<int32_t, N> b) {
  const DFromV<decltype(a)> d;
  const auto diff = a - b;
  const auto overflow_mask = MaskFromVec(
      Vec128<int32_t, N>{_mm_ternarylogic_epi32(a.raw, b.raw, diff.raw, 0x18)});
  const auto i32_max = Set(d, LimitsMax<int32_t>());
  const Vec128<int32_t, N> overflow_result{_mm_mask_ternarylogic_epi32(
      i32_max.raw, MaskFromVec(a).raw, i32_max.raw, i32_max.raw, 0x55)};
  return IfThenElse(overflow_mask, overflow_result, diff);
}

template <size_t N>
HWY_API Vec128<int64_t, N> SaturatedSub(Vec128<int64_t, N> a,
                                        Vec128<int64_t, N> b) {
  const DFromV<decltype(a)> d;
  const auto diff = a - b;
  const auto overflow_mask = MaskFromVec(
      Vec128<int64_t, N>{_mm_ternarylogic_epi64(a.raw, b.raw, diff.raw, 0x18)});
  const auto i64_max = Set(d, LimitsMax<int64_t>());
  const Vec128<int64_t, N> overflow_result{_mm_mask_ternarylogic_epi64(
      i64_max.raw, MaskFromVec(a).raw, i64_max.raw, i64_max.raw, 0x55)};
  return IfThenElse(overflow_mask, overflow_result, diff);
}
#endif  // HWY_TARGET <= HWY_AVX3 && !HWY_IS_MSAN

// ------------------------------ AverageRound

// Returns (a + b + 1) / 2

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> AverageRound(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_avg_epu8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> AverageRound(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_avg_epu16(a.raw, b.raw)};
}

// I8/I16 AverageRound is generic for all vector lengths
template <class V, HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API V AverageRound(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  const V sign_bit = SignBit(d);
  return Xor(BitCast(d, AverageRound(BitCast(du, Xor(a, sign_bit)),
                                     BitCast(du, Xor(b, sign_bit)))),
             sign_bit);
}

// ------------------------------ Integer multiplication

template <size_t N>
HWY_API Vec128<uint16_t, N> operator*(const Vec128<uint16_t, N> a,
                                      const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_mullo_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> operator*(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_mullo_epi16(a.raw, b.raw)};
}

// Returns the upper sizeof(T)*8 bits of a * b in each lane.
template <size_t N>
HWY_API Vec128<uint16_t, N> MulHigh(const Vec128<uint16_t, N> a,
                                    const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{_mm_mulhi_epu16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> MulHigh(const Vec128<int16_t, N> a,
                                   const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_mulhi_epi16(a.raw, b.raw)};
}

template <class V, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 4)),
          HWY_IF_LANES_D(DFromV<V>, 1)>
HWY_API V MulHigh(V a, V b) {
  const DFromV<decltype(a)> d;
  const Full128<TFromD<decltype(d)>> d_full;
  return ResizeBitCast(
      d, Slide1Down(d_full, ResizeBitCast(d_full, MulEven(a, b))));
}

// I8/U8/I32/U32 MulHigh is generic for all vector lengths >= 2 lanes
template <class V, HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 4)),
          HWY_IF_LANES_GT_D(DFromV<V>, 1)>
HWY_API V MulHigh(V a, V b) {
  const DFromV<decltype(a)> d;

  const auto p_even = BitCast(d, MulEven(a, b));
  const auto p_odd = BitCast(d, MulOdd(a, b));
  return InterleaveOdd(d, p_even, p_odd);
}

// Multiplies even lanes (0, 2 ..) and places the double-wide result into
// even and the upper half into its odd neighbor lane.
template <class V, HWY_IF_U8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulEven(V a, V b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const auto lo8_mask = Set(dw, uint16_t{0x00FF});
  return And(ResizeBitCast(dw, a), lo8_mask) *
         And(ResizeBitCast(dw, b), lo8_mask);
}

template <class V, HWY_IF_I8_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulEven(V a, V b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  return ShiftRight<8>(ShiftLeft<8>(ResizeBitCast(dw, a))) *
         ShiftRight<8>(ShiftLeft<8>(ResizeBitCast(dw, b)));
}

template <class V, HWY_IF_UI16_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulEven(V a, V b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const RepartitionToNarrow<decltype(dw)> dw_as_d16;

  const auto lo = ResizeBitCast(dw, a * b);
  const auto hi = ShiftLeft<16>(ResizeBitCast(dw, MulHigh(a, b)));
  return BitCast(dw, OddEven(BitCast(dw_as_d16, hi), BitCast(dw_as_d16, lo)));
}

template <size_t N>
HWY_API Vec128<uint64_t, (N + 1) / 2> MulEven(const Vec128<uint32_t, N> a,
                                              const Vec128<uint32_t, N> b) {
  return Vec128<uint64_t, (N + 1) / 2>{_mm_mul_epu32(a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<int64_t, (N + 1) / 2> MulEven(const Vec128<int32_t, N> a,
                                             const Vec128<int32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;

  // p[i] = (((a[i] >> 31) * (a[i] >> 31)) << 64) +
  //        (((a[i] >> 31) * b[i]) << 32) +
  //        (((b[i] >> 31) * a[i]) << 32) +
  //        ((a[i] & int64_t{0xFFFFFFFF}) * (b[i] & int64_t{0xFFFFFFFF}))

  // ((a[i] >> 31) * (a[i] >> 31)) << 64 does not need to be computed as the
  // lower 64 bits of ((a[i] >> 31) * (a[i] >> 31)) << 64 is zero.

  // (((a[i] >> 31) * b[i]) << 32) + (((b[i] >> 31) * a[i]) << 32) ==
  // -((((a[i] >> 31) & b[i]) + ((b[i] >> 31) & a[i])) << 32)

  // ((a[i] & int64_t{0xFFFFFFFF}) * (b[i] & int64_t{0xFFFFFFFF})) can be
  // computed using MulEven(BitCast(du, a), BitCast(du, b))

  const auto neg_p_hi = ShiftLeft<32>(
      ResizeBitCast(dw, And(ShiftRight<31>(a), b) + And(ShiftRight<31>(b), a)));
  const auto p_lo = BitCast(dw, MulEven(BitCast(du, a), BitCast(du, b)));
  return p_lo - neg_p_hi;
#else
  return Vec128<int64_t, (N + 1) / 2>{_mm_mul_epi32(a.raw, b.raw)};
#endif
}

template <class V, HWY_IF_T_SIZE_V(V, 1)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulOdd(V a, V b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  return ShiftRight<8>(ResizeBitCast(dw, a)) *
         ShiftRight<8>(ResizeBitCast(dw, b));
}

template <class V, HWY_IF_UI16_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulOdd(V a, V b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const RebindToUnsigned<decltype(dw)> dw_u;
  const RepartitionToNarrow<decltype(dw)> dw_as_d16;

  const auto lo = ShiftRight<16>(BitCast(dw_u, ResizeBitCast(dw, a * b)));
  const auto hi = ResizeBitCast(dw, MulHigh(a, b));
  return BitCast(dw, OddEven(BitCast(dw_as_d16, hi), BitCast(dw_as_d16, lo)));
}

template <class V, HWY_IF_UI32_D(DFromV<V>)>
HWY_API VFromD<RepartitionToWide<DFromV<V>>> MulOdd(V a, V b) {
  return MulEven(DupOdd(a), DupOdd(b));
}

template <size_t N>
HWY_API Vec128<uint32_t, N> operator*(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  // Not as inefficient as it looks: _mm_mullo_epi32 has 10 cycle latency.
  // 64-bit right shift would also work but also needs port 5, so no benefit.
  // Notation: x=don't care, z=0.
  const __m128i a_x3x1 = _mm_shuffle_epi32(a.raw, _MM_SHUFFLE(3, 3, 1, 1));
  const auto mullo_x2x0 = MulEven(a, b);
  const __m128i b_x3x1 = _mm_shuffle_epi32(b.raw, _MM_SHUFFLE(3, 3, 1, 1));
  const auto mullo_x3x1 =
      MulEven(Vec128<uint32_t, N>{a_x3x1}, Vec128<uint32_t, N>{b_x3x1});
  // We could _mm_slli_epi64 by 32 to get 3z1z and OR with z2z0, but generating
  // the latter requires one more instruction or a constant.
  const __m128i mul_20 =
      _mm_shuffle_epi32(mullo_x2x0.raw, _MM_SHUFFLE(2, 0, 2, 0));
  const __m128i mul_31 =
      _mm_shuffle_epi32(mullo_x3x1.raw, _MM_SHUFFLE(2, 0, 2, 0));
  return Vec128<uint32_t, N>{_mm_unpacklo_epi32(mul_20, mul_31)};
#else
  return Vec128<uint32_t, N>{_mm_mullo_epi32(a.raw, b.raw)};
#endif
}

template <size_t N>
HWY_API Vec128<int32_t, N> operator*(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  // Same as unsigned; avoid duplicating the SSSE3 code.
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, BitCast(du, a) * BitCast(du, b));
}

// ------------------------------ RotateRight (ShiftRight, Or)

// U8 RotateRight implementation on AVX3_DL is now in x86_512-inl.h as U8
// RotateRight uses detail::GaloisAffine on AVX3_DL

#if HWY_TARGET > HWY_AVX3_DL
template <int kBits, size_t N>
HWY_API Vec128<uint8_t, N> RotateRight(const Vec128<uint8_t, N> v) {
  static_assert(0 <= kBits && kBits < 8, "Invalid shift count");
  if (kBits == 0) return v;
  // AVX3 does not support 8-bit.
  return Or(ShiftRight<kBits>(v), ShiftLeft<HWY_MIN(7, 8 - kBits)>(v));
}
#endif

template <int kBits, size_t N>
HWY_API Vec128<uint16_t, N> RotateRight(const Vec128<uint16_t, N> v) {
  static_assert(0 <= kBits && kBits < 16, "Invalid shift count");
  if (kBits == 0) return v;
#if HWY_TARGET <= HWY_AVX3_DL
  return Vec128<uint16_t, N>{_mm_shrdi_epi16(v.raw, v.raw, kBits)};
#else
  // AVX3 does not support 16-bit.
  return Or(ShiftRight<kBits>(v), ShiftLeft<HWY_MIN(15, 16 - kBits)>(v));
#endif
}

template <int kBits, size_t N>
HWY_API Vec128<uint32_t, N> RotateRight(const Vec128<uint32_t, N> v) {
  static_assert(0 <= kBits && kBits < 32, "Invalid shift count");
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint32_t, N>{_mm_ror_epi32(v.raw, kBits)};
#else
  if (kBits == 0) return v;
  return Or(ShiftRight<kBits>(v), ShiftLeft<HWY_MIN(31, 32 - kBits)>(v));
#endif
}

template <int kBits, size_t N>
HWY_API Vec128<uint64_t, N> RotateRight(const Vec128<uint64_t, N> v) {
  static_assert(0 <= kBits && kBits < 64, "Invalid shift count");
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint64_t, N>{_mm_ror_epi64(v.raw, kBits)};
#else
  if (kBits == 0) return v;
  return Or(ShiftRight<kBits>(v), ShiftLeft<HWY_MIN(63, 64 - kBits)>(v));
#endif
}

// I8/I16/I32/I64 RotateRight is generic for all vector lengths
template <int kBits, class V, HWY_IF_SIGNED_V(V)>
HWY_API V RotateRight(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, RotateRight<kBits>(BitCast(du, v)));
}

// ------------------------------ Rol/Ror
#if HWY_TARGET <= HWY_AVX3_DL
#ifdef HWY_NATIVE_ROL_ROR_16
#undef HWY_NATIVE_ROL_ROR_16
#else
#define HWY_NATIVE_ROL_ROR_16
#endif

template <class T, size_t N, HWY_IF_UI16(T)>
HWY_API Vec128<T, N> Ror(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_shrdv_epi16(a.raw, a.raw, b.raw)};
}

// U16/I16 Rol is generic for all vector lengths on AVX3_DL
template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V Rol(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  return Ror(a, BitCast(d, Neg(BitCast(di, b))));
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_ROL_ROR_32_64
#undef HWY_NATIVE_ROL_ROR_32_64
#else
#define HWY_NATIVE_ROL_ROR_32_64
#endif

template <class T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> Rol(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_rolv_epi32(a.raw, b.raw)};
}

template <class T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> Ror(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_rorv_epi32(a.raw, b.raw)};
}

template <class T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> Rol(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_rolv_epi64(a.raw, b.raw)};
}

template <class T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> Ror(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_rorv_epi64(a.raw, b.raw)};
}

#endif

// ------------------------------ RotateLeftSame/RotateRightSame

#if HWY_TARGET <= HWY_AVX3_DL

#ifdef HWY_NATIVE_ROL_ROR_SAME_16
#undef HWY_NATIVE_ROL_ROR_SAME_16
#else
#define HWY_NATIVE_ROL_ROR_SAME_16
#endif

// Generic for all vector lengths
template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  return Ror(v,
             Set(d, static_cast<TFromV<V>>(0u - static_cast<unsigned>(bits))));
}

template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  return Ror(v, Set(d, static_cast<TFromV<V>>(bits)));
}
#endif  // HWY_TARGET <= HWY_AVX3_DL

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_ROL_ROR_SAME_32_64
#undef HWY_NATIVE_ROL_ROR_SAME_32_64
#else
#define HWY_NATIVE_ROL_ROR_SAME_32_64
#endif

// Generic for all vector lengths
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 4) | (1 << 8))>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  return Rol(v, Set(d, static_cast<TFromV<V>>(static_cast<unsigned>(bits))));
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 4) | (1 << 8))>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  return Ror(v, Set(d, static_cast<TFromV<V>>(static_cast<unsigned>(bits))));
}
#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ BroadcastSignBit (ShiftRight, compare, mask)

template <size_t N>
HWY_API Vec128<int8_t, N> BroadcastSignBit(const Vec128<int8_t, N> v) {
  const DFromV<decltype(v)> d;
  return VecFromMask(v < Zero(d));
}

template <size_t N>
HWY_API Vec128<int16_t, N> BroadcastSignBit(const Vec128<int16_t, N> v) {
  return ShiftRight<15>(v);
}

template <size_t N>
HWY_API Vec128<int32_t, N> BroadcastSignBit(const Vec128<int32_t, N> v) {
  return ShiftRight<31>(v);
}

template <size_t N>
HWY_API Vec128<int64_t, N> BroadcastSignBit(const Vec128<int64_t, N> v) {
  const DFromV<decltype(v)> d;
#if HWY_TARGET <= HWY_AVX3
  (void)d;
  return Vec128<int64_t, N>{_mm_srai_epi64(v.raw, 63)};
#elif HWY_TARGET == HWY_AVX2 || HWY_TARGET == HWY_SSE4
  return VecFromMask(v < Zero(d));
#else
  // Efficient Lt() requires SSE4.2 and BLENDVPD requires SSE4.1. 32-bit shift
  // avoids generating a zero.
  const RepartitionToNarrow<decltype(d)> d32;
  const auto sign = ShiftRight<31>(BitCast(d32, v));
  return Vec128<int64_t, N>{
      _mm_shuffle_epi32(sign.raw, _MM_SHUFFLE(3, 3, 1, 1))};
#endif
}

// ------------------------------ Integer Abs

// Returns absolute value, except that LimitsMin() maps to LimitsMax() + 1.
template <size_t N>
HWY_API Vec128<int8_t, N> Abs(const Vec128<int8_t, N> v) {
#if HWY_COMPILER_MSVC || HWY_TARGET == HWY_SSE2
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const auto zero = Zero(du);
  const auto v_as_u8 = BitCast(du, v);
  return BitCast(d, Min(v_as_u8, zero - v_as_u8));
#else
  return Vec128<int8_t, N>{_mm_abs_epi8(v.raw)};
#endif
}

template <size_t N>
HWY_API Vec128<int16_t, N> Abs(const Vec128<int16_t, N> v) {
#if HWY_TARGET == HWY_SSE2
  const auto zero = Zero(DFromV<decltype(v)>());
  return Max(v, zero - v);
#else
  return Vec128<int16_t, N>{_mm_abs_epi16(v.raw)};
#endif
}

template <size_t N>
HWY_API Vec128<int32_t, N> Abs(const Vec128<int32_t, N> v) {
#if HWY_TARGET <= HWY_SSSE3
  return Vec128<int32_t, N>{_mm_abs_epi32(v.raw)};
#else
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfThenElse(MaskFromVec(BroadcastSignBit(v)), zero - v, v);
#endif
}

#if HWY_TARGET <= HWY_AVX3
template <size_t N>
HWY_API Vec128<int64_t, N> Abs(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{_mm_abs_epi64(v.raw)};
}
#else
// I64 Abs is generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class V, HWY_IF_I64(TFromV<V>)>
HWY_API V Abs(V v) {
  const auto zero = Zero(DFromV<decltype(v)>());
  return IfNegativeThenElse(v, zero - v, v);
}
#endif

#ifdef HWY_NATIVE_SATURATED_ABS
#undef HWY_NATIVE_SATURATED_ABS
#else
#define HWY_NATIVE_SATURATED_ABS
#endif

// Generic for all vector lengths
template <class V, HWY_IF_I8(TFromV<V>)>
HWY_API V SaturatedAbs(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Min(BitCast(du, v), BitCast(du, SaturatedSub(Zero(d), v))));
}

// Generic for all vector lengths
template <class V, HWY_IF_I16(TFromV<V>)>
HWY_API V SaturatedAbs(V v) {
  return Max(v, SaturatedSub(Zero(DFromV<V>()), v));
}

// Generic for all vector lengths
template <class V, HWY_IF_I32(TFromV<V>)>
HWY_API V SaturatedAbs(V v) {
  const auto abs_v = Abs(v);

#if HWY_TARGET <= HWY_SSE4
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Min(BitCast(du, abs_v),
                        Set(du, static_cast<uint32_t>(LimitsMax<int32_t>()))));
#else
  return Add(abs_v, BroadcastSignBit(abs_v));
#endif
}

// Generic for all vector lengths
template <class V, HWY_IF_I64(TFromV<V>)>
HWY_API V SaturatedAbs(V v) {
  const auto abs_v = Abs(v);
  return Add(abs_v, BroadcastSignBit(abs_v));
}

// GCC <14 and Clang <11 do not follow the Intel documentation for AVX-512VL
// srli_epi64: the count should be unsigned int. Note that this is not the same
// as the Shift3264Count in x86_512-inl.h (GCC also requires int).
#if (HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1100) || \
    (HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1400)
using Shift64Count = int;
#else
// Assume documented behavior. Clang 12, GCC 14 and MSVC 14.28.29910 match this.
using Shift64Count = unsigned int;
#endif

template <int kBits, size_t N>
HWY_API Vec128<int64_t, N> ShiftRight(const Vec128<int64_t, N> v) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<int64_t, N>{
      _mm_srai_epi64(v.raw, static_cast<Shift64Count>(kBits))};
#else
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const auto right = BitCast(di, ShiftRight<kBits>(BitCast(du, v)));
  const auto sign = ShiftLeft<64 - kBits>(BroadcastSignBit(v));
  return right | sign;
#endif
}

// ------------------------------ IfNegativeThenElse
template <size_t N>
HWY_API Vec128<int8_t, N> IfNegativeThenElse(const Vec128<int8_t, N> v,
                                             const Vec128<int8_t, N> yes,
                                             const Vec128<int8_t, N> no) {
// int8: IfThenElse only looks at the MSB on SSE4 or newer
#if HWY_TARGET <= HWY_SSE4
  const auto mask = MaskFromVec(v);
#else
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const auto mask = MaskFromVec(BitCast(d, BroadcastSignBit(BitCast(di, v))));
#endif

  return IfThenElse(mask, yes, no);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  static_assert(IsSigned<T>(), "Only works for signed/float");

// 16-bit: no native blendv on AVX2 or earlier, so copy sign to lower byte's
// MSB.
#if HWY_TARGET <= HWY_AVX3
  const auto mask = MaskFromVec(v);
#else
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const auto mask = MaskFromVec(BitCast(d, BroadcastSignBit(BitCast(di, v))));
#endif

  return IfThenElse(mask, yes, no);
}

template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 4) | (1 << 8))>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  static_assert(IsSigned<T>(), "Only works for signed/float");
  const DFromV<decltype(v)> d;

#if HWY_TARGET > HWY_AVX3 && HWY_TARGET <= HWY_SSE4
  // 32/64-bit: use float IfThenElse on SSE4/AVX2, which only looks at the MSB
  // on SSE4 or later.
  const RebindToFloat<decltype(d)> df;
  const auto mask = MaskFromVec(BitCast(df, v));
  return BitCast(d, IfThenElse(mask, BitCast(df, yes), BitCast(df, no)));
#else  // SSE2, SSSE3, or AVX3

#if HWY_TARGET <= HWY_AVX3
  // No need to cast to float or broadcast sign bit on AVX3 as IfThenElse only
  // looks at the MSB on AVX3
  (void)d;
  const auto mask = MaskFromVec(v);
#else
  const RebindToSigned<decltype(d)> di;
  const auto mask = MaskFromVec(BitCast(d, BroadcastSignBit(BitCast(di, v))));
#endif

  return IfThenElse(mask, yes, no);
#endif
}

#if HWY_TARGET > HWY_AVX3 && HWY_TARGET <= HWY_SSE4

#ifdef HWY_NATIVE_IF_NEG_THEN_ELSE_ZERO
#undef HWY_NATIVE_IF_NEG_THEN_ELSE_ZERO
#else
#define HWY_NATIVE_IF_NEG_THEN_ELSE_ZERO
#endif

#ifdef HWY_NATIVE_IF_NEG_THEN_ZERO_ELSE
#undef HWY_NATIVE_IF_NEG_THEN_ZERO_ELSE
#else
#define HWY_NATIVE_IF_NEG_THEN_ZERO_ELSE
#endif

// SSE4/AVX2 IfNegativeThenElseZero/IfNegativeThenZeroElse is generic for all
// vector lengths
template <class V, HWY_IF_NOT_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 4) | (1 << 8))>
HWY_API V IfNegativeThenElseZero(V v, V yes) {
  const DFromV<decltype(v)> d;
  return IfNegativeThenElse(v, yes, Zero(d));
}

template <class V, HWY_IF_NOT_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, 2)>
HWY_API V IfNegativeThenElseZero(V v, V yes) {
  return IfThenElseZero(IsNegative(v), yes);
}

template <class V, HWY_IF_NOT_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 4) | (1 << 8))>
HWY_API V IfNegativeThenZeroElse(V v, V no) {
  const DFromV<decltype(v)> d;
  return IfNegativeThenElse(v, Zero(d), no);
}

template <class V, HWY_IF_NOT_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, 2)>
HWY_API V IfNegativeThenZeroElse(V v, V no) {
  return IfThenZeroElse(IsNegative(v), no);
}

#endif  // HWY_TARGET > HWY_AVX3 && HWY_TARGET <= HWY_SSE4

// ------------------------------ IfNegativeThenNegOrUndefIfZero

#if HWY_TARGET <= HWY_SSSE3

#ifdef HWY_NATIVE_INTEGER_IF_NEGATIVE_THEN_NEG
#undef HWY_NATIVE_INTEGER_IF_NEGATIVE_THEN_NEG
#else
#define HWY_NATIVE_INTEGER_IF_NEGATIVE_THEN_NEG
#endif

template <size_t N>
HWY_API Vec128<int8_t, N> IfNegativeThenNegOrUndefIfZero(Vec128<int8_t, N> mask,
                                                         Vec128<int8_t, N> v) {
  return Vec128<int8_t, N>{_mm_sign_epi8(v.raw, mask.raw)};
}

template <size_t N>
HWY_API Vec128<int16_t, N> IfNegativeThenNegOrUndefIfZero(
    Vec128<int16_t, N> mask, Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{_mm_sign_epi16(v.raw, mask.raw)};
}

template <size_t N>
HWY_API Vec128<int32_t, N> IfNegativeThenNegOrUndefIfZero(
    Vec128<int32_t, N> mask, Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{_mm_sign_epi32(v.raw, mask.raw)};
}

// Generic for all vector lengths
template <class V, HWY_IF_I64_D(DFromV<V>)>
HWY_API V IfNegativeThenNegOrUndefIfZero(V mask, V v) {
#if HWY_TARGET <= HWY_AVX3
  // MaskedSubOr is more efficient than IfNegativeThenElse on AVX3
  const DFromV<decltype(v)> d;
  return MaskedSubOr(v, MaskFromVec(mask), Zero(d), v);
#else
  // IfNegativeThenElse is more efficient than MaskedSubOr on SSE4/AVX2
  return IfNegativeThenElse(mask, Neg(v), v);
#endif
}

#endif  // HWY_TARGET <= HWY_SSSE3

// ------------------------------ ShiftLeftSame

template <size_t N>
HWY_API Vec128<uint16_t, N> ShiftLeftSame(const Vec128<uint16_t, N> v,
                                          const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint16_t, N>{_mm_slli_epi16(v.raw, bits)};
  }
#endif
  return Vec128<uint16_t, N>{_mm_sll_epi16(v.raw, _mm_cvtsi32_si128(bits))};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> ShiftLeftSame(const Vec128<uint32_t, N> v,
                                          const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint32_t, N>{_mm_slli_epi32(v.raw, bits)};
  }
#endif
  return Vec128<uint32_t, N>{_mm_sll_epi32(v.raw, _mm_cvtsi32_si128(bits))};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> ShiftLeftSame(const Vec128<uint64_t, N> v,
                                          const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint64_t, N>{_mm_slli_epi64(v.raw, bits)};
  }
#endif
  return Vec128<uint64_t, N>{_mm_sll_epi64(v.raw, _mm_cvtsi32_si128(bits))};
}

template <size_t N>
HWY_API Vec128<int16_t, N> ShiftLeftSame(const Vec128<int16_t, N> v,
                                         const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int16_t, N>{_mm_slli_epi16(v.raw, bits)};
  }
#endif
  return Vec128<int16_t, N>{_mm_sll_epi16(v.raw, _mm_cvtsi32_si128(bits))};
}

template <size_t N>
HWY_API Vec128<int32_t, N> ShiftLeftSame(const Vec128<int32_t, N> v,
                                         const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int32_t, N>{_mm_slli_epi32(v.raw, bits)};
  }
#endif
  return Vec128<int32_t, N>{_mm_sll_epi32(v.raw, _mm_cvtsi32_si128(bits))};
}

template <size_t N>
HWY_API Vec128<int64_t, N> ShiftLeftSame(const Vec128<int64_t, N> v,
                                         const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int64_t, N>{_mm_slli_epi64(v.raw, bits)};
  }
#endif
  return Vec128<int64_t, N>{_mm_sll_epi64(v.raw, _mm_cvtsi32_si128(bits))};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShiftLeftSame(const Vec128<T, N> v, const int bits) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<T, N> shifted{
      ShiftLeftSame(Vec128<MakeWide<T>>{v.raw}, bits).raw};
  return shifted & Set(d8, static_cast<T>((0xFF << bits) & 0xFF));
}

// ------------------------------ ShiftRightSame (BroadcastSignBit)

template <size_t N>
HWY_API Vec128<uint16_t, N> ShiftRightSame(const Vec128<uint16_t, N> v,
                                           const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint16_t, N>{_mm_srli_epi16(v.raw, bits)};
  }
#endif
  return Vec128<uint16_t, N>{_mm_srl_epi16(v.raw, _mm_cvtsi32_si128(bits))};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> ShiftRightSame(const Vec128<uint32_t, N> v,
                                           const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint32_t, N>{_mm_srli_epi32(v.raw, bits)};
  }
#endif
  return Vec128<uint32_t, N>{_mm_srl_epi32(v.raw, _mm_cvtsi32_si128(bits))};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> ShiftRightSame(const Vec128<uint64_t, N> v,
                                           const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<uint64_t, N>{_mm_srli_epi64(v.raw, bits)};
  }
#endif
  return Vec128<uint64_t, N>{_mm_srl_epi64(v.raw, _mm_cvtsi32_si128(bits))};
}

template <size_t N>
HWY_API Vec128<uint8_t, N> ShiftRightSame(Vec128<uint8_t, N> v,
                                          const int bits) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<uint8_t, N> shifted{
      ShiftRightSame(Vec128<uint16_t>{v.raw}, bits).raw};
  return shifted & Set(d8, static_cast<uint8_t>(0xFF >> bits));
}

template <size_t N>
HWY_API Vec128<int16_t, N> ShiftRightSame(const Vec128<int16_t, N> v,
                                          const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int16_t, N>{_mm_srai_epi16(v.raw, bits)};
  }
#endif
  return Vec128<int16_t, N>{_mm_sra_epi16(v.raw, _mm_cvtsi32_si128(bits))};
}

template <size_t N>
HWY_API Vec128<int32_t, N> ShiftRightSame(const Vec128<int32_t, N> v,
                                          const int bits) {
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int32_t, N>{_mm_srai_epi32(v.raw, bits)};
  }
#endif
  return Vec128<int32_t, N>{_mm_sra_epi32(v.raw, _mm_cvtsi32_si128(bits))};
}
template <size_t N>
HWY_API Vec128<int64_t, N> ShiftRightSame(const Vec128<int64_t, N> v,
                                          const int bits) {
#if HWY_TARGET <= HWY_AVX3
#if HWY_COMPILER_GCC
  if (__builtin_constant_p(bits)) {
    return Vec128<int64_t, N>{
        _mm_srai_epi64(v.raw, static_cast<Shift64Count>(bits))};
  }
#endif
  return Vec128<int64_t, N>{_mm_sra_epi64(v.raw, _mm_cvtsi32_si128(bits))};
#else
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const auto right = BitCast(di, ShiftRightSame(BitCast(du, v), bits));
  const auto sign = ShiftLeftSame(BroadcastSignBit(v), 64 - bits);
  return right | sign;
#endif
}

template <size_t N>
HWY_API Vec128<int8_t, N> ShiftRightSame(Vec128<int8_t, N> v, const int bits) {
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const auto shifted = BitCast(di, ShiftRightSame(BitCast(du, v), bits));
  const auto shifted_sign =
      BitCast(di, Set(du, static_cast<uint8_t>(0x80 >> bits)));
  return (shifted ^ shifted_sign) - shifted_sign;
}

// ------------------------------ Floating-point mul / div

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> operator*(Vec128<float16_t, N> a,
                                       Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_mul_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> operator*(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_mul_ps(a.raw, b.raw)};
}
HWY_API Vec128<float, 1> operator*(const Vec128<float, 1> a,
                                   const Vec128<float, 1> b) {
  return Vec128<float, 1>{_mm_mul_ss(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator*(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{_mm_mul_pd(a.raw, b.raw)};
}
HWY_API Vec64<double> operator*(const Vec64<double> a, const Vec64<double> b) {
  return Vec64<double>{_mm_mul_sd(a.raw, b.raw)};
}

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_MUL_BY_POW2
#undef HWY_NATIVE_MUL_BY_POW2
#else
#define HWY_NATIVE_MUL_BY_POW2
#endif

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> MulByFloorPow2(Vec128<float16_t, N> a,
                                            Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_scalef_ph(a.raw, b.raw)};
}
#endif

template <size_t N>
HWY_API Vec128<float, N> MulByFloorPow2(Vec128<float, N> a,
                                        Vec128<float, N> b) {
  return Vec128<float, N>{_mm_scalef_ps(a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> MulByFloorPow2(Vec128<double, N> a,
                                         Vec128<double, N> b) {
  return Vec128<double, N>{_mm_scalef_pd(a.raw, b.raw)};
}

// MulByPow2 is generic for all vector lengths on AVX3
template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V MulByPow2(V v, VFromD<RebindToSigned<DFromV<V>>> exp) {
  const DFromV<decltype(v)> d;
  return MulByFloorPow2(v, ConvertTo(d, exp));
}

#endif  // HWY_TARGET <= HWY_AVX3

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> operator/(const Vec128<float16_t, N> a,
                                       const Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_div_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> operator/(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{_mm_div_ps(a.raw, b.raw)};
}
HWY_API Vec128<float, 1> operator/(const Vec128<float, 1> a,
                                   const Vec128<float, 1> b) {
  return Vec128<float, 1>{_mm_div_ss(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator/(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{_mm_div_pd(a.raw, b.raw)};
}
HWY_API Vec64<double> operator/(const Vec64<double> a, const Vec64<double> b) {
  return Vec64<double>{_mm_div_sd(a.raw, b.raw)};
}

// Approximate reciprocal
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> ApproximateReciprocal(
    const Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{_mm_rcp_ph(v.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> ApproximateReciprocal(const Vec128<float, N> v) {
  return Vec128<float, N>{_mm_rcp_ps(v.raw)};
}
HWY_API Vec128<float, 1> ApproximateReciprocal(const Vec128<float, 1> v) {
  return Vec128<float, 1>{_mm_rcp_ss(v.raw)};
}

#if HWY_TARGET <= HWY_AVX3
#ifdef HWY_NATIVE_F64_APPROX_RECIP
#undef HWY_NATIVE_F64_APPROX_RECIP
#else
#define HWY_NATIVE_F64_APPROX_RECIP
#endif

HWY_API Vec128<double> ApproximateReciprocal(Vec128<double> v) {
  return Vec128<double>{_mm_rcp14_pd(v.raw)};
}
HWY_API Vec64<double> ApproximateReciprocal(Vec64<double> v) {
  return Vec64<double>{_mm_rcp14_sd(v.raw, v.raw)};
}
#endif

// Generic for all vector lengths.
template <class V, HWY_IF_FLOAT_V(V)>
HWY_API V AbsDiff(V a, V b) {
  return Abs(a - b);
}

// ------------------------------ MaskedMinOr

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_MASKED_ARITH
#undef HWY_NATIVE_MASKED_ARITH
#else
#define HWY_NATIVE_MASKED_ARITH
#endif

template <typename T, size_t N, HWY_IF_U8(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epu8(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I8(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U16(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epu16(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I16(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U32(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epu32(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I32(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epi32(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U64(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epu64(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I64(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_epi64(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F32(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F64(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <typename T, size_t N, HWY_IF_F16(T)>
HWY_API Vec128<T, N> MaskedMinOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_min_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// ------------------------------ MaskedMaxOr

template <typename T, size_t N, HWY_IF_U8(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epu8(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I8(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U16(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epu16(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I16(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U32(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epu32(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I32(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epi32(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U64(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epu64(no.raw, m.raw, a.raw, b.raw)};
}
template <typename T, size_t N, HWY_IF_I64(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_epi64(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F32(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F64(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <typename T, size_t N, HWY_IF_F16(T)>
HWY_API Vec128<T, N> MaskedMaxOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_max_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// ------------------------------ MaskedAddOr

template <typename T, size_t N, HWY_IF_UI8(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_epi32(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_epi64(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F32(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F64(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <typename T, size_t N, HWY_IF_F16(T)>
HWY_API Vec128<T, N> MaskedAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_add_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// ------------------------------ MaskedSubOr

template <typename T, size_t N, HWY_IF_UI8(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_epi32(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_epi64(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F32(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_F64(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <typename T, size_t N, HWY_IF_F16(T)>
HWY_API Vec128<T, N> MaskedSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                 Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_sub_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// ------------------------------ MaskedMulOr

// There are no elementwise integer mask_mul. Generic for all vector lengths.
template <class V, class M>
HWY_API V MaskedMulOr(V no, M m, V a, V b) {
  return IfThenElse(m, a * b, no);
}

template <size_t N>
HWY_API Vec128<float, N> MaskedMulOr(Vec128<float, N> no, Mask128<float, N> m,
                                     Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_mask_mul_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> MaskedMulOr(Vec128<double, N> no,
                                      Mask128<double, N> m, Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Vec128<double, N>{_mm_mask_mul_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> MaskedMulOr(Vec128<float16_t, N> no,
                                         Mask128<float16_t, N> m,
                                         Vec128<float16_t, N> a,
                                         Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_mask_mul_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// ------------------------------ MaskedDivOr

template <size_t N>
HWY_API Vec128<float, N> MaskedDivOr(Vec128<float, N> no, Mask128<float, N> m,
                                     Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_mask_div_ps(no.raw, m.raw, a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> MaskedDivOr(Vec128<double, N> no,
                                      Mask128<double, N> m, Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Vec128<double, N>{_mm_mask_div_pd(no.raw, m.raw, a.raw, b.raw)};
}

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> MaskedDivOr(Vec128<float16_t, N> no,
                                         Mask128<float16_t, N> m,
                                         Vec128<float16_t, N> a,
                                         Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_mask_div_ph(no.raw, m.raw, a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16

// Generic for all vector lengths
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V MaskedDivOr(V no, MFromD<DFromV<V>> m, V a, V b) {
  return IfThenElse(m, Div(a, b), no);
}

// ------------------------------ MaskedModOr
// Generic for all vector lengths
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V MaskedModOr(V no, MFromD<DFromV<V>> m, V a, V b) {
  return IfThenElse(m, Mod(a, b), no);
}

// ------------------------------ MaskedSatAddOr

template <typename T, size_t N, HWY_IF_I8(T)>
HWY_API Vec128<T, N> MaskedSatAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_adds_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U8(T)>
HWY_API Vec128<T, N> MaskedSatAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_adds_epu8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_I16(T)>
HWY_API Vec128<T, N> MaskedSatAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_adds_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U16(T)>
HWY_API Vec128<T, N> MaskedSatAddOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_adds_epu16(no.raw, m.raw, a.raw, b.raw)};
}

// ------------------------------ MaskedSatSubOr

template <typename T, size_t N, HWY_IF_I8(T)>
HWY_API Vec128<T, N> MaskedSatSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_subs_epi8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U8(T)>
HWY_API Vec128<T, N> MaskedSatSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_subs_epu8(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_I16(T)>
HWY_API Vec128<T, N> MaskedSatSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_subs_epi16(no.raw, m.raw, a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_U16(T)>
HWY_API Vec128<T, N> MaskedSatSubOr(Vec128<T, N> no, Mask128<T, N> m,
                                    Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{_mm_mask_subs_epu16(no.raw, m.raw, a.raw, b.raw)};
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Floating-point multiply-add variants

#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> MulAdd(Vec128<float16_t, N> mul,
                                    Vec128<float16_t, N> x,
                                    Vec128<float16_t, N> add) {
  return Vec128<float16_t, N>{_mm_fmadd_ph(mul.raw, x.raw, add.raw)};
}

template <size_t N>
HWY_API Vec128<float16_t, N> NegMulAdd(Vec128<float16_t, N> mul,
                                       Vec128<float16_t, N> x,
                                       Vec128<float16_t, N> add) {
  return Vec128<float16_t, N>{_mm_fnmadd_ph(mul.raw, x.raw, add.raw)};
}

template <size_t N>
HWY_API Vec128<float16_t, N> MulSub(Vec128<float16_t, N> mul,
                                    Vec128<float16_t, N> x,
                                    Vec128<float16_t, N> sub) {
  return Vec128<float16_t, N>{_mm_fmsub_ph(mul.raw, x.raw, sub.raw)};
}

template <size_t N>
HWY_API Vec128<float16_t, N> NegMulSub(Vec128<float16_t, N> mul,
                                       Vec128<float16_t, N> x,
                                       Vec128<float16_t, N> sub) {
  return Vec128<float16_t, N>{_mm_fnmsub_ph(mul.raw, x.raw, sub.raw)};
}

#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> MulAdd(Vec128<float, N> mul, Vec128<float, N> x,
                                Vec128<float, N> add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return mul * x + add;
#else
  return Vec128<float, N>{_mm_fmadd_ps(mul.raw, x.raw, add.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<double, N> MulAdd(Vec128<double, N> mul, Vec128<double, N> x,
                                 Vec128<double, N> add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return mul * x + add;
#else
  return Vec128<double, N>{_mm_fmadd_pd(mul.raw, x.raw, add.raw)};
#endif
}

// Returns add - mul * x
template <size_t N>
HWY_API Vec128<float, N> NegMulAdd(Vec128<float, N> mul, Vec128<float, N> x,
                                   Vec128<float, N> add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return add - mul * x;
#else
  return Vec128<float, N>{_mm_fnmadd_ps(mul.raw, x.raw, add.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<double, N> NegMulAdd(Vec128<double, N> mul, Vec128<double, N> x,
                                    Vec128<double, N> add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return add - mul * x;
#else
  return Vec128<double, N>{_mm_fnmadd_pd(mul.raw, x.raw, add.raw)};
#endif
}

// Returns mul * x - sub
template <size_t N>
HWY_API Vec128<float, N> MulSub(Vec128<float, N> mul, Vec128<float, N> x,
                                Vec128<float, N> sub) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return mul * x - sub;
#else
  return Vec128<float, N>{_mm_fmsub_ps(mul.raw, x.raw, sub.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<double, N> MulSub(Vec128<double, N> mul, Vec128<double, N> x,
                                 Vec128<double, N> sub) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return mul * x - sub;
#else
  return Vec128<double, N>{_mm_fmsub_pd(mul.raw, x.raw, sub.raw)};
#endif
}

// Returns -mul * x - sub
template <size_t N>
HWY_API Vec128<float, N> NegMulSub(Vec128<float, N> mul, Vec128<float, N> x,
                                   Vec128<float, N> sub) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return Neg(mul) * x - sub;
#else
  return Vec128<float, N>{_mm_fnmsub_ps(mul.raw, x.raw, sub.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<double, N> NegMulSub(Vec128<double, N> mul, Vec128<double, N> x,
                                    Vec128<double, N> sub) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return Neg(mul) * x - sub;
#else
  return Vec128<double, N>{_mm_fnmsub_pd(mul.raw, x.raw, sub.raw)};
#endif
}

#if HWY_TARGET <= HWY_SSSE3

#undef HWY_IF_MULADDSUB_V
#define HWY_IF_MULADDSUB_V(V)                        \
  HWY_IF_LANES_GT_D(DFromV<V>, 1),                   \
      HWY_IF_T_SIZE_ONE_OF_V(                        \
          V, (1 << 1) | ((hwy::IsFloat<TFromV<V>>()) \
                             ? 0                     \
                             : ((1 << 2) | (1 << 4) | (1 << 8))))

#if HWY_HAVE_FLOAT16
template <size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float16_t, N> MulAddSub(Vec128<float16_t, N> mul,
                                       Vec128<float16_t, N> x,
                                       Vec128<float16_t, N> sub_or_add) {
  return Vec128<float16_t, N>{_mm_fmaddsub_ph(mul.raw, x.raw, sub_or_add.raw)};
}
#endif  // HWY_HAVE_FLOAT16

template <size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<float, N> MulAddSub(Vec128<float, N> mul, Vec128<float, N> x,
                                   Vec128<float, N> sub_or_add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return AddSub(mul * x, sub_or_add);
#else
  return Vec128<float, N>{_mm_fmaddsub_ps(mul.raw, x.raw, sub_or_add.raw)};
#endif
}

HWY_API Vec128<double> MulAddSub(Vec128<double> mul, Vec128<double> x,
                                 Vec128<double> sub_or_add) {
#if HWY_TARGET >= HWY_SSE4 || defined(HWY_DISABLE_BMI2_FMA)
  return AddSub(mul * x, sub_or_add);
#else
  return Vec128<double>{_mm_fmaddsub_pd(mul.raw, x.raw, sub_or_add.raw)};
#endif
}

#endif  // HWY_TARGET <= HWY_SSSE3

// ------------------------------ Floating-point square root

// Full precision square root
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Sqrt(Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{_mm_sqrt_ph(v.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Sqrt(Vec128<float, N> v) {
  return Vec128<float, N>{_mm_sqrt_ps(v.raw)};
}
HWY_API Vec128<float, 1> Sqrt(Vec128<float, 1> v) {
  return Vec128<float, 1>{_mm_sqrt_ss(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Sqrt(Vec128<double, N> v) {
  return Vec128<double, N>{_mm_sqrt_pd(v.raw)};
}
HWY_API Vec64<double> Sqrt(Vec64<double> v) {
  return Vec64<double>{_mm_sqrt_sd(_mm_setzero_pd(), v.raw)};
}

// Approximate reciprocal square root
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> ApproximateReciprocalSqrt(Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{_mm_rsqrt_ph(v.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> ApproximateReciprocalSqrt(Vec128<float, N> v) {
  return Vec128<float, N>{_mm_rsqrt_ps(v.raw)};
}
HWY_API Vec128<float, 1> ApproximateReciprocalSqrt(Vec128<float, 1> v) {
  return Vec128<float, 1>{_mm_rsqrt_ss(v.raw)};
}

#if HWY_TARGET <= HWY_AVX3
#ifdef HWY_NATIVE_F64_APPROX_RSQRT
#undef HWY_NATIVE_F64_APPROX_RSQRT
#else
#define HWY_NATIVE_F64_APPROX_RSQRT
#endif

HWY_API Vec64<double> ApproximateReciprocalSqrt(Vec64<double> v) {
  return Vec64<double>{_mm_rsqrt14_sd(v.raw, v.raw)};
}
HWY_API Vec128<double> ApproximateReciprocalSqrt(Vec128<double> v) {
#if HWY_COMPILER_MSVC
  const DFromV<decltype(v)> d;
  return Vec128<double>{_mm_mask_rsqrt14_pd(
      Undefined(d).raw, static_cast<__mmask8>(0xFF), v.raw)};
#else
  return Vec128<double>{_mm_rsqrt14_pd(v.raw)};
#endif
}
#endif

// ------------------------------ Min (Gt, IfThenElse)

namespace detail {

template <typename T, size_t N>
HWY_INLINE HWY_MAYBE_UNUSED Vec128<T, N> MinU(const Vec128<T, N> a,
                                              const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;
  const auto msb = Set(du, static_cast<T>(T(1) << (sizeof(T) * 8 - 1)));
  const auto gt = RebindMask(du, BitCast(di, a ^ msb) > BitCast(di, b ^ msb));
  return IfThenElse(gt, b, a);
}

}  // namespace detail

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> Min(Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_min_epu8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> Min(Vec128<uint16_t, N> a, Vec128<uint16_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return detail::MinU(a, b);
#else
  return Vec128<uint16_t, N>{_mm_min_epu16(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<uint32_t, N> Min(Vec128<uint32_t, N> a, Vec128<uint32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return detail::MinU(a, b);
#else
  return Vec128<uint32_t, N>{_mm_min_epu32(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<uint64_t, N> Min(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint64_t, N>{_mm_min_epu64(a.raw, b.raw)};
#else
  return detail::MinU(a, b);
#endif
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> Min(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return IfThenElse(a < b, a, b);
#else
  return Vec128<int8_t, N>{_mm_min_epi8(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<int16_t, N> Min(Vec128<int16_t, N> a, Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_min_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Min(Vec128<int32_t, N> a, Vec128<int32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return IfThenElse(a < b, a, b);
#else
  return Vec128<int32_t, N>{_mm_min_epi32(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<int64_t, N> Min(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<int64_t, N>{_mm_min_epi64(a.raw, b.raw)};
#else
  return IfThenElse(a < b, a, b);
#endif
}

// Float
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Min(Vec128<float16_t, N> a,
                                 Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_min_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Min(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_min_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Min(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{_mm_min_pd(a.raw, b.raw)};
}

// ------------------------------ Max (Gt, IfThenElse)

namespace detail {
template <typename T, size_t N>
HWY_INLINE HWY_MAYBE_UNUSED Vec128<T, N> MaxU(const Vec128<T, N> a,
                                              const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;
  const auto msb = Set(du, static_cast<T>(T(1) << (sizeof(T) * 8 - 1)));
  const auto gt = RebindMask(du, BitCast(di, a ^ msb) > BitCast(di, b ^ msb));
  return IfThenElse(gt, a, b);
}

}  // namespace detail

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> Max(Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{_mm_max_epu8(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> Max(Vec128<uint16_t, N> a, Vec128<uint16_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return detail::MaxU(a, b);
#else
  return Vec128<uint16_t, N>{_mm_max_epu16(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<uint32_t, N> Max(Vec128<uint32_t, N> a, Vec128<uint32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return detail::MaxU(a, b);
#else
  return Vec128<uint32_t, N>{_mm_max_epu32(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<uint64_t, N> Max(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint64_t, N>{_mm_max_epu64(a.raw, b.raw)};
#else
  return detail::MaxU(a, b);
#endif
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> Max(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return IfThenElse(a < b, b, a);
#else
  return Vec128<int8_t, N>{_mm_max_epi8(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<int16_t, N> Max(Vec128<int16_t, N> a, Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_max_epi16(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Max(Vec128<int32_t, N> a, Vec128<int32_t, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  return IfThenElse(a < b, b, a);
#else
  return Vec128<int32_t, N>{_mm_max_epi32(a.raw, b.raw)};
#endif
}
template <size_t N>
HWY_API Vec128<int64_t, N> Max(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<int64_t, N>{_mm_max_epi64(a.raw, b.raw)};
#else
  return IfThenElse(a < b, b, a);
#endif
}

// Float
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Max(Vec128<float16_t, N> a,
                                 Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{_mm_max_ph(a.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Max(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{_mm_max_ps(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Max(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{_mm_max_pd(a.raw, b.raw)};
}

// ================================================== MEMORY (3)

// ------------------------------ Non-temporal stores

// On clang6, we see incorrect code generated for _mm_stream_pi, so
// round even partial vectors up to 16 bytes.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API void Stream(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  _mm_stream_si128(reinterpret_cast<__m128i*>(aligned), BitCast(du, v).raw);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API void Stream(VFromD<D> v, D /* tag */, float* HWY_RESTRICT aligned) {
  _mm_stream_ps(aligned, v.raw);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API void Stream(VFromD<D> v, D /* tag */, double* HWY_RESTRICT aligned) {
  _mm_stream_pd(aligned, v.raw);
}

// ------------------------------ Scatter

// Work around warnings in the intrinsic definitions (passing -1 as a mask).
HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")

// Unfortunately the GCC/Clang intrinsics do not accept int64_t*.
using GatherIndex64 = long long int;  // NOLINT(runtime/int)
static_assert(sizeof(GatherIndex64) == 8, "Must be 64-bit type");

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_SCATTER
#undef HWY_NATIVE_SCATTER
#else
#define HWY_NATIVE_SCATTER
#endif

namespace detail {

template <int kScale, class D, class VI, HWY_IF_UI32_D(D)>
HWY_INLINE void NativeScatter128(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT base,
                                 VI index) {
  if (d.MaxBytes() == 16) {
    _mm_i32scatter_epi32(base, index.raw, v.raw, kScale);
  } else {
    const __mmask8 mask = (1u << MaxLanes(d)) - 1;
    _mm_mask_i32scatter_epi32(base, mask, index.raw, v.raw, kScale);
  }
}

template <int kScale, class D, class VI, HWY_IF_UI64_D(D)>
HWY_INLINE void NativeScatter128(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT base,
                                 VI index) {
  if (d.MaxBytes() == 16) {
    _mm_i64scatter_epi64(base, index.raw, v.raw, kScale);
  } else {
    const __mmask8 mask = (1u << MaxLanes(d)) - 1;
    _mm_mask_i64scatter_epi64(base, mask, index.raw, v.raw, kScale);
  }
}

template <int kScale, class D, class VI, HWY_IF_F32_D(D)>
HWY_INLINE void NativeScatter128(VFromD<D> v, D d, float* HWY_RESTRICT base,
                                 VI index) {
  if (d.MaxBytes() == 16) {
    _mm_i32scatter_ps(base, index.raw, v.raw, kScale);
  } else {
    const __mmask8 mask = (1u << MaxLanes(d)) - 1;
    _mm_mask_i32scatter_ps(base, mask, index.raw, v.raw, kScale);
  }
}

template <int kScale, class D, class VI, HWY_IF_F64_D(D)>
HWY_INLINE void NativeScatter128(VFromD<D> v, D d, double* HWY_RESTRICT base,
                                 VI index) {
  if (d.MaxBytes() == 16) {
    _mm_i64scatter_pd(base, index.raw, v.raw, kScale);
  } else {
    const __mmask8 mask = (1u << MaxLanes(d)) - 1;
    _mm_mask_i64scatter_pd(base, mask, index.raw, v.raw, kScale);
  }
}

template <int kScale, class D, class VI, HWY_IF_UI32_D(D)>
HWY_INLINE void NativeMaskedScatter128(VFromD<D> v, MFromD<D> m, D d,
                                       TFromD<D>* HWY_RESTRICT base, VI index) {
  // For partial vectors, ensure upper mask lanes are zero to prevent faults.
  if (!detail::IsFull(d)) m = And(m, FirstN(d, Lanes(d)));
  _mm_mask_i32scatter_epi32(base, m.raw, index.raw, v.raw, kScale);
}

template <int kScale, class D, class VI, HWY_IF_UI64_D(D)>
HWY_INLINE void NativeMaskedScatter128(VFromD<D> v, MFromD<D> m, D d,
                                       TFromD<D>* HWY_RESTRICT base, VI index) {
  // For partial vectors, ensure upper mask lanes are zero to prevent faults.
  if (!detail::IsFull(d)) m = And(m, FirstN(d, Lanes(d)));
  _mm_mask_i64scatter_epi64(base, m.raw, index.raw, v.raw, kScale);
}

template <int kScale, class D, class VI, HWY_IF_F32_D(D)>
HWY_INLINE void NativeMaskedScatter128(VFromD<D> v, MFromD<D> m, D d,
                                       float* HWY_RESTRICT base, VI index) {
  // For partial vectors, ensure upper mask lanes are zero to prevent faults.
  if (!detail::IsFull(d)) m = And(m, FirstN(d, Lanes(d)));
  _mm_mask_i32scatter_ps(base, m.raw, index.raw, v.raw, kScale);
}

template <int kScale, class D, class VI, HWY_IF_F64_D(D)>
HWY_INLINE void NativeMaskedScatter128(VFromD<D> v, MFromD<D> m, D d,
                                       double* HWY_RESTRICT base, VI index) {
  // For partial vectors, ensure upper mask lanes are zero to prevent faults.
  if (!detail::IsFull(d)) m = And(m, FirstN(d, Lanes(d)));
  _mm_mask_i64scatter_pd(base, m.raw, index.raw, v.raw, kScale);
}

}  // namespace detail

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API void ScatterOffset(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT base,
                           VFromD<RebindToSigned<D>> offset) {
  return detail::NativeScatter128<1>(v, d, base, offset);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API void ScatterIndex(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT base,
                          VFromD<RebindToSigned<D>> index) {
  return detail::NativeScatter128<sizeof(TFromD<D>)>(v, d, base, index);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API void MaskedScatterIndex(VFromD<D> v, MFromD<D> m, D d,
                                TFromD<D>* HWY_RESTRICT base,
                                VFromD<RebindToSigned<D>> index) {
  return detail::NativeMaskedScatter128<sizeof(TFromD<D>)>(v, m, d, base,
                                                           index);
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Gather (Load/Store)

#if HWY_TARGET <= HWY_AVX2

#ifdef HWY_NATIVE_GATHER
#undef HWY_NATIVE_GATHER
#else
#define HWY_NATIVE_GATHER
#endif

namespace detail {

template <int kScale, typename T, size_t N, HWY_IF_UI32(T)>
HWY_INLINE Vec128<T, N> NativeGather128(const T* HWY_RESTRICT base,
                                        Vec128<int32_t, N> indices) {
  return Vec128<T, N>{_mm_i32gather_epi32(
      reinterpret_cast<const int32_t*>(base), indices.raw, kScale)};
}

template <int kScale, typename T, size_t N, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T, N> NativeGather128(const T* HWY_RESTRICT base,
                                        Vec128<int64_t, N> indices) {
  return Vec128<T, N>{_mm_i64gather_epi64(
      reinterpret_cast<const GatherIndex64*>(base), indices.raw, kScale)};
}

template <int kScale, size_t N>
HWY_INLINE Vec128<float, N> NativeGather128(const float* HWY_RESTRICT base,
                                            Vec128<int32_t, N> indices) {
  return Vec128<float, N>{_mm_i32gather_ps(base, indices.raw, kScale)};
}

template <int kScale, size_t N>
HWY_INLINE Vec128<double, N> NativeGather128(const double* HWY_RESTRICT base,
                                             Vec128<int64_t, N> indices) {
  return Vec128<double, N>{_mm_i64gather_pd(base, indices.raw, kScale)};
}

template <int kScale, typename T, size_t N, HWY_IF_UI32(T)>
HWY_INLINE Vec128<T, N> NativeMaskedGatherOr128(Vec128<T, N> no,
                                                Mask128<T, N> m,
                                                const T* HWY_RESTRICT base,
                                                Vec128<int32_t, N> indices) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<T, N>{_mm_mmask_i32gather_epi32(
      no.raw, m.raw, indices.raw, reinterpret_cast<const int32_t*>(base),
      kScale)};
#else
  return Vec128<T, N>{
      _mm_mask_i32gather_epi32(no.raw, reinterpret_cast<const int32_t*>(base),
                               indices.raw, m.raw, kScale)};
#endif
}

template <int kScale, typename T, size_t N, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T, N> NativeMaskedGatherOr128(Vec128<T, N> no,
                                                Mask128<T, N> m,
                                                const T* HWY_RESTRICT base,
                                                Vec128<int64_t, N> indices) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<T, N>{_mm_mmask_i64gather_epi64(
      no.raw, m.raw, indices.raw, reinterpret_cast<const GatherIndex64*>(base),
      kScale)};
#else
  return Vec128<T, N>{_mm_mask_i64gather_epi64(
      no.raw, reinterpret_cast<const GatherIndex64*>(base), indices.raw, m.raw,
      kScale)};
#endif
}

template <int kScale, size_t N>
HWY_INLINE Vec128<float, N> NativeMaskedGatherOr128(
    Vec128<float, N> no, Mask128<float, N> m, const float* HWY_RESTRICT base,
    Vec128<int32_t, N> indices) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<float, N>{
      _mm_mmask_i32gather_ps(no.raw, m.raw, indices.raw, base, kScale)};
#else
  return Vec128<float, N>{
      _mm_mask_i32gather_ps(no.raw, base, indices.raw, m.raw, kScale)};
#endif
}

template <int kScale, size_t N>
HWY_INLINE Vec128<double, N> NativeMaskedGatherOr128(
    Vec128<double, N> no, Mask128<double, N> m, const double* HWY_RESTRICT base,
    Vec128<int64_t, N> indices) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<double, N>{
      _mm_mmask_i64gather_pd(no.raw, m.raw, indices.raw, base, kScale)};
#else
  return Vec128<double, N>{
      _mm_mask_i64gather_pd(no.raw, base, indices.raw, m.raw, kScale)};
#endif
}

}  // namespace detail

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> GatherOffset(D /*d*/, const TFromD<D>* HWY_RESTRICT base,
                               VFromD<RebindToSigned<D>> offsets) {
  return detail::NativeGather128<1>(base, offsets);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), typename T = TFromD<D>>
HWY_API VFromD<D> GatherIndex(D /*d*/, const T* HWY_RESTRICT base,
                              VFromD<RebindToSigned<D>> indices) {
  return detail::NativeGather128<sizeof(T)>(base, indices);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), typename T = TFromD<D>>
HWY_API VFromD<D> MaskedGatherIndexOr(VFromD<D> no, MFromD<D> m, D d,
                                      const T* HWY_RESTRICT base,
                                      VFromD<RebindToSigned<D>> indices) {
  // For partial vectors, ensure upper mask lanes are zero to prevent faults.
  if (!detail::IsFull(d)) m = And(m, FirstN(d, Lanes(d)));

  return detail::NativeMaskedGatherOr128<sizeof(T)>(no, m, base, indices);
}

// Generic for all vector lengths.
template <class D>
HWY_API VFromD<D> MaskedGatherIndex(MFromD<D> m, D d,
                                    const TFromD<D>* HWY_RESTRICT base,
                                    VFromD<RebindToSigned<D>> indices) {
  return MaskedGatherIndexOr(Zero(d), m, d, base, indices);
}

#endif  // HWY_TARGET <= HWY_AVX2

HWY_DIAGNOSTICS(pop)

// ================================================== SWIZZLE (2)

// ------------------------------ LowerHalf

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> LowerHalf(D /* tag */, VFromD<Twice<D>> v) {
  return VFromD<D>{v.raw};
}
template <typename T, size_t N>
HWY_API Vec128<T, N / 2> LowerHalf(Vec128<T, N> v) {
  return Vec128<T, N / 2>{v.raw};
}

// ------------------------------ ShiftLeftBytes

template <int kBytes, class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ShiftLeftBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, VFromD<decltype(du)>{_mm_slli_si128(BitCast(du, v).raw, kBytes)});
}

// Generic for all vector lengths.
template <int kBytes, class V>
HWY_API V ShiftLeftBytes(const V v) {
  return ShiftLeftBytes<kBytes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftLeftLanes

// Generic for all vector lengths.
template <int kLanes, class D>
HWY_API VFromD<D> ShiftLeftLanes(D d, const VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftLeftBytes<kLanes * sizeof(TFromD<D>)>(BitCast(d8, v)));
}

// Generic for all vector lengths.
template <int kLanes, class V>
HWY_API V ShiftLeftLanes(const V v) {
  return ShiftLeftLanes<kLanes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftRightBytes
template <int kBytes, class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ShiftRightBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  const RebindToUnsigned<decltype(d)> du;
  // For partial vectors, clear upper lanes so we shift in zeros.
  if (d.MaxBytes() != 16) {
    const Full128<TFromD<D>> dfull;
    const VFromD<decltype(dfull)> vfull{v.raw};
    v = VFromD<D>{IfThenElseZero(FirstN(dfull, MaxLanes(d)), vfull).raw};
  }
  return BitCast(
      d, VFromD<decltype(du)>{_mm_srli_si128(BitCast(du, v).raw, kBytes)});
}

// ------------------------------ ShiftRightLanes
// Generic for all vector lengths.
template <int kLanes, class D>
HWY_API VFromD<D> ShiftRightLanes(D d, const VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr size_t kBytes = kLanes * sizeof(TFromD<D>);
  return BitCast(d, ShiftRightBytes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ UpperHalf (ShiftRightBytes)

// Full input: copy hi into lo (smaller instruction encoding than shifts).
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API VFromD<D> UpperHalf(D d, VFromD<Twice<D>> v) {
  const Twice<RebindToUnsigned<decltype(d)>> dut;
  using VUT = VFromD<decltype(dut)>;  // for float16_t
  const VUT vut = BitCast(dut, v);
  return BitCast(d, LowerHalf(VUT{_mm_unpackhi_epi64(vut.raw, vut.raw)}));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> UpperHalf(D /* tag */, Vec128<float> v) {
  return Vec64<float>{_mm_movehl_ps(v.raw, v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F64_D(D)>
HWY_API Vec64<double> UpperHalf(D /* tag */, Vec128<double> v) {
  return Vec64<double>{_mm_unpackhi_pd(v.raw, v.raw)};
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 4)>
HWY_API VFromD<D> UpperHalf(D d, VFromD<Twice<D>> v) {
  return LowerHalf(d, ShiftRightBytes<d.MaxBytes()>(Twice<D>(), v));
}

// ------------------------------ ExtractLane (UpperHalf)

namespace detail {

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  const int pair = _mm_extract_epi16(v.raw, kLane / 2);
  constexpr int kShift = kLane & 1 ? 8 : 0;
  return static_cast<T>((pair >> kShift) & 0xFF);
#else
  return static_cast<T>(_mm_extract_epi8(v.raw, kLane) & 0xFF);
#endif
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  static_assert(kLane < N, "Lane index out of bounds");
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const uint16_t lane = static_cast<uint16_t>(
      _mm_extract_epi16(BitCast(du, v).raw, kLane) & 0xFFFF);
  return BitCastScalar<T>(lane);
}

template <size_t kLane, typename T, size_t N, HWY_IF_UI32(T)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  return static_cast<T>(_mm_cvtsi128_si32(
      (kLane == 0) ? v.raw : _mm_shuffle_epi32(v.raw, kLane)));
#else
  return static_cast<T>(_mm_extract_epi32(v.raw, kLane));
#endif
}

template <size_t kLane, typename T, size_t N, HWY_IF_UI64(T)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_ARCH_X86_32
  alignas(16) T lanes[2];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[kLane];
#elif HWY_TARGET >= HWY_SSSE3
  return static_cast<T>(
      _mm_cvtsi128_si64((kLane == 0) ? v.raw : _mm_shuffle_epi32(v.raw, 0xEE)));
#else
  return static_cast<T>(_mm_extract_epi64(v.raw, kLane));
#endif
}

template <size_t kLane, size_t N>
HWY_INLINE float ExtractLane(const Vec128<float, N> v) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  return _mm_cvtss_f32((kLane == 0) ? v.raw
                                    : _mm_shuffle_ps(v.raw, v.raw, kLane));
#else
  // Bug in the intrinsic, returns int but should be float.
  const int32_t bits = _mm_extract_ps(v.raw, kLane);
  return BitCastScalar<float>(bits);
#endif
}

// There is no extract_pd; two overloads because there is no UpperHalf for N=1.
template <size_t kLane>
HWY_INLINE double ExtractLane(const Vec64<double> v) {
  static_assert(kLane == 0, "Lane index out of bounds");
  return GetLane(v);
}

template <size_t kLane>
HWY_INLINE double ExtractLane(const Vec128<double> v) {
  static_assert(kLane < 2, "Lane index out of bounds");
  const Half<DFromV<decltype(v)>> dh;
  return kLane == 0 ? GetLane(v) : GetLane(UpperHalf(dh, v));
}

}  // namespace detail

// Requires one overload per vector length because ExtractLane<3> may be a
// compile error if it calls _mm_extract_epi64.
template <typename T>
HWY_API T ExtractLane(const Vec128<T, 1> v, size_t i) {
  HWY_DASSERT(i == 0);
  (void)i;
  return GetLane(v);
}

template <typename T>
HWY_API T ExtractLane(const Vec128<T, 2> v, size_t i) {
#if !HWY_IS_DEBUG_BUILD && HWY_COMPILER_GCC  // includes clang
  if (__builtin_constant_p(i)) {
    switch (i) {
      case 0:
        return detail::ExtractLane<0>(v);
      case 1:
        return detail::ExtractLane<1>(v);
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
        return detail::ExtractLane<0>(v);
      case 1:
        return detail::ExtractLane<1>(v);
      case 2:
        return detail::ExtractLane<2>(v);
      case 3:
        return detail::ExtractLane<3>(v);
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
        return detail::ExtractLane<0>(v);
      case 1:
        return detail::ExtractLane<1>(v);
      case 2:
        return detail::ExtractLane<2>(v);
      case 3:
        return detail::ExtractLane<3>(v);
      case 4:
        return detail::ExtractLane<4>(v);
      case 5:
        return detail::ExtractLane<5>(v);
      case 6:
        return detail::ExtractLane<6>(v);
      case 7:
        return detail::ExtractLane<7>(v);
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
        return detail::ExtractLane<0>(v);
      case 1:
        return detail::ExtractLane<1>(v);
      case 2:
        return detail::ExtractLane<2>(v);
      case 3:
        return detail::ExtractLane<3>(v);
      case 4:
        return detail::ExtractLane<4>(v);
      case 5:
        return detail::ExtractLane<5>(v);
      case 6:
        return detail::ExtractLane<6>(v);
      case 7:
        return detail::ExtractLane<7>(v);
      case 8:
        return detail::ExtractLane<8>(v);
      case 9:
        return detail::ExtractLane<9>(v);
      case 10:
        return detail::ExtractLane<10>(v);
      case 11:
        return detail::ExtractLane<11>(v);
      case 12:
        return detail::ExtractLane<12>(v);
      case 13:
        return detail::ExtractLane<13>(v);
      case 14:
        return detail::ExtractLane<14>(v);
      case 15:
        return detail::ExtractLane<15>(v);
    }
  }
#endif
  alignas(16) T lanes[16];
  Store(v, DFromV<decltype(v)>(), lanes);
  return lanes[i];
}

// ------------------------------ InsertLane (UpperHalf)

namespace detail {

template <class V>
HWY_INLINE V InsertLaneUsingBroadcastAndBlend(V v, size_t i, TFromV<V> t) {
  const DFromV<decltype(v)> d;

#if HWY_TARGET <= HWY_AVX3
  using RawMask = decltype(MaskFromVec(VFromD<decltype(d)>()).raw);
  const auto mask = MFromD<decltype(d)>{static_cast<RawMask>(uint64_t{1} << i)};
#else
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  const auto mask = RebindMask(d, Iota(du, 0) == Set(du, static_cast<TU>(i)));
#endif

  return IfThenElse(mask, Set(d, t), v);
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  return InsertLaneUsingBroadcastAndBlend(v, kLane, t);
#else
  return Vec128<T, N>{_mm_insert_epi8(v.raw, t, kLane)};
#endif
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const uint16_t bits = BitCastScalar<uint16_t>(t);
  return BitCast(d, VFromD<decltype(du)>{
                        _mm_insert_epi16(BitCast(du, v).raw, bits, kLane)});
}

template <size_t kLane, typename T, size_t N, HWY_IF_UI32(T)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  return InsertLaneUsingBroadcastAndBlend(v, kLane, t);
#else
  const MakeSigned<T> ti = BitCastScalar<MakeSigned<T>>(t);
  return Vec128<T, N>{_mm_insert_epi32(v.raw, ti, kLane)};
#endif
}

template <size_t kLane, typename T, size_t N, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3 || HWY_ARCH_X86_32
  const DFromV<decltype(v)> d;
  const RebindToFloat<decltype(d)> df;
  const auto vt = BitCast(df, Set(d, t));
  if (kLane == 0) {
    return BitCast(
        d, Vec128<double, N>{_mm_shuffle_pd(vt.raw, BitCast(df, v).raw, 2)});
  }
  return BitCast(
      d, Vec128<double, N>{_mm_shuffle_pd(BitCast(df, v).raw, vt.raw, 0)});
#else
  const MakeSigned<T> ti = BitCastScalar<MakeSigned<T>>(t);
  return Vec128<T, N>{_mm_insert_epi64(v.raw, ti, kLane)};
#endif
}

template <size_t kLane, size_t N>
HWY_INLINE Vec128<float, N> InsertLane(const Vec128<float, N> v, float t) {
  static_assert(kLane < N, "Lane index out of bounds");
#if HWY_TARGET >= HWY_SSSE3
  return InsertLaneUsingBroadcastAndBlend(v, kLane, t);
#else
  return Vec128<float, N>{_mm_insert_ps(v.raw, _mm_set_ss(t), kLane << 4)};
#endif
}

// There is no insert_pd; two overloads because there is no UpperHalf for N=1.
template <size_t kLane>
HWY_INLINE Vec128<double, 1> InsertLane(const Vec128<double, 1> v, double t) {
  static_assert(kLane == 0, "Lane index out of bounds");
  return Set(DFromV<decltype(v)>(), t);
}

template <size_t kLane>
HWY_INLINE Vec128<double> InsertLane(const Vec128<double> v, double t) {
  static_assert(kLane < 2, "Lane index out of bounds");
  const DFromV<decltype(v)> d;
  const Vec128<double> vt = Set(d, t);
  if (kLane == 0) {
    return Vec128<double>{_mm_shuffle_pd(vt.raw, v.raw, 2)};
  }
  return Vec128<double>{_mm_shuffle_pd(v.raw, vt.raw, 0)};
}

}  // namespace detail

// Requires one overload per vector length because InsertLane<3> may be a
// compile error if it calls _mm_insert_epi64.

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
  return detail::InsertLaneUsingBroadcastAndBlend(v, i, t);
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
  return detail::InsertLaneUsingBroadcastAndBlend(v, i, t);
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
  return detail::InsertLaneUsingBroadcastAndBlend(v, i, t);
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
  return detail::InsertLaneUsingBroadcastAndBlend(v, i, t);
}

// ------------------------------ CombineShiftRightBytes

#if HWY_TARGET == HWY_SSE2
template <int kBytes, class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  static_assert(0 < kBytes && kBytes < 16, "kBytes invalid");
  return Or(ShiftRightBytes<kBytes>(d, lo), ShiftLeftBytes<16 - kBytes>(d, hi));
}
template <int kBytes, class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  constexpr size_t kSize = d.MaxBytes();
  static_assert(0 < kBytes && kBytes < kSize, "kBytes invalid");

  const Twice<decltype(d)> dt;
  return VFromD<D>{ShiftRightBytes<kBytes>(dt, Combine(dt, hi, lo)).raw};
}
#else
template <int kBytes, class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Vec128<uint8_t>{_mm_alignr_epi8(
                        BitCast(d8, hi).raw, BitCast(d8, lo).raw, kBytes)});
}

template <int kBytes, class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  constexpr size_t kSize = d.MaxBytes();
  static_assert(0 < kBytes && kBytes < kSize, "kBytes invalid");
  const Repartition<uint8_t, decltype(d)> d8;
  using V8 = Vec128<uint8_t>;
  const DFromV<V8> dfull8;
  const Repartition<TFromD<D>, decltype(dfull8)> dfull;
  const V8 hi8{BitCast(d8, hi).raw};
  // Move into most-significant bytes
  const V8 lo8 = ShiftLeftBytes<16 - kSize>(V8{BitCast(d8, lo).raw});
  const V8 r = CombineShiftRightBytes<16 - kSize + kBytes>(dfull8, hi8, lo8);
  return VFromD<D>{BitCast(dfull, r).raw};
}
#endif

// ------------------------------ Broadcast/splat any lane

template <int kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const VU vu = BitCast(du, v);  // for float16_t
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  if (kLane < 4) {
    const __m128i lo = _mm_shufflelo_epi16(vu.raw, (0x55 * kLane) & 0xFF);
    return BitCast(d, VU{_mm_unpacklo_epi64(lo, lo)});
  } else {
    const __m128i hi = _mm_shufflehi_epi16(vu.raw, (0x55 * (kLane - 4)) & 0xFF);
    return BitCast(d, VU{_mm_unpackhi_epi64(hi, hi)});
  }
}

template <int kLane, typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{_mm_shuffle_epi32(v.raw, 0x55 * kLane)};
}

template <int kLane, typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{_mm_shuffle_epi32(v.raw, kLane ? 0xEE : 0x44)};
}

template <int kLane, size_t N>
HWY_API Vec128<float, N> Broadcast(const Vec128<float, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<float, N>{_mm_shuffle_ps(v.raw, v.raw, 0x55 * kLane)};
}

template <int kLane, size_t N>
HWY_API Vec128<double, N> Broadcast(const Vec128<double, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<double, N>{_mm_shuffle_pd(v.raw, v.raw, 3 * kLane)};
}

// ------------------------------ TableLookupLanes (Shuffle01)

// Returned by SetTableIndices/IndicesFromVec for use by TableLookupLanes.
template <typename T, size_t N = 16 / sizeof(T)>
struct Indices128 {
  __m128i raw;
};

template <class D, typename T = TFromD<D>, typename TI, size_t kN,
          HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE(T, 1)>
HWY_API Indices128<T, kN> IndicesFromVec(D d, Vec128<TI, kN> vec) {
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const Rebind<TI, decltype(d)> di;
  HWY_DASSERT(AllFalse(di, Lt(vec, Zero(di))) &&
              AllTrue(di, Lt(vec, Set(di, kN * 2))));
#endif

  // No change as byte indices are always used for 8-bit lane types
  (void)d;
  return Indices128<T, kN>{vec.raw};
}

template <class D, typename T = TFromD<D>, typename TI, size_t kN,
          HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE(T, 2)>
HWY_API Indices128<T, kN> IndicesFromVec(D d, Vec128<TI, kN> vec) {
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const Rebind<TI, decltype(d)> di;
  HWY_DASSERT(AllFalse(di, Lt(vec, Zero(di))) &&
              AllTrue(di, Lt(vec, Set(di, kN * 2))));
#endif

#if HWY_TARGET <= HWY_AVX3 || HWY_TARGET == HWY_SSE2
  (void)d;
  return Indices128<T, kN>{vec.raw};
#else   // SSSE3, SSE4, or AVX2
  const Repartition<uint8_t, decltype(d)> d8;
  using V8 = VFromD<decltype(d8)>;
  alignas(16) static constexpr uint8_t kByteOffsets[16] = {
      0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1};

  // Broadcast each lane index to all 4 bytes of T
  alignas(16) static constexpr uint8_t kBroadcastLaneBytes[16] = {
      0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
  const V8 lane_indices = TableLookupBytes(vec, Load(d8, kBroadcastLaneBytes));

  // Shift to bytes
  const Repartition<uint16_t, decltype(d)> d16;
  const V8 byte_indices = BitCast(d8, ShiftLeft<1>(BitCast(d16, lane_indices)));

  return Indices128<T, kN>{Add(byte_indices, Load(d8, kByteOffsets)).raw};
#endif  // HWY_TARGET <= HWY_AVX3 || HWY_TARGET == HWY_SSE2
}

template <class D, typename T = TFromD<D>, typename TI, size_t kN,
          HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE(T, 4)>
HWY_API Indices128<T, kN> IndicesFromVec(D d, Vec128<TI, kN> vec) {
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const Rebind<TI, decltype(d)> di;
  HWY_DASSERT(AllFalse(di, Lt(vec, Zero(di))) &&
              AllTrue(di, Lt(vec, Set(di, kN * 2))));
#endif

#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_SSE2
  (void)d;
  return Indices128<T, kN>{vec.raw};
#else
  const Repartition<uint8_t, decltype(d)> d8;
  using V8 = VFromD<decltype(d8)>;
  alignas(16) static constexpr uint8_t kByteOffsets[16] = {
      0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3};

  // Broadcast each lane index to all 4 bytes of T
  alignas(16) static constexpr uint8_t kBroadcastLaneBytes[16] = {
      0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
  const V8 lane_indices = TableLookupBytes(vec, Load(d8, kBroadcastLaneBytes));

  // Shift to bytes
  const Repartition<uint16_t, decltype(d)> d16;
  const V8 byte_indices = BitCast(d8, ShiftLeft<2>(BitCast(d16, lane_indices)));

  return Indices128<T, kN>{Add(byte_indices, Load(d8, kByteOffsets)).raw};
#endif
}

template <class D, typename T = TFromD<D>, typename TI, size_t kN,
          HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE(T, 8)>
HWY_API Indices128<T, kN> IndicesFromVec(D d, Vec128<TI, kN> vec) {
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane");
#if HWY_IS_DEBUG_BUILD
  const Rebind<TI, decltype(d)> di;
  HWY_DASSERT(AllFalse(di, Lt(vec, Zero(di))) &&
              AllTrue(di, Lt(vec, Set(di, static_cast<TI>(kN * 2)))));
#else
  (void)d;
#endif

  // No change - even without AVX3, we can shuffle+blend.
  return Indices128<T, kN>{vec.raw};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), typename TI>
HWY_API Indices128<TFromD<D>, HWY_MAX_LANES_D(D)> SetTableIndices(
    D d, const TI* idx) {
  static_assert(sizeof(TFromD<D>) == sizeof(TI), "Index size must match lane");
  const Rebind<TI, decltype(d)> di;
  return IndicesFromVec(d, LoadU(di, idx));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
  return TableLookupBytes(v, Vec128<T, N>{idx.raw});
}

template <typename T, size_t N, HWY_IF_UI16(T)>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
#if HWY_TARGET <= HWY_AVX3
  return {_mm_permutexvar_epi16(idx.raw, v.raw)};
#elif HWY_TARGET == HWY_SSE2
#if HWY_COMPILER_GCC_ACTUAL && HWY_HAS_BUILTIN(__builtin_shuffle)
  typedef uint16_t GccU16RawVectType __attribute__((__vector_size__(16)));
  return Vec128<T, N>{reinterpret_cast<typename detail::Raw128<T>::type>(
      __builtin_shuffle(reinterpret_cast<GccU16RawVectType>(v.raw),
                        reinterpret_cast<GccU16RawVectType>(idx.raw)))};
#else
  const Full128<T> d_full;
  alignas(16) T src_lanes[8];
  alignas(16) uint16_t indices[8];
  alignas(16) T result_lanes[8];

  Store(Vec128<T>{v.raw}, d_full, src_lanes);
  _mm_store_si128(reinterpret_cast<__m128i*>(indices), idx.raw);

  for (int i = 0; i < 8; i++) {
    result_lanes[i] = src_lanes[indices[i] & 7u];
  }

  return Vec128<T, N>{Load(d_full, result_lanes).raw};
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_HAS_BUILTIN(__builtin_shuffle)
#else
  return TableLookupBytes(v, Vec128<T, N>{idx.raw});
#endif
}

#if HWY_HAVE_FLOAT16
template <size_t N, HWY_IF_V_SIZE_GT(float16_t, N, 2)>
HWY_API Vec128<float16_t, N> TableLookupLanes(Vec128<float16_t, N> v,
                                              Indices128<float16_t, N> idx) {
  return {_mm_permutexvar_ph(idx.raw, v.raw)};
}
#endif  // HWY_HAVE_FLOAT16

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
#if HWY_TARGET <= HWY_AVX2
  const DFromV<decltype(v)> d;
  const RebindToFloat<decltype(d)> df;
  const Vec128<float, N> perm{_mm_permutevar_ps(BitCast(df, v).raw, idx.raw)};
  return BitCast(d, perm);
#elif HWY_TARGET == HWY_SSE2
#if HWY_COMPILER_GCC_ACTUAL && HWY_HAS_BUILTIN(__builtin_shuffle)
  typedef uint32_t GccU32RawVectType __attribute__((__vector_size__(16)));
  return Vec128<T, N>{reinterpret_cast<typename detail::Raw128<T>::type>(
      __builtin_shuffle(reinterpret_cast<GccU32RawVectType>(v.raw),
                        reinterpret_cast<GccU32RawVectType>(idx.raw)))};
#else
  const Full128<T> d_full;
  alignas(16) T src_lanes[4];
  alignas(16) uint32_t indices[4];
  alignas(16) T result_lanes[4];

  Store(Vec128<T>{v.raw}, d_full, src_lanes);
  _mm_store_si128(reinterpret_cast<__m128i*>(indices), idx.raw);

  for (int i = 0; i < 4; i++) {
    result_lanes[i] = src_lanes[indices[i] & 3u];
  }

  return Vec128<T, N>{Load(d_full, result_lanes).raw};
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_HAS_BUILTIN(__builtin_shuffle)
#else   // SSSE3 or SSE4
  return TableLookupBytes(v, Vec128<T, N>{idx.raw});
#endif
}

#if HWY_TARGET <= HWY_SSSE3
template <size_t N, HWY_IF_V_SIZE_GT(float, N, 4)>
HWY_API Vec128<float, N> TableLookupLanes(Vec128<float, N> v,
                                          Indices128<float, N> idx) {
#if HWY_TARGET <= HWY_AVX2
  return Vec128<float, N>{_mm_permutevar_ps(v.raw, idx.raw)};
#else   // SSSE3 or SSE4
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;
  return BitCast(df,
                 TableLookupBytes(BitCast(di, v), Vec128<int32_t, N>{idx.raw}));
#endif  // HWY_TARGET <= HWY_AVX2
}
#endif  // HWY_TARGET <= HWY_SSSE3

// Single lane: no change
template <typename T>
HWY_API Vec128<T, 1> TableLookupLanes(Vec128<T, 1> v,
                                      Indices128<T, 1> /* idx */) {
  return v;
}

template <typename T, HWY_IF_UI64(T)>
HWY_API Vec128<T> TableLookupLanes(Vec128<T> v, Indices128<T> idx) {
  const DFromV<decltype(v)> d;
  Vec128<int64_t> vidx{idx.raw};
#if HWY_TARGET <= HWY_AVX2
  // There is no _mm_permute[x]var_epi64.
  vidx += vidx;  // bit1 is the decider (unusual)
  const RebindToFloat<decltype(d)> df;
  return BitCast(
      d, Vec128<double>{_mm_permutevar_pd(BitCast(df, v).raw, vidx.raw)});
#else
  // Only 2 lanes: can swap+blend. Choose v if vidx == iota. To avoid a 64-bit
  // comparison (expensive on SSSE3), just invert the upper lane and subtract 1
  // to obtain an all-zero or all-one mask.
  const RebindToSigned<decltype(d)> di;
  const Vec128<int64_t> same = (vidx ^ Iota(di, 0)) - Set(di, 1);
  const Mask128<T> mask_same = RebindMask(d, MaskFromVec(same));
  return IfThenElse(mask_same, v, Shuffle01(v));
#endif
}

HWY_API Vec128<double> TableLookupLanes(Vec128<double> v,
                                        Indices128<double> idx) {
  Vec128<int64_t> vidx{idx.raw};
#if HWY_TARGET <= HWY_AVX2
  vidx += vidx;  // bit1 is the decider (unusual)
  return Vec128<double>{_mm_permutevar_pd(v.raw, vidx.raw)};
#else
  // Only 2 lanes: can swap+blend. Choose v if vidx == iota. To avoid a 64-bit
  // comparison (expensive on SSSE3), just invert the upper lane and subtract 1
  // to obtain an all-zero or all-one mask.
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const Vec128<int64_t> same = (vidx ^ Iota(di, 0)) - Set(di, 1);
  const Mask128<double> mask_same = RebindMask(d, MaskFromVec(same));
  return IfThenElse(mask_same, v, Shuffle01(v));
#endif
}

// ------------------------------ ReverseBlocks

// Single block: no change
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ReverseBlocks(D /* tag */, VFromD<D> v) {
  return v;
}

// ------------------------------ Reverse (Shuffle0123, Shuffle2301)

// Single lane: no change
template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> Reverse(D /* tag */, VFromD<D> v) {
  return v;
}

// 32-bit x2: shuffle
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse(D /* tag */, const VFromD<D> v) {
  return VFromD<D>{Shuffle2301(Vec128<TFromD<D>>{v.raw}).raw};
}

// 64-bit x2: shuffle
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse(D /* tag */, const VFromD<D> v) {
  return Shuffle01(v);
}

// 32-bit x4: shuffle
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse(D /* tag */, const VFromD<D> v) {
  return Shuffle0123(v);
}

// 16-bit
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2),
          HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Reverse(D d, const VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const VU vu = BitCast(du, v);  // for float16_t
  constexpr size_t kN = MaxLanes(d);
  if (kN == 1) return v;
  if (kN == 2) {
    return BitCast(d, VU{_mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(0, 1, 0, 1))});
  }
  if (kN == 4) {
    return BitCast(d, VU{_mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(0, 1, 2, 3))});
  }

#if HWY_TARGET == HWY_SSE2
  const VU rev4{
      _mm_shufflehi_epi16(_mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(0, 1, 2, 3)),
                          _MM_SHUFFLE(0, 1, 2, 3))};
  return BitCast(d, VU{_mm_shuffle_epi32(rev4.raw, _MM_SHUFFLE(1, 0, 3, 2))});
#else
  const RebindToSigned<decltype(d)> di;
  const VFromD<decltype(di)> shuffle = Dup128VecFromValues(
      di, 0x0F0E, 0x0D0C, 0x0B0A, 0x0908, 0x0706, 0x0504, 0x0302, 0x0100);
  return BitCast(d, TableLookupBytes(v, shuffle));
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1),
          HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Reverse(D d, const VFromD<D> v) {
  constexpr int kN = static_cast<int>(MaxLanes(d));
  if (kN == 1) return v;
#if HWY_TARGET <= HWY_SSSE3
  // NOTE: Lanes with negative shuffle control mask values are set to zero.
  alignas(16) static constexpr int8_t kReverse[16] = {
      kN - 1, kN - 2,  kN - 3,  kN - 4,  kN - 5,  kN - 6,  kN - 7,  kN - 8,
      kN - 9, kN - 10, kN - 11, kN - 12, kN - 13, kN - 14, kN - 15, kN - 16};
  const RebindToSigned<decltype(d)> di;
  const VFromD<decltype(di)> idx = Load(di, kReverse);
  return VFromD<D>{_mm_shuffle_epi8(BitCast(di, v).raw, idx.raw)};
#else
  const RepartitionToWide<decltype(d)> d16;
  return BitCast(d, Reverse(d16, RotateRight<8>(BitCast(d16, v))));
#endif
}

// ------------------------------ Reverse2

// Single lane: no change
template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> Reverse2(D /* tag */, VFromD<D> v) {
  return v;
}

// Generic for all vector lengths (128-bit sufficient if SSE2).
template <class D, HWY_IF_T_SIZE_D(D, 2), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
#if HWY_TARGET <= HWY_AVX3
  const Repartition<uint32_t, decltype(d)> du32;
  return BitCast(d, RotateRight<16>(BitCast(du32, v)));
#elif HWY_TARGET == HWY_SSE2
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const VU vu = BitCast(du, v);  // for float16_t
  constexpr size_t kN = MaxLanes(d);
  __m128i shuf_result = _mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(2, 3, 0, 1));
  if (kN > 4) {
    shuf_result = _mm_shufflehi_epi16(shuf_result, _MM_SHUFFLE(2, 3, 0, 1));
  }
  return BitCast(d, VU{shuf_result});
#else
  const RebindToSigned<decltype(d)> di;
  const VFromD<decltype(di)> shuffle = Dup128VecFromValues(
      di, 0x0302, 0x0100, 0x0706, 0x0504, 0x0B0A, 0x0908, 0x0F0E, 0x0D0C);
  return BitCast(d, TableLookupBytes(v, shuffle));
#endif
}

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_D(D, 4), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Reverse2(D /* tag */, VFromD<D> v) {
  return Shuffle2301(v);
}

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_D(D, 8), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Reverse2(D /* tag */, VFromD<D> v) {
  return Shuffle01(v);
}

// ------------------------------ Reverse4

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const VU vu = BitCast(du, v);  // for float16_t
  // 4x 16-bit: a single shufflelo suffices.
  constexpr size_t kN = MaxLanes(d);
  if (kN <= 4) {
    return BitCast(d, VU{_mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(0, 1, 2, 3))});
  }

#if HWY_TARGET == HWY_SSE2
  return BitCast(d, VU{_mm_shufflehi_epi16(
                        _mm_shufflelo_epi16(vu.raw, _MM_SHUFFLE(0, 1, 2, 3)),
                        _MM_SHUFFLE(0, 1, 2, 3))});
#else
  const RebindToSigned<decltype(d)> di;
  const VFromD<decltype(di)> shuffle = Dup128VecFromValues(
      di, 0x0706, 0x0504, 0x0302, 0x0100, 0x0F0E, 0x0D0C, 0x0B0A, 0x0908);
  return BitCast(d, TableLookupBytes(v, shuffle));
#endif
}

// Generic for all vector lengths.
template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse4(D /* tag */, const VFromD<D> v) {
  return Shuffle0123(v);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse4(D /* tag */, VFromD<D> /* v */) {
  HWY_ASSERT(0);  // don't have 4 u64 lanes
}

// ------------------------------ Reverse8

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
#if HWY_TARGET == HWY_SSE2
  const RepartitionToWide<decltype(d)> dw;
  return Reverse2(d, BitCast(d, Shuffle0123(BitCast(dw, v))));
#else
  const RebindToSigned<decltype(d)> di;
  const VFromD<decltype(di)> shuffle = Dup128VecFromValues(
      di, 0x0F0E, 0x0D0C, 0x0B0A, 0x0908, 0x0706, 0x0504, 0x0302, 0x0100);
  return BitCast(d, TableLookupBytes(v, shuffle));
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API VFromD<D> Reverse8(D /* tag */, VFromD<D> /* v */) {
  HWY_ASSERT(0);  // don't have 8 lanes if larger than 16-bit
}

// ------------------------------ ReverseBits in x86_512

// ------------------------------ InterleaveUpper (UpperHalf)

// Full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_unpackhi_epi8(a.raw, b.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;  // for float16_t
  return BitCast(
      d, VU{_mm_unpackhi_epi16(BitCast(du, a).raw, BitCast(du, b).raw)});
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_unpackhi_epi32(a.raw, b.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_unpackhi_epi64(a.raw, b.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_unpackhi_ps(a.raw, b.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> InterleaveUpper(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_unpackhi_pd(a.raw, b.raw)};
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> InterleaveUpper(D d, VFromD<D> a, VFromD<D> b) {
  const Half<decltype(d)> d2;
  return InterleaveLower(d, VFromD<D>{UpperHalf(d2, a).raw},
                         VFromD<D>{UpperHalf(d2, b).raw});
}

// -------------------------- I8/U8 Broadcast (InterleaveLower, InterleaveUpper)

template <int kLane, class T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  const DFromV<decltype(v)> d;

#if HWY_TARGET == HWY_SSE2
  const Full128<T> d_full;
  const Vec128<T> v_full{v.raw};
  const auto v_interleaved = (kLane < 8)
                                 ? InterleaveLower(d_full, v_full, v_full)
                                 : InterleaveUpper(d_full, v_full, v_full);
  return ResizeBitCast(
      d, Broadcast<kLane & 7>(BitCast(Full128<uint16_t>(), v_interleaved)));
#else
  return TableLookupBytes(v, Set(d, static_cast<T>(kLane)));
#endif
}

// ------------------------------ ZipLower/ZipUpper (InterleaveLower)

// Same as Interleave*, except that the return lanes are double-width integers;
// this is necessary because the single-lane scalar cannot return two values.
// Generic for all vector lengths.
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

// ================================================== CONVERT (1)

// ------------------------------ PromoteTo unsigned (TableLookupBytesOr0)
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const __m128i zero = _mm_setzero_si128();
  return VFromD<D>{_mm_unpacklo_epi8(v.raw, zero)};
#else
  return VFromD<D>{_mm_cvtepu8_epi16(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  return VFromD<D>{_mm_unpacklo_epi16(v.raw, _mm_setzero_si128())};
#else
  return VFromD<D>{_mm_cvtepu16_epi32(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  return VFromD<D>{_mm_unpacklo_epi32(v.raw, _mm_setzero_si128())};
#else
  return VFromD<D>{_mm_cvtepu32_epi64(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const __m128i zero = _mm_setzero_si128();
  const __m128i u16 = _mm_unpacklo_epi8(v.raw, zero);
  return VFromD<D>{_mm_unpacklo_epi16(u16, zero)};
#else
  return VFromD<D>{_mm_cvtepu8_epi32(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint8_t, D>> v) {
#if HWY_TARGET > HWY_SSSE3
  const Rebind<uint32_t, decltype(d)> du32;
  return PromoteTo(d, PromoteTo(du32, v));
#elif HWY_TARGET == HWY_SSSE3
  alignas(16) static constexpr int8_t kShuffle[16] = {
      0, -1, -1, -1, -1, -1, -1, -1, 1, -1, -1, -1, -1, -1, -1, -1};
  const Repartition<int8_t, decltype(d)> di8;
  return TableLookupBytesOr0(v, BitCast(d, Load(di8, kShuffle)));
#else
  (void)d;
  return VFromD<D>{_mm_cvtepu8_epi64(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<uint16_t, D>> v) {
#if HWY_TARGET > HWY_SSSE3
  const Rebind<uint32_t, decltype(d)> du32;
  return PromoteTo(d, PromoteTo(du32, v));
#elif HWY_TARGET == HWY_SSSE3
  alignas(16) static constexpr int8_t kShuffle[16] = {
      0, 1, -1, -1, -1, -1, -1, -1, 2, 3, -1, -1, -1, -1, -1, -1};
  const Repartition<int8_t, decltype(d)> di8;
  return TableLookupBytesOr0(v, BitCast(d, Load(di8, kShuffle)));
#else
  (void)d;
  return VFromD<D>{_mm_cvtepu16_epi64(v.raw)};
#endif
}

// Unsigned to signed: same plus cast.
template <class D, class V, HWY_IF_SIGNED_D(D), HWY_IF_UNSIGNED_V(V),
          HWY_IF_LANES_GT(sizeof(TFromD<D>), sizeof(TFromV<V>)),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V))>
HWY_API VFromD<D> PromoteTo(D di, V v) {
  const RebindToUnsigned<decltype(di)> du;
  return BitCast(di, PromoteTo(du, v));
}

// ------------------------------ PromoteTo signed (ShiftRight, ZipLower)

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  return ShiftRight<8>(VFromD<D>{_mm_unpacklo_epi8(v.raw, v.raw)});
#else
  return VFromD<D>{_mm_cvtepi8_epi16(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  return ShiftRight<16>(VFromD<D>{_mm_unpacklo_epi16(v.raw, v.raw)});
#else
  return VFromD<D>{_mm_cvtepi16_epi32(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  return ShiftRight<32>(VFromD<D>{_mm_unpacklo_epi32(v.raw, v.raw)});
#else
  return VFromD<D>{_mm_cvtepi32_epi64(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const __m128i x2 = _mm_unpacklo_epi8(v.raw, v.raw);
  const __m128i x4 = _mm_unpacklo_epi16(x2, x2);
  return ShiftRight<24>(VFromD<D>{x4});
#else
  return VFromD<D>{_mm_cvtepi8_epi32(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<int8_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const Repartition<int32_t, decltype(d)> di32;
  const Half<decltype(di32)> dh_i32;
  const VFromD<decltype(di32)> x4{PromoteTo(dh_i32, v).raw};
  const VFromD<decltype(di32)> s4{
      _mm_shufflelo_epi16(x4.raw, _MM_SHUFFLE(3, 3, 1, 1))};
  return ZipLower(d, x4, s4);
#else
  (void)d;
  return VFromD<D>{_mm_cvtepi8_epi64(v.raw)};
#endif
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D d, VFromD<Rebind<int16_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const Repartition<int32_t, decltype(d)> di32;
  const Half<decltype(di32)> dh_i32;
  const VFromD<decltype(di32)> x2{PromoteTo(dh_i32, v).raw};
  const VFromD<decltype(di32)> s2{
      _mm_shufflelo_epi16(x2.raw, _MM_SHUFFLE(3, 3, 1, 1))};
  return ZipLower(d, x2, s2);
#else
  (void)d;
  return VFromD<D>{_mm_cvtepi16_epi64(v.raw)};
#endif
}

// -------------------- PromoteTo float (ShiftLeft, IfNegativeThenElse)
#if HWY_TARGET < HWY_SSE4 && !defined(HWY_DISABLE_F16C)

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif

// Workaround for origin tracking bug in Clang msan prior to 11.0
// (spurious "uninitialized memory" for TestF16 with "ORIGIN: invalid")
#if HWY_IS_MSAN && (HWY_COMPILER_CLANG != 0 && HWY_COMPILER_CLANG < 1100)
#define HWY_INLINE_F16 HWY_NOINLINE
#else
#define HWY_INLINE_F16 HWY_INLINE
#endif
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_INLINE_F16 VFromD<D> PromoteTo(D /*tag*/, VFromD<Rebind<float16_t, D>> v) {
#if HWY_HAVE_FLOAT16
  const RebindToUnsigned<DFromV<decltype(v)>> du16;
  return VFromD<D>{_mm_cvtph_ps(BitCast(du16, v).raw)};
#else
  return VFromD<D>{_mm_cvtph_ps(v.raw)};
#endif
}

#endif  // HWY_NATIVE_F16C

#if HWY_HAVE_FLOAT16

#ifdef HWY_NATIVE_PROMOTE_F16_TO_F64
#undef HWY_NATIVE_PROMOTE_F16_TO_F64
#else
#define HWY_NATIVE_PROMOTE_F16_TO_F64
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_INLINE VFromD<D> PromoteTo(D /*tag*/, VFromD<Rebind<float16_t, D>> v) {
  return VFromD<D>{_mm_cvtph_pd(v.raw)};
}

#endif  // HWY_HAVE_FLOAT16

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D df32, VFromD<Rebind<bfloat16_t, D>> v) {
  const Rebind<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteTo(di32, BitCast(du16, v))));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return VFromD<D>{_mm_cvtps_pd(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{_mm_cvtepi32_pd(v.raw)};
}

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /*df64*/, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{_mm_cvtepu32_pd(v.raw)};
}
#else
// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D df64, VFromD<Rebind<uint32_t, D>> v) {
  const Rebind<int32_t, decltype(df64)> di32;
  const auto i32_to_f64_result = PromoteTo(df64, BitCast(di32, v));
  return i32_to_f64_result + IfNegativeThenElse(i32_to_f64_result,
                                                Set(df64, 4294967296.0),
                                                Zero(df64));
}
#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Per4LaneBlockShuffle
namespace detail {

#ifdef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#undef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#else
#define HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE VFromD<D> Per4LaneBlkShufDupSet4xU32(D d, const uint32_t x3,
                                                const uint32_t x2,
                                                const uint32_t x1,
                                                const uint32_t x0) {
  return ResizeBitCast(
      d, Vec128<uint32_t>{_mm_set_epi32(
             static_cast<int32_t>(x3), static_cast<int32_t>(x2),
             static_cast<int32_t>(x1), static_cast<int32_t>(x0))});
}

template <size_t kIdx3210, class V>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<2> /*lane_size_tag*/,
                                  hwy::SizeTag<8> /*vect_size_tag*/, V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d,
                 VFromD<decltype(du)>{_mm_shufflelo_epi16(
                     BitCast(du, v).raw, static_cast<int>(kIdx3210 & 0xFF))});
}

#if HWY_TARGET == HWY_SSE2
template <size_t kIdx3210, class V>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<2> /*lane_size_tag*/,
                                  hwy::SizeTag<16> /*vect_size_tag*/, V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  constexpr int kShuffle = static_cast<int>(kIdx3210 & 0xFF);
  return BitCast(
      d, VFromD<decltype(du)>{_mm_shufflehi_epi16(
             _mm_shufflelo_epi16(BitCast(du, v).raw, kShuffle), kShuffle)});
}

template <size_t kIdx3210, size_t kVectSize, class V,
          hwy::EnableIf<(kVectSize == 4 || kVectSize == 8)>* = nullptr>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> idx_3210_tag,
                                  hwy::SizeTag<1> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Rebind<uint16_t, decltype(d)> du16;
  const RebindToSigned<decltype(du16)> di16;

  const auto vu16 = PromoteTo(du16, BitCast(du, v));
  const auto shuf16_result = Per4LaneBlockShuffle(
      idx_3210_tag, hwy::SizeTag<2>(), hwy::SizeTag<kVectSize * 2>(), vu16);
  return BitCast(d, DemoteTo(du, BitCast(di16, shuf16_result)));
}

template <size_t kIdx3210, size_t kVectSize, class V>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> idx_3210_tag,
                                  hwy::SizeTag<1> /*lane_size_tag*/,
                                  hwy::SizeTag<16> /*vect_size_tag*/, V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint16_t, decltype(d)> du16;
  const RebindToSigned<decltype(du16)> di16;

  const auto zero = Zero(d);
  const auto v_lo16 = BitCast(du16, InterleaveLower(d, v, zero));
  const auto v_hi16 = BitCast(du16, InterleaveUpper(d, v, zero));

  const auto lo_shuf_result = Per4LaneBlockShuffle(
      idx_3210_tag, hwy::SizeTag<2>(), hwy::SizeTag<16>(), v_lo16);
  const auto hi_shuf_result = Per4LaneBlockShuffle(
      idx_3210_tag, hwy::SizeTag<2>(), hwy::SizeTag<16>(), v_hi16);

  return BitCast(d, OrderedDemote2To(du, BitCast(di16, lo_shuf_result),
                                     BitCast(di16, hi_shuf_result)));
}
#endif

template <size_t kIdx3210, class V, HWY_IF_NOT_FLOAT(TFromV<V>)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<4> /*lane_size_tag*/,
                                  hwy::SizeTag<16> /*vect_size_tag*/, V v) {
  return V{_mm_shuffle_epi32(v.raw, static_cast<int>(kIdx3210 & 0xFF))};
}

template <size_t kIdx3210, class V, HWY_IF_FLOAT(TFromV<V>)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<4> /*lane_size_tag*/,
                                  hwy::SizeTag<16> /*vect_size_tag*/, V v) {
  return V{_mm_shuffle_ps(v.raw, v.raw, static_cast<int>(kIdx3210 & 0xFF))};
}

}  // namespace detail

// ------------------------------ SlideUpLanes

namespace detail {

template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V SlideUpLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Full64<uint64_t> du64;
  const auto vu64 = ResizeBitCast(du64, v);
  return ResizeBitCast(
      d, ShiftLeftSame(vu64, static_cast<int>(amt * sizeof(TFromV<V>) * 8)));
}

#if HWY_TARGET <= HWY_SSSE3
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideUpLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const auto idx =
      Iota(du8, static_cast<uint8_t>(size_t{0} - amt * sizeof(TFromV<V>)));
  return BitCast(d, TableLookupBytesOr0(BitCast(du8, v), idx));
}
#else
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideUpLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<int32_t, decltype(d)> di32;
  const Repartition<uint64_t, decltype(d)> du64;
  constexpr size_t kNumOfLanesPerU64 = 8 / sizeof(TFromV<V>);

  const auto vu64 = BitCast(du64, v);
  const auto v_hi = IfVecThenElse(
      BitCast(du64, Set(di32, -static_cast<int32_t>(amt >= kNumOfLanesPerU64))),
      BitCast(du64, ShiftLeftBytes<8>(du64, vu64)), vu64);
  const auto v_lo = ShiftLeftBytes<8>(du64, v_hi);

  const int shl_amt = static_cast<int>((amt * sizeof(TFromV<V>) * 8) & 63);
  return BitCast(
      d, Or(ShiftLeftSame(v_hi, shl_amt), ShiftRightSame(v_lo, 64 - shl_amt)));
}
#endif

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
  const Repartition<UnsignedFromSize<d.MaxBytes()>, decltype(d)> dv;
  return BitCast(d,
                 ShiftRightSame(BitCast(dv, v),
                                static_cast<int>(amt * sizeof(TFromV<V>) * 8)));
}

#if HWY_TARGET <= HWY_SSSE3
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideDownLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<int8_t, decltype(d)> di8;
  auto idx = Iota(di8, static_cast<int8_t>(amt * sizeof(TFromV<V>)));
  idx = Or(idx, VecFromMask(di8, idx > Set(di8, int8_t{15})));
  return BitCast(d, TableLookupBytesOr0(BitCast(di8, v), idx));
}
#else
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V SlideDownLanes(V v, size_t amt) {
  const DFromV<decltype(v)> d;
  const Repartition<int32_t, decltype(d)> di32;
  const Repartition<uint64_t, decltype(d)> du64;
  constexpr size_t kNumOfLanesPerU64 = 8 / sizeof(TFromV<V>);

  const auto vu64 = BitCast(du64, v);
  const auto v_lo = IfVecThenElse(
      BitCast(du64, Set(di32, -static_cast<int32_t>(amt >= kNumOfLanesPerU64))),
      BitCast(du64, ShiftRightBytes<8>(du64, vu64)), vu64);
  const auto v_hi = ShiftRightBytes<8>(du64, v_lo);

  const int shr_amt = static_cast<int>((amt * sizeof(TFromV<V>) * 8) & 63);
  return BitCast(
      d, Or(ShiftRightSame(v_lo, shr_amt), ShiftLeftSame(v_hi, 64 - shr_amt)));
}
#endif

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

// ================================================== MEMORY (4)

// ------------------------------ StoreN (ExtractLane)

#if HWY_TARGET <= HWY_AVX2

#ifdef HWY_NATIVE_STORE_N
#undef HWY_NATIVE_STORE_N
#else
#define HWY_NATIVE_STORE_N
#endif

template <class D, HWY_IF_T_SIZE_ONE_OF_D(
                       D, (HWY_TARGET <= HWY_AVX3 ? ((1 << 1) | (1 << 2)) : 0) |
                              (1 << 4) | (1 << 8))>
HWY_API void StoreN(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  const size_t num_lanes_to_store =
      HWY_MIN(max_lanes_to_store, HWY_MAX_LANES_D(D));

#if HWY_COMPILER_MSVC
  // Work around MSVC compiler bug by using a HWY_FENCE before the BlendedStore
  HWY_FENCE;
#endif

  BlendedStore(v, FirstN(d, num_lanes_to_store), d, p);

#if HWY_COMPILER_MSVC
  // Work around MSVC compiler bug by using a HWY_FENCE after the BlendedStore
  HWY_FENCE;
#endif

  detail::MaybeUnpoison(p, num_lanes_to_store);
}

#if HWY_TARGET > HWY_AVX3
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_LANES_D(D, 1)>
HWY_API void StoreN(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  if (max_lanes_to_store > 0) {
    StoreU(v, d, p);
  }
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_LANES_D(D, 2)>
HWY_API void StoreN(VFromD<D> v, D /*d*/, TFromD<D>* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  if (max_lanes_to_store >= 1) {
    p[static_cast<size_t>(max_lanes_to_store > 1)] = detail::ExtractLane<1>(v);
    p[0] = GetLane(v);
  }
}

namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API void AVX2UIF8Or16StoreTrailingN(VFromD<D> v_trailing, D /*d*/,
                                        TFromD<D>* HWY_RESTRICT p,
                                        size_t num_lanes_to_store) {
  // AVX2UIF8Or16StoreTrailingN should only be called for an I8/U8 vector if
  // (num_lanes_to_store & 3) != 0 is true
  const auto v_full128 = ResizeBitCast(Full128<TFromD<D>>(), v_trailing);
  if ((num_lanes_to_store & 2) != 0) {
    const uint16_t u16_bits = GetLane(BitCast(Full128<uint16_t>(), v_full128));
    p[num_lanes_to_store - 1] = detail::ExtractLane<2>(v_full128);
    CopyBytes<sizeof(uint16_t)>(&u16_bits,
                                p + (num_lanes_to_store & ~size_t{3}));
  } else {
    p[num_lanes_to_store - 1] = GetLane(v_full128);
  }
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API void AVX2UIF8Or16StoreTrailingN(VFromD<D> v_trailing, D /*d*/,
                                        TFromD<D>* p,
                                        size_t num_lanes_to_store) {
  // AVX2UIF8Or16StoreTrailingN should only be called for an I16/U16/F16/BF16
  // vector if (num_lanes_to_store & 1) == 1 is true
  p[num_lanes_to_store - 1] = GetLane(v_trailing);
}

}  // namespace detail

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_LANES_GT_D(D, 2)>
HWY_API void StoreN(VFromD<D> v, D d, TFromD<D>* p, size_t max_lanes_to_store) {
  const size_t num_lanes_to_store =
      HWY_MIN(max_lanes_to_store, HWY_MAX_LANES_D(D));

  const FixedTag<TFromD<D>, HWY_MAX(HWY_MAX_LANES_D(D), 16 / sizeof(TFromD<D>))>
      d_full;
  const RebindToUnsigned<decltype(d_full)> du_full;
  const Repartition<int32_t, decltype(d_full)> di32_full;

  const auto i32_store_mask = BitCast(
      di32_full, VecFromMask(du_full, FirstN(du_full, num_lanes_to_store)));
  const auto vi32 = ResizeBitCast(di32_full, v);

#if HWY_COMPILER_MSVC
  // Work around MSVC compiler bug by using a HWY_FENCE before the BlendedStore
  HWY_FENCE;
#endif

  BlendedStore(vi32, MaskFromVec(i32_store_mask), di32_full,
               reinterpret_cast<int32_t*>(p));

  constexpr size_t kNumOfLanesPerI32 = 4 / sizeof(TFromD<D>);
  constexpr size_t kTrailingLenMask = kNumOfLanesPerI32 - 1;
  const size_t trailing_n = (num_lanes_to_store & kTrailingLenMask);

  if (trailing_n != 0) {
    const VFromD<D> v_trailing = ResizeBitCast(
        d, SlideDownLanes(di32_full, vi32,
                          num_lanes_to_store / kNumOfLanesPerI32));
    detail::AVX2UIF8Or16StoreTrailingN(v_trailing, d, p, num_lanes_to_store);
  }

#if HWY_COMPILER_MSVC
  // Work around MSVC compiler bug by using a HWY_FENCE after the BlendedStore
  HWY_FENCE;
#endif

  detail::MaybeUnpoison(p, num_lanes_to_store);
}
#endif  // HWY_TARGET > HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX2

// ================================================== COMBINE

// ------------------------------ Combine (InterleaveLower)

// N = N/2 + N/2 (upper half undefined)
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), class VH = VFromD<Half<D>>>
HWY_API VFromD<D> Combine(D d, VH hi_half, VH lo_half) {
  const Half<decltype(d)> dh;
  const RebindToUnsigned<decltype(dh)> duh;
  // Treat half-width input as one lane, and expand to two lanes.
  using VU = Vec128<UnsignedFromSize<dh.MaxBytes()>, 2>;
  const VU lo{BitCast(duh, lo_half).raw};
  const VU hi{BitCast(duh, hi_half).raw};
  return BitCast(d, InterleaveLower(lo, hi));
}

// ------------------------------ ZeroExtendVector (Combine, IfThenElseZero)

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const RebindToUnsigned<decltype(d)> du;
  const Half<decltype(du)> duh;
  return BitCast(d, VFromD<decltype(du)>{_mm_move_epi64(BitCast(duh, lo).raw)});
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const Half<D> dh;
  return IfThenElseZero(FirstN(d, MaxLanes(dh)), VFromD<D>{lo.raw});
}

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const RebindToUnsigned<decltype(d)> du;
  const Half<decltype(du)> duh;
  return BitCast(d, ZeroExtendVector(du, BitCast(duh, lo)));
}
#endif

// Generic for all vector lengths.
template <class D, HWY_X86_IF_EMULATED_D(D)>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const RebindToUnsigned<decltype(d)> du;
  const Half<decltype(du)> duh;
  return BitCast(d, ZeroExtendVector(du, BitCast(duh, lo)));
}

// ------------------------------ Concat full (InterleaveLower)

// hiH,hiL loH,loL |-> hiL,loL (= lower halves)
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConcatLowerLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<uint64_t, decltype(d)> d64;
  return BitCast(d, InterleaveLower(BitCast(d64, lo), BitCast(d64, hi)));
}

// hiH,hiL loH,loL |-> hiH,loH (= upper halves)
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConcatUpperUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<uint64_t, decltype(d)> d64;
  return BitCast(d, InterleaveUpper(d64, BitCast(d64, lo), BitCast(d64, hi)));
}

// hiH,hiL loH,loL |-> hiL,loH (= inner halves)
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConcatLowerUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  return CombineShiftRightBytes<8>(d, hi, lo);
}

// hiH,hiL loH,loL |-> hiH,loL (= outer halves)
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API VFromD<D> ConcatUpperLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<double, decltype(d)> dd;
#if HWY_TARGET >= HWY_SSSE3
  return BitCast(
      d, Vec128<double>{_mm_shuffle_pd(BitCast(dd, lo).raw, BitCast(dd, hi).raw,
                                       _MM_SHUFFLE2(1, 0))});
#else
  // _mm_blend_epi16 has throughput 1/cycle on SKX, whereas _pd can do 3/cycle.
  return BitCast(d, Vec128<double>{_mm_blend_pd(BitCast(dd, hi).raw,
                                                BitCast(dd, lo).raw, 1)});
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<float> ConcatUpperLower(D d, Vec128<float> hi,
                                       Vec128<float> lo) {
#if HWY_TARGET >= HWY_SSSE3
  (void)d;
  return Vec128<float>{_mm_shuffle_ps(lo.raw, hi.raw, _MM_SHUFFLE(3, 2, 1, 0))};
#else
  // _mm_shuffle_ps has throughput 1/cycle on SKX, whereas blend can do 3/cycle.
  const RepartitionToWide<decltype(d)> dd;
  return BitCast(d, Vec128<double>{_mm_blend_pd(BitCast(dd, hi).raw,
                                                BitCast(dd, lo).raw, 1)});
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<double> ConcatUpperLower(D /* tag */, Vec128<double> hi,
                                        Vec128<double> lo) {
#if HWY_TARGET >= HWY_SSSE3
  return Vec128<double>{_mm_shuffle_pd(lo.raw, hi.raw, _MM_SHUFFLE2(1, 0))};
#else
  // _mm_shuffle_pd has throughput 1/cycle on SKX, whereas blend can do 3/cycle.
  return Vec128<double>{_mm_blend_pd(hi.raw, lo.raw, 1)};
#endif
}

// ------------------------------ Concat partial (Combine, LowerHalf)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConcatLowerLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, LowerHalf(d2, hi), LowerHalf(d2, lo));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConcatUpperUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, UpperHalf(d2, hi), UpperHalf(d2, lo));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConcatLowerUpper(D d, const VFromD<D> hi,
                                   const VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, LowerHalf(d2, hi), UpperHalf(d2, lo));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConcatUpperLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, UpperHalf(d2, hi), LowerHalf(d2, lo));
}

// ------------------------------ ConcatOdd

// 8-bit full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<uint16_t, decltype(d)> dw;
  // Right-shift 8 bits per u16 so we can pack.
  const Vec128<uint16_t> uH = ShiftRight<8>(BitCast(dw, hi));
  const Vec128<uint16_t> uL = ShiftRight<8>(BitCast(dw, lo));
  return VFromD<D>{_mm_packus_epi16(uL.raw, uH.raw)};
}

// 8-bit x8
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  const Repartition<uint16_t, decltype(d)> dw;
  // Right-shift 8 bits per u16 so we can pack.
  const Vec64<uint16_t> uH = ShiftRight<8>(BitCast(dw, hi));
  const Vec64<uint16_t> uL = ShiftRight<8>(BitCast(dw, lo));
  return VFromD<D>{_mm_shuffle_epi32(_mm_packus_epi16(uL.raw, uH.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
#else
  const Repartition<uint32_t, decltype(d)> du32;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactOddU8[8] = {1, 3, 5, 7};
  const VFromD<D> shuf = BitCast(d, Load(Full64<uint8_t>(), kCompactOddU8));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du32, BitCast(du32, L), BitCast(du32, H)));
#endif
}

// 8-bit x4
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  const Repartition<uint16_t, decltype(d)> dw;
  const Twice<decltype(dw)> dw_2;
  // Right-shift 8 bits per u16 so we can pack.
  const Vec32<uint16_t> uH = ShiftRight<8>(BitCast(dw, hi));
  const Vec32<uint16_t> uL = ShiftRight<8>(BitCast(dw, lo));
  const Vec64<uint16_t> uHL = Combine(dw_2, uH, uL);
  return VFromD<D>{_mm_packus_epi16(uHL.raw, uHL.raw)};
#else
  const Repartition<uint16_t, decltype(d)> du16;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactOddU8[4] = {1, 3};
  const VFromD<D> shuf = BitCast(d, Load(Full32<uint8_t>(), kCompactOddU8));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du16, BitCast(du16, L), BitCast(du16, H)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  // Right-shift 16 bits per i32 - a *signed* shift of 0x8000xxxx returns
  // 0xFFFF8000, which correctly saturates to 0x8000.
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<int32_t, decltype(d)> dw;
  const Vec128<int32_t> uH = ShiftRight<16>(BitCast(dw, hi));
  const Vec128<int32_t> uL = ShiftRight<16>(BitCast(dw, lo));
  return BitCast(d, VFromD<decltype(du)>{_mm_packs_epi32(uL.raw, uH.raw)});
}

// 16-bit x4
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  // Right-shift 16 bits per i32 - a *signed* shift of 0x8000xxxx returns
  // 0xFFFF8000, which correctly saturates to 0x8000.
  const Repartition<int32_t, decltype(d)> dw;
  const Vec64<int32_t> uH = ShiftRight<16>(BitCast(dw, hi));
  const Vec64<int32_t> uL = ShiftRight<16>(BitCast(dw, lo));
  return VFromD<D>{_mm_shuffle_epi32(_mm_packs_epi32(uL.raw, uH.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
#else
  const Repartition<uint32_t, decltype(d)> du32;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactOddU16[8] = {2, 3, 6, 7};
  const VFromD<D> shuf = BitCast(d, Load(Full64<uint8_t>(), kCompactOddU16));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du32, BitCast(du32, L), BitCast(du32, H)));
#endif
}

// 32-bit full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  const RebindToFloat<decltype(d)> df;
  return BitCast(
      d, Vec128<float>{_mm_shuffle_ps(BitCast(df, lo).raw, BitCast(df, hi).raw,
                                      _MM_SHUFFLE(3, 1, 3, 1))});
}

// Any type x2
template <class D, HWY_IF_LANES_D(D, 2)>
HWY_API VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  return InterleaveUpper(d, lo, hi);
}

// ------------------------------ ConcatEven (InterleaveLower)

// 8-bit full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const Repartition<uint16_t, decltype(d)> dw;
  // Isolate lower 8 bits per u16 so we can pack.
  const Vec128<uint16_t> mask = Set(dw, 0x00FF);
  const Vec128<uint16_t> uH = And(BitCast(dw, hi), mask);
  const Vec128<uint16_t> uL = And(BitCast(dw, lo), mask);
  return VFromD<D>{_mm_packus_epi16(uL.raw, uH.raw)};
}

// 8-bit x8
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  const Repartition<uint16_t, decltype(d)> dw;
  // Isolate lower 8 bits per u16 so we can pack.
  const Vec64<uint16_t> mask = Set(dw, 0x00FF);
  const Vec64<uint16_t> uH = And(BitCast(dw, hi), mask);
  const Vec64<uint16_t> uL = And(BitCast(dw, lo), mask);
  return VFromD<D>{_mm_shuffle_epi32(_mm_packus_epi16(uL.raw, uH.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
#else
  const Repartition<uint32_t, decltype(d)> du32;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactEvenU8[8] = {0, 2, 4, 6};
  const VFromD<D> shuf = BitCast(d, Load(Full64<uint8_t>(), kCompactEvenU8));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du32, BitCast(du32, L), BitCast(du32, H)));
#endif
}

// 8-bit x4
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  const Repartition<uint16_t, decltype(d)> dw;
  const Twice<decltype(dw)> dw_2;
  // Isolate lower 8 bits per u16 so we can pack.
  const Vec32<uint16_t> mask = Set(dw, 0x00FF);
  const Vec32<uint16_t> uH = And(BitCast(dw, hi), mask);
  const Vec32<uint16_t> uL = And(BitCast(dw, lo), mask);
  const Vec64<uint16_t> uHL = Combine(dw_2, uH, uL);
  return VFromD<D>{_mm_packus_epi16(uHL.raw, uHL.raw)};
#else
  const Repartition<uint16_t, decltype(d)> du16;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactEvenU8[4] = {0, 2};
  const VFromD<D> shuf = BitCast(d, Load(Full32<uint8_t>(), kCompactEvenU8));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du16, BitCast(du16, L), BitCast(du16, H)));
#endif
}

// 16-bit full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET <= HWY_SSE4
  // Isolate lower 16 bits per u32 so we can pack.
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  const Repartition<uint32_t, decltype(d)> dw;
  const Vec128<uint32_t> mask = Set(dw, 0x0000FFFF);
  const Vec128<uint32_t> uH = And(BitCast(dw, hi), mask);
  const Vec128<uint32_t> uL = And(BitCast(dw, lo), mask);
  return BitCast(d, VFromD<decltype(du)>{_mm_packus_epi32(uL.raw, uH.raw)});
#elif HWY_TARGET == HWY_SSE2
  const Repartition<uint32_t, decltype(d)> dw;
  return ConcatOdd(d, BitCast(d, ShiftLeft<16>(BitCast(dw, hi))),
                   BitCast(d, ShiftLeft<16>(BitCast(dw, lo))));
#else
  const RebindToUnsigned<decltype(d)> du;
  // packs_epi32 saturates 0x8000 to 0x7FFF. Instead ConcatEven within the two
  // inputs, then concatenate them.
  alignas(16)
      const uint16_t kCompactEvenU16[8] = {0x0100, 0x0504, 0x0908, 0x0D0C};
  const VFromD<D> shuf = BitCast(d, Load(du, kCompactEvenU16));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return ConcatLowerLower(d, H, L);
#endif
}

// 16-bit x4
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
#if HWY_TARGET == HWY_SSE2
  const Repartition<uint32_t, decltype(d)> dw;
  return ConcatOdd(d, BitCast(d, ShiftLeft<16>(BitCast(dw, hi))),
                   BitCast(d, ShiftLeft<16>(BitCast(dw, lo))));
#else
  const Repartition<uint32_t, decltype(d)> du32;
  // Don't care about upper half, no need to zero.
  alignas(16) const uint8_t kCompactEvenU16[8] = {0, 1, 4, 5};
  const VFromD<D> shuf = BitCast(d, Load(Full64<uint8_t>(), kCompactEvenU16));
  const VFromD<D> L = TableLookupBytes(lo, shuf);
  const VFromD<D> H = TableLookupBytes(hi, shuf);
  return BitCast(d, InterleaveLower(du32, BitCast(du32, L), BitCast(du32, H)));
#endif
}

// 32-bit full
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const RebindToFloat<decltype(d)> df;
  return BitCast(
      d, Vec128<float>{_mm_shuffle_ps(BitCast(df, lo).raw, BitCast(df, hi).raw,
                                      _MM_SHUFFLE(2, 0, 2, 0))});
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConcatEven(D /* d */, VFromD<D> hi, VFromD<D> lo) {
  return VFromD<D>{_mm_shuffle_ps(lo.raw, hi.raw, _MM_SHUFFLE(2, 0, 2, 0))};
}

// Any T x2
template <class D, HWY_IF_LANES_D(D, 2)>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  return InterleaveLower(d, lo, hi);
}

// ------------------------------ DupEven (InterleaveLower)

template <typename T>
HWY_API Vec128<T, 1> DupEven(const Vec128<T, 1> v) {
  return v;
}

template <typename T>
HWY_API Vec128<T, 2> DupEven(const Vec128<T, 2> v) {
  return InterleaveLower(DFromV<decltype(v)>(), v, v);
}

template <typename V, HWY_IF_T_SIZE_V(V, 1), HWY_IF_V_SIZE_GT_V(V, 2)>
HWY_API V DupEven(V v) {
  const DFromV<decltype(v)> d;

#if HWY_TARGET <= HWY_SSSE3
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> shuffle = Dup128VecFromValues(
      du, 0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14);
  return TableLookupBytes(v, BitCast(d, shuffle));
#else
  const Repartition<uint16_t, decltype(d)> du16;
  return IfVecThenElse(BitCast(d, Set(du16, uint16_t{0xFF00})),
                       BitCast(d, ShiftLeft<8>(BitCast(du16, v))), v);
#endif
}

template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> DupEven(const Vec64<T> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d, VFromD<decltype(du)>{_mm_shufflelo_epi16(
                        BitCast(du, v).raw, _MM_SHUFFLE(2, 2, 0, 0))});
}

// Generic for all vector lengths.
template <class V, HWY_IF_T_SIZE_V(V, 2)>
HWY_API V DupEven(const V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
#if HWY_TARGET <= HWY_SSSE3
  const VFromD<decltype(du)> shuffle = Dup128VecFromValues(
      du, 0x0100, 0x0100, 0x0504, 0x0504, 0x0908, 0x0908, 0x0d0c, 0x0d0c);
  return TableLookupBytes(v, BitCast(d, shuffle));
#else
  return BitCast(
      d, VFromD<decltype(du)>{_mm_shufflehi_epi16(
             _mm_shufflelo_epi16(BitCast(du, v).raw, _MM_SHUFFLE(2, 2, 0, 0)),
             _MM_SHUFFLE(2, 2, 0, 0))});
#endif
}

template <typename T, HWY_IF_UI32(T)>
HWY_API Vec128<T> DupEven(Vec128<T> v) {
  return Vec128<T>{_mm_shuffle_epi32(v.raw, _MM_SHUFFLE(2, 2, 0, 0))};
}

HWY_API Vec128<float> DupEven(Vec128<float> v) {
  return Vec128<float>{_mm_shuffle_ps(v.raw, v.raw, _MM_SHUFFLE(2, 2, 0, 0))};
}

// ------------------------------ DupOdd (InterleaveUpper)

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, 1> DupOdd(Vec128<T, 1> v) {
  return v;
}

template <typename V, HWY_IF_T_SIZE_V(V, 1), HWY_IF_V_SIZE_GT_V(V, 1)>
HWY_API V DupOdd(V v) {
  const DFromV<decltype(v)> d;

#if HWY_TARGET <= HWY_SSSE3
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> shuffle = Dup128VecFromValues(
      du, 1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15);
  return TableLookupBytes(v, BitCast(d, shuffle));
#else
  const Repartition<uint16_t, decltype(d)> du16;
  return IfVecThenElse(BitCast(d, Set(du16, uint16_t{0x00FF})),
                       BitCast(d, ShiftRight<8>(BitCast(du16, v))), v);
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2), HWY_IF_LANES_LE(N, 4)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d, VFromD<decltype(du)>{_mm_shufflelo_epi16(
                        BitCast(du, v).raw, _MM_SHUFFLE(3, 3, 1, 1))});
}

// Generic for all vector lengths.
template <typename V, HWY_IF_T_SIZE_V(V, 2), HWY_IF_V_SIZE_GT_V(V, 8)>
HWY_API V DupOdd(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
#if HWY_TARGET <= HWY_SSSE3
  const VFromD<decltype(du)> shuffle = Dup128VecFromValues(
      du, 0x0302, 0x0302, 0x0706, 0x0706, 0x0b0a, 0x0b0a, 0x0f0e, 0x0f0e);
  return TableLookupBytes(v, BitCast(d, shuffle));
#else
  return BitCast(
      d, VFromD<decltype(du)>{_mm_shufflehi_epi16(
             _mm_shufflelo_epi16(BitCast(du, v).raw, _MM_SHUFFLE(3, 3, 1, 1)),
             _MM_SHUFFLE(3, 3, 1, 1))});
#endif
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return Vec128<T, N>{_mm_shuffle_epi32(v.raw, _MM_SHUFFLE(3, 3, 1, 1))};
}
template <size_t N>
HWY_API Vec128<float, N> DupOdd(Vec128<float, N> v) {
  return Vec128<float, N>{
      _mm_shuffle_ps(v.raw, v.raw, _MM_SHUFFLE(3, 3, 1, 1))};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupOdd(const Vec128<T, N> v) {
  return InterleaveUpper(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ TwoTablesLookupLanes (DupEven)

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
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

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
#if HWY_TARGET <= HWY_AVX3_DL
  return Vec128<T>{_mm_permutex2var_epi8(a.raw, idx.raw, b.raw)};
#else  // AVX3 or below
  const DFromV<decltype(a)> d;
  const Vec128<T> idx_vec{idx.raw};

#if HWY_TARGET <= HWY_SSE4
  const Repartition<uint16_t, decltype(d)> du16;
  const auto sel_hi_mask =
      MaskFromVec(BitCast(d, ShiftLeft<3>(BitCast(du16, idx_vec))));
#else
  const RebindToSigned<decltype(d)> di;
  const auto sel_hi_mask =
      RebindMask(d, BitCast(di, idx_vec) > Set(di, int8_t{15}));
#endif

  const auto lo_lookup_result = TableLookupBytes(a, idx_vec);
#if HWY_TARGET <= HWY_AVX3
  const Vec128<T> lookup_result{_mm_mask_shuffle_epi8(
      lo_lookup_result.raw, sel_hi_mask.raw, b.raw, idx_vec.raw)};
  return lookup_result;
#else
  const auto hi_lookup_result = TableLookupBytes(b, idx_vec);
  return IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result);
#endif  // HWY_TARGET <= HWY_AVX3
#endif  // HWY_TARGET <= HWY_AVX3_DL
}

template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<T>{_mm_permutex2var_epi16(a.raw, idx.raw, b.raw)};
#elif HWY_TARGET == HWY_SSE2
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const Vec128<T> idx_vec{idx.raw};
  const auto sel_hi_mask =
      RebindMask(d, BitCast(di, idx_vec) > Set(di, int16_t{7}));
  const auto lo_lookup_result = TableLookupLanes(a, idx);
  const auto hi_lookup_result = TableLookupLanes(b, idx);
  return IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result);
#else
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  return BitCast(d, TwoTablesLookupLanes(BitCast(du8, a), BitCast(du8, b),
                                         Indices128<uint8_t>{idx.raw}));
#endif
}

template <typename T, HWY_IF_UI32(T)>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<T>{_mm_permutex2var_epi32(a.raw, idx.raw, b.raw)};
#else  // AVX2 or below
  const DFromV<decltype(a)> d;

#if HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_SSE2
  const Vec128<T> idx_vec{idx.raw};

#if HWY_TARGET <= HWY_AVX2
  const RebindToFloat<decltype(d)> d_sel;
  const auto sel_hi_mask = MaskFromVec(BitCast(d_sel, ShiftLeft<29>(idx_vec)));
#else
  const RebindToSigned<decltype(d)> d_sel;
  const auto sel_hi_mask = BitCast(d_sel, idx_vec) > Set(d_sel, int32_t{3});
#endif

  const auto lo_lookup_result = BitCast(d_sel, TableLookupLanes(a, idx));
  const auto hi_lookup_result = BitCast(d_sel, TableLookupLanes(b, idx));
  return BitCast(d,
                 IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result));
#else   // SSSE3 or SSE4
  const Repartition<uint8_t, decltype(d)> du8;
  return BitCast(d, TwoTablesLookupLanes(BitCast(du8, a), BitCast(du8, b),
                                         Indices128<uint8_t>{idx.raw}));
#endif  // HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_SSE2
#endif  // HWY_TARGET <= HWY_AVX3
}

#if HWY_HAVE_FLOAT16
HWY_API Vec128<float16_t> TwoTablesLookupLanes(Vec128<float16_t> a,
                                               Vec128<float16_t> b,
                                               Indices128<float16_t> idx) {
  return Vec128<float16_t>{_mm_permutex2var_ph(a.raw, idx.raw, b.raw)};
}
#endif  // HWY_HAVE_FLOAT16
HWY_API Vec128<float> TwoTablesLookupLanes(Vec128<float> a, Vec128<float> b,
                                           Indices128<float> idx) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<float>{_mm_permutex2var_ps(a.raw, idx.raw, b.raw)};
#elif HWY_TARGET <= HWY_AVX2 || HWY_TARGET == HWY_SSE2
  const DFromV<decltype(a)> d;

#if HWY_TARGET <= HWY_AVX2
  const auto sel_hi_mask =
      MaskFromVec(BitCast(d, ShiftLeft<29>(Vec128<int32_t>{idx.raw})));
#else
  const RebindToSigned<decltype(d)> di;
  const auto sel_hi_mask =
      RebindMask(d, Vec128<int32_t>{idx.raw} > Set(di, int32_t{3}));
#endif

  const auto lo_lookup_result = TableLookupLanes(a, idx);
  const auto hi_lookup_result = TableLookupLanes(b, idx);
  return IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result);
#else  // SSSE3 or SSE4
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  return BitCast(d, TwoTablesLookupLanes(BitCast(du8, a), BitCast(du8, b),
                                         Indices128<uint8_t>{idx.raw}));
#endif
}

template <typename T, HWY_IF_UI64(T)>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<T>{_mm_permutex2var_epi64(a.raw, idx.raw, b.raw)};
#else
  const DFromV<decltype(a)> d;
  const Vec128<T> idx_vec{idx.raw};
  const Indices128<T> idx_mod{And(idx_vec, Set(d, T{1})).raw};

#if HWY_TARGET <= HWY_SSE4
  const RebindToFloat<decltype(d)> d_sel;
  const auto sel_hi_mask = MaskFromVec(BitCast(d_sel, ShiftLeft<62>(idx_vec)));
#else   // SSE2 or SSSE3
  const Repartition<int32_t, decltype(d)> di32;
  const RebindToSigned<decltype(d)> d_sel;
  const auto sel_hi_mask = MaskFromVec(
      BitCast(d_sel, VecFromMask(di32, DupEven(BitCast(di32, idx_vec)) >
                                           Set(di32, int32_t{1}))));
#endif  // HWY_TARGET <= HWY_SSE4

  const auto lo_lookup_result = BitCast(d_sel, TableLookupLanes(a, idx_mod));
  const auto hi_lookup_result = BitCast(d_sel, TableLookupLanes(b, idx_mod));
  return BitCast(d,
                 IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result));
#endif  // HWY_TARGET <= HWY_AVX3
}

HWY_API Vec128<double> TwoTablesLookupLanes(Vec128<double> a, Vec128<double> b,
                                            Indices128<double> idx) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<double>{_mm_permutex2var_pd(a.raw, idx.raw, b.raw)};
#else
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const Vec128<int64_t> idx_vec{idx.raw};
  const Indices128<double> idx_mod{And(idx_vec, Set(di, int64_t{1})).raw};

#if HWY_TARGET <= HWY_SSE4
  const auto sel_hi_mask = MaskFromVec(BitCast(d, ShiftLeft<62>(idx_vec)));
#else   // SSE2 or SSSE3
  const Repartition<int32_t, decltype(d)> di32;
  const auto sel_hi_mask =
      MaskFromVec(BitCast(d, VecFromMask(di32, DupEven(BitCast(di32, idx_vec)) >
                                                   Set(di32, int32_t{1}))));
#endif  // HWY_TARGET <= HWY_SSE4

  const auto lo_lookup_result = TableLookupLanes(a, idx_mod);
  const auto hi_lookup_result = TableLookupLanes(b, idx_mod);
  return IfThenElse(sel_hi_mask, hi_lookup_result, lo_lookup_result);
#endif  // HWY_TARGET <= HWY_AVX3
}

// ------------------------------ OddEven (IfThenElse)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t mask[16] = {
      0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0};
  return IfThenElse(MaskFromVec(BitCast(d, Load(d8, mask))), b, a);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
#if HWY_TARGET >= HWY_SSSE3
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t mask[16] = {
      0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0};
  return IfThenElse(MaskFromVec(BitCast(d, Load(d8, mask))), b, a);
#else
  const RebindToUnsigned<decltype(d)> du;  // for float16_t
  return BitCast(d, VFromD<decltype(du)>{_mm_blend_epi16(
                        BitCast(du, a).raw, BitCast(du, b).raw, 0x55)});
#endif
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_INLINE Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  const __m128i odd = _mm_shuffle_epi32(a.raw, _MM_SHUFFLE(3, 1, 3, 1));
  const __m128i even = _mm_shuffle_epi32(b.raw, _MM_SHUFFLE(2, 0, 2, 0));
  return Vec128<T, N>{_mm_unpacklo_epi32(even, odd)};
#else
  // _mm_blend_epi16 has throughput 1/cycle on SKX, whereas _ps can do 3/cycle.
  const DFromV<decltype(a)> d;
  const RebindToFloat<decltype(d)> df;
  return BitCast(d, Vec128<float, N>{_mm_blend_ps(BitCast(df, a).raw,
                                                  BitCast(df, b).raw, 5)});
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
  // Same as ConcatUpperLower for full vectors; do not call that because this
  // is more efficient for 64x1 vectors.
  const DFromV<decltype(a)> d;
  const RebindToFloat<decltype(d)> dd;
#if HWY_TARGET >= HWY_SSSE3
  return BitCast(
      d, Vec128<double, N>{_mm_shuffle_pd(
             BitCast(dd, b).raw, BitCast(dd, a).raw, _MM_SHUFFLE2(1, 0))});
#else
  // _mm_shuffle_pd has throughput 1/cycle on SKX, whereas blend can do 3/cycle.
  return BitCast(d, Vec128<double, N>{_mm_blend_pd(BitCast(dd, a).raw,
                                                   BitCast(dd, b).raw, 1)});
#endif
}

template <size_t N>
HWY_API Vec128<float, N> OddEven(Vec128<float, N> a, Vec128<float, N> b) {
#if HWY_TARGET >= HWY_SSSE3
  // SHUFPS must fill the lower half of the output from one input, so we
  // need another shuffle. Unpack avoids another immediate byte.
  const __m128 odd = _mm_shuffle_ps(a.raw, a.raw, _MM_SHUFFLE(3, 1, 3, 1));
  const __m128 even = _mm_shuffle_ps(b.raw, b.raw, _MM_SHUFFLE(2, 0, 2, 0));
  return Vec128<float, N>{_mm_unpacklo_ps(even, odd)};
#else
  return Vec128<float, N>{_mm_blend_ps(a.raw, b.raw, 5)};
#endif
}

// -------------------------- InterleaveEven

template <class D, HWY_IF_LANES_LE_D(D, 2)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  return ConcatEven(d, b, a);
}

// I8/U8 InterleaveEven is generic for all vector lengths that are >= 4 bytes
template <class D, HWY_IF_LANES_GT_D(D, 2), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  const Repartition<uint16_t, decltype(d)> du16;
  return OddEven(BitCast(d, ShiftLeft<8>(BitCast(du16, b))), a);
}

// I16/U16 InterleaveEven is generic for all vector lengths that are >= 8 bytes
template <class D, HWY_IF_LANES_GT_D(D, 2), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  const Repartition<uint32_t, decltype(d)> du32;
  return OddEven(BitCast(d, ShiftLeft<16>(BitCast(du32, b))), a);
}

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_mask_shuffle_epi32(
      a.raw, static_cast<__mmask8>(0x0A), b.raw,
      static_cast<_MM_PERM_ENUM>(_MM_SHUFFLE(2, 2, 0, 0)))};
}
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_F32_D(D)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_mask_shuffle_ps(a.raw, static_cast<__mmask8>(0x0A),
                                       b.raw, b.raw, _MM_SHUFFLE(2, 2, 0, 0))};
}
#else
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  const RebindToFloat<decltype(d)> df;
  const auto b2_b0_a2_a0 = ConcatEven(df, BitCast(df, b), BitCast(df, a));
  return BitCast(
      d, VFromD<decltype(df)>{_mm_shuffle_ps(b2_b0_a2_a0.raw, b2_b0_a2_a0.raw,
                                             _MM_SHUFFLE(3, 1, 2, 0))});
}
#endif

// -------------------------- InterleaveOdd

template <class D, HWY_IF_LANES_LE_D(D, 2)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  return ConcatOdd(d, b, a);
}

// I8/U8 InterleaveOdd is generic for all vector lengths that are >= 4 bytes
template <class D, HWY_IF_LANES_GT_D(D, 2), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  const Repartition<uint16_t, decltype(d)> du16;
  return OddEven(b, BitCast(d, ShiftRight<8>(BitCast(du16, a))));
}

// I16/U16 InterleaveOdd is generic for all vector lengths that are >= 8 bytes
template <class D, HWY_IF_LANES_GT_D(D, 2), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  const Repartition<uint32_t, decltype(d)> du32;
  return OddEven(b, BitCast(d, ShiftRight<16>(BitCast(du32, a))));
}

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_mask_shuffle_epi32(
      b.raw, static_cast<__mmask8>(0x05), a.raw,
      static_cast<_MM_PERM_ENUM>(_MM_SHUFFLE(3, 3, 1, 1)))};
}
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_F32_D(D)>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{_mm_mask_shuffle_ps(b.raw, static_cast<__mmask8>(0x05),
                                       a.raw, a.raw, _MM_SHUFFLE(3, 3, 1, 1))};
}
#else
template <class D, HWY_IF_LANES_D(D, 4), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  const RebindToFloat<decltype(d)> df;
  const auto b3_b1_a3_a1 = ConcatOdd(df, BitCast(df, b), BitCast(df, a));
  return BitCast(
      d, VFromD<decltype(df)>{_mm_shuffle_ps(b3_b1_a3_a1.raw, b3_b1_a3_a1.raw,
                                             _MM_SHUFFLE(3, 1, 2, 0))});
}
#endif

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

// ------------------------------ Shl (ZipLower, Mul)

// Use AVX2/3 variable shifts where available, otherwise multiply by powers of
// two from loading float exponents, which is considerably faster (according
// to LLVM-MCA) than scalar or testing bits: https://gcc.godbolt.org/z/9G7Y9v.

namespace detail {

#if HWY_TARGET == HWY_AVX2  // Unused for AVX3 - we use sllv directly
template <class V>
HWY_API V AVX2ShlU16Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint32_t, decltype(d)> du32;
  return TruncateTo(d, PromoteTo(du32, v) << PromoteTo(du32, bits));
}
#elif HWY_TARGET > HWY_AVX2

template <class D32>
static HWY_INLINE VFromD<D32> Pow2ConvF32ToI32(
    D32 d32, VFromD<RebindToFloat<D32>> vf32) {
  const RebindToSigned<decltype(d32)> di32;
#if HWY_COMPILER_GCC_ACTUAL
  // ConvertInRangeTo is safe with GCC due the inline assembly workaround used
  // for F32->I32 ConvertInRangeTo with GCC
  return BitCast(d32, ConvertInRangeTo(di32, vf32));
#else
  // Otherwise, use NearestIntInRange because we rely on the native 0x80..00
  // overflow behavior
  return BitCast(d32, NearestIntInRange(di32, vf32));
#endif
}

// Returns 2^v for use as per-lane multipliers to emulate 16-bit shifts.
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<MakeUnsigned<T>> Pow2(const Vec128<T> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RepartitionToWide<decltype(d)> dw;
  const Rebind<float, decltype(dw)> df;
  const auto zero = Zero(d);
  // Move into exponent (this u16 will become the upper half of an f32)
  const auto exp = ShiftLeft<23 - 16>(v);
  const auto upper = exp + Set(d, 0x3F80);  // upper half of 1.0f
  // Insert 0 into lower halves for reinterpreting as binary32.
  const auto f0 = ZipLower(dw, zero, upper);
  const auto f1 = ZipUpper(dw, zero, upper);
  // See cvtps comment below.
  const VFromD<decltype(dw)> bits0 = Pow2ConvF32ToI32(dw, BitCast(df, f0));
  const VFromD<decltype(dw)> bits1 = Pow2ConvF32ToI32(dw, BitCast(df, f1));
#if HWY_TARGET <= HWY_SSE4
  return VFromD<decltype(du)>{_mm_packus_epi32(bits0.raw, bits1.raw)};
#else
  return ConcatEven(du, BitCast(du, bits1), BitCast(du, bits0));
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2), HWY_IF_LANES_LE(N, 4)>
HWY_INLINE Vec128<MakeUnsigned<T>, N> Pow2(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const Twice<decltype(du)> dt_u;
  const RepartitionToWide<decltype(dt_u)> dt_w;
  const RebindToFloat<decltype(dt_w)> dt_f;
  // Move into exponent (this u16 will become the upper half of an f32)
  const auto exp = ShiftLeft<23 - 16>(v);
  const auto upper = exp + Set(d, 0x3F80);  // upper half of 1.0f
  // Insert 0 into lower halves for reinterpreting as binary32.
  const auto f0 = ZipLower(dt_w, Zero(dt_u), ResizeBitCast(dt_u, upper));
  // See cvtps comment below.
  const VFromD<decltype(dt_w)> bits0 =
      Pow2ConvF32ToI32(dt_w, BitCast(dt_f, f0));
#if HWY_TARGET <= HWY_SSE4
  return VFromD<decltype(du)>{_mm_packus_epi32(bits0.raw, bits0.raw)};
#elif HWY_TARGET == HWY_SSSE3
  alignas(16)
      const uint16_t kCompactEvenU16[8] = {0x0100, 0x0504, 0x0908, 0x0D0C};
  return TableLookupBytes(bits0, Load(du, kCompactEvenU16));
#else
  const RebindToSigned<decltype(dt_w)> dt_i32;
  const auto bits0_i32 = ShiftRight<16>(BitCast(dt_i32, ShiftLeft<16>(bits0)));
  return VFromD<decltype(du)>{_mm_packs_epi32(bits0_i32.raw, bits0_i32.raw)};
#endif
}

// Same, for 32-bit shifts.
template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE Vec128<MakeUnsigned<T>, N> Pow2(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToFloat<decltype(d)> df;
  const auto exp = ShiftLeft<23>(v);
  const auto f = exp + Set(d, 0x3F800000);  // 1.0f
  // Do not use ConvertTo because we rely on the native 0x80..00 overflow
  // behavior.
  return Pow2ConvF32ToI32(d, BitCast(df, f));
}

#endif  // HWY_TARGET > HWY_AVX2

template <size_t N>
HWY_API Vec128<uint16_t, N> Shl(hwy::UnsignedTag /*tag*/, Vec128<uint16_t, N> v,
                                Vec128<uint16_t, N> bits) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint16_t, N>{_mm_sllv_epi16(v.raw, bits.raw)};
#elif HWY_TARGET == HWY_AVX2
  return AVX2ShlU16Vec128(v, bits);
#else
  return v * Pow2(bits);
#endif
}

#if HWY_TARGET > HWY_AVX3
HWY_API Vec16<uint16_t> Shl(hwy::UnsignedTag /*tag*/, Vec16<uint16_t> v,
                            Vec16<uint16_t> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<uint16_t> bits16{_mm_cvtepu16_epi64(bits.raw)};
#else
  const auto bits16 = And(bits, Vec16<uint16_t>{_mm_set_epi64x(0, 0xFFFF)});
#endif
  return Vec16<uint16_t>{_mm_sll_epi16(v.raw, bits16.raw)};
}
#endif

#if HWY_TARGET <= HWY_AVX3
template <class V>
HWY_INLINE V AVX2ShlU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint16_t, decltype(d)> du16;
  return TruncateTo(d, PromoteTo(du16, v) << PromoteTo(du16, bits));
}
#elif HWY_TARGET <= HWY_AVX2
template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V AVX2ShlU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint32_t, decltype(d)> du32;
  return TruncateTo(d, PromoteTo(du32, v) << PromoteTo(du32, bits));
}
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V AVX2ShlU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Half<decltype(d)> dh;
  const Rebind<uint16_t, decltype(d)> du16;
  const Rebind<uint32_t, decltype(dh)> dh_u32;

  const VFromD<decltype(dh_u32)> lo_shl_result =
      PromoteTo(dh_u32, LowerHalf(dh, v))
      << PromoteTo(dh_u32, LowerHalf(dh, bits));
  const VFromD<decltype(dh_u32)> hi_shl_result =
      PromoteTo(dh_u32, UpperHalf(dh, v))
      << PromoteTo(dh_u32, UpperHalf(dh, bits));
  const VFromD<decltype(du16)> u16_shl_result = ConcatEven(
      du16, BitCast(du16, hi_shl_result), BitCast(du16, lo_shl_result));
  return TruncateTo(d, u16_shl_result);
}
#endif  // HWY_TARGET <= HWY_AVX3

// 8-bit: may use the Shl overload for uint16_t.
template <size_t N>
HWY_API Vec128<uint8_t, N> Shl(hwy::UnsignedTag tag, Vec128<uint8_t, N> v,
                               Vec128<uint8_t, N> bits) {
  const DFromV<decltype(v)> d;
#if HWY_TARGET <= HWY_AVX3_DL
  (void)tag;
  // kMask[i] = 0xFF >> i
  alignas(16) static constexpr uint8_t kMasks[16] = {
      0xFF, 0x7F, 0x3F, 0x1F, 0x0F, 0x07, 0x03, 0x01, 0x00};
  // kShl[i] = 1 << i
  alignas(16) static constexpr uint8_t kShl[16] = {1,    2,    4,    8,   0x10,
                                                   0x20, 0x40, 0x80, 0x00};
  v = And(v, TableLookupBytes(Load(Full64<uint8_t>(), kMasks), bits));
  const VFromD<decltype(d)> mul =
      TableLookupBytes(Load(Full64<uint8_t>(), kShl), bits);
  return VFromD<decltype(d)>{_mm_gf2p8mul_epi8(v.raw, mul.raw)};
#elif HWY_TARGET <= HWY_AVX2
  (void)tag;
  (void)d;
  return AVX2ShlU8Vec128(v, bits);
#else
  const Repartition<uint16_t, decltype(d)> dw;
  using VW = VFromD<decltype(dw)>;
  const VW even_mask = Set(dw, 0x00FF);
  const VW odd_mask = Set(dw, 0xFF00);
  const VW vw = BitCast(dw, v);
  const VW bits16 = BitCast(dw, bits);
  // Shift even lanes in-place
  const VW evens = Shl(tag, vw, And(bits16, even_mask));
  const VW odds = Shl(tag, And(vw, odd_mask), ShiftRight<8>(bits16));
  return OddEven(BitCast(d, odds), BitCast(d, evens));
#endif
}
HWY_API Vec128<uint8_t, 1> Shl(hwy::UnsignedTag /*tag*/, Vec128<uint8_t, 1> v,
                               Vec128<uint8_t, 1> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<uint16_t> bits8{_mm_cvtepu8_epi64(bits.raw)};
#else
  const Vec16<uint16_t> bits8 =
      And(Vec16<uint16_t>{bits.raw}, Vec16<uint16_t>{_mm_set_epi64x(0, 0xFF)});
#endif
  return Vec128<uint8_t, 1>{_mm_sll_epi16(v.raw, bits8.raw)};
}

template <size_t N>
HWY_API Vec128<uint32_t, N> Shl(hwy::UnsignedTag /*tag*/, Vec128<uint32_t, N> v,
                                Vec128<uint32_t, N> bits) {
#if HWY_TARGET >= HWY_SSE4
  return v * Pow2(bits);
#else
  return Vec128<uint32_t, N>{_mm_sllv_epi32(v.raw, bits.raw)};
#endif
}

#if HWY_TARGET >= HWY_SSE4
HWY_API Vec32<uint32_t> Shl(hwy::UnsignedTag /*tag*/, Vec32<uint32_t> v,
                            const Vec32<uint32_t> bits) {
#if HWY_TARGET == HWY_SSE4
  const Vec32<uint32_t> bits32{_mm_cvtepu32_epi64(bits.raw)};
#else
  const auto bits32 =
      Combine(Full64<uint32_t>(), Zero(Full32<uint32_t>()), bits);
#endif
  return Vec32<uint32_t>{_mm_sll_epi32(v.raw, bits32.raw)};
}
#endif

HWY_API Vec128<uint64_t> Shl(hwy::UnsignedTag /*tag*/, Vec128<uint64_t> v,
                             Vec128<uint64_t> bits) {
#if HWY_TARGET >= HWY_SSE4
  const DFromV<decltype(v)> d;
  // Individual shifts and combine
  const Vec128<uint64_t> out0{_mm_sll_epi64(v.raw, bits.raw)};
  const __m128i bits1 = _mm_unpackhi_epi64(bits.raw, bits.raw);
  const Vec128<uint64_t> out1{_mm_sll_epi64(v.raw, bits1)};
  return ConcatUpperLower(d, out1, out0);
#else
  return Vec128<uint64_t>{_mm_sllv_epi64(v.raw, bits.raw)};
#endif
}
HWY_API Vec64<uint64_t> Shl(hwy::UnsignedTag /*tag*/, Vec64<uint64_t> v,
                            Vec64<uint64_t> bits) {
  return Vec64<uint64_t>{_mm_sll_epi64(v.raw, bits.raw)};
}

// Signed left shift is the same as unsigned.
template <typename T, size_t N>
HWY_API Vec128<T, N> Shl(hwy::SignedTag /*tag*/, Vec128<T, N> v,
                         Vec128<T, N> bits) {
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  return BitCast(di,
                 Shl(hwy::UnsignedTag(), BitCast(du, v), BitCast(du, bits)));
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, Vec128<T, N> bits) {
  return detail::Shl(hwy::TypeTag<T>(), v, bits);
}

// ------------------------------ Shr (mul, mask, BroadcastSignBit)

// Use AVX2+ variable shifts except for SSSE3/SSE4. There, we use
// widening multiplication by powers of two obtained by loading float exponents,
// followed by a constant right-shift. This is still faster than a scalar or
// bit-test approach: https://gcc.godbolt.org/z/9G7Y9v.

#if HWY_TARGET <= HWY_AVX2
namespace detail {

#if HWY_TARGET <= HWY_AVX3
template <class V>
HWY_INLINE V AVX2ShrU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint16_t, decltype(d)> du16;
  const RebindToSigned<decltype(du16)> di16;
  return DemoteTo(d,
                  BitCast(di16, PromoteTo(du16, v) >> PromoteTo(du16, bits)));
}
#else   // AVX2
template <class V>
HWY_INLINE V AVX2ShrU16Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint32_t, decltype(d)> du32;
  const RebindToSigned<decltype(du32)> di32;
  return DemoteTo(d,
                  BitCast(di32, PromoteTo(du32, v) >> PromoteTo(du32, bits)));
}
template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V AVX2ShrU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<uint32_t, decltype(d)> du32;
  const RebindToSigned<decltype(du32)> di32;
  return DemoteTo(d,
                  BitCast(di32, PromoteTo(du32, v) >> PromoteTo(du32, bits)));
}
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V AVX2ShrU8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Half<decltype(d)> dh;
  const Rebind<int16_t, decltype(d)> di16;
  const Rebind<uint16_t, decltype(d)> du16;
  const Rebind<int32_t, decltype(dh)> dh_i32;
  const Rebind<uint32_t, decltype(dh)> dh_u32;

  const auto lo_shr_result =
      BitCast(dh_i32, PromoteTo(dh_u32, LowerHalf(dh, v)) >>
                          PromoteTo(dh_u32, LowerHalf(dh, bits)));
  const auto hi_shr_result =
      BitCast(dh_i32, PromoteTo(dh_u32, UpperHalf(dh, v)) >>
                          PromoteTo(dh_u32, UpperHalf(dh, bits)));
  const auto i16_shr_result =
      BitCast(di16, OrderedDemote2To(du16, lo_shr_result, hi_shr_result));
  return DemoteTo(d, i16_shr_result);
}
#endif  // HWY_TARGET <= HWY_AVX3

}  // namespace detail
#endif  // HWY_TARGET <= HWY_AVX2

template <size_t N>
HWY_API Vec128<uint16_t, N> operator>>(Vec128<uint16_t, N> in,
                                       const Vec128<uint16_t, N> bits) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<uint16_t, N>{_mm_srlv_epi16(in.raw, bits.raw)};
#elif HWY_TARGET <= HWY_AVX2
  return detail::AVX2ShrU16Vec128(in, bits);
#else
  const DFromV<decltype(in)> d;
  // For bits=0, we cannot mul by 2^16, so fix the result later.
  const auto out = MulHigh(in, detail::Pow2(Set(d, 16) - bits));
  // Replace output with input where bits == 0.
  return IfThenElse(bits == Zero(d), in, out);
#endif
}

#if HWY_TARGET > HWY_AVX3
HWY_API Vec16<uint16_t> operator>>(const Vec16<uint16_t> in,
                                   const Vec16<uint16_t> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<uint16_t> bits16{_mm_cvtepu16_epi64(bits.raw)};
#else
  const auto bits16 = And(bits, Vec16<uint16_t>{_mm_set_epi64x(0, 0xFFFF)});
#endif
  return Vec16<uint16_t>{_mm_srl_epi16(in.raw, bits16.raw)};
}
#endif

// 8-bit uses 16-bit shifts.
template <size_t N>
HWY_API Vec128<uint8_t, N> operator>>(Vec128<uint8_t, N> in,
                                      const Vec128<uint8_t, N> bits) {
#if HWY_TARGET <= HWY_AVX2
  return detail::AVX2ShrU8Vec128(in, bits);
#else
  const DFromV<decltype(in)> d;
  const Repartition<uint16_t, decltype(d)> dw;
  using VW = VFromD<decltype(dw)>;
  const VW mask = Set(dw, 0x00FF);
  const VW vw = BitCast(dw, in);
  const VW bits16 = BitCast(dw, bits);
  const VW evens = And(vw, mask) >> And(bits16, mask);
  // Shift odd lanes in-place
  const VW odds = vw >> ShiftRight<8>(bits16);
  return OddEven(BitCast(d, odds), BitCast(d, evens));
#endif
}
HWY_API Vec128<uint8_t, 1> operator>>(const Vec128<uint8_t, 1> in,
                                      const Vec128<uint8_t, 1> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<uint16_t> in8{_mm_cvtepu8_epi16(in.raw)};
  const Vec16<uint16_t> bits8{_mm_cvtepu8_epi64(bits.raw)};
#else
  const Vec16<uint16_t> mask{_mm_set_epi64x(0, 0xFF)};
  const Vec16<uint16_t> in8 = And(Vec16<uint16_t>{in.raw}, mask);
  const Vec16<uint16_t> bits8 = And(Vec16<uint16_t>{bits.raw}, mask);
#endif
  return Vec128<uint8_t, 1>{_mm_srl_epi16(in8.raw, bits8.raw)};
}

template <size_t N>
HWY_API Vec128<uint32_t, N> operator>>(const Vec128<uint32_t, N> in,
                                       const Vec128<uint32_t, N> bits) {
#if HWY_TARGET >= HWY_SSE4
  // 32x32 -> 64 bit mul, then shift right by 32.
  const DFromV<decltype(in)> d32;
  // Move odd lanes into position for the second mul. Shuffle more gracefully
  // handles N=1 than repartitioning to u64 and shifting 32 bits right.
  const Vec128<uint32_t, N> in31{_mm_shuffle_epi32(in.raw, 0x31)};
  // For bits=0, we cannot mul by 2^32, so fix the result later.
  const auto mul = detail::Pow2(Set(d32, 32) - bits);
  const auto out20 = ShiftRight<32>(MulEven(in, mul));  // z 2 z 0
  const Vec128<uint32_t, N> mul31{_mm_shuffle_epi32(mul.raw, 0x31)};
  // No need to shift right, already in the correct position.
  const auto out31 = BitCast(d32, MulEven(in31, mul31));  // 3 ? 1 ?
  const Vec128<uint32_t, N> out = OddEven(out31, BitCast(d32, out20));
  // Replace output with input where bits == 0.
  return IfThenElse(bits == Zero(d32), in, out);
#else
  return Vec128<uint32_t, N>{_mm_srlv_epi32(in.raw, bits.raw)};
#endif
}

#if HWY_TARGET >= HWY_SSE4
HWY_API Vec128<uint32_t, 1> operator>>(const Vec128<uint32_t, 1> in,
                                       const Vec128<uint32_t, 1> bits) {
#if HWY_TARGET == HWY_SSE4
  const Vec32<uint32_t> bits32{_mm_cvtepu32_epi64(bits.raw)};
#else
  const auto bits32 =
      Combine(Full64<uint32_t>(), Zero(Full32<uint32_t>()), bits);
#endif
  return Vec128<uint32_t, 1>{_mm_srl_epi32(in.raw, bits32.raw)};
}
#endif

HWY_API Vec128<uint64_t> operator>>(const Vec128<uint64_t> v,
                                    const Vec128<uint64_t> bits) {
#if HWY_TARGET >= HWY_SSE4
  const DFromV<decltype(v)> d;
  // Individual shifts and combine
  const Vec128<uint64_t> out0{_mm_srl_epi64(v.raw, bits.raw)};
  const __m128i bits1 = _mm_unpackhi_epi64(bits.raw, bits.raw);
  const Vec128<uint64_t> out1{_mm_srl_epi64(v.raw, bits1)};
  return ConcatUpperLower(d, out1, out0);
#else
  return Vec128<uint64_t>{_mm_srlv_epi64(v.raw, bits.raw)};
#endif
}
HWY_API Vec64<uint64_t> operator>>(const Vec64<uint64_t> v,
                                   const Vec64<uint64_t> bits) {
  return Vec64<uint64_t>{_mm_srl_epi64(v.raw, bits.raw)};
}

namespace detail {

#if HWY_TARGET <= HWY_AVX3
template <class V>
HWY_INLINE V AVX2ShrI8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<int16_t, decltype(d)> di16;
  return DemoteTo(d, PromoteTo(di16, v) >> PromoteTo(di16, bits));
}
#elif HWY_TARGET <= HWY_AVX2  // AVX2
template <class V>
HWY_INLINE V AVX2ShrI16Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<int32_t, decltype(d)> di32;
  return DemoteTo(d, PromoteTo(di32, v) >> PromoteTo(di32, bits));
}
template <class V, HWY_IF_V_SIZE_LE_V(V, 8)>
HWY_INLINE V AVX2ShrI8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Rebind<int32_t, decltype(d)> di32;
  return DemoteTo(d, PromoteTo(di32, v) >> PromoteTo(di32, bits));
}
template <class V, HWY_IF_V_SIZE_V(V, 16)>
HWY_INLINE V AVX2ShrI8Vec128(V v, V bits) {
  const DFromV<decltype(v)> d;
  const Half<decltype(d)> dh;
  const Rebind<int16_t, decltype(d)> di16;
  const Rebind<int32_t, decltype(dh)> dh_i32;

  const auto lo_shr_result = PromoteTo(dh_i32, LowerHalf(dh, v)) >>
                             PromoteTo(dh_i32, LowerHalf(dh, bits));
  const auto hi_shr_result = PromoteTo(dh_i32, UpperHalf(dh, v)) >>
                             PromoteTo(dh_i32, UpperHalf(dh, bits));
  const auto i16_shr_result =
      OrderedDemote2To(di16, lo_shr_result, hi_shr_result);
  return DemoteTo(d, i16_shr_result);
}
#endif

#if HWY_TARGET > HWY_AVX3
// Also used in x86_256-inl.h.
template <class DI, class V>
HWY_INLINE V SignedShr(const DI di, const V v, const V count_i) {
  const RebindToUnsigned<DI> du;
  const auto count = BitCast(du, count_i);  // same type as value to shift
  // Clear sign and restore afterwards. This is preferable to shifting the MSB
  // downwards because Shr is somewhat more expensive than Shl.
  const auto sign = BroadcastSignBit(v);
  const auto abs = BitCast(du, v ^ sign);  // off by one, but fixed below
  return BitCast(di, abs >> count) ^ sign;
}
#endif

}  // namespace detail

template <size_t N>
HWY_API Vec128<int16_t, N> operator>>(Vec128<int16_t, N> v,
                                      Vec128<int16_t, N> bits) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<int16_t, N>{_mm_srav_epi16(v.raw, bits.raw)};
#elif HWY_TARGET <= HWY_AVX2
  return detail::AVX2ShrI16Vec128(v, bits);
#else
  const DFromV<decltype(v)> d;
  return detail::SignedShr(d, v, bits);
#endif
}

#if HWY_TARGET > HWY_AVX3
HWY_API Vec16<int16_t> operator>>(Vec16<int16_t> v, Vec16<int16_t> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<int16_t> bits16{_mm_cvtepu16_epi64(bits.raw)};
#else
  const auto bits16 = And(bits, Vec16<int16_t>{_mm_set_epi64x(0, 0xFFFF)});
#endif
  return Vec16<int16_t>{_mm_sra_epi16(v.raw, bits16.raw)};
}
#endif

template <size_t N>
HWY_API Vec128<int8_t, N> operator>>(Vec128<int8_t, N> v,
                                     Vec128<int8_t, N> bits) {
#if HWY_TARGET <= HWY_AVX2
  return detail::AVX2ShrI8Vec128(v, bits);
#else
  const DFromV<decltype(v)> d;
  return detail::SignedShr(d, v, bits);
#endif
}
HWY_API Vec128<int8_t, 1> operator>>(Vec128<int8_t, 1> v,
                                     Vec128<int8_t, 1> bits) {
#if HWY_TARGET <= HWY_SSE4
  const Vec16<int16_t> vi16{_mm_cvtepi8_epi16(v.raw)};
  const Vec16<uint16_t> bits8{_mm_cvtepu8_epi64(bits.raw)};
#else
  const DFromV<decltype(v)> d;
  const Rebind<int16_t, decltype(d)> di16;
  const Twice<decltype(d)> dt;

  const auto vi16 = ShiftRight<8>(BitCast(di16, Combine(dt, v, v)));
  const Vec16<uint16_t> bits8 =
      And(Vec16<uint16_t>{bits.raw}, Vec16<uint16_t>{_mm_set_epi64x(0, 0xFF)});
#endif
  return Vec128<int8_t, 1>{_mm_sra_epi16(vi16.raw, bits8.raw)};
}

template <size_t N>
HWY_API Vec128<int32_t, N> operator>>(Vec128<int32_t, N> v,
                                      Vec128<int32_t, N> bits) {
#if HWY_TARGET <= HWY_AVX2
  return Vec128<int32_t, N>{_mm_srav_epi32(v.raw, bits.raw)};
#else
  const DFromV<decltype(v)> d;
  return detail::SignedShr(d, v, bits);
#endif
}

#if HWY_TARGET > HWY_AVX2
HWY_API Vec32<int32_t> operator>>(Vec32<int32_t> v, Vec32<int32_t> bits) {
#if HWY_TARGET == HWY_SSE4
  const Vec32<uint32_t> bits32{_mm_cvtepu32_epi64(bits.raw)};
#else
  const auto bits32 = Combine(Full64<int32_t>(), Zero(Full32<int32_t>()), bits);
#endif
  return Vec32<int32_t>{_mm_sra_epi32(v.raw, bits32.raw)};
}
#endif

template <size_t N>
HWY_API Vec128<int64_t, N> operator>>(Vec128<int64_t, N> v,
                                      Vec128<int64_t, N> bits) {
#if HWY_TARGET <= HWY_AVX3
  return Vec128<int64_t, N>{_mm_srav_epi64(v.raw, bits.raw)};
#else
  const DFromV<decltype(v)> d;
  return detail::SignedShr(d, v, bits);
#endif
}

// ------------------------------ MulEven/Odd 64x64 (UpperHalf)

namespace detail {

template <class V, HWY_IF_U64(TFromV<V>)>
static HWY_INLINE V SSE2Mul128(V a, V b, V& mulH) {
  const DFromV<decltype(a)> du64;
  const RepartitionToNarrow<decltype(du64)> du32;
  const auto maskL = Set(du64, 0xFFFFFFFFULL);
  const auto a32 = BitCast(du32, a);
  const auto b32 = BitCast(du32, b);
  // Inputs for MulEven: we only need the lower 32 bits
  const auto aH = Shuffle2301(a32);
  const auto bH = Shuffle2301(b32);

  // Knuth double-word multiplication. We use 32x32 = 64 MulEven and only need
  // the even (lower 64 bits of every 128-bit block) results. See
  // https://github.com/hcs0/Hackers-Delight/blob/master/muldwu.c.txt
  const auto aLbL = MulEven(a32, b32);
  const auto w3 = aLbL & maskL;

  const auto t2 = MulEven(aH, b32) + ShiftRight<32>(aLbL);
  const auto w2 = t2 & maskL;
  const auto w1 = ShiftRight<32>(t2);

  const auto t = MulEven(a32, bH) + w2;
  const auto k = ShiftRight<32>(t);

  mulH = MulEven(aH, bH) + w1 + k;
  return ShiftLeft<32>(t) + w3;
}

template <class V, HWY_IF_I64(TFromV<V>)>
static HWY_INLINE V SSE2Mul128(V a, V b, V& mulH) {
  const DFromV<decltype(a)> di64;
  const RebindToUnsigned<decltype(di64)> du64;
  using VU64 = VFromD<decltype(du64)>;

  VU64 unsigned_mulH;
  const auto mulL = BitCast(
      di64, SSE2Mul128(BitCast(du64, a), BitCast(du64, b), unsigned_mulH));
  mulH = BitCast(di64, unsigned_mulH) - And(BroadcastSignBit(a), b) -
         And(a, BroadcastSignBit(b));
  return mulL;
}

}  // namespace detail

#if !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2

template <class V, HWY_IF_UI64(TFromV<V>),
          HWY_IF_V_SIZE_GT_V(V, (HWY_ARCH_X86_64 ? 16 : 8))>
HWY_API V MulEven(V a, V b) {
  V mulH;
  const V mulL = detail::SSE2Mul128(a, b, mulH);
  return InterleaveLower(mulL, mulH);
}

template <class V, HWY_IF_UI64(TFromV<V>),
          HWY_IF_V_SIZE_GT_V(V, (HWY_ARCH_X86_64 ? 16 : 8))>
HWY_API V MulOdd(V a, V b) {
  const DFromV<decltype(a)> du64;
  V mulH;
  const V mulL = detail::SSE2Mul128(a, b, mulH);
  return InterleaveUpper(du64, mulL, mulH);
}

#endif  // !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2

template <class V, HWY_IF_UI64(TFromV<V>),
          HWY_IF_V_SIZE_GT_V(V, (HWY_ARCH_X86_64 ? 8 : 0))>
HWY_API V MulHigh(V a, V b) {
  V mulH;
  detail::SSE2Mul128(a, b, mulH);
  return mulH;
}

#if HWY_ARCH_X86_64

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulEven(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  alignas(16) T mul[2];
  mul[0] = Mul128(GetLane(a), GetLane(b), &mul[1]);
  return Load(d, mul);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulOdd(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const Half<decltype(d)> d2;
  alignas(16) T mul[2];
  const T a1 = GetLane(UpperHalf(d2, a));
  const T b1 = GetLane(UpperHalf(d2, b));
  mul[0] = Mul128(a1, b1, &mul[1]);
  return Load(d, mul);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec64<T> MulHigh(Vec64<T> a, Vec64<T> b) {
  T hi;
  Mul128(GetLane(a), GetLane(b), &hi);
  return Vec64<T>{_mm_cvtsi64_si128(static_cast<int64_t>(hi))};
}

#endif  // HWY_ARCH_X86_64

// ================================================== CONVERT (2)

// ------------------------------ PromoteEvenTo/PromoteOddTo

#if HWY_TARGET > HWY_AVX3
namespace detail {

// I32->I64 PromoteEvenTo/PromoteOddTo

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   Vec64<int32_t> v) {
  return PromoteLowerTo(d_to, v);
}

template <class D, HWY_IF_LANES_D(D, 2)>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D d_to,
                                   Vec128<int32_t> v) {
  const Repartition<int32_t, D> d_from;
  return PromoteLowerTo(d_to, ConcatEven(d_from, v, v));
}

template <class D, class V, HWY_IF_LANES_LE_D(D, 2)>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::SignedTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  hwy::SignedTag /*from_type_tag*/, D d_to,
                                  V v) {
  const Repartition<int32_t, D> d_from;
  return PromoteLowerTo(d_to, ConcatOdd(d_from, v, v));
}

}  // namespace detail
#endif

// ------------------------------ PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ------------------------------ WidenMulPairwiseAdd (PromoteEvenTo)

#if HWY_NATIVE_DOT_BF16

template <class DF, HWY_IF_F32_D(DF), HWY_IF_V_SIZE_LE_D(DF, 16),
          class VBF = VFromD<Repartition<bfloat16_t, DF>>>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  return VFromD<DF>{_mm_dpbf16_ps(Zero(df).raw,
                                  reinterpret_cast<__m128bh>(a.raw),
                                  reinterpret_cast<__m128bh>(b.raw))};
}

#else

// Generic for all vector lengths.
template <class DF, HWY_IF_F32_D(DF),
          class VBF = VFromD<Repartition<bfloat16_t, DF>>>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
}

#endif  // HWY_NATIVE_DOT_BF16

// Even if N=1, the input is always at least 2 lanes, hence madd_epi16 is safe.
template <class D32, HWY_IF_I32_D(D32), HWY_IF_V_SIZE_LE_D(D32, 16),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> WidenMulPairwiseAdd(D32 /* tag */, V16 a, V16 b) {
  return VFromD<D32>{_mm_madd_epi16(a.raw, b.raw)};
}

// Generic for all vector lengths.
template <class DU32, HWY_IF_U32_D(DU32),
          class VU16 = VFromD<RepartitionToNarrow<DU32>>>
HWY_API VFromD<DU32> WidenMulPairwiseAdd(DU32 du32, VU16 a, VU16 b) {
  const auto p_lo = a * b;
  const auto p_hi = MulHigh(a, b);

  const auto p_hi1_lo0 = BitCast(du32, OddEven(p_hi, p_lo));
  const auto p_hi0_lo1 = Or(ShiftLeft<16>(BitCast(du32, p_hi)),
                            ShiftRight<16>(BitCast(du32, p_lo)));
  return Add(BitCast(du32, p_hi1_lo0), BitCast(du32, p_hi0_lo1));
}

// ------------------------------ SatWidenMulPairwiseAdd

#if HWY_TARGET <= HWY_SSSE3

#ifdef HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#undef HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#else
#define HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#endif

// Even if N=1, the input is always at least 2 lanes, hence _mm_maddubs_epi16
// is safe.
template <class DI16, HWY_IF_I16_D(DI16), HWY_IF_V_SIZE_LE_D(DI16, 16)>
HWY_API VFromD<DI16> SatWidenMulPairwiseAdd(
    DI16 /* tag */, VFromD<Repartition<uint8_t, DI16>> a,
    VFromD<Repartition<int8_t, DI16>> b) {
  return VFromD<DI16>{_mm_maddubs_epi16(a.raw, b.raw)};
}

#endif

// ------------------------------ SatWidenMulPairwiseAccumulate

#if HWY_TARGET <= HWY_AVX3_DL

#ifdef HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#undef HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#else
#define HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#endif

// Even if N=1, the I16 vectors have at least 2 lanes, hence _mm_dpwssds_epi32
// is safe.
template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 16)>
HWY_API VFromD<DI32> SatWidenMulPairwiseAccumulate(
    DI32 /* tag */, VFromD<Repartition<int16_t, DI32>> a,
    VFromD<Repartition<int16_t, DI32>> b, VFromD<DI32> sum) {
  return VFromD<DI32>{_mm_dpwssds_epi32(sum.raw, a.raw, b.raw)};
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

// ------------------------------ ReorderWidenMulAccumulate (PromoteEvenTo)

#if HWY_NATIVE_DOT_BF16

#ifdef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#undef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#else
#define HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#endif

template <class DF, HWY_IF_F32_D(DF), HWY_IF_V_SIZE_LE_D(DF, 16),
          class VBF = VFromD<Repartition<bfloat16_t, DF>>>
HWY_API VFromD<DF> ReorderWidenMulAccumulate(DF /*df*/, VBF a, VBF b,
                                             const VFromD<DF> sum0,
                                             VFromD<DF>& /*sum1*/) {
  return VFromD<DF>{_mm_dpbf16_ps(sum0.raw, reinterpret_cast<__m128bh>(a.raw),
                                  reinterpret_cast<__m128bh>(b.raw))};
}

#endif  // HWY_NATIVE_DOT_BF16

// Even if N=1, the input is always at least 2 lanes, hence madd_epi16 is safe.
template <class D32, HWY_IF_I32_D(D32), HWY_IF_V_SIZE_LE_D(D32, 16),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> ReorderWidenMulAccumulate(D32 d, V16 a, V16 b,
                                              const VFromD<D32> sum0,
                                              VFromD<D32>& /*sum1*/) {
  (void)d;
#if HWY_TARGET <= HWY_AVX3_DL
  return VFromD<D32>{_mm_dpwssd_epi32(sum0.raw, a.raw, b.raw)};
#else
  return sum0 + WidenMulPairwiseAdd(d, a, b);
#endif
}

template <class DU32, HWY_IF_U32_D(DU32),
          class VU16 = VFromD<RepartitionToNarrow<DU32>>>
HWY_API VFromD<DU32> ReorderWidenMulAccumulate(DU32 d, VU16 a, VU16 b,
                                               const VFromD<DU32> sum0,
                                               VFromD<DU32>& /*sum1*/) {
  (void)d;
  return sum0 + WidenMulPairwiseAdd(d, a, b);
}

// ------------------------------ RearrangeToOddPlusEven
template <size_t N>
HWY_API Vec128<int32_t, N> RearrangeToOddPlusEven(const Vec128<int32_t, N> sum0,
                                                  Vec128<int32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <size_t N>
HWY_API Vec128<uint32_t, N> RearrangeToOddPlusEven(
    const Vec128<uint32_t, N> sum0, Vec128<uint32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <class VW>
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  return Add(sum0, sum1);
}

// ------------------------------ SumOfMulQuadAccumulate
#if HWY_TARGET <= HWY_AVX3_DL

#ifdef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 16)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(
    DI32 /*di32*/, VFromD<Repartition<uint8_t, DI32>> a_u,
    VFromD<Repartition<int8_t, DI32>> b_i, VFromD<DI32> sum) {
  return VFromD<DI32>{_mm_dpbusd_epi32(sum.raw, a_u.raw, b_i.raw)};
}

#ifdef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_I8_I8_SUMOFMULQUADACCUMULATE
#endif
template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(DI32 di32,
                                            VFromD<Repartition<int8_t, DI32>> a,
                                            VFromD<Repartition<int8_t, DI32>> b,
                                            VFromD<DI32> sum) {
  // TODO(janwas): AVX-VNNI-INT8 has dpbssd.
  const Repartition<uint8_t, decltype(di32)> du8;

  const auto a_u = BitCast(du8, a);
  const auto result_sum_0 = SumOfMulQuadAccumulate(di32, a_u, b, sum);
  const auto result_sum_1 = ShiftLeft<8>(
      SumOfMulQuadAccumulate(di32, ShiftRight<7>(a_u), b, Zero(di32)));
  return result_sum_0 - result_sum_1;
}

#ifdef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#endif
template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API VFromD<DU32> SumOfMulQuadAccumulate(
    DU32 du32, VFromD<Repartition<uint8_t, DU32>> a,
    VFromD<Repartition<uint8_t, DU32>> b, VFromD<DU32> sum) {
  // TODO(janwas): AVX-VNNI-INT8 has dpbuud.
  const Repartition<uint8_t, decltype(du32)> du8;
  const RebindToSigned<decltype(du8)> di8;
  const RebindToSigned<decltype(du32)> di32;

  const auto b_i = BitCast(di8, b);
  const auto result_sum_0 =
      SumOfMulQuadAccumulate(di32, a, b_i, BitCast(di32, sum));
  const auto result_sum_1 = ShiftLeft<8>(
      SumOfMulQuadAccumulate(di32, a, BroadcastSignBit(b_i), Zero(di32)));

  return BitCast(du32, result_sum_0 - result_sum_1);
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

// ------------------------------ Demotions (full -> part w/ narrow lanes)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{_mm_packs_epi32(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
#if HWY_TARGET >= HWY_SSSE3
  const Rebind<int32_t, D> di32;
  const auto zero_if_neg = AndNot(ShiftRight<31>(v), v);
  const auto too_big = VecFromMask(di32, Gt(v, Set(di32, 0xFFFF)));
  const auto clamped = Or(zero_if_neg, too_big);
#if HWY_TARGET == HWY_SSE2
  const Rebind<uint16_t, decltype(di32)> du16;
  const RebindToSigned<decltype(du16)> di16;
  return BitCast(du16, DemoteTo(di16, ShiftRight<16>(ShiftLeft<16>(clamped))));
#else
  const Repartition<uint16_t, decltype(di32)> du16;
  // Lower 2 bytes from each 32-bit lane; same as return type for fewer casts.
  alignas(16) static constexpr uint16_t kLower2Bytes[16] = {
      0x0100, 0x0504, 0x0908, 0x0D0C, 0x8080, 0x8080, 0x8080, 0x8080};
  const auto lo2 = Load(du16, kLower2Bytes);
  return VFromD<D>{TableLookupBytes(BitCast(du16, clamped), lo2).raw};
#endif
#else
  return VFromD<D>{_mm_packus_epi32(v.raw, v.raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D du16, VFromD<Rebind<uint32_t, D>> v) {
  const DFromV<decltype(v)> du32;
  const RebindToSigned<decltype(du32)> di32;
#if HWY_TARGET >= HWY_SSSE3
  const auto too_big =
      VecFromMask(di32, Gt(BitCast(di32, ShiftRight<16>(v)), Zero(di32)));
  const auto clamped = Or(BitCast(di32, v), too_big);
#if HWY_TARGET == HWY_SSE2
  const RebindToSigned<decltype(du16)> di16;
  return BitCast(du16, DemoteTo(di16, ShiftRight<16>(ShiftLeft<16>(clamped))));
#else
  (void)du16;
  const Repartition<uint16_t, decltype(di32)> du16_full;
  // Lower 2 bytes from each 32-bit lane; same as return type for fewer casts.
  alignas(16) static constexpr uint16_t kLower2Bytes[16] = {
      0x0100, 0x0504, 0x0908, 0x0D0C, 0x8080, 0x8080, 0x8080, 0x8080};
  const auto lo2 = Load(du16_full, kLower2Bytes);
  return VFromD<D>{TableLookupBytes(BitCast(du16_full, clamped), lo2).raw};
#endif
#else
  return DemoteTo(du16, BitCast(di32, Min(v, Set(du32, 0x7FFFFFFF))));
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const __m128i i16 = _mm_packs_epi32(v.raw, v.raw);
  return VFromD<D>{_mm_packus_epi16(i16, i16)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{_mm_packus_epi16(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const __m128i i16 = _mm_packs_epi32(v.raw, v.raw);
  return VFromD<D>{_mm_packs_epi16(i16, i16)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{_mm_packs_epi16(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D du8, VFromD<Rebind<uint32_t, D>> v) {
#if HWY_TARGET <= HWY_AVX3
  // NOTE: _mm_cvtusepi32_epi8 is a saturated conversion of 32-bit unsigned
  // integers to 8-bit unsigned integers
  (void)du8;
  return VFromD<D>{_mm_cvtusepi32_epi8(v.raw)};
#else
  const DFromV<decltype(v)> du32;
  const RebindToSigned<decltype(du32)> di32;
  const auto max_i32 = Set(du32, 0x7FFFFFFFu);

#if HWY_TARGET >= HWY_SSSE3
  // On SSE2/SSSE3, clamp u32 values to an i32 using the u8 Min operation
  // as SSE2/SSSE3 can do an u8 Min operation in a single instruction.

  // The u8 Min operation below leaves the lower 24 bits of each 32-bit
  // lane unchanged.

  // The u8 Min operation below will leave any values that are less than or
  // equal to 0x7FFFFFFF unchanged.

  // For values that are greater than or equal to 0x80000000, the u8 Min
  // operation below will force the upper 8 bits to 0x7F and leave the lower
  // 24 bits unchanged.

  // An u8 Min operation is okay here as any clamped value that is greater than
  // or equal to 0x80000000 will be clamped to a value between 0x7F000000 and
  // 0x7FFFFFFF through the u8 Min operation below, which will then be converted
  // to 0xFF through the i32->u8 demotion.
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;
  const auto clamped = BitCast(
      di32, Min(BitCast(du32_as_du8, v), BitCast(du32_as_du8, max_i32)));
#else
  const auto clamped = BitCast(di32, Min(v, max_i32));
#endif

  return DemoteTo(du8, clamped);
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D du8, VFromD<Rebind<uint16_t, D>> v) {
  const DFromV<decltype(v)> du16;
  const RebindToSigned<decltype(du16)> di16;
  const auto max_i16 = Set(du16, 0x7FFF);

#if HWY_TARGET >= HWY_SSSE3
  // On SSE2/SSSE3, clamp u16 values to an i16 using the u8 Min operation
  // as SSE2/SSSE3 can do an u8 Min operation in a single instruction.

  // The u8 Min operation below leaves the lower 8 bits of each 16-bit
  // lane unchanged.

  // The u8 Min operation below will leave any values that are less than or
  // equal to 0x7FFF unchanged.

  // For values that are greater than or equal to 0x8000, the u8 Min
  // operation below will force the upper 8 bits to 0x7F and leave the lower
  // 8 bits unchanged.

  // An u8 Min operation is okay here as any clamped value that is greater than
  // or equal to 0x8000 will be clamped to a value between 0x7F00 and
  // 0x7FFF through the u8 Min operation below, which will then be converted
  // to 0xFF through the i16->u8 demotion.
  const Repartition<uint8_t, decltype(du16)> du16_as_du8;
  const auto clamped = BitCast(
      di16, Min(BitCast(du16_as_du8, v), BitCast(du16_as_du8, max_i16)));
#else
  const auto clamped = BitCast(di16, Min(v, max_i16));
#endif

  return DemoteTo(du8, clamped);
}

#if HWY_TARGET < HWY_SSE4 && !defined(HWY_DISABLE_F16C)

// HWY_NATIVE_F16C was already toggled above.

// Work around MSVC warning for _mm_cvtps_ph (8 is actually a valid immediate).
// clang-cl requires a non-empty string, so we 'ignore' the irrelevant -Wmain.
HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4556, ignored "-Wmain")

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<float, D>> v) {
  const RebindToUnsigned<decltype(df16)> du16;
  return BitCast(
      df16, VFromD<decltype(du16)>{_mm_cvtps_ph(v.raw, _MM_FROUND_NO_EXC)});
}

HWY_DIAGNOSTICS(pop)

#endif  // F16C

#if HWY_HAVE_FLOAT16

#ifdef HWY_NATIVE_DEMOTE_F64_TO_F16
#undef HWY_NATIVE_DEMOTE_F64_TO_F16
#else
#define HWY_NATIVE_DEMOTE_F64_TO_F16
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D /*df16*/, VFromD<Rebind<double, D>> v) {
  return VFromD<D>{_mm_cvtpd_ph(v.raw)};
}

#endif  // HWY_HAVE_FLOAT16

// The _mm*_cvtneps_pbh and _mm*_cvtne2ps_pbh intrinsics require GCC 9 or later
// or Clang 10 or later

// Also need GCC or Clang to bit cast the __m128bh, __m256bh, or __m512bh vector
// returned by the _mm*_cvtneps_pbh and _mm*_cvtne2ps_pbh intrinsics to a
// __m128i, __m256i, or __m512i as there are currently no intrinsics available
// (as of GCC 13 and Clang 17) to bit cast a __m128bh, __m256bh, or __m512bh
// vector to a __m128i, __m256i, or __m512i vector

#if HWY_AVX3_HAVE_F32_TO_BF16C
#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> DemoteTo(D /*dbf16*/, VFromD<Rebind<float, D>> v) {
#if HWY_COMPILER_CLANG >= 1600 && HWY_COMPILER_CLANG < 2000
  // Inline assembly workaround for LLVM codegen bug
  __m128i raw_result;
  __asm__("vcvtneps2bf16 %1, %0" : "=v"(raw_result) : "v"(v.raw));
  return VFromD<D>{raw_result};
#else
  // The _mm_cvtneps_pbh intrinsic returns a __m128bh vector that needs to be
  // bit casted to a __m128i vector
  return VFromD<D>{detail::BitCastToInteger(_mm_cvtneps_pbh(v.raw))};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /*dbf16*/, Vec128<float> a,
                                   Vec128<float> b) {
#if HWY_COMPILER_CLANG >= 1600 && HWY_COMPILER_CLANG < 2000
  // Inline assembly workaround for LLVM codegen bug
  __m128i raw_result;
  __asm__("vcvtne2ps2bf16 %2, %1, %0"
          : "=v"(raw_result)
          : "v"(b.raw), "v"(a.raw));
  return VFromD<D>{raw_result};
#else
  // The _mm_cvtne2ps_pbh intrinsic returns a __m128bh vector that needs to be
  // bit casted to a __m128i vector
  return VFromD<D>{detail::BitCastToInteger(_mm_cvtne2ps_pbh(b.raw, a.raw))};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec64<float> a,
                                   Vec64<float> b) {
  return VFromD<D>{_mm_shuffle_epi32(
      detail::BitCastToInteger(_mm_cvtne2ps_pbh(b.raw, a.raw)),
      _MM_SHUFFLE(2, 0, 2, 0))};
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_BF16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dbf16, Vec32<float> a, Vec32<float> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dbf16, Combine(dt, b, a));
}
#endif  // HWY_AVX3_HAVE_F32_TO_BF16C

// Specializations for partial vectors because packs_epi32 sets lanes above 2*N.
template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec32<int32_t> a, Vec32<int32_t> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec64<int32_t> a,
                                   Vec64<int32_t> b) {
  return VFromD<D>{_mm_shuffle_epi32(_mm_packs_epi32(a.raw, b.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec128<int32_t> a,
                                   Vec128<int32_t> b) {
  return VFromD<D>{_mm_packs_epi32(a.raw, b.raw)};
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec32<int32_t> a, Vec32<int32_t> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec64<int32_t> a, Vec64<int32_t> b) {
#if HWY_TARGET >= HWY_SSSE3
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
#else
  (void)dn;
  return VFromD<D>{_mm_shuffle_epi32(_mm_packus_epi32(a.raw, b.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
#endif
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<int32_t> a, Vec128<int32_t> b) {
#if HWY_TARGET >= HWY_SSSE3
  const Half<decltype(dn)> dnh;
  const auto u16_a = DemoteTo(dnh, a);
  const auto u16_b = DemoteTo(dnh, b);
  return Combine(dn, u16_b, u16_a);
#else
  (void)dn;
  return VFromD<D>{_mm_packus_epi32(a.raw, b.raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<uint32_t> a,
                                   Vec128<uint32_t> b) {
  const DFromV<decltype(a)> du32;
  const RebindToSigned<decltype(du32)> di32;
  const auto max_i32 = Set(du32, 0x7FFFFFFFu);

#if HWY_TARGET >= HWY_SSSE3
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;
  // On SSE2/SSSE3, clamp a and b using u8 Min operation
  const auto clamped_a = BitCast(
      di32, Min(BitCast(du32_as_du8, a), BitCast(du32_as_du8, max_i32)));
  const auto clamped_b = BitCast(
      di32, Min(BitCast(du32_as_du8, b), BitCast(du32_as_du8, max_i32)));
#else
  const auto clamped_a = BitCast(di32, Min(a, max_i32));
  const auto clamped_b = BitCast(di32, Min(b, max_i32));
#endif

  return ReorderDemote2To(dn, clamped_a, clamped_b);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint32_t, D>> a,
                                   VFromD<Repartition<uint32_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

// Specializations for partial vectors because packs_epi32 sets lanes above 2*N.
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec64<int16_t> a,
                                   Vec64<int16_t> b) {
  return VFromD<D>{_mm_shuffle_epi32(_mm_packs_epi16(a.raw, b.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec128<int16_t> a,
                                   Vec128<int16_t> b) {
  return VFromD<D>{_mm_packs_epi16(a.raw, b.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec64<int16_t> a,
                                   Vec64<int16_t> b) {
  return VFromD<D>{_mm_shuffle_epi32(_mm_packus_epi16(a.raw, b.raw),
                                     _MM_SHUFFLE(2, 0, 2, 0))};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D /* tag */, Vec128<int16_t> a,
                                   Vec128<int16_t> b) {
  return VFromD<D>{_mm_packus_epi16(a.raw, b.raw)};
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<uint16_t> a,
                                   Vec128<uint16_t> b) {
  const DFromV<decltype(a)> du16;
  const RebindToSigned<decltype(du16)> di16;
  const auto max_i16 = Set(du16, 0x7FFFu);

#if HWY_TARGET >= HWY_SSSE3
  const Repartition<uint8_t, decltype(du16)> du16_as_du8;
  // On SSE2/SSSE3, clamp a and b using u8 Min operation
  const auto clamped_a = BitCast(
      di16, Min(BitCast(du16_as_du8, a), BitCast(du16_as_du8, max_i16)));
  const auto clamped_b = BitCast(
      di16, Min(BitCast(du16_as_du8, b), BitCast(du16_as_du8, max_i16)));
#else
  const auto clamped_a = BitCast(di16, Min(a, max_i16));
  const auto clamped_b = BitCast(di16, Min(b, max_i16));
#endif

  return ReorderDemote2To(dn, clamped_a, clamped_b);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint16_t, D>> a,
                                   VFromD<Repartition<uint16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>),
          HWY_IF_V_SIZE_LE_D(D, 16), class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<D>) * 2),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D d, V a, V b) {
  return ReorderDemote2To(d, a, b);
}

#if HWY_AVX3_HAVE_F32_TO_BF16C
// F32 to BF16 OrderedDemote2To is generic for all vector lengths on targets
// that support AVX512BF16
template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> OrderedDemote2To(D dbf16, VFromD<Repartition<float, D>> a,
                                   VFromD<Repartition<float, D>> b) {
  return ReorderDemote2To(dbf16, a, b);
}
#endif  // HWY_AVX3_HAVE_F32_TO_BF16C

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<double, D>> v) {
  return VFromD<D>{_mm_cvtpd_ps(v.raw)};
}

namespace detail {

// Generic for all vector lengths.
template <class D>
HWY_INLINE VFromD<D> ClampF64ToI32Max(D d, VFromD<D> v) {
  // The max can be exactly represented in binary64, so clamping beforehand
  // prevents x86 conversion from raising an exception and returning 80..00.
  return Min(v, Set(d, 2147483647.0));
}

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
template <class TTo, class TF>
static constexpr HWY_INLINE TTo
X86ConvertScalarFromFloat(hwy::FloatTag /* to_type_tag */, TF from_val) {
  return ConvertScalarTo<TTo>(from_val);
}

template <class TTo, class TF>
static HWY_BITCASTSCALAR_CONSTEXPR HWY_INLINE TTo
X86ConvertScalarFromFloat(hwy::SpecialTag /* to_type_tag */, TF from_val) {
  return ConvertScalarTo<TTo>(from_val);
}

template <class TTo, class TF>
static HWY_BITCASTSCALAR_CXX14_CONSTEXPR HWY_INLINE TTo
X86ConvertScalarFromFloat(hwy::SignedTag /* to_type_tag */, TF from_val) {
#if HWY_HAVE_SCALAR_F16_TYPE && HWY_HAVE_SCALAR_F16_OPERATORS
  using TFArith = If<hwy::IsSame<RemoveCvRef<TTo>, hwy::bfloat16_t>(), float,
                     RemoveCvRef<TF>>;
#else
  using TFArith = If<sizeof(TF) <= sizeof(float), float, RemoveCvRef<TF>>;
#endif

  const TFArith from_val_in_arith_type = ConvertScalarTo<TFArith>(from_val);
  constexpr TTo kMinResultVal = LimitsMin<TTo>();
  HWY_BITCASTSCALAR_CONSTEXPR const TFArith kMinOutOfRangePosVal =
      ScalarAbs(ConvertScalarTo<TFArith>(kMinResultVal));

  return (ScalarAbs(from_val_in_arith_type) < kMinOutOfRangePosVal)
             ? ConvertScalarTo<TTo>(from_val_in_arith_type)
             : kMinResultVal;
}

template <class TTo, class TF>
static HWY_CXX14_CONSTEXPR HWY_INLINE TTo
X86ConvertScalarFromFloat(hwy::UnsignedTag /* to_type_tag */, TF from_val) {
#if HWY_HAVE_SCALAR_F16_TYPE && HWY_HAVE_SCALAR_F16_OPERATORS
  using TFArith = If<hwy::IsSame<RemoveCvRef<TTo>, hwy::bfloat16_t>(), float,
                     RemoveCvRef<TF>>;
#else
  using TFArith = If<sizeof(TF) <= sizeof(float), float, RemoveCvRef<TF>>;
#endif

  const TFArith from_val_in_arith_type = ConvertScalarTo<TFArith>(from_val);
  constexpr TTo kTToMsb = static_cast<TTo>(TTo{1} << (sizeof(TTo) * 8 - 1));
  constexpr const TFArith kNegOne = ConvertScalarTo<TFArith>(-1.0);
  constexpr const TFArith kMinOutOfRangePosVal =
      ConvertScalarTo<TFArith>(static_cast<double>(kTToMsb) * 2.0);

  return (from_val_in_arith_type > kNegOne &&
          from_val_in_arith_type < kMinOutOfRangePosVal)
             ? ConvertScalarTo<TTo>(from_val_in_arith_type)
             : LimitsMax<TTo>();
}

template <class TTo, class TF>
static constexpr HWY_INLINE HWY_MAYBE_UNUSED TTo
X86ConvertScalarFromFloat(TF from_val) {
  return X86ConvertScalarFromFloat<TTo>(hwy::TypeTag<RemoveCvRef<TTo>>(),
                                        from_val);
}
#endif  // HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD

}  // namespace detail

#ifdef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#endif

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> DemoteInRangeTo(D /* tag */, VFromD<Rebind<double, D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttpd_epi32 with GCC if any
  // values of v[i] are not within the range of an int32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int32_t>(v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<int32_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<int32_t>(raw_v[1]), int32_t{0},
        int32_t{0});
  }
#endif

  __m128i raw_result;
  __asm__("%vcvttpd2dq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<D>{_mm_cvttpd_epi32(v.raw)};
#endif
}

// F64 to I32 DemoteTo is generic for all vector lengths
template <class D, HWY_IF_I32_D(D)>
HWY_API VFromD<D> DemoteTo(D di32, VFromD<Rebind<double, D>> v) {
  const Rebind<double, decltype(di32)> df64;
  const VFromD<decltype(df64)> clamped = detail::ClampF64ToI32Max(df64, v);
  return DemoteInRangeTo(di32, clamped);
}

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteInRangeTo(D /* tag */, VFromD<Rebind<double, D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttpd_epu32 with GCC if any
  // values of v[i] are not within the range of an uint32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<uint32_t>(v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[1]), uint32_t{0},
        uint32_t{0});
  }
#endif

  __m128i raw_result;
  __asm__("vcvttpd2udq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else
  return VFromD<D>{_mm_cvttpd_epu32(v.raw)};
#endif
}

// F64->U32 DemoteTo is generic for all vector lengths
template <class D, HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<double, D>> v) {
  return DemoteInRangeTo(D(), ZeroIfNegative(v));
}
#else   // HWY_TARGET > HWY_AVX3

// F64 to U32 DemoteInRangeTo is generic for all vector lengths on
// SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteInRangeTo(D du32, VFromD<Rebind<double, D>> v) {
  const RebindToSigned<decltype(du32)> di32;
  const Rebind<double, decltype(du32)> df64;
  const RebindToUnsigned<decltype(df64)> du64;

  const auto k2_31 = Set(df64, 2147483648.0);
  const auto v_is_ge_k2_31 = (v >= k2_31);
  const auto clamped_lo31_f64 = v - IfThenElseZero(v_is_ge_k2_31, k2_31);
  const auto clamped_lo31_u32 =
      BitCast(du32, DemoteInRangeTo(di32, clamped_lo31_f64));
  const auto clamped_u32_msb = ShiftLeft<31>(
      TruncateTo(du32, BitCast(du64, VecFromMask(df64, v_is_ge_k2_31))));
  return Or(clamped_lo31_u32, clamped_u32_msb);
}

// F64 to U32 DemoteTo is generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteTo(D du32, VFromD<Rebind<double, D>> v) {
  const Rebind<double, decltype(du32)> df64;
  const auto clamped = Min(ZeroIfNegative(v), Set(df64, 4294967295.0));
  return DemoteInRangeTo(du32, clamped);
}
#endif  // HWY_TARGET <= HWY_AVX3

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  return VFromD<D>{_mm_cvtepi64_ps(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint64_t, D>> v) {
  return VFromD<D>{_mm_cvtepu64_ps(v.raw)};
}
#else
// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D df32, VFromD<Rebind<int64_t, D>> v) {
  const Rebind<double, decltype(df32)> df64;
  const RebindToUnsigned<decltype(df64)> du64;
  const RebindToSigned<decltype(df32)> di32;
  const RebindToUnsigned<decltype(df32)> du32;

  const auto k2p64_63 = Set(df64, 27670116110564327424.0);
  const auto f64_hi52 =
      Xor(BitCast(df64, ShiftRight<12>(BitCast(du64, v))), k2p64_63) - k2p64_63;
  const auto f64_lo12 =
      PromoteTo(df64, BitCast(di32, And(TruncateTo(du32, BitCast(du64, v)),
                                        Set(du32, uint32_t{0x00000FFF}))));

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
}

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D df32, VFromD<Rebind<uint64_t, D>> v) {
  const Rebind<double, decltype(df32)> df64;
  const RebindToUnsigned<decltype(df64)> du64;
  const RebindToSigned<decltype(df32)> di32;
  const RebindToUnsigned<decltype(df32)> du32;

  const auto k2p64 = Set(df64, 18446744073709551616.0);
  const auto f64_hi52 = Or(BitCast(df64, ShiftRight<12>(v)), k2p64) - k2p64;
  const auto f64_lo12 =
      PromoteTo(df64, BitCast(di32, And(TruncateTo(du32, BitCast(du64, v)),
                                        Set(du32, uint32_t{0x00000FFF}))));

  const auto f64_sum = f64_hi52 + f64_lo12;
  const auto f64_carry = (f64_hi52 - f64_sum) + f64_lo12;
  const auto f64_sum_is_inexact =
      ShiftRight<63>(BitCast(du64, VecFromMask(df64, f64_carry != Zero(df64))));

  const auto adj_f64_val = BitCast(
      df64,
      Or(BitCast(du64, f64_sum) - ShiftRight<63>(BitCast(du64, f64_carry)),
         f64_sum_is_inexact));

  return DemoteTo(df32, adj_f64_val);
}
#endif

// For already range-limited input [0, 255].
template <size_t N>
HWY_API Vec128<uint8_t, N> U8FromU32(const Vec128<uint32_t, N> v) {
#if HWY_TARGET == HWY_SSE2
  const RebindToSigned<DFromV<decltype(v)>> di32;
  const Rebind<uint8_t, decltype(di32)> du8;
  return DemoteTo(du8, BitCast(di32, v));
#else
  const DFromV<decltype(v)> d32;
  const Repartition<uint8_t, decltype(d32)> d8;
  alignas(16) static constexpr uint32_t k8From32[4] = {
      0x0C080400u, 0x0C080400u, 0x0C080400u, 0x0C080400u};
  // Also replicate bytes into all 32 bit lanes for safety.
  const auto quad = TableLookupBytes(v, Load(d32, k8From32));
  return LowerHalf(LowerHalf(BitCast(d8, quad)));
#endif
}

// ------------------------------ F32->UI64 PromoteTo
#ifdef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#endif

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteInRangeTo(D /*di64*/, VFromD<Rebind<float, D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior with GCC if any values of v[i] are not
  // within the range of an int64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int64_t>(v)) {
    typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<int64_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<int64_t>(raw_v[1]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttps2qq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else
  return VFromD<D>{_mm_cvttps_epi64(v.raw)};
#endif
}

// Generic for all vector lengths.
template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D di64, VFromD<Rebind<float, D>> v) {
  const Rebind<float, decltype(di64)> df32;
  const RebindToFloat<decltype(di64)> df64;
  // We now avoid GCC UB in PromoteInRangeTo via assembly, see #2189 and
  // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=115115. Previously we fixed up
  // the result afterwards using three instructions. Now we instead check if
  // v >= 2^63, and if so replace the output with 2^63-1, which is likely more
  // efficient. Note that the previous representable f32 is less than 2^63 and
  // thus fits in i64.
  const MFromD<D> overflow = RebindMask(
      di64, PromoteMaskTo(df64, df32, Ge(v, Set(df32, 9.223372e18f))));
  return IfThenElse(overflow, Set(di64, LimitsMax<int64_t>()),
                    PromoteInRangeTo(di64, v));
}
template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return PromoteInRangeTo(D(), ZeroIfNegative(v));
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteInRangeTo(D /* tag */, VFromD<Rebind<float, D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior with GCC if any values of v[i] are not
  // within the range of an uint64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<uint64_t>(v)) {
    typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<uint64_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<uint64_t>(raw_v[1]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttps2uqq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else
  return VFromD<D>{_mm_cvttps_epu64(v.raw)};
#endif
}
#else   // AVX2 or below

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
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

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_UI64_D(D)>
HWY_API VFromD<D> PromoteInRangeTo(D d64, VFromD<Rebind<float, D>> v) {
  const Rebind<MakeNarrow<TFromD<D>>, decltype(d64)> d32;
  const RebindToSigned<decltype(d32)> di32;
  const RebindToFloat<decltype(d32)> df32;
  const RebindToUnsigned<decltype(d32)> du32;
  const Repartition<uint8_t, decltype(d32)> du32_as_du8;

  const auto exponent_adj = BitCast(
      du32,
      SaturatedSub(BitCast(du32_as_du8, ShiftRight<23>(BitCast(du32, v))),
                   BitCast(du32_as_du8, Set(du32, uint32_t{0xFFFFFF9Du}))));
  const auto adj_v =
      BitCast(df32, BitCast(du32, v) - ShiftLeft<23>(exponent_adj));

  const auto f32_to_i32_result = ConvertInRangeTo(di32, adj_v);
  return PromoteTo(d64, BitCast(d32, f32_to_i32_result))
         << PromoteTo(d64, exponent_adj);
}

namespace detail {

template <class DU64, HWY_IF_V_SIZE_LE_D(DU64, 16)>
HWY_INLINE VFromD<DU64> PromoteF32ToU64OverflowMaskToU64(
    DU64 du64, VFromD<Rebind<int32_t, DU64>> i32_overflow_mask) {
  const Rebind<int32_t, decltype(du64)> di32;
  const Twice<decltype(di32)> dt_i32;

  const auto vt_i32_overflow_mask = ResizeBitCast(dt_i32, i32_overflow_mask);
  return BitCast(du64,
                 InterleaveLower(vt_i32_overflow_mask, vt_i32_overflow_mask));
}

template <class DU64, HWY_IF_V_SIZE_GT_D(DU64, 16)>
HWY_INLINE VFromD<DU64> PromoteF32ToU64OverflowMaskToU64(
    DU64 du64, VFromD<Rebind<int32_t, DU64>> i32_overflow_mask) {
  const RebindToSigned<decltype(du64)> di64;
  return BitCast(du64, PromoteTo(di64, i32_overflow_mask));
}

}  // namespace detail

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D du64, VFromD<Rebind<float, D>> v) {
  const Rebind<int32_t, decltype(du64)> di32;
  const RebindToFloat<decltype(di32)> df32;
  const RebindToUnsigned<decltype(di32)> du32;
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;

  const auto non_neg_v = ZeroIfNegative(v);

  const auto exponent_adj = BitCast(
      du32, Min(SaturatedSub(BitCast(du32_as_du8,
                                     ShiftRight<23>(BitCast(du32, non_neg_v))),
                             BitCast(du32_as_du8, Set(du32, uint32_t{157}))),
                BitCast(du32_as_du8, Set(du32, uint32_t{33}))));

  const auto adj_v =
      BitCast(df32, BitCast(du32, non_neg_v) - ShiftLeft<23>(exponent_adj));
  const auto f32_to_i32_result = ConvertInRangeTo(di32, adj_v);

  const auto i32_overflow_mask = BroadcastSignBit(f32_to_i32_result);
  const auto overflow_result =
      detail::PromoteF32ToU64OverflowMaskToU64(du64, i32_overflow_mask);

  return Or(PromoteTo(du64, BitCast(du32, f32_to_i32_result))
                << PromoteTo(du64, exponent_adj),
            overflow_result);
}
#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ MulFixedPoint15

#if HWY_TARGET == HWY_SSE2
HWY_API Vec128<int16_t> MulFixedPoint15(const Vec128<int16_t> a,
                                        const Vec128<int16_t> b) {
  const DFromV<decltype(a)> d;
  const Repartition<int32_t, decltype(d)> di32;

  auto lo_product = a * b;
  auto hi_product = MulHigh(a, b);

  const VFromD<decltype(di32)> i32_product_lo{
      _mm_unpacklo_epi16(lo_product.raw, hi_product.raw)};
  const VFromD<decltype(di32)> i32_product_hi{
      _mm_unpackhi_epi16(lo_product.raw, hi_product.raw)};

  const auto round_up_incr = Set(di32, 0x4000);
  return ReorderDemote2To(d, ShiftRight<15>(i32_product_lo + round_up_incr),
                          ShiftRight<15>(i32_product_hi + round_up_incr));
}

template <size_t N, HWY_IF_V_SIZE_LE(int16_t, N, 8)>
HWY_API Vec128<int16_t, N> MulFixedPoint15(const Vec128<int16_t, N> a,
                                           const Vec128<int16_t, N> b) {
  const DFromV<decltype(a)> d;
  const Rebind<int32_t, decltype(d)> di32;

  const auto lo_product = a * b;
  const auto hi_product = MulHigh(a, b);
  const VFromD<decltype(di32)> i32_product{
      _mm_unpacklo_epi16(lo_product.raw, hi_product.raw)};

  return DemoteTo(d, ShiftRight<15>(i32_product + Set(di32, 0x4000)));
}
#else
template <size_t N>
HWY_API Vec128<int16_t, N> MulFixedPoint15(const Vec128<int16_t, N> a,
                                           const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{_mm_mulhrs_epi16(a.raw, b.raw)};
}
#endif

// ------------------------------ Truncations

template <typename From, class DTo, HWY_IF_LANES_D(DTo, 1)>
HWY_API VFromD<DTo> TruncateTo(DTo /* tag */, Vec128<From, 1> v) {
  // BitCast requires the same size; DTo might be u8x1 and v u16x1.
  const Repartition<TFromD<DTo>, DFromV<decltype(v)>> dto;
  return VFromD<DTo>{BitCast(dto, v).raw};
}

template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API VFromD<D> TruncateTo(D d, Vec128<uint64_t> v) {
#if HWY_TARGET == HWY_SSE2
  const Vec128<uint8_t, 1> lo{v.raw};
  const Vec128<uint8_t, 1> hi{_mm_unpackhi_epi64(v.raw, v.raw)};
  return Combine(d, hi, lo);
#else
  const Repartition<uint8_t, DFromV<decltype(v)>> d8;
  (void)d;
  alignas(16) static constexpr uint8_t kIdx[16] = {0, 8, 0, 8, 0, 8, 0, 8,
                                                   0, 8, 0, 8, 0, 8, 0, 8};
  const Vec128<uint8_t> v8 = TableLookupBytes(v, Load(d8, kIdx));
  return LowerHalf(LowerHalf(LowerHalf(v8)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> TruncateTo(D d, Vec128<uint64_t> v) {
#if HWY_TARGET == HWY_SSE2
  const Vec128<uint16_t, 1> lo{v.raw};
  const Vec128<uint16_t, 1> hi{_mm_unpackhi_epi64(v.raw, v.raw)};
  return Combine(d, hi, lo);
#else
  (void)d;
  const Repartition<uint16_t, DFromV<decltype(v)>> d16;
  alignas(16) static constexpr uint16_t kIdx[8] = {
      0x100u, 0x908u, 0x100u, 0x908u, 0x100u, 0x908u, 0x100u, 0x908u};
  const Vec128<uint16_t> v16 = TableLookupBytes(v, Load(d16, kIdx));
  return LowerHalf(LowerHalf(v16));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  return VFromD<D>{_mm_shuffle_epi32(v.raw, 0x88)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const DFromV<decltype(v)> du32;
#if HWY_TARGET == HWY_SSE2
  const RebindToSigned<decltype(du32)> di32;
  const Rebind<uint8_t, decltype(di32)> du8;
  return DemoteTo(du8, BitCast(di32, ShiftRight<24>(ShiftLeft<24>(v))));
#else
  const Repartition<uint8_t, decltype(du32)> d;
  alignas(16) static constexpr uint8_t kIdx[16] = {
      0x0u, 0x4u, 0x8u, 0xCu, 0x0u, 0x4u, 0x8u, 0xCu,
      0x0u, 0x4u, 0x8u, 0xCu, 0x0u, 0x4u, 0x8u, 0xCu};
  return LowerHalf(LowerHalf(TableLookupBytes(v, Load(d, kIdx))));
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const DFromV<decltype(v)> du32;
#if HWY_TARGET == HWY_SSE2
  const RebindToSigned<decltype(du32)> di32;
  const Rebind<uint16_t, decltype(di32)> du16;
  const RebindToSigned<decltype(du16)> di16;
  return BitCast(
      du16, DemoteTo(di16, ShiftRight<16>(BitCast(di32, ShiftLeft<16>(v)))));
#else
  const Repartition<uint16_t, decltype(du32)> d;
  return LowerHalf(ConcatEven(d, BitCast(d, v), BitCast(d, v)));
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  const DFromV<decltype(v)> du16;
#if HWY_TARGET == HWY_SSE2
  const RebindToSigned<decltype(du16)> di16;
  const Rebind<uint8_t, decltype(di16)> du8;
  const RebindToSigned<decltype(du8)> di8;
  return BitCast(du8,
                 DemoteTo(di8, ShiftRight<8>(BitCast(di16, ShiftLeft<8>(v)))));
#else
  const Repartition<uint8_t, decltype(du16)> d;
  return LowerHalf(ConcatEven(d, BitCast(d, v), BitCast(d, v)));
#endif
}

// ------------------------------ Demotions to/from i64

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  return VFromD<D>{_mm_cvtsepi64_epi32(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  return VFromD<D>{_mm_cvtsepi64_epi16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  return VFromD<D>{_mm_cvtsepi64_epi8(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  const __mmask8 non_neg_mask = detail::UnmaskedNot(MaskFromVec(v)).raw;
  return VFromD<D>{_mm_maskz_cvtusepi64_epi32(non_neg_mask, v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  const __mmask8 non_neg_mask = detail::UnmaskedNot(MaskFromVec(v)).raw;
  return VFromD<D>{_mm_maskz_cvtusepi64_epi16(non_neg_mask, v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int64_t, D>> v) {
  const __mmask8 non_neg_mask = detail::UnmaskedNot(MaskFromVec(v)).raw;
  return VFromD<D>{_mm_maskz_cvtusepi64_epi8(non_neg_mask, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint64_t, D>> v) {
  return VFromD<D>{_mm_cvtusepi64_epi32(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint64_t, D>> v) {
  return VFromD<D>{_mm_cvtusepi64_epi16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<uint64_t, D>> v) {
  return VFromD<D>{_mm_cvtusepi64_epi8(v.raw)};
}
#else  // AVX2 or below

// Disable the default unsigned to signed DemoteTo/ReorderDemote2To
// implementations in generic_ops-inl.h for U64->I8/I16/I32 demotions on
// SSE2/SSSE3/SSE4/AVX2 as U64->I8/I16/I32 DemoteTo/ReorderDemote2To for
// SSE2/SSSE3/SSE4/AVX2 is implemented in x86_128-inl.h

// The default unsigned to signed DemoteTo/ReorderDemote2To
// implementations in generic_ops-inl.h are still used for U32->I8/I16 and
// U16->I8 demotions on SSE2/SSSE3/SSE4/AVX2

#undef HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V
#define HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V(V) HWY_IF_NOT_T_SIZE_V(V, 8)

namespace detail {
template <class D, HWY_IF_UNSIGNED_D(D)>
HWY_INLINE VFromD<Rebind<uint64_t, D>> DemoteFromU64MaskOutResult(
    D /*dn*/, VFromD<Rebind<uint64_t, D>> v) {
  return v;
}

template <class D, HWY_IF_SIGNED_D(D)>
HWY_INLINE VFromD<Rebind<uint64_t, D>> DemoteFromU64MaskOutResult(
    D /*dn*/, VFromD<Rebind<uint64_t, D>> v) {
  const DFromV<decltype(v)> du64;
  return And(v,
             Set(du64, static_cast<uint64_t>(hwy::HighestValue<TFromD<D>>())));
}

template <class D>
HWY_INLINE VFromD<Rebind<uint64_t, D>> DemoteFromU64Saturate(
    D dn, VFromD<Rebind<uint64_t, D>> v) {
  const Rebind<uint64_t, D> du64;
  const RebindToSigned<decltype(du64)> di64;
  constexpr int kShiftAmt = static_cast<int>(sizeof(TFromD<D>) * 8) -
                            static_cast<int>(hwy::IsSigned<TFromD<D>>());

  const auto too_big = BitCast(
      du64, VecFromMask(
                di64, Gt(BitCast(di64, ShiftRight<kShiftAmt>(v)), Zero(di64))));
  return DemoteFromU64MaskOutResult(dn, Or(v, too_big));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), class V>
HWY_INLINE VFromD<D> ReorderDemote2From64To32Combine(D dn, V a, V b) {
  return ConcatEven(dn, BitCast(dn, b), BitCast(dn, a));
}

}  // namespace detail

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_SIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<int64_t, D>> v) {
  const DFromV<decltype(v)> di64;
  const RebindToUnsigned<decltype(di64)> du64;
  const RebindToUnsigned<decltype(dn)> dn_u;

  // Negative values are saturated by first saturating their bitwise inverse
  // and then inverting the saturation result
  const auto invert_mask = BitCast(du64, BroadcastSignBit(v));
  const auto saturated_vals = Xor(
      invert_mask,
      detail::DemoteFromU64Saturate(dn, Xor(invert_mask, BitCast(du64, v))));
  return BitCast(dn, TruncateTo(dn_u, saturated_vals));
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<int64_t, D>> v) {
  const DFromV<decltype(v)> di64;
  const RebindToUnsigned<decltype(di64)> du64;

  const auto non_neg_vals = BitCast(du64, AndNot(BroadcastSignBit(v), v));
  return TruncateTo(dn, detail::DemoteFromU64Saturate(dn, non_neg_vals));
}

template <class D,
          HWY_IF_T_SIZE_ONE_OF_D(
              D, ((HWY_TARGET != HWY_SSE2) ? ((1 << 1) | (1 << 2)) : 0) |
                     (1 << 4)),
          HWY_IF_SIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<uint64_t, D>> v) {
  const RebindToUnsigned<decltype(dn)> dn_u;
  return BitCast(dn, TruncateTo(dn_u, detail::DemoteFromU64Saturate(dn, v)));
}

#if HWY_TARGET == HWY_SSE2
template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          HWY_IF_SIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<uint64_t, D>> v) {
  const Rebind<int32_t, decltype(dn)> di32;
  return DemoteTo(dn, DemoteTo(di32, v));
}
#endif  // HWY_TARGET == HWY_SSE2

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<uint64_t, D>> v) {
  return TruncateTo(dn, detail::DemoteFromU64Saturate(dn, v));
}
#endif  // HWY_TARGET <= HWY_AVX3

template <class D, HWY_IF_V_SIZE_LE_D(D, HWY_MAX_BYTES / 2),
          HWY_IF_T_SIZE_D(D, 4), HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int64_t, D>> a,
                                   VFromD<Repartition<int64_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, HWY_MAX_BYTES / 2), HWY_IF_U32_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint64_t, D>> a,
                                   VFromD<Repartition<uint64_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

#if HWY_TARGET > HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, HWY_MAX_BYTES / 2), HWY_IF_I32_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint64_t, D>> a,
                                   VFromD<Repartition<uint64_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
#endif

#if HWY_TARGET > HWY_AVX2
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API Vec128<int32_t> ReorderDemote2To(D dn, Vec128<int64_t> a,
                                         Vec128<int64_t> b) {
  const DFromV<decltype(a)> di64;
  const RebindToUnsigned<decltype(di64)> du64;
  const Half<decltype(dn)> dnh;

  // Negative values are saturated by first saturating their bitwise inverse
  // and then inverting the saturation result
  const auto invert_mask_a = BitCast(du64, BroadcastSignBit(a));
  const auto invert_mask_b = BitCast(du64, BroadcastSignBit(b));
  const auto saturated_a = Xor(
      invert_mask_a,
      detail::DemoteFromU64Saturate(dnh, Xor(invert_mask_a, BitCast(du64, a))));
  const auto saturated_b = Xor(
      invert_mask_b,
      detail::DemoteFromU64Saturate(dnh, Xor(invert_mask_b, BitCast(du64, b))));

  return ConcatEven(dn, BitCast(dn, saturated_b), BitCast(dn, saturated_a));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> ReorderDemote2To(D dn, Vec128<int64_t> a,
                                          Vec128<int64_t> b) {
  const DFromV<decltype(a)> di64;
  const RebindToUnsigned<decltype(di64)> du64;
  const Half<decltype(dn)> dnh;

  const auto saturated_a = detail::DemoteFromU64Saturate(
      dnh, BitCast(du64, AndNot(BroadcastSignBit(a), a)));
  const auto saturated_b = detail::DemoteFromU64Saturate(
      dnh, BitCast(du64, AndNot(BroadcastSignBit(b), b)));

  return ConcatEven(dn, BitCast(dn, saturated_b), BitCast(dn, saturated_a));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<uint64_t> a,
                                   Vec128<uint64_t> b) {
  const Half<decltype(dn)> dnh;

  const auto saturated_a = detail::DemoteFromU64Saturate(dnh, a);
  const auto saturated_b = detail::DemoteFromU64Saturate(dnh, b);

  return ConcatEven(dn, BitCast(dn, saturated_b), BitCast(dn, saturated_a));
}
#endif  // HWY_TARGET > HWY_AVX2

// ------------------------------ Integer <=> fp (ShiftRight, OddEven)

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>{_mm_cvtepu16_ph(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F16_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{_mm_cvtepi16_ph(v.raw)};
}
#endif  // HWY_HAVE_FLOAT16

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{_mm_cvtepi32_ps(v.raw)};
}

#if HWY_TARGET <= HWY_AVX3
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /*df*/, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{_mm_cvtepu32_ps(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> ConvertTo(D /*dd*/, VFromD<Rebind<int64_t, D>> v) {
  return VFromD<D>{_mm_cvtepi64_pd(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> ConvertTo(D /*dd*/, VFromD<Rebind<uint64_t, D>> v) {
  return VFromD<D>{_mm_cvtepu64_pd(v.raw)};
}
#else   // AVX2 or below
// Generic for all vector lengths.
template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D df, VFromD<Rebind<uint32_t, D>> v) {
  // Based on wim's approach (https://stackoverflow.com/questions/34066228/)
  const RebindToUnsigned<decltype(df)> du32;
  const RebindToSigned<decltype(df)> d32;

  const auto msk_lo = Set(du32, 0xFFFF);
  const auto cnst2_16_flt = Set(df, 65536.0f);  // 2^16

  // Extract the 16 lowest/highest significant bits of v and cast to signed int
  const auto v_lo = BitCast(d32, And(v, msk_lo));
  const auto v_hi = BitCast(d32, ShiftRight<16>(v));
  return MulAdd(cnst2_16_flt, ConvertTo(df, v_hi), ConvertTo(df, v_lo));
}

// Generic for all vector lengths.
template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> ConvertTo(D dd, VFromD<Rebind<int64_t, D>> v) {
  // Based on wim's approach (https://stackoverflow.com/questions/41144668/)
  const Repartition<uint32_t, decltype(dd)> d32;
  const Repartition<uint64_t, decltype(dd)> d64;

  // Toggle MSB of lower 32-bits and insert exponent for 2^84 + 2^63
  const auto k84_63 = Set(d64, 0x4530000080000000ULL);
  const auto v_upper = BitCast(dd, ShiftRight<32>(BitCast(d64, v)) ^ k84_63);

  // Exponent is 2^52, lower 32 bits from v (=> 32-bit OddEven)
  const auto k52 = Set(d32, 0x43300000);
  const auto v_lower = BitCast(dd, OddEven(k52, BitCast(d32, v)));

  const auto k84_63_52 = BitCast(dd, Set(d64, 0x4530000080100000ULL));
  return (v_upper - k84_63_52) + v_lower;  // order matters!
}

namespace detail {
template <class VW>
HWY_INLINE VFromD<Rebind<double, DFromV<VW>>> U64ToF64VecFast(VW w) {
  const DFromV<decltype(w)> d64;
  const RebindToFloat<decltype(d64)> dd;
  const auto cnst2_52_dbl = Set(dd, 0x0010000000000000);  // 2^52
  return BitCast(dd, Or(w, BitCast(d64, cnst2_52_dbl))) - cnst2_52_dbl;
}
}  // namespace detail

// Generic for all vector lengths.
template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> ConvertTo(D dd, VFromD<Rebind<uint64_t, D>> v) {
  // Based on wim's approach (https://stackoverflow.com/questions/41144668/)
  const RebindToUnsigned<decltype(dd)> d64;
  using VU = VFromD<decltype(d64)>;

  const VU msk_lo = Set(d64, 0xFFFFFFFF);
  const auto cnst2_32_dbl = Set(dd, 4294967296.0);  // 2^32

  // Extract the 32 lowest/highest significant bits of v
  const VU v_lo = And(v, msk_lo);
  const VU v_hi = ShiftRight<32>(v);

  const auto v_lo_dbl = detail::U64ToF64VecFast(v_lo);
  return MulAdd(cnst2_32_dbl, detail::U64ToF64VecFast(v_hi), v_lo_dbl);
}
#endif  // HWY_TARGET <= HWY_AVX3

// Truncates (rounds toward zero).

#ifdef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#undef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#else
#define HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#endif

#if HWY_HAVE_FLOAT16
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> ConvertInRangeTo(D /*di*/, VFromD<RebindToFloat<D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttph_epi16 if any values of v[i]
  // are not within the range of an int16_t

#if HWY_COMPILER_GCC_ACTUAL >= 1200 && !HWY_IS_DEBUG_BUILD && \
    HWY_HAVE_SCALAR_F16_TYPE
  if (detail::IsConstantX86VecForF2IConv<int16_t>(v)) {
    typedef hwy::float16_t::Native GccF16RawVectType
        __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF16RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<int16_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[1]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[2]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[3]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[4]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[5]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[6]),
        detail::X86ConvertScalarFromFloat<int16_t>(raw_v[7]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttph2w {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<D>{_mm_cvttph_epi16(v.raw)};
#endif
}

// F16 to I16 ConvertTo is generic for all vector lengths
template <class D, HWY_IF_I16_D(D)>
HWY_API VFromD<D> ConvertTo(D di, VFromD<RebindToFloat<D>> v) {
  const RebindToFloat<decltype(di)> df;
  // See comment at the first occurrence of "IfThenElse(overflow,".
  const MFromD<D> overflow =
      RebindMask(di, Ge(v, Set(df, ConvertScalarTo<hwy::float16_t>(32768.0f))));
  return IfThenElse(overflow, Set(di, LimitsMax<int16_t>()),
                    ConvertInRangeTo(di, v));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ConvertInRangeTo(D /* tag */, VFromD<RebindToFloat<D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttph_epu16 if any values of v[i]
  // are not within the range of an uint16_t

#if HWY_COMPILER_GCC_ACTUAL >= 1200 && !HWY_IS_DEBUG_BUILD && \
    HWY_HAVE_SCALAR_F16_TYPE
  if (detail::IsConstantX86VecForF2IConv<uint16_t>(v)) {
    typedef hwy::float16_t::Native GccF16RawVectType
        __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF16RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[1]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[2]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[3]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[4]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[5]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[6]),
        detail::X86ConvertScalarFromFloat<uint16_t>(raw_v[7]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttph2uw {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<D>{_mm_cvttph_epu16(v.raw)};
#endif
}

// F16->U16 ConvertTo is generic for all vector lengths
template <class D, HWY_IF_U16_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<RebindToFloat<D>> v) {
  return ConvertInRangeTo(D(), ZeroIfNegative(v));
}
#endif  // HWY_HAVE_FLOAT16

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> ConvertInRangeTo(D /*di*/, VFromD<RebindToFloat<D>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttps_epi32 with GCC if any
  // values of v[i] are not within the range of an int32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int32_t>(v)) {
    typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(
        D(), detail::X86ConvertScalarFromFloat<int32_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<int32_t>(raw_v[1]),
        detail::X86ConvertScalarFromFloat<int32_t>(raw_v[2]),
        detail::X86ConvertScalarFromFloat<int32_t>(raw_v[3]));
  }
#endif

  __m128i raw_result;
  __asm__("%vcvttps2dq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<D>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<D>{_mm_cvttps_epi32(v.raw)};
#endif
}

// F32 to I32 ConvertTo is generic for all vector lengths
template <class D, HWY_IF_I32_D(D)>
HWY_API VFromD<D> ConvertTo(D di, VFromD<RebindToFloat<D>> v) {
  const RebindToFloat<decltype(di)> df;
  // See comment at the first occurrence of "IfThenElse(overflow,".
  const MFromD<D> overflow = RebindMask(di, Ge(v, Set(df, 2147483648.0f)));
  return IfThenElse(overflow, Set(di, LimitsMax<int32_t>()),
                    ConvertInRangeTo(di, v));
}

#if HWY_TARGET <= HWY_AVX3
template <class DI, HWY_IF_V_SIZE_LE_D(DI, 16), HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertInRangeTo(DI /*di*/, VFromD<RebindToFloat<DI>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttpd_epi64 with GCC if any
  // values of v[i] are not within the range of an int64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int64_t>(v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return Dup128VecFromValues(
        DI(), detail::X86ConvertScalarFromFloat<int64_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<int64_t>(raw_v[1]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttpd2qq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DI>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DI>{_mm_cvttpd_epi64(v.raw)};
#endif
}

// F64 to I64 ConvertTo is generic for all vector lengths on AVX3
template <class DI, HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertTo(DI di, VFromD<RebindToFloat<DI>> v) {
  const RebindToFloat<decltype(di)> df;
  // See comment at the first occurrence of "IfThenElse(overflow,".
  const MFromD<DI> overflow =
      RebindMask(di, Ge(v, Set(df, 9.223372036854776e18)));
  return IfThenElse(overflow, Set(di, LimitsMax<int64_t>()),
                    ConvertInRangeTo(di, v));
}

template <class DU, HWY_IF_V_SIZE_LE_D(DU, 16), HWY_IF_U32_D(DU)>
HWY_API VFromD<DU> ConvertInRangeTo(DU /*du*/, VFromD<RebindToFloat<DU>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttps_epu32 with GCC if any
  // values of v[i] are not within the range of an uint32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<uint32_t>(v)) {
    typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(
        DU(), detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[1]),
        detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[2]),
        detail::X86ConvertScalarFromFloat<uint32_t>(raw_v[3]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttps2udq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DU>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DU>{_mm_cvttps_epu32(v.raw)};
#endif
}

// F32->U32 ConvertTo is generic for all vector lengths
template <class DU, HWY_IF_U32_D(DU)>
HWY_API VFromD<DU> ConvertTo(DU /*du*/, VFromD<RebindToFloat<DU>> v) {
  return ConvertInRangeTo(DU(), ZeroIfNegative(v));
}

template <class DU, HWY_IF_V_SIZE_LE_D(DU, 16), HWY_IF_U64_D(DU)>
HWY_API VFromD<DU> ConvertInRangeTo(DU /*du*/, VFromD<RebindToFloat<DU>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttpd_epu64 with GCC if any
  // values of v[i] are not within the range of an uint64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<uint64_t>(v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return Dup128VecFromValues(
        DU(), detail::X86ConvertScalarFromFloat<uint64_t>(raw_v[0]),
        detail::X86ConvertScalarFromFloat<uint64_t>(raw_v[1]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvttpd2uqq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DU>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DU>{_mm_cvttpd_epu64(v.raw)};
#endif
}

// F64->U64 ConvertTo is generic for all vector lengths
template <class DU, HWY_IF_U64_D(DU)>
HWY_API VFromD<DU> ConvertTo(DU /*du*/, VFromD<RebindToFloat<DU>> v) {
  return ConvertInRangeTo(DU(), ZeroIfNegative(v));
}

#else  // AVX2 or below

namespace detail {

template <class DU32, HWY_IF_U32_D(DU32)>
static HWY_INLINE VFromD<DU32> ConvInRangeF32ToU32(
    DU32 du32, VFromD<RebindToFloat<DU32>> v, VFromD<DU32>& exp_diff) {
  const RebindToSigned<decltype(du32)> di32;
  const RebindToFloat<decltype(du32)> df32;

  exp_diff = Set(du32, uint32_t{158}) - ShiftRight<23>(BitCast(du32, v));
  const auto scale_down_f32_val_mask =
      VecFromMask(du32, Eq(exp_diff, Zero(du32)));

  const auto v_scaled =
      BitCast(df32, BitCast(du32, v) + ShiftLeft<23>(scale_down_f32_val_mask));
  const auto f32_to_u32_result =
      BitCast(du32, ConvertInRangeTo(di32, v_scaled));

  return f32_to_u32_result + And(f32_to_u32_result, scale_down_f32_val_mask);
}

}  // namespace detail

// F32 to U32 ConvertInRangeTo is generic for all vector lengths on
// SSE2/SSSE3/SSE4/AVX2
template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API VFromD<DU32> ConvertInRangeTo(DU32 du32,
                                      VFromD<RebindToFloat<DU32>> v) {
  VFromD<DU32> exp_diff;
  const auto f32_to_u32_result = detail::ConvInRangeF32ToU32(du32, v, exp_diff);
  return f32_to_u32_result;
}

// F32 to U32 ConvertTo is generic for all vector lengths on
// SSE2/SSSE3/SSE4/AVX2
template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API VFromD<DU32> ConvertTo(DU32 du32, VFromD<RebindToFloat<DU32>> v) {
  const RebindToSigned<decltype(du32)> di32;

  const auto non_neg_v = ZeroIfNegative(v);
  VFromD<DU32> exp_diff;
  const auto f32_to_u32_result =
      detail::ConvInRangeF32ToU32(du32, non_neg_v, exp_diff);

  return Or(f32_to_u32_result,
            BitCast(du32, BroadcastSignBit(BitCast(di32, exp_diff))));
}

namespace detail {

template <class D64, HWY_IF_UI64_D(D64)>
HWY_API VFromD<D64> ConvAbsInRangeF64ToUI64(D64 d64,
                                            VFromD<Rebind<double, D64>> v,
                                            VFromD<D64>& biased_exp) {
  const RebindToSigned<decltype(d64)> di64;
  const RebindToUnsigned<decltype(d64)> du64;
  using VU64 = VFromD<decltype(du64)>;
  const Repartition<uint16_t, decltype(di64)> du16;
  const VU64 k1075 = Set(du64, 1075); /* biased exponent of 2^52 */

  // Exponent indicates whether the number can be represented as int64_t.
  biased_exp = BitCast(d64, ShiftRight<52>(BitCast(du64, v)));
  HWY_IF_CONSTEXPR(IsSigned<TFromD<D64>>()) {
    biased_exp = And(biased_exp, Set(d64, TFromD<D64>{0x7FF}));
  }

  // If we were to cap the exponent at 51 and add 2^52, the number would be in
  // [2^52, 2^53) and mantissa bits could be read out directly. We need to
  // round-to-0 (truncate), but changing rounding mode in MXCSR hits a
  // compiler reordering bug: https://gcc.godbolt.org/z/4hKj6c6qc . We instead
  // manually shift the mantissa into place (we already have many of the
  // inputs anyway).

  // Use 16-bit saturated unsigned subtraction to compute shift_mnt and
  // shift_int since biased_exp[i] is a non-negative integer that is less than
  // or equal to 2047.

  // 16-bit saturated unsigned subtraction is also more efficient than a
  // 64-bit subtraction followed by a 64-bit signed Max operation on
  // SSE2/SSSE3/SSE4/AVX2.

  // The upper 48 bits of both shift_mnt and shift_int are guaranteed to be
  // zero as the upper 48 bits of both k1075 and biased_exp are zero.

  const VU64 shift_mnt = BitCast(
      du64, SaturatedSub(BitCast(du16, k1075), BitCast(du16, biased_exp)));
  const VU64 shift_int = BitCast(
      du64, SaturatedSub(BitCast(du16, biased_exp), BitCast(du16, k1075)));
  const VU64 mantissa = BitCast(du64, v) & Set(du64, (1ULL << 52) - 1);
  // Include implicit 1-bit. NOTE: the shift count may exceed 63; we rely on x86
  // returning zero in that case.
  const VU64 int53 = (mantissa | Set(du64, 1ULL << 52)) >> shift_mnt;

  // For inputs larger than 2^53 - 1, insert zeros at the bottom.

  // For inputs less than 2^64, the implicit 1-bit is guaranteed not to be
  // shifted out of the left shift result below as shift_int[i] <= 11 is true
  // for any inputs that are less than 2^64.

  return BitCast(d64, int53 << shift_int);
}

}  // namespace detail

#if HWY_ARCH_X86_64

namespace detail {

template <size_t N>
static HWY_INLINE int64_t SSE2ConvFirstF64LaneToI64(Vec128<double, N> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvttsd_si64 with GCC if v[0] is
  // not within the range of an int64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (IsConstantX86Vec(hwy::SizeTag<1>(), v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return X86ConvertScalarFromFloat<int64_t>(raw_v[0]);
  }
#endif

  int64_t result;
  __asm__("%vcvttsd2si {%1, %0|%0, %1}"
          : "=r"(result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return result;
#else
  return _mm_cvttsd_si64(v.raw);
#endif
}

}  // namespace detail

template <class DI, HWY_IF_V_SIZE_D(DI, 8), HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertInRangeTo(DI /*di*/, Vec64<double> v) {
  return VFromD<DI>{_mm_cvtsi64_si128(detail::SSE2ConvFirstF64LaneToI64(v))};
}
template <class DI, HWY_IF_V_SIZE_D(DI, 16), HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertInRangeTo(DI /*di*/, Vec128<double> v) {
  const __m128i i0 = _mm_cvtsi64_si128(detail::SSE2ConvFirstF64LaneToI64(v));
  const Full64<double> dd2;
  const __m128i i1 =
      _mm_cvtsi64_si128(detail::SSE2ConvFirstF64LaneToI64(UpperHalf(dd2, v)));
  return VFromD<DI>{_mm_unpacklo_epi64(i0, i1)};
}

template <class DI, HWY_IF_V_SIZE_LE_D(DI, 16), HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertTo(DI di, VFromD<Rebind<double, DI>> v) {
  const RebindToFloat<decltype(di)> df;
  // See comment at the first occurrence of "IfThenElse(overflow,".
  const MFromD<DI> overflow =
      RebindMask(di, Ge(v, Set(df, 9.223372036854776e18)));
  return IfThenElse(overflow, Set(di, LimitsMax<int64_t>()),
                    ConvertInRangeTo(di, v));
}
#endif  // HWY_ARCH_X86_64

#if !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2
template <class DI, HWY_IF_V_SIZE_GT_D(DI, (HWY_ARCH_X86_64 ? 16 : 0)),
          HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertInRangeTo(DI di, VFromD<Rebind<double, DI>> v) {
  using VI = VFromD<DI>;

  VI biased_exp;
  const VI shifted = detail::ConvAbsInRangeF64ToUI64(di, v, biased_exp);
  const VI sign_mask = BroadcastSignBit(BitCast(di, v));

  // If the input was negative, negate the integer (two's complement).
  return (shifted ^ sign_mask) - sign_mask;
}

template <class DI, HWY_IF_V_SIZE_GT_D(DI, (HWY_ARCH_X86_64 ? 16 : 0)),
          HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertTo(DI di, VFromD<Rebind<double, DI>> v) {
  using VI = VFromD<DI>;

  VI biased_exp;
  const VI shifted = detail::ConvAbsInRangeF64ToUI64(di, v, biased_exp);

#if HWY_TARGET <= HWY_SSE4
  const auto in_range = biased_exp < Set(di, 1086);
#else
  const Repartition<int32_t, decltype(di)> di32;
  const auto in_range = MaskFromVec(BitCast(
      di,
      VecFromMask(di32, DupEven(BitCast(di32, biased_exp)) < Set(di32, 1086))));
#endif

  // Saturate to LimitsMin (unchanged when negating below) or LimitsMax.
  const VI sign_mask = BroadcastSignBit(BitCast(di, v));
  const VI limit = Set(di, LimitsMax<int64_t>()) - sign_mask;
  const VI magnitude = IfThenElse(in_range, shifted, limit);

  // If the input was negative, negate the integer (two's complement).
  return (magnitude ^ sign_mask) - sign_mask;
}
#endif  // !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class DU, HWY_IF_U64_D(DU)>
HWY_API VFromD<DU> ConvertInRangeTo(DU du, VFromD<Rebind<double, DU>> v) {
  VFromD<DU> biased_exp;
  const auto shifted = detail::ConvAbsInRangeF64ToUI64(du, v, biased_exp);
  return shifted;
}

// Generic for all vector lengths on SSE2/SSSE3/SSE4/AVX2
template <class DU, HWY_IF_U64_D(DU)>
HWY_API VFromD<DU> ConvertTo(DU du, VFromD<Rebind<double, DU>> v) {
  const RebindToSigned<DU> di;
  using VU = VFromD<DU>;

  VU biased_exp;
  const VU shifted =
      detail::ConvAbsInRangeF64ToUI64(du, ZeroIfNegative(v), biased_exp);

  // Exponent indicates whether the number can be represented as uint64_t.
#if HWY_TARGET <= HWY_SSE4
  const VU out_of_range =
      BitCast(du, VecFromMask(di, BitCast(di, biased_exp) > Set(di, 1086)));
#else
  const Repartition<int32_t, decltype(di)> di32;
  const VU out_of_range = BitCast(
      du,
      VecFromMask(di32, DupEven(BitCast(di32, biased_exp)) > Set(di32, 1086)));
#endif

  return (shifted | out_of_range);
}
#endif  // HWY_TARGET <= HWY_AVX3

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
namespace detail {

template <class TTo, class TF, HWY_IF_SIGNED(TTo)>
static HWY_INLINE HWY_MAYBE_UNUSED HWY_BITCASTSCALAR_CXX14_CONSTEXPR TTo
X86ScalarNearestInt(TF flt_val) {
#if HWY_HAVE_SCALAR_F16_TYPE && HWY_HAVE_SCALAR_F16_OPERATORS
  using TFArith = If<hwy::IsSame<RemoveCvRef<TTo>, hwy::bfloat16_t>(), float,
                     RemoveCvRef<TF>>;
#else
  using TFArith = If<sizeof(TF) <= sizeof(float), float, RemoveCvRef<TF>>;
#endif

  const TTo trunc_int_val = X86ConvertScalarFromFloat<TTo>(flt_val);
  const TFArith abs_val_diff = ScalarAbs(
      ConvertScalarTo<TFArith>(ConvertScalarTo<TFArith>(flt_val) -
                               ConvertScalarTo<TFArith>(trunc_int_val)));
  constexpr TFArith kHalf = ConvertScalarTo<TFArith>(0.5);

  const bool round_result_up =
      ((trunc_int_val ^ ScalarShr(trunc_int_val, sizeof(TTo) * 8 - 1)) !=
       LimitsMax<TTo>()) &&
      (abs_val_diff > kHalf ||
       (abs_val_diff == kHalf && (trunc_int_val & 1) != 0));
  return static_cast<TTo>(
      trunc_int_val +
      (round_result_up ? (ScalarSignBit(flt_val) ? (-1) : 1) : 0));
}

}  // namespace detail
#endif  // HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD

// If these are in namespace detail, the x86_256/512 templates are not found.
template <class DI, HWY_IF_V_SIZE_LE_D(DI, 16), HWY_IF_I32_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI,
                                               VFromD<RebindToFloat<DI>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvtps_epi32 with GCC if any values
  // of v[i] are not within the range of an int32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int32_t>(v)) {
    typedef float GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(DI(),
                               detail::X86ScalarNearestInt<int32_t>(raw_v[0]),
                               detail::X86ScalarNearestInt<int32_t>(raw_v[1]),
                               detail::X86ScalarNearestInt<int32_t>(raw_v[2]),
                               detail::X86ScalarNearestInt<int32_t>(raw_v[3]));
  }
#endif

  __m128i raw_result;
  __asm__("%vcvtps2dq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DI>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DI>{_mm_cvtps_epi32(v.raw)};
#endif
}

#if HWY_HAVE_FLOAT16
template <class DI, HWY_IF_V_SIZE_LE_D(DI, 16), HWY_IF_I16_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI /*di*/,
                                               VFromD<RebindToFloat<DI>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvtph_epi16 if any values of v[i]
  // are not within the range of an int16_t

#if HWY_COMPILER_GCC_ACTUAL >= 1200 && !HWY_IS_DEBUG_BUILD && \
    HWY_HAVE_SCALAR_F16_TYPE
  if (detail::IsConstantX86VecForF2IConv<int16_t>(v)) {
    typedef hwy::float16_t::Native GccF16RawVectType
        __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF16RawVectType>(v.raw);
    return Dup128VecFromValues(DI(),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[0]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[1]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[2]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[3]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[4]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[5]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[6]),
                               detail::X86ScalarNearestInt<int16_t>(raw_v[7]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvtph2w {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DI>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DI>{_mm_cvtph_epi16(v.raw)};
#endif
}
#endif  // HWY_HAVE_FLOAT16

#if HWY_TARGET <= HWY_AVX3

template <class DI, HWY_IF_V_SIZE_LE_D(DI, 16), HWY_IF_I64_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI /*di*/,
                                               VFromD<RebindToFloat<DI>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvtpd_epi64 with GCC if any
  // values of v[i] are not within the range of an int64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int64_t>(v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return Dup128VecFromValues(DI(),
                               detail::X86ScalarNearestInt<int64_t>(raw_v[0]),
                               detail::X86ScalarNearestInt<int64_t>(raw_v[1]));
  }
#endif

  __m128i raw_result;
  __asm__("vcvtpd2qq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DI>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DI>{_mm_cvtpd_epi64(v.raw)};
#endif
}

#else  // HWY_TARGET > HWY_AVX3

namespace detail {

#if HWY_ARCH_X86_64
template <size_t N>
static HWY_INLINE int64_t
SSE2ConvFirstF64LaneToNearestI64(Vec128<double, N> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvtsd_si64 with GCC if v[0] is
  // not within the range of an int64_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (IsConstantX86Vec(hwy::SizeTag<1>(), v)) {
    typedef double GccF64RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF64RawVectType>(v.raw);
    return X86ScalarNearestInt<int64_t>(raw_v[0]);
  }
#endif

  int64_t result;
  __asm__("%vcvtsd2si {%1, %0|%0, %1}"
          : "=r"(result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return result;
#else
  return _mm_cvtsd_si64(v.raw);
#endif
}
#endif  // HWY_ARCH_X86_64

#if !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2
template <class DI64, HWY_IF_I64_D(DI64)>
static HWY_INLINE VFromD<DI64> SSE2NearestI64InRange(
    DI64 di64, VFromD<RebindToFloat<DI64>> v) {
  const RebindToFloat<DI64> df64;
  const RebindToUnsigned<DI64> du64;
  using VI64 = VFromD<decltype(di64)>;

  const auto mant_end = Set(df64, MantissaEnd<double>());
  const auto is_small = Lt(Abs(v), mant_end);

  const auto adj_v = Max(v, Set(df64, -9223372036854775808.0)) +
                     IfThenElseZero(is_small, CopySignToAbs(mant_end, v));
  const auto adj_v_biased_exp =
      And(BitCast(di64, ShiftRight<52>(BitCast(du64, adj_v))),
          Set(di64, int64_t{0x7FF}));

  // We can simply subtract 1075 from adj_v_biased_exp[i] to get shift_int since
  // adj_v_biased_exp[i] is at least 1075
  const VI64 shift_int = adj_v_biased_exp + Set(di64, int64_t{-1075});

  const VI64 mantissa = BitCast(di64, adj_v) & Set(di64, (1LL << 52) - 1);
  // Include implicit 1-bit if is_small[i] is 0. NOTE: the shift count may
  // exceed 63; we rely on x86 returning zero in that case.
  const VI64 int53 = mantissa | IfThenZeroElse(RebindMask(di64, is_small),
                                               Set(di64, 1LL << 52));

  const VI64 sign_mask = BroadcastSignBit(BitCast(di64, v));
  // If the input was negative, negate the integer (two's complement).
  return ((int53 << shift_int) ^ sign_mask) - sign_mask;
}
#endif  // !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2

}  // namespace detail

#if HWY_ARCH_X86_64
template <class DI, HWY_IF_V_SIZE_D(DI, 8), HWY_IF_I64_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI /*di*/, Vec64<double> v) {
  return VFromD<DI>{
      _mm_cvtsi64_si128(detail::SSE2ConvFirstF64LaneToNearestI64(v))};
}
template <class DI, HWY_IF_V_SIZE_D(DI, 16), HWY_IF_I64_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI /*di*/, Vec128<double> v) {
  const __m128i i0 =
      _mm_cvtsi64_si128(detail::SSE2ConvFirstF64LaneToNearestI64(v));
  const Full64<double> dd2;
  const __m128i i1 = _mm_cvtsi64_si128(
      detail::SSE2ConvFirstF64LaneToNearestI64(UpperHalf(dd2, v)));
  return VFromD<DI>{_mm_unpacklo_epi64(i0, i1)};
}
#endif  // HWY_ARCH_X86_64

#if !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2
template <class DI, HWY_IF_V_SIZE_GT_D(DI, (HWY_ARCH_X86_64 ? 16 : 0)),
          HWY_IF_I64_D(DI)>
static HWY_INLINE VFromD<DI> NearestIntInRange(DI di,
                                               VFromD<RebindToFloat<DI>> v) {
  return detail::SSE2NearestI64InRange(di, v);
}
#endif  //  !HWY_ARCH_X86_64 || HWY_TARGET <= HWY_AVX2

#endif  // HWY_TARGET <= HWY_AVX3

template <class DI, HWY_IF_V_SIZE_LE_D(DI, 8), HWY_IF_I32_D(DI)>
static HWY_INLINE VFromD<DI> DemoteToNearestIntInRange(
    DI, VFromD<Rebind<double, DI>> v) {
#if HWY_COMPILER_GCC_ACTUAL
  // Workaround for undefined behavior in _mm_cvtpd_epi32 with GCC if any values
  // of v[i] are not within the range of an int32_t

#if HWY_COMPILER_GCC_ACTUAL >= 700 && !HWY_IS_DEBUG_BUILD
  if (detail::IsConstantX86VecForF2IConv<int32_t>(v)) {
    typedef double GccF32RawVectType __attribute__((__vector_size__(16)));
    const auto raw_v = reinterpret_cast<GccF32RawVectType>(v.raw);
    return Dup128VecFromValues(
        DI(), detail::X86ScalarNearestInt<int32_t>(raw_v[0]),
        detail::X86ScalarNearestInt<int32_t>(raw_v[1]), int32_t{0}, int32_t{0});
  }
#endif

  __m128i raw_result;
  __asm__("%vcvtpd2dq {%1, %0|%0, %1}"
          : "=" HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(raw_result)
          : HWY_X86_GCC_INLINE_ASM_VEC_CONSTRAINT(v.raw)
          :);
  return VFromD<DI>{raw_result};
#else  // !HWY_COMPILER_GCC_ACTUAL
  return VFromD<DI>{_mm_cvtpd_epi32(v.raw)};
#endif
}

// F16/F32/F64 NearestInt is generic for all vector lengths
template <class VF, class DF = DFromV<VF>, class DI = RebindToSigned<DF>,
          HWY_IF_FLOAT_D(DF),
          HWY_IF_T_SIZE_ONE_OF_D(DF, (1 << 4) | (1 << 8) |
                                         (HWY_HAVE_FLOAT16 ? (1 << 2) : 0))>
HWY_API VFromD<DI> NearestInt(const VF v) {
  const DI di;
  using TI = TFromD<DI>;
  using TF = TFromD<DF>;
  using TFArith = If<sizeof(TF) <= sizeof(float), float, RemoveCvRef<TF>>;

  constexpr TFArith kMinOutOfRangePosVal =
      static_cast<TFArith>(-static_cast<TFArith>(LimitsMin<TI>()));
  static_assert(kMinOutOfRangePosVal > static_cast<TFArith>(0.0),
                "kMinOutOfRangePosVal > 0.0 must be true");

  // See comment at the first occurrence of "IfThenElse(overflow,".
  // Here we are rounding, whereas previous occurrences truncate, but there is
  // no difference because the previous float value is well below the max i32.
  const auto overflow = RebindMask(
      di, Ge(v, Set(DF(), ConvertScalarTo<TF>(kMinOutOfRangePosVal))));
  auto result =
      IfThenElse(overflow, Set(di, LimitsMax<TI>()), NearestIntInRange(di, v));

  return result;
}

template <class DI, HWY_IF_I32_D(DI)>
HWY_API VFromD<DI> DemoteToNearestInt(DI, VFromD<Rebind<double, DI>> v) {
  const DI di;
  const Rebind<double, DI> df64;
  return DemoteToNearestIntInRange(di, Min(v, Set(df64, 2147483647.0)));
}

// ------------------------------ Floating-point rounding (ConvertTo)

#if HWY_TARGET >= HWY_SSSE3

// Toward nearest integer, ties to even
template <typename T, size_t N>
HWY_API Vec128<T, N> Round(const Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  // Rely on rounding after addition with a large value such that no mantissa
  // bits remain (assuming the current mode is nearest-even). We may need a
  // compiler flag for precise floating-point to prevent "optimizing" this out.
  const DFromV<decltype(v)> df;
  const auto max = Set(df, MantissaEnd<T>());
  const auto large = CopySignToAbs(max, v);
  const auto added = large + v;
  const auto rounded = added - large;
  // Keep original if NaN or the magnitude is large (already an int).
  return IfThenElse(Abs(v) < max, rounded, v);
}

namespace detail {

// Truncating to integer and converting back to float is correct except when the
// input magnitude is large, in which case the input was already an integer
// (because mantissa >> exponent is zero).
template <typename T, size_t N>
HWY_INLINE Mask128<T, N> UseInt(const Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  const DFromV<decltype(v)> d;
  return Abs(v) < Set(d, MantissaEnd<T>());
}

}  // namespace detail

// Toward zero, aka truncate
template <typename T, size_t N>
HWY_API Vec128<T, N> Trunc(const Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertInRangeTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  return IfThenElse(detail::UseInt(v), CopySign(int_f, v), v);
}

// Toward +infinity, aka ceiling
template <typename T, size_t N>
HWY_API Vec128<T, N> Ceil(const Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertInRangeTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a positive non-integer ends up smaller; if so, add 1.
  const auto neg1 = ConvertTo(df, VecFromMask(di, RebindMask(di, int_f < v)));

  return IfThenElse(detail::UseInt(v), int_f - neg1, v);
}

#ifdef HWY_NATIVE_CEIL_FLOOR_INT
#undef HWY_NATIVE_CEIL_FLOOR_INT
#else
#define HWY_NATIVE_CEIL_FLOOR_INT
#endif

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API VFromD<RebindToSigned<DFromV<V>>> CeilInt(V v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a positive non-integer ends up smaller; if so, add 1.
  return integer -
         VecFromMask(di, RebindMask(di, And(detail::UseInt(v), int_f < v)));
}

// Toward -infinity, aka floor
template <typename T, size_t N>
HWY_API Vec128<T, N> Floor(const Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertInRangeTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a negative non-integer ends up larger; if so, subtract 1.
  const auto neg1 = ConvertTo(df, VecFromMask(di, RebindMask(di, int_f > v)));

  return IfThenElse(detail::UseInt(v), int_f + neg1, v);
}

template <class V, HWY_IF_FLOAT_V(V)>
HWY_API VFromD<RebindToSigned<DFromV<V>>> FloorInt(V v) {
  const DFromV<decltype(v)> df;
  const RebindToSigned<decltype(df)> di;

  const auto integer = ConvertTo(di, v);  // round toward 0
  const auto int_f = ConvertTo(df, integer);

  // Truncating a negative non-integer ends up larger; if so, subtract 1.
  return integer +
         VecFromMask(di, RebindMask(di, And(detail::UseInt(v), int_f > v)));
}

#else

// Toward nearest integer, ties to even
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Round(const Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{
      _mm_roundscale_ph(v.raw, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Round(const Vec128<float, N> v) {
  return Vec128<float, N>{
      _mm_round_ps(v.raw, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)};
}
template <size_t N>
HWY_API Vec128<double, N> Round(const Vec128<double, N> v) {
  return Vec128<double, N>{
      _mm_round_pd(v.raw, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC)};
}

// Toward zero, aka truncate
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Trunc(const Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{
      _mm_roundscale_ph(v.raw, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Trunc(const Vec128<float, N> v) {
  return Vec128<float, N>{
      _mm_round_ps(v.raw, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC)};
}
template <size_t N>
HWY_API Vec128<double, N> Trunc(const Vec128<double, N> v) {
  return Vec128<double, N>{
      _mm_round_pd(v.raw, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC)};
}

// Toward +infinity, aka ceiling
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Ceil(const Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{
      _mm_roundscale_ph(v.raw, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Ceil(const Vec128<float, N> v) {
  return Vec128<float, N>{
      _mm_round_ps(v.raw, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC)};
}
template <size_t N>
HWY_API Vec128<double, N> Ceil(const Vec128<double, N> v) {
  return Vec128<double, N>{
      _mm_round_pd(v.raw, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC)};
}

// Toward -infinity, aka floor
#if HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float16_t, N> Floor(const Vec128<float16_t, N> v) {
  return Vec128<float16_t, N>{
      _mm_roundscale_ph(v.raw, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC)};
}
#endif  // HWY_HAVE_FLOAT16
template <size_t N>
HWY_API Vec128<float, N> Floor(const Vec128<float, N> v) {
  return Vec128<float, N>{
      _mm_round_ps(v.raw, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC)};
}
template <size_t N>
HWY_API Vec128<double, N> Floor(const Vec128<double, N> v) {
  return Vec128<double, N>{
      _mm_round_pd(v.raw, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC)};
}

#endif  // !HWY_SSSE3

// ------------------------------ Floating-point classification

#define HWY_X86_FPCLASS_QNAN 0x01
#define HWY_X86_FPCLASS_POS0 0x02
#define HWY_X86_FPCLASS_NEG0 0x04
#define HWY_X86_FPCLASS_POS_INF 0x08
#define HWY_X86_FPCLASS_NEG_INF 0x10
#define HWY_X86_FPCLASS_SUBNORMAL 0x20
#define HWY_X86_FPCLASS_NEG 0x40
#define HWY_X86_FPCLASS_SNAN 0x80

#if HWY_HAVE_FLOAT16 || HWY_IDE

template <size_t N>
HWY_API Mask128<float16_t, N> IsNaN(const Vec128<float16_t, N> v) {
  return Mask128<float16_t, N>{
      _mm_fpclass_ph_mask(v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN)};
}

template <size_t N>
HWY_API Mask128<float16_t, N> IsEitherNaN(Vec128<float16_t, N> a,
                                          Vec128<float16_t, N> b) {
  // Work around warnings in the intrinsic definitions (passing -1 as a mask).
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")
  return Mask128<float16_t, N>{_mm_cmp_ph_mask(a.raw, b.raw, _CMP_UNORD_Q)};
  HWY_DIAGNOSTICS(pop)
}

template <size_t N>
HWY_API Mask128<float16_t, N> IsInf(const Vec128<float16_t, N> v) {
  return Mask128<float16_t, N>{_mm_fpclass_ph_mask(
      v.raw, HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)};
}

template <size_t N>
HWY_API Mask128<float16_t, N> IsFinite(const Vec128<float16_t, N> v) {
  // fpclass doesn't have a flag for positive, so we have to check for inf/NaN
  // and negate the mask.
  return Not(Mask128<float16_t, N>{_mm_fpclass_ph_mask(
      v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN |
                 HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)});
}

#endif  // HWY_HAVE_FLOAT16

template <size_t N>
HWY_API Mask128<float, N> IsNaN(const Vec128<float, N> v) {
#if HWY_TARGET <= HWY_AVX3
  return Mask128<float, N>{
      _mm_fpclass_ps_mask(v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN)};
#else
  return Mask128<float, N>{_mm_cmpunord_ps(v.raw, v.raw)};
#endif
}
template <size_t N>
HWY_API Mask128<double, N> IsNaN(const Vec128<double, N> v) {
#if HWY_TARGET <= HWY_AVX3
  return Mask128<double, N>{
      _mm_fpclass_pd_mask(v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN)};
#else
  return Mask128<double, N>{_mm_cmpunord_pd(v.raw, v.raw)};
#endif
}

#ifdef HWY_NATIVE_IS_EITHER_NAN
#undef HWY_NATIVE_IS_EITHER_NAN
#else
#define HWY_NATIVE_IS_EITHER_NAN
#endif

template <size_t N>
HWY_API Mask128<float, N> IsEitherNaN(Vec128<float, N> a, Vec128<float, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Mask128<float, N>{_mm_cmp_ps_mask(a.raw, b.raw, _CMP_UNORD_Q)};
#else
  return Mask128<float, N>{_mm_cmpunord_ps(a.raw, b.raw)};
#endif
}

template <size_t N>
HWY_API Mask128<double, N> IsEitherNaN(Vec128<double, N> a,
                                       Vec128<double, N> b) {
#if HWY_TARGET <= HWY_AVX3
  return Mask128<double, N>{_mm_cmp_pd_mask(a.raw, b.raw, _CMP_UNORD_Q)};
#else
  return Mask128<double, N>{_mm_cmpunord_pd(a.raw, b.raw)};
#endif
}

#if HWY_TARGET <= HWY_AVX3

// Per-target flag to prevent generic_ops-inl.h from defining IsInf / IsFinite.
#ifdef HWY_NATIVE_ISINF
#undef HWY_NATIVE_ISINF
#else
#define HWY_NATIVE_ISINF
#endif

template <size_t N>
HWY_API Mask128<float, N> IsInf(const Vec128<float, N> v) {
  return Mask128<float, N>{_mm_fpclass_ps_mask(
      v.raw, HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)};
}
template <size_t N>
HWY_API Mask128<double, N> IsInf(const Vec128<double, N> v) {
  return Mask128<double, N>{_mm_fpclass_pd_mask(
      v.raw, HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)};
}

// Returns whether normal/subnormal/zero.
template <size_t N>
HWY_API Mask128<float, N> IsFinite(const Vec128<float, N> v) {
  // fpclass doesn't have a flag for positive, so we have to check for inf/NaN
  // and negate the mask.
  return Not(Mask128<float, N>{_mm_fpclass_ps_mask(
      v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN |
                 HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)});
}
template <size_t N>
HWY_API Mask128<double, N> IsFinite(const Vec128<double, N> v) {
  return Not(Mask128<double, N>{_mm_fpclass_pd_mask(
      v.raw, HWY_X86_FPCLASS_SNAN | HWY_X86_FPCLASS_QNAN |
                 HWY_X86_FPCLASS_NEG_INF | HWY_X86_FPCLASS_POS_INF)});
}

#endif  // HWY_TARGET <= HWY_AVX3

// ================================================== CRYPTO

#if !defined(HWY_DISABLE_PCLMUL_AES) && HWY_TARGET <= HWY_SSE4

// Per-target flag to prevent generic_ops-inl.h from defining AESRound.
#ifdef HWY_NATIVE_AES
#undef HWY_NATIVE_AES
#else
#define HWY_NATIVE_AES
#endif

HWY_API Vec128<uint8_t> AESRound(Vec128<uint8_t> state,
                                 Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>{_mm_aesenc_si128(state.raw, round_key.raw)};
}

HWY_API Vec128<uint8_t> AESLastRound(Vec128<uint8_t> state,
                                     Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>{_mm_aesenclast_si128(state.raw, round_key.raw)};
}

HWY_API Vec128<uint8_t> AESInvMixColumns(Vec128<uint8_t> state) {
  return Vec128<uint8_t>{_mm_aesimc_si128(state.raw)};
}

HWY_API Vec128<uint8_t> AESRoundInv(Vec128<uint8_t> state,
                                    Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>{_mm_aesdec_si128(state.raw, round_key.raw)};
}

HWY_API Vec128<uint8_t> AESLastRoundInv(Vec128<uint8_t> state,
                                        Vec128<uint8_t> round_key) {
  return Vec128<uint8_t>{_mm_aesdeclast_si128(state.raw, round_key.raw)};
}

template <uint8_t kRcon>
HWY_API Vec128<uint8_t> AESKeyGenAssist(Vec128<uint8_t> v) {
  return Vec128<uint8_t>{_mm_aeskeygenassist_si128(v.raw, kRcon)};
}

template <size_t N>
HWY_API Vec128<uint64_t, N> CLMulLower(Vec128<uint64_t, N> a,
                                       Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{_mm_clmulepi64_si128(a.raw, b.raw, 0x00)};
}

template <size_t N>
HWY_API Vec128<uint64_t, N> CLMulUpper(Vec128<uint64_t, N> a,
                                       Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{_mm_clmulepi64_si128(a.raw, b.raw, 0x11)};
}

#endif  // !defined(HWY_DISABLE_PCLMUL_AES) && HWY_TARGET <= HWY_SSE4

// ================================================== MISC

// ------------------------------ LoadMaskBits (TestBit)

#if HWY_TARGET > HWY_AVX3
namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE MFromD<D> LoadMaskBits128(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  // Easier than Set(), which would require an >8-bit type, which would not
  // compile for T=uint8_t, kN=1.
  const VFromD<D> vbits{_mm_cvtsi32_si128(static_cast<int>(mask_bits))};

#if HWY_TARGET == HWY_SSE2
  // {b0, b1, ...} ===> {b0, b0, b1, b1, ...}
  __m128i unpacked_vbits = _mm_unpacklo_epi8(vbits.raw, vbits.raw);
  // {b0, b0, b1, b1, ...} ==> {b0, b0, b0, b0, b1, b1, b1, b1, ...}
  unpacked_vbits = _mm_unpacklo_epi16(unpacked_vbits, unpacked_vbits);
  // {b0, b0, b0, b0, b1, b1, b1, b1, ...} ==>
  // {b0, b0, b0, b0, b0, b0, b0, b0, b1, b1, b1, b1, b1, b1, b1, b1}
  const VFromD<decltype(du)> rep8{
      _mm_unpacklo_epi32(unpacked_vbits, unpacked_vbits)};
#else
  // Replicate bytes 8x such that each byte contains the bit that governs it.
  alignas(16) static constexpr uint8_t kRep8[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                    1, 1, 1, 1, 1, 1, 1, 1};
  const auto rep8 = TableLookupBytes(vbits, Load(du, kRep8));
#endif
  const VFromD<decltype(du)> bit = Dup128VecFromValues(
      du, 1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128);
  return RebindMask(d, TestBit(rep8, bit));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE MFromD<D> LoadMaskBits128(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint16_t kBit[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  const auto vmask_bits = Set(du, static_cast<uint16_t>(mask_bits));
  return RebindMask(d, TestBit(vmask_bits, Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE MFromD<D> LoadMaskBits128(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint32_t kBit[8] = {1, 2, 4, 8};
  const auto vmask_bits = Set(du, static_cast<uint32_t>(mask_bits));
  return RebindMask(d, TestBit(vmask_bits, Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> LoadMaskBits128(D d, uint64_t mask_bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint64_t kBit[8] = {1, 2};
  return RebindMask(d, TestBit(Set(du, mask_bits), Load(du, kBit)));
}

}  // namespace detail
#endif  // HWY_TARGET > HWY_AVX3

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  constexpr size_t kN = MaxLanes(d);
#if HWY_TARGET <= HWY_AVX3
  (void)d;
  uint64_t mask_bits = 0;
  constexpr size_t kNumBytes = (kN + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (kN < 8) {
    mask_bits &= (1ull << kN) - 1;
  }

  return MFromD<D>::FromBits(mask_bits);
#else
  uint64_t mask_bits = 0;
  constexpr size_t kNumBytes = (kN + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (kN < 8) {
    mask_bits &= (1ull << kN) - 1;
  }

  return detail::LoadMaskBits128(d, mask_bits);
#endif
}

// ------------------------------ Dup128MaskFromMaskBits

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;

#if HWY_TARGET <= HWY_AVX3
  return MFromD<D>::FromBits(mask_bits);
#else
  return detail::LoadMaskBits128(d, mask_bits);
#endif
}

template <typename T>
struct CompressIsPartition {
#if HWY_TARGET <= HWY_AVX3
  // AVX3 supports native compress, but a table-based approach allows
  // 'partitioning' (also moving mask=false lanes to the top), which helps
  // vqsort. This is only feasible for eight or less lanes, i.e. sizeof(T) == 8
  // on AVX3. For simplicity, we only use tables for 64-bit lanes (not AVX3
  // u32x8 etc.).
  enum { value = (sizeof(T) == 8) };
#else
  // generic_ops-inl does not guarantee IsPartition for 8-bit.
  enum { value = (sizeof(T) != 1) };
#endif
};

#if HWY_TARGET <= HWY_AVX3

// ------------------------------ StoreMaskBits

// `p` points to at least 8 writable bytes.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t StoreMaskBits(D d, MFromD<D> mask, uint8_t* bits) {
  constexpr size_t kN = MaxLanes(d);
  constexpr size_t kNumBytes = (kN + 7) / 8;
  CopyBytes<kNumBytes>(&mask.raw, bits);

  // Non-full byte, need to clear the undefined upper bits.
  if (kN < 8) {
    const int mask_bits = (1 << kN) - 1;
    bits[0] = static_cast<uint8_t>(bits[0] & mask_bits);
  }

  return kNumBytes;
}

// ------------------------------ Mask testing

// Beware: the suffix indicates the number of mask bits, not lane size!

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t CountTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint64_t mask_bits = uint64_t{mask.raw} & ((1ull << kN) - 1);
  return PopCount(mask_bits);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t FindKnownFirstTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint32_t mask_bits = uint32_t{mask.raw} & ((1u << kN) - 1);
  return Num0BitsBelowLS1Bit_Nonzero32(mask_bits);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API intptr_t FindFirstTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint32_t mask_bits = uint32_t{mask.raw} & ((1u << kN) - 1);
  return mask_bits ? intptr_t(Num0BitsBelowLS1Bit_Nonzero32(mask_bits)) : -1;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t FindKnownLastTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint32_t mask_bits = uint32_t{mask.raw} & ((1u << kN) - 1);
  return 31 - Num0BitsAboveMS1Bit_Nonzero32(mask_bits);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API intptr_t FindLastTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint32_t mask_bits = uint32_t{mask.raw} & ((1u << kN) - 1);
  return mask_bits ? intptr_t(31 - Num0BitsAboveMS1Bit_Nonzero32(mask_bits))
                   : -1;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API bool AllFalse(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint64_t mask_bits = uint64_t{mask.raw} & ((1ull << kN) - 1);
  return mask_bits == 0;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API bool AllTrue(D d, MFromD<D> mask) {
  constexpr size_t kN = MaxLanes(d);
  const uint64_t mask_bits = uint64_t{mask.raw} & ((1ull << kN) - 1);
  // Cannot use _kortestc because we may have less than 8 mask bits.
  return mask_bits == (1ull << kN) - 1;
}

// ------------------------------ Compress

// 8-16 bit Compress, CompressStore defined in x86_512 because they use Vec512.

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> Compress(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

template <size_t N, HWY_IF_V_SIZE_GT(float, N, 4)>
HWY_API Vec128<float, N> Compress(Vec128<float, N> v, Mask128<float, N> mask) {
  return Vec128<float, N>{_mm_maskz_compress_ps(mask.raw, v.raw)};
}

template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Compress(Vec128<T> v, Mask128<T> mask) {
  HWY_DASSERT(mask.raw < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[64] = {
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  const auto index = Load(d8, u8_indices + 16 * mask.raw);
  return BitCast(d, TableLookupBytes(BitCast(d8, v), index));
}

// ------------------------------ CompressNot (Compress)

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> CompressNot(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> CompressNot(Vec128<T> v, Mask128<T> mask) {
  // See CompressIsPartition, PrintCompressNot64x2NibbleTables
  alignas(16) static constexpr uint64_t packed_array[16] = {
      0x00000010, 0x00000001, 0x00000010, 0x00000010};

  // For lane i, shift the i-th 4-bit index down to bits [0, 2) -
  // _mm_permutexvar_epi64 will ignore the upper bits.
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du64;
  const auto packed = Set(du64, packed_array[mask.raw]);
  alignas(16) static constexpr uint64_t shifts[2] = {0, 4};
  const auto indices = Indices128<T>{(packed >> Load(du64, shifts)).raw};
  return TableLookupLanes(v, indices);
}

// ------------------------------ CompressBlocksNot
HWY_API Vec128<uint64_t> CompressBlocksNot(Vec128<uint64_t> v,
                                           Mask128<uint64_t> /* m */) {
  return v;
}

// ------------------------------ CompressStore (defined in x86_512)

// ------------------------------ CompressBlendedStore (CompressStore)
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  // AVX-512 already does the blending at no extra cost (latency 11,
  // rthroughput 2 - same as compress plus store).
  if (HWY_TARGET == HWY_AVX3_DL ||
      (HWY_TARGET != HWY_AVX3_ZEN4 && sizeof(TFromD<D>) > 2)) {
    // We're relying on the mask to blend. Clear the undefined upper bits.
    constexpr size_t kN = MaxLanes(d);
    if (kN != 16 / sizeof(TFromD<D>)) {
      m = And(m, FirstN(d, kN));
    }
    return CompressStore(v, m, d, unaligned);
  } else {
    const size_t count = CountTrue(d, m);
    const VFromD<D> compressed = Compress(v, m);
#if HWY_MEM_OPS_MIGHT_FAULT
    // BlendedStore tests mask for each lane, but we know that the mask is
    // FirstN, so we can just copy.
    alignas(16) TFromD<D> buf[MaxLanes(d)];
    Store(compressed, d, buf);
    CopyBytes(buf, unaligned, count * sizeof(TFromD<D>));
#else
    BlendedStore(compressed, FirstN(d, count), d, unaligned);
#endif
    detail::MaybeUnpoison(unaligned, count);
    return count;
  }
}

// ------------------------------ CompressBitsStore (defined in x86_512)

#else  // AVX2 or below

// ------------------------------ StoreMaskBits

namespace detail {

constexpr HWY_INLINE uint64_t U64FromInt(int mask_bits) {
  return static_cast<uint64_t>(static_cast<unsigned>(mask_bits));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/,
                                 const Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  const auto sign_bits = BitCast(d, VecFromMask(d, mask)).raw;
  return U64FromInt(_mm_movemask_epi8(sign_bits));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<2> /*tag*/,
                                 const Mask128<T, N> mask) {
  // Remove useless lower half of each u16 while preserving the sign bit.
  const auto sign_bits = _mm_packs_epi16(mask.raw, _mm_setzero_si128());
  return U64FromInt(_mm_movemask_epi8(sign_bits));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<4> /*tag*/, Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  const Simd<float, N, 0> df;
  const auto sign_bits = BitCast(df, VecFromMask(d, mask));
  return U64FromInt(_mm_movemask_ps(sign_bits.raw));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<8> /*tag*/, Mask128<T, N> mask) {
  const Simd<T, N, 0> d;
  const Simd<double, N, 0> df;
  const auto sign_bits = BitCast(df, VecFromMask(d, mask));
  return U64FromInt(_mm_movemask_pd(sign_bits.raw));
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(const Mask128<T, N> mask) {
  return OnlyActive<T, N>(BitsFromMask(hwy::SizeTag<sizeof(T)>(), mask));
}

}  // namespace detail

// `p` points to at least 8 writable bytes.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t StoreMaskBits(D d, MFromD<D> mask, uint8_t* bits) {
  constexpr size_t kNumBytes = (MaxLanes(d) + 7) / 8;
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  CopyBytes<kNumBytes>(&mask_bits, bits);
  return kNumBytes;
}

// ------------------------------ Mask testing

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API bool AllFalse(D /* tag */, MFromD<D> mask) {
  // Cheaper than PTEST, which is 2 uop / 3L.
  return detail::BitsFromMask(mask) == 0;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API bool AllTrue(D d, MFromD<D> mask) {
  constexpr uint64_t kAllBits = (1ull << MaxLanes(d)) - 1;
  return detail::BitsFromMask(mask) == kAllBits;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t CountTrue(D /* tag */, MFromD<D> mask) {
  return PopCount(detail::BitsFromMask(mask));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t FindKnownFirstTrue(D /* tag */, MFromD<D> mask) {
  return Num0BitsBelowLS1Bit_Nonzero32(
      static_cast<uint32_t>(detail::BitsFromMask(mask)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API intptr_t FindFirstTrue(D /* tag */, MFromD<D> mask) {
  const uint32_t mask_bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return mask_bits ? intptr_t(Num0BitsBelowLS1Bit_Nonzero32(mask_bits)) : -1;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API size_t FindKnownLastTrue(D /* tag */, MFromD<D> mask) {
  return 31 - Num0BitsAboveMS1Bit_Nonzero32(
                  static_cast<uint32_t>(detail::BitsFromMask(mask)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API intptr_t FindLastTrue(D /* tag */, MFromD<D> mask) {
  const uint32_t mask_bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return mask_bits ? intptr_t(31 - Num0BitsAboveMS1Bit_Nonzero32(mask_bits))
                   : -1;
}

// ------------------------------ Compress, CompressBits

namespace detail {

// Also works for N < 8 because the first 16 4-tuples only reference bytes 0-6.
template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<D> IndicesFromBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Rebind<uint8_t, decltype(d)> d8;
  const Twice<decltype(d8)> d8t;
  const RebindToUnsigned<decltype(d)> du;

  // compress_epi16 requires VBMI2 and there is no permutevar_epi16, so we need
  // byte indices for PSHUFB (one vector's worth for each of 256 combinations of
  // 8 mask bits). Loading them directly would require 4 KiB. We can instead
  // store lane indices and convert to byte indices (2*lane + 0..1), with the
  // doubling baked into the table. AVX2 Compress32 stores eight 4-bit lane
  // indices (total 1 KiB), broadcasts them into each 32-bit lane and shifts.
  // Here, 16-bit lanes are too narrow to hold all bits, and unpacking nibbles
  // is likely more costly than the higher cache footprint from storing bytes.
  alignas(16) static constexpr uint8_t table[2048] = {
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

  const VFromD<decltype(d8t)> byte_idx{Load(d8, table + mask_bits * 8).raw};
  const VFromD<decltype(du)> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<D> IndicesFromNotBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Rebind<uint8_t, decltype(d)> d8;
  const Twice<decltype(d8)> d8t;
  const RebindToUnsigned<decltype(d)> du;

  // compress_epi16 requires VBMI2 and there is no permutevar_epi16, so we need
  // byte indices for PSHUFB (one vector's worth for each of 256 combinations of
  // 8 mask bits). Loading them directly would require 4 KiB. We can instead
  // store lane indices and convert to byte indices (2*lane + 0..1), with the
  // doubling baked into the table. AVX2 Compress32 stores eight 4-bit lane
  // indices (total 1 KiB), broadcasts them into each 32-bit lane and shifts.
  // Here, 16-bit lanes are too narrow to hold all bits, and unpacking nibbles
  // is likely more costly than the higher cache footprint from storing bytes.
  alignas(16) static constexpr uint8_t table[2048] = {
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

  const VFromD<decltype(d8t)> byte_idx{Load(d8, table + mask_bits * 8).raw};
  const VFromD<decltype(du)> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<D> IndicesFromBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 16);

  // There are only 4 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[256] = {
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

  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<D> IndicesFromNotBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 16);

  // There are only 4 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[256] = {
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

  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<D> IndicesFromBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[64] = {
      // PrintCompress64x2Tables
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<D> IndicesFromNotBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[64] = {
      // PrintCompressNot64x2Tables
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v, uint64_t mask_bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  HWY_DASSERT(mask_bits < (1ull << N));
  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  return BitCast(d, TableLookupBytes(BitCast(du, v), indices));
}

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressNotBits(Vec128<T, N> v, uint64_t mask_bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  HWY_DASSERT(mask_bits < (1ull << N));
  const auto indices = BitCast(du, detail::IndicesFromNotBits128(d, mask_bits));
  return BitCast(d, TableLookupBytes(BitCast(du, v), indices));
}

}  // namespace detail

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> Compress(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

// Two lanes: conditional swap
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Compress(Vec128<T> v, Mask128<T> mask) {
  // If mask[1] = 1 and mask[0] = 0, then swap both halves, else keep.
  const DFromV<decltype(v)> d;
  const Vec128<T> m = VecFromMask(d, mask);
  const Vec128<T> maskL = DupEven(m);
  const Vec128<T> maskH = DupOdd(m);
  const Vec128<T> swap = AndNot(maskL, maskH);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

// General case, 2 or 4 bytes
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> Compress(Vec128<T, N> v, Mask128<T, N> mask) {
  return detail::CompressBits(v, detail::BitsFromMask(mask));
}

// ------------------------------ CompressNot

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

template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> CompressNot(Vec128<T, N> v, Mask128<T, N> mask) {
  // For partial vectors, we cannot pull the Not() into the table because
  // BitsFromMask clears the upper bits.
  if (N < 16 / sizeof(T)) {
    return detail::CompressBits(v, detail::BitsFromMask(Not(mask)));
  }
  return detail::CompressNotBits(v, detail::BitsFromMask(mask));
}

// ------------------------------ CompressBlocksNot
HWY_API Vec128<uint64_t> CompressBlocksNot(Vec128<uint64_t> v,
                                           Mask128<uint64_t> /* m */) {
  return v;
}

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v,
                                  const uint8_t* HWY_RESTRICT bits) {
  uint64_t mask_bits = 0;
  constexpr size_t kNumBytes = (N + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (N < 8) {
    mask_bits &= (1ull << N) - 1;
  }

  return detail::CompressBits(v, mask_bits);
}

// ------------------------------ CompressStore, CompressBitsStore

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(VFromD<D> v, MFromD<D> m, D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  const uint64_t mask_bits = detail::BitsFromMask(m);
  HWY_DASSERT(mask_bits < (1ull << MaxLanes(d)));
  const size_t count = PopCount(mask_bits);

  // Avoid _mm_maskmoveu_si128 (>500 cycle latency because it bypasses caches).
  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
  StoreU(compressed, d, unaligned);
  detail::MaybeUnpoison(unaligned, count);
  return count;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  const uint64_t mask_bits = detail::BitsFromMask(m);
  HWY_DASSERT(mask_bits < (1ull << MaxLanes(d)));
  const size_t count = PopCount(mask_bits);

  // Avoid _mm_maskmoveu_si128 (>500 cycle latency because it bypasses caches).
  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
  BlendedStore(compressed, FirstN(d, count), d, unaligned);
  detail::MaybeUnpoison(unaligned, count);
  return count;
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  uint64_t mask_bits = 0;
  constexpr size_t kN = MaxLanes(d);
  constexpr size_t kNumBytes = (kN + 7) / 8;
  CopyBytes<kNumBytes>(bits, &mask_bits);
  if (kN < 8) {
    mask_bits &= (1ull << kN) - 1;
  }
  const size_t count = PopCount(mask_bits);

  // Avoid _mm_maskmoveu_si128 (>500 cycle latency because it bypasses caches).
  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
  StoreU(compressed, d, unaligned);

  detail::MaybeUnpoison(unaligned, count);
  return count;
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Expand

// Otherwise, use the generic_ops-inl.h fallback.
#if HWY_TARGET <= HWY_AVX3 || HWY_IDE

// The native instructions for 8/16-bit actually require VBMI2 (HWY_AVX3_DL),
// but we still want to override generic_ops-inl's table-based implementation
// whenever we have the 32-bit expand provided by AVX3.
#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

namespace detail {

#if HWY_TARGET <= HWY_AVX3_DL || HWY_IDE  // VBMI2

template <size_t N>
HWY_INLINE Vec128<uint8_t, N> NativeExpand(Vec128<uint8_t, N> v,
                                           Mask128<uint8_t, N> mask) {
  return Vec128<uint8_t, N>{_mm_maskz_expand_epi8(mask.raw, v.raw)};
}

template <size_t N>
HWY_INLINE Vec128<uint16_t, N> NativeExpand(Vec128<uint16_t, N> v,
                                            Mask128<uint16_t, N> mask) {
  return Vec128<uint16_t, N>{_mm_maskz_expand_epi16(mask.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U8_D(D)>
HWY_INLINE VFromD<D> NativeLoadExpand(MFromD<D> mask, D /* d */,
                                      const uint8_t* HWY_RESTRICT unaligned) {
  return VFromD<D>{_mm_maskz_expandloadu_epi8(mask.raw, unaligned)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U16_D(D)>
HWY_INLINE VFromD<D> NativeLoadExpand(MFromD<D> mask, D /* d */,
                                      const uint16_t* HWY_RESTRICT unaligned) {
  return VFromD<D>{_mm_maskz_expandloadu_epi16(mask.raw, unaligned)};
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

template <size_t N>
HWY_INLINE Vec128<uint32_t, N> NativeExpand(Vec128<uint32_t, N> v,
                                            Mask128<uint32_t, N> mask) {
  return Vec128<uint32_t, N>{_mm_maskz_expand_epi32(mask.raw, v.raw)};
}

template <size_t N>
HWY_INLINE Vec128<uint64_t, N> NativeExpand(Vec128<uint64_t, N> v,
                                            Mask128<uint64_t, N> mask) {
  return Vec128<uint64_t, N>{_mm_maskz_expand_epi64(mask.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_INLINE VFromD<D> NativeLoadExpand(MFromD<D> mask, D /* d */,
                                      const uint32_t* HWY_RESTRICT unaligned) {
  return VFromD<D>{_mm_maskz_expandloadu_epi32(mask.raw, unaligned)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> NativeLoadExpand(MFromD<D> mask, D /* d */,
                                      const uint64_t* HWY_RESTRICT unaligned) {
  return VFromD<D>{_mm_maskz_expandloadu_epi64(mask.raw, unaligned)};
}

}  // namespace detail

// Otherwise, 8/16-bit are implemented in x86_512 using PromoteTo.
#if HWY_TARGET <= HWY_AVX3_DL || HWY_IDE  // VBMI2

template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T, N> Expand(Vec128<T, N> v, Mask128<T, N> mask) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const MFromD<decltype(du)> mu = RebindMask(du, mask);
  return BitCast(d, detail::NativeExpand(BitCast(du, v), mu));
}

#endif  // HWY_TARGET <= HWY_AVX3_DL

template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 4) | (1 << 8))>
HWY_API Vec128<T, N> Expand(Vec128<T, N> v, Mask128<T, N> mask) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const MFromD<decltype(du)> mu = RebindMask(du, mask);
  return BitCast(d, detail::NativeExpand(BitCast(du, v), mu));
}

// ------------------------------ LoadExpand

template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
#if HWY_TARGET <= HWY_AVX3_DL  // VBMI2
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  const TU* HWY_RESTRICT pu = reinterpret_cast<const TU*>(unaligned);
  const MFromD<decltype(du)> mu = RebindMask(du, mask);
  return BitCast(d, detail::NativeLoadExpand(mu, du, pu));
#else
  return Expand(LoadU(d, unaligned), mask);
#endif
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
#if HWY_TARGET <= HWY_AVX3
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  const TU* HWY_RESTRICT pu = reinterpret_cast<const TU*>(unaligned);
  const MFromD<decltype(du)> mu = RebindMask(du, mask);
  return BitCast(d, detail::NativeLoadExpand(mu, du, pu));
#else
  return Expand(LoadU(d, unaligned), mask);
#endif
}

#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ StoreInterleaved2/3/4

// HWY_NATIVE_LOAD_STORE_INTERLEAVED not set, hence defined in
// generic_ops-inl.h.

// ------------------------------ Additional mask logical operations

#if HWY_TARGET <= HWY_AVX3
namespace detail {

template <class T, HWY_IF_LANES_LE(sizeof(T), 4)>
static HWY_INLINE uint32_t AVX3Blsi(T x) {
  using TU = MakeUnsigned<T>;
  const auto u32_val = static_cast<uint32_t>(static_cast<TU>(x));
#if HWY_COMPILER_CLANGCL
  return static_cast<uint32_t>(u32_val & (0u - u32_val));
#else
  return static_cast<uint32_t>(_blsi_u32(u32_val));
#endif
}
template <class T, HWY_IF_T_SIZE(T, 8)>
static HWY_INLINE uint64_t AVX3Blsi(T x) {
  const auto u64_val = static_cast<uint64_t>(x);
#if HWY_COMPILER_CLANGCL || HWY_ARCH_X86_32
  return static_cast<uint64_t>(u64_val & (0ULL - u64_val));
#else
  return static_cast<uint64_t>(_blsi_u64(u64_val));
#endif
}

template <class T, HWY_IF_LANES_LE(sizeof(T), 4)>
static HWY_INLINE uint32_t AVX3Blsmsk(T x) {
  using TU = MakeUnsigned<T>;
  const auto u32_val = static_cast<uint32_t>(static_cast<TU>(x));
#if HWY_COMPILER_CLANGCL
  return static_cast<uint32_t>(u32_val ^ (u32_val - 1u));
#else
  return static_cast<uint32_t>(_blsmsk_u32(u32_val));
#endif
}
template <class T, HWY_IF_T_SIZE(T, 8)>
static HWY_INLINE uint64_t AVX3Blsmsk(T x) {
  const auto u64_val = static_cast<uint64_t>(x);
#if HWY_COMPILER_CLANGCL || HWY_ARCH_X86_32
  return static_cast<uint64_t>(u64_val ^ (u64_val - 1ULL));
#else
  return static_cast<uint64_t>(_blsmsk_u64(u64_val));
#endif
}

}  // namespace detail

template <class T, size_t N>
HWY_API Mask128<T, N> SetAtOrAfterFirst(Mask128<T, N> mask) {
  constexpr uint32_t kActiveElemMask = (uint32_t{1} << N) - 1;
  return Mask128<T, N>{static_cast<typename Mask128<T, N>::Raw>(
      (0u - detail::AVX3Blsi(mask.raw)) & kActiveElemMask)};
}
template <class T, size_t N>
HWY_API Mask128<T, N> SetBeforeFirst(Mask128<T, N> mask) {
  constexpr uint32_t kActiveElemMask = (uint32_t{1} << N) - 1;
  return Mask128<T, N>{static_cast<typename Mask128<T, N>::Raw>(
      (detail::AVX3Blsi(mask.raw) - 1u) & kActiveElemMask)};
}
template <class T, size_t N>
HWY_API Mask128<T, N> SetAtOrBeforeFirst(Mask128<T, N> mask) {
  constexpr uint32_t kActiveElemMask = (uint32_t{1} << N) - 1;
  return Mask128<T, N>{static_cast<typename Mask128<T, N>::Raw>(
      detail::AVX3Blsmsk(mask.raw) & kActiveElemMask)};
}
template <class T, size_t N>
HWY_API Mask128<T, N> SetOnlyFirst(Mask128<T, N> mask) {
  return Mask128<T, N>{
      static_cast<typename Mask128<T, N>::Raw>(detail::AVX3Blsi(mask.raw))};
}
#else   // AVX2 or below
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
  const Repartition<float, decltype(d)> df32;
  const Repartition<int32_t, decltype(d)> di32;
  using VF = VFromD<decltype(df32)>;

  auto vmask = BitCast(di64, VecFromMask(d, mask));
  vmask = Or(vmask, Neg(vmask));

  // Copy the sign bit of the first int64_t lane to the second int64_t lane
  const auto vmask2 = BroadcastSignBit(
      BitCast(di32, VF{_mm_shuffle_ps(Zero(df32).raw, BitCast(df32, vmask).raw,
                                      _MM_SHUFFLE(1, 1, 0, 0))}));
  return MaskFromVec(BitCast(d, Or(vmask, BitCast(di64, vmask2))));
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
#endif  // HWY_TARGET <= HWY_AVX3

// ------------------------------ Reductions

// Nothing fully native, generic_ops-inl defines SumOfLanes and ReduceSum.

// We provide specializations of u8x8 and u8x16, so exclude those.
#undef HWY_IF_SUM_OF_LANES_D
#define HWY_IF_SUM_OF_LANES_D(D)                                        \
  HWY_IF_LANES_GT_D(D, 1),                                              \
      hwy::EnableIf<!hwy::IsSame<TFromD<D>, uint8_t>() ||               \
                    (HWY_V_SIZE_D(D) != 8 && HWY_V_SIZE_D(D) != 16)>* = \
          nullptr

template <class D, HWY_IF_U8_D(D), HWY_IF_LANES_D(D, 8)>
HWY_API VFromD<D> SumOfLanes(D d, VFromD<D> v) {
  return Set(d, static_cast<uint8_t>(GetLane(SumsOf8(v)) & 0xFF));
}
template <class D, HWY_IF_U8_D(D), HWY_IF_LANES_D(D, 16)>
HWY_API VFromD<D> SumOfLanes(D d, VFromD<D> v) {
  const Repartition<uint64_t, decltype(d)> d64;
  VFromD<decltype(d64)> sums = SumsOf8(v);
  sums = SumOfLanes(d64, sums);
  return Broadcast<0>(BitCast(d, sums));
}

#if HWY_TARGET <= HWY_SSE4
// We provide specializations of u8x8, u8x16, and u16x8, so exclude those.
#undef HWY_IF_MINMAX_OF_LANES_D
#define HWY_IF_MINMAX_OF_LANES_D(D)                                        \
  HWY_IF_LANES_GT_D(D, 1),                                                 \
      hwy::EnableIf<(!hwy::IsSame<TFromD<D>, uint8_t>() ||                 \
                     ((HWY_V_SIZE_D(D) < 8) || (HWY_V_SIZE_D(D) > 16))) && \
                    (!hwy::IsSame<TFromD<D>, uint16_t>() ||                \
                     (HWY_V_SIZE_D(D) != 16))>* = nullptr

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> MinOfLanes(D /* tag */, Vec128<uint16_t> v) {
  return Broadcast<0>(Vec128<uint16_t>{_mm_minpos_epu16(v.raw)});
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> MaxOfLanes(D d, Vec128<uint16_t> v) {
  const Vec128<uint16_t> max = Set(d, LimitsMax<uint16_t>());
  return max - MinOfLanes(d, max - v);
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> MinOfLanes(D d, Vec64<uint8_t> v) {
  const Rebind<uint16_t, decltype(d)> d16;
  return TruncateTo(d, MinOfLanes(d16, PromoteTo(d16, v)));
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> MinOfLanes(D d, Vec128<uint8_t> v) {
  const Half<decltype(d)> dh;
  Vec64<uint8_t> result =
      Min(MinOfLanes(dh, UpperHalf(dh, v)), MinOfLanes(dh, LowerHalf(dh, v)));
  return Combine(d, result, result);
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> MaxOfLanes(D d, Vec64<uint8_t> v) {
  const Vec64<uint8_t> m(Set(d, LimitsMax<uint8_t>()));
  return m - MinOfLanes(d, m - v);
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> MaxOfLanes(D d, Vec128<uint8_t> v) {
  const Vec128<uint8_t> m(Set(d, LimitsMax<uint8_t>()));
  return m - MinOfLanes(d, m - v);
}

#endif  // HWY_TARGET <= HWY_SSE4

// ------------------------------ BitShuffle
#if HWY_TARGET <= HWY_AVX3_DL

#ifdef HWY_NATIVE_BITSHUFFLE
#undef HWY_NATIVE_BITSHUFFLE
#else
#define HWY_NATIVE_BITSHUFFLE
#endif

template <class V, class VI, HWY_IF_UI64(TFromV<V>), HWY_IF_UI8(TFromV<VI>),
          HWY_IF_V_SIZE_LE_V(V, 16),
          HWY_IF_V_SIZE_V(VI, HWY_MAX_LANES_V(V) * 8)>
HWY_API V BitShuffle(V v, VI idx) {
  const DFromV<decltype(v)> d64;
  const RebindToUnsigned<decltype(d64)> du64;
  const Rebind<uint8_t, decltype(d64)> du8;

  int32_t i32_bit_shuf_result = static_cast<int32_t>(
      static_cast<uint16_t>(_mm_bitshuffle_epi64_mask(v.raw, idx.raw)));

  return BitCast(d64, PromoteTo(du64, VFromD<decltype(du8)>{_mm_cvtsi32_si128(
                                          i32_bit_shuf_result)}));
}
#endif  // HWY_TARGET <= HWY_AVX3_DL

// ------------------------------ Lt128

namespace detail {

// Returns vector-mask for Lt128. Generic for all vector lengths.
template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Lt128Vec(const D d, VFromD<D> a, VFromD<D> b) {
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
  const auto eqHL = Eq(a, b);
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  const VFromD<D> ltLX = ShiftLeftLanes<1>(ltHL);
  const VFromD<D> vecHx = IfThenElse(eqHL, ltLX, ltHL);
  return InterleaveUpper(d, vecHx, vecHx);
}

// Returns vector-mask for Eq128. Generic for all vector lengths.
template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Eq128Vec(D d, VFromD<D> a, VFromD<D> b) {
  const auto eqHL = VecFromMask(d, Eq(a, b));
  const auto eqLH = Reverse2(d, eqHL);
  return And(eqHL, eqLH);
}

template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Ne128Vec(D d, VFromD<D> a, VFromD<D> b) {
  const auto neHL = VecFromMask(d, Ne(a, b));
  const auto neLH = Reverse2(d, neHL);
  return Or(neHL, neLH);
}

template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Lt128UpperVec(D d, VFromD<D> a, VFromD<D> b) {
  // No specialization required for AVX-512: Mask <-> Vec is fast, and
  // copying mask bits to their neighbor seems infeasible.
  const VFromD<D> ltHL = VecFromMask(d, Lt(a, b));
  return InterleaveUpper(d, ltHL, ltHL);
}

template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Eq128UpperVec(D d, VFromD<D> a, VFromD<D> b) {
  // No specialization required for AVX-512: Mask <-> Vec is fast, and
  // copying mask bits to their neighbor seems infeasible.
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  return InterleaveUpper(d, eqHL, eqHL);
}

template <class D, HWY_IF_U64_D(D)>
HWY_INLINE VFromD<D> Ne128UpperVec(D d, VFromD<D> a, VFromD<D> b) {
  // No specialization required for AVX-512: Mask <-> Vec is fast, and
  // copying mask bits to their neighbor seems infeasible.
  const VFromD<D> neHL = VecFromMask(d, Ne(a, b));
  return InterleaveUpper(d, neHL, neHL);
}

}  // namespace detail

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Lt128(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Lt128Vec(d, a, b));
}

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Eq128(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Eq128Vec(d, a, b));
}

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Ne128(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Ne128Vec(d, a, b));
}

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Lt128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Lt128UpperVec(d, a, b));
}

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Eq128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Eq128UpperVec(d, a, b));
}

template <class D, HWY_IF_U64_D(D)>
HWY_API MFromD<D> Ne128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return MaskFromVec(detail::Ne128UpperVec(d, a, b));
}

// ------------------------------ Min128, Max128 (Lt128)

// Avoids the extra MaskFromVec in Lt128.
template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> Min128(D d, VFromD<D> a, VFromD<D> b) {
  return IfVecThenElse(detail::Lt128Vec(d, a, b), a, b);
}

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> Max128(D d, VFromD<D> a, VFromD<D> b) {
  return IfVecThenElse(detail::Lt128Vec(d, b, a), a, b);
}

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> Min128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfVecThenElse(detail::Lt128UpperVec(d, a, b), a, b);
}

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> Max128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfVecThenElse(detail::Lt128UpperVec(d, b, a), a, b);
}

// -------------------- LeadingZeroCount, TrailingZeroCount, HighestSetBitIndex

#if HWY_TARGET <= HWY_AVX3

#ifdef HWY_NATIVE_LEADING_ZERO_COUNT
#undef HWY_NATIVE_LEADING_ZERO_COUNT
#else
#define HWY_NATIVE_LEADING_ZERO_COUNT
#endif

template <class V, HWY_IF_UI32(TFromV<V>), HWY_IF_V_SIZE_LE_D(DFromV<V>, 16)>
HWY_API V LeadingZeroCount(V v) {
  return V{_mm_lzcnt_epi32(v.raw)};
}

template <class V, HWY_IF_UI64(TFromV<V>), HWY_IF_V_SIZE_LE_D(DFromV<V>, 16)>
HWY_API V LeadingZeroCount(V v) {
  return V{_mm_lzcnt_epi64(v.raw)};
}

// HighestSetBitIndex and TrailingZeroCount is implemented in x86_512-inl.h
// for AVX3 targets

#endif  // HWY_TARGET <= HWY_AVX3

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#undef HWY_X86_IF_EMULATED_D

// Note that the GCC warnings are not suppressed if we only wrap the *intrin.h -
// the warning seems to be issued at the call site of intrinsics, i.e. our code.
HWY_DIAGNOSTICS(pop)
