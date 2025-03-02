// Copyright 2020 Google LLC
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

// Per-target definitions shared by ops/*.h and user code.

// IWYU pragma: begin_exports
// Export does not seem to be recursive, so re-export these (also in base.h)
#include <stddef.h>

#include "hwy/base.h"
// "IWYU pragma: keep" does not work for this include, so hide it from the IDE.
#if !HWY_IDE
#include <stdint.h>
#endif

#include "hwy/detect_compiler_arch.h"
#include "hwy/detect_targets.h"

// Separate header because foreach_target.h re-enables its include guard.
#include "hwy/ops/set_macros-inl.h"

// IWYU pragma: end_exports

#if HWY_IS_MSAN
#include <sanitizer/msan_interface.h>
#endif

// We are covered by the highway.h include guard, but generic_ops-inl.h
// includes this again #if HWY_IDE.
// clang-format off
#if defined(HIGHWAY_HWY_OPS_SHARED_TOGGLE) == defined(HWY_TARGET_TOGGLE)  // NOLINT
// clang-format on
#ifdef HIGHWAY_HWY_OPS_SHARED_TOGGLE
#undef HIGHWAY_HWY_OPS_SHARED_TOGGLE
#else
#define HIGHWAY_HWY_OPS_SHARED_TOGGLE
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// NOTE: GCC generates incorrect code for vector arguments to non-inlined
// functions in two situations:
// - on Windows and GCC 10.3, passing by value crashes due to unaligned loads:
//   https://gcc.gnu.org/bugzilla/show_bug.cgi?id=54412.
// - on aarch64 and GCC 9.3.0 or 11.2.1, passing by value causes many (but not
//   all) tests to fail.
//
// We therefore pass by const& only on GCC and (Windows or aarch64). This alias
// must be used for all vector/mask parameters of functions marked HWY_NOINLINE,
// and possibly also other functions that are not inlined.
//
// Even better is to avoid passing vector arguments to non-inlined functions,
// because the SVE and RISC-V ABIs are still works in progress and may lead to
// incorrect codegen.
#if HWY_COMPILER_GCC_ACTUAL && (HWY_OS_WIN || HWY_ARCH_ARM_A64)
template <class V>
using VecArg = const V&;
#else
template <class V>
using VecArg = V;
#endif

namespace detail {

template <typename T>
struct NativeLaneTypeT {
  using type = T;
};
template <>
struct NativeLaneTypeT<hwy::float16_t> {
#if HWY_HAVE_SCALAR_F16_TYPE
  using type = hwy::float16_t::Native;
#else
  using type = uint16_t;
#endif
};
template <>
struct NativeLaneTypeT<hwy::bfloat16_t> {
#if HWY_HAVE_SCALAR_BF16_TYPE
  using type = hwy::bfloat16_t::Native;
#else
  using type = uint16_t;
#endif
};

// The type expected by intrinsics for the given Highway lane type T. This
// usually matches T, but differs for our wrapper types [b]float16_t. Use this
// only when defining intrinsic wrappers, and NOT for casting, which is UB.
template <typename T>
using NativeLaneType = typename NativeLaneTypeT<T>::type;

// Returns the same pointer after changing type to NativeLaneType. Use this only
// for wrapper functions that call intrinsics (e.g. load/store) where some of
// the overloads expect _Float16* or __bf16* arguments. For non-special floats,
// this returns the same pointer and type.
//
// This makes use of the fact that a wrapper struct is pointer-interconvertible
// with its first member (a union), thus also with the union members. Do NOT
// call both this and U16LanePointer on the same object - they access different
// union members, and this is not guaranteed to be safe.
template <typename T, HWY_IF_NOT_SPECIAL_FLOAT(T)>
HWY_INLINE T* NativeLanePointer(T* p) {
  return p;
}
template <typename T, typename NT = NativeLaneType<RemoveConst<T>>,
          HWY_IF_F16(T)>
HWY_INLINE constexpr If<IsConst<T>(), const NT*, NT*> NativeLanePointer(T* p) {
#if HWY_HAVE_SCALAR_F16_TYPE
  return &p->native;
#else
  return &p->bits;
#endif
}
template <typename T, typename NT = NativeLaneType<RemoveConst<T>>,
          HWY_IF_BF16(T)>
HWY_INLINE constexpr If<IsConst<T>(), const NT*, NT*> NativeLanePointer(T* p) {
#if HWY_HAVE_SCALAR_BF16_TYPE
  return &p->native;
#else
  return &p->bits;
#endif
}

// Returns a pointer to the u16 member of our [b]float16_t wrapper structs.
// Use this in Highway targets that lack __bf16 intrinsics; for storing to
// memory, we BitCast vectors to u16 and write to the pointer returned here.
// Do NOT call both this and U16LanePointer on the same object - they access
// different union members, and this is not guaranteed to be safe.
template <typename T, HWY_IF_SPECIAL_FLOAT(T)>
HWY_INLINE If<IsConst<T>(), const uint16_t*, uint16_t*> U16LanePointer(T* p) {
  return &p->bits;
}

// Returns N * 2^pow2. N is the number of lanes in a full vector and pow2 the
// desired fraction or multiple of it, see Simd<>. `pow2` is most often in
// [-3, 3] but can also be lower for user-specified fractions.
constexpr size_t ScaleByPower(size_t N, int pow2) {
  return pow2 >= 0 ? (N << pow2) : (N >> (-pow2));
}

template <typename T>
HWY_INLINE void MaybePoison(T* HWY_RESTRICT unaligned, size_t count) {
#if HWY_IS_MSAN
  __msan_poison(unaligned, count * sizeof(T));
#else
  (void)unaligned;
  (void)count;
#endif
}

template <typename T>
HWY_INLINE void MaybeUnpoison(T* HWY_RESTRICT unaligned, size_t count) {
  // Workaround for MSAN not marking compressstore as initialized (b/233326619)
#if HWY_IS_MSAN
  __msan_unpoison(unaligned, count * sizeof(T));
#else
  (void)unaligned;
  (void)count;
#endif
}

}  // namespace detail

