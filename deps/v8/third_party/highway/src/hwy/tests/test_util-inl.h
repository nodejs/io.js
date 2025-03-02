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

// Target-specific helper functions for use by *_test.cc.

#include <stdio.h>
#include <string.h>  // memset

// IWYU pragma: begin_exports
#include "hwy/aligned_allocator.h"
#include "hwy/base.h"
#include "hwy/detect_targets.h"
#include "hwy/per_target.h"
#include "hwy/targets.h"
#include "hwy/tests/hwy_gtest.h"
#include "hwy/tests/test_util.h"
// IWYU pragma: end_exports

// After test_util (also includes highway.h)
#include "hwy/print-inl.h"

// Per-target include guard
// clang-format off
#if defined(HIGHWAY_HWY_TESTS_TEST_UTIL_INL_H_) == defined(HWY_TARGET_TOGGLE)  // NOLINT
// clang-format on
#ifdef HIGHWAY_HWY_TESTS_TEST_UTIL_INL_H_
#undef HIGHWAY_HWY_TESTS_TEST_UTIL_INL_H_
#else
#define HIGHWAY_HWY_TESTS_TEST_UTIL_INL_H_
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Like Iota, but avoids wrapping around to negative integers.
template <class D, HWY_IF_FLOAT_D(D)>
HWY_INLINE Vec<D> PositiveIota(D d) {
  return Iota(d, 1);
}
template <class D, HWY_IF_NOT_FLOAT_NOR_SPECIAL_D(D)>
HWY_INLINE Vec<D> PositiveIota(D d) {
  const auto vi = Iota(d, 1);
  return Max(And(vi, Set(d, LimitsMax<TFromD<D>>())),
             Set(d, static_cast<TFromD<D>>(1)));
}

// Same as Iota, but supports bf16. This is possibly too expensive for general
// use, but fine for tests.
template <class D, typename First, HWY_IF_NOT_SPECIAL_FLOAT_D(D)>
VFromD<D> IotaForSpecial(D d, First first) {
  return Iota(d, first);
}
#if HWY_HAVE_FLOAT16
template <class D, typename First, HWY_IF_F16_D(D), HWY_IF_LANES_GT_D(D, 1)>
VFromD<D> IotaForSpecial(D d, First first) {
  return Iota(d, first);
}
#else   // !HWY_HAVE_FLOAT16
template <class D, typename First, HWY_IF_F16_D(D), HWY_IF_LANES_GT_D(D, 1),
          HWY_IF_POW2_GT_D(D, -1)>
VFromD<D> IotaForSpecial(D d, First first) {
  const Repartition<float, D> df;
  const size_t NW = Lanes(d) / 2;
  const Half<D> dh;
  const float first2 = static_cast<float>(first) + static_cast<float>(NW);
  return Combine(d, DemoteTo(dh, Iota(df, first2)),
                 DemoteTo(dh, Iota(df, first)));
  // TODO(janwas): enable when supported for f16
  // return OrderedDemote2To(d, Iota(df, first), Iota(df, first + NW));
}
// For partial vectors, a single f32 vector is enough, and the prior overload
// might not be able to Repartition.
template <class D, typename First, HWY_IF_F16_D(D), HWY_IF_LANES_GT_D(D, 1),
          HWY_IF_POW2_LE_D(D, -1)>
VFromD<D> IotaForSpecial(D d, First first) {
  const Rebind<float, D> df;
  return DemoteTo(d, Iota(df, first));
}
#endif  // HWY_HAVE_FLOAT16
template <class D, typename First, HWY_IF_BF16_D(D), HWY_IF_LANES_GT_D(D, 1),
          HWY_IF_POW2_GT_D(D, -1)>
VFromD<D> IotaForSpecial(D d, First first) {
  const Repartition<float, D> df;
  const float first1 = ConvertScalarTo<float>(first);
  const float first2 = first1 + static_cast<float>(Lanes(d) / 2);
  return OrderedDemote2To(d, Iota(df, first1), Iota(df, first2));
}
// For partial vectors, a single f32 vector is enough, and the prior overload
// might not be able to Repartition.
template <class D, typename First, HWY_IF_BF16_D(D), HWY_IF_LANES_GT_D(D, 1),
          HWY_IF_POW2_LE_D(D, -1)>
