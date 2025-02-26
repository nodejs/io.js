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
#if defined(HIGHWAY_HWY_CONTRIB_ALGO_FIND_INL_H_) == \
    defined(HWY_TARGET_TOGGLE)  // NOLINT
#ifdef HIGHWAY_HWY_CONTRIB_ALGO_FIND_INL_H_
#undef HIGHWAY_HWY_CONTRIB_ALGO_FIND_INL_H_
#else
#define HIGHWAY_HWY_CONTRIB_ALGO_FIND_INL_H_
#endif

#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Returns index of the first element equal to `value` in `in[0, count)`, or
// `count` if not found.
template <class D, typename T = TFromD<D>>
size_t Find(D d, T value, const T* HWY_RESTRICT in, size_t count) {
  const size_t N = Lanes(d);
  const Vec<D> broadcasted = Set(d, value);

  size_t i = 0;
  if (count >= N) {
    for (; i <= count - N; i += N) {
      const intptr_t pos = FindFirstTrue(d, Eq(broadcasted, LoadU(d, in + i)));
      if (pos >= 0) return i + static_cast<size_t>(pos);
    }
  }

  if (i != count) {
#if HWY_MEM_OPS_MIGHT_FAULT
    // Scan single elements.
    const CappedTag<T, 1> d1;
    using V1 = Vec<decltype(d1)>;
    const V1 broadcasted1 = Set(d1, GetLane(broadcasted));
    for (; i < count; ++i) {
      if (AllTrue(d1, Eq(broadcasted1, LoadU(d1, in + i)))) {
        return i;
      }
    }
#else
    const size_t remaining = count - i;
    HWY_DASSERT(0 != remaining && remaining < N);
    const Mask<D> mask = FirstN(d, remaining);
    const Vec<D> v = MaskedLoad(mask, d, in + i);
    // Apply mask so that we don't 'find' the zero-padding from MaskedLoad.
    const intptr_t pos = FindFirstTrue(d, And(Eq(broadcasted, v), mask));
    if (pos >= 0) return i + static_cast<size_t>(pos);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
  }

  return count;  // not found
}

// Returns index of the first element in `in[0, count)` for which `func(d, vec)`
// returns true, otherwise `count`.
template <class D, class Func, typename T = TFromD<D>>
size_t FindIf(D d, const T* HWY_RESTRICT in, size_t count, const Func& func) {
  const size_t N = Lanes(d);

  size_t i = 0;
  if (count >= N) {
    for (; i <= count - N; i += N) {
      const intptr_t pos = FindFirstTrue(d, func(d, LoadU(d, in + i)));
      if (pos >= 0) return i + static_cast<size_t>(pos);
    }
  }

  if (i != count) {
#if HWY_MEM_OPS_MIGHT_FAULT
    // Scan single elements.
    const CappedTag<T, 1> d1;
    for (; i < count; ++i) {
      if (AllTrue(d1, func(d1, LoadU(d1, in + i)))) {
        return i;
      }
    }
#else
    const size_t remaining = count - i;
    HWY_DASSERT(0 != remaining && remaining < N);
    const Mask<D> mask = FirstN(d, remaining);
    const Vec<D> v = MaskedLoad(mask, d, in + i);
    // Apply mask so that we don't 'find' the zero-padding from MaskedLoad.
    const intptr_t pos = FindFirstTrue(d, And(func(d, v), mask));
    if (pos >= 0) return i + static_cast<size_t>(pos);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
  }

  return count;  // not found
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_ALGO_FIND_INL_H_