// Highway operations are implemented as overloaded functions selected using a
// zero-sized tag type D := Simd<T, N, kPow2>. T denotes the lane type.
//
// N defines how many lanes are in a 'full' vector, typically equal to
// HWY_LANES(T) (which is the actual count on targets with vectors of known
// size, and an upper bound in case of scalable vectors), otherwise a
// user-specified limit at most that large.
//
// 2^kPow2 is a _subsequently_ applied scaling factor that indicates the
// desired fraction of a 'full' vector: 0 means full, -1 means half; 1,2,3
// means two/four/eight full vectors ganged together. The largest supported
// kPow2 is `HWY_MAX_POW2` and the aliases below take care of clamping
// user-specified values to that. Note that `Simd<T, 1, 0>` and `Simd<T, 2, -1>`
// have the same `MaxLanes` and `Lanes`.
//
// We can theoretically keep halving Lanes(), but recursive instantiations of
// kPow2 - 1 will eventually fail e.g. because -64 is not a valid shift count.
// Users must terminate such compile-time recursions at or above HWY_MIN_POW2.
//
// WARNING: do not use N directly because it may be a special representation of
// a fractional MaxLanes. This arises when we Rebind Simd<uint8_t, 1, 0> to
// Simd<uint32_t, ??, 2>. RVV requires that the last argument (kPow2) be two,
// but we want MaxLanes to be the same in both cases. Hence ?? is a
// fixed-point encoding of 1/4.
//
// Instead of referring to Simd<> directly, users create D via aliases:
// - ScalableTag<T> for a full vector;
// - ScalableTag<T, kPow2>() for a fraction/group, where `kPow2` is
//   interpreted as `HWY_MIN(kPow2, HWY_MAX_POW2)`;
// - CappedTag<T, kLimit> for a vector with up to kLimit lanes; or
// - FixedTag<T, kNumLanes> for a vector with exactly kNumLanes lanes.
//
// Instead of N, use Lanes(D()) for the actual number of lanes at runtime and
// D().MaxLanes() for a constexpr upper bound. Both are powers of two.
template <typename Lane, size_t N, int kPow2>
struct Simd {
  constexpr Simd() = default;
  using T = Lane;

