// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_UNMARKER_H_
#define V8_HEAP_CPPGC_UNMARKER_H_

#include "src/heap/cppgc/heap-object-header.h"
#include "src/heap/cppgc/heap-visitor.h"

namespace cppgc {
namespace internal {

class SequentialUnmarker final : private HeapVisitor<SequentialUnmarker> {
  friend class HeapVisitor<SequentialUnmarker>;

 public:
  explicit SequentialUnmarker(RawHeap& heap) { Traverse(heap); }

  bool VisitNormalPage(NormalPage& page) {
    page.ResetMarkedBytes();
    return false;
  }

  bool VisitLargePage(LargePage& page) {
    page.ResetMarkedBytes();
    return false;
  }

 private:
  bool VisitHeapObjectHeader(HeapObjectHeader& header) {
    if (header.IsMarked()) {
      header.Unmark();
    }
    return true;
  }
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_UNMARKER_H_
