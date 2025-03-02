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
#if defined(HIGHWAY_HWY_CONTRIB_SORT_SORTING_NETWORKS_TOGGLE) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_SORT_SORTING_NETWORKS_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_SORTING_NETWORKS_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_SORTING_NETWORKS_TOGGLE
#endif

#include "hwy/contrib/sort/shared-inl.h"  // SortConstants
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace detail {

#if VQSORT_ENABLED

using Constants = hwy::SortConstants;

// ------------------------------ SharedTraits

// Code shared between all traits. It's unclear whether these can profitably be
// specialized for Lane vs Block, or optimized like SortPairsDistance1 using
// Compare/DupOdd.
template <class Base>
struct SharedTraits : public Base {
  using SharedTraitsForSortingNetwork =
      SharedTraits<typename Base::TraitsForSortingNetwork>;

  // Conditionally swaps lane 0 with 2, 1 with 3 etc.
  template <class D>
  HWY_INLINE Vec<D> SortPairsDistance2(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->SwapAdjacentPairs(d, v);
    base->Sort2(d, v, swapped);
    return base->OddEvenPairs(d, swapped, v);
  }

  // Swaps with the vector formed by reversing contiguous groups of 8 keys.
  template <class D>
  HWY_INLINE Vec<D> SortPairsReverse8(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    Vec<D> swapped = base->ReverseKeys8(d, v);
    base->Sort2(d, v, swapped);
    return base->OddEvenQuads(d, swapped, v);
  }

  // Swaps with the vector formed by reversing contiguous groups of 8 keys.
  template <class D>
  HWY_INLINE Vec<D> SortPairsReverse16(D d, Vec<D> v) const {
    const Base* base = static_cast<const Base*>(this);
    static_assert(Constants::kMaxCols <= 16, "Need actual Reverse16");
    Vec<D> swapped = base->ReverseKeys(d, v);
    base->Sort2(d, v, swapped);
    return ConcatUpperLower(d, swapped, v);  // 8 = half of the vector
  }
};

// ------------------------------ Sorting network

// Sorting networks for independent columns in 2, 4 and 8 vectors from
// https://bertdobbelaere.github.io/sorting_networks.html.

template <class D, class Traits, class V = Vec<D>>
HWY_INLINE void Sort2(D d, Traits st, V& v0, V& v1) {
  st.Sort2(d, v0, v1);
}

template <class D, class Traits, class V = Vec<D>>
HWY_INLINE void Sort4(D d, Traits st, V& v0, V& v1, V& v2, V& v3) {
  st.Sort2(d, v0, v2);
  st.Sort2(d, v1, v3);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v1, v2);
}

template <class D, class Traits, class V = Vec<D>>
HWY_INLINE void Sort8(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4, V& v5,
                      V& v6, V& v7) {
  st.Sort2(d, v0, v2);
  st.Sort2(d, v1, v3);
  st.Sort2(d, v4, v6);
  st.Sort2(d, v5, v7);

  st.Sort2(d, v0, v4);
  st.Sort2(d, v1, v5);
  st.Sort2(d, v2, v6);
  st.Sort2(d, v3, v7);

  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);

  st.Sort2(d, v2, v4);
  st.Sort2(d, v3, v5);

  st.Sort2(d, v1, v4);
  st.Sort2(d, v3, v6);

  st.Sort2(d, v1, v2);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v5, v6);
}