 private:
  static_assert(sizeof(Lane) <= 8, "Lanes are up to 64-bit");
  static_assert(IsSame<Lane, RemoveCvRef<Lane>>(),
                "Lane must not be a reference type, const-qualified type, or "
                "volatile-qualified type");
  static_assert(IsIntegerLaneType<Lane>() || IsFloat<Lane>() ||
                    IsSpecialFloat<Lane>(),
                "IsIntegerLaneType<T>(), IsFloat<T>(), or IsSpecialFloat<T>() "
                "must be true");
  // 20 bits are sufficient for any HWY_MAX_BYTES. This is the 'normal' value of
  // N when kFrac == 0, otherwise it is one (see FracN).
  static constexpr size_t kWhole = N & 0xFFFFF;
  // Fractional part is in the bits above kWhole.
  static constexpr int kFrac = static_cast<int>(N >> 20);
  // Can be 8x larger because kPow2 may be as low as -3 (Rebind of a larger
  // type to u8 results in fractions).
  static_assert(kWhole <= 8 * HWY_MAX_N && kFrac <= 3, "Out of range");
  static_assert(kFrac == 0 || kWhole == 1, "If frac, whole must be 1");
  static_assert((kWhole & (kWhole - 1)) == 0 && kWhole != 0, "Not 2^x");
  // Important to check this here because kPow2 <= -64 causes confusing
  // compile errors (invalid shift count).
  static_assert(kPow2 >= HWY_MIN_POW2, "Forgot kPow2 recursion terminator?");
  // However, do NOT verify kPow2 <= HWY_MAX_POW2 - users should be able to
  // Rebind<uint64_t, ScalableTag<uint8_t, 3>> in order to discover that its
  // kPow2 is out of bounds.

 public:
  // Upper bound on the number of lanes (tight if !HWY_HAVE_SCALABLE). In the
  // common case, N == kWhole, but if kFrac is nonzero, we deduct it from kPow2.
  // E.g. Rebind<uint32_t, Simd<uint8_t, 1, 0>> is Simd<uint32_t, 0x200001, 2>.
  // The resulting number of lanes is still 1 because this N represents 1/4
  // (the ratio of the sizes). Note that RVV requires kPow2 to be the ratio of
  // the sizes so that the correct LMUL overloads are chosen, even if N is
  // small enough that it would fit in an LMUL=1 vector.
  //
  // Cannot be an enum because GCC warns when using enums and non-enums in the
  // same expression. Cannot be a static constexpr function (MSVC limitation).
  // Rounded up to one so this is a valid array length.
  //
  // Do not use this directly - only 'public' so it is visible from the accessor
  // macro required by MSVC.
  static constexpr size_t kPrivateLanes =
      HWY_MAX(size_t{1}, detail::ScaleByPower(kWhole, kPow2 - kFrac));
  // Do not use this directly - only 'public' so it is visible from the accessor
  // macro required by MSVC.
  static constexpr int kPrivatePow2 = kPow2;

  constexpr size_t MaxLanes() const { return kPrivateLanes; }
  constexpr size_t MaxBytes() const { return kPrivateLanes * sizeof(Lane); }
  constexpr size_t MaxBlocks() const { return (MaxBytes() + 15) / 16; }
  // For SFINAE (HWY_IF_POW2_GT_D).
  constexpr int Pow2() const { return kPow2; }

  // ------------------------------ Changing lane type or count
  // Do not use any of these directly. Anything used from member typedefs cannot
  // be made private, but functions only used within other functions can.

  // Returns number of NewT lanes that fit within MaxBytes().
  template <typename NewT>
  static constexpr size_t RepartitionLanes() {
    // Round up to correctly handle larger NewT.
    return (kPrivateLanes * sizeof(T) + sizeof(NewT) - 1) / sizeof(NewT);
  }

  // Returns the new kPow2 required for lanes of type NewT.
  template <typename NewT>
  static constexpr int RebindPow2() {
    return kPow2 +
           ((sizeof(NewT) >= sizeof(T))
                ? static_cast<int>(CeilLog2(sizeof(NewT) / sizeof(T)))
                : -static_cast<int>(CeilLog2(sizeof(T) / sizeof(NewT))));
  }

 private:
  // Returns 0 or whole NewN such that kNewMaxLanes = NewN * 2^kNewPow2.
  template <int kNewPow2, size_t kNewMaxLanes>
  static constexpr size_t WholeN() {
    return detail::ScaleByPower(kNewMaxLanes, -kNewPow2);
  }

