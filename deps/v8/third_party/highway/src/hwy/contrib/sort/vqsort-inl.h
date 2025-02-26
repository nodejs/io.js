// Copyright 2021 Google LLC
// Copyright 2024 Arm Limited and/or its affiliates <open-source-office@arm.com>
// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: BSD-3-Clause
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

// Normal include guard for target-independent parts
#ifndef HIGHWAY_HWY_CONTRIB_SORT_VQSORT_INL_H_
#define HIGHWAY_HWY_CONTRIB_SORT_VQSORT_INL_H_

// unconditional #include so we can use if(VQSORT_PRINT), which unlike #if does
// not interfere with code-folding.
#include <stdio.h>
#include <time.h>  // clock

// IWYU pragma: begin_exports
#include "hwy/base.h"
#include "hwy/contrib/sort/order.h"  // SortAscending
// IWYU pragma: end_exports

#include "hwy/cache_control.h"  // Prefetch
#include "hwy/print.h"          // unconditional, see above.

// If 1, VQSortStatic can be called without including vqsort.h, and we avoid
// any DLLEXPORT. This simplifies integration into other build systems, but
// decreases the security of random seeds.
#ifndef VQSORT_ONLY_STATIC
#define VQSORT_ONLY_STATIC 0
#endif

// Verbosity: 0 for none, 1 for brief per-sort, 2+ for more details.
#ifndef VQSORT_PRINT
#define VQSORT_PRINT 0
#endif

#if !VQSORT_ONLY_STATIC
#include "hwy/contrib/sort/vqsort.h"  // Fill16BytesSecure
#endif

namespace hwy {
namespace detail {

HWY_INLINE void Fill16BytesStatic(void* bytes) {
#if !VQSORT_ONLY_STATIC
  if (Fill16BytesSecure(bytes)) return;
#endif

  uint64_t* words = reinterpret_cast<uint64_t*>(bytes);

  // Static-only, or Fill16BytesSecure failed. Get some entropy from the
  // stack/code location, and the clock() timer.
  uint64_t** seed_stack = &words;
  void (*seed_code)(void*) = &Fill16BytesStatic;
  const uintptr_t bits_stack = reinterpret_cast<uintptr_t>(seed_stack);
  const uintptr_t bits_code = reinterpret_cast<uintptr_t>(seed_code);
  const uint64_t bits_time = static_cast<uint64_t>(clock());
  words[0] = bits_stack ^ bits_time ^ 0xFEDCBA98;  // "Nothing up my sleeve"
  words[1] = bits_code ^ bits_time ^ 0x01234567;   // constants.
}

HWY_INLINE uint64_t* GetGeneratorStateStatic() {
  thread_local uint64_t state[3] = {0};
  // This is a counter; zero indicates not yet initialized.
  if (HWY_UNLIKELY(state[2] == 0)) {
    Fill16BytesStatic(state);
    state[2] = 1;
  }
  return state;
}

}  // namespace detail
}  // namespace hwy

#endif  // HIGHWAY_HWY_CONTRIB_SORT_VQSORT_INL_H_

// Per-target
#if defined(HIGHWAY_HWY_CONTRIB_SORT_VQSORT_TOGGLE) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_SORT_VQSORT_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_VQSORT_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_VQSORT_TOGGLE
#endif

#if VQSORT_PRINT
#include "hwy/print-inl.h"
#endif

#include "hwy/contrib/algo/copy-inl.h"
#include "hwy/contrib/sort/shared-inl.h"
#include "hwy/contrib/sort/sorting_networks-inl.h"
#include "hwy/contrib/sort/traits-inl.h"
#include "hwy/contrib/sort/traits128-inl.h"
// Placeholder for internal instrumentation. Do not remove.
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace detail {

using Constants = hwy::SortConstants;

// Wrapper avoids #if in user code (interferes with code folding)
template <class D>
HWY_INLINE void MaybePrintVector(D d, const char* label, Vec<D> v,
                                 size_t start = 0, size_t max_lanes = 16) {
#if VQSORT_PRINT >= 2  // Print is only defined #if
  Print(d, label, v, start, max_lanes);
#else
  (void)d;
  (void)label;
  (void)v;
  (void)start;
  (void)max_lanes;
#endif
}

// ------------------------------ HeapSort

template <class Traits, typename T>
void SiftDown(Traits st, T* HWY_RESTRICT lanes, const size_t num_lanes,
              size_t start) {
  constexpr size_t N1 = st.LanesPerKey();
  const FixedTag<T, N1> d;

  while (start < num_lanes) {
    const size_t left = 2 * start + N1;
    const size_t right = 2 * start + 2 * N1;
    if (left >= num_lanes) break;
    size_t idx_larger = start;
    const auto key_j = st.SetKey(d, lanes + start);
    if (AllTrue(d, st.Compare(d, key_j, st.SetKey(d, lanes + left)))) {
      idx_larger = left;
    }
    if (right < num_lanes &&
        AllTrue(d, st.Compare(d, st.SetKey(d, lanes + idx_larger),
                              st.SetKey(d, lanes + right)))) {
      idx_larger = right;
    }
    if (idx_larger == start) break;
    st.Swap(lanes + start, lanes + idx_larger);
    start = idx_larger;
  }
}

// Heapsort: O(1) space, O(N*logN) worst-case comparisons.
// Based on LLVM sanitizer_common.h, licensed under Apache-2.0.
template <class Traits, typename T>
void HeapSort(Traits st, T* HWY_RESTRICT lanes, const size_t num_lanes) {
  constexpr size_t N1 = st.LanesPerKey();
  HWY_DASSERT(num_lanes % N1 == 0);
  if (num_lanes == N1) return;

  // Build heap.
  for (size_t i = ((num_lanes - N1) / N1 / 2) * N1; i != (~N1 + 1); i -= N1) {
    SiftDown(st, lanes, num_lanes, i);
  }

  for (size_t i = num_lanes - N1; i != 0; i -= N1) {
// Workaround for -Waggressive-loop-optimizations warning that might be emitted
// by GCC
#if HWY_COMPILER_GCC_ACTUAL
    HWY_DIAGNOSTICS(push)
    HWY_DIAGNOSTICS_OFF(disable : 4756,
                        ignored "-Waggressive-loop-optimizations")
#endif
    // Swap root with last
    st.Swap(lanes + 0, lanes + i);

#if HWY_COMPILER_GCC_ACTUAL
    HWY_DIAGNOSTICS(pop)
#endif

    // Sift down the new root.
    SiftDown(st, lanes, i, 0);
  }
}

template <class Traits, typename T>
void HeapSelect(Traits st, T* HWY_RESTRICT lanes, const size_t num_lanes,
                const size_t k_lanes) {
  constexpr size_t N1 = st.LanesPerKey();
  const size_t k = k_lanes + N1;
  HWY_DASSERT(num_lanes % N1 == 0);
  if (num_lanes == N1) return;

  const FixedTag<T, N1> d;

  // Build heap.
  for (size_t i = ((k - N1) / N1 / 2) * N1; i != (~N1 + 1); i -= N1) {
    SiftDown(st, lanes, k, i);
  }

  for (size_t i = k; i <= num_lanes - N1; i += N1) {
    if (AllTrue(d, st.Compare(d, st.SetKey(d, lanes + i),
                              st.SetKey(d, lanes + 0)))) {
// Workaround for -Waggressive-loop-optimizations warning that might be emitted
// by GCC
#if HWY_COMPILER_GCC_ACTUAL
      HWY_DIAGNOSTICS(push)
      HWY_DIAGNOSTICS_OFF(disable : 4756,
                          ignored "-Waggressive-loop-optimizations")
#endif

      // Swap root with last
      st.Swap(lanes + 0, lanes + i);

#if HWY_COMPILER_GCC_ACTUAL
      HWY_DIAGNOSTICS(pop)
#endif

      // Sift down the new root.
      SiftDown(st, lanes, k, 0);
    }
  }

  st.Swap(lanes + 0, lanes + k - N1);
}

template <class Traits, typename T>
void HeapPartialSort(Traits st, T* HWY_RESTRICT lanes, const size_t num_lanes,
                     const size_t k_lanes) {
  HeapSelect(st, lanes, num_lanes, k_lanes);
  HeapSort(st, lanes, k_lanes);
}

#if VQSORT_ENABLED || HWY_IDE

// ------------------------------ BaseCase

// Special cases where `num_lanes` is in the specified range (inclusive).
template <class Traits, typename T>
HWY_INLINE void Sort2To2(Traits st, T* HWY_RESTRICT keys, size_t num_lanes,
                         T* HWY_RESTRICT /* buf */) {
  constexpr size_t kLPK = st.LanesPerKey();
  const size_t num_keys = num_lanes / kLPK;
  HWY_DASSERT(num_keys == 2);
  HWY_ASSUME(num_keys == 2);

  // One key per vector, required to avoid reading past the end of `keys`.
  const CappedTag<T, kLPK> d;
  using V = Vec<decltype(d)>;

  V v0 = LoadU(d, keys + 0x0 * kLPK);
  V v1 = LoadU(d, keys + 0x1 * kLPK);

  Sort2(d, st, v0, v1);

  StoreU(v0, d, keys + 0x0 * kLPK);
  StoreU(v1, d, keys + 0x1 * kLPK);
}

template <class Traits, typename T>
HWY_INLINE void Sort3To4(Traits st, T* HWY_RESTRICT keys, size_t num_lanes,
                         T* HWY_RESTRICT buf) {
  constexpr size_t kLPK = st.LanesPerKey();
  const size_t num_keys = num_lanes / kLPK;
  HWY_DASSERT(3 <= num_keys && num_keys <= 4);
  HWY_ASSUME(num_keys >= 3);
  HWY_ASSUME(num_keys <= 4);  // reduces branches

  // One key per vector, required to avoid reading past the end of `keys`.
  const CappedTag<T, kLPK> d;
  using V = Vec<decltype(d)>;

  // If num_keys == 3, initialize padding for the last sorting network element
  // so that it does not influence the other elements.
  Store(st.LastValue(d), d, buf);

  // Points to a valid key, or padding. This avoids special-casing
  // HWY_MEM_OPS_MIGHT_FAULT because there is only a single key per vector.
  T* in_out3 = num_keys == 3 ? buf : keys + 0x3 * kLPK;

  V v0 = LoadU(d, keys + 0x0 * kLPK);
  V v1 = LoadU(d, keys + 0x1 * kLPK);
  V v2 = LoadU(d, keys + 0x2 * kLPK);
  V v3 = LoadU(d, in_out3);

  Sort4(d, st, v0, v1, v2, v3);

  StoreU(v0, d, keys + 0x0 * kLPK);
  StoreU(v1, d, keys + 0x1 * kLPK);
  StoreU(v2, d, keys + 0x2 * kLPK);
  StoreU(v3, d, in_out3);
}

#if HWY_MEM_OPS_MIGHT_FAULT

template <size_t kRows, size_t kLanesPerRow, class D, class Traits,
          typename T = TFromD<D>>
HWY_INLINE void CopyHalfToPaddedBuf(D d, Traits st, T* HWY_RESTRICT keys,
                                    size_t num_lanes, T* HWY_RESTRICT buf) {
  constexpr size_t kMinLanes = kRows / 2 * kLanesPerRow;
  // Must cap for correctness: we will load up to the last valid lane, so
  // Lanes(dmax) must not exceed `num_lanes` (known to be at least kMinLanes).
  const CappedTag<T, kMinLanes> dmax;
  const size_t Nmax = Lanes(dmax);
  HWY_DASSERT(Nmax < num_lanes);
  HWY_ASSUME(Nmax <= kMinLanes);

  // Fill with padding - last in sort order, not copied to keys.
  const Vec<decltype(dmax)> kPadding = st.LastValue(dmax);

  // Rounding down allows aligned stores, which are typically faster.
  size_t i = num_lanes & ~(Nmax - 1);
  HWY_ASSUME(i != 0);  // because Nmax <= num_lanes; avoids branch
  do {
    Store(kPadding, dmax, buf + i);
    i += Nmax;
    // Initialize enough for the last vector even if Nmax > kLanesPerRow.
  } while (i < (kRows - 1) * kLanesPerRow + Lanes(d));

  // Ensure buf contains all we will read, and perhaps more before.
  ptrdiff_t end = static_cast<ptrdiff_t>(num_lanes);
  do {
    end -= static_cast<ptrdiff_t>(Nmax);
    StoreU(LoadU(dmax, keys + end), dmax, buf + end);
  } while (end > static_cast<ptrdiff_t>(kRows / 2 * kLanesPerRow));
}

#endif  // HWY_MEM_OPS_MIGHT_FAULT

template <size_t kKeysPerRow, class Traits, typename T>
HWY_NOINLINE void Sort8Rows(Traits st, T* HWY_RESTRICT keys, size_t num_lanes,
                            T* HWY_RESTRICT buf) {
  // kKeysPerRow <= 4 because 8 64-bit keys implies 512-bit vectors, which
  // are likely slower than 16x4, so 8x4 is the largest we handle here.
  static_assert(kKeysPerRow <= 4, "");

  constexpr size_t kLPK = st.LanesPerKey();

  // We reshape the 1D keys into kRows x kKeysPerRow.
  constexpr size_t kRows = 8;
  constexpr size_t kLanesPerRow = kKeysPerRow * kLPK;
  constexpr size_t kMinLanes = kRows / 2 * kLanesPerRow;
  HWY_DASSERT(kMinLanes < num_lanes && num_lanes <= kRows * kLanesPerRow);

  const CappedTag<T, kLanesPerRow> d;
  using V = Vec<decltype(d)>;
  V v4, v5, v6, v7;

  // At least half the kRows are valid, otherwise a different function would
  // have been called to handle this num_lanes.
  V v0 = LoadU(d, keys + 0x0 * kLanesPerRow);
  V v1 = LoadU(d, keys + 0x1 * kLanesPerRow);
  V v2 = LoadU(d, keys + 0x2 * kLanesPerRow);
  V v3 = LoadU(d, keys + 0x3 * kLanesPerRow);
#if HWY_MEM_OPS_MIGHT_FAULT
  CopyHalfToPaddedBuf<kRows, kLanesPerRow>(d, st, keys, num_lanes, buf);
  v4 = LoadU(d, buf + 0x4 * kLanesPerRow);
  v5 = LoadU(d, buf + 0x5 * kLanesPerRow);
  v6 = LoadU(d, buf + 0x6 * kLanesPerRow);
  v7 = LoadU(d, buf + 0x7 * kLanesPerRow);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
#if !HWY_MEM_OPS_MIGHT_FAULT || HWY_IDE
  (void)buf;
  const V vnum_lanes = Set(d, ConvertScalarTo<T>(num_lanes));
  // First offset where not all vector are guaranteed valid.
  const V kIota = Iota(d, static_cast<T>(kMinLanes));
  const V k1 = Set(d, static_cast<T>(kLanesPerRow));
  const V k2 = Add(k1, k1);

  using M = Mask<decltype(d)>;
  const M m4 = Gt(vnum_lanes, kIota);
  const M m5 = Gt(vnum_lanes, Add(kIota, k1));
  const M m6 = Gt(vnum_lanes, Add(kIota, k2));
  const M m7 = Gt(vnum_lanes, Add(kIota, Add(k2, k1)));

  const V kPadding = st.LastValue(d);  // Not copied to keys.
  v4 = MaskedLoadOr(kPadding, m4, d, keys + 0x4 * kLanesPerRow);
  v5 = MaskedLoadOr(kPadding, m5, d, keys + 0x5 * kLanesPerRow);
  v6 = MaskedLoadOr(kPadding, m6, d, keys + 0x6 * kLanesPerRow);
  v7 = MaskedLoadOr(kPadding, m7, d, keys + 0x7 * kLanesPerRow);
#endif  // !HWY_MEM_OPS_MIGHT_FAULT

  Sort8(d, st, v0, v1, v2, v3, v4, v5, v6, v7);

  // Merge8x2 is a no-op if kKeysPerRow < 2 etc.
  Merge8x2<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7);
  Merge8x4<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7);

  StoreU(v0, d, keys + 0x0 * kLanesPerRow);
  StoreU(v1, d, keys + 0x1 * kLanesPerRow);
  StoreU(v2, d, keys + 0x2 * kLanesPerRow);
  StoreU(v3, d, keys + 0x3 * kLanesPerRow);