VFromD<D> IotaForSpecial(D d, First first) {
  const Rebind<float, D> df;
  return DemoteTo(d, Iota(df, first));
}
// OrderedDemote2To does not work for single lanes, so special-case that.
template <class D, typename First, HWY_IF_SPECIAL_FLOAT_D(D),
          HWY_IF_LANES_D(D, 1)>
VFromD<D> IotaForSpecial(D d, First first) {
  const Rebind<float, D> df;
  return DemoteTo(d, Set(df, static_cast<float>(first)));
}

// Compare expected array to vector.
// TODO(b/287462770): inline to work around incorrect SVE codegen.
template <class D, typename T = TFromD<D>>
HWY_INLINE void AssertVecEqual(D d, const T* expected, Vec<D> actual,
                               const char* filename, const int line) {
  const size_t N = Lanes(d);
  auto actual_lanes = AllocateAligned<T>(N);
  HWY_ASSERT(actual_lanes);
  Store(actual, d, actual_lanes.get());

  const auto info = hwy::detail::MakeTypeInfo<T>();
  const char* target_name = hwy::TargetName(HWY_TARGET);
  hwy::detail::AssertArrayEqual(info, expected, actual_lanes.get(), N,
                                target_name, filename, line);
}

// Compare expected vector to vector.
// TODO(b/287462770): inline to work around incorrect SVE codegen.
template <class D, typename T = TFromD<D>>
HWY_INLINE void AssertVecEqual(D d, Vec<D> expected, Vec<D> actual,
                               const char* filename, int line) {
  const size_t N = Lanes(d);
  auto expected_lanes = AllocateAligned<T>(N);
  auto actual_lanes = AllocateAligned<T>(N);
  HWY_ASSERT(expected_lanes && actual_lanes);
  Store(expected, d, expected_lanes.get());
  Store(actual, d, actual_lanes.get());

  const auto info = hwy::detail::MakeTypeInfo<T>();
  const char* target_name = hwy::TargetName(HWY_TARGET);
  hwy::detail::AssertArrayEqual(info, expected_lanes.get(), actual_lanes.get(),
                                N, target_name, filename, line);
}

// Only checks the valid mask elements (those whose index < Lanes(d)).
template <class D>
HWY_NOINLINE void AssertMaskEqual(D d, VecArg<Mask<D>> a, VecArg<Mask<D>> b,
                                  const char* filename, int line) {
  // lvalues prevented MSAN failure in farm_sve.
  const Vec<D> va = VecFromMask(d, a);
  const Vec<D> vb = VecFromMask(d, b);
  AssertVecEqual(d, va, vb, filename, line);

  const char* target_name = hwy::TargetName(HWY_TARGET);
  AssertEqual(CountTrue(d, a), CountTrue(d, b), target_name, filename, line);
  AssertEqual(AllTrue(d, a), AllTrue(d, b), target_name, filename, line);
  AssertEqual(AllFalse(d, a), AllFalse(d, b), target_name, filename, line);

  const size_t N = Lanes(d);
#if HWY_TARGET == HWY_SCALAR
  const Rebind<uint8_t, D> d8;
#else
  const Repartition<uint8_t, D> d8;
#endif
  const size_t N8 = Lanes(d8);
  auto bits_a = AllocateAligned<uint8_t>(HWY_MAX(size_t{8}, N8));
  auto bits_b = AllocateAligned<uint8_t>(size_t{HWY_MAX(8, N8)});
  HWY_ASSERT(bits_a && bits_b);
  memset(bits_a.get(), 0, N8);
  memset(bits_b.get(), 0, N8);
  const size_t num_bytes_a = StoreMaskBits(d, a, bits_a.get());
  const size_t num_bytes_b = StoreMaskBits(d, b, bits_b.get());
  AssertEqual(num_bytes_a, num_bytes_b, target_name, filename, line);
  size_t i = 0;
  // First check whole bytes (if that many elements are still valid)
  for (; i < N / 8; ++i) {
    if (bits_a[i] != bits_b[i]) {
      fprintf(stderr, "Mismatch in byte %d: %d != %d\n", static_cast<int>(i),
              bits_a[i], bits_b[i]);
      Print(d8, "expect", Load(d8, bits_a.get()), 0, N8);
      Print(d8, "actual", Load(d8, bits_b.get()), 0, N8);
      hwy::Abort(filename, line, "Masks not equal");
    }
  }
  // Then the valid bit(s) in the last byte.
  const size_t remainder = N % 8;
  if (remainder != 0) {
    const int mask = (1 << remainder) - 1;
    const int valid_a = bits_a[i] & mask;
    const int valid_b = bits_b[i] & mask;
    if (valid_a != valid_b) {
      fprintf(stderr, "Mismatch in last byte %d: %d != %d\n",
              static_cast<int>(i), valid_a, valid_b);
      Print(d8, "expect", Load(d8, bits_a.get()), 0, N8);
      Print(d8, "actual", Load(d8, bits_b.get()), 0, N8);
      hwy::Abort(filename, line, "Masks not equal");
    }
  }
}