// (Green's irregular) sorting network for independent columns in 16 vectors.
template <class D, class Traits, class V = Vec<D>>
HWY_INLINE void Sort16(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4, V& v5,
                       V& v6, V& v7, V& v8, V& v9, V& va, V& vb, V& vc, V& vd,
                       V& ve, V& vf) {
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
  st.Sort2(d, va, vb);
  st.Sort2(d, vc, vd);
  st.Sort2(d, ve, vf);
  st.Sort2(d, v0, v2);
  st.Sort2(d, v1, v3);
  st.Sort2(d, v4, v6);
  st.Sort2(d, v5, v7);
  st.Sort2(d, v8, va);
  st.Sort2(d, v9, vb);
  st.Sort2(d, vc, ve);
  st.Sort2(d, vd, vf);
  st.Sort2(d, v0, v4);
  st.Sort2(d, v1, v5);
  st.Sort2(d, v2, v6);
  st.Sort2(d, v3, v7);
  st.Sort2(d, v8, vc);
  st.Sort2(d, v9, vd);
  st.Sort2(d, va, ve);
  st.Sort2(d, vb, vf);
  st.Sort2(d, v0, v8);
  st.Sort2(d, v1, v9);
  st.Sort2(d, v2, va);
  st.Sort2(d, v3, vb);
  st.Sort2(d, v4, vc);
  st.Sort2(d, v5, vd);
  st.Sort2(d, v6, ve);
  st.Sort2(d, v7, vf);
  st.Sort2(d, v5, va);
  st.Sort2(d, v6, v9);
  st.Sort2(d, v3, vc);
  st.Sort2(d, v7, vb);
  st.Sort2(d, vd, ve);
  st.Sort2(d, v4, v8);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v1, v4);
  st.Sort2(d, v7, vd);
  st.Sort2(d, v2, v8);
  st.Sort2(d, vb, ve);
  st.Sort2(d, v2, v4);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v9, va);
  st.Sort2(d, vb, vd);
  st.Sort2(d, v3, v8);
  st.Sort2(d, v7, vc);
  st.Sort2(d, v3, v5);
  st.Sort2(d, v6, v8);
  st.Sort2(d, v7, v9);
  st.Sort2(d, va, vc);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v7, v8);
  st.Sort2(d, v9, va);
  st.Sort2(d, vb, vc);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
}

// ------------------------------ Merging networks

// Blacher's hybrid bitonic/odd-even networks, generated by print_network.cc.
// For acceptable performance, these must be inlined, otherwise vectors are
// loaded from the stack. The kKeysPerVector allows calling from generic code
// but skipping the functions when vectors have too few lanes for
// st.SortPairsDistance1 to compile. `if constexpr` in the caller would also
// work, but is not available in C++11. We write out the (unused) argument types
// rather than `...` because GCC 9 (but not 10) fails to compile with `...`.

template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 1)>
HWY_INLINE void Merge8x2(D, Traits, V, V, V, V, V, V, V, V) {}
template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 2)>
HWY_INLINE void Merge8x4(D, Traits, V, V, V, V, V, V, V, V) {}

template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 1)>
HWY_INLINE void Merge16x2(D, Traits, V, V, V, V, V, V, V, V, V, V, V, V, V, V,
                          V, V) {}
template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 2)>
HWY_INLINE void Merge16x4(D, Traits, V, V, V, V, V, V, V, V, V, V, V, V, V, V,
                          V, V) {}
template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 4)>
HWY_INLINE void Merge16x8(D, Traits, V, V, V, V, V, V, V, V, V, V, V, V, V, V,
                          V, V) {}
template <size_t kKeysPerVector, class D, class Traits, class V,
          HWY_IF_LANES_LE(kKeysPerVector, 8)>
HWY_INLINE void Merge16x16(D, Traits, V, V, V, V, V, V, V, V, V, V, V, V, V, V,
                           V, V) {}

template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 1)>
HWY_INLINE void Merge8x2(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                         V& v5, V& v6, V& v7) {
  v7 = st.ReverseKeys2(d, v7);
  v6 = st.ReverseKeys2(d, v6);
  v5 = st.ReverseKeys2(d, v5);
  v4 = st.ReverseKeys2(d, v4);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);

  v3 = st.ReverseKeys2(d, v3);
  v2 = st.ReverseKeys2(d, v2);
  v7 = st.ReverseKeys2(d, v7);
  v6 = st.ReverseKeys2(d, v6);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);

  v1 = st.ReverseKeys2(d, v1);
  v3 = st.ReverseKeys2(d, v3);
  v5 = st.ReverseKeys2(d, v5);
  v7 = st.ReverseKeys2(d, v7);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
}