#if HWY_MEM_OPS_MIGHT_FAULT
  // Store remaining vectors into buf and safely copy them into keys.
  StoreU(v4, d, buf + 0x4 * kLanesPerRow);
  StoreU(v5, d, buf + 0x5 * kLanesPerRow);
  StoreU(v6, d, buf + 0x6 * kLanesPerRow);
  StoreU(v7, d, buf + 0x7 * kLanesPerRow);

  const ScalableTag<T> dmax;
  const size_t Nmax = Lanes(dmax);

  // The first half of vectors have already been stored unconditionally into
  // `keys`, so we do not copy them.
  size_t i = kMinLanes;
  HWY_UNROLL(1)
  for (; i + Nmax <= num_lanes; i += Nmax) {
    StoreU(LoadU(dmax, buf + i), dmax, keys + i);
  }

  // Last iteration: copy partial vector
  const size_t remaining = num_lanes - i;
  HWY_ASSUME(remaining < 256);  // helps FirstN
  SafeCopyN(remaining, dmax, buf + i, keys + i);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
#if !HWY_MEM_OPS_MIGHT_FAULT || HWY_IDE
  BlendedStore(v4, m4, d, keys + 0x4 * kLanesPerRow);
  BlendedStore(v5, m5, d, keys + 0x5 * kLanesPerRow);
  BlendedStore(v6, m6, d, keys + 0x6 * kLanesPerRow);
  BlendedStore(v7, m7, d, keys + 0x7 * kLanesPerRow);
#endif  // !HWY_MEM_OPS_MIGHT_FAULT
}

template <size_t kKeysPerRow, class Traits, typename T>
HWY_NOINLINE void Sort16Rows(Traits st, T* HWY_RESTRICT keys, size_t num_lanes,
                             T* HWY_RESTRICT buf) {
  static_assert(kKeysPerRow <= SortConstants::kMaxCols, "");

  constexpr size_t kLPK = st.LanesPerKey();

  // We reshape the 1D keys into kRows x kKeysPerRow.
  constexpr size_t kRows = 16;
  constexpr size_t kLanesPerRow = kKeysPerRow * kLPK;
  constexpr size_t kMinLanes = kRows / 2 * kLanesPerRow;
  HWY_DASSERT(kMinLanes < num_lanes && num_lanes <= kRows * kLanesPerRow);

  const CappedTag<T, kLanesPerRow> d;
  using V = Vec<decltype(d)>;
  V v8, v9, va, vb, vc, vd, ve, vf;

  // At least half the kRows are valid, otherwise a different function would
  // have been called to handle this num_lanes.
  V v0 = LoadU(d, keys + 0x0 * kLanesPerRow);
  V v1 = LoadU(d, keys + 0x1 * kLanesPerRow);
  V v2 = LoadU(d, keys + 0x2 * kLanesPerRow);
  V v3 = LoadU(d, keys + 0x3 * kLanesPerRow);
  V v4 = LoadU(d, keys + 0x4 * kLanesPerRow);
  V v5 = LoadU(d, keys + 0x5 * kLanesPerRow);
  V v6 = LoadU(d, keys + 0x6 * kLanesPerRow);
  V v7 = LoadU(d, keys + 0x7 * kLanesPerRow);
#if HWY_MEM_OPS_MIGHT_FAULT
  CopyHalfToPaddedBuf<kRows, kLanesPerRow>(d, st, keys, num_lanes, buf);
  v8 = LoadU(d, buf + 0x8 * kLanesPerRow);
  v9 = LoadU(d, buf + 0x9 * kLanesPerRow);
  va = LoadU(d, buf + 0xa * kLanesPerRow);
  vb = LoadU(d, buf + 0xb * kLanesPerRow);
  vc = LoadU(d, buf + 0xc * kLanesPerRow);
  vd = LoadU(d, buf + 0xd * kLanesPerRow);
  ve = LoadU(d, buf + 0xe * kLanesPerRow);
  vf = LoadU(d, buf + 0xf * kLanesPerRow);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
#if !HWY_MEM_OPS_MIGHT_FAULT || HWY_IDE
  (void)buf;
  const V vnum_lanes = Set(d, ConvertScalarTo<T>(num_lanes));
  // First offset where not all vector are guaranteed valid.
  const V kIota = Iota(d, static_cast<T>(kMinLanes));
  const V k1 = Set(d, static_cast<T>(kLanesPerRow));
  const V k2 = Add(k1, k1);
  const V k4 = Add(k2, k2);
  const V k8 = Add(k4, k4);

  using M = Mask<decltype(d)>;
  const M m8 = Gt(vnum_lanes, kIota);
  const M m9 = Gt(vnum_lanes, Add(kIota, k1));
  const M ma = Gt(vnum_lanes, Add(kIota, k2));
  const M mb = Gt(vnum_lanes, Add(kIota, Sub(k4, k1)));
  const M mc = Gt(vnum_lanes, Add(kIota, k4));
  const M md = Gt(vnum_lanes, Add(kIota, Add(k4, k1)));
  const M me = Gt(vnum_lanes, Add(kIota, Add(k4, k2)));
  const M mf = Gt(vnum_lanes, Add(kIota, Sub(k8, k1)));

  const V kPadding = st.LastValue(d);  // Not copied to keys.
  v8 = MaskedLoadOr(kPadding, m8, d, keys + 0x8 * kLanesPerRow);
  v9 = MaskedLoadOr(kPadding, m9, d, keys + 0x9 * kLanesPerRow);
  va = MaskedLoadOr(kPadding, ma, d, keys + 0xa * kLanesPerRow);
  vb = MaskedLoadOr(kPadding, mb, d, keys + 0xb * kLanesPerRow);
  vc = MaskedLoadOr(kPadding, mc, d, keys + 0xc * kLanesPerRow);
  vd = MaskedLoadOr(kPadding, md, d, keys + 0xd * kLanesPerRow);
  ve = MaskedLoadOr(kPadding, me, d, keys + 0xe * kLanesPerRow);
  vf = MaskedLoadOr(kPadding, mf, d, keys + 0xf * kLanesPerRow);
#endif  // !HWY_MEM_OPS_MIGHT_FAULT

  Sort16(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb, vc, vd, ve, vf);

  // Merge16x4 is a no-op if kKeysPerRow < 4 etc.
  Merge16x2<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                         vc, vd, ve, vf);
  Merge16x4<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                         vc, vd, ve, vf);
  Merge16x8<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                         vc, vd, ve, vf);
#if !HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD
  Merge16x16<kKeysPerRow>(d, st, v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, va, vb,
                          vc, vd, ve, vf);
#endif

  StoreU(v0, d, keys + 0x0 * kLanesPerRow);
  StoreU(v1, d, keys + 0x1 * kLanesPerRow);
  StoreU(v2, d, keys + 0x2 * kLanesPerRow);
  StoreU(v3, d, keys + 0x3 * kLanesPerRow);
  StoreU(v4, d, keys + 0x4 * kLanesPerRow);
  StoreU(v5, d, keys + 0x5 * kLanesPerRow);
  StoreU(v6, d, keys + 0x6 * kLanesPerRow);
  StoreU(v7, d, keys + 0x7 * kLanesPerRow);

#if HWY_MEM_OPS_MIGHT_FAULT
  // Store remaining vectors into buf and safely copy them into keys.
  StoreU(v8, d, buf + 0x8 * kLanesPerRow);
  StoreU(v9, d, buf + 0x9 * kLanesPerRow);
  StoreU(va, d, buf + 0xa * kLanesPerRow);
  StoreU(vb, d, buf + 0xb * kLanesPerRow);
  StoreU(vc, d, buf + 0xc * kLanesPerRow);
  StoreU(vd, d, buf + 0xd * kLanesPerRow);
  StoreU(ve, d, buf + 0xe * kLanesPerRow);
  StoreU(vf, d, buf + 0xf * kLanesPerRow);

  const ScalableTag<T> dmax;
  const size_t Nmax = Lanes(dmax);

  // The first half of vectors have already been stored unconditionally into
  // `keys`, so we do not copy them.
  size_t i = kMinLanes;
  HWY_UNROLL(1)
  for (; i + Nmax <= num_lanes; i += Nmax) {
    StoreU(LoadU(dmax, buf + i), dmax, keys + i);
  }

  // Last iteration: copy partial vector
  const size_t remaining = num_lanes - i;
  HWY_ASSUME(remaining < 256);  // helps FirstN
  SafeCopyN(remaining, dmax, buf + i, keys + i);
