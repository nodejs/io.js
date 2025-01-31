// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CRDTP_FIND_BY_FIRST_H_
#define CRDTP_FIND_BY_FIRST_H_

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

#include "export.h"
#include "span.h"

namespace crdtp {
// =============================================================================
// FindByFirst - Retrieval from a sorted vector that's keyed by span<uint8_t>.
// =============================================================================

// Given a vector of pairs sorted by the first element of each pair, find
// the corresponding value given a key to be compared to the first element.
// Together with std::inplace_merge and pre-sorting or std::sort, this can
// be used to implement a minimalistic equivalent of Chromium's flat_map.

// In this variant, the template parameter |T| is a value type and a
// |default_value| is provided.
template <typename T>
T FindByFirst(const std::vector<std::pair<span<uint8_t>, T>>& sorted_by_first,
              span<uint8_t> key,
              T default_value) {
  auto it = std::lower_bound(
      sorted_by_first.begin(), sorted_by_first.end(), key,
      [](const std::pair<span<uint8_t>, T>& left, span<uint8_t> right) {
        return SpanLessThan(left.first, right);
      });
  return (it != sorted_by_first.end() && SpanEquals(it->first, key))
             ? it->second
             : default_value;
}

// In this variant, the template parameter |T| is a class or struct that's
// instantiated in std::unique_ptr, and we return either a T* or a nullptr.
template <typename T>
T* FindByFirst(const std::vector<std::pair<span<uint8_t>, std::unique_ptr<T>>>&
                   sorted_by_first,
               span<uint8_t> key) {
  auto it = std::lower_bound(
      sorted_by_first.begin(), sorted_by_first.end(), key,
      [](const std::pair<span<uint8_t>, std::unique_ptr<T>>& left,
         span<uint8_t> right) { return SpanLessThan(left.first, right); });
  return (it != sorted_by_first.end() && SpanEquals(it->first, key))
             ? it->second.get()
             : nullptr;
}
}  // namespace crdtp

#endif  // CRDTP_FIND_BY_FIRST_H_
