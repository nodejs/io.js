// Copyright Google LLC 2021
//           Matthew Kolbe 2023
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

#include <vector>

#include "hwy/base.h"

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/unroller/unroller_test.cc"  //NOLINT
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
#include "hwy/contrib/unroller/unroller-inl.h"
#include "hwy/tests/test_util-inl.h"
// clang-format on

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

template <typename T>
T SimpleDot(const T* pa, const T* pb, size_t num) {
  T sum = 0;
  for (size_t i = 0; i < num; ++i) {
    // For reasons unknown, fp16 += does not compile on clang (Arm).
    sum = ConvertScalarTo<T>(sum + pa[i] * pb[i]);
  }
  return sum;
}

template <typename T>
T SimpleAcc(const T* pa, size_t num) {
  T sum = 0;
  for (size_t i = 0; i < num; ++i) {
    sum += pa[i];
  }
  return sum;
}

template <typename T>
T SimpleMin(const T* pa, size_t num) {
  T min = HighestValue<T>();
  for (size_t i = 0; i < num; ++i) {
    if (min > pa[i]) min = pa[i];
  }
  return min;
}

template <typename T>
struct MultiplyUnit : UnrollerUnit2D<MultiplyUnit<T>, T, T, T> {
  using TT = hn::ScalableTag<T>;
  HWY_INLINE hn::Vec<TT> Func(ptrdiff_t idx, const hn::Vec<TT> x0,
                              const hn::Vec<TT> x1, const hn::Vec<TT> y) {
    (void)idx;
    (void)y;
    return hn::Mul(x0, x1);
  }
};

template <typename FROM_T, typename TO_T>
struct ConvertUnit : UnrollerUnit<ConvertUnit<FROM_T, TO_T>, FROM_T, TO_T> {
  using Base = UnrollerUnit<ConvertUnit<FROM_T, TO_T>, FROM_T, TO_T>;
  using Base::MaxUnitLanes;
  using typename Base::LargerD;

  using TT_FROM = hn::Rebind<FROM_T, LargerD>;
  using TT_TO = hn::Rebind<TO_T, LargerD>;

  template <
      class ToD, class FromV,
      hwy::EnableIf<(sizeof(TFromV<FromV>) > sizeof(TFromD<ToD>))>* = nullptr>
  static HWY_INLINE hn::Vec<ToD> DoConvertVector(ToD d, FromV v) {
    return hn::DemoteTo(d, v);
  }
  template <
      class ToD, class FromV,
      hwy::EnableIf<(sizeof(TFromV<FromV>) == sizeof(TFromD<ToD>))>* = nullptr>
  static HWY_INLINE hn::Vec<ToD> DoConvertVector(ToD d, FromV v) {
    return hn::ConvertTo(d, v);
  }
  template <
      class ToD, class FromV,
      hwy::EnableIf<(sizeof(TFromV<FromV>) < sizeof(TFromD<ToD>))>* = nullptr>
  static HWY_INLINE hn::Vec<ToD> DoConvertVector(ToD d, FromV v) {
    return hn::PromoteTo(d, v);
  }

  hn::Vec<TT_TO> Func(ptrdiff_t idx, const hn::Vec<TT_FROM> x,
                      const hn::Vec<TT_TO> y) {
    (void)idx;
    (void)y;
    TT_TO d;
    return DoConvertVector(d, x);
  }
};

// Returns a value that does not compare equal to `value`.
template <class D, HWY_IF_FLOAT_D(D)>
HWY_INLINE Vec<D> OtherValue(D d, TFromD<D> /*value*/) {
  return NaN(d);
}
template <class D, HWY_IF_NOT_FLOAT_D(D)>
HWY_INLINE Vec<D> OtherValue(D d, TFromD<D> value) {
  return hn::Set(d, hwy::AddWithWraparound(value, 1));
}

