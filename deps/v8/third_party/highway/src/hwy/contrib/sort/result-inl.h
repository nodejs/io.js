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

#include "hwy/contrib/sort/algo-inl.h"

// Normal include guard for non-SIMD parts
#ifndef HIGHWAY_HWY_CONTRIB_SORT_RESULT_INL_H_
#define HIGHWAY_HWY_CONTRIB_SORT_RESULT_INL_H_

#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include <algorithm>  // std::sort
#include <string>
#include <vector>

#include "hwy/aligned_allocator.h"
#include "hwy/base.h"
#include "hwy/contrib/sort/order.h"
#include "hwy/per_target.h"  // DispatchedTarget
#include "hwy/targets.h"     // TargetName

namespace hwy {

// Returns trimmed mean (we don't want to run an out-of-L3-cache sort often
// enough for the mode to be reliable).
static inline double SummarizeMeasurements(std::vector<double>& seconds) {
  std::sort(seconds.begin(), seconds.end());
  double sum = 0;
  int count = 0;
  const size_t num = seconds.size();
  for (size_t i = num / 4; i < num / 2; ++i) {
    sum += seconds[i];
    count += 1;
  }
  return sum / count;
}

struct SortResult {
  SortResult() {}
  SortResult(const Algo algo, Dist dist, size_t num_keys, size_t num_threads,
             double sec, size_t sizeof_key, const char* key_name)
      : target(DispatchedTarget()),
        algo(algo),
        dist(dist),
        num_keys(num_keys),
        num_threads(num_threads),
        sec(sec),
        sizeof_key(sizeof_key),
        key_name(key_name) {}

  void Print() const {
    const double bytes = static_cast<double>(num_keys) *
                         static_cast<double>(num_threads) *
                         static_cast<double>(sizeof_key);
    printf("%10s: %12s: %7s: %9s: %05g %4.0f MB/s (%2zu threads)\n",
           hwy::TargetName(target), AlgoName(algo), key_name.c_str(),
           DistName(dist), static_cast<double>(num_keys), bytes * 1E-6 / sec,
           num_threads);
  }

  int64_t target;
  Algo algo;
  Dist dist;
  size_t num_keys = 0;
  size_t num_threads = 0;
  double sec = 0.0;
  size_t sizeof_key = 0;
  std::string key_name;
};

}  // namespace hwy
#endif  // HIGHWAY_HWY_CONTRIB_SORT_RESULT_INL_H_

// Per-target
#if defined(HIGHWAY_HWY_CONTRIB_SORT_RESULT_TOGGLE) == \
    defined(HWY_TARGET_TOGGLE)
#ifdef HIGHWAY_HWY_CONTRIB_SORT_RESULT_TOGGLE
#undef HIGHWAY_HWY_CONTRIB_SORT_RESULT_TOGGLE
#else
#define HIGHWAY_HWY_CONTRIB_SORT_RESULT_TOGGLE
#endif

HWY_BEFORE_NAMESPACE();
namespace hwy {
namespace HWY_NAMESPACE {

// Copies the input, and compares results to that of a reference algorithm.
template <class Traits>
class ReferenceSortVerifier {
  using LaneType = typename Traits::LaneType;
  using KeyType = typename Traits::KeyType;
  using Order = typename Traits::Order;
  static constexpr bool kAscending = Order::IsAscending();
  static constexpr size_t kLPK = Traits().LanesPerKey();

 public:
  ReferenceSortVerifier(const LaneType* in_lanes, size_t num_lanes) {
    num_lanes_ = num_lanes;
    num_keys_ = num_lanes / kLPK;
    in_lanes_ = hwy::AllocateAligned<LaneType>(num_lanes);
    HWY_ASSERT(in_lanes_);
    CopyBytes(in_lanes, in_lanes_.get(), num_lanes * sizeof(LaneType));
  }