// Only sets valid elements (those whose index < Lanes(d)). This helps catch
// tests that are not masking off the (undefined) upper mask elements.
//
// TODO(janwas): with HWY_NOINLINE GCC zeros the upper half of AVX2 masks.
template <class D>
HWY_INLINE Mask<D> MaskTrue(const D d) {
  return FirstN(d, Lanes(d));
}

// MaskFalse is now implemented in x86_128-inl.h on AVX3, arm_sve-inl.h on SVE,
// rvv-inl.h on RVV, and generic_ops-inl.h on all other targets

#ifndef HWY_ASSERT_EQ

#define HWY_ASSERT_EQ(expected, actual)                                     \
  hwy::AssertEqual(expected, actual, hwy::TargetName(HWY_TARGET), __FILE__, \
                   __LINE__)

#define HWY_ASSERT_ARRAY_EQ(expected, actual, count)                          \
  hwy::AssertArrayEqual(expected, actual, count, hwy::TargetName(HWY_TARGET), \
                        __FILE__, __LINE__)

#define HWY_ASSERT_STRING_EQ(expected, actual)                          \
  hwy::AssertStringEqual(expected, actual, hwy::TargetName(HWY_TARGET), \
                         __FILE__, __LINE__)

#define HWY_ASSERT_VEC_EQ(d, expected, actual) \
  AssertVecEqual(d, expected, actual, __FILE__, __LINE__)

#define HWY_ASSERT_MASK_EQ(d, expected, actual) \
  AssertMaskEqual(d, expected, actual, __FILE__, __LINE__)

#endif  // HWY_ASSERT_EQ

