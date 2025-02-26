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

// Must be included inside an existing include guard, with the following ops
// already defined: BitCast, And, Set, ShiftLeft, ShiftRight, PromoteLowerTo,
// ConcatEven, ConcatOdd, plus the optional detail::PromoteEvenTo and
// detail::PromoteOddTo (if implemented in the target-specific header).

// This is normally set by set_macros-inl.h before this header is included;
// if not, we are viewing this header standalone. Reduce IDE errors by:
#if !defined(HWY_NAMESPACE)
// 1) Defining HWY_IDE so we get syntax highlighting rather than all-gray text.
#include "hwy/ops/shared-inl.h"
// 2) Entering the HWY_NAMESPACE to make definitions from shared-inl.h visible.
HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
#define HWY_INSIDE_END_NAMESPACE
// 3) Providing a dummy VFromD (usually done by the target-specific header).
template <class D>
using VFromD = int;
template <class D>
using TFromV = int;
template <class D>
struct DFromV {};
#endif

// ------------------------------ Vec/Create/Get/Set2..4

// On SVE and RVV, Vec2..4 are aliases to built-in types. Also exclude the
// fixed-size SVE targets.
#if HWY_IDE || (!HWY_HAVE_SCALABLE && !HWY_TARGET_IS_SVE)

// NOTE: these are used inside arm_neon-inl.h, hence they cannot be defined in
// generic_ops-inl.h, which is included after that.
template <class D>
struct Vec2 {
  VFromD<D> v0;
  VFromD<D> v1;
};

template <class D>
struct Vec3 {
  VFromD<D> v0;
  VFromD<D> v1;
  VFromD<D> v2;
};

template <class D>
struct Vec4 {
  VFromD<D> v0;
  VFromD<D> v1;
  VFromD<D> v2;
  VFromD<D> v3;
};

// D arg is unused but allows deducing D.
template <class D>
HWY_API Vec2<D> Create2(D /* tag */, VFromD<D> v0, VFromD<D> v1) {
  return Vec2<D>{v0, v1};
}

template <class D>
HWY_API Vec3<D> Create3(D /* tag */, VFromD<D> v0, VFromD<D> v1, VFromD<D> v2) {
  return Vec3<D>{v0, v1, v2};
}

template <class D>
HWY_API Vec4<D> Create4(D /* tag */, VFromD<D> v0, VFromD<D> v1, VFromD<D> v2,
                        VFromD<D> v3) {
  return Vec4<D>{v0, v1, v2, v3};
}

template <size_t kIndex, class D>
HWY_API VFromD<D> Get2(Vec2<D> tuple) {
  static_assert(kIndex < 2, "Tuple index out of bounds");
  return kIndex == 0 ? tuple.v0 : tuple.v1;
}

template <size_t kIndex, class D>
HWY_API VFromD<D> Get3(Vec3<D> tuple) {
  static_assert(kIndex < 3, "Tuple index out of bounds");
  return kIndex == 0 ? tuple.v0 : kIndex == 1 ? tuple.v1 : tuple.v2;
}

template <size_t kIndex, class D>
HWY_API VFromD<D> Get4(Vec4<D> tuple) {
  static_assert(kIndex < 4, "Tuple index out of bounds");
  return kIndex == 0   ? tuple.v0
         : kIndex == 1 ? tuple.v1
         : kIndex == 2 ? tuple.v2
                       : tuple.v3;
}

template <size_t kIndex, class D>
HWY_API Vec2<D> Set2(Vec2<D> tuple, VFromD<D> val) {
  static_assert(kIndex < 2, "Tuple index out of bounds");
  if (kIndex == 0) {
    tuple.v0 = val;
  } else {
    tuple.v1 = val;
  }
  return tuple;
}

template <size_t kIndex, class D>
HWY_API Vec3<D> Set3(Vec3<D> tuple, VFromD<D> val) {
  static_assert(kIndex < 3, "Tuple index out of bounds");
  if (kIndex == 0) {
    tuple.v0 = val;
  } else if (kIndex == 1) {
    tuple.v1 = val;
  } else {
    tuple.v2 = val;
  }
  return tuple;
}

template <size_t kIndex, class D>
HWY_API Vec4<D> Set4(Vec4<D> tuple, VFromD<D> val) {
  static_assert(kIndex < 4, "Tuple index out of bounds");
  if (kIndex == 0) {
    tuple.v0 = val;
  } else if (kIndex == 1) {
    tuple.v1 = val;
  } else if (kIndex == 2) {
    tuple.v2 = val;
  } else {
    tuple.v3 = val;
  }
  return tuple;
}

