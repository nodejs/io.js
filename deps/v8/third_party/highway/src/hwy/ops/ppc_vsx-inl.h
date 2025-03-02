// Copyright 2023 Google LLC
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

// 128-bit vectors for VSX/Z14
// External include guard in highway.h - see comment there.

#if HWY_TARGET == HWY_Z14 || HWY_TARGET == HWY_Z15
#define HWY_S390X_HAVE_Z14 1
#else
#define HWY_S390X_HAVE_Z14 0
#endif

#pragma push_macro("vector")
#pragma push_macro("pixel")
#pragma push_macro("bool")

#undef vector
#undef pixel
#undef bool

#if HWY_S390X_HAVE_Z14
#include <vecintrin.h>
#else
#include <altivec.h>
#endif

#pragma pop_macro("vector")
#pragma pop_macro("pixel")
#pragma pop_macro("bool")

#include "hwy/ops/shared-inl.h"

// clang's altivec.h gates some intrinsics behind #ifdef __POWER10_VECTOR__, and
// some GCC do the same for _ARCH_PWR10.
// This means we can only use POWER10-specific intrinsics in static dispatch
// mode (where the -mpower10-vector compiler flag is passed). Same for PPC9.
// On other compilers, the usual target check is sufficient.
#if !HWY_S390X_HAVE_Z14 && HWY_TARGET <= HWY_PPC9 && \
    (defined(_ARCH_PWR9) || defined(__POWER9_VECTOR__))
#define HWY_PPC_HAVE_9 1
#else
#define HWY_PPC_HAVE_9 0
#endif

#if !HWY_S390X_HAVE_Z14 && HWY_TARGET <= HWY_PPC10 && \
    (defined(_ARCH_PWR10) || defined(__POWER10_VECTOR__))
#define HWY_PPC_HAVE_10 1
#else
#define HWY_PPC_HAVE_10 0
#endif

#if HWY_S390X_HAVE_Z14 && HWY_TARGET <= HWY_Z15 && __ARCH__ >= 13
#define HWY_S390X_HAVE_Z15 1
#else
#define HWY_S390X_HAVE_Z15 0
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace detail {

template <typename T>
struct Raw128;

// Each Raw128 specialization defines the following typedefs:
// - type:
//   the backing Altivec/VSX raw vector type of the Vec128<T, N> type
// - RawBoolVec:
//   the backing Altivec/VSX raw __bool vector type of the Mask128<T, N> type
// - RawT:
//   the lane type for intrinsics, in particular vec_splat
// - AlignedRawVec:
//   the 128-bit GCC/Clang vector type for aligned loads/stores
// - UnalignedRawVec:
//   the 128-bit GCC/Clang vector type for unaligned loads/stores
#define HWY_VSX_RAW128(LANE_TYPE, RAW_VECT_LANE_TYPE, RAW_BOOL_VECT_LANE_TYPE) \
  template <>                                                                  \
  struct Raw128<LANE_TYPE> {                                                   \
    using type = __vector RAW_VECT_LANE_TYPE;                                  \
    using RawBoolVec = __vector __bool RAW_BOOL_VECT_LANE_TYPE;                \
    using RawT = RAW_VECT_LANE_TYPE;                                           \
    typedef LANE_TYPE AlignedRawVec                                            \
        __attribute__((__vector_size__(16), __aligned__(16), __may_alias__));  \
    typedef LANE_TYPE UnalignedRawVec __attribute__((                          \
        __vector_size__(16), __aligned__(alignof(LANE_TYPE)), __may_alias__)); \
  };

HWY_VSX_RAW128(int8_t, signed char, char)
HWY_VSX_RAW128(uint8_t, unsigned char, char)
HWY_VSX_RAW128(int16_t, signed short, short)     // NOLINT(runtime/int)
HWY_VSX_RAW128(uint16_t, unsigned short, short)  // NOLINT(runtime/int)
HWY_VSX_RAW128(int32_t, signed int, int)
HWY_VSX_RAW128(uint32_t, unsigned int, int)
HWY_VSX_RAW128(int64_t, signed long long, long long)     // NOLINT(runtime/int)
HWY_VSX_RAW128(uint64_t, unsigned long long, long long)  // NOLINT(runtime/int)
HWY_VSX_RAW128(float, float, int)
HWY_VSX_RAW128(double, double, long long)  // NOLINT(runtime/int)

template <>
struct Raw128<bfloat16_t> : public Raw128<uint16_t> {};

template <>
struct Raw128<float16_t> : public Raw128<uint16_t> {};

#undef HWY_VSX_RAW128

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
  typename detail::Raw128<T>::RawBoolVec raw;

  using PrivateT = T;                     // only for DFromM
  static constexpr size_t kPrivateN = N;  // only for DFromM
};

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class M>
using DFromM = Simd<typename M::PrivateT, M::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ Zero

// Returns an all-zero vector/part.
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  // There is no vec_splats for 64-bit, so we cannot rely on casting the 0
  // argument in order to select the correct overload. We instead cast the
  // return vector type; see also the comment in BitCast.
  return Vec128<T, HWY_MAX_LANES_D(D)>{
      reinterpret_cast<typename detail::Raw128<T>::type>(vec_splats(0))};
}

template <class D>
using VFromD = decltype(Zero(D()));

// ------------------------------ BitCast

template <class D, typename FromT>
HWY_API VFromD<D> BitCast(D /*d*/,
                          Vec128<FromT, Repartition<FromT, D>().MaxLanes()> v) {
  // C-style casts are not sufficient when compiling with
  // -fno-lax-vector-conversions, which will be the future default in Clang,
  // but reinterpret_cast is.
  return VFromD<D>{
      reinterpret_cast<typename detail::Raw128<TFromD<D>>::type>(v.raw)};
}

// ------------------------------ ResizeBitCast

template <class D, typename FromV>
HWY_API VFromD<D> ResizeBitCast(D /*d*/, FromV v) {
  // C-style casts are not sufficient when compiling with
  // -fno-lax-vector-conversions, which will be the future default in Clang,
  // but reinterpret_cast is.
  return VFromD<D>{
      reinterpret_cast<typename detail::Raw128<TFromD<D>>::type>(v.raw)};
}

// ------------------------------ Set

// Returns a vector/part with all lanes set to "t".
template <class D, HWY_IF_NOT_SPECIAL_FLOAT(TFromD<D>)>
HWY_API VFromD<D> Set(D /* tag */, TFromD<D> t) {
  using RawLane = typename detail::Raw128<TFromD<D>>::RawT;
  return VFromD<D>{vec_splats(static_cast<RawLane>(t))};
}

template <class D, HWY_IF_SPECIAL_FLOAT(TFromD<D>)>
HWY_API VFromD<D> Set(D d, TFromD<D> t) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, Set(du, BitCastScalar<TFromD<decltype(du)>>(t)));
}

// Returns a vector with uninitialized elements.
template <class D>
HWY_API VFromD<D> Undefined(D d) {
#if HWY_COMPILER_GCC_ACTUAL
  // Suppressing maybe-uninitialized both here and at the caller does not work,
  // so initialize.
  return Zero(d);
#elif HWY_HAS_BUILTIN(__builtin_nondeterministic_value)
  return VFromD<D>{__builtin_nondeterministic_value(Zero(d).raw)};
#else
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4700, ignored "-Wuninitialized")
  typename detail::Raw128<TFromD<D>>::type raw;
  return VFromD<decltype(d)>{raw};
  HWY_DIAGNOSTICS(pop)
#endif
}

// ------------------------------ GetLane

// Gets the single value stored in a vector/part.

template <typename T, size_t N>
HWY_API T GetLane(Vec128<T, N> v) {
  return static_cast<T>(v.raw[0]);
}

// ------------------------------ Dup128VecFromValues

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6, TFromD<D> t7,
                                      TFromD<D> t8, TFromD<D> t9, TFromD<D> t10,
                                      TFromD<D> t11, TFromD<D> t12,
                                      TFromD<D> t13, TFromD<D> t14,
                                      TFromD<D> t15) {
  const typename detail::Raw128<TFromD<D>>::type raw = {
      t0, t1, t2, t3, t4, t5, t6, t7, t8, t9, t10, t11, t12, t13, t14, t15};
  return VFromD<D>{raw};
}

template <class D, HWY_IF_UI16_D(D)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  const typename detail::Raw128<TFromD<D>>::type raw = {t0, t1, t2, t3,
                                                        t4, t5, t6, t7};
  return VFromD<D>{raw};
}

template <class D, HWY_IF_SPECIAL_FLOAT_D(D)>
HWY_API VFromD<D> Dup128VecFromValues(D d, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, Dup128VecFromValues(
             du, BitCastScalar<uint16_t>(t0), BitCastScalar<uint16_t>(t1),
             BitCastScalar<uint16_t>(t2), BitCastScalar<uint16_t>(t3),
             BitCastScalar<uint16_t>(t4), BitCastScalar<uint16_t>(t5),
             BitCastScalar<uint16_t>(t6), BitCastScalar<uint16_t>(t7)));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  const typename detail::Raw128<TFromD<D>>::type raw = {t0, t1, t2, t3};
  return VFromD<D>{raw};
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  const typename detail::Raw128<TFromD<D>>::type raw = {t0, t1};
  return VFromD<D>{raw};
}

// ================================================== LOGICAL

// ------------------------------ And

template <typename T, size_t N>
HWY_API Vec128<T, N> And(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VU{BitCast(du, a).raw & BitCast(du, b).raw});
#else
  return BitCast(d, VU{vec_and(BitCast(du, a).raw, BitCast(du, b).raw)});
#endif
}

// ------------------------------ AndNot

// Returns ~not_mask & mask.
template <typename T, size_t N>
HWY_API Vec128<T, N> AndNot(Vec128<T, N> not_mask, Vec128<T, N> mask) {
  const DFromV<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  return BitCast(
      d, VU{vec_andc(BitCast(du, mask).raw, BitCast(du, not_mask).raw)});
}

// ------------------------------ Or

template <typename T, size_t N>
HWY_API Vec128<T, N> Or(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VU{BitCast(du, a).raw | BitCast(du, b).raw});
#else
  return BitCast(d, VU{vec_or(BitCast(du, a).raw, BitCast(du, b).raw)});
#endif
}

// ------------------------------ Xor

template <typename T, size_t N>
HWY_API Vec128<T, N> Xor(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VU{BitCast(du, a).raw ^ BitCast(du, b).raw});
#else
  return BitCast(d, VU{vec_xor(BitCast(du, a).raw, BitCast(du, b).raw)});
#endif
}

// ------------------------------ Not
template <typename T, size_t N>
HWY_API Vec128<T, N> Not(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  return BitCast(d, VU{vec_nor(BitCast(du, v).raw, BitCast(du, v).raw)});
}

// ------------------------------ IsConstantRawAltivecVect
namespace detail {

template <class RawV>
static HWY_INLINE bool IsConstantRawAltivecVect(
    hwy::SizeTag<1> /* lane_size_tag */, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]) &&
         __builtin_constant_p(v[4]) && __builtin_constant_p(v[5]) &&
         __builtin_constant_p(v[6]) && __builtin_constant_p(v[7]) &&
         __builtin_constant_p(v[8]) && __builtin_constant_p(v[9]) &&
         __builtin_constant_p(v[10]) && __builtin_constant_p(v[11]) &&
         __builtin_constant_p(v[12]) && __builtin_constant_p(v[13]) &&
         __builtin_constant_p(v[14]) && __builtin_constant_p(v[15]);
}

template <class RawV>
static HWY_INLINE bool IsConstantRawAltivecVect(
    hwy::SizeTag<2> /* lane_size_tag */, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]) &&
         __builtin_constant_p(v[4]) && __builtin_constant_p(v[5]) &&
         __builtin_constant_p(v[6]) && __builtin_constant_p(v[7]);
}

template <class RawV>
static HWY_INLINE bool IsConstantRawAltivecVect(
    hwy::SizeTag<4> /* lane_size_tag */, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]) &&
         __builtin_constant_p(v[2]) && __builtin_constant_p(v[3]);
}

template <class RawV>
static HWY_INLINE bool IsConstantRawAltivecVect(
    hwy::SizeTag<8> /* lane_size_tag */, RawV v) {
  return __builtin_constant_p(v[0]) && __builtin_constant_p(v[1]);
}

template <class RawV>
static HWY_INLINE bool IsConstantRawAltivecVect(RawV v) {
  return IsConstantRawAltivecVect(hwy::SizeTag<sizeof(decltype(v[0]))>(), v);
}

}  // namespace detail

// ------------------------------ TernaryLogic
#if HWY_PPC_HAVE_10
namespace detail {

// NOTE: the kTernLogOp bits of the PPC10 TernaryLogic operation are in reverse
// order of the kTernLogOp bits of AVX3
// _mm_ternarylogic_epi64(a, b, c, kTernLogOp)
template <uint8_t kTernLogOp, class V>
HWY_INLINE V TernaryLogic(V a, V b, V c) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  using VU = VFromD<decltype(du)>;
  const auto a_raw = BitCast(du, a).raw;
  const auto b_raw = BitCast(du, b).raw;
  const auto c_raw = BitCast(du, c).raw;

#if HWY_COMPILER_GCC_ACTUAL
  // Use inline assembly on GCC to work around GCC compiler bug
  typename detail::Raw128<TFromV<VU>>::type raw_ternlog_result;
  __asm__("xxeval %x0,%x1,%x2,%x3,%4"
          : "=wa"(raw_ternlog_result)
          : "wa"(a_raw), "wa"(b_raw), "wa"(c_raw),
            "n"(static_cast<unsigned>(kTernLogOp))
          :);
#else
  const auto raw_ternlog_result =
      vec_ternarylogic(a_raw, b_raw, c_raw, kTernLogOp);
#endif

  return BitCast(d, VU{raw_ternlog_result});
}

}  // namespace detail
#endif  // HWY_PPC_HAVE_10

// ------------------------------ Xor3
template <typename T, size_t N>
HWY_API Vec128<T, N> Xor3(Vec128<T, N> x1, Vec128<T, N> x2, Vec128<T, N> x3) {
#if HWY_PPC_HAVE_10
#if defined(__OPTIMIZE__)
  if (static_cast<int>(detail::IsConstantRawAltivecVect(x1.raw)) +
          static_cast<int>(detail::IsConstantRawAltivecVect(x2.raw)) +
          static_cast<int>(detail::IsConstantRawAltivecVect(x3.raw)) >=
      2) {
    return Xor(x1, Xor(x2, x3));
  } else  // NOLINT
#endif
  {
    return detail::TernaryLogic<0x69>(x1, x2, x3);
  }
#else
  return Xor(x1, Xor(x2, x3));
#endif
}

// ------------------------------ Or3
template <typename T, size_t N>
HWY_API Vec128<T, N> Or3(Vec128<T, N> o1, Vec128<T, N> o2, Vec128<T, N> o3) {
#if HWY_PPC_HAVE_10
#if defined(__OPTIMIZE__)
  if (static_cast<int>(detail::IsConstantRawAltivecVect(o1.raw)) +
          static_cast<int>(detail::IsConstantRawAltivecVect(o2.raw)) +
          static_cast<int>(detail::IsConstantRawAltivecVect(o3.raw)) >=
      2) {
    return Or(o1, Or(o2, o3));
  } else  // NOLINT
#endif
  {
    return detail::TernaryLogic<0x7F>(o1, o2, o3);
  }
#else
  return Or(o1, Or(o2, o3));
#endif
}

// ------------------------------ OrAnd
template <typename T, size_t N>
HWY_API Vec128<T, N> OrAnd(Vec128<T, N> o, Vec128<T, N> a1, Vec128<T, N> a2) {
#if HWY_PPC_HAVE_10
#if defined(__OPTIMIZE__)
  if (detail::IsConstantRawAltivecVect(a1.raw) &&
      detail::IsConstantRawAltivecVect(a2.raw)) {
    return Or(o, And(a1, a2));
  } else  // NOLINT
#endif
  {
    return detail::TernaryLogic<0x1F>(o, a1, a2);
  }
#else
  return Or(o, And(a1, a2));
#endif
}

// ------------------------------ IfVecThenElse
template <typename T, size_t N>
HWY_API Vec128<T, N> IfVecThenElse(Vec128<T, N> mask, Vec128<T, N> yes,
                                   Vec128<T, N> no) {
  const DFromV<decltype(yes)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, VFromD<decltype(du)>{vec_sel(BitCast(du, no).raw, BitCast(du, yes).raw,
                                      BitCast(du, mask).raw)});
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
HWY_API Vec128<T, N> operator&(Vec128<T, N> a, Vec128<T, N> b) {
  return And(a, b);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> operator|(Vec128<T, N> a, Vec128<T, N> b) {
  return Or(a, b);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> operator^(Vec128<T, N> a, Vec128<T, N> b) {
  return Xor(a, b);
}

// ================================================== SIGN

// ------------------------------ Neg

template <typename T, size_t N, HWY_IF_SIGNED(T)>
HWY_API Vec128<T, N> Neg(Vec128<T, N> v) {
  // If T is an signed integer type, use Zero(d) - v instead of vec_neg to
  // avoid undefined behavior in the case where v[i] == LimitsMin<T>()
  const DFromV<decltype(v)> d;
  return Zero(d) - v;
}

template <typename T, size_t N, HWY_IF_FLOAT3264(T)>
HWY_API Vec128<T, N> Neg(Vec128<T, N> v) {
#if HWY_S390X_HAVE_Z14
  return Xor(v, SignBit(DFromV<decltype(v)>()));
#else
  return Vec128<T, N>{vec_neg(v.raw)};
#endif
}

template <typename T, size_t N, HWY_IF_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> Neg(const Vec128<T, N> v) {
  return Xor(v, SignBit(DFromV<decltype(v)>()));
}

// ------------------------------ Abs

// Returns absolute value, except that LimitsMin() maps to LimitsMax() + 1.
template <class T, size_t N, HWY_IF_SIGNED(T)>
HWY_API Vec128<T, N> Abs(Vec128<T, N> v) {
  // If T is a signed integer type, use Max(v, Neg(v)) instead of vec_abs to
  // avoid undefined behavior in the case where v[i] == LimitsMin<T>().
  return Max(v, Neg(v));
}

template <class T, size_t N, HWY_IF_FLOAT3264(T)>
HWY_API Vec128<T, N> Abs(Vec128<T, N> v) {
  return Vec128<T, N>{vec_abs(v.raw)};
}

// ------------------------------ CopySign

#if HWY_S390X_HAVE_Z14
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
#else  // VSX
template <size_t N>
HWY_API Vec128<float, N> CopySign(Vec128<float, N> magn,
                                  Vec128<float, N> sign) {
  // Work around compiler bugs that are there with vec_cpsgn on older versions
  // of GCC/Clang
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1200
  return Vec128<float, N>{__builtin_vec_copysign(magn.raw, sign.raw)};
#elif HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200 && \
    HWY_HAS_BUILTIN(__builtin_vsx_xvcpsgnsp)
  return Vec128<float, N>{__builtin_vsx_xvcpsgnsp(magn.raw, sign.raw)};
#else
  return Vec128<float, N>{vec_cpsgn(sign.raw, magn.raw)};
#endif
}

template <size_t N>
HWY_API Vec128<double, N> CopySign(Vec128<double, N> magn,
                                   Vec128<double, N> sign) {
  // Work around compiler bugs that are there with vec_cpsgn on older versions
  // of GCC/Clang
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1200
  return Vec128<double, N>{__builtin_vec_copysign(magn.raw, sign.raw)};
#elif HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1200 && \
    HWY_HAS_BUILTIN(__builtin_vsx_xvcpsgndp)
  return Vec128<double, N>{__builtin_vsx_xvcpsgndp(magn.raw, sign.raw)};
#else
  return Vec128<double, N>{vec_cpsgn(sign.raw, magn.raw)};
#endif
}
#endif  // HWY_S390X_HAVE_Z14

template <typename T, size_t N>
HWY_API Vec128<T, N> CopySignToAbs(Vec128<T, N> abs, Vec128<T, N> sign) {
  // PPC8 can also handle abs < 0, so no extra action needed.
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  return CopySign(abs, sign);
}

// ================================================== MEMORY (1)

// Note: type punning is safe because the types are tagged with may_alias.
// (https://godbolt.org/z/fqrWjfjsP)

// ------------------------------ Load

template <class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API Vec128<T> Load(D /* tag */, const T* HWY_RESTRICT aligned) {
// Suppress the ignoring attributes warning that is generated by
// HWY_RCAST_ALIGNED(const LoadRaw*, aligned) with GCC
#if HWY_COMPILER_GCC
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4649, ignored "-Wignored-attributes")
#endif

  using LoadRaw = typename detail::Raw128<T>::AlignedRawVec;
  const LoadRaw* HWY_RESTRICT p = HWY_RCAST_ALIGNED(const LoadRaw*, aligned);
  using ResultRaw = typename detail::Raw128<T>::type;
  return Vec128<T>{reinterpret_cast<ResultRaw>(*p)};

#if HWY_COMPILER_GCC
  HWY_DIAGNOSTICS(pop)
#endif
}

// Any <= 64 bit
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), typename T = TFromD<D>>
HWY_API VFromD<D> Load(D d, const T* HWY_RESTRICT p) {
  using BitsT = UnsignedFromSize<d.MaxBytes()>;

  BitsT bits;
  const Repartition<BitsT, decltype(d)> d_bits;
  CopyBytes<d.MaxBytes()>(p, &bits);
  return BitCast(d, Set(d_bits, bits));
}

// ================================================== MASK

// ------------------------------ Mask

// Mask and Vec are both backed by vector types (true = FF..FF).
template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(Vec128<T, N> v) {
  using Raw = typename detail::Raw128<T>::RawBoolVec;
  return Mask128<T, N>{reinterpret_cast<Raw>(v.raw)};
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <typename T, size_t N>
HWY_API Vec128<T, N> VecFromMask(Mask128<T, N> v) {
  return Vec128<T, N>{
      reinterpret_cast<typename detail::Raw128<T>::type>(v.raw)};
}

template <class D>
HWY_API VFromD<D> VecFromMask(D /* tag */, MFromD<D> v) {
  return VFromD<D>{
      reinterpret_cast<typename detail::Raw128<TFromD<D>>::type>(v.raw)};
}

// mask ? yes : no
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  const DFromV<decltype(yes)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, VFromD<decltype(du)>{vec_sel(
                        BitCast(du, no).raw, BitCast(du, yes).raw, mask.raw)});
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

// ------------------------------ Mask logical

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(Mask128<T, N> m) {
  return Mask128<T, N>{vec_nor(m.raw, m.raw)};
}

template <typename T, size_t N>
HWY_API Mask128<T, N> And(Mask128<T, N> a, Mask128<T, N> b) {
#if HWY_S390X_HAVE_Z14
  return Mask128<T, N>{a.raw & b.raw};
#else
  return Mask128<T, N>{vec_and(a.raw, b.raw)};
#endif
}

