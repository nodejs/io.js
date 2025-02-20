// Copyright 2022 Google LLC
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

// Single-element vectors and operations.
// External include guard in highway.h - see comment there.

#include "hwy/base.h"

#ifndef HWY_NO_LIBCXX
#include <math.h>  // sqrtf
#endif

#include "hwy/ops/shared-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

template <typename T>
using Full128 = Simd<T, 16 / sizeof(T), 0>;

// (Wrapper class required for overloading comparison operators.)
template <typename T, size_t N = 16 / sizeof(T)>
struct Vec128 {
  using PrivateT = T;                     // only for DFromV
  static constexpr size_t kPrivateN = N;  // only for DFromV

  HWY_INLINE Vec128() = default;
  Vec128(const Vec128&) = default;
  Vec128& operator=(const Vec128&) = default;

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

  // Behave like wasm128 (vectors can always hold 128 bits). generic_ops-inl.h
  // relies on this for LoadInterleaved*. CAVEAT: this method of padding
  // prevents using range for, especially in SumOfLanes, where it would be
  // incorrect. Moving padding to another field would require handling the case
  // where N = 16 / sizeof(T) (i.e. there is no padding), which is also awkward.
  T raw[16 / sizeof(T)] = {};
};

// 0 or FF..FF, same size as Vec128.
template <typename T, size_t N = 16 / sizeof(T)>
struct Mask128 {
  using Raw = hwy::MakeUnsigned<T>;
  static HWY_INLINE Raw FromBool(bool b) {
    return b ? static_cast<Raw>(~Raw{0}) : 0;
  }

  // Must match the size of Vec128.
  Raw bits[16 / sizeof(T)] = {};
};

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ Zero

// Use HWY_MAX_LANES_D here because VFromD is defined in terms of Zero.
template <class D, HWY_IF_V_SIZE_LE_D(D, 16)>
HWY_API Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> Zero(D /* tag */) {
  Vec128<TFromD<D>, HWY_MAX_LANES_D(D)> v;  // zero-initialized
  return v;
}

template <class D>
using VFromD = decltype(Zero(D()));

// ------------------------------ BitCast

template <class D, class VFrom>
HWY_API VFromD<D> BitCast(D /* tag */, VFrom v) {
  VFromD<D> to;
  CopySameSize(&v.raw, &to.raw);
  return to;
}

// ------------------------------ ResizeBitCast

template <class D, class VFrom>
HWY_API VFromD<D> ResizeBitCast(D d, VFrom v) {
  using DFrom = DFromV<VFrom>;
  using TFrom = TFromD<DFrom>;
  using TTo = TFromD<D>;

  constexpr size_t kFromByteLen = sizeof(TFrom) * HWY_MAX_LANES_D(DFrom);
  constexpr size_t kToByteLen = sizeof(TTo) * HWY_MAX_LANES_D(D);
  constexpr size_t kCopyByteLen = HWY_MIN(kFromByteLen, kToByteLen);

  VFromD<D> to = Zero(d);
  CopyBytes<kCopyByteLen>(&v.raw, &to.raw);
  return to;
}

namespace detail {

// ResizeBitCast on the HWY_EMU128 target has zero-extending semantics if
// VFromD<DTo> is a larger vector than FromV
template <class FromSizeTag, class ToSizeTag, class DTo, class DFrom>
HWY_INLINE VFromD<DTo> ZeroExtendResizeBitCast(FromSizeTag /* from_size_tag */,
                                               ToSizeTag /* to_size_tag */,
                                               DTo d_to, DFrom /* d_from */,
                                               VFromD<DFrom> v) {
  return ResizeBitCast(d_to, v);
}

}  // namespace detail

// ------------------------------ Set
template <class D, typename T2>
HWY_API VFromD<D> Set(D d, const T2 t) {
  VFromD<D> v;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    v.raw[i] = ConvertScalarTo<TFromD<D>>(t);
  }
  return v;
}

// ------------------------------ Undefined
template <class D>
HWY_API VFromD<D> Undefined(D d) {
  return Zero(d);
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
  VFromD<D> result;
  result.raw[0] = t0;
  result.raw[1] = t1;
  result.raw[2] = t2;
  result.raw[3] = t3;
  result.raw[4] = t4;
  result.raw[5] = t5;
  result.raw[6] = t6;
  result.raw[7] = t7;
  result.raw[8] = t8;
  result.raw[9] = t9;
  result.raw[10] = t10;
  result.raw[11] = t11;
  result.raw[12] = t12;
  result.raw[13] = t13;
  result.raw[14] = t14;
  result.raw[15] = t15;
  return result;
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3, TFromD<D> t4,
                                      TFromD<D> t5, TFromD<D> t6,
                                      TFromD<D> t7) {
  VFromD<D> result;
  result.raw[0] = t0;
  result.raw[1] = t1;
  result.raw[2] = t2;
  result.raw[3] = t3;
  result.raw[4] = t4;
  result.raw[5] = t5;
  result.raw[6] = t6;
  result.raw[7] = t7;
  return result;
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1,
                                      TFromD<D> t2, TFromD<D> t3) {
  VFromD<D> result;
  result.raw[0] = t0;
  result.raw[1] = t1;
  result.raw[2] = t2;
  result.raw[3] = t3;
  return result;
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> t1) {
  VFromD<D> result;
  result.raw[0] = t0;
  result.raw[1] = t1;
  return result;
}

// ------------------------------ Iota

template <class D, typename T = TFromD<D>, typename T2>
HWY_API VFromD<D> Iota(D d, T2 first) {
  VFromD<D> v;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    v.raw[i] = AddWithWraparound(static_cast<T>(first), i);
  }
  return v;
}

// ================================================== LOGICAL

// ------------------------------ Not
template <typename T, size_t N>
HWY_API Vec128<T, N> Not(Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  using TU = TFromD<decltype(du)>;
  VFromD<decltype(du)> vu = BitCast(du, v);
  for (size_t i = 0; i < N; ++i) {
    vu.raw[i] = static_cast<TU>(~vu.raw[i]);
  }
  return BitCast(d, vu);
}

// ------------------------------ And
template <typename T, size_t N>
HWY_API Vec128<T, N> And(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  auto au = BitCast(du, a);
  auto bu = BitCast(du, b);
  for (size_t i = 0; i < N; ++i) {
    au.raw[i] &= bu.raw[i];
  }
  return BitCast(d, au);
}
template <typename T, size_t N>
HWY_API Vec128<T, N> operator&(Vec128<T, N> a, Vec128<T, N> b) {
  return And(a, b);
}

// ------------------------------ AndNot
template <typename T, size_t N>
HWY_API Vec128<T, N> AndNot(Vec128<T, N> a, Vec128<T, N> b) {
  return And(Not(a), b);
}

// ------------------------------ Or
template <typename T, size_t N>
HWY_API Vec128<T, N> Or(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  auto au = BitCast(du, a);
  auto bu = BitCast(du, b);
  for (size_t i = 0; i < N; ++i) {
    au.raw[i] |= bu.raw[i];
  }
  return BitCast(d, au);
}
template <typename T, size_t N>
HWY_API Vec128<T, N> operator|(Vec128<T, N> a, Vec128<T, N> b) {
  return Or(a, b);
}

// ------------------------------ Xor
template <typename T, size_t N>
HWY_API Vec128<T, N> Xor(Vec128<T, N> a, Vec128<T, N> b) {
  const DFromV<decltype(a)> d;
  const RebindToUnsigned<decltype(d)> du;
  auto au = BitCast(du, a);
  auto bu = BitCast(du, b);
  for (size_t i = 0; i < N; ++i) {
    au.raw[i] ^= bu.raw[i];
  }
  return BitCast(d, au);
}
template <typename T, size_t N>
HWY_API Vec128<T, N> operator^(Vec128<T, N> a, Vec128<T, N> b) {
  return Xor(a, b);
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
  return Or(And(mask, yes), AndNot(mask, no));
}

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
template <typename T, size_t N>
HWY_API Vec128<T, N> BroadcastSignBit(Vec128<T, N> v) {
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = ScalarShr(v.raw[i], sizeof(T) * 8 - 1);
  }
  return v;
}

// ------------------------------ Mask

// v must be 0 or FF..FF.
template <typename T, size_t N>
HWY_API Mask128<T, N> MaskFromVec(Vec128<T, N> v) {
  Mask128<T, N> mask;
  CopySameSize(&v.raw, &mask.bits);
  return mask;
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <class DTo, class MFrom>
HWY_API MFromD<DTo> RebindMask(DTo /* tag */, MFrom mask) {
  MFromD<DTo> to;
  CopySameSize(&mask.bits, &to.bits);
  return to;
}

template <class D>
VFromD<D> VecFromMask(D /* tag */, MFromD<D> mask) {
  VFromD<D> v;
  CopySameSize(&mask.bits, &v.raw);
  return v;
}

template <class D>
HWY_API MFromD<D> FirstN(D d, size_t n) {
  MFromD<D> m;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    m.bits[i] = MFromD<D>::FromBool(i < n);
  }
  return m;
}

