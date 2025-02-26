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

// Single-element vectors and operations.
// External include guard in highway.h - see comment there.

#include <stdint.h>
#ifndef HWY_NO_LIBCXX
#include <math.h>  // sqrtf
#endif

#include "hwy/ops/shared-inl.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Single instruction, single data.
template <typename T>
using Sisd = Simd<T, 1, 0>;

// (Wrapper class required for overloading comparison operators.)
template <typename T>
struct Vec1 {
  using PrivateT = T;                     // only for DFromV
  static constexpr size_t kPrivateN = 1;  // only for DFromV

  HWY_INLINE Vec1() = default;
  Vec1(const Vec1&) = default;
  Vec1& operator=(const Vec1&) = default;
  HWY_INLINE explicit Vec1(const T t) : raw(t) {}

  HWY_INLINE Vec1& operator*=(const Vec1 other) {
    return *this = (*this * other);
  }
  HWY_INLINE Vec1& operator/=(const Vec1 other) {
    return *this = (*this / other);
  }
  HWY_INLINE Vec1& operator+=(const Vec1 other) {
    return *this = (*this + other);
  }
  HWY_INLINE Vec1& operator-=(const Vec1 other) {
    return *this = (*this - other);
  }
  HWY_INLINE Vec1& operator%=(const Vec1 other) {
    return *this = (*this % other);
  }
  HWY_INLINE Vec1& operator&=(const Vec1 other) {
    return *this = (*this & other);
  }
  HWY_INLINE Vec1& operator|=(const Vec1 other) {
    return *this = (*this | other);
  }
  HWY_INLINE Vec1& operator^=(const Vec1 other) {
    return *this = (*this ^ other);
  }

  T raw;
};

// 0 or FF..FF, same size as Vec1.
template <typename T>
class Mask1 {
  using Raw = hwy::MakeUnsigned<T>;

 public:
  static HWY_INLINE Mask1<T> FromBool(bool b) {
    Mask1<T> mask;
    mask.bits = b ? static_cast<Raw>(~Raw{0}) : 0;
    return mask;
  }

  Raw bits;
};

template <class V>
using DFromV = Simd<typename V::PrivateT, V::kPrivateN, 0>;

template <class V>
using TFromV = typename V::PrivateT;

// ------------------------------ BitCast

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom>
HWY_API Vec1<TTo> BitCast(DTo /* tag */, Vec1<TFrom> v) {
  static_assert(sizeof(TTo) <= sizeof(TFrom), "Promoting is undefined");
  TTo to;
  CopyBytes<sizeof(TTo)>(&v.raw, &to);  // not same size - ok to shrink
  return Vec1<TTo>(to);
}

// ------------------------------ Zero

template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Vec1<T> Zero(D /* tag */) {
  return Vec1<T>(ConvertScalarTo<T>(0));
}

template <class D>
using VFromD = decltype(Zero(D()));

// ------------------------------ Set
template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>, typename T2>
HWY_API Vec1<T> Set(D /* tag */, const T2 t) {
  return Vec1<T>(static_cast<T>(t));
}

// ------------------------------ Undefined
template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Vec1<T> Undefined(D d) {
  return Zero(d);
}

// ------------------------------ Iota
template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>, typename T2>
HWY_API Vec1<T> Iota(const D /* tag */, const T2 first) {
  return Vec1<T>(static_cast<T>(first));
}

// ------------------------------ ResizeBitCast

template <class D, typename FromV>
HWY_API VFromD<D> ResizeBitCast(D /* tag */, FromV v) {
  using TFrom = TFromV<FromV>;
  using TTo = TFromD<D>;
  constexpr size_t kCopyLen = HWY_MIN(sizeof(TFrom), sizeof(TTo));
  TTo to{};
  CopyBytes<kCopyLen>(&v.raw, &to);
  return VFromD<D>(to);
}

namespace detail {

// ResizeBitCast on the HWY_SCALAR target has zero-extending semantics if
// sizeof(TFromD<DTo>) is greater than sizeof(TFromV<FromV>)
template <class FromSizeTag, class ToSizeTag, class DTo, class DFrom>
HWY_INLINE VFromD<DTo> ZeroExtendResizeBitCast(FromSizeTag /* from_size_tag */,
                                               ToSizeTag /* to_size_tag */,
                                               DTo d_to, DFrom /*d_from*/,
                                               VFromD<DFrom> v) {
  return ResizeBitCast(d_to, v);
}

}  // namespace detail

// ------------------------------ Dup128VecFromValues

template <class D, HWY_IF_T_SIZE_D(D, 1)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> /*t1*/,
                                      TFromD<D> /*t2*/, TFromD<D> /*t3*/,
                                      TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                      TFromD<D> /*t6*/, TFromD<D> /*t7*/,
                                      TFromD<D> /*t8*/, TFromD<D> /*t9*/,
                                      TFromD<D> /*t10*/, TFromD<D> /*t11*/,
                                      TFromD<D> /*t12*/, TFromD<D> /*t13*/,
                                      TFromD<D> /*t14*/, TFromD<D> /*t15*/) {
  return VFromD<D>(t0);
}

template <class D, HWY_IF_T_SIZE_D(D, 2)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> /*t1*/,
                                      TFromD<D> /*t2*/, TFromD<D> /*t3*/,
                                      TFromD<D> /*t4*/, TFromD<D> /*t5*/,
                                      TFromD<D> /*t6*/, TFromD<D> /*t7*/) {
  return VFromD<D>(t0);
}

template <class D, HWY_IF_T_SIZE_D(D, 4)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> /*t1*/,
                                      TFromD<D> /*t2*/, TFromD<D> /*t3*/) {
  return VFromD<D>(t0);
}

template <class D, HWY_IF_T_SIZE_D(D, 8)>
HWY_API VFromD<D> Dup128VecFromValues(D /*d*/, TFromD<D> t0, TFromD<D> /*t1*/) {
  return VFromD<D>(t0);
}

// ================================================== LOGICAL

// ------------------------------ Not

template <typename T>
HWY_API Vec1<T> Not(const Vec1<T> v) {
  using TU = MakeUnsigned<T>;
  const Sisd<TU> du;
  return BitCast(Sisd<T>(), Vec1<TU>(static_cast<TU>(~BitCast(du, v).raw)));
}

// ------------------------------ And

template <typename T>
HWY_API Vec1<T> And(const Vec1<T> a, const Vec1<T> b) {
  using TU = MakeUnsigned<T>;
  const Sisd<TU> du;
  return BitCast(Sisd<T>(), Vec1<TU>(BitCast(du, a).raw & BitCast(du, b).raw));
}
template <typename T>
HWY_API Vec1<T> operator&(const Vec1<T> a, const Vec1<T> b) {
  return And(a, b);
}

// ------------------------------ AndNot

template <typename T>
HWY_API Vec1<T> AndNot(const Vec1<T> a, const Vec1<T> b) {
  using TU = MakeUnsigned<T>;
  const Sisd<TU> du;
  return BitCast(Sisd<T>(), Vec1<TU>(static_cast<TU>(~BitCast(du, a).raw &
                                                     BitCast(du, b).raw)));
}

// ------------------------------ Or

template <typename T>
HWY_API Vec1<T> Or(const Vec1<T> a, const Vec1<T> b) {
  using TU = MakeUnsigned<T>;
  const Sisd<TU> du;
  return BitCast(Sisd<T>(), Vec1<TU>(BitCast(du, a).raw | BitCast(du, b).raw));
}
template <typename T>
HWY_API Vec1<T> operator|(const Vec1<T> a, const Vec1<T> b) {
  return Or(a, b);
}

// ------------------------------ Xor

template <typename T>
HWY_API Vec1<T> Xor(const Vec1<T> a, const Vec1<T> b) {
  using TU = MakeUnsigned<T>;
  const Sisd<TU> du;
  return BitCast(Sisd<T>(), Vec1<TU>(BitCast(du, a).raw ^ BitCast(du, b).raw));
}
template <typename T>
HWY_API Vec1<T> operator^(const Vec1<T> a, const Vec1<T> b) {
  return Xor(a, b);
}

// ------------------------------ Xor3

template <typename T>
HWY_API Vec1<T> Xor3(Vec1<T> x1, Vec1<T> x2, Vec1<T> x3) {
  return Xor(x1, Xor(x2, x3));
}

// ------------------------------ Or3

template <typename T>
HWY_API Vec1<T> Or3(Vec1<T> o1, Vec1<T> o2, Vec1<T> o3) {
  return Or(o1, Or(o2, o3));
}

// ------------------------------ OrAnd

template <typename T>
HWY_API Vec1<T> OrAnd(const Vec1<T> o, const Vec1<T> a1, const Vec1<T> a2) {
  return Or(o, And(a1, a2));
}

// ------------------------------ Mask

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom>
HWY_API Mask1<TTo> RebindMask(DTo /*tag*/, Mask1<TFrom> m) {
  static_assert(sizeof(TFrom) == sizeof(TTo), "Must have same size");
  return Mask1<TTo>{m.bits};
}

// v must be 0 or FF..FF.
template <typename T>
HWY_API Mask1<T> MaskFromVec(const Vec1<T> v) {
  Mask1<T> mask;
  CopySameSize(&v, &mask);
  return mask;
}

template <class D>
using MFromD = decltype(MaskFromVec(VFromD<D>()));

template <typename T>
Vec1<T> VecFromMask(const Mask1<T> mask) {
  Vec1<T> v;
  CopySameSize(&mask, &v);
  return v;
}