namespace detail {

// Helpers for instantiating tests with combinations of lane types / counts.

// Calls Test for each CappedTag<T, N> where N is in [kMinLanes, kMul * kMinArg]
// and the resulting Lanes() is in [min_lanes, max_lanes]. The upper bound
// is required to ensure capped vectors remain extendable. Implemented by
// recursively halving kMul until it is zero.
template <typename T, size_t kMul, size_t kMinArg, class Test, int kPow2 = 0>
struct ForeachCappedR {
  static void Do(size_t min_lanes, size_t max_lanes) {
    const CappedTag<T, kMul * kMinArg, kPow2> d;

    // If we already don't have enough lanes, stop.
    const size_t lanes = Lanes(d);
    if (lanes < min_lanes) return;

    if (lanes <= max_lanes) {
      Test()(T(), d);
    }
    ForeachCappedR<T, kMul / 2, kMinArg, Test, kPow2>::Do(min_lanes, max_lanes);
  }
};

// Base case to stop the recursion.
template <typename T, size_t kMinArg, class Test, int kPow2>
struct ForeachCappedR<T, 0, kMinArg, Test, kPow2> {
  static void Do(size_t, size_t) {}
};

#if HWY_HAVE_SCALABLE

template <typename T>
constexpr int MinPow2() {
  // Highway follows RVV LMUL in that the smallest fraction is 1/8th (encoded
  // as kPow2 == -3). The fraction also must not result in zero lanes for the
  // smallest possible vector size, which is 128 bits even on RISC-V (with the
  // application processor profile).
  return HWY_MAX(-3, -static_cast<int>(CeilLog2(16 / sizeof(T))));
}

constexpr int MaxPow2() {
#if HWY_TARGET == HWY_RVV
  // Only RVV allows multiple vector registers.
  return 3;  // LMUL=8
#else
  // For all other platforms, we cannot exceed a full vector.
  return 0;
#endif
}

// Iterates kPow2 up to and including kMaxPow2. Below we specialize for
// valid=false to stop the iteration. The ForeachPow2Trim enables shorter
// argument lists, but use ForeachPow2 when you want to specify the actual min.
template <typename T, int kPow2, int kMaxPow2, bool valid, class Test>
struct ForeachPow2 {
  static void Do(size_t min_lanes) {
    const ScalableTag<T, kPow2> d;

    static_assert(MinPow2<T>() <= kPow2 && kPow2 <= MaxPow2(), "");
    if (Lanes(d) >= min_lanes) {
      Test()(T(), d);
    } else {
      fprintf(stderr, "%d lanes < %d: T=%d pow=%d\n",
              static_cast<int>(Lanes(d)), static_cast<int>(min_lanes),
              static_cast<int>(sizeof(T)), kPow2);
      HWY_ASSERT(min_lanes != 1);
    }

    ForeachPow2<T, kPow2 + 1, kMaxPow2, (kPow2 + 1) <= kMaxPow2, Test>::Do(
        min_lanes);
  }
};

// Base case to stop the iteration.
template <typename T, int kPow2, int kMaxPow2, class Test>
struct ForeachPow2<T, kPow2, kMaxPow2, /*valid=*/false, Test> {
  static void Do(size_t) {}
};

// Iterates kPow2 over [MinPow2<T>() + kAddMin, MaxPow2() - kSubMax].
// This is a wrapper that shortens argument lists, allowing users to skip the
// MinPow2 and MaxPow2. Nonzero kAddMin implies a minimum LMUL, and nonzero
// kSubMax reduces the maximum LMUL (e.g. for type promotions, where the result
// is larger, thus the input cannot already use the maximum LMUL).
template <typename T, int kAddMin, int kSubMax, class Test>
using ForeachPow2Trim =
    ForeachPow2<T, MinPow2<T>() + kAddMin, MaxPow2() - kSubMax,
                MinPow2<T>() + kAddMin <= MaxPow2() - kSubMax, Test>;

#else
// ForeachCappedR already handled all possible sizes.
#endif  // HWY_HAVE_SCALABLE

}  // namespace detail

// These 'adapters' call a test for all possible N or kPow2 subject to
// constraints such as "vectors must be extendable" or "vectors >= 128 bits".
// They may be called directly, or via For*Types. Note that for an adapter C,
// `C<Test>(T())` does not call the test - the correct invocation is
// `C<Test>()(T())`, or preferably `ForAllTypes(C<Test>())`. We check at runtime
// that operator() is called to prevent such bugs. Note that this is not
// thread-safe, but that is fine because C are typically local variables.

// Calls Test for all powers of two in [1, Lanes(d) * (RVV? 2 : 1) ]. For
// interleaved_test; RVV segments are limited to 8 registers, so we can only go
// up to LMUL=2.
template <class Test>
class ForMaxPow2 {
  mutable bool called_ = false;