#endif  // !HWY_HAVE_SCALABLE || HWY_IDE

// ------------------------------ Rol/Ror (And, Or, Neg, Shl, Shr)
#if (defined(HWY_NATIVE_ROL_ROR_8) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_8
#undef HWY_NATIVE_ROL_ROR_8
#else
#define HWY_NATIVE_ROL_ROR_8
#endif

template <class V, HWY_IF_UI8(TFromV<V>)>
HWY_API V Rol(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint8_t{7});
  const auto shl_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shr_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

template <class V, HWY_IF_UI8(TFromV<V>)>
HWY_API V Ror(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint8_t{7});
  const auto shr_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shl_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

#endif  // HWY_NATIVE_ROL_ROR_8

#if (defined(HWY_NATIVE_ROL_ROR_16) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_16
#undef HWY_NATIVE_ROL_ROR_16
#else
#define HWY_NATIVE_ROL_ROR_16
#endif

template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V Rol(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint16_t{15});
  const auto shl_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shr_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V Ror(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint16_t{15});
  const auto shr_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shl_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

#endif  // HWY_NATIVE_ROL_ROR_16

#if (defined(HWY_NATIVE_ROL_ROR_32_64) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_32_64
#undef HWY_NATIVE_ROL_ROR_32_64
#else
#define HWY_NATIVE_ROL_ROR_32_64
#endif

template <class V, HWY_IF_UI32(TFromV<V>)>
HWY_API V Rol(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint32_t{31});
  const auto shl_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shr_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

template <class V, HWY_IF_UI32(TFromV<V>)>
HWY_API V Ror(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint32_t{31});
  const auto shr_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shl_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

#if HWY_HAVE_INTEGER64
template <class V, HWY_IF_UI64(TFromV<V>)>
HWY_API V Rol(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint64_t{63});
  const auto shl_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shr_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}

template <class V, HWY_IF_UI64(TFromV<V>)>
HWY_API V Ror(V a, V b) {
  const DFromV<decltype(a)> d;
  const RebindToSigned<decltype(d)> di;
  const RebindToUnsigned<decltype(d)> du;

  const auto shift_amt_mask = Set(du, uint64_t{63});
  const auto shr_amt = And(BitCast(du, b), shift_amt_mask);
  const auto shl_amt = And(BitCast(du, Neg(BitCast(di, b))), shift_amt_mask);

  const auto vu = BitCast(du, a);
  return BitCast(d, Or(Shl(vu, shl_amt), Shr(vu, shr_amt)));
}
#endif  // HWY_HAVE_INTEGER64

#endif  // HWY_NATIVE_ROL_ROR_32_64

// ------------------------------ RotateLeftSame/RotateRightSame

#if (defined(HWY_NATIVE_ROL_ROR_SAME_8) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_SAME_8
#undef HWY_NATIVE_ROL_ROR_SAME_8
#else
#define HWY_NATIVE_ROL_ROR_SAME_8
#endif

template <class V, HWY_IF_UI8(TFromV<V>)>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shl_amt = bits & 7;
  const int shr_amt = static_cast<int>((0u - static_cast<unsigned>(bits)) & 7u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

template <class V, HWY_IF_UI8(TFromV<V>)>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shr_amt = bits & 7;
  const int shl_amt = static_cast<int>((0u - static_cast<unsigned>(bits)) & 7u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

#endif  // HWY_NATIVE_ROL_ROR_SAME_8

#if (defined(HWY_NATIVE_ROL_ROR_SAME_16) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_SAME_16
#undef HWY_NATIVE_ROL_ROR_SAME_16
#else
#define HWY_NATIVE_ROL_ROR_SAME_16
#endif

template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shl_amt = bits & 15;
  const int shr_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 15u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

template <class V, HWY_IF_UI16(TFromV<V>)>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shr_amt = bits & 15;
  const int shl_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 15u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}
#endif  // HWY_NATIVE_ROL_ROR_SAME_16

#if (defined(HWY_NATIVE_ROL_ROR_SAME_32_64) == defined(HWY_TARGET_TOGGLE))
#ifdef HWY_NATIVE_ROL_ROR_SAME_32_64
#undef HWY_NATIVE_ROL_ROR_SAME_32_64
#else
#define HWY_NATIVE_ROL_ROR_SAME_32_64
#endif

template <class V, HWY_IF_UI32(TFromV<V>)>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shl_amt = bits & 31;
  const int shr_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 31u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

template <class V, HWY_IF_UI32(TFromV<V>)>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shr_amt = bits & 31;
  const int shl_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 31u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

#if HWY_HAVE_INTEGER64
template <class V, HWY_IF_UI64(TFromV<V>)>
HWY_API V RotateLeftSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shl_amt = bits & 63;
  const int shr_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 63u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}