#endif  // HWY_MEM_OPS_MIGHT_FAULT
#if !HWY_MEM_OPS_MIGHT_FAULT || HWY_IDE
  BlendedStore(v8, m8, d, keys + 0x8 * kLanesPerRow);
  BlendedStore(v9, m9, d, keys + 0x9 * kLanesPerRow);
  BlendedStore(va, ma, d, keys + 0xa * kLanesPerRow);
  BlendedStore(vb, mb, d, keys + 0xb * kLanesPerRow);
  BlendedStore(vc, mc, d, keys + 0xc * kLanesPerRow);
  BlendedStore(vd, md, d, keys + 0xd * kLanesPerRow);
  BlendedStore(ve, me, d, keys + 0xe * kLanesPerRow);
  BlendedStore(vf, mf, d, keys + 0xf * kLanesPerRow);
#endif  // !HWY_MEM_OPS_MIGHT_FAULT
}

// Sorts `keys` within the range [0, num_lanes) via sorting network.
// Reshapes into a matrix, sorts columns independently, and then merges
// into a sorted 1D array without transposing.
//
// `TraitsKV` is SharedTraits<Traits*<Order*>>. This abstraction layer bridges
//   differences in sort order and single-lane vs 128-bit keys. For key-value
//   types, items with the same key are not equivalent. Our sorting network
//   does not preserve order, thus we prevent mixing padding into the items by
//   comparing all the item bits, including the value (see *ForSortingNetwork).
//
// See M. Blacher's thesis: https://github.com/mark-blacher/masterthesis
template <class D, class TraitsKV, typename T>
HWY_NOINLINE void BaseCase(D d, TraitsKV, T* HWY_RESTRICT keys,
                           size_t num_lanes, T* buf) {
  using Traits = typename TraitsKV::SharedTraitsForSortingNetwork;
  Traits st;
  constexpr size_t kLPK = st.LanesPerKey();
  HWY_DASSERT(num_lanes <= Constants::BaseCaseNumLanes<kLPK>(Lanes(d)));
  const size_t num_keys = num_lanes / kLPK;

  // Can be zero when called through HandleSpecialCases, but also 1 (in which
  // case the array is already sorted). Also ensures num_lanes - 1 != 0.
  if (HWY_UNLIKELY(num_keys <= 1)) return;

  const size_t ceil_log2 =
      32 - Num0BitsAboveMS1Bit_Nonzero32(static_cast<uint32_t>(num_keys - 1));

  // Checking kMaxKeysPerVector avoids generating unreachable codepaths.
  constexpr size_t kMaxKeysPerVector = MaxLanes(d) / kLPK;

  using FuncPtr = decltype(&Sort2To2<Traits, T>);
  const FuncPtr funcs[9] = {
      /* <= 1 */ nullptr,  // We ensured num_keys > 1.
      /* <= 2 */ &Sort2To2<Traits, T>,
      /* <= 4 */ &Sort3To4<Traits, T>,
      /* <= 8 */ &Sort8Rows<1, Traits, T>,  // 1 key per row
      /* <= 16 */ kMaxKeysPerVector >= 2 ? &Sort8Rows<2, Traits, T> : nullptr,
      /* <= 32 */ kMaxKeysPerVector >= 4 ? &Sort8Rows<4, Traits, T> : nullptr,
      /* <= 64 */ kMaxKeysPerVector >= 4 ? &Sort16Rows<4, Traits, T> : nullptr,
      /* <= 128 */ kMaxKeysPerVector >= 8 ? &Sort16Rows<8, Traits, T> : nullptr,
#if !HWY_COMPILER_MSVC && !HWY_IS_DEBUG_BUILD
      /* <= 256 */ kMaxKeysPerVector >= 16 ? &Sort16Rows<16, Traits, T>
                                           : nullptr,
#endif
  };
  funcs[ceil_log2](st, keys, num_lanes, buf);
}

// ------------------------------ Partition

// Partitions O(1) of the *rightmost* keys, at least `N`, until a multiple of
// kUnroll*N remains, or all keys if there are too few for that.
//
// Returns how many remain to partition at the *start* of `keys`, sets `bufL` to
// the number of keys for the left partition written to `buf`, and `writeR` to
// the start of the finished right partition at the end of `keys`.
template <class D, class Traits, class T>
HWY_INLINE size_t PartitionRightmost(D d, Traits st, T* const keys,
                                     const size_t num, const Vec<D> pivot,
                                     size_t& bufL, size_t& writeR,
                                     T* HWY_RESTRICT buf) {
  const size_t N = Lanes(d);
  HWY_DASSERT(num > 2 * N);  // BaseCase handles smaller arrays

  constexpr size_t kUnroll = Constants::kPartitionUnroll;
  size_t num_here;  // how many to process here
  size_t num_main;  // how many for main Partition loop (return value)
  {
    // The main Partition loop increments by kUnroll * N, so at least handle
    // the remainders here.
    const size_t remainder = num & (kUnroll * N - 1);
    // Ensure we handle at least one vector to prevent overruns (see below), but
    // still leave a multiple of kUnroll * N.
    const size_t min = remainder + (remainder < N ? kUnroll * N : 0);
    // Do not exceed the input size.
    num_here = HWY_MIN(min, num);
    num_main = num - num_here;
    // Before the main Partition loop we load two blocks; if not enough left for
    // that, handle everything here.
    if (num_main < 2 * kUnroll * N) {
      num_here = num;
      num_main = 0;
    }
  }

  // Note that `StoreLeftRight` uses `CompressBlendedStore`, which may load and
  // store a whole vector starting at `writeR`, and thus overrun `keys`. To
  // prevent this, we partition at least `N` of the rightmost `keys` so that
  // `StoreLeftRight` will be able to safely blend into them.
  HWY_DASSERT(num_here >= N);

  // We cannot use `CompressBlendedStore` for the same reason, so we instead
  // write the right-of-partition keys into a buffer in ascending order.
  // `min` may be up to (kUnroll + 1) * N, hence `num_here` could be as much as
  // (3 * kUnroll + 1) * N, and they might all fall on one side of the pivot.
  const size_t max_buf = (3 * kUnroll + 1) * N;
  HWY_DASSERT(num_here <= max_buf);

  const T* pReadR = keys + num;  // pre-decremented by N

  bufL = 0;
  size_t bufR = max_buf;  // starting position, not the actual count.

  size_t i = 0;
  // For whole vectors, we can LoadU.
  for (; i <= num_here - N; i += N) {
    pReadR -= N;
    HWY_DASSERT(pReadR >= keys);
    const Vec<D> v = LoadU(d, pReadR);

    const Mask<D> comp = st.Compare(d, pivot, v);
    const size_t numL = CompressStore(v, Not(comp), d, buf + bufL);
    bufL += numL;
    (void)CompressStore(v, comp, d, buf + bufR);
    bufR += (N - numL);
  }

  // Last iteration: avoid reading past the end.
  const size_t remaining = num_here - i;
  if (HWY_LIKELY(remaining != 0)) {
    const Mask<D> mask = FirstN(d, remaining);
    pReadR -= remaining;
    HWY_DASSERT(pReadR >= keys);
    const Vec<D> v = LoadN(d, pReadR, remaining);

    const Mask<D> comp = st.Compare(d, pivot, v);
    const size_t numL = CompressStore(v, AndNot(comp, mask), d, buf + bufL);
    bufL += numL;
    (void)CompressStore(v, comp, d, buf + bufR);
    bufR += (remaining - numL);
  }

  const size_t numWrittenR = bufR - max_buf;
  // MSan seems not to understand CompressStore.
  detail::MaybeUnpoison(buf, bufL);
  detail::MaybeUnpoison(buf + max_buf, numWrittenR);

  // Overwrite already-read end of keys with bufR.
  writeR = num - numWrittenR;
  hwy::CopyBytes(buf + max_buf, keys + writeR, numWrittenR * sizeof(T));
  // Ensure we finished reading/writing all we wanted
  HWY_DASSERT(pReadR == keys + num_main);
  HWY_DASSERT(bufL + numWrittenR == num_here);
  return num_main;
}

// Note: we could track the OrXor of v and pivot to see if the entire left
// partition is equal, but that happens rarely and thus is a net loss.
template <class D, class Traits, typename T>
HWY_INLINE void StoreLeftRight(D d, Traits st, const Vec<D> v,
                               const Vec<D> pivot, T* HWY_RESTRICT keys,
                               size_t& writeL, size_t& remaining) {
  const size_t N = Lanes(d);

  const Mask<D> comp = st.Compare(d, pivot, v);

  // Otherwise StoreU/CompressStore overwrites right keys.
  HWY_DASSERT(remaining >= 2 * N);

  remaining -= N;
  if (hwy::HWY_NAMESPACE::CompressIsPartition<T>::value ||
      (HWY_MAX_BYTES == 16 && st.Is128())) {
    // Non-native Compress (e.g. AVX2): we are able to partition a vector using
    // a single Compress+two StoreU instead of two Compress[Blended]Store. The
    // latter are more expensive. Because we store entire vectors, the contents
    // between the updated writeL and writeR are ignored and will be overwritten
    // by subsequent calls. This works because writeL and writeR are at least
    // two vectors apart.
    const Vec<D> lr = st.CompressKeys(v, comp);
    const size_t num_left = N - CountTrue(d, comp);
    StoreU(lr, d, keys + writeL);
    // Now write the right-side elements (if any), such that the previous writeR
    // is one past the end of the newly written right elements, then advance.
    StoreU(lr, d, keys + remaining + writeL);
    writeL += num_left;
  } else {
    // Native Compress[Store] (e.g. AVX3), which only keep the left or right
    // side, not both, hence we require two calls.
    const size_t num_left = CompressStore(v, Not(comp), d, keys + writeL);
    writeL += num_left;

    (void)CompressBlendedStore(v, comp, d, keys + remaining + writeL);
  }
}

template <class D, class Traits, typename T>
HWY_INLINE void StoreLeftRight4(D d, Traits st, const Vec<D> v0,
                                const Vec<D> v1, const Vec<D> v2,
                                const Vec<D> v3, const Vec<D> pivot,
                                T* HWY_RESTRICT keys, size_t& writeL,
                                size_t& remaining) {
  StoreLeftRight(d, st, v0, pivot, keys, writeL, remaining);
  StoreLeftRight(d, st, v1, pivot, keys, writeL, remaining);
  StoreLeftRight(d, st, v2, pivot, keys, writeL, remaining);
  StoreLeftRight(d, st, v3, pivot, keys, writeL, remaining);
}

// For the last two vectors, we cannot use StoreLeftRight because it might
// overwrite prior right-side keys. Instead write R and append L into `buf`.
template <class D, class Traits, typename T>
HWY_INLINE void StoreRightAndBuf(D d, Traits st, const Vec<D> v,
                                 const Vec<D> pivot, T* HWY_RESTRICT keys,
                                 size_t& writeR, T* HWY_RESTRICT buf,
                                 size_t& bufL) {
  const size_t N = Lanes(d);
  const Mask<D> comp = st.Compare(d, pivot, v);
  const size_t numL = CompressStore(v, Not(comp), d, buf + bufL);
  const size_t numR = N - numL;
  bufL += numL;
  writeR -= numR;
  StoreN(Compress(v, comp), d, keys + writeR, numR);
}

