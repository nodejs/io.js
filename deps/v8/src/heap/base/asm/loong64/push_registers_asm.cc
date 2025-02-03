// Copyright 2021 the V8 project authors. All rights reserved.
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
asm(".text                                               \n"
    ".global PushAllRegistersAndIterateStack             \n"
    ".type PushAllRegistersAndIterateStack, %function    \n"
    ".hidden PushAllRegistersAndIterateStack             \n"
    "PushAllRegistersAndIterateStack:                    \n"
    // Push all callee-saved registers and save return address.
    "  addi.d $sp, $sp, -96                              \n"
    "  st.d $s8, $sp, 88                                 \n"
    "  st.d $s7, $sp, 80                                 \n"
    "  st.d $s6, $sp, 72                                 \n"
    "  st.d $s5, $sp, 64                                 \n"
    "  st.d $s4, $sp, 56                                 \n"
    "  st.d $s3, $sp, 48                                 \n"
    "  st.d $s2, $sp, 40                                 \n"
    "  st.d $s1, $sp, 32                                 \n"
    "  st.d $s0, $sp, 24                                 \n"
    "  st.d $fp, $sp, 16                                 \n"
    "  st.d $sp, $sp,  8                                 \n"
    "  st.d $ra, $sp,  0                                 \n"
    // Maintain frame pointer.
    "  addi.d $fp, $sp, 0                                \n"
    // Pass 1st parameter (a0) unchanged (Stack*).
    // Pass 2nd parameter (a1) unchanged (StackVisitor*).
    // Save 3rd parameter (a2; IterateStackCallback).
    "  addi.d $t7, $a2, 0                                \n"
    // Call the callback.
    // Pass 3rd parameter as sp (stack pointer).
    "  addi.d $a2, $sp, 0                                \n"
    "  jirl $ra, $t7, 0                                  \n"
    // Load return address.
    "  ld.d $ra, $sp, 0                                  \n"
    // Restore frame pointer.
    "  ld.d $fp, $sp, 16                                 \n"
    // Discard all callee-saved registers.
    "  addi.d $sp, $sp, 96                               \n"
    "  jirl $zero, $ra, 0                                \n"
    ".Lfunc_end0:                                        \n"
    ".size PushAllRegistersAndIterateStack, "
    ".Lfunc_end0-PushAllRegistersAndIterateStack         \n");