 public:
  ~ForMaxPow2() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;

#if HWY_TARGET == HWY_SCALAR
    detail::ForeachCappedR<T, 1, 1, Test>::Do(1, 1);
#else
    detail::ForeachCappedR<T, HWY_LANES(T), 1, Test>::Do(
        1, Lanes(ScalableTag<T>()));

#if HWY_TARGET == HWY_RVV
    // To get LMUL=2 (kPow2=1), 2 is what we subtract from MaxPow2()=3.
    detail::ForeachPow2Trim<T, 0, 2, Test>::Do(1);
#elif HWY_HAVE_SCALABLE
    detail::ForeachPow2Trim<T, 0, 0, Test>::Do(1);
#endif
#endif  // HWY_TARGET == HWY_SCALAR
  }
};

// Calls Test for all powers of two in [1, Lanes(d) >> kPow2]. This is for
// ops that widen their input, e.g. Combine (not supported by HWY_SCALAR).
template <class Test, int kPow2 = 1>
class ForExtendableVectors {
  mutable bool called_ = false;

 public:
  ~ForExtendableVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;
    constexpr size_t kMaxCapped = HWY_LANES(T);
    // Skip CappedTag that are already full vectors.
    const size_t max_lanes = Lanes(ScalableTag<T>()) >> kPow2;
    (void)kMaxCapped;
    (void)max_lanes;
#if HWY_TARGET == HWY_SCALAR
    // not supported
#else
    constexpr size_t kMul = kMaxCapped >> kPow2;
    constexpr size_t kMinArg = size_t{1} << kPow2;
    detail::ForeachCappedR<T, kMul, kMinArg, Test, -kPow2>::Do(1, max_lanes);
#if HWY_HAVE_SCALABLE
    detail::ForeachPow2Trim<T, 0, kPow2, Test>::Do(1);
#endif
#endif  // HWY_SCALAR
  }
};

// Calls Test for all power of two N in [1 << kPow2, Lanes(d)]. This is for ops
// that narrow their input, e.g. UpperHalf.
template <class Test, int kPow2 = 1>
class ForShrinkableVectors {
  mutable bool called_ = false;

 public:
  ~ForShrinkableVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;
    constexpr size_t kMinLanes = size_t{1} << kPow2;
    constexpr size_t kMaxCapped = HWY_LANES(T);
    // For shrinking, an upper limit is unnecessary.
    constexpr size_t max_lanes = kMaxCapped;

    (void)kMinLanes;
    (void)max_lanes;
    (void)max_lanes;
#if HWY_TARGET == HWY_SCALAR
    // not supported
#elif HWY_HAVE_SCALABLE
    detail::ForeachPow2Trim<T, kPow2, 0, Test>::Do(kMinLanes);
#else
    detail::ForeachCappedR<T, (kMaxCapped >> kPow2), kMinLanes, Test>::Do(
        kMinLanes, max_lanes);
#endif  // HWY_TARGET == HWY_SCALAR
  }
};

// Calls Test for all supported power of two vectors of at least kMinBits.
// Examples: AES or 64x64 require 128 bits, casts may require 64 bits.
template <size_t kMinBits, class Test>
class ForGEVectors {
  mutable bool called_ = false;

 public:
  ~ForGEVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;
    constexpr size_t kMaxCapped = HWY_LANES(T);
    constexpr size_t kMinLanes = kMinBits / 8 / sizeof(T);
    // An upper limit is unnecessary.
    constexpr size_t max_lanes = kMaxCapped;
    (void)max_lanes;
#if HWY_TARGET == HWY_SCALAR
    (void)kMinLanes;  // not supported
#else
    detail::ForeachCappedR<T, HWY_LANES(T) / kMinLanes, kMinLanes, Test>::Do(
        kMinLanes, max_lanes);
#if HWY_HAVE_SCALABLE
    // Can be 0 (handled below) if kMinBits > 128.
    constexpr size_t kRatio = 128 / kMinBits;
    constexpr int kMinPow2 =
        kRatio == 0 ? 0 : -static_cast<int>(CeilLog2(kRatio));
    constexpr bool kValid = kMinPow2 <= detail::MaxPow2();
    detail::ForeachPow2<T, kMinPow2, detail::MaxPow2(), kValid, Test>::Do(
        kMinLanes);
#endif
#endif  // HWY_TARGET == HWY_SCALAR
  }
};