// Caveat: stores lane indices as MakeSigned<T>, which may overflow for 8-bit T
// on HWY_RVV.
template <typename T>
struct FindUnit : UnrollerUnit<FindUnit<T>, T, MakeSigned<T>> {
  using TI = MakeSigned<T>;
  using Base = UnrollerUnit<FindUnit<T>, T, TI>;
  using Base::ActualLanes;
  using Base::MaxUnitLanes;

  using D = hn::CappedTag<T, MaxUnitLanes()>;
  T to_find;
  D d;
  using DI = RebindToSigned<D>;
  DI di;

  FindUnit(T find) : to_find(find) {}

  hn::Vec<DI> Func(ptrdiff_t idx, const hn::Vec<D> x, const hn::Vec<DI> y) {
    const Mask<D> msk = hn::Eq(x, hn::Set(d, to_find));
    const TI first_idx = static_cast<TI>(hn::FindFirstTrue(d, msk));
    if (first_idx > -1)
      return hn::Set(di, static_cast<TI>(static_cast<TI>(idx) + first_idx));
    else
      return y;
  }

  hn::Vec<D> X0InitImpl() { return OtherValue(D(), to_find); }

  hn::Vec<DI> YInitImpl() { return hn::Set(di, TI{-1}); }

  hn::Vec<D> MaskLoadImpl(const ptrdiff_t idx, T* from,
                          const ptrdiff_t places) {
    auto mask = hn::FirstN(d, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;
    return hn::IfThenElse(mask, hn::MaskedLoad(mask, d, from + idx),
                          X0InitImpl());
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, TI* to,
                                const hn::Vec<DI> x) {
    (void)idx;

    TI a = hn::GetLane(x);
    to[0] = a;

    if (a == -1) return true;

    return false;
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, TI* to, const hn::Vec<DI> x,
                          const ptrdiff_t places) {
    (void)idx;
    (void)places;
    TI a = hn::GetLane(x);
    to[0] = a;
    return 1;
  }
};

template <typename T>
struct AccumulateUnit : UnrollerUnit<AccumulateUnit<T>, T, T> {
  using TT = hn::ScalableTag<T>;
  hn::Vec<TT> Func(ptrdiff_t idx, const hn::Vec<TT> x, const hn::Vec<TT> y) {
    (void)idx;
    return hn::Add(x, y);
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, T* to,
                                const hn::Vec<TT> x) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    return true;
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, T* to, const hn::Vec<TT> x,
                          const ptrdiff_t places) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    (void)places;
    return 0;
  }

  ptrdiff_t ReduceImpl(const hn::Vec<TT> x, T* to) {
    const hn::ScalableTag<T> d;
    (*to) = hn::ReduceSum(d, x);
    return 1;
  }

  void ReduceImpl(const hn::Vec<TT> x0, const hn::Vec<TT> x1,
                  const hn::Vec<TT> x2, hn::Vec<TT>* y) {
    (*y) = hn::Add(hn::Add(*y, x0), hn::Add(x1, x2));
  }
};

template <typename T>
struct MinUnit : UnrollerUnit<MinUnit<T>, T, T> {
  using Base = UnrollerUnit<MinUnit<T>, T, T>;
  using Base::ActualLanes;

  using TT = hn::ScalableTag<T>;
  TT d;

  hn::Vec<TT> Func(const ptrdiff_t idx, const hn::Vec<TT> x,
                   const hn::Vec<TT> y) {
    (void)idx;
    return hn::Min(y, x);
  }

  hn::Vec<TT> YInitImpl() { return hn::Set(d, HighestValue<T>()); }

  hn::Vec<TT> MaskLoadImpl(const ptrdiff_t idx, T* from,
                           const ptrdiff_t places) {
    auto mask = hn::FirstN(d, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    auto def = YInitImpl();
    return hn::MaskedLoadOr(def, mask, d, from + idx);
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, T* to,
                                const hn::Vec<TT> x) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    return true;
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, T* to, const hn::Vec<TT> x,
                          const ptrdiff_t places) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    (void)places;
    return 0;
  }

  ptrdiff_t ReduceImpl(const hn::Vec<TT> x, T* to) {
    auto minvect = hn::MinOfLanes(d, x);
    (*to) = hn::ExtractLane(minvect, 0);
    return 1;
  }

  void ReduceImpl(const hn::Vec<TT> x0, const hn::Vec<TT> x1,
                  const hn::Vec<TT> x2, hn::Vec<TT>* y) {
    auto a = hn::Min(x1, x0);
    auto b = hn::Min(*y, x2);
    (*y) = hn::Min(a, b);
  }
};

