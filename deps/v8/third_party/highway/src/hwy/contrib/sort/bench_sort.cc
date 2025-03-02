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

#include <vector>

// clang-format off
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "hwy/contrib/sort/bench_sort.cc"
#include "hwy/foreach_target.h"  // IWYU pragma: keep

// After foreach_target
#include "hwy/contrib/sort/algo-inl.h"
#include "hwy/contrib/sort/vqsort.h"
#include "hwy/contrib/sort/result-inl.h"
#include "hwy/contrib/sort/sorting_networks-inl.h"  // SharedTraits
#include "hwy/contrib/sort/traits-inl.h"
#include "hwy/contrib/sort/traits128-inl.h"
#include "hwy/tests/test_util-inl.h"
#include "hwy/timer-inl.h"
#include "hwy/nanobenchmark.h"
#include "hwy/timer.h"
#include "hwy/per_target.h"
// clang-format on

#if HWY_OS_LINUX
#include <unistd.h>  // usleep
#endif

// Mode for larger sorts because M1 is able to access more than the per-core
// share of L2, so 1M elements might still be in cache.
#define SORT_100M 0

#ifndef SORT_ONLY_COLD
#define SORT_ONLY_COLD 0
#endif
#ifndef SORT_BENCH_BASE_AND_PARTITION
#define SORT_BENCH_BASE_AND_PARTITION (!SORT_ONLY_COLD && 0)
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
// Defined within HWY_ONCE, used by BenchAllSort.
extern int64_t first_sort_target;
extern int64_t first_cold_target;  // for BenchAllColdSort

