// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Push all callee-saved registers to get them on the stack for conservative
// stack scanning.

// See asm/x64/push_registers_clang.cc for why the function is not generated
// using clang.

// Do not depend on V8_TARGET_OS_* defines as some embedders may override the
// GN toolchain (e.g. ChromeOS) and not provide them.

#include "src/heap/base/stack.h"

namespace heap {
namespace base {
using IterateStackCallback = void (*)(const Stack*, StackVisitor*, intptr_t*);
extern "C" void PushAllRegistersAndIterateStack(const Stack* sp,
                                                StackVisitor* sv,
                                                IterateStackCallback callback) {
  __asm volatile(
      " lg 1,%0 \n"     // Set up first parameter (sp)
      " lg 2,%1 \n"     // Set up second parameter (sv)
      " lg 7,%2 \n"     // Get callback function descriptor into r7
      " lgr 3,4 \n"     // Set up third parameter (r4 - stack pointer)
      " lg 6,8(,7) \n"  // Get code address into r6
      " lg 5,0(,7) \n"  // Get environment into r5
      " basr 7,6 \n"    // Branch to r6 (callback)
      " nopr 0 \n"
      :
      : "m"(sp), "m"(sv), "m"(callback)
      : "r0", "r1", "r2", "r3", "r5", "r6", "r7", "r9", "r10", "r11", "r12",
        "r13", "r14", "r15");
}
}  // namespace base
}  // namespace heap
