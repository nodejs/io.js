// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_TRACED_HANDLES_MARKING_VISITOR_H_
#define V8_HEAP_TRACED_HANDLES_MARKING_VISITOR_H_

#include "src/handles/traced-handles.h"
#include "src/heap/base/stack.h"
#include "src/heap/heap.h"
#include "src/heap/mark-compact.h"

namespace v8 {
namespace internal {

// Marking visitor for conservatively marking handles creates through
// `TracedHandles`. The visitor assumes that pointers (on stack, or
// conservatively scanned on-heap) may point into traced handle nodes which
// requires them to be kept alive.
class ConservativeTracedHandlesMarkingVisitor final
    : public ::heap::base::StackVisitor {
 public:
  ConservativeTracedHandlesMarkingVisitor(Heap&, MarkingWorklists::Local&,
                                          cppgc::internal::CollectionType);
  ~ConservativeTracedHandlesMarkingVisitor() override = default;

  void VisitPointer(const void*) override;

 private:
  Heap& heap_;
  MarkingState& marking_state_;
  MarkingWorklists::Local& local_marking_worklist_;
  const TracedHandles::NodeBounds traced_node_bounds_;
  const TracedHandles::MarkMode mark_mode_;
};

#endif  // V8_HEAP_TRACED_HANDLES_MARKING_VISITOR_H_

}  // namespace internal
}  // namespace v8
