// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_HEAP_CPPGC_TASK_HANDLE_H_
#define V8_HEAP_CPPGC_TASK_HANDLE_H_

#include <memory>

#include "src/base/logging.h"

namespace cppgc {
namespace internal {

// A handle that is used for cancelling individual tasks.
struct SingleThreadedHandle {
  struct NonEmptyTag {};

  // Default construction results in empty handle.
  SingleThreadedHandle() = default;

  explicit SingleThreadedHandle(NonEmptyTag)
      : is_cancelled_(std::make_shared<bool>(false)) {}

  void Cancel() {
    DCHECK(is_cancelled_);
    *is_cancelled_ = true;
  }

  void CancelIfNonEmpty() {
    if (is_cancelled_) {
      *is_cancelled_ = true;
    }
  }

  bool IsCanceled() const {
    DCHECK(is_cancelled_);
    return *is_cancelled_;
  }

  // A handle is active if it is non-empty and not cancelled.
  explicit operator bool() const {
    return is_cancelled_.get() && !*is_cancelled_.get();
  }

 private:
  std::shared_ptr<bool> is_cancelled_;
};

}  // namespace internal
}  // namespace cppgc

#endif  // V8_HEAP_CPPGC_TASK_HANDLE_H_