template <class Test>
using ForGE128Vectors = ForGEVectors<128, Test>;

// Calls Test for all N that can be promoted (not the same as Extendable because
// HWY_SCALAR has one lane). Also used for ZipLower, but not ZipUpper.
template <class Test, int kPow2 = 1>
class ForPromoteVectors {
  mutable bool called_ = false;

 public:
  ~ForPromoteVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;
    constexpr size_t kFactor = size_t{1} << kPow2;
    static_assert(kFactor >= 2 && kFactor * sizeof(T) <= sizeof(uint64_t), "");
    constexpr size_t kMaxCapped = HWY_LANES(T);
    // Skip CappedTag that are already full vectors.
    const size_t max_lanes = Lanes(ScalableTag<T>()) >> kPow2;
    (void)kMaxCapped;
    (void)max_lanes;
#if HWY_TARGET == HWY_SCALAR
    detail::ForeachCappedR<T, 1, 1, Test>::Do(1, 1);
#else
    using DLargestFrom = CappedTag<T, (kMaxCapped >> kPow2) * kFactor, -kPow2>;
    static_assert(HWY_MAX_LANES_D(DLargestFrom) <= (kMaxCapped >> kPow2),
                  "HWY_MAX_LANES_D(DLargestFrom) must be less than or equal to "
                  "(kMaxCapped >> kPow2)");
    detail::ForeachCappedR<T, (kMaxCapped >> kPow2), kFactor, Test, -kPow2>::Do(
        1, max_lanes);
#if HWY_HAVE_SCALABLE
    detail::ForeachPow2Trim<T, 0, kPow2, Test>::Do(1);
#endif
#endif  // HWY_SCALAR
  }
};

// Calls Test for all N than can be demoted (not the same as Shrinkable because
// HWY_SCALAR has one lane and as a one-lane vector with a lane size of at least
// 2 bytes can always be demoted to a vector with a smaller lane type).
template <class Test, int kPow2 = 1>
class ForDemoteVectors {
  mutable bool called_ = false;

 public:
  ~ForDemoteVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;

#if HWY_HAVE_SCALABLE
    // kMinTVecPow2 is the smallest Pow2 for a vector with lane type T that is
    // supported by detail::ForeachPow2Trim
    constexpr int kMinTVecPow2 = detail::MinPow2<T>();

    // detail::MinPow2<T>() + kMinPow2Adj is the smallest Pow2 for a vector with
    // lane type T that can be demoted to a vector with a lane size of
    // (sizeof(T) >> kPow2)
    constexpr int kMinPow2Adj = HWY_MAX(-3 - kMinTVecPow2 + kPow2, 0);

    detail::ForeachPow2Trim<T, kMinPow2Adj, 0, Test>::Do(1);

    // On targets with scalable vectors, detail::ForeachCappedR below only
    // needs to be executed for vectors that have less than
    // Lanes(ScalableTag<T>()) as full vectors were already checked by the
    // detail::ForeachPow2Trim above.
    constexpr size_t kMaxCapped = HWY_LANES(T) >> 1;
    const size_t max_lanes = Lanes(ScalableTag<T>()) >> 1;
#else
    // On targets where HWY_HAVE_SCALABLE is 0, any vector with HWY_LANES(T)
    // or fewer lanes can always be demoted to a vector with a smaller lane
    // type.
    constexpr size_t kMaxCapped = HWY_LANES(T);
    const size_t max_lanes = kMaxCapped;
#endif

    detail::ForeachCappedR<T, kMaxCapped, 1, Test>::Do(1, max_lanes);
  }
};

// For LowerHalf/Quarter.
template <class Test, int kPow2 = 1>
class ForHalfVectors {
  mutable bool called_ = false;