// Moves "<= pivot" keys to the front, and others to the back. pivot is
// broadcasted. Returns the index of the first key in the right partition.
//
// Time-critical, but aligned loads do not seem to be worthwhile because we
// are not bottlenecked by load ports.
template <class D, class Traits, typename T>
HWY_INLINE size_t Partition(D d, Traits st, T* const keys, const size_t num,
                            const Vec<D> pivot, T* HWY_RESTRICT buf) {
  using V = decltype(Zero(d));
  const size_t N = Lanes(d);

  size_t bufL, writeR;
  const size_t num_main =
      PartitionRightmost(d, st, keys, num, pivot, bufL, writeR, buf);
  HWY_DASSERT(num_main <= num && writeR <= num);
  HWY_DASSERT(bufL <= Constants::PartitionBufNum(N));
  HWY_DASSERT(num_main + bufL == writeR);

  if (VQSORT_PRINT >= 3) {
    fprintf(stderr, "  num_main %zu bufL %zu writeR %zu\n", num_main, bufL,
            writeR);
  }

  constexpr size_t kUnroll = Constants::kPartitionUnroll;

  // Partition splits the vector into 3 sections, left to right: Elements
  // smaller or equal to the pivot, unpartitioned elements and elements larger
  // than the pivot. To write elements unconditionally on the loop body without
  // overwriting existing data, we maintain two regions of the loop where all
  // elements have been copied elsewhere (e.g. vector registers.). I call these
  // bufferL and bufferR, for left and right respectively.
  //
  // These regions are tracked by the indices (writeL, writeR, left, right) as
  // presented in the diagram below.
  //
  //              writeL                                  writeR
  //               \/                                       \/
  //  |  <= pivot   | bufferL |   unpartitioned   | bufferR |   > pivot   |
  //                          \/                  \/                      \/
  //                         readL               readR                   num
  //
  // In the main loop body below we choose a side, load some elements out of the
  // vector and move either `readL` or `readR`. Next we call into StoreLeftRight
  // to partition the data, and the partitioned elements will be written either
  // to writeR or writeL and the corresponding index will be moved accordingly.
  //
  // Note that writeR is not explicitly tracked as an optimization for platforms
  // with conditional operations. Instead we track writeL and the number of
  // not yet written elements (`remaining`). From the diagram above we can see
  // that:
  //    writeR - writeL = remaining => writeR = remaining + writeL
  //
  // Tracking `remaining` is advantageous because each iteration reduces the
  // number of unpartitioned elements by a fixed amount, so we can compute
  // `remaining` without data dependencies.
  size_t writeL = 0;
  size_t remaining = writeR - writeL;

  const T* readL = keys;
  const T* readR = keys + num_main;
  // Cannot load if there were fewer than 2 * kUnroll * N.
  if (HWY_LIKELY(num_main != 0)) {
    HWY_DASSERT(num_main >= 2 * kUnroll * N);
    HWY_DASSERT((num_main & (kUnroll * N - 1)) == 0);

    // Make space for writing in-place by reading from readL/readR.
    const V vL0 = LoadU(d, readL + 0 * N);
    const V vL1 = LoadU(d, readL + 1 * N);
    const V vL2 = LoadU(d, readL + 2 * N);
    const V vL3 = LoadU(d, readL + 3 * N);
    readL += kUnroll * N;
    readR -= kUnroll * N;
    const V vR0 = LoadU(d, readR + 0 * N);
    const V vR1 = LoadU(d, readR + 1 * N);
    const V vR2 = LoadU(d, readR + 2 * N);
    const V vR3 = LoadU(d, readR + 3 * N);

    // readL/readR changed above, so check again before the loop.
    while (readL != readR) {
      V v0, v1, v2, v3;

      // Data-dependent but branching is faster than forcing branch-free.
      const size_t capacityL =
          static_cast<size_t>((readL - keys) - static_cast<ptrdiff_t>(writeL));
      HWY_DASSERT(capacityL <= num_main);  // >= 0
      // Load data from the end of the vector with less data (front or back).
      // The next paragraphs explain how this works.
      //
      // let block_size = (kUnroll * N)
      // On the loop prelude we load block_size elements from the front of the
      // vector and an additional block_size elements from the back. On each
      // iteration k elements are written to the front of the vector and
      // (block_size - k) to the back.
      //
      // This creates a loop invariant where the capacity on the front
      // (capacityL) and on the back (capacityR) always add to 2 * block_size.
      // In other words:
      //    capacityL + capacityR = 2 * block_size
      //    capacityR = 2 * block_size - capacityL
      //
      // This means that:
      //    capacityL > capacityR <=>
      //    capacityL > 2 * block_size - capacityL <=>
      //    2 * capacityL > 2 * block_size <=>
      //    capacityL > block_size
      if (capacityL > kUnroll * N) {  // equivalent to capacityL > capacityR.
        readR -= kUnroll * N;
        v0 = LoadU(d, readR + 0 * N);
        v1 = LoadU(d, readR + 1 * N);
        v2 = LoadU(d, readR + 2 * N);
        v3 = LoadU(d, readR + 3 * N);
        hwy::Prefetch(readR - 3 * kUnroll * N);
      } else {
        v0 = LoadU(d, readL + 0 * N);
        v1 = LoadU(d, readL + 1 * N);
        v2 = LoadU(d, readL + 2 * N);
        v3 = LoadU(d, readL + 3 * N);
        readL += kUnroll * N;
        hwy::Prefetch(readL + 3 * kUnroll * N);
      }

      StoreLeftRight4(d, st, v0, v1, v2, v3, pivot, keys, writeL, remaining);
    }

    // Now finish writing the saved vectors to the middle.
    StoreLeftRight4(d, st, vL0, vL1, vL2, vL3, pivot, keys, writeL, remaining);

    StoreLeftRight(d, st, vR0, pivot, keys, writeL, remaining);
    StoreLeftRight(d, st, vR1, pivot, keys, writeL, remaining);

    // Switch back to updating writeR for clarity. The middle is missing vR2/3
    // and what is in the buffer.
    HWY_DASSERT(remaining == bufL + 2 * N);
    writeR = writeL + remaining;
    // Switch to StoreRightAndBuf for the last two vectors because
    // StoreLeftRight may overwrite prior keys.
    StoreRightAndBuf(d, st, vR2, pivot, keys, writeR, buf, bufL);
    StoreRightAndBuf(d, st, vR3, pivot, keys, writeR, buf, bufL);
    HWY_DASSERT(writeR <= num);  // >= 0
    HWY_DASSERT(bufL <= Constants::PartitionBufNum(N));
  }

  // We have partitioned [0, num) into [0, writeL) and [writeR, num).
  // Now insert left keys from `buf` to empty space starting at writeL.
  HWY_DASSERT(writeL + bufL == writeR);
  CopyBytes(buf, keys + writeL, bufL * sizeof(T));

  return writeL + bufL;
}

// Returns true and partitions if [keys, keys + num) contains only {valueL,
// valueR}. Otherwise, sets third to the first differing value; keys may have
// been reordered and a regular Partition is still necessary.
// Called from two locations, hence NOINLINE.
template <class D, class Traits, typename T>
HWY_NOINLINE bool MaybePartitionTwoValue(D d, Traits st, T* HWY_RESTRICT keys,
                                         size_t num, const Vec<D> valueL,
                                         const Vec<D> valueR, Vec<D>& third,
                                         T* HWY_RESTRICT buf) {
  const size_t N = Lanes(d);
  // No guarantee that num >= N because this is called for subarrays!

  size_t i = 0;
  size_t writeL = 0;

  // As long as all lanes are equal to L or R, we can overwrite with valueL.
  // This is faster than first counting, then backtracking to fill L and R.
  if (num >= N) {
    for (; i <= num - N; i += N) {
      const Vec<D> v = LoadU(d, keys + i);
      // It is not clear how to apply OrXor here - that can check if *both*
      // comparisons are true, but here we want *either*. Comparing the unsigned
      // min of differences to zero works, but is expensive for u64 prior to
      // AVX3.
      const Mask<D> eqL = st.EqualKeys(d, v, valueL);
      const Mask<D> eqR = st.EqualKeys(d, v, valueR);
      // At least one other value present; will require a regular partition.
      // On AVX-512, Or + AllTrue are folded into a single kortest if we are
      // careful with the FindKnownFirstTrue argument, see below.
      if (HWY_UNLIKELY(!AllTrue(d, Or(eqL, eqR)))) {
        // If we repeat Or(eqL, eqR) here, the compiler will hoist it into the
        // loop, which is a pessimization because this if-true branch is cold.
        // We can defeat this via Not(Xor), which is equivalent because eqL and
        // eqR cannot be true at the same time. Can we elide the additional Not?
        // FindFirstFalse instructions are generally unavailable, but we can
        // fuse Not and Xor/Or into one ExclusiveNeither.
        const size_t lane = FindKnownFirstTrue(d, ExclusiveNeither(eqL, eqR));
        third = st.SetKey(d, keys + i + lane);
        if (VQSORT_PRINT >= 2) {
          fprintf(stderr, "found 3rd value at vec %zu; writeL %zu\n", i,
                  writeL);
        }
        // 'Undo' what we did by filling the remainder of what we read with R.
        if (i >= N) {
          for (; writeL <= i - N; writeL += N) {
            StoreU(valueR, d, keys + writeL);
          }
        }
        StoreN(valueR, d, keys + writeL, i - writeL);
        return false;
      }
      StoreU(valueL, d, keys + writeL);
      writeL += CountTrue(d, eqL);
    }
  }

  // Final vector, masked comparison (no effect if i == num)
  const size_t remaining = num - i;
  SafeCopyN(remaining, d, keys + i, buf);
  const Vec<D> v = Load(d, buf);
  const Mask<D> valid = FirstN(d, remaining);
  const Mask<D> eqL = And(st.EqualKeys(d, v, valueL), valid);
  const Mask<D> eqR = st.EqualKeys(d, v, valueR);
  // Invalid lanes are considered equal.
  const Mask<D> eq = Or(Or(eqL, eqR), Not(valid));
  // At least one other value present; will require a regular partition.
  if (HWY_UNLIKELY(!AllTrue(d, eq))) {
    const size_t lane = FindKnownFirstTrue(d, Not(eq));
    third = st.SetKey(d, keys + i + lane);
    if (VQSORT_PRINT >= 2) {
      fprintf(stderr, "found 3rd value at partial vec %zu; writeL %zu\n", i,
              writeL);
    }
    // 'Undo' what we did by filling the remainder of what we read with R.
    if (i >= N) {
      for (; writeL <= i - N; writeL += N) {
        StoreU(valueR, d, keys + writeL);
      }
    }
    StoreN(valueR, d, keys + writeL, i - writeL);
    return false;
  }
  StoreN(valueL, d, keys + writeL, remaining);
  writeL += CountTrue(d, eqL);

  // Fill right side
  i = writeL;
  if (num >= N) {
    for (; i <= num - N; i += N) {
      StoreU(valueR, d, keys + i);
    }
  }
  StoreN(valueR, d, keys + i, num - i);

  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "Successful MaybePartitionTwoValue\n");
  }
  return true;
}

