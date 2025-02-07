// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// CPU specific code for arm independent of OS goes here.

#include <sys/syscall.h>
#include <unistd.h>

#include "src/codegen/cpu-features.h"

namespace v8 {
namespace internal {

void CpuFeatures::FlushICache(void* start, size_t size) {
#if !defined(USE_SIMULATOR)
  char* end = reinterpret_cast<char*>(start) + size;
  // SYS_riscv_flush_icache is a symbolic constant used in user-space code to
  // identify the flush_icache system call, while __NR_riscv_flush_icache is the
  // corresponding system call number used in the kernel to dispatch the system
  // call.
  // The flag set to zero will flush all cpu cores.
  syscall(__NR_riscv_flush_icache, start, end, 0);
#endif  // !USE_SIMULATOR.
}

}  // namespace internal
}  // namespace v8
