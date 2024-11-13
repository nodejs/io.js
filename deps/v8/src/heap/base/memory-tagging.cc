// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/heap/base/memory-tagging.h"

#include "src/base/cpu.h"
#include "src/base/logging.h"
#include "v8config.h"

#define SUPPORTS_MTE V8_OS_LINUX&& V8_HOST_ARCH_ARM64

namespace heap::base {

SuspendTagCheckingScope::SuspendTagCheckingScope() noexcept {
#if SUPPORTS_MTE
  v8::base::CPU cpu;
  if (V8_UNLIKELY(cpu.has_mte())) {
    uint64_t val;
    // Do a test to see if anything else has interfered with TCO.
    // We expect TCO to be unset here.
    asm volatile(".arch_extension memtag \n mrs %0, tco" : "=r"(val));
    CHECK_EQ(val, 0);

    // Suspend tag checks via PSTATE.TCO.
    asm volatile(".arch_extension memtag \n msr tco, #1" ::: "memory");
  }
#endif
}

SuspendTagCheckingScope::~SuspendTagCheckingScope() {
#if SUPPORTS_MTE
  v8::base::CPU cpu;
  if (V8_UNLIKELY(cpu.has_mte())) {
    uint64_t val;
    // Do a test to see if anything else has interfered with TCO.
    // We expect TCO to be set here.
    asm volatile(".arch_extension memtag \n mrs %0, tco" : "=r"(val));
    CHECK_EQ(val, 1u << 25);

    asm volatile(".arch_extension memtag \n msr tco, #0" ::: "memory");
  }
#endif
}

}  // namespace heap::base