template <class V, HWY_IF_UI64(TFromV<V>)>
HWY_API V RotateRightSame(V v, int bits) {
  const DFromV<decltype(v)> d;
  const RebindToUnsigned<decltype(d)> du;

  const int shr_amt = bits & 63;
  const int shl_amt =
      static_cast<int>((0u - static_cast<unsigned>(bits)) & 63u);

  const auto vu = BitCast(du, v);
  return BitCast(d,
                 Or(ShiftLeftSame(vu, shl_amt), ShiftRightSame(vu, shr_amt)));
}
#endif  // HWY_HAVE_INTEGER64

#endif  // HWY_NATIVE_ROL_ROR_SAME_32_64

// ------------------------------ PromoteEvenTo/PromoteOddTo

// These are used by target-specific headers for ReorderWidenMulAccumulate etc.

#if HWY_TARGET != HWY_SCALAR || HWY_IDE
namespace detail {

// Tag dispatch is used in detail::PromoteEvenTo and detail::PromoteOddTo as
// there are target-specific specializations for some of the
// detail::PromoteEvenTo and detail::PromoteOddTo cases on
// SVE/PPC/SSE2/SSSE3/SSE4/AVX2.

// All targets except HWY_SCALAR use the implementations of
// detail::PromoteEvenTo and detail::PromoteOddTo in generic_ops-inl.h for at
// least some of the PromoteEvenTo and PromoteOddTo cases.

// Signed to signed PromoteEvenTo/PromoteOddTo
template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(
    hwy::SignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::SignedTag /*from_type_tag*/, D d_to, V v) {
#if HWY_TARGET_IS_SVE
  // The intrinsic expects the wide lane type.
  return NativePromoteEvenTo(BitCast(d_to, v));
#else
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian targets, need to shift each lane of the bitcasted
  // vector left by kToLaneSize * 4 bits to get the bits of the even
  // source lanes into the upper kToLaneSize * 4 bits of even_in_hi.
  const auto even_in_hi = ShiftLeft<kToLaneSize * 4>(BitCast(d_to, v));
#else
  // On big-endian targets, the bits of the even source lanes are already
  // in the upper kToLaneSize * 4 bits of the lanes of the bitcasted
  // vector.
  const auto even_in_hi = BitCast(d_to, v);
#endif

  // Right-shift even_in_hi by kToLaneSize * 4 bits
  return ShiftRight<kToLaneSize * 4>(even_in_hi);
#endif  // HWY_TARGET_IS_SVE
}

// Unsigned to unsigned PromoteEvenTo/PromoteOddTo
template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(
    hwy::UnsignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::UnsignedTag /*from_type_tag*/, D d_to, V v) {
#if HWY_TARGET_IS_SVE
  // The intrinsic expects the wide lane type.
  return NativePromoteEvenTo(BitCast(d_to, v));
#else
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian targets, the bits of the even source lanes are already
  // in the lower kToLaneSize * 4 bits of the lanes of the bitcasted vector.

  // Simply need to zero out the upper bits of each lane of the bitcasted
  // vector.
  return And(BitCast(d_to, v),
             Set(d_to, static_cast<TFromD<D>>(LimitsMax<TFromV<V>>())));
#else
  // On big-endian targets, need to shift each lane of the bitcasted vector
  // right by kToLaneSize * 4 bits to get the bits of the even source lanes into
  // the lower kToLaneSize * 4 bits of the result.

  // The right shift below will zero out the upper kToLaneSize * 4 bits of the
  // result.
  return ShiftRight<kToLaneSize * 4>(BitCast(d_to, v));
#endif
#endif  // HWY_TARGET_IS_SVE
}

template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(
    hwy::SignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::SignedTag /*from_type_tag*/, D d_to, V v) {
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian targets, the bits of the odd source lanes are already in
  // the upper kToLaneSize * 4 bits of the lanes of the bitcasted vector.
  const auto odd_in_hi = BitCast(d_to, v);
#else
  // On big-endian targets, need to shift each lane of the bitcasted vector
  // left by kToLaneSize * 4 bits to get the bits of the odd source lanes into
  // the upper kToLaneSize * 4 bits of odd_in_hi.
  const auto odd_in_hi = ShiftLeft<kToLaneSize * 4>(BitCast(d_to, v));
#endif

  // Right-shift odd_in_hi by kToLaneSize * 4 bits
  return ShiftRight<kToLaneSize * 4>(odd_in_hi);
}

template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(
    hwy::UnsignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::UnsignedTag /*from_type_tag*/, D d_to, V v) {
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian targets, need to shift each lane of the bitcasted vector
  // right by kToLaneSize * 4 bits to get the bits of the odd source lanes into
  // the lower kToLaneSize * 4 bits of the result.

  // The right shift below will zero out the upper kToLaneSize * 4 bits of the
  // result.
  return ShiftRight<kToLaneSize * 4>(BitCast(d_to, v));
#else
  // On big-endian targets, the bits of the even source lanes are already
  // in the lower kToLaneSize * 4 bits of the lanes of the bitcasted vector.

  // Simply need to zero out the upper bits of each lane of the bitcasted
  // vector.
  return And(BitCast(d_to, v),
             Set(d_to, static_cast<TFromD<D>>(LimitsMax<TFromV<V>>())));
#endif
}

// Unsigned to signed: Same as unsigned->unsigned PromoteEvenTo/PromoteOddTo
// followed by BitCast to signed
template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteEvenTo(
    hwy::SignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::UnsignedTag /*from_type_tag*/, D d_to, V v) {
  const RebindToUnsigned<decltype(d_to)> du_to;
  return BitCast(d_to,
                 PromoteEvenTo(hwy::UnsignedTag(), hwy::SizeTag<kToLaneSize>(),
                               hwy::UnsignedTag(), du_to, v));
}

template <size_t kToLaneSize, class D, class V>
HWY_INLINE VFromD<D> PromoteOddTo(
    hwy::SignedTag /*to_type_tag*/,
    hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    hwy::UnsignedTag /*from_type_tag*/, D d_to, V v) {
  const RebindToUnsigned<decltype(d_to)> du_to;
  return BitCast(d_to,
                 PromoteOddTo(hwy::UnsignedTag(), hwy::SizeTag<kToLaneSize>(),
                              hwy::UnsignedTag(), du_to, v));
}

// BF16->F32 PromoteEvenTo

// NOTE: It is possible for FromTypeTag to be hwy::SignedTag or hwy::UnsignedTag
// instead of hwy::FloatTag on targets that use scalable vectors.

// VBF16 is considered to be a bfloat16_t vector if TFromV<VBF16> is the same
// type as TFromV<VFromD<Repartition<bfloat16_t, DF32>>>

// The BF16->F32 PromoteEvenTo overload is only enabled if VBF16 is considered
// to be a bfloat16_t vector.
template <class FromTypeTag, class DF32, class VBF16,
          class VBF16_2 = VFromD<Repartition<bfloat16_t, DF32>>,
          hwy::EnableIf<IsSame<TFromV<VBF16>, TFromV<VBF16_2>>()>* = nullptr>
HWY_INLINE VFromD<DF32> PromoteEvenTo(hwy::FloatTag /*to_type_tag*/,
                                      hwy::SizeTag<4> /*to_lane_size_tag*/,
                                      FromTypeTag /*from_type_tag*/, DF32 d_to,
                                      VBF16 v) {
  const RebindToUnsigned<decltype(d_to)> du_to;
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian platforms, need to shift left each lane of the bitcasted
  // vector by 16 bits.
  return BitCast(d_to, ShiftLeft<16>(BitCast(du_to, v)));
#else
  // On big-endian platforms, the even lanes of the source vector are already
  // in the upper 16 bits of the lanes of the bitcasted vector.

  // Need to simply zero out the lower 16 bits of each lane of the bitcasted
  // vector.
  return BitCast(d_to,
                 And(BitCast(du_to, v), Set(du_to, uint32_t{0xFFFF0000u})));
#endif
}

// BF16->F32 PromoteOddTo

// NOTE: It is possible for FromTypeTag to be hwy::SignedTag or hwy::UnsignedTag
// instead of hwy::FloatTag on targets that use scalable vectors.

// VBF16 is considered to be a bfloat16_t vector if TFromV<VBF16> is the same
// type as TFromV<VFromD<Repartition<bfloat16_t, DF32>>>

// The BF16->F32 PromoteEvenTo overload is only enabled if VBF16 is considered
// to be a bfloat16_t vector.
template <class FromTypeTag, class DF32, class VBF16,
          class VBF16_2 = VFromD<Repartition<bfloat16_t, DF32>>,
          hwy::EnableIf<IsSame<TFromV<VBF16>, TFromV<VBF16_2>>()>* = nullptr>
HWY_INLINE VFromD<DF32> PromoteOddTo(hwy::FloatTag /*to_type_tag*/,
                                     hwy::SizeTag<4> /*to_lane_size_tag*/,
                                     FromTypeTag /*from_type_tag*/, DF32 d_to,
                                     VBF16 v) {
  const RebindToUnsigned<decltype(d_to)> du_to;
#if HWY_IS_LITTLE_ENDIAN
  // On little-endian platforms, the odd lanes of the source vector are already
  // in the upper 16 bits of the lanes of the bitcasted vector.

  // Need to simply zero out the lower 16 bits of each lane of the bitcasted
  // vector.
  return BitCast(d_to,
                 And(BitCast(du_to, v), Set(du_to, uint32_t{0xFFFF0000u})));
#else
  // On big-endian platforms, need to shift left each lane of the bitcasted
  // vector by 16 bits.
  return BitCast(d_to, ShiftLeft<16>(BitCast(du_to, v)));
#endif
}

// Default PromoteEvenTo/PromoteOddTo implementations
template <class ToTypeTag, size_t kToLaneSize, class FromTypeTag, class D,
          class V, HWY_IF_LANES_D(D, 1)>
HWY_INLINE VFromD<D> PromoteEvenTo(
    ToTypeTag /*to_type_tag*/, hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    FromTypeTag /*from_type_tag*/, D d_to, V v) {
  return PromoteLowerTo(d_to, v);
}

template <class ToTypeTag, size_t kToLaneSize, class FromTypeTag, class D,
          class V, HWY_IF_LANES_GT_D(D, 1)>
HWY_INLINE VFromD<D> PromoteEvenTo(
    ToTypeTag /*to_type_tag*/, hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    FromTypeTag /*from_type_tag*/, D d_to, V v) {
  const DFromV<decltype(v)> d;
  return PromoteLowerTo(d_to, ConcatEven(d, v, v));
}

template <class ToTypeTag, size_t kToLaneSize, class FromTypeTag, class D,
          class V>
HWY_INLINE VFromD<D> PromoteOddTo(
    ToTypeTag /*to_type_tag*/, hwy::SizeTag<kToLaneSize> /*to_lane_size_tag*/,
    FromTypeTag /*from_type_tag*/, D d_to, V v) {
  const DFromV<decltype(v)> d;
  return PromoteLowerTo(d_to, ConcatOdd(d, v, v));
}

}  // namespace detail