template <typename T, size_t N>
HWY_API Mask128<T, N> AndNot(Mask128<T, N> a, Mask128<T, N> b) {
  return Mask128<T, N>{vec_andc(b.raw, a.raw)};
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Or(Mask128<T, N> a, Mask128<T, N> b) {
#if HWY_S390X_HAVE_Z14
  return Mask128<T, N>{a.raw | b.raw};
#else
  return Mask128<T, N>{vec_or(a.raw, b.raw)};
#endif
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Xor(Mask128<T, N> a, Mask128<T, N> b) {
#if HWY_S390X_HAVE_Z14
  return Mask128<T, N>{a.raw ^ b.raw};
#else
  return Mask128<T, N>{vec_xor(a.raw, b.raw)};
#endif
}

template <typename T, size_t N>
HWY_API Mask128<T, N> ExclusiveNeither(Mask128<T, N> a, Mask128<T, N> b) {
  return Mask128<T, N>{vec_nor(a.raw, b.raw)};
}

// ------------------------------ ShiftLeftSame

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> ShiftLeftSame(Vec128<T, N> v, const int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;

#if HWY_S390X_HAVE_Z14
  return BitCast(d,
                 VFromD<decltype(du)>{BitCast(du, v).raw
                                      << Set(du, static_cast<TU>(bits)).raw});
#else
  // Do an unsigned vec_sl operation to avoid undefined behavior
  return BitCast(
      d, VFromD<decltype(du)>{
             vec_sl(BitCast(du, v).raw, Set(du, static_cast<TU>(bits)).raw)});
#endif
}

// ------------------------------ ShiftRightSame

template <typename T, size_t N, HWY_IF_UNSIGNED(T)>
HWY_API Vec128<T, N> ShiftRightSame(Vec128<T, N> v, const int bits) {
  using TU = typename detail::Raw128<MakeUnsigned<T>>::RawT;
#if HWY_S390X_HAVE_Z14
  return Vec128<T, N>{v.raw >> vec_splats(static_cast<TU>(bits))};
#else
  return Vec128<T, N>{vec_sr(v.raw, vec_splats(static_cast<TU>(bits)))};
#endif
}

template <typename T, size_t N, HWY_IF_SIGNED(T)>
HWY_API Vec128<T, N> ShiftRightSame(Vec128<T, N> v, const int bits) {
#if HWY_S390X_HAVE_Z14
  using TI = typename detail::Raw128<T>::RawT;
  return Vec128<T, N>{v.raw >> vec_splats(static_cast<TI>(bits))};
#else
  using TU = typename detail::Raw128<MakeUnsigned<T>>::RawT;
  return Vec128<T, N>{vec_sra(v.raw, vec_splats(static_cast<TU>(bits)))};
#endif
}

// ------------------------------ ShiftLeft

template <int kBits, typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> ShiftLeft(Vec128<T, N> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  return ShiftLeftSame(v, kBits);
}

// ------------------------------ ShiftRight

template <int kBits, typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> ShiftRight(Vec128<T, N> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  return ShiftRightSame(v, kBits);
}

// ------------------------------ BroadcastSignBit

template <typename T, size_t N, HWY_IF_SIGNED(T)>
HWY_API Vec128<T, N> BroadcastSignBit(Vec128<T, N> v) {
  return ShiftRightSame(v, static_cast<int>(sizeof(T) * 8 - 1));
}

// ================================================== SWIZZLE (1)

// ------------------------------ TableLookupBytes
template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytes(Vec128<T, N> bytes,
                                        Vec128<TI, NI> from) {
  const Repartition<uint8_t, DFromV<decltype(from)>> du8_from;
  return Vec128<TI, NI>{reinterpret_cast<typename detail::Raw128<TI>::type>(
      vec_perm(bytes.raw, bytes.raw, BitCast(du8_from, from).raw))};
}

// ------------------------------ TableLookupBytesOr0
// For all vector widths; Altivec/VSX needs zero out
template <class V, class VI>
HWY_API VI TableLookupBytesOr0(const V bytes, const VI from) {
  const DFromV<VI> di;
  Repartition<int8_t, decltype(di)> di8;
  const VI zeroOutMask = BitCast(di, BroadcastSignBit(BitCast(di8, from)));
  return AndNot(zeroOutMask, TableLookupBytes(bytes, from));
}

// ------------------------------ Reverse
template <class D, typename T = TFromD<D>, HWY_IF_LANES_GT_D(D, 1)>
HWY_API Vec128<T> Reverse(D /* tag */, Vec128<T> v) {
  return Vec128<T>{vec_reve(v.raw)};
}

// ------------------------------ Shuffles (Reverse)

// Notation: let Vec128<int32_t> have lanes 3,2,1,0 (0 is least-significant).
// Shuffle0321 rotates one lane to the right (the previous least-significant
// lane is now most-significant). These could also be implemented via
// CombineShiftRightBytes but the shuffle_abcd notation is more convenient.

// Swap 32-bit halves in 64-bit halves.
template <typename T, size_t N>
HWY_API Vec128<T, N> Shuffle2301(Vec128<T, N> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit lanes");
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  const __vector unsigned char kShuffle = {4,  5,  6,  7,  0, 1, 2,  3,
                                           12, 13, 14, 15, 8, 9, 10, 11};
  return Vec128<T, N>{vec_perm(v.raw, v.raw, kShuffle)};
}

// These are used by generic_ops-inl to implement LoadInterleaved3. As with
// Intel's shuffle* intrinsics and InterleaveLower, the lower half of the output
// comes from the first argument.
namespace detail {

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo2301(Vec32<T> a, Vec32<T> b) {
  const __vector unsigned char kShuffle16 = {1, 0, 19, 18};
  return Vec32<T>{vec_perm(a.raw, b.raw, kShuffle16)};
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo2301(Vec64<T> a, Vec64<T> b) {
  const __vector unsigned char kShuffle = {2, 3, 0, 1, 22, 23, 20, 21};
  return Vec64<T>{vec_perm(a.raw, b.raw, kShuffle)};
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo2301(Vec128<T> a, Vec128<T> b) {
  const __vector unsigned char kShuffle = {4,  5,  6,  7,  0,  1,  2,  3,
                                           28, 29, 30, 31, 24, 25, 26, 27};
  return Vec128<T>{vec_perm(a.raw, b.raw, kShuffle)};
}

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo1230(Vec32<T> a, Vec32<T> b) {
  const __vector unsigned char kShuffle = {0, 3, 18, 17};
  return Vec32<T>{vec_perm(a.raw, b.raw, kShuffle)};
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo1230(Vec64<T> a, Vec64<T> b) {
  const __vector unsigned char kShuffle = {0, 1, 6, 7, 20, 21, 18, 19};
  return Vec64<T>{vec_perm(a.raw, b.raw, kShuffle)};
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo1230(Vec128<T> a, Vec128<T> b) {
  const __vector unsigned char kShuffle = {0,  1,  2,  3,  12, 13, 14, 15,
                                           24, 25, 26, 27, 20, 21, 22, 23};
  return Vec128<T>{vec_perm(a.raw, b.raw, kShuffle)};
}

template <typename T, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ShuffleTwo3012(Vec32<T> a, Vec32<T> b) {
  const __vector unsigned char kShuffle = {2, 1, 16, 19};
  return Vec32<T>{vec_perm(a.raw, b.raw, kShuffle)};
}
template <typename T, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ShuffleTwo3012(Vec64<T> a, Vec64<T> b) {
  const __vector unsigned char kShuffle = {4, 5, 2, 3, 16, 17, 22, 23};
  return Vec64<T>{vec_perm(a.raw, b.raw, kShuffle)};
}
template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ShuffleTwo3012(Vec128<T> a, Vec128<T> b) {
  const __vector unsigned char kShuffle = {8,  9,  10, 11, 4,  5,  6,  7,
                                           16, 17, 18, 19, 28, 29, 30, 31};
  return Vec128<T>{vec_perm(a.raw, b.raw, kShuffle)};
}

}  // namespace detail

// Swap 64-bit halves
template <class T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Shuffle1032(Vec128<T> v) {
  const Full128<T> d;
  const Full128<uint64_t> du64;
  return BitCast(d, Reverse(du64, BitCast(du64, v)));
}
template <class T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Shuffle01(Vec128<T> v) {
  return Reverse(Full128<T>(), v);
}

// Rotate right 32 bits
template <class T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Shuffle0321(Vec128<T> v) {
#if HWY_IS_LITTLE_ENDIAN
  return Vec128<T>{vec_sld(v.raw, v.raw, 12)};
#else
  return Vec128<T>{vec_sld(v.raw, v.raw, 4)};
#endif
}
// Rotate left 32 bits
template <class T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Shuffle2103(Vec128<T> v) {
#if HWY_IS_LITTLE_ENDIAN
  return Vec128<T>{vec_sld(v.raw, v.raw, 4)};
#else
  return Vec128<T>{vec_sld(v.raw, v.raw, 12)};
#endif
}

template <class T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> Shuffle0123(Vec128<T> v) {
  return Reverse(Full128<T>(), v);
}

// ================================================== COMPARE

// Comparisons fill a lane with 1-bits if the condition is true, else 0.

template <class DTo, typename TFrom, size_t NFrom>
HWY_API MFromD<DTo> RebindMask(DTo /*dto*/, Mask128<TFrom, NFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TFromD<DTo>), "Must have same size");
  return MFromD<DTo>{m.raw};
}

template <typename T, size_t N>
HWY_API Mask128<T, N> TestBit(Vec128<T, N> v, Vec128<T, N> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return (v & bit) == bit;
}

// ------------------------------ Equality

template <typename T, size_t N>
HWY_API Mask128<T, N> operator==(Vec128<T, N> a, Vec128<T, N> b) {
  return Mask128<T, N>{vec_cmpeq(a.raw, b.raw)};
}

// ------------------------------ Inequality

// This cannot have T as a template argument, otherwise it is not more
// specialized than rewritten operator== in C++20, leading to compile
// errors: https://gcc.godbolt.org/z/xsrPhPvPT.
template <size_t N>
HWY_API Mask128<uint8_t, N> operator!=(Vec128<uint8_t, N> a,
                                       Vec128<uint8_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<uint8_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<uint16_t, N> operator!=(Vec128<uint16_t, N> a,
                                        Vec128<uint16_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<uint16_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<uint32_t, N> operator!=(Vec128<uint32_t, N> a,
                                        Vec128<uint32_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<uint32_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<uint64_t, N> operator!=(Vec128<uint64_t, N> a,
                                        Vec128<uint64_t, N> b) {
  return Not(a == b);
}
template <size_t N>
HWY_API Mask128<int8_t, N> operator!=(Vec128<int8_t, N> a,
                                      Vec128<int8_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<int8_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<int16_t, N> operator!=(Vec128<int16_t, N> a,
                                       Vec128<int16_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<int16_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<int32_t, N> operator!=(Vec128<int32_t, N> a,
                                       Vec128<int32_t, N> b) {
#if HWY_PPC_HAVE_9
  return Mask128<int32_t, N>{vec_cmpne(a.raw, b.raw)};
#else
  return Not(a == b);
#endif
}
template <size_t N>
HWY_API Mask128<int64_t, N> operator!=(Vec128<int64_t, N> a,
                                       Vec128<int64_t, N> b) {
  return Not(a == b);
}

template <size_t N>
HWY_API Mask128<float, N> operator!=(Vec128<float, N> a, Vec128<float, N> b) {
  return Not(a == b);
}

template <size_t N>
HWY_API Mask128<double, N> operator!=(Vec128<double, N> a,
                                      Vec128<double, N> b) {
  return Not(a == b);
}

// ------------------------------ Strict inequality

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_INLINE Mask128<T, N> operator>(Vec128<T, N> a, Vec128<T, N> b) {
  return Mask128<T, N>{vec_cmpgt(a.raw, b.raw)};
}

// ------------------------------ Weak inequality

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Mask128<T, N> operator>=(Vec128<T, N> a, Vec128<T, N> b) {
  return Mask128<T, N>{vec_cmpge(a.raw, b.raw)};
}

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Mask128<T, N> operator>=(Vec128<T, N> a, Vec128<T, N> b) {
  return Not(b > a);
}

// ------------------------------ Reversed comparisons

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Mask128<T, N> operator<(Vec128<T, N> a, Vec128<T, N> b) {
  return b > a;
}

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Mask128<T, N> operator<=(Vec128<T, N> a, Vec128<T, N> b) {
  return b >= a;
}

// ================================================== MEMORY (2)

// ------------------------------ Load
template <class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API Vec128<T> LoadU(D /* tag */, const T* HWY_RESTRICT p) {
  using LoadRaw = typename detail::Raw128<T>::UnalignedRawVec;
  const LoadRaw* HWY_RESTRICT praw = reinterpret_cast<const LoadRaw*>(p);
  using ResultRaw = typename detail::Raw128<T>::type;
  return Vec128<T>{reinterpret_cast<ResultRaw>(*praw)};
}

// For < 128 bit, LoadU == Load.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), typename T = TFromD<D>>
HWY_API VFromD<D> LoadU(D d, const T* HWY_RESTRICT p) {
  return Load(d, p);
}

// 128-bit SIMD => nothing to duplicate, same as an unaligned load.
template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> LoadDup128(D d, const T* HWY_RESTRICT p) {
  return LoadU(d, p);
}

#if (HWY_PPC_HAVE_9 && HWY_ARCH_PPC_64) || HWY_S390X_HAVE_Z14
#ifdef HWY_NATIVE_LOAD_N
#undef HWY_NATIVE_LOAD_N
#else
#define HWY_NATIVE_LOAD_N
#endif

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> LoadN(D d, const T* HWY_RESTRICT p,
                        size_t max_lanes_to_load) {
#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(max_lanes_to_load) && max_lanes_to_load == 0) {
    return Zero(d);
  }

  if (__builtin_constant_p(max_lanes_to_load >= HWY_MAX_LANES_D(D)) &&
      max_lanes_to_load >= HWY_MAX_LANES_D(D)) {
    return LoadU(d, p);
  }
#endif

  const size_t num_of_bytes_to_load =
      HWY_MIN(max_lanes_to_load, HWY_MAX_LANES_D(D)) * sizeof(TFromD<D>);
  const Repartition<uint8_t, decltype(d)> du8;
#if HWY_S390X_HAVE_Z14
  return (num_of_bytes_to_load > 0)
             ? BitCast(d, VFromD<decltype(du8)>{vec_load_len(
                              const_cast<unsigned char*>(
                                  reinterpret_cast<const unsigned char*>(p)),
                              static_cast<unsigned>(num_of_bytes_to_load - 1))})
             : Zero(d);
#else
  return BitCast(
      d,
      VFromD<decltype(du8)>{vec_xl_len(
          const_cast<unsigned char*>(reinterpret_cast<const unsigned char*>(p)),
          num_of_bytes_to_load)});
#endif
}

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> LoadNOr(VFromD<D> no, D d, const T* HWY_RESTRICT p,
                          size_t max_lanes_to_load) {
#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(max_lanes_to_load) && max_lanes_to_load == 0) {
    return no;
  }

  if (__builtin_constant_p(max_lanes_to_load >= HWY_MAX_LANES_D(D)) &&
      max_lanes_to_load >= HWY_MAX_LANES_D(D)) {
    return LoadU(d, p);
  }
#endif

  return IfThenElse(FirstN(d, max_lanes_to_load),
                    LoadN(d, p, max_lanes_to_load), no);
}

#endif  // HWY_PPC_HAVE_9 || HWY_S390X_HAVE_Z14

// Returns a vector with lane i=[0, N) set to "first" + i.
namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<D> Iota0(D d) {
  constexpr __vector unsigned char kU8Iota0 = {0, 1, 2,  3,  4,  5,  6,  7,
                                               8, 9, 10, 11, 12, 13, 14, 15};
  return BitCast(d, VFromD<RebindToUnsigned<D>>{kU8Iota0});
}

template <class D, HWY_IF_T_SIZE_D(D, 2), HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
HWY_INLINE VFromD<D> Iota0(D d) {
  constexpr __vector unsigned short kU16Iota0 = {0, 1, 2, 3, 4, 5, 6, 7};
  return BitCast(d, VFromD<RebindToUnsigned<D>>{kU16Iota0});
}

template <class D, HWY_IF_UI32_D(D)>
HWY_INLINE VFromD<D> Iota0(D d) {
  constexpr __vector unsigned int kU32Iota0 = {0, 1, 2, 3};
  return BitCast(d, VFromD<RebindToUnsigned<D>>{kU32Iota0});
}

template <class D, HWY_IF_UI64_D(D)>
HWY_INLINE VFromD<D> Iota0(D d) {
  constexpr __vector unsigned long long kU64Iota0 = {0, 1};
  return BitCast(d, VFromD<RebindToUnsigned<D>>{kU64Iota0});
}

template <class D, HWY_IF_F32_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  constexpr __vector float kF32Iota0 = {0.0f, 1.0f, 2.0f, 3.0f};
  return VFromD<D>{kF32Iota0};
}

template <class D, HWY_IF_F64_D(D)>
HWY_INLINE VFromD<D> Iota0(D /*d*/) {
  constexpr __vector double kF64Iota0 = {0.0, 1.0};
  return VFromD<D>{kF64Iota0};
}

}  // namespace detail

template <class D, typename T2>
HWY_API VFromD<D> Iota(D d, const T2 first) {
  return detail::Iota0(d) + Set(d, static_cast<TFromD<D>>(first));
}

// ------------------------------ FirstN (Iota, Lt)

template <class D>
HWY_API MFromD<D> FirstN(D d, size_t num) {
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  return RebindMask(d, Iota(du, 0) < Set(du, static_cast<TU>(num)));
}

// ------------------------------ MaskedLoad
template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d, const T* HWY_RESTRICT p) {
  return IfThenElseZero(m, LoadU(d, p));
}

// ------------------------------ MaskedLoadOr
template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const T* HWY_RESTRICT p) {
  return IfThenElse(m, LoadU(d, p), v);
}

// ------------------------------ Store

template <class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API void Store(Vec128<T> v, D /* tag */, T* HWY_RESTRICT aligned) {
// Suppress the ignoring attributes warning that is generated by
// HWY_RCAST_ALIGNED(StoreRaw*, aligned) with GCC
#if HWY_COMPILER_GCC
  HWY_DIAGNOSTICS(push)
  HWY_DIAGNOSTICS_OFF(disable : 4649, ignored "-Wignored-attributes")
#endif

  using StoreRaw = typename detail::Raw128<T>::AlignedRawVec;
  *HWY_RCAST_ALIGNED(StoreRaw*, aligned) = reinterpret_cast<StoreRaw>(v.raw);

#if HWY_COMPILER_GCC
  HWY_DIAGNOSTICS(pop)
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API void StoreU(Vec128<T> v, D /* tag */, T* HWY_RESTRICT p) {
  using StoreRaw = typename detail::Raw128<T>::UnalignedRawVec;
  *reinterpret_cast<StoreRaw*>(p) = reinterpret_cast<StoreRaw>(v.raw);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), typename T = TFromD<D>>
HWY_API void Store(VFromD<D> v, D d, T* HWY_RESTRICT p) {
  using BitsT = UnsignedFromSize<d.MaxBytes()>;

  const Repartition<BitsT, decltype(d)> d_bits;
  const BitsT bits = GetLane(BitCast(d_bits, v));
  CopyBytes<d.MaxBytes()>(&bits, p);
}

// For < 128 bit, StoreU == Store.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), typename T = TFromD<D>>
HWY_API void StoreU(VFromD<D> v, D d, T* HWY_RESTRICT p) {
  Store(v, d, p);
}

#if (HWY_PPC_HAVE_9 && HWY_ARCH_PPC_64) || HWY_S390X_HAVE_Z14

#ifdef HWY_NATIVE_STORE_N
#undef HWY_NATIVE_STORE_N
#else
#define HWY_NATIVE_STORE_N
#endif

template <class D, typename T = TFromD<D>>
HWY_API void StoreN(VFromD<D> v, D d, T* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
#if HWY_COMPILER_GCC && !HWY_IS_DEBUG_BUILD
  if (__builtin_constant_p(max_lanes_to_store) && max_lanes_to_store == 0) {
    return;
  }

  if (__builtin_constant_p(max_lanes_to_store >= HWY_MAX_LANES_D(D)) &&
      max_lanes_to_store >= HWY_MAX_LANES_D(D)) {
    StoreU(v, d, p);
    return;
  }
#endif

  const size_t num_of_bytes_to_store =
      HWY_MIN(max_lanes_to_store, HWY_MAX_LANES_D(D)) * sizeof(TFromD<D>);
  const Repartition<uint8_t, decltype(d)> du8;
#if HWY_S390X_HAVE_Z14
  if (num_of_bytes_to_store > 0) {
    vec_store_len(BitCast(du8, v).raw, reinterpret_cast<unsigned char*>(p),
                  static_cast<unsigned>(num_of_bytes_to_store - 1));
  }
#else
  vec_xst_len(BitCast(du8, v).raw, reinterpret_cast<unsigned char*>(p),
              num_of_bytes_to_store);
#endif
}
#endif

// ------------------------------ BlendedStore

template <class D>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  const VFromD<D> old = LoadU(d, p);
  StoreU(IfThenElse(RebindMask(d, m), v, old), d, p);
}

// ================================================== ARITHMETIC

namespace detail {
// If TFromD<D> is an integer type, detail::RebindToUnsignedIfNotFloat<D>
// rebinds D to MakeUnsigned<TFromD<D>>.

// Otherwise, if TFromD<D> is a floating-point type (including F16 and BF16),
// detail::RebindToUnsignedIfNotFloat<D> is the same as D.
template <class D>
using RebindToUnsignedIfNotFloat =
    hwy::If<(!hwy::IsFloat<TFromD<D>>() && !hwy::IsSpecialFloat<TFromD<D>>()),
            RebindToUnsigned<D>, D>;
}  // namespace detail

// ------------------------------ Addition

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> operator+(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const detail::RebindToUnsignedIfNotFloat<decltype(d)> d_arith;

  // If T is an integer type, do an unsigned vec_add to avoid undefined behavior
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VFromD<decltype(d_arith)>{BitCast(d_arith, a).raw +
                                              BitCast(d_arith, b).raw});
#else
  return BitCast(d, VFromD<decltype(d_arith)>{vec_add(
                        BitCast(d_arith, a).raw, BitCast(d_arith, b).raw)});
#endif
}

// ------------------------------ Subtraction

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> operator-(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const detail::RebindToUnsignedIfNotFloat<decltype(d)> d_arith;

  // If T is an integer type, do an unsigned vec_sub to avoid undefined behavior
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VFromD<decltype(d_arith)>{BitCast(d_arith, a).raw -
                                              BitCast(d_arith, b).raw});
#else
  return BitCast(d, VFromD<decltype(d_arith)>{vec_sub(
                        BitCast(d_arith, a).raw, BitCast(d_arith, b).raw)});
#endif
}

// ------------------------------ SumsOf8
template <class V, HWY_IF_U8(TFromV<V>)>
HWY_API VFromD<RepartitionToWideX3<DFromV<V>>> SumsOf8(V v) {
  return SumsOf2(SumsOf4(v));
}

template <class V, HWY_IF_I8(TFromV<V>)>
HWY_API VFromD<RepartitionToWideX3<DFromV<V>>> SumsOf8(V v) {
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> di8;
  const RebindToUnsigned<decltype(di8)> du8;
  const RepartitionToWideX3<decltype(di8)> di64;

  return BitCast(di64, SumsOf8(BitCast(du8, Xor(v, SignBit(di8))))) +
         Set(di64, int64_t{-1024});
#else
  return SumsOf2(SumsOf4(v));
#endif
}

// ------------------------------ SaturatedAdd

// Returns a + b clamped to the destination range.

#if HWY_S390X_HAVE_Z14
// Z14/Z15/Z16 does not have I8/U8/I16/U16 SaturatedAdd instructions unlike most
// other integer SIMD instruction sets

template <typename T, size_t N, HWY_IF_UNSIGNED(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T, N> SaturatedAdd(Vec128<T, N> a, Vec128<T, N> b) {
  return Add(a, Min(b, Not(a)));
}

template <typename T, size_t N, HWY_IF_SIGNED(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T, N> SaturatedAdd(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const auto sum = Add(a, b);
  const auto overflow_mask = AndNot(Xor(a, b), Xor(a, sum));
  const auto overflow_result = Xor(BroadcastSignBit(a), Set(d, LimitsMax<T>()));
  return IfNegativeThenElse(overflow_mask, overflow_result, sum);
}

#else  // VSX

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

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> SaturatedAdd(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_adds(a.raw, b.raw)};
}
#endif  // HWY_S390X_HAVE_Z14

#if HWY_PPC_HAVE_10

#ifdef HWY_NATIVE_I64_SATURATED_ADDSUB
#undef HWY_NATIVE_I64_SATURATED_ADDSUB
#else
#define HWY_NATIVE_I64_SATURATED_ADDSUB
#endif

template <class V, HWY_IF_I64_D(DFromV<V>)>
HWY_API V SaturatedAdd(V a, V b) {
  const DFromV<decltype(a)> d;
  const auto sum = Add(a, b);
  const auto overflow_mask =
      BroadcastSignBit(detail::TernaryLogic<0x42>(a, b, sum));
  const auto overflow_result =
      Xor(BroadcastSignBit(a), Set(d, LimitsMax<int64_t>()));
  return IfNegativeThenElse(overflow_mask, overflow_result, sum);
}

#endif  // HWY_PPC_HAVE_10

// ------------------------------ SaturatedSub

// Returns a - b clamped to the destination range.

#if HWY_S390X_HAVE_Z14
// Z14/Z15/Z16 does not have I8/U8/I16/U16 SaturatedSub instructions unlike most
// other integer SIMD instruction sets

template <typename T, size_t N, HWY_IF_UNSIGNED(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T, N> SaturatedSub(Vec128<T, N> a, Vec128<T, N> b) {
  return Sub(a, Min(a, b));
}

template <typename T, size_t N, HWY_IF_SIGNED(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T, N> SaturatedSub(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const auto diff = Sub(a, b);
  const auto overflow_mask = And(Xor(a, b), Xor(a, diff));
  const auto overflow_result = Xor(BroadcastSignBit(a), Set(d, LimitsMax<T>()));
  return IfNegativeThenElse(overflow_mask, overflow_result, diff);
}

#else   // VSX

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> SaturatedSub(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_subs(a.raw, b.raw)};
}
#endif  // HWY_S390X_HAVE_Z14

#if HWY_PPC_HAVE_10

template <class V, HWY_IF_I64_D(DFromV<V>)>
HWY_API V SaturatedSub(V a, V b) {
  const DFromV<decltype(a)> d;
  const auto diff = Sub(a, b);
  const auto overflow_mask =
      BroadcastSignBit(detail::TernaryLogic<0x18>(a, b, diff));
  const auto overflow_result =
      Xor(BroadcastSignBit(a), Set(d, LimitsMax<int64_t>()));
  return IfNegativeThenElse(overflow_mask, overflow_result, diff);
}

#endif  // HWY_PPC_HAVE_10

// ------------------------------ AverageRound

// Returns (a + b + 1) / 2

#ifdef HWY_NATIVE_AVERAGE_ROUND_UI32
#undef HWY_NATIVE_AVERAGE_ROUND_UI32
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI32
#endif

#if HWY_S390X_HAVE_Z14
#ifdef HWY_NATIVE_AVERAGE_ROUND_UI64
#undef HWY_NATIVE_AVERAGE_ROUND_UI64
#else
#define HWY_NATIVE_AVERAGE_ROUND_UI64
#endif

#define HWY_PPC_IF_AVERAGE_ROUND_T(T) void* = nullptr
#else  // !HWY_S390X_HAVE_Z14
#define HWY_PPC_IF_AVERAGE_ROUND_T(T) \
  HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))
#endif  // HWY_S390X_HAVE_Z14

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_PPC_IF_AVERAGE_ROUND_T(T)>
HWY_API Vec128<T, N> AverageRound(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_avg(a.raw, b.raw)};
}

#undef HWY_PPC_IF_AVERAGE_ROUND_T

// ------------------------------ Multiplication

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

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> operator*(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const detail::RebindToUnsignedIfNotFloat<decltype(d)> d_arith;

  // If T is an integer type, do an unsigned vec_mul to avoid undefined behavior
#if HWY_S390X_HAVE_Z14
  return BitCast(d, VFromD<decltype(d_arith)>{BitCast(d_arith, a).raw *
                                              BitCast(d_arith, b).raw});
#else
  return BitCast(d, VFromD<decltype(d_arith)>{vec_mul(
                        BitCast(d_arith, a).raw, BitCast(d_arith, b).raw)});
#endif
}

// Returns the upper sizeof(T)*8 bits of a * b in each lane.

#if HWY_S390X_HAVE_Z14
#define HWY_PPC_IF_MULHIGH_USING_VEC_MULH(T) \
  HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))
#define HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH(T) \
  hwy::EnableIf<!hwy::IsSame<T, T>()>* = nullptr
#elif HWY_PPC_HAVE_10
#define HWY_PPC_IF_MULHIGH_USING_VEC_MULH(T) \
  HWY_IF_T_SIZE_ONE_OF(T, (1 << 4) | (1 << 8))
#define HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH(T) \
  HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))
#else
#define HWY_PPC_IF_MULHIGH_USING_VEC_MULH(T) \
  hwy::EnableIf<!hwy::IsSame<T, T>()>* = nullptr
#define HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH(T) \
  HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))
#endif

#if HWY_S390X_HAVE_Z14 || HWY_PPC_HAVE_10
template <typename T, size_t N, HWY_PPC_IF_MULHIGH_USING_VEC_MULH(T),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> MulHigh(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_mulh(a.raw, b.raw)};
}
#endif

template <typename T, HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH(T),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, 1> MulHigh(Vec128<T, 1> a, Vec128<T, 1> b) {
  const auto p_even = MulEven(a, b);

#if HWY_IS_LITTLE_ENDIAN
  const auto p_even_full = ResizeBitCast(Full128<T>(), p_even);
  return Vec128<T, 1>{
      vec_sld(p_even_full.raw, p_even_full.raw, 16 - sizeof(T))};
#else
  const DFromV<decltype(a)> d;
  return ResizeBitCast(d, p_even);
#endif
}

template <typename T, size_t N,
          HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH(T),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T), HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<T, N> MulHigh(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;

  const auto p_even = BitCast(d, MulEven(a, b));
  const auto p_odd = BitCast(d, MulOdd(a, b));

#if HWY_IS_LITTLE_ENDIAN
  return InterleaveOdd(d, p_even, p_odd);
#else
  return InterleaveEven(d, p_even, p_odd);
#endif
}

#if !HWY_PPC_HAVE_10
template <class T, HWY_IF_UI64(T)>
HWY_API Vec64<T> MulHigh(Vec64<T> a, Vec64<T> b) {
  T p_hi;
  Mul128(GetLane(a), GetLane(b), &p_hi);
  return Set(Full64<T>(), p_hi);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulHigh(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const Half<decltype(d)> dh;
  return Combine(d, MulHigh(UpperHalf(dh, a), UpperHalf(dh, b)),
                 MulHigh(LowerHalf(dh, a), LowerHalf(dh, b)));
}
#endif  // !HWY_PPC_HAVE_10

#undef HWY_PPC_IF_MULHIGH_USING_VEC_MULH
#undef HWY_PPC_IF_MULHIGH_8_16_32_NOT_USING_VEC_MULH

// Multiplies even lanes (0, 2, ..) and places the double-wide result into
// even and the upper half into its odd neighbor lane.
template <typename T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulEven(Vec128<T, N> a,
                                                 Vec128<T, N> b) {
  return Vec128<MakeWide<T>, (N + 1) / 2>{vec_mule(a.raw, b.raw)};
}

// Multiplies odd lanes (1, 3, ..) and places the double-wide result into
// even and the upper half into its odd neighbor lane.
template <typename T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulOdd(Vec128<T, N> a,
                                                Vec128<T, N> b) {
  return Vec128<MakeWide<T>, (N + 1) / 2>{vec_mulo(a.raw, b.raw)};
}

// ------------------------------ Rol/Ror

#ifdef HWY_NATIVE_ROL_ROR_8
#undef HWY_NATIVE_ROL_ROR_8
#else
#define HWY_NATIVE_ROL_ROR_8
#endif

#ifdef HWY_NATIVE_ROL_ROR_16
#undef HWY_NATIVE_ROL_ROR_16
#else
#define HWY_NATIVE_ROL_ROR_16
#endif

#ifdef HWY_NATIVE_ROL_ROR_32_64
#undef HWY_NATIVE_ROL_ROR_32_64
#else
#define HWY_NATIVE_ROL_ROR_32_64
#endif

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> Rol(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, VFromD<decltype(du)>{vec_rl(BitCast(du, a).raw, BitCast(du, b).raw)});
}

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> Ror(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  return Rol(a, BitCast(d, Neg(BitCast(di, b))));
}

// ------------------------------ RotateRight
template <int kBits, typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> RotateRight(const Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  constexpr size_t kSizeInBits = sizeof(T) * 8;
  static_assert(0 <= kBits && kBits < kSizeInBits, "Invalid shift count");

  return (kBits == 0)
             ? v
             : Rol(v, Set(d, static_cast<T>(static_cast<int>(kSizeInBits) -
                                            kBits)));
}

// ------------------------------ RotateLeftSame/RotateRightSame
#ifdef HWY_NATIVE_ROL_ROR_SAME_8
#undef HWY_NATIVE_ROL_ROR_SAME_8
#else
#define HWY_NATIVE_ROL_ROR_SAME_8
#endif

#ifdef HWY_NATIVE_ROL_ROR_SAME_16
#undef HWY_NATIVE_ROL_ROR_SAME_16
#else
#define HWY_NATIVE_ROL_ROR_SAME_16
#endif

#ifdef HWY_NATIVE_ROL_ROR_SAME_32_64
#undef HWY_NATIVE_ROL_ROR_SAME_32_64
#else
#define HWY_NATIVE_ROL_ROR_SAME_32_64
#endif

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> RotateLeftSame(Vec128<T, N> v, int bits) {
  const DFromV<decltype(v)> d;
  return Rol(v, Set(d, static_cast<T>(static_cast<unsigned>(bits))));
}

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> RotateRightSame(Vec128<T, N> v, int bits) {
  const DFromV<decltype(v)> d;
  return Rol(v, Set(d, static_cast<T>(0u - static_cast<unsigned>(bits))));
}

// ------------------------------ IfNegativeThenElse

template <typename T, size_t N>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  static_assert(IsSigned<T>(), "Only works for signed/float");

  const DFromV<decltype(v)> d;
#if HWY_PPC_HAVE_10
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, VFromD<decltype(du)>{vec_blendv(
             BitCast(du, no).raw, BitCast(du, yes).raw, BitCast(du, v).raw)});
#else
  const RebindToSigned<decltype(d)> di;
  return IfVecThenElse(BitCast(d, BroadcastSignBit(BitCast(di, v))), yes, no);
#endif
}

#if HWY_PPC_HAVE_10
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

template <class V, HWY_IF_NOT_UNSIGNED_V(V)>
HWY_API V IfNegativeThenElseZero(V v, V yes) {
  const DFromV<decltype(v)> d;
  return IfNegativeThenElse(v, yes, Zero(d));
}

template <class V, HWY_IF_NOT_UNSIGNED_V(V)>
HWY_API V IfNegativeThenZeroElse(V v, V no) {
  const DFromV<decltype(v)> d;
  return IfNegativeThenElse(v, Zero(d), no);
}
#endif

// generic_ops takes care of integer T.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> AbsDiff(Vec128<T, N> a, Vec128<T, N> b) {
  return Abs(a - b);
}

// ------------------------------ Floating-point multiply-add variants

// Returns mul * x + add
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> add) {
  return Vec128<T, N>{vec_madd(mul.raw, x.raw, add.raw)};
}

// Returns add - mul * x
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulAdd(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> add) {
  // NOTE: the vec_nmsub operation below computes -(mul * x - add),
  // which is equivalent to add - mul * x in the round-to-nearest
  // and round-towards-zero rounding modes
  return Vec128<T, N>{vec_nmsub(mul.raw, x.raw, add.raw)};
}

// Returns mul * x - sub
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> MulSub(Vec128<T, N> mul, Vec128<T, N> x,
                            Vec128<T, N> sub) {
  return Vec128<T, N>{vec_msub(mul.raw, x.raw, sub.raw)};
}

// Returns -mul * x - sub
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> NegMulSub(Vec128<T, N> mul, Vec128<T, N> x,
                               Vec128<T, N> sub) {
  // NOTE: The vec_nmadd operation below computes -(mul * x + sub),
  // which is equivalent to -mul * x - sub in the round-to-nearest
  // and round-towards-zero rounding modes
  return Vec128<T, N>{vec_nmadd(mul.raw, x.raw, sub.raw)};
}