namespace HWY_NAMESPACE {
namespace {
using detail::OrderAscending;
using detail::OrderDescending;
using detail::SharedTraits;
using detail::TraitsLane;

#if HWY_TARGET != HWY_SCALAR
using detail::OrderAscending128;
using detail::OrderAscendingKV128;
using detail::Traits128;
#endif  // HWY_TARGET != HWY_SCALAR

HWY_NOINLINE void BenchAllColdSort() {
  // Only run the best(first) enabled target
  if (first_cold_target == 0) first_cold_target = HWY_TARGET;
  if (HWY_TARGET != first_cold_target) {
    return;
  }

  char cpu100[100];
  if (!platform::HaveTimerStop(cpu100)) {
    fprintf(stderr, "CPU '%s' does not support RDTSCP, skipping benchmark.\n",
            cpu100);
    return;
  }

  // Initialize random seeds
#if VQSORT_ENABLED
  HWY_ASSERT(GetGeneratorState() != nullptr);  // vqsort
#endif
  RandomState rng(static_cast<uint64_t>(Unpredictable1() * 129));  // this test

  using T = uint64_t;
  constexpr size_t kSize = 10 * 1000;
  HWY_ALIGN T items[kSize];

  // Initialize array
#if 0  // optional: deliberate AVX-512 to verify VQSort performance improves
  const ScalableTag<T> d;
  const RebindToSigned<decltype(d)> di;
  const size_t N = Lanes(d);
  size_t i = 0;
  for (; i + N <= kSize; i += N) {
    // Super-slow scatter so that we spend enough time to warm up SKX.
    const Vec<decltype(d)> val = Set(d, static_cast<T>(Unpredictable1()));
    const Vec<decltype(di)> idx =
        Iota(di, static_cast<T>(Unpredictable1() - 1));
    ScatterIndex(val, d, items + i, idx);
  }
  for (; i < kSize; ++i) {
    items[i] = static_cast<T>(Unpredictable1());
  }
#else  // scalar-only, verified with clang-16
  for (size_t i = 0; i < kSize; ++i) {
    items[i] = static_cast<T>(Unpredictable1());
  }
#endif
  items[Random32(&rng) % kSize] = static_cast<T>(Unpredictable1() + 1);

  const timer::Ticks t0 = timer::Start();
  const SortAscending order;
#if VQSORT_ENABLED && 1  // change to && 0 to switch to std::sort.
  VQSort(items, kSize, order);
#else
  SharedState shared;
  Run(Algo::kStdSort, items, kSize, shared, /*thread=*/0, /*k_keys=*/0, order);
#endif
  const timer::Ticks t1 = timer::Stop();

  const double ticks = static_cast<double>(t1 - t0);
  const double elapsed = ticks / platform::InvariantTicksPerSecond();
  const double GBps = kSize * sizeof(T) * 1E-9 / elapsed;

  fprintf(stderr, "N=%zu GB/s=%.2f ns=%.1f random output: %g\n", kSize, GBps,
          elapsed * 1E9, static_cast<double>(items[Random32(&rng) % kSize]));

#if SORT_ONLY_COLD
#if HWY_OS_LINUX
  // Long enough for the CPU to switch off AVX-512 mode before the next run.
  usleep(100 * 1000);  // NOLINT
#endif
#endif
}

#if (VQSORT_ENABLED && SORT_BENCH_BASE_AND_PARTITION) || HWY_IDE

template <class Traits>
HWY_NOINLINE void BenchPartition() {
  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  const SortTag<LaneType> d;
  detail::SharedTraits<Traits> st;
  const Dist dist = Dist::kUniform8;
  double sum = 0.0;

  constexpr size_t kLPK = st.LanesPerKey();
  HWY_ALIGN LaneType
      buf[SortConstants::BufBytes<LaneType, kLPK>(HWY_MAX_BYTES) /
          sizeof(LaneType)];
  uint64_t* HWY_RESTRICT state = GetGeneratorState();

  const size_t max_log2 = AdjustedLog2Reps(20);
  for (size_t log2 = max_log2; log2 < max_log2 + 1; ++log2) {
    const size_t num_lanes = 1ull << log2;
    const size_t num_keys = num_lanes / kLPK;
    auto aligned = hwy::AllocateAligned<LaneType>(num_lanes);

    std::vector<double> seconds;
    const size_t num_reps = (1ull << (14 - log2 / 2)) * 30;
    for (size_t rep = 0; rep < num_reps; ++rep) {
      (void)GenerateInput(dist, aligned.get(), num_lanes);

      // The pivot value can influence performance. Do exactly what vqsort will
      // do so that the performance (influenced by prefetching and branch
      // prediction) is likely to predict the actual performance inside vqsort.
      detail::DrawSamples(d, st, aligned.get(), num_lanes, buf, state);
      detail::SortSamples(d, st, buf);
      auto pivot = detail::ChoosePivotByRank(d, st, buf);

      const Timestamp t0;
      detail::Partition(d, st, aligned.get(), num_lanes - 1, pivot, buf);
      seconds.push_back(SecondsSince(t0));
      // 'Use' the result to prevent optimizing out the partition.
      sum += static_cast<double>(aligned.get()[num_lanes / 2]);
    }

    SortResult(Algo::kVQSort, dist, num_keys, 1, SummarizeMeasurements(seconds),
               sizeof(KeyType), st.KeyString())
        .Print();
  }
  HWY_ASSERT(sum != 999999);  // Prevent optimizing out
}

HWY_NOINLINE void BenchAllPartition() {
  // Not interested in benchmark results for these targets
  if (HWY_TARGET == HWY_SSSE3) {
    return;
  }

  BenchPartition<TraitsLane<OrderDescending<float>>>();
  BenchPartition<TraitsLane<OrderDescending<int32_t>>>();
  BenchPartition<TraitsLane<OrderDescending<int64_t>>>();
  BenchPartition<Traits128<OrderAscending128>>();
  // BenchPartition<Traits128<OrderDescending128>>();
  BenchPartition<Traits128<OrderAscendingKV128>>();
}

template <class Traits>
HWY_NOINLINE void BenchBase(std::vector<SortResult>& results) {
  // Not interested in benchmark results for these targets
  if (HWY_TARGET == HWY_SSSE3 || HWY_TARGET == HWY_SSE4) {
    return;
  }

  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  const SortTag<LaneType> d;
  detail::SharedTraits<Traits> st;
  const Dist dist = Dist::kUniform32;
  const Algo algo = Algo::kVQSort;

  const size_t N = Lanes(d);
  constexpr size_t kLPK = st.LanesPerKey();
  const size_t num_lanes = SortConstants::BaseCaseNumLanes<kLPK>(N);
  const size_t num_keys = num_lanes / kLPK;
  auto keys = hwy::AllocateAligned<LaneType>(num_lanes);
  auto buf = hwy::AllocateAligned<LaneType>(num_lanes + N);

  std::vector<double> seconds;
  double sum = 0;                             // prevents elision
  constexpr size_t kMul = AdjustedReps(600);  // ensures long enough to measure

  for (size_t rep = 0; rep < 30; ++rep) {
    InputStats<LaneType> input_stats =
        GenerateInput(dist, keys.get(), num_lanes);

    const Timestamp t0;
    for (size_t i = 0; i < kMul; ++i) {
      detail::BaseCase(d, st, keys.get(), num_lanes, buf.get());
      sum += static_cast<double>(keys[0]);
    }
    seconds.push_back(SecondsSince(t0));
    // printf("%f\n", seconds.back());

    SortOrderVerifier<Traits>()(algo, input_stats, keys.get(), num_keys,
                                num_keys);
  }
  HWY_ASSERT(sum < 1E99);
  results.emplace_back(algo, dist, num_keys * kMul, 1,
                       SummarizeMeasurements(seconds), sizeof(KeyType),
                       st.KeyString());
}

HWY_NOINLINE void BenchAllBase() {
  // Not interested in benchmark results for these targets
  if (HWY_TARGET == HWY_SSSE3) {
    return;
  }

  std::vector<SortResult> results;
  BenchBase<TraitsLane<OrderAscending<float>>>(results);
  BenchBase<TraitsLane<OrderDescending<int64_t>>>(results);
  BenchBase<Traits128<OrderAscending128>>(results);
  for (const SortResult& r : results) {
    r.Print();
  }
}

#endif  // VQSORT_ENABLED && SORT_BENCH_BASE_AND_PARTITION

std::vector<Algo> AlgoForBench() {
  return {
#if HAVE_AVX2SORT
    Algo::kSEA,
#endif
#if HAVE_PARALLEL_IPS4O
        Algo::kParallelIPS4O,
#elif HAVE_IPS4O
    Algo::kIPS4O,
#endif
#if HAVE_PDQSORT
        Algo::kPDQ,
#endif
#if HAVE_SORT512
        Algo::kSort512,
#endif
// Only include if we're compiling for the target it supports.
#if HAVE_VXSORT && ((VXSORT_AVX3 && HWY_TARGET == HWY_AVX3) || \
                    (!VXSORT_AVX3 && HWY_TARGET == HWY_AVX2))
        Algo::kVXSort,
#endif
// Only include if we're compiling for the target it supports.
#if HAVE_INTEL && HWY_TARGET <= HWY_AVX3
        Algo::kIntel,
#endif

#if !HAVE_PARALLEL_IPS4O
#if !SORT_100M
    // 10-20x slower, but that's OK for the default size when we are not
    // testing the parallel nor 100M modes.
    // Algo::kStdSort,
#endif

#if VQSORT_ENABLED
        Algo::kVQSort,
#endif
#endif  // !HAVE_PARALLEL_IPS4O
  };
}

template <class Traits>
HWY_NOINLINE void BenchSort(size_t num_keys) {
  if (first_sort_target == 0) first_sort_target = HWY_TARGET;

  SharedState shared;
  detail::SharedTraits<Traits> st;
  using Order = typename Traits::Order;
  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  const size_t num_lanes = num_keys * st.LanesPerKey();
  auto aligned = hwy::AllocateAligned<LaneType>(num_lanes);

  const size_t reps = num_keys > 1000 * 1000 ? 10 : 30;

  for (Algo algo : AlgoForBench()) {
    // Other algorithms don't depend on the vector instructions, so only run
    // them for the first target.
#if !HAVE_VXSORT
    if (algo != Algo::kVQSort && HWY_TARGET != first_sort_target) {
      continue;
    }
#endif

    for (Dist dist : AllDist()) {
      std::vector<double> seconds;
      for (size_t rep = 0; rep < reps; ++rep) {
        InputStats<LaneType> input_stats =
            GenerateInput(dist, aligned.get(), num_lanes);

        const Timestamp t0;
        Run(algo, HWY_RCAST_ALIGNED(KeyType*, aligned.get()), num_keys, shared,
            /*thread=*/0, /*k_keys=*/0, Order());
        seconds.push_back(SecondsSince(t0));
        // printf("%f\n", seconds.back());

        SortOrderVerifier<Traits>()(algo, input_stats, aligned.get(), num_keys,
                                    num_keys);
      }
      SortResult(algo, dist, num_keys, 1, SummarizeMeasurements(seconds),
                 sizeof(KeyType), st.KeyString())
          .Print();
    }  // dist
  }    // algo
}

enum class BenchmarkModes {
  kDefault,
  k1M,
  k10K,
  kAllSmall,
  kSmallPow2,
  kSmallPow2Between,  // includes padding
  kPow4,
  kPow10
};

std::vector<size_t> SizesToBenchmark(BenchmarkModes mode) {
  std::vector<size_t> sizes;
  switch (mode) {
    default:
    case BenchmarkModes::kDefault:
#if HAVE_PARALLEL_IPS4O || SORT_100M
      sizes.push_back(100 * 1000 * size_t{1000});
#else
      sizes.push_back(100);
      sizes.push_back(100 * 1000);
#endif
      break;
    case BenchmarkModes::k1M:
      sizes.push_back(1000 * 1000);
      break;
    case BenchmarkModes::k10K:
      sizes.push_back(10 * 1000);
      break;

    case BenchmarkModes::kAllSmall:
      sizes.reserve(128);
      for (size_t i = 1; i <= 128; ++i) {
        sizes.push_back(i);
      }
      break;
    case BenchmarkModes::kSmallPow2:
      for (size_t size = 2; size <= 128; size *= 2) {
        sizes.push_back(size);
      }
      break;
    case BenchmarkModes::kSmallPow2Between:
      for (size_t size = 2; size <= 128; size *= 2) {
        sizes.push_back(3 * size / 2);
      }
      break;

    case BenchmarkModes::kPow4:
      for (size_t size = 4; size <= 256 * 1024; size *= 4) {
        sizes.push_back(size);
      }
      break;
    case BenchmarkModes::kPow10:
      for (size_t size = 10; size <= 100 * 1000; size *= 10) {
        sizes.push_back(size);
      }
      break;
  }
  return sizes;
}

HWY_NOINLINE void BenchAllSort() {
  // Not interested in benchmark results for these targets. Note that SSE4 is
  // numerically less than SSE2, hence it is the lower bound.
  if (HWY_SSE4 <= HWY_TARGET && HWY_TARGET <= HWY_SSE2) {
    return;
  }
#if HAVE_INTEL
  if (HWY_TARGET > HWY_AVX3) return;
#endif

  for (size_t num_keys : SizesToBenchmark(BenchmarkModes::kSmallPow2)) {
#if !HAVE_INTEL
#if HWY_HAVE_FLOAT16
    if (hwy::HaveFloat16()) {
      BenchSort<TraitsLane<OtherOrder<float16_t>>>(num_keys);
    }
#endif
    BenchSort<TraitsLane<OrderAscending<float>>>(num_keys);
#if HWY_HAVE_FLOAT64
    if (hwy::HaveFloat64()) {
      // BenchSort<TraitsLane<OtherOrder<double>>>(num_keys);
    }
#endif
#endif  // !HAVE_INTEL
    // BenchSort<TraitsLane<OrderAscending<int16_t>>>(num_keys);
    BenchSort<TraitsLane<OtherOrder<int32_t>>>(num_keys);
    BenchSort<TraitsLane<OrderAscending<int64_t>>>(num_keys);
    // BenchSort<TraitsLane<OtherOrder<uint16_t>>>(num_keys);
    // BenchSort<TraitsLane<OtherOrder<uint32_t>>>(num_keys);
    // BenchSort<TraitsLane<OrderAscending<uint64_t>>>(num_keys);

#if !HAVE_VXSORT && !HAVE_INTEL && HWY_TARGET != HWY_SCALAR
    BenchSort<Traits128<OrderAscending128>>(num_keys);
    BenchSort<Traits128<OrderAscendingKV128>>(num_keys);
#endif
  }
}

}  // namespace
// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace hwy {
int64_t first_sort_target = 0;  // none run yet
int64_t first_cold_target = 0;  // none run yet
HWY_BEFORE_TEST(BenchSort);
HWY_EXPORT_AND_TEST_P(BenchSort, BenchAllColdSort);
#if SORT_BENCH_BASE_AND_PARTITION
HWY_EXPORT_AND_TEST_P(BenchSort, BenchAllPartition);
HWY_EXPORT_AND_TEST_P(BenchSort, BenchAllBase);
#endif

#if !SORT_ONLY_COLD  // skip (warms up vector unit for next run)
HWY_EXPORT_AND_TEST_P(BenchSort, BenchAllSort);
#endif
HWY_AFTER_TEST();
}  // namespace hwy

#endif  // HWY_ONCE