  // For full sorts, k_keys == num_keys.
  void operator()(Algo algo, const LaneType* out_lanes, size_t k_keys) {
    SharedState shared;
    const Traits st;
    const CappedTag<LaneType, kLPK> d;

    HWY_ASSERT(hwy::IsAligned(in_lanes_.get(), sizeof(KeyType)));
    KeyType* in_keys = HWY_RCAST_ALIGNED(KeyType*, in_lanes_.get());

    char caption[10];
    const char* algo_type = IsPartialSort(algo) ? "PartialSort" : "Sort";

    HWY_ASSERT(k_keys <= num_keys_);
    Run(ReferenceAlgoFor(algo), in_keys, num_keys_, shared, /*thread=*/0,
        k_keys, Order());

    if (IsSelect(algo)) {
      // Print lanes centered around k_keys.
      if (VQSORT_PRINT >= 3) {
        const size_t begin_lane = k_keys < 3 ? 0 : (k_keys - 3) * kLPK;
        const size_t end_lane = HWY_MIN(num_lanes_, (k_keys + 3) * kLPK);
        fprintf(stderr, "\nExpected:\n");
        for (size_t i = begin_lane; i < end_lane; i += kLPK) {
          snprintf(caption, sizeof(caption), "%4zu ", i / kLPK);
          Print(d, caption, st.SetKey(d, &in_lanes_[i]));
        }
        fprintf(stderr, "\n\nActual:\n");
        for (size_t i = begin_lane; i < end_lane; i += kLPK) {
          snprintf(caption, sizeof(caption), "%4zu ", i / kLPK);
          Print(d, caption, st.SetKey(d, &out_lanes[i]));
        }
        fprintf(stderr, "\n\n");
      }

      // At k_keys: should be equivalent, i.e. neither a < b nor b < a.
      // SortOrderVerifier will also check the ordering of the rest of the keys.
      const size_t k = k_keys * kLPK;
      if (st.Compare1(&in_lanes_[k], &out_lanes[k]) ||
          st.Compare1(&out_lanes[k], &in_lanes_[k])) {
        Print(d, "Expected", st.SetKey(d, &in_lanes_[k]));
        Print(d, "  Actual", st.SetKey(d, &out_lanes[k]));
        HWY_ABORT("Select %s asc=%d: mismatch at k_keys=%zu, num_keys=%zu\n",
                  st.KeyString(), kAscending, k_keys, num_keys_);
      }
    } else {
      if (VQSORT_PRINT >= 3) {
        const size_t lanes_to_print = HWY_MIN(40, k_keys * kLPK);
        fprintf(stderr, "\nExpected:\n");
        for (size_t i = 0; i < lanes_to_print; i += kLPK) {
          snprintf(caption, sizeof(caption), "%4zu ", i / kLPK);
          Print(d, caption, st.SetKey(d, &in_lanes_[i]));
        }
        fprintf(stderr, "\n\nActual:\n");
        for (size_t i = 0; i < lanes_to_print; i += kLPK) {
          snprintf(caption, sizeof(caption), "%4zu ", i / kLPK);
          Print(d, caption, st.SetKey(d, &out_lanes[i]));
        }
        fprintf(stderr, "\n\n");
      }

      // Full or partial sort: all elements up to k_keys are equivalent to the
      // reference sort. SortOrderVerifier also checks the output's ordering.
      for (size_t i = 0; i < k_keys * kLPK; i += kLPK) {
        // All up to k_keys should be equivalent, i.e. neither a < b nor b < a.
        if (st.Compare1(&in_lanes_[i], &out_lanes[i]) ||
            st.Compare1(&out_lanes[i], &in_lanes_[i])) {
          Print(d, "Expected", st.SetKey(d, &in_lanes_[i]));
          Print(d, "  Actual", st.SetKey(d, &out_lanes[i]));
          HWY_ABORT("%s %s asc=%d: mismatch at %zu, k_keys=%zu, num_keys=%zu\n",
                    algo_type, st.KeyString(), kAscending, i / kLPK, k_keys,
                    num_keys_);
        }
      }
    }
  }