 public:
  ~ForHalfVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*unused*/) const {
    called_ = true;
#if HWY_TARGET == HWY_SCALAR
    detail::ForeachCappedR<T, 1, 1, Test>::Do(1, 1);
#else
    constexpr size_t kMinLanes = size_t{1} << kPow2;
    // For shrinking, an upper limit is unnecessary.
    constexpr size_t kMaxCapped = HWY_LANES(T);
    detail::ForeachCappedR<T, (kMaxCapped >> kPow2), kMinLanes, Test>::Do(
        kMinLanes, kMaxCapped);

// TODO(janwas): call Extendable if kMinLanes check not required?
#if HWY_HAVE_SCALABLE
    detail::ForeachPow2Trim<T, kPow2, 0, Test>::Do(kMinLanes);
#endif
#endif  // HWY_TARGET == HWY_SCALAR
  }
};

// Calls Test for all power of two N in [1, Lanes(d)]. This is the default
// for ops that do not narrow nor widen their input, nor require 128 bits.
template <class Test>
class ForPartialVectors {
  mutable bool called_ = false;

 public:
  ~ForPartialVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T t) const {
    called_ = true;
#if HWY_TARGET == HWY_SCALAR
    (void)t;
    detail::ForeachCappedR<T, 1, 1, Test>::Do(1, 1);
#else
    ForExtendableVectors<Test, 0>()(t);
#endif
  }
};

// ForPartialFixedOrFullScalableVectors calls Test for each D where
// MaxLanes(D()) == MaxLanes(DFromV<VFromD<D>>())
#if HWY_HAVE_SCALABLE
template <class Test>
class ForPartialFixedOrFullScalableVectors {
  mutable bool called_ = false;

 public:
  ~ForPartialFixedOrFullScalableVectors() {
    if (!called_) {
      HWY_ABORT("Test is incorrect, ensure operator() is called");
    }
  }

  template <typename T>
  void operator()(T /*t*/) const {
    called_ = true;
#if HWY_TARGET == HWY_RVV
    constexpr int kMinPow2 = -3 + static_cast<int>(CeilLog2(sizeof(T)));
    constexpr int kMaxPow2 = 3;
#else
    constexpr int kMinPow2 = 0;
    constexpr int kMaxPow2 = 0;
#endif
    detail::ForeachPow2<T, kMinPow2, kMaxPow2, true, Test>::Do(1);
  }
};
#elif HWY_TARGET_IS_SVE
template <class Test>
using ForPartialFixedOrFullScalableVectors =
    ForGEVectors<HWY_MAX_BYTES * 8, Test>;
#else
template <class Test>
using ForPartialFixedOrFullScalableVectors = ForPartialVectors<Test>;
#endif

// Type lists to shorten call sites:

template <class Func>
void ForSignedTypes(const Func& func) {
  func(int8_t());
  func(int16_t());
  func(int32_t());
#if HWY_HAVE_INTEGER64
  func(int64_t());
#endif
}

template <class Func>
void ForUnsignedTypes(const Func& func) {
  func(uint8_t());
  func(uint16_t());
  func(uint32_t());
#if HWY_HAVE_INTEGER64
  func(uint64_t());
#endif
}

template <class Func>
void ForIntegerTypes(const Func& func) {
  ForSignedTypes(func);
  ForUnsignedTypes(func);
}

template <class Func>
void ForFloat16Types(const Func& func) {
#if HWY_HAVE_FLOAT16
  func(float16_t());
#else
  (void)func;
#endif
}

template <class Func>
void ForFloat64Types(const Func& func) {
#if HWY_HAVE_FLOAT64
  func(double());
#else
  (void)func;
#endif
}

// `#if HWY_HAVE_FLOAT*` is sufficient for tests using static dispatch. In
// sort_test we also use dynamic dispatch, so there we call the For*Dynamic
// functions which also check hwy::HaveFloat*.
template <class Func>
void ForFloat16TypesDynamic(const Func& func) {
#if HWY_HAVE_FLOAT16
  if (hwy::HaveFloat16()) {
    func(float16_t());
  }
#else
  (void)func;
#endif
}

