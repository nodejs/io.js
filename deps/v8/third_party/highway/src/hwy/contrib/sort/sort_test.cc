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

#include <stdint.h>
#include <stdio.h>

#include <numeric>  // std::iota
#include <random>
#include <vector>

#include "hwy/aligned_allocator.h"  // IsAligned
#include "hwy/base.h"
#include "hwy/contrib/sort/vqsort.h"
#include "hwy/contrib/thread_pool/thread_pool.h"
#include "hwy/contrib/thread_pool/topology.h"
#include "hwy/per_target.h"

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/sort/sort_test.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep
#include "hwy/highway.h"
// After highway.h
#include "hwy/contrib/sort/algo-inl.h"
#include "hwy/contrib/sort/result-inl.h"
#include "hwy/contrib/sort/vqsort-inl.h"  // BaseCase
#include "hwy/print-inl.h"
#include "hwy/tests/test_util-inl.h"

// TODO(b/314758657): Compiler bug causes incorrect results on SSE2/S-SSE3.
#undef VQSORT_SKIP
#if !defined(VQSORT_DO_NOT_SKIP) && HWY_COMPILER_CLANG && HWY_ARCH_X86 && \
    HWY_TARGET >= HWY_SSSE3
#define VQSORT_SKIP 1
#else
#define VQSORT_SKIP 0
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {
namespace {

using detail::OrderAscending;
using detail::OrderAscendingKV64;
using detail::OrderDescendingKV64;
using detail::SharedTraits;
using detail::TraitsLane;

#if !HAVE_INTEL && HWY_TARGET != HWY_SCALAR
using detail::OrderAscending128;
using detail::OrderAscendingKV128;
using detail::OrderDescending128;
using detail::OrderDescendingKV128;
using detail::Traits128;
#endif  // !HAVE_INTEL && HWY_TARGET != HWY_SCALAR

template <typename Key>
void TestSortIota(hwy::ThreadPool& pool) {
  pool.Run(128, 300, [](uint64_t task, size_t /*thread*/) {
    const size_t num = static_cast<size_t>(task);
    Key keys[300];
    std::iota(keys, keys + num, Key{0});
    VQSort(keys, num, hwy::SortAscending());
    for (size_t i = 0; i < num; ++i) {
      if (keys[i] != static_cast<Key>(i)) {
        HWY_ABORT("num %zu i %zu: not iota, got %.0f\n", num, i,
                  static_cast<double>(keys[i]));
      }
    }
  });
}

void TestAllSortIota() {
#if VQSORT_ENABLED
  hwy::ThreadPool pool(hwy::HaveThreadingSupport() ? 4 : 0);
  TestSortIota<uint32_t>(pool);
  TestSortIota<int32_t>(pool);
  if (hwy::HaveInteger64()) {
    TestSortIota<int64_t>(pool);
    TestSortIota<uint64_t>(pool);
  }
  TestSortIota<float>(pool);
  if (hwy::HaveFloat64()) {
    TestSortIota<double>(pool);
  }
  fprintf(stderr, "Iota OK\n");
#endif
}

// Supports full/partial sort and select.
template <class Traits>
void TestAnySort(const std::vector<Algo>& algos, size_t num_lanes) {
// Workaround for stack overflow on clang-cl (/F 8388608 does not help).
#if defined(_MSC_VER)
  return;
#endif
  using Order = typename Traits::Order;
  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  SharedState shared;
  SharedTraits<Traits> st;

  constexpr size_t kLPK = st.LanesPerKey();
  num_lanes = hwy::RoundUpTo(num_lanes, kLPK);
  const size_t num_keys = num_lanes / kLPK;

  std::mt19937 rng(42);
  std::uniform_int_distribution<size_t> k_dist(1, num_keys - 1);

  constexpr size_t kMaxMisalign = 16;
  auto aligned =
      hwy::AllocateAligned<LaneType>(kMaxMisalign + num_lanes + kMaxMisalign);
  HWY_ASSERT(aligned);

  for (Algo algo : algos) {
    if (IsVQ(algo) && (!VQSORT_ENABLED || VQSORT_SKIP)) continue;

    for (Dist dist : AllDist()) {
      for (size_t misalign :
           {size_t{0}, size_t{kLPK}, size_t{3 * kLPK}, kMaxMisalign / 2}) {
        for (size_t k_rep = 0; k_rep < AdjustedReps(10); ++k_rep) {
          // Skip reps for full sort because they do not use k.
          if (!IsPartialSort(algo) && !IsSelect(algo) && k_rep > 0) break;

          LaneType* lanes = aligned.get() + misalign;
          HWY_ASSERT(hwy::IsAligned(lanes, sizeof(KeyType)));
          KeyType* keys = HWY_RCAST_ALIGNED(KeyType*, lanes);

          // Set up red zones before/after the keys to sort
          for (size_t i = 0; i < misalign; ++i) {
            aligned[i] = hwy::LowestValue<LaneType>();
          }
          for (size_t i = 0; i < kMaxMisalign; ++i) {
            lanes[num_lanes + i] = hwy::HighestValue<LaneType>();
          }
          detail::MaybePoison(aligned.get(), misalign * sizeof(LaneType));
          detail::MaybePoison(lanes + num_lanes,
                              kMaxMisalign * sizeof(LaneType));

          InputStats<LaneType> input_stats =
              GenerateInput(dist, lanes, num_lanes);
          ReferenceSortVerifier<Traits> reference_verifier(lanes, num_lanes);
          const size_t k_keys = k_dist(rng);
          Run(algo, keys, num_keys, shared, /*thread=*/0, k_keys, Order());
          reference_verifier(algo, lanes, k_keys);
          SortOrderVerifier<Traits>()(algo, input_stats, lanes, num_keys,
                                      k_keys);

          // Check red zones
          detail::MaybeUnpoison(aligned.get(), misalign);
          detail::MaybeUnpoison(lanes + num_lanes, kMaxMisalign);
          for (size_t i = 0; i < misalign; ++i) {
            if (aligned[i] != hwy::LowestValue<LaneType>())
              HWY_ABORT("Overrun left at %d\n", static_cast<int>(i));
          }
          for (size_t i = num_lanes; i < num_lanes + kMaxMisalign; ++i) {
            if (lanes[i] != hwy::HighestValue<LaneType>())
              HWY_ABORT("Overrun right at %d\n", static_cast<int>(i));
          }
        }  // k_rep
      }  // misalign
    }  // dist
  }  // algo
}

// Calls TestAnySort with all traits.
void CallAllSortTraits(const std::vector<Algo>& algos, size_t num_lanes) {
#if !HAVE_INTEL
  TestAnySort<TraitsLane<OrderAscending<int16_t>>>(algos, num_lanes);
  TestAnySort<TraitsLane<OtherOrder<uint16_t>>>(algos, num_lanes);
#endif

  TestAnySort<TraitsLane<OtherOrder<int32_t>>>(algos, num_lanes);
  TestAnySort<TraitsLane<OtherOrder<uint32_t>>>(algos, num_lanes);

  TestAnySort<TraitsLane<OrderAscending<int64_t>>>(algos, num_lanes);
  TestAnySort<TraitsLane<OrderAscending<uint64_t>>>(algos, num_lanes);

  // WARNING: for float types, SIMD comparisons will flush denormals to
  // zero, causing mismatches with scalar sorts. In this test, we avoid
  // generating denormal inputs.
#if HWY_HAVE_FLOAT16  // #if protects algo-inl.h's GenerateRandom
  // Must also check whether the dynamic-dispatch target supports float16_t!
  if (hwy::HaveFloat16()) {
    TestAnySort<TraitsLane<OrderAscending<float16_t>>>(algos, num_lanes);
  }
#endif
  TestAnySort<TraitsLane<OrderAscending<float>>>(algos, num_lanes);
#if HWY_HAVE_FLOAT64  // #if protects algo-inl.h's GenerateRandom
  // Must also check whether the dynamic-dispatch target supports float64!
  if (hwy::HaveFloat64()) {
    TestAnySort<TraitsLane<OtherOrder<double>>>(algos, num_lanes);
  }
#endif

  // Other algorithms do not support 128-bit nor KV keys.
#if !HAVE_VXSORT && !HAVE_INTEL
  TestAnySort<TraitsLane<OrderAscendingKV64>>(algos, num_lanes);
  TestAnySort<TraitsLane<OrderDescendingKV64>>(algos, num_lanes);

// 128-bit keys require 128-bit SIMD.
#if HWY_TARGET != HWY_SCALAR
  TestAnySort<Traits128<OrderAscending128>>(algos, num_lanes);
  TestAnySort<Traits128<OrderDescending128>>(algos, num_lanes);

  TestAnySort<Traits128<OrderAscendingKV128>>(algos, num_lanes);
  TestAnySort<Traits128<OrderDescendingKV128>>(algos, num_lanes);
#endif  // HWY_TARGET != HWY_SCALAR
#endif  // !HAVE_VXSORT && !HAVE_INTEL
}

void TestAllSort() {
  const std::vector<Algo> algos{
#if HAVE_AVX2SORT
      Algo::kSEA,
#endif
#if HAVE_IPS4O
      Algo::kIPS4O,
#endif
#if HAVE_PDQSORT
      Algo::kPDQ,
#endif
#if HAVE_SORT512
      Algo::kSort512,
#endif
      Algo::kVQSort,  Algo::kHeapSort,
  };

  for (int num : {129, 504, 3 * 1000, 34567}) {
    const size_t num_lanes = AdjustedReps(static_cast<size_t>(num));
    CallAllSortTraits(algos, num_lanes);
  }
}

void TestAllPartialSort() {
  const std::vector<Algo> algos{Algo::kVQPartialSort, Algo::kHeapPartialSort};

  for (int num : {129, 504, 3 * 1000, 34567}) {
    const size_t num_lanes = AdjustedReps(static_cast<size_t>(num));
    CallAllSortTraits(algos, num_lanes);
  }
}

void TestAllSelect() {
  const std::vector<Algo> algos{Algo::kVQSelect, Algo::kHeapSelect};

  for (int num : {129, 504, 3 * 1000, 34567}) {
    const size_t num_lanes = AdjustedReps(static_cast<size_t>(num));
    CallAllSortTraits(algos, num_lanes);
  }
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace hwy {
namespace {
HWY_BEFORE_TEST(SortTest);
HWY_EXPORT_AND_TEST_P(SortTest, TestAllSortIota);
HWY_EXPORT_AND_TEST_P(SortTest, TestAllSort);
HWY_EXPORT_AND_TEST_P(SortTest, TestAllSelect);
HWY_EXPORT_AND_TEST_P(SortTest, TestAllPartialSort);
HWY_AFTER_TEST();
}  // namespace
}  // namespace hwy
HWY_TEST_MAIN();
#endif  // HWY_ONCE
