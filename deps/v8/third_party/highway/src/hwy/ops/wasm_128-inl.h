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

// 128-bit WASM vectors and operations.
// External include guard in highway.h - see comment there.

#include <wasm_simd128.h>

#include "hwy/base.h"
#include "hwy/ops/shared-inl.h"

#ifdef HWY_WASM_OLD_NAMES
#define wasm_i8x16_shuffle wasm_v8x16_shuffle
#define wasm_i16x8_shuffle wasm_v16x8_shuffle
#define wasm_i32x4_shuffle wasm_v32x4_shuffle
#define wasm_i64x2_shuffle wasm_v64x2_shuffle
#define wasm_u16x8_extend_low_u8x16 wasm_i16x8_widen_low_u8x16
#define wasm_u32x4_extend_low_u16x8 wasm_i32x4_widen_low_u16x8
#define wasm_i32x4_extend_low_i16x8 wasm_i32x4_widen_low_i16x8
#define wasm_i16x8_extend_low_i8x16 wasm_i16x8_widen_low_i8x16
#define wasm_u32x4_extend_high_u16x8 wasm_i32x4_widen_high_u16x8
#define wasm_i32x4_extend_high_i16x8 wasm_i32x4_widen_high_i16x8
#define wasm_i32x4_trunc_sat_f32x4 wasm_i32x4_trunc_saturate_f32x4
#define wasm_i62x2_trunc_sat_f64x2 wasm_i64x2_trunc_saturate_f64x2
#define wasm_u8x16_add_sat wasm_u8x16_add_saturate
#define wasm_u8x16_sub_sat wasm_u8x16_sub_saturate
#define wasm_u16x8_add_sat wasm_u16x8_add_saturate
#define wasm_u16x8_sub_sat wasm_u16x8_sub_saturate
#define wasm_i8x16_add_sat wasm_i8x16_add_saturate
#define wasm_i8x16_sub_sat wasm_i8x16_sub_saturate
#define wasm_i16x8_add_sat wasm_i16x8_add_saturate
#define wasm_i16x8_sub_sat wasm_i16x8_sub_saturate
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

#if HWY_TARGET == HWY_WASM_EMU256
template <typename T>
using Full256 = Simd<T, 32 / sizeof(T), 0>;
#endif

namespace detail {

template <typename T>
struct Raw128 {
  using type = __v128_u;
};
template <>
struct Raw128<float> {
  using type = __f32x4;
};
template <>
struct Raw128<double> {
  using type = __f64x2;
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

// FF..FF or 0.
template <typename T, size_t N = 16 / sizeof(T)>
struct Mask128 {
  using PrivateT = T;                     // only for DFromM
  static constexpr size_t kPrivateN = N;  // only for DFromM

  typename detail::Raw128<T>::type raw;
};

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class M>
using DFromM = Simd<typename M::PrivateT, M::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ Zero

// Use HWY_MAX_LANES_D here because VFromD is defined in terms of Zero.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_NOT_FLOAT3264_D(D)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<TFromD<D>, HWY_MAX_LANES_D(D)>{wasm_i32x4_splat(0)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<TFromD<D>, HWY_MAX_LANES_D(D)>{wasm_f32x4_splat(0.0f)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  return Vec128<TFromD<D>, HWY_MAX_LANES_D(D)>{wasm_f64x2_splat(0.0)};
}

template <class D>
using VFromD = decltype(Zero(D()));

// ------------------------------ BitCast

namespace detail {

HWY_INLINE __v128_u BitCastToInteger(__v128_u v) { return v; }
HWY_INLINE __v128_u BitCastToInteger(__f32x4 v) {
  return static_cast<__v128_u>(v);
}
HWY_INLINE __v128_u BitCastToInteger(__f64x2 v) {
  return static_cast<__v128_u>(v);
}

template <typename T, size_t N>
HWY_INLINE Vec128<uint8_t, N * sizeof(T)> BitCastToByte(Vec128<T, N> v) {
  return Vec128<uint8_t, N * sizeof(T)>{BitCastToInteger(v.raw)};
}

// Cannot rely on function overloading because return types differ.
template <typename T>
struct BitCastFromInteger128 {
  HWY_INLINE __v128_u operator()(__v128_u v) { return v; }
};
template <>
struct BitCastFromInteger128<float> {
  HWY_INLINE __f32x4 operator()(__v128_u v) { return static_cast<__f32x4>(v); }
};
template <>
struct BitCastFromInteger128<double> {
  HWY_INLINE __f64x2 operator()(__v128_u v) { return static_cast<__f64x2>(v); }
};

template <class D>
HWY_INLINE VFromD<D> BitCastFromByte(D d, Vec128<uint8_t, d.MaxBytes()> v) {
  return VFromD<D>{BitCastFromInteger128<TFromD<D>>()(v.raw)};
}

}  // namespace detail

template <class D, typename FromT>
HWY_API VFromD<D> BitCast(D d,
                          Vec128<FromT, Repartition<FromT, D>().MaxLanes()> v) {
  return detail::BitCastFromByte(d, detail::BitCastToByte(v));
}

// ------------------------------ ResizeBitCast

template <class D, typename FromV, HWY_IF_V_SIZE_LE_V(FromV, 16),
          HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ResizeBitCast(D d, FromV v) {
  const Repartition<uint8_t, decltype(d)> du8_to;
  return BitCast(d, VFromD<decltype(du8_to)>{detail::BitCastToInteger(v.raw)});
}

// ------------------------------ Set

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_i8x16_splat(static_cast<int8_t>(t))};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI16_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_i16x8_splat(static_cast<int16_t>(t))};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_i32x4_splat(static_cast<int32_t>(t))};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_i64x2_splat(static_cast<int64_t>(t))};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_SPECIAL_FLOAT_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_i16x8_splat(BitCastScalar<int16_t>(t))};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_f32x4_splat(t)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  return VFromD<D>{wasm_f64x2_splat(t)};
}

HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wuninitialized")

// For all vector sizes.
template <class D>
HWY_API VFromD<D> Undefined(D d) {
  return Zero(d);
}

HWY_DIAGNOSTICS(pop)

// For all vector sizes.
template <class D, typename T = TFromD<D>, typename T2>
HWY_API VFromD<D> Iota(D d, const T2 first) {
  HWY_ALIGN T lanes[MaxLanes(d)];
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    lanes[i] = AddWithWraparound(static_cast<T>(first), i);
  }
  return Load(d, lanes);
}

// ------------------------------ Dup128VecFromValues
template <class D, HWY_IF_I8_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  return VFromD<D>{wasm_i8x16_make(t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10,
                                   t11, t12, t13, t14, t15)};
}

template <class D, HWY_IF_U8_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  return VFromD<D>{wasm_u8x16_make(t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10,
                                   t11, t12, t13, t14, t15)};
}

template <class D, HWY_IF_I16_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  return VFromD<D>{wasm_i16x8_make(t0, t1, t2, t3, t4, t5, t6, t7)};
}

template <class D, HWY_IF_U16_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  return VFromD<D>{wasm_u16x8_make(t0, t1, t2, t3, t4, t5, t6, t7)};
}

template <class D, HWY_IF_SPECIAL_FLOAT_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
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

template <class D, HWY_IF_I32_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return VFromD<D>{wasm_i32x4_make(t0, t1, t2, t3)};
}

template <class D, HWY_IF_U32_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return VFromD<D>{wasm_u32x4_make(t0, t1, t2, t3)};
}

template <class D, HWY_IF_F32_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  return VFromD<D>{wasm_f32x4_make(t0, t1, t2, t3)};
}

template <class D, HWY_IF_I64_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return VFromD<D>{wasm_i64x2_make(t0, t1)};
}

template <class D, HWY_IF_U64_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return VFromD<D>{wasm_u64x2_make(t0, t1)};
}

template <class D, HWY_IF_F64_D(D), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  return VFromD<D>{wasm_f64x2_make(t0, t1)};
}

// ================================================== ARITHMETIC