template <class D, typename T = TFromD<D>>
Vec1<T> VecFromMask(D /* tag */, const Mask1<T> mask) {
  Vec1<T> v;
  CopySameSize(&mask, &v);
  return v;
}

template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Mask1<T> FirstN(D /*tag*/, size_t n) {
  return Mask1<T>::FromBool(n != 0);
}

// ------------------------------ IfVecThenElse
template <typename T>
HWY_API Vec1<T> IfVecThenElse(Vec1<T> mask, Vec1<T> yes, Vec1<T> no) {
  return IfThenElse(MaskFromVec(mask), yes, no);
}

// ------------------------------ CopySign
template <typename T>
HWY_API Vec1<T> CopySign(const Vec1<T> magn, const Vec1<T> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const DFromV<decltype(magn)> d;
  return BitwiseIfThenElse(SignBit(d), sign, magn);
}

// ------------------------------ CopySignToAbs
template <typename T>
HWY_API Vec1<T> CopySignToAbs(const Vec1<T> abs, const Vec1<T> sign) {
  static_assert(IsFloat<T>(), "Only makes sense for floating-point");
  const Sisd<T> d;
  return OrAnd(abs, SignBit(d), sign);
}

// ------------------------------ BroadcastSignBit
template <typename T>
HWY_API Vec1<T> BroadcastSignBit(const Vec1<T> v) {
  return Vec1<T>(ScalarShr(v.raw, sizeof(T) * 8 - 1));
}

// ------------------------------ PopulationCount

#ifdef HWY_NATIVE_POPCNT
#undef HWY_NATIVE_POPCNT
#else
#define HWY_NATIVE_POPCNT
#endif

template <typename T>
HWY_API Vec1<T> PopulationCount(Vec1<T> v) {
  return Vec1<T>(static_cast<T>(PopCount(v.raw)));
}

// ------------------------------ IfThenElse

// Returns mask ? yes : no.
template <typename T>
HWY_API Vec1<T> IfThenElse(const Mask1<T> mask, const Vec1<T> yes,
                           const Vec1<T> no) {
  return mask.bits ? yes : no;
}

template <typename T>
HWY_API Vec1<T> IfThenElseZero(const Mask1<T> mask, const Vec1<T> yes) {
  return mask.bits ? yes : Vec1<T>(ConvertScalarTo<T>(0));
}

template <typename T>
HWY_API Vec1<T> IfThenZeroElse(const Mask1<T> mask, const Vec1<T> no) {
  return mask.bits ? Vec1<T>(ConvertScalarTo<T>(0)) : no;
}

template <typename T>
HWY_API Vec1<T> IfNegativeThenElse(Vec1<T> v, Vec1<T> yes, Vec1<T> no) {
  const DFromV<decltype(v)> d;
  const RebindToSigned<decltype(d)> di;
  const auto vi = BitCast(di, v);

  return vi.raw < 0 ? yes : no;
}

// ------------------------------ Mask logical

template <typename T>
HWY_API Mask1<T> Not(const Mask1<T> m) {
  return MaskFromVec(Not(VecFromMask(Sisd<T>(), m)));
}

