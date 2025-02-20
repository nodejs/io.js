// Copyright 2023 Matthew Kolbe
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

#if defined(HIGHWAY_HWY_CONTRIB_UNROLLER_UNROLLER_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_UNROLLER_UNROLLER_INL_H_
#undef HIGHWAY_HWY_CONTRIB_UNROLLER_UNROLLER_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_UNROLLER_UNROLLER_INL_H_
#endif

#include <cstdlib>  // std::abs

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

template <class DERIVED, typename IN_T, typename OUT_T>
struct UnrollerUnit {
  static constexpr size_t kMaxTSize = HWY_MAX(sizeof(IN_T), sizeof(OUT_T));
  using LargerT = SignedFromSize<kMaxTSize>;  // only the size matters.

  DERIVED* me() { return static_cast<DERIVED*>(this); }

  static constexpr size_t MaxUnitLanes() {
    return HWY_MAX_LANES_D(hn::ScalableTag<LargerT>);
  }
  static size_t ActualLanes() { return Lanes(hn::ScalableTag<LargerT>()); }

  using LargerD = hn::CappedTag<LargerT, MaxUnitLanes()>;
  using IT = hn::Rebind<IN_T, LargerD>;
  using OT = hn::Rebind<OUT_T, LargerD>;
  IT d_in;
  OT d_out;
  using Y_VEC = hn::Vec<OT>;
  using X_VEC = hn::Vec<IT>;

  Y_VEC Func(const ptrdiff_t idx, const X_VEC x, const Y_VEC y) {
    return me()->Func(idx, x, y);
  }

  X_VEC X0Init() { return me()->X0InitImpl(); }

  X_VEC X0InitImpl() { return hn::Zero(d_in); }

  Y_VEC YInit() { return me()->YInitImpl(); }

  Y_VEC YInitImpl() { return hn::Zero(d_out); }

  X_VEC Load(const ptrdiff_t idx, IN_T* from) {
    return me()->LoadImpl(idx, from);
  }

  X_VEC LoadImpl(const ptrdiff_t idx, IN_T* from) {
    return hn::LoadU(d_in, from + idx);
  }

  // MaskLoad can take in either a positive or negative number for `places`. if
  // the number is positive, then it loads the top `places` values, and if it's
  // negative, it loads the bottom |places| values. example: places = 3
  //      | o | o | o | x | x | x | x | x |
  // example places = -3
  //      | x | x | x | x | x | o | o | o |
  X_VEC MaskLoad(const ptrdiff_t idx, IN_T* from, const ptrdiff_t places) {
    return me()->MaskLoadImpl(idx, from, places);
  }