template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 2)>
HWY_INLINE void Merge8x4(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                         V& v5, V& v6, V& v7) {
  v7 = st.ReverseKeys4(d, v7);
  v6 = st.ReverseKeys4(d, v6);
  v5 = st.ReverseKeys4(d, v5);
  v4 = st.ReverseKeys4(d, v4);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);

  v3 = st.ReverseKeys4(d, v3);
  v2 = st.ReverseKeys4(d, v2);
  v7 = st.ReverseKeys4(d, v7);
  v6 = st.ReverseKeys4(d, v6);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);

  v1 = st.ReverseKeys4(d, v1);
  v3 = st.ReverseKeys4(d, v3);
  v5 = st.ReverseKeys4(d, v5);
  v7 = st.ReverseKeys4(d, v7);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);

  v0 = st.SortPairsReverse4(d, v0);
  v1 = st.SortPairsReverse4(d, v1);
  v2 = st.SortPairsReverse4(d, v2);
  v3 = st.SortPairsReverse4(d, v3);
  v4 = st.SortPairsReverse4(d, v4);
  v5 = st.SortPairsReverse4(d, v5);
  v6 = st.SortPairsReverse4(d, v6);
  v7 = st.SortPairsReverse4(d, v7);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
}

// Only used by the now-deprecated SortingNetwork().
template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 1)>
HWY_INLINE void Merge16x2(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                          V& v5, V& v6, V& v7, V& v8, V& v9, V& va, V& vb,
                          V& vc, V& vd, V& ve, V& vf) {
  vf = st.ReverseKeys2(d, vf);
  ve = st.ReverseKeys2(d, ve);
  vd = st.ReverseKeys2(d, vd);
  vc = st.ReverseKeys2(d, vc);
  vb = st.ReverseKeys2(d, vb);
  va = st.ReverseKeys2(d, va);
  v9 = st.ReverseKeys2(d, v9);
  v8 = st.ReverseKeys2(d, v8);
  st.Sort2(d, v0, vf);
  st.Sort2(d, v1, ve);
  st.Sort2(d, v2, vd);
  st.Sort2(d, v3, vc);
  st.Sort2(d, v4, vb);
  st.Sort2(d, v5, va);
  st.Sort2(d, v6, v9);
  st.Sort2(d, v7, v8);

  v7 = st.ReverseKeys2(d, v7);
  v6 = st.ReverseKeys2(d, v6);
  v5 = st.ReverseKeys2(d, v5);
  v4 = st.ReverseKeys2(d, v4);
  vf = st.ReverseKeys2(d, vf);
  ve = st.ReverseKeys2(d, ve);
  vd = st.ReverseKeys2(d, vd);
  vc = st.ReverseKeys2(d, vc);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v8, vf);
  st.Sort2(d, v9, ve);
  st.Sort2(d, va, vd);
  st.Sort2(d, vb, vc);

  v3 = st.ReverseKeys2(d, v3);
  v2 = st.ReverseKeys2(d, v2);
  v7 = st.ReverseKeys2(d, v7);
  v6 = st.ReverseKeys2(d, v6);
  vb = st.ReverseKeys2(d, vb);
  va = st.ReverseKeys2(d, va);
  vf = st.ReverseKeys2(d, vf);
  ve = st.ReverseKeys2(d, ve);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v8, vb);
  st.Sort2(d, v9, va);
  st.Sort2(d, vc, vf);
  st.Sort2(d, vd, ve);

  v1 = st.ReverseKeys2(d, v1);
  v3 = st.ReverseKeys2(d, v3);
  v5 = st.ReverseKeys2(d, v5);
  v7 = st.ReverseKeys2(d, v7);
  v9 = st.ReverseKeys2(d, v9);
  vb = st.ReverseKeys2(d, vb);
  vd = st.ReverseKeys2(d, vd);
  vf = st.ReverseKeys2(d, vf);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
  st.Sort2(d, va, vb);
  st.Sort2(d, vc, vd);
  st.Sort2(d, ve, vf);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
  v8 = st.SortPairsDistance1(d, v8);
  v9 = st.SortPairsDistance1(d, v9);
  va = st.SortPairsDistance1(d, va);
  vb = st.SortPairsDistance1(d, vb);
  vc = st.SortPairsDistance1(d, vc);
  vd = st.SortPairsDistance1(d, vd);
  ve = st.SortPairsDistance1(d, ve);
  vf = st.SortPairsDistance1(d, vf);
}

