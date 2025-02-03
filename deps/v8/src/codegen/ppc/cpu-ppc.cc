// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for ppc independent of OS goes here.

#if V8_TARGET_ARCH_PPC64

#include "src/codegen/cpu-features.h"

namespace v8 {
namespace internal {

void CpuFeatures::FlushICache(void* buffer, size_t size) {
#if !defined(USE_SIMULATOR)
  __asm__ __volatile__(
      "sync \n"
      "icbi 0, %0  \n"
      "isync  \n"
      : /* no output */
      : "r"(buffer)
      : "memory");
#endif  // !USE_SIMULATOR
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_PPC64
