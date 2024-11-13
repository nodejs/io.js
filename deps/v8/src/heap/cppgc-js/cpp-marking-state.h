// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_
#define V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_

#include <memory>

#include "src/heap/cppgc-js/cpp-heap.h"
#include "src/heap/cppgc/marking-state.h"
#include "src/heap/cppgc/marking-worklists.h"
#include "src/objects/embedder-data-slot.h"

namespace v8 {
namespace internal {

class JSObject;
class EmbedderDataSlot;

class CppMarkingState final {
 public:
  explicit CppMarkingState(
      cppgc::internal::MarkingStateBase& main_thread_marking_state)
      : owned_marking_state_(nullptr),
        marking_state_(main_thread_marking_state) {}

  explicit CppMarkingState(std::unique_ptr<cppgc::internal::MarkingStateBase>
                               concurrent_marking_state)
      : owned_marking_state_(std::move(concurrent_marking_state)),
        marking_state_(*owned_marking_state_) {}
  CppMarkingState(const CppMarkingState&) = delete;
  CppMarkingState& operator=(const CppMarkingState&) = delete;

  void Publish() { marking_state_.Publish(); }

  inline void MarkAndPush(void* instance);

  bool IsLocalEmpty() const {
    return marking_state_.marking_worklist().IsLocalEmpty();
  }

 private:
  std::unique_ptr<cppgc::internal::MarkingStateBase> owned_marking_state_;
  cppgc::internal::MarkingStateBase& marking_state_;
};

}  // namespace internal
}  // namespace v8

#endif  // V8_HEAP_CPPGC_JS_CPP_MARKING_STATE_H_