template <typename T>
struct DotUnit : UnrollerUnit2D<DotUnit<T>, T, T, T> {
  using TT = hn::ScalableTag<T>;

  hn::Vec<TT> Func(const ptrdiff_t idx, const hn::Vec<TT> x0,
                   const hn::Vec<TT> x1, const hn::Vec<TT> y) {
    (void)idx;
    return hn::MulAdd(x0, x1, y);
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, T* to,
                                const hn::Vec<TT> x) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    return true;
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, T* to, const hn::Vec<TT> x,
                          const ptrdiff_t places) {
    // no stores in a reducer
    (void)idx;
    (void)to;
    (void)x;
    (void)places;
    return 0;
  }

  ptrdiff_t ReduceImpl(const hn::Vec<TT> x, T* to) {
    const hn::ScalableTag<T> d;
    (*to) = hn::ReduceSum(d, x);
    return 1;
  }

  void ReduceImpl(const hn::Vec<TT> x0, const hn::Vec<TT> x1,
                  const hn::Vec<TT> x2, hn::Vec<TT>* y) {
    (*y) = hn::Add(hn::Add(*y, x0), hn::Add(x1, x2));
  }
};

template <class D>
std::vector<size_t> Counts(D d) {
  const size_t N = Lanes(d);
  return std::vector<size_t>{1,
                             3,
                             7,
                             16,
                             HWY_MAX(N / 2, 1),
                             HWY_MAX(2 * N / 3, 1),
                             N,
                             N + 1,
                             4 * N / 3,
                             3 * N,
                             8 * N,
                             8 * N + 2,
                             256 * N - 1,
                             256 * N};
}

struct TestDot {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // TODO(janwas): avoid internal compiler error
#if HWY_TARGET == HWY_SVE || HWY_TARGET == HWY_SVE2 || HWY_COMPILER_MSVC
    (void)d;
#else
    RandomState rng;
    const auto random_t = [&rng]() {
      const int32_t bits = static_cast<int32_t>(Random32(&rng)) & 1023;
      return static_cast<float>(bits - 512) * (1.0f / 64);
    };

    for (size_t num : Counts(d)) {
      AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(num);
      AlignedFreeUniquePtr<T[]> pb = AllocateAligned<T>(num);
      AlignedFreeUniquePtr<T[]> py = AllocateAligned<T>(num);

      HWY_ASSERT(pa && pb && py);
      T* a = pa.get();
      T* b = pb.get();
      T* y = py.get();

      size_t i = 0;
      for (; i < num; ++i) {
        a[i] = ConvertScalarTo<T>(random_t());
        b[i] = ConvertScalarTo<T>(random_t());
      }

      const T expected_dot = SimpleDot(a, b, num);
      MultiplyUnit<T> multfn;
      Unroller(multfn, a, b, y, static_cast<ptrdiff_t>(num));
      AccumulateUnit<T> accfn;
      T dot_via_mul_acc;
      Unroller(accfn, y, &dot_via_mul_acc, static_cast<ptrdiff_t>(num));
      const double tolerance = 48.0 *
                               ConvertScalarTo<double>(hwy::Epsilon<T>()) *
                               ScalarAbs(expected_dot);
      HWY_ASSERT(ScalarAbs(expected_dot - dot_via_mul_acc) < tolerance);

      DotUnit<T> dotfn;
      T dotr;
      Unroller(dotfn, a, b, &dotr, static_cast<ptrdiff_t>(num));
      HWY_ASSERT(ConvertScalarTo<double>(ScalarAbs((expected_dot - dotr))) <
                 tolerance);

      auto expected_min = SimpleMin(a, num);
      MinUnit<T> minfn;
      T minr;
      Unroller(minfn, a, &minr, static_cast<ptrdiff_t>(num));

      HWY_ASSERT(ConvertScalarTo<double>(ScalarAbs(expected_min - minr)) <
                 1e-7);
    }
#endif
  }
};

