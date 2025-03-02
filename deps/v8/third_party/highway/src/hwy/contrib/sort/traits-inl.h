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

// Per-target
#if defined(HIGHWAY_HWY_CONTRIB_SORT_TRAITS_TOGGLE) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_SORT_TRAITS_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_TRAITS_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_TRAITS_TOGGLE
#endif

#include <stddef.h>
#include <stdint.h>

#include "hwy/contrib/sort/order.h"       // SortDescending
#include "hwy/contrib/sort/shared-inl.h"  // SortConstants
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace detail {

// Base class of both KeyLane variants
template <typename LaneTypeArg, typename KeyTypeArg>
struct KeyLaneBase {
  static constexpr bool Is128() { return false; }
  constexpr size_t LanesPerKey() const { return 1; }

  // What type bench_sort should allocate for generating inputs.
  using LaneType = LaneTypeArg;
  // What type to pass to VQSort.
  using KeyType = KeyTypeArg;

  const char* KeyString() const {
    return IsSame<KeyTypeArg, float16_t>()     ? "f16"
           : IsSame<KeyTypeArg, float>()       ? "f32"
           : IsSame<KeyTypeArg, double>()      ? "f64"
           : IsSame<KeyTypeArg, int16_t>()     ? "i16"
           : IsSame<KeyTypeArg, int32_t>()     ? "i32"
           : IsSame<KeyTypeArg, int64_t>()     ? "i64"
           : IsSame<KeyTypeArg, uint16_t>()    ? "u32"
           : IsSame<KeyTypeArg, uint32_t>()    ? "u32"
           : IsSame<KeyTypeArg, uint64_t>()    ? "u64"
           : IsSame<KeyTypeArg, hwy::K32V32>() ? "k+v=64"
                                               : "?";
  }
};

// Wrapper functions so we can specialize for floats - infinity trumps
// HighestValue (the normal value with the largest magnitude). Must be outside
// Order* classes to enable SFINAE.

template <class D, HWY_IF_FLOAT_OR_SPECIAL_D(D)>
Vec<D> LargestSortValue(D d) {
  return Inf(d);
}
template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
Vec<D> LargestSortValue(D d) {
  return Set(d, hwy::HighestValue<TFromD<D>>());
}

template <class D, HWY_IF_FLOAT_OR_SPECIAL_D(D)>
Vec<D> SmallestSortValue(D d) {
  return Neg(Inf(d));
}
template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
Vec<D> SmallestSortValue(D d) {
  return Set(d, hwy::LowestValue<TFromD<D>>());
}

// Returns the next distinct larger value unless already +inf.
template <class D, HWY_IF_FLOAT_OR_SPECIAL_D(D)>
Vec<D> LargerSortValue(D d, Vec<D> v) {
  HWY_DASSERT(AllFalse(d, IsNaN(v)));  // we replaced all NaN with LastValue.
  using T = TFromD<decltype(d)>;
  const RebindToUnsigned<D> du;
  using VU = Vec<decltype(du)>;
  using TU = TFromD<decltype(du)>;

  const VU vu = BitCast(du, Abs(v));

  // The direction depends on the original sign. Integer comparison is cheaper
  // than float comparison and treats -0 as 0 (so we return +epsilon).
  const Mask<decltype(du)> was_pos = Le(BitCast(du, v), SignBit(du));
  // If positive, add 1, else -1.
  const VU add = IfThenElse(was_pos, Set(du, 1u), Set(du, LimitsMax<TU>()));
  // Prev/next integer is the prev/next value, even if mantissa under/overflows.
  v = BitCast(d, Add(vu, add));
  // But we may have overflowed into inf or NaN; replace with inf if positive,
  // but the largest (later negated!) value if the input was -inf.
  const Mask<D> was_pos_f = RebindMask(d, was_pos);
  v = IfThenElse(IsFinite(v), v,
                 IfThenElse(was_pos_f, Inf(d), Set(d, HighestValue<T>())));
  // Restore the original sign - not via CopySignToAbs because we used a mask.
  return IfThenElse(was_pos_f, v, Neg(v));
}

// Returns the next distinct smaller value unless already -inf.
template <class D, HWY_IF_FLOAT_OR_SPECIAL_D(D)>
Vec<D> SmallerSortValue(D d, Vec<D> v) {
  HWY_DASSERT(AllFalse(d, IsNaN(v)));  // we replaced all NaN with LastValue.
  using T = TFromD<decltype(d)>;
  const RebindToUnsigned<D> du;
  using VU = Vec<decltype(du)>;
  using TU = TFromD<decltype(du)>;

  const VU vu = BitCast(du, Abs(v));

  // The direction depends on the original sign. Float comparison because we
  // want to treat 0 as -0 so we return -epsilon.
  const Mask<D> was_pos = Gt(v, Zero(d));
  // If positive, add -1, else 1.
  const VU add =
      IfThenElse(RebindMask(du, was_pos), Set(du, LimitsMax<TU>()), Set(du, 1));
  // Prev/next integer is the prev/next value, even if mantissa under/overflows.
  v = BitCast(d, Add(vu, add));
  // But we may have overflowed into inf or NaN; replace with +inf (which will
  // later be negated) if negative, but the largest value if the input was +inf.
  v = IfThenElse(IsFinite(v), v,
                 IfThenElse(was_pos, Set(d, HighestValue<T>()), Inf(d)));
  // Restore the original sign - not via CopySignToAbs because we used a mask.
  return IfThenElse(was_pos, v, Neg(v));
}

template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
Vec<D> LargerSortValue(D d, Vec<D> v) {
  return Add(v, Set(d, TFromD<D>{1}));
}

template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
Vec<D> SmallerSortValue(D d, Vec<D> v) {
  return Sub(v, Set(d, TFromD<D>{1}));
}

// Highway does not provide a lane type for 128-bit keys, so we use uint64_t
// along with an abstraction layer for single-lane vs. lane-pair, which is
// independent of the order.
template <typename LaneType, typename KeyType>
struct KeyLane : public KeyLaneBase<LaneType, KeyType> {
  // For HeapSort
  HWY_INLINE void Swap(LaneType* a, LaneType* b) const {
    const LaneType temp = *a;
    *a = *b;
    *b = temp;
  }

  template <class V, class M>
  HWY_INLINE V CompressKeys(V keys, M mask) const {
    return CompressNot(keys, mask);
  }

  // Broadcasts one key into a vector
  template <class D>
  HWY_INLINE Vec<D> SetKey(D d, const LaneType* key) const {
    return Set(d, *key);
  }

  template <class D>
  HWY_INLINE Mask<D> EqualKeys(D /*tag*/, Vec<D> a, Vec<D> b) const {
    return Eq(a, b);
  }

  template <class D>
  HWY_INLINE Mask<D> NotEqualKeys(D /*tag*/, Vec<D> a, Vec<D> b) const {
    return Ne(a, b);
  }

  // For keys=lanes, any difference counts.
  template <class D>
  HWY_INLINE bool NoKeyDifference(D /*tag*/, Vec<D> diff) const {
    // Must avoid floating-point comparisons (for -0)
    const RebindToUnsigned<D> du;
    return AllTrue(du, Eq(BitCast(du, diff), Zero(du)));
  }

  HWY_INLINE bool Equal1(const LaneType* a, const LaneType* b) const {
    return *a == *b;
  }

  template <class D>
  HWY_INLINE Vec<D> ReverseKeys(D d, Vec<D> v) const {
    return Reverse(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> ReverseKeys2(D d, Vec<D> v) const {
    return Reverse2(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> ReverseKeys4(D d, Vec<D> v) const {
    return Reverse4(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> ReverseKeys8(D d, Vec<D> v) const {
    return Reverse8(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> ReverseKeys16(D d, Vec<D> v) const {
    static_assert(SortConstants::kMaxCols <= 16, "Assumes u32x16 = 512 bit");
    return ReverseKeys(d, v);
  }

  template <class V>
  HWY_INLINE V OddEvenKeys(const V odd, const V even) const {
    return OddEven(odd, even);
  }

  template <class D, HWY_IF_T_SIZE_D(D, 2)>
  HWY_INLINE Vec<D> SwapAdjacentPairs(D d, const Vec<D> v) const {
    const Repartition<uint32_t, D> du32;
    return BitCast(d, Shuffle2301(BitCast(du32, v)));
  }
  template <class D, HWY_IF_T_SIZE_D(D, 4)>
  HWY_INLINE Vec<D> SwapAdjacentPairs(D /* tag */, const Vec<D> v) const {
    return Shuffle1032(v);
  }
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> SwapAdjacentPairs(D /* tag */, const Vec<D> v) const {
    return SwapAdjacentBlocks(v);
  }

  template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> SwapAdjacentQuads(D d, const Vec<D> v) const {
#if HWY_HAVE_FLOAT64  // in case D is float32
    const RepartitionToWide<D> dw;
#else
    const RepartitionToWide<RebindToUnsigned<D>> dw;
#endif
    return BitCast(d, SwapAdjacentPairs(dw, BitCast(dw, v)));
  }
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> SwapAdjacentQuads(D d, const Vec<D> v) const {
    // Assumes max vector size = 512
    return ConcatLowerUpper(d, v, v);
  }

  template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> OddEvenPairs(D d, const Vec<D> odd,
                                 const Vec<D> even) const {
#if HWY_HAVE_FLOAT64  // in case D is float32
    const RepartitionToWide<D> dw;
#else
    const RepartitionToWide<RebindToUnsigned<D>> dw;
#endif
    return BitCast(d, OddEven(BitCast(dw, odd), BitCast(dw, even)));
  }
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> OddEvenPairs(D /* tag */, Vec<D> odd, Vec<D> even) const {
    return OddEvenBlocks(odd, even);
  }

  template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> OddEvenQuads(D d, Vec<D> odd, Vec<D> even) const {
#if HWY_HAVE_FLOAT64  // in case D is float32
    const RepartitionToWide<D> dw;
#else
    const RepartitionToWide<RebindToUnsigned<D>> dw;
#endif
    return BitCast(d, OddEvenPairs(dw, BitCast(dw, odd), BitCast(dw, even)));
  }
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> OddEvenQuads(D d, Vec<D> odd, Vec<D> even) const {
    return ConcatUpperLower(d, odd, even);
  }
};

// Anything order-related depends on the key traits *and* the order (see
// FirstOfLanes). We cannot implement just one Compare function because Lt128
// only compiles if the lane type is u64. Thus we need either overloaded
// functions with a tag type, class specializations, or separate classes.
// We avoid overloaded functions because we want all functions to be callable
// from a SortTraits without per-function wrappers. Specializing would work, but
// we are anyway going to specialize at a higher level.
template <typename T>
struct OrderAscending : public KeyLane<T, T> {
  // False indicates the entire key (i.e. lane) should be compared. KV stands
  // for key-value.
  static constexpr bool IsKV() { return false; }

  using Order = SortAscending;
  using OrderForSortingNetwork = OrderAscending<T>;

  HWY_INLINE bool Compare1(const T* a, const T* b) const { return *a < *b; }

  template <class D>
  HWY_INLINE Mask<D> Compare(D /* tag */, Vec<D> a, Vec<D> b) const {
    return Lt(a, b);
  }

  // Two halves of Sort2, used in ScanMinMax.
  template <class D>
  HWY_INLINE Vec<D> First(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Min(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> Last(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Max(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstOfLanes(D d, Vec<D> v,
                                 T* HWY_RESTRICT /* buf */) const {
    return MinOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> LastOfLanes(D d, Vec<D> v,
                                T* HWY_RESTRICT /* buf */) const {
    return MaxOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstValue(D d) const {
    return SmallestSortValue(d);
  }

  template <class D>
  HWY_INLINE Vec<D> LastValue(D d) const {
    return LargestSortValue(d);
  }

  template <class D>
  HWY_INLINE Vec<D> PrevValue(D d, Vec<D> v) const {
    return SmallerSortValue(d, v);
  }
};

template <typename T>
struct OrderDescending : public KeyLane<T, T> {
  // False indicates the entire key (i.e. lane) should be compared. KV stands
  // for key-value.
  static constexpr bool IsKV() { return false; }

  using Order = SortDescending;
  using OrderForSortingNetwork = OrderDescending<T>;

  HWY_INLINE bool Compare1(const T* a, const T* b) const { return *b < *a; }

  template <class D>
  HWY_INLINE Mask<D> Compare(D /* tag */, Vec<D> a, Vec<D> b) const {
    return Lt(b, a);
  }

  template <class D>
  HWY_INLINE Vec<D> First(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Max(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> Last(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Min(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstOfLanes(D d, Vec<D> v,
                                 T* HWY_RESTRICT /* buf */) const {
    return MaxOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> LastOfLanes(D d, Vec<D> v,
                                T* HWY_RESTRICT /* buf */) const {
    return MinOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstValue(D d) const {
    return LargestSortValue(d);
  }

  template <class D>
  HWY_INLINE Vec<D> LastValue(D d) const {
    return SmallestSortValue(d);
  }

  template <class D>
  HWY_INLINE Vec<D> PrevValue(D d, Vec<D> v) const {
    return LargerSortValue(d, v);
  }
};

struct KeyValue64 : public KeyLane<uint64_t, hwy::K32V32> {
  // True indicates only part of the key (i.e. lane) should be compared. KV
  // stands for key-value.
  static constexpr bool IsKV() { return true; }

  template <class D>
  HWY_INLINE Mask<D> EqualKeys(D /*tag*/, Vec<D> a, Vec<D> b) const {
    return Eq(ShiftRight<32>(a), ShiftRight<32>(b));
  }

  template <class D>
  HWY_INLINE Mask<D> NotEqualKeys(D /*tag*/, Vec<D> a, Vec<D> b) const {
    return Ne(ShiftRight<32>(a), ShiftRight<32>(b));
  }

  HWY_INLINE bool Equal1(const uint64_t* a, const uint64_t* b) const {
    return (*a >> 32) == (*b >> 32);
  }

  // Only count differences in the actual key, not the value.
  template <class D>
  HWY_INLINE bool NoKeyDifference(D /*tag*/, Vec<D> diff) const {
    // Must avoid floating-point comparisons (for -0)
    const RebindToUnsigned<D> du;
    const Vec<decltype(du)> zero = Zero(du);
    const Vec<decltype(du)> keys = ShiftRight<32>(diff);  // clear values
    return AllTrue(du, Eq(BitCast(du, keys), zero));
  }
};

struct OrderAscendingKV64 : public KeyValue64 {
  using Order = SortAscending;
  using OrderForSortingNetwork = OrderAscending<LaneType>;

  HWY_INLINE bool Compare1(const LaneType* a, const LaneType* b) const {
    return (*a >> 32) < (*b >> 32);
  }

  template <class D>
  HWY_INLINE Mask<D> Compare(D /* tag */, Vec<D> a, Vec<D> b) const {
    return Lt(ShiftRight<32>(a), ShiftRight<32>(b));
  }

  // Not required to be stable (preserving the order of equivalent keys), so
  // we can include the value in the comparison.
  template <class D>
  HWY_INLINE Vec<D> First(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Min(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> Last(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Max(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstOfLanes(D d, Vec<D> v,
                                 uint64_t* HWY_RESTRICT /* buf */) const {
    return MinOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> LastOfLanes(D d, Vec<D> v,
                                uint64_t* HWY_RESTRICT /* buf */) const {
    return MaxOfLanes(d, v);
  }

  // Same as for regular lanes.
  template <class D>
  HWY_INLINE Vec<D> FirstValue(D d) const {
    return Set(d, hwy::LowestValue<TFromD<D>>());
  }

  template <class D>
  HWY_INLINE Vec<D> LastValue(D d) const {
    return Set(d, hwy::HighestValue<TFromD<D>>());
  }

  template <class D>
  HWY_INLINE Vec<D> PrevValue(D d, Vec<D> v) const {
    return Sub(v, Set(d, uint64_t{1} << 32));
  }
};

struct OrderDescendingKV64 : public KeyValue64 {
  using Order = SortDescending;
  using OrderForSortingNetwork = OrderDescending<LaneType>;

  HWY_INLINE bool Compare1(const LaneType* a, const LaneType* b) const {
    return (*b >> 32) < (*a >> 32);
  }

  template <class D>
  HWY_INLINE Mask<D> Compare(D /* tag */, Vec<D> a, Vec<D> b) const {
    return Lt(ShiftRight<32>(b), ShiftRight<32>(a));
  }

  // Not required to be stable (preserving the order of equivalent keys), so
  // we can include the value in the comparison.
  template <class D>
  HWY_INLINE Vec<D> First(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Max(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> Last(D /* tag */, const Vec<D> a, const Vec<D> b) const {
    return Min(a, b);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstOfLanes(D d, Vec<D> v,
                                 uint64_t* HWY_RESTRICT /* buf */) const {
    return MaxOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> LastOfLanes(D d, Vec<D> v,
                                uint64_t* HWY_RESTRICT /* buf */) const {
    return MinOfLanes(d, v);
  }

  template <class D>
  HWY_INLINE Vec<D> FirstValue(D d) const {
    return Set(d, hwy::HighestValue<TFromD<D>>());
  }

  template <class D>
  HWY_INLINE Vec<D> LastValue(D d) const {
    return Set(d, hwy::LowestValue<TFromD<D>>());
  }

  template <class D>
  HWY_INLINE Vec<D> PrevValue(D d, Vec<D> v) const {
    return Add(v, Set(d, uint64_t{1} << 32));
  }
};

// Shared code that depends on Order.
template <class Base>
struct TraitsLane : public Base {
  using TraitsForSortingNetwork =
      TraitsLane<typename Base::OrderForSortingNetwork>;

  // For each lane i: replaces a[i] with the first and b[i] with the second
  // according to Base.
  // Corresponds to a conditional swap, which is one "node" of a sorting
  // network. Min/Max are cheaper than compare + blend at least for integers.
  template <class D>
  HWY_INLINE void Sort2(D d, Vec<D>& a, Vec<D>& b) const {
    const Base* base = static_cast<const Base*>(this);

    const Vec<D> a_copy = a;
    // Prior to AVX3, there is no native 64-bit Min/Max, so they compile to 4
    // instructions. We can reduce it to a compare + 2 IfThenElse.
#if HWY_AVX3 < HWY_TARGET && HWY_TARGET <= HWY_SSSE3
    if (sizeof(TFromD<D>) == 8) {
      const Mask<D> cmp = base->Compare(d, a, b);
      a = IfThenElse(cmp, a, b);
      b = IfThenElse(cmp, b, a_copy);
      return;
    }
#endif
    a = base->First(d, a, b);
    b = base->Last(d, a_copy, b);
  }

  // Conditionally swaps even-numbered lanes with their odd-numbered neighbor.
  template <class D, HWY_IF_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> SortPairsDistance1(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->ReverseKeys2(d, v);
    // Further to the above optimization, Sort2+OddEvenKeys compile to four
    // instructions; we can save one by combining two blends.
#if HWY_AVX3 < HWY_TARGET && HWY_TARGET <= HWY_SSSE3
    const Vec<D> cmp = VecFromMask(d, base->Compare(d, v, swapped));
    return IfVecThenElse(DupOdd(cmp), swapped, v);
#else
    Sort2(d, v, swapped);
    return base->OddEvenKeys(swapped, v);
#endif
  }

  // (See above - we use Sort2 for non-64-bit types.)
  template <class D, HWY_IF_NOT_T_SIZE_D(D, 8)>
  HWY_INLINE Vec<D> SortPairsDistance1(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->ReverseKeys2(d, v);
    Sort2(d, v, swapped);
    return base->OddEvenKeys(swapped, v);
  }

  // Swaps with the vector formed by reversing contiguous groups of 4 keys.
  template <class D>
  HWY_INLINE Vec<D> SortPairsReverse4(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->ReverseKeys4(d, v);
    Sort2(d, v, swapped);
    return base->OddEvenPairs(d, swapped, v);
  }

  // Conditionally swaps lane 0 with 4, 1 with 5 etc.
  template <class D>
  HWY_INLINE Vec<D> SortPairsDistance4(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->SwapAdjacentQuads(d, v);
    // Only used in Merge16, so this will not be used on AVX2 (which only has 4
    // u64 lanes), so skip the above optimization for 64-bit AVX2.
    Sort2(d, v, swapped);
    return base->OddEvenQuads(d, swapped, v);
  }
};

}  // namespace detail
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_SORT_TRAITS_TOGGLE