template <typename T>
HWY_API Mask1<T> And(const Mask1<T> a, Mask1<T> b) {
  const Sisd<T> d;
  return MaskFromVec(And(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T>
HWY_API Mask1<T> AndNot(const Mask1<T> a, Mask1<T> b) {
  const Sisd<T> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T>
HWY_API Mask1<T> Or(const Mask1<T> a, Mask1<T> b) {
  const Sisd<T> d;
  return MaskFromVec(Or(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T>
HWY_API Mask1<T> Xor(const Mask1<T> a, Mask1<T> b) {
  const Sisd<T> d;
  return MaskFromVec(Xor(VecFromMask(d, a), VecFromMask(d, b)));
}

template <typename T>
HWY_API Mask1<T> ExclusiveNeither(const Mask1<T> a, Mask1<T> b) {
  const Sisd<T> d;
  return MaskFromVec(AndNot(VecFromMask(d, a), Not(VecFromMask(d, b))));
}

template <class T>
HWY_API Mask1<T> SetAtOrAfterFirst(Mask1<T> mask) {
  return mask;
}

template <class T>
HWY_API Mask1<T> SetBeforeFirst(Mask1<T> mask) {
  return Not(mask);
}

template <class T>
HWY_API Mask1<T> SetOnlyFirst(Mask1<T> mask) {
  return mask;
}

template <class T>
HWY_API Mask1<T> SetAtOrBeforeFirst(Mask1<T> /*mask*/) {
  return Mask1<T>::FromBool(true);
}

// ------------------------------ LowerHalfOfMask

#ifdef HWY_NATIVE_LOWER_HALF_OF_MASK
#undef HWY_NATIVE_LOWER_HALF_OF_MASK
#else
#define HWY_NATIVE_LOWER_HALF_OF_MASK
#endif

template <class D>
HWY_API MFromD<D> LowerHalfOfMask(D /*d*/, MFromD<D> m) {
  return m;
}

// ================================================== SHIFTS

// ------------------------------ ShiftLeft/ShiftRight (BroadcastSignBit)

template <int kBits, typename T>
HWY_API Vec1<T> ShiftLeft(const Vec1<T> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  return Vec1<T>(
      static_cast<T>(static_cast<hwy::MakeUnsigned<T>>(v.raw) << kBits));
}

template <int kBits, typename T>
HWY_API Vec1<T> ShiftRight(const Vec1<T> v) {
  static_assert(0 <= kBits && kBits < sizeof(T) * 8, "Invalid shift");
  return Vec1<T>(ScalarShr(v.raw, kBits));
}

// ------------------------------ RotateRight (ShiftRight)
template <int kBits, typename T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec1<T> RotateRight(const Vec1<T> v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  constexpr size_t kSizeInBits = sizeof(T) * 8;
  static_assert(0 <= kBits && kBits < kSizeInBits, "Invalid shift count");
  if (kBits == 0) return v;

  return Or(BitCast(d, ShiftRight<kBits>(BitCast(du, v))),
            ShiftLeft<HWY_MIN(kSizeInBits - 1, kSizeInBits - kBits)>(v));
}

// ------------------------------ ShiftLeftSame (BroadcastSignBit)

template <typename T>
HWY_API Vec1<T> ShiftLeftSame(const Vec1<T> v, int bits) {
  return Vec1<T>(
      static_cast<T>(static_cast<hwy::MakeUnsigned<T>>(v.raw) << bits));
}

template <typename T>
HWY_API Vec1<T> ShiftRightSame(const Vec1<T> v, int bits) {
  return Vec1<T>(ScalarShr(v.raw, bits));
}

// ------------------------------ Shl

// Single-lane => same as ShiftLeftSame except for the argument type.
template <typename T>
HWY_API Vec1<T> operator<<(const Vec1<T> v, const Vec1<T> bits) {
  return ShiftLeftSame(v, static_cast<int>(bits.raw));
}

template <typename T>
HWY_API Vec1<T> operator>>(const Vec1<T> v, const Vec1<T> bits) {
  return ShiftRightSame(v, static_cast<int>(bits.raw));
}

// ================================================== ARITHMETIC

template <typename T>
HWY_API Vec1<T> operator+(Vec1<T> a, Vec1<T> b) {
  const uint64_t a64 = static_cast<uint64_t>(a.raw);
  const uint64_t b64 = static_cast<uint64_t>(b.raw);
  return Vec1<T>(static_cast<T>((a64 + b64) & static_cast<uint64_t>(~T(0))));
}
HWY_API Vec1<float> operator+(const Vec1<float> a, const Vec1<float> b) {
  return Vec1<float>(a.raw + b.raw);
}
HWY_API Vec1<double> operator+(const Vec1<double> a, const Vec1<double> b) {
  return Vec1<double>(a.raw + b.raw);
}

template <typename T>
HWY_API Vec1<T> operator-(Vec1<T> a, Vec1<T> b) {
  const uint64_t a64 = static_cast<uint64_t>(a.raw);
  const uint64_t b64 = static_cast<uint64_t>(b.raw);
  return Vec1<T>(static_cast<T>((a64 - b64) & static_cast<uint64_t>(~T(0))));
}
HWY_API Vec1<float> operator-(const Vec1<float> a, const Vec1<float> b) {
  return Vec1<float>(a.raw - b.raw);
}
HWY_API Vec1<double> operator-(const Vec1<double> a, const Vec1<double> b) {
  return Vec1<double>(a.raw - b.raw);
}

// ------------------------------ SumsOf8

HWY_API Vec1<int64_t> SumsOf8(const Vec1<int8_t> v) {
  return Vec1<int64_t>(v.raw);
}
HWY_API Vec1<uint64_t> SumsOf8(const Vec1<uint8_t> v) {
  return Vec1<uint64_t>(v.raw);
}

// ------------------------------ SumsOf2

template <class T>
HWY_API Vec1<MakeWide<T>> SumsOf2(const Vec1<T> v) {
  const DFromV<decltype(v)> d;
  const Rebind<MakeWide<T>, decltype(d)> dw;
  return PromoteTo(dw, v);
}

// ------------------------------ SaturatedAdd

// Returns a + b clamped to the destination range.

// Unsigned
HWY_API Vec1<uint8_t> SaturatedAdd(const Vec1<uint8_t> a,
                                   const Vec1<uint8_t> b) {
  return Vec1<uint8_t>(
      static_cast<uint8_t>(HWY_MIN(HWY_MAX(0, a.raw + b.raw), 255)));
}
HWY_API Vec1<uint16_t> SaturatedAdd(const Vec1<uint16_t> a,
                                    const Vec1<uint16_t> b) {
  return Vec1<uint16_t>(static_cast<uint16_t>(
      HWY_MIN(HWY_MAX(0, static_cast<int32_t>(a.raw) + b.raw), 65535)));
}

// Signed
HWY_API Vec1<int8_t> SaturatedAdd(const Vec1<int8_t> a, const Vec1<int8_t> b) {
  return Vec1<int8_t>(
      static_cast<int8_t>(HWY_MIN(HWY_MAX(-128, a.raw + b.raw), 127)));
}
HWY_API Vec1<int16_t> SaturatedAdd(const Vec1<int16_t> a,
                                   const Vec1<int16_t> b) {
  return Vec1<int16_t>(static_cast<int16_t>(
      HWY_MIN(HWY_MAX(-32768, static_cast<int32_t>(a.raw) + b.raw), 32767)));
}

// ------------------------------ Saturating subtraction

// Returns a - b clamped to the destination range.

// Unsigned
HWY_API Vec1<uint8_t> SaturatedSub(const Vec1<uint8_t> a,
                                   const Vec1<uint8_t> b) {
  return Vec1<uint8_t>(
      static_cast<uint8_t>(HWY_MIN(HWY_MAX(0, a.raw - b.raw), 255)));
}
HWY_API Vec1<uint16_t> SaturatedSub(const Vec1<uint16_t> a,
                                    const Vec1<uint16_t> b) {
  return Vec1<uint16_t>(static_cast<uint16_t>(
      HWY_MIN(HWY_MAX(0, static_cast<int32_t>(a.raw) - b.raw), 65535)));
}

// Signed
HWY_API Vec1<int8_t> SaturatedSub(const Vec1<int8_t> a, const Vec1<int8_t> b) {
  return Vec1<int8_t>(
      static_cast<int8_t>(HWY_MIN(HWY_MAX(-128, a.raw - b.raw), 127)));
}
HWY_API Vec1<int16_t> SaturatedSub(const Vec1<int16_t> a,
                                   const Vec1<int16_t> b) {
  return Vec1<int16_t>(static_cast<int16_t>(
      HWY_MIN(HWY_MAX(-32768, static_cast<int32_t>(a.raw) - b.raw), 32767)));
}

// ------------------------------ Average

// Returns (a + b + 1) / 2

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

template <class T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec1<T> AverageRound(const Vec1<T> a, const Vec1<T> b) {
  const T a_val = a.raw;
  const T b_val = b.raw;
  return Vec1<T>(static_cast<T>(ScalarShr(a_val, 1) + ScalarShr(b_val, 1) +
                                ((a_val | b_val) & 1)));
}

// ------------------------------ Absolute value

template <typename T>
HWY_API Vec1<T> Abs(const Vec1<T> a) {
  return Vec1<T>(ScalarAbs(a.raw));
}

// ------------------------------ Min/Max

// <cmath> may be unavailable, so implement our own.

template <typename T, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec1<T> Min(const Vec1<T> a, const Vec1<T> b) {
  return Vec1<T>(HWY_MIN(a.raw, b.raw));
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> Min(const Vec1<T> a, const Vec1<T> b) {
  if (ScalarIsNaN(a.raw)) return b;
  if (ScalarIsNaN(b.raw)) return a;
  return Vec1<T>(HWY_MIN(a.raw, b.raw));
}

template <typename T, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec1<T> Max(const Vec1<T> a, const Vec1<T> b) {
  return Vec1<T>(HWY_MAX(a.raw, b.raw));
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> Max(const Vec1<T> a, const Vec1<T> b) {
  if (ScalarIsNaN(a.raw)) return b;
  if (ScalarIsNaN(b.raw)) return a;
  return Vec1<T>(HWY_MAX(a.raw, b.raw));
}

// ------------------------------ Floating-point negate

template <typename T, HWY_IF_FLOAT_OR_SPECIAL(T)>
HWY_API Vec1<T> Neg(const Vec1<T> v) {
  return Xor(v, SignBit(Sisd<T>()));
}

template <typename T, HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec1<T> Neg(const Vec1<T> v) {
  return Zero(Sisd<T>()) - v;
}

// ------------------------------ mul/div

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

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> operator*(const Vec1<T> a, const Vec1<T> b) {
  return Vec1<T>(static_cast<T>(double{a.raw} * b.raw));
}

template <typename T, HWY_IF_NOT_FLOAT(T)>
HWY_API Vec1<T> operator*(const Vec1<T> a, const Vec1<T> b) {
  return Vec1<T>(static_cast<T>(static_cast<uint64_t>(a.raw) *
                                static_cast<uint64_t>(b.raw)));
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> operator/(const Vec1<T> a, const Vec1<T> b) {
  return Vec1<T>(a.raw / b.raw);
}

// Returns the upper sizeof(T)*8 bits of a * b in each lane.
template <class T, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec1<T> MulHigh(const Vec1<T> a, const Vec1<T> b) {
  using TW = MakeWide<T>;
  return Vec1<T>(static_cast<T>(
      (static_cast<TW>(a.raw) * static_cast<TW>(b.raw)) >> (sizeof(T) * 8)));
}
template <class T, HWY_IF_UI64(T)>
HWY_API Vec1<T> MulHigh(const Vec1<T> a, const Vec1<T> b) {
  T hi;
  Mul128(a.raw, b.raw, &hi);
  return Vec1<T>(hi);
}

HWY_API Vec1<int16_t> MulFixedPoint15(Vec1<int16_t> a, Vec1<int16_t> b) {
  return Vec1<int16_t>(static_cast<int16_t>((a.raw * b.raw + 16384) >> 15));
}

// Multiplies even lanes (0, 2 ..) and returns the double-wide result.
template <class T, HWY_IF_T_SIZE_ONE_OF(T, (1 << 1) | (1 << 2) | (1 << 4)),
          HWY_IF_NOT_FLOAT_NOR_SPECIAL(T)>
HWY_API Vec1<MakeWide<T>> MulEven(const Vec1<T> a, const Vec1<T> b) {
  using TW = MakeWide<T>;
  const TW a_wide = a.raw;
  return Vec1<TW>(static_cast<TW>(a_wide * b.raw));
}

// Approximate reciprocal
HWY_API Vec1<float> ApproximateReciprocal(const Vec1<float> v) {
  // Zero inputs are allowed, but callers are responsible for replacing the
  // return value with something else (typically using IfThenElse). This check
  // avoids a ubsan error. The return value is arbitrary.
  if (v.raw == 0.0f) return Vec1<float>(0.0f);
  return Vec1<float>(1.0f / v.raw);
}

// generic_ops takes care of integer T.
template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> AbsDiff(const Vec1<T> a, const Vec1<T> b) {
  return Abs(a - b);
}

// ------------------------------ Floating-point multiply-add variants

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> MulAdd(const Vec1<T> mul, const Vec1<T> x, const Vec1<T> add) {
  return mul * x + add;
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> NegMulAdd(const Vec1<T> mul, const Vec1<T> x,
                          const Vec1<T> add) {
  return add - mul * x;
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> MulSub(const Vec1<T> mul, const Vec1<T> x, const Vec1<T> sub) {
  return mul * x - sub;
}

template <typename T, HWY_IF_FLOAT(T)>
HWY_API Vec1<T> NegMulSub(const Vec1<T> mul, const Vec1<T> x,
                          const Vec1<T> sub) {
  return Neg(mul) * x - sub;
}

// ------------------------------ Floating-point square root

// Approximate reciprocal square root
HWY_API Vec1<float> ApproximateReciprocalSqrt(const Vec1<float> v) {
  float f = v.raw;
  const float half = f * 0.5f;
  uint32_t bits;
  CopySameSize(&f, &bits);
  // Initial guess based on log2(f)
  bits = 0x5F3759DF - (bits >> 1);
  CopySameSize(&bits, &f);
  // One Newton-Raphson iteration
  return Vec1<float>(f * (1.5f - (half * f * f)));
}

// Square root
HWY_API Vec1<float> Sqrt(Vec1<float> v) {
#if defined(HWY_NO_LIBCXX)
#if HWY_COMPILER_GCC_ACTUAL
  return Vec1<float>(__builtin_sqrt(v.raw));
#else
  uint32_t bits;
  CopyBytes<sizeof(bits)>(&v, &bits);
  // Coarse approximation, letting the exponent LSB leak into the mantissa
  bits = (1 << 29) + (bits >> 1) - (1 << 22);
  CopyBytes<sizeof(bits)>(&bits, &v);
  return v;
#endif  // !HWY_COMPILER_GCC_ACTUAL
#else
  return Vec1<float>(sqrtf(v.raw));
#endif  // !HWY_NO_LIBCXX
}
HWY_API Vec1<double> Sqrt(Vec1<double> v) {
#if defined(HWY_NO_LIBCXX)
#if HWY_COMPILER_GCC_ACTUAL
  return Vec1<double>(__builtin_sqrt(v.raw));
#else
  uint64_t bits;
  CopyBytes<sizeof(bits)>(&v, &bits);
  // Coarse approximation, letting the exponent LSB leak into the mantissa
  bits = (1ULL << 61) + (bits >> 1) - (1ULL << 51);
  CopyBytes<sizeof(bits)>(&bits, &v);
  return v;
#endif  // !HWY_COMPILER_GCC_ACTUAL
#else
  return Vec1<double>(sqrt(v.raw));
#endif  // HWY_NO_LIBCXX
}

// ------------------------------ Floating-point rounding

template <typename T>
HWY_API Vec1<T> Round(const Vec1<T> v) {
  using TI = MakeSigned<T>;
  if (!(Abs(v).raw < MantissaEnd<T>())) {  // Huge or NaN
    return v;
  }
  const T k0 = ConvertScalarTo<T>(0);
  const T bias = ConvertScalarTo<T>(v.raw < k0 ? -0.5 : 0.5);
  const TI rounded = ConvertScalarTo<TI>(v.raw + bias);
  if (rounded == 0) return CopySignToAbs(Vec1<T>(k0), v);
  TI offset = 0;
  // Round to even
  if ((rounded & 1) && ScalarAbs(ConvertScalarTo<T>(rounded) - v.raw) ==
                           ConvertScalarTo<T>(0.5)) {
    offset = v.raw < k0 ? -1 : 1;
  }
  return Vec1<T>(ConvertScalarTo<T>(rounded - offset));
}

// Round-to-nearest even.
template <class T, HWY_IF_FLOAT3264(T)>
HWY_API Vec1<MakeSigned<T>> NearestInt(const Vec1<T> v) {
  using TI = MakeSigned<T>;

  const T abs = Abs(v).raw;
  const bool is_sign = ScalarSignBit(v.raw);

  if (!(abs < MantissaEnd<T>())) {  // Huge or NaN
    // Check if too large to cast or NaN
    if (!(abs <= ConvertScalarTo<T>(LimitsMax<TI>()))) {
      return Vec1<TI>(is_sign ? LimitsMin<TI>() : LimitsMax<TI>());
    }
    return Vec1<TI>(ConvertScalarTo<TI>(v.raw));
  }
  const T bias =
      ConvertScalarTo<T>(v.raw < ConvertScalarTo<T>(0.0) ? -0.5 : 0.5);
  const TI rounded = ConvertScalarTo<TI>(v.raw + bias);
  if (rounded == 0) return Vec1<TI>(0);
  TI offset = 0;
  // Round to even
  if ((rounded & 1) && ScalarAbs(ConvertScalarTo<T>(rounded) - v.raw) ==
                           ConvertScalarTo<T>(0.5)) {
    offset = is_sign ? -1 : 1;
  }
  return Vec1<TI>(rounded - offset);
}

// Round-to-nearest even.
template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> DemoteToNearestInt(DI32 /*di32*/, const Vec1<double> v) {
  using T = double;
  using TI = int32_t;

  const T abs = Abs(v).raw;
  const bool is_sign = ScalarSignBit(v.raw);

  // Check if too large to cast or NaN
  if (!(abs <= ConvertScalarTo<T>(LimitsMax<TI>()))) {
    return Vec1<TI>(is_sign ? LimitsMin<TI>() : LimitsMax<TI>());
  }

  const T bias =
      ConvertScalarTo<T>(v.raw < ConvertScalarTo<T>(0.0) ? -0.5 : 0.5);
  const TI rounded = ConvertScalarTo<TI>(v.raw + bias);
  if (rounded == 0) return Vec1<TI>(0);
  TI offset = 0;
  // Round to even
  if ((rounded & 1) && ScalarAbs(ConvertScalarTo<T>(rounded) - v.raw) ==
                           ConvertScalarTo<T>(0.5)) {
    offset = is_sign ? -1 : 1;
  }
  return Vec1<TI>(rounded - offset);
}

template <typename T>
HWY_API Vec1<T> Trunc(const Vec1<T> v) {
  using TI = MakeSigned<T>;
  if (!(Abs(v).raw <= MantissaEnd<T>())) {  // Huge or NaN
    return v;
  }
  const TI truncated = ConvertScalarTo<TI>(v.raw);
  if (truncated == 0) return CopySignToAbs(Vec1<T>(0), v);
  return Vec1<T>(ConvertScalarTo<T>(truncated));
}

template <typename Float, typename Bits, int kMantissaBits, int kExponentBits,
          class V>
V Ceiling(const V v) {
  const Bits kExponentMask = (1ull << kExponentBits) - 1;
  const Bits kMantissaMask = (1ull << kMantissaBits) - 1;
  const Bits kBias = kExponentMask / 2;

  Float f = v.raw;
  const bool positive = f > Float(0.0);

  Bits bits;
  CopySameSize(&v, &bits);

  const int exponent =
      static_cast<int>(((bits >> kMantissaBits) & kExponentMask) - kBias);
  // Already an integer.
  if (exponent >= kMantissaBits) return v;
  // |v| <= 1 => 0 or 1.
  if (exponent < 0) return positive ? V(1) : V(-0.0);

  const Bits mantissa_mask = kMantissaMask >> exponent;
  // Already an integer
  if ((bits & mantissa_mask) == 0) return v;

  // Clear fractional bits and round up
  if (positive) bits += (kMantissaMask + 1) >> exponent;
  bits &= ~mantissa_mask;

  CopySameSize(&bits, &f);
  return V(f);
}

template <typename Float, typename Bits, int kMantissaBits, int kExponentBits,
          class V>
V Floor(const V v) {
  const Bits kExponentMask = (1ull << kExponentBits) - 1;
  const Bits kMantissaMask = (1ull << kMantissaBits) - 1;
  const Bits kBias = kExponentMask / 2;

  Float f = v.raw;
  const bool negative = f < Float(0.0);

  Bits bits;
  CopySameSize(&v, &bits);

  const int exponent =
      static_cast<int>(((bits >> kMantissaBits) & kExponentMask) - kBias);
  // Already an integer.
  if (exponent >= kMantissaBits) return v;
  // |v| <= 1 => -1 or 0.
  if (exponent < 0) return V(negative ? Float(-1.0) : Float(0.0));

  const Bits mantissa_mask = kMantissaMask >> exponent;
  // Already an integer
  if ((bits & mantissa_mask) == 0) return v;

  // Clear fractional bits and round down
  if (negative) bits += (kMantissaMask + 1) >> exponent;
  bits &= ~mantissa_mask;

  CopySameSize(&bits, &f);
  return V(f);
}

// Toward +infinity, aka ceiling
HWY_API Vec1<float> Ceil(const Vec1<float> v) {
  return Ceiling<float, uint32_t, 23, 8>(v);
}
HWY_API Vec1<double> Ceil(const Vec1<double> v) {
  return Ceiling<double, uint64_t, 52, 11>(v);
}

// Toward -infinity, aka floor
HWY_API Vec1<float> Floor(const Vec1<float> v) {
  return Floor<float, uint32_t, 23, 8>(v);
}
HWY_API Vec1<double> Floor(const Vec1<double> v) {
  return Floor<double, uint64_t, 52, 11>(v);
}

// ================================================== COMPARE

template <typename T>
HWY_API Mask1<T> operator==(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw == b.raw);
}

template <typename T>
HWY_API Mask1<T> operator!=(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw != b.raw);
}

template <typename T>
HWY_API Mask1<T> TestBit(const Vec1<T> v, const Vec1<T> bit) {
  static_assert(!hwy::IsFloat<T>(), "Only integer vectors supported");
  return (v & bit) == bit;
}

template <typename T>
HWY_API Mask1<T> operator<(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw < b.raw);
}
template <typename T>
HWY_API Mask1<T> operator>(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw > b.raw);
}

template <typename T>
HWY_API Mask1<T> operator<=(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw <= b.raw);
}
template <typename T>
HWY_API Mask1<T> operator>=(const Vec1<T> a, const Vec1<T> b) {
  return Mask1<T>::FromBool(a.raw >= b.raw);
}

// ------------------------------ Floating-point classification (==)

template <typename T>
HWY_API Mask1<T> IsNaN(const Vec1<T> v) {
  // std::isnan returns false for 0x7F..FF in clang AVX3 builds, so DIY.
  return Mask1<T>::FromBool(ScalarIsNaN(v.raw));
}

// Per-target flag to prevent generic_ops-inl.h from defining IsInf / IsFinite.
#ifdef HWY_NATIVE_ISINF
#undef HWY_NATIVE_ISINF
#else
#define HWY_NATIVE_ISINF
#endif

HWY_API Mask1<float> IsInf(const Vec1<float> v) {
  const Sisd<float> d;
  const RebindToUnsigned<decltype(d)> du;
  const Vec1<uint32_t> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, check for exponent=max and mantissa=0.
  return RebindMask(d, (vu + vu) == Set(du, 0xFF000000u));
}
HWY_API Mask1<double> IsInf(const Vec1<double> v) {
  const Sisd<double> d;
  const RebindToUnsigned<decltype(d)> du;
  const Vec1<uint64_t> vu = BitCast(du, v);
  // 'Shift left' to clear the sign bit, check for exponent=max and mantissa=0.
  return RebindMask(d, (vu + vu) == Set(du, 0xFFE0000000000000ull));
}

HWY_API Mask1<float> IsFinite(const Vec1<float> v) {
  const Vec1<uint32_t> vu = BitCast(Sisd<uint32_t>(), v);
  // Shift left to clear the sign bit, check whether exponent != max value.
  return Mask1<float>::FromBool((vu.raw << 1) < 0xFF000000u);
}
HWY_API Mask1<double> IsFinite(const Vec1<double> v) {
  const Vec1<uint64_t> vu = BitCast(Sisd<uint64_t>(), v);
  // Shift left to clear the sign bit, check whether exponent != max value.
  return Mask1<double>::FromBool((vu.raw << 1) < 0xFFE0000000000000ull);
}

// ================================================== MEMORY

// ------------------------------ Load

template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Vec1<T> Load(D /* tag */, const T* HWY_RESTRICT aligned) {
  T t;
  CopySameSize(aligned, &t);
  return Vec1<T>(t);
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> MaskedLoad(Mask1<T> m, D d, const T* HWY_RESTRICT aligned) {
  return IfThenElseZero(m, Load(d, aligned));
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> MaskedLoadOr(Vec1<T> v, Mask1<T> m, D d,
                             const T* HWY_RESTRICT aligned) {
  return IfThenElse(m, Load(d, aligned), v);
}

template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Vec1<T> LoadU(D d, const T* HWY_RESTRICT p) {
  return Load(d, p);
}

// In some use cases, "load single lane" is sufficient; otherwise avoid this.
template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Vec1<T> LoadDup128(D d, const T* HWY_RESTRICT aligned) {
  return Load(d, aligned);
}

#ifdef HWY_NATIVE_LOAD_N
#undef HWY_NATIVE_LOAD_N
#else
#define HWY_NATIVE_LOAD_N
#endif

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> LoadN(D d, const T* HWY_RESTRICT p,
                        size_t max_lanes_to_load) {
  return (max_lanes_to_load > 0) ? Load(d, p) : Zero(d);
}

template <class D, typename T = TFromD<D>>
HWY_API VFromD<D> LoadNOr(VFromD<D> no, D d, const T* HWY_RESTRICT p,
                          size_t max_lanes_to_load) {
  return (max_lanes_to_load > 0) ? Load(d, p) : no;
}

// ------------------------------ Store

template <class D, typename T = TFromD<D>>
HWY_API void Store(const Vec1<T> v, D /* tag */, T* HWY_RESTRICT aligned) {
  CopySameSize(&v.raw, aligned);
}

template <class D, typename T = TFromD<D>>
HWY_API void StoreU(const Vec1<T> v, D d, T* HWY_RESTRICT p) {
  return Store(v, d, p);
}

template <class D, typename T = TFromD<D>>
HWY_API void BlendedStore(const Vec1<T> v, Mask1<T> m, D d, T* HWY_RESTRICT p) {
  if (!m.bits) return;
  StoreU(v, d, p);
}

#ifdef HWY_NATIVE_STORE_N
#undef HWY_NATIVE_STORE_N
#else
#define HWY_NATIVE_STORE_N
#endif

template <class D, typename T = TFromD<D>>
HWY_API void StoreN(VFromD<D> v, D d, T* HWY_RESTRICT p,
                    size_t max_lanes_to_store) {
  if (max_lanes_to_store > 0) {
    Store(v, d, p);
  }
}

// ------------------------------ Tuples
#include "hwy/ops/inside-inl.h"

// ------------------------------ LoadInterleaved2/3/4

// Per-target flag to prevent generic_ops-inl.h from defining StoreInterleaved2.
#ifdef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#undef HWY_NATIVE_LOAD_STORE_INTERLEAVED
#else
#define HWY_NATIVE_LOAD_STORE_INTERLEAVED
#endif

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved2(D d, const T* HWY_RESTRICT unaligned, Vec1<T>& v0,
                              Vec1<T>& v1) {
  v0 = LoadU(d, unaligned + 0);
  v1 = LoadU(d, unaligned + 1);
}

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved3(D d, const T* HWY_RESTRICT unaligned, Vec1<T>& v0,
                              Vec1<T>& v1, Vec1<T>& v2) {
  v0 = LoadU(d, unaligned + 0);
  v1 = LoadU(d, unaligned + 1);
  v2 = LoadU(d, unaligned + 2);
}

template <class D, typename T = TFromD<D>>
HWY_API void LoadInterleaved4(D d, const T* HWY_RESTRICT unaligned, Vec1<T>& v0,
                              Vec1<T>& v1, Vec1<T>& v2, Vec1<T>& v3) {
  v0 = LoadU(d, unaligned + 0);
  v1 = LoadU(d, unaligned + 1);
  v2 = LoadU(d, unaligned + 2);
  v3 = LoadU(d, unaligned + 3);
}

// ------------------------------ StoreInterleaved2/3/4

template <class D, typename T = TFromD<D>>
HWY_API void StoreInterleaved2(const Vec1<T> v0, const Vec1<T> v1, D d,
                               T* HWY_RESTRICT unaligned) {
  StoreU(v0, d, unaligned + 0);
  StoreU(v1, d, unaligned + 1);
}

template <class D, typename T = TFromD<D>>
HWY_API void StoreInterleaved3(const Vec1<T> v0, const Vec1<T> v1,
                               const Vec1<T> v2, D d,
                               T* HWY_RESTRICT unaligned) {
  StoreU(v0, d, unaligned + 0);
  StoreU(v1, d, unaligned + 1);
  StoreU(v2, d, unaligned + 2);
}

template <class D, typename T = TFromD<D>>
HWY_API void StoreInterleaved4(const Vec1<T> v0, const Vec1<T> v1,
                               const Vec1<T> v2, const Vec1<T> v3, D d,
                               T* HWY_RESTRICT unaligned) {
  StoreU(v0, d, unaligned + 0);
  StoreU(v1, d, unaligned + 1);
  StoreU(v2, d, unaligned + 2);
  StoreU(v3, d, unaligned + 3);
}

// ------------------------------ Stream

template <class D, typename T = TFromD<D>>
HWY_API void Stream(const Vec1<T> v, D d, T* HWY_RESTRICT aligned) {
  return Store(v, d, aligned);
}

// ------------------------------ Scatter

#ifdef HWY_NATIVE_SCATTER
#undef HWY_NATIVE_SCATTER
#else
#define HWY_NATIVE_SCATTER
#endif

template <class D, typename T = TFromD<D>, typename TI>
HWY_API void ScatterOffset(Vec1<T> v, D d, T* base, Vec1<TI> offset) {
  static_assert(sizeof(T) == sizeof(TI), "Index/lane size must match");
  const intptr_t addr =
      reinterpret_cast<intptr_t>(base) + static_cast<intptr_t>(offset.raw);
  Store(v, d, reinterpret_cast<T*>(addr));
}

template <class D, typename T = TFromD<D>, typename TI>
HWY_API void ScatterIndex(Vec1<T> v, D d, T* HWY_RESTRICT base,
                          Vec1<TI> index) {
  static_assert(sizeof(T) == sizeof(TI), "Index/lane size must match");
  Store(v, d, base + index.raw);
}

template <class D, typename T = TFromD<D>, typename TI>
HWY_API void MaskedScatterIndex(Vec1<T> v, Mask1<T> m, D d,
                                T* HWY_RESTRICT base, Vec1<TI> index) {
  static_assert(sizeof(T) == sizeof(TI), "Index/lane size must match");
  if (m.bits) Store(v, d, base + index.raw);
}

// ------------------------------ Gather

#ifdef HWY_NATIVE_GATHER
#undef HWY_NATIVE_GATHER
#else
#define HWY_NATIVE_GATHER
#endif

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> GatherOffset(D d, const T* base, Vec1<MakeSigned<T>> offset) {
  HWY_DASSERT(offset.raw >= 0);
  const intptr_t addr =
      reinterpret_cast<intptr_t>(base) + static_cast<intptr_t>(offset.raw);
  return Load(d, reinterpret_cast<const T*>(addr));
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> GatherIndex(D d, const T* HWY_RESTRICT base,
                            Vec1<MakeSigned<T>> index) {
  HWY_DASSERT(index.raw >= 0);
  return Load(d, base + index.raw);
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> MaskedGatherIndex(Mask1<T> m, D d, const T* HWY_RESTRICT base,
                                  Vec1<MakeSigned<T>> index) {
  HWY_DASSERT(index.raw >= 0);
  return MaskedLoad(m, d, base + index.raw);
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> MaskedGatherIndexOr(Vec1<T> no, Mask1<T> m, D d,
                                    const T* HWY_RESTRICT base,
                                    Vec1<MakeSigned<T>> index) {
  HWY_DASSERT(index.raw >= 0);
  return MaskedLoadOr(no, m, d, base + index.raw);
}

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

#ifdef HWY_NATIVE_PROMOTE_F16_TO_F64
#undef HWY_NATIVE_PROMOTE_F16_TO_F64
#else
#define HWY_NATIVE_PROMOTE_F16_TO_F64
#endif

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom>
HWY_API Vec1<TTo> PromoteTo(DTo /* tag */, Vec1<TFrom> from) {
  static_assert(sizeof(TTo) > sizeof(TFrom), "Not promoting");
  // For bits Y > X, floatX->floatY and intX->intY are always representable.
  return Vec1<TTo>(
      detail::CastValueForPromoteTo<TTo>(hwy::TypeTag<TTo>(), from.raw));
}

#ifdef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F32_TO_UI64_PROMOTE_IN_RANGE_TO
#endif

template <class DTo, HWY_IF_UI64_D(DTo)>
HWY_API VFromD<DTo> PromoteInRangeTo(DTo /* tag */, Vec1<float> from) {
  using TTo = TFromD<DTo>;
  return Vec1<TTo>(detail::CastValueForInRangeF2IConv<TTo>(from.raw));
}

// MSVC 19.10 cannot deduce the argument type if HWY_IF_FLOAT(TFrom) is here,
// so we overload for TFrom=double and TTo={float,int32_t}.
template <class D, HWY_IF_F32_D(D)>
HWY_API Vec1<float> DemoteTo(D /* tag */, Vec1<double> from) {
  // Prevent ubsan errors when converting float to narrower integer/float
  if (IsInf(from).bits ||
      Abs(from).raw > static_cast<double>(HighestValue<float>())) {
    return Vec1<float>(ScalarSignBit(from.raw) ? LowestValue<float>()
                                               : HighestValue<float>());
  }
  return Vec1<float>(static_cast<float>(from.raw));
}
template <class D, HWY_IF_UI32_D(D)>
HWY_API VFromD<D> DemoteTo(D /* tag */, Vec1<double> from) {
  // Prevent ubsan errors when converting int32_t to narrower integer/int32_t
  return Vec1<TFromD<D>>(detail::CastValueForF2IConv<TFromD<D>>(from.raw));
}

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_SIGNED(TFrom), HWY_IF_NOT_FLOAT_NOR_SPECIAL(TFromD<DTo>)>
HWY_API Vec1<TTo> DemoteTo(DTo /* tag */, Vec1<TFrom> from) {
  static_assert(!IsFloat<TFrom>(), "TFrom=double are handled above");
  static_assert(sizeof(TTo) < sizeof(TFrom), "Not demoting");

  // Int to int: choose closest value in TTo to `from` (avoids UB)
  from.raw = HWY_MIN(HWY_MAX(LimitsMin<TTo>(), from.raw), LimitsMax<TTo>());
  return Vec1<TTo>(static_cast<TTo>(from.raw));
}

// Disable the default unsigned to signed DemoteTo implementation in
// generic_ops-inl.h on SCALAR as the SCALAR target has a target-specific
// implementation of the unsigned to signed DemoteTo op and as ReorderDemote2To
// is not supported on the SCALAR target

// NOTE: hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr is used instead of
// hwy::EnableIf<false>* = nullptr to avoid compiler errors since
// !hwy::IsSame<V, V>() is always false and as !hwy::IsSame<V, V>() will cause
// SFINAE to occur instead of a hard error due to a dependency on the V template
// argument
#undef HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V
#define HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V(V) \
  hwy::EnableIf<!hwy::IsSame<V, V>()>* = nullptr

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_UNSIGNED(TFrom), HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(DTo)>
HWY_API Vec1<TTo> DemoteTo(DTo /* tag */, Vec1<TFrom> from) {
  static_assert(!IsFloat<TFrom>(), "TFrom=double are handled above");
  static_assert(sizeof(TTo) < sizeof(TFrom), "Not demoting");

  const auto max = static_cast<MakeUnsigned<TTo>>(LimitsMax<TTo>());

  // Int to int: choose closest value in TTo to `from` (avoids UB)
  return Vec1<TTo>(static_cast<TTo>(HWY_MIN(from.raw, max)));
}

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_UI64(TFrom), HWY_IF_F32_D(DTo)>
HWY_API Vec1<TTo> DemoteTo(DTo /* tag */, Vec1<TFrom> from) {
  // int64_t/uint64_t to float: simply cast to TTo
  return Vec1<TTo>(static_cast<TTo>(from.raw));
}

#ifdef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#undef HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#else
#define HWY_NATIVE_F64_TO_UI32_DEMOTE_IN_RANGE_TO
#endif

template <class D32, HWY_IF_UI32_D(D32)>
HWY_API VFromD<D32> DemoteInRangeTo(D32 /*d32*/,
                                    VFromD<Rebind<double, D32>> v) {
  using TTo = TFromD<D32>;
  return Vec1<TTo>(detail::CastValueForInRangeF2IConv<TTo>(v.raw));
}

// Per-target flag to prevent generic_ops-inl.h from defining f16 conversions;
// use this scalar version to verify the vector implementation.
#ifdef HWY_NATIVE_F16C
#undef HWY_NATIVE_F16C
#else
#define HWY_NATIVE_F16C
#endif

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec1<float> PromoteTo(D /* tag */, const Vec1<float16_t> v) {
  return Vec1<float>(F32FromF16(v.raw));
}

template <class D, HWY_IF_F32_D(D)>
HWY_API Vec1<float> PromoteTo(D d, const Vec1<bfloat16_t> v) {
  return Set(d, F32FromBF16(v.raw));
}

template <class DTo, typename TFrom>
HWY_API VFromD<DTo> PromoteEvenTo(DTo d_to, Vec1<TFrom> v) {
  return PromoteTo(d_to, v);
}

template <class D, HWY_IF_F16_D(D)>
HWY_API Vec1<float16_t> DemoteTo(D /* tag */, const Vec1<float> v) {
  return Vec1<float16_t>(F16FromF32(v.raw));
}

#ifdef HWY_NATIVE_DEMOTE_F32_TO_BF16
#undef HWY_NATIVE_DEMOTE_F32_TO_BF16
#else
#define HWY_NATIVE_DEMOTE_F32_TO_BF16
#endif

template <class D, HWY_IF_BF16_D(D)>
HWY_API Vec1<bfloat16_t> DemoteTo(D d, const Vec1<float> v) {
  return Set(d, BF16FromF32(v.raw));
}

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_FLOAT(TFrom)>
HWY_API Vec1<TTo> ConvertTo(DTo /* tag */, Vec1<TFrom> from) {
  static_assert(sizeof(TTo) == sizeof(TFrom), "Should have same size");
  // float## -> int##: return closest representable value.
  return Vec1<TTo>(detail::CastValueForF2IConv<TTo>(from.raw));
}

template <class DTo, typename TTo = TFromD<DTo>, typename TFrom,
          HWY_IF_NOT_FLOAT(TFrom)>
HWY_API Vec1<TTo> ConvertTo(DTo /* tag */, Vec1<TFrom> from) {
  static_assert(sizeof(TTo) == sizeof(TFrom), "Should have same size");
  // int## -> float##: no check needed
  return Vec1<TTo>(static_cast<TTo>(from.raw));
}

#ifdef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#undef HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#else
#define HWY_NATIVE_F2I_CONVERT_IN_RANGE_TO
#endif

template <class DI, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(DI),
          HWY_IF_T_SIZE_ONE_OF_D(DI, (1 << 4) | (1 << 8))>
HWY_API VFromD<DI> ConvertInRangeTo(DI /*di*/, VFromD<RebindToFloat<DI>> v) {
  using TTo = TFromD<DI>;
  return VFromD<DI>(detail::CastValueForInRangeF2IConv<TTo>(v.raw));
}

HWY_API Vec1<uint8_t> U8FromU32(const Vec1<uint32_t> v) {
  return DemoteTo(Sisd<uint8_t>(), v);
}

// ------------------------------ TruncateTo

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec1<uint8_t> TruncateTo(D /* tag */, Vec1<uint64_t> v) {
  return Vec1<uint8_t>{static_cast<uint8_t>(v.raw & 0xFF)};
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec1<uint16_t> TruncateTo(D /* tag */, Vec1<uint64_t> v) {
  return Vec1<uint16_t>{static_cast<uint16_t>(v.raw & 0xFFFF)};
}

template <class D, HWY_IF_U32_D(D)>
HWY_API Vec1<uint32_t> TruncateTo(D /* tag */, Vec1<uint64_t> v) {
  return Vec1<uint32_t>{static_cast<uint32_t>(v.raw & 0xFFFFFFFFu)};
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec1<uint8_t> TruncateTo(D /* tag */, Vec1<uint32_t> v) {
  return Vec1<uint8_t>{static_cast<uint8_t>(v.raw & 0xFF)};
}

template <class D, HWY_IF_U16_D(D)>
HWY_API Vec1<uint16_t> TruncateTo(D /* tag */, Vec1<uint32_t> v) {
  return Vec1<uint16_t>{static_cast<uint16_t>(v.raw & 0xFFFF)};
}

template <class D, HWY_IF_U8_D(D)>
HWY_API Vec1<uint8_t> TruncateTo(D /* tag */, Vec1<uint16_t> v) {
  return Vec1<uint8_t>{static_cast<uint8_t>(v.raw & 0xFF)};
}

// ================================================== COMBINE
// UpperHalf, ZeroExtendVector, Combine, Concat* are unsupported.

template <typename T>
HWY_API Vec1<T> LowerHalf(Vec1<T> v) {
  return v;
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> LowerHalf(D /* tag */, Vec1<T> v) {
  return v;
}

// ================================================== SWIZZLE

template <typename T>
HWY_API T GetLane(const Vec1<T> v) {
  return v.raw;
}

template <typename T>
HWY_API T ExtractLane(const Vec1<T> v, size_t i) {
  HWY_DASSERT(i == 0);
  (void)i;
  return v.raw;
}

template <typename T>
HWY_API Vec1<T> InsertLane(Vec1<T> v, size_t i, T t) {
  HWY_DASSERT(i == 0);
  (void)i;
  v.raw = t;
  return v;
}

template <typename T>
HWY_API Vec1<T> DupEven(Vec1<T> v) {
  return v;
}
// DupOdd is unsupported.

template <typename T>
HWY_API Vec1<T> OddEven(Vec1<T> /* odd */, Vec1<T> even) {
  return even;
}

template <typename T>
HWY_API Vec1<T> OddEvenBlocks(Vec1<T> /* odd */, Vec1<T> even) {
  return even;
}

// ------------------------------ SwapAdjacentBlocks

template <typename T>
HWY_API Vec1<T> SwapAdjacentBlocks(Vec1<T> v) {
  return v;
}

// ------------------------------ TableLookupLanes

// Returned by SetTableIndices for use by TableLookupLanes.
template <typename T>
struct Indices1 {
  MakeSigned<T> raw;
};

template <class D, typename T = TFromD<D>, typename TI>
HWY_API Indices1<T> IndicesFromVec(D, Vec1<TI> vec) {
  static_assert(sizeof(T) == sizeof(TI), "Index size must match lane size");
  HWY_DASSERT(vec.raw <= 1);
  return Indices1<T>{static_cast<MakeSigned<T>>(vec.raw)};
}

template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>, typename TI>
HWY_API Indices1<T> SetTableIndices(D d, const TI* idx) {
  return IndicesFromVec(d, LoadU(Sisd<TI>(), idx));
}

template <typename T>
HWY_API Vec1<T> TableLookupLanes(const Vec1<T> v, const Indices1<T> /* idx */) {
  return v;
}

template <typename T>
HWY_API Vec1<T> TwoTablesLookupLanes(const Vec1<T> a, const Vec1<T> b,
                                     const Indices1<T> idx) {
  return (idx.raw == 0) ? a : b;
}

// ------------------------------ ReverseBlocks

// Single block: no change
template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> ReverseBlocks(D /* tag */, const Vec1<T> v) {
  return v;
}

// ------------------------------ Reverse

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> Reverse(D /* tag */, const Vec1<T> v) {
  return v;
}

// Per-target flag to prevent generic_ops-inl.h defining 8-bit Reverse2/4/8.
#ifdef HWY_NATIVE_REVERSE2_8
#undef HWY_NATIVE_REVERSE2_8
#else
#define HWY_NATIVE_REVERSE2_8
#endif

// Must not be called:
template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> Reverse2(D /* tag */, const Vec1<T> v) {
  return v;
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> Reverse4(D /* tag */, const Vec1<T> v) {
  return v;
}

template <class D, typename T = TFromD<D>>
HWY_API Vec1<T> Reverse8(D /* tag */, const Vec1<T> v) {
  return v;
}

// ------------------------------ ReverseLaneBytes

#ifdef HWY_NATIVE_REVERSE_LANE_BYTES
#undef HWY_NATIVE_REVERSE_LANE_BYTES
#else
#define HWY_NATIVE_REVERSE_LANE_BYTES
#endif

HWY_API Vec1<uint16_t> ReverseLaneBytes(Vec1<uint16_t> v) {
  const uint32_t val{v.raw};
  return Vec1<uint16_t>(
      static_cast<uint16_t>(((val << 8) & 0xFF00u) | ((val >> 8) & 0x00FFu)));
}

HWY_API Vec1<uint32_t> ReverseLaneBytes(Vec1<uint32_t> v) {
  const uint32_t val = v.raw;
  return Vec1<uint32_t>(static_cast<uint32_t>(
      ((val << 24) & 0xFF000000u) | ((val << 8) & 0x00FF0000u) |
      ((val >> 8) & 0x0000FF00u) | ((val >> 24) & 0x000000FFu)));
}

HWY_API Vec1<uint64_t> ReverseLaneBytes(Vec1<uint64_t> v) {
  const uint64_t val = v.raw;
  return Vec1<uint64_t>(static_cast<uint64_t>(
      ((val << 56) & 0xFF00000000000000u) |
      ((val << 40) & 0x00FF000000000000u) |
      ((val << 24) & 0x0000FF0000000000u) | ((val << 8) & 0x000000FF00000000u) |
      ((val >> 8) & 0x00000000FF000000u) | ((val >> 24) & 0x0000000000FF0000u) |
      ((val >> 40) & 0x000000000000FF00u) |
      ((val >> 56) & 0x00000000000000FFu)));
}

template <class V, HWY_IF_SIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API V ReverseLaneBytes(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, ReverseLaneBytes(BitCast(du, v)));
}

// ------------------------------ ReverseBits
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

namespace detail {

template <class T>
HWY_INLINE T ReverseBitsOfEachByte(T val) {
  using TU = MakeUnsigned<T>;
  constexpr TU kMaxUnsignedVal{LimitsMax<TU>()};
  constexpr TU kShrMask1 =
      static_cast<TU>(0x5555555555555555u & kMaxUnsignedVal);
  constexpr TU kShrMask2 =
      static_cast<TU>(0x3333333333333333u & kMaxUnsignedVal);
  constexpr TU kShrMask3 =
      static_cast<TU>(0x0F0F0F0F0F0F0F0Fu & kMaxUnsignedVal);

  constexpr TU kShlMask1 = static_cast<TU>(~kShrMask1);
  constexpr TU kShlMask2 = static_cast<TU>(~kShrMask2);
  constexpr TU kShlMask3 = static_cast<TU>(~kShrMask3);

  TU result = static_cast<TU>(val);
  result = static_cast<TU>(((result << 1) & kShlMask1) |
                           ((result >> 1) & kShrMask1));
  result = static_cast<TU>(((result << 2) & kShlMask2) |
                           ((result >> 2) & kShrMask2));
  result = static_cast<TU>(((result << 4) & kShlMask3) |
                           ((result >> 4) & kShrMask3));
  return static_cast<T>(result);
}

}  // namespace detail

template <class V, HWY_IF_UNSIGNED_V(V), HWY_IF_T_SIZE_V(V, 1)>
HWY_API V ReverseBits(V v) {
  return V(detail::ReverseBitsOfEachByte(v.raw));
}

template <class V, HWY_IF_UNSIGNED_V(V),
          HWY_IF_T_SIZE_ONE_OF_V(V, (1 << 2) | (1 << 4) | (1 << 8))>
HWY_API V ReverseBits(V v) {
  return ReverseLaneBytes(V(detail::ReverseBitsOfEachByte(v.raw)));
}

template <class V, HWY_IF_SIGNED_V(V)>
HWY_API V ReverseBits(V v) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;
  return BitCast(d, ReverseBits(BitCast(du, v)));
}

// ------------------------------ SlideUpLanes

template <typename D>
HWY_API VFromD<D> SlideUpLanes(D /*d*/, VFromD<D> v, size_t /*amt*/) {
  return v;
}

// ------------------------------ SlideDownLanes

template <typename D>
HWY_API VFromD<D> SlideDownLanes(D /*d*/, VFromD<D> v, size_t /*amt*/) {
  return v;
}

// ================================================== BLOCKWISE
// Shift*Bytes, CombineShiftRightBytes, Interleave*, Shuffle* are unsupported.

// ------------------------------ Broadcast/splat any lane

template <int kLane, typename T>
HWY_API Vec1<T> Broadcast(const Vec1<T> v) {
  static_assert(kLane == 0, "Scalar only has one lane");
  return v;
}

// ------------------------------ TableLookupBytes, TableLookupBytesOr0

template <typename T, typename TI>
HWY_API Vec1<TI> TableLookupBytes(const Vec1<T> in, const Vec1<TI> indices) {
  uint8_t in_bytes[sizeof(T)];
  uint8_t idx_bytes[sizeof(T)];
  uint8_t out_bytes[sizeof(T)];
  CopyBytes<sizeof(T)>(&in, &in_bytes);  // copy to bytes
  CopyBytes<sizeof(T)>(&indices, &idx_bytes);
  for (size_t i = 0; i < sizeof(T); ++i) {
    out_bytes[i] = in_bytes[idx_bytes[i]];
  }
  TI out;
  CopyBytes<sizeof(TI)>(&out_bytes, &out);
  return Vec1<TI>{out};
}

template <typename T, typename TI>
HWY_API Vec1<TI> TableLookupBytesOr0(const Vec1<T> in, const Vec1<TI> indices) {
  uint8_t in_bytes[sizeof(T)];
  uint8_t idx_bytes[sizeof(T)];
  uint8_t out_bytes[sizeof(T)];
  CopyBytes<sizeof(T)>(&in, &in_bytes);  // copy to bytes
  CopyBytes<sizeof(T)>(&indices, &idx_bytes);
  for (size_t i = 0; i < sizeof(T); ++i) {
    out_bytes[i] = idx_bytes[i] & 0x80 ? 0 : in_bytes[idx_bytes[i]];
  }
  TI out;
  CopyBytes<sizeof(TI)>(&out_bytes, &out);
  return Vec1<TI>{out};
}

// ------------------------------ ZipLower

HWY_API Vec1<uint16_t> ZipLower(Vec1<uint8_t> a, Vec1<uint8_t> b) {
  return Vec1<uint16_t>(static_cast<uint16_t>((uint32_t{b.raw} << 8) + a.raw));
}
HWY_API Vec1<uint32_t> ZipLower(Vec1<uint16_t> a, Vec1<uint16_t> b) {
  return Vec1<uint32_t>((uint32_t{b.raw} << 16) + a.raw);
}
HWY_API Vec1<uint64_t> ZipLower(Vec1<uint32_t> a, Vec1<uint32_t> b) {
  return Vec1<uint64_t>((uint64_t{b.raw} << 32) + a.raw);
}
HWY_API Vec1<int16_t> ZipLower(Vec1<int8_t> a, Vec1<int8_t> b) {
  return Vec1<int16_t>(static_cast<int16_t>((int32_t{b.raw} << 8) + a.raw));
}
HWY_API Vec1<int32_t> ZipLower(Vec1<int16_t> a, Vec1<int16_t> b) {
  return Vec1<int32_t>((int32_t{b.raw} << 16) + a.raw);
}
HWY_API Vec1<int64_t> ZipLower(Vec1<int32_t> a, Vec1<int32_t> b) {
  return Vec1<int64_t>((int64_t{b.raw} << 32) + a.raw);
}

template <class DW, typename TW = TFromD<DW>, typename TN = MakeNarrow<TW>>
HWY_API Vec1<TW> ZipLower(DW /* tag */, Vec1<TN> a, Vec1<TN> b) {
  return Vec1<TW>(static_cast<TW>((TW{b.raw} << (sizeof(TN) * 8)) + a.raw));
}

// ================================================== MASK

template <class D, typename T = TFromD<D>>
HWY_API bool AllFalse(D /* tag */, const Mask1<T> mask) {
  return mask.bits == 0;
}

template <class D, typename T = TFromD<D>>
HWY_API bool AllTrue(D /* tag */, const Mask1<T> mask) {
  return mask.bits != 0;
}

// `p` points to at least 8 readable bytes, not all of which need be valid.
template <class D, HWY_IF_LANES_D(D, 1), typename T = TFromD<D>>
HWY_API Mask1<T> LoadMaskBits(D /* tag */, const uint8_t* HWY_RESTRICT bits) {
  return Mask1<T>::FromBool((bits[0] & 1) != 0);
}

template <class D, HWY_IF_LANES_D(D, 1)>
HWY_API MFromD<D> Dup128MaskFromMaskBits(D /*d*/, unsigned mask_bits) {
  return MFromD<D>::FromBool((mask_bits & 1) != 0);
}

// `p` points to at least 8 writable bytes.
template <class D, typename T = TFromD<D>>
HWY_API size_t StoreMaskBits(D d, const Mask1<T> mask, uint8_t* bits) {
  *bits = AllTrue(d, mask);
  return 1;
}

template <class D, typename T = TFromD<D>>
HWY_API size_t CountTrue(D /* tag */, const Mask1<T> mask) {
  return mask.bits == 0 ? 0 : 1;
}

template <class D, typename T = TFromD<D>>
HWY_API intptr_t FindFirstTrue(D /* tag */, const Mask1<T> mask) {
  return mask.bits == 0 ? -1 : 0;
}

template <class D, typename T = TFromD<D>>
HWY_API size_t FindKnownFirstTrue(D /* tag */, const Mask1<T> /* m */) {
  return 0;  // There is only one lane and we know it is true.
}

template <class D, typename T = TFromD<D>>
HWY_API intptr_t FindLastTrue(D /* tag */, const Mask1<T> mask) {
  return mask.bits == 0 ? -1 : 0;
}

template <class D, typename T = TFromD<D>>
HWY_API size_t FindKnownLastTrue(D /* tag */, const Mask1<T> /* m */) {
  return 0;  // There is only one lane and we know it is true.
}

// ------------------------------ Compress, CompressBits

template <typename T>
struct CompressIsPartition {
  enum { value = 1 };
};

template <typename T>
HWY_API Vec1<T> Compress(Vec1<T> v, const Mask1<T> /* mask */) {
  // A single lane is already partitioned by definition.
  return v;
}

template <typename T>
HWY_API Vec1<T> CompressNot(Vec1<T> v, const Mask1<T> /* mask */) {
  // A single lane is already partitioned by definition.
  return v;
}

// ------------------------------ CompressStore
template <class D, typename T = TFromD<D>>
HWY_API size_t CompressStore(Vec1<T> v, const Mask1<T> mask, D d,
                             T* HWY_RESTRICT unaligned) {
  StoreU(Compress(v, mask), d, unaligned);
  return CountTrue(d, mask);
}

// ------------------------------ CompressBlendedStore
template <class D, typename T = TFromD<D>>
HWY_API size_t CompressBlendedStore(Vec1<T> v, const Mask1<T> mask, D d,
                                    T* HWY_RESTRICT unaligned) {
  if (!mask.bits) return 0;
  StoreU(v, d, unaligned);
  return 1;
}

// ------------------------------ CompressBits
template <typename T>
HWY_API Vec1<T> CompressBits(Vec1<T> v, const uint8_t* HWY_RESTRICT /*bits*/) {
  return v;
}

// ------------------------------ CompressBitsStore
template <class D, typename T = TFromD<D>>
HWY_API size_t CompressBitsStore(Vec1<T> v, const uint8_t* HWY_RESTRICT bits,
                                 D d, T* HWY_RESTRICT unaligned) {
  const Mask1<T> mask = LoadMaskBits(d, bits);
  StoreU(Compress(v, mask), d, unaligned);
  return CountTrue(d, mask);
}

// ------------------------------ Expand

// generic_ops-inl.h requires Vec64/128, so implement [Load]Expand here.
#ifdef HWY_NATIVE_EXPAND
#undef HWY_NATIVE_EXPAND
#else
#define HWY_NATIVE_EXPAND
#endif

template <typename T>
HWY_API Vec1<T> Expand(Vec1<T> v, const Mask1<T> mask) {
  return IfThenElseZero(mask, v);
}

// ------------------------------ LoadExpand
template <class D>
HWY_API VFromD<D> LoadExpand(MFromD<D> mask, D d,
                             const TFromD<D>* HWY_RESTRICT unaligned) {
  return MaskedLoad(mask, d, unaligned);
}

// ------------------------------ WidenMulPairwiseAdd

template <class D32, HWY_IF_F32_D(D32)>
HWY_API Vec1<float> WidenMulPairwiseAdd(D32 /* tag */, Vec1<bfloat16_t> a,
                                        Vec1<bfloat16_t> b) {
  return Vec1<float>(F32FromBF16(a.raw)) * Vec1<float>(F32FromBF16(b.raw));
}

template <class D32, HWY_IF_I32_D(D32)>
HWY_API Vec1<int32_t> WidenMulPairwiseAdd(D32 /* tag */, Vec1<int16_t> a,
                                          Vec1<int16_t> b) {
  return Vec1<int32_t>(a.raw * b.raw);
}

// ------------------------------ SatWidenMulAccumFixedPoint
#ifdef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#undef HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#else
#define HWY_NATIVE_I16_SATWIDENMULACCUMFIXEDPOINT
#endif

template <class DI32, HWY_IF_I32_D(DI32)>
HWY_API VFromD<DI32> SatWidenMulAccumFixedPoint(DI32 di32,
                                                VFromD<Rebind<int16_t, DI32>> a,
                                                VFromD<Rebind<int16_t, DI32>> b,
                                                VFromD<DI32> sum) {
  // Multiplying static_cast<int32_t>(a.raw) by static_cast<int32_t>(b.raw)
  // followed by an addition of the product is okay as
  // (a.raw * b.raw * 2) is between -2147418112 and 2147483648 and as
  // a.raw * b.raw * 2 can only overflow an int32_t if both a.raw and b.raw are
  // equal to -32768.

  const VFromD<DI32> product(static_cast<int32_t>(a.raw) *
                             static_cast<int32_t>(b.raw));
  const VFromD<DI32> product2 = Add(product, product);

  const auto mul_overflow =
      VecFromMask(di32, Eq(product2, Set(di32, LimitsMin<int32_t>())));

  return SaturatedAdd(Sub(sum, And(BroadcastSignBit(sum), mul_overflow)),
                      Add(product2, mul_overflow));
}

// ------------------------------ SatWidenMulPairwiseAdd

#ifdef HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#undef HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#else
#define HWY_NATIVE_U8_I8_SATWIDENMULPAIRWISEADD
#endif

template <class DI16, HWY_IF_I16_D(DI16)>
HWY_API Vec1<int16_t> SatWidenMulPairwiseAdd(DI16 /* tag */, Vec1<uint8_t> a,
                                             Vec1<int8_t> b) {
  // Saturation of a.raw * b.raw is not needed on the HWY_SCALAR target as the
  // input vectors only have 1 lane on the HWY_SCALAR target and as
  // a.raw * b.raw is between -32640 and 32385, which is already within the
  // range of an int16_t.

  // On other targets, a saturated addition of a[0]*b[0] + a[1]*b[1] is needed
  // as it is possible for the addition of a[0]*b[0] + a[1]*b[1] to overflow if
  // a[0], a[1], b[0], and b[1] are all non-zero and b[0] and b[1] both have the
  // same sign.

  return Vec1<int16_t>(static_cast<int16_t>(a.raw) *
                       static_cast<int16_t>(b.raw));
}

// ------------------------------ ReorderWidenMulAccumulate (MulAdd, ZipLower)

#ifdef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#undef HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#else
#define HWY_NATIVE_REORDER_WIDEN_MUL_ACC_BF16
#endif

template <class D32, HWY_IF_F32_D(D32)>
HWY_API Vec1<float> ReorderWidenMulAccumulate(D32 /* tag */, Vec1<bfloat16_t> a,
                                              Vec1<bfloat16_t> b,
                                              const Vec1<float> sum0,
                                              Vec1<float>& /* sum1 */) {
  return MulAdd(Vec1<float>(F32FromBF16(a.raw)),
                Vec1<float>(F32FromBF16(b.raw)), sum0);
}

template <class D32, HWY_IF_I32_D(D32)>
HWY_API Vec1<int32_t> ReorderWidenMulAccumulate(D32 /* tag */, Vec1<int16_t> a,
                                                Vec1<int16_t> b,
                                                const Vec1<int32_t> sum0,
                                                Vec1<int32_t>& /* sum1 */) {
  return Vec1<int32_t>(a.raw * b.raw + sum0.raw);
}

template <class DU32, HWY_IF_U32_D(DU32)>
HWY_API Vec1<uint32_t> ReorderWidenMulAccumulate(DU32 /* tag */,
                                                 Vec1<uint16_t> a,
                                                 Vec1<uint16_t> b,
                                                 const Vec1<uint32_t> sum0,
                                                 Vec1<uint32_t>& /* sum1 */) {
  return Vec1<uint32_t>(static_cast<uint32_t>(a.raw) * b.raw + sum0.raw);
}

// ------------------------------ RearrangeToOddPlusEven
template <typename TW>
HWY_API Vec1<TW> RearrangeToOddPlusEven(Vec1<TW> sum0, Vec1<TW> /* sum1 */) {
  return sum0;  // invariant already holds
}

// ================================================== REDUCTIONS

// Nothing native, generic_ops-inl defines SumOfLanes and ReduceSum.

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