template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 2)>
HWY_INLINE void Merge16x4(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                          V& v5, V& v6, V& v7, V& v8, V& v9, V& va, V& vb,
                          V& vc, V& vd, V& ve, V& vf) {
  vf = st.ReverseKeys4(d, vf);
  ve = st.ReverseKeys4(d, ve);
  vd = st.ReverseKeys4(d, vd);
  vc = st.ReverseKeys4(d, vc);
  vb = st.ReverseKeys4(d, vb);
  va = st.ReverseKeys4(d, va);
  v9 = st.ReverseKeys4(d, v9);
  v8 = st.ReverseKeys4(d, v8);
  st.Sort2(d, v0, vf);
  st.Sort2(d, v1, ve);
  st.Sort2(d, v2, vd);
  st.Sort2(d, v3, vc);
  st.Sort2(d, v4, vb);
  st.Sort2(d, v5, va);
  st.Sort2(d, v6, v9);
  st.Sort2(d, v7, v8);

  v7 = st.ReverseKeys4(d, v7);
  v6 = st.ReverseKeys4(d, v6);
  v5 = st.ReverseKeys4(d, v5);
  v4 = st.ReverseKeys4(d, v4);
  vf = st.ReverseKeys4(d, vf);
  ve = st.ReverseKeys4(d, ve);
  vd = st.ReverseKeys4(d, vd);
  vc = st.ReverseKeys4(d, vc);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v8, vf);
  st.Sort2(d, v9, ve);
  st.Sort2(d, va, vd);
  st.Sort2(d, vb, vc);

  v3 = st.ReverseKeys4(d, v3);
  v2 = st.ReverseKeys4(d, v2);
  v7 = st.ReverseKeys4(d, v7);
  v6 = st.ReverseKeys4(d, v6);
  vb = st.ReverseKeys4(d, vb);
  va = st.ReverseKeys4(d, va);
  vf = st.ReverseKeys4(d, vf);
  ve = st.ReverseKeys4(d, ve);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v8, vb);
  st.Sort2(d, v9, va);
  st.Sort2(d, vc, vf);
  st.Sort2(d, vd, ve);

  v1 = st.ReverseKeys4(d, v1);
  v3 = st.ReverseKeys4(d, v3);
  v5 = st.ReverseKeys4(d, v5);
  v7 = st.ReverseKeys4(d, v7);
  v9 = st.ReverseKeys4(d, v9);
  vb = st.ReverseKeys4(d, vb);
  vd = st.ReverseKeys4(d, vd);
  vf = st.ReverseKeys4(d, vf);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
  st.Sort2(d, va, vb);
  st.Sort2(d, vc, vd);
  st.Sort2(d, ve, vf);

  v0 = st.SortPairsReverse4(d, v0);
  v1 = st.SortPairsReverse4(d, v1);
  v2 = st.SortPairsReverse4(d, v2);
  v3 = st.SortPairsReverse4(d, v3);
  v4 = st.SortPairsReverse4(d, v4);
  v5 = st.SortPairsReverse4(d, v5);
  v6 = st.SortPairsReverse4(d, v6);
  v7 = st.SortPairsReverse4(d, v7);
  v8 = st.SortPairsReverse4(d, v8);
  v9 = st.SortPairsReverse4(d, v9);
  va = st.SortPairsReverse4(d, va);
  vb = st.SortPairsReverse4(d, vb);
  vc = st.SortPairsReverse4(d, vc);
  vd = st.SortPairsReverse4(d, vd);
  ve = st.SortPairsReverse4(d, ve);
  vf = st.SortPairsReverse4(d, vf);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
  v8 = st.SortPairsDistance1(d, v8);
  v9 = st.SortPairsDistance1(d, v9);
  va = st.SortPairsDistance1(d, va);
  vb = st.SortPairsDistance1(d, vb);
  vc = st.SortPairsDistance1(d, vc);
  vd = st.SortPairsDistance1(d, vd);
  ve = st.SortPairsDistance1(d, ve);
  vf = st.SortPairsDistance1(d, vf);
}