// ------------------------------ Addition

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> operator+(const Vec128<uint8_t, N> a,
                                     const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_i8x16_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> operator+(const Vec128<uint16_t, N> a,
                                      const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_i16x8_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> operator+(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_i32x4_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> operator+(const Vec128<uint64_t, N> a,
                                      const Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{wasm_i64x2_add(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> operator+(const Vec128<int8_t, N> a,
                                    const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> operator+(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> operator+(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> operator+(const Vec128<int64_t, N> a,
                                     const Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{wasm_i64x2_add(a.raw, b.raw)};
}

// Float
template <size_t N>
HWY_API Vec128<float, N> operator+(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{wasm_f32x4_add(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator+(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{wasm_f64x2_add(a.raw, b.raw)};
}

// ------------------------------ Subtraction

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> operator-(const Vec128<uint8_t, N> a,
                                     const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_i8x16_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> operator-(Vec128<uint16_t, N> a,
                                      Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_i16x8_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> operator-(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_i32x4_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> operator-(const Vec128<uint64_t, N> a,
                                      const Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{wasm_i64x2_sub(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> operator-(const Vec128<int8_t, N> a,
                                    const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> operator-(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> operator-(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> operator-(const Vec128<int64_t, N> a,
                                     const Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{wasm_i64x2_sub(a.raw, b.raw)};
}

// Float
template <size_t N>
HWY_API Vec128<float, N> operator-(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{wasm_f32x4_sub(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator-(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{wasm_f64x2_sub(a.raw, b.raw)};
}

// ------------------------------ SaturatedAdd

// Returns a + b clamped to the destination range.

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> SaturatedAdd(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_u8x16_add_sat(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> SaturatedAdd(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_u16x8_add_sat(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> SaturatedAdd(const Vec128<int8_t, N> a,
                                       const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_add_sat(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> SaturatedAdd(const Vec128<int16_t, N> a,
                                        const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_add_sat(a.raw, b.raw)};
}

// ------------------------------ SaturatedSub

// Returns a - b clamped to the destination range.

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> SaturatedSub(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_u8x16_sub_sat(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> SaturatedSub(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_u16x8_sub_sat(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> SaturatedSub(const Vec128<int8_t, N> a,
                                       const Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_sub_sat(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> SaturatedSub(const Vec128<int16_t, N> a,
                                        const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_sub_sat(a.raw, b.raw)};
}

// ------------------------------ Average

// Returns (a + b + 1) / 2

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> AverageRound(const Vec128<uint8_t, N> a,
                                        const Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_u8x16_avgr(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> AverageRound(const Vec128<uint16_t, N> a,
                                         const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_u16x8_avgr(a.raw, b.raw)};
}

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

// ------------------------------ Absolute value

// Returns absolute value, except that LimitsMin() maps to LimitsMax() + 1.
template <size_t N>
HWY_API Vec128<int8_t, N> Abs(const Vec128<int8_t, N> v) {
  return Vec128<int8_t, N>{wasm_i8x16_abs(v.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> Abs(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{wasm_i16x8_abs(v.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Abs(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{wasm_i32x4_abs(v.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> Abs(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{wasm_i64x2_abs(v.raw)};
}

template <size_t N>
HWY_API Vec128<float, N> Abs(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_abs(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Abs(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_abs(v.raw)};
}

// ------------------------------ Shift lanes by constant #bits

// Unsigned
template <int kBits, size_t N>
HWY_API Vec128<uint16_t, N> ShiftLeft(const Vec128<uint16_t, N> v) {
  return Vec128<uint16_t, N>{wasm_i16x8_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint16_t, N> ShiftRight(const Vec128<uint16_t, N> v) {
  return Vec128<uint16_t, N>{wasm_u16x8_shr(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint32_t, N> ShiftLeft(const Vec128<uint32_t, N> v) {
  return Vec128<uint32_t, N>{wasm_i32x4_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint64_t, N> ShiftLeft(const Vec128<uint64_t, N> v) {
  return Vec128<uint64_t, N>{wasm_i64x2_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint32_t, N> ShiftRight(const Vec128<uint32_t, N> v) {
  return Vec128<uint32_t, N>{wasm_u32x4_shr(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<uint64_t, N> ShiftRight(const Vec128<uint64_t, N> v) {
  return Vec128<uint64_t, N>{wasm_u64x2_shr(v.raw, kBits)};
}

// Signed
template <int kBits, size_t N>
HWY_API Vec128<int16_t, N> ShiftLeft(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{wasm_i16x8_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int16_t, N> ShiftRight(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{wasm_i16x8_shr(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int32_t, N> ShiftLeft(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{wasm_i32x4_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int64_t, N> ShiftLeft(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{wasm_i64x2_shl(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int32_t, N> ShiftRight(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{wasm_i32x4_shr(v.raw, kBits)};
}
template <int kBits, size_t N>
HWY_API Vec128<int64_t, N> ShiftRight(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{wasm_i64x2_shr(v.raw, kBits)};
}

// 8-bit
template <int kBits, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShiftLeft(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<T, N> shifted{ShiftLeft<kBits>(Vec128<MakeWide<T>>{v.raw}).raw};
  return kBits == 1
             ? (v + v)
             : (shifted & Set(d8, static_cast<T>((0xFF << kBits) & 0xFF)));
}

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

// ------------------------------ Shift lanes by same variable #bits

// After https://reviews.llvm.org/D108415 shift argument became unsigned.
HWY_DIAGNOSTICS(push)
HWY_DIAGNOSTICS_OFF(disable : 4245 4365, ignored "-Wsign-conversion")

// Unsigned
template <size_t N>
HWY_API Vec128<uint16_t, N> ShiftLeftSame(const Vec128<uint16_t, N> v,
                                          const int bits) {
  return Vec128<uint16_t, N>{wasm_i16x8_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> ShiftRightSame(const Vec128<uint16_t, N> v,
                                           const int bits) {
  return Vec128<uint16_t, N>{wasm_u16x8_shr(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> ShiftLeftSame(const Vec128<uint32_t, N> v,
                                          const int bits) {
  return Vec128<uint32_t, N>{wasm_i32x4_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> ShiftRightSame(const Vec128<uint32_t, N> v,
                                           const int bits) {
  return Vec128<uint32_t, N>{wasm_u32x4_shr(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> ShiftLeftSame(const Vec128<uint64_t, N> v,
                                          const int bits) {
  return Vec128<uint64_t, N>{wasm_i64x2_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> ShiftRightSame(const Vec128<uint64_t, N> v,
                                           const int bits) {
  return Vec128<uint64_t, N>{wasm_u64x2_shr(v.raw, bits)};
}

// Signed
template <size_t N>
HWY_API Vec128<int16_t, N> ShiftLeftSame(const Vec128<int16_t, N> v,
                                         const int bits) {
  return Vec128<int16_t, N>{wasm_i16x8_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> ShiftRightSame(const Vec128<int16_t, N> v,
                                          const int bits) {
  return Vec128<int16_t, N>{wasm_i16x8_shr(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> ShiftLeftSame(const Vec128<int32_t, N> v,
                                         const int bits) {
  return Vec128<int32_t, N>{wasm_i32x4_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> ShiftRightSame(const Vec128<int32_t, N> v,
                                          const int bits) {
  return Vec128<int32_t, N>{wasm_i32x4_shr(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> ShiftLeftSame(const Vec128<int64_t, N> v,
                                         const int bits) {
  return Vec128<int64_t, N>{wasm_i64x2_shl(v.raw, bits)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> ShiftRightSame(const Vec128<int64_t, N> v,
                                          const int bits) {
  return Vec128<int64_t, N>{wasm_i64x2_shr(v.raw, bits)};
}

// 8-bit
template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShiftLeftSame(const Vec128<T, N> v, const int bits) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<T, N> shifted{
      ShiftLeftSame(Vec128<MakeWide<T>>{v.raw}, bits).raw};
  return shifted & Set(d8, static_cast<T>((0xFF << bits) & 0xFF));
}

template <size_t N>
HWY_API Vec128<uint8_t, N> ShiftRightSame(Vec128<uint8_t, N> v,
                                          const int bits) {
  const DFromV<decltype(v)> d8;
  // Use raw instead of BitCast to support N=1.
  const Vec128<uint8_t, N> shifted{
      ShiftRightSame(Vec128<uint16_t>{v.raw}, bits).raw};
  return shifted & Set(d8, 0xFF >> bits);
}

template <size_t N>
HWY_API Vec128<int8_t, N> ShiftRightSame(Vec128<int8_t, N> v, const int bits) {
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  const auto shifted = BitCast(di, ShiftRightSame(BitCast(du, v), bits));
  const auto shifted_sign = BitCast(di, Set(du, 0x80 >> bits));
  return (shifted ^ shifted_sign) - shifted_sign;
}

// ignore Wsign-conversion
HWY_DIAGNOSTICS(pop)

// ------------------------------ Minimum

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> Min(Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_u8x16_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> Min(Vec128<uint16_t, N> a, Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_u16x8_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> Min(Vec128<uint32_t, N> a, Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_u32x4_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> Min(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
  // Avoid wasm_u64x2_extract_lane - not all implementations have it yet.
  const uint64_t a0 = static_cast<uint64_t>(wasm_i64x2_extract_lane(a.raw, 0));
  const uint64_t b0 = static_cast<uint64_t>(wasm_i64x2_extract_lane(b.raw, 0));
  const uint64_t a1 = static_cast<uint64_t>(wasm_i64x2_extract_lane(a.raw, 1));
  const uint64_t b1 = static_cast<uint64_t>(wasm_i64x2_extract_lane(b.raw, 1));
  alignas(16) uint64_t min[2] = {HWY_MIN(a0, b0), HWY_MIN(a1, b1)};
  return Vec128<uint64_t, N>{wasm_v128_load(min)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> Min(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> Min(Vec128<int16_t, N> a, Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Min(Vec128<int32_t, N> a, Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_min(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> Min(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
  alignas(16) int64_t min[4];
  min[0] = HWY_MIN(wasm_i64x2_extract_lane(a.raw, 0),
                   wasm_i64x2_extract_lane(b.raw, 0));
  min[1] = HWY_MIN(wasm_i64x2_extract_lane(a.raw, 1),
                   wasm_i64x2_extract_lane(b.raw, 1));
  return Vec128<int64_t, N>{wasm_v128_load(min)};
}

// Float
template <size_t N>
HWY_API Vec128<float, N> Min(Vec128<float, N> a, Vec128<float, N> b) {
  // Equivalent to a < b ? a : b (taking into account our swapped arg order,
  // so that Min(NaN, x) is x to match x86).
  return Vec128<float, N>{wasm_f32x4_pmin(b.raw, a.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Min(Vec128<double, N> a, Vec128<double, N> b) {
  // Equivalent to a < b ? a : b (taking into account our swapped arg order,
  // so that Min(NaN, x) is x to match x86).
  return Vec128<double, N>{wasm_f64x2_pmin(b.raw, a.raw)};
}

// ------------------------------ Maximum

// Unsigned
template <size_t N>
HWY_API Vec128<uint8_t, N> Max(Vec128<uint8_t, N> a, Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_u8x16_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> Max(Vec128<uint16_t, N> a, Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_u16x8_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> Max(Vec128<uint32_t, N> a, Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_u32x4_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> Max(Vec128<uint64_t, N> a, Vec128<uint64_t, N> b) {
  // Avoid wasm_u64x2_extract_lane - not all implementations have it yet.
  const uint64_t a0 = static_cast<uint64_t>(wasm_i64x2_extract_lane(a.raw, 0));
  const uint64_t b0 = static_cast<uint64_t>(wasm_i64x2_extract_lane(b.raw, 0));
  const uint64_t a1 = static_cast<uint64_t>(wasm_i64x2_extract_lane(a.raw, 1));
  const uint64_t b1 = static_cast<uint64_t>(wasm_i64x2_extract_lane(b.raw, 1));
  alignas(16) uint64_t max[2] = {HWY_MAX(a0, b0), HWY_MAX(a1, b1)};
  return Vec128<uint64_t, N>{wasm_v128_load(max)};
}

// Signed
template <size_t N>
HWY_API Vec128<int8_t, N> Max(Vec128<int8_t, N> a, Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> Max(Vec128<int16_t, N> a, Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Max(Vec128<int32_t, N> a, Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_max(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> Max(Vec128<int64_t, N> a, Vec128<int64_t, N> b) {
  alignas(16) int64_t max[2];
  max[0] = HWY_MAX(wasm_i64x2_extract_lane(a.raw, 0),
                   wasm_i64x2_extract_lane(b.raw, 0));
  max[1] = HWY_MAX(wasm_i64x2_extract_lane(a.raw, 1),
                   wasm_i64x2_extract_lane(b.raw, 1));
  return Vec128<int64_t, N>{wasm_v128_load(max)};
}

// Float
template <size_t N>
HWY_API Vec128<float, N> Max(Vec128<float, N> a, Vec128<float, N> b) {
  // Equivalent to b < a ? a : b (taking into account our swapped arg order,
  // so that Max(NaN, x) is x to match x86).
  return Vec128<float, N>{wasm_f32x4_pmax(b.raw, a.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Max(Vec128<double, N> a, Vec128<double, N> b) {
  // Equivalent to b < a ? a : b (taking into account our swapped arg order,
  // so that Max(NaN, x) is x to match x86).
  return Vec128<double, N>{wasm_f64x2_pmax(b.raw, a.raw)};
}

// ------------------------------ Integer multiplication

// Unsigned
template <size_t N>
HWY_API Vec128<uint16_t, N> operator*(const Vec128<uint16_t, N> a,
                                      const Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{wasm_i16x8_mul(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> operator*(const Vec128<uint32_t, N> a,
                                      const Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_i32x4_mul(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Vec128<int16_t, N> operator*(const Vec128<int16_t, N> a,
                                     const Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_mul(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> operator*(const Vec128<int32_t, N> a,
                                     const Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_mul(a.raw, b.raw)};
}

// Returns the upper sizeof(T)*8 bits of a * b in each lane.
template <size_t N>
HWY_API Vec128<uint8_t, N> MulHigh(const Vec128<uint8_t, N> a,
                                   const Vec128<uint8_t, N> b) {
  const auto l = wasm_u16x8_extmul_low_u8x16(a.raw, b.raw);
  const auto h = wasm_u16x8_extmul_high_u8x16(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<uint8_t, N>{wasm_i8x16_shuffle(l, h, 1, 3, 5, 7, 9, 11, 13, 15,
                                               17, 19, 21, 23, 25, 27, 29, 31)};
}
template <size_t N>
HWY_API Vec128<int8_t, N> MulHigh(const Vec128<int8_t, N> a,
                                  const Vec128<int8_t, N> b) {
  const auto l = wasm_i16x8_extmul_low_i8x16(a.raw, b.raw);
  const auto h = wasm_i16x8_extmul_high_i8x16(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<int8_t, N>{wasm_i8x16_shuffle(l, h, 1, 3, 5, 7, 9, 11, 13, 15,
                                              17, 19, 21, 23, 25, 27, 29, 31)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> MulHigh(const Vec128<uint16_t, N> a,
                                    const Vec128<uint16_t, N> b) {
  const auto l = wasm_u32x4_extmul_low_u16x8(a.raw, b.raw);
  const auto h = wasm_u32x4_extmul_high_u16x8(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<uint16_t, N>{
      wasm_i16x8_shuffle(l, h, 1, 3, 5, 7, 9, 11, 13, 15)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> MulHigh(const Vec128<int16_t, N> a,
                                   const Vec128<int16_t, N> b) {
  const auto l = wasm_i32x4_extmul_low_i16x8(a.raw, b.raw);
  const auto h = wasm_i32x4_extmul_high_i16x8(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<int16_t, N>{
      wasm_i16x8_shuffle(l, h, 1, 3, 5, 7, 9, 11, 13, 15)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> MulHigh(const Vec128<uint32_t, N> a,
                                    const Vec128<uint32_t, N> b) {
  const auto l = wasm_u64x2_extmul_low_u32x4(a.raw, b.raw);
  const auto h = wasm_u64x2_extmul_high_u32x4(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<uint32_t, N>{wasm_i32x4_shuffle(l, h, 1, 3, 5, 7)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> MulHigh(const Vec128<int32_t, N> a,
                                   const Vec128<int32_t, N> b) {
  const auto l = wasm_i64x2_extmul_low_i32x4(a.raw, b.raw);
  const auto h = wasm_i64x2_extmul_high_i32x4(a.raw, b.raw);
  // TODO(eustas): shift-right + narrow?
  return Vec128<int32_t, N>{wasm_i32x4_shuffle(l, h, 1, 3, 5, 7)};
}

template <size_t N>
HWY_API Vec128<int16_t, N> MulFixedPoint15(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{wasm_i16x8_q15mulr_sat(a.raw, b.raw)};
}

// Multiplies even lanes (0, 2 ..) and returns the double-width result.
template <class T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_SIGNED(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulEven(const Vec128<T, N> a,
                                                 const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  constexpr int kSrcBits = sizeof(T) * 8;

  const auto ae =
      ShiftRight<kSrcBits>(ShiftLeft<kSrcBits>(ResizeBitCast(dw, a)));
  const auto be =
      ShiftRight<kSrcBits>(ShiftLeft<kSrcBits>(ResizeBitCast(dw, b)));
  return ae * be;
}
template <class T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_UNSIGNED(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulEven(const Vec128<T, N> a,
                                                 const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const auto kEvenMask = Set(dw, LimitsMax<T>());

  const auto ae = And(ResizeBitCast(dw, a), kEvenMask);
  const auto be = And(ResizeBitCast(dw, b), kEvenMask);
  return ae * be;
}
template <size_t N>
HWY_API Vec128<int64_t, (N + 1) / 2> MulEven(const Vec128<int32_t, N> a,
                                             const Vec128<int32_t, N> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  const auto ae = ShiftRight<32>(ShiftLeft<32>(ResizeBitCast(dw, a))).raw;
  const auto be = ShiftRight<32>(ShiftLeft<32>(ResizeBitCast(dw, b))).raw;
  return Vec128<int64_t, (N + 1) / 2>{wasm_i64x2_mul(ae, be)};
}
template <size_t N>
HWY_API Vec128<uint64_t, (N + 1) / 2> MulEven(const Vec128<uint32_t, N> a,
                                              const Vec128<uint32_t, N> b) {
  const auto kEvenMask = wasm_i32x4_make(-1, 0, -1, 0);
  const auto ae = wasm_v128_and(a.raw, kEvenMask);
  const auto be = wasm_v128_and(b.raw, kEvenMask);
  return Vec128<uint64_t, (N + 1) / 2>{wasm_i64x2_mul(ae, be)};
}

// Multiplies odd lanes (1, 3 ..) and returns the double-width result.
template <class T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulOdd(const Vec128<T, N> a,
                                                const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  constexpr int kSrcBits = sizeof(T) * 8;

  const auto ao = ShiftRight<kSrcBits>(BitCast(dw, a));
  const auto bo = ShiftRight<kSrcBits>(BitCast(dw, b));
  return ao * bo;
}
template <class T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulOdd(const Vec128<T, N> a,
                                                const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;

  const auto ao = ShiftRight<32>(BitCast(dw, a));
  const auto bo = ShiftRight<32>(BitCast(dw, b));
  return Vec128<MakeWide<T>, (N + 1) / 2>{wasm_i64x2_mul(ao.raw, bo.raw)};
}

// ------------------------------ Negate

template <typename T, size_t N, HWY_IF_FLOAT_OR_SPECIAL(T)>
HWY_API Vec128<T, N> Neg(const Vec128<T, N> v) {
  return Xor(v, SignBit(DFromV<decltype(v)>()));
}

template <size_t N>
HWY_API Vec128<int8_t, N> Neg(const Vec128<int8_t, N> v) {
  return Vec128<int8_t, N>{wasm_i8x16_neg(v.raw)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> Neg(const Vec128<int16_t, N> v) {
  return Vec128<int16_t, N>{wasm_i16x8_neg(v.raw)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> Neg(const Vec128<int32_t, N> v) {
  return Vec128<int32_t, N>{wasm_i32x4_neg(v.raw)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> Neg(const Vec128<int64_t, N> v) {
  return Vec128<int64_t, N>{wasm_i64x2_neg(v.raw)};
}

// ------------------------------ Floating-point mul / div

template <size_t N>
HWY_API Vec128<float, N> operator*(Vec128<float, N> a, Vec128<float, N> b) {
  return Vec128<float, N>{wasm_f32x4_mul(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator*(Vec128<double, N> a, Vec128<double, N> b) {
  return Vec128<double, N>{wasm_f64x2_mul(a.raw, b.raw)};
}

template <size_t N>
HWY_API Vec128<float, N> operator/(const Vec128<float, N> a,
                                   const Vec128<float, N> b) {
  return Vec128<float, N>{wasm_f32x4_div(a.raw, b.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> operator/(const Vec128<double, N> a,
                                    const Vec128<double, N> b) {
  return Vec128<double, N>{wasm_f64x2_div(a.raw, b.raw)};
}

template <class V, HWY_IF_F32(TFromV<V>)>
HWY_API V ApproximateReciprocal(const V v) {
  return Set(DFromV<decltype(v)>(), 1.0f) / v;
}

// Integer overload defined in generic_ops-inl.h.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> AbsDiff(const Vec128<T, N> a, const Vec128<T, N> b) {
  return Abs(a - b);
}

// ------------------------------ Floating-point multiply-add variants

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> add) {
  return mul * x + add;
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> add) {
  return add - mul * x;
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulSub(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> sub) {
  return mul * x - sub;
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulSub(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> sub) {
  return Neg(mul) * x - sub;
}

// ------------------------------ Floating-point square root

// Full precision square root
template <size_t N>
HWY_API Vec128<float, N> Sqrt(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_sqrt(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Sqrt(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_sqrt(v.raw)};
}

// Approximate reciprocal square root
template <class V, HWY_IF_F32(TFromV<V>)>
HWY_API V ApproximateReciprocalSqrt(V v) {
  // TODO(eustas): find cheaper a way to calculate this.
  return Set(DFromV<decltype(v)>(), static_cast<TFromV<V>>(1.0)) / Sqrt(v);
}

// ------------------------------ Floating-point rounding

// Toward nearest integer, ties to even
template <size_t N>
HWY_API Vec128<float, N> Round(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_nearest(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Round(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_nearest(v.raw)};
}

// Toward zero, aka truncate
template <size_t N>
HWY_API Vec128<float, N> Trunc(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_trunc(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Trunc(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_trunc(v.raw)};
}

// Toward +infinity, aka ceiling
template <size_t N>
HWY_API Vec128<float, N> Ceil(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_ceil(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Ceil(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_ceil(v.raw)};
}

// Toward -infinity, aka floor
template <size_t N>
HWY_API Vec128<float, N> Floor(const Vec128<float, N> v) {
  return Vec128<float, N>{wasm_f32x4_floor(v.raw)};
}
template <size_t N>
HWY_API Vec128<double, N> Floor(const Vec128<double, N> v) {
  return Vec128<double, N>{wasm_f64x2_floor(v.raw)};
}

// ------------------------------ Floating-point classification
template <typename T, size_t N>
HWY_API Mask128<T, N> IsNaN(const Vec128<T, N> v) {
  return v != v;
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Mask128<T, N> IsInf(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, check for exponent=max and mantissa=0.
  return RebindMask(d, Eq(Add(vu, vu), Set(du, hwy::MaxExponentTimes2<T>())));
}

// Returns whether normal/subnormal/zero.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Mask128<T, N> IsFinite(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const RebindToSigned<decltype(d)> di;  // cheaper than unsigned comparison
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, then right so we can compare with the
  // max exponent (cannot compare with MaxExponentTimes2 directly because it is
  // negative and non-negative floats would be greater).
  const VFromD<decltype(di)> exp =
      BitCast(di, ShiftRight<hwy::MantissaBits<T>() + 1>(Add(vu, vu)));
  return RebindMask(d, Lt(exp, Set(di, hwy::MaxExponentField<T>())));
}

// ================================================== COMPARE

// Comparisons fill a lane with 1-bits if the condition is true, else 0.

// Mask and Vec are the same (true = FF..FF).
template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(const Vec128<T, N> v) {
  return Mask128<T, N>{v.raw};
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <typename TFrom, size_t NFrom, class DTo>
HWY_API MFromD<DTo> RebindMask(DTo /* tag */, Mask128<TFrom, NFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TFromD<DTo>), "Must have same size");
  return MFromD<DTo>{m.raw};
}

template <typename T, size_t N>
HWY_API Mask128<T, N> TestBit(Vec128<T, N> v, Vec128<T, N> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return (v & bit) == bit;
}

// ------------------------------ Equality

// Unsigned
template <size_t N>
HWY_API Mask128<uint8_t, N> operator==(const Vec128<uint8_t, N> a,
                                       const Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{wasm_i8x16_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator==(const Vec128<uint16_t, N> a,
                                        const Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{wasm_i16x8_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator==(const Vec128<uint32_t, N> a,
                                        const Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{wasm_i32x4_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator==(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
  return Mask128<uint64_t, N>{wasm_i64x2_eq(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Mask128<int8_t, N> operator==(const Vec128<int8_t, N> a,
                                      const Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{wasm_i8x16_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator==(Vec128<int16_t, N> a,
                                       Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{wasm_i16x8_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator==(const Vec128<int32_t, N> a,
                                       const Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{wasm_i32x4_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator==(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{wasm_i64x2_eq(a.raw, b.raw)};
}

// Float
template <size_t N>
HWY_API Mask128<float, N> operator==(const Vec128<float, N> a,
                                     const Vec128<float, N> b) {
  return Mask128<float, N>{wasm_f32x4_eq(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator==(const Vec128<double, N> a,
                                      const Vec128<double, N> b) {
  return Mask128<double, N>{wasm_f64x2_eq(a.raw, b.raw)};
}

// ------------------------------ Inequality

// Unsigned
template <size_t N>
HWY_API Mask128<uint8_t, N> operator!=(const Vec128<uint8_t, N> a,
                                       const Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{wasm_i8x16_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator!=(const Vec128<uint16_t, N> a,
                                        const Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{wasm_i16x8_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator!=(const Vec128<uint32_t, N> a,
                                        const Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{wasm_i32x4_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator!=(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
  return Mask128<uint64_t, N>{wasm_i64x2_ne(a.raw, b.raw)};
}

// Signed
template <size_t N>
HWY_API Mask128<int8_t, N> operator!=(const Vec128<int8_t, N> a,
                                      const Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{wasm_i8x16_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator!=(const Vec128<int16_t, N> a,
                                       const Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{wasm_i16x8_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator!=(const Vec128<int32_t, N> a,
                                       const Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{wasm_i32x4_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator!=(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{wasm_i64x2_ne(a.raw, b.raw)};
}

// Float
template <size_t N>
HWY_API Mask128<float, N> operator!=(const Vec128<float, N> a,
                                     const Vec128<float, N> b) {
  return Mask128<float, N>{wasm_f32x4_ne(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator!=(const Vec128<double, N> a,
                                      const Vec128<double, N> b) {
  return Mask128<double, N>{wasm_f64x2_ne(a.raw, b.raw)};
}

// ------------------------------ Strict inequality

template <size_t N>
HWY_API Mask128<int8_t, N> operator>(const Vec128<int8_t, N> a,
                                     const Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{wasm_i8x16_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator>(const Vec128<int16_t, N> a,
                                      const Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{wasm_i16x8_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator>(const Vec128<int32_t, N> a,
                                      const Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{wasm_i32x4_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator>(const Vec128<int64_t, N> a,
                                      const Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{wasm_i64x2_gt(a.raw, b.raw)};
}

template <size_t N>
HWY_API Mask128<uint8_t, N> operator>(const Vec128<uint8_t, N> a,
                                      const Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{wasm_u8x16_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator>(const Vec128<uint16_t, N> a,
                                       const Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{wasm_u16x8_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator>(const Vec128<uint32_t, N> a,
                                       const Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{wasm_u32x4_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator>(const Vec128<uint64_t, N> a,
                                       const Vec128<uint64_t, N> b) {
  const DFromV<decltype(a)> d;
  const Repartition<uint32_t, decltype(d)> d32;
  const auto a32 = BitCast(d32, a);
  const auto b32 = BitCast(d32, b);
  // If the upper halves are not equal, this is the answer.
  const auto m_gt = a32 > b32;

  // Otherwise, the lower half decides.
  const auto m_eq = a32 == b32;
  const auto lo_in_hi = wasm_i32x4_shuffle(m_gt.raw, m_gt.raw, 0, 0, 2, 2);
  const auto lo_gt = And(m_eq, MaskFromVec(VFromD<decltype(d32)>{lo_in_hi}));

  const auto gt = Or(lo_gt, m_gt);
  // Copy result in upper 32 bits to lower 32 bits.
  return Mask128<uint64_t, N>{wasm_i32x4_shuffle(gt.raw, gt.raw, 1, 1, 3, 3)};
}

template <size_t N>
HWY_API Mask128<float, N> operator>(const Vec128<float, N> a,
                                    const Vec128<float, N> b) {
  return Mask128<float, N>{wasm_f32x4_gt(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator>(const Vec128<double, N> a,
                                     const Vec128<double, N> b) {
  return Mask128<double, N>{wasm_f64x2_gt(a.raw, b.raw)};
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<(const Vec128<T, N> a, const Vec128<T, N> b) {
  return operator>(b, a);
}

// ------------------------------ Weak inequality

// Float >=
template <size_t N>
HWY_API Mask128<float, N> operator>=(const Vec128<float, N> a,
                                     const Vec128<float, N> b) {
  return Mask128<float, N>{wasm_f32x4_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<double, N> operator>=(const Vec128<double, N> a,
                                      const Vec128<double, N> b) {
  return Mask128<double, N>{wasm_f64x2_ge(a.raw, b.raw)};
}

template <size_t N>
HWY_API Mask128<int8_t, N> operator>=(const Vec128<int8_t, N> a,
                                      const Vec128<int8_t, N> b) {
  return Mask128<int8_t, N>{wasm_i8x16_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator>=(const Vec128<int16_t, N> a,
                                       const Vec128<int16_t, N> b) {
  return Mask128<int16_t, N>{wasm_i16x8_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator>=(const Vec128<int32_t, N> a,
                                       const Vec128<int32_t, N> b) {
  return Mask128<int32_t, N>{wasm_i32x4_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator>=(const Vec128<int64_t, N> a,
                                       const Vec128<int64_t, N> b) {
  return Mask128<int64_t, N>{wasm_i64x2_ge(a.raw, b.raw)};
}

template <size_t N>
HWY_API Mask128<uint8_t, N> operator>=(const Vec128<uint8_t, N> a,
                                       const Vec128<uint8_t, N> b) {
  return Mask128<uint8_t, N>{wasm_u8x16_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator>=(const Vec128<uint16_t, N> a,
                                        const Vec128<uint16_t, N> b) {
  return Mask128<uint16_t, N>{wasm_u16x8_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator>=(const Vec128<uint32_t, N> a,
                                        const Vec128<uint32_t, N> b) {
  return Mask128<uint32_t, N>{wasm_u32x4_ge(a.raw, b.raw)};
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator>=(const Vec128<uint64_t, N> a,
                                        const Vec128<uint64_t, N> b) {
  return Not(b > a);
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<=(const Vec128<T, N> a, const Vec128<T, N> b) {
  return operator>=(b, a);
}

// ------------------------------ FirstN (Iota, Lt)

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> FirstN(D d, size_t num) {
  const RebindToSigned<decltype(d)> di;  // Signed comparisons may be cheaper.
  using TI = TFromD<decltype(di)>;
  return RebindMask(d, Iota(di, 0) < Set(di, static_cast<TI>(num)));
}

// ================================================== LOGICAL

// ------------------------------ Not

template <typename T, size_t N>
HWY_API Vec128<T, N> Not(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_v128_not(v.raw)};
}

// ------------------------------ And

template <typename T, size_t N>
HWY_API Vec128<T, N> And(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{wasm_v128_and(a.raw, b.raw)};
}

// ------------------------------ AndNot

// Returns ~not_mask & mask.
template <typename T, size_t N>
HWY_API Vec128<T, N> AndNot(Vec128<T, N> not_mask, Vec128<T, N> mask) {
  return Vec128<T, N>{wasm_v128_andnot(mask.raw, not_mask.raw)};
}

// ------------------------------ Or

template <typename T, size_t N>
HWY_API Vec128<T, N> Or(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{wasm_v128_or(a.raw, b.raw)};
}

// ------------------------------ Xor

template <typename T, size_t N>
HWY_API Vec128<T, N> Xor(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{wasm_v128_xor(a.raw, b.raw)};
}

// ------------------------------ Xor3

template <typename T, size_t N>
HWY_API Vec128<T, N> Xor3(Vec128<T, N> x1, Vec128<T, N> x2, Vec128<T, N> x3) {
  return Xor(x1, Xor(x2, x3));
}

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

// ------------------------------ CopySign
template <typename T, size_t N>
HWY_API Vec128<T, N> CopySign(const Vec128<T, N> magn,
                              const Vec128<T, N> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const DFromV<decltype(magn)> d;
  return BitwiseIfThenElse(SignBit(d), sign, magn);
}

// ------------------------------ CopySignToAbs
template <typename T, size_t N>
HWY_API Vec128<T, N> CopySignToAbs(const Vec128<T, N> abs,
                                   const Vec128<T, N> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const DFromV<decltype(abs)> d;
  return OrAnd(abs, SignBit(d), sign);
}

// ------------------------------ BroadcastSignBit (compare)

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_API Vec128<T, N> BroadcastSignBit(const Vec128<T, N> v) {
  return ShiftRight<sizeof(T) * 8 - 1>(v);
}
template <size_t N>
HWY_API Vec128<int8_t, N> BroadcastSignBit(const Vec128<int8_t, N> v) {
  const DFromV<decltype(v)> d;
  return VecFromMask(d, v < Zero(d));
}

// ------------------------------ Mask

template <class D>
HWY_API VFromD<D> VecFromMask(D /* tag */, MFromD<D> v) {
  return VFromD<D>{v.raw};
}

// mask ? yes : no
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  return Vec128<T, N>{wasm_v128_bitselect(yes.raw, no.raw, mask.raw)};
}

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

template <typename T, size_t N>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  static_assert(IsSigned<T>(), "Only works for signed/float");
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;

  v = BitCast(d, BroadcastSignBit(BitCast(di, v)));
  return IfThenElse(MaskFromVec(v), yes, no);
}

// ------------------------------ Mask logical

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(const Mask128<T, N> m) {
  const DFromM<decltype(m)> d;
  return MaskFromVec(Not(VecFromMask(d, m)));
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

// ------------------------------ Shl (BroadcastSignBit, IfThenElse)

// The x86 multiply-by-Pow2() trick will not work because WASM saturates
// float->int correctly to 2^31-1 (not 2^31). Because WASM's shifts take a
// scalar count operand, per-lane shift instructions would require extract_lane
// for each lane, and hoping that shuffle is correctly mapped to a native
// instruction. Using non-vector shifts would incur a store-load forwarding
// stall when loading the result vector. We instead test bits of the shift
// count to "predicate" a shift of the entire vector by a constant.

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<5>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftLeft<1>(v), v);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<12>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<8>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftLeft<1>(v), v);
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<27>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<16>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<8>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftLeft<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftLeft<1>(v), v);
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = MakeUnsigned<T>;
  alignas(16) TU lanes[2] = {};
  alignas(16) TU bits_lanes[2] = {};
  Store(BitCast(du, v), du, lanes);
  Store(BitCast(du, bits), du, bits_lanes);
  lanes[0] <<= (bits_lanes[0] & 63);
  lanes[1] <<= (bits_lanes[1] & 63);
  return BitCast(d, Load(du, lanes));
}

// ------------------------------ Shr (BroadcastSignBit, IfThenElse)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<5>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftRight<1>(v), v);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<12>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<8>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftRight<1>(v), v);
}

template <typename T, size_t N, HWY_IF_UI32(T)>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  Mask128<T, N> mask;
  // Need a signed type for BroadcastSignBit.
  auto test = BitCast(RebindToSigned<decltype(d)>(), bits);
  // Move the highest valid bit of the shift count into the sign bit.
  test = ShiftLeft<27>(test);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<16>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<8>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<4>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  test = ShiftLeft<1>(test);  // next bit (descending order)
  v = IfThenElse(mask, ShiftRight<2>(v), v);

  mask = RebindMask(d, MaskFromVec(BroadcastSignBit(test)));
  return IfThenElse(mask, ShiftRight<1>(v), v);
}

template <typename T, size_t N, HWY_IF_UI64(T)>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, const Vec128<T, N> bits) {
  const DFromV<decltype(v)> d;
  alignas(16) T lanes[2] = {};
  alignas(16) T bits_lanes[2] = {};
  Store(v, d, lanes);
  Store(bits, d, bits_lanes);
  lanes[0] >>= (bits_lanes[0] & 63);
  lanes[1] >>= (bits_lanes[1] & 63);
  return Load(d, lanes);
}

// ================================================== MEMORY

// ------------------------------ Load

template <class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API Vec128<T> Load(D /* tag */, const T* HWY_RESTRICT aligned) {
  return Vec128<T>{wasm_v128_load(aligned)};
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT p) {
  VFromD<D> v;
  CopyBytes<d.MaxBytes()>(p, &v);
  return v;
}

// LoadU == Load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  return Load(d, p);
}

// 128-bit SIMD => nothing to duplicate, same as an unaligned load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT p) {
  return Load(d, p);
}

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d, const T* HWY_RESTRICT aligned) {
  return IfThenElseZero(m, Load(d, aligned));
}

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const T* HWY_RESTRICT aligned) {
  return IfThenElse(m, Load(d, aligned), v);
}

// ------------------------------ Store

namespace detail {

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  return static_cast<T>(wasm_i8x16_extract_lane(v.raw, kLane));
}
template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2),
          HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  const int16_t lane = wasm_i16x8_extract_lane(v.raw, kLane);
  return static_cast<T>(lane);
}
template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2),
          HWY_IF_SPECIAL_FLOAT(T)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const uint16_t bits = ExtractLane<kLane>(BitCast(du, v));
  return BitCastScalar<T>(bits);
}
template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  return static_cast<T>(wasm_i32x4_extract_lane(v.raw, kLane));
}
template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE T ExtractLane(const Vec128<T, N> v) {
  return static_cast<T>(wasm_i64x2_extract_lane(v.raw, kLane));
}

template <size_t kLane, size_t N>
HWY_INLINE float ExtractLane(const Vec128<float, N> v) {
  return wasm_f32x4_extract_lane(v.raw, kLane);
}
template <size_t kLane, size_t N>
HWY_INLINE double ExtractLane(const Vec128<double, N> v) {
  return wasm_f64x2_extract_lane(v.raw, kLane);
}

}  // namespace detail

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API void Store(VFromD<D> v, D /* tag */, TFromD<D>* HWY_RESTRICT aligned) {
  wasm_v128_store(aligned, v.raw);
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_LANES_GT_D(D, 1)>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  CopyBytes<d.MaxBytes()>(&v, p);
}

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API void Store(VFromD<D> v, D /* tag */, TFromD<D>* HWY_RESTRICT p) {
  *p = detail::ExtractLane<0>(v);
}

// StoreU == Store.
template <class D>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Store(v, d, p);
}

template <class D>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  StoreU(IfThenElse(m, v, LoadU(d, p)), d, p);
}

// ------------------------------ Non-temporal stores

// Same as aligned stores on non-x86.

template <class D>
HWY_API void Stream(VFromD<D> v, D /* tag */, TFromD<D>* HWY_RESTRICT aligned) {
  wasm_v128_store(aligned, v.raw);
}

// ------------------------------ Scatter in generic_ops-inl.h
// ------------------------------ Gather in generic_ops-inl.h

// ================================================== SWIZZLE

// ------------------------------ ExtractLane

// One overload per vector length just in case *_extract_lane raise compile
// errors if their argument is out of bounds (even if that would never be
// reached at runtime).
template <typename T>
HWY_API T ExtractLane(const Vec128<T, 1> v, size_t i) {
  HWY_DASSERT(i == 0);
  (void)i;
  return detail::ExtractLane<0>(v);
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

// ------------------------------ GetLane
template <typename T, size_t N>
HWY_API T GetLane(const Vec128<T, N> v) {
  return detail::ExtractLane<0>(v);
}

// ------------------------------ InsertLane

namespace detail {

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
  return Vec128<T, N>{
      wasm_i8x16_replace_lane(v.raw, kLane, static_cast<int8_t>(t))};
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
  return Vec128<T, N>{
      wasm_i16x8_replace_lane(v.raw, kLane, BitCastScalar<int16_t>(t))};
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
  return Vec128<T, N>{
      wasm_i32x4_replace_lane(v.raw, kLane, static_cast<int32_t>(t))};
}

template <size_t kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE Vec128<T, N> InsertLane(const Vec128<T, N> v, T t) {
  static_assert(kLane < N, "Lane index out of bounds");
  return Vec128<T, N>{
      wasm_i64x2_replace_lane(v.raw, kLane, static_cast<int64_t>(t))};
}

template <size_t kLane, size_t N>
HWY_INLINE Vec128<float, N> InsertLane(const Vec128<float, N> v, float t) {
  static_assert(kLane < N, "Lane index out of bounds");
  return Vec128<float, N>{wasm_f32x4_replace_lane(v.raw, kLane, t)};
}

template <size_t kLane, size_t N>
HWY_INLINE Vec128<double, N> InsertLane(const Vec128<double, N> v, double t) {
  static_assert(kLane < 2, "Lane index out of bounds");
  return Vec128<double, N>{wasm_f64x2_replace_lane(v.raw, kLane, t)};
}

}  // namespace detail

// Requires one overload per vector length because InsertLane<3> may be a
// compile error if it calls wasm_f64x2_replace_lane.

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

// ------------------------------ LowerHalf

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LowerHalf(D /* tag */, VFromD<Twice<D>> v) {
  return VFromD<D>{v.raw};
}
template <typename T, size_t N>
HWY_API Vec128<T, N / 2> LowerHalf(Vec128<T, N> v) {
  return Vec128<T, N / 2>{v.raw};
}

// ------------------------------ ShiftLeftBytes

// 0x01..0F, kBytes = 1 => 0x02..0F00
template <int kBytes, class D>
HWY_API VFromD<D> ShiftLeftBytes(D /* tag */, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  const __i8x16 zero = wasm_i8x16_splat(0);
  switch (kBytes) {
    case 0:
      return v;

    case 1:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 0, 1, 2, 3, 4, 5, 6,
                                          7, 8, 9, 10, 11, 12, 13, 14)};

    case 2:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 0, 1, 2, 3, 4, 5,
                                          6, 7, 8, 9, 10, 11, 12, 13)};

    case 3:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 0, 1, 2, 3,
                                          4, 5, 6, 7, 8, 9, 10, 11, 12)};

    case 4:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 0, 1, 2,
                                          3, 4, 5, 6, 7, 8, 9, 10, 11)};

    case 5:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 0, 1,
                                          2, 3, 4, 5, 6, 7, 8, 9, 10)};

    case 6:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          0, 1, 2, 3, 4, 5, 6, 7, 8, 9)};

    case 7:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 0, 1, 2, 3, 4, 5, 6, 7, 8)};

    case 8:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 0, 1, 2, 3, 4, 5, 6, 7)};

    case 9:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 0, 1, 2, 3, 4, 5, 6)};

    case 10:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 0, 1, 2, 3, 4, 5)};

    case 11:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 16, 0, 1, 2, 3, 4)};

    case 12:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 16, 16, 0, 1, 2, 3)};

    case 13:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 16, 16, 16, 0, 1, 2)};

    case 14:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 16, 16, 16, 16, 0,
                                          1)};

    case 15:
      return VFromD<D>{wasm_i8x16_shuffle(v.raw, zero, 16, 16, 16, 16, 16, 16,
                                          16, 16, 16, 16, 16, 16, 16, 16, 16,
                                          0)};
  }
  return VFromD<D>{zero};
}

template <int kBytes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftBytes(Vec128<T, N> v) {
  return ShiftLeftBytes<kBytes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftLeftLanes

template <int kLanes, class D>
HWY_API VFromD<D> ShiftLeftLanes(D d, const VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr size_t kBytes = kLanes * sizeof(TFromD<D>);
  return BitCast(d, ShiftLeftBytes<kBytes>(BitCast(d8, v)));
}

template <int kLanes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftLanes(const Vec128<T, N> v) {
  return ShiftLeftLanes<kLanes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftRightBytes
namespace detail {

// Helper function allows zeroing invalid lanes in caller.
template <int kBytes, typename T, size_t N>
HWY_API __i8x16 ShrBytes(const Vec128<T, N> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  const __i8x16 zero = wasm_i8x16_splat(0);

  switch (kBytes) {
    case 0:
      return v.raw;

    case 1:
      return wasm_i8x16_shuffle(v.raw, zero, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
                                12, 13, 14, 15, 16);

    case 2:
      return wasm_i8x16_shuffle(v.raw, zero, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                13, 14, 15, 16, 16);

    case 3:
      return wasm_i8x16_shuffle(v.raw, zero, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                13, 14, 15, 16, 16, 16);

    case 4:
      return wasm_i8x16_shuffle(v.raw, zero, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
                                14, 15, 16, 16, 16, 16);

    case 5:
      return wasm_i8x16_shuffle(v.raw, zero, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                15, 16, 16, 16, 16, 16);

    case 6:
      return wasm_i8x16_shuffle(v.raw, zero, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                16, 16, 16, 16, 16, 16);

    case 7:
      return wasm_i8x16_shuffle(v.raw, zero, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                16, 16, 16, 16, 16, 16, 16);

    case 8:
      return wasm_i8x16_shuffle(v.raw, zero, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 9:
      return wasm_i8x16_shuffle(v.raw, zero, 9, 10, 11, 12, 13, 14, 15, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 10:
      return wasm_i8x16_shuffle(v.raw, zero, 10, 11, 12, 13, 14, 15, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 11:
      return wasm_i8x16_shuffle(v.raw, zero, 11, 12, 13, 14, 15, 16, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 12:
      return wasm_i8x16_shuffle(v.raw, zero, 12, 13, 14, 15, 16, 16, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 13:
      return wasm_i8x16_shuffle(v.raw, zero, 13, 14, 15, 16, 16, 16, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 14:
      return wasm_i8x16_shuffle(v.raw, zero, 14, 15, 16, 16, 16, 16, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);

    case 15:
      return wasm_i8x16_shuffle(v.raw, zero, 15, 16, 16, 16, 16, 16, 16, 16, 16,
                                16, 16, 16, 16, 16, 16, 16);
    case 16:
      return zero;
  }
}

}  // namespace detail

// 0x01..0F, kBytes = 1 => 0x0001..0E
template <int kBytes, class D>
HWY_API VFromD<D> ShiftRightBytes(D d, VFromD<D> v) {
  // For partial vectors, clear upper lanes so we shift in zeros.
  if (d.MaxBytes() != 16) {
    const Full128<TFromD<D>> dfull;
    const VFromD<decltype(dfull)> vfull{v.raw};
    v = VFromD<D>{IfThenElseZero(FirstN(dfull, MaxLanes(d)), vfull).raw};
  }
  return VFromD<D>{detail::ShrBytes<kBytes>(v)};
}

// ------------------------------ ShiftRightLanes
template <int kLanes, class D>
HWY_API VFromD<D> ShiftRightLanes(D d, const VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr size_t kBytes = kLanes * sizeof(TFromD<D>);
  return BitCast(d, ShiftRightBytes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ UpperHalf (ShiftRightBytes)

template <class D, typename T = TFromD<D>>
HWY_API Vec64<T> UpperHalf(D /* tag */, const Vec128<T> v) {
  return Vec64<T>{wasm_i32x4_shuffle(v.raw, v.raw, 2, 3, 2, 3)};
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> UpperHalf(D d, VFromD<Twice<D>> v) {
  return LowerHalf(d, ShiftRightBytes<d.MaxBytes()>(Twice<D>(), v));
}

// ------------------------------ CombineShiftRightBytes

template <int kBytes, class D, typename T = TFromD<D>>
HWY_API Vec128<T> CombineShiftRightBytes(D /* tag */, Vec128<T> hi,
                                         Vec128<T> lo) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  switch (kBytes) {
    case 0:
      return lo;

    case 1:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 1, 2, 3, 4, 5, 6, 7,
                                          8, 9, 10, 11, 12, 13, 14, 15, 16)};

    case 2:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 2, 3, 4, 5, 6, 7, 8,
                                          9, 10, 11, 12, 13, 14, 15, 16, 17)};

    case 3:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 3, 4, 5, 6, 7, 8, 9,
                                          10, 11, 12, 13, 14, 15, 16, 17, 18)};

    case 4:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 4, 5, 6, 7, 8, 9, 10,
                                          11, 12, 13, 14, 15, 16, 17, 18, 19)};

    case 5:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 5, 6, 7, 8, 9, 10, 11,
                                          12, 13, 14, 15, 16, 17, 18, 19, 20)};

    case 6:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 6, 7, 8, 9, 10, 11,
                                          12, 13, 14, 15, 16, 17, 18, 19, 20,
                                          21)};

    case 7:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 7, 8, 9, 10, 11, 12,
                                          13, 14, 15, 16, 17, 18, 19, 20, 21,
                                          22)};

    case 8:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 8, 9, 10, 11, 12, 13,
                                          14, 15, 16, 17, 18, 19, 20, 21, 22,
                                          23)};

    case 9:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 9, 10, 11, 12, 13, 14,
                                          15, 16, 17, 18, 19, 20, 21, 22, 23,
                                          24)};

    case 10:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 10, 11, 12, 13, 14,
                                          15, 16, 17, 18, 19, 20, 21, 22, 23,
                                          24, 25)};

    case 11:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 11, 12, 13, 14, 15,
                                          16, 17, 18, 19, 20, 21, 22, 23, 24,
                                          25, 26)};

    case 12:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 12, 13, 14, 15, 16,
                                          17, 18, 19, 20, 21, 22, 23, 24, 25,
                                          26, 27)};

    case 13:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 13, 14, 15, 16, 17,
                                          18, 19, 20, 21, 22, 23, 24, 25, 26,
                                          27, 28)};

    case 14:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 14, 15, 16, 17, 18,
                                          19, 20, 21, 22, 23, 24, 25, 26, 27,
                                          28, 29)};

    case 15:
      return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 15, 16, 17, 18, 19,
                                          20, 21, 22, 23, 24, 25, 26, 27, 28,
                                          29, 30)};
  }
  return hi;
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

// ------------------------------ Broadcast/splat any lane

template <int kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{wasm_i8x16_shuffle(
      v.raw, v.raw, kLane, kLane, kLane, kLane, kLane, kLane, kLane, kLane,
      kLane, kLane, kLane, kLane, kLane, kLane, kLane, kLane)};
}

template <int kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{wasm_i16x8_shuffle(v.raw, v.raw, kLane, kLane, kLane,
                                         kLane, kLane, kLane, kLane, kLane)};
}

template <int kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{
      wasm_i32x4_shuffle(v.raw, v.raw, kLane, kLane, kLane, kLane)};
}

template <int kLane, typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> Broadcast(const Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{wasm_i64x2_shuffle(v.raw, v.raw, kLane, kLane)};
}

// ------------------------------ TableLookupBytes

// Returns vector of bytes[from[i]]. "from" is also interpreted as bytes, i.e.
// lane indices in [0, 16).
template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytes(const Vec128<T, N> bytes,
                                        const Vec128<TI, NI> from) {
  return Vec128<TI, NI>{wasm_i8x16_swizzle(bytes.raw, from.raw)};
}

template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytesOr0(const Vec128<T, N> bytes,
                                           const Vec128<TI, NI> from) {
  const DFromV<decltype(from)> d;
  // Mask size must match vector type, so cast everything to this type.
  Repartition<int8_t, decltype(d)> di8;
  Repartition<int8_t, DFromV<decltype(bytes)>> d_bytes8;
  const auto msb = BitCast(di8, from) < Zero(di8);
  const auto lookup =
      TableLookupBytes(BitCast(d_bytes8, bytes), BitCast(di8, from));
  return BitCast(d, IfThenZeroElse(msb, lookup));
}

// ------------------------------ Hard-coded shuffles

// Notation: let Vec128<int32_t> have lanes 3,2,1,0 (0 is least-significant).
// Shuffle0321 rotates one lane to the right (the previous least-significant
// lane is now most-significant). These could also be implemented via
// CombineShiftRightBytes but the shuffle_abcd notation is more convenient.

// Swap 32-bit halves in 64-bit halves.
template <typename T, size_t N>
HWY_API Vec128<T, N> Shuffle2301(const Vec128<T, N> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i32x4_shuffle(v.raw, v.raw, 1, 0, 3, 2)};
}

// These are used by generic_ops-inl to implement LoadInterleaved3.
namespace detail {

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShuffleTwo2301(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i8x16_shuffle(a.raw, b.raw, 1, 0, 3 + 16, 2 + 16,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> ShuffleTwo2301(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i16x8_shuffle(a.raw, b.raw, 1, 0, 3 + 8, 2 + 8,
                                         0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> ShuffleTwo2301(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i32x4_shuffle(a.raw, b.raw, 1, 0, 3 + 4, 2 + 4)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShuffleTwo1230(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i8x16_shuffle(a.raw, b.raw, 0, 3, 2 + 16, 1 + 16,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> ShuffleTwo1230(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i16x8_shuffle(a.raw, b.raw, 0, 3, 2 + 8, 1 + 8,
                                         0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> ShuffleTwo1230(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i32x4_shuffle(a.raw, b.raw, 0, 3, 2 + 4, 1 + 4)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> ShuffleTwo3012(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i8x16_shuffle(a.raw, b.raw, 2, 1, 0 + 16, 3 + 16,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
                                         0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> ShuffleTwo3012(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i16x8_shuffle(a.raw, b.raw, 2, 1, 0 + 8, 3 + 8,
                                         0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF)};
}
template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> ShuffleTwo3012(const Vec128<T, N> a,
                                    const Vec128<T, N> b) {
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Vec128<T, N>{wasm_i32x4_shuffle(a.raw, b.raw, 2, 1, 0 + 4, 3 + 4)};
}

}  // namespace detail

// Swap 64-bit halves
template <typename T>
HWY_API Vec128<T> Shuffle01(const Vec128<T> v) {
  static_assert(sizeof(T) == 8, "Only for 64-bit lanes");
  return Vec128<T>{wasm_i64x2_shuffle(v.raw, v.raw, 1, 0)};
}
template <typename T>
HWY_API Vec128<T> Shuffle1032(const Vec128<T> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  return Vec128<T>{wasm_i64x2_shuffle(v.raw, v.raw, 1, 0)};
}

// Rotate right 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle0321(const Vec128<T> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  return Vec128<T>{wasm_i32x4_shuffle(v.raw, v.raw, 1, 2, 3, 0)};
}

// Rotate left 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle2103(const Vec128<T> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  return Vec128<T>{wasm_i32x4_shuffle(v.raw, v.raw, 3, 0, 1, 2)};
}

// Reverse
template <typename T>
HWY_API Vec128<T> Shuffle0123(const Vec128<T> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  return Vec128<T>{wasm_i32x4_shuffle(v.raw, v.raw, 3, 2, 1, 0)};
}

// ------------------------------ TableLookupLanes

// Returned by SetTableIndices for use by TableLookupLanes.
template <typename T, size_t N = 16 / sizeof(T)>
struct Indices128 {
  __v128_u raw;
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

template <class D, typename TI, HWY_IF_V_SIZE_LE_D(D, 16),
          HWY_IF_T_SIZE_D(D, 1)>
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
  return Indices128<TFromD<D>, MaxLanes(D())>{vec.raw};
}

template <class D, typename TI, HWY_IF_V_SIZE_LE_D(D, 16),
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

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), typename TI>
HWY_API Indices128<TFromD<D>, HWY_MAX_LANES_D(D)> SetTableIndices(
    D d, const TI* idx) {
  const Rebind<TI, decltype(d)> di;
  return IndicesFromVec(d, LoadU(di, idx));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
  using TI = MakeSigned<T>;
  const DFromV<decltype(v)> d;
  const Rebind<TI, decltype(d)> di;
  return BitCast(d, TableLookupBytes(BitCast(di, v), Vec128<TI, N>{idx.raw}));
}

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

template <typename T>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> du8;

  const VFromD<decltype(du8)> byte_idx{idx.raw};
  const auto byte_idx_mod = byte_idx & Set(du8, uint8_t{0x0F});
  // If ANDing did not change the index, it is for the lower half.
  const auto is_lo = (byte_idx == byte_idx_mod);

  return BitCast(d, IfThenElse(is_lo, TableLookupBytes(a, byte_idx_mod),
                               TableLookupBytes(b, byte_idx_mod)));
}

// ------------------------------ Reverse (Shuffle0123, Shuffle2301, Shuffle01)

// Single lane: no change
template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 1)>
HWY_API Vec128<T, 1> Reverse(D /* tag */, Vec128<T, 1> v) {
  return v;
}

// 32-bit x2: shuffle
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec64<T> Reverse(D /* tag */, const Vec64<T> v) {
  return Vec64<T>{Shuffle2301(Vec128<T>{v.raw}).raw};
}

// 64-bit x2: shuffle
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Reverse(D /* tag */, const Vec128<T> v) {
  return Shuffle01(v);
}

// 32-bit x2: shuffle
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Reverse(D /* tag */, const Vec128<T> v) {
  return Shuffle0123(v);
}

// 16-bit
template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse(D d, const VFromD<D> v) {
  const RepartitionToWide<RebindToUnsigned<decltype(d)>> du32;
  return BitCast(d, RotateRight<16>(Reverse(du32, BitCast(du32, v))));
}

template <class D, HWY_IF_T_SIZE_D(D, 1), HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> Reverse(D d, const VFromD<D> v) {
  static constexpr int kN = 16 + Lanes(d);
  return VFromD<D>{wasm_i8x16_shuffle(
      v.raw, v.raw,
      // kN is adjusted to ensure we have valid indices for all lengths.
      kN - 1, kN - 2, kN - 3, kN - 4, kN - 5, kN - 6, kN - 7, kN - 8, kN - 9,
      kN - 10, kN - 11, kN - 12, kN - 13, kN - 14, kN - 15, kN - 16)};
}

// ------------------------------ Reverse2

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse2(D d, const VFromD<D> v) {
  const RepartitionToWide<RebindToUnsigned<decltype(d)>> dw;
  return BitCast(d, RotateRight<16>(BitCast(dw, v)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse2(D /* tag */, const VFromD<D> v) {
  return Shuffle2301(v);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse2(D /* tag */, const VFromD<D> v) {
  return Shuffle01(v);
}

// ------------------------------ Reverse4

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse4(D /* tag */, const VFromD<D> v) {
  return VFromD<D>{wasm_i16x8_shuffle(v.raw, v.raw, 3, 2, 1, 0, 7, 6, 5, 4)};
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse4(D /* tag */, const VFromD<D> v) {
  return Shuffle0123(v);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse4(D /* tag */, const VFromD<D>) {
  HWY_ASSERT(0);  // don't have 8 u64 lanes
}

// ------------------------------ Reverse8

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse8(D d, const VFromD<D> v) {
  return Reverse(d, v);
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API VFromD<D> Reverse8(D /* tag */, const VFromD<D>) {
  HWY_ASSERT(0);  // don't have 8 lanes for > 16-bit lanes
}

// ------------------------------ InterleaveLower

template <size_t N>
HWY_API Vec128<uint8_t, N> InterleaveLower(Vec128<uint8_t, N> a,
                                           Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_i8x16_shuffle(
      a.raw, b.raw, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> InterleaveLower(Vec128<uint16_t, N> a,
                                            Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 0, 8, 1, 9, 2, 10, 3, 11)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> InterleaveLower(Vec128<uint32_t, N> a,
                                            Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_i32x4_shuffle(a.raw, b.raw, 0, 4, 1, 5)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> InterleaveLower(Vec128<uint64_t, N> a,
                                            Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{wasm_i64x2_shuffle(a.raw, b.raw, 0, 2)};
}

template <size_t N>
HWY_API Vec128<int8_t, N> InterleaveLower(Vec128<int8_t, N> a,
                                          Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_shuffle(
      a.raw, b.raw, 0, 16, 1, 17, 2, 18, 3, 19, 4, 20, 5, 21, 6, 22, 7, 23)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> InterleaveLower(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 0, 8, 1, 9, 2, 10, 3, 11)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> InterleaveLower(Vec128<int32_t, N> a,
                                           Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_shuffle(a.raw, b.raw, 0, 4, 1, 5)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> InterleaveLower(Vec128<int64_t, N> a,
                                           Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{wasm_i64x2_shuffle(a.raw, b.raw, 0, 2)};
}

template <size_t N>
HWY_API Vec128<float, N> InterleaveLower(Vec128<float, N> a,
                                         Vec128<float, N> b) {
  return Vec128<float, N>{wasm_i32x4_shuffle(a.raw, b.raw, 0, 4, 1, 5)};
}

template <size_t N>
HWY_API Vec128<double, N> InterleaveLower(Vec128<double, N> a,
                                          Vec128<double, N> b) {
  return Vec128<double, N>{wasm_i64x2_shuffle(a.raw, b.raw, 0, 2)};
}

template <class T, size_t N, HWY_IF_T_SIZE(T, 2), HWY_IF_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, InterleaveLower(BitCast(du, a), BitCast(du, b)));
}

// Additional overload for the optional tag (all vector lengths).
template <class D>
HWY_API VFromD<D> InterleaveLower(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveUpper (UpperHalf)

// All functions inside detail lack the required D parameter.
namespace detail {

template <size_t N>
HWY_API Vec128<uint8_t, N> InterleaveUpper(Vec128<uint8_t, N> a,
                                           Vec128<uint8_t, N> b) {
  return Vec128<uint8_t, N>{wasm_i8x16_shuffle(a.raw, b.raw, 8, 24, 9, 25, 10,
                                               26, 11, 27, 12, 28, 13, 29, 14,
                                               30, 15, 31)};
}
template <size_t N>
HWY_API Vec128<uint16_t, N> InterleaveUpper(Vec128<uint16_t, N> a,
                                            Vec128<uint16_t, N> b) {
  return Vec128<uint16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 4, 12, 5, 13, 6, 14, 7, 15)};
}
template <size_t N>
HWY_API Vec128<uint32_t, N> InterleaveUpper(Vec128<uint32_t, N> a,
                                            Vec128<uint32_t, N> b) {
  return Vec128<uint32_t, N>{wasm_i32x4_shuffle(a.raw, b.raw, 2, 6, 3, 7)};
}
template <size_t N>
HWY_API Vec128<uint64_t, N> InterleaveUpper(Vec128<uint64_t, N> a,
                                            Vec128<uint64_t, N> b) {
  return Vec128<uint64_t, N>{wasm_i64x2_shuffle(a.raw, b.raw, 1, 3)};
}

template <size_t N>
HWY_API Vec128<int8_t, N> InterleaveUpper(Vec128<int8_t, N> a,
                                          Vec128<int8_t, N> b) {
  return Vec128<int8_t, N>{wasm_i8x16_shuffle(a.raw, b.raw, 8, 24, 9, 25, 10,
                                              26, 11, 27, 12, 28, 13, 29, 14,
                                              30, 15, 31)};
}
template <size_t N>
HWY_API Vec128<int16_t, N> InterleaveUpper(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  return Vec128<int16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 4, 12, 5, 13, 6, 14, 7, 15)};
}
template <size_t N>
HWY_API Vec128<int32_t, N> InterleaveUpper(Vec128<int32_t, N> a,
                                           Vec128<int32_t, N> b) {
  return Vec128<int32_t, N>{wasm_i32x4_shuffle(a.raw, b.raw, 2, 6, 3, 7)};
}
template <size_t N>
HWY_API Vec128<int64_t, N> InterleaveUpper(Vec128<int64_t, N> a,
                                           Vec128<int64_t, N> b) {
  return Vec128<int64_t, N>{wasm_i64x2_shuffle(a.raw, b.raw, 1, 3)};
}

template <size_t N>
HWY_API Vec128<float16_t, N> InterleaveUpper(Vec128<float16_t, N> a,
                                             Vec128<float16_t, N> b) {
  return Vec128<float16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 4, 12, 5, 13, 6, 14, 7, 15)};
}
template <size_t N>
HWY_API Vec128<bfloat16_t, N> InterleaveUpper(Vec128<bfloat16_t, N> a,
                                              Vec128<bfloat16_t, N> b) {
  return Vec128<bfloat16_t, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 4, 12, 5, 13, 6, 14, 7, 15)};
}

template <size_t N>
HWY_API Vec128<float, N> InterleaveUpper(Vec128<float, N> a,
                                         Vec128<float, N> b) {
  return Vec128<float, N>{wasm_i32x4_shuffle(a.raw, b.raw, 2, 6, 3, 7)};
}

template <size_t N>
HWY_API Vec128<double, N> InterleaveUpper(Vec128<double, N> a,
                                          Vec128<double, N> b) {
  return Vec128<double, N>{wasm_i64x2_shuffle(a.raw, b.raw, 1, 3)};
}

}  // namespace detail

// Full
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> InterleaveUpper(D /* tag */, Vec128<T> a, Vec128<T> b) {
  return detail::InterleaveUpper(a, b);
}

// Partial
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> InterleaveUpper(D d, VFromD<D> a, VFromD<D> b) {
  const Half<decltype(d)> d2;
  return InterleaveLower(d, VFromD<D>{UpperHalf(d2, a).raw},
                         VFromD<D>{UpperHalf(d2, b).raw});
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

template <size_t kIdx3210, size_t kVectSize, class V,
          HWY_IF_LANES_LE(kVectSize, 16)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<1> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  constexpr int kIdx3 = static_cast<int>((kIdx3210 >> 6) & 3);
  constexpr int kIdx2 = static_cast<int>((kIdx3210 >> 4) & 3);
  constexpr int kIdx1 = static_cast<int>((kIdx3210 >> 2) & 3);
  constexpr int kIdx0 = static_cast<int>(kIdx3210 & 3);
  return V{wasm_i8x16_shuffle(v.raw, v.raw, kIdx0, kIdx1, kIdx2, kIdx3,
                              kIdx0 + 4, kIdx1 + 4, kIdx2 + 4, kIdx3 + 4,
                              kIdx0 + 8, kIdx1 + 8, kIdx2 + 8, kIdx3 + 8,
                              kIdx0 + 12, kIdx1 + 12, kIdx2 + 12, kIdx3 + 12)};
}

template <size_t kIdx3210, size_t kVectSize, class V,
          HWY_IF_LANES_LE(kVectSize, 16)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<2> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  constexpr int kIdx3 = static_cast<int>((kIdx3210 >> 6) & 3);
  constexpr int kIdx2 = static_cast<int>((kIdx3210 >> 4) & 3);
  constexpr int kIdx1 = static_cast<int>((kIdx3210 >> 2) & 3);
  constexpr int kIdx0 = static_cast<int>(kIdx3210 & 3);
  return V{wasm_i16x8_shuffle(v.raw, v.raw, kIdx0, kIdx1, kIdx2, kIdx3,
                              kIdx0 + 4, kIdx1 + 4, kIdx2 + 4, kIdx3 + 4)};
}

template <size_t kIdx3210, size_t kVectSize, class V,
          HWY_IF_LANES_LE(kVectSize, 16)>
HWY_INLINE V Per4LaneBlockShuffle(hwy::SizeTag<kIdx3210> /*idx_3210_tag*/,
                                  hwy::SizeTag<4> /*lane_size_tag*/,
                                  hwy::SizeTag<kVectSize> /*vect_size_tag*/,
                                  V v) {
  constexpr int kIdx3 = static_cast<int>((kIdx3210 >> 6) & 3);
  constexpr int kIdx2 = static_cast<int>((kIdx3210 >> 4) & 3);
  constexpr int kIdx1 = static_cast<int>((kIdx3210 >> 2) & 3);
  constexpr int kIdx0 = static_cast<int>(kIdx3210 & 3);
  return V{wasm_i32x4_shuffle(v.raw, v.raw, kIdx0, kIdx1, kIdx2, kIdx3)};
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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
#if !HWY_IS_DEBUG_BUILD
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

// ------------------------------ ZeroExtendVector (IfThenElseZero)
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const Half<D> dh;
  return IfThenElseZero(FirstN(d, MaxLanes(dh)), VFromD<D>{lo.raw});
}

// ------------------------------ ConcatLowerLower
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatLowerLower(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i64x2_shuffle(lo.raw, hi.raw, 0, 2)};
}

// ------------------------------ ConcatUpperUpper
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatUpperUpper(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i64x2_shuffle(lo.raw, hi.raw, 1, 3)};
}

// ------------------------------ ConcatLowerUpper
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatLowerUpper(D d, Vec128<T> hi, Vec128<T> lo) {
  return CombineShiftRightBytes<8>(d, hi, lo);
}

// ------------------------------ ConcatUpperLower
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatUpperLower(D d, Vec128<T> hi, Vec128<T> lo) {
  return IfThenElse(FirstN(d, Lanes(d) / 2), lo, hi);
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
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> ConcatOdd(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 1, 3, 5, 7, 9, 11, 13, 15,
                                      17, 19, 21, 23, 25, 27, 29, 31)};
}

// 8-bit x8
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec64<T> ConcatOdd(D /* tag */, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half.
  return Vec128<T, 8>{wasm_i8x16_shuffle(lo.raw, hi.raw, 1, 3, 5, 7, 17, 19, 21,
                                         23, 1, 3, 5, 7, 17, 19, 21, 23)};
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatOdd(D /* tag */, Vec32<T> hi, Vec32<T> lo) {
  // Don't care about upper 3/4.
  return Vec128<T, 4>{wasm_i8x16_shuffle(lo.raw, hi.raw, 1, 3, 17, 19, 1, 3, 17,
                                         19, 1, 3, 17, 19, 1, 3, 17, 19)};
}

// 16-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> ConcatOdd(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{
      wasm_i16x8_shuffle(lo.raw, hi.raw, 1, 3, 5, 7, 9, 11, 13, 15)};
}

// 16-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ConcatOdd(D /* tag */, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half.
  return Vec128<T, 4>{
      wasm_i16x8_shuffle(lo.raw, hi.raw, 1, 3, 9, 11, 1, 3, 9, 11)};
}

// 32-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ConcatOdd(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i32x4_shuffle(lo.raw, hi.raw, 1, 3, 5, 7)};
}

// Any T x2
template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 2)>
HWY_API Vec128<T, 2> ConcatOdd(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveUpper(d, lo, hi);
}

// ------------------------------ ConcatEven (InterleaveLower)

// 8-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> ConcatEven(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 0, 2, 4, 6, 8, 10, 12, 14,
                                      16, 18, 20, 22, 24, 26, 28, 30)};
}

// 8-bit x8
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec64<T> ConcatEven(D /* tag */, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half.
  return Vec64<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 0, 2, 4, 6, 16, 18, 20, 22,
                                     0, 2, 4, 6, 16, 18, 20, 22)};
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatEven(D /* tag */, Vec32<T> hi, Vec32<T> lo) {
  // Don't care about upper 3/4.
  return Vec32<T>{wasm_i8x16_shuffle(lo.raw, hi.raw, 0, 2, 16, 18, 0, 2, 16, 18,
                                     0, 2, 16, 18, 0, 2, 16, 18)};
}

// 16-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> ConcatEven(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{
      wasm_i16x8_shuffle(lo.raw, hi.raw, 0, 2, 4, 6, 8, 10, 12, 14)};
}

// 16-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ConcatEven(D /* tag */, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half.
  return Vec64<T>{wasm_i16x8_shuffle(lo.raw, hi.raw, 0, 2, 8, 10, 0, 2, 8, 10)};
}

// 32-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ConcatEven(D /* tag */, Vec128<T> hi, Vec128<T> lo) {
  return Vec128<T>{wasm_i32x4_shuffle(lo.raw, hi.raw, 0, 2, 4, 6)};
}

// Any T x2
template <typename D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 2)>
HWY_API Vec128<T, 2> ConcatEven(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveLower(d, lo, hi);
}

// ------------------------------ DupEven (InterleaveLower)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i8x16_shuffle(v.raw, v.raw, 0, 0, 2, 2, 4, 4, 6, 6,
                                         8, 8, 10, 10, 12, 12, 14, 14)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i16x8_shuffle(v.raw, v.raw, 0, 0, 2, 2, 4, 4, 6, 6)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i32x4_shuffle(v.raw, v.raw, 0, 0, 2, 2)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupEven(const Vec128<T, N> v) {
  return InterleaveLower(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ DupOdd (InterleaveUpper)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i8x16_shuffle(v.raw, v.raw, 1, 1, 3, 3, 5, 5, 7, 7,
                                         9, 9, 11, 11, 13, 13, 15, 15)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i16x8_shuffle(v.raw, v.raw, 1, 1, 3, 3, 5, 5, 7, 7)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return Vec128<T, N>{wasm_i32x4_shuffle(v.raw, v.raw, 1, 1, 3, 3)};
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupOdd(const Vec128<T, N> v) {
  return InterleaveUpper(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ OddEven

namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> OddEven(hwy::SizeTag<1> /* tag */, const Vec128<T, N> a,
                                const Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  alignas(16) static constexpr uint8_t mask[16] = {
      0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0};
  return IfThenElse(MaskFromVec(BitCast(d, Load(d8, mask))), b, a);
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> OddEven(hwy::SizeTag<2> /* tag */, const Vec128<T, N> a,
                                const Vec128<T, N> b) {
  return Vec128<T, N>{
      wasm_i16x8_shuffle(a.raw, b.raw, 8, 1, 10, 3, 12, 5, 14, 7)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> OddEven(hwy::SizeTag<4> /* tag */, const Vec128<T, N> a,
                                const Vec128<T, N> b) {
  return Vec128<T, N>{wasm_i32x4_shuffle(a.raw, b.raw, 4, 1, 6, 3)};
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> OddEven(hwy::SizeTag<8> /* tag */, const Vec128<T, N> a,
                                const Vec128<T, N> b) {
  return Vec128<T, N>{wasm_i64x2_shuffle(a.raw, b.raw, 2, 1)};
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> OddEven(const Vec128<T, N> a, const Vec128<T, N> b) {
  return detail::OddEven(hwy::SizeTag<sizeof(T)>(), a, b);
}
template <size_t N>
HWY_API Vec128<float, N> OddEven(const Vec128<float, N> a,
                                 const Vec128<float, N> b) {
  return Vec128<float, N>{wasm_i32x4_shuffle(a.raw, b.raw, 4, 1, 6, 3)};
}

// ------------------------------ InterleaveEven
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i8x16_shuffle(a.raw, b.raw, 0, 16, 2, 18, 4, 20, 6, 22,
                                      8, 24, 10, 26, 12, 28, 14, 30)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i16x8_shuffle(a.raw, b.raw, 0, 8, 2, 10, 4, 12, 6, 14)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i32x4_shuffle(a.raw, b.raw, 0, 4, 2, 6)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveOdd
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i8x16_shuffle(a.raw, b.raw, 1, 17, 3, 19, 5, 21, 7, 23,
                                      9, 25, 11, 27, 13, 29, 15, 31)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i16x8_shuffle(a.raw, b.raw, 1, 9, 3, 11, 5, 13, 7, 15)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return VFromD<D>{wasm_i32x4_shuffle(a.raw, b.raw, 1, 5, 3, 7)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_T_SIZE_D(D, 8)>
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
template <class D>
HWY_API VFromD<D> ReverseBlocks(D /* tag */, VFromD<D> v) {
  return v;
}

// ================================================== CONVERT

// ------------------------------ Promotions (part w/ narrow lanes -> full)

// Unsigned: zero-extend.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>{wasm_u16x8_extend_low_u8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>{wasm_u32x4_extend_low_u16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{wasm_u64x2_extend_low_u32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>{
      wasm_u32x4_extend_low_u16x8(wasm_u16x8_extend_low_u8x16(v.raw))};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>{wasm_u16x8_extend_low_u8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  return VFromD<D>{wasm_u32x4_extend_low_u16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{wasm_u64x2_extend_low_u32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint8_t, D>> v) {
  return VFromD<D>{
      wasm_u32x4_extend_low_u16x8(wasm_u16x8_extend_low_u8x16(v.raw))};
}

// U8/U16 to U64/I64: First, zero-extend to U32, and then zero-extend to
// TFromD<D>
template <class D, class V, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_UI64_D(D),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V)), HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> PromoteTo(D d, V v) {
  const Rebind<uint32_t, decltype(d)> du32;
  return PromoteTo(d, PromoteTo(du32, v));
}

// Signed: replicate sign bit.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
  return VFromD<D>{wasm_i16x8_extend_low_i8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{wasm_i32x4_extend_low_i16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{wasm_i64x2_extend_low_i32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int8_t, D>> v) {
  return VFromD<D>{
      wasm_i32x4_extend_low_i16x8(wasm_i16x8_extend_low_i8x16(v.raw))};
}

// I8/I16 to I64: First, promote to I32, and then promote to I64
template <class D, class V, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V)), HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> PromoteTo(D d, V v) {
  const Rebind<int32_t, decltype(d)> di32;
  return PromoteTo(d, PromoteTo(di32, v));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D df32, VFromD<Rebind<bfloat16_t, D>> v) {
  const Rebind<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteTo(di32, BitCast(du16, v))));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{wasm_f64x2_convert_low_i32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{wasm_f64x2_convert_low_u32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return VFromD<D>{wasm_f64x2_promote_low_f32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I64_D(D)>
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

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
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

// ------------------------------ PromoteUpperTo

// Per-target flag to prevent generic_ops-inl.h from defining PromoteUpperTo.
#ifdef HWY_NATIVE_PROMOTE_UPPER_TO
#undef HWY_NATIVE_PROMOTE_UPPER_TO
#else
#define HWY_NATIVE_PROMOTE_UPPER_TO
#endif

// Unsigned: zero-extend.
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>{wasm_u16x8_extend_high_u8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint16_t, D>> v) {
  return VFromD<D>{wasm_u32x4_extend_high_u16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint32_t, D>> v) {
  return VFromD<D>{wasm_u64x2_extend_high_u32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint8_t, D>> v) {
  return VFromD<D>{wasm_u16x8_extend_high_u8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint16_t, D>> v) {
  return VFromD<D>{wasm_u32x4_extend_high_u16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<uint32_t, D>> v) {
  return VFromD<D>{wasm_u64x2_extend_high_u32x4(v.raw)};
}

// Signed: replicate sign bit.
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<int8_t, D>> v) {
  return VFromD<D>{wasm_i16x8_extend_high_i8x16(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<int16_t, D>> v) {
  return VFromD<D>{wasm_i32x4_extend_high_i16x8(v.raw)};
}
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /* tag */,
                                 VFromD<Repartition<int32_t, D>> v) {
  return VFromD<D>{wasm_i64x2_extend_high_i32x4(v.raw)};
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df32, VFromD<Repartition<float16_t, D>> v) {
  const Rebind<float16_t, decltype(df32)> dh;
  return PromoteTo(df32, UpperHalf(dh, v));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df32, VFromD<Repartition<bfloat16_t, D>> v) {
  const Repartition<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteUpperTo(di32, BitCast(du16, v))));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D dd, VFromD<Repartition<int32_t, D>> v) {
  // There is no wasm_f64x2_convert_high_i32x4.
  return PromoteTo(dd, UpperHalf(Rebind<int32_t, D>(), v));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D dd, VFromD<Repartition<uint32_t, D>> v) {
  // There is no wasm_f64x2_convert_high_u32x4.
  return PromoteTo(dd, UpperHalf(Rebind<uint32_t, D>(), v));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D dd, VFromD<Repartition<float, D>> v) {
  // There is no wasm_f64x2_promote_high_f32x4.
  return PromoteTo(dd, UpperHalf(Rebind<float, D>(), v));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D d64, VFromD<Repartition<float, D>> v) {
  return PromoteTo(d64, UpperHalf(Rebind<float, D>(), v));
}

// Generic version for <=64 bit input/output (_high is only for full vectors).
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), class V>
HWY_API VFromD<D> PromoteUpperTo(D d, V v) {
  const Rebind<TFromV<V>, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
}

// ------------------------------ PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ------------------------------ Demotions (full -> part w/ narrow lanes)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{wasm_u16x8_narrow_i32x4(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{wasm_i16x8_narrow_i32x4(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const auto intermediate = wasm_i16x8_narrow_i32x4(v.raw, v.raw);
  return VFromD<D>{wasm_u8x16_narrow_i16x8(intermediate, intermediate)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{wasm_u8x16_narrow_i16x8(v.raw, v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  const auto intermediate = wasm_i16x8_narrow_i32x4(v.raw, v.raw);
  return VFromD<D>{wasm_i8x16_narrow_i16x8(intermediate, intermediate)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<int16_t, D>> v) {
  return VFromD<D>{wasm_i8x16_narrow_i16x8(v.raw, v.raw)};
}

template <class D, HWY_IF_UNSIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<uint32_t, D>> v) {
  const DFromV<decltype(v)> du32;
  const RebindToSigned<decltype(du32)> di32;
  return DemoteTo(dn, BitCast(di32, Min(v, Set(du32, 0x7FFFFFFF))));
}

template <class D, HWY_IF_U8_D(D)>
HWY_API VFromD<D> DemoteTo(D du8, VFromD<Rebind<uint16_t, D>> v) {
  const DFromV<decltype(v)> du16;
  const RebindToSigned<decltype(du16)> di16;
  return DemoteTo(du8, BitCast(di16, Min(v, Set(du16, 0x7FFF))));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<double, D>> v) {
  return VFromD<D>{wasm_i32x4_trunc_sat_f64x2_zero(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<double, D>> v) {
  return VFromD<D>{wasm_u32x4_trunc_sat_f64x2_zero(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, VFromD<Rebind<double, D>> v) {
  return VFromD<D>{wasm_f32x4_demote_f64x2_zero(v.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
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
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F32_D(D)>
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

// Specializations for partial vectors because i16x8_narrow_i32x4 sets lanes
// above 2*N.
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec32<int16_t> ReorderDemote2To(D dn, Vec32<int32_t> a,
                                        Vec32<int32_t> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> ReorderDemote2To(D dn, Vec64<int32_t> a,
                                        Vec64<int32_t> b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const Vec128<int16_t> v_full{wasm_i16x8_narrow_i32x4(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class D, HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> ReorderDemote2To(D /* tag */, Vec128<int32_t> a,
                                         Vec128<int32_t> b) {
  return Vec128<int16_t>{wasm_i16x8_narrow_i32x4(a.raw, b.raw)};
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> ReorderDemote2To(D dn, Vec32<int32_t> a,
                                         Vec32<int32_t> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> ReorderDemote2To(D dn, Vec64<int32_t> a,
                                         Vec64<int32_t> b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const Vec128<int16_t> v_full{wasm_u16x8_narrow_i32x4(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class D, HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> ReorderDemote2To(D /* tag */, Vec128<int32_t> a,
                                          Vec128<int32_t> b) {
  return Vec128<uint16_t>{wasm_u16x8_narrow_i32x4(a.raw, b.raw)};
}

template <class D, HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<uint32_t> a,
                                   Vec128<uint32_t> b) {
  const DFromV<decltype(a)> du32;
  const RebindToSigned<decltype(du32)> di32;
  const auto max_i32 = Set(du32, 0x7FFFFFFFu);

  const auto clamped_a = BitCast(di32, Min(a, max_i32));
  const auto clamped_b = BitCast(di32, Min(b, max_i32));
  return ReorderDemote2To(dn, clamped_a, clamped_b);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint32_t, D>> a,
                                   VFromD<Repartition<uint32_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

// Specializations for partial vectors because i8x16_narrow_i16x8 sets lanes
// above 2*N.
template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_I8_D(D)>
HWY_API Vec64<int8_t> ReorderDemote2To(D dn, Vec64<int16_t> a,
                                       Vec64<int16_t> b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const Vec128<int8_t> v_full{wasm_i8x16_narrow_i16x8(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class D, HWY_IF_I8_D(D)>
HWY_API Vec128<int8_t> ReorderDemote2To(D /* tag */, Vec128<int16_t> a,
                                        Vec128<int16_t> b) {
  return Vec128<int8_t>{wasm_i8x16_narrow_i16x8(a.raw, b.raw)};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int16_t, D>> a,
                                   VFromD<Repartition<int16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> ReorderDemote2To(D dn, Vec64<int16_t> a,
                                        Vec64<int16_t> b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const Vec128<uint8_t> v_full{wasm_u8x16_narrow_i16x8(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class D, HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> ReorderDemote2To(D /* tag */, Vec128<int16_t> a,
                                         Vec128<int16_t> b) {
  return Vec128<uint8_t>{wasm_u8x16_narrow_i16x8(a.raw, b.raw)};
}

template <class D, HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, Vec128<uint16_t> a,
                                   Vec128<uint16_t> b) {
  const DFromV<decltype(a)> du16;
  const RebindToSigned<decltype(du16)> di16;
  const auto max_i16 = Set(du16, 0x7FFFu);

  const auto clamped_a = BitCast(di16, Min(a, max_i16));
  const auto clamped_b = BitCast(di16, Min(b, max_i16));
  return ReorderDemote2To(dn, clamped_a, clamped_b);
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint16_t, D>> a,
                                   VFromD<Repartition<uint16_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

// For already range-limited input [0, 255].
template <size_t N>
HWY_API Vec128<uint8_t, N> U8FromU32(const Vec128<uint32_t, N> v) {
  const auto intermediate = wasm_i16x8_narrow_i32x4(v.raw, v.raw);
  return Vec128<uint8_t, N>{
      wasm_u8x16_narrow_i16x8(intermediate, intermediate)};
}

// ------------------------------ Truncations

template <typename From, class DTo, HWY_IF_LANES_D(DTo, 1)>
HWY_API VFromD<DTo> TruncateTo(DTo /* tag */, Vec128<From, 1> v) {
  // BitCast requires the same size; DTo might be u8x1 and v u16x1.
  const Repartition<TFromD<DTo>, DFromV<decltype(v)>> dto;
  return VFromD<DTo>{BitCast(dto, v).raw};
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec16<uint8_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Full128<uint8_t> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = ConcatEven(d, v1, v1);
  const auto v4 = ConcatEven(d, v2, v2);
  return LowerHalf(LowerHalf(LowerHalf(ConcatEven(d, v4, v4))));
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Full128<uint16_t> d;
  const auto v1 = BitCast(d, v);
  const auto v2 = ConcatEven(d, v1, v1);
  return LowerHalf(LowerHalf(ConcatEven(d, v2, v2)));
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> TruncateTo(D /* tag */, Vec128<uint64_t> v) {
  const Full128<uint32_t> d;
  const auto v1 = BitCast(d, v);
  return LowerHalf(ConcatEven(d, v1, v1));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d;
  const auto v1 = Vec128<uint8_t>{v.raw};
  const auto v2 = ConcatEven(d, v1, v1);
  const auto v3 = ConcatEven(d, v2, v2);
  return VFromD<D>{v3.raw};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  const Repartition<uint16_t, DFromV<decltype(v)>> d;
  const auto v1 = Vec128<uint16_t>{v.raw};
  const auto v2 = ConcatEven(d, v1, v1);
  return VFromD<D>{v2.raw};
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API VFromD<D> TruncateTo(D /* tag */, VFromD<Rebind<uint16_t, D>> v) {
  const Repartition<uint8_t, DFromV<decltype(v)>> d;
  const auto v1 = Vec128<uint8_t>{v.raw};
  const auto v2 = ConcatEven(d, v1, v1);
  return VFromD<D>{v2.raw};
}

// ------------------------------ Demotions to/from i64

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

template <class D, class V>
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

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_UNSIGNED_D(D)>
HWY_API VFromD<D> DemoteTo(D dn, VFromD<Rebind<uint64_t, D>> v) {
  return TruncateTo(dn, detail::DemoteFromU64Saturate(dn, v));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_T_SIZE_D(D, 4),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<int64_t, D>> a,
                                   VFromD<Repartition<int64_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API VFromD<D> ReorderDemote2To(D dn, VFromD<Repartition<uint64_t, D>> a,
                                   VFromD<Repartition<uint64_t, D>> b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}

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

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API Vec128<uint32_t> ReorderDemote2To(D dn, Vec128<uint64_t> a,
                                          Vec128<uint64_t> b) {
  const Half<decltype(dn)> dnh;

  const auto saturated_a = detail::DemoteFromU64Saturate(dnh, a);
  const auto saturated_b = detail::DemoteFromU64Saturate(dnh, b);

  return ConcatEven(dn, BitCast(dn, saturated_b), BitCast(dn, saturated_a));
}

template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>), class V,
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<D>) * 2),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D d, V a, V b) {
  return ReorderDemote2To(d, a, b);
}

// ------------------------------ ConvertTo

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<int32_t, D>> v) {
  return VFromD<D>{wasm_f32x4_convert_i32x4(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<uint32_t, D>> v) {
  return VFromD<D>{wasm_f32x4_convert_u32x4(v.raw)};
}

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

// Truncates (rounds toward zero).
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_I32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return VFromD<D>{wasm_i32x4_trunc_sat_f32x4(v.raw)};
}
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  return VFromD<D>{wasm_u32x4_trunc_sat_f32x4(v.raw)};
}

template <class DI, HWY_IF_I64_D(DI)>
HWY_API VFromD<DI> ConvertTo(DI di, VFromD<Rebind<double, DI>> v) {
  using VI = VFromD<decltype(di)>;
  using MI = MFromD<decltype(di)>;
  const RebindToUnsigned<decltype(di)> du;
  using VU = VFromD<decltype(du)>;
  const Repartition<uint16_t, decltype(di)> du16;
  const VI k1075 = Set(di, 1075);  // biased exponent of 2^52

  // Exponent indicates whether the number can be represented as int64_t.
  const VU biased_exp = ShiftRight<52>(BitCast(du, v)) & Set(du, 0x7FF);
  const MI in_range = BitCast(di, biased_exp) < Set(di, 1086);

  // If we were to cap the exponent at 51 and add 2^52, the number would be in
  // [2^52, 2^53) and mantissa bits could be read out directly. We need to
  // round-to-0 (truncate).
  // Use 16-bit saturated unsigned subtraction to compute shift_mnt and
  // shift_int since biased_exp[i] is a non-negative integer that is less than
  // or equal to 2047.
  // The upper 48 bits of both shift_mnt and shift_int are guaranteed to be
  // zero as the upper 48 bits of both k1075 and biased_exp are zero.

  const VU shift_mnt = BitCast(
      du, SaturatedSub(BitCast(du16, k1075), BitCast(du16, biased_exp)));
  const VU shift_int = BitCast(
      du, SaturatedSub(BitCast(du16, biased_exp), BitCast(du16, k1075)));
  const VU mantissa = BitCast(du, v) & Set(du, (1ULL << 52) - 1);
  // Include implicit 1-bit
  VU int53 = (mantissa | Set(du, 1ULL << 52)) >> shift_mnt;
  // WASM clamps shift count; zero if greater.
  const MI tiny = BitCast(di, shift_mnt) > Set(di, 63);
  int53 = IfThenZeroElse(RebindMask(du, tiny), int53);

  // For inputs larger than 2^53 - 1, insert zeros at the bottom.
  // For inputs less than 2^63, the implicit 1-bit is guaranteed not to be
  // shifted out of the left shift result below as shift_int[i] <= 10 is true
  // for any inputs that are less than 2^63.
  const VU shifted = int53 << shift_int;

  // Saturate to LimitsMin (unchanged when negating below) or LimitsMax.
  const VI sign_mask = BroadcastSignBit(BitCast(di, v));
  const VI limit = Set(di, LimitsMax<int64_t>()) - sign_mask;
  const VI magnitude = IfThenElse(in_range, BitCast(di, shifted), limit);

  // If the input was negative, negate the integer (two's complement).
  return (magnitude ^ sign_mask) - sign_mask;
}

template <class DU, HWY_IF_U64_D(DU)>
HWY_API VFromD<DU> ConvertTo(DU du, VFromD<Rebind<double, DU>> v) {
  const RebindToSigned<decltype(du)> di;
  using MI = MFromD<decltype(di)>;
  using VU = VFromD<decltype(du)>;
  const Repartition<uint16_t, decltype(di)> du16;
  const VU k1075 = Set(du, 1075); /* biased exponent of 2^52 */

  const auto non_neg_v = ZeroIfNegative(v);

  // Exponent indicates whether the number can be represented as int64_t.
  const VU biased_exp = ShiftRight<52>(BitCast(du, non_neg_v));
  const VU out_of_range =
      BitCast(du, VecFromMask(di, BitCast(di, biased_exp) > Set(di, 1086)));

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
  // WASM.

  // The upper 48 bits of both shift_mnt and shift_int are guaranteed to be
  // zero as the upper 48 bits of both k1075 and biased_exp are zero.

  const VU shift_mnt = BitCast(
      du, SaturatedSub(BitCast(du16, k1075), BitCast(du16, biased_exp)));
  const VU shift_int = BitCast(
      du, SaturatedSub(BitCast(du16, biased_exp), BitCast(du16, k1075)));
  const VU mantissa = BitCast(du, non_neg_v) & Set(du, (1ULL << 52) - 1);
  // Include implicit 1-bit.
  VU int53 = (mantissa | Set(du, 1ULL << 52)) >> shift_mnt;
  // WASM clamps shift count; zero if greater.
  const MI tiny = BitCast(di, shift_mnt) > Set(di, 63);
  int53 = IfThenZeroElse(RebindMask(du, tiny), int53);

  // For inputs larger than 2^53 - 1, insert zeros at the bottom.

  // For inputs less than 2^64, the implicit 1-bit is guaranteed not to be
  // shifted out of the left shift result below as shift_int[i] <= 11 is true
  // for any inputs that are less than 2^64.

  const VU shifted = int53 << shift_int;
  return (shifted | out_of_range);
}

// ------------------------------ NearestInt (Round)
template <typename T, size_t N, HWY_IF_FLOAT3264(T)>
HWY_API Vec128<MakeSigned<T>, N> NearestInt(const Vec128<T, N> v) {
  return ConvertTo(RebindToSigned<DFromV<decltype(v)>>(), Round(v));
}

// ------------------------------ DemoteToNearestInt (Round)
template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 di32,
                                        VFromD<Rebind<double, DI32>> v) {
  // No single instruction, round then demote.
  return DemoteTo(di32, Round(v));
}

// ================================================== MISC

// ------------------------------ SumsOf8 (ShiftRight, Add)
template <size_t N>
HWY_API Vec128<uint64_t, N / 8> SumsOf8(const Vec128<uint8_t, N> v) {
  const DFromV<decltype(v)> du8;
  const RepartitionToWide<decltype(du8)> du16;
  const RepartitionToWide<decltype(du16)> du32;
  const RepartitionToWide<decltype(du32)> du64;
  using VU16 = VFromD<decltype(du16)>;

  const VU16 vFDB97531 = ShiftRight<8>(BitCast(du16, v));
  const VU16 vECA86420 = And(BitCast(du16, v), Set(du16, 0xFF));
  const VU16 sFE_DC_BA_98_76_54_32_10 = Add(vFDB97531, vECA86420);

  const VU16 szz_FE_zz_BA_zz_76_zz_32 =
      BitCast(du16, ShiftRight<16>(BitCast(du32, sFE_DC_BA_98_76_54_32_10)));
  const VU16 sxx_FC_xx_B8_xx_74_xx_30 =
      Add(sFE_DC_BA_98_76_54_32_10, szz_FE_zz_BA_zz_76_zz_32);
  const VU16 szz_zz_xx_FC_zz_zz_xx_74 =
      BitCast(du16, ShiftRight<32>(BitCast(du64, sxx_FC_xx_B8_xx_74_xx_30)));
  const VU16 sxx_xx_xx_F8_xx_xx_xx_70 =
      Add(sxx_FC_xx_B8_xx_74_xx_30, szz_zz_xx_FC_zz_zz_xx_74);
  return And(BitCast(du64, sxx_xx_xx_F8_xx_xx_xx_70), Set(du64, 0xFFFF));
}

template <size_t N>
HWY_API Vec128<int64_t, N / 8> SumsOf8(const Vec128<int8_t, N> v) {
  const DFromV<decltype(v)> di8;
  const RepartitionToWide<decltype(di8)> di16;
  const RepartitionToWide<decltype(di16)> di32;
  const RepartitionToWide<decltype(di32)> di64;
  const RebindToUnsigned<decltype(di32)> du32;
  const RebindToUnsigned<decltype(di64)> du64;
  using VI16 = VFromD<decltype(di16)>;

  const VI16 vFDB97531 = ShiftRight<8>(BitCast(di16, v));
  const VI16 vECA86420 = ShiftRight<8>(ShiftLeft<8>(BitCast(di16, v)));
  const VI16 sFE_DC_BA_98_76_54_32_10 = Add(vFDB97531, vECA86420);

  const VI16 sDC_zz_98_zz_54_zz_10_zz =
      BitCast(di16, ShiftLeft<16>(BitCast(du32, sFE_DC_BA_98_76_54_32_10)));
  const VI16 sFC_xx_B8_xx_74_xx_30_xx =
      Add(sFE_DC_BA_98_76_54_32_10, sDC_zz_98_zz_54_zz_10_zz);
  const VI16 sB8_xx_zz_zz_30_xx_zz_zz =
      BitCast(di16, ShiftLeft<32>(BitCast(du64, sFC_xx_B8_xx_74_xx_30_xx)));
  const VI16 sF8_xx_xx_xx_70_xx_xx_xx =
      Add(sFC_xx_B8_xx_74_xx_30_xx, sB8_xx_zz_zz_30_xx_zz_zz);
  return ShiftRight<48>(BitCast(di64, sF8_xx_xx_xx_70_xx_xx_xx));
}

// ------------------------------ LoadMaskBits (TestBit)

namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t bits) {
  const RebindToUnsigned<decltype(d)> du;
  // Easier than Set(), which would require an >8-bit type, which would not
  // compile for T=uint8_t, N=1.
  const VFromD<D> vbits{wasm_i32x4_splat(static_cast<int32_t>(bits))};

  // Replicate bytes 8x such that each byte contains the bit that governs it.
  alignas(16) static constexpr uint8_t kRep8[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                                    1, 1, 1, 1, 1, 1, 1, 1};
  const auto rep8 = TableLookupBytes(vbits, Load(du, kRep8));

  alignas(16) static constexpr uint8_t kBit[16] = {1, 2, 4, 8, 16, 32, 64, 128,
                                                   1, 2, 4, 8, 16, 32, 64, 128};
  return RebindMask(d, TestBit(rep8, LoadDup128(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint16_t kBit[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  return RebindMask(
      d, TestBit(Set(du, static_cast<uint16_t>(bits)), Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint32_t kBit[8] = {1, 2, 4, 8};
  return RebindMask(
      d, TestBit(Set(du, static_cast<uint32_t>(bits)), Load(du, kBit)));
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> LoadMaskBits(D d, uint64_t bits) {
  const RebindToUnsigned<decltype(d)> du;
  alignas(16) static constexpr uint64_t kBit[8] = {1, 2};
  return RebindMask(d, TestBit(Set(du, bits), Load(du, kBit)));
}

}  // namespace detail

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  uint64_t mask_bits = 0;
  CopyBytes<(MaxLanes(d) + 7) / 8>(bits, &mask_bits);
  return detail::LoadMaskBits(d, mask_bits);
}

// ------------------------------ Dup128MaskFromMaskBits

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;
  return detail::LoadMaskBits(d, mask_bits);
}

// ------------------------------ Mask

namespace detail {

// Full
template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/,
                                 const Mask128<T> mask) {
  alignas(16) uint64_t lanes[2];
  wasm_v128_store(lanes, mask.raw);

  constexpr uint64_t kMagic = 0x103070F1F3F80ULL;
  const uint64_t lo = ((lanes[0] * kMagic) >> 56);
  const uint64_t hi = ((lanes[1] * kMagic) >> 48) & 0xFF00;
  return (hi + lo);
}

// 64-bit
template <typename T>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/,
                                 const Mask128<T, 8> mask) {
  constexpr uint64_t kMagic = 0x103070F1F3F80ULL;
  return (static_cast<uint64_t>(wasm_i64x2_extract_lane(mask.raw, 0)) *
          kMagic) >>
         56;
}

// 32-bit or less: need masking
template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 4)>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/,
                                 const Mask128<T, N> mask) {
  uint64_t bytes = static_cast<uint64_t>(wasm_i64x2_extract_lane(mask.raw, 0));
  // Clear potentially undefined bytes.
  bytes &= (1ULL << (N * 8)) - 1;
  constexpr uint64_t kMagic = 0x103070F1F3F80ULL;
  return (bytes * kMagic) >> 56;
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<2> /*tag*/,
                                 const Mask128<T, N> mask) {
  // Remove useless lower half of each u16 while preserving the sign bit.
  const __i16x8 zero = wasm_i16x8_splat(0);
  const Mask128<uint8_t, N> mask8{wasm_i8x16_narrow_i16x8(mask.raw, zero)};
  return BitsFromMask(hwy::SizeTag<1>(), mask8);
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<4> /*tag*/,
                                 const Mask128<T, N> mask) {
  const __i32x4 mask_i = static_cast<__i32x4>(mask.raw);
  const __i32x4 slice = wasm_i32x4_make(1, 2, 4, 8);
  const __i32x4 sliced_mask = wasm_v128_and(mask_i, slice);
  alignas(16) uint32_t lanes[4];
  wasm_v128_store(lanes, sliced_mask);
  return lanes[0] | lanes[1] | lanes[2] | lanes[3];
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<8> /*tag*/,
                                 const Mask128<T, N> mask) {
  const __i64x2 mask_i = static_cast<__i64x2>(mask.raw);
  const __i64x2 slice = wasm_i64x2_make(1, 2);
  const __i64x2 sliced_mask = wasm_v128_and(mask_i, slice);
  alignas(16) uint64_t lanes[2];
  wasm_v128_store(lanes, sliced_mask);
  return lanes[0] | lanes[1];
}

// Returns the lowest N bits for the BitsFromMask result.
template <typename T, size_t N>
constexpr uint64_t OnlyActive(uint64_t bits) {
  return ((N * sizeof(T)) == 16) ? bits : bits & ((1ull << N) - 1);
}

// Returns 0xFF for bytes with index >= N, otherwise 0.
template <size_t N>
constexpr __i8x16 BytesAbove() {
  return /**/
      (N == 0)    ? wasm_i32x4_make(-1, -1, -1, -1)
      : (N == 4)  ? wasm_i32x4_make(0, -1, -1, -1)
      : (N == 8)  ? wasm_i32x4_make(0, 0, -1, -1)
      : (N == 12) ? wasm_i32x4_make(0, 0, 0, -1)
      : (N == 16) ? wasm_i32x4_make(0, 0, 0, 0)
      : (N == 2)  ? wasm_i16x8_make(0, -1, -1, -1, -1, -1, -1, -1)
      : (N == 6)  ? wasm_i16x8_make(0, 0, 0, -1, -1, -1, -1, -1)
      : (N == 10) ? wasm_i16x8_make(0, 0, 0, 0, 0, -1, -1, -1)
      : (N == 14) ? wasm_i16x8_make(0, 0, 0, 0, 0, 0, 0, -1)
      : (N == 1)  ? wasm_i8x16_make(0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                    -1, -1, -1, -1, -1)
      : (N == 3)  ? wasm_i8x16_make(0, 0, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                                    -1, -1, -1, -1)
      : (N == 5)  ? wasm_i8x16_make(0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1, -1,
                                    -1, -1, -1, -1)
      : (N == 7)  ? wasm_i8x16_make(0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1, -1,
                                    -1, -1, -1)
      : (N == 9)  ? wasm_i8x16_make(0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1,
                                    -1, -1, -1)
      : (N == 11)
          ? wasm_i8x16_make(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1, -1)
      : (N == 13)
          ? wasm_i8x16_make(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, -1)
          : wasm_i8x16_make(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(const Mask128<T, N> mask) {
  return OnlyActive<T, N>(BitsFromMask(hwy::SizeTag<sizeof(T)>(), mask));
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<1> tag, const Mask128<T> m) {
  return PopCount(BitsFromMask(tag, m));
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<2> tag, const Mask128<T> m) {
  return PopCount(BitsFromMask(tag, m));
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<4> /*tag*/, const Mask128<T> m) {
  const __i32x4 var_shift = wasm_i32x4_make(1, 2, 4, 8);
  const __i32x4 shifted_bits = wasm_v128_and(m.raw, var_shift);
  alignas(16) uint64_t lanes[2];
  wasm_v128_store(lanes, shifted_bits);
  return PopCount(lanes[0] | lanes[1]);
}

template <typename T>
HWY_INLINE size_t CountTrue(hwy::SizeTag<8> /*tag*/, const Mask128<T> m) {
  alignas(16) int64_t lanes[2];
  wasm_v128_store(lanes, m.raw);
  return static_cast<size_t>(-(lanes[0] + lanes[1]));
}

}  // namespace detail

// `p` points to at least 8 writable bytes.
template <class D>
HWY_API size_t StoreMaskBits(D d, const MFromD<D> mask, uint8_t* bits) {
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  const size_t kNumBytes = (d.MaxLanes() + 7) / 8;
  CopyBytes<kNumBytes>(&mask_bits, bits);
  return kNumBytes;
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API size_t CountTrue(D /* tag */, const MFromD<D> m) {
  return detail::CountTrue(hwy::SizeTag<sizeof(TFromD<D>)>(), m);
}

// Partial
template <class D, typename T = TFromD<D>, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API size_t CountTrue(D d, MFromD<D> m) {
  // Ensure all undefined bytes are 0.
  const MFromD<D> mask{detail::BytesAbove<d.MaxBytes()>()};
  const Full128<T> dfull;
  return CountTrue(dfull, Mask128<T>{AndNot(mask, m).raw});
}

// Full vector
template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API bool AllFalse(D d, const MFromD<D> m) {
  const auto v8 = BitCast(Full128<int8_t>(), VecFromMask(d, m));
  return !wasm_v128_any_true(v8.raw);
}

// Full vector
namespace detail {
template <typename T>
HWY_INLINE bool AllTrue(hwy::SizeTag<1> /*tag*/, const Mask128<T> m) {
  return wasm_i8x16_all_true(m.raw);
}
template <typename T>
HWY_INLINE bool AllTrue(hwy::SizeTag<2> /*tag*/, const Mask128<T> m) {
  return wasm_i16x8_all_true(m.raw);
}
template <typename T>
HWY_INLINE bool AllTrue(hwy::SizeTag<4> /*tag*/, const Mask128<T> m) {
  return wasm_i32x4_all_true(m.raw);
}
template <typename T>
HWY_INLINE bool AllTrue(hwy::SizeTag<8> /*tag*/, const Mask128<T> m) {
  return wasm_i64x2_all_true(m.raw);
}

}  // namespace detail

template <class D, typename T = TFromD<D>>
HWY_API bool AllTrue(D /* tag */, const Mask128<T> m) {
  return detail::AllTrue(hwy::SizeTag<sizeof(T)>(), m);
}

// Partial vectors

template <class D, typename T = TFromD<D>, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API bool AllFalse(D d, const MFromD<D> m) {
  // Ensure all undefined bytes are 0.
  const MFromD<D> mask{detail::BytesAbove<d.MaxBytes()>()};
  return AllFalse(Full128<T>(), Mask128<T>{AndNot(mask, m).raw});
}

template <class D, typename T = TFromD<D>, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API bool AllTrue(D d, const MFromD<D> m) {
  // Ensure all undefined bytes are FF.
  const MFromD<D> mask{detail::BytesAbove<d.MaxBytes()>()};
  return AllTrue(Full128<T>(), Mask128<T>{Or(mask, m).raw});
}

template <class D>
HWY_API size_t FindKnownFirstTrue(D /* tag */, const MFromD<D> mask) {
  const uint32_t bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return Num0BitsBelowLS1Bit_Nonzero32(bits);
}

template <class D>
HWY_API intptr_t FindFirstTrue(D /* tag */, const MFromD<D> mask) {
  const uint32_t bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return bits ? static_cast<intptr_t>(Num0BitsBelowLS1Bit_Nonzero32(bits)) : -1;
}

template <class D>
HWY_API size_t FindKnownLastTrue(D /* tag */, const MFromD<D> mask) {
  const uint32_t bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return 31 - Num0BitsAboveMS1Bit_Nonzero32(bits);
}

template <class D>
HWY_API intptr_t FindLastTrue(D /* tag */, const MFromD<D> mask) {
  const uint32_t bits = static_cast<uint32_t>(detail::BitsFromMask(mask));
  return bits
             ? (31 - static_cast<intptr_t>(Num0BitsAboveMS1Bit_Nonzero32(bits)))
             : -1;
}

// ------------------------------ Compress

namespace detail {

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> IdxFromBits(const uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Simd<T, N, 0> d;
  const Rebind<uint8_t, decltype(d)> d8;
  const Simd<uint16_t, N, 0> du;

  // We need byte indices for TableLookupBytes (one vector's worth for each of
  // 256 combinations of 8 mask bits). Loading them directly requires 4 KiB. We
  // can instead store lane indices and convert to byte indices (2*lane + 0..1),
  // with the doubling baked into the table. Unpacking nibbles is likely more
  // costly than the higher cache footprint from storing bytes.
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

  const Vec128<uint8_t, 2 * N> byte_idx{Load(d8, table + mask_bits * 8).raw};
  const Vec128<uint16_t, N> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> IdxFromNotBits(const uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Simd<T, N, 0> d;
  const Rebind<uint8_t, decltype(d)> d8;
  const Simd<uint16_t, N, 0> du;

  // We need byte indices for TableLookupBytes (one vector's worth for each of
  // 256 combinations of 8 mask bits). Loading them directly requires 4 KiB. We
  // can instead store lane indices and convert to byte indices (2*lane + 0..1),
  // with the doubling baked into the table. Unpacking nibbles is likely more
  // costly than the higher cache footprint from storing bytes.
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

  const Vec128<uint8_t, 2 * N> byte_idx{Load(d8, table + mask_bits * 8).raw};
  const Vec128<uint16_t, N> pairs = ZipLower(byte_idx, byte_idx);
  return BitCast(d, pairs + Set(du, 0x0100));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE Vec128<T, N> IdxFromBits(const uint64_t mask_bits) {
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

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE Vec128<T, N> IdxFromNotBits(const uint64_t mask_bits) {
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

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE Vec128<T, N> IdxFromBits(const uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 4);

  // There are only 2 lanes, so we can afford to load the index vector directly.
  alignas(16) static constexpr uint8_t u8_indices[4 * 16] = {
      // PrintCompress64x2Tables
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15,
      8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2,  3,  4,  5,  6,  7,
      0, 1, 2,  3,  4,  5,  6,  7,  8, 9, 10, 11, 12, 13, 14, 15};

  const Simd<T, N, 0> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, Load(d8, u8_indices + 16 * mask_bits));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE Vec128<T, N> IdxFromNotBits(const uint64_t mask_bits) {
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

// Helper functions called by both Compress and CompressStore - avoids a
// redundant BitsFromMask in the latter.

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Compress(Vec128<T, N> v, const uint64_t mask_bits) {
  const auto idx = detail::IdxFromBits<T, N>(mask_bits);
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  return BitCast(d, TableLookupBytes(BitCast(di, v), BitCast(di, idx)));
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> CompressNot(Vec128<T, N> v, const uint64_t mask_bits) {
  const auto idx = detail::IdxFromNotBits<T, N>(mask_bits);
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  return BitCast(d, TableLookupBytes(BitCast(di, v), BitCast(di, idx)));
}

}  // namespace detail

template <typename T>
struct CompressIsPartition {
#if HWY_TARGET == HWY_WASM_EMU256
  enum { value = 0 };
#else
  enum { value = (sizeof(T) != 1) };
#endif
};

// Single lane: no-op
template <typename T>
HWY_API Vec128<T, 1> Compress(Vec128<T, 1> v, Mask128<T, 1> /*m*/) {
  return v;
}

// Two lanes: conditional swap
template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Compress(Vec128<T> v, Mask128<T> mask) {
  // If mask[1] = 1 and mask[0] = 0, then swap both halves, else keep.
  const Full128<T> d;
  const Vec128<T> m = VecFromMask(d, mask);
  const Vec128<T> maskL = DupEven(m);
  const Vec128<T> maskH = DupOdd(m);
  const Vec128<T> swap = AndNot(maskL, maskH);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

// General case, 2 or 4 byte lanes
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 4) | (1 << 2))>
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
  const Full128<T> d;
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
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v,
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
  const auto c = detail::Compress(v, mask_bits);
  StoreU(c, d, unaligned);
  return PopCount(mask_bits);
}

// ------------------------------ CompressBlendedStore
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;  // so we can support fp16/bf16
  const uint64_t mask_bits = detail::BitsFromMask(m);
  const size_t count = PopCount(mask_bits);
  const VFromD<decltype(du)> compressed =
      detail::Compress(BitCast(du, v), mask_bits);
  const MFromD<D> store_mask = RebindMask(d, FirstN(du, count));
  BlendedStore(BitCast(d, compressed), store_mask, d, unaligned);
  return count;
}

// ------------------------------ CompressBitsStore

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  uint64_t mask_bits = 0;
  constexpr size_t kN = MaxLanes(d);
  CopyBytes<(kN + 7) / 8>(bits, &mask_bits);
  if (kN < 8) {
    mask_bits &= (1ull << kN) - 1;
  }

  const auto c = detail::Compress(v, mask_bits);
  StoreU(c, d, unaligned);
  return PopCount(mask_bits);
}

// ------------------------------ StoreInterleaved2/3/4

// HWY_NATIVE_LOAD_STORE_INTERLEAVED not set, hence defined in
// generic_ops-inl.h.

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

// ------------------------------ MulEven/Odd (Load)

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulEven(Vec128<T> a, Vec128<T> b) {
  alignas(16) T mul[2];
  mul[0] = Mul128(static_cast<T>(wasm_i64x2_extract_lane(a.raw, 0)),
                  static_cast<T>(wasm_i64x2_extract_lane(b.raw, 0)), &mul[1]);
  return Load(Full128<T>(), mul);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulOdd(Vec128<T> a, Vec128<T> b) {
  alignas(16) T mul[2];
  mul[0] = Mul128(static_cast<T>(wasm_i64x2_extract_lane(a.raw, 1)),
                  static_cast<T>(wasm_i64x2_extract_lane(b.raw, 1)), &mul[1]);
  return Load(Full128<T>(), mul);
}

// ------------------------------ I64/U64 MulHigh (GetLane)
template <class T, HWY_IF_UI64(T)>
HWY_API Vec64<T> MulHigh(Vec64<T> a, Vec64<T> b) {
  T hi;
  Mul128(GetLane(a), GetLane(b), &hi);
  return Set(Full64<T>(), hi);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulHigh(Vec128<T> a, Vec128<T> b) {
  T hi_0;
  T hi_1;
  Mul128(GetLane(a), GetLane(b), &hi_0);
  Mul128(detail::ExtractLane<1>(a), detail::ExtractLane<1>(b), &hi_1);
  return Dup128VecFromValues(Full128<T>(), hi_0, hi_1);
}

// ------------------------------ WidenMulPairwiseAdd (MulAdd, PromoteEvenTo)

// Generic for all vector lengths.
template <class DF, HWY_IF_F32_D(DF),
          class VBF = VFromD<Repartition<bfloat16_t, DF>>>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
}

// Even if N=1, the input is always at least 2 lanes, hence i32x4_dot_i16x8 is
// safe.
template <class D32, HWY_IF_I32_D(D32), HWY_IF_V_SIZE_LE_D(D32, 16),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> WidenMulPairwiseAdd(D32 /* tag */, V16 a, V16 b) {
  return VFromD<D32>{wasm_i32x4_dot_i16x8(a.raw, b.raw)};
}

template <class DU32, HWY_IF_U32_D(DU32), HWY_IF_V_SIZE_LE_D(DU32, 16),
          class VU16 = VFromD<RepartitionToNarrow<DU32>>>
HWY_API VFromD<DU32> WidenMulPairwiseAdd(DU32 du32, VU16 a, VU16 b) {
  return MulAdd(PromoteEvenTo(du32, a), PromoteEvenTo(du32, b),
                Mul(PromoteOddTo(du32, a), PromoteOddTo(du32, b)));
}

// ------------------------------ ReorderWidenMulAccumulate

template <class D32, HWY_IF_UI32_D(D32), HWY_IF_V_SIZE_LE_D(D32, 16),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> ReorderWidenMulAccumulate(D32 d32, V16 a, V16 b,
                                              const VFromD<D32> sum0,
                                              VFromD<D32>& /*sum1*/) {
  return sum0 + WidenMulPairwiseAdd(d32, a, b);
}

// ------------------------------ RearrangeToOddPlusEven
template <size_t N>
HWY_API Vec128<int32_t, N> RearrangeToOddPlusEven(
    const Vec128<int32_t, N> sum0, const Vec128<int32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <size_t N>
HWY_API Vec128<uint32_t, N> RearrangeToOddPlusEven(
    const Vec128<uint32_t, N> sum0, const Vec128<uint32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <size_t N>
HWY_API Vec128<float, N> RearrangeToOddPlusEven(const Vec128<float, N> sum0,
                                                const Vec128<float, N> sum1) {
  return Add(sum0, sum1);
}

// ------------------------------ Reductions

// Nothing native, generic_ops-inl defines SumOfLanes and ReduceSum.

// ------------------------------ Lt128

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_INLINE MFromD<D> Lt128(D d, VFromD<D> a, VFromD<D> b) {
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

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_INLINE MFromD<D> Eq128(D d, VFromD<D> a, VFromD<D> b) {
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  return MaskFromVec(And(Reverse2(d, eqHL), eqHL));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_INLINE MFromD<D> Eq128Upper(D d, VFromD<D> a, VFromD<D> b) {
  const VFromD<D> eqHL = VecFromMask(d, Eq(a, b));
  return MaskFromVec(InterleaveUpper(d, eqHL, eqHL));
}

// ------------------------------ Ne128

template <class D, HWY_IF_V_SIZE_LE_D(D, 16), HWY_IF_U64_D(D)>
HWY_INLINE MFromD<D> Ne128(D d, VFromD<D> a, VFromD<D> b) {
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
HWY_INLINE VFromD<D> Min128(D d, const VFromD<D> a, const VFromD<D> b) {
  return IfThenElse(Lt128(d, a, b), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Max128(D d, const VFromD<D> a, const VFromD<D> b) {
  return IfThenElse(Lt128(d, b, a), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Min128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, a, b), a, b);
}

template <class D>
HWY_INLINE VFromD<D> Max128Upper(D d, const VFromD<D> a, const VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, b, a), a, b);
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