// Returns mask ? yes : no.
template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElse(Mask128<T, N> mask, Vec128<T, N> yes,
                                Vec128<T, N> no) {
  const DFromV<decltype(yes)> d;
  return IfVecThenElse(VecFromMask(d, mask), yes, no);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenElseZero(Mask128<T, N> mask, Vec128<T, N> yes) {
  const DFromV<decltype(yes)> d;
  return IfVecThenElse(VecFromMask(d, mask), yes, Zero(d));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> IfThenZeroElse(Mask128<T, N> mask, Vec128<T, N> no) {
  const DFromV<decltype(no)> d;
  return IfVecThenElse(VecFromMask(d, mask), Zero(d), no);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> IfNegativeThenElse(Vec128<T, N> v, Vec128<T, N> yes,
                                        Vec128<T, N> no) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const auto vi = BitCast(di, v);

  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = vi.raw[i] < 0 ? yes.raw[i] : no.raw[i];
  }
  return v;
}

// ------------------------------ Mask logical

template <typename T, size_t N>
HWY_API Mask128<T, N> Not(Mask128<T, N> m) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Not(VecFromMask(d, m)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> And(Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(And(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> AndNot(Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Or(Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Or(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> Xor(Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(Xor(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T, size_t N>
HWY_API Mask128<T, N> ExclusiveNeither(Mask128<T, N> a, Mask128<T, N> b) {
  const Simd<T, N, 0> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), Not(VecFromMask(d, b))));
}

// ================================================== SHIFTS

// ------------------------------ ShiftLeft/ShiftRight (BroadcastSignBit)

template <int kBits, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeft(Vec128<T, N> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  using TU = hwy::MakeUnsigned<T>;
  for (size_t i = 0; i < N; ++i) {
    const TU raw_u = static_cast<TU>(v.raw[i]);
    const auto shifted = raw_u << kBits;  // separate line to avoid MSVC warning
    v.raw[i] = static_cast<T>(shifted);
  }
  return v;
}

template <int kBits, typename T, size_t N>
HWY_API Vec128<T, N> ShiftRight(Vec128<T, N> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  // Signed right shift is now guaranteed to be arithmetic (rounding toward
  // negative infinity, i.e. shifting in the sign bit).
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = ScalarShr(v.raw[i], kBits);
  }

  return v;
}

// ------------------------------ RotateRight (ShiftRight)
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

// ------------------------------ ShiftLeftSame

template <typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftSame(Vec128<T, N> v, int bits) {
  for (size_t i = 0; i < N; ++i) {
    const auto shifted = static_cast<hwy::MakeUnsigned<T>>(v.raw[i]) << bits;
    v.raw[i] = static_cast<T>(shifted);
  }
  return v;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> ShiftRightSame(Vec128<T, N> v, int bits) {
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = ScalarShr(v.raw[i], bits);
  }

  return v;
}

// ------------------------------ Shl

template <typename T, size_t N>
HWY_API Vec128<T, N> operator<<(Vec128<T, N> v, Vec128<T, N> bits) {
  for (size_t i = 0; i < N; ++i) {
    const auto shifted = static_cast<hwy::MakeUnsigned<T>>(v.raw[i])
                         << bits.raw[i];
    v.raw[i] = static_cast<T>(shifted);
  }
  return v;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> operator>>(Vec128<T, N> v, Vec128<T, N> bits) {
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = ScalarShr(v.raw[i], static_cast<int>(bits.raw[i]));
  }

  return v;
}

// ================================================== ARITHMETIC

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Add(hwy::NonFloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    const uint64_t a64 = static_cast<uint64_t>(a.raw[i]);
    const uint64_t b64 = static_cast<uint64_t>(b.raw[i]);
    a.raw[i] = static_cast<T>((a64 + b64) & static_cast<uint64_t>(~T(0)));
  }
  return a;
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Sub(hwy::NonFloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    const uint64_t a64 = static_cast<uint64_t>(a.raw[i]);
    const uint64_t b64 = static_cast<uint64_t>(b.raw[i]);
    a.raw[i] = static_cast<T>((a64 - b64) & static_cast<uint64_t>(~T(0)));
  }
  return a;
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Add(hwy::FloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] += b.raw[i];
  }
  return a;
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Sub(hwy::FloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] -= b.raw[i];
  }
  return a;
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> operator-(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Sub(hwy::IsFloatTag<T>(), a, b);
}
template <typename T, size_t N>
HWY_API Vec128<T, N> operator+(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Add(hwy::IsFloatTag<T>(), a, b);
}

// ------------------------------ SumsOf8

template <size_t N>
HWY_API Vec128<uint64_t, (N + 7) / 8> SumsOf8(Vec128<uint8_t, N> v) {
  Vec128<uint64_t, (N + 7) / 8> sums;
  for (size_t i = 0; i < N; ++i) {
    sums.raw[i / 8] += v.raw[i];
  }
  return sums;
}

template <size_t N>
HWY_API Vec128<int64_t, (N + 7) / 8> SumsOf8(Vec128<int8_t, N> v) {
  Vec128<int64_t, (N + 7) / 8> sums;
  for (size_t i = 0; i < N; ++i) {
    sums.raw[i / 8] += v.raw[i];
  }
  return sums;
}

// ------------------------------ SaturatedAdd
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> SaturatedAdd(Vec128<T, N> a, Vec128<T, N> b) {
  using TW = MakeSigned<MakeWide<T>>;
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<T>(HWY_MIN(
        HWY_MAX(hwy::LowestValue<T>(), static_cast<TW>(a.raw[i]) + b.raw[i]),
        hwy::HighestValue<T>()));
  }
  return a;
}

// ------------------------------ SaturatedSub
template <typename T, size_t N, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> SaturatedSub(Vec128<T, N> a, Vec128<T, N> b) {
  using TW = MakeSigned<MakeWide<T>>;
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<T>(HWY_MIN(
        HWY_MAX(hwy::LowestValue<T>(), static_cast<TW>(a.raw[i]) - b.raw[i]),
        hwy::HighestValue<T>()));
  }
  return a;
}

// ------------------------------ AverageRound

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

template <typename T, size_t N, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> AverageRound(Vec128<T, N> a, Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    const T a_val = a.raw[i];
    const T b_val = b.raw[i];
    a.raw[i] = static_cast<T>(ScalarShr(a_val, 1) + ScalarShr(b_val, 1) +
                              ((a_val | b_val) & 1));
  }
  return a;
}

// ------------------------------ Abs

template <typename T, size_t N>
HWY_API Vec128<T, N> Abs(Vec128<T, N> a) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = ScalarAbs(a.raw[i]);
  }
  return a;
}

// ------------------------------ Min/Max

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Min(hwy::NonFloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = HWY_MIN(a.raw[i], b.raw[i]);
  }
  return a;
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Max(hwy::NonFloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = HWY_MAX(a.raw[i], b.raw[i]);
  }
  return a;
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Min(hwy::FloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    if (ScalarIsNaN(a.raw[i])) {
      a.raw[i] = b.raw[i];
    } else if (ScalarIsNaN(b.raw[i])) {
      // no change
    } else {
      a.raw[i] = HWY_MIN(a.raw[i], b.raw[i]);
    }
  }
  return a;
}
template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Max(hwy::FloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    if (ScalarIsNaN(a.raw[i])) {
      a.raw[i] = b.raw[i];
    } else if (ScalarIsNaN(b.raw[i])) {
      // no change
    } else {
      a.raw[i] = HWY_MAX(a.raw[i], b.raw[i]);
    }
  }
  return a;
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> Min(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Min(hwy::IsFloatTag<T>(), a, b);
}

template <typename T, size_t N>
HWY_API Vec128<T, N> Max(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Max(hwy::IsFloatTag<T>(), a, b);
}

// ------------------------------ Neg

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename T, size_t N>
HWY_API Vec128<T, N> Neg(hwy::NonFloatTag /*tag*/, Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  return Zero(d) - v;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> Neg(hwy::FloatTag /*tag*/, Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  return Xor(v, SignBit(d));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> Neg(hwy::SpecialTag /*tag*/, Vec128<T, N> v) {
  const DFromV<decltype(v)> d;
  return Xor(v, SignBit(d));
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> Neg(Vec128<T, N> v) {
  return detail::Neg(hwy::IsFloatTag<T>(), v);
}

// ------------------------------ Mul/Div

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Mul(hwy::FloatTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] *= b.raw[i];
  }
  return a;
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Mul(SignedTag /*tag*/, Vec128<T, N> a, Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<T>(static_cast<uint64_t>(a.raw[i]) *
                              static_cast<uint64_t>(b.raw[i]));
  }
  return a;
}

template <typename T, size_t N>
HWY_INLINE Vec128<T, N> Mul(UnsignedTag /*tag*/, Vec128<T, N> a,
                            Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<T>(static_cast<uint64_t>(a.raw[i]) *
                              static_cast<uint64_t>(b.raw[i]));
  }
  return a;
}

}  // namespace detail

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

template <typename T, size_t N>
HWY_API Vec128<T, N> operator*(Vec128<T, N> a, Vec128<T, N> b) {
  return detail::Mul(hwy::TypeTag<T>(), a, b);
}

template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> operator/(Vec128<T, N> a, Vec128<T, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = (b.raw[i] == T{0}) ? 0 : a.raw[i] / b.raw[i];
  }
  return a;
}