// ------------------------------ Floating-point div
// Approximate reciprocal

#ifdef HWY_NATIVE_F64_APPROX_RECIP
#undef HWY_NATIVE_F64_APPROX_RECIP
#else
#define HWY_NATIVE_F64_APPROX_RECIP
#endif

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> operator/(Vec128<T, N> a, Vec128<T, N> b) {
#if HWY_S390X_HAVE_Z14
  return Vec128<T, N>{a.raw / b.raw};
#else
  return Vec128<T, N>{vec_div(a.raw, b.raw)};
#endif
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> ApproximateReciprocal(Vec128<T, N> v) {
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> d;
  return Set(d, T(1.0)) / v;
#else
  return Vec128<T, N>{vec_re(v.raw)};
#endif
}

// ------------------------------ Floating-point square root

#if HWY_S390X_HAVE_Z14
// Approximate reciprocal square root
template <size_t N>
HWY_API Vec128<float, N> ApproximateReciprocalSqrt(Vec128<float, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const auto half = v * Set(d, 0.5f);
  // Initial guess based on log2(f)
  const auto guess = BitCast(
      d, Set(du, uint32_t{0x5F3759DFu}) - ShiftRight<1>(BitCast(du, v)));
  // One Newton-Raphson iteration
  return guess * NegMulAdd(half * guess, guess, Set(d, 1.5f));
}
#else  // VSX

#ifdef HWY_NATIVE_F64_APPROX_RSQRT
#undef HWY_NATIVE_F64_APPROX_RSQRT
#else
#define HWY_NATIVE_F64_APPROX_RSQRT
#endif

// Approximate reciprocal square root
template <class T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> ApproximateReciprocalSqrt(Vec128<T, N> v) {
  return Vec128<T, N>{vec_rsqrte(v.raw)};
}
#endif  // HWY_S390X_HAVE_Z14

// Full precision square root
template <class T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Sqrt(Vec128<T, N> v) {
  return Vec128<T, N>{vec_sqrt(v.raw)};
}

// ------------------------------ Min (Gt, IfThenElse)

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> Min(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_min(a.raw, b.raw)};
}

// ------------------------------ Max (Gt, IfThenElse)

template <typename T, size_t N, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_API Vec128<T, N> Max(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_max(a.raw, b.raw)};
}

// ------------------------------- Integer AbsDiff for PPC9/PPC10

#if HWY_PPC_HAVE_9
#ifdef HWY_NATIVE_INTEGER_ABS_DIFF
#undef HWY_NATIVE_INTEGER_ABS_DIFF
#else
#define HWY_NATIVE_INTEGER_ABS_DIFF
#endif

template <class V, HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API V AbsDiff(const V a, const V b) {
  return V{vec_absd(a.raw, b.raw)};
}

template <class V, HWY_IF_U64_D(DFromV<V>)>
HWY_API V AbsDiff(const V a, const V b) {
  return Sub(Max(a, b), Min(a, b));
}

template <class V, HWY_IF_SIGNED_V(V)>
HWY_API V AbsDiff(const V a, const V b) {
  return Sub(Max(a, b), Min(a, b));
}

#endif  // HWY_PPC_HAVE_9

// ------------------------------ Integer Div for PPC10
#if HWY_PPC_HAVE_10
#ifdef HWY_NATIVE_INT_DIV
#undef HWY_NATIVE_INT_DIV
#else
#define HWY_NATIVE_INT_DIV
#endif

template <size_t N>
HWY_API Vec128<int32_t, N> operator/(Vec128<int32_t, N> a,
                                     Vec128<int32_t, N> b) {
  // Inline assembly is used instead of vec_div for I32 Div on PPC10 to avoid
  // undefined behavior if b[i] == 0 or
  // (a[i] == LimitsMin<int32_t>() && b[i] == -1)

  // Clang will also optimize out I32 vec_div on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector signed int raw_result;
  __asm__("vdivsw %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<int32_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<uint32_t, N> operator/(Vec128<uint32_t, N> a,
                                      Vec128<uint32_t, N> b) {
  // Inline assembly is used instead of vec_div for U32 Div on PPC10 to avoid
  // undefined behavior if b[i] == 0

  // Clang will also optimize out U32 vec_div on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector unsigned int raw_result;
  __asm__("vdivuw %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<uint32_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<int64_t, N> operator/(Vec128<int64_t, N> a,
                                     Vec128<int64_t, N> b) {
  // Inline assembly is used instead of vec_div for I64 Div on PPC10 to avoid
  // undefined behavior if b[i] == 0 or
  // (a[i] == LimitsMin<int64_t>() && b[i] == -1)

  // Clang will also optimize out I64 vec_div on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector signed long long raw_result;
  __asm__("vdivsd %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<int64_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<uint64_t, N> operator/(Vec128<uint64_t, N> a,
                                      Vec128<uint64_t, N> b) {
  // Inline assembly is used instead of vec_div for U64 Div on PPC10 to avoid
  // undefined behavior if b[i] == 0

  // Clang will also optimize out U64 vec_div on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector unsigned long long raw_result;
  __asm__("vdivud %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<uint64_t, N>{raw_result};
}

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T> operator/(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  return OrderedDemote2To(d, PromoteLowerTo(dw, a) / PromoteLowerTo(dw, b),
                          PromoteUpperTo(dw, a) / PromoteUpperTo(dw, b));
}

template <class T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<T, N> operator/(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const Rebind<MakeWide<T>, decltype(d)> dw;
  return DemoteTo(d, PromoteTo(dw, a) / PromoteTo(dw, b));
}

template <size_t N>
HWY_API Vec128<int32_t, N> operator%(Vec128<int32_t, N> a,
                                     Vec128<int32_t, N> b) {
  // Inline assembly is used instead of vec_mod for I32 Mod on PPC10 to avoid
  // undefined behavior if b[i] == 0 or
  // (a[i] == LimitsMin<int32_t>() && b[i] == -1)

  // Clang will also optimize out I32 vec_mod on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector signed int raw_result;
  __asm__("vmodsw %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<int32_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<uint32_t, N> operator%(Vec128<uint32_t, N> a,
                                      Vec128<uint32_t, N> b) {
  // Inline assembly is used instead of vec_mod for U32 Mod on PPC10 to avoid
  // undefined behavior if b[i] == 0

  // Clang will also optimize out U32 vec_mod on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector unsigned int raw_result;
  __asm__("vmoduw %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<uint32_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<int64_t, N> operator%(Vec128<int64_t, N> a,
                                     Vec128<int64_t, N> b) {
  // Inline assembly is used instead of vec_mod for I64 Mod on PPC10 to avoid
  // undefined behavior if b[i] == 0 or
  // (a[i] == LimitsMin<int64_t>() && b[i] == -1)

  // Clang will also optimize out I64 vec_mod on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector signed long long raw_result;
  __asm__("vmodsd %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<int64_t, N>{raw_result};
}

template <size_t N>
HWY_API Vec128<uint64_t, N> operator%(Vec128<uint64_t, N> a,
                                      Vec128<uint64_t, N> b) {
  // Inline assembly is used instead of vec_mod for U64 Mod on PPC10 to avoid
  // undefined behavior if b[i] == 0

  // Clang will also optimize out U64 vec_mod on PPC10 if optimizations are
  // enabled and any of the lanes of b are known to be zero (even in the unused
  // lanes of a partial vector)
  __vector unsigned long long raw_result;
  __asm__("vmodud %0,%1,%2" : "=v"(raw_result) : "v"(a.raw), "v"(b.raw));
  return Vec128<uint64_t, N>{raw_result};
}

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2))>
HWY_API Vec128<T> operator%(Vec128<T> a, Vec128<T> b) {
  const DFromV<decltype(a)> d;
  const RepartitionToWide<decltype(d)> dw;
  return OrderedDemote2To(d, PromoteLowerTo(dw, a) % PromoteLowerTo(dw, b),
                          PromoteUpperTo(dw, a) % PromoteUpperTo(dw, b));
}

template <class T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<T, N> operator%(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const Rebind<MakeWide<T>, decltype(d)> dw;
  return DemoteTo(d, PromoteTo(dw, a) % PromoteTo(dw, b));
}
#endif

// ================================================== MEMORY (3)

// ------------------------------ Non-temporal stores

template <class D>
HWY_API void Stream(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  __builtin_prefetch(aligned, 1, 0);
  Store(v, d, aligned);
}

// ------------------------------ Scatter in generic_ops-inl.h
// ------------------------------ Gather in generic_ops-inl.h

// ================================================== SWIZZLE (2)

// ------------------------------ LowerHalf

// Returns upper/lower half of a vector.
template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> LowerHalf(D /* tag */, VFromD<Twice<D>> v) {
  return VFromD<D>{v.raw};
}
template <typename T, size_t N>
HWY_API Vec128<T, N / 2> LowerHalf(Vec128<T, N> v) {
  return Vec128<T, N / 2>{v.raw};
}

// ------------------------------ ShiftLeftBytes

// NOTE: The ShiftLeftBytes operation moves the elements of v to the right
// by kBytes bytes and zeroes out the first kBytes bytes of v on both
// little-endian and big-endian PPC targets
// (same behavior as the HWY_EMU128 ShiftLeftBytes operation on both
// little-endian and big-endian targets)

template <int kBytes, class D>
HWY_API VFromD<D> ShiftLeftBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  if (kBytes == 0) return v;
  const auto zeros = Zero(d);
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_sld(v.raw, zeros.raw, kBytes)};
#else
  return VFromD<D>{vec_sld(zeros.raw, v.raw, (-kBytes) & 15)};
#endif
}

template <int kBytes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftBytes(Vec128<T, N> v) {
  return ShiftLeftBytes<kBytes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftLeftLanes

// NOTE: The ShiftLeftLanes operation moves the elements of v to the right
// by kLanes lanes and zeroes out the first kLanes lanes of v on both
// little-endian and big-endian PPC targets
// (same behavior as the HWY_EMU128 ShiftLeftLanes operation on both
// little-endian and big-endian targets)

template <int kLanes, class D, typename T = TFromD<D>>
HWY_API VFromD<D> ShiftLeftLanes(D d, VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, ShiftLeftBytes<kLanes * sizeof(T)>(BitCast(d8, v)));
}

template <int kLanes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftLanes(Vec128<T, N> v) {
  return ShiftLeftLanes<kLanes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftRightBytes

// NOTE: The ShiftRightBytes operation moves the elements of v to the left
// by kBytes bytes and zeroes out the last kBytes bytes of v on both
// little-endian and big-endian PPC targets
// (same behavior as the HWY_EMU128 ShiftRightBytes operation on both
// little-endian and big-endian targets)

template <int kBytes, class D>
HWY_API VFromD<D> ShiftRightBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  if (kBytes == 0) return v;

  // For partial vectors, clear upper lanes so we shift in zeros.
  if (d.MaxBytes() != 16) {
    const Full128<TFromD<D>> dfull;
    VFromD<decltype(dfull)> vfull{v.raw};
    v = VFromD<D>{IfThenElseZero(FirstN(dfull, MaxLanes(d)), vfull).raw};
  }

  const auto zeros = Zero(d);
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_sld(zeros.raw, v.raw, (-kBytes) & 15)};
#else
  return VFromD<D>{vec_sld(v.raw, zeros.raw, kBytes)};
#endif
}

// ------------------------------ ShiftRightLanes

// NOTE: The ShiftRightLanes operation moves the elements of v to the left
// by kLanes lanes and zeroes out the last kLanes lanes of v on both
// little-endian and big-endian PPC targets
// (same behavior as the HWY_EMU128 ShiftRightLanes operation on both
// little-endian and big-endian targets)

template <int kLanes, class D>
HWY_API VFromD<D> ShiftRightLanes(D d, VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr size_t kBytes = kLanes * sizeof(TFromD<D>);
  return BitCast(d, ShiftRightBytes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ UpperHalf (ShiftRightBytes)

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> UpperHalf(D d, VFromD<Twice<D>> v) {
  return LowerHalf(d, ShiftRightBytes<d.MaxBytes()>(Twice<D>(), v));
}

// ------------------------------ ExtractLane
template <typename T, size_t N>
HWY_API T ExtractLane(Vec128<T, N> v, size_t i) {
  return static_cast<T>(v.raw[i]);
}

// ------------------------------ InsertLane
template <typename T, size_t N>
HWY_API Vec128<T, N> InsertLane(Vec128<T, N> v, size_t i, T t) {
#if HWY_IS_LITTLE_ENDIAN
  typename detail::Raw128<T>::type raw_result = v.raw;
  raw_result[i] = BitCastScalar<typename detail::Raw128<T>::RawT>(t);
  return Vec128<T, N>{raw_result};
#else
  // On ppc64be without this, mul_test fails, but swizzle_test passes.
  DFromV<decltype(v)> d;
  alignas(16) T lanes[16 / sizeof(T)];
  Store(v, d, lanes);
  lanes[i] = t;
  return Load(d, lanes);
#endif
}

// ------------------------------ CombineShiftRightBytes

// NOTE: The CombineShiftRightBytes operation below moves the elements of lo to
// the left by kBytes bytes and moves the elements of hi right by (d.MaxBytes()
// - kBytes) bytes on both little-endian and big-endian PPC targets.

template <int kBytes, class D, HWY_IF_V_SIZE_D(D, 16), typename T = TFromD<D>>
HWY_API Vec128<T> CombineShiftRightBytes(D /*d*/, Vec128<T> hi, Vec128<T> lo) {
  constexpr size_t kSize = 16;
  static_assert(0 < kBytes && kBytes < kSize, "kBytes invalid");
#if HWY_IS_LITTLE_ENDIAN
  return Vec128<T>{vec_sld(hi.raw, lo.raw, (-kBytes) & 15)};
#else
  return Vec128<T>{vec_sld(lo.raw, hi.raw, kBytes)};
#endif
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

template <int kLane, typename T, size_t N>
HWY_API Vec128<T, N> Broadcast(Vec128<T, N> v) {
  static_assert(0 <= kLane && kLane < N, "Invalid lane");
  return Vec128<T, N>{vec_splat(v.raw, kLane)};
}

// ------------------------------ TableLookupLanes (Shuffle01)

// Returned by SetTableIndices/IndicesFromVec for use by TableLookupLanes.
template <typename T, size_t N = 16 / sizeof(T)>
struct Indices128 {
  __vector unsigned char raw;
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
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      0, 0, 2, 2, 4, 4, 6, 6, 8, 8, 10, 10, 12, 12, 14, 14};
#else
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15};
#endif
  return VFromD<decltype(d8)>{kBroadcastLaneBytes};
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
#else
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15};
#endif
  return VFromD<decltype(d8)>{kBroadcastLaneBytes};
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecBroadcastLaneBytes(
    D d) {
  const Repartition<uint8_t, decltype(d)> d8;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8};
#else
  constexpr __vector unsigned char kBroadcastLaneBytes = {
      7, 7, 7, 7, 7, 7, 7, 7, 15, 15, 15, 15, 15, 15, 15, 15};
#endif
  return VFromD<decltype(d8)>{kBroadcastLaneBytes};
}

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  return Zero(d8);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr __vector unsigned char kByteOffsets = {0, 1, 0, 1, 0, 1, 0, 1,
                                                   0, 1, 0, 1, 0, 1, 0, 1};
  return VFromD<decltype(d8)>{kByteOffsets};
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr __vector unsigned char kByteOffsets = {0, 1, 2, 3, 0, 1, 2, 3,
                                                   0, 1, 2, 3, 0, 1, 2, 3};
  return VFromD<decltype(d8)>{kByteOffsets};
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE VFromD<Repartition<uint8_t, D>> IndicesFromVecByteOffsets(D d) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr __vector unsigned char kByteOffsets = {0, 1, 2, 3, 4, 5, 6, 7,
                                                   0, 1, 2, 3, 4, 5, 6, 7};
  return VFromD<decltype(d8)>{kByteOffsets};
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

  const Repartition<uint8_t, decltype(d)> d8;
  return Indices128<TFromD<D>, MaxLanes(D())>{BitCast(d8, vec).raw};
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
  return Indices128<TFromD<D>, MaxLanes(D())>{sum.raw};
}

template <class D, typename TI>
HWY_API Indices128<TFromD<D>, HWY_MAX_LANES_D(D)> SetTableIndices(
    D d, const TI* idx) {
  const Rebind<TI, decltype(d)> di;
  return IndicesFromVec(d, LoadU(di, idx));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> d8;
  return BitCast(d, TableLookupBytes(v, VFromD<decltype(d8)>{idx.raw}));
}

// Single lane: no change
template <typename T>
HWY_API Vec128<T, 1> TableLookupLanes(Vec128<T, 1> v,
                                      Indices128<T, 1> /* idx */) {
  return v;
}

template <typename T, size_t N, HWY_IF_V_SIZE_LE(T, N, 8)>
HWY_API Vec128<T, N> TwoTablesLookupLanes(Vec128<T, N> a, Vec128<T, N> b,
                                          Indices128<T, N> idx) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  const Repartition<uint8_t, decltype(dt)> dt_u8;
// TableLookupLanes currently requires table and index vectors to be the same
// size, though a half-length index vector would be sufficient here.
#if HWY_IS_MSAN
  const Vec128<T, N> idx_vec{idx.raw};
  const Indices128<T, N * 2> idx2{Combine(dt, idx_vec, idx_vec).raw};
#else
  // We only keep LowerHalf of the result, which is valid in idx.
  const Indices128<T, N * 2> idx2{idx.raw};
#endif
  return LowerHalf(
      d, TableLookupBytes(Combine(dt, b, a),
                          BitCast(dt, VFromD<decltype(dt_u8)>{idx2.raw})));
}

template <typename T>
HWY_API Vec128<T> TwoTablesLookupLanes(Vec128<T> a, Vec128<T> b,
                                       Indices128<T> idx) {
  return Vec128<T>{vec_perm(a.raw, b.raw, idx.raw)};
}

// ------------------------------ ReverseBlocks

// Single block: no change
template <class D>
HWY_API VFromD<D> ReverseBlocks(D /* tag */, VFromD<D> v) {
  return v;
}

// ------------------------------ Reverse (Shuffle0123, Shuffle2301)

// Single lane: no change
template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 1)>
HWY_API Vec128<T, 1> Reverse(D /* tag */, Vec128<T, 1> v) {
  return v;
}

// 32-bit x2: shuffle
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec64<T> Reverse(D /* tag */, Vec64<T> v) {
  return Vec64<T>{Shuffle2301(Vec128<T>{v.raw}).raw};
}

// 16-bit x4: shuffle
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> Reverse(D /* tag */, Vec64<T> v) {
  const __vector unsigned char kShuffle = {6,  7,  4,  5,  2,  3,  0, 1,
                                           14, 15, 12, 13, 10, 11, 8, 9};
  return Vec64<T>{vec_perm(v.raw, v.raw, kShuffle)};
}

// 16-bit x2: rotate bytes
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec32<T> Reverse(D d, Vec32<T> v) {
  const RepartitionToWide<RebindToUnsigned<decltype(d)>> du32;
  return BitCast(d, RotateRight<16>(Reverse(du32, BitCast(du32, v))));
}

// ------------------------------- ReverseLaneBytes

#if (HWY_PPC_HAVE_9 || HWY_S390X_HAVE_Z14) && \
    (HWY_COMPILER_GCC_ACTUAL >= 710 || HWY_COMPILER_CLANG >= 400)

// Per-target flag to prevent generic_ops-inl.h defining 8-bit ReverseLaneBytes.
#ifdef HWY_NATIVE_REVERSE_LANE_BYTES
#undef HWY_NATIVE_REVERSE_LANE_BYTES
#else
#define HWY_NATIVE_REVERSE_LANE_BYTES
#endif

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API V ReverseLaneBytes(V v) {
  return V{vec_revb(v.raw)};
}

// Per-target flag to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const Repartition<uint16_t, decltype(d)> du16;
  return BitCast(d, ReverseLaneBytes(BitCast(du16, v)));
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  const Repartition<uint32_t, decltype(d)> du32;
  return BitCast(d, ReverseLaneBytes(BitCast(du32, v)));
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API VFromD<D> Reverse8(D d, VFromD<D> v) {
  const Repartition<uint64_t, decltype(d)> du64;
  return BitCast(d, ReverseLaneBytes(BitCast(du64, v)));
}

#endif  // HWY_PPC_HAVE_9 || HWY_S390X_HAVE_Z14

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec16<T> Reverse(D d, Vec16<T> v) {
  return Reverse2(d, v);
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> Reverse(D d, Vec32<T> v) {
  return Reverse4(d, v);
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec64<T> Reverse(D d, Vec64<T> v) {
  return Reverse8(d, v);
}

// ------------------------------ Reverse2

// Single lane: no change
template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 1)>
HWY_API Vec128<T, 1> Reverse2(D /* tag */, Vec128<T, 1> v) {
  return v;
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const Repartition<uint32_t, decltype(d)> du32;
  return BitCast(d, RotateRight<16>(BitCast(du32, v)));
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  const Repartition<uint64_t, decltype(d)> du64;
  return BitCast(d, RotateRight<32>(BitCast(du64, v)));
}

template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 8)>
HWY_API VFromD<D> Reverse2(D /* tag */, VFromD<D> v) {
  return Shuffle01(v);
}

// ------------------------------ Reverse4

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse4(D /*d*/, VFromD<D> v) {
  const __vector unsigned char kShuffle = {6,  7,  4,  5,  2,  3,  0, 1,
                                           14, 15, 12, 13, 10, 11, 8, 9};
  return VFromD<D>{vec_perm(v.raw, v.raw, kShuffle)};
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  return Reverse(d, v);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Reverse4(D /* tag */, VFromD<D> /* v */) {
  HWY_ASSERT(0);  // don't have 4 u64 lanes
}

// ------------------------------ Reverse8

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Reverse8(D d, VFromD<D> v) {
  return Reverse(d, v);
}

template <class D, HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))>
HWY_API VFromD<D> Reverse8(D /* tag */, VFromD<D> /* v */) {
  HWY_ASSERT(0);  // don't have 8 lanes if larger than 16-bit
}

// ------------------------------ InterleaveLower

// Interleaves lanes from halves of the 128-bit blocks of "a" (which provides
// the least-significant lane) and "b". To concatenate two half-width integers
// into one, use ZipLower/Upper instead (also works with scalar).

template <typename T, size_t N>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  return Vec128<T, N>{vec_mergeh(a.raw, b.raw)};
}

// Additional overload for the optional tag
template <class D>
HWY_API VFromD<D> InterleaveLower(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveUpper (UpperHalf)

// Full
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> InterleaveUpper(D /* tag */, Vec128<T> a, Vec128<T> b) {
  return Vec128<T>{vec_mergel(a.raw, b.raw)};
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

// ------------------------------ Per4LaneBlkShufDupSet4xU32

// Used by hwy/ops/generic_ops-inl.h to implement Per4LaneBlockShuffle
namespace detail {

#ifdef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#undef HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#else
#define HWY_NATIVE_PER4LANEBLKSHUF_DUP32
#endif

template <class D>
HWY_INLINE VFromD<D> Per4LaneBlkShufDupSet4xU32(D d, const uint32_t x3,
                                                const uint32_t x2,
                                                const uint32_t x1,
                                                const uint32_t x0) {
  const __vector unsigned int raw = {x0, x1, x2, x3};
  return ResizeBitCast(d, Vec128<uint32_t>{raw});
}

}  // namespace detail

// ------------------------------ SlideUpLanes

template <class D>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
  const Repartition<uint8_t, decltype(d)> du8;
  using VU8 = VFromD<decltype(du8)>;
  const auto v_shift_amt =
      BitCast(Full128<uint8_t>(),
              Set(Full128<uint32_t>(),
                  static_cast<uint32_t>(amt * sizeof(TFromD<D>) * 8)));

#if HWY_S390X_HAVE_Z14
  return BitCast(d, VU8{vec_srb(BitCast(du8, v).raw, v_shift_amt.raw)});
#else  // VSX
#if HWY_IS_LITTLE_ENDIAN
  return BitCast(d, VU8{vec_slo(BitCast(du8, v).raw, v_shift_amt.raw)});
#else
  return BitCast(d, VU8{vec_sro(BitCast(du8, v).raw, v_shift_amt.raw)});
#endif  // HWY_IS_LITTLE_ENDIAN
#endif  // HWY_S390X_HAVE_Z14
}

// ------------------------------ SlideDownLanes

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
  using TU = UnsignedFromSize<d.MaxBytes()>;
  const Repartition<TU, decltype(d)> du;
  const auto v_shift_amt =
      Set(du, static_cast<TU>(amt * sizeof(TFromD<D>) * 8));

#if HWY_IS_LITTLE_ENDIAN
  return BitCast(d, BitCast(du, v) >> v_shift_amt);
#else
  return BitCast(d, BitCast(du, v) << v_shift_amt);
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
  const Repartition<uint8_t, decltype(d)> du8;
  using VU8 = VFromD<decltype(du8)>;
  const auto v_shift_amt =
      BitCast(Full128<uint8_t>(),
              Set(Full128<uint32_t>(),
                  static_cast<uint32_t>(amt * sizeof(TFromD<D>) * 8)));

#if HWY_S390X_HAVE_Z14
  return BitCast(d, VU8{vec_slb(BitCast(du8, v).raw, v_shift_amt.raw)});
#else  // VSX
#if HWY_IS_LITTLE_ENDIAN
  return BitCast(d, VU8{vec_sro(BitCast(du8, v).raw, v_shift_amt.raw)});
#else
  return BitCast(d, VU8{vec_slo(BitCast(du8, v).raw, v_shift_amt.raw)});
#endif  // HWY_IS_LITTLE_ENDIAN
#endif  // HWY_S390X_HAVE_Z14
}

// ================================================== COMBINE

// ------------------------------ Combine (InterleaveLower)

// N = N/2 + N/2 (upper half undefined)
template <class D, HWY_IF_V_SIZE_LE_D(D, 16), class VH = VFromD<Half<D>>>
HWY_API VFromD<D> Combine(D d, VH hi_half, VH lo_half) {
  const Half<decltype(d)> dh;
  // Treat half-width input as one lane, and expand to two lanes.
  using VU = Vec128<UnsignedFromSize<dh.MaxBytes()>, 2>;
  using Raw = typename detail::Raw128<TFromV<VU>>::type;
  const VU lo{reinterpret_cast<Raw>(lo_half.raw)};
  const VU hi{reinterpret_cast<Raw>(hi_half.raw)};
  return BitCast(d, InterleaveLower(lo, hi));
}

// ------------------------------ ZeroExtendVector (Combine, IfThenElseZero)

template <class D>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> lo) {
  const Half<D> dh;
  return IfThenElseZero(FirstN(d, MaxLanes(dh)), VFromD<D>{lo.raw});
}

// ------------------------------ Concat full (InterleaveLower)

// hiH,hiL loH,loL |-> hiL,loL (= lower halves)
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatLowerLower(D d, Vec128<T> hi, Vec128<T> lo) {
  const Repartition<uint64_t, decltype(d)> d64;
  return BitCast(d, InterleaveLower(BitCast(d64, lo), BitCast(d64, hi)));
}

// hiH,hiL loH,loL |-> hiH,loH (= upper halves)
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatUpperUpper(D d, Vec128<T> hi, Vec128<T> lo) {
  const Repartition<uint64_t, decltype(d)> d64;
  return BitCast(d, InterleaveUpper(d64, BitCast(d64, lo), BitCast(d64, hi)));
}

// hiH,hiL loH,loL |-> hiL,loH (= inner halves)
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatLowerUpper(D d, Vec128<T> hi, Vec128<T> lo) {
  return CombineShiftRightBytes<8>(d, hi, lo);
}

// hiH,hiL loH,loL |-> hiH,loL (= outer halves)
template <class D, typename T = TFromD<D>>
HWY_API Vec128<T> ConcatUpperLower(D /*d*/, Vec128<T> hi, Vec128<T> lo) {
  const __vector unsigned char kShuffle = {0,  1,  2,  3,  4,  5,  6,  7,
                                           24, 25, 26, 27, 28, 29, 30, 31};
  return Vec128<T>{vec_perm(lo.raw, hi.raw, kShuffle)};
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
HWY_API VFromD<D> ConcatLowerUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, LowerHalf(d2, hi), UpperHalf(d2, lo));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConcatUpperLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> d2;
  return Combine(d, UpperHalf(d2, hi), LowerHalf(d2, lo));
}

// ------------------------------ TruncateTo

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          hwy::EnableIf<(sizeof(FromT) >= sizeof(TFromD<D>) * 2)>* = nullptr,
          HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<FromT, 1> v) {
  using Raw = typename detail::Raw128<TFromD<D>>::type;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{reinterpret_cast<Raw>(v.raw)};
#else
  return VFromD<D>{reinterpret_cast<Raw>(
      vec_sld(v.raw, v.raw, sizeof(FromT) - sizeof(TFromD<D>)))};
#endif
}

namespace detail {

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          HWY_IF_T_SIZE(FromT, sizeof(TFromD<D>) * 2), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> Truncate2To(
    D /* tag */, Vec128<FromT, Repartition<FromT, D>().MaxLanes()> lo,
    Vec128<FromT, Repartition<FromT, D>().MaxLanes()> hi) {
  return VFromD<D>{vec_pack(lo.raw, hi.raw)};
}

}  // namespace detail

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          HWY_IF_T_SIZE(FromT, sizeof(TFromD<D>) * 2), HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D /* d */,
                             Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  return VFromD<D>{vec_pack(v.raw, v.raw)};
}

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          hwy::EnableIf<(sizeof(FromT) >= sizeof(TFromD<D>) * 4)>* = nullptr,
          HWY_IF_LANES_GT_D(D, 1)>