// Same as above, except that the pivot equals valueR, so scan right to left.
template <class D, class Traits, typename T>
HWY_INLINE bool MaybePartitionTwoValueR(D d, Traits st, T* HWY_RESTRICT keys,
                                        size_t num, const Vec<D> valueL,
                                        const Vec<D> valueR, Vec<D>& third,
                                        T* HWY_RESTRICT buf) {
  const size_t N = Lanes(d);

  HWY_DASSERT(num >= N);
  size_t pos = num - N;  // current read/write position
  size_t countR = 0;     // number of valueR found

  // For whole vectors, in descending address order: as long as all lanes are
  // equal to L or R, overwrite with valueR. This is faster than counting, then
  // filling both L and R. Loop terminates after unsigned wraparound.
  for (; pos < num; pos -= N) {
    const Vec<D> v = LoadU(d, keys + pos);
    // It is not clear how to apply OrXor here - that can check if *both*
    // comparisons are true, but here we want *either*. Comparing the unsigned
    // min of differences to zero works, but is expensive for u64 prior to AVX3.
    const Mask<D> eqL = st.EqualKeys(d, v, valueL);
    const Mask<D> eqR = st.EqualKeys(d, v, valueR);
    // If there is a third value, stop and undo what we've done. On AVX-512,
    // Or + AllTrue are folded into a single kortest, but only if we are
    // careful with the FindKnownFirstTrue argument - see prior comment on that.
    if (HWY_UNLIKELY(!AllTrue(d, Or(eqL, eqR)))) {
      const size_t lane = FindKnownFirstTrue(d, ExclusiveNeither(eqL, eqR));
      third = st.SetKey(d, keys + pos + lane);
      if (VQSORT_PRINT >= 2) {
        fprintf(stderr, "found 3rd value at vec %zu; countR %zu\n", pos,
                countR);
        MaybePrintVector(d, "third", third, 0, st.LanesPerKey());
      }
      pos += N;  // rewind: we haven't yet committed changes in this iteration.
      // We have filled [pos, num) with R, but only countR of them should have
      // been written. Rewrite [pos, num - countR) to L.
      HWY_DASSERT(countR <= num - pos);
      const size_t endL = num - countR;
      if (endL >= N) {
        for (; pos <= endL - N; pos += N) {
          StoreU(valueL, d, keys + pos);
        }
      }
      StoreN(valueL, d, keys + pos, endL - pos);
      return false;
    }
    StoreU(valueR, d, keys + pos);
    countR += CountTrue(d, eqR);
  }

  // Final partial (or empty) vector, masked comparison.
  const size_t remaining = pos + N;
  HWY_DASSERT(remaining <= N);
  const Vec<D> v = LoadU(d, keys);  // Safe because num >= N.
  const Mask<D> valid = FirstN(d, remaining);
  const Mask<D> eqL = st.EqualKeys(d, v, valueL);
  const Mask<D> eqR = And(st.EqualKeys(d, v, valueR), valid);
  // Invalid lanes are considered equal.
  const Mask<D> eq = Or(Or(eqL, eqR), Not(valid));
  // At least one other value present; will require a regular partition.
  if (HWY_UNLIKELY(!AllTrue(d, eq))) {
    const size_t lane = FindKnownFirstTrue(d, Not(eq));
    third = st.SetKey(d, keys + lane);
    if (VQSORT_PRINT >= 2) {
      fprintf(stderr, "found 3rd value at partial vec %zu; writeR %zu\n", pos,
              countR);
      MaybePrintVector(d, "third", third, 0, st.LanesPerKey());
    }
    pos += N;  // rewind: we haven't yet committed changes in this iteration.
    // We have filled [pos, num) with R, but only countR of them should have
    // been written. Rewrite [pos, num - countR) to L.
    HWY_DASSERT(countR <= num - pos);
    const size_t endL = num - countR;
    if (endL >= N) {
      for (; pos <= endL - N; pos += N) {
        StoreU(valueL, d, keys + pos);
      }
    }
    StoreN(valueL, d, keys + pos, endL - pos);
    return false;
  }
  const size_t lastR = CountTrue(d, eqR);
  countR += lastR;

  // First finish writing valueR - [0, N) lanes were not yet written.
  StoreU(valueR, d, keys);  // Safe because num >= N.

  // Fill left side (ascending order for clarity)
  const size_t endL = num - countR;
  size_t i = 0;
  if (endL >= N) {
    for (; i <= endL - N; i += N) {
      StoreU(valueL, d, keys + i);
    }
  }
  Store(valueL, d, buf);
  SafeCopyN(endL - i, d, buf, keys + i);  // avoids ASan overrun

  if (VQSORT_PRINT >= 2) {
    fprintf(stderr,
            "MaybePartitionTwoValueR countR %zu pos %zu i %zu endL %zu\n",
            countR, pos, i, endL);
  }

  return true;
}

// `idx_second` is `first_mismatch` from `AllEqual` and thus the index of the
// second key. This is the first path into `MaybePartitionTwoValue`, called
// when all samples are equal. Returns false if there are at least a third
// value and sets `third`. Otherwise, partitions the array and returns true.
template <class D, class Traits, typename T>
HWY_INLINE bool PartitionIfTwoKeys(D d, Traits st, const Vec<D> pivot,
                                   T* HWY_RESTRICT keys, size_t num,
                                   const size_t idx_second, const Vec<D> second,
                                   Vec<D>& third, T* HWY_RESTRICT buf) {
  // True if second comes before pivot.
  const bool is_pivotR = AllFalse(d, st.Compare(d, pivot, second));
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "Samples all equal, diff at %zu, isPivotR %d\n", idx_second,
            is_pivotR);
  }
  HWY_DASSERT(AllFalse(d, st.EqualKeys(d, second, pivot)));

  // If pivot is R, we scan backwards over the entire array. Otherwise,
  // we already scanned up to idx_second and can leave those in place.
  return is_pivotR ? MaybePartitionTwoValueR(d, st, keys, num, second, pivot,
                                             third, buf)
                   : MaybePartitionTwoValue(d, st, keys + idx_second,
                                            num - idx_second, pivot, second,
                                            third, buf);
}

// Second path into `MaybePartitionTwoValue`, called when not all samples are
// equal. `samples` is sorted.
template <class D, class Traits, typename T>
HWY_INLINE bool PartitionIfTwoSamples(D d, Traits st, T* HWY_RESTRICT keys,
                                      size_t num, T* HWY_RESTRICT samples) {
  constexpr size_t kSampleLanes = Constants::SampleLanes<T>();
  constexpr size_t N1 = st.LanesPerKey();
  const Vec<D> valueL = st.SetKey(d, samples);
  const Vec<D> valueR = st.SetKey(d, samples + kSampleLanes - N1);
  HWY_DASSERT(AllTrue(d, st.Compare(d, valueL, valueR)));
  HWY_DASSERT(AllFalse(d, st.EqualKeys(d, valueL, valueR)));
  const Vec<D> prev = st.PrevValue(d, valueR);
  // If the sample has more than two values, then the keys have at least that
  // many, and thus this special case is inapplicable.
  if (HWY_UNLIKELY(!AllTrue(d, st.EqualKeys(d, valueL, prev)))) {
    return false;
  }

  // Must not overwrite samples because if this returns false, caller wants to
  // read the original samples again.
  T* HWY_RESTRICT buf = samples + kSampleLanes;
  Vec<D> third;  // unused
  return MaybePartitionTwoValue(d, st, keys, num, valueL, valueR, third, buf);
}

// ------------------------------ Pivot sampling

template <class Traits, class V>
HWY_INLINE V MedianOf3(Traits st, V v0, V v1, V v2) {
  const DFromV<V> d;
  // Slightly faster for 128-bit, apparently because not serially dependent.
  if (st.Is128()) {
    // Median = XOR-sum 'minus' the first and last. Calling First twice is
    // slightly faster than Compare + 2 IfThenElse or even IfThenElse + XOR.
    const V sum = Xor(Xor(v0, v1), v2);
    const V first = st.First(d, st.First(d, v0, v1), v2);
    const V last = st.Last(d, st.Last(d, v0, v1), v2);
    return Xor(Xor(sum, first), last);
  }
  st.Sort2(d, v0, v2);
  v1 = st.Last(d, v0, v1);
  v1 = st.First(d, v1, v2);
  return v1;
}

// Based on https://github.com/numpy/numpy/issues/16313#issuecomment-641897028
HWY_INLINE uint64_t RandomBits(uint64_t* HWY_RESTRICT state) {
  const uint64_t a = state[0];
  const uint64_t b = state[1];
  const uint64_t w = state[2] + 1;
  const uint64_t next = a ^ w;
  state[0] = (b + (b << 3)) ^ (b >> 11);
  const uint64_t rot = (b << 24) | (b >> 40);
  state[1] = rot + next;
  state[2] = w;
  return next;
}

// Returns slightly biased random index of a chunk in [0, num_chunks).
// See https://www.pcg-random.org/posts/bounded-rands.html.
HWY_INLINE size_t RandomChunkIndex(const uint32_t num_chunks, uint32_t bits) {
  const uint64_t chunk_index = (static_cast<uint64_t>(bits) * num_chunks) >> 32;
  HWY_DASSERT(chunk_index < num_chunks);
  return static_cast<size_t>(chunk_index);
}

// Writes samples from `keys[0, num)` into `buf`.
template <class D, class Traits, typename T>
HWY_INLINE void DrawSamples(D d, Traits st, T* HWY_RESTRICT keys, size_t num,
                            T* HWY_RESTRICT buf, uint64_t* HWY_RESTRICT state) {
  using V = decltype(Zero(d));
  const size_t N = Lanes(d);

  // Power of two
  constexpr size_t kLanesPerChunk = Constants::LanesPerChunk(sizeof(T));

  // Align start of keys to chunks. We have at least 2 chunks (x 64 bytes)
  // because the base case handles anything up to 8 vectors (x 16 bytes).
  HWY_DASSERT(num >= Constants::SampleLanes<T>());
  const size_t misalign =
      (reinterpret_cast<uintptr_t>(keys) / sizeof(T)) & (kLanesPerChunk - 1);
  if (misalign != 0) {
    const size_t consume = kLanesPerChunk - misalign;
    keys += consume;
    num -= consume;
  }

  // Generate enough random bits for 6 uint32
  uint32_t bits[6];
  for (size_t i = 0; i < 6; i += 2) {
    const uint64_t bits64 = RandomBits(state);
    CopyBytes<8>(&bits64, bits + i);
  }

  const size_t num_chunks64 = num / kLanesPerChunk;
  // Clamp to uint32 for RandomChunkIndex
  const uint32_t num_chunks =
      static_cast<uint32_t>(HWY_MIN(num_chunks64, 0xFFFFFFFFull));

  const size_t offset0 = RandomChunkIndex(num_chunks, bits[0]) * kLanesPerChunk;
  const size_t offset1 = RandomChunkIndex(num_chunks, bits[1]) * kLanesPerChunk;
  const size_t offset2 = RandomChunkIndex(num_chunks, bits[2]) * kLanesPerChunk;
  const size_t offset3 = RandomChunkIndex(num_chunks, bits[3]) * kLanesPerChunk;
  const size_t offset4 = RandomChunkIndex(num_chunks, bits[4]) * kLanesPerChunk;
  const size_t offset5 = RandomChunkIndex(num_chunks, bits[5]) * kLanesPerChunk;
  for (size_t i = 0; i < kLanesPerChunk; i += N) {
    const V v0 = Load(d, keys + offset0 + i);
    const V v1 = Load(d, keys + offset1 + i);
    const V v2 = Load(d, keys + offset2 + i);
    const V medians0 = MedianOf3(st, v0, v1, v2);
    Store(medians0, d, buf + i);

    const V v3 = Load(d, keys + offset3 + i);
    const V v4 = Load(d, keys + offset4 + i);
    const V v5 = Load(d, keys + offset5 + i);
    const V medians1 = MedianOf3(st, v3, v4, v5);
    Store(medians1, d, buf + i + kLanesPerChunk);
  }
}

template <class V>
V OrXor(const V o, const V x1, const V x2) {
  return Or(o, Xor(x1, x2));  // TERNLOG on AVX3
}

// For detecting inputs where (almost) all keys are equal.
template <class D, class Traits>
HWY_INLINE bool UnsortedSampleEqual(D d, Traits st,
                                    const TFromD<D>* HWY_RESTRICT samples) {
  constexpr size_t kSampleLanes = Constants::SampleLanes<TFromD<D>>();
  const size_t N = Lanes(d);
  // Both are powers of two, so there will be no remainders.
  HWY_DASSERT(N < kSampleLanes);
  using V = Vec<D>;

  const V first = st.SetKey(d, samples);

  if (!hwy::IsFloat<TFromD<D>>()) {
    // OR of XOR-difference may be faster than comparison.
    V diff = Zero(d);
    for (size_t i = 0; i < kSampleLanes; i += N) {
      const V v = Load(d, samples + i);
      diff = OrXor(diff, first, v);
    }
    return st.NoKeyDifference(d, diff);
  } else {
    // Disable the OrXor optimization for floats because OrXor will not treat
    // subnormals the same as actual comparisons, leading to logic errors for
    // 2-value cases.
    for (size_t i = 0; i < kSampleLanes; i += N) {
      const V v = Load(d, samples + i);
      if (!AllTrue(d, st.EqualKeys(d, v, first))) {
        return false;
      }
    }
    return true;
  }
}

template <class D, class Traits, typename T>
HWY_INLINE void SortSamples(D d, Traits st, T* HWY_RESTRICT buf) {
  const size_t N = Lanes(d);
  constexpr size_t kSampleLanes = Constants::SampleLanes<T>();
  // Network must be large enough to sort two chunks.
  HWY_DASSERT(Constants::BaseCaseNumLanes<st.LanesPerKey()>(N) >= kSampleLanes);

  BaseCase(d, st, buf, kSampleLanes, buf + kSampleLanes);

  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "Samples:\n");
    for (size_t i = 0; i < kSampleLanes; i += N) {
      MaybePrintVector(d, "", Load(d, buf + i), 0, N);
    }
  }
}

// ------------------------------ Pivot selection

enum class PivotResult {
  kDone,     // stop without partitioning (all equal, or two-value partition)
  kNormal,   // partition and recurse left and right
  kIsFirst,  // partition but skip left recursion
  kWasLast,  // partition but skip right recursion
};