// Returns the upper sizeof(T)*8 bits of a * b in each lane.
template <class T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<T, N> MulHigh(Vec128<T, N> a, Vec128<T, N> b) {
  using TW = MakeWide<T>;
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<T>(
        (static_cast<TW>(a.raw[i]) * static_cast<TW>(b.raw[i])) >>
        (sizeof(T) * 8));
  }
  return a;
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T, 1> MulHigh(Vec128<T, 1> a, Vec128<T, 1> b) {
  T hi;
  Mul128(GetLane(a), GetLane(b), &hi);
  return Set(Full64<T>(), hi);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulHigh(Vec128<T> a, Vec128<T> b) {
  T hi_0;
  T hi_1;

  Mul128(GetLane(a), GetLane(b), &hi_0);
  Mul128(ExtractLane(a, 1), ExtractLane(b, 1), &hi_1);

  return Dup128VecFromValues(Full128<T>(), hi_0, hi_1);
}

template <size_t N>
HWY_API Vec128<int16_t, N> MulFixedPoint15(Vec128<int16_t, N> a,
                                           Vec128<int16_t, N> b) {
  for (size_t i = 0; i < N; ++i) {
    a.raw[i] = static_cast<int16_t>((a.raw[i] * b.raw[i] + 16384) >> 15);
  }
  return a;
}

// Multiplies even lanes (0, 2, ..) and returns the double-wide result.
template <class T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulEven(Vec128<T, N> a,
                                                 Vec128<T, N> b) {
  using TW = MakeWide<T>;
  Vec128<TW, (N + 1) / 2> mul;
  for (size_t i = 0; i < N; i += 2) {
    const TW a_wide = a.raw[i];
    mul.raw[i / 2] = static_cast<TW>(a_wide * b.raw[i]);
  }
  return mul;
}

// Multiplies odd lanes (1, 3, ..) and returns the double-wide result.
template <class T, size_t N,
          HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec128<MakeWide<T>, (N + 1) / 2> MulOdd(Vec128<T, N> a,
                                                Vec128<T, N> b) {
  using TW = MakeWide<T>;
  Vec128<TW, (N + 1) / 2> mul;
  for (size_t i = 0; i < N; i += 2) {
    const TW a_wide = a.raw[i + 1];
    mul.raw[i / 2] = static_cast<TW>(a_wide * b.raw[i + 1]);
  }
  return mul;
}

template <size_t N>
HWY_API Vec128<float, N> ApproximateReciprocal(Vec128<float, N> v) {
  for (size_t i = 0; i < N; ++i) {
    // Zero inputs are allowed, but callers are responsible for replacing the
    // return value with something else (typically using IfThenElse). This check
    // avoids a ubsan error. The result is arbitrary.
    v.raw[i] = (ScalarAbs(v.raw[i]) == 0.0f) ? 0.0f : 1.0f / v.raw[i];
  }
  return v;
}

// generic_ops takes care of integer T.
template <typename T, size_t N, HWY_IF_FLOAT(T)>
HWY_API Vec128<T, N> AbsDiff(Vec128<T, N> a, Vec128<T, N> b) {
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

template <size_t N>
HWY_API Vec128<float, N> ApproximateReciprocalSqrt(Vec128<float, N> v) {
  for (size_t i = 0; i < N; ++i) {
    const float half = v.raw[i] * 0.5f;
    // Initial guess based on log2(f)
    v.raw[i] = BitCastScalar<float>(static_cast<uint32_t>(
        0x5F3759DF - (BitCastScalar<uint32_t>(v.raw[i]) >> 1)));
    // One Newton-Raphson iteration
    v.raw[i] = v.raw[i] * (1.5f - (half * v.raw[i] * v.raw[i]));
  }
  return v;
}

namespace detail {

static HWY_INLINE float ScalarSqrt(float v) {
#if defined(HWY_NO_LIBCXX)
#if HWY_COMPILER_GCC_ACTUAL
  return __builtin_sqrt(v);
#else
  uint32_t bits = BitCastScalar<uint32_t>(v);
  // Coarse approximation, letting the exponent LSB leak into the mantissa
  bits = (1 << 29) + (bits >> 1) - (1 << 22);
  return BitCastScalar<float>(bits);
#endif  // !HWY_COMPILER_GCC_ACTUAL
#else
  return sqrtf(v);
#endif  // !HWY_NO_LIBCXX
}
static HWY_INLINE double ScalarSqrt(double v) {
#if defined(HWY_NO_LIBCXX)
#if HWY_COMPILER_GCC_ACTUAL
  return __builtin_sqrt(v);
#else
  uint64_t bits = BitCastScalar<uint64_t>(v);
  // Coarse approximation, letting the exponent LSB leak into the mantissa
  bits = (1ULL << 61) + (bits >> 1) - (1ULL << 51);
  return BitCastScalar<double>(bits);
#endif  // !HWY_COMPILER_GCC_ACTUAL
#else
  return sqrt(v);
#endif  // HWY_NO_LIBCXX
}

}  // namespace detail

template <typename T, size_t N>
HWY_API Vec128<T, N> Sqrt(Vec128<T, N> v) {
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = detail::ScalarSqrt(v.raw[i]);
  }
  return v;
}

// ------------------------------ Floating-point rounding

template <typename T, size_t N>
HWY_API Vec128<T, N> Round(Vec128<T, N> v) {
  using TI = MakeSigned<T>;
  const T k0 = ConvertScalarTo<T>(0);
  const Vec128<T, N> a = Abs(v);
  for (size_t i = 0; i < N; ++i) {
    if (!(a.raw[i] < MantissaEnd<T>())) {  // Huge or NaN
      continue;
    }
    const T bias = ConvertScalarTo<T>(v.raw[i] < k0 ? -0.5 : 0.5);
    const TI rounded = ConvertScalarTo<TI>(v.raw[i] + bias);
    if (rounded == 0) {
      v.raw[i] = v.raw[i] < 0 ? ConvertScalarTo<T>(-0) : k0;
      continue;
    }
    const T rounded_f = ConvertScalarTo<T>(rounded);
    // Round to even
    if ((rounded & 1) &&
        ScalarAbs(rounded_f - v.raw[i]) == ConvertScalarTo<T>(0.5)) {
      v.raw[i] = ConvertScalarTo<T>(rounded - (v.raw[i] < k0 ? -1 : 1));
      continue;
    }
    v.raw[i] = rounded_f;
  }
  return v;
}

// Round-to-nearest even.
template <class T, size_t N, HWY_IF_FLOAT3264(T)>
HWY_API Vec128<MakeSigned<T>, N> NearestInt(Vec128<T, N> v) {
  using TI = MakeSigned<T>;
  const T k0 = ConvertScalarTo<T>(0);

  const Vec128<T, N> abs = Abs(v);
  Vec128<TI, N> ret;
  for (size_t i = 0; i < N; ++i) {
    const bool signbit = ScalarSignBit(v.raw[i]);

    if (!(abs.raw[i] < MantissaEnd<T>())) {  // Huge or NaN
      // Check if too large to cast or NaN
      if (!(abs.raw[i] <= ConvertScalarTo<T>(LimitsMax<TI>()))) {
        ret.raw[i] = signbit ? LimitsMin<TI>() : LimitsMax<TI>();
        continue;
      }
      ret.raw[i] = static_cast<TI>(v.raw[i]);
      continue;
    }
    const T bias = ConvertScalarTo<T>(v.raw[i] < k0 ? -0.5 : 0.5);
    const TI rounded = ConvertScalarTo<TI>(v.raw[i] + bias);
    if (rounded == 0) {
      ret.raw[i] = 0;
      continue;
    }
    const T rounded_f = ConvertScalarTo<T>(rounded);
    // Round to even
    if ((rounded & 1) &&
        ScalarAbs(rounded_f - v.raw[i]) == ConvertScalarTo<T>(0.5)) {
      ret.raw[i] = rounded - (signbit ? -1 : 1);
      continue;
    }
    ret.raw[i] = rounded;
  }
  return ret;
}

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 /*di32*/,
                                        VFromD<Rebind<double, DI32>> v) {
  using T = double;
  using TI = int32_t;
  const T k0 = ConvertScalarTo<T>(0);

  constexpr size_t N = HWY_MAX_LANES_D(DI32);

  const VFromD<Rebind<double, DI32>> abs = Abs(v);
  VFromD<DI32> ret;
  for (size_t i = 0; i < N; ++i) {
    const bool signbit = ScalarSignBit(v.raw[i]);

    // Check if too large to cast or NaN
    if (!(abs.raw[i] <= ConvertScalarTo<T>(LimitsMax<TI>()))) {
      ret.raw[i] = signbit ? LimitsMin<TI>() : LimitsMax<TI>();
      continue;
    }

    const T bias = ConvertScalarTo<T>(v.raw[i] < k0 ? -0.5 : 0.5);
    const TI rounded = ConvertScalarTo<TI>(v.raw[i] + bias);
    if (rounded == 0) {
      ret.raw[i] = 0;
      continue;
    }
    const T rounded_f = ConvertScalarTo<T>(rounded);
    // Round to even
    if ((rounded & 1) &&
        ScalarAbs(rounded_f - v.raw[i]) == ConvertScalarTo<T>(0.5)) {
      ret.raw[i] = rounded - (signbit ? -1 : 1);
      continue;
    }
    ret.raw[i] = rounded;
  }
  return ret;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> Trunc(Vec128<T, N> v) {
  using TI = MakeSigned<T>;
  const Vec128<T, N> abs = Abs(v);
  for (size_t i = 0; i < N; ++i) {
    if (!(abs.raw[i] <= MantissaEnd<T>())) {  // Huge or NaN
      continue;
    }
    const TI truncated = static_cast<TI>(v.raw[i]);
    if (truncated == 0) {
      v.raw[i] = v.raw[i] < 0 ? -T{0} : T{0};
      continue;
    }
    v.raw[i] = static_cast<T>(truncated);
  }
  return v;
}

// Toward +infinity, aka ceiling
template <typename Float, size_t N>
Vec128<Float, N> Ceil(Vec128<Float, N> v) {
  constexpr int kMantissaBits = MantissaBits<Float>();
  using Bits = MakeUnsigned<Float>;
  const Bits kExponentMask = MaxExponentField<Float>();
  const Bits kMantissaMask = MantissaMask<Float>();
  const Bits kBias = kExponentMask / 2;

  for (size_t i = 0; i < N; ++i) {
    const bool positive = v.raw[i] > Float(0.0);

    Bits bits = BitCastScalar<Bits>(v.raw[i]);

    const int exponent =
        static_cast<int>(((bits >> kMantissaBits) & kExponentMask) - kBias);
    // Already an integer.
    if (exponent >= kMantissaBits) continue;
    // |v| <= 1 => 0 or 1.
    if (exponent < 0) {
      v.raw[i] = positive ? Float{1} : Float{-0.0};
      continue;
    }

    const Bits mantissa_mask = kMantissaMask >> exponent;
    // Already an integer
    if ((bits & mantissa_mask) == 0) continue;

    // Clear fractional bits and round up
    if (positive) bits += (kMantissaMask + 1) >> exponent;
    bits &= ~mantissa_mask;

    v.raw[i] = BitCastScalar<Float>(bits);
  }
  return v;
}

// Toward -infinity, aka floor
template <typename Float, size_t N>
Vec128<Float, N> Floor(Vec128<Float, N> v) {
  constexpr int kMantissaBits = MantissaBits<Float>();
  using Bits = MakeUnsigned<Float>;
  const Bits kExponentMask = MaxExponentField<Float>();
  const Bits kMantissaMask = MantissaMask<Float>();
  const Bits kBias = kExponentMask / 2;

  for (size_t i = 0; i < N; ++i) {
    const bool negative = v.raw[i] < Float(0.0);

    Bits bits = BitCastScalar<Bits>(v.raw[i]);

    const int exponent =
        static_cast<int>(((bits >> kMantissaBits) & kExponentMask) - kBias);
    // Already an integer.
    if (exponent >= kMantissaBits) continue;
    // |v| <= 1 => -1 or 0.
    if (exponent < 0) {
      v.raw[i] = negative ? Float(-1.0) : Float(0.0);
      continue;
    }

    const Bits mantissa_mask = kMantissaMask >> exponent;
    // Already an integer
    if ((bits & mantissa_mask) == 0) continue;

    // Clear fractional bits and round down
    if (negative) bits += (kMantissaMask + 1) >> exponent;
    bits &= ~mantissa_mask;

    v.raw[i] = BitCastScalar<Float>(bits);
  }
  return v;
}

// ------------------------------ Floating-point classification

template <typename T, size_t N>
HWY_API Mask128<T, N> IsNaN(Vec128<T, N> v) {
  Mask128<T, N> ret;
  for (size_t i = 0; i < N; ++i) {
    // std::isnan returns false for 0x7F..FF in clang AVX3 builds, so DIY.
    ret.bits[i] = Mask128<T, N>::FromBool(ScalarIsNaN(v.raw[i]));
  }
  return ret;
}

// ================================================== COMPARE

template <typename T, size_t N>
HWY_API Mask128<T, N> operator==(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] == b.raw[i]);
  }
  return m;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator!=(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] != b.raw[i]);
  }
  return m;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> TestBit(Vec128<T, N> v, Vec128<T, N> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return (v & bit) == bit;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] < b.raw[i]);
  }
  return m;
}
template <typename T, size_t N>
HWY_API Mask128<T, N> operator>(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] > b.raw[i]);
  }
  return m;
}

template <typename T, size_t N>
HWY_API Mask128<T, N> operator<=(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] <= b.raw[i]);
  }
  return m;
}
template <typename T, size_t N>
HWY_API Mask128<T, N> operator>=(Vec128<T, N> a, Vec128<T, N> b) {
  Mask128<T, N> m;
  for (size_t i = 0; i < N; ++i) {
    m.bits[i] = Mask128<T, N>::FromBool(a.raw[i] >= b.raw[i]);
  }
  return m;
}

// ------------------------------ Lt128

// Only makes sense for full vectors of u64.
template <class D>
HWY_API MFromD<D> Lt128(D /* tag */, Vec128<uint64_t> a, Vec128<uint64_t> b) {
  const bool lt =
      (a.raw[1] < b.raw[1]) || (a.raw[1] == b.raw[1] && a.raw[0] < b.raw[0]);
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(lt);
  return ret;
}

template <class D>
HWY_API MFromD<D> Lt128Upper(D /* tag */, Vec128<uint64_t> a,
                             Vec128<uint64_t> b) {
  const bool lt = a.raw[1] < b.raw[1];
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(lt);
  return ret;
}

// ------------------------------ Eq128

// Only makes sense for full vectors of u64.
template <class D>
HWY_API MFromD<D> Eq128(D /* tag */, Vec128<uint64_t> a, Vec128<uint64_t> b) {
  const bool eq = a.raw[1] == b.raw[1] && a.raw[0] == b.raw[0];
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(eq);
  return ret;
}

