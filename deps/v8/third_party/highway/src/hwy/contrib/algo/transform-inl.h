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

// Per-target include guard
#if defined(HIGHWAY_HWY_CONTRIB_ALGO_TRANSFORM_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_ALGO_TRANSFORM_INL_H_
#undef HIGHWAY_HWY_CONTRIB_ALGO_TRANSFORM_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_ALGO_TRANSFORM_INL_H_
#endif

#include <stddef.h>

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// These functions avoid having to write a loop plus remainder handling in the
// (unfortunately still common) case where arrays are not aligned/padded. If the
// inputs are known to be aligned/padded, it is more efficient to write a single
// loop using Load(). We do not provide a TransformAlignedPadded because it
// would be more verbose than such a loop.
//
// Func is either a functor with a templated operator()(d, v[, v1[, v2]]), or a
// generic lambda if using C++14. The d argument is the same as was passed to
// the Generate etc. functions. Due to apparent limitations of Clang, it is
// currently necessary to add HWY_ATTR before the opening { of the lambda to
// avoid errors about "always_inline function .. requires target".
//
// We do not check HWY_MEM_OPS_MIGHT_FAULT because LoadN/StoreN do not fault.

// Fills `out[0, count)` with the vectors returned by `func(d, index_vec)`,
// where `index_vec` is `Vec<RebindToUnsigned<D>>`. On the first call to `func`,
// the value of its lane i is i, and increases by `Lanes(d)` after every call.
// Note that some of these indices may be `>= count`, but the elements that
// `func` returns in those lanes will not be written to `out`.
template <class D, class Func, typename T = TFromD<D>>
void Generate(D d, T* HWY_RESTRICT out, size_t count, const Func& func) {
  const RebindToUnsigned<D> du;
  using TU = TFromD<decltype(du)>;
  const size_t N = Lanes(d);

  size_t idx = 0;
  Vec<decltype(du)> vidx = Iota(du, 0);
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      StoreU(func(d, vidx), d, out + idx);
      vidx = Add(vidx, Set(du, static_cast<TU>(N)));
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  StoreN(func(d, vidx), d, out + idx, remaining);
}

// Calls `func(d, v)` for each input vector; out of bound lanes with index i >=
// `count` are instead taken from `no[i % Lanes(d)]`.
template <class D, class Func, typename T = TFromD<D>>
void Foreach(D d, const T* HWY_RESTRICT in, const size_t count, const Vec<D> no,
             const Func& func) {
  const size_t N = Lanes(d);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      const Vec<D> v = LoadU(d, in + idx);
      func(d, v);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadNOr(no, d, in + idx, remaining);
  func(d, v);
}

// Replaces `inout[idx]` with `func(d, inout[idx])`. Example usage: multiplying
// array elements by a constant.
template <class D, class Func, typename T = TFromD<D>>
void Transform(D d, T* HWY_RESTRICT inout, size_t count, const Func& func) {
  const size_t N = Lanes(d);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      const Vec<D> v = LoadU(d, inout + idx);
      StoreU(func(d, v), d, inout + idx);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadN(d, inout + idx, remaining);
  StoreN(func(d, v), d, inout + idx, remaining);
}

// Replaces `inout[idx]` with `func(d, inout[idx], in1[idx])`. Example usage:
// multiplying array elements by those of another array.
template <class D, class Func, typename T = TFromD<D>>
void Transform1(D d, T* HWY_RESTRICT inout, size_t count,
                const T* HWY_RESTRICT in1, const Func& func) {
  const size_t N = Lanes(d);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      const Vec<D> v = LoadU(d, inout + idx);
      const Vec<D> v1 = LoadU(d, in1 + idx);
      StoreU(func(d, v, v1), d, inout + idx);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadN(d, inout + idx, remaining);
  const Vec<D> v1 = LoadN(d, in1 + idx, remaining);
  StoreN(func(d, v, v1), d, inout + idx, remaining);
}

// Replaces `inout[idx]` with `func(d, inout[idx], in1[idx], in2[idx])`. Example
// usage: FMA of elements from three arrays, stored into the first array.
template <class D, class Func, typename T = TFromD<D>>
void Transform2(D d, T* HWY_RESTRICT inout, size_t count,
                const T* HWY_RESTRICT in1, const T* HWY_RESTRICT in2,
                const Func& func) {
  const size_t N = Lanes(d);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      const Vec<D> v = LoadU(d, inout + idx);
      const Vec<D> v1 = LoadU(d, in1 + idx);
      const Vec<D> v2 = LoadU(d, in2 + idx);
      StoreU(func(d, v, v1, v2), d, inout + idx);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadN(d, inout + idx, remaining);
  const Vec<D> v1 = LoadN(d, in1 + idx, remaining);
  const Vec<D> v2 = LoadN(d, in2 + idx, remaining);
  StoreN(func(d, v, v1, v2), d, inout + idx, remaining);
}

template <class D, typename T = TFromD<D>>
void Replace(D d, T* HWY_RESTRICT inout, size_t count, T new_t, T old_t) {
  const size_t N = Lanes(d);
  const Vec<D> old_v = Set(d, old_t);
  const Vec<D> new_v = Set(d, new_t);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      Vec<D> v = LoadU(d, inout + idx);
      StoreU(IfThenElse(Eq(v, old_v), new_v, v), d, inout + idx);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadN(d, inout + idx, remaining);
  StoreN(IfThenElse(Eq(v, old_v), new_v, v), d, inout + idx, remaining);
}

template <class D, class Func, typename T = TFromD<D>>
void ReplaceIf(D d, T* HWY_RESTRICT inout, size_t count, T new_t,
               const Func& func) {
  const size_t N = Lanes(d);
  const Vec<D> new_v = Set(d, new_t);

  size_t idx = 0;
  if (count >= N) {
    for (; idx <= count - N; idx += N) {
      Vec<D> v = LoadU(d, inout + idx);
      StoreU(IfThenElse(func(d, v), new_v, v), d, inout + idx);
    }
  }

  // `count` was a multiple of the vector length `N`: already done.
  if (HWY_UNLIKELY(idx == count)) return;

  const size_t remaining = count - idx;
  HWY_DASSERT(0 != remaining && remaining < N);
  const Vec<D> v = LoadN(d, inout + idx, remaining);
  StoreN(IfThenElse(func(d, v), new_v, v), d, inout + idx, remaining);
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_ALGO_TRANSFORM_INL_H_
