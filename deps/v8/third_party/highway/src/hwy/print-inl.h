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

// Print() function

#include "hwy/highway.h"
#include "hwy/print.h"

// Per-target include guard
#if defined(HIGHWAY_HWY_PRINT_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_PRINT_INL_H_
#undef HIGHWAY_HWY_PRINT_INL_H_
#else
#define HIGHWAY_HWY_PRINT_INL_H_
#endif

#if HWY_TARGET == HWY_RVV
#include "hwy/aligned_allocator.h"
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Prints lanes around `lane`, in memory order.
template <class D, class V = VFromD<D>>
HWY_API void Print(const D d, const char* caption, V v, size_t lane_u = 0,
                   size_t max_lanes = 7) {
  const size_t N = Lanes(d);
  using T = TFromD<D>;
#if HWY_TARGET == HWY_RVV
  auto storage = AllocateAligned<T>(N);
  T* HWY_RESTRICT lanes = storage.get();
#else
  // This works around an SVE compile error on GCC 11 and 12. Calling
  // AllocateAligned here would seem to require it be marked with HWY_ATTR.
  HWY_ALIGN T lanes[MaxLanes(d)];
#endif
  Store(v, d, lanes);

  const auto info = hwy::detail::MakeTypeInfo<T>();
  hwy::detail::PrintArray(info, caption, lanes, N, lane_u, max_lanes);
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // per-target include guard