template <class D>
HWY_API Mask128<uint64_t> Ne128(D /* tag */, Vec128<uint64_t> a,
                                Vec128<uint64_t> b) {
  const bool ne = a.raw[1] != b.raw[1] || a.raw[0] != b.raw[0];
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(ne);
  return ret;
}

template <class D>
HWY_API MFromD<D> Eq128Upper(D /* tag */, Vec128<uint64_t> a,
                             Vec128<uint64_t> b) {
  const bool eq = a.raw[1] == b.raw[1];
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(eq);
  return ret;
}

template <class D>
HWY_API MFromD<D> Ne128Upper(D /* tag */, Vec128<uint64_t> a,
                             Vec128<uint64_t> b) {
  const bool ne = a.raw[1] != b.raw[1];
  Mask128<uint64_t> ret;
  ret.bits[0] = ret.bits[1] = Mask128<uint64_t>::FromBool(ne);
  return ret;
}

// ------------------------------ Min128, Max128 (Lt128)

template <class D>
HWY_API VFromD<D> Min128(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128(d, a, b), a, b);
}

template <class D>
HWY_API VFromD<D> Max128(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128(d, b, a), a, b);
}

template <class D>
HWY_API VFromD<D> Min128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, a, b), a, b);
}

template <class D>
HWY_API VFromD<D> Max128Upper(D d, VFromD<D> a, VFromD<D> b) {
  return IfThenElse(Lt128Upper(d, b, a), a, b);
}

// ================================================== MEMORY

// ------------------------------ Load

template <class D>
HWY_API VFromD<D> Load(D d, const TFromD<D>* HWY_RESTRICT aligned) {
  VFromD<D> v;
  CopyBytes<d.MaxBytes()>(aligned, v.raw);  // copy from array
  return v;
}

template <class D>
HWY_API VFromD<D> MaskedLoad(MFromD<D> m, D d,
                             const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElseZero(m, LoadU(d, p));
}

template <class D>
HWY_API VFromD<D> MaskedLoadOr(VFromD<D> v, MFromD<D> m, D d,
                               const TFromD<D>* HWY_RESTRICT p) {
  return IfThenElse(m, LoadU(d, p), v);
}

template <class D>
HWY_API VFromD<D> LoadU(D d, const TFromD<D>* HWY_RESTRICT p) {
  return Load(d, p);
}

// In some use cases, "load single lane" is sufficient; otherwise avoid this.
template <class D>
HWY_API VFromD<D> LoadDup128(D d, const TFromD<D>* HWY_RESTRICT aligned) {
  return Load(d, aligned);
}

#ifdef HWY_NATIVE_LOAD_N
#undef HWY_NATIVE_LOAD_N
#else
#define HWY_NATIVE_LOAD_N
#endif

template <class D>
HWY_API VFromD<D> LoadN(D d, const TFromD<D>* HWY_RESTRICT p,
                        size_t max_lanes_to_load) {
  VFromD<D> v = Zero(d);
  const size_t N = Lanes(d);
  const size_t num_of_lanes_to_load = HWY_MIN(max_lanes_to_load, N);
  CopyBytes(p, v.raw, num_of_lanes_to_load * sizeof(TFromD<D>));
  return v;
}

template <class D>
HWY_API VFromD<D> LoadNOr(VFromD<D> no, D d, const TFromD<D>* HWY_RESTRICT p,
                          size_t max_lanes_to_load) {
  VFromD<D> v = no;
  const size_t N = Lanes(d);
  const size_t num_of_lanes_to_load = HWY_MIN(max_lanes_to_load, N);
  CopyBytes(p, v.raw, num_of_lanes_to_load * sizeof(TFromD<D>));
  return v;
}

// ------------------------------ Store

template <class D>
HWY_API void Store(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  CopyBytes<d.MaxBytes()>(v.raw, aligned);  // copy to array
}

template <class D>
HWY_API void StoreU(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p) {
  Store(v, d, p);
}

template <class D>
HWY_API void BlendedStore(VFromD<D> v, MFromD<D> m, D d,
                          TFromD<D>* HWY_RESTRICT p) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    if (m.bits[i]) p[i] = v.raw[i];
  }
}

#ifdef HWY_NATIVE_STORE_N
#undef HWY_NATIVE_STORE_N
#else
#define HWY_NATIVE_STORE_N
#endif

template <class D>
HWY_API void StoreN(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  const size_t N = Lanes(d);
  const size_t num_of_lanes_to_store = HWY_MIN(max_lanes_to_store, N);
  CopyBytes(v.raw, p, num_of_lanes_to_store * sizeof(TFromD<D>));
}

// ================================================== COMBINE

template <typename T, size_t N>
HWY_API Vec128<T, N / 2> LowerHalf(Vec128<T, N> v) {
  Vec128<T, N / 2> ret;
  CopyBytes<N / 2 * sizeof(T)>(v.raw, ret.raw);
  return ret;
}

template <class D>
HWY_API VFromD<D> LowerHalf(D /* tag */, VFromD<Twice<D>> v) {
  return LowerHalf(v);
}

template <class D>
HWY_API VFromD<D> UpperHalf(D d, VFromD<Twice<D>> v) {
  VFromD<D> ret;
  CopyBytes<d.MaxBytes()>(&v.raw[MaxLanes(d)], ret.raw);
  return ret;
}

template <class D>
HWY_API VFromD<D> ZeroExtendVector(D d, VFromD<Half<D>> v) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;  // zero-initialized
  CopyBytes<dh.MaxBytes()>(v.raw, ret.raw);
  return ret;
}

template <class D, class VH = VFromD<Half<D>>>
HWY_API VFromD<D> Combine(D d, VH hi_half, VH lo_half) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  CopyBytes<dh.MaxBytes()>(lo_half.raw, &ret.raw[0]);
  CopyBytes<dh.MaxBytes()>(hi_half.raw, &ret.raw[MaxLanes(dh)]);
  return ret;
}

template <class D>
HWY_API VFromD<D> ConcatLowerLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  CopyBytes<dh.MaxBytes()>(lo.raw, &ret.raw[0]);
  CopyBytes<dh.MaxBytes()>(hi.raw, &ret.raw[MaxLanes(dh)]);
  return ret;
}

template <class D>
HWY_API VFromD<D> ConcatUpperUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  CopyBytes<dh.MaxBytes()>(&lo.raw[MaxLanes(dh)], &ret.raw[0]);
  CopyBytes<dh.MaxBytes()>(&hi.raw[MaxLanes(dh)], &ret.raw[MaxLanes(dh)]);
  return ret;
}

template <class D>
HWY_API VFromD<D> ConcatLowerUpper(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  CopyBytes<dh.MaxBytes()>(&lo.raw[MaxLanes(dh)], &ret.raw[0]);
  CopyBytes<dh.MaxBytes()>(hi.raw, &ret.raw[MaxLanes(dh)]);
  return ret;
}

template <class D>
HWY_API VFromD<D> ConcatUpperLower(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  CopyBytes<dh.MaxBytes()>(lo.raw, &ret.raw[0]);
  CopyBytes<dh.MaxBytes()>(&hi.raw[MaxLanes(dh)], &ret.raw[MaxLanes(dh)]);
  return ret;
}

template <class D>
HWY_API VFromD<D> ConcatEven(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(dh); ++i) {
    ret.raw[i] = lo.raw[2 * i];
  }
  for (size_t i = 0; i < MaxLanes(dh); ++i) {
    ret.raw[MaxLanes(dh) + i] = hi.raw[2 * i];
  }
  return ret;
}

// 2023-11-23: workaround for incorrect codegen (reduction_test fails for
// SumsOf2 because PromoteOddTo, which uses ConcatOdd, returns zero).
#if HWY_ARCH_RISCV && HWY_TARGET == HWY_EMU128 && HWY_COMPILER_CLANG
#define HWY_EMU128_CONCAT_INLINE HWY_NOINLINE
#else
#define HWY_EMU128_CONCAT_INLINE HWY_API
#endif

template <class D>
HWY_EMU128_CONCAT_INLINE VFromD<D> ConcatOdd(D d, VFromD<D> hi, VFromD<D> lo) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(dh); ++i) {
    ret.raw[i] = lo.raw[2 * i + 1];
  }
  for (size_t i = 0; i < MaxLanes(dh); ++i) {
    ret.raw[MaxLanes(dh) + i] = hi.raw[2 * i + 1];
  }
  return ret;
}

// ------------------------------ CombineShiftRightBytes
template <int kBytes, class D>
HWY_API VFromD<D> CombineShiftRightBytes(D d, VFromD<D> hi, VFromD<D> lo) {
  VFromD<D> ret;
  const uint8_t* HWY_RESTRICT lo8 =
      reinterpret_cast<const uint8_t * HWY_RESTRICT>(lo.raw);
  uint8_t* HWY_RESTRICT ret8 =
      reinterpret_cast<uint8_t * HWY_RESTRICT>(ret.raw);
  CopyBytes<d.MaxBytes() - kBytes>(lo8 + kBytes, ret8);
  CopyBytes<kBytes>(hi.raw, ret8 + d.MaxBytes() - kBytes);
  return ret;
}

// ------------------------------ ShiftLeftBytes

template <int kBytes, class D>
HWY_API VFromD<D> ShiftLeftBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  VFromD<D> ret;
  uint8_t* HWY_RESTRICT ret8 =
      reinterpret_cast<uint8_t * HWY_RESTRICT>(ret.raw);
  ZeroBytes<kBytes>(ret8);
  CopyBytes<d.MaxBytes() - kBytes>(v.raw, ret8 + kBytes);
  return ret;
}

template <int kBytes, typename T, size_t N>
HWY_API Vec128<T, N> ShiftLeftBytes(Vec128<T, N> v) {
  return ShiftLeftBytes<kBytes>(DFromV<decltype(v)>(), v);
}

// ------------------------------ ShiftLeftLanes

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
template <int kBytes, class D>
HWY_API VFromD<D> ShiftRightBytes(D d, VFromD<D> v) {
  static_assert(0 <= kBytes && kBytes <= 16, "Invalid kBytes");
  VFromD<D> ret;
  const uint8_t* HWY_RESTRICT v8 =
      reinterpret_cast<const uint8_t * HWY_RESTRICT>(v.raw);
  uint8_t* HWY_RESTRICT ret8 =
      reinterpret_cast<uint8_t * HWY_RESTRICT>(ret.raw);
  CopyBytes<d.MaxBytes() - kBytes>(v8 + kBytes, ret8);
  ZeroBytes<kBytes>(ret8 + d.MaxBytes() - kBytes);
  return ret;
}

// ------------------------------ ShiftRightLanes
template <int kLanes, class D>
HWY_API VFromD<D> ShiftRightLanes(D d, VFromD<D> v) {
  const Repartition<uint8_t, decltype(d)> d8;
  constexpr size_t kBytes = kLanes * sizeof(TFromD<D>);
  return BitCast(d, ShiftRightBytes<kBytes>(d8, BitCast(d8, v)));
}

