// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_INDEX_GENERATOR_H_
#define V8_HEAP_INDEX_GENERATOR_H_

#include <cstddef>
#include <optional>
#include <queue>

#include "src/base/macros.h"
#include "src/base/platform/mutex.h"

namespace v8 {
namespace internal {

// A thread-safe data structure that generates heuristic starting points in a
// range to process items in parallel.
class V8_EXPORT_PRIVATE IndexGenerator {
 public:
  explicit IndexGenerator(size_t size);
  IndexGenerator(const IndexGenerator&) = delete;
  IndexGenerator& operator=(const IndexGenerator&) = delete;

  std::optional<size_t> GetNext();

 private:
  base::Mutex lock_;
  bool first_use_;
  // Pending [start, end) ranges to split and hand out indices from.
  std::queue<std::pair<size_t, size_t>> ranges_to_split_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_INDEX_GENERATOR_H_
