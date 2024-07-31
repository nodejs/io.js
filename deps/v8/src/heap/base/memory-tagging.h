// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Helper functions for Arm's Memory Tagging Extension (MTE).

#ifndef V8_HEAP_BASE_MEMORY_TAGGING_H_
#define V8_HEAP_BASE_MEMORY_TAGGING_H_

#include "src/base/macros.h"

namespace heap::base {
// SuspendTagCheckingScope stops checking MTE tags whilst it's alive. This is
// useful for traversing the stack during garbage collection.
class V8_EXPORT SuspendTagCheckingScope final {
 public:
  // MTE only works on AArch64 Android and Linux.
  SuspendTagCheckingScope() noexcept;
  ~SuspendTagCheckingScope();
};

}  // namespace heap::base

#endif  // V8_HEAP_BASE_MEMORY_TAGGING_H_