HWY_API VFromD<D> TruncateTo(D d,
                             Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<MakeNarrow<FromT>, decltype(d)> d2;
  return TruncateTo(d, TruncateTo(d2, v));
}

// ------------------------------ ConcatOdd (TruncateTo)

// 8-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> ConcatOdd(D d, Vec128<T> hi, Vec128<T> lo) {
  const Repartition<uint16_t, decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;
#if HWY_IS_LITTLE_ENDIAN
  // Right-shift 8 bits per u16 so we can pack.
  const Vec128<uint16_t> uH = ShiftRight<8>(BitCast(dw, hi));
  const Vec128<uint16_t> uL = ShiftRight<8>(BitCast(dw, lo));
#else
  const Vec128<uint16_t> uH = BitCast(dw, hi);
  const Vec128<uint16_t> uL = BitCast(dw, lo);
#endif
  return BitCast(d, detail::Truncate2To(du, uL, uH));
}

// 8-bit x8
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec64<T> ConcatOdd(D /*d*/, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactOddU8 = {1, 3, 5, 7, 17, 19, 21, 23};
  return Vec64<T>{vec_perm(lo.raw, hi.raw, kCompactOddU8)};
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatOdd(D /*d*/, Vec32<T> hi, Vec32<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactOddU8 = {1, 3, 17, 19};
  return Vec32<T>{vec_perm(lo.raw, hi.raw, kCompactOddU8)};
}

// 16-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> ConcatOdd(D d, Vec128<T> hi, Vec128<T> lo) {
  const Repartition<uint32_t, decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<uint32_t> uH = ShiftRight<16>(BitCast(dw, hi));
  const Vec128<uint32_t> uL = ShiftRight<16>(BitCast(dw, lo));
#else
  const Vec128<uint32_t> uH = BitCast(dw, hi);
  const Vec128<uint32_t> uL = BitCast(dw, lo);
#endif
  return BitCast(d, detail::Truncate2To(du, uL, uH));
}

// 16-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ConcatOdd(D /*d*/, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactOddU16 = {2, 3, 6, 7, 18, 19, 22, 23};
  return Vec64<T>{vec_perm(lo.raw, hi.raw, kCompactOddU16)};
}

// 32-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ConcatOdd(D d, Vec128<T> hi, Vec128<T> lo) {
#if HWY_IS_LITTLE_ENDIAN
  (void)d;
  const __vector unsigned char kShuffle = {4,  5,  6,  7,  12, 13, 14, 15,
                                           20, 21, 22, 23, 28, 29, 30, 31};
  return Vec128<T>{vec_perm(lo.raw, hi.raw, kShuffle)};
#else
  const RebindToUnsigned<decltype(d)> du;
  const Repartition<uint64_t, decltype(d)> dw;
  return BitCast(d, detail::Truncate2To(du, BitCast(dw, lo), BitCast(dw, hi)));
#endif
}

// Any type x2
template <class D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 2)>
HWY_API Vec128<T, 2> ConcatOdd(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveUpper(d, lo, hi);
}

// ------------------------------ ConcatEven (TruncateTo)

// 8-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T> ConcatEven(D d, Vec128<T> hi, Vec128<T> lo) {
  const Repartition<uint16_t, decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<uint16_t> uH = BitCast(dw, hi);
  const Vec128<uint16_t> uL = BitCast(dw, lo);
#else
  // Right-shift 8 bits per u16 so we can pack.
  const Vec128<uint16_t> uH = ShiftRight<8>(BitCast(dw, hi));
  const Vec128<uint16_t> uL = ShiftRight<8>(BitCast(dw, lo));
#endif
  return BitCast(d, detail::Truncate2To(du, uL, uH));
}

// 8-bit x8
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec64<T> ConcatEven(D /*d*/, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactEvenU8 = {0, 2, 4, 6, 16, 18, 20, 22};
  return Vec64<T>{vec_perm(lo.raw, hi.raw, kCompactEvenU8)};
}

// 8-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec32<T> ConcatEven(D /*d*/, Vec32<T> hi, Vec32<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactEvenU8 = {0, 2, 16, 18};
  return Vec32<T>{vec_perm(lo.raw, hi.raw, kCompactEvenU8)};
}

// 16-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T> ConcatEven(D d, Vec128<T> hi, Vec128<T> lo) {
  // Isolate lower 16 bits per u32 so we can pack.
  const Repartition<uint32_t, decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<uint32_t> uH = BitCast(dw, hi);
  const Vec128<uint32_t> uL = BitCast(dw, lo);
#else
  const Vec128<uint32_t> uH = ShiftRight<16>(BitCast(dw, hi));
  const Vec128<uint32_t> uL = ShiftRight<16>(BitCast(dw, lo));
#endif
  return BitCast(d, detail::Truncate2To(du, uL, uH));
}

// 16-bit x4
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec64<T> ConcatEven(D /*d*/, Vec64<T> hi, Vec64<T> lo) {
  // Don't care about upper half, no need to zero.
  const __vector unsigned char kCompactEvenU16 = {0, 1, 4, 5, 16, 17, 20, 21};
  return Vec64<T>{vec_perm(lo.raw, hi.raw, kCompactEvenU16)};
}

// 32-bit full
template <class D, typename T = TFromD<D>, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> ConcatEven(D d, Vec128<T> hi, Vec128<T> lo) {
#if HWY_IS_LITTLE_ENDIAN
  const Repartition<uint64_t, decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, detail::Truncate2To(du, BitCast(dw, lo), BitCast(dw, hi)));
#else
  (void)d;
  constexpr __vector unsigned char kShuffle = {0,  1,  2,  3,  8,  9,  10, 11,
                                               16, 17, 18, 19, 24, 25, 26, 27};
  return Vec128<T>{vec_perm(lo.raw, hi.raw, kShuffle)};
#endif
}

// Any T x2
template <typename D, typename T = TFromD<D>, HWY_IF_LANES_D(D, 2)>
HWY_API Vec128<T, 2> ConcatEven(D d, Vec128<T, 2> hi, Vec128<T, 2> lo) {
  return InterleaveLower(d, lo, hi);
}

// ------------------------------ OrderedTruncate2To (ConcatEven, ConcatOdd)
#ifdef HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#undef HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#else
#define HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#endif

template <class D, HWY_IF_UNSIGNED_D(D), class V, HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<D>) * 2),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedTruncate2To(D d, V a, V b) {
#if HWY_IS_LITTLE_ENDIAN
  return ConcatEven(d, BitCast(d, b), BitCast(d, a));
#else
  return ConcatOdd(d, BitCast(d, b), BitCast(d, a));
#endif
}

// ------------------------------ DupEven (InterleaveLower)

template <typename T>
HWY_API Vec128<T, 1> DupEven(Vec128<T, 1> v) {
  return v;
}

template <typename T>
HWY_API Vec128<T, 2> DupEven(Vec128<T, 2> v) {
  return InterleaveLower(DFromV<decltype(v)>(), v, v);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  constexpr __vector unsigned char kShuffle = {0, 0, 2,  2,  4,  4,  6,  6,
                                               8, 8, 10, 10, 12, 12, 14, 14};
  return TableLookupBytes(v, BitCast(d, VFromD<decltype(du8)>{kShuffle}));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  constexpr __vector unsigned char kShuffle = {0, 1, 0, 1, 4,  5,  4,  5,
                                               8, 9, 8, 9, 12, 13, 12, 13};
  return TableLookupBytes(v, BitCast(d, VFromD<decltype(du8)>{kShuffle}));
}

template <typename T, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T> DupEven(Vec128<T> v) {
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  return TableLookupBytes(
      v, BitCast(d, Dup128VecFromValues(du8, 0, 1, 2, 3, 0, 1, 2, 3, 8, 9, 10,
                                        11, 8, 9, 10, 11)));
#else
  return Vec128<T>{vec_mergee(v.raw, v.raw)};
#endif
}

// ------------------------------ DupOdd (InterleaveUpper)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  constexpr __vector unsigned char kShuffle = {1, 1, 3,  3,  5,  5,  7,  7,
                                               9, 9, 11, 11, 13, 13, 15, 15};
  return TableLookupBytes(v, BitCast(d, VFromD<decltype(du8)>{kShuffle}));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  constexpr __vector unsigned char kShuffle = {2,  3,  2,  3,  6,  7,  6,  7,
                                               10, 11, 10, 11, 14, 15, 14, 15};
  return TableLookupBytes(v, BitCast(d, VFromD<decltype(du8)>{kShuffle}));
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  return TableLookupBytes(
      v, BitCast(d, Dup128VecFromValues(du8, 4, 5, 6, 7, 4, 5, 6, 7, 12, 13, 14,
                                        15, 12, 13, 14, 15)));
#else
  return Vec128<T, N>{vec_mergeo(v.raw, v.raw)};
#endif
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  return InterleaveUpper(DFromV<decltype(v)>(), v, v);
}

// ------------------------------ OddEven (IfThenElse)

template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_INLINE Vec128<T, N> OddEven(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const __vector unsigned char mask = {0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0,
                                       0xFF, 0, 0xFF, 0, 0xFF, 0, 0xFF, 0};
  return IfVecThenElse(BitCast(d, Vec128<uint8_t, N>{mask}), b, a);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 2)>
HWY_INLINE Vec128<T, N> OddEven(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const __vector unsigned char mask = {0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0,
                                       0xFF, 0xFF, 0, 0, 0xFF, 0xFF, 0, 0};
  return IfVecThenElse(BitCast(d, Vec128<uint8_t, N * 2>{mask}), b, a);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 4)>
HWY_INLINE Vec128<T, N> OddEven(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const __vector unsigned char mask = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
                                       0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
  return IfVecThenElse(BitCast(d, Vec128<uint8_t, N * 4>{mask}), b, a);
}

template <typename T, size_t N, HWY_IF_T_SIZE(T, 8)>
HWY_INLINE Vec128<T, N> OddEven(Vec128<T, N> a, Vec128<T, N> b) {
  // Same as ConcatUpperLower for full vectors; do not call that because this
  // is more efficient for 64x1 vectors.
  const DFromV<decltype(a)> d;
  const __vector unsigned char mask = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0, 0, 0, 0, 0};
  return IfVecThenElse(BitCast(d, Vec128<uint8_t, N * 8>{mask}), b, a);
}

// ------------------------------ InterleaveEven

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{
      Dup128VecFromValues(Full128<uint8_t>(), 0, 16, 2, 18, 4, 20, 6, 22, 8, 24,
                          10, 26, 12, 28, 14, 30)
          .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{Dup128VecFromValues(Full128<uint8_t>(), 0, 1,
                                                      16, 17, 4, 5, 20, 21, 8,
                                                      9, 24, 25, 12, 13, 28, 29)
                                      .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveEven(D d, VFromD<D> a, VFromD<D> b) {
#if HWY_S390X_HAVE_Z14
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{Dup128VecFromValues(Full128<uint8_t>(), 0, 1,
                                                      2, 3, 16, 17, 18, 19, 8,
                                                      9, 10, 11, 24, 25, 26, 27)
                                      .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
#else
  (void)d;
  return VFromD<D>{vec_mergee(a.raw, b.raw)};
#endif
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

// ------------------------------ InterleaveOdd

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{
      Dup128VecFromValues(Full128<uint8_t>(), 1, 17, 3, 19, 5, 21, 7, 23, 9, 25,
                          11, 27, 13, 29, 15, 31)
          .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{
      Dup128VecFromValues(Full128<uint8_t>(), 2, 3, 18, 19, 6, 7, 22, 23, 10,
                          11, 26, 27, 14, 15, 30, 31)
          .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> InterleaveOdd(D d, VFromD<D> a, VFromD<D> b) {
#if HWY_S390X_HAVE_Z14
  const Full128<TFromD<D>> d_full;
  const Indices128<TFromD<D>> idx{
      Dup128VecFromValues(Full128<uint8_t>(), 4, 5, 6, 7, 20, 21, 22, 23, 12,
                          13, 14, 15, 28, 29, 30, 31)
          .raw};
  return ResizeBitCast(d, TwoTablesLookupLanes(ResizeBitCast(d_full, a),
                                               ResizeBitCast(d_full, b), idx));
#else
  (void)d;
  return VFromD<D>{vec_mergeo(a.raw, b.raw)};
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

// ------------------------------ MulFixedPoint15 (OddEven)

#if HWY_S390X_HAVE_Z14
HWY_API Vec16<int16_t> MulFixedPoint15(Vec16<int16_t> a, Vec16<int16_t> b) {
  const DFromV<decltype(a)> di16;
  const RepartitionToWide<decltype(di16)> di32;

  const auto round_up_incr = Set(di32, 0x4000);
  const auto i32_product = MulEven(a, b) + round_up_incr;

  return ResizeBitCast(di16, ShiftLeft<1>(i32_product));
}
template <size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Vec128<int16_t, N> MulFixedPoint15(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  const DFromV<decltype(a)> di16;
  const RepartitionToWide<decltype(di16)> di32;

  const auto round_up_incr = Set(di32, 0x4000);
  const auto even_product = MulEven(a, b) + round_up_incr;
  const auto odd_product = MulOdd(a, b) + round_up_incr;

  return OddEven(BitCast(di16, ShiftRight<15>(odd_product)),
                 BitCast(di16, ShiftLeft<1>(even_product)));
}
#else
template <size_t N>
HWY_API Vec128<int16_t, N> MulFixedPoint15(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  const Vec128<int16_t> zero = Zero(Full128<int16_t>());
  return Vec128<int16_t, N>{vec_mradds(a.raw, b.raw, zero.raw)};
}
#endif

// ------------------------------ Shl

namespace detail {
template <typename T, size_t N>
HWY_API Vec128<T, N> Shl(hwy::UnsignedTag /*tag*/, Vec128<T, N> v,
                         Vec128<T, N> bits) {
#if HWY_S390X_HAVE_Z14
  return Vec128<T, N>{v.raw << bits.raw};
#else
  return Vec128<T, N>{vec_sl(v.raw, bits.raw)};
#endif
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

template <typename T, size_t N, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, Vec128<T, N> bits) {
  return detail::Shl(hwy::TypeTag<T>(), v, bits);
}

// ------------------------------ Shr

namespace detail {
template <typename T, size_t N>
HWY_API Vec128<T, N> Shr(hwy::UnsignedTag /*tag*/, Vec128<T, N> v,
                         Vec128<T, N> bits) {
#if HWY_S390X_HAVE_Z14
  return Vec128<T, N>{v.raw >> bits.raw};
#else
  return Vec128<T, N>{vec_sr(v.raw, bits.raw)};
#endif
}

template <typename T, size_t N>
HWY_API Vec128<T, N> Shr(hwy::SignedTag /*tag*/, Vec128<T, N> v,
                         Vec128<T, N> bits) {
#if HWY_S390X_HAVE_Z14
  return Vec128<T, N>{v.raw >> bits.raw};
#else
  const DFromV<decltype(v)> di;
  const RebindToUnsigned<decltype(di)> du;
  return Vec128<T, N>{vec_sra(v.raw, BitCast(du, bits).raw)};
#endif
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, Vec128<T, N> bits) {
  return detail::Shr(hwy::TypeTag<T>(), v, bits);
}

// ------------------------------ MulEven/Odd 64x64 (UpperHalf)

template <class T, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T> MulEven(Vec128<T> a, Vec128<T> b) {
#if HWY_PPC_HAVE_10 && defined(__SIZEOF_INT128__)
  using V64 = typename detail::Raw128<T>::type;
  const V64 mul128_result = reinterpret_cast<V64>(vec_mule(a.raw, b.raw));
#if HWY_IS_LITTLE_ENDIAN
  return Vec128<T>{mul128_result};
#else
  // Need to swap the two halves of mul128_result on big-endian targets as
  // the upper 64 bits of the product are in lane 0 of mul128_result and
  // the lower 64 bits of the product are in lane 1 of mul128_result
  return Vec128<T>{vec_sld(mul128_result, mul128_result, 8)};
#endif
#else
  alignas(16) T mul[2];
  mul[0] = Mul128(GetLane(a), GetLane(b), &mul[1]);
  return Load(Full128<T>(), mul);
#endif
}

template <class T, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T> MulOdd(Vec128<T> a, Vec128<T> b) {
#if HWY_PPC_HAVE_10 && defined(__SIZEOF_INT128__)
  using V64 = typename detail::Raw128<T>::type;
  const V64 mul128_result = reinterpret_cast<V64>(vec_mulo(a.raw, b.raw));
#if HWY_IS_LITTLE_ENDIAN
  return Vec128<T>{mul128_result};
#else
  // Need to swap the two halves of mul128_result on big-endian targets as
  // the upper 64 bits of the product are in lane 0 of mul128_result and
  // the lower 64 bits of the product are in lane 1 of mul128_result
  return Vec128<T>{vec_sld(mul128_result, mul128_result, 8)};
#endif
#else
  alignas(16) T mul[2];
  const Full64<T> d2;
  mul[0] =
      Mul128(GetLane(UpperHalf(d2, a)), GetLane(UpperHalf(d2, b)), &mul[1]);
  return Load(Full128<T>(), mul);
#endif
}

// ------------------------------ PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ------------------------------ WidenMulPairwiseAdd

template <class DF, HWY_IF_F32_D(DF),
          class VBF = VFromD<Repartition<bfloat16_t, DF>>>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
}

// Even if N=1, the input is always at least 2 lanes, hence vec_msum is safe.
template <class D32, HWY_IF_UI32_D(D32),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> WidenMulPairwiseAdd(D32 d32, V16 a, V16 b) {
#if HWY_S390X_HAVE_Z14
  (void)d32;
  return MulEven(a, b) + MulOdd(a, b);
#else
  return VFromD<D32>{vec_msum(a.raw, b.raw, Zero(d32).raw)};
#endif
}

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

// Even if N=1, the input is always at least 2 lanes, hence vec_msum is safe.
template <class D32, HWY_IF_UI32_D(D32),
          class V16 = VFromD<RepartitionToNarrow<D32>>>
HWY_API VFromD<D32> ReorderWidenMulAccumulate(D32 /*d32*/, V16 a, V16 b,
                                              VFromD<D32> sum0,
                                              VFromD<D32>& /*sum1*/) {
#if HWY_S390X_HAVE_Z14
  return MulEven(a, b) + MulOdd(a, b) + sum0;
#else
  return VFromD<D32>{vec_msum(a.raw, b.raw, sum0.raw)};
#endif
}

// ------------------------------ RearrangeToOddPlusEven
template <size_t N>
HWY_API Vec128<int32_t, N> RearrangeToOddPlusEven(Vec128<int32_t, N> sum0,
                                                  Vec128<int32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <size_t N>
HWY_API Vec128<uint32_t, N> RearrangeToOddPlusEven(
    Vec128<uint32_t, N> sum0, Vec128<uint32_t, N> /*sum1*/) {
  return sum0;  // invariant already holds
}

template <class VW>
HWY_API VW RearrangeToOddPlusEven(const VW sum0, const VW sum1) {
  return Add(sum0, sum1);
}

// ------------------------------ SatWidenMulPairwiseAccumulate
#if !HWY_S390X_HAVE_Z14

#ifdef HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#undef HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#else
#define HWY_NATIVE_I16_I16_SATWIDENMULPAIRWISEACCUM
#endif

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 16)>
HWY_API VFromD<DI32> SatWidenMulPairwiseAccumulate(
    DI32 /* tag */, VFromD<Repartition<int16_t, DI32>> a,
    VFromD<Repartition<int16_t, DI32>> b, VFromD<DI32> sum) {
  return VFromD<DI32>{vec_msums(a.raw, b.raw, sum.raw)};
}

#endif  // !HWY_S390X_HAVE_Z14

// ------------------------------ SumOfMulQuadAccumulate
#if !HWY_S390X_HAVE_Z14

#ifdef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_U8_SUMOFMULQUADACCUMULATE
#endif
template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API VFromD<DU32> SumOfMulQuadAccumulate(
    DU32 /*du32*/, VFromD<Repartition<uint8_t, DU32>> a,
    VFromD<Repartition<uint8_t, DU32>> b, VFromD<DU32> sum) {
  return VFromD<DU32>{vec_msum(a.raw, b.raw, sum.raw)};
}

#ifdef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#undef HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#else
#define HWY_NATIVE_U8_I8_SUMOFMULQUADACCUMULATE
#endif

template <class DI32, HWY_IF_I32_D(DI32), HWY_IF_V_SIZE_LE_D(DI32, 16)>
HWY_API VFromD<DI32> SumOfMulQuadAccumulate(
    DI32 /*di32*/, VFromD<Repartition<uint8_t, DI32>> a_u,
    VFromD<Repartition<int8_t, DI32>> b_i, VFromD<DI32> sum) {
  return VFromD<DI32>{vec_msum(b_i.raw, a_u.raw, sum.raw)};
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
  const Repartition<uint8_t, decltype(di32)> du8;

  const auto result_sum_0 =
      SumOfMulQuadAccumulate(di32, BitCast(du8, a), b, sum);
  const auto result_sum_1 = ShiftLeft<8>(SumsOf4(And(b, BroadcastSignBit(a))));
  return result_sum_0 - result_sum_1;
}

#endif  // !HWY_S390X_HAVE_Z14

// ================================================== CONVERT

// ------------------------------ Promotions (part w/ narrow lanes -> full)

// Unsigned to signed/unsigned: zero-extend.
template <class D, typename FromT, HWY_IF_T_SIZE_D(D, 2 * sizeof(FromT)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D), HWY_IF_UNSIGNED(FromT)>
HWY_API VFromD<D> PromoteTo(D /* d */,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  // First pretend the input has twice the lanes - the upper half will be
  // ignored by ZipLower.
  const Rebind<FromT, Twice<D>> d2;
  const VFromD<decltype(d2)> twice{v.raw};
  // Then cast to narrow as expected by ZipLower, in case the sign of FromT
  // differs from that of D.
  const RepartitionToNarrow<D> dn;

#if HWY_IS_LITTLE_ENDIAN
  return ZipLower(BitCast(dn, twice), Zero(dn));
#else
  return ZipLower(Zero(dn), BitCast(dn, twice));
#endif
}

// Signed: replicate sign bit.
template <class D, typename FromT, HWY_IF_T_SIZE_D(D, 2 * sizeof(FromT)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D), HWY_IF_SIGNED(FromT)>
HWY_API VFromD<D> PromoteTo(D /* d */,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  using Raw = typename detail::Raw128<TFromD<D>>::type;
  return VFromD<D>{reinterpret_cast<Raw>(vec_unpackh(v.raw))};
}

// 8-bit to 32-bit: First, promote to 16-bit, and then convert to 32-bit.
template <class D, typename FromT, HWY_IF_T_SIZE_D(D, 4), HWY_IF_NOT_FLOAT_D(D),
          HWY_IF_T_SIZE(FromT, 1)>
HWY_API VFromD<D> PromoteTo(D d32,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const DFromV<decltype(v)> d8;
  const Rebind<MakeWide<FromT>, decltype(d8)> d16;
  return PromoteTo(d32, PromoteTo(d16, v));
}

// 8-bit or 16-bit to 64-bit: First, promote to MakeWide<FromT>, and then
// convert to 64-bit.
template <class D, typename FromT, HWY_IF_T_SIZE_D(D, 8), HWY_IF_NOT_FLOAT_D(D),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(FromT),
          HWY_IF_T_SIZE_ONE_OF(FromT, (1 << 1) | (1 << 2))>
HWY_API VFromD<D> PromoteTo(D d64,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<MakeWide<FromT>, decltype(d64)> dw;
  return PromoteTo(d64, PromoteTo(dw, v));
}

#if HWY_PPC_HAVE_9

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif

template <class D, HWY_IF_F32_D(D)>
HWY_INLINE VFromD<D> PromoteTo(D /*tag*/, VFromD<Rebind<float16_t, D>> v) {
  return VFromD<D>{vec_extract_fp32_from_shorth(v.raw)};
}

#endif  // HWY_PPC_HAVE_9

template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteTo(D df32, VFromD<Rebind<bfloat16_t, D>> v) {
  const Rebind<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteTo(di32, BitCast(du16, v))));
}

template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D /* tag */, VFromD<Rebind<float, D>> v) {
  const __vector float raw_v = InterleaveLower(v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
}

template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D df64, VFromD<Rebind<int32_t, D>> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToSigned<decltype(df64)> di64;
  return ConvertTo(df64, PromoteTo(di64, v));
#else  // VSX
  (void)df64;
  const __vector signed int raw_v = InterleaveLower(v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
#endif  // HWY_S390X_HAVE_Z14
}

template <class D, HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteTo(D df64, VFromD<Rebind<uint32_t, D>> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(df64)> du64;
  return ConvertTo(df64, PromoteTo(du64, v));
#else  // VSX
  (void)df64;
  const __vector unsigned int raw_v = InterleaveLower(v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
#endif  // HWY_S390X_HAVE_Z14
}

#if !HWY_S390X_HAVE_Z14
namespace detail {

template <class V>
static HWY_INLINE V VsxF2INormalizeSrcVals(V v) {
#if !defined(HWY_DISABLE_PPC_VSX_QEMU_F2I_WORKAROUND)
  // Workaround for QEMU 7/8 VSX float to int conversion bug
  return IfThenElseZero(v == v, v);
#else
  return v;
#endif
}

}  // namespace detail
#endif  // !HWY_S390X_HAVE_Z14

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteTo(D di64, VFromD<Rebind<float, D>> v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspsxds))
  const __vector float raw_v =
      detail::VsxF2INormalizeSrcVals(InterleaveLower(v, v)).raw;
  return VFromD<decltype(di64)>{__builtin_vsx_xvcvspsxds(raw_v)};
#else
  const RebindToFloat<decltype(di64)> df64;
  return ConvertTo(di64, PromoteTo(df64, v));
#endif
}

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteTo(D du64, VFromD<Rebind<float, D>> v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspuxds))
  const __vector float raw_v =
      detail::VsxF2INormalizeSrcVals(InterleaveLower(v, v)).raw;
  return VFromD<decltype(du64)>{reinterpret_cast<__vector unsigned long long>(
      __builtin_vsx_xvcvspuxds(raw_v))};
#else
  const RebindToFloat<decltype(du64)> df64;
  return ConvertTo(du64, PromoteTo(df64, v));
#endif
}

// ------------------------------ PromoteUpperTo

#ifdef HWY_NATIVE_PROMOTE_UPPER_TO
#undef HWY_NATIVE_PROMOTE_UPPER_TO
#else
#define HWY_NATIVE_PROMOTE_UPPER_TO
#endif

// Unsigned to signed/unsigned: zero-extend.
template <class D, typename FromT, HWY_IF_V_SIZE_D(D, 16),
          HWY_IF_T_SIZE_D(D, 2 * sizeof(FromT)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D), HWY_IF_UNSIGNED(FromT)>
HWY_API VFromD<D> PromoteUpperTo(D d, Vec128<FromT> v) {
  const RebindToUnsigned<D> du;
  const RepartitionToNarrow<decltype(du)> dn;

#if HWY_IS_LITTLE_ENDIAN
  return BitCast(d, ZipUpper(du, v, Zero(dn)));
#else
  return BitCast(d, ZipUpper(du, Zero(dn), v));
#endif
}

// Signed: replicate sign bit.
template <class D, typename FromT, HWY_IF_V_SIZE_D(D, 16),
          HWY_IF_T_SIZE_D(D, 2 * sizeof(FromT)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D), HWY_IF_SIGNED(FromT)>
HWY_API VFromD<D> PromoteUpperTo(D /* d */, Vec128<FromT> v) {
  using Raw = typename detail::Raw128<TFromD<D>>::type;
  return VFromD<D>{reinterpret_cast<Raw>(vec_unpackl(v.raw))};
}

// F16 to F32
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df32, Vec128<float16_t> v) {
#if HWY_PPC_HAVE_9
  (void)df32;
  return VFromD<D>{vec_extract_fp32_from_shortl(v.raw)};
#else
  const Rebind<float16_t, decltype(df32)> dh;
  return PromoteTo(df32, UpperHalf(dh, v));
#endif
}

// BF16 to F32
template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F32_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df32, Vec128<bfloat16_t> v) {
  const Repartition<uint16_t, decltype(df32)> du16;
  const RebindToSigned<decltype(df32)> di32;
  return BitCast(df32, ShiftLeft<16>(PromoteUpperTo(di32, BitCast(du16, v))));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D /*tag*/, Vec128<float> v) {
  const __vector float raw_v = InterleaveUpper(Full128<float>(), v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df64, Vec128<int32_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToSigned<decltype(df64)> di64;
  return ConvertTo(df64, PromoteUpperTo(di64, v));
#else  // VSX
  (void)df64;
  const __vector signed int raw_v =
      InterleaveUpper(Full128<int32_t>(), v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
#endif  // HWY_S390X_HAVE_Z14
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_F64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D df64, Vec128<uint32_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(df64)> du64;
  return ConvertTo(df64, PromoteUpperTo(du64, v));
#else  // VSX
  (void)df64;
  const __vector unsigned int raw_v =
      InterleaveUpper(Full128<uint32_t>(), v, v).raw;
#if HWY_IS_LITTLE_ENDIAN
  return VFromD<D>{vec_doubleo(raw_v)};
#else
  return VFromD<D>{vec_doublee(raw_v)};
#endif
#endif  // HWY_S390X_HAVE_Z14
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D di64, Vec128<float> v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspsxds))
  const __vector float raw_v =
      detail::VsxF2INormalizeSrcVals(InterleaveUpper(Full128<float>(), v, v))
          .raw;
  return VFromD<decltype(di64)>{__builtin_vsx_xvcvspsxds(raw_v)};
#else
  const RebindToFloat<decltype(di64)> df64;
  return ConvertTo(di64, PromoteUpperTo(df64, v));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U64_D(D)>
HWY_API VFromD<D> PromoteUpperTo(D du64, Vec128<float> v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspuxds))
  const __vector float raw_v =
      detail::VsxF2INormalizeSrcVals(InterleaveUpper(Full128<float>(), v, v))
          .raw;
  return VFromD<decltype(du64)>{reinterpret_cast<__vector unsigned long long>(
      __builtin_vsx_xvcvspuxds(raw_v))};
#else
  const RebindToFloat<decltype(du64)> df64;
  return ConvertTo(du64, PromoteUpperTo(df64, v));
#endif
}