  // Returns fractional NewN such that kNewMaxLanes = NewN * 2^kNewPow2.
  template <int kNewPow2, size_t kNewMaxLanes>
  static constexpr size_t FracN() {
    // Only reached if kNewPow2 > CeilLog2(kNewMaxLanes) >= 0 (else WholeN
    // would not have been zero), but clamp to zero to avoid warnings. kFrac is
    // the difference, stored in the upper bits of N, and we also set kWhole =
    // 1 so that the new kPrivateLanes = kNewMaxLanes.
    static_assert(HWY_MAX_N <= (size_t{1} << 20), "Change bit shift");
    return static_cast<size_t>(
        1 + (HWY_MAX(0, kNewPow2 - static_cast<int>(CeilLog2(kNewMaxLanes)))
             << 20));
  }

 public:
  // Returns (whole or fractional) NewN, see above.
  template <int kNewPow2, size_t kNewMaxLanes>
  static constexpr size_t NewN() {
    // We require a fraction if inverting kNewPow2 results in 0.
    return WholeN<kNewPow2, kNewMaxLanes>() == 0
               ? FracN<kNewPow2, kNewMaxLanes>()
               : WholeN<kNewPow2, kNewMaxLanes>();
  }

  // PromoteTo/DemoteTo() with another lane type, but same number of lanes.
  template <typename NewT>
  using Rebind =
      Simd<NewT, NewN<RebindPow2<NewT>(), kPrivateLanes>(), RebindPow2<NewT>()>;

  // Change lane type while keeping the same vector size, e.g. for MulEven.
  template <typename NewT>
  using Repartition =
      Simd<NewT, NewN<kPow2, RepartitionLanes<NewT>()>(), kPow2>;

  // Half the lanes while keeping the same lane type, e.g. for LowerHalf.
  using Half = Simd<T, N, kPow2 - 1>;

  // Twice the lanes while keeping the same lane type, e.g. for Combine.
  using Twice = Simd<T, N, kPow2 + 1>;
};

namespace detail {

template <typename T, size_t N, int kPow2>
constexpr bool IsFull(Simd<T, N, kPow2> /* d */) {
  return N == HWY_LANES(T) && kPow2 == 0;
}

// Struct wrappers enable validation of arguments via static_assert.
template <typename T, size_t N, int kPow2>
struct ClampNAndPow2 {
  using type = Simd<T, HWY_MIN(N, HWY_MAX_N), HWY_MIN(kPow2, HWY_MAX_POW2)>;
};

template <typename T, int kPow2>
struct ScalableTagChecker {
  using type = typename ClampNAndPow2<T, HWY_LANES(T), kPow2>::type;
};

template <typename T, size_t kLimit, int kPow2>
struct CappedTagChecker {
  static_assert(kLimit != 0, "Does not make sense to have zero lanes");
  // Safely handle non-power-of-two inputs by rounding down, which is allowed by
  // CappedTag. Otherwise, Simd<T, 3, 0> would static_assert.
  static constexpr size_t kLimitPow2 = size_t{1} << hwy::FloorLog2(kLimit);
  static constexpr size_t N = HWY_MIN(kLimitPow2, HWY_LANES(T));
  using type = typename ClampNAndPow2<T, N, kPow2>::type;
};

template <typename T, size_t kNumLanes>
struct FixedTagChecker {
  static_assert(kNumLanes != 0, "Does not make sense to have zero lanes");
  static_assert(kNumLanes <= HWY_LANES(T), "Too many lanes");
  using type = Simd<T, kNumLanes, 0>;
};

}  // namespace detail

// ------------------------------ Aliases for Simd<>

// Tag describing a full vector (kPow2 == 0: the most common usage, e.g. 1D
// loops where the application does not care about the vector size) or a
// fraction/multiple of one. Fractions (kPow2 < 0) are useful for arguments or
// return values of type promotion and demotion. User-specified kPow2 is
// interpreted as `HWY_MIN(kPow2, HWY_MAX_POW2)`.
template <typename T, int kPow2 = 0>
using ScalableTag = typename detail::ScalableTagChecker<T, kPow2>::type;

