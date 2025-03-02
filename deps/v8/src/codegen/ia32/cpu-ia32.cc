// Copyright 2011 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for ia32 independent of OS goes here.

#if V8_TARGET_ARCH_IA32

#include "src/codegen/cpu-features.h"
#if defined(__GNUC__) && !defined(GOOGLE3)
#include "third_party/valgrind/valgrind.h"
#endif

namespace v8 {
namespace internal {

void CpuFeatures::FlushICache(void* start, size_t size) {
  // No need to flush the instruction cache on Intel. On Intel instruction
  // cache flushing is only necessary when multiple cores running the same
  // code simultaneously. V8 (and JavaScript) is single threaded and when code
  // is patched on an intel CPU the core performing the patching will have its
  // own instruction cache updated automatically.

  // If flushing of the instruction cache becomes necessary Windows has the
  // API function FlushInstructionCache.

  // By default, valgrind only checks the stack for writes that might need to
  // invalidate already cached translated code.  This leads to random
  // instability when code patches or moves are sometimes unnoticed.  One
  // solution is to run valgrind with --smc-check=all, but this comes at a big
  // performance cost.  We can notify valgrind to invalidate its cache.
#ifdef VALGRIND_DISCARD_TRANSLATIONS
  unsigned res = VALGRIND_DISCARD_TRANSLATIONS(start, size);
  USE(res);
#endif
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32