  X_VEC MaskLoadImpl(const ptrdiff_t idx, IN_T* from, const ptrdiff_t places) {
    auto mask = hn::FirstN(d_in, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d_in,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    return hn::MaskedLoad(mask, d_in, from + idx);
  }

  bool StoreAndShortCircuit(const ptrdiff_t idx, OUT_T* to, const Y_VEC x) {
    return me()->StoreAndShortCircuitImpl(idx, to, x);
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, OUT_T* to, const Y_VEC x) {
    hn::StoreU(x, d_out, to + idx);
    return true;
  }

  ptrdiff_t MaskStore(const ptrdiff_t idx, OUT_T* to, const Y_VEC x,
                      ptrdiff_t const places) {
    return me()->MaskStoreImpl(idx, to, x, places);
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, OUT_T* to, const Y_VEC x,
                          const ptrdiff_t places) {
    auto mask = hn::FirstN(d_out, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d_out,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    hn::BlendedStore(x, mask, d_out, to + idx);
    return std::abs(places);
  }

  ptrdiff_t Reduce(const Y_VEC x, OUT_T* to) { return me()->ReduceImpl(x, to); }

  ptrdiff_t ReduceImpl(const Y_VEC x, OUT_T* to) {
    // default does nothing
    (void)x;
    (void)to;
    return 0;
  }

  void Reduce(const Y_VEC x0, const Y_VEC x1, const Y_VEC x2, Y_VEC* y) {
    me()->ReduceImpl(x0, x1, x2, y);
  }

  void ReduceImpl(const Y_VEC x0, const Y_VEC x1, const Y_VEC x2, Y_VEC* y) {
    // default does nothing
    (void)x0;
    (void)x1;
    (void)x2;
    (void)y;
  }
};

template <class DERIVED, typename IN0_T, typename IN1_T, typename OUT_T>
struct UnrollerUnit2D {
  DERIVED* me() { return static_cast<DERIVED*>(this); }

  static constexpr size_t kMaxTSize =
      HWY_MAX(sizeof(IN0_T), HWY_MAX(sizeof(IN1_T), sizeof(OUT_T)));
  using LargerT = SignedFromSize<kMaxTSize>;  // only the size matters.

  static constexpr size_t MaxUnitLanes() {
    return HWY_MAX_LANES_D(hn::ScalableTag<LargerT>);
  }
  static size_t ActualLanes() { return Lanes(hn::ScalableTag<LargerT>()); }

  using LargerD = hn::CappedTag<LargerT, MaxUnitLanes()>;

  using I0T = hn::Rebind<IN0_T, LargerD>;
  using I1T = hn::Rebind<IN1_T, LargerD>;
  using OT = hn::Rebind<OUT_T, LargerD>;
  I0T d_in0;
  I1T d_in1;
  OT d_out;
  using Y_VEC = hn::Vec<OT>;
  using X0_VEC = hn::Vec<I0T>;
  using X1_VEC = hn::Vec<I1T>;

  hn::Vec<OT> Func(const ptrdiff_t idx, const hn::Vec<I0T> x0,
                   const hn::Vec<I1T> x1, const Y_VEC y) {
    return me()->Func(idx, x0, x1, y);
  }

  X0_VEC X0Init() { return me()->X0InitImpl(); }

  X0_VEC X0InitImpl() { return hn::Zero(d_in0); }

  X1_VEC X1Init() { return me()->X1InitImpl(); }

  X1_VEC X1InitImpl() { return hn::Zero(d_in1); }

  Y_VEC YInit() { return me()->YInitImpl(); }

  Y_VEC YInitImpl() { return hn::Zero(d_out); }

  X0_VEC Load0(const ptrdiff_t idx, IN0_T* from) {
    return me()->Load0Impl(idx, from);
  }

  X0_VEC Load0Impl(const ptrdiff_t idx, IN0_T* from) {
    return hn::LoadU(d_in0, from + idx);
  }

  X1_VEC Load1(const ptrdiff_t idx, IN1_T* from) {
    return me()->Load1Impl(idx, from);
  }

  X1_VEC Load1Impl(const ptrdiff_t idx, IN1_T* from) {
    return hn::LoadU(d_in1, from + idx);
  }

  // maskload can take in either a positive or negative number for `places`. if
  // the number is positive, then it loads the top `places` values, and if it's
  // negative, it loads the bottom |places| values. example: places = 3
  //      | o | o | o | x | x | x | x | x |
  // example places = -3
  //      | x | x | x | x | x | o | o | o |
  X0_VEC MaskLoad0(const ptrdiff_t idx, IN0_T* from, const ptrdiff_t places) {
    return me()->MaskLoad0Impl(idx, from, places);
  }

  X0_VEC MaskLoad0Impl(const ptrdiff_t idx, IN0_T* from,
                       const ptrdiff_t places) {
    auto mask = hn::FirstN(d_in0, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d_in0,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    return hn::MaskedLoad(mask, d_in0, from + idx);
  }

  hn::Vec<I1T> MaskLoad1(const ptrdiff_t idx, IN1_T* from,
                         const ptrdiff_t places) {
    return me()->MaskLoad1Impl(idx, from, places);
  }

  hn::Vec<I1T> MaskLoad1Impl(const ptrdiff_t idx, IN1_T* from,
                             const ptrdiff_t places) {
    auto mask = hn::FirstN(d_in1, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d_in1,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    return hn::MaskedLoad(mask, d_in1, from + idx);
  }

  // store returns a bool that is `false` when
  bool StoreAndShortCircuit(const ptrdiff_t idx, OUT_T* to, const Y_VEC x) {
    return me()->StoreAndShortCircuitImpl(idx, to, x);
  }

  bool StoreAndShortCircuitImpl(const ptrdiff_t idx, OUT_T* to, const Y_VEC x) {
    hn::StoreU(x, d_out, to + idx);
    return true;
  }

  ptrdiff_t MaskStore(const ptrdiff_t idx, OUT_T* to, const Y_VEC x,
                      const ptrdiff_t places) {
    return me()->MaskStoreImpl(idx, to, x, places);
  }

  ptrdiff_t MaskStoreImpl(const ptrdiff_t idx, OUT_T* to, const Y_VEC x,
                          const ptrdiff_t places) {
    auto mask = hn::FirstN(d_out, static_cast<size_t>(places));
    auto maskneg = hn::Not(hn::FirstN(
        d_out,
        static_cast<size_t>(places + static_cast<ptrdiff_t>(ActualLanes()))));
    if (places < 0) mask = maskneg;

    hn::BlendedStore(x, mask, d_out, to + idx);
    return std::abs(places);
  }

  ptrdiff_t Reduce(const Y_VEC x, OUT_T* to) { return me()->ReduceImpl(x, to); }

  ptrdiff_t ReduceImpl(const Y_VEC x, OUT_T* to) {
    // default does nothing
    (void)x;
    (void)to;
    return 0;
  }

  void Reduce(const Y_VEC x0, const Y_VEC x1, const Y_VEC x2, Y_VEC* y) {
    me()->ReduceImpl(x0, x1, x2, y);
  }

  void ReduceImpl(const Y_VEC x0, const Y_VEC x1, const Y_VEC x2, Y_VEC* y) {
    // default does nothing
    (void)x0;
    (void)x1;
    (void)x2;
    (void)y;
  }
};

template <class FUNC, typename IN_T, typename OUT_T>
inline void Unroller(FUNC& f, IN_T* HWY_RESTRICT x, OUT_T* HWY_RESTRICT y,
                     const ptrdiff_t n) {
  auto xx = f.X0Init();
  auto yy = f.YInit();
  ptrdiff_t i = 0;

#if HWY_MEM_OPS_MIGHT_FAULT
  constexpr auto lane_sz =
      static_cast<ptrdiff_t>(RemoveRef<FUNC>::MaxUnitLanes());
  if (n < lane_sz) {
    const DFromV<decltype(yy)> d;
    // this may not fit on the stack for HWY_RVV, but we do not reach this code
    // there
    HWY_ALIGN IN_T xtmp[static_cast<size_t>(lane_sz)];
    HWY_ALIGN OUT_T ytmp[static_cast<size_t>(lane_sz)];

    CopyBytes(x, xtmp, static_cast<size_t>(n) * sizeof(IN_T));
    xx = f.MaskLoad(0, xtmp, n);
    yy = f.Func(0, xx, yy);
    Store(Zero(d), d, ytmp);
    i += f.MaskStore(0, ytmp, yy, n);
    i += f.Reduce(yy, ytmp);
    CopyBytes(ytmp, y, static_cast<size_t>(i) * sizeof(OUT_T));
    return;
  }
#endif

  const ptrdiff_t actual_lanes =
      static_cast<ptrdiff_t>(RemoveRef<FUNC>::ActualLanes());
  if (n > 4 * actual_lanes) {
    auto xx1 = f.X0Init();
    auto yy1 = f.YInit();
    auto xx2 = f.X0Init();
    auto yy2 = f.YInit();
    auto xx3 = f.X0Init();
    auto yy3 = f.YInit();

    while (i + 4 * actual_lanes - 1 < n) {
      xx = f.Load(i, x);
      i += actual_lanes;
      xx1 = f.Load(i, x);
      i += actual_lanes;
      xx2 = f.Load(i, x);
      i += actual_lanes;
      xx3 = f.Load(i, x);
      i -= 3 * actual_lanes;

      yy = f.Func(i, xx, yy);
      yy1 = f.Func(i + actual_lanes, xx1, yy1);
      yy2 = f.Func(i + 2 * actual_lanes, xx2, yy2);
      yy3 = f.Func(i + 3 * actual_lanes, xx3, yy3);

      if (!f.StoreAndShortCircuit(i, y, yy)) return;
      i += actual_lanes;
      if (!f.StoreAndShortCircuit(i, y, yy1)) return;
      i += actual_lanes;
      if (!f.StoreAndShortCircuit(i, y, yy2)) return;
      i += actual_lanes;
      if (!f.StoreAndShortCircuit(i, y, yy3)) return;
      i += actual_lanes;
    }

    f.Reduce(yy3, yy2, yy1, &yy);
  }

  while (i + actual_lanes - 1 < n) {
    xx = f.Load(i, x);
    yy = f.Func(i, xx, yy);
    if (!f.StoreAndShortCircuit(i, y, yy)) return;
    i += actual_lanes;
  }

  if (i != n) {
    xx = f.MaskLoad(n - actual_lanes, x, i - n);
    yy = f.Func(n - actual_lanes, xx, yy);
    f.MaskStore(n - actual_lanes, y, yy, i - n);
  }

  f.Reduce(yy, y);
}

template <class FUNC, typename IN0_T, typename IN1_T, typename OUT_T>
inline void Unroller(FUNC& HWY_RESTRICT f, IN0_T* HWY_RESTRICT x0,
                     IN1_T* HWY_RESTRICT x1, OUT_T* HWY_RESTRICT y,
                     const ptrdiff_t n) {
  const ptrdiff_t lane_sz =
      static_cast<ptrdiff_t>(RemoveRef<FUNC>::ActualLanes());

  auto xx00 = f.X0Init();
  auto xx10 = f.X1Init();
  auto yy = f.YInit();

  ptrdiff_t i = 0;

#if HWY_MEM_OPS_MIGHT_FAULT
  if (n < lane_sz) {
    const DFromV<decltype(yy)> d;
    // this may not fit on the stack for HWY_RVV, but we do not reach this code
    // there
    constexpr auto max_lane_sz =
        static_cast<ptrdiff_t>(RemoveRef<FUNC>::MaxUnitLanes());
    HWY_ALIGN IN0_T xtmp0[static_cast<size_t>(max_lane_sz)];
    HWY_ALIGN IN1_T xtmp1[static_cast<size_t>(max_lane_sz)];
    HWY_ALIGN OUT_T ytmp[static_cast<size_t>(max_lane_sz)];

    CopyBytes(x0, xtmp0, static_cast<size_t>(n) * sizeof(IN0_T));
    CopyBytes(x1, xtmp1, static_cast<size_t>(n) * sizeof(IN1_T));
    xx00 = f.MaskLoad0(0, xtmp0, n);
    xx10 = f.MaskLoad1(0, xtmp1, n);
    yy = f.Func(0, xx00, xx10, yy);
    Store(Zero(d), d, ytmp);
    i += f.MaskStore(0, ytmp, yy, n);
    i += f.Reduce(yy, ytmp);
    CopyBytes(ytmp, y, static_cast<size_t>(i) * sizeof(OUT_T));
    return;
  }
#endif

  if (n > 4 * lane_sz) {
    auto xx01 = f.X0Init();
    auto xx11 = f.X1Init();
    auto yy1 = f.YInit();
    auto xx02 = f.X0Init();
    auto xx12 = f.X1Init();
    auto yy2 = f.YInit();
    auto xx03 = f.X0Init();
    auto xx13 = f.X1Init();
    auto yy3 = f.YInit();

    while (i + 4 * lane_sz - 1 < n) {
      xx00 = f.Load0(i, x0);
      xx10 = f.Load1(i, x1);
      i += lane_sz;
      xx01 = f.Load0(i, x0);
      xx11 = f.Load1(i, x1);
      i += lane_sz;
      xx02 = f.Load0(i, x0);
      xx12 = f.Load1(i, x1);
      i += lane_sz;
      xx03 = f.Load0(i, x0);
      xx13 = f.Load1(i, x1);
      i -= 3 * lane_sz;

      yy = f.Func(i, xx00, xx10, yy);
      yy1 = f.Func(i + lane_sz, xx01, xx11, yy1);
      yy2 = f.Func(i + 2 * lane_sz, xx02, xx12, yy2);
      yy3 = f.Func(i + 3 * lane_sz, xx03, xx13, yy3);

      if (!f.StoreAndShortCircuit(i, y, yy)) return;
      i += lane_sz;
      if (!f.StoreAndShortCircuit(i, y, yy1)) return;
      i += lane_sz;
      if (!f.StoreAndShortCircuit(i, y, yy2)) return;
      i += lane_sz;
      if (!f.StoreAndShortCircuit(i, y, yy3)) return;
      i += lane_sz;
    }

    f.Reduce(yy3, yy2, yy1, &yy);
  }

  while (i + lane_sz - 1 < n) {
    xx00 = f.Load0(i, x0);
    xx10 = f.Load1(i, x1);
    yy = f.Func(i, xx00, xx10, yy);
    if (!f.StoreAndShortCircuit(i, y, yy)) return;
    i += lane_sz;
  }

  if (i != n) {
    xx00 = f.MaskLoad0(n - lane_sz, x0, i - n);
    xx10 = f.MaskLoad1(n - lane_sz, x1, i - n);
    yy = f.Func(n - lane_sz, xx00, xx10, yy);
    f.MaskStore(n - lane_sz, y, yy, i - n);
  }

  f.Reduce(yy, y);
}

}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_UNROLLER_UNROLLER_INL_H_