template <class D, class V, HWY_IF_T_SIZE_D(D, 2 * sizeof(TFromV<V>)),
          class V2 = VFromD<Repartition<TFromV<V>, D>>,
          HWY_IF_LANES_D(DFromV<V>, HWY_MAX_LANES_V(V2))>
HWY_API VFromD<D> PromoteEvenTo(D d, V v) {
  return detail::PromoteEvenTo(hwy::TypeTag<TFromD<D>>(),
                               hwy::SizeTag<sizeof(TFromD<D>)>(),
                               hwy::TypeTag<TFromV<V>>(), d, v);
}

template <class D, class V, HWY_IF_T_SIZE_D(D, 2 * sizeof(TFromV<V>)),
          class V2 = VFromD<Repartition<TFromV<V>, D>>,
          HWY_IF_LANES_D(DFromV<V>, HWY_MAX_LANES_V(V2))>
HWY_API VFromD<D> PromoteOddTo(D d, V v) {
  return detail::PromoteOddTo(hwy::TypeTag<TFromD<D>>(),
                              hwy::SizeTag<sizeof(TFromD<D>)>(),
                              hwy::TypeTag<TFromV<V>>(), d, v);
}
#endif  // HWY_TARGET != HWY_SCALAR

#ifdef HWY_INSIDE_END_NAMESPACE
#undef HWY_INSIDE_END_NAMESPACE
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();
#endif
