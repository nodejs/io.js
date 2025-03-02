// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for s390 independent of OS goes here.
#if V8_TARGET_ARCH_S390X

#include "src/codegen/cpu-features.h"

namespace v8 {
namespace internal {

void CpuFeatures::FlushICache(void* buffer, size_t size) {
  // Given the strong memory model on z/Architecture, and the single
  // thread nature of V8 and JavaScript, instruction cache flushing
  // is not necessary.  The architecture guarantees that if a core
  // patches its own instruction cache, the updated instructions will be
  // reflected automatically.
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390X
