// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_H_
#define V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_H_

#include "include/v8-cppgc.h"
#include "src/handles/traced-handles.h"
#include "src/heap/mark-compact.h"
#include "src/heap/marking-worklist.h"

namespace v8 {
namespace internal {

// `UnifiedHeapMarkingState` is used to handle `TracedReferenceBase` and
// friends. It is used when `CppHeap` is attached but also detached. In detached
// mode, the expectation is that no non-null `TracedReferenceBase` is found.
class UnifiedHeapMarkingState final {
 public:
  UnifiedHeapMarkingState(Heap*, MarkingWorklists::Local*,
                          cppgc::internal::CollectionType);

  UnifiedHeapMarkingState(const UnifiedHeapMarkingState&) = delete;
  UnifiedHeapMarkingState& operator=(const UnifiedHeapMarkingState&) = delete;

  void Update(MarkingWorklists::Local*);

  V8_INLINE void MarkAndPush(const TracedReferenceBase&);

 private:
  Heap* const heap_;
  MarkingState* const marking_state_;
  MarkingWorklists::Local* local_marking_worklist_ = nullptr;
  const TracedHandles::MarkMode mark_mode_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_UNIFIED_HEAP_MARKING_STATE_H_
