// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sandbox/trusted-pointer-scope.h"

#include "src/objects/heap-object-inl.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8::internal {

TrustedPointerPublishingScope::TrustedPointerPublishingScope(
    IsolateForSandbox isolate, const DisallowJavascriptExecution& no_js)
    : isolate_(isolate) {
  // Nesting TrustedPointerPublishingScopes is not supported for now.
  DCHECK_NULL(isolate.GetTrustedPointerPublishingScope());
  isolate.SetTrustedPointerPublishingScope(this);
}

TrustedPointerPublishingScope::~TrustedPointerPublishingScope() {
  if (state_ == State::kFailure) {
    if (storage_ == Storage::kSingleton) {
      singleton_->MakeZappedEntry();
    } else if (storage_ == Storage::kVector) {
      for (TrustedPointerTableEntry* entry : *vector_) {
        entry->MakeZappedEntry();
      }
      delete vector_;
    }
  } else {
    // If this DCHECK fails, you probably forgot to call {MarkSuccess()}.
    DCHECK_EQ(state_, State::kSuccess);
  }
  DCHECK_EQ(this, isolate_.GetTrustedPointerPublishingScope());
  isolate_.SetTrustedPointerPublishingScope(nullptr);
}

void TrustedPointerPublishingScope::TrackPointer(
    TrustedPointerTableEntry* entry) {
  if (storage_ == Storage::kEmpty) {
    singleton_ = entry;
    storage_ = Storage::kSingleton;
    return;
  }
  if (storage_ == Storage::kSingleton) {
    TrustedPointerTableEntry* previous = singleton_;
    vector_ = new std::vector<TrustedPointerTableEntry*>();
    vector_->reserve(4);
    vector_->push_back(previous);
    storage_ = Storage::kVector;
  }
  vector_->push_back(entry);
}

}  // namespace v8::internal

#endif  // V8_ENABLE_SANDBOX