// Generic version for <=64 bit input/output
template <class D, HWY_IF_V_SIZE_LE_D(D, 8), class V>
HWY_API VFromD<D> PromoteUpperTo(D d, V v) {
  const Rebind<TFromV<V>, decltype(d)> dh;
  return PromoteTo(d, UpperHalf(dh, v));
}

// ------------------------------ PromoteEvenTo/PromoteOddTo

namespace detail {

// Signed to Signed PromoteEvenTo/PromoteOddTo for PPC9/PPC10
#if HWY_PPC_HAVE_9 && \
    (HWY_COMPILER_GCC_ACTUAL >= 1200 || HWY_COMPILER_CLANG >= 1200)

#if HWY_IS_LITTLE_ENDIAN
template <class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<4> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D /*d_to*/,
                                   V v) {
  return VFromD<D>{vec_signexti(v.raw)};
}
template <class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::SignedTag /*from_type_tag*/, D /*d_to*/,
                                   V v) {
  return VFromD<D>{vec_signextll(v.raw)};
}
#else
template <class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::SignedTag /*to_type_tag*/,
                                  hwy::SizeTag<4> /*to_lane_size_tag*/,
                                  hwy::SignedTag /*from_type_tag*/, D /*d_to*/,
                                  V v) {
  return VFromD<D>{vec_signexti(v.raw)};
}
template <class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::SignedTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  hwy::SignedTag /*from_type_tag*/, D /*d_to*/,
                                  V v) {
  return VFromD<D>{vec_signextll(v.raw)};
}
#endif  // HWY_IS_LITTLE_ENDIAN

#endif  // HWY_PPC_HAVE_9

// I32/U32/F32->F64 PromoteEvenTo
#if HWY_S390X_HAVE_Z14
template <class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D /*d_to*/,
                                   V v) {
  return VFromD<D>{vec_doublee(v.raw)};
}
template <class D, class V, class FromTypeTag, HWY_IF_UI32(TFromV<V>)>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   FromTypeTag /*from_type_tag*/, D d_to, V v) {
  const Rebind<MakeWide<TFromV<V>>, decltype(d_to)> dw;
  return ConvertTo(d_to, PromoteEvenTo(dw, v));
}
#else   // VSX
template <class D, class V, class FromTypeTag>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   FromTypeTag /*from_type_tag*/, D /*d_to*/,
                                   V v) {
  return VFromD<D>{vec_doublee(v.raw)};
}
#endif  // HWY_S390X_HAVE_Z14

// F32->I64 PromoteEvenTo
template <class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::SignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   V v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspsxds))
  (void)d_to;
  const auto normalized_v = detail::VsxF2INormalizeSrcVals(v);
#if HWY_IS_LITTLE_ENDIAN
  // __builtin_vsx_xvcvspsxds expects the source values to be in the odd lanes
  // on little-endian PPC, and the vec_sld operation below will shift the even
  // lanes of normalized_v into the odd lanes.
  return VFromD<D>{
      __builtin_vsx_xvcvspsxds(vec_sld(normalized_v.raw, normalized_v.raw, 4))};
#else
  // __builtin_vsx_xvcvspsxds expects the source values to be in the even lanes
  // on big-endian PPC.
  return VFromD<D>{__builtin_vsx_xvcvspsxds(normalized_v.raw)};
#endif
#else
  const RebindToFloat<decltype(d_to)> df64;
  return ConvertTo(d_to, PromoteEvenTo(hwy::FloatTag(), hwy::SizeTag<8>(),
                                       hwy::FloatTag(), df64, v));
#endif
}

// F32->U64 PromoteEvenTo
template <class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(hwy::UnsignedTag /*to_type_tag*/,
                                   hwy::SizeTag<8> /*to_lane_size_tag*/,
                                   hwy::FloatTag /*from_type_tag*/, D d_to,
                                   V v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspuxds))
  (void)d_to;
  const auto normalized_v = detail::VsxF2INormalizeSrcVals(v);
#if HWY_IS_LITTLE_ENDIAN
  // __builtin_vsx_xvcvspuxds expects the source values to be in the odd lanes
  // on little-endian PPC, and the vec_sld operation below will shift the even
  // lanes of normalized_v into the odd lanes.
  return VFromD<D>{
      reinterpret_cast<__vector unsigned long long>(__builtin_vsx_xvcvspuxds(
          vec_sld(normalized_v.raw, normalized_v.raw, 4)))};
#else
  // __builtin_vsx_xvcvspuxds expects the source values to be in the even lanes
  // on big-endian PPC.
  return VFromD<D>{reinterpret_cast<__vector unsigned long long>(
      __builtin_vsx_xvcvspuxds(normalized_v.raw))};
#endif
#else
  const RebindToFloat<decltype(d_to)> df64;
  return ConvertTo(d_to, PromoteEvenTo(hwy::FloatTag(), hwy::SizeTag<8>(),
                                       hwy::FloatTag(), df64, v));
#endif
}

// I32/U32/F32->F64 PromoteOddTo
#if HWY_S390X_HAVE_Z14
template <class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::FloatTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  hwy::FloatTag /*from_type_tag*/, D d_to,
                                  V v) {
  return PromoteEvenTo(hwy::FloatTag(), hwy::SizeTag<8>(), hwy::FloatTag(),
                       d_to, V{vec_sld(v.raw, v.raw, 4)});
}
template <class D, class V, class FromTypeTag, HWY_IF_UI32(TFromV<V>)>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::FloatTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  FromTypeTag /*from_type_tag*/, D d_to, V v) {
  const Rebind<MakeWide<TFromV<V>>, decltype(d_to)> dw;
  return ConvertTo(d_to, PromoteOddTo(dw, v));
}
#else
template <class D, class V, class FromTypeTag>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::FloatTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  FromTypeTag /*from_type_tag*/, D /*d_to*/,
                                  V v) {
  return VFromD<D>{vec_doubleo(v.raw)};
}
#endif

// F32->I64 PromoteOddTo
template <class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::SignedTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  hwy::FloatTag /*from_type_tag*/, D d_to,
                                  V v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspsxds))
  (void)d_to;
  const auto normalized_v = detail::VsxF2INormalizeSrcVals(v);
#if HWY_IS_LITTLE_ENDIAN
  // __builtin_vsx_xvcvspsxds expects the source values to be in the odd lanes
  // on little-endian PPC
  return VFromD<D>{__builtin_vsx_xvcvspsxds(normalized_v.raw)};
#else
  // __builtin_vsx_xvcvspsxds expects the source values to be in the even lanes
  // on big-endian PPC, and the vec_sld operation below will shift the odd lanes
  // of normalized_v into the even lanes.
  return VFromD<D>{
      __builtin_vsx_xvcvspsxds(vec_sld(normalized_v.raw, normalized_v.raw, 4))};
#endif
#else
  const RebindToFloat<decltype(d_to)> df64;
  return ConvertTo(d_to, PromoteOddTo(hwy::FloatTag(), hwy::SizeTag<8>(),
                                      hwy::FloatTag(), df64, v));
#endif
}

// F32->U64 PromoteOddTo
template <class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(hwy::UnsignedTag /*to_type_tag*/,
                                  hwy::SizeTag<8> /*to_lane_size_tag*/,
                                  hwy::FloatTag /*from_type_tag*/, D d_to,
                                  V v) {
#if !HWY_S390X_HAVE_Z14 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvspuxds))
  (void)d_to;
  const auto normalized_v = detail::VsxF2INormalizeSrcVals(v);
#if HWY_IS_LITTLE_ENDIAN
  // __builtin_vsx_xvcvspuxds expects the source values to be in the odd lanes
  // on little-endian PPC
  return VFromD<D>{reinterpret_cast<__vector unsigned long long>(
      __builtin_vsx_xvcvspuxds(normalized_v.raw))};
#else
  // __builtin_vsx_xvcvspuxds expects the source values to be in the even lanes
  // on big-endian PPC, and the vec_sld operation below will shift the odd lanes
  // of normalized_v into the even lanes.
  return VFromD<D>{
      reinterpret_cast<__vector unsigned long long>(__builtin_vsx_xvcvspuxds(
          vec_sld(normalized_v.raw, normalized_v.raw, 4)))};
#endif
#else
  const RebindToFloat<decltype(d_to)> df64;
  return ConvertTo(d_to, PromoteOddTo(hwy::FloatTag(), hwy::SizeTag<8>(),
                                      hwy::FloatTag(), df64, v));
#endif
}

}  // namespace detail

// ------------------------------ Demotions (full -> part w/ narrow lanes)

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_SIGNED(FromT), HWY_IF_T_SIZE(FromT, sizeof(TFromD<D>) * 2)>
HWY_API VFromD<D> DemoteTo(D /* tag */,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  return VFromD<D>{vec_packsu(v.raw, v.raw)};
}

template <class D, typename FromT, HWY_IF_SIGNED_D(D), HWY_IF_SIGNED(FromT),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE(FromT, sizeof(TFromD<D>) * 2)>
HWY_API VFromD<D> DemoteTo(D /* tag */,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  return VFromD<D>{vec_packs(v.raw, v.raw)};
}

template <class D, typename FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE(FromT, sizeof(TFromD<D>) * 2)>
HWY_API VFromD<D> DemoteTo(D /* tag */,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  return VFromD<D>{vec_packs(v.raw, v.raw)};
}

template <class D, class FromT, HWY_IF_SIGNED_D(D), HWY_IF_SIGNED(FromT),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          hwy::EnableIf<(sizeof(FromT) >= sizeof(TFromD<D>) * 4)>* = nullptr>
HWY_API VFromD<D> DemoteTo(D d,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<MakeNarrow<FromT>, D> d2;
  return DemoteTo(d, DemoteTo(d2, v));
}

template <class D, class FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_UNSIGNED(FromT),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          hwy::EnableIf<(sizeof(FromT) >= sizeof(TFromD<D>) * 4)>* = nullptr>
HWY_API VFromD<D> DemoteTo(D d,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<MakeNarrow<FromT>, D> d2;
  return DemoteTo(d, DemoteTo(d2, v));
}

template <class D, class FromT, HWY_IF_UNSIGNED_D(D), HWY_IF_SIGNED(FromT),
          HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 1) | (1 << 2)),
          hwy::EnableIf<(sizeof(FromT) >= sizeof(TFromD<D>) * 4)>* = nullptr>
HWY_API VFromD<D> DemoteTo(D d,
                           Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<MakeUnsigned<MakeNarrow<FromT>>, D> d2;
  return DemoteTo(d, DemoteTo(d2, v));
}

#if HWY_PPC_HAVE_9 && \
    (HWY_COMPILER_GCC_ACTUAL || HWY_HAS_BUILTIN(__builtin_vsx_xvcvsphp))

// We already toggled HWY_NATIVE_F16C above.

template <class D, HWY_IF_V_SIZE_LE_D(D, 8), HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<float, D>> v) {
// Avoid vec_pack_to_short_fp32 on Clang because its implementation is buggy.
#if HWY_COMPILER_GCC_ACTUAL
  (void)df16;
  return VFromD<D>{vec_pack_to_short_fp32(v.raw, v.raw)};
#elif HWY_HAS_BUILTIN(__builtin_vsx_xvcvsphp)
  // Work around bug in the clang implementation of vec_pack_to_short_fp32
  // by using the __builtin_vsx_xvcvsphp builtin on PPC9/PPC10 targets
  // if the __builtin_vsx_xvcvsphp intrinsic is available
  const RebindToUnsigned<decltype(df16)> du16;
  const Rebind<uint32_t, D> du;
  const VFromD<decltype(du)> bits16{
      reinterpret_cast<__vector unsigned int>(__builtin_vsx_xvcvsphp(v.raw))};
  return BitCast(df16, TruncateTo(du16, bits16));
#else
#error "Only define the function if we have a native implementation"
#endif
}

#endif  // HWY_PPC_HAVE_9

#if HWY_PPC_HAVE_9

#ifdef HWY_NATIVE_DEMOTE_F64_TO_F16
#undef HWY_NATIVE_DEMOTE_F64_TO_F16
#else
#define HWY_NATIVE_DEMOTE_F64_TO_F16
#endif

namespace detail {

// On big-endian PPC9, VsxXscvdphp converts vf64[0] to a F16, returned as an U64
// vector with the resulting F16 bits in the lower 16 bits of U64 lane 0

// On little-endian PPC9, VsxXscvdphp converts vf64[1] to a F16, returned as
// an U64 vector with the resulting F16 bits in the lower 16 bits of U64 lane 1
static HWY_INLINE Vec128<uint64_t> VsxXscvdphp(Vec128<double> vf64) {
  // Inline assembly is needed for the PPC9 xscvdphp instruction as there is
  // currently no intrinsic available for the PPC9 xscvdphp instruction
  __vector unsigned long long raw_result;
  __asm__("xscvdphp %x0, %x1" : "=wa"(raw_result) : "wa"(vf64.raw));
  return Vec128<uint64_t>{raw_result};
}

}  // namespace detail

template <class D, HWY_IF_F16_D(D), HWY_IF_LANES_D(D, 1)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<double, D>> v) {
  const RebindToUnsigned<decltype(df16)> du16;
  const Rebind<uint64_t, decltype(df16)> du64;

  const Full128<double> df64_full;
#if HWY_IS_LITTLE_ENDIAN
  const auto bits16_as_u64 =
      UpperHalf(du64, detail::VsxXscvdphp(Combine(df64_full, v, v)));
#else
  const auto bits16_as_u64 =
      LowerHalf(du64, detail::VsxXscvdphp(ResizeBitCast(df64_full, v)));
#endif

  return BitCast(df16, TruncateTo(du16, bits16_as_u64));
}

template <class D, HWY_IF_F16_D(D), HWY_IF_LANES_D(D, 2)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<double, D>> v) {
  const RebindToUnsigned<decltype(df16)> du16;
  const Rebind<uint64_t, decltype(df16)> du64;
  const Rebind<double, decltype(df16)> df64;

#if HWY_IS_LITTLE_ENDIAN
  const auto bits64_as_u64_0 = detail::VsxXscvdphp(InterleaveLower(df64, v, v));
  const auto bits64_as_u64_1 = detail::VsxXscvdphp(v);
  const auto bits64_as_u64 =
      InterleaveUpper(du64, bits64_as_u64_0, bits64_as_u64_1);
#else
  const auto bits64_as_u64_0 = detail::VsxXscvdphp(v);
  const auto bits64_as_u64_1 = detail::VsxXscvdphp(InterleaveUpper(df64, v, v));
  const auto bits64_as_u64 =
      InterleaveLower(du64, bits64_as_u64_0, bits64_as_u64_1);
#endif

  return BitCast(df16, TruncateTo(du16, bits64_as_u64));
}

#elif HWY_S390X_HAVE_Z14

#ifdef HWY_NATIVE_DEMOTE_F64_TO_F16
#undef HWY_NATIVE_DEMOTE_F64_TO_F16
#else
#define HWY_NATIVE_DEMOTE_F64_TO_F16
#endif

namespace detail {

template <class DF32, HWY_IF_F32_D(DF32)>
static HWY_INLINE VFromD<DF32> DemoteToF32WithRoundToOdd(
    DF32 df32, VFromD<Rebind<double, DF32>> v) {
  const Twice<DF32> dt_f32;

  __vector float raw_f32_in_even;
  __asm__("vledb %0,%1,0,3" : "=v"(raw_f32_in_even) : "v"(v.raw));

  const VFromD<decltype(dt_f32)> f32_in_even{raw_f32_in_even};
  return LowerHalf(df32, ConcatEven(dt_f32, f32_in_even, f32_in_even));
}

}  // namespace detail

template <class D, HWY_IF_V_SIZE_LE_D(D, 4), HWY_IF_F16_D(D)>
HWY_API VFromD<D> DemoteTo(D df16, VFromD<Rebind<double, D>> v) {
  const Rebind<float, decltype(df16)> df32;
  return DemoteTo(df16, detail::DemoteToF32WithRoundToOdd(df32, v));
}

#endif  // HWY_PPC_HAVE_9

#if HWY_PPC_HAVE_10 && HWY_HAS_BUILTIN(__builtin_vsx_xvcvspbf16)

#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

namespace detail {

// VsxXvcvspbf16 converts a F32 vector to a BF16 vector, bitcasted to an U32
// vector with the resulting BF16 bits in the lower 16 bits of each U32 lane
template <class D, HWY_IF_BF16_D(D)>
static HWY_INLINE VFromD<Rebind<uint32_t, D>> VsxXvcvspbf16(
    D dbf16, VFromD<Rebind<float, D>> v) {
  const Rebind<uint32_t, decltype(dbf16)> du32;
  const Repartition<uint8_t, decltype(du32)> du32_as_du8;

  using VU32 = __vector unsigned int;

  // Even though the __builtin_vsx_xvcvspbf16 builtin performs a F32 to BF16
  // conversion, the __builtin_vsx_xvcvspbf16 intrinsic expects a
  // __vector unsigned char argument (at least as of GCC 13 and Clang 17)
  return VFromD<Rebind<uint32_t, D>>{reinterpret_cast<VU32>(
      __builtin_vsx_xvcvspbf16(BitCast(du32_as_du8, v).raw))};
}

}  // namespace detail

template <class D, HWY_IF_BF16_D(D)>
HWY_API VFromD<D> DemoteTo(D dbf16, VFromD<Rebind<float, D>> v) {
  const RebindToUnsigned<decltype(dbf16)> du16;
  return BitCast(dbf16, TruncateTo(du16, detail::VsxXvcvspbf16(dbf16, v)));
}

#endif  // HWY_PPC_HAVE_10 && HWY_HAS_BUILTIN(__builtin_vsx_xvcvspbf16)

// Specializations for partial vectors because vec_packs sets lanes above 2*N.
template <class DN, typename V, HWY_IF_V_SIZE_LE_D(DN, 4), HWY_IF_SIGNED_D(DN),
          HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 8), HWY_IF_SIGNED_D(DN),
          HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const VFromD<decltype(dn_full)> v_full{vec_packs(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 16), HWY_IF_SIGNED_D(DN),
          HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN /*dn*/, V a, V b) {
  return VFromD<DN>{vec_packs(a.raw, b.raw)};
}

template <class DN, typename V, HWY_IF_V_SIZE_LE_D(DN, 4),
          HWY_IF_UNSIGNED_D(DN), HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 8), HWY_IF_UNSIGNED_D(DN),
          HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const VFromD<decltype(dn_full)> v_full{vec_packsu(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 16), HWY_IF_UNSIGNED_D(DN),
          HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN /*dn*/, V a, V b) {
  return VFromD<DN>{vec_packsu(a.raw, b.raw)};
}

template <class DN, typename V, HWY_IF_V_SIZE_LE_D(DN, 4),
          HWY_IF_UNSIGNED_D(DN), HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const DFromV<decltype(a)> d;
  const Twice<decltype(d)> dt;
  return DemoteTo(dn, Combine(dt, b, a));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 8), HWY_IF_UNSIGNED_D(DN),
          HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const Twice<decltype(dn)> dn_full;
  const Repartition<uint32_t, decltype(dn_full)> du32_full;

  const VFromD<decltype(dn_full)> v_full{vec_packs(a.raw, b.raw)};
  const auto vu32_full = BitCast(du32_full, v_full);
  return LowerHalf(
      BitCast(dn_full, ConcatEven(du32_full, vu32_full, vu32_full)));
}
template <class DN, typename V, HWY_IF_V_SIZE_D(DN, 16), HWY_IF_UNSIGNED_D(DN),
          HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_D(DN, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN /*dn*/, V a, V b) {
  return VFromD<DN>{vec_packs(a.raw, b.raw)};
}

#if HWY_PPC_HAVE_10 && HWY_HAS_BUILTIN(__builtin_vsx_xvcvspbf16)
template <class D, class V, HWY_IF_BF16_D(D), HWY_IF_F32(TFromV<V>),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_V(V) * 2)>
HWY_API VFromD<D> ReorderDemote2To(D dbf16, V a, V b) {
  const RebindToUnsigned<decltype(dbf16)> du16;
  const Half<decltype(dbf16)> dh_bf16;
  return BitCast(dbf16,
                 OrderedTruncate2To(du16, detail::VsxXvcvspbf16(dh_bf16, a),
                                    detail::VsxXvcvspbf16(dh_bf16, b)));
}
#endif

template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<D>), class V,
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<D>) * 2),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D d, V a, V b) {
  return ReorderDemote2To(d, a, b);
}

