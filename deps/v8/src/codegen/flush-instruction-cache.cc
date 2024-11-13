// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/flush-instruction-cache.h"

#include "src/base/platform/mutex.h"
#include "src/codegen/cpu-features.h"
#include "src/execution/simulator.h"

namespace v8 {
namespace internal {

void FlushInstructionCache(void* start, size_t size) {
  if (size == 0) return;
  if (v8_flags.jitless) return;

  TRACE_EVENT2(TRACE_DISABLED_BY_DEFAULT("v8.compile"), "FlushInstructionCache",
               "start", start, "size", size);

#if defined(USE_SIMULATOR)
  base::MutexGuard lock_guard(Simulator::i_cache_mutex());
  Simulator::FlushICache(Simulator::i_cache(), start, size);
#else
  CpuFeatures::FlushICache(start, size);
#endif  // USE_SIMULATOR
}

}  // namespace internal
}  // namespace v8