HWY_INLINE const char* PivotResultString(PivotResult result) {
  switch (result) {
    case PivotResult::kDone:
      return "done";
    case PivotResult::kNormal:
      return "normal";
    case PivotResult::kIsFirst:
      return "first";
    case PivotResult::kWasLast:
      return "last";
  }
  return "unknown";
}

// (Could vectorize, but only 0.2% of total time)
template <class Traits, typename T>
HWY_INLINE size_t PivotRank(Traits st, const T* HWY_RESTRICT samples) {
  constexpr size_t kSampleLanes = Constants::SampleLanes<T>();
  constexpr size_t N1 = st.LanesPerKey();

  constexpr size_t kRankMid = kSampleLanes / 2;
  static_assert(kRankMid % N1 == 0, "Mid is not an aligned key");

  // Find the previous value not equal to the median.
  size_t rank_prev = kRankMid - N1;
  for (; st.Equal1(samples + rank_prev, samples + kRankMid); rank_prev -= N1) {
    // All previous samples are equal to the median.
    if (rank_prev == 0) return 0;
  }

  size_t rank_next = rank_prev + N1;
  for (; st.Equal1(samples + rank_next, samples + kRankMid); rank_next += N1) {
    // The median is also the largest sample. If it is also the largest key,
    // we'd end up with an empty right partition, so choose the previous key.
    if (rank_next == kSampleLanes - N1) return rank_prev;
  }

  // If we choose the median as pivot, the ratio of keys ending in the left
  // partition will likely be rank_next/kSampleLanes (if the sample is
  // representative). This is because equal-to-pivot values also land in the
  // left - it's infeasible to do an in-place vectorized 3-way partition.
  // Check whether prev would lead to a more balanced partition.
  const size_t excess_if_median = rank_next - kRankMid;
  const size_t excess_if_prev = kRankMid - rank_prev;
  return excess_if_median < excess_if_prev ? kRankMid : rank_prev;
}

// Returns pivot chosen from `samples`. It will never be the largest key
// (thus the right partition will never be empty).
template <class D, class Traits, typename T>
HWY_INLINE Vec<D> ChoosePivotByRank(D d, Traits st,
                                    const T* HWY_RESTRICT samples) {
  const size_t pivot_rank = PivotRank(st, samples);
  const Vec<D> pivot = st.SetKey(d, samples + pivot_rank);
  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "  Pivot rank %3zu\n", pivot_rank);
    HWY_ALIGN T pivot_lanes[MaxLanes(d)];
    Store(pivot, d, pivot_lanes);
    using Key = typename Traits::KeyType;
    Key key;
    CopyBytes<sizeof(Key)>(pivot_lanes, &key);
    PrintValue(key);
  }
  // Verify pivot is not equal to the last sample.
  constexpr size_t kSampleLanes = Constants::SampleLanes<T>();
  constexpr size_t N1 = st.LanesPerKey();
  const Vec<D> last = st.SetKey(d, samples + kSampleLanes - N1);
  const bool all_neq = AllTrue(d, st.NotEqualKeys(d, pivot, last));
  (void)all_neq;
  HWY_DASSERT(all_neq);
  return pivot;
}

// Returns true if all keys equal `pivot`, otherwise returns false and sets
// `*first_mismatch' to the index of the first differing key.
template <class D, class Traits, typename T>
HWY_INLINE bool AllEqual(D d, Traits st, const Vec<D> pivot,
                         const T* HWY_RESTRICT keys, size_t num,
                         size_t* HWY_RESTRICT first_mismatch) {
  const size_t N = Lanes(d);
  // Ensures we can use overlapping loads for the tail; see HandleSpecialCases.
  HWY_DASSERT(num >= N);
  const Vec<D> zero = Zero(d);

  // Vector-align keys + i.
  const size_t misalign =
      (reinterpret_cast<uintptr_t>(keys) / sizeof(T)) & (N - 1);
  HWY_DASSERT(misalign % st.LanesPerKey() == 0);
  const size_t consume = N - misalign;
  {
    const Vec<D> v = LoadU(d, keys);
    // Only check masked lanes; consider others to be equal.
    const Mask<D> diff = And(FirstN(d, consume), st.NotEqualKeys(d, v, pivot));
    if (HWY_UNLIKELY(!AllFalse(d, diff))) {
      const size_t lane = FindKnownFirstTrue(d, diff);
      *first_mismatch = lane;
      return false;
    }
  }
  size_t i = consume;
  HWY_DASSERT(((reinterpret_cast<uintptr_t>(keys + i) / sizeof(T)) & (N - 1)) ==
              0);

  // Disable the OrXor optimization for floats because OrXor will not treat
  // subnormals the same as actual comparisons, leading to logic errors for
  // 2-value cases.
  if (!hwy::IsFloat<T>()) {
    // Sticky bits registering any difference between `keys` and the first key.
    // We use vector XOR because it may be cheaper than comparisons, especially
    // for 128-bit. 2x unrolled for more ILP.
    Vec<D> diff0 = zero;
    Vec<D> diff1 = zero;

    // We want to stop once a difference has been found, but without slowing
    // down the loop by comparing during each iteration. The compromise is to
    // compare after a 'group', which consists of kLoops times two vectors.
    constexpr size_t kLoops = 8;
    const size_t lanes_per_group = kLoops * 2 * N;

    if (num >= lanes_per_group) {
      for (; i <= num - lanes_per_group; i += lanes_per_group) {
        HWY_DEFAULT_UNROLL
        for (size_t loop = 0; loop < kLoops; ++loop) {
          const Vec<D> v0 = Load(d, keys + i + loop * 2 * N);
          const Vec<D> v1 = Load(d, keys + i + loop * 2 * N + N);
          diff0 = OrXor(diff0, v0, pivot);
          diff1 = OrXor(diff1, v1, pivot);
        }

        // If there was a difference in the entire group:
        if (HWY_UNLIKELY(!st.NoKeyDifference(d, Or(diff0, diff1)))) {
          // .. then loop until the first one, with termination guarantee.
          for (;; i += N) {
            const Vec<D> v = Load(d, keys + i);
            const Mask<D> diff = st.NotEqualKeys(d, v, pivot);
            if (HWY_UNLIKELY(!AllFalse(d, diff))) {
              const size_t lane = FindKnownFirstTrue(d, diff);
              *first_mismatch = i + lane;
              return false;
            }
          }
        }
      }
    }
  }  // !hwy::IsFloat<T>()

  // Whole vectors, no unrolling, compare directly
  for (; i <= num - N; i += N) {
    const Vec<D> v = Load(d, keys + i);
    const Mask<D> diff = st.NotEqualKeys(d, v, pivot);
    if (HWY_UNLIKELY(!AllFalse(d, diff))) {
      const size_t lane = FindKnownFirstTrue(d, diff);
      *first_mismatch = i + lane;
      return false;
    }
  }
  // Always re-check the last (unaligned) vector to reduce branching.
  i = num - N;
  const Vec<D> v = LoadU(d, keys + i);
  const Mask<D> diff = st.NotEqualKeys(d, v, pivot);
  if (HWY_UNLIKELY(!AllFalse(d, diff))) {
    const size_t lane = FindKnownFirstTrue(d, diff);
    *first_mismatch = i + lane;
    return false;
  }

  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "All keys equal\n");
  }
  return true;  // all equal
}

// Called from 'two locations', but only one is active (IsKV is constexpr).
template <class D, class Traits, typename T>
HWY_INLINE bool ExistsAnyBefore(D d, Traits st, const T* HWY_RESTRICT keys,
                                size_t num, const Vec<D> pivot) {
  const size_t N = Lanes(d);
  HWY_DASSERT(num >= N);  // See HandleSpecialCases

  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "Scanning for before\n");
  }

  size_t i = 0;

  constexpr size_t kLoops = 16;
  const size_t lanes_per_group = kLoops * N;

  Vec<D> first = pivot;

  // Whole group, unrolled
  if (num >= lanes_per_group) {
    for (; i <= num - lanes_per_group; i += lanes_per_group) {
      HWY_DEFAULT_UNROLL
      for (size_t loop = 0; loop < kLoops; ++loop) {
        const Vec<D> curr = LoadU(d, keys + i + loop * N);
        first = st.First(d, first, curr);
      }

      if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, first, pivot)))) {
        if (VQSORT_PRINT >= 2) {
          fprintf(stderr, "Stopped scanning at end of group %zu\n",
                  i + lanes_per_group);
        }
        return true;
      }
    }
  }
  // Whole vectors, no unrolling
  for (; i <= num - N; i += N) {
    const Vec<D> curr = LoadU(d, keys + i);
    if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, curr, pivot)))) {
      if (VQSORT_PRINT >= 2) {
        fprintf(stderr, "Stopped scanning at %zu\n", i);
      }
      return true;
    }
  }
  // If there are remainders, re-check the last whole vector.
  if (HWY_LIKELY(i != num)) {
    const Vec<D> curr = LoadU(d, keys + num - N);
    if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, curr, pivot)))) {
      if (VQSORT_PRINT >= 2) {
        fprintf(stderr, "Stopped scanning at last %zu\n", num - N);
      }
      return true;
    }
  }

  return false;  // pivot is the first
}

// Called from 'two locations', but only one is active (IsKV is constexpr).
template <class D, class Traits, typename T>
HWY_INLINE bool ExistsAnyAfter(D d, Traits st, const T* HWY_RESTRICT keys,
                               size_t num, const Vec<D> pivot) {
  const size_t N = Lanes(d);
  HWY_DASSERT(num >= N);  // See HandleSpecialCases

  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "Scanning for after\n");
  }

  size_t i = 0;

  constexpr size_t kLoops = 16;
  const size_t lanes_per_group = kLoops * N;

  Vec<D> last = pivot;

  // Whole group, unrolled
  if (num >= lanes_per_group) {
    for (; i + lanes_per_group <= num; i += lanes_per_group) {
      HWY_DEFAULT_UNROLL
      for (size_t loop = 0; loop < kLoops; ++loop) {
        const Vec<D> curr = LoadU(d, keys + i + loop * N);
        last = st.Last(d, last, curr);
      }

      if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, pivot, last)))) {
        if (VQSORT_PRINT >= 2) {
          fprintf(stderr, "Stopped scanning at end of group %zu\n",
                  i + lanes_per_group);
        }
        return true;
      }
    }
  }
  // Whole vectors, no unrolling
  for (; i <= num - N; i += N) {
    const Vec<D> curr = LoadU(d, keys + i);
    if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, pivot, curr)))) {
      if (VQSORT_PRINT >= 2) {
        fprintf(stderr, "Stopped scanning at %zu\n", i);
      }
      return true;
    }
  }
  // If there are remainders, re-check the last whole vector.
  if (HWY_LIKELY(i != num)) {
    const Vec<D> curr = LoadU(d, keys + num - N);
    if (HWY_UNLIKELY(!AllFalse(d, st.Compare(d, pivot, curr)))) {
      if (VQSORT_PRINT >= 2) {
        fprintf(stderr, "Stopped scanning at last %zu\n", num - N);
      }
      return true;
    }
  }

  return false;  // pivot is the last
}

