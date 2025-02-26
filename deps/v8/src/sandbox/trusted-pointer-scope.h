// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_SANDBOX_TRUSTED_POINTER_SCOPE_H_
#define V8_SANDBOX_TRUSTED_POINTER_SCOPE_H_

#include "src/sandbox/isolate.h"

#ifdef V8_ENABLE_SANDBOX

namespace v8::internal {

class DisallowJavascriptExecution;
struct TrustedPointerTableEntry;

// A TrustedPointerPublishingScope is an optional facility for tracking
// (multiple) newly created TrustedPointerTable entries, and having the
// ability to neuter them afterwards, e.g. if their initialization as a
// group failed in such a way that they should not be made accessible to
// untrusted code (not even via existing in-sandbox corruption).
class TrustedPointerPublishingScope {
 public:
  TrustedPointerPublishingScope(IsolateForSandbox isolate,
                                const DisallowJavascriptExecution& no_js);
  ~TrustedPointerPublishingScope();

  // Decide whether the tracked pointers should be published or discarded.
  void MarkSuccess() { state_ = State::kSuccess; }
  void MarkFailure() { state_ = State::kFailure; }

  void TrackPointer(TrustedPointerTableEntry* entry);

 private:
  enum class State : uint8_t { kInProgress, kSuccess, kFailure };
  enum class Storage : uint8_t { kEmpty, kSingleton, kVector };

  State state_{State::kInProgress};
  Storage storage_{Storage::kEmpty};
  // We could use a base::SmallVector here, but it'd make the object bigger.
  union {
    TrustedPointerTableEntry* singleton_{nullptr};
    std::vector<TrustedPointerTableEntry*>* vector_;
  };
  IsolateForSandbox isolate_;
};

}  // namespace v8::internal

#endif  // V8_ENABLE_SANDBOX

#endif  // V8_SANDBOX_TRUSTED_POINTER_SCOPE_H_
