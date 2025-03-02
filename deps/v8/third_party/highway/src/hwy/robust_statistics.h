// Copyright 2023 Google LLC
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

#ifndef HIGHWAY_HWY_ROBUST_STATISTICS_H_
#define HIGHWAY_HWY_ROBUST_STATISTICS_H_

#include <algorithm>  // std::sort, std::find_if
#include <limits>
#include <utility>  // std::pair
#include <vector>

#include "hwy/base.h"

namespace hwy {
namespace robust_statistics {

// Sorts integral values in ascending order (e.g. for Mode). About 3x faster
// than std::sort for input distributions with very few unique values.
template <class T>
void CountingSort(T* values, size_t num_values) {
  // Unique values and their frequency (similar to flat_map).
  using Unique = std::pair<T, int>;
  std::vector<Unique> unique;
  for (size_t i = 0; i < num_values; ++i) {
    const T value = values[i];
    const auto pos =
        std::find_if(unique.begin(), unique.end(),
                     [value](const Unique u) { return u.first == value; });
    if (pos == unique.end()) {
      unique.push_back(std::make_pair(value, 1));
    } else {
      ++pos->second;
    }
  }

  // Sort in ascending order of value (pair.first).
  std::sort(unique.begin(), unique.end());

  // Write that many copies of each unique value to the array.
  T* HWY_RESTRICT p = values;
  for (const auto& value_count : unique) {
    std::fill(p, p + value_count.second, value_count.first);
    p += value_count.second;
  }
  HWY_ASSERT(p == values + num_values);
}

// @return i in [idx_begin, idx_begin + half_count) that minimizes
// sorted[i + half_count] - sorted[i].
template <typename T>
size_t MinRange(const T* const HWY_RESTRICT sorted, const size_t idx_begin,
                const size_t half_count) {
  T min_range = std::numeric_limits<T>::max();
  size_t min_idx = 0;

  for (size_t idx = idx_begin; idx < idx_begin + half_count; ++idx) {
    HWY_ASSERT(sorted[idx] <= sorted[idx + half_count]);
    const T range = sorted[idx + half_count] - sorted[idx];
    if (range < min_range) {
      min_range = range;
      min_idx = idx;
    }
  }

  return min_idx;
}

// Returns an estimate of the mode by calling MinRange on successively
// halved intervals. "sorted" must be in ascending order. This is the
// Half Sample Mode estimator proposed by Bickel in "On a fast, robust
// estimator of the mode", with complexity O(N log N). The mode is less
// affected by outliers in highly-skewed distributions than the median.
// The averaging operation below assumes "T" is an unsigned integer type.
template <typename T>
T ModeOfSorted(const T* const HWY_RESTRICT sorted, const size_t num_values) {
  size_t idx_begin = 0;
  size_t half_count = num_values / 2;
  while (half_count > 1) {
    idx_begin = MinRange(sorted, idx_begin, half_count);
    half_count >>= 1;
  }

  const T x = sorted[idx_begin + 0];
  if (half_count == 0) {
    return x;
  }
  HWY_ASSERT(half_count == 1);
  const T average = (x + sorted[idx_begin + 1] + 1) / 2;
  return average;
}

// Returns the mode. Side effect: sorts "values".
template <typename T>
T Mode(T* values, const size_t num_values) {
  CountingSort(values, num_values);
  return ModeOfSorted(values, num_values);
}

template <typename T, size_t N>
T Mode(T (&values)[N]) {
  return Mode(&values[0], N);
}

// Returns the median value. Side effect: sorts "values".
template <typename T>
T Median(T* values, const size_t num_values) {
  HWY_ASSERT(num_values != 0);
  std::sort(values, values + num_values);
  const size_t half = num_values / 2;
  // Odd count: return middle
  if (num_values % 2) {
    return values[half];
  }
  // Even count: return average of middle two.
  return (values[half] + values[half - 1] + 1) / 2;
}

// Returns a robust measure of variability.
template <typename T>
T MedianAbsoluteDeviation(const T* values, const size_t num_values,
                          const T median) {
  HWY_ASSERT(num_values != 0);
  std::vector<T> abs_deviations;
  abs_deviations.reserve(num_values);
  for (size_t i = 0; i < num_values; ++i) {
    const int64_t abs = ScalarAbs(static_cast<int64_t>(values[i]) -
                                  static_cast<int64_t>(median));
    abs_deviations.push_back(static_cast<T>(abs));
  }
  return Median(abs_deviations.data(), num_values);
}

}  // namespace robust_statistics
}  // namespace hwy

#endif  // HIGHWAY_HWY_ROBUST_STATISTICS_H_