// Returns pivot chosen from `keys[0, num)`. It will never be the largest key
// (thus the right partition will never be empty).
template <class D, class Traits, typename T>
HWY_INLINE Vec<D> ChoosePivotForEqualSamples(D d, Traits st,
                                             T* HWY_RESTRICT keys, size_t num,
                                             T* HWY_RESTRICT samples,
                                             Vec<D> second, Vec<D> third,
                                             PivotResult& result) {
  const Vec<D> pivot = st.SetKey(d, samples);  // the single unique sample

  // Early out for mostly-0 arrays, where pivot is often FirstValue.
  if (HWY_UNLIKELY(AllTrue(d, st.EqualKeys(d, pivot, st.FirstValue(d))))) {
    result = PivotResult::kIsFirst;
    if (VQSORT_PRINT >= 2) {
      fprintf(stderr, "Pivot equals first possible value\n");
    }
    return pivot;
  }
  if (HWY_UNLIKELY(AllTrue(d, st.EqualKeys(d, pivot, st.LastValue(d))))) {
    if (VQSORT_PRINT >= 2) {
      fprintf(stderr, "Pivot equals last possible value\n");
    }
    result = PivotResult::kWasLast;
    return st.PrevValue(d, pivot);
  }

  // If key-value, we didn't run PartitionIfTwo* and thus `third` is unknown and
  // cannot be used.
  if (st.IsKV()) {
    // If true, pivot is either middle or last.
    const bool before = !AllFalse(d, st.Compare(d, second, pivot));
    if (HWY_UNLIKELY(before)) {
      // Not last, so middle.
      if (HWY_UNLIKELY(ExistsAnyAfter(d, st, keys, num, pivot))) {
        result = PivotResult::kNormal;
        return pivot;
      }

      // We didn't find anything after pivot, so it is the last. Because keys
      // equal to the pivot go to the left partition, the right partition would
      // be empty and Partition will not have changed anything. Instead use the
      // previous value in sort order, which is not necessarily an actual key.
      result = PivotResult::kWasLast;
      return st.PrevValue(d, pivot);
    }

    // Otherwise, pivot is first or middle. Rule out it being first:
    if (HWY_UNLIKELY(ExistsAnyBefore(d, st, keys, num, pivot))) {
      result = PivotResult::kNormal;
      return pivot;
    }
    // It is first: fall through to shared code below.
  } else {
    // Check if pivot is between two known values. If so, it is not the first
    // nor the last and we can avoid scanning.
    st.Sort2(d, second, third);
    HWY_DASSERT(AllTrue(d, st.Compare(d, second, third)));
    const bool before = !AllFalse(d, st.Compare(d, second, pivot));
    const bool after = !AllFalse(d, st.Compare(d, pivot, third));
    // Only reached if there are three keys, which means pivot is either first,
    // last, or in between. Thus there is another key that comes before or
    // after.
    HWY_DASSERT(before || after);
    if (HWY_UNLIKELY(before)) {
      // Neither first nor last.
      if (HWY_UNLIKELY(after || ExistsAnyAfter(d, st, keys, num, pivot))) {
        result = PivotResult::kNormal;
        return pivot;
      }

      // We didn't find anything after pivot, so it is the last. Because keys
      // equal to the pivot go to the left partition, the right partition would
      // be empty and Partition will not have changed anything. Instead use the
      // previous value in sort order, which is not necessarily an actual key.
      result = PivotResult::kWasLast;
      return st.PrevValue(d, pivot);
    }

    // Has after, and we found one before: in the middle.
    if (HWY_UNLIKELY(ExistsAnyBefore(d, st, keys, num, pivot))) {
      result = PivotResult::kNormal;
      return pivot;
    }
  }

  // Pivot is first. We could consider a special partition mode that only
  // reads from and writes to the right side, and later fills in the left
  // side, which we know is equal to the pivot. However, that leads to more
  // cache misses if the array is large, and doesn't save much, hence is a
  // net loss.
  result = PivotResult::kIsFirst;
  return pivot;
}

// ------------------------------ Quicksort recursion

enum class RecurseMode {
  kSort,    // Sort mode.
  kSelect,  // Select mode.
            // The element pointed at by nth is changed to whatever element
            // would occur in that position if [first, last) were sorted. All of
            // the elements before this new nth element are less than or equal
            // to the elements after the new nth element.
  kLooseSelect,  // Loose select mode.
                 // The first n elements will contain the n smallest elements in
                 // unspecified order
};

template <class D, class Traits, typename T>
HWY_NOINLINE void PrintMinMax(D d, Traits st, const T* HWY_RESTRICT keys,
                              size_t num, T* HWY_RESTRICT buf) {
  if (VQSORT_PRINT >= 2) {
    const size_t N = Lanes(d);
    if (num < N) return;

    Vec<D> first = st.LastValue(d);
    Vec<D> last = st.FirstValue(d);

    size_t i = 0;
    for (; i <= num - N; i += N) {
      const Vec<D> v = LoadU(d, keys + i);
      first = st.First(d, v, first);
      last = st.Last(d, v, last);
    }
    if (HWY_LIKELY(i != num)) {
      HWY_DASSERT(num >= N);  // See HandleSpecialCases
      const Vec<D> v = LoadU(d, keys + num - N);
      first = st.First(d, v, first);
      last = st.Last(d, v, last);
    }

    first = st.FirstOfLanes(d, first, buf);
    last = st.LastOfLanes(d, last, buf);
    MaybePrintVector(d, "first", first, 0, st.LanesPerKey());
    MaybePrintVector(d, "last", last, 0, st.LanesPerKey());
  }
}

template <RecurseMode mode, class D, class Traits, typename T>
HWY_NOINLINE void Recurse(D d, Traits st, T* HWY_RESTRICT keys,
                          const size_t num, T* HWY_RESTRICT buf,
                          uint64_t* HWY_RESTRICT state,
                          const size_t remaining_levels, const size_t k = 0) {
  HWY_DASSERT(num != 0);

  const size_t N = Lanes(d);
  constexpr size_t kLPK = st.LanesPerKey();
  if (HWY_UNLIKELY(num <= Constants::BaseCaseNumLanes<kLPK>(N))) {
    BaseCase(d, st, keys, num, buf);
    return;
  }

  // Move after BaseCase so we skip printing for small subarrays.
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "\n\n=== Recurse depth=%zu len=%zu k=%zu\n",
            remaining_levels, num, k);
    PrintMinMax(d, st, keys, num, buf);
  }

  DrawSamples(d, st, keys, num, buf, state);

  Vec<D> pivot;
  PivotResult result = PivotResult::kNormal;
  if (HWY_UNLIKELY(UnsortedSampleEqual(d, st, buf))) {
    pivot = st.SetKey(d, buf);
    size_t idx_second = 0;
    if (HWY_UNLIKELY(AllEqual(d, st, pivot, keys, num, &idx_second))) {
      return;
    }
    HWY_DASSERT(idx_second % st.LanesPerKey() == 0);
    // Must capture the value before PartitionIfTwoKeys may overwrite it.
    const Vec<D> second = st.SetKey(d, keys + idx_second);
    MaybePrintVector(d, "pivot", pivot, 0, st.LanesPerKey());
    MaybePrintVector(d, "second", second, 0, st.LanesPerKey());

    Vec<D> third = Zero(d);
    // Not supported for key-value types because two 'keys' may be equivalent
    // but not interchangeable (their values may differ).
    if (HWY_UNLIKELY(!st.IsKV() &&
                     PartitionIfTwoKeys(d, st, pivot, keys, num, idx_second,
                                        second, third, buf))) {
      return;  // Done, skip recursion because each side has all-equal keys.
    }

    // We can no longer start scanning from idx_second because
    // PartitionIfTwoKeys may have reordered keys.
    pivot = ChoosePivotForEqualSamples(d, st, keys, num, buf, second, third,
                                       result);
    // If kNormal, `pivot` is very common but not the first/last. It is
    // tempting to do a 3-way partition (to avoid moving the =pivot keys a
    // second time), but that is a net loss due to the extra comparisons.
  } else {
    SortSamples(d, st, buf);

    // Not supported for key-value types because two 'keys' may be equivalent
    // but not interchangeable (their values may differ).
    if (HWY_UNLIKELY(!st.IsKV() &&
                     PartitionIfTwoSamples(d, st, keys, num, buf))) {
      return;
    }

    pivot = ChoosePivotByRank(d, st, buf);
  }

  // Too many recursions. This is unlikely to happen because we select pivots
  // from large (though still O(1)) samples.
  if (HWY_UNLIKELY(remaining_levels == 0)) {
    if (VQSORT_PRINT >= 1) {
      fprintf(stderr, "HeapSort reached, size=%zu\n", num);
    }
    HeapSort(st, keys, num);  // Slow but N*logN.
    return;
  }

  const size_t bound = Partition(d, st, keys, num, pivot, buf);
  if (VQSORT_PRINT >= 2) {
    fprintf(stderr, "bound %zu num %zu result %s\n", bound, num,
            PivotResultString(result));
  }
  // The left partition is not empty because the pivot is usually one of the
  // keys. Exception: if kWasLast, we set pivot to PrevValue(pivot), but we
  // still have at least one value <= pivot because AllEqual ruled out the case
  // of only one unique value. Note that for floating-point, PrevValue can
  // return the same value (for -inf inputs), but that would just mean the
  // pivot is again one of the keys.
  using Order = typename Traits::Order;
  (void)Order::IsAscending();
  HWY_DASSERT_M(bound != 0,
                (Order::IsAscending() ? "Ascending" : "Descending"));
  // ChoosePivot* ensure pivot != last, so the right partition is never empty
  // except in the rare case of the pivot matching the last-in-sort-order value,
  // which implies we anyway skip the right partition due to kWasLast.
  HWY_DASSERT(bound != num || result == PivotResult::kWasLast);

  HWY_IF_CONSTEXPR(mode == RecurseMode::kSelect) {
    if (HWY_LIKELY(result != PivotResult::kIsFirst) && k < bound) {
      Recurse<RecurseMode::kSelect>(d, st, keys, bound, buf, state,
                                    remaining_levels - 1, k);
    } else if (HWY_LIKELY(result != PivotResult::kWasLast) && k >= bound) {
      Recurse<RecurseMode::kSelect>(d, st, keys + bound, num - bound, buf,
                                    state, remaining_levels - 1, k - bound);
    }
  }
  HWY_IF_CONSTEXPR(mode == RecurseMode::kSort) {
    if (HWY_LIKELY(result != PivotResult::kIsFirst)) {
      Recurse<RecurseMode::kSort>(d, st, keys, bound, buf, state,
                                  remaining_levels - 1);
    }
    if (HWY_LIKELY(result != PivotResult::kWasLast)) {
      Recurse<RecurseMode::kSort>(d, st, keys + bound, num - bound, buf, state,
                                  remaining_levels - 1);
    }
  }
}

// Returns true if sorting is finished.
template <class D, class Traits, typename T>
HWY_INLINE bool HandleSpecialCases(D d, Traits st, T* HWY_RESTRICT keys,
                                   size_t num, T* HWY_RESTRICT buf) {
  const size_t N = Lanes(d);
  constexpr size_t kLPK = st.LanesPerKey();
  const size_t base_case_num = Constants::BaseCaseNumLanes<kLPK>(N);

  // Recurse will also check this, but doing so here first avoids setting up
  // the random generator state.
  if (HWY_UNLIKELY(num <= base_case_num)) {
    if (VQSORT_PRINT >= 1) {
      fprintf(stderr, "Special-casing small, %zu lanes\n", num);
    }
    BaseCase(d, st, keys, num, buf);
    return true;
  }

  // 128-bit keys require vectors with at least two u64 lanes, which is always
  // the case unless `d` requests partial vectors (e.g. fraction = 1/2) AND the
  // hardware vector width is less than 128bit / fraction.
  const bool partial_128 = !IsFull(d) && N < 2 && st.Is128();
  // Partition assumes its input is at least two vectors. If vectors are huge,
  // base_case_num may actually be smaller. If so, which is only possible on
  // RVV, pass a capped or partial d (LMUL < 1). Use HWY_MAX_BYTES instead of
  // HWY_LANES to account for the largest possible LMUL.
  constexpr bool kPotentiallyHuge =
      HWY_MAX_BYTES / sizeof(T) > Constants::kMaxRows * Constants::kMaxCols;
  const bool huge_vec = kPotentiallyHuge && (2 * N > base_case_num);
  if (partial_128 || huge_vec) {
    if (VQSORT_PRINT >= 1) {
      fprintf(stderr, "WARNING: using slow HeapSort: partial %d huge %d\n",
              partial_128, huge_vec);
    }
    HeapSort(st, keys, num);
    return true;
  }

  // We could also check for already sorted/reverse/equal, but that's probably
  // counterproductive if vqsort is used as a base case.

  return false;  // not finished sorting
}

#endif  // VQSORT_ENABLED

template <class D, class Traits, typename T, HWY_IF_FLOAT(T)>
HWY_INLINE size_t CountAndReplaceNaN(D d, Traits st, T* HWY_RESTRICT keys,
                                     size_t num) {
  const size_t N = Lanes(d);
  // Will be sorted to the back of the array.
  const Vec<D> sentinel = st.LastValue(d);
  size_t num_nan = 0;
  size_t i = 0;
  if (num >= N) {
    for (; i <= num - N; i += N) {
      const Mask<D> is_nan = IsNaN(LoadU(d, keys + i));
      BlendedStore(sentinel, is_nan, d, keys + i);
      num_nan += CountTrue(d, is_nan);
    }
  }

  const size_t remaining = num - i;
  HWY_DASSERT(remaining < N);
  const Vec<D> v = LoadN(d, keys + i, remaining);
  const Mask<D> is_nan = IsNaN(v);
  StoreN(IfThenElse(is_nan, sentinel, v), d, keys + i, remaining);
  num_nan += CountTrue(d, is_nan);
  return num_nan;
}