// Tag describing a vector with *up to* kLimit active lanes, even on targets
// with scalable vectors and HWY_SCALAR. The runtime lane count `Lanes(tag)` may
// be less than kLimit, and is 1 on HWY_SCALAR. This alias is typically used for
// 1D loops with a relatively low application-defined upper bound, e.g. for 8x8
// DCTs. However, it is better if data structures are designed to be
// vector-length-agnostic (e.g. a hybrid SoA where there are chunks of `M >=
// MaxLanes(d)` DC components followed by M AC1, .., and M AC63; this would
// enable vector-length-agnostic loops using ScalableTag). User-specified kPow2
// is interpreted as `HWY_MIN(kPow2, HWY_MAX_POW2)`.
template <typename T, size_t kLimit, int kPow2 = 0>
using CappedTag = typename detail::CappedTagChecker<T, kLimit, kPow2>::type;

#if !HWY_HAVE_SCALABLE
// If the vector size is known, and the app knows it does not want more than
// kLimit lanes, then capping can be beneficial. For example, AVX-512 has lower
// IPC and potentially higher costs for unaligned load/store vs. 256-bit AVX2.
template <typename T, size_t kLimit, int kPow2 = 0>
using CappedTagIfFixed = CappedTag<T, kLimit, kPow2>;
#else  // HWY_HAVE_SCALABLE
// .. whereas on RVV/SVE, the cost of clamping Lanes() may exceed the benefit.
template <typename T, size_t kLimit, int kPow2 = 0>
using CappedTagIfFixed = ScalableTag<T, kPow2>;
#endif

// Alias for a tag describing a vector with *exactly* kNumLanes active lanes,
// even on targets with scalable vectors. Requires `kNumLanes` to be a power of
// two not exceeding `HWY_LANES(T)`.
//
// NOTE: if the application does not need to support HWY_SCALAR (+), use this
// instead of CappedTag to emphasize that there will be exactly kNumLanes lanes.
// This is useful for data structures that rely on exactly 128-bit SIMD, but
// these are discouraged because they cannot benefit from wider vectors.
// Instead, applications would ideally define a larger problem size and loop
// over it with the (unknown size) vectors from ScalableTag.
//
// + e.g. if the baseline is known to support SIMD, or the application requires
//   ops such as TableLookupBytes not supported by HWY_SCALAR.
template <typename T, size_t kNumLanes>
using FixedTag = typename detail::FixedTagChecker<T, kNumLanes>::type;

// Convenience form for fixed sizes.
template <typename T>
using Full16 = Simd<T, 2 / sizeof(T), 0>;

template <typename T>
using Full32 = Simd<T, 4 / sizeof(T), 0>;

template <typename T>
using Full64 = Simd<T, 8 / sizeof(T), 0>;

template <typename T>
using Full128 = Simd<T, 16 / sizeof(T), 0>;

// ------------------------------ Accessors for Simd<>

// Lane type.
template <class D>
using TFromD = typename D::T;

// Upper bound on the number of lanes, typically used for SFINAE conditions and
// to allocate storage for targets with known vector sizes. Note: this may be a
// loose bound, instead use Lanes() as the actual size for AllocateAligned.
// MSVC workaround: use static constant directly instead of a function.
#define HWY_MAX_LANES_D(D) D::kPrivateLanes

// Same as D().Pow2(), but this is too complex for SFINAE with MSVC, so we use a
// static constant directly.
#define HWY_POW2_D(D) D::kPrivatePow2

// Non-macro form of HWY_MAX_LANES_D in case that is preferable. WARNING: the
// macro form may be required for MSVC, which has limitations on deducing
// arguments.
template <class D>
HWY_INLINE HWY_MAYBE_UNUSED constexpr size_t MaxLanes(D) {
  return HWY_MAX_LANES_D(D);
}

#if !HWY_HAVE_SCALABLE

// If non-scalable, this is constexpr; otherwise the target's header defines a
// non-constexpr version of this function. This is the actual vector length,
// used when advancing loop counters.
template <class D>
HWY_INLINE HWY_MAYBE_UNUSED constexpr size_t Lanes(D) {
  return HWY_MAX_LANES_D(D);
}

#endif  // !HWY_HAVE_SCALABLE