template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 4)>
HWY_INLINE void Merge16x8(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                          V& v5, V& v6, V& v7, V& v8, V& v9, V& va, V& vb,
                          V& vc, V& vd, V& ve, V& vf) {
  vf = st.ReverseKeys8(d, vf);
  ve = st.ReverseKeys8(d, ve);
  vd = st.ReverseKeys8(d, vd);
  vc = st.ReverseKeys8(d, vc);
  vb = st.ReverseKeys8(d, vb);
  va = st.ReverseKeys8(d, va);
  v9 = st.ReverseKeys8(d, v9);
  v8 = st.ReverseKeys8(d, v8);
  st.Sort2(d, v0, vf);
  st.Sort2(d, v1, ve);
  st.Sort2(d, v2, vd);
  st.Sort2(d, v3, vc);
  st.Sort2(d, v4, vb);
  st.Sort2(d, v5, va);
  st.Sort2(d, v6, v9);
  st.Sort2(d, v7, v8);

  v7 = st.ReverseKeys8(d, v7);
  v6 = st.ReverseKeys8(d, v6);
  v5 = st.ReverseKeys8(d, v5);
  v4 = st.ReverseKeys8(d, v4);
  vf = st.ReverseKeys8(d, vf);
  ve = st.ReverseKeys8(d, ve);
  vd = st.ReverseKeys8(d, vd);
  vc = st.ReverseKeys8(d, vc);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v8, vf);
  st.Sort2(d, v9, ve);
  st.Sort2(d, va, vd);
  st.Sort2(d, vb, vc);

  v3 = st.ReverseKeys8(d, v3);
  v2 = st.ReverseKeys8(d, v2);
  v7 = st.ReverseKeys8(d, v7);
  v6 = st.ReverseKeys8(d, v6);
  vb = st.ReverseKeys8(d, vb);
  va = st.ReverseKeys8(d, va);
  vf = st.ReverseKeys8(d, vf);
  ve = st.ReverseKeys8(d, ve);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v8, vb);
  st.Sort2(d, v9, va);
  st.Sort2(d, vc, vf);
  st.Sort2(d, vd, ve);

  v1 = st.ReverseKeys8(d, v1);
  v3 = st.ReverseKeys8(d, v3);
  v5 = st.ReverseKeys8(d, v5);
  v7 = st.ReverseKeys8(d, v7);
  v9 = st.ReverseKeys8(d, v9);
  vb = st.ReverseKeys8(d, vb);
  vd = st.ReverseKeys8(d, vd);
  vf = st.ReverseKeys8(d, vf);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
  st.Sort2(d, va, vb);
  st.Sort2(d, vc, vd);
  st.Sort2(d, ve, vf);

  v0 = st.SortPairsReverse8(d, v0);
  v1 = st.SortPairsReverse8(d, v1);
  v2 = st.SortPairsReverse8(d, v2);
  v3 = st.SortPairsReverse8(d, v3);
  v4 = st.SortPairsReverse8(d, v4);
  v5 = st.SortPairsReverse8(d, v5);
  v6 = st.SortPairsReverse8(d, v6);
  v7 = st.SortPairsReverse8(d, v7);
  v8 = st.SortPairsReverse8(d, v8);
  v9 = st.SortPairsReverse8(d, v9);
  va = st.SortPairsReverse8(d, va);
  vb = st.SortPairsReverse8(d, vb);
  vc = st.SortPairsReverse8(d, vc);
  vd = st.SortPairsReverse8(d, vd);
  ve = st.SortPairsReverse8(d, ve);
  vf = st.SortPairsReverse8(d, vf);

  v0 = st.SortPairsDistance2(d, v0);
  v1 = st.SortPairsDistance2(d, v1);
  v2 = st.SortPairsDistance2(d, v2);
  v3 = st.SortPairsDistance2(d, v3);
  v4 = st.SortPairsDistance2(d, v4);
  v5 = st.SortPairsDistance2(d, v5);
  v6 = st.SortPairsDistance2(d, v6);
  v7 = st.SortPairsDistance2(d, v7);
  v8 = st.SortPairsDistance2(d, v8);
  v9 = st.SortPairsDistance2(d, v9);
  va = st.SortPairsDistance2(d, va);
  vb = st.SortPairsDistance2(d, vb);
  vc = st.SortPairsDistance2(d, vc);
  vd = st.SortPairsDistance2(d, vd);
  ve = st.SortPairsDistance2(d, ve);
  vf = st.SortPairsDistance2(d, vf);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
  v8 = st.SortPairsDistance1(d, v8);
  v9 = st.SortPairsDistance1(d, v9);
  va = st.SortPairsDistance1(d, va);
  vb = st.SortPairsDistance1(d, vb);
  vc = st.SortPairsDistance1(d, vc);
  vd = st.SortPairsDistance1(d, vd);
  ve = st.SortPairsDistance1(d, ve);
  vf = st.SortPairsDistance1(d, vf);
}