void TestAllDot() { ForFloatTypes(ForPartialVectors<TestDot>()); }

struct TestConvert {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    // TODO(janwas): avoid internal compiler error
#if HWY_TARGET == HWY_SVE || HWY_TARGET == HWY_SVE2 || HWY_COMPILER_MSVC
    (void)d;
#else
    for (size_t num : Counts(d)) {
      AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(num);
      AlignedFreeUniquePtr<int[]> pto = AllocateAligned<int>(num);
      HWY_ASSERT(pa && pto);
      T* HWY_RESTRICT a = pa.get();
      int* HWY_RESTRICT to = pto.get();

      for (size_t i = 0; i < num; ++i) {
        a[i] = ConvertScalarTo<T>(static_cast<double>(i) * 0.25);
      }

      ConvertUnit<T, int> cvtfn;
      Unroller(cvtfn, a, to, static_cast<ptrdiff_t>(num));
      for (size_t i = 0; i < num; ++i) {
        // TODO(janwas): RVV QEMU fcvt_rtz appears to 'truncate' 4.75 to 5.
        HWY_ASSERT(
            static_cast<int>(a[i]) == to[i] ||
            (HWY_TARGET == HWY_RVV && static_cast<int>(a[i]) == to[i] - 1));
      }

      ConvertUnit<int, T> cvtbackfn;
      Unroller(cvtbackfn, to, a, static_cast<ptrdiff_t>(num));
      for (size_t i = 0; i < num; ++i) {
        HWY_ASSERT_EQ(ConvertScalarTo<T>(to[i]), a[i]);
      }
    }
#endif
  }
};

void TestAllConvert() { ForFloat3264Types(ForPartialVectors<TestConvert>()); }

struct TestFind {
  template <typename T, class D>
  HWY_NOINLINE void operator()(T /*unused*/, D d) {
    for (size_t num : Counts(d)) {
      AlignedFreeUniquePtr<T[]> pa = AllocateAligned<T>(num);
      HWY_ASSERT(pa);
      T* a = pa.get();

      for (size_t i = 0; i < num; ++i) a[i] = ConvertScalarTo<T>(i);

      FindUnit<T> cvtfn(ConvertScalarTo<T>(num - 1));
      MakeSigned<T> idx = 0;
      Unroller(cvtfn, a, &idx, static_cast<ptrdiff_t>(num));
      HWY_ASSERT(static_cast<MakeUnsigned<T>>(idx) < num);
      HWY_ASSERT(a[idx] == ConvertScalarTo<T>(num - 1));

      FindUnit<T> cvtfnzero((T)(0));
      Unroller(cvtfnzero, a, &idx, static_cast<ptrdiff_t>(num));
      HWY_ASSERT(static_cast<MakeUnsigned<T>>(idx) < num);
      HWY_ASSERT(a[idx] == (T)(0));

      // For f16, we cannot search for `num` because it may round to a value
      // that is actually in the (large) array.
      FindUnit<T> cvtfnnotin(HighestValue<T>());
      Unroller(cvtfnnotin, a, &idx, static_cast<ptrdiff_t>(num));
      HWY_ASSERT(idx == -1);
    }
  }
};

void TestAllFind() { ForFloatTypes(ForPartialVectors<TestFind>()); }

}  // namespace
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(UnrollerTest);
HWY_EXPORT_AND_TEST_P(UnrollerTest, TestAllDot);
HWY_EXPORT_AND_TEST_P(UnrollerTest, TestAllConvert);
HWY_EXPORT_AND_TEST_P(UnrollerTest, TestAllFind);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
