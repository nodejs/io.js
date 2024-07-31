// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/embedder-state.h"

#include "src/api/api-inl.h"
#include "src/base/logging.h"

namespace v8 {

namespace internal {

EmbedderState::EmbedderState(v8::Isolate* isolate, Local<v8::Context> context,
                             EmbedderStateTag tag)
    : isolate_(reinterpret_cast<i::Isolate*>(isolate)),
      tag_(tag),
      previous_embedder_state_(isolate_->current_embedder_state()) {
  if (!context.IsEmpty()) {
    native_context_address_ =
        v8::Utils::OpenDirectHandle(*context)->native_context().address();
  }

  DCHECK_NE(this, isolate_->current_embedder_state());
  isolate_->set_current_embedder_state(this);
}

EmbedderState::~EmbedderState() {
  DCHECK_EQ(this, isolate_->current_embedder_state());
  isolate_->set_current_embedder_state(previous_embedder_state_);
}

void EmbedderState::OnMoveEvent(Address from, Address to) {
  EmbedderState* state = this;
  do {
    if (state->native_context_address_ == from) {
      native_context_address_ = to;
    }
    state = state->previous_embedder_state_;
  } while (state != nullptr);
}

}  // namespace internal

}  // namespace v8