template <class Func>
void ForFloat64TypesDynamic(const Func& func) {
#if HWY_HAVE_FLOAT64
  if (hwy::HaveFloat64()) {
    func(double());
  }
#else
  (void)func;
#endif
}

template <class Func>
void ForFloat3264Types(const Func& func) {
  func(float());
  ForFloat64Types(func);
}

template <class Func>
void ForFloatTypes(const Func& func) {
  ForFloat16Types(func);
  ForFloat3264Types(func);
}

template <class Func>
void ForFloatTypesDynamic(const Func& func) {
  ForFloat16TypesDynamic(func);
  func(float());
  ForFloat64TypesDynamic(func);
}

template <class Func>
void ForAllTypes(const Func& func) {
  ForIntegerTypes(func);
  ForFloatTypes(func);
}

// For ops that are also unconditionally available for bfloat16_t/float16_t.
template <class Func>
void ForSpecialTypes(const Func& func) {
  func(float16_t());
  func(bfloat16_t());
}
template <class Func>
void ForAllTypesAndSpecial(const Func& func) {
  ForAllTypes(func);
  ForSpecialTypes(func);
}

template <class Func>
void ForUI8(const Func& func) {
  func(uint8_t());
  func(int8_t());
}

template <class Func>
void ForUI16(const Func& func) {
  func(uint16_t());
  func(int16_t());
}

template <class Func>
void ForUIF16(const Func& func) {
  ForUI16(func);
  ForFloat16Types(func);
}

template <class Func>
void ForUI32(const Func& func) {
  func(uint32_t());
  func(int32_t());
}

template <class Func>
void ForUIF32(const Func& func) {
  ForUI32(func);
  func(float());
}

template <class Func>
void ForUI64(const Func& func) {
#if HWY_HAVE_INTEGER64
  func(uint64_t());
  func(int64_t());
#endif
}

template <class Func>
void ForUIF64(const Func& func) {
  ForUI64(func);
  ForFloat64Types(func);
}

template <class Func>
void ForUI3264(const Func& func) {
  ForUI32(func);
  ForUI64(func);
}

template <class Func>
void ForUIF3264(const Func& func) {
  ForUIF32(func);
  ForUIF64(func);
}

template <class Func>
void ForU816(const Func& func) {
  func(uint8_t());
  func(uint16_t());
}

template <class Func>
void ForI816(const Func& func) {
  func(int8_t());
  func(int16_t());
}

template <class Func>
void ForU163264(const Func& func) {
  func(uint16_t());
  func(uint32_t());
#if HWY_HAVE_INTEGER64
  func(uint64_t());
#endif
}

template <class Func>
void ForUI163264(const Func& func) {
  ForUI16(func);
  ForUI3264(func);
}

template <class Func>
void ForUIF163264(const Func& func) {
  ForUIF16(func);
  ForUIF3264(func);
}

// For tests that involve loops, adjust the trip count so that emulated tests
// finish quickly (but always at least 2 iterations to ensure some diversity).
constexpr size_t AdjustedReps(size_t max_reps) {
#if HWY_ARCH_RISCV
  return HWY_MAX(max_reps / 32, 2);
#elif HWY_IS_DEBUG_BUILD
  return HWY_MAX(max_reps / 8, 2);
#elif HWY_ARCH_ARM
  return HWY_MAX(max_reps / 4, 2);
#elif HWY_COMPILER_MSVC
  return HWY_MAX(max_reps / 2, 2);
#else
  return HWY_MAX(max_reps, 2);
#endif
}

// Same as above, but the loop trip count will be 1 << max_pow2.
constexpr size_t AdjustedLog2Reps(size_t max_pow2) {
  // If "negative" (unsigned wraparound), use original.
#if HWY_ARCH_RISCV
  return HWY_MIN(max_pow2 - 4, max_pow2);
#elif HWY_IS_DEBUG_BUILD
  return HWY_MIN(max_pow2 - 1, max_pow2);
#elif HWY_ARCH_ARM
  return HWY_MIN(max_pow2 - 1, max_pow2);
#else
  return max_pow2;
#endif
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // per-target include guard