// ------------------------------ Tuples, PromoteEvenTo/PromoteOddTo
#include "hwy/ops/inside-inl.h"

// ------------------------------ LoadInterleaved2/3/4

// Per-target flag to prevent generic_ops-inl.h from defining LoadInterleaved2.
// We implement those here because scalar code is likely faster than emulation
// via shuffles.
#ifdef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_INTERLEAVED
#endif

// Same for Load/StoreInterleaved of special floats.
#ifdef HWY_NATIVE_LOAD_STORE_SPECIAL_FLOAT_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_SPECIAL_FLOAT_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_SPECIAL_FLOAT_INTERLEAVED
#endif

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved2(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1) {
  alignas(16) T buf0[MaxLanes(d)];
  alignas(16) T buf1[MaxLanes(d)];
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    buf0[i] = *unaligned++;
    buf1[i] = *unaligned++;
  }
  v0 = Load(d, buf0);
  v1 = Load(d, buf1);
}

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved3(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2) {
  alignas(16) T buf0[MaxLanes(d)];
  alignas(16) T buf1[MaxLanes(d)];
  alignas(16) T buf2[MaxLanes(d)];
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    buf0[i] = *unaligned++;
    buf1[i] = *unaligned++;
    buf2[i] = *unaligned++;
  }
  v0 = Load(d, buf0);
  v1 = Load(d, buf1);
  v2 = Load(d, buf2);
}

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved4(D d, const T* HWY_RESTRICT unaligned,
                              VFromD<D>& v0, VFromD<D>& v1, VFromD<D>& v2,
                              VFromD<D>& v3) {
  alignas(16) T buf0[MaxLanes(d)];
  alignas(16) T buf1[MaxLanes(d)];
  alignas(16) T buf2[MaxLanes(d)];
  alignas(16) T buf3[MaxLanes(d)];
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    buf0[i] = *unaligned++;
    buf1[i] = *unaligned++;
    buf2[i] = *unaligned++;
    buf3[i] = *unaligned++;
  }
  v0 = Load(d, buf0);
  v1 = Load(d, buf1);
  v2 = Load(d, buf2);
  v3 = Load(d, buf3);
}

// ------------------------------ StoreInterleaved2/3/4

template <class D>
HWY_API void StoreInterleaved2(VFromD<D> v0, VFromD<D> v1, D d,
                               TFromD<D>* HWY_RESTRICT unaligned) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    *unaligned++ = v0.raw[i];
    *unaligned++ = v1.raw[i];
  }
}

template <class D>
HWY_API void StoreInterleaved3(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2, D d,
                               TFromD<D>* HWY_RESTRICT unaligned) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    *unaligned++ = v0.raw[i];
    *unaligned++ = v1.raw[i];
    *unaligned++ = v2.raw[i];
  }
}

template <class D>
HWY_API void StoreInterleaved4(VFromD<D> v0, VFromD<D> v1, VFromD<D> v2,
                               VFromD<D> v3, D d,
                               TFromD<D>* HWY_RESTRICT unaligned) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    *unaligned++ = v0.raw[i];
    *unaligned++ = v1.raw[i];
    *unaligned++ = v2.raw[i];
    *unaligned++ = v3.raw[i];
  }
}

// ------------------------------ Stream
template <class D>
HWY_API void Stream(VFromD<D> v, D d, TFromD<D>* HWY_RESTRICT aligned) {
  Store(v, d, aligned);
}

// ------------------------------ Scatter in generic_ops-inl.h
// ------------------------------ Gather in generic_ops-inl.h

// ================================================== CONVERT

// ConvertTo and DemoteTo with floating-point input and integer output truncate
// (rounding toward zero).

namespace detail {

template <class ToT, class FromT>
HWY_INLINE ToT CastValueForF2IConv(FromT val) {
  // Prevent ubsan errors when converting float to narrower integer

  using FromTU = MakeUnsigned<FromT>;
  using ToTU = MakeUnsigned<ToT>;

  constexpr unsigned kMaxExpField =
      static_cast<unsigned>(MaxExponentField<FromT>());
  constexpr unsigned kExpBias = kMaxExpField >> 1;
  constexpr unsigned kMinOutOfRangeExpField = static_cast<unsigned>(HWY_MIN(
      kExpBias + sizeof(ToT) * 8 - static_cast<unsigned>(IsSigned<ToT>()),
      kMaxExpField));

  // If ToT is signed, compare only the exponent bits of val against
  // kMinOutOfRangeExpField.
  //
  // Otherwise, if ToT is unsigned, compare the sign bit plus exponent bits of
  // val against kMinOutOfRangeExpField as a negative value is outside of the
  // range of an unsigned integer type.
  const FromT val_to_compare =
      static_cast<FromT>(IsSigned<ToT>() ? ScalarAbs(val) : val);

  // val is within the range of ToT if
  // (BitCastScalar<FromTU>(val_to_compare) >> MantissaBits<FromT>()) is less
  // than kMinOutOfRangeExpField
  //
  // Otherwise, val is either outside of the range of ToT or equal to
  // LimitsMin<ToT>() if
  // (BitCastScalar<FromTU>(val_to_compare) >> MantissaBits<FromT>()) is greater
  // than or equal to kMinOutOfRangeExpField.

  return (static_cast<unsigned>(BitCastScalar<FromTU>(val_to_compare) >>
                                MantissaBits<FromT>()) < kMinOutOfRangeExpField)
             ? static_cast<ToT>(val)
             : static_cast<ToT>(static_cast<ToTU>(LimitsMax<ToT>()) +
                                static_cast<ToTU>(ScalarSignBit(val)));
}

template <class ToT, class ToTypeTag, class FromT>
HWY_INLINE ToT CastValueForPromoteTo(ToTypeTag /* to_type_tag */, FromT val) {
  return ConvertScalarTo<ToT>(val);
}

template <class ToT>
HWY_INLINE ToT CastValueForPromoteTo(hwy::SignedTag /*to_type_tag*/,
                                     float val) {
  return CastValueForF2IConv<ToT>(val);
}

template <class ToT>
HWY_INLINE ToT CastValueForPromoteTo(hwy::UnsignedTag /*to_type_tag*/,
                                     float val) {
  return CastValueForF2IConv<ToT>(val);
}
// If val is within the range of ToT, CastValueForInRangeF2IConv<ToT>(val)
// returns static_cast<ToT>(val)
//
// Otherwise, CastValueForInRangeF2IConv<ToT>(val) returns an
// implementation-defined result if val is not within the range of ToT.
template <class ToT, class FromT>
HWY_INLINE ToT CastValueForInRangeF2IConv(FromT val) {
  // Prevent ubsan errors when converting float to narrower integer

  using FromTU = MakeUnsigned<FromT>;

  constexpr unsigned kMaxExpField =
      static_cast<unsigned>(MaxExponentField<FromT>());
  constexpr unsigned kExpBias = kMaxExpField >> 1;
  constexpr unsigned kMinOutOfRangeExpField = static_cast<unsigned>(HWY_MIN(
      kExpBias + sizeof(ToT) * 8 - static_cast<unsigned>(IsSigned<ToT>()),
      kMaxExpField));

  // If ToT is signed, compare only the exponent bits of val against
  // kMinOutOfRangeExpField.
  //
  // Otherwise, if ToT is unsigned, compare the sign bit plus exponent bits of
  // val against kMinOutOfRangeExpField as a negative value is outside of the
  // range of an unsigned integer type.
  const FromT val_to_compare =
      static_cast<FromT>(IsSigned<ToT>() ? ScalarAbs(val) : val);

  // val is within the range of ToT if
  // (BitCastScalar<FromTU>(val_to_compare) >> MantissaBits<FromT>()) is less
  // than kMinOutOfRangeExpField
  //
  // Otherwise, val is either outside of the range of ToT or equal to
  // LimitsMin<ToT>() if
  // (BitCastScalar<FromTU>(val_to_compare) >> MantissaBits<FromT>()) is greater
  // than or equal to kMinOutOfRangeExpField.

  return (static_cast<unsigned>(BitCastScalar<FromTU>(val_to_compare) >>
                                MantissaBits<FromT>()) < kMinOutOfRangeExpField)
             ? static_cast<ToT>(val)
             : static_cast<ToT>(LimitsMin<ToT>());
}

}  // namespace detail

template <class DTo, typename TFrom, HWY_IF_NOT_SPECIAL_FLOAT(TFrom)>
HWY_API VFromD<DTo> PromoteTo(DTo d, Vec128<TFrom, HWY_MAX_LANES_D(DTo)> from) {
  static_assert(sizeof(TFromD<DTo>) > sizeof(TFrom), "Not promoting");
  VFromD<DTo> ret;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    // For bits Y > X, floatX->floatY and intX->intY are always representable.
    ret.raw[i] = detail::CastValueForPromoteTo<TFromD<DTo>>(
        hwy::TypeTag<TFromD<DTo>>(), from.raw[i]);
  }
  return ret;
}

#ifdef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#endif

template <class D64, HWY_IF_UI64_D(D64)>
HWY_API VFromD<D64> PromoteInRangeTo(D64 d64, VFromD<Rebind<float, D64>> v) {
  VFromD<D64> ret;
  for (size_t i = 0; i < MaxLanes(d64); ++i) {
    ret.raw[i] = detail::CastValueForInRangeF2IConv<TFromD<D64>>(v.raw[i]);
  }
  return ret;
}

// MSVC 19.10 cannot deduce the argument type if HWY_IF_FLOAT(TFrom) is here,
// so we overload for TFrom=double and ToT={float,int32_t}.
template <class D, HWY_IF_F32_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<double, D>> from) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    // Prevent ubsan errors when converting float to narrower integer/float
    if (ScalarIsInf(from.raw[i]) ||
        ScalarAbs(from.raw[i]) > static_cast<double>(HighestValue<float>())) {
      ret.raw[i] = ScalarSignBit(from.raw[i]) ? LowestValue<float>()
                                              : HighestValue<float>();
      continue;
    }
    ret.raw[i] = static_cast<float>(from.raw[i]);
  }
  return ret;
}
template <class D, HWY_IF_UI32_D(D)>
HWY_API VFromD<D> DemoteTo(D d, VFromD<Rebind<double, D>> from) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    // Prevent ubsan errors when converting double to narrower integer/int32_t
    ret.raw[i] = detail::CastValueForF2IConv<TFromD<D>>(from.raw[i]);
  }
  return ret;
}

template <class DTo, typename TFrom, size_t N, HWY_IF_SIGNED(TFrom),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DTo>)>
HWY_API VFromD<DTo> DemoteTo(DTo /* tag */, Vec128<TFrom, N> from) {
  using TTo = TFromD<DTo>;
  static_assert(sizeof(TTo) < sizeof(TFrom), "Not demoting");

  VFromD<DTo> ret;
  for (size_t i = 0; i < N; ++i) {
    // Int to int: choose closest value in ToT to `from` (avoids UB)
    from.raw[i] =
        HWY_MIN(HWY_MAX(LimitsMin<TTo>(), from.raw[i]), LimitsMax<TTo>());
    ret.raw[i] = static_cast<TTo>(from.raw[i]);
  }
  return ret;
}

