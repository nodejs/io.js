// Copyright 2025 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASE_FPU_H_
#define V8_BASE_FPU_H_

#include "src/base/base-export.h"

namespace v8::base {
class FPU final {
 public:
  V8_BASE_EXPORT static bool GetFlushDenormals();
  V8_BASE_EXPORT static void SetFlushDenormals(bool);
};
class V8_BASE_EXPORT FlushDenormalsScope final {
 public:
  explicit FlushDenormalsScope(bool value)
      : old_flush_state_(FPU::GetFlushDenormals()) {
    FPU::SetFlushDenormals(value);
  }
  ~FlushDenormalsScope() { FPU::SetFlushDenormals(old_flush_state_); }

 private:
  bool old_flush_state_;
};
}  // namespace v8::base

#endif  // V8_BASE_FPU_H_