// IsNaN is not implemented for non-float, so skip it.
template <class D, class Traits, typename T, HWY_IF_NOT_FLOAT(T)>
HWY_INLINE size_t CountAndReplaceNaN(D, Traits, T* HWY_RESTRICT, size_t) {
  return 0;
}

}  // namespace detail

// Old interface with user-specified buffer, retained for compatibility. Called
// by the newer overload below. `buf` must be vector-aligned and hold at least
// SortConstants::BufBytes(HWY_MAX_BYTES, st.LanesPerKey()).
template <class D, class Traits, typename T>
void Sort(D d, Traits st, T* HWY_RESTRICT keys, const size_t num,
          T* HWY_RESTRICT buf) {
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "=============== Sort %s num=%zu, vec bytes=%zu\n",
            st.KeyString(), num, sizeof(T) * Lanes(d));
  }

#if HWY_MAX_BYTES > 64
  // sorting_networks-inl and traits assume no more than 512 bit vectors.
  if (HWY_UNLIKELY(Lanes(d) > 64 / sizeof(T))) {
    return Sort(CappedTag<T, 64 / sizeof(T)>(), st, keys, num, buf);
  }
#endif  // HWY_MAX_BYTES > 64

  const size_t num_nan = detail::CountAndReplaceNaN(d, st, keys, num);

#if VQSORT_ENABLED || HWY_IDE
  if (!detail::HandleSpecialCases(d, st, keys, num, buf)) {
    uint64_t* HWY_RESTRICT state = hwy::detail::GetGeneratorStateStatic();
    // Introspection: switch to worst-case N*logN heapsort after this many.
    // Should never be reached, so computing log2 exactly does not help.
    const size_t max_levels = 50;
    detail::Recurse<detail::RecurseMode::kSort>(d, st, keys, num, buf, state,
                                                max_levels);
  }
#else   // !VQSORT_ENABLED
  (void)d;
  (void)buf;
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "WARNING: using slow HeapSort because vqsort disabled\n");
  }
  detail::HeapSort(st, keys, num);
#endif  // VQSORT_ENABLED

  if (num_nan != 0) {
    Fill(d, GetLane(NaN(d)), num_nan, keys + num - num_nan);
  }
}

template <class D, class Traits, typename T>
void PartialSort(D d, Traits st, T* HWY_RESTRICT keys, size_t num, size_t k,
                 T* HWY_RESTRICT buf) {
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr,
            "=============== PartialSort %s num=%zu, k=%zu vec bytes=%zu\n",
            st.KeyString(), num, k, sizeof(T) * Lanes(d));
  }
  HWY_DASSERT(k <= num);

#if HWY_MAX_BYTES > 64
  // sorting_networks-inl and traits assume no more than 512 bit vectors.
  if (HWY_UNLIKELY(Lanes(d) > 64 / sizeof(T))) {
    return PartialSort(CappedTag<T, 64 / sizeof(T)>(), st, keys, num, k, buf);
  }
#endif  // HWY_MAX_BYTES > 64

  const size_t num_nan = detail::CountAndReplaceNaN(d, st, keys, num);

#if VQSORT_ENABLED || HWY_IDE
  if (!detail::HandleSpecialCases(d, st, keys, num, buf)) {  // TODO
    uint64_t* HWY_RESTRICT state = hwy::detail::GetGeneratorStateStatic();
    // Introspection: switch to worst-case N*logN heapsort after this many.
    // Should never be reached, so computing log2 exactly does not help.
    const size_t max_levels = 50;
    // TODO: optimize to use kLooseSelect
    detail::Recurse<detail::RecurseMode::kSelect>(d, st, keys, num, buf, state,
                                                  max_levels, k);
    detail::Recurse<detail::RecurseMode::kSort>(d, st, keys, k, buf, state,
                                                max_levels);
  }
#else   // !VQSORT_ENABLED
  (void)d;
  (void)buf;
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "WARNING: using slow HeapSort because vqsort disabled\n");
  }
  detail::HeapPartialSort(st, keys, num, k);
#endif  // VQSORT_ENABLED

  if (num_nan != 0) {
    Fill(d, GetLane(NaN(d)), num_nan, keys + num - num_nan);
  }
}

template <class D, class Traits, typename T>
void Select(D d, Traits st, T* HWY_RESTRICT keys, const size_t num,
            const size_t k, T* HWY_RESTRICT buf) {
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "=============== Select %s num=%zu, k=%zu vec bytes=%zu\n",
            st.KeyString(), num, k, sizeof(T) * Lanes(d));
  }
  HWY_DASSERT(k < num);

#if HWY_MAX_BYTES > 64
  // sorting_networks-inl and traits assume no more than 512 bit vectors.
  if (HWY_UNLIKELY(Lanes(d) > 64 / sizeof(T))) {
    return Select(CappedTag<T, 64 / sizeof(T)>(), st, keys, num, k, buf);
  }
#endif  // HWY_MAX_BYTES > 64

  const size_t num_nan = detail::CountAndReplaceNaN(d, st, keys, num);

#if VQSORT_ENABLED || HWY_IDE
  if (!detail::HandleSpecialCases(d, st, keys, num, buf)) {  // TODO
    uint64_t* HWY_RESTRICT state = hwy::detail::GetGeneratorStateStatic();
    // Introspection: switch to worst-case N*logN heapsort after this many.
    // Should never be reached, so computing log2 exactly does not help.
    const size_t max_levels = 50;
    detail::Recurse<detail::RecurseMode::kSelect>(d, st, keys, num, buf, state,
                                                  max_levels, k);
  }
#else   // !VQSORT_ENABLED
  (void)d;
  (void)buf;
  if (VQSORT_PRINT >= 1) {
    fprintf(stderr, "WARNING: using slow HeapSort because vqsort disabled\n");
  }
  detail::HeapSelect(st, keys, num, k);
#endif  // VQSORT_ENABLED

  if (num_nan != 0) {
    Fill(d, GetLane(NaN(d)), num_nan, keys + num - num_nan);
  }
}

// Sorts `keys[0..num-1]` according to the order defined by `st.Compare`.
// In-place i.e. O(1) additional storage. Worst-case N*logN comparisons.
// Non-stable (order of equal keys may change), except for the common case where
// the upper bits of T are the key, and the lower bits are a sequential or at
// least unique ID. Any NaN will be moved to the back of the array and replaced
// with the canonical NaN(d).
// There is no upper limit on `num`, but note that pivots may be chosen by
// sampling only from the first 256 GiB.
//
// `d` is typically SortTag<T> (chooses between full and partial vectors).
// `st` is SharedTraits<Traits*<Order*>>. This abstraction layer bridges
//   differences in sort order and single-lane vs 128-bit keys.
// `num` is in units of `T`, not keys!
template <class D, class Traits, typename T>
HWY_API void Sort(D d, Traits st, T* HWY_RESTRICT keys, const size_t num) {
  constexpr size_t kLPK = st.LanesPerKey();
  HWY_ALIGN T buf[SortConstants::BufBytes<T, kLPK>(HWY_MAX_BYTES) / sizeof(T)];
  Sort(d, st, keys, num, buf);
}

// Rearranges elements such that the range [0, k) contains the sorted first `k`
// elements in the range [0, n) ordered by `st.Compare`. See also the comment
// for `Sort()`; note that `num` and `k` are in units of `T`, not keys!
template <class D, class Traits, typename T>
HWY_API void PartialSort(D d, Traits st, T* HWY_RESTRICT keys, const size_t num,
                         const size_t k) {
  constexpr size_t kLPK = st.LanesPerKey();
  HWY_ALIGN T buf[SortConstants::BufBytes<T, kLPK>(HWY_MAX_BYTES) / sizeof(T)];
  PartialSort(d, st, keys, num, k, buf);
}

// Reorders `keys[0..num-1]` such that `keys+k` is the k-th element if keys was
// sorted by `st.Compare`, and all of the elements before it are ordered
// by `st.Compare` relative to `keys[k]`. See also the comment for `Sort()`;
// note that `num` and `k` are in units of `T`, not keys!
template <class D, class Traits, typename T>
HWY_API void Select(D d, Traits st, T* HWY_RESTRICT keys, const size_t num,
                    const size_t k) {
  constexpr size_t kLPK = st.LanesPerKey();
  HWY_ALIGN T buf[SortConstants::BufBytes<T, kLPK>(HWY_MAX_BYTES) / sizeof(T)];
  Select(d, st, keys, num, k, buf);
}

// Translates Key and Order (SortAscending or SortDescending) to SharedTraits.
namespace detail {

// Primary template for built-in key types = lane type.
template <typename Key>
struct KeyAdapter {
  template <class Order>
  using Traits = TraitsLane<
      hwy::If<Order::IsAscending(), OrderAscending<Key>, OrderDescending<Key>>>;
};

template <>
struct KeyAdapter<hwy::K32V32> {
  template <class Order>
  using Traits = TraitsLane<
      hwy::If<Order::IsAscending(), OrderAscendingKV64, OrderDescendingKV64>>;
};

// 128-bit keys require 128-bit SIMD.
#if HWY_TARGET != HWY_SCALAR

template <>
struct KeyAdapter<hwy::K64V64> {
  template <class Order>
  using Traits = Traits128<
      hwy::If<Order::IsAscending(), OrderAscendingKV128, OrderDescendingKV128>>;
};

template <>
struct KeyAdapter<hwy::uint128_t> {
  template <class Order>
  using Traits = Traits128<
      hwy::If<Order::IsAscending(), OrderAscending128, OrderDescending128>>;
};

#endif  // HWY_TARGET != HWY_SCALAR

template <typename Key, class Order>
using MakeTraits =
    SharedTraits<typename KeyAdapter<Key>::template Traits<Order>>;

}  // namespace detail

// Simpler interface matching VQSort(), but without dynamic dispatch. Uses the
// instructions available in the current target (HWY_NAMESPACE). Supported key
// types: 16-64 bit unsigned/signed/floating-point (but float16/64 only #if
// HWY_HAVE_FLOAT16/64), uint128_t, K64V64, K32V32. Note that `num`, and for
// VQPartialSortStatic/VQSelectStatic also `k`, are in units of *keys*, whereas
// for all functions above this point, they are in units of `T`. Order is either
// SortAscending or SortDescending.
template <typename Key, class Order>
void VQSortStatic(Key* HWY_RESTRICT keys, const size_t num_keys, Order) {
#if VQSORT_ENABLED
  const detail::MakeTraits<Key, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  const SortTag<LaneType> d;
  Sort(d, st, reinterpret_cast<LaneType*>(keys), num_keys * st.LanesPerKey());
#else
  (void)keys;
  (void)num_keys;
  HWY_ASSERT(0);
#endif  // VQSORT_ENABLED
}

template <typename Key, class Order>
void VQPartialSortStatic(Key* HWY_RESTRICT keys, const size_t num_keys,
                         const size_t k_keys, Order) {
#if VQSORT_ENABLED
  const detail::MakeTraits<Key, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  const SortTag<LaneType> d;
  PartialSort(d, st, reinterpret_cast<LaneType*>(keys),
              num_keys * st.LanesPerKey(), k_keys * st.LanesPerKey());
#else
  (void)keys;
  (void)num_keys;
  (void)k_keys;
  HWY_ASSERT(0);
#endif  // VQSORT_ENABLED
}

template <typename Key, class Order>
void VQSelectStatic(Key* HWY_RESTRICT keys, const size_t num_keys,
                    const size_t k_keys, Order) {
#if VQSORT_ENABLED
  const detail::MakeTraits<Key, Order> st;
  using LaneType = typename decltype(st)::LaneType;
  const SortTag<LaneType> d;
  Select(d, st, reinterpret_cast<LaneType*>(keys), num_keys * st.LanesPerKey(),
         k_keys * st.LanesPerKey());
#else
  (void)keys;
  (void)num_keys;
  (void)k_keys;
  HWY_ASSERT(0);
#endif  // VQSORT_ENABLED
}

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_SORT_VQSORT_TOGGLE
