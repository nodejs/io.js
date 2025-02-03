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

// We maintain 16-byte alignment.
//
// Calling convention source:
// https://en.wikipedia.org/wiki/Calling_convention#ARM_(A64)

asm(
#if defined(__APPLE__)
    ".globl _PushAllRegistersAndIterateStack            \n"
    ".private_extern _PushAllRegistersAndIterateStack   \n"
    ".p2align 2                                         \n"
    "_PushAllRegistersAndIterateStack:                  \n"
#else  // !defined(__APPLE__)
    ".globl PushAllRegistersAndIterateStack             \n"
#if !defined(_WIN64)
    ".type PushAllRegistersAndIterateStack, %function   \n"
    ".hidden PushAllRegistersAndIterateStack            \n"
#endif  // !defined(_WIN64)
    ".p2align 2                                         \n"
    "PushAllRegistersAndIterateStack:                   \n"
#endif  // !defined(__APPLE__)
    // x19-x29 are callee-saved.
    "  stp x19, x20, [sp, #-16]!                        \n"
    "  stp x21, x22, [sp, #-16]!                        \n"
    "  stp x23, x24, [sp, #-16]!                        \n"
    "  stp x25, x26, [sp, #-16]!                        \n"
    "  stp x27, x28, [sp, #-16]!                        \n"
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
    // Sign return address.
    "  paciasp                                          \n"
#endif
    "  stp fp, lr,   [sp, #-16]!                        \n"
    // Maintain frame pointer.
    "  mov fp, sp                                       \n"
    // Pass 1st parameter (x0) unchanged (Stack*).
    // Pass 2nd parameter (x1) unchanged (StackVisitor*).
    // Save 3rd parameter (x2; IterateStackCallback)
    "  mov x7, x2                                       \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mov x2, sp                                       \n"
    "  blr x7                                           \n"
    // Load return address and frame pointer.
    "  ldp fp, lr, [sp], #16                            \n"
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
    // Authenticate return address.
    "  autiasp                                          \n"
#endif
    // Drop all callee-saved registers.
    "  add sp, sp, #80                                  \n"
    "  ret                                              \n"
#if !defined(__APPLE__) && !defined(_WIN64)
    ".Lfunc_end0:                                       \n"
    ".size PushAllRegistersAndIterateStack, "
    ".Lfunc_end0-PushAllRegistersAndIterateStack\n"
#endif  // !defined(__APPLE__) && !defined(_WIN64)
    );