// Disable the default unsigned to signed DemoteTo/ReorderDemote2To
// implementations in generic_ops-inl.h on EMU128 as the EMU128 target has
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

template <class DTo, typename TFrom, size_t N, HWY_IF_UNSIGNED(TFrom),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(DTo)>
HWY_API VFromD<DTo> DemoteTo(DTo /* tag */, Vec128<TFrom, N> from) {
  using TTo = TFromD<DTo>;
  static_assert(sizeof(TTo) < sizeof(TFrom), "Not demoting");

  const auto max = static_cast<MakeUnsigned<TTo>>(LimitsMax<TTo>());

  VFromD<DTo> ret;
  for (size_t i = 0; i < N; ++i) {
    // Int to int: choose closest value in ToT to `from` (avoids UB)
    ret.raw[i] = static_cast<TTo>(HWY_MIN(from.raw[i], max));
  }
  return ret;
}

template <class DTo, typename TFrom, size_t N, HWY_IF_UI64(TFrom),
          HWY_IF_F32_D(DTo)>
HWY_API VFromD<DTo> DemoteTo(DTo /* tag */, Vec128<TFrom, N> from) {
  using TTo = TFromD<DTo>;
  static_assert(sizeof(TTo) < sizeof(TFrom), "Not demoting");

  VFromD<DTo> ret;
  for (size_t i = 0; i < N; ++i) {
    // int64_t/uint64_t to float: okay to cast to float as an int64_t/uint64_t
    // value is always within the range of a float
    ret.raw[i] = static_cast<TTo>(from.raw[i]);
  }
  return ret;
}

template <class DBF16, HWY_IF_BF16_D(DBF16), class VF32>
HWY_API VFromD<DBF16> ReorderDemote2To(DBF16 dbf16, VF32 a, VF32 b) {
  const Repartition<uint32_t, decltype(dbf16)> du32;
  const VFromD<decltype(du32)> b_in_lower = ShiftRight<16>(BitCast(du32, b));
  // Avoid OddEven - we want the upper half of `a` even on big-endian systems.
  const VFromD<decltype(du32)> a_mask = Set(du32, 0xFFFF0000);
  return BitCast(dbf16, IfVecThenElse(a_mask, BitCast(du32, a), b_in_lower));
}

template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DN>), class V,
          HWY_IF_SIGNED_V(V), HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          HWY_IF_LANES_D(DN, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const RepartitionToWide<decltype(dn)> dw;
  const size_t NW = Lanes(dw);
  using TN = TFromD<DN>;
  const TN min = LimitsMin<TN>();
  const TN max = LimitsMax<TN>();
  VFromD<DN> ret;
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[i] = static_cast<TN>(HWY_MIN(HWY_MAX(min, a.raw[i]), max));
  }
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[NW + i] = static_cast<TN>(HWY_MIN(HWY_MAX(min, b.raw[i]), max));
  }
  return ret;
}

template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(DN), class V,
          HWY_IF_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          HWY_IF_LANES_D(DN, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<DN> ReorderDemote2To(DN dn, V a, V b) {
  const RepartitionToWide<decltype(dn)> dw;
  const size_t NW = Lanes(dw);
  using TN = TFromD<DN>;
  using TN_U = MakeUnsigned<TN>;
  const TN_U max = static_cast<TN_U>(LimitsMax<TN>());
  VFromD<DN> ret;
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[i] = static_cast<TN>(HWY_MIN(a.raw[i], max));
  }
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[NW + i] = static_cast<TN>(HWY_MIN(b.raw[i], max));
  }
  return ret;
}

template <class DN, HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DN>), class V,
          HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          HWY_IF_LANES_D(DN, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<DN> OrderedDemote2To(DN dn, V a, V b) {
  return ReorderDemote2To(dn, a, b);
}

template <class DN, HWY_IF_SPECIAL_FLOAT_D(DN), class V,
          HWY_IF_F32_D(DFromV<V>),
          HWY_IF_LANES_D(DN, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<DN> OrderedDemote2To(DN dn, V a, V b) {
  const size_t NW = Lanes(dn) / 2;
  using TN = TFromD<DN>;
  VFromD<DN> ret;
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[i] = ConvertScalarTo<TN>(a.raw[i]);
  }
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[NW + i] = ConvertScalarTo<TN>(b.raw[i]);
  }
  return ret;
}

namespace detail {

HWY_INLINE void StoreU16ToF16(const uint16_t val,
                              hwy::float16_t* HWY_RESTRICT to) {
  CopySameSize(&val, to);
}

HWY_INLINE uint16_t U16FromF16(const hwy::float16_t* HWY_RESTRICT from) {
  uint16_t bits16;
  CopySameSize(from, &bits16);
  return bits16;
}

}  // namespace detail

template <class D, HWY_IF_F32_D(D), size_t N>
HWY_API VFromD<D> PromoteTo(D /* tag */, Vec128<bfloat16_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = F32FromBF16(v.raw[i]);
  }
  return ret;
}

#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

template <class D, HWY_IF_BF16_D(D), size_t N>
HWY_API VFromD<D> DemoteTo(D /* tag */, Vec128<float, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = BF16FromF32(v.raw[i]);
  }
  return ret;
}

#ifdef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#endif

template <class D32, HWY_IF_UI32_D(D32)>
HWY_API VFromD<D32> DemoteInRangeTo(D32 d32, VFromD<Rebind<double, D32>> v) {
  VFromD<D32> ret;
  for (size_t i = 0; i < MaxLanes(d32); ++i) {
    ret.raw[i] = detail::CastValueForInRangeF2IConv<TFromD<D32>>(v.raw[i]);
  }
  return ret;
}

// Tag dispatch instead of SFINAE for MSVC 2017 compatibility
namespace detail {

template <typename TFrom, typename DTo>
HWY_API VFromD<DTo> ConvertTo(hwy::FloatTag /*tag*/, DTo /*tag*/,
                              Vec128<TFrom, HWY_MAX_LANES_D(DTo)> from) {
  using ToT = TFromD<DTo>;
  static_assert(sizeof(ToT) == sizeof(TFrom), "Should have same size");
  VFromD<DTo> ret;
  constexpr size_t N = HWY_MAX_LANES_D(DTo);

  for (size_t i = 0; i < N; ++i) {
    // float## -> int##: return closest representable value
    ret.raw[i] = CastValueForF2IConv<ToT>(from.raw[i]);
  }
  return ret;
}

template <typename TFrom, typename DTo>
HWY_API VFromD<DTo> ConvertTo(hwy::NonFloatTag /*tag*/, DTo /* tag */,
                              Vec128<TFrom, HWY_MAX_LANES_D(DTo)> from) {
  using ToT = TFromD<DTo>;
  static_assert(sizeof(ToT) == sizeof(TFrom), "Should have same size");
  VFromD<DTo> ret;
  constexpr size_t N = HWY_MAX_LANES_D(DTo);
  for (size_t i = 0; i < N; ++i) {
    // int## -> float##: no check needed
    ret.raw[i] = static_cast<ToT>(from.raw[i]);
  }
  return ret;
}

}  // namespace detail

template <class DTo, typename TFrom>
HWY_API VFromD<DTo> ConvertTo(DTo d, Vec128<TFrom, HWY_MAX_LANES_D(DTo)> from) {
  return detail::ConvertTo(hwy::IsFloatTag<TFrom>(), d, from);
}

#ifdef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#undef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#else
#define HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#endif

template <class DI, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(DI),
          HWY_IF_T_SIZE_ONE_OF_D(DI, (1 << 4) | (1 << 8))>
HWY_API VFromD<DI> ConvertInRangeTo(DI di, VFromD<RebindToFloat<DI>> v) {
  VFromD<DI> ret;
  for (size_t i = 0; i < MaxLanes(di); i++) {
    ret.raw[i] = detail::CastValueForInRangeF2IConv<TFromD<DI>>(v.raw[i]);
  }
  return ret;
}

template <size_t N>
HWY_API Vec128<uint8_t, N> U8FromU32(Vec128<uint32_t, N> v) {
  return DemoteTo(Simd<uint8_t, N, 0>(), v);
}

// ------------------------------ Truncations

template <class D, HWY_IF_U8_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint64_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint8_t>(v.raw[i] & 0xFF);
  }
  return ret;
}

template <class D, HWY_IF_U16_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint64_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint16_t>(v.raw[i] & 0xFFFF);
  }
  return ret;
}

template <class D, HWY_IF_U32_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint64_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint32_t>(v.raw[i] & 0xFFFFFFFFu);
  }
  return ret;
}

template <class D, HWY_IF_U8_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint32_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint8_t>(v.raw[i] & 0xFF);
  }
  return ret;
}

template <class D, HWY_IF_U16_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint32_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint16_t>(v.raw[i] & 0xFFFF);
  }
  return ret;
}

template <class D, HWY_IF_U8_D(D), size_t N>
HWY_API VFromD<D> TruncateTo(D /* tag */, Vec128<uint16_t, N> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = static_cast<uint8_t>(v.raw[i] & 0xFF);
  }
  return ret;
}

#ifdef HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#undef HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#else
#define HWY_NATIVE_ORDERED_TRUNCATE_2_TO
#endif

template <class DN, HWY_IF_UNSIGNED_D(DN), class V, HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_V(V, sizeof(TFromD<DN>) * 2),
          HWY_IF_LANES_D(DN, HWY_MAX_LANES_D(DFromV<V>) * 2)>
HWY_API VFromD<DN> OrderedTruncate2To(DN dn, V a, V b) {
  const RepartitionToWide<decltype(dn)> dw;
  const size_t NW = Lanes(dw);
  using TW = TFromD<decltype(dw)>;
  using TN = TFromD<decltype(dn)>;
  VFromD<DN> ret;
  constexpr TW max_val{LimitsMax<TN>()};

  for (size_t i = 0; i < NW; ++i) {
    ret.raw[i] = static_cast<TN>(a.raw[i] & max_val);
  }
  for (size_t i = 0; i < NW; ++i) {
    ret.raw[NW + i] = static_cast<TN>(b.raw[i] & max_val);
  }
  return ret;
}

// ================================================== SWIZZLE

template <typename T, size_t N>
HWY_API T GetLane(Vec128<T, N> v) {
  return v.raw[0];
}

template <typename T, size_t N>
HWY_API Vec128<T, N> InsertLane(Vec128<T, N> v, size_t i, T t) {
  v.raw[i] = t;
  return v;
}

template <typename T, size_t N>
HWY_API T ExtractLane(Vec128<T, N> v, size_t i) {
  return v.raw[i];
}

