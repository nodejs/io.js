// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Push all callee-saved registers to get them on the stack for conservative
// stack scanning.
//
// See asm/x64/push_registers_clang.cc for why the function is not generated
// using clang.
//
// Do not depend on V8_TARGET_OS_* defines as some embedders may override the
// GN toolchain (e.g. ChromeOS) and not provide them.

// We maintain 8-byte alignment at calls by pushing an additional
// non-callee-saved register (r3).
//
// Calling convention source:
// https://en.wikipedia.org/wiki/Calling_convention#ARM_(A32)
// http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.faqs/ka4127.html
asm(".globl PushAllRegistersAndIterateStack             \n"
    ".type PushAllRegistersAndIterateStack, %function   \n"
    ".hidden PushAllRegistersAndIterateStack            \n"
    "PushAllRegistersAndIterateStack:                   \n"
    // Push all callee-saved registers and save return address.
    // Only {r4-r11} are callee-saved registers. Push r3 in addition to align
    // the stack back to 8 bytes.
    "  push {r3-r11, lr}                                \n"
    // Pass 1st parameter (r0) unchanged (Stack*).
    // Pass 2nd parameter (r1) unchanged (StackVisitor*).
    // Save 3rd parameter (r2; IterateStackCallback).
    "  mov r3, r2                                       \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mov r2, sp                                       \n"
    // Call the callback.
    "  blx r3                                           \n"
    // Discard all the registers.
    "  add sp, sp, #36                                  \n"
    // Pop lr into pc which returns and switches mode if needed.
    "  pop {pc}                                         \n"
#if !defined(__APPLE__)
    ".Lfunc_end0:                                       \n"
    ".size PushAllRegistersAndIterateStack, "
    ".Lfunc_end0-PushAllRegistersAndIterateStack\n"
#endif  // !defined(__APPLE__)
    );