 private:
  hwy::AlignedFreeUniquePtr<LaneType[]> in_lanes_;
  size_t num_lanes_;
  size_t num_keys_;
};

// Faster than ReferenceSortVerifier, for use in bench_sort. Only verifies
// order, without running a slow reference sorter. This means it can't verify
// Select places the correct key at `k_keys`, nor that input and output keys are
// the same.
template <class Traits>
class SortOrderVerifier {
  using LaneType = typename Traits::LaneType;
  using Order = typename Traits::Order;
  static constexpr bool kAscending = Order::IsAscending();
  static constexpr size_t kLPK = Traits().LanesPerKey();

 public:
  void operator()(Algo algo, const InputStats<LaneType>& input_stats,
                  const LaneType* output, size_t num_keys, size_t k_keys) {
    if (IsSelect(algo)) {
      CheckSelectOrder(input_stats, output, num_keys, k_keys);
    } else {
      CheckSortedOrder(algo, input_stats, output, num_keys, k_keys);
    }
  }

 private:
  // For full or partial sorts: ensures keys are in sorted order.
  void CheckSortedOrder(const Algo algo,
                        const InputStats<LaneType>& input_stats,
                        const LaneType* output, const size_t num_keys,
                        const size_t k_keys) {
    const Traits st;
    const CappedTag<LaneType, kLPK> d;
    const size_t num_lanes = num_keys * kLPK;
    const size_t k = k_keys * kLPK;
    const char* algo_type = IsPartialSort(algo) ? "PartialSort" : "Sort";

    InputStats<LaneType> output_stats;
    // Even for partial sorts, loop over all keys to verify none disappeared.
    for (size_t i = 0; i < num_lanes - kLPK; i += kLPK) {
      output_stats.Notify(output[i]);
      if (kLPK == 2) output_stats.Notify(output[i + 1]);

      // Only check the first k_keys (== num_keys for a full sort).
      // Reverse order instead of checking !Compare1 so we accept equal keys.
      if (i < k - kLPK && st.Compare1(output + i + kLPK, output + i)) {
        Print(d, " cur", st.SetKey(d, &output[i]));
        Print(d, "next", st.SetKey(d, &output[i + kLPK]));
        HWY_ABORT(
            "%s %s asc=%d: wrong order at %zu, k_keys=%zu, num_keys=%zu\n",
            algo_type, st.KeyString(), kAscending, i / kLPK, k_keys, num_keys);
      }
    }
    output_stats.Notify(output[num_lanes - kLPK]);
    if (kLPK == 2) output_stats.Notify(output[num_lanes - kLPK + 1]);

    HWY_ASSERT(input_stats == output_stats);
  }

  // Ensures keys below index k_keys are less, and all above are greater.
  void CheckSelectOrder(const InputStats<LaneType>& input_stats,
                        const LaneType* output, const size_t num_keys,
                        const size_t k_keys) {
    const Traits st;
    const CappedTag<LaneType, kLPK> d;
    const size_t num_lanes = num_keys * kLPK;
    const size_t k = k_keys * kLPK;

    InputStats<LaneType> output_stats;
    for (size_t i = 0; i < num_lanes - kLPK; i += kLPK) {
      output_stats.Notify(output[i]);
      if (kLPK == 2) output_stats.Notify(output[i + 1]);
      // Reverse order instead of checking !Compare1 so we accept equal keys.
      if (i < k ? st.Compare1(output + k, output + i)
                : st.Compare1(output + i, output + k)) {
        Print(d, "cur", st.SetKey(d, &output[i]));
        Print(d, "kth", st.SetKey(d, &output[k]));
        HWY_ABORT(
            "Select %s asc=%d: wrong order at %zu, k_keys=%zu, num_keys=%zu\n",
            st.KeyString(), kAscending, i / kLPK, k_keys, num_keys);
      }
    }
    output_stats.Notify(output[num_lanes - kLPK]);
    if (kLPK == 2) output_stats.Notify(output[num_lanes - kLPK + 1]);

    HWY_ASSERT(input_stats == output_stats);
  }
};

// NOLINTNEXTLINE(google-readability-namespace-comments)
}  // namespace HWY_NAMESPACE
}  // namespace hwy
HWY_AFTER_NAMESPACE();

#endif  // HIGHWAY_HWY_CONTRIB_SORT_RESULT_TOGGLE
