;; Copyright 2020 the V8 project authors. All rights reserved.
;; Use of this source code is governed by a BSD-style license that can be
;; found in the LICENSE file.

;; MASM syntax
;; https://docs.microsoft.com/en-us/cpp/assembler/masm/microsoft-macro-assembler-reference?view=vs-2019

public PushAllRegistersAndIterateStack

.code
PushAllRegistersAndIterateStack proc frame
    ;; Push all callee-saved registers to get them on the stack for conservative
    ;; stack scanning.
    ;;
    ;; We maintain 16-byte alignment at calls. There is an 8-byte return address
    ;; on the stack and we push 232 bytes which maintains 16-byte stack
    ;; alignment at the call.
    ;; Source: https://docs.microsoft.com/en-us/cpp/build/x64-calling-convention
    ;;
    ;; rbp is callee-saved. Maintain proper frame pointer for debugging.
    push rbp
    .pushreg rbp
    mov rbp, rsp
    .setframe rbp, 0
    push 0CDCDCDh  ;; Dummy for alignment.
    .allocstack 8
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push rbx
    .pushreg rbx
    push r12
    .pushreg r12
    push r13
    .pushreg r13
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp, 160
    .allocstack 160
    .endprolog
    ;; Use aligned instrs as we are certain that the stack is properly aligned.
    movdqa  xmmword ptr [rsp + 144], xmm6
    movdqa  xmmword ptr [rsp + 128], xmm7
    movdqa  xmmword ptr [rsp + 112], xmm8
    movdqa  xmmword ptr [rsp + 96], xmm9
    movdqa  xmmword ptr [rsp + 80], xmm10
    movdqa  xmmword ptr [rsp + 64], xmm11
    movdqa  xmmword ptr [rsp + 48], xmm12
    movdqa  xmmword ptr [rsp + 32], xmm13
    movdqa  xmmword ptr [rsp + 16], xmm14
    movdqa  xmmword ptr [rsp], xmm15
    ;; Pass 1st parameter (rcx) unchanged (Stack*).
    ;; Pass 2nd parameter (rdx) unchanged (StackVisitor*).
    ;; Save 3rd parameter (r8; IterateStackCallback)
    mov r9, r8
    ;; Pass 3rd parameter as rsp (stack pointer).
    mov r8, rsp
    ;; Call the callback.
    call r9
    ;; Pop the callee-saved registers.
    add rsp, 224
    ;; Restore rbp as it was used as frame pointer.
    pop rbp
    ret
    PushAllRegistersAndIterateStack endp

end