#if HWY_PPC_HAVE_10 && HWY_HAS_BUILTIN(__builtin_vsx_xvcvspbf16)
template <class D, HWY_IF_BF16_D(D), class V, HWY_IF_F32(TFromV<V>),
          HWY_IF_LANES_D(D, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<D> OrderedDemote2To(D d, V a, V b) {
  return ReorderDemote2To(d, a, b);
}
#endif

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API Vec32<float> DemoteTo(D /* tag */, Vec64<double> v) {
  return Vec32<float>{vec_floate(v.raw)};
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> DemoteTo(D d, Vec128<double> v) {
#if HWY_S390X_HAVE_Z14 || HWY_IS_LITTLE_ENDIAN
  const Vec128<float> f64_to_f32{vec_floate(v.raw)};
#else
  const Vec128<float> f64_to_f32{vec_floato(v.raw)};
#endif

#if HWY_S390X_HAVE_Z14
  const Twice<decltype(d)> dt;
  return LowerHalf(d, ConcatEven(dt, f64_to_f32, f64_to_f32));
#else
  const RebindToUnsigned<D> du;
  const Rebind<uint64_t, D> du64;
  return Vec64<float>{
      BitCast(d, TruncateTo(du, BitCast(du64, f64_to_f32))).raw};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I32_D(D)>
HWY_API Vec32<int32_t> DemoteTo(D di32, Vec64<double> v) {
#if HWY_S390X_HAVE_Z14
  const Rebind<int64_t, decltype(di32)> di64;
  return DemoteTo(di32, ConvertTo(di64, v));
#else
  (void)di32;
  return Vec32<int32_t>{vec_signede(detail::VsxF2INormalizeSrcVals(v).raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I32_D(D)>
HWY_API Vec64<int32_t> DemoteTo(D di32, Vec128<double> v) {
#if HWY_S390X_HAVE_Z14
  const Rebind<int64_t, decltype(di32)> di64;
  return DemoteTo(di32, ConvertTo(di64, v));
#else
  (void)di32;

#if HWY_IS_LITTLE_ENDIAN
  const Vec128<int32_t> f64_to_i32{
      vec_signede(detail::VsxF2INormalizeSrcVals(v).raw)};
#else
  const Vec128<int32_t> f64_to_i32{
      vec_signedo(detail::VsxF2INormalizeSrcVals(v).raw)};
#endif

  const Rebind<int64_t, D> di64;
  const Vec128<int64_t> vi64 = BitCast(di64, f64_to_i32);
  return Vec64<int32_t>{vec_pack(vi64.raw, vi64.raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U32_D(D)>
HWY_API Vec32<uint32_t> DemoteTo(D du32, Vec64<double> v) {
#if HWY_S390X_HAVE_Z14
  const Rebind<uint64_t, decltype(du32)> du64;
  return DemoteTo(du32, ConvertTo(du64, v));
#else
  (void)du32;
  return Vec32<uint32_t>{vec_unsignede(detail::VsxF2INormalizeSrcVals(v).raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U32_D(D)>
HWY_API Vec64<uint32_t> DemoteTo(D du32, Vec128<double> v) {
#if HWY_S390X_HAVE_Z14
  const Rebind<uint64_t, decltype(du32)> du64;
  return DemoteTo(du32, ConvertTo(du64, v));
#else
  (void)du32;
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<uint32_t> f64_to_u32{
      vec_unsignede(detail::VsxF2INormalizeSrcVals(v).raw)};
#else
  const Vec128<uint32_t> f64_to_u32{
      vec_unsignedo(detail::VsxF2INormalizeSrcVals(v).raw)};
#endif

  const Rebind<uint64_t, D> du64;
  const Vec128<uint64_t> vu64 = BitCast(du64, f64_to_u32);
  return Vec64<uint32_t>{vec_pack(vu64.raw, vu64.raw)};
#endif
}

#if HWY_S390X_HAVE_Z14
namespace detail {

template <class V, HWY_IF_I64(TFromV<V>)>
HWY_INLINE VFromD<RebindToFloat<DFromV<V>>> ConvToF64WithRoundToOdd(V v) {
  __vector double raw_result;
  // Use inline assembly to do a round-to-odd I64->F64 conversion on Z14
  __asm__("vcdgb %0,%1,0,3" : "=v"(raw_result) : "v"(v.raw));
  return VFromD<RebindToFloat<DFromV<V>>>{raw_result};
}

template <class V, HWY_IF_U64(TFromV<V>)>
HWY_INLINE VFromD<RebindToFloat<DFromV<V>>> ConvToF64WithRoundToOdd(V v) {
  __vector double raw_result;
  // Use inline assembly to do a round-to-odd U64->F64 conversion on Z14
  __asm__("vcdlgb %0,%1,0,3" : "=v"(raw_result) : "v"(v.raw));
  return VFromD<RebindToFloat<DFromV<V>>>{raw_result};
}

}  // namespace detail
#endif  // HWY_S390X_HAVE_Z14

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API Vec32<float> DemoteTo(D df32, Vec64<int64_t> v) {
#if HWY_S390X_HAVE_Z14
  return DemoteTo(df32, detail::ConvToF64WithRoundToOdd(v));
#else  // VSX
  (void)df32;
  return Vec32<float>{vec_floate(v.raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> DemoteTo(D df32, Vec128<int64_t> v) {
#if HWY_S390X_HAVE_Z14
  return DemoteTo(df32, detail::ConvToF64WithRoundToOdd(v));
#else  // VSX
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<float> i64_to_f32{vec_floate(v.raw)};
#else
  const Vec128<float> i64_to_f32{vec_floato(v.raw)};
#endif

  const RebindToUnsigned<decltype(df32)> du32;
  const Rebind<uint64_t, decltype(df32)> du64;
  return Vec64<float>{
      BitCast(df32, TruncateTo(du32, BitCast(du64, i64_to_f32))).raw};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_F32_D(D)>
HWY_API Vec32<float> DemoteTo(D df32, Vec64<uint64_t> v) {
#if HWY_S390X_HAVE_Z14
  return DemoteTo(df32, detail::ConvToF64WithRoundToOdd(v));
#else  // VSX
  (void)df32;
  return Vec32<float>{vec_floate(v.raw)};
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_F32_D(D)>
HWY_API Vec64<float> DemoteTo(D df32, Vec128<uint64_t> v) {
#if HWY_S390X_HAVE_Z14
  return DemoteTo(df32, detail::ConvToF64WithRoundToOdd(v));
#else  // VSX
#if HWY_IS_LITTLE_ENDIAN
  const Vec128<float> u64_to_f32{vec_floate(v.raw)};
#else
  const Vec128<float> u64_to_f32{vec_floato(v.raw)};
#endif

  const RebindToUnsigned<decltype(df32)> du;
  const Rebind<uint64_t, decltype(df32)> du64;
  return Vec64<float>{
      BitCast(df32, TruncateTo(du, BitCast(du64, u64_to_f32))).raw};
#endif
}

// For already range-limited input [0, 255].
template <size_t N>
HWY_API Vec128<uint8_t, N> U8FromU32(Vec128<uint32_t, N> v) {
  const Rebind<uint16_t, DFromV<decltype(v)>> du16;
  const Rebind<uint8_t, decltype(du16)> du8;
  return TruncateTo(du8, TruncateTo(du16, v));
}
// ------------------------------ Integer <=> fp (ShiftRight, OddEven)

// Note: altivec.h vec_ct* currently contain C casts which triggers
// -Wdeprecate-lax-vec-conv-all warnings, so disable them.

#if HWY_S390X_HAVE_Z14 && !HWY_S390X_HAVE_Z15
template <class D, typename FromT, HWY_IF_F32_D(D), HWY_IF_UI32(FromT),
          HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConvertTo(D df32,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  const Rebind<double, decltype(df32)> df64;
  return DemoteTo(df32, PromoteTo(df64, v));
}
template <class D, typename FromT, HWY_IF_F32_D(D), HWY_IF_UI32(FromT),
          HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConvertTo(D df32, Vec128<FromT> v) {
  const RepartitionToWide<decltype(df32)> df64;

  const VFromD<D> vf32_lo{vec_floate(PromoteLowerTo(df64, v).raw)};
  const VFromD<D> vf32_hi{vec_floate(PromoteUpperTo(df64, v).raw)};
  return ConcatEven(df32, vf32_hi, vf32_lo);
}
#else  // Z15 or PPC
template <class D, typename FromT, HWY_IF_F32_D(D), HWY_IF_UI32(FromT)>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS_OFF(disable : 5219, ignored "-Wdeprecate-lax-vec-conv-all")
#endif
#if HWY_S390X_HAVE_Z15
  return VFromD<D>{vec_float(v.raw)};
#else
  return VFromD<D>{vec_ctf(v.raw, 0)};
#endif
  HWY_DIAGNOSTICS(pop)
}
#endif  // HWY_TARGET == HWY_Z14

template <class D, typename FromT, HWY_IF_F64_D(D), HWY_IF_NOT_FLOAT(FromT),
          HWY_IF_T_SIZE_D(D, sizeof(FromT))>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<FromT, Rebind<FromT, D>().MaxLanes()> v) {
  return VFromD<D>{vec_double(v.raw)};
}

// Truncates (rounds toward zero).
#if HWY_S390X_HAVE_Z14 && !HWY_S390X_HAVE_Z15
template <class D, HWY_IF_I32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConvertTo(D di32,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
  const Rebind<int64_t, decltype(di32)> di64;
  return DemoteTo(di32, PromoteTo(di64, v));
}
template <class D, HWY_IF_I32_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConvertTo(D di32,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
  const RepartitionToWide<decltype(di32)> di64;
  return OrderedDemote2To(di32, PromoteLowerTo(di64, v),
                          PromoteUpperTo(di64, v));
}
#else  // Z15 or PPC
template <class D, HWY_IF_I32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
#if defined(__OPTIMIZE__)
  if (detail::IsConstantRawAltivecVect(v.raw)) {
    constexpr int32_t kMinI32 = LimitsMin<int32_t>();
    constexpr int32_t kMaxI32 = LimitsMax<int32_t>();
    return Dup128VecFromValues(
        D(),
        (v.raw[0] >= -2147483648.0f)
            ? ((v.raw[0] < 2147483648.0f) ? static_cast<int32_t>(v.raw[0])
                                          : kMaxI32)
            : ((v.raw[0] < 0) ? kMinI32 : 0),
        (v.raw[1] >= -2147483648.0f)
            ? ((v.raw[1] < 2147483648.0f) ? static_cast<int32_t>(v.raw[1])
                                          : kMaxI32)
            : ((v.raw[1] < 0) ? kMinI32 : 0),
        (v.raw[2] >= -2147483648.0f)
            ? ((v.raw[2] < 2147483648.0f) ? static_cast<int32_t>(v.raw[2])
                                          : kMaxI32)
            : ((v.raw[2] < 0) ? kMinI32 : 0),
        (v.raw[3] >= -2147483648.0f)
            ? ((v.raw[3] < 2147483648.0f) ? static_cast<int32_t>(v.raw[3])
                                          : kMaxI32)
            : ((v.raw[3] < 0) ? kMinI32 : 0));
  }
#endif

#if HWY_S390X_HAVE_Z15
  // Use inline assembly on Z15 to avoid undefined behavior if v[i] is not in
  // the range of an int32_t
  __vector signed int raw_result;
  __asm__("vcfeb %0,%1,0,5" : "=v"(raw_result) : "v"(v.raw));
  return VFromD<D>{raw_result};
#else
  HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS_OFF(disable : 5219, ignored "-Wdeprecate-lax-vec-conv-all")
#endif
  return VFromD<D>{vec_cts(v.raw, 0)};
  HWY_DIAGNOSTICS(pop)
#endif  // HWY_S390X_HAVE_Z15
}
#endif  // HWY_S390X_HAVE_Z14 && !HWY_S390X_HAVE_Z15

template <class D, HWY_IF_I64_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<double, Rebind<double, D>().MaxLanes()> v) {
#if defined(__OPTIMIZE__) && (!HWY_COMPILER_CLANG || !HWY_S390X_HAVE_Z14)
  if (detail::IsConstantRawAltivecVect(v.raw)) {
    constexpr int64_t kMinI64 = LimitsMin<int64_t>();
    constexpr int64_t kMaxI64 = LimitsMax<int64_t>();
    return Dup128VecFromValues(D(),
                               (v.raw[0] >= -9223372036854775808.0)
                                   ? ((v.raw[0] < 9223372036854775808.0)
                                          ? static_cast<int64_t>(v.raw[0])
                                          : kMaxI64)
                                   : ((v.raw[0] < 0) ? kMinI64 : 0LL),
                               (v.raw[1] >= -9223372036854775808.0)
                                   ? ((v.raw[1] < 9223372036854775808.0)
                                          ? static_cast<int64_t>(v.raw[1])
                                          : kMaxI64)
                                   : ((v.raw[1] < 0) ? kMinI64 : 0LL));
  }
#endif

  // Use inline assembly to avoid undefined behavior if v[i] is not within the
  // range of an int64_t
  __vector signed long long raw_result;
#if HWY_S390X_HAVE_Z14
  __asm__("vcgdb %0,%1,0,5" : "=v"(raw_result) : "v"(v.raw));
#else
  __asm__("xvcvdpsxds %x0,%x1"
          : "=wa"(raw_result)
          : "wa"(detail::VsxF2INormalizeSrcVals(v).raw));
#endif
  return VFromD<D>{raw_result};
}

#if HWY_S390X_HAVE_Z14 && !HWY_S390X_HAVE_Z15
template <class D, HWY_IF_U32_D(D), HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API VFromD<D> ConvertTo(D du32,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
  const Rebind<uint64_t, decltype(du32)> du64;
  return DemoteTo(du32, PromoteTo(du64, v));
}
template <class D, HWY_IF_U32_D(D), HWY_IF_V_SIZE_D(D, 16)>
HWY_API VFromD<D> ConvertTo(D du32,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
  const RepartitionToWide<decltype(du32)> du64;
  return OrderedDemote2To(du32, PromoteLowerTo(du64, v),
                          PromoteUpperTo(du64, v));
}
#else  // Z15 or VSX
template <class D, HWY_IF_U32_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<float, Rebind<float, D>().MaxLanes()> v) {
#if defined(__OPTIMIZE__)
  if (detail::IsConstantRawAltivecVect(v.raw)) {
    constexpr uint32_t kMaxU32 = LimitsMax<uint32_t>();
    return Dup128VecFromValues(
        D(),
        (v.raw[0] >= 0.0f)
            ? ((v.raw[0] < 4294967296.0f) ? static_cast<uint32_t>(v.raw[0])
                                          : kMaxU32)
            : 0,
        (v.raw[1] >= 0.0f)
            ? ((v.raw[1] < 4294967296.0f) ? static_cast<uint32_t>(v.raw[1])
                                          : kMaxU32)
            : 0,
        (v.raw[2] >= 0.0f)
            ? ((v.raw[2] < 4294967296.0f) ? static_cast<uint32_t>(v.raw[2])
                                          : kMaxU32)
            : 0,
        (v.raw[3] >= 0.0f)
            ? ((v.raw[3] < 4294967296.0f) ? static_cast<uint32_t>(v.raw[3])
                                          : kMaxU32)
            : 0);
  }
#endif

#if HWY_S390X_HAVE_Z15
  // Use inline assembly on Z15 to avoid undefined behavior if v[i] is not in
  // the range of an uint32_t
  __vector unsigned int raw_result;
  __asm__("vclfeb %0,%1,0,5" : "=v"(raw_result) : "v"(v.raw));
  return VFromD<D>{raw_result};
#else  // VSX
  HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS_OFF(disable : 5219, ignored "-Wdeprecate-lax-vec-conv-all")
#endif
  VFromD<D> result{vec_ctu(v.raw, 0)};
  HWY_DIAGNOSTICS(pop)
  return result;
#endif  // HWY_S390X_HAVE_Z15
}
#endif  // HWY_S390X_HAVE_Z14 && !HWY_S390X_HAVE_Z15

template <class D, HWY_IF_U64_D(D)>
HWY_API VFromD<D> ConvertTo(D /* tag */,
                            Vec128<double, Rebind<double, D>().MaxLanes()> v) {
  HWY_DIAGNOSTICS(push)
#if HWY_COMPILER_CLANG
  HWY_DIAGNOSTICS_OFF(disable : 5219, ignored "-Wdeprecate-lax-vec-conv-all")
#endif

#if defined(__OPTIMIZE__) && (!HWY_COMPILER_CLANG || !HWY_S390X_HAVE_Z14)
  if (detail::IsConstantRawAltivecVect(v.raw)) {
    constexpr uint64_t kMaxU64 = LimitsMax<uint64_t>();
    return Dup128VecFromValues(
        D(),
        (v.raw[0] >= 0.0) ? ((v.raw[0] < 18446744073709551616.0)
                                 ? static_cast<uint64_t>(v.raw[0])
                                 : kMaxU64)
                          : 0,
        (v.raw[1] >= 0.0) ? ((v.raw[1] < 18446744073709551616.0)
                                 ? static_cast<uint64_t>(v.raw[1])
                                 : kMaxU64)
                          : 0);
  }
#endif

  // Use inline assembly to avoid undefined behavior if v[i] is not within the
  // range of an uint64_t
  __vector unsigned long long raw_result;
#if HWY_S390X_HAVE_Z14
  __asm__("vclgdb %0,%1,0,5" : "=v"(raw_result) : "v"(v.raw));
#else  // VSX
  __asm__("xvcvdpuxds %x0,%x1"
          : "=wa"(raw_result)
          : "wa"(detail::VsxF2INormalizeSrcVals(v).raw));
#endif
  return VFromD<D>{raw_result};
}

// ------------------------------ Floating-point rounding (ConvertTo)

// Toward nearest integer, ties to even
template <size_t N>
HWY_API Vec128<float, N> Round(Vec128<float, N> v) {
  return Vec128<float, N>{vec_round(v.raw)};
}

template <size_t N>
HWY_API Vec128<double, N> Round(Vec128<double, N> v) {
#if HWY_S390X_HAVE_Z14
  return Vec128<double, N>{vec_round(v.raw)};
#else
  return Vec128<double, N>{vec_rint(v.raw)};
#endif
}

template <typename T, size_t N, HWY_IF_FLOAT3264(T)>
HWY_API Vec128<MakeSigned<T>, N> NearestInt(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  return ConvertTo(di, Round(v));
}

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 di32,
                                        VFromD<Rebind<double, DI32>> v) {
  return DemoteTo(di32, Round(v));
}

// Toward zero, aka truncate
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Trunc(Vec128<T, N> v) {
  return Vec128<T, N>{vec_trunc(v.raw)};
}

// Toward +infinity, aka ceiling
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Ceil(Vec128<T, N> v) {
  return Vec128<T, N>{vec_ceil(v.raw)};
}

// Toward -infinity, aka floor
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> Floor(Vec128<T, N> v) {
  return Vec128<T, N>{vec_floor(v.raw)};
}

// ------------------------------ Floating-point classification

template <typename T, size_t N>
HWY_API Mask128<T, N> IsNaN(Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  return v != v;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> IsInf(Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  using TU = MakeUnsigned<T>;
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, check for exponent=max and mantissa=0.
  return RebindMask(
      d,
      Eq(Add(vu, vu), Set(du, static_cast<TU>(hwy::MaxExponentTimes2<T>()))));
}

// Returns whether normal/subnormal/zero.
template <typename T, size_t N>
HWY_API Mask128<T, N> IsFinite(Vec128<T, N> v) {
  static_assert(IsFloat<T>(), "Only for float");
  using TU = MakeUnsigned<T>;
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const VFromD<decltype(du)> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, check for exponent<max.
  return RebindMask(
      d,
      Lt(Add(vu, vu), Set(du, static_cast<TU>(hwy::MaxExponentTimes2<T>()))));
}

// ================================================== CRYPTO

#if !HWY_S390X_HAVE_Z14 && !defined(HWY_DISABLE_PPC8_CRYPTO)

// Per-target flag to prevent generic_ops-inl.h from defining AESRound.
#ifdef HWY_NATIVE_AES
#undef HWY_NATIVE_AES
#else
#define HWY_NATIVE_AES
#endif

namespace detail {
#if HWY_COMPILER_CLANG && HWY_COMPILER_CLANG < 1600
using CipherTag = Full128<uint64_t>;
#else
using CipherTag = Full128<uint8_t>;
#endif  // !HWY_COMPILER_CLANG
using CipherVec = VFromD<CipherTag>;
}  // namespace detail

HWY_API Vec128<uint8_t> AESRound(Vec128<uint8_t> state,
                                 Vec128<uint8_t> round_key) {
  const detail::CipherTag dc;
  const Full128<uint8_t> du8;
#if HWY_IS_LITTLE_ENDIAN
  return Reverse(du8,
                 BitCast(du8, detail::CipherVec{vec_cipher_be(
                                  BitCast(dc, Reverse(du8, state)).raw,
                                  BitCast(dc, Reverse(du8, round_key)).raw)}));
#else
  return BitCast(du8, detail::CipherVec{vec_cipher_be(
                          BitCast(dc, state).raw, BitCast(dc, round_key).raw)});
#endif
}

HWY_API Vec128<uint8_t> AESLastRound(Vec128<uint8_t> state,
                                     Vec128<uint8_t> round_key) {
  const detail::CipherTag dc;
  const Full128<uint8_t> du8;
#if HWY_IS_LITTLE_ENDIAN
  return Reverse(du8,
                 BitCast(du8, detail::CipherVec{vec_cipherlast_be(
                                  BitCast(dc, Reverse(du8, state)).raw,
                                  BitCast(dc, Reverse(du8, round_key)).raw)}));
#else
  return BitCast(du8, detail::CipherVec{vec_cipherlast_be(
                          BitCast(dc, state).raw, BitCast(dc, round_key).raw)});
#endif
}

HWY_API Vec128<uint8_t> AESRoundInv(Vec128<uint8_t> state,
                                    Vec128<uint8_t> round_key) {
  const detail::CipherTag dc;
  const Full128<uint8_t> du8;
#if HWY_IS_LITTLE_ENDIAN
  return Xor(Reverse(du8, BitCast(du8, detail::CipherVec{vec_ncipher_be(
                                           BitCast(dc, Reverse(du8, state)).raw,
                                           Zero(dc).raw)})),
             round_key);
#else
  return Xor(BitCast(du8, detail::CipherVec{vec_ncipher_be(
                              BitCast(dc, state).raw, Zero(dc).raw)}),
             round_key);
#endif
}

HWY_API Vec128<uint8_t> AESLastRoundInv(Vec128<uint8_t> state,
                                        Vec128<uint8_t> round_key) {
  const detail::CipherTag dc;
  const Full128<uint8_t> du8;
#if HWY_IS_LITTLE_ENDIAN
  return Reverse(du8,
                 BitCast(du8, detail::CipherVec{vec_ncipherlast_be(
                                  BitCast(dc, Reverse(du8, state)).raw,
                                  BitCast(dc, Reverse(du8, round_key)).raw)}));
#else
  return BitCast(du8, detail::CipherVec{vec_ncipherlast_be(
                          BitCast(dc, state).raw, BitCast(dc, round_key).raw)});
#endif
}

HWY_API Vec128<uint8_t> AESInvMixColumns(Vec128<uint8_t> state) {
  const Full128<uint8_t> du8;
  const auto zero = Zero(du8);

  // PPC8/PPC9/PPC10 does not have a single instruction for the AES
  // InvMixColumns operation like ARM Crypto, SVE2 Crypto, or AES-NI do.

  // The AESInvMixColumns operation can be carried out on PPC8/PPC9/PPC10
  // by doing an AESLastRound operation with a zero round_key followed by an
  // AESRoundInv operation with a zero round_key.
  return AESRoundInv(AESLastRound(state, zero), zero);
}

template <uint8_t kRcon>
HWY_API Vec128<uint8_t> AESKeyGenAssist(Vec128<uint8_t> v) {
  constexpr __vector unsigned char kRconXorMask = {0, 0, 0, 0, kRcon, 0, 0, 0,
                                                   0, 0, 0, 0, kRcon, 0, 0, 0};
  constexpr __vector unsigned char kRotWordShuffle = {
      4, 5, 6, 7, 5, 6, 7, 4, 12, 13, 14, 15, 13, 14, 15, 12};
  const detail::CipherTag dc;
  const Full128<uint8_t> du8;
  const auto sub_word_result =
      BitCast(du8, detail::CipherVec{vec_sbox_be(BitCast(dc, v).raw)});
  const auto rot_word_result =
      TableLookupBytes(sub_word_result, Vec128<uint8_t>{kRotWordShuffle});
  return Xor(rot_word_result, Vec128<uint8_t>{kRconXorMask});
}

template <size_t N>
HWY_API Vec128<uint64_t, N> CLMulLower(Vec128<uint64_t, N> a,
                                       Vec128<uint64_t, N> b) {
  // NOTE: Lane 1 of both a and b need to be zeroed out for the
  // vec_pmsum_be operation below as the vec_pmsum_be operation
  // does a carryless multiplication of each 64-bit half and then
  // adds the two halves using an bitwise XOR operation.

  const DFromV<decltype(a)> d;
  const auto zero = Zero(d);

  using VU64 = __vector unsigned long long;
  const VU64 pmsum_result = reinterpret_cast<VU64>(
      vec_pmsum_be(InterleaveLower(a, zero).raw, InterleaveLower(b, zero).raw));

#if HWY_IS_LITTLE_ENDIAN
  return Vec128<uint64_t, N>{pmsum_result};
#else
  // Need to swap the two halves of pmsum_result on big-endian targets as
  // the upper 64 bits of the carryless multiplication result are in lane 0 of
  // pmsum_result and the lower 64 bits of the carryless multiplication result
  // are in lane 1 of mul128_result
  return Vec128<uint64_t, N>{vec_sld(pmsum_result, pmsum_result, 8)};
#endif
}

template <size_t N>
HWY_API Vec128<uint64_t, N> CLMulUpper(Vec128<uint64_t, N> a,
                                       Vec128<uint64_t, N> b) {
  // NOTE: Lane 0 of both a and b need to be zeroed out for the
  // vec_pmsum_be operation below as the vec_pmsum_be operation
  // does a carryless multiplication of each 64-bit half and then
  // adds the two halves using an bitwise XOR operation.

  const DFromV<decltype(a)> d;
  const auto zero = Zero(d);

  using VU64 = __vector unsigned long long;
  const VU64 pmsum_result = reinterpret_cast<VU64>(
      vec_pmsum_be(vec_mergel(zero.raw, a.raw), vec_mergel(zero.raw, b.raw)));

#if HWY_IS_LITTLE_ENDIAN
  return Vec128<uint64_t, N>{pmsum_result};
#else
  // Need to swap the two halves of pmsum_result on big-endian targets as
  // the upper 64 bits of the carryless multiplication result are in lane 0 of
  // pmsum_result and the lower 64 bits of the carryless multiplication result
  // are in lane 1 of mul128_result
  return Vec128<uint64_t, N>{vec_sld(pmsum_result, pmsum_result, 8)};
#endif
}

#endif  // !defined(HWY_DISABLE_PPC8_CRYPTO)

// ================================================== MISC

// ------------------------------ LoadMaskBits (TestBit)

namespace detail {

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE MFromD<D> LoadMaskBits128(D /*d*/, uint64_t mask_bits) {
#if HWY_PPC_HAVE_10
  const Vec128<uint8_t> mask_vec{vec_genbm(mask_bits)};

#if HWY_IS_LITTLE_ENDIAN
  return MFromD<D>{MaskFromVec(mask_vec).raw};
#else
  return MFromD<D>{MaskFromVec(Reverse(Full128<uint8_t>(), mask_vec)).raw};
#endif  // HWY_IS_LITTLE_ENDIAN

#else  // PPC9 or earlier
  const Full128<uint8_t> du8;
  const Full128<uint16_t> du16;
  const Vec128<uint8_t> vbits =
      BitCast(du8, Set(du16, static_cast<uint16_t>(mask_bits)));

  // Replicate bytes 8x such that each byte contains the bit that governs it.
#if HWY_IS_LITTLE_ENDIAN
  const __vector unsigned char kRep8 = {0, 0, 0, 0, 0, 0, 0, 0,
                                        1, 1, 1, 1, 1, 1, 1, 1};
#else
  const __vector unsigned char kRep8 = {1, 1, 1, 1, 1, 1, 1, 1,
                                        0, 0, 0, 0, 0, 0, 0, 0};
#endif  // HWY_IS_LITTLE_ENDIAN

  const Vec128<uint8_t> rep8{vec_perm(vbits.raw, vbits.raw, kRep8)};
  const __vector unsigned char kBit = {1, 2, 4, 8, 16, 32, 64, 128,
                                       1, 2, 4, 8, 16, 32, 64, 128};
  return MFromD<D>{TestBit(rep8, Vec128<uint8_t>{kBit}).raw};
#endif  // HWY_PPC_HAVE_10
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE MFromD<D> LoadMaskBits128(D /*d*/, uint64_t mask_bits) {
#if HWY_PPC_HAVE_10
  const Vec128<uint16_t> mask_vec{vec_genhm(mask_bits)};

#if HWY_IS_LITTLE_ENDIAN
  return MFromD<D>{MaskFromVec(mask_vec).raw};
#else
  return MFromD<D>{MaskFromVec(Reverse(Full128<uint16_t>(), mask_vec)).raw};
#endif  // HWY_IS_LITTLE_ENDIAN

#else   // PPC9 or earlier
  const __vector unsigned short kBit = {1, 2, 4, 8, 16, 32, 64, 128};
  const auto vmask_bits =
      Set(Full128<uint16_t>(), static_cast<uint16_t>(mask_bits));
  return MFromD<D>{TestBit(vmask_bits, Vec128<uint16_t>{kBit}).raw};
#endif  // HWY_PPC_HAVE_10
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE MFromD<D> LoadMaskBits128(D /*d*/, uint64_t mask_bits) {
#if HWY_PPC_HAVE_10
  const Vec128<uint32_t> mask_vec{vec_genwm(mask_bits)};

#if HWY_IS_LITTLE_ENDIAN
  return MFromD<D>{MaskFromVec(mask_vec).raw};
#else
  return MFromD<D>{MaskFromVec(Reverse(Full128<uint32_t>(), mask_vec)).raw};
#endif  // HWY_IS_LITTLE_ENDIAN

#else   // PPC9 or earlier
  const __vector unsigned int kBit = {1, 2, 4, 8};
  const auto vmask_bits =
      Set(Full128<uint32_t>(), static_cast<uint32_t>(mask_bits));
  return MFromD<D>{TestBit(vmask_bits, Vec128<uint32_t>{kBit}).raw};
#endif  // HWY_PPC_HAVE_10
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_INLINE MFromD<D> LoadMaskBits128(D /*d*/, uint64_t mask_bits) {
#if HWY_PPC_HAVE_10
  const Vec128<uint64_t> mask_vec{vec_gendm(mask_bits)};

#if HWY_IS_LITTLE_ENDIAN
  return MFromD<D>{MaskFromVec(mask_vec).raw};
#else
  return MFromD<D>{MaskFromVec(Reverse(Full128<uint64_t>(), mask_vec)).raw};
#endif  // HWY_IS_LITTLE_ENDIAN

#else   // PPC9 or earlier
  const __vector unsigned long long kBit = {1, 2};
  const auto vmask_bits =
      Set(Full128<uint64_t>(), static_cast<uint64_t>(mask_bits));
  return MFromD<D>{TestBit(vmask_bits, Vec128<uint64_t>{kBit}).raw};
#endif  // HWY_PPC_HAVE_10
}

}  // namespace detail

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_LANES_LE_D(D, 8)>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  // If there are 8 or fewer lanes, simply convert bits[0] to a uint64_t
  uint64_t mask_bits = bits[0];

  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;

  return detail::LoadMaskBits128(d, mask_bits);
}

template <class D, HWY_IF_LANES_D(D, 16)>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  // First, copy the mask bits to a uint16_t as there as there are at most
  // 16 lanes in a vector.

  // Copying the mask bits to a uint16_t first will also ensure that the
  // mask bits are loaded into the lower 16 bits on big-endian PPC targets.
  uint16_t u16_mask_bits;
  CopyBytes<sizeof(uint16_t)>(bits, &u16_mask_bits);

#if HWY_IS_LITTLE_ENDIAN
  return detail::LoadMaskBits128(d, u16_mask_bits);
#else
  // On big-endian targets, u16_mask_bits need to be byte swapped as bits
  // contains the mask bits in little-endian byte order

  // GCC/Clang will optimize the load of u16_mask_bits and byte swap to a
  // single lhbrx instruction on big-endian PPC targets when optimizations
  // are enabled.
#if HWY_HAS_BUILTIN(__builtin_bswap16)
  return detail::LoadMaskBits128(d, __builtin_bswap16(u16_mask_bits));
#else
  return detail::LoadMaskBits128(
      d, static_cast<uint16_t>((u16_mask_bits << 8) | (u16_mask_bits >> 8)));
#endif
#endif
}

template <typename T>
struct CompressIsPartition {
  // generic_ops-inl does not guarantee IsPartition for 8-bit.
  enum { value = (sizeof(T) != 1) };
};

// ------------------------------ Dup128MaskFromMaskBits

template <class D>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) mask_bits &= (1u << kN) - 1;
  return detail::LoadMaskBits128(d, mask_bits);
}

// ------------------------------ StoreMaskBits

namespace detail {

#if !HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN
// fallback for missing vec_extractm
template <size_t N>
HWY_INLINE uint64_t ExtractSignBits(Vec128<uint8_t, N> sign_bits,
                                    __vector unsigned char bit_shuffle) {
  // clang POWER8 and 9 targets appear to differ in their return type of
  // vec_vbpermq: unsigned or signed, so cast to avoid a warning.
  using VU64 = detail::Raw128<uint64_t>::type;
#if HWY_S390X_HAVE_Z14
  const Vec128<uint64_t> extracted{
      reinterpret_cast<VU64>(vec_bperm_u128(sign_bits.raw, bit_shuffle))};
#else
  const Vec128<uint64_t> extracted{
      reinterpret_cast<VU64>(vec_vbpermq(sign_bits.raw, bit_shuffle))};
#endif
  return extracted.raw[HWY_IS_LITTLE_ENDIAN];
}

#endif  // !HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<1> /*tag*/, Mask128<T, N> mask) {
  const DFromM<decltype(mask)> d;
  const Repartition<uint8_t, decltype(d)> du8;
  const VFromD<decltype(du8)> sign_bits = BitCast(du8, VecFromMask(d, mask));

#if HWY_PPC_HAVE_10 && HWY_IS_LITTLE_ENDIAN
  return static_cast<uint64_t>(vec_extractm(sign_bits.raw));
#else   // Z14, Z15, PPC8, PPC9, or big-endian PPC10
  const __vector unsigned char kBitShuffle = {120, 112, 104, 96, 88, 80, 72, 64,
                                              56,  48,  40,  32, 24, 16, 8,  0};
  return ExtractSignBits(sign_bits, kBitShuffle);
#endif  // HWY_PPC_HAVE_10 && HWY_IS_LITTLE_ENDIAN
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<2> /*tag*/, Mask128<T, N> mask) {
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;

  const Repartition<uint8_t, decltype(d)> du8;
  const VFromD<decltype(du8)> sign_bits = BitCast(du8, VecFromMask(d, mask));

#if HWY_PPC_HAVE_10 && HWY_IS_LITTLE_ENDIAN
  return static_cast<uint64_t>(vec_extractm(BitCast(du, sign_bits).raw));
#else  // Z14, Z15, PPC8, PPC9, or big-endian PPC10
  (void)du;
#if HWY_IS_LITTLE_ENDIAN
  const __vector unsigned char kBitShuffle = {
      112, 96, 80, 64, 48, 32, 16, 0, 128, 128, 128, 128, 128, 128, 128, 128};
#else
  const __vector unsigned char kBitShuffle = {
      128, 128, 128, 128, 128, 128, 128, 128, 112, 96, 80, 64, 48, 32, 16, 0};
#endif
  return ExtractSignBits(sign_bits, kBitShuffle);
#endif  // HWY_PPC_HAVE_10
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<4> /*tag*/, Mask128<T, N> mask) {
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;

  const Repartition<uint8_t, decltype(d)> du8;
  const VFromD<decltype(du8)> sign_bits = BitCast(du8, VecFromMask(d, mask));

#if HWY_PPC_HAVE_10 && HWY_IS_LITTLE_ENDIAN
  return static_cast<uint64_t>(vec_extractm(BitCast(du, sign_bits).raw));
#else  // Z14, Z15, PPC8, PPC9, or big-endian PPC10
  (void)du;
#if HWY_IS_LITTLE_ENDIAN
  const __vector unsigned char kBitShuffle = {96,  64,  32,  0,   128, 128,
                                              128, 128, 128, 128, 128, 128,
                                              128, 128, 128, 128};
#else
  const __vector unsigned char kBitShuffle = {128, 128, 128, 128, 128, 128,
                                              128, 128, 128, 128, 128, 128,
                                              96,  64,  32,  0};
#endif
  return ExtractSignBits(sign_bits, kBitShuffle);
#endif  // HWY_PPC_HAVE_10
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(hwy::SizeTag<8> /*tag*/, Mask128<T, N> mask) {
  const DFromM<decltype(mask)> d;
  const RebindToUnsigned<decltype(d)> du;

  const Repartition<uint8_t, decltype(d)> du8;
  const VFromD<decltype(du8)> sign_bits = BitCast(du8, VecFromMask(d, mask));

#if HWY_PPC_HAVE_10 && HWY_IS_LITTLE_ENDIAN
  return static_cast<uint64_t>(vec_extractm(BitCast(du, sign_bits).raw));
#else  // Z14, Z15, PPC8, PPC9, or big-endian PPC10
  (void)du;
#if HWY_IS_LITTLE_ENDIAN
  const __vector unsigned char kBitShuffle = {64,  0,   128, 128, 128, 128,
                                              128, 128, 128, 128, 128, 128,
                                              128, 128, 128, 128};
#else
  const __vector unsigned char kBitShuffle = {128, 128, 128, 128, 128, 128,
                                              128, 128, 128, 128, 128, 128,
                                              128, 128, 64,  0};
#endif
  return ExtractSignBits(sign_bits, kBitShuffle);
#endif  // HWY_PPC_HAVE_10
}

// Returns the lowest N of the mask bits.
template <typename T, size_t N>
constexpr uint64_t OnlyActive(uint64_t mask_bits) {
  return ((N * sizeof(T)) == 16) ? mask_bits : mask_bits & ((1ull << N) - 1);
}

template <typename T, size_t N>
HWY_INLINE uint64_t BitsFromMask(Mask128<T, N> mask) {
  return OnlyActive<T, N>(BitsFromMask(hwy::SizeTag<sizeof(T)>(), mask));
}

}  // namespace detail

// `p` points to at least 8 writable bytes.
template <class D, HWY_IF_LANES_LE_D(D, 8)>
HWY_API size_t StoreMaskBits(D /*d*/, MFromD<D> mask, uint8_t* bits) {
  // For vectors with 8 or fewer lanes, simply cast the result of BitsFromMask
  // to an uint8_t and store the result in bits[0].
  bits[0] = static_cast<uint8_t>(detail::BitsFromMask(mask));
  return sizeof(uint8_t);
}

template <class D, HWY_IF_LANES_D(D, 16)>
HWY_API size_t StoreMaskBits(D /*d*/, MFromD<D> mask, uint8_t* bits) {
  const auto mask_bits = detail::BitsFromMask(mask);

  // First convert mask_bits to a uint16_t as we only want to store
  // the lower 16 bits of mask_bits as there are 16 lanes in mask.

  // Converting mask_bits to a uint16_t first will also ensure that
  // the lower 16 bits of mask_bits are stored instead of the upper 16 bits
  // of mask_bits on big-endian PPC targets.
#if HWY_IS_LITTLE_ENDIAN
  const uint16_t u16_mask_bits = static_cast<uint16_t>(mask_bits);
#else
  // On big-endian targets, the bytes of mask_bits need to be swapped
  // as StoreMaskBits expects the mask bits to be stored in little-endian
  // byte order.

  // GCC will also optimize the byte swap and CopyBytes operations below
  // to a single sthbrx instruction when optimizations are enabled on
  // big-endian PPC targets
#if HWY_HAS_BUILTIN(__builtin_bswap16)
  const uint16_t u16_mask_bits =
      __builtin_bswap16(static_cast<uint16_t>(mask_bits));
#else
  const uint16_t u16_mask_bits = static_cast<uint16_t>(
      (mask_bits << 8) | (static_cast<uint16_t>(mask_bits) >> 8));
#endif
#endif

  CopyBytes<sizeof(uint16_t)>(&u16_mask_bits, bits);
  return sizeof(uint16_t);
}

// ------------------------------ Mask testing

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API bool AllFalse(D d, MFromD<D> mask) {
  const RebindToUnsigned<decltype(d)> du;
  return static_cast<bool>(
      vec_all_eq(VecFromMask(du, RebindMask(du, mask)).raw, Zero(du).raw));
}

template <class D, HWY_IF_V_SIZE_D(D, 16)>
HWY_API bool AllTrue(D d, MFromD<D> mask) {
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  return static_cast<bool>(vec_all_eq(VecFromMask(du, RebindMask(du, mask)).raw,
                                      Set(du, hwy::LimitsMax<TU>()).raw));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API bool AllFalse(D d, MFromD<D> mask) {
  const Full128<TFromD<D>> d_full;
  constexpr size_t kN = MaxLanes(d);
  return AllFalse(d_full,
                  And(MFromD<decltype(d_full)>{mask.raw}, FirstN(d_full, kN)));
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 8)>
HWY_API bool AllTrue(D d, MFromD<D> mask) {
  const Full128<TFromD<D>> d_full;
  constexpr size_t kN = MaxLanes(d);
  return AllTrue(
      d_full, Or(MFromD<decltype(d_full)>{mask.raw}, Not(FirstN(d_full, kN))));
}

template <class D>
HWY_API size_t CountTrue(D /* tag */, MFromD<D> mask) {
  return PopCount(detail::BitsFromMask(mask));
}

#if HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
namespace detail {

template <class V>
static HWY_INLINE size_t VsxCntlzLsbb(V v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1200 && \
    HWY_IS_LITTLE_ENDIAN
  // Use inline assembly to work around bug in GCC 11 and earlier on
  // little-endian PPC9
  int idx;
  __asm__("vctzlsbb %0,%1" : "=r"(idx) : "v"(v.raw));
  return static_cast<size_t>(idx);
#else
  return static_cast<size_t>(vec_cntlz_lsbb(v.raw));
#endif
}

template <class V>
static HWY_INLINE size_t VsxCnttzLsbb(V v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 1200 && \
    HWY_IS_LITTLE_ENDIAN
  // Use inline assembly to work around bug in GCC 11 and earlier on
  // little-endian PPC9
  int idx;
  __asm__("vclzlsbb %0,%1" : "=r"(idx) : "v"(v.raw));
  return static_cast<size_t>(idx);
#else
  return static_cast<size_t>(vec_cnttz_lsbb(v.raw));
#endif
}

}  // namespace detail
#endif

template <class D, typename T = TFromD<D>>
HWY_API size_t FindKnownFirstTrue(D d, MFromD<D> mask) {
// For little-endian PPC10, BitsFromMask is already efficient.
#if HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  if (detail::IsFull(d)) {
    const Repartition<uint8_t, D> d8;
    const auto bytes = BitCast(d8, VecFromMask(d, mask));
    return detail::VsxCntlzLsbb(bytes) / sizeof(T);
  }
#endif  // HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  (void)d;
  return Num0BitsBelowLS1Bit_Nonzero64(detail::BitsFromMask(mask));
}

template <class D, typename T = TFromD<D>>
HWY_API intptr_t FindFirstTrue(D d, MFromD<D> mask) {
// For little-endian PPC10, BitsFromMask is already efficient.
#if HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  constexpr size_t kN = 16 / sizeof(T);
  if (detail::IsFull(d)) {
    const Repartition<uint8_t, D> d8;
    const auto bytes = BitCast(d8, VecFromMask(d, mask));
    const size_t idx = detail::VsxCntlzLsbb(bytes) / sizeof(T);
    return idx == kN ? -1 : static_cast<intptr_t>(idx);
  }
#endif  // HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  (void)d;
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  return mask_bits ? intptr_t(Num0BitsBelowLS1Bit_Nonzero64(mask_bits)) : -1;
}

template <class D, typename T = TFromD<D>>
HWY_API size_t FindKnownLastTrue(D d, MFromD<D> mask) {
// For little-endian PPC10, BitsFromMask is already efficient.
#if HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  if (detail::IsFull(d)) {
    const Repartition<uint8_t, D> d8;
    const auto bytes = BitCast(d8, VecFromMask(d, mask));
    const size_t idx = detail::VsxCnttzLsbb(bytes) / sizeof(T);
    return 16 / sizeof(T) - 1 - idx;
  }
#endif  // HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  (void)d;
  return 63 - Num0BitsAboveMS1Bit_Nonzero64(detail::BitsFromMask(mask));
}

template <class D, typename T = TFromD<D>>
HWY_API intptr_t FindLastTrue(D d, MFromD<D> mask) {
// For little-endian PPC10, BitsFromMask is already efficient.
#if HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  constexpr size_t kN = 16 / sizeof(T);
  if (detail::IsFull(d)) {
    const Repartition<uint8_t, D> d8;
    const auto bytes = BitCast(d8, VecFromMask(d, mask));
    const size_t idx = detail::VsxCnttzLsbb(bytes) / sizeof(T);
    return idx == kN ? -1 : static_cast<intptr_t>(kN - 1 - idx);
  }
#endif  // HWY_PPC_HAVE_9 && (!HWY_PPC_HAVE_10 || HWY_IS_BIG_ENDIAN)
  (void)d;
  const uint64_t mask_bits = detail::BitsFromMask(mask);
  return mask_bits ? intptr_t(63 - Num0BitsAboveMS1Bit_Nonzero64(mask_bits))
                   : -1;
}

// ------------------------------ Compress, CompressBits

namespace detail {

#if HWY_PPC_HAVE_10
template <bool kIsCompress, class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_INLINE VFromD<D> CompressOrExpandIndicesFromMask(D d, MFromD<D> mask) {
  constexpr unsigned kGenPcvmMode =
      (kIsCompress ? 1u : 0u) | (HWY_IS_LITTLE_ENDIAN ? 2u : 0u);

  // Inline assembly is used instead of the vec_genpcvm intrinsic to work around
  // compiler bugs on little-endian PPC10
  typename detail::Raw128<TFromD<D>>::type idx;
  __asm__("xxgenpcvbm %x0, %1, %2"
          : "=wa"(idx)
          : "v"(mask.raw), "i"(kGenPcvmMode));
  return VFromD<decltype(d)>{idx};
}
template <bool kIsCompress, class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<D> CompressOrExpandIndicesFromMask(D d, MFromD<D> mask) {
  constexpr unsigned kGenPcvmMode =
      (kIsCompress ? 1u : 0u) | (HWY_IS_LITTLE_ENDIAN ? 2u : 0u);

  // Inline assembly is used instead of the vec_genpcvm intrinsic to work around
  // compiler bugs on little-endian PPC10
  typename detail::Raw128<TFromD<D>>::type idx;
  __asm__("xxgenpcvhm %x0, %1, %2"
          : "=wa"(idx)
          : "v"(mask.raw), "i"(kGenPcvmMode));
  return VFromD<decltype(d)>{idx};
}
template <bool kIsCompress, class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_INLINE VFromD<D> CompressOrExpandIndicesFromMask(D d, MFromD<D> mask) {
  constexpr unsigned kGenPcvmMode =
      (kIsCompress ? 1u : 0u) | (HWY_IS_LITTLE_ENDIAN ? 2u : 0u);

  // Inline assembly is used instead of the vec_genpcvm intrinsic to work around
  // compiler bugs on little-endian PPC10
  typename detail::Raw128<TFromD<D>>::type idx;
  __asm__("xxgenpcvwm %x0, %1, %2"
          : "=wa"(idx)
          : "v"(mask.raw), "i"(kGenPcvmMode));
  return VFromD<decltype(d)>{idx};
}
#endif

// Also works for N < 8 because the first 16 4-tuples only reference bytes 0-6.
template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<D> IndicesFromBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Rebind<uint8_t, decltype(d)> d8;
  const Twice<decltype(d8)> d8t;
  const RebindToUnsigned<decltype(d)> du;

  // To reduce cache footprint, store lane indices and convert to byte indices
  // (2*lane + 0..1), with the doubling baked into the table. It's not clear
  // that the additional cost of unpacking nibbles is worthwhile.
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
  constexpr uint16_t kPairIndexIncrement =
      HWY_IS_LITTLE_ENDIAN ? 0x0100 : 0x0001;

  return BitCast(d, pairs + Set(du, kPairIndexIncrement));
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_INLINE VFromD<D> IndicesFromNotBits128(D d, uint64_t mask_bits) {
  HWY_DASSERT(mask_bits < 256);
  const Rebind<uint8_t, decltype(d)> d8;
  const Twice<decltype(d8)> d8t;
  const RebindToUnsigned<decltype(d)> du;

  // To reduce cache footprint, store lane indices and convert to byte indices
  // (2*lane + 0..1), with the doubling baked into the table. It's not clear
  // that the additional cost of unpacking nibbles is worthwhile.
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
  constexpr uint16_t kPairIndexIncrement =
      HWY_IS_LITTLE_ENDIAN ? 0x0100 : 0x0001;

  return BitCast(d, pairs + Set(du, kPairIndexIncrement));
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
  const Full128<T> d;
  const Vec128<T> m = VecFromMask(d, mask);
  const Vec128<T> maskL = DupEven(m);
  const Vec128<T> maskH = DupOdd(m);
  const Vec128<T> swap = AndNot(maskL, maskH);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

#if HWY_PPC_HAVE_10
#ifdef HWY_NATIVE_COMPRESS8
#undef HWY_NATIVE_COMPRESS8
#else
#define HWY_NATIVE_COMPRESS8
#endif

// General case, 1 byte
template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> Compress(Vec128<T, N> v, Mask128<T, N> mask) {
  const DFromV<decltype(v)> d;
  return TableLookupBytes(
      v, detail::CompressOrExpandIndicesFromMask<true>(d, mask));
}
#endif

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
  const Full128<T> d;
  const Vec128<T> m = VecFromMask(d, mask);
  const Vec128<T> maskL = DupEven(m);
  const Vec128<T> maskH = DupOdd(m);
  const Vec128<T> swap = AndNot(maskH, maskL);
  return IfVecThenElse(swap, Shuffle01(v), v);
}

#if HWY_PPC_HAVE_10
// General case, 1 byte
template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressNot(Vec128<T, N> v, Mask128<T, N> mask) {
  const DFromV<decltype(v)> d;
  return TableLookupBytes(
      v, detail::CompressOrExpandIndicesFromMask<true>(d, Not(mask)));
}
#endif

// General case, 2 or 4 bytes
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

#if HWY_PPC_HAVE_10
template <typename T, size_t N, HWY_IF_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v,
                                  const uint8_t* HWY_RESTRICT bits) {
  const DFromV<decltype(v)> d;
  return Compress(v, LoadMaskBits(d, bits));
}
#endif

template <typename T, size_t N, HWY_IF_NOT_T_SIZE(T, 1)>
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v,
                                  const uint8_t* HWY_RESTRICT bits) {
  // As there are at most 8 lanes in v if sizeof(TFromD<D>) > 1, simply
  // convert bits[0] to a uint64_t
  uint64_t mask_bits = bits[0];
  if (N < 8) {
    mask_bits &= (1ull << N) - 1;
  }

  return detail::CompressBits(v, mask_bits);
}

// ------------------------------ CompressStore, CompressBitsStore

#if HWY_PPC_HAVE_10
template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(VFromD<D> v, MFromD<D> m, D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  const size_t count = CountTrue(d, m);
  const auto indices = detail::CompressOrExpandIndicesFromMask<true>(d, m);
  const auto compressed = TableLookupBytes(v, indices);
  StoreU(compressed, d, unaligned);
  return count;
}
#endif

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(VFromD<D> v, MFromD<D> m, D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  const uint64_t mask_bits = detail::BitsFromMask(m);
  HWY_DASSERT(mask_bits < (1ull << MaxLanes(d)));
  const size_t count = PopCount(mask_bits);

  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
  StoreU(compressed, d, unaligned);
  return count;
}

#if HWY_PPC_HAVE_10
template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const size_t count = CountTrue(d, m);
  const auto indices = detail::CompressOrExpandIndicesFromMask<true>(d, m);
  const auto compressed = TableLookupBytes(v, indices);
  StoreN(compressed, d, unaligned, count);
  return count;
}
#endif

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> m, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  const uint64_t mask_bits = detail::BitsFromMask(m);
  HWY_DASSERT(mask_bits < (1ull << MaxLanes(d)));
  const size_t count = PopCount(mask_bits);

  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
#if (HWY_PPC_HAVE_9 && HWY_ARCH_PPC_64) || HWY_S390X_HAVE_Z14
  StoreN(compressed, d, unaligned, count);
#else
  BlendedStore(compressed, FirstN(d, count), d, unaligned);
#endif
  return count;
}

#if HWY_PPC_HAVE_10
template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  return CompressStore(v, LoadMaskBits(d, bits), d, unaligned);
}
#endif

template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  const RebindToUnsigned<decltype(d)> du;

  // As there are at most 8 lanes in v if sizeof(TFromD<D>) > 1, simply
  // convert bits[0] to a uint64_t
  uint64_t mask_bits = bits[0];
  constexpr size_t kN = MaxLanes(d);
  if (kN < 8) {
    mask_bits &= (1ull << kN) - 1;
  }
  const size_t count = PopCount(mask_bits);

  const auto indices = BitCast(du, detail::IndicesFromBits128(d, mask_bits));
  const auto compressed = BitCast(d, TableLookupBytes(BitCast(du, v), indices));
  StoreU(compressed, d, unaligned);

  return count;
}

// ------------------------------ Expand
#if HWY_PPC_HAVE_10
#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

template <typename T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4))>
HWY_API Vec128<T, N> Expand(Vec128<T, N> v, Mask128<T, N> mask) {
  const DFromV<decltype(v)> d;
  const auto idx = detail::CompressOrExpandIndicesFromMask<false>(d, mask);
  return IfThenElseZero(mask, TableLookupBytes(v, idx));
}

template <typename T, HWY_IF_T_SIZE(T, 8)>
HWY_API Vec128<T> Expand(Vec128<T> v, Mask128<T> mask) {
  // Same as Compress, just zero out the mask=false lanes.
  return IfThenElseZero(mask, Compress(v, mask));
}

// For single-element vectors, this is at least as fast as native.
template <typename T>
HWY_API Vec128<T, 1> Expand(Vec128<T, 1> v, Mask128<T, 1> mask) {
  return IfThenElseZero(mask, v);
}

template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
  return Expand(LoadU(d, unaligned), mask);
}
#endif  // HWY_PPC_HAVE_10

// ------------------------------ StoreInterleaved2/3/4

// HWY_NATIVE_LOAD_STORE_INTERLEAVED not set, hence defined in
// generic_ops-inl.h.

// ------------------------------ Additional mask logical operations
namespace detail {

#if HWY_IS_LITTLE_ENDIAN
template <class V>
HWY_INLINE V Per64BitBlkRevLanesOnBe(V v) {
  return v;
}
template <class V>
HWY_INLINE V Per128BitBlkRevLanesOnBe(V v) {
  return v;
}
#else
template <class V, HWY_IF_T_SIZE_V(V, 1)>
HWY_INLINE V Per64BitBlkRevLanesOnBe(V v) {
  const DFromV<decltype(v)> d;
  return Reverse8(d, v);
}
template <class V, HWY_IF_T_SIZE_V(V, 2)>
HWY_INLINE V Per64BitBlkRevLanesOnBe(V v) {
  const DFromV<decltype(v)> d;
  return Reverse4(d, v);
}
template <class V, HWY_IF_T_SIZE_V(V, 4)>
HWY_INLINE V Per64BitBlkRevLanesOnBe(V v) {
  const DFromV<decltype(v)> d;
  return Reverse2(d, v);
}
template <class V, HWY_IF_T_SIZE_V(V, 8)>
HWY_INLINE V Per64BitBlkRevLanesOnBe(V v) {
  return v;
}
template <class V>
HWY_INLINE V Per128BitBlkRevLanesOnBe(V v) {
  const DFromV<decltype(v)> d;
  return Reverse(d, v);
}
#endif

template <class V>
HWY_INLINE V I128Subtract(V a, V b) {
#if HWY_S390X_HAVE_Z14
#if HWY_COMPILER_CLANG
  // Workaround for bug in vec_sub_u128 in Clang vecintrin.h
  typedef __uint128_t VU128 __attribute__((__vector_size__(16)));
  const V diff_i128{reinterpret_cast<typename detail::Raw128<TFromV<V>>::type>(
      reinterpret_cast<VU128>(a.raw) - reinterpret_cast<VU128>(b.raw))};
#else   // !HWY_COMPILER_CLANG
  const V diff_i128{reinterpret_cast<typename detail::Raw128<TFromV<V>>::type>(
      vec_sub_u128(reinterpret_cast<__vector unsigned char>(a.raw),
                   reinterpret_cast<__vector unsigned char>(b.raw)))};
#endif  // HWY_COMPILER_CLANG
#elif defined(__SIZEOF_INT128__)
  using VU128 = __vector unsigned __int128;
  const V diff_i128{reinterpret_cast<typename detail::Raw128<TFromV<V>>::type>(
      vec_sub(reinterpret_cast<VU128>(a.raw), reinterpret_cast<VU128>(b.raw)))};
#else
  const DFromV<decltype(a)> d;
  const Repartition<uint64_t, decltype(d)> du64;

  const auto u64_a = BitCast(du64, a);
  const auto u64_b = BitCast(du64, b);

  const auto diff_u64 = u64_a - u64_b;
  const auto borrow_u64 = VecFromMask(du64, u64_a < u64_b);

#if HWY_IS_LITTLE_ENDIAN
  const auto borrow_u64_shifted = ShiftLeftBytes<8>(du64, borrow_u64);
#else
  const auto borrow_u64_shifted = ShiftRightBytes<8>(du64, borrow_u64);
#endif

  const auto diff_i128 = BitCast(d, diff_u64 + borrow_u64_shifted);
#endif

  return diff_i128;
}

}  // namespace detail

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
  const Full64<T> d_full64;

  const auto vmask = VecFromMask(d, mask);
  const auto vmask_le64 =
      BitCast(Full64<int64_t>(),
              detail::Per64BitBlkRevLanesOnBe(ResizeBitCast(d_full64, vmask)));
  const auto neg_vmask_le64 = Neg(vmask_le64);
  const auto neg_vmask = ResizeBitCast(
      d, detail::Per64BitBlkRevLanesOnBe(BitCast(d_full64, neg_vmask_le64)));

  return MaskFromVec(Or(vmask, neg_vmask));
}
template <class T, HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Mask128<T> SetAtOrAfterFirst(Mask128<T> mask) {
  const Full128<T> d;
  auto vmask = VecFromMask(d, mask);

  const auto vmask_le128 = detail::Per128BitBlkRevLanesOnBe(vmask);
  const auto neg_vmask_le128 = detail::I128Subtract(Zero(d), vmask_le128);
  const auto neg_vmask = detail::Per128BitBlkRevLanesOnBe(neg_vmask_le128);

  return MaskFromVec(BitCast(d, Or(vmask, neg_vmask)));
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
  const Full64<T> d_full64;
  const RebindToSigned<decltype(d)> di;

  const auto vmask = VecFromMask(d, mask);
  const auto vmask_le64 =
      BitCast(Full64<int64_t>(),
              detail::Per64BitBlkRevLanesOnBe(ResizeBitCast(d_full64, vmask)));
  const auto neg_vmask_le64 = Neg(vmask_le64);
  const auto neg_vmask = ResizeBitCast(
      d, detail::Per64BitBlkRevLanesOnBe(BitCast(d_full64, neg_vmask_le64)));

  const auto first_vmask = BitCast(di, And(vmask, neg_vmask));
  return MaskFromVec(BitCast(d, Or(first_vmask, Neg(first_vmask))));
}
template <class T, HWY_IF_NOT_T_SIZE(T, 8)>
HWY_API Mask128<T> SetOnlyFirst(Mask128<T> mask) {
  const Full128<T> d;
  const RebindToSigned<decltype(d)> di;

  const auto vmask = VecFromMask(d, mask);
  const auto vmask_le128 = detail::Per128BitBlkRevLanesOnBe(vmask);
  const auto neg_vmask_le128 = detail::I128Subtract(Zero(d), vmask_le128);
  const auto neg_vmask = detail::Per128BitBlkRevLanesOnBe(neg_vmask_le128);

  return MaskFromVec(BitCast(d, Neg(BitCast(di, And(vmask, neg_vmask)))));
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

// ------------------------------ SumsOf2 and SumsOf4
namespace detail {

#if !HWY_S390X_HAVE_Z14
// Casts nominally int32_t result to D.
template <class D>
HWY_INLINE VFromD<D> AltivecVsum4sbs(D d, __vector signed char a,
                                     __vector signed int b) {
  const Repartition<int32_t, D> di32;
#ifdef __OPTIMIZE__
  if (IsConstantRawAltivecVect(a) && IsConstantRawAltivecVect(b)) {
    const int64_t sum0 =
        static_cast<int64_t>(a[0]) + static_cast<int64_t>(a[1]) +
        static_cast<int64_t>(a[2]) + static_cast<int64_t>(a[3]) +
        static_cast<int64_t>(b[0]);
    const int64_t sum1 =
        static_cast<int64_t>(a[4]) + static_cast<int64_t>(a[5]) +
        static_cast<int64_t>(a[6]) + static_cast<int64_t>(a[7]) +
        static_cast<int64_t>(b[1]);
    const int64_t sum2 =
        static_cast<int64_t>(a[8]) + static_cast<int64_t>(a[9]) +
        static_cast<int64_t>(a[10]) + static_cast<int64_t>(a[11]) +
        static_cast<int64_t>(b[2]);
    const int64_t sum3 =
        static_cast<int64_t>(a[12]) + static_cast<int64_t>(a[13]) +
        static_cast<int64_t>(a[14]) + static_cast<int64_t>(a[15]) +
        static_cast<int64_t>(b[3]);
    const int32_t sign0 = static_cast<int32_t>(sum0 >> 63);
    const int32_t sign1 = static_cast<int32_t>(sum1 >> 63);
    const int32_t sign2 = static_cast<int32_t>(sum2 >> 63);
    const int32_t sign3 = static_cast<int32_t>(sum3 >> 63);
    using Raw = typename detail::Raw128<int32_t>::type;
    return BitCast(
        d,
        VFromD<decltype(di32)>{Raw{
            (sign0 == (sum0 >> 31)) ? static_cast<int32_t>(sum0)
                                    : static_cast<int32_t>(sign0 ^ 0x7FFFFFFF),
            (sign1 == (sum1 >> 31)) ? static_cast<int32_t>(sum1)
                                    : static_cast<int32_t>(sign1 ^ 0x7FFFFFFF),
            (sign2 == (sum2 >> 31)) ? static_cast<int32_t>(sum2)
                                    : static_cast<int32_t>(sign2 ^ 0x7FFFFFFF),
            (sign3 == (sum3 >> 31))
                ? static_cast<int32_t>(sum3)
                : static_cast<int32_t>(sign3 ^ 0x7FFFFFFF)}});
  } else  // NOLINT
#endif
  {
    return BitCast(d, VFromD<decltype(di32)>{vec_vsum4sbs(a, b)});
  }
}

// Casts nominally uint32_t result to D.
template <class D>
HWY_INLINE VFromD<D> AltivecVsum4ubs(D d, __vector unsigned char a,
                                     __vector unsigned int b) {
  const Repartition<uint32_t, D> du32;
#ifdef __OPTIMIZE__
  if (IsConstantRawAltivecVect(a) && IsConstantRawAltivecVect(b)) {
    const uint64_t sum0 =
        static_cast<uint64_t>(a[0]) + static_cast<uint64_t>(a[1]) +
        static_cast<uint64_t>(a[2]) + static_cast<uint64_t>(a[3]) +
        static_cast<uint64_t>(b[0]);
    const uint64_t sum1 =
        static_cast<uint64_t>(a[4]) + static_cast<uint64_t>(a[5]) +
        static_cast<uint64_t>(a[6]) + static_cast<uint64_t>(a[7]) +
        static_cast<uint64_t>(b[1]);
    const uint64_t sum2 =
        static_cast<uint64_t>(a[8]) + static_cast<uint64_t>(a[9]) +
        static_cast<uint64_t>(a[10]) + static_cast<uint64_t>(a[11]) +
        static_cast<uint64_t>(b[2]);
    const uint64_t sum3 =
        static_cast<uint64_t>(a[12]) + static_cast<uint64_t>(a[13]) +
        static_cast<uint64_t>(a[14]) + static_cast<uint64_t>(a[15]) +
        static_cast<uint64_t>(b[3]);
    return BitCast(
        d,
        VFromD<decltype(du32)>{(__vector unsigned int){
            static_cast<unsigned int>(sum0 <= 0xFFFFFFFFu ? sum0 : 0xFFFFFFFFu),
            static_cast<unsigned int>(sum1 <= 0xFFFFFFFFu ? sum1 : 0xFFFFFFFFu),
            static_cast<unsigned int>(sum2 <= 0xFFFFFFFFu ? sum2 : 0xFFFFFFFFu),
            static_cast<unsigned int>(sum3 <= 0xFFFFFFFFu ? sum3
                                                          : 0xFFFFFFFFu)}});
  } else  // NOLINT
#endif
  {
    return BitCast(d, VFromD<decltype(du32)>{vec_vsum4ubs(a, b)});
  }
}

// Casts nominally int32_t result to D.
template <class D>
HWY_INLINE VFromD<D> AltivecVsum2sws(D d, __vector signed int a,
                                     __vector signed int b) {
  const Repartition<int32_t, D> di32;
#ifdef __OPTIMIZE__
  const Repartition<uint64_t, D> du64;
  constexpr int kDestLaneOffset = HWY_IS_BIG_ENDIAN;
  if (IsConstantRawAltivecVect(a) && __builtin_constant_p(b[kDestLaneOffset]) &&
      __builtin_constant_p(b[kDestLaneOffset + 2])) {
    const int64_t sum0 = static_cast<int64_t>(a[0]) +
                         static_cast<int64_t>(a[1]) +
                         static_cast<int64_t>(b[kDestLaneOffset]);
    const int64_t sum1 = static_cast<int64_t>(a[2]) +
                         static_cast<int64_t>(a[3]) +
                         static_cast<int64_t>(b[kDestLaneOffset + 2]);
    const int32_t sign0 = static_cast<int32_t>(sum0 >> 63);
    const int32_t sign1 = static_cast<int32_t>(sum1 >> 63);
    return BitCast(d, VFromD<decltype(du64)>{(__vector unsigned long long){
                          (sign0 == (sum0 >> 31))
                              ? static_cast<uint32_t>(sum0)
                              : static_cast<uint32_t>(sign0 ^ 0x7FFFFFFF),
                          (sign1 == (sum1 >> 31))
                              ? static_cast<uint32_t>(sum1)
                              : static_cast<uint32_t>(sign1 ^ 0x7FFFFFFF)}});
  } else  // NOLINT
#endif
  {
    __vector signed int sum;

    // Inline assembly is used for vsum2sws to avoid unnecessary shuffling
    // on little-endian PowerPC targets as the result of the vsum2sws
    // instruction will already be in the correct lanes on little-endian
    // PowerPC targets.
    __asm__("vsum2sws %0,%1,%2" : "=v"(sum) : "v"(a), "v"(b));

    return BitCast(d, VFromD<decltype(di32)>{sum});
  }
}

// Casts nominally int32_t result to D.
template <class D>
HWY_INLINE VFromD<D> AltivecVsum4shs(D d, __vector signed short a,
                                     __vector signed int b) {
  const Repartition<int32_t, D> di32;
#ifdef __OPTIMIZE__
  if (IsConstantRawAltivecVect(a) && IsConstantRawAltivecVect(b)) {
    const int64_t sum0 = static_cast<int64_t>(a[0]) +
                         static_cast<int64_t>(a[1]) +
                         static_cast<int64_t>(b[0]);
    const int64_t sum1 = static_cast<int64_t>(a[2]) +
                         static_cast<int64_t>(a[3]) +
                         static_cast<int64_t>(b[1]);
    const int64_t sum2 = static_cast<int64_t>(a[4]) +
                         static_cast<int64_t>(a[5]) +
                         static_cast<int64_t>(b[2]);
    const int64_t sum3 = static_cast<int64_t>(a[6]) +
                         static_cast<int64_t>(a[7]) +
                         static_cast<int64_t>(b[3]);
    const int32_t sign0 = static_cast<int32_t>(sum0 >> 63);
    const int32_t sign1 = static_cast<int32_t>(sum1 >> 63);
    const int32_t sign2 = static_cast<int32_t>(sum2 >> 63);
    const int32_t sign3 = static_cast<int32_t>(sum3 >> 63);
    using Raw = typename detail::Raw128<int32_t>::type;
    return BitCast(
        d,
        VFromD<decltype(di32)>{Raw{
            (sign0 == (sum0 >> 31)) ? static_cast<int32_t>(sum0)
                                    : static_cast<int32_t>(sign0 ^ 0x7FFFFFFF),
            (sign1 == (sum1 >> 31)) ? static_cast<int32_t>(sum1)
                                    : static_cast<int32_t>(sign1 ^ 0x7FFFFFFF),
            (sign2 == (sum2 >> 31)) ? static_cast<int32_t>(sum2)
                                    : static_cast<int32_t>(sign2 ^ 0x7FFFFFFF),
            (sign3 == (sum3 >> 31))
                ? static_cast<int32_t>(sum3)
                : static_cast<int32_t>(sign3 ^ 0x7FFFFFFF)}});
  } else  // NOLINT
#endif
  {
    return BitCast(d, VFromD<decltype(di32)>{vec_vsum4shs(a, b)});
  }
}

// Casts nominally int32_t result to D.
template <class D>
HWY_INLINE VFromD<D> AltivecVsumsws(D d, __vector signed int a,
                                    __vector signed int b) {
  const Repartition<int32_t, D> di32;
#ifdef __OPTIMIZE__
  constexpr int kDestLaneOffset = HWY_IS_LITTLE_ENDIAN ? 0 : 3;
  if (IsConstantRawAltivecVect(a) && __builtin_constant_p(b[kDestLaneOffset])) {
    const int64_t sum =
        static_cast<int64_t>(a[0]) + static_cast<int64_t>(a[1]) +
        static_cast<int64_t>(a[2]) + static_cast<int64_t>(a[3]) +
        static_cast<int64_t>(b[kDestLaneOffset]);
    const int32_t sign = static_cast<int32_t>(sum >> 63);
#if HWY_IS_LITTLE_ENDIAN
    return BitCast(
        d, VFromD<decltype(di32)>{(__vector signed int){
               (sign == (sum >> 31)) ? static_cast<int32_t>(sum)
                                     : static_cast<int32_t>(sign ^ 0x7FFFFFFF),
               0, 0, 0}});
#else
    return BitCast(d, VFromD<decltype(di32)>{(__vector signed int){
                          0, 0, 0,
                          (sign == (sum >> 31))
                              ? static_cast<int32_t>(sum)
                              : static_cast<int32_t>(sign ^ 0x7FFFFFFF)}});
#endif
  } else  // NOLINT
#endif
  {
    __vector signed int sum;

    // Inline assembly is used for vsumsws to avoid unnecessary shuffling
    // on little-endian PowerPC targets as the result of the vsumsws
    // instruction will already be in the correct lanes on little-endian
    // PowerPC targets.
    __asm__("vsumsws %0,%1,%2" : "=v"(sum) : "v"(a), "v"(b));

    return BitCast(d, VFromD<decltype(di32)>{sum});
  }
}

template <size_t N>
HWY_INLINE Vec128<int32_t, N / 2> AltivecU16SumsOf2(Vec128<uint16_t, N> v) {
  const RebindToSigned<DFromV<decltype(v)>> di16;
  const RepartitionToWide<decltype(di16)> di32;
  return AltivecVsum4shs(di32, Xor(BitCast(di16, v), Set(di16, -32768)).raw,
                         Set(di32, 65536).raw);
}
#endif  // !HWY_S390X_HAVE_Z14

// U16->U32 SumsOf2
template <class V>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag /*type_tag*/, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;

#if HWY_S390X_HAVE_Z14
  return VFromD<decltype(dw)>{vec_sum4(v.raw, Zero(d).raw)};
#else
  return BitCast(dw, AltivecU16SumsOf2(v));
#endif
}

// I16->I32 SumsOf2
template <class V>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag /*type_tag*/, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;

#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(dw, SumsOf2(hwy::UnsignedTag(), hwy::SizeTag<2>(),
                             BitCast(du, Xor(v, SignBit(d))))) +
         Set(dw, int32_t{-65536});