template <typename T, size_t N>
HWY_API Vec128<T, N> DupEven(Vec128<T, N> v) {
  for (size_t i = 0; i < N; i += 2) {
    v.raw[i + 1] = v.raw[i];
  }
  return v;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> DupOdd(Vec128<T, N> v) {
  for (size_t i = 0; i < N; i += 2) {
    v.raw[i] = v.raw[i + 1];
  }
  return v;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> OddEven(Vec128<T, N> odd, Vec128<T, N> even) {
  for (size_t i = 0; i < N; i += 2) {
    odd.raw[i] = even.raw[i];
  }
  return odd;
}

template <class D>
HWY_API VFromD<D> InterleaveEven(D /*d*/, VFromD<D> a, VFromD<D> b) {
  constexpr size_t N = HWY_MAX_LANES_D(D);
  for (size_t i = 1; i < N; i += 2) {
    a.raw[i] = b.raw[i - 1];
  }
  return a;
}

template <class D>
HWY_API VFromD<D> InterleaveOdd(D /*d*/, VFromD<D> a, VFromD<D> b) {
  constexpr size_t N = HWY_MAX_LANES_D(D);
  for (size_t i = 1; i < N; i += 2) {
    b.raw[i - 1] = a.raw[i];
  }
  return b;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> OddEvenBlocks(Vec128<T, N> /* odd */, Vec128<T, N> even) {
  return even;
}

// ------------------------------ SwapAdjacentBlocks
template <typename T, size_t N>
HWY_API Vec128<T, N> SwapAdjacentBlocks(Vec128<T, N> v) {
  return v;
}

// ------------------------------ TableLookupLanes

// Returned by SetTableIndices for use by TableLookupLanes.
template <typename T, size_t N>
struct Indices128 {
  MakeSigned<T> raw[N];
};

template <class D, typename TI, size_t N>
HWY_API Indices128<TFromD<D>, N> IndicesFromVec(D d, Vec128<TI, N> vec) {
  static_assert(sizeof(TFromD<D>) == sizeof(TI), "Index/lane size must match");
  Indices128<TFromD<D>, N> ret;
  CopyBytes<d.MaxBytes()>(vec.raw, ret.raw);
  return ret;
}

template <class D, typename TI>
HWY_API Indices128<TFromD<D>, HWY_MAX_LANES_D(D)> SetTableIndices(
    D d, const TI* idx) {
  return IndicesFromVec(d, LoadU(Rebind<TI, D>(), idx));
}

template <typename T, size_t N>
HWY_API Vec128<T, N> TableLookupLanes(Vec128<T, N> v, Indices128<T, N> idx) {
  Vec128<T, N> ret;
  for (size_t i = 0; i < N; ++i) {
    ret.raw[i] = v.raw[idx.raw[i]];
  }
  return ret;
}

template <typename T, size_t N>
HWY_API Vec128<T, N> TwoTablesLookupLanes(Vec128<T, N> a, Vec128<T, N> b,
                                          Indices128<T, N> idx) {
  using TI = MakeSigned<T>;
  Vec128<T, N> ret;
  constexpr TI kVecLaneIdxMask = static_cast<TI>(N - 1);
  for (size_t i = 0; i < N; ++i) {
    const auto src_idx = idx.raw[i];
    const auto masked_src_lane_idx = src_idx & kVecLaneIdxMask;
    ret.raw[i] = (src_idx < static_cast<TI>(N)) ? a.raw[masked_src_lane_idx]
                                                : b.raw[masked_src_lane_idx];
  }
  return ret;
}

// ------------------------------ ReverseBlocks
template <class D>
HWY_API VFromD<D> ReverseBlocks(D /* tag */, VFromD<D> v) {
  return v;  // Single block: no change
}

// ------------------------------ Reverse

template <class D>
HWY_API VFromD<D> Reverse(D d, VFromD<D> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    ret.raw[i] = v.raw[MaxLanes(d) - 1 - i];
  }
  return ret;
}

// Per-target flag to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

template <class D>
HWY_API VFromD<D> Reverse2(D d, VFromD<D> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); i += 2) {
    ret.raw[i + 0] = v.raw[i + 1];
    ret.raw[i + 1] = v.raw[i + 0];
  }
  return ret;
}

template <class D>
HWY_API VFromD<D> Reverse4(D d, VFromD<D> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); i += 4) {
    ret.raw[i + 0] = v.raw[i + 3];
    ret.raw[i + 1] = v.raw[i + 2];
    ret.raw[i + 2] = v.raw[i + 1];
    ret.raw[i + 3] = v.raw[i + 0];
  }
  return ret;
}

template <class D>
HWY_API VFromD<D> Reverse8(D d, VFromD<D> v) {
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(d); i += 8) {
    ret.raw[i + 0] = v.raw[i + 7];
    ret.raw[i + 1] = v.raw[i + 6];
    ret.raw[i + 2] = v.raw[i + 5];
    ret.raw[i + 3] = v.raw[i + 4];
    ret.raw[i + 4] = v.raw[i + 3];
    ret.raw[i + 5] = v.raw[i + 2];
    ret.raw[i + 6] = v.raw[i + 1];
    ret.raw[i + 7] = v.raw[i + 0];
  }
  return ret;
}

// ------------------------------ SlideUpLanes

template <class D>
HWY_API VFromD<D> SlideUpLanes(D d, VFromD<D> v, size_t amt) {
  VFromD<D> ret = Zero(d);
  constexpr size_t N = HWY_MAX_LANES_D(D);
  const size_t clamped_amt = HWY_MIN(amt, N);
  CopyBytes(v.raw, ret.raw + clamped_amt,
            (N - clamped_amt) * sizeof(TFromD<D>));
  return ret;
}

// ------------------------------ SlideDownLanes

template <class D>
HWY_API VFromD<D> SlideDownLanes(D d, VFromD<D> v, size_t amt) {
  VFromD<D> ret = Zero(d);
  constexpr size_t N = HWY_MAX_LANES_D(D);
  const size_t clamped_amt = HWY_MIN(amt, N);
  CopyBytes(v.raw + clamped_amt, ret.raw,
            (N - clamped_amt) * sizeof(TFromD<D>));
  return ret;
}

// ================================================== BLOCKWISE

// ------------------------------ Shuffle*

// Swap 32-bit halves in 64-bit halves.
template <typename T, size_t N>
HWY_API Vec128<T, N> Shuffle2301(Vec128<T, N> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit");
  static_assert(N == 2 || N == 4, "Does not make sense for N=1");
  return Reverse2(DFromV<decltype(v)>(), v);
}

// Swap 64-bit halves
template <typename T>
HWY_API Vec128<T> Shuffle1032(Vec128<T> v) {
  static_assert(sizeof(T) == 4, "Only for 32-bit");
  Vec128<T> ret;
  ret.raw[3] = v.raw[1];
  ret.raw[2] = v.raw[0];
  ret.raw[1] = v.raw[3];
  ret.raw[0] = v.raw[2];
  return ret;
}
template <typename T>
HWY_API Vec128<T> Shuffle01(Vec128<T> v) {
  static_assert(sizeof(T) == 8, "Only for 64-bit");
  return Reverse2(DFromV<decltype(v)>(), v);
}

// Rotate right 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle0321(Vec128<T> v) {
  Vec128<T> ret;
  ret.raw[3] = v.raw[0];
  ret.raw[2] = v.raw[3];
  ret.raw[1] = v.raw[2];
  ret.raw[0] = v.raw[1];
  return ret;
}

// Rotate left 32 bits
template <typename T>
HWY_API Vec128<T> Shuffle2103(Vec128<T> v) {
  Vec128<T> ret;
  ret.raw[3] = v.raw[2];
  ret.raw[2] = v.raw[1];
  ret.raw[1] = v.raw[0];
  ret.raw[0] = v.raw[3];
  return ret;
}

template <typename T>
HWY_API Vec128<T> Shuffle0123(Vec128<T> v) {
  return Reverse4(DFromV<decltype(v)>(), v);
}

// ------------------------------ Broadcast
template <int kLane, typename T, size_t N>
HWY_API Vec128<T, N> Broadcast(Vec128<T, N> v) {
  for (size_t i = 0; i < N; ++i) {
    v.raw[i] = v.raw[kLane];
  }
  return v;
}

// ------------------------------ TableLookupBytes, TableLookupBytesOr0

template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytes(Vec128<T, N> v,
                                        Vec128<TI, NI> indices) {
  const uint8_t* HWY_RESTRICT v_bytes =
      reinterpret_cast<const uint8_t * HWY_RESTRICT>(v.raw);
  const uint8_t* HWY_RESTRICT idx_bytes =
      reinterpret_cast<const uint8_t*>(indices.raw);
  Vec128<TI, NI> ret;
  uint8_t* HWY_RESTRICT ret_bytes =
      reinterpret_cast<uint8_t * HWY_RESTRICT>(ret.raw);
  for (size_t i = 0; i < NI * sizeof(TI); ++i) {
    const size_t idx = idx_bytes[i];
    // Avoid out of bounds reads.
    ret_bytes[i] = idx < sizeof(T) * N ? v_bytes[idx] : 0;
  }
  return ret;
}

template <typename T, size_t N, typename TI, size_t NI>
HWY_API Vec128<TI, NI> TableLookupBytesOr0(Vec128<T, N> v,
                                           Vec128<TI, NI> indices) {
  // Same as TableLookupBytes, which already returns 0 if out of bounds.
  return TableLookupBytes(v, indices);
}

// ------------------------------ InterleaveLower/InterleaveUpper

template <typename T, size_t N>
HWY_API Vec128<T, N> InterleaveLower(Vec128<T, N> a, Vec128<T, N> b) {
  Vec128<T, N> ret;
  for (size_t i = 0; i < N / 2; ++i) {
    ret.raw[2 * i + 0] = a.raw[i];
    ret.raw[2 * i + 1] = b.raw[i];
  }
  return ret;
}

// Additional overload for the optional tag.
template <class D>
HWY_API VFromD<D> InterleaveLower(D /* tag */, VFromD<D> a, VFromD<D> b) {
  return InterleaveLower(a, b);
}

template <class D>
HWY_API VFromD<D> InterleaveUpper(D d, VFromD<D> a, VFromD<D> b) {
  const Half<decltype(d)> dh;
  VFromD<D> ret;
  for (size_t i = 0; i < MaxLanes(dh); ++i) {
    ret.raw[2 * i + 0] = a.raw[MaxLanes(dh) + i];
    ret.raw[2 * i + 1] = b.raw[MaxLanes(dh) + i];
  }
  return ret;
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

// ================================================== MASK

template <class D>
HWY_API bool AllFalse(D d, MFromD<D> mask) {
  typename MFromD<D>::Raw or_sum = 0;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    or_sum |= mask.bits[i];
  }
  return or_sum == 0;
}

template <class D>
HWY_API bool AllTrue(D d, MFromD<D> mask) {
  constexpr uint64_t kAll = LimitsMax<typename MFromD<D>::Raw>();
  uint64_t and_sum = kAll;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    and_sum &= mask.bits[i];
  }
  return and_sum == kAll;
}

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D>
HWY_API MFromD<D> LoadMaskBits(D d, const uint8_t* HWY_RESTRICT bits) {
  MFromD<D> m;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    const size_t bit = size_t{1} << (i & 7);
    const size_t idx_byte = i >> 3;
    m.bits[i] = MFromD<D>::FromBool((bits[idx_byte] & bit) != 0);
  }
  return m;
}

template <class D>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D d, unsigned mask_bits) {
  MFromD<D> m;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    m.bits[i] = MFromD<D>::FromBool(((mask_bits >> i) & 1u) != 0);
  }
  return m;
}

