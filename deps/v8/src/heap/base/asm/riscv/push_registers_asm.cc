// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Push all callee-saved registers to get them on the stack for conservative
// stack scanning.
//
// See asm/x64/push_registers_asm.cc for why the function is not generated
// using clang.
//
// Calling convention source:
// https://riscv.org/wp-content/uploads/2015/01/riscv-calling.pdf Table 18.2
#ifdef V8_TARGET_ARCH_RISCV64
asm(".global PushAllRegistersAndIterateStack             \n"
    ".type PushAllRegistersAndIterateStack, %function    \n"
    ".hidden PushAllRegistersAndIterateStack             \n"
    "PushAllRegistersAndIterateStack:                    \n"
    // Push all callee-saved registers and save return address.
    "  addi sp, sp, -112                                 \n"
    // Save return address.
    "  sd ra, 104(sp)                                    \n"
    // sp is callee-saved.
    "  sd sp, 96(sp)                                     \n"
    // s0-s11 are callee-saved.
    "  sd s11, 88(sp)                                    \n"
    "  sd s10, 80(sp)                                    \n"
    "  sd s9, 72(sp)                                     \n"
    "  sd s8, 64(sp)                                     \n"
    "  sd s7, 56(sp)                                     \n"
    "  sd s6, 48(sp)                                     \n"
    "  sd s5, 40(sp)                                     \n"
    "  sd s4, 32(sp)                                     \n"
    "  sd s3, 24(sp)                                     \n"
    "  sd s2, 16(sp)                                     \n"
    "  sd s1,  8(sp)                                     \n"
    "  sd s0,  0(sp)                                     \n"
    // Maintain frame pointer(fp is s0).
    "  mv s0, sp                                         \n"
    // Pass 1st parameter (a0) unchanged (Stack*).
    // Pass 2nd parameter (a1) unchanged (StackVisitor*).
    // Save 3rd parameter (a2; IterateStackCallback) to a3.
    "  mv a3, a2                                         \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mv a2, sp                                         \n"
    // Call the callback.
    "  jalr a3                                           \n"
    // Load return address.
    "  ld ra, 104(sp)                                    \n"
    // Restore frame pointer.
    "  ld s0, 0(sp)                                      \n"
    "  addi sp, sp, 112                                  \n"
    "  jr ra                                             \n"
    ".Lfunc_end0:                                        \n"
    ".size PushAllRegistersAndIterateStack, "
    ".Lfunc_end0-PushAllRegistersAndIterateStack         \n");
#elif V8_TARGET_ARCH_RISCV32
asm(".global PushAllRegistersAndIterateStack             \n"
    ".type PushAllRegistersAndIterateStack, %function    \n"
    ".hidden PushAllRegistersAndIterateStack             \n"
    "PushAllRegistersAndIterateStack:                    \n"
    // Push all callee-saved registers and save return address.
    "  addi sp, sp, -56                                  \n"
    // Save return address.
    "  sw ra, 52(sp)                                     \n"
    // sp is callee-saved.
    "  sw sp, 48(sp)                                     \n"
    // s0-s11 are callee-saved.
    "  sw s11, 44(sp)                                    \n"
    "  sw s10, 40(sp)                                    \n"
    "  sw s9, 36(sp)                                     \n"
    "  sw s8, 32(sp)                                     \n"
    "  sw s7, 28(sp)                                     \n"
    "  sw s6, 24(sp)                                     \n"
    "  sw s5, 20(sp)                                     \n"
    "  sw s4, 16(sp)                                     \n"
    "  sw s3, 12(sp)                                     \n"
    "  sw s2, 8(sp)                                      \n"
    "  sw s1,  4(sp)                                     \n"
    "  sw s0,  0(sp)                                     \n"
    // Maintain frame pointer(fp is s0).
    "  mv s0, sp                                         \n"
    // Pass 1st parameter (a0) unchanged (Stack*).
    // Pass 2nd parameter (a1) unchanged (StackVisitor*).
    // Save 3rd parameter (a2; IterateStackCallback) to a3.
    "  mv a3, a2                                         \n"
    // Pass 3rd parameter as sp (stack pointer).
    "  mv a2, sp                                         \n"
    // Call the callback.
    "  jalr a3                                           \n"
    // Load return address.
    "  lw ra, 52(sp)                                     \n"
    // Restore frame pointer.
    "  lw s0, 0(sp)                                      \n"
    "  addi sp, sp, 56                                   \n"
    "  jr ra                                             \n"
    ".Lfunc_end0:                                        \n"
    ".size PushAllRegistersAndIterateStack, "
    ".Lfunc_end0-PushAllRegistersAndIterateStack         \n");
#endif
