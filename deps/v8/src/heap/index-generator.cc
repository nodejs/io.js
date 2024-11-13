// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/index-generator.h"

#include <optional>

namespace v8 {
namespace internal {

IndexGenerator::IndexGenerator(size_t size) : first_use_(size > 0) {
  if (size == 0) return;
  base::MutexGuard guard(&lock_);
  ranges_to_split_.emplace(0, size);
}

std::optional<size_t> IndexGenerator::GetNext() {
  base::MutexGuard guard(&lock_);
  if (first_use_) {
    first_use_ = false;
    return 0;
  }
  if (ranges_to_split_.empty()) return std::nullopt;

  // Split the oldest running range in 2 and return the middle index as
  // starting point.
  auto range = ranges_to_split_.front();
  ranges_to_split_.pop();
  size_t size = range.second - range.first;
  size_t mid = range.first + size / 2;
  // Both sides of the range are added to |ranges_to_split_| so they may be
  // further split if possible.
  if (mid - range.first > 1) ranges_to_split_.emplace(range.first, mid);
  if (range.second - mid > 1) ranges_to_split_.emplace(mid, range.second);
  return mid;
}

}  // namespace internal
}  // namespace v8