// `p` points to at least 8 writable bytes.
template <class D>
HWY_API size_t StoreMaskBits(D d, MFromD<D> mask, uint8_t* bits) {
  bits[0] = 0;
  if (MaxLanes(d) > 8) bits[1] = 0;  // MaxLanes(d) <= 16, so max two bytes
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    const size_t bit = size_t{1} << (i & 7);
    const size_t idx_byte = i >> 3;
    if (mask.bits[i]) {
      bits[idx_byte] = static_cast<uint8_t>(bits[idx_byte] | bit);
    }
  }
  return MaxLanes(d) > 8 ? 2 : 1;
}

template <class D>
HWY_API size_t CountTrue(D d, MFromD<D> mask) {
  size_t count = 0;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    count += mask.bits[i] != 0;
  }
  return count;
}

template <class D>
HWY_API size_t FindKnownFirstTrue(D d, MFromD<D> mask) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    if (mask.bits[i] != 0) return i;
  }
  HWY_DASSERT(false);
  return 0;
}

template <class D>
HWY_API intptr_t FindFirstTrue(D d, MFromD<D> mask) {
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    if (mask.bits[i] != 0) return static_cast<intptr_t>(i);
  }
  return intptr_t{-1};
}

template <class D>
HWY_API size_t FindKnownLastTrue(D d, MFromD<D> mask) {
  for (intptr_t i = static_cast<intptr_t>(MaxLanes(d) - 1); i >= 0; i--) {
    if (mask.bits[i] != 0) return static_cast<size_t>(i);
  }
  HWY_DASSERT(false);
  return 0;
}

template <class D>
HWY_API intptr_t FindLastTrue(D d, MFromD<D> mask) {
  for (intptr_t i = static_cast<intptr_t>(MaxLanes(d) - 1); i >= 0; i--) {
    if (mask.bits[i] != 0) return i;
  }
  return intptr_t{-1};
}

// ------------------------------ Compress

template <typename T>
struct CompressIsPartition {
  enum { value = (sizeof(T) != 1) };
};

template <typename T, size_t N>
HWY_API Vec128<T, N> Compress(Vec128<T, N> v, Mask128<T, N> mask) {
  size_t count = 0;
  Vec128<T, N> ret;
  for (size_t i = 0; i < N; ++i) {
    if (mask.bits[i]) {
      ret.raw[count++] = v.raw[i];
    }
  }
  for (size_t i = 0; i < N; ++i) {
    if (!mask.bits[i]) {
      ret.raw[count++] = v.raw[i];
    }
  }
  HWY_DASSERT(count == N);
  return ret;
}

// ------------------------------ Expand

// Could also just allow generic_ops-inl.h to implement these, but use our
// simple implementation below to ensure the test is correct.
#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

template <typename T, size_t N>
HWY_API Vec128<T, N> Expand(Vec128<T, N> v, const Mask128<T, N> mask) {
  size_t in_pos = 0;
  Vec128<T, N> ret;
  for (size_t i = 0; i < N; ++i) {
    if (mask.bits[i]) {
      ret.raw[i] = v.raw[in_pos++];
    } else {
      ret.raw[i] = ConvertScalarTo<T>(0);
    }
  }
  return ret;
}

// ------------------------------ LoadExpand

template <class D>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
  size_t in_pos = 0;
  VFromD<D> ret;
  for (size_t i = 0; i < Lanes(d); ++i) {
    if (mask.bits[i]) {
      ret.raw[i] = unaligned[in_pos++];
    } else {
      ret.raw[i] = TFromD<D>();  // zero, also works for float16_t
    }
  }
  return ret;
}

// ------------------------------ CompressNot
template <typename T, size_t N>
HWY_API Vec128<T, N> CompressNot(Vec128<T, N> v, Mask128<T, N> mask) {
  size_t count = 0;
  Vec128<T, N> ret;
  for (size_t i = 0; i < N; ++i) {
    if (!mask.bits[i]) {
      ret.raw[count++] = v.raw[i];
    }
  }
  for (size_t i = 0; i < N; ++i) {
    if (mask.bits[i]) {
      ret.raw[count++] = v.raw[i];
    }
  }
  HWY_DASSERT(count == N);
  return ret;
}

// ------------------------------ CompressBlocksNot
HWY_API Vec128<uint64_t> CompressBlocksNot(Vec128<uint64_t> v,
                                           Mask128<uint64_t> /* m */) {
  return v;
}

// ------------------------------ CompressBits
template <typename T, size_t N>
HWY_API Vec128<T, N> CompressBits(Vec128<T, N> v,
                                  const uint8_t* HWY_RESTRICT bits) {
  return Compress(v, LoadMaskBits(Simd<T, N, 0>(), bits));
}

// ------------------------------ CompressStore

// generic_ops-inl defines the 8-bit versions.
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressStore(VFromD<D> v, MFromD<D> mask, D d,
                             TFromD<D>* HWY_RESTRICT unaligned) {
  size_t count = 0;
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    if (mask.bits[i]) {
      unaligned[count++] = v.raw[i];
    }
  }
  return count;
}

// ------------------------------ CompressBlendedStore
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBlendedStore(VFromD<D> v, MFromD<D> mask, D d,
                                    TFromD<D>* HWY_RESTRICT unaligned) {
  return CompressStore(v, mask, d, unaligned);
}

// ------------------------------ CompressBitsStore
template <class D, HWY_IF_NOT_T_SIZE_D(D, 1)>
HWY_API size_t CompressBitsStore(VFromD<D> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, TFromD<D>* HWY_RESTRICT unaligned) {
  const MFromD<D> mask = LoadMaskBits(d, bits);
  StoreU(Compress(v, mask), d, unaligned);
  return CountTrue(d, mask);
}

// ------------------------------ Additional mask logical operations
template <class T>
HWY_API Mask128<T, 1> SetAtOrAfterFirst(Mask128<T, 1> mask) {
  return mask;
}

template <class T, size_t N, HWY_IF_LANES_GT(N, 1)>
HWY_API Mask128<T, N> SetAtOrAfterFirst(Mask128<T, N> mask) {
  using TU = hwy::MakeUnsigned<T>;

  Mask128<T, N> result;
  TU result_lane_mask{0};
  for (size_t i = 0; i < N; i++) {
    result_lane_mask = static_cast<TU>(result_lane_mask | mask.bits[i]);
    result.bits[i] = result_lane_mask;
  }
  return result;
}

template <class T, size_t N>
HWY_API Mask128<T, N> SetBeforeFirst(Mask128<T, N> mask) {
  return Not(SetAtOrAfterFirst(mask));
}

template <class T, size_t N>
HWY_API Mask128<T, N> SetOnlyFirst(Mask128<T, N> mask) {
  using TU = hwy::MakeUnsigned<T>;
  using TI = hwy::MakeSigned<T>;

  Mask128<T, N> result;
  TU result_lane_mask = static_cast<TU>(~TU{0});
  for (size_t i = 0; i < N; i++) {
    const auto curr_lane_mask_bits = mask.bits[i];
    result.bits[i] = static_cast<TU>(curr_lane_mask_bits & result_lane_mask);
    result_lane_mask =
        static_cast<TU>(result_lane_mask &
                        static_cast<TU>(-static_cast<TI>(mask.bits[i] == 0)));
  }
  return result;
}

template <class T, size_t N>
HWY_API Mask128<T, N> SetAtOrBeforeFirst(Mask128<T, N> mask) {
  using TU = hwy::MakeUnsigned<T>;
  using TI = hwy::MakeSigned<T>;

  Mask128<T, N> result;
  TU result_lane_mask = static_cast<TU>(~TU{0});
  for (size_t i = 0; i < N; i++) {
    result.bits[i] = result_lane_mask;
    result_lane_mask =
        static_cast<TU>(result_lane_mask &
                        static_cast<TU>(-static_cast<TI>(mask.bits[i] == 0)));
  }
  return result;
}

// ------------------------------ WidenMulPairwiseAdd

template <class DF, HWY_IF_F32_D(DF), class VBF>
HWY_API VFromD<DF> WidenMulPairwiseAdd(DF df, VBF a, VBF b) {
  return MulAdd(PromoteEvenTo(df, a), PromoteEvenTo(df, b),
                Mul(PromoteOddTo(df, a), PromoteOddTo(df, b)));
}

template <class D, HWY_IF_UI32_D(D), class V16>
HWY_API VFromD<D> WidenMulPairwiseAdd(D d32, V16 a, V16 b) {
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b),
                Mul(PromoteOddTo(d32, a), PromoteOddTo(d32, b)));
}

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

template <class D, HWY_IF_UI32_D(D), class V16>
HWY_API VFromD<D> ReorderWidenMulAccumulate(D d32, V16 a, V16 b,
                                            const VFromD<D> sum0,
                                            VFromD<D>& sum1) {
  sum1 = MulAdd(PromoteOddTo(d32, a), PromoteOddTo(d32, b), sum1);
  return MulAdd(PromoteEvenTo(d32, a), PromoteEvenTo(d32, b), sum0);
}

// ------------------------------ RearrangeToOddPlusEven
template <class VW>
HWY_API VW RearrangeToOddPlusEven(VW sum0, VW sum1) {
  return Add(sum0, sum1);
}

// ================================================== REDUCTIONS

#ifdef HWY_NATIVE_REDUCE_SCALAR
#undef HWY_NATIVE_REDUCE_SCALAR
#else
#define HWY_NATIVE_REDUCE_SCALAR
#endif

template <class D, typename T = TFromD<D>, HWY_IF_REDUCE_D(D)>
HWY_API T ReduceSum(D d, VFromD<D> v) {
  T sum = T{0};
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    sum += v.raw[i];
  }
  return sum;
}
template <class D, typename T = TFromD<D>, HWY_IF_REDUCE_D(D)>
HWY_API T ReduceMin(D d, VFromD<D> v) {
  T min = HighestValue<T>();
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    min = HWY_MIN(min, v.raw[i]);
  }
  return min;
}
template <class D, typename T = TFromD<D>, HWY_IF_REDUCE_D(D)>
HWY_API T ReduceMax(D d, VFromD<D> v) {
  T max = LowestValue<T>();
  for (size_t i = 0; i < MaxLanes(d); ++i) {
    max = HWY_MAX(max, v.raw[i]);
  }
  return max;
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

// ================================================== OPS WITH DEPENDENCIES

// ------------------------------ MulEven/Odd 64x64 (UpperHalf)

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulEven(Vec128<T> a, Vec128<T> b) {
  alignas(16) T mul[2];
  mul[0] = Mul128(GetLane(a), GetLane(b), &mul[1]);
  return Load(Full128<T>(), mul);
}

template <class T, HWY_IF_UI64(T)>
HWY_API Vec128<T> MulOdd(Vec128<T> a, Vec128<T> b) {
  alignas(16) T mul[2];
  const Half<Full128<T>> d2;
  mul[0] =
      Mul128(GetLane(UpperHalf(d2, a)), GetLane(UpperHalf(d2, b)), &mul[1]);
  return Load(Full128<T>(), mul);
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