// Tag for the same number of lanes as D, but with the LaneType T.
template <class T, class D>
using Rebind = typename D::template Rebind<T>;

template <class D>
using RebindToSigned = Rebind<MakeSigned<TFromD<D>>, D>;
template <class D>
using RebindToUnsigned = Rebind<MakeUnsigned<TFromD<D>>, D>;
template <class D>
using RebindToFloat = Rebind<MakeFloat<TFromD<D>>, D>;

// Tag for the same total size as D, but with the LaneType T.
template <class T, class D>
using Repartition = typename D::template Repartition<T>;

template <class D>
using RepartitionToWide = Repartition<MakeWide<TFromD<D>>, D>;
template <class D>
using RepartitionToNarrow = Repartition<MakeNarrow<TFromD<D>>, D>;

// Shorthand for applying RepartitionToWide twice (for 8/16-bit types).
template <class D>
using RepartitionToWideX2 = RepartitionToWide<RepartitionToWide<D>>;
// Shorthand for applying RepartitionToWide three times (for 8-bit types).
template <class D>
using RepartitionToWideX3 = RepartitionToWide<RepartitionToWideX2<D>>;

// Tag for the same lane type as D, but half the lanes.
template <class D>
using Half = typename D::Half;

// Tag for the same lane type as D, but twice the lanes.
template <class D>
using Twice = typename D::Twice;

// Tag for a 16-byte block with the same lane type as D
#if HWY_HAVE_SCALABLE
namespace detail {

template <class D>
class BlockDFromD_t {};

template <typename T, size_t N, int kPow2>
class BlockDFromD_t<Simd<T, N, kPow2>> {
  using D = Simd<T, N, kPow2>;
  static constexpr int kNewPow2 = HWY_MIN(kPow2, 0);
  static constexpr size_t kMaxLpb = HWY_MIN(16 / sizeof(T), HWY_MAX_LANES_D(D));
  static constexpr size_t kNewN = D::template NewN<kNewPow2, kMaxLpb>();

 public:
  using type = Simd<T, kNewN, kNewPow2>;
};

}  // namespace detail

template <class D>
using BlockDFromD = typename detail::BlockDFromD_t<RemoveConst<D>>::type;
#else
template <class D>
using BlockDFromD =
    Simd<TFromD<D>, HWY_MIN(16 / sizeof(TFromD<D>), HWY_MAX_LANES_D(D)), 0>;
#endif

// Returns whether `ptr` is a multiple of `Lanes(d)` elements.
template <class D, typename T>
HWY_API bool IsAligned(D d, T* ptr) {
  const size_t N = Lanes(d);
  return reinterpret_cast<uintptr_t>(ptr) % (N * sizeof(T)) == 0;
}

// ------------------------------ Choosing overloads (SFINAE)