// Unused on MSVC, see below
#if !HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD

template <size_t kKeysPerVector, class D, class Traits, class V = Vec<D>,
          HWY_IF_LANES_GT(kKeysPerVector, 8)>
HWY_INLINE void Merge16x16(D d, Traits st, V& v0, V& v1, V& v2, V& v3, V& v4,
                           V& v5, V& v6, V& v7, V& v8, V& v9, V& va, V& vb,
                           V& vc, V& vd, V& ve, V& vf) {
  vf = st.ReverseKeys16(d, vf);
  ve = st.ReverseKeys16(d, ve);
  vd = st.ReverseKeys16(d, vd);
  vc = st.ReverseKeys16(d, vc);
  vb = st.ReverseKeys16(d, vb);
  va = st.ReverseKeys16(d, va);
  v9 = st.ReverseKeys16(d, v9);
  v8 = st.ReverseKeys16(d, v8);
  st.Sort2(d, v0, vf);
  st.Sort2(d, v1, ve);
  st.Sort2(d, v2, vd);
  st.Sort2(d, v3, vc);
  st.Sort2(d, v4, vb);
  st.Sort2(d, v5, va);
  st.Sort2(d, v6, v9);
  st.Sort2(d, v7, v8);

  v7 = st.ReverseKeys16(d, v7);
  v6 = st.ReverseKeys16(d, v6);
  v5 = st.ReverseKeys16(d, v5);
  v4 = st.ReverseKeys16(d, v4);
  vf = st.ReverseKeys16(d, vf);
  ve = st.ReverseKeys16(d, ve);
  vd = st.ReverseKeys16(d, vd);
  vc = st.ReverseKeys16(d, vc);
  st.Sort2(d, v0, v7);
  st.Sort2(d, v1, v6);
  st.Sort2(d, v2, v5);
  st.Sort2(d, v3, v4);
  st.Sort2(d, v8, vf);
  st.Sort2(d, v9, ve);
  st.Sort2(d, va, vd);
  st.Sort2(d, vb, vc);

  v3 = st.ReverseKeys16(d, v3);
  v2 = st.ReverseKeys16(d, v2);
  v7 = st.ReverseKeys16(d, v7);
  v6 = st.ReverseKeys16(d, v6);
  vb = st.ReverseKeys16(d, vb);
  va = st.ReverseKeys16(d, va);
  vf = st.ReverseKeys16(d, vf);
  ve = st.ReverseKeys16(d, ve);
  st.Sort2(d, v0, v3);
  st.Sort2(d, v1, v2);
  st.Sort2(d, v4, v7);
  st.Sort2(d, v5, v6);
  st.Sort2(d, v8, vb);
  st.Sort2(d, v9, va);
  st.Sort2(d, vc, vf);
  st.Sort2(d, vd, ve);

  v1 = st.ReverseKeys16(d, v1);
  v3 = st.ReverseKeys16(d, v3);
  v5 = st.ReverseKeys16(d, v5);
  v7 = st.ReverseKeys16(d, v7);
  v9 = st.ReverseKeys16(d, v9);
  vb = st.ReverseKeys16(d, vb);
  vd = st.ReverseKeys16(d, vd);
  vf = st.ReverseKeys16(d, vf);
  st.Sort2(d, v0, v1);
  st.Sort2(d, v2, v3);
  st.Sort2(d, v4, v5);
  st.Sort2(d, v6, v7);
  st.Sort2(d, v8, v9);
  st.Sort2(d, va, vb);
  st.Sort2(d, vc, vd);
  st.Sort2(d, ve, vf);

  v0 = st.SortPairsReverse16(d, v0);
  v1 = st.SortPairsReverse16(d, v1);
  v2 = st.SortPairsReverse16(d, v2);
  v3 = st.SortPairsReverse16(d, v3);
  v4 = st.SortPairsReverse16(d, v4);
  v5 = st.SortPairsReverse16(d, v5);
  v6 = st.SortPairsReverse16(d, v6);
  v7 = st.SortPairsReverse16(d, v7);
  v8 = st.SortPairsReverse16(d, v8);
  v9 = st.SortPairsReverse16(d, v9);
  va = st.SortPairsReverse16(d, va);
  vb = st.SortPairsReverse16(d, vb);
  vc = st.SortPairsReverse16(d, vc);
  vd = st.SortPairsReverse16(d, vd);
  ve = st.SortPairsReverse16(d, ve);
  vf = st.SortPairsReverse16(d, vf);

  v0 = st.SortPairsDistance4(d, v0);
  v1 = st.SortPairsDistance4(d, v1);
  v2 = st.SortPairsDistance4(d, v2);
  v3 = st.SortPairsDistance4(d, v3);
  v4 = st.SortPairsDistance4(d, v4);
  v5 = st.SortPairsDistance4(d, v5);
  v6 = st.SortPairsDistance4(d, v6);
  v7 = st.SortPairsDistance4(d, v7);
  v8 = st.SortPairsDistance4(d, v8);
  v9 = st.SortPairsDistance4(d, v9);
  va = st.SortPairsDistance4(d, va);
  vb = st.SortPairsDistance4(d, vb);
  vc = st.SortPairsDistance4(d, vc);
  vd = st.SortPairsDistance4(d, vd);
  ve = st.SortPairsDistance4(d, ve);
  vf = st.SortPairsDistance4(d, vf);

  v0 = st.SortPairsDistance2(d, v0);
  v1 = st.SortPairsDistance2(d, v1);
  v2 = st.SortPairsDistance2(d, v2);
  v3 = st.SortPairsDistance2(d, v3);
  v4 = st.SortPairsDistance2(d, v4);
  v5 = st.SortPairsDistance2(d, v5);
  v6 = st.SortPairsDistance2(d, v6);
  v7 = st.SortPairsDistance2(d, v7);
  v8 = st.SortPairsDistance2(d, v8);
  v9 = st.SortPairsDistance2(d, v9);
  va = st.SortPairsDistance2(d, va);
  vb = st.SortPairsDistance2(d, vb);
  vc = st.SortPairsDistance2(d, vc);
  vd = st.SortPairsDistance2(d, vd);
  ve = st.SortPairsDistance2(d, ve);
  vf = st.SortPairsDistance2(d, vf);

  v0 = st.SortPairsDistance1(d, v0);
  v1 = st.SortPairsDistance1(d, v1);
  v2 = st.SortPairsDistance1(d, v2);
  v3 = st.SortPairsDistance1(d, v3);
  v4 = st.SortPairsDistance1(d, v4);
  v5 = st.SortPairsDistance1(d, v5);
  v6 = st.SortPairsDistance1(d, v6);
  v7 = st.SortPairsDistance1(d, v7);
  v8 = st.SortPairsDistance1(d, v8);
  v9 = st.SortPairsDistance1(d, v9);
  va = st.SortPairsDistance1(d, va);
  vb = st.SortPairsDistance1(d, vb);
  vc = st.SortPairsDistance1(d, vc);
  vd = st.SortPairsDistance1(d, vd);
  ve = st.SortPairsDistance1(d, ve);
  vf = st.SortPairsDistance1(d, vf);
}