#else
  return AltivecVsum4shs(dw, v.raw, Zero(dw).raw);
#endif
}

#if HWY_S390X_HAVE_Z14
// U32->U64 SumsOf2
template <class V>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::UnsignedTag /*type_tag*/, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;
  return VFromD<decltype(dw)>{vec_sum2(v.raw, Zero(d).raw)};
}

// I32->I64 SumsOf2
template <class V>
HWY_INLINE VFromD<RepartitionToWide<DFromV<V>>> SumsOf2(
    hwy::SignedTag /*type_tag*/, hwy::SizeTag<4> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;
  const RebindToUnsigned<decltype(d)> du;

  return BitCast(dw, SumsOf2(hwy::UnsignedTag(), hwy::SizeTag<4>(),
                             BitCast(du, Xor(v, SignBit(d))))) +
         Set(dw, int64_t{-4294967296LL});
}
#endif

// U8->U32 SumsOf4
template <class V>
HWY_INLINE VFromD<RepartitionToWideX2<DFromV<V>>> SumsOf4(
    hwy::UnsignedTag /*type_tag*/, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWideX2<decltype(d)> dw2;

#if HWY_S390X_HAVE_Z14
  return VFromD<decltype(dw2)>{vec_sum4(v.raw, Zero(d).raw)};
#else
  return AltivecVsum4ubs(dw2, v.raw, Zero(dw2).raw);
#endif
}

