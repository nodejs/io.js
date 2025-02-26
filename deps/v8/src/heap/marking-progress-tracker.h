// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_MARKING_PROGRESS_TRACKER_H_
#define V8_HEAP_MARKING_PROGRESS_TRACKER_H_

#include <atomic>
#include <cstdint>

#include "src/base/logging.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

// The MarkingProgressTracker allows for keeping track of the bytes processed of
// a single object. It splits marking of large arrays into chunks so that the
// work can be shared across multiple concurrent markers. The tracker must be
// enabled before it's used.
//
// Only large objects use the tracker which is stored in their page metadata.
// These objects are scanned in increments and concurrently and will be kept
// black while being scanned. Even if the mutator writes to them they will be
// kept black and a white to grey transition is performed in the value via
// regular write barrier.
//
// The tracker starts as disabled. After enabling (through `Enable()`), it can
// never be disabled again.
class MarkingProgressTracker final {
 public:
  static constexpr size_t kChunkSize = kMaxRegularHeapObjectSize;

  void Enable(size_t size) {
    DCHECK(!IsEnabled());
    overall_chunks_ = (size + kChunkSize - 1) / kChunkSize;
    current_chunk_ = 0;
  }

  bool IsEnabled() const { return overall_chunks_ != 0; }

  size_t GetNextChunkToMark() {
    const size_t new_chunk =
        current_chunk_.fetch_add(1, std::memory_order_acq_rel);
    DCHECK_LT(new_chunk, overall_chunks_);
    return new_chunk;
  }

  size_t TotalNumberOfChunks() const { return overall_chunks_; }

  void ResetIfEnabled() {
    if (IsEnabled()) {
      current_chunk_ = 0;
    }
  }

  size_t GetCurrentChunkForTesting() const {
    return current_chunk_.load(std::memory_order_relaxed);
  }

 private:
  size_t overall_chunks_ = 0;
  std::atomic<size_t> current_chunk_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_MARKING_PROGRESS_TRACKER_H_