#endif  // !HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD

// Reshapes `buf` into a matrix, sorts columns independently, and then merges
// into a sorted 1D array without transposing.
//
// DEPRECATED, use BaseCase() instead.
template <class Traits, class V>
HWY_INLINE void SortingNetwork(Traits st, size_t cols, V& v0, V& v1, V& v2,
                               V& v3, V& v4, V& v5, V& v6, V& v7, V& v8, V& v9,
                               V& va, V& vb, V& vc, V& vd, V& ve, V& vf) {
  // traits*-inl assume 'full' vectors (but still capped to kMaxCols).
  const CappedTag<typename Traits::LaneType, Constants::kMaxCols> d;

  HWY_DASSERT(cols <= Constants::kMaxCols);

  // The network width depends on the number of keys, not lanes.
  constexpr size_t kLanesPerKey = st.LanesPerKey();
  const size_t keys = cols / kLanesPerKey;
  constexpr size_t kMaxKeys = MaxLanes(d) / kLanesPerKey;

  Sort16(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb, vc, vd, ve, vf);

  // Checking MaxLanes avoids generating HWY_ASSERT code for the unreachable
  // code paths: if MaxLanes < 2, then keys <= cols < 2.
  if (HWY_LIKELY(keys >= 2 && kMaxKeys >= 2)) {
    Merge16x2<kMaxKeys>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                        vc, vd, ve, vf);

    if (HWY_LIKELY(keys >= 4 && kMaxKeys >= 4)) {
      Merge16x4<kMaxKeys>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                          vc, vd, ve, vf);

      if (HWY_LIKELY(keys >= 8 && kMaxKeys >= 8)) {
        Merge16x8<kMaxKeys>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va,
                            vb, vc, vd, ve, vf);

        // Avoids build timeout. Must match #if condition in kMaxCols.
#if !HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD
        if (HWY_LIKELY(keys >= 16 && kMaxKeys >= 16)) {
          Merge16x16<kMaxKeys>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9,
                               va, vb, vc, vd, ve, vf);

          static_assert(Constants::kMaxCols <= 16, "Add more branches");
        }