// I8->I32 SumsOf4
template <class V>
HWY_INLINE VFromD<RepartitionToWideX2<DFromV<V>>> SumsOf4(
    hwy::SignedTag /*type_tag*/, hwy::SizeTag<1> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWideX2<decltype(d)> dw2;

#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(dw2, SumsOf4(hwy::UnsignedTag(), hwy::SizeTag<1>(),
                              BitCast(du, Xor(v, SignBit(d))))) +
         Set(dw2, int32_t{-512});
#else
  return AltivecVsum4sbs(dw2, v.raw, Zero(dw2).raw);
#endif
}

// U16->U64 SumsOf4
template <class V>
HWY_INLINE VFromD<RepartitionToWideX2<DFromV<V>>> SumsOf4(
    hwy::UnsignedTag /*type_tag*/, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;
  const RepartitionToWide<decltype(dw)> dw2;

#if HWY_S390X_HAVE_Z14
  return VFromD<decltype(dw2)>{vec_sum2(v.raw, Zero(d).raw)};
#else
  const RebindToSigned<decltype(dw)> dw_i;
  return AltivecVsum2sws(dw2, BitCast(dw_i, SumsOf2(v)).raw, Zero(dw_i).raw);
#endif
}

// I16->I64 SumsOf4
template <class V>
HWY_INLINE VFromD<RepartitionToWideX2<DFromV<V>>> SumsOf4(
    hwy::SignedTag /*type_tag*/, hwy::SizeTag<2> /*lane_size_tag*/, V v) {
  const DFromV<V> d;
  const RepartitionToWide<decltype(d)> dw;
  const RepartitionToWide<decltype(dw)> dw2;

#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(dw2, SumsOf4(hwy::UnsignedTag(), hwy::SizeTag<2>(),
                              BitCast(du, Xor(v, SignBit(d))))) +
         Set(dw2, int64_t{-131072});
#else  // VSX
  const auto sums_of_4_in_lo32 =
      AltivecVsum2sws(dw, SumsOf2(v).raw, Zero(dw).raw);

#if HWY_IS_LITTLE_ENDIAN
  return PromoteEvenTo(dw2, sums_of_4_in_lo32);
#else
  return PromoteOddTo(dw2, sums_of_4_in_lo32);
#endif  // HWY_IS_LITTLE_ENDIAN
#endif  // HWY_S390X_HAVE_Z14
}

}  // namespace detail

// ------------------------------ SumOfLanes

// We define SumOfLanes for 8/16-bit types (and I32/U32/I64/U64 on Z14/Z15/Z16);
// enable generic for the rest.
#undef HWY_IF_SUM_OF_LANES_D
#if HWY_S390X_HAVE_Z14
#define HWY_IF_SUM_OF_LANES_D(D) HWY_IF_LANES_GT_D(D, 1), HWY_IF_FLOAT3264_D(D)
#else
#define HWY_IF_SUM_OF_LANES_D(D) \
  HWY_IF_LANES_GT_D(D, 1), HWY_IF_T_SIZE_ONE_OF_D(D, (1 << 4) | (1 << 8))
#endif

#if HWY_S390X_HAVE_Z14
namespace detail {

#if HWY_COMPILER_CLANG && HWY_HAS_BUILTIN(__builtin_s390_vsumqf) && \
    HWY_HAS_BUILTIN(__builtin_s390_vsumqg)
// Workaround for bug in vec_sum_u128 in Clang vecintrin.h
template <class T, HWY_IF_UI32(T)>
HWY_INLINE Vec128<T> SumOfU32OrU64LanesAsU128(Vec128<T> v) {
  typedef __uint128_t VU128 __attribute__((__vector_size__(16)));
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const VU128 sum = {__builtin_s390_vsumqf(BitCast(du, v).raw, Zero(du).raw)};
  return Vec128<T>{reinterpret_cast<typename detail::Raw128<T>::type>(sum)};
}
template <class T, HWY_IF_UI64(T)>
HWY_INLINE Vec128<T> SumOfU32OrU64LanesAsU128(Vec128<T> v) {
  typedef __uint128_t VU128 __attribute__((__vector_size__(16)));
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  const VU128 sum = {__builtin_s390_vsumqg(BitCast(du, v).raw, Zero(du).raw)};
  return Vec128<T>{reinterpret_cast<typename detail::Raw128<T>::type>(sum)};
}
#else
template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T),
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 4) | (1 << 8))>
HWY_INLINE Vec128<T> SumOfU32OrU64LanesAsU128(Vec128<T> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(
      d, Vec128<uint8_t>{vec_sum_u128(BitCast(du, v).raw, Zero(du).raw)});
}
#endif

}  // namespace detail

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI64_D(D)>
HWY_API VFromD<D> SumOfLanes(D /*d64*/, VFromD<D> v) {
  return Broadcast<1>(detail::SumOfU32OrU64LanesAsU128(v));
}
#endif

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U16_D(D)>
HWY_API Vec32<uint16_t> SumOfLanes(D du16, Vec32<uint16_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_BIG_ENDIAN;
  return Broadcast<kSumLaneIdx>(
      BitCast(du16, detail::SumsOf2(hwy::UnsignedTag(), hwy::SizeTag<2>(), v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U16_D(D)>
HWY_API Vec64<uint16_t> SumOfLanes(D du16, Vec64<uint16_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 3;
  return Broadcast<kSumLaneIdx>(
      BitCast(du16, detail::SumsOf4(hwy::UnsignedTag(), hwy::SizeTag<2>(), v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U16_D(D)>
HWY_API Vec128<uint16_t> SumOfLanes(D du16, Vec128<uint16_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 7;
#if HWY_S390X_HAVE_Z14
  return Broadcast<kSumLaneIdx>(
      BitCast(du16, detail::SumOfU32OrU64LanesAsU128(detail::SumsOf4(
                        hwy::UnsignedTag(), hwy::SizeTag<2>(), v))));
#else  // VSX
  const auto zero = Zero(Full128<int32_t>());
  return Broadcast<kSumLaneIdx>(
      detail::AltivecVsumsws(du16, detail::AltivecU16SumsOf2(v).raw, zero.raw));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I16_D(D)>
HWY_API Vec32<int16_t> SumOfLanes(D di16, Vec32<int16_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di16)> du16;
  return BitCast(di16, SumOfLanes(du16, BitCast(du16, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_BIG_ENDIAN;
  return Broadcast<kSumLaneIdx>(
      BitCast(di16, detail::SumsOf2(hwy::SignedTag(), hwy::SizeTag<2>(), v)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I16_D(D)>
HWY_API Vec64<int16_t> SumOfLanes(D di16, Vec64<int16_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di16)> du16;
  return BitCast(di16, SumOfLanes(du16, BitCast(du16, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 3;
  return Broadcast<kSumLaneIdx>(
      BitCast(di16, detail::SumsOf4(hwy::SignedTag(), hwy::SizeTag<2>(), v)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I16_D(D)>
HWY_API Vec128<int16_t> SumOfLanes(D di16, Vec128<int16_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di16)> du16;
  return BitCast(di16, SumOfLanes(du16, BitCast(du16, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 7;
  const Full128<int32_t> di32;
  const auto zero = Zero(di32);
  return Broadcast<kSumLaneIdx>(detail::AltivecVsumsws(
      di16, detail::AltivecVsum4shs(di32, v.raw, zero.raw).raw, zero.raw));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_U8_D(D)>
HWY_API Vec32<uint8_t> SumOfLanes(D du8, Vec32<uint8_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 3;
  return Broadcast<kSumLaneIdx>(
      BitCast(du8, detail::SumsOf4(hwy::UnsignedTag(), hwy::SizeTag<1>(), v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_U8_D(D)>
HWY_API Vec16<uint8_t> SumOfLanes(D du8, Vec16<uint8_t> v) {
  const Twice<decltype(du8)> dt_u8;
  return LowerHalf(du8, SumOfLanes(dt_u8, Combine(dt_u8, Zero(du8), v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_U8_D(D)>
HWY_API Vec64<uint8_t> SumOfLanes(D du8, Vec64<uint8_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 7;
  return Broadcast<kSumLaneIdx>(BitCast(du8, SumsOf8(v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_U8_D(D)>
HWY_API Vec128<uint8_t> SumOfLanes(D du8, Vec128<uint8_t> v) {
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 15;

#if HWY_S390X_HAVE_Z14
  return Broadcast<kSumLaneIdx>(
      BitCast(du8, detail::SumOfU32OrU64LanesAsU128(detail::SumsOf4(
                       hwy::UnsignedTag(), hwy::SizeTag<1>(), v))));
#else
  const Full128<uint32_t> du32;
  const RebindToSigned<decltype(du32)> di32;
  const Vec128<uint32_t> zero = Zero(du32);
  return Broadcast<kSumLaneIdx>(detail::AltivecVsumsws(
      du8, detail::AltivecVsum4ubs(di32, v.raw, zero.raw).raw,
      BitCast(di32, zero).raw));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_I8_D(D)>
HWY_API Vec32<int8_t> SumOfLanes(D di8, Vec32<int8_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di8)> du8;
  return BitCast(di8, SumOfLanes(du8, BitCast(du8, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 3;
  return Broadcast<kSumLaneIdx>(
      BitCast(di8, detail::SumsOf4(hwy::SignedTag(), hwy::SizeTag<1>(), v)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 2), HWY_IF_I8_D(D)>
HWY_API Vec16<int8_t> SumOfLanes(D di8, Vec16<int8_t> v) {
  const Twice<decltype(di8)> dt_i8;
  return LowerHalf(di8, SumOfLanes(dt_i8, Combine(dt_i8, Zero(di8), v)));
}

template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_I8_D(D)>
HWY_API Vec64<int8_t> SumOfLanes(D di8, Vec64<int8_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di8)> du8;
  return BitCast(di8, SumOfLanes(du8, BitCast(du8, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 7;
  return Broadcast<kSumLaneIdx>(BitCast(di8, SumsOf8(v)));
#endif
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_I8_D(D)>
HWY_API Vec128<int8_t> SumOfLanes(D di8, Vec128<int8_t> v) {
#if HWY_S390X_HAVE_Z14
  const RebindToUnsigned<decltype(di8)> du8;
  return BitCast(di8, SumOfLanes(du8, BitCast(du8, v)));
#else
  constexpr int kSumLaneIdx = HWY_IS_LITTLE_ENDIAN ? 0 : 15;
  const Full128<int32_t> di32;
  const Vec128<int32_t> zero = Zero(di32);
  return Broadcast<kSumLaneIdx>(detail::AltivecVsumsws(
      di8, detail::AltivecVsum4sbs(di32, v.raw, zero.raw).raw, zero.raw));
#endif
}

#if HWY_S390X_HAVE_Z14
template <class D, HWY_IF_V_SIZE_D(D, 8), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> SumOfLanes(D d32, VFromD<D> v) {
  const RebindToUnsigned<decltype(d32)> du32;
  return Broadcast<1>(
      BitCast(d32, detail::SumsOf2(hwy::UnsignedTag(), hwy::SizeTag<4>(),
                                   BitCast(du32, v))));
}

template <class D, HWY_IF_V_SIZE_D(D, 16), HWY_IF_UI32_D(D)>
HWY_API VFromD<D> SumOfLanes(D /*d32*/, VFromD<D> v) {
  return Broadcast<3>(detail::SumOfU32OrU64LanesAsU128(v));
}
#endif

// generic_ops defines MinOfLanes and MaxOfLanes.

// ------------------------------ ReduceSum for N=4 I8/U8

// GetLane(SumsOf4(v)) is more efficient on PPC/Z14 than the default N=4
// I8/U8 ReduceSum implementation in generic_ops-inl.h
#ifdef HWY_NATIVE_REDUCE_SUM_4_UI8
#undef HWY_NATIVE_REDUCE_SUM_4_UI8
#else
#define HWY_NATIVE_REDUCE_SUM_4_UI8
#endif

template <class D, HWY_IF_V_SIZE_D(D, 4), HWY_IF_UI8_D(D)>
HWY_API TFromD<D> ReduceSum(D /*d*/, VFromD<D> v) {
  return static_cast<TFromD<D>>(GetLane(SumsOf4(v)));
}

// ------------------------------ BitShuffle

#ifdef HWY_NATIVE_BITSHUFFLE
#undef HWY_NATIVE_BITSHUFFLE
#else
#define HWY_NATIVE_BITSHUFFLE
#endif

template <class V, class VI, HWY_IF_UI64(TFromV<V>), HWY_IF_UI8(TFromV<VI>),
          HWY_IF_V_SIZE_V(VI, HWY_MAX_LANES_V(V) * 8)>
HWY_API V BitShuffle(V v, VI idx) {
  const DFromV<decltype(v)> d64;
  const RebindToUnsigned<decltype(d64)> du64;
  const Repartition<uint8_t, decltype(d64)> du8;

  const Full128<TFromD<decltype(du64)>> d_full_u64;
  const Full128<TFromD<decltype(du8)>> d_full_u8;

  using RawVU64 = __vector unsigned long long;

#if HWY_PPC_HAVE_9

#if HWY_IS_LITTLE_ENDIAN
  (void)d_full_u64;
  auto bit_idx = ResizeBitCast(d_full_u8, idx);
#else
  auto bit_idx =
      BitCast(d_full_u8, ReverseLaneBytes(ResizeBitCast(d_full_u64, idx)));
#endif

  bit_idx = Xor(bit_idx, Set(d_full_u8, uint8_t{0x3F}));

  return BitCast(d64, VFromD<decltype(du64)>{reinterpret_cast<RawVU64>(
                          vec_bperm(BitCast(du64, v).raw, bit_idx.raw))});
#else  // !HWY_PPC_HAVE_9

#if HWY_IS_LITTLE_ENDIAN
  const auto bit_idx_xor_mask = BitCast(
      d_full_u8, Dup128VecFromValues(d_full_u64, uint64_t{0x7F7F7F7F7F7F7F7Fu},
                                     uint64_t{0x3F3F3F3F3F3F3F3Fu}));
  const auto bit_idx = Xor(ResizeBitCast(d_full_u8, idx), bit_idx_xor_mask);
  constexpr int kBitShufResultByteShrAmt = 8;
#else
  const auto bit_idx_xor_mask = BitCast(
      d_full_u8, Dup128VecFromValues(d_full_u64, uint64_t{0x3F3F3F3F3F3F3F3Fu},
                                     uint64_t{0x7F7F7F7F7F7F7F7Fu}));
  const auto bit_idx =
      Xor(BitCast(d_full_u8, ReverseLaneBytes(ResizeBitCast(d_full_u64, idx))),
          bit_idx_xor_mask);
  constexpr int kBitShufResultByteShrAmt = 6;
#endif

#if HWY_S390X_HAVE_Z14
  const VFromD<decltype(d_full_u64)> bit_shuf_result{reinterpret_cast<RawVU64>(
      vec_bperm_u128(BitCast(du8, v).raw, bit_idx.raw))};
#elif defined(__SIZEOF_INT128__)
  using RawVU128 = __vector unsigned __int128;
  const VFromD<decltype(d_full_u64)> bit_shuf_result{reinterpret_cast<RawVU64>(
      vec_vbpermq(reinterpret_cast<RawVU128>(v.raw), bit_idx.raw))};
#else
  using RawVU128 = __vector unsigned char;
  const VFromD<decltype(d_full_u64)> bit_shuf_result{reinterpret_cast<RawVU64>(
      vec_vbpermq(reinterpret_cast<RawVU128>(v.raw), bit_idx.raw))};
#endif

  return ResizeBitCast(
      d64, PromoteTo(d_full_u64,
                     ResizeBitCast(
                         Rebind<uint8_t, decltype(d_full_u64)>(),
                         CombineShiftRightBytes<kBitShufResultByteShrAmt>(
                             d_full_u64, bit_shuf_result, bit_shuf_result))));
#endif  // HWY_PPC_HAVE_9
}

// ------------------------------ Lt128

namespace detail {

// Returns vector-mask for Lt128.
template <class D, class V = VFromD<D>>
HWY_INLINE V Lt128Vec(D d, V a, V b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
#if HWY_PPC_HAVE_10 && defined(__SIZEOF_INT128__)
  (void)d;
  using VU64 = __vector unsigned long long;
  using VU128 = __vector unsigned __int128;
#if HWY_IS_LITTLE_ENDIAN
  const VU128 a_u128 = reinterpret_cast<VU128>(a.raw);
  const VU128 b_u128 = reinterpret_cast<VU128>(b.raw);
#else
  // NOTE: Need to swap the halves of both a and b on big-endian targets
  // as the upper 64 bits of a and b are in lane 1 and the lower 64 bits
  // of a and b are in lane 0 whereas the vec_cmplt operation below expects
  // the upper 64 bits in lane 0 and the lower 64 bits in lane 1 on
  // big-endian PPC targets.
  const VU128 a_u128 = reinterpret_cast<VU128>(vec_sld(a.raw, a.raw, 8));
  const VU128 b_u128 = reinterpret_cast<VU128>(vec_sld(b.raw, b.raw, 8));
#endif
  return V{reinterpret_cast<VU64>(vec_cmplt(a_u128, b_u128))};
#else  // !HWY_PPC_HAVE_10
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
  const V ltHL = VecFromMask(d, Lt(a, b));
  const V ltLX = ShiftLeftLanes<1>(ltHL);
  const V vecHx = IfThenElse(eqHL, ltLX, ltHL);
  return InterleaveUpper(d, vecHx, vecHx);
#endif
}

// Returns vector-mask for Eq128.
template <class D, class V = VFromD<D>>
HWY_INLINE V Eq128Vec(D d, V a, V b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
#if HWY_PPC_HAVE_10 && defined(__SIZEOF_INT128__)
  (void)d;
  using VU64 = __vector unsigned long long;
  using VU128 = __vector unsigned __int128;
  return V{reinterpret_cast<VU64>(vec_cmpeq(reinterpret_cast<VU128>(a.raw),
                                            reinterpret_cast<VU128>(b.raw)))};
#else
  const auto eqHL = VecFromMask(d, Eq(a, b));
  const auto eqLH = Reverse2(d, eqHL);
  return And(eqHL, eqLH);
#endif
}

template <class D, class V = VFromD<D>>
HWY_INLINE V Ne128Vec(D d, V a, V b) {
  static_assert(IsSame<TFromD<D>, uint64_t>(), "D must be u64");
#if HWY_PPC_HAVE_10 && defined(__SIZEOF_INT128__)
  (void)d;
  using VU64 = __vector unsigned long long;
  using VU128 = __vector unsigned __int128;
  return V{reinterpret_cast<VU64>(vec_cmpne(reinterpret_cast<VU128>(a.raw),
                                            reinterpret_cast<VU128>(b.raw)))};
#else
  const auto neHL = VecFromMask(d, Ne(a, b));
  const auto neLH = Reverse2(d, neHL);
  return Or(neHL, neLH);
#endif
}

template <class D, class V = VFromD<D>>
HWY_INLINE V Lt128UpperVec(D d, V a, V b) {
  const V ltHL = VecFromMask(d, Lt(a, b));
  return InterleaveUpper(d, ltHL, ltHL);
}

template <class D, class V = VFromD<D>>
HWY_INLINE V Eq128UpperVec(D d, V a, V b) {
  const V eqHL = VecFromMask(d, Eq(a, b));
  return InterleaveUpper(d, eqHL, eqHL);
}

template <class D, class V = VFromD<D>>
HWY_INLINE V Ne128UpperVec(D d, V a, V b) {
  const V neHL = VecFromMask(d, Ne(a, b));
  return InterleaveUpper(d, neHL, neHL);
}

}  // namespace detail

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Lt128(D d, V a, V b) {
  return MaskFromVec(detail::Lt128Vec(d, a, b));
}

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Eq128(D d, V a, V b) {
  return MaskFromVec(detail::Eq128Vec(d, a, b));
}

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Ne128(D d, V a, V b) {
  return MaskFromVec(detail::Ne128Vec(d, a, b));
}

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Lt128Upper(D d, V a, V b) {
  return MaskFromVec(detail::Lt128UpperVec(d, a, b));
}

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Eq128Upper(D d, V a, V b) {
  return MaskFromVec(detail::Eq128UpperVec(d, a, b));
}

template <class D, class V = VFromD<D>>
HWY_API MFromD<D> Ne128Upper(D d, V a, V b) {
  return MaskFromVec(detail::Ne128UpperVec(d, a, b));
}

// ------------------------------ Min128, Max128 (Lt128)

// Avoids the extra MaskFromVec in Lt128.
template <class D, class V = VFromD<D>>
HWY_API V Min128(D d, const V a, const V b) {
  return IfVecThenElse(detail::Lt128Vec(d, a, b), a, b);
}

template <class D, class V = VFromD<D>>
HWY_API V Max128(D d, const V a, const V b) {
  return IfVecThenElse(detail::Lt128Vec(d, b, a), a, b);
}

template <class D, class V = VFromD<D>>
HWY_API V Min128Upper(D d, const V a, const V b) {
  return IfVecThenElse(detail::Lt128UpperVec(d, a, b), a, b);
}

template <class D, class V = VFromD<D>>
HWY_API V Max128Upper(D d, const V a, const V b) {
  return IfVecThenElse(detail::Lt128UpperVec(d, b, a), a, b);
}

// -------------------- LeadingZeroCount, TrailingZeroCount, HighestSetBitIndex

#ifdef HWY_NATIVE_LEADING_ZERO_COUNT
#undef HWY_NATIVE_LEADING_ZERO_COUNT
#else
#define HWY_NATIVE_LEADING_ZERO_COUNT
#endif

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V LeadingZeroCount(V v) {
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

#if HWY_COMPILER_GCC_ACTUAL && defined(__OPTIMIZE__)
  // Work around for GCC compiler bug in vec_cnttz on Z14/Z15 if v[i] is a
  // constant
  __asm__("" : "+v"(v.raw));
#endif

  return BitCast(d, VFromD<decltype(du)>{vec_cntlz(BitCast(du, v).raw)});
#else
  return V{vec_cntlz(v.raw)};
#endif
}

template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V HighestSetBitIndex(V v) {
  const DFromV<decltype(v)> d;
  using T = TFromD<decltype(d)>;
  return BitCast(d, Set(d, T{sizeof(T) * 8 - 1}) - LeadingZeroCount(v));
}

#if HWY_PPC_HAVE_9 || HWY_S390X_HAVE_Z14
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V TrailingZeroCount(V v) {
#if HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
  return V{vec_vctz(v.raw)};
#else
#if HWY_S390X_HAVE_Z14
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

#if HWY_COMPILER_GCC_ACTUAL && defined(__OPTIMIZE__)
  // Work around for GCC compiler bug in vec_cnttz on Z14/Z15 if v[i] is a
  // constant
  __asm__("" : "+v"(v.raw));
#endif

  return BitCast(d, VFromD<decltype(du)>{vec_cnttz(BitCast(du, v).raw)});
#else
  return V{vec_cnttz(v.raw)};
#endif  // HWY_S390X_HAVE_Z14
#endif  // HWY_COMPILER_GCC_ACTUAL && HWY_COMPILER_GCC_ACTUAL < 700
}
#else
template <class V, HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V)>
HWY_API V TrailingZeroCount(V v) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  using TI = TFromD<decltype(di)>;

  const auto vi = BitCast(di, v);
  const auto lowest_bit = And(vi, Neg(vi));
  constexpr TI kNumOfBitsInT{sizeof(TI) * 8};
  const auto bit_idx = HighestSetBitIndex(lowest_bit);
  return BitCast(d, IfThenElse(MaskFromVec(BroadcastSignBit(bit_idx)),
                               Set(di, kNumOfBitsInT), bit_idx));
}
#endif

#undef HWY_PPC_HAVE_9
#undef HWY_PPC_HAVE_10
#undef HWY_S390X_HAVE_Z14
#undef HWY_S390X_HAVE_Z15

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