// Same as base.h macros but with a Simd<T, N, kPow2> argument instead of T.
#define HWY_IF_UNSIGNED_D(D) HWY_IF_UNSIGNED(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_UNSIGNED_D(D) \
  HWY_IF_NOT_UNSIGNED(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_SIGNED_D(D) HWY_IF_SIGNED(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_FLOAT_D(D) HWY_IF_FLOAT(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_FLOAT_D(D) HWY_IF_NOT_FLOAT(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_FLOAT3264_D(D) HWY_IF_FLOAT3264(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_FLOAT3264_D(D) \
  HWY_IF_NOT_FLOAT3264(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_SPECIAL_FLOAT_D(D) \
  HWY_IF_SPECIAL_FLOAT(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_SPECIAL_FLOAT_D(D) \
  HWY_IF_NOT_SPECIAL_FLOAT(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_FLOAT_OR_SPECIAL_D(D) \
  HWY_IF_FLOAT_OR_SPECIAL(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D) \
  HWY_IF_NOT_FLOAT_NOR_SPECIAL(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_IF_T_SIZE_D(D, bytes) \
  HWY_IF_T_SIZE(hwy::HWY_NAMESPACE::TFromD<D>, bytes)
#define HWY_IF_NOT_T_SIZE_D(D, bytes) \
  HWY_IF_NOT_T_SIZE(hwy::HWY_NAMESPACE::TFromD<D>, bytes)
#define HWY_IF_T_SIZE_ONE_OF_D(D, bit_array) \
  HWY_IF_T_SIZE_ONE_OF(hwy::HWY_NAMESPACE::TFromD<D>, bit_array)
#define HWY_IF_T_SIZE_LE_D(D, bytes) \
  HWY_IF_T_SIZE_LE(hwy::HWY_NAMESPACE::TFromD<D>, bytes)
#define HWY_IF_T_SIZE_GT_D(D, bytes) \
  HWY_IF_T_SIZE_GT(hwy::HWY_NAMESPACE::TFromD<D>, bytes)

#define HWY_IF_LANES_D(D, lanes) HWY_IF_LANES(HWY_MAX_LANES_D(D), lanes)
#define HWY_IF_LANES_LE_D(D, lanes) HWY_IF_LANES_LE(HWY_MAX_LANES_D(D), lanes)
#define HWY_IF_LANES_GT_D(D, lanes) HWY_IF_LANES_GT(HWY_MAX_LANES_D(D), lanes)
#define HWY_IF_LANES_PER_BLOCK_D(D, lanes)                                  \
  HWY_IF_LANES_PER_BLOCK(hwy::HWY_NAMESPACE::TFromD<D>, HWY_MAX_LANES_D(D), \
                         lanes)

#if HWY_COMPILER_MSVC
#define HWY_IF_POW2_LE_D(D, pow2) \
  hwy::EnableIf<HWY_POW2_D(D) <= pow2>* = nullptr
#define HWY_IF_POW2_GT_D(D, pow2) \
  hwy::EnableIf<(HWY_POW2_D(D) > pow2)>* = nullptr
#else
#define HWY_IF_POW2_LE_D(D, pow2) hwy::EnableIf<D().Pow2() <= pow2>* = nullptr
#define HWY_IF_POW2_GT_D(D, pow2) hwy::EnableIf<(D().Pow2() > pow2)>* = nullptr
#endif  // HWY_COMPILER_MSVC

#define HWY_IF_U8_D(D) HWY_IF_U8(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_U16_D(D) HWY_IF_U16(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_U32_D(D) HWY_IF_U32(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_U64_D(D) HWY_IF_U64(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_IF_I8_D(D) HWY_IF_I8(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_I16_D(D) HWY_IF_I16(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_I32_D(D) HWY_IF_I32(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_I64_D(D) HWY_IF_I64(hwy::HWY_NAMESPACE::TFromD<D>)

// Use instead of HWY_IF_T_SIZE_D to avoid ambiguity with float16_t/float/double
// overloads.
#define HWY_IF_UI8_D(D) HWY_IF_UI8(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_UI16_D(D) HWY_IF_UI16(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_UI32_D(D) HWY_IF_UI32(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_UI64_D(D) HWY_IF_UI64(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_IF_BF16_D(D) HWY_IF_BF16(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_BF16_D(D) HWY_IF_NOT_BF16(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_IF_F16_D(D) HWY_IF_F16(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_NOT_F16_D(D) HWY_IF_NOT_F16(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_IF_F32_D(D) HWY_IF_F32(hwy::HWY_NAMESPACE::TFromD<D>)
#define HWY_IF_F64_D(D) HWY_IF_F64(hwy::HWY_NAMESPACE::TFromD<D>)

#define HWY_V_SIZE_D(D) \
  (HWY_MAX_LANES_D(D) * sizeof(hwy::HWY_NAMESPACE::TFromD<D>))
#define HWY_IF_V_SIZE_D(D, bytes) \
  HWY_IF_V_SIZE(hwy::HWY_NAMESPACE::TFromD<D>, HWY_MAX_LANES_D(D), bytes)
#define HWY_IF_V_SIZE_LE_D(D, bytes) \
  HWY_IF_V_SIZE_LE(hwy::HWY_NAMESPACE::TFromD<D>, HWY_MAX_LANES_D(D), bytes)
#define HWY_IF_V_SIZE_GT_D(D, bytes) \
  HWY_IF_V_SIZE_GT(hwy::HWY_NAMESPACE::TFromD<D>, HWY_MAX_LANES_D(D), bytes)

// Same, but with a vector argument. ops/*-inl.h define their own TFromV.
#define HWY_IF_UNSIGNED_V(V) HWY_IF_UNSIGNED(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_NOT_UNSIGNED_V(V) \
  HWY_IF_NOT_UNSIGNED(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_SIGNED_V(V) HWY_IF_SIGNED(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_FLOAT_V(V) HWY_IF_FLOAT(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_NOT_FLOAT_V(V) HWY_IF_NOT_FLOAT(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_SPECIAL_FLOAT_V(V) \
  HWY_IF_SPECIAL_FLOAT(hwy::HWY_NAMESPACE::TFromV<V>)
#define HWY_IF_NOT_FLOAT_NOR_SPECIAL_V(V) \
  HWY_IF_NOT_FLOAT_NOR_SPECIAL(hwy::HWY_NAMESPACE::TFromV<V>)

#define HWY_IF_T_SIZE_V(V, bytes) \
  HWY_IF_T_SIZE(hwy::HWY_NAMESPACE::TFromV<V>, bytes)
#define HWY_IF_NOT_T_SIZE_V(V, bytes) \
  HWY_IF_NOT_T_SIZE(hwy::HWY_NAMESPACE::TFromV<V>, bytes)
#define HWY_IF_T_SIZE_ONE_OF_V(V, bit_array) \
  HWY_IF_T_SIZE_ONE_OF(hwy::HWY_NAMESPACE::TFromV<V>, bit_array)

#define HWY_MAX_LANES_V(V) HWY_MAX_LANES_D(hwy::HWY_NAMESPACE::DFromV<V>)
#define HWY_IF_V_SIZE_V(V, bytes) \
  HWY_IF_V_SIZE(hwy::HWY_NAMESPACE::TFromV<V>, HWY_MAX_LANES_V(V), bytes)
#define HWY_IF_V_SIZE_LE_V(V, bytes) \
  HWY_IF_V_SIZE_LE(hwy::HWY_NAMESPACE::TFromV<V>, HWY_MAX_LANES_V(V), bytes)
#define HWY_IF_V_SIZE_GT_V(V, bytes) \
  HWY_IF_V_SIZE_GT(hwy::HWY_NAMESPACE::TFromV<V>, HWY_MAX_LANES_V(V), bytes)

// Use in implementations of ReduceSum etc. to avoid conflicts with the N=1 and
// N=4 8-bit specializations in generic_ops-inl.
#undef HWY_IF_REDUCE_D
#define HWY_IF_REDUCE_D(D)                  \
  hwy::EnableIf<HWY_MAX_LANES_D(D) != 1 &&  \
                (HWY_MAX_LANES_D(D) != 4 || \
                 sizeof(hwy::HWY_NAMESPACE::TFromD<D>) != 1)>* = nullptr

#undef HWY_IF_SUM_OF_LANES_D
#define HWY_IF_SUM_OF_LANES_D(D) HWY_IF_LANES_GT_D(D, 1)

#undef HWY_IF_MINMAX_OF_LANES_D
#define HWY_IF_MINMAX_OF_LANES_D(D) HWY_IF_LANES_GT_D(D, 1)

#undef HWY_IF_ADDSUB_V
#define HWY_IF_ADDSUB_V(V) HWY_IF_LANES_GT_D(hwy::HWY_NAMESPACE::DFromV<V>, 1)

#undef HWY_IF_MULADDSUB_V
#define HWY_IF_MULADDSUB_V(V) \
  HWY_IF_LANES_GT_D(hwy::HWY_NAMESPACE::DFromV<V>, 1)

// HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V is used to disable the default
// implementation of unsigned to signed DemoteTo/ReorderDemote2To in
// generic_ops-inl.h for at least some of the unsigned to signed demotions on
// SCALAR/EMU128/SSE2/SSSE3/SSE4/AVX2/SVE/SVE2

#undef HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V
#define HWY_IF_U2I_DEMOTE_FROM_LANE_SIZE_V(V) void* = nullptr

// Old names (deprecated)
#define HWY_IF_LANE_SIZE_D(D, bytes) HWY_IF_T_SIZE_D(D, bytes)
#define HWY_IF_NOT_LANE_SIZE_D(D, bytes) HWY_IF_NOT_T_SIZE_D(D, bytes)

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_OPS_SHARED_TOGGLE
