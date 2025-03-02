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

// Demo of functions that might be called from multiple SIMD modules (either
// other -inl.h files, or a .cc file between begin/end_target-inl). This is
// optional - all SIMD code can reside in .cc files. However, this allows
// splitting code into different files while still inlining instead of requiring
// calling through function pointers.

// Per-target include guard. This is only required when using dynamic dispatch,
// i.e. including foreach_target.h. For static dispatch, a normal include
// guard would be fine because the header is only compiled once.
#if defined(HIGHWAY_HWY_EXAMPLES_SKELETON_INL_H_) == defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_EXAMPLES_SKELETON_INL_H_
#undef HIGHWAY_HWY_EXAMPLES_SKELETON_INL_H_
#else
#define HIGHWAY_HWY_EXAMPLES_SKELETON_INL_H_
#endif

// It is fine to #include normal or *-inl headers.
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace skeleton {
namespace HWY_NAMESPACE {

// Highway ops reside here; ADL does not find templates nor builtins.
namespace hn = hwy::HWY_NAMESPACE;

// Example of a type-agnostic (caller-specified lane type) and width-agnostic
// (uses best available instruction set) function in a header.
//
// Computes x[i] = mul_array[i] * x_array[i] + add_array[i] for i < size.
template <class D, typename T>
HWY_MAYBE_UNUSED void MulAddLoop(const D d, const T* HWY_RESTRICT mul_array,
                                 const T* HWY_RESTRICT add_array,
                                 const size_t size, T* HWY_RESTRICT x_array) {
  for (size_t i = 0; i < size; i += hn::Lanes(d)) {
    const auto mul = hn::Load(d, mul_array + i);
    const auto add = hn::Load(d, add_array + i);
    auto x = hn::Load(d, x_array + i);
    x = hn::MulAdd(mul, x, add);
    hn::Store(x, d, x_array + i);
  }
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace skeleton
HWY_AFTER_NAMESPACE();

#endif  // include guard