#endif
      }
    }
  }
}

// As above, but loads from/stores to `buf`. This ensures full vectors are
// aligned, and enables loads/stores without bounds checks.
//
// DEPRECATED, use BaseCase() instead.
template <class Traits, typename T>
HWY_NOINLINE void SortingNetwork(Traits st, T* HWY_RESTRICT buf, size_t cols) {
  // traits*-inl assume 'full' vectors (but still capped to kMaxCols).
  // However, for smaller arrays and sub-maximal `cols` we have overlapping
  // loads where only the lowest `cols` are valid, and we skip Merge16 etc.
  const CappedTag<T, Constants::kMaxCols> d;
  using V = decltype(Zero(d));

  HWY_DASSERT(cols <= Constants::kMaxCols);

  // These are aligned iff cols == Lanes(d). We prefer unaligned/non-constexpr
  // offsets to duplicating this code for every value of cols.
  static_assert(Constants::kMaxRows == 16, "Update loads/stores/args");
  V v0 = LoadU(d, buf + 0x0 * cols);
  V v1 = LoadU(d, buf + 0x1 * cols);
  V v2 = LoadU(d, buf + 0x2 * cols);
  V v3 = LoadU(d, buf + 0x3 * cols);
  V v4 = LoadU(d, buf + 0x4 * cols);
  V v5 = LoadU(d, buf + 0x5 * cols);
  V v6 = LoadU(d, buf + 0x6 * cols);
  V v7 = LoadU(d, buf + 0x7 * cols);
  V v8 = LoadU(d, buf + 0x8 * cols);
  V v9 = LoadU(d, buf + 0x9 * cols);
  V va = LoadU(d, buf + 0xa * cols);
  V vb = LoadU(d, buf + 0xb * cols);
  V vc = LoadU(d, buf + 0xc * cols);
  V vd = LoadU(d, buf + 0xd * cols);
  V ve = LoadU(d, buf + 0xe * cols);
  V vf = LoadU(d, buf + 0xf * cols);

  SortingNetwork(st, cols, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb, vc,
                 vd, ve, vf);

  StoreU(v0, d, buf + 0x0 * cols);
  StoreU(v1, d, buf + 0x1 * cols);
  StoreU(v2, d, buf + 0x2 * cols);
  StoreU(v3, d, buf + 0x3 * cols);
  StoreU(v4, d, buf + 0x4 * cols);
  StoreU(v5, d, buf + 0x5 * cols);
  StoreU(v6, d, buf + 0x6 * cols);
  StoreU(v7, d, buf + 0x7 * cols);
  StoreU(v8, d, buf + 0x8 * cols);
  StoreU(v9, d, buf + 0x9 * cols);
  StoreU(va, d, buf + 0xa * cols);
  StoreU(vb, d, buf + 0xb * cols);
  StoreU(vc, d, buf + 0xc * cols);
  StoreU(vd, d, buf + 0xd * cols);
  StoreU(ve, d, buf + 0xe * cols);
  StoreU(vf, d, buf + 0xf * cols);
}

#else
template <class Base>
struct SharedTraits : public Base {};
#endif  // VQSORT_ENABLED

}  // namespace detail
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_SORT_SORTING_NETWORKS_TOGGLE
