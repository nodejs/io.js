// Copyright 2013 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/diagnostics/arm64/disasm-arm64.h"

#include <stdio.h>

#include <cstring>

#include "src/codegen/arm64/assembler-arm64.h"
#include "src/codegen/arm64/decoder-arm64-inl.h"
#include "src/codegen/arm64/utils-arm64.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/execution/frames-inl.h"
#include "src/init/v8.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using DisasmArm64Test = TestWithIsolate;

#define EXP_SIZE (256)
#define INSTR_SIZE (1024)
#define SET_UP_MASM()                                                         \
  HandleScope scope(isolate());                                               \
  uint8_t* buf = static_cast<uint8_t*>(malloc(INSTR_SIZE));                   \
  uint32_t encoding = 0;                                                      \
  MacroAssembler* assm =                                                      \
      new MacroAssembler((isolate()), v8::internal::CodeObjectRequired::kYes, \
                         ExternalAssemblerBuffer(buf, INSTR_SIZE));           \
  Decoder<DispatchingDecoderVisitor>* decoder =                               \
      new Decoder<DispatchingDecoderVisitor>();                               \
  DisassemblingDecoder* disasm = new DisassemblingDecoder();                  \
  decoder->AppendVisitor(disasm)

#define SET_UP_ASM()                                                          \
  HandleScope scope(isolate());                                               \
  uint8_t* buf = static_cast<uint8_t*>(malloc(INSTR_SIZE));                   \
  uint32_t encoding = 0;                                                      \
  Assembler* assm = new Assembler(isolate()->allocator(), AssemblerOptions{}, \
                                  ExternalAssemblerBuffer(buf, INSTR_SIZE));  \
  Decoder<DispatchingDecoderVisitor>* decoder =                               \
      new Decoder<DispatchingDecoderVisitor>();                               \
  DisassemblingDecoder* disasm = new DisassemblingDecoder();                  \
  decoder->AppendVisitor(disasm)

#define COMPARE(ASM, EXP)                                                \
  assm->Reset();                                                         \
  assm->ASM;                                                             \
  {                                                                      \
    CodeDesc desc;                                                       \
    assm->GetCode(isolate(), &desc);                                     \
  }                                                                      \
  decoder->Decode(reinterpret_cast<Instruction*>(buf));                  \
  encoding = *reinterpret_cast<uint32_t*>(buf);                          \
  if (strcmp(disasm->GetOutput(), EXP) != 0) {                           \
    printf("%u : Encoding: %08" PRIx32 "\nExpected: %s\nFound:    %s\n", \
           __LINE__, encoding, EXP, disasm->GetOutput());                \
    abort();                                                             \
  }

#define COMPARE_PREFIX(ASM, EXP)                                         \
  assm->Reset();                                                         \
  assm->ASM;                                                             \
  {                                                                      \
    CodeDesc desc;                                                       \
    assm->GetCode(isolate(), &desc);                                     \
  }                                                                      \
  decoder->Decode(reinterpret_cast<Instruction*>(buf));                  \
  encoding = *reinterpret_cast<uint32_t*>(buf);                          \
  if (strncmp(disasm->GetOutput(), EXP, strlen(EXP)) != 0) {             \
    printf("%u : Encoding: %08" PRIx32 "\nExpected: %s\nFound:    %s\n", \
           __LINE__, encoding, EXP, disasm->GetOutput());                \
    abort();                                                             \
  }

#define CLEANUP() \
  delete disasm;  \
  delete decoder; \
  delete assm;    \
  free(buf)

TEST_F(DisasmArm64Test, bootstrap) {
  SET_UP_ASM();

  // Instructions generated by C compiler, disassembled by objdump, and
  // reformatted to suit our disassembly style.
  COMPARE(dci(0xa9ba7bfd), "stp fp, lr, [sp, #-96]!");
  COMPARE(dci(0x910003fd), "mov fp, sp");
  COMPARE(dci(0x9100e3a0), "add x0, fp, #0x38 (56)");
  COMPARE(dci(0xb900001f), "str wzr, [x0]");
  COMPARE(dci(0x528000e1), "movz w1, #0x7");
  COMPARE(dci(0xb9001c01), "str w1, [x0, #28]");
  COMPARE(dci(0x390043a0), "strb w0, [fp, #16]");
  COMPARE(dci(0x790027a0), "strh w0, [fp, #18]");
  COMPARE(dci(0xb9400400), "ldr w0, [x0, #4]");
  COMPARE(dci(0x0b000021), "add w1, w1, w0");
  COMPARE(dci(0x531b6800), "lsl w0, w0, #5");
  COMPARE(dci(0x521e0400), "eor w0, w0, #0xc");
  COMPARE(dci(0x72af0f00), "movk w0, #0x7878, lsl #16");
  COMPARE(dci(0xd360fc00), "lsr x0, x0, #32");
  COMPARE(dci(0x13037c01), "asr w1, w0, #3");
  COMPARE(dci(0x4b000021), "sub w1, w1, w0");
  COMPARE(dci(0x2a0103e0), "mov w0, w1");
  COMPARE(dci(0x93407c00), "sxtw x0, w0");
  COMPARE(dci(0x2a000020), "orr w0, w1, w0");
  COMPARE(dci(0xa8c67bfd), "ldp fp, lr, [sp], #96");

  CLEANUP();
}

TEST_F(DisasmArm64Test, mov_mvn) {
  SET_UP_MASM();

  COMPARE(Mov(w0, Operand(0x1234)), "movz w0, #0x1234");
  COMPARE(Mov(x1, Operand(0x1234)), "movz x1, #0x1234");
  COMPARE(Mov(w2, Operand(w3)), "mov w2, w3");
  COMPARE(Mov(x4, Operand(x5)), "mov x4, x5");
  COMPARE(Mov(w6, Operand(w7, LSL, 5)), "lsl w6, w7, #5");
  COMPARE(Mov(x8, Operand(x9, ASR, 42)), "asr x8, x9, #42");
  COMPARE(Mov(w10, Operand(w11, UXTB)), "uxtb w10, w11");
  COMPARE(Mov(x12, Operand(x13, UXTB, 1)), "ubfiz x12, x13, #1, #8");
  COMPARE(Mov(w14, Operand(w15, SXTH, 2)), "sbfiz w14, w15, #2, #16");
  COMPARE(Mov(x16, Operand(x20, SXTW, 3)), "sbfiz x16, x20, #3, #32");

  COMPARE(Mov(x0, sp), "mov x0, sp");
  COMPARE(Mov(w0, wsp), "mov w0, wsp");
  COMPARE(Mov(x0, xzr), "mov x0, xzr");
  COMPARE(Mov(w0, wzr), "mov w0, wzr");
  COMPARE(mov(x0, sp), "mov x0, sp");
  COMPARE(mov(w0, wsp), "mov w0, wsp");
  COMPARE(mov(x0, xzr), "mov x0, xzr");
  COMPARE(mov(w0, wzr), "mov w0, wzr");

  COMPARE(Mvn(w0, Operand(0x1)), "movn w0, #0x1");
  COMPARE(Mvn(x1, Operand(0xfff)), "movn x1, #0xfff");
  COMPARE(Mvn(w2, Operand(w3)), "mvn w2, w3");
  COMPARE(Mvn(x4, Operand(x5)), "mvn x4, x5");
  COMPARE(Mvn(w6, Operand(w7, LSL, 12)), "mvn w6, w7, lsl #12");
  COMPARE(Mvn(x8, Operand(x9, ASR, 63)), "mvn x8, x9, asr #63");

  CLEANUP();
}

TEST_F(DisasmArm64Test, move_immediate) {
  SET_UP_ASM();

  COMPARE(movz(w0, 0x1234), "movz w0, #0x1234");
  COMPARE(movz(x1, 0xabcd0000), "movz x1, #0xabcd0000");
  COMPARE(movz(x2, 0x555500000000), "movz x2, #0x555500000000");
  COMPARE(movz(x3, 0xaaaa000000000000), "movz x3, #0xaaaa000000000000");
  COMPARE(movz(x4, 0xabcd, 16), "movz x4, #0xabcd0000");
  COMPARE(movz(x5, 0x5555, 32), "movz x5, #0x555500000000");
  COMPARE(movz(x6, 0xaaaa, 48), "movz x6, #0xaaaa000000000000");

  COMPARE(movk(w7, 0x1234), "movk w7, #0x1234");
  COMPARE(movk(x8, 0xabcd0000), "movk x8, #0xabcd, lsl #16");
  COMPARE(movk(x9, 0x555500000000), "movk x9, #0x5555, lsl #32");
  COMPARE(movk(x10, 0xaaaa000000000000), "movk x10, #0xaaaa, lsl #48");
  COMPARE(movk(w11, 0xabcd, 16), "movk w11, #0xabcd, lsl #16");
  COMPARE(movk(x12, 0x5555, 32), "movk x12, #0x5555, lsl #32");
  COMPARE(movk(x13, 0xaaaa, 48), "movk x13, #0xaaaa, lsl #48");

  COMPARE(movn(w14, 0x1234), "movn w14, #0x1234");
  COMPARE(movn(x15, 0xabcd0000), "movn x15, #0xabcd0000");
  COMPARE(movn(x16, 0x555500000000), "movn x16, #0x555500000000");
  COMPARE(movn(x17, 0xaaaa000000000000), "movn x17, #0xaaaa000000000000");
  COMPARE(movn(w18, 0xabcd, 16), "movn w18, #0xabcd0000");
  COMPARE(movn(x19, 0x5555, 32), "movn x19, #0x555500000000");
  COMPARE(movn(x20, 0xaaaa, 48), "movn x20, #0xaaaa000000000000");

  COMPARE(movk(w21, 0), "movk w21, #0x0");
  COMPARE(movk(x22, 0, 0), "movk x22, #0x0");
  COMPARE(movk(w23, 0, 16), "movk w23, #0x0, lsl #16");
  COMPARE(movk(x24, 0, 32), "movk x24, #0x0, lsl #32");
  COMPARE(movk(x25, 0, 48), "movk x25, #0x0, lsl #48");

  CLEANUP();
}

TEST_F(DisasmArm64Test, move_immediate_2) {
  SET_UP_MASM();

  // Move instructions expected for certain immediates. This is really a macro
  // assembler test, to ensure it generates immediates efficiently.
  COMPARE(Mov(w0, 0), "movz w0, #0x0");
  COMPARE(Mov(w0, 0x0000ffff), "movz w0, #0xffff");
  COMPARE(Mov(w0, 0x00010000), "movz w0, #0x10000");
  COMPARE(Mov(w0, 0xffff0000), "movz w0, #0xffff0000");
  COMPARE(Mov(w0, 0x0001ffff), "movn w0, #0xfffe0000");
  COMPARE(Mov(w0, 0xffff8000), "movn w0, #0x7fff");
  COMPARE(Mov(w0, 0xfffffffe), "movn w0, #0x1");
  COMPARE(Mov(w0, 0xffffffff), "movn w0, #0x0");
  COMPARE(Mov(w0, 0x00ffff00), "mov w0, #0xffff00");
  COMPARE(Mov(w0, 0xfffe7fff), "mov w0, #0xfffe7fff");
  COMPARE(Mov(w0, 0xfffeffff), "movn w0, #0x10000");
  COMPARE(Mov(w0, 0xffff7fff), "movn w0, #0x8000");

  COMPARE(Mov(x0, 0), "movz x0, #0x0");
  COMPARE(Mov(x0, 0x0000ffff), "movz x0, #0xffff");
  COMPARE(Mov(x0, 0x00010000), "movz x0, #0x10000");
  COMPARE(Mov(x0, 0xffff0000), "movz x0, #0xffff0000");
  COMPARE(Mov(x0, 0x0001ffff), "mov x0, #0x1ffff");
  COMPARE(Mov(x0, 0xffff8000), "mov x0, #0xffff8000");
  COMPARE(Mov(x0, 0xfffffffe), "mov x0, #0xfffffffe");
  COMPARE(Mov(x0, 0xffffffff), "mov x0, #0xffffffff");
  COMPARE(Mov(x0, 0x00ffff00), "mov x0, #0xffff00");
  COMPARE(Mov(x0, 0xffff000000000000), "movz x0, #0xffff000000000000");
  COMPARE(Mov(x0, 0x0000ffff00000000), "movz x0, #0xffff00000000");
  COMPARE(Mov(x0, 0x00000000ffff0000), "movz x0, #0xffff0000");
  COMPARE(Mov(x0, 0xffffffffffff0000), "movn x0, #0xffff");
  COMPARE(Mov(x0, 0xffffffff0000ffff), "movn x0, #0xffff0000");
  COMPARE(Mov(x0, 0xffff0000ffffffff), "movn x0, #0xffff00000000");
  COMPARE(Mov(x0, 0x0000ffffffffffff), "movn x0, #0xffff000000000000");
  COMPARE(Mov(x0, 0xfffe7fffffffffff), "mov x0, #0xfffe7fffffffffff");
  COMPARE(Mov(x0, 0xfffeffffffffffff), "movn x0, #0x1000000000000");
  COMPARE(Mov(x0, 0xffff7fffffffffff), "movn x0, #0x800000000000");
  COMPARE(Mov(x0, 0xfffffffe7fffffff), "mov x0, #0xfffffffe7fffffff");
  COMPARE(Mov(x0, 0xfffffffeffffffff), "movn x0, #0x100000000");
  COMPARE(Mov(x0, 0xffffffff7fffffff), "movn x0, #0x80000000");
  COMPARE(Mov(x0, 0xfffffffffffe7fff), "mov x0, #0xfffffffffffe7fff");
  COMPARE(Mov(x0, 0xfffffffffffeffff), "movn x0, #0x10000");
  COMPARE(Mov(x0, 0xffffffffffff7fff), "movn x0, #0x8000");
  COMPARE(Mov(x0, 0xffffffffffffffff), "movn x0, #0x0");

  COMPARE(Movk(w0, 0x1234, 0), "movk w0, #0x1234");
  COMPARE(Movk(x1, 0x2345, 0), "movk x1, #0x2345");
  COMPARE(Movk(w2, 0x3456, 16), "movk w2, #0x3456, lsl #16");
  COMPARE(Movk(x3, 0x4567, 16), "movk x3, #0x4567, lsl #16");
  COMPARE(Movk(x4, 0x5678, 32), "movk x4, #0x5678, lsl #32");
  COMPARE(Movk(x5, 0x6789, 48), "movk x5, #0x6789, lsl #48");

  CLEANUP();
}

TEST_F(DisasmArm64Test, add_immediate) {
  SET_UP_ASM();

  COMPARE(add(w0, w1, Operand(0xff)), "add w0, w1, #0xff (255)");
  COMPARE(add(x2, x3, Operand(0x3ff)), "add x2, x3, #0x3ff (1023)");
  COMPARE(add(w4, w5, Operand(0xfff)), "add w4, w5, #0xfff (4095)");
  COMPARE(add(x6, x7, Operand(0x1000)), "add x6, x7, #0x1000 (4096)");
  COMPARE(add(w8, w9, Operand(0xff000)), "add w8, w9, #0xff000 (1044480)");
  COMPARE(add(x10, x11, Operand(0x3ff000)),
          "add x10, x11, #0x3ff000 (4190208)");
  COMPARE(add(w12, w13, Operand(0xfff000)),
          "add w12, w13, #0xfff000 (16773120)");
  COMPARE(adds(w14, w15, Operand(0xff)), "adds w14, w15, #0xff (255)");
  COMPARE(adds(x16, x17, Operand(0xaa000)), "adds x16, x17, #0xaa000 (696320)");
  COMPARE(cmn(w18, Operand(0xff)), "cmn w18, #0xff (255)");
  COMPARE(cmn(x19, Operand(0xff000)), "cmn x19, #0xff000 (1044480)");
  COMPARE(add(w0, wsp, Operand(0)), "mov w0, wsp");
  COMPARE(add(sp, x0, Operand(0)), "mov sp, x0");

  COMPARE(add(w1, wsp, Operand(8)), "add w1, wsp, #0x8 (8)");
  COMPARE(add(x2, sp, Operand(16)), "add x2, sp, #0x10 (16)");
  COMPARE(add(wsp, wsp, Operand(42)), "add wsp, wsp, #0x2a (42)");
  COMPARE(cmn(sp, Operand(24)), "cmn sp, #0x18 (24)");
  COMPARE(adds(wzr, wsp, Operand(9)), "cmn wsp, #0x9 (9)");

  CLEANUP();
}

TEST_F(DisasmArm64Test, sub_immediate) {
  SET_UP_ASM();

  COMPARE(sub(w0, w1, Operand(0xff)), "sub w0, w1, #0xff (255)");
  COMPARE(sub(x2, x3, Operand(0x3ff)), "sub x2, x3, #0x3ff (1023)");
  COMPARE(sub(w4, w5, Operand(0xfff)), "sub w4, w5, #0xfff (4095)");
  COMPARE(sub(x6, x7, Operand(0x1000)), "sub x6, x7, #0x1000 (4096)");
  COMPARE(sub(w8, w9, Operand(0xff000)), "sub w8, w9, #0xff000 (1044480)");
  COMPARE(sub(x10, x11, Operand(0x3ff000)),
          "sub x10, x11, #0x3ff000 (4190208)");
  COMPARE(sub(w12, w13, Operand(0xfff000)),
          "sub w12, w13, #0xfff000 (16773120)");
  COMPARE(subs(w14, w15, Operand(0xff)), "subs w14, w15, #0xff (255)");
  COMPARE(subs(x16, x17, Operand(0xaa000)), "subs x16, x17, #0xaa000 (696320)");
  COMPARE(cmp(w18, Operand(0xff)), "cmp w18, #0xff (255)");
  COMPARE(cmp(x19, Operand(0xff000)), "cmp x19, #0xff000 (1044480)");

  COMPARE(add(w1, wsp, Operand(8)), "add w1, wsp, #0x8 (8)");
  COMPARE(add(x2, sp, Operand(16)), "add x2, sp, #0x10 (16)");
  COMPARE(add(wsp, wsp, Operand(42)), "add wsp, wsp, #0x2a (42)");
  COMPARE(cmn(sp, Operand(24)), "cmn sp, #0x18 (24)");
  COMPARE(adds(wzr, wsp, Operand(9)), "cmn wsp, #0x9 (9)");

  CLEANUP();
}

TEST_F(DisasmArm64Test, add_shifted) {
  SET_UP_ASM();

  COMPARE(add(w0, w1, Operand(w2)), "add w0, w1, w2");
  COMPARE(add(x3, x4, Operand(x5)), "add x3, x4, x5");
  COMPARE(add(w6, w7, Operand(w8, LSL, 1)), "add w6, w7, w8, lsl #1");
  COMPARE(add(x9, x10, Operand(x11, LSL, 2)), "add x9, x10, x11, lsl #2");
  COMPARE(add(w12, w13, Operand(w14, LSR, 3)), "add w12, w13, w14, lsr #3");
  COMPARE(add(x15, x16, Operand(x17, LSR, 4)), "add x15, x16, x17, lsr #4");
  COMPARE(add(w18, w19, Operand(w20, ASR, 5)), "add w18, w19, w20, asr #5");
  COMPARE(add(x21, x22, Operand(x23, ASR, 6)), "add x21, x22, x23, asr #6");
  COMPARE(cmn(w24, Operand(w25)), "cmn w24, w25");
  COMPARE(cmn(x26, Operand(cp, LSL, 63)), "cmn x26, cp, lsl #63");

  COMPARE(add(x0, sp, Operand(x1)), "add x0, sp, x1");
  COMPARE(add(w2, wsp, Operand(w3)), "add w2, wsp, w3");
  COMPARE(add(x4, sp, Operand(x5, LSL, 1)), "add x4, sp, x5, lsl #1");
  COMPARE(add(x4, xzr, Operand(x5, LSL, 1)), "add x4, xzr, x5, lsl #1");
  COMPARE(add(w6, wsp, Operand(w7, LSL, 3)), "add w6, wsp, w7, lsl #3");
  COMPARE(adds(xzr, sp, Operand(x8, LSL, 4)), "cmn sp, x8, lsl #4");
  COMPARE(adds(xzr, xzr, Operand(x8, LSL, 5)), "cmn xzr, x8, lsl #5");

  CLEANUP();
}

TEST_F(DisasmArm64Test, sub_shifted) {
  SET_UP_ASM();

  COMPARE(sub(w0, w1, Operand(w2)), "sub w0, w1, w2");
  COMPARE(sub(x3, x4, Operand(x5)), "sub x3, x4, x5");
  COMPARE(sub(w6, w7, Operand(w8, LSL, 1)), "sub w6, w7, w8, lsl #1");
  COMPARE(sub(x9, x10, Operand(x11, LSL, 2)), "sub x9, x10, x11, lsl #2");
  COMPARE(sub(w12, w13, Operand(w14, LSR, 3)), "sub w12, w13, w14, lsr #3");
  COMPARE(sub(x15, x16, Operand(x17, LSR, 4)), "sub x15, x16, x17, lsr #4");
  COMPARE(sub(w18, w19, Operand(w20, ASR, 5)), "sub w18, w19, w20, asr #5");
  COMPARE(sub(x21, x22, Operand(x23, ASR, 6)), "sub x21, x22, x23, asr #6");
  COMPARE(cmp(w24, Operand(w25)), "cmp w24, w25");
  COMPARE(cmp(x26, Operand(cp, LSL, 63)), "cmp x26, cp, lsl #63");
  COMPARE(neg(w28, Operand(w29)), "neg w28, w29");
  COMPARE(neg(lr, Operand(x0, LSR, 62)), "neg lr, x0, lsr #62");
  COMPARE(negs(w1, Operand(w2)), "negs w1, w2");
  COMPARE(negs(x3, Operand(x4, ASR, 61)), "negs x3, x4, asr #61");

  COMPARE(sub(x0, sp, Operand(x1)), "sub x0, sp, x1");
  COMPARE(sub(w2, wsp, Operand(w3)), "sub w2, wsp, w3");
  COMPARE(sub(x4, sp, Operand(x5, LSL, 1)), "sub x4, sp, x5, lsl #1");
  COMPARE(sub(x4, xzr, Operand(x5, LSL, 1)), "neg x4, x5, lsl #1");
  COMPARE(sub(w6, wsp, Operand(w7, LSL, 3)), "sub w6, wsp, w7, lsl #3");
  COMPARE(subs(xzr, sp, Operand(x8, LSL, 4)), "cmp sp, x8, lsl #4");
  COMPARE(subs(xzr, xzr, Operand(x8, LSL, 5)), "cmp xzr, x8, lsl #5");

  CLEANUP();
}

TEST_F(DisasmArm64Test, add_extended) {
  SET_UP_ASM();

  COMPARE(add(w0, w1, Operand(w2, UXTB)), "add w0, w1, w2, uxtb");
  COMPARE(adds(x3, x4, Operand(w5, UXTB, 1)), "adds x3, x4, w5, uxtb #1");
  COMPARE(add(w6, w7, Operand(w8, UXTH, 2)), "add w6, w7, w8, uxth #2");
  COMPARE(adds(x9, x10, Operand(x11, UXTW, 3)), "adds x9, x10, w11, uxtw #3");
  COMPARE(add(x12, x13, Operand(x14, UXTX, 4)), "add x12, x13, x14, uxtx #4");
  COMPARE(adds(w15, w16, Operand(w17, SXTB, 4)), "adds w15, w16, w17, sxtb #4");
  COMPARE(add(x18, x19, Operand(x20, SXTB, 3)), "add x18, x19, w20, sxtb #3");
  COMPARE(adds(w21, w22, Operand(w23, SXTH, 2)), "adds w21, w22, w23, sxth #2");
  COMPARE(add(x24, x25, Operand(x26, SXTW, 1)), "add x24, x25, w26, sxtw #1");
  COMPARE(adds(cp, x28, Operand(fp, SXTX)), "adds cp, x28, fp, sxtx");
  COMPARE(cmn(w0, Operand(w1, UXTB, 2)), "cmn w0, w1, uxtb #2");
  COMPARE(cmn(x2, Operand(x3, SXTH, 4)), "cmn x2, w3, sxth #4");

  COMPARE(add(w0, wsp, Operand(w1, UXTB)), "add w0, wsp, w1, uxtb");
  COMPARE(add(x2, sp, Operand(x3, UXTH, 1)), "add x2, sp, w3, uxth #1");
  COMPARE(add(wsp, wsp, Operand(w4, UXTW, 2)), "add wsp, wsp, w4, lsl #2");
  COMPARE(cmn(sp, Operand(xzr, UXTX, 3)), "cmn sp, xzr, lsl #3");
  COMPARE(cmn(sp, Operand(xzr, LSL, 4)), "cmn sp, xzr, lsl #4");

  CLEANUP();
}

TEST_F(DisasmArm64Test, sub_extended) {
  SET_UP_ASM();

  COMPARE(sub(w0, w1, Operand(w2, UXTB)), "sub w0, w1, w2, uxtb");
  COMPARE(subs(x3, x4, Operand(w5, UXTB, 1)), "subs x3, x4, w5, uxtb #1");
  COMPARE(sub(w6, w7, Operand(w8, UXTH, 2)), "sub w6, w7, w8, uxth #2");
  COMPARE(subs(x9, x10, Operand(x11, UXTW, 3)), "subs x9, x10, w11, uxtw #3");
  COMPARE(sub(x12, x13, Operand(x14, UXTX, 4)), "sub x12, x13, x14, uxtx #4");
  COMPARE(subs(w15, w16, Operand(w17, SXTB, 4)), "subs w15, w16, w17, sxtb #4");
  COMPARE(sub(x18, x19, Operand(x20, SXTB, 3)), "sub x18, x19, w20, sxtb #3");
  COMPARE(subs(w21, w22, Operand(w23, SXTH, 2)), "subs w21, w22, w23, sxth #2");
  COMPARE(sub(x24, x25, Operand(x26, SXTW, 1)), "sub x24, x25, w26, sxtw #1");
  COMPARE(subs(cp, x28, Operand(fp, SXTX)), "subs cp, x28, fp, sxtx");
  COMPARE(cmp(w0, Operand(w1, SXTB, 1)), "cmp w0, w1, sxtb #1");
  COMPARE(cmp(x2, Operand(x3, UXTH, 3)), "cmp x2, w3, uxth #3");

  COMPARE(sub(w0, wsp, Operand(w1, UXTB)), "sub w0, wsp, w1, uxtb");
  COMPARE(sub(x2, sp, Operand(x3, UXTH, 1)), "sub x2, sp, w3, uxth #1");
  COMPARE(sub(wsp, wsp, Operand(w4, UXTW, 2)), "sub wsp, wsp, w4, lsl #2");
  COMPARE(cmp(sp, Operand(xzr, UXTX, 3)), "cmp sp, xzr, lsl #3");
  COMPARE(cmp(sp, Operand(xzr, LSL, 4)), "cmp sp, xzr, lsl #4");

  CLEANUP();
}

TEST_F(DisasmArm64Test, adc_subc_ngc) {
  SET_UP_ASM();

  COMPARE(adc(w0, w1, Operand(w2)), "adc w0, w1, w2");
  COMPARE(adc(x3, x4, Operand(x5)), "adc x3, x4, x5");
  COMPARE(adcs(w6, w7, Operand(w8)), "adcs w6, w7, w8");
  COMPARE(adcs(x9, x10, Operand(x11)), "adcs x9, x10, x11");
  COMPARE(sbc(w12, w13, Operand(w14)), "sbc w12, w13, w14");
  COMPARE(sbc(x15, x16, Operand(x17)), "sbc x15, x16, x17");
  COMPARE(sbcs(w18, w19, Operand(w20)), "sbcs w18, w19, w20");
  COMPARE(sbcs(x21, x22, Operand(x23)), "sbcs x21, x22, x23");
  COMPARE(ngc(w24, Operand(w25)), "ngc w24, w25");
  COMPARE(ngc(x26, Operand(cp)), "ngc x26, cp");
  COMPARE(ngcs(w28, Operand(w29)), "ngcs w28, w29");
  COMPARE(ngcs(lr, Operand(x0)), "ngcs lr, x0");

  CLEANUP();
}

TEST_F(DisasmArm64Test, mul_and_div) {
  SET_UP_ASM();

  COMPARE(mul(w0, w1, w2), "mul w0, w1, w2");
  COMPARE(mul(x3, x4, x5), "mul x3, x4, x5");
  COMPARE(mul(w30, w0, w1), "mul w30, w0, w1");
  COMPARE(mul(lr, x0, x1), "mul lr, x0, x1");
  COMPARE(mneg(w0, w1, w2), "mneg w0, w1, w2");
  COMPARE(mneg(x3, x4, x5), "mneg x3, x4, x5");
  COMPARE(mneg(w30, w0, w1), "mneg w30, w0, w1");
  COMPARE(mneg(lr, x0, x1), "mneg lr, x0, x1");
  COMPARE(smull(x0, w0, w1), "smull x0, w0, w1");
  COMPARE(smull(lr, w30, w0), "smull lr, w30, w0");
  COMPARE(smulh(x0, x1, x2), "smulh x0, x1, x2");

  COMPARE(madd(w0, w1, w2, w3), "madd w0, w1, w2, w3");
  COMPARE(madd(x4, x5, x6, x7), "madd x4, x5, x6, x7");
  COMPARE(madd(w8, w9, w10, wzr), "mul w8, w9, w10");
  COMPARE(madd(x11, x12, x13, xzr), "mul x11, x12, x13");
  COMPARE(msub(w14, w15, w16, w17), "msub w14, w15, w16, w17");
  COMPARE(msub(x18, x19, x20, x21), "msub x18, x19, x20, x21");
  COMPARE(msub(w22, w23, w24, wzr), "mneg w22, w23, w24");
  COMPARE(msub(x25, x26, x0, xzr), "mneg x25, x26, x0");

  COMPARE(sdiv(w0, w1, w2), "sdiv w0, w1, w2");
  COMPARE(sdiv(x3, x4, x5), "sdiv x3, x4, x5");
  COMPARE(udiv(w6, w7, w8), "udiv w6, w7, w8");
  COMPARE(udiv(x9, x10, x11), "udiv x9, x10, x11");

  CLEANUP();
}

TEST_F(DisasmArm64Test, maddl_msubl) {
  SET_UP_ASM();

  COMPARE(smaddl(x0, w1, w2, x3), "smaddl x0, w1, w2, x3");
  COMPARE(smaddl(x25, w21, w22, x16), "smaddl x25, w21, w22, x16");
  COMPARE(umaddl(x0, w1, w2, x3), "umaddl x0, w1, w2, x3");
  COMPARE(umaddl(x25, w21, w22, x16), "umaddl x25, w21, w22, x16");

  COMPARE(smsubl(x0, w1, w2, x3), "smsubl x0, w1, w2, x3");
  COMPARE(smsubl(x25, w21, w22, x16), "smsubl x25, w21, w22, x16");
  COMPARE(umsubl(x0, w1, w2, x3), "umsubl x0, w1, w2, x3");
  COMPARE(umsubl(x25, w21, w22, x16), "umsubl x25, w21, w22, x16");

  CLEANUP();
}

TEST_F(DisasmArm64Test, dp_1_source) {
  SET_UP_ASM();

  COMPARE(rbit(w0, w1), "rbit w0, w1");
  COMPARE(rbit(x2, x3), "rbit x2, x3");
  COMPARE(rev16(w4, w5), "rev16 w4, w5");
  COMPARE(rev16(x6, x7), "rev16 x6, x7");
  COMPARE(rev32(x8, x9), "rev32 x8, x9");
  COMPARE(rev(w10, w11), "rev w10, w11");
  COMPARE(rev(x12, x13), "rev x12, x13");
  COMPARE(clz(w14, w15), "clz w14, w15");
  COMPARE(clz(x16, x17), "clz x16, x17");
  COMPARE(cls(w18, w19), "cls w18, w19");
  COMPARE(cls(x20, x21), "cls x20, x21");

  CLEANUP();
}

TEST_F(DisasmArm64Test, bitfield) {
  SET_UP_ASM();

  COMPARE(sxtb(w0, w1), "sxtb w0, w1");
  COMPARE(sxtb(x2, x3), "sxtb x2, w3");
  COMPARE(sxth(w4, w5), "sxth w4, w5");
  COMPARE(sxth(x6, x7), "sxth x6, w7");
  COMPARE(sxtw(x8, x9), "sxtw x8, w9");
  COMPARE(sxtb(x0, w1), "sxtb x0, w1");
  COMPARE(sxth(x2, w3), "sxth x2, w3");
  COMPARE(sxtw(x4, w5), "sxtw x4, w5");

  COMPARE(uxtb(w10, w11), "uxtb w10, w11");
  COMPARE(uxtb(x12, x13), "uxtb x12, w13");
  COMPARE(uxth(w14, w15), "uxth w14, w15");
  COMPARE(uxth(x16, x17), "uxth x16, w17");
  COMPARE(uxtw(x18, x19), "ubfx x18, x19, #0, #32");

  COMPARE(asr(w20, w21, 10), "asr w20, w21, #10");
  COMPARE(asr(x22, x23, 20), "asr x22, x23, #20");
  COMPARE(lsr(w24, w25, 10), "lsr w24, w25, #10");
  COMPARE(lsr(x26, cp, 20), "lsr x26, cp, #20");
  COMPARE(lsl(w28, w29, 10), "lsl w28, w29, #10");
  COMPARE(lsl(lr, x0, 20), "lsl lr, x0, #20");

  COMPARE(sbfiz(w1, w2, 1, 20), "sbfiz w1, w2, #1, #20");
  COMPARE(sbfiz(x3, x4, 2, 19), "sbfiz x3, x4, #2, #19");
  COMPARE(sbfx(w5, w6, 3, 18), "sbfx w5, w6, #3, #18");
  COMPARE(sbfx(x7, x8, 4, 17), "sbfx x7, x8, #4, #17");
  COMPARE(bfi(w9, w10, 5, 16), "bfi w9, w10, #5, #16");
  COMPARE(bfi(x11, x12, 6, 15), "bfi x11, x12, #6, #15");
  COMPARE(bfxil(w13, w14, 7, 14), "bfxil w13, w14, #7, #14");
  COMPARE(bfxil(x15, x16, 8, 13), "bfxil x15, x16, #8, #13");
  COMPARE(ubfiz(w17, w18, 9, 12), "ubfiz w17, w18, #9, #12");
  COMPARE(ubfiz(x19, x20, 10, 11), "ubfiz x19, x20, #10, #11");
  COMPARE(ubfx(w21, w22, 11, 10), "ubfx w21, w22, #11, #10");
  COMPARE(ubfx(x23, x24, 12, 9), "ubfx x23, x24, #12, #9");

  CLEANUP();
}

TEST_F(DisasmArm64Test, extract) {
  SET_UP_ASM();

  COMPARE(extr(w0, w1, w2, 0), "extr w0, w1, w2, #0");
  COMPARE(extr(x3, x4, x5, 1), "extr x3, x4, x5, #1");
  COMPARE(extr(w6, w7, w8, 31), "extr w6, w7, w8, #31");
  COMPARE(extr(x9, x10, x11, 63), "extr x9, x10, x11, #63");
  COMPARE(extr(w12, w13, w13, 10), "ror w12, w13, #10");
  COMPARE(extr(x14, x15, x15, 42), "ror x14, x15, #42");

  CLEANUP();
}

TEST_F(DisasmArm64Test, logical_immediate) {
  SET_UP_ASM();
#define RESULT_SIZE (256)

  char result[RESULT_SIZE];

  // Test immediate encoding - 64-bit destination.
  // 64-bit patterns.
  uint64_t value = 0x7fffffff;
  for (int i = 0; i < 64; i++) {
    snprintf(result, RESULT_SIZE, "and x0, x0, #0x%" PRIx64, value);
    COMPARE(and_(x0, x0, Operand(value)), result);
    value = ((value & 1) << 63) | (value >> 1);  // Rotate right 1 bit.
  }

  // 32-bit patterns.
  value = 0x00003fff00003fffL;
  for (int i = 0; i < 32; i++) {
    snprintf(result, RESULT_SIZE, "and x0, x0, #0x%" PRIx64, value);
    COMPARE(and_(x0, x0, Operand(value)), result);
    value = ((value & 1) << 63) | (value >> 1);  // Rotate right 1 bit.
  }

  // 16-bit patterns.
  value = 0x001f001f001f001fL;
  for (int i = 0; i < 16; i++) {
    snprintf(result, RESULT_SIZE, "and x0, x0, #0x%" PRIx64, value);
    COMPARE(and_(x0, x0, Operand(value)), result);
    value = ((value & 1) << 63) | (value >> 1);  // Rotate right 1 bit.
  }

  // 8-bit patterns.
  value = 0x0e0e0e0e0e0e0e0eL;
  for (int i = 0; i < 8; i++) {
    snprintf(result, RESULT_SIZE, "and x0, x0, #0x%" PRIx64, value);
    COMPARE(and_(x0, x0, Operand(value)), result);
    value = ((value & 1) << 63) | (value >> 1);  // Rotate right 1 bit.
  }

  // 4-bit patterns.
  value = 0x6666666666666666L;
  for (int i = 0; i < 4; i++) {
    snprintf(result, RESULT_SIZE, "and x0, x0, #0x%" PRIx64, value);
    COMPARE(and_(x0, x0, Operand(value)), result);
    value = ((value & 1) << 63) | (value >> 1);  // Rotate right 1 bit.
  }

  // 2-bit patterns.
  COMPARE(and_(x0, x0, Operand(0x5555555555555555L)),
          "and x0, x0, #0x5555555555555555");
  COMPARE(and_(x0, x0, Operand(0xaaaaaaaaaaaaaaaaL)),
          "and x0, x0, #0xaaaaaaaaaaaaaaaa");

  // Test immediate encoding - 32-bit destination.
  COMPARE(and_(w0, w0, Operand(0xff8007ff)),
          "and w0, w0, #0xff8007ff");  // 32-bit pattern.
  COMPARE(and_(w0, w0, Operand(0xf87ff87f)),
          "and w0, w0, #0xf87ff87f");  // 16-bit pattern.
  COMPARE(and_(w0, w0, Operand(0x87878787)),
          "and w0, w0, #0x87878787");  // 8-bit pattern.
  COMPARE(and_(w0, w0, Operand(0x66666666)),
          "and w0, w0, #0x66666666");  // 4-bit pattern.
  COMPARE(and_(w0, w0, Operand(0x55555555)),
          "and w0, w0, #0x55555555");  // 2-bit pattern.

  // Test other instructions.
  COMPARE(tst(w1, Operand(0x11111111)), "tst w1, #0x11111111");
  COMPARE(tst(x2, Operand(0x8888888888888888L)), "tst x2, #0x8888888888888888");
  COMPARE(orr(w7, w8, Operand(0xaaaaaaaa)), "orr w7, w8, #0xaaaaaaaa");
  COMPARE(orr(x9, x10, Operand(0x5555555555555555L)),
          "orr x9, x10, #0x5555555555555555");
  COMPARE(eor(w15, w16, Operand(0x00000001)), "eor w15, w16, #0x1");
  COMPARE(eor(x17, x18, Operand(0x0000000000000003L)), "eor x17, x18, #0x3");
  COMPARE(ands(w23, w24, Operand(0x0000000f)), "ands w23, w24, #0xf");
  COMPARE(ands(x25, x26, Operand(0x800000000000000fL)),
          "ands x25, x26, #0x800000000000000f");

  // Test inverse.
  COMPARE(bic(w3, w4, Operand(0x20202020)), "and w3, w4, #0xdfdfdfdf");
  COMPARE(bic(x5, x6, Operand(0x4040404040404040L)),
          "and x5, x6, #0xbfbfbfbfbfbfbfbf");
  COMPARE(orn(w11, w12, Operand(0x40004000)), "orr w11, w12, #0xbfffbfff");
  COMPARE(orn(x13, x14, Operand(0x8181818181818181L)),
          "orr x13, x14, #0x7e7e7e7e7e7e7e7e");
  COMPARE(eon(w19, w20, Operand(0x80000001)), "eor w19, w20, #0x7ffffffe");
  COMPARE(eon(x21, x22, Operand(0xc000000000000003L)),
          "eor x21, x22, #0x3ffffffffffffffc");
  COMPARE(bics(w27, w28, Operand(0xfffffff7)), "ands w27, w28, #0x8");
  COMPARE(bics(fp, x0, Operand(0xfffffffeffffffffL)),
          "ands fp, x0, #0x100000000");

  // Test stack pointer.
  COMPARE(and_(wsp, wzr, Operand(7)), "and wsp, wzr, #0x7");
  COMPARE(ands(xzr, xzr, Operand(7)), "tst xzr, #0x7");
  COMPARE(orr(sp, xzr, Operand(15)), "orr sp, xzr, #0xf");
  COMPARE(eor(wsp, w0, Operand(31)), "eor wsp, w0, #0x1f");

  // Test move aliases.
  COMPARE(orr(w0, wzr, Operand(0x00000780)), "orr w0, wzr, #0x780");
  COMPARE(orr(w1, wzr, Operand(0x00007800)), "orr w1, wzr, #0x7800");
  COMPARE(orr(w2, wzr, Operand(0x00078000)), "mov w2, #0x78000");
  COMPARE(orr(w3, wzr, Operand(0x00780000)), "orr w3, wzr, #0x780000");
  COMPARE(orr(w4, wzr, Operand(0x07800000)), "orr w4, wzr, #0x7800000");
  COMPARE(orr(x5, xzr, Operand(0xffffffffffffc001UL)),
          "orr x5, xzr, #0xffffffffffffc001");
  COMPARE(orr(x6, xzr, Operand(0xfffffffffffc001fUL)),
          "mov x6, #0xfffffffffffc001f");
  COMPARE(orr(x7, xzr, Operand(0xffffffffffc001ffUL)),
          "mov x7, #0xffffffffffc001ff");
  COMPARE(orr(x8, xzr, Operand(0xfffffffffc001fffUL)),
          "mov x8, #0xfffffffffc001fff");
  COMPARE(orr(x9, xzr, Operand(0xffffffffc001ffffUL)),
          "orr x9, xzr, #0xffffffffc001ffff");

  CLEANUP();
}

TEST_F(DisasmArm64Test, logical_shifted) {
  SET_UP_ASM();

  COMPARE(and_(w0, w1, Operand(w2)), "and w0, w1, w2");
  COMPARE(and_(x3, x4, Operand(x5, LSL, 1)), "and x3, x4, x5, lsl #1");
  COMPARE(and_(w6, w7, Operand(w8, LSR, 2)), "and w6, w7, w8, lsr #2");
  COMPARE(and_(x9, x10, Operand(x11, ASR, 3)), "and x9, x10, x11, asr #3");
  COMPARE(and_(w12, w13, Operand(w14, ROR, 4)), "and w12, w13, w14, ror #4");

  COMPARE(bic(w15, w16, Operand(w17)), "bic w15, w16, w17");
  COMPARE(bic(x18, x19, Operand(x20, LSL, 5)), "bic x18, x19, x20, lsl #5");
  COMPARE(bic(w21, w22, Operand(w23, LSR, 6)), "bic w21, w22, w23, lsr #6");
  COMPARE(bic(x24, x25, Operand(x26, ASR, 7)), "bic x24, x25, x26, asr #7");
  COMPARE(bic(w27, w28, Operand(w29, ROR, 8)), "bic w27, w28, w29, ror #8");

  COMPARE(orr(w0, w1, Operand(w2)), "orr w0, w1, w2");
  COMPARE(orr(x3, x4, Operand(x5, LSL, 9)), "orr x3, x4, x5, lsl #9");
  COMPARE(orr(w6, w7, Operand(w8, LSR, 10)), "orr w6, w7, w8, lsr #10");
  COMPARE(orr(x9, x10, Operand(x11, ASR, 11)), "orr x9, x10, x11, asr #11");
  COMPARE(orr(w12, w13, Operand(w14, ROR, 12)), "orr w12, w13, w14, ror #12");

  COMPARE(orn(w15, w16, Operand(w17)), "orn w15, w16, w17");
  COMPARE(orn(x18, x19, Operand(x20, LSL, 13)), "orn x18, x19, x20, lsl #13");
  COMPARE(orn(w21, w22, Operand(w23, LSR, 14)), "orn w21, w22, w23, lsr #14");
  COMPARE(orn(x24, x25, Operand(x26, ASR, 15)), "orn x24, x25, x26, asr #15");
  COMPARE(orn(w27, w28, Operand(w29, ROR, 16)), "orn w27, w28, w29, ror #16");

  COMPARE(eor(w0, w1, Operand(w2)), "eor w0, w1, w2");
  COMPARE(eor(x3, x4, Operand(x5, LSL, 17)), "eor x3, x4, x5, lsl #17");
  COMPARE(eor(w6, w7, Operand(w8, LSR, 18)), "eor w6, w7, w8, lsr #18");
  COMPARE(eor(x9, x10, Operand(x11, ASR, 19)), "eor x9, x10, x11, asr #19");
  COMPARE(eor(w12, w13, Operand(w14, ROR, 20)), "eor w12, w13, w14, ror #20");

  COMPARE(eon(w15, w16, Operand(w17)), "eon w15, w16, w17");
  COMPARE(eon(x18, x19, Operand(x20, LSL, 21)), "eon x18, x19, x20, lsl #21");
  COMPARE(eon(w21, w22, Operand(w23, LSR, 22)), "eon w21, w22, w23, lsr #22");
  COMPARE(eon(x24, x25, Operand(x26, ASR, 23)), "eon x24, x25, x26, asr #23");
  COMPARE(eon(w27, w28, Operand(w29, ROR, 24)), "eon w27, w28, w29, ror #24");

  COMPARE(ands(w0, w1, Operand(w2)), "ands w0, w1, w2");
  COMPARE(ands(x3, x4, Operand(x5, LSL, 1)), "ands x3, x4, x5, lsl #1");
  COMPARE(ands(w6, w7, Operand(w8, LSR, 2)), "ands w6, w7, w8, lsr #2");
  COMPARE(ands(x9, x10, Operand(x11, ASR, 3)), "ands x9, x10, x11, asr #3");
  COMPARE(ands(w12, w13, Operand(w14, ROR, 4)), "ands w12, w13, w14, ror #4");

  COMPARE(bics(w15, w16, Operand(w17)), "bics w15, w16, w17");
  COMPARE(bics(x18, x19, Operand(x20, LSL, 5)), "bics x18, x19, x20, lsl #5");
  COMPARE(bics(w21, w22, Operand(w23, LSR, 6)), "bics w21, w22, w23, lsr #6");
  COMPARE(bics(x24, x25, Operand(x26, ASR, 7)), "bics x24, x25, x26, asr #7");
  COMPARE(bics(w27, w28, Operand(w29, ROR, 8)), "bics w27, w28, w29, ror #8");

  COMPARE(tst(w0, Operand(w1)), "tst w0, w1");
  COMPARE(tst(w2, Operand(w3, ROR, 10)), "tst w2, w3, ror #10");
  COMPARE(tst(x0, Operand(x1)), "tst x0, x1");
  COMPARE(tst(x2, Operand(x3, ROR, 42)), "tst x2, x3, ror #42");

  COMPARE(orn(w0, wzr, Operand(w1)), "mvn w0, w1");
  COMPARE(orn(w2, wzr, Operand(w3, ASR, 5)), "mvn w2, w3, asr #5");
  COMPARE(orn(x0, xzr, Operand(x1)), "mvn x0, x1");
  COMPARE(orn(x2, xzr, Operand(x3, ASR, 42)), "mvn x2, x3, asr #42");

  COMPARE(orr(w0, wzr, Operand(w1)), "mov w0, w1");
  COMPARE(orr(x0, xzr, Operand(x1)), "mov x0, x1");
  COMPARE(orr(w16, wzr, Operand(w17, LSL, 1)), "orr w16, wzr, w17, lsl #1");
  COMPARE(orr(x16, xzr, Operand(x17, ASR, 2)), "orr x16, xzr, x17, asr #2");

  CLEANUP();
}

TEST_F(DisasmArm64Test, dp_2_source) {
  SET_UP_ASM();

  COMPARE(lslv(w0, w1, w2), "lsl w0, w1, w2");
  COMPARE(lslv(x3, x4, x5), "lsl x3, x4, x5");
  COMPARE(lsrv(w6, w7, w8), "lsr w6, w7, w8");
  COMPARE(lsrv(x9, x10, x11), "lsr x9, x10, x11");
  COMPARE(asrv(w12, w13, w14), "asr w12, w13, w14");
  COMPARE(asrv(x15, x16, x17), "asr x15, x16, x17");
  COMPARE(rorv(w18, w19, w20), "ror w18, w19, w20");
  COMPARE(rorv(x21, x22, x23), "ror x21, x22, x23");

  CLEANUP();
}

TEST_F(DisasmArm64Test, adr) {
  SET_UP_ASM();

  char expected[100];
  snprintf(expected, sizeof(expected), "adr x0, #+0x0 (addr %p)", buf);
  COMPARE(adr(x0, 0), expected);
  snprintf(expected, sizeof(expected), "adr x0, #+0x1 (addr %p)", buf + 1);
  COMPARE(adr(x0, 1), expected);
  snprintf(expected, sizeof(expected), "adr x0, #-0x1 (addr %p)", buf - 1);
  COMPARE(adr(x0, -1), expected);
  COMPARE_PREFIX(adr(x0, 0), "adr x0, #+0x0");
  COMPARE_PREFIX(adr(x1, 1), "adr x1, #+0x1");
  COMPARE_PREFIX(adr(x2, -1), "adr x2, #-0x1");
  COMPARE_PREFIX(adr(x3, 4), "adr x3, #+0x4");
  COMPARE_PREFIX(adr(x4, -4), "adr x4, #-0x4");
  COMPARE_PREFIX(adr(x5, 0x000fffff), "adr x5, #+0xfffff");
  COMPARE_PREFIX(adr(x6, -0x00100000), "adr x6, #-0x100000");
  COMPARE_PREFIX(adr(xzr, 0), "adr xzr, #+0x0");

  CLEANUP();
}

TEST_F(DisasmArm64Test, branch) {
  SET_UP_ASM();

#define INST_OFF(x) ((x) >> kInstrSizeLog2)
  COMPARE_PREFIX(b(INST_OFF(0x4)), "b #+0x4");
  COMPARE_PREFIX(b(INST_OFF(-0x4)), "b #-0x4");
  COMPARE_PREFIX(b(INST_OFF(0x7fffffc)), "b #+0x7fffffc");
  COMPARE_PREFIX(b(INST_OFF(-0x8000000)), "b #-0x8000000");
  COMPARE_PREFIX(b(INST_OFF(0xffffc), eq), "b.eq #+0xffffc");
  COMPARE_PREFIX(b(INST_OFF(-0x100000), mi), "b.mi #-0x100000");
  COMPARE_PREFIX(bl(INST_OFF(0x4)), "bl #+0x4");
  COMPARE_PREFIX(bl(INST_OFF(-0x4)), "bl #-0x4");
  COMPARE_PREFIX(bl(INST_OFF(0xffffc)), "bl #+0xffffc");
  COMPARE_PREFIX(bl(INST_OFF(-0x100000)), "bl #-0x100000");
  COMPARE_PREFIX(cbz(w0, INST_OFF(0xffffc)), "cbz w0, #+0xffffc");
  COMPARE_PREFIX(cbz(x1, INST_OFF(-0x100000)), "cbz x1, #-0x100000");
  COMPARE_PREFIX(cbnz(w2, INST_OFF(0xffffc)), "cbnz w2, #+0xffffc");
  COMPARE_PREFIX(cbnz(x3, INST_OFF(-0x100000)), "cbnz x3, #-0x100000");
  COMPARE_PREFIX(tbz(w4, 0, INST_OFF(0x7ffc)), "tbz w4, #0, #+0x7ffc");
  COMPARE_PREFIX(tbz(x5, 63, INST_OFF(-0x8000)), "tbz x5, #63, #-0x8000");
  COMPARE_PREFIX(tbz(w6, 31, INST_OFF(0)), "tbz w6, #31, #+0x0");
  COMPARE_PREFIX(tbz(x7, 31, INST_OFF(0x4)), "tbz w7, #31, #+0x4");
  COMPARE_PREFIX(tbz(x8, 32, INST_OFF(0x8)), "tbz x8, #32, #+0x8");
  COMPARE_PREFIX(tbnz(w8, 0, INST_OFF(0x7ffc)), "tbnz w8, #0, #+0x7ffc");
  COMPARE_PREFIX(tbnz(x9, 63, INST_OFF(-0x8000)), "tbnz x9, #63, #-0x8000");
  COMPARE_PREFIX(tbnz(w10, 31, INST_OFF(0)), "tbnz w10, #31, #+0x0");
  COMPARE_PREFIX(tbnz(x11, 31, INST_OFF(0x4)), "tbnz w11, #31, #+0x4");
  COMPARE_PREFIX(tbnz(x12, 32, INST_OFF(0x8)), "tbnz x12, #32, #+0x8");
#undef INST_OFF
  COMPARE(br(x0), "br x0");
  COMPARE(blr(x1), "blr x1");
  COMPARE(ret(x2), "ret x2");
  COMPARE(ret(lr), "ret");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store) {
  SET_UP_ASM();

  COMPARE(ldr(w0, MemOperand(x1)), "ldr w0, [x1]");
  COMPARE(ldr(w2, MemOperand(x3, 4)), "ldr w2, [x3, #4]");
  COMPARE(ldr(w4, MemOperand(x5, 16380)), "ldr w4, [x5, #16380]");
  COMPARE(ldr(x6, MemOperand(x7)), "ldr x6, [x7]");
  COMPARE(ldr(x8, MemOperand(x9, 8)), "ldr x8, [x9, #8]");
  COMPARE(ldr(x10, MemOperand(x11, 32760)), "ldr x10, [x11, #32760]");
  COMPARE(str(w12, MemOperand(x13)), "str w12, [x13]");
  COMPARE(str(w14, MemOperand(x15, 4)), "str w14, [x15, #4]");
  COMPARE(str(w16, MemOperand(x17, 16380)), "str w16, [x17, #16380]");
  COMPARE(str(x18, MemOperand(x19)), "str x18, [x19]");
  COMPARE(str(x20, MemOperand(x21, 8)), "str x20, [x21, #8]");
  COMPARE(str(x22, MemOperand(x23, 32760)), "str x22, [x23, #32760]");

  COMPARE(ldr(w0, MemOperand(x1, 4, PreIndex)), "ldr w0, [x1, #4]!");
  COMPARE(ldr(w2, MemOperand(x3, 255, PreIndex)), "ldr w2, [x3, #255]!");
  COMPARE(ldr(w4, MemOperand(x5, -256, PreIndex)), "ldr w4, [x5, #-256]!");
  COMPARE(ldr(x6, MemOperand(x7, 8, PreIndex)), "ldr x6, [x7, #8]!");
  COMPARE(ldr(x8, MemOperand(x9, 255, PreIndex)), "ldr x8, [x9, #255]!");
  COMPARE(ldr(x10, MemOperand(x11, -256, PreIndex)), "ldr x10, [x11, #-256]!");
  COMPARE(str(w12, MemOperand(x13, 4, PreIndex)), "str w12, [x13, #4]!");
  COMPARE(str(w14, MemOperand(x15, 255, PreIndex)), "str w14, [x15, #255]!");
  COMPARE(str(w16, MemOperand(x17, -256, PreIndex)), "str w16, [x17, #-256]!");
  COMPARE(str(x18, MemOperand(x19, 8, PreIndex)), "str x18, [x19, #8]!");
  COMPARE(str(x20, MemOperand(x21, 255, PreIndex)), "str x20, [x21, #255]!");
  COMPARE(str(x22, MemOperand(x23, -256, PreIndex)), "str x22, [x23, #-256]!");

  COMPARE(ldr(w0, MemOperand(x1, 4, PostIndex)), "ldr w0, [x1], #4");
  COMPARE(ldr(w2, MemOperand(x3, 255, PostIndex)), "ldr w2, [x3], #255");
  COMPARE(ldr(w4, MemOperand(x5, -256, PostIndex)), "ldr w4, [x5], #-256");
  COMPARE(ldr(x6, MemOperand(x7, 8, PostIndex)), "ldr x6, [x7], #8");
  COMPARE(ldr(x8, MemOperand(x9, 255, PostIndex)), "ldr x8, [x9], #255");
  COMPARE(ldr(x10, MemOperand(x11, -256, PostIndex)), "ldr x10, [x11], #-256");
  COMPARE(str(w12, MemOperand(x13, 4, PostIndex)), "str w12, [x13], #4");
  COMPARE(str(w14, MemOperand(x15, 255, PostIndex)), "str w14, [x15], #255");
  COMPARE(str(w16, MemOperand(x17, -256, PostIndex)), "str w16, [x17], #-256");
  COMPARE(str(x18, MemOperand(x19, 8, PostIndex)), "str x18, [x19], #8");
  COMPARE(str(x20, MemOperand(x21, 255, PostIndex)), "str x20, [x21], #255");
  COMPARE(str(x22, MemOperand(x23, -256, PostIndex)), "str x22, [x23], #-256");

  COMPARE(ldr(w24, MemOperand(x28)), "ldr w24, [x28]");
  COMPARE(ldr(x25, MemOperand(x28, 8)), "ldr x25, [x28, #8]");
  COMPARE(str(w26, MemOperand(x28, 4, PreIndex)), "str w26, [x28, #4]!");
  COMPARE(str(cp, MemOperand(x28, -8, PostIndex)), "str cp, [x28], #-8");

  COMPARE(ldrsw(x0, MemOperand(x1)), "ldrsw x0, [x1]");
  COMPARE(ldrsw(x2, MemOperand(x3, 8)), "ldrsw x2, [x3, #8]");
  COMPARE(ldrsw(x4, MemOperand(x5, 42, PreIndex)), "ldrsw x4, [x5, #42]!");
  COMPARE(ldrsw(x6, MemOperand(x7, -11, PostIndex)), "ldrsw x6, [x7], #-11");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_regoffset) {
  SET_UP_ASM();

  COMPARE(ldr(w0, MemOperand(x1, w2, UXTW)), "ldr w0, [x1, w2, uxtw]");
  COMPARE(ldr(w3, MemOperand(x4, w5, UXTW, 2)), "ldr w3, [x4, w5, uxtw #2]");
  COMPARE(ldr(w6, MemOperand(x7, x8)), "ldr w6, [x7, x8]");
  COMPARE(ldr(w9, MemOperand(x10, x11, LSL, 2)), "ldr w9, [x10, x11, lsl #2]");
  COMPARE(ldr(w12, MemOperand(x13, w14, SXTW)), "ldr w12, [x13, w14, sxtw]");
  COMPARE(ldr(w15, MemOperand(x16, w17, SXTW, 2)),
          "ldr w15, [x16, w17, sxtw #2]");
  COMPARE(ldr(w18, MemOperand(x19, x20, SXTX)), "ldr w18, [x19, x20, sxtx]");
  COMPARE(ldr(w21, MemOperand(x22, x23, SXTX, 2)),
          "ldr w21, [x22, x23, sxtx #2]");
  COMPARE(ldr(x0, MemOperand(x1, w2, UXTW)), "ldr x0, [x1, w2, uxtw]");
  COMPARE(ldr(x3, MemOperand(x4, w5, UXTW, 3)), "ldr x3, [x4, w5, uxtw #3]");
  COMPARE(ldr(x6, MemOperand(x7, x8)), "ldr x6, [x7, x8]");
  COMPARE(ldr(x9, MemOperand(x10, x11, LSL, 3)), "ldr x9, [x10, x11, lsl #3]");
  COMPARE(ldr(x12, MemOperand(x13, w14, SXTW)), "ldr x12, [x13, w14, sxtw]");
  COMPARE(ldr(x15, MemOperand(x16, w17, SXTW, 3)),
          "ldr x15, [x16, w17, sxtw #3]");
  COMPARE(ldr(x18, MemOperand(x19, x20, SXTX)), "ldr x18, [x19, x20, sxtx]");
  COMPARE(ldr(x21, MemOperand(x22, x23, SXTX, 3)),
          "ldr x21, [x22, x23, sxtx #3]");

  COMPARE(str(w0, MemOperand(x1, w2, UXTW)), "str w0, [x1, w2, uxtw]");
  COMPARE(str(w3, MemOperand(x4, w5, UXTW, 2)), "str w3, [x4, w5, uxtw #2]");
  COMPARE(str(w6, MemOperand(x7, x8)), "str w6, [x7, x8]");
  COMPARE(str(w9, MemOperand(x10, x11, LSL, 2)), "str w9, [x10, x11, lsl #2]");
  COMPARE(str(w12, MemOperand(x13, w14, SXTW)), "str w12, [x13, w14, sxtw]");
  COMPARE(str(w15, MemOperand(x16, w17, SXTW, 2)),
          "str w15, [x16, w17, sxtw #2]");
  COMPARE(str(w18, MemOperand(x19, x20, SXTX)), "str w18, [x19, x20, sxtx]");
  COMPARE(str(w21, MemOperand(x22, x23, SXTX, 2)),
          "str w21, [x22, x23, sxtx #2]");
  COMPARE(str(x0, MemOperand(x1, w2, UXTW)), "str x0, [x1, w2, uxtw]");
  COMPARE(str(x3, MemOperand(x4, w5, UXTW, 3)), "str x3, [x4, w5, uxtw #3]");
  COMPARE(str(x6, MemOperand(x7, x8)), "str x6, [x7, x8]");
  COMPARE(str(x9, MemOperand(x10, x11, LSL, 3)), "str x9, [x10, x11, lsl #3]");
  COMPARE(str(x12, MemOperand(x13, w14, SXTW)), "str x12, [x13, w14, sxtw]");
  COMPARE(str(x15, MemOperand(x16, w17, SXTW, 3)),
          "str x15, [x16, w17, sxtw #3]");
  COMPARE(str(x18, MemOperand(x19, x20, SXTX)), "str x18, [x19, x20, sxtx]");
  COMPARE(str(x21, MemOperand(x22, x23, SXTX, 3)),
          "str x21, [x22, x23, sxtx #3]");

  COMPARE(ldrb(w0, MemOperand(x1, w2, UXTW)), "ldrb w0, [x1, w2, uxtw]");
  COMPARE(ldrb(w6, MemOperand(x7, x8)), "ldrb w6, [x7, x8]");
  COMPARE(ldrb(w12, MemOperand(x13, w14, SXTW)), "ldrb w12, [x13, w14, sxtw]");
  COMPARE(ldrb(w18, MemOperand(x19, x20, SXTX)), "ldrb w18, [x19, x20, sxtx]");
  COMPARE(strb(w0, MemOperand(x1, w2, UXTW)), "strb w0, [x1, w2, uxtw]");
  COMPARE(strb(w6, MemOperand(x7, x8)), "strb w6, [x7, x8]");
  COMPARE(strb(w12, MemOperand(x13, w14, SXTW)), "strb w12, [x13, w14, sxtw]");
  COMPARE(strb(w18, MemOperand(x19, x20, SXTX)), "strb w18, [x19, x20, sxtx]");

  COMPARE(ldrh(w0, MemOperand(x1, w2, UXTW)), "ldrh w0, [x1, w2, uxtw]");
  COMPARE(ldrh(w3, MemOperand(x4, w5, UXTW, 1)), "ldrh w3, [x4, w5, uxtw #1]");
  COMPARE(ldrh(w6, MemOperand(x7, x8)), "ldrh w6, [x7, x8]");
  COMPARE(ldrh(w9, MemOperand(x10, x11, LSL, 1)),
          "ldrh w9, [x10, x11, lsl #1]");
  COMPARE(ldrh(w12, MemOperand(x13, w14, SXTW)), "ldrh w12, [x13, w14, sxtw]");
  COMPARE(ldrh(w15, MemOperand(x16, w17, SXTW, 1)),
          "ldrh w15, [x16, w17, sxtw #1]");
  COMPARE(ldrh(w18, MemOperand(x19, x20, SXTX)), "ldrh w18, [x19, x20, sxtx]");
  COMPARE(ldrh(w21, MemOperand(x22, x23, SXTX, 1)),
          "ldrh w21, [x22, x23, sxtx #1]");
  COMPARE(strh(w0, MemOperand(x1, w2, UXTW)), "strh w0, [x1, w2, uxtw]");
  COMPARE(strh(w3, MemOperand(x4, w5, UXTW, 1)), "strh w3, [x4, w5, uxtw #1]");
  COMPARE(strh(w6, MemOperand(x7, x8)), "strh w6, [x7, x8]");
  COMPARE(strh(w9, MemOperand(x10, x11, LSL, 1)),
          "strh w9, [x10, x11, lsl #1]");
  COMPARE(strh(w12, MemOperand(x13, w14, SXTW)), "strh w12, [x13, w14, sxtw]");
  COMPARE(strh(w15, MemOperand(x16, w17, SXTW, 1)),
          "strh w15, [x16, w17, sxtw #1]");
  COMPARE(strh(w18, MemOperand(x19, x20, SXTX)), "strh w18, [x19, x20, sxtx]");
  COMPARE(strh(w21, MemOperand(x22, x23, SXTX, 1)),
          "strh w21, [x22, x23, sxtx #1]");

  COMPARE(ldr(x0, MemOperand(x28, wzr, SXTW)), "ldr x0, [x28, wzr, sxtw]");
  COMPARE(str(x1, MemOperand(x28, xzr)), "str x1, [x28, xzr]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_byte) {
  SET_UP_ASM();

  COMPARE(ldrb(w0, MemOperand(x1)), "ldrb w0, [x1]");
  COMPARE(ldrb(x2, MemOperand(x3)), "ldrb w2, [x3]");
  COMPARE(ldrb(w4, MemOperand(x5, 4095)), "ldrb w4, [x5, #4095]");
  COMPARE(ldrb(w6, MemOperand(x7, 255, PreIndex)), "ldrb w6, [x7, #255]!");
  COMPARE(ldrb(w8, MemOperand(x9, -256, PreIndex)), "ldrb w8, [x9, #-256]!");
  COMPARE(ldrb(w10, MemOperand(x11, 255, PostIndex)), "ldrb w10, [x11], #255");
  COMPARE(ldrb(w12, MemOperand(x13, -256, PostIndex)),
          "ldrb w12, [x13], #-256");
  COMPARE(strb(w14, MemOperand(x15)), "strb w14, [x15]");
  COMPARE(strb(x16, MemOperand(x17)), "strb w16, [x17]");
  COMPARE(strb(w18, MemOperand(x19, 4095)), "strb w18, [x19, #4095]");
  COMPARE(strb(w20, MemOperand(x21, 255, PreIndex)), "strb w20, [x21, #255]!");
  COMPARE(strb(w22, MemOperand(x23, -256, PreIndex)),
          "strb w22, [x23, #-256]!");
  COMPARE(strb(w24, MemOperand(x25, 255, PostIndex)), "strb w24, [x25], #255");
  COMPARE(strb(w26, MemOperand(cp, -256, PostIndex)), "strb w26, [cp], #-256");
  COMPARE(ldrb(w28, MemOperand(x28, 3, PostIndex)), "ldrb w28, [x28], #3");
  COMPARE(strb(fp, MemOperand(x28, -42, PreIndex)), "strb w29, [x28, #-42]!");
  COMPARE(ldrsb(w0, MemOperand(x1)), "ldrsb w0, [x1]");
  COMPARE(ldrsb(x2, MemOperand(x3, 8)), "ldrsb x2, [x3, #8]");
  COMPARE(ldrsb(w4, MemOperand(x5, 42, PreIndex)), "ldrsb w4, [x5, #42]!");
  COMPARE(ldrsb(x6, MemOperand(x7, -11, PostIndex)), "ldrsb x6, [x7], #-11");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_half) {
  SET_UP_ASM();

  COMPARE(ldrh(w0, MemOperand(x1)), "ldrh w0, [x1]");
  COMPARE(ldrh(x2, MemOperand(x3)), "ldrh w2, [x3]");
  COMPARE(ldrh(w4, MemOperand(x5, 8190)), "ldrh w4, [x5, #8190]");
  COMPARE(ldrh(w6, MemOperand(x7, 255, PreIndex)), "ldrh w6, [x7, #255]!");
  COMPARE(ldrh(w8, MemOperand(x9, -256, PreIndex)), "ldrh w8, [x9, #-256]!");
  COMPARE(ldrh(w10, MemOperand(x11, 255, PostIndex)), "ldrh w10, [x11], #255");
  COMPARE(ldrh(w12, MemOperand(x13, -256, PostIndex)),
          "ldrh w12, [x13], #-256");
  COMPARE(strh(w14, MemOperand(x15)), "strh w14, [x15]");
  COMPARE(strh(x16, MemOperand(x17)), "strh w16, [x17]");
  COMPARE(strh(w18, MemOperand(x19, 8190)), "strh w18, [x19, #8190]");
  COMPARE(strh(w20, MemOperand(x21, 255, PreIndex)), "strh w20, [x21, #255]!");
  COMPARE(strh(w22, MemOperand(x23, -256, PreIndex)),
          "strh w22, [x23, #-256]!");
  COMPARE(strh(w24, MemOperand(x25, 255, PostIndex)), "strh w24, [x25], #255");
  COMPARE(strh(w26, MemOperand(cp, -256, PostIndex)), "strh w26, [cp], #-256");
  COMPARE(ldrh(w28, MemOperand(x28, 3, PostIndex)), "ldrh w28, [x28], #3");
  COMPARE(strh(fp, MemOperand(x28, -42, PreIndex)), "strh w29, [x28, #-42]!");
  COMPARE(ldrh(w30, MemOperand(x0, 255)), "ldurh w30, [x0, #255]");
  COMPARE(ldrh(x1, MemOperand(x2, -256)), "ldurh w1, [x2, #-256]");
  COMPARE(strh(w3, MemOperand(x4, 255)), "sturh w3, [x4, #255]");
  COMPARE(strh(x5, MemOperand(x6, -256)), "sturh w5, [x6, #-256]");
  COMPARE(ldrsh(w0, MemOperand(x1)), "ldrsh w0, [x1]");
  COMPARE(ldrsh(w2, MemOperand(x3, 8)), "ldrsh w2, [x3, #8]");
  COMPARE(ldrsh(w4, MemOperand(x5, 42, PreIndex)), "ldrsh w4, [x5, #42]!");
  COMPARE(ldrsh(x6, MemOperand(x7, -11, PostIndex)), "ldrsh x6, [x7], #-11");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_v_offset) {
  SET_UP_ASM();

  COMPARE(ldr(s0, MemOperand(x1)), "ldr s0, [x1]");
  COMPARE(ldr(s2, MemOperand(x3, 4)), "ldr s2, [x3, #4]");
  COMPARE(ldr(s4, MemOperand(x5, 16380)), "ldr s4, [x5, #16380]");
  COMPARE(ldr(d6, MemOperand(x7)), "ldr d6, [x7]");
  COMPARE(ldr(d8, MemOperand(x9, 8)), "ldr d8, [x9, #8]");
  COMPARE(ldr(d10, MemOperand(x11, 32760)), "ldr d10, [x11, #32760]");
  COMPARE(str(s12, MemOperand(x13)), "str s12, [x13]");
  COMPARE(str(s14, MemOperand(x15, 4)), "str s14, [x15, #4]");
  COMPARE(str(s16, MemOperand(x17, 16380)), "str s16, [x17, #16380]");
  COMPARE(str(d18, MemOperand(x19)), "str d18, [x19]");
  COMPARE(str(d20, MemOperand(x21, 8)), "str d20, [x21, #8]");
  COMPARE(str(d22, MemOperand(x23, 32760)), "str d22, [x23, #32760]");

  COMPARE(ldr(b0, MemOperand(x1)), "ldr b0, [x1]");
  COMPARE(ldr(b2, MemOperand(x3, 1)), "ldr b2, [x3, #1]");
  COMPARE(ldr(b4, MemOperand(x5, 4095)), "ldr b4, [x5, #4095]");
  COMPARE(ldr(h6, MemOperand(x7)), "ldr h6, [x7]");
  COMPARE(ldr(h8, MemOperand(x9, 2)), "ldr h8, [x9, #2]");
  COMPARE(ldr(h10, MemOperand(x11, 8190)), "ldr h10, [x11, #8190]");
  COMPARE(ldr(q12, MemOperand(x13)), "ldr q12, [x13]");
  COMPARE(ldr(q14, MemOperand(x15, 16)), "ldr q14, [x15, #16]");
  COMPARE(ldr(q16, MemOperand(x17, 65520)), "ldr q16, [x17, #65520]");
  COMPARE(str(b18, MemOperand(x19)), "str b18, [x19]");
  COMPARE(str(b20, MemOperand(x21, 1)), "str b20, [x21, #1]");
  COMPARE(str(b22, MemOperand(x23, 4095)), "str b22, [x23, #4095]");
  COMPARE(str(h24, MemOperand(x25)), "str h24, [x25]");
  COMPARE(str(h26, MemOperand(x27, 2)), "str h26, [cp, #2]");
  COMPARE(str(h28, MemOperand(x29, 8190)), "str h28, [fp, #8190]");
  COMPARE(str(q30, MemOperand(x30)), "str q30, [lr]");
  COMPARE(str(q31, MemOperand(x1, 16)), "str q31, [x1, #16]");
  COMPARE(str(q0, MemOperand(x3, 65520)), "str q0, [x3, #65520]");

  COMPARE(ldr(s24, MemOperand(sp)), "ldr s24, [sp]");
  COMPARE(ldr(d25, MemOperand(sp, 8)), "ldr d25, [sp, #8]");
  COMPARE(ldr(b26, MemOperand(sp, 1)), "ldr b26, [sp, #1]");
  COMPARE(ldr(h27, MemOperand(sp, 2)), "ldr h27, [sp, #2]");
  COMPARE(ldr(q28, MemOperand(sp, 16)), "ldr q28, [sp, #16]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_v_pre) {
  SET_UP_ASM();

  COMPARE(ldr(s0, MemOperand(x1, 4, PreIndex)), "ldr s0, [x1, #4]!");
  COMPARE(ldr(s2, MemOperand(x3, 255, PreIndex)), "ldr s2, [x3, #255]!");
  COMPARE(ldr(s4, MemOperand(x5, -256, PreIndex)), "ldr s4, [x5, #-256]!");
  COMPARE(ldr(d6, MemOperand(x7, 8, PreIndex)), "ldr d6, [x7, #8]!");
  COMPARE(ldr(d8, MemOperand(x9, 255, PreIndex)), "ldr d8, [x9, #255]!");
  COMPARE(ldr(d10, MemOperand(x11, -256, PreIndex)), "ldr d10, [x11, #-256]!");

  COMPARE(str(s12, MemOperand(x13, 4, PreIndex)), "str s12, [x13, #4]!");
  COMPARE(str(s14, MemOperand(x15, 255, PreIndex)), "str s14, [x15, #255]!");
  COMPARE(str(s16, MemOperand(x17, -256, PreIndex)), "str s16, [x17, #-256]!");
  COMPARE(str(d18, MemOperand(x19, 8, PreIndex)), "str d18, [x19, #8]!");
  COMPARE(str(d20, MemOperand(x21, 255, PreIndex)), "str d20, [x21, #255]!");
  COMPARE(str(d22, MemOperand(x23, -256, PreIndex)), "str d22, [x23, #-256]!");

  COMPARE(ldr(b0, MemOperand(x1, 1, PreIndex)), "ldr b0, [x1, #1]!");
  COMPARE(ldr(b2, MemOperand(x3, 255, PreIndex)), "ldr b2, [x3, #255]!");
  COMPARE(ldr(b4, MemOperand(x5, -256, PreIndex)), "ldr b4, [x5, #-256]!");
  COMPARE(ldr(h6, MemOperand(x7, 2, PreIndex)), "ldr h6, [x7, #2]!");
  COMPARE(ldr(h8, MemOperand(x9, 255, PreIndex)), "ldr h8, [x9, #255]!");
  COMPARE(ldr(h10, MemOperand(x11, -256, PreIndex)), "ldr h10, [x11, #-256]!");
  COMPARE(ldr(q12, MemOperand(x13, 16, PreIndex)), "ldr q12, [x13, #16]!");
  COMPARE(ldr(q14, MemOperand(x15, 255, PreIndex)), "ldr q14, [x15, #255]!");
  COMPARE(ldr(q16, MemOperand(x17, -256, PreIndex)), "ldr q16, [x17, #-256]!");

  COMPARE(str(b18, MemOperand(x19, 1, PreIndex)), "str b18, [x19, #1]!");
  COMPARE(str(b20, MemOperand(x21, 255, PreIndex)), "str b20, [x21, #255]!");
  COMPARE(str(b22, MemOperand(x23, -256, PreIndex)), "str b22, [x23, #-256]!");
  COMPARE(str(h24, MemOperand(x25, 2, PreIndex)), "str h24, [x25, #2]!");
  COMPARE(str(h26, MemOperand(x27, 255, PreIndex)), "str h26, [cp, #255]!");
  COMPARE(str(h28, MemOperand(x29, -256, PreIndex)), "str h28, [fp, #-256]!");
  COMPARE(str(q30, MemOperand(x1, 16, PreIndex)), "str q30, [x1, #16]!");
  COMPARE(str(q31, MemOperand(x3, 255, PreIndex)), "str q31, [x3, #255]!");
  COMPARE(str(q0, MemOperand(x5, -256, PreIndex)), "str q0, [x5, #-256]!");

  COMPARE(str(b24, MemOperand(sp, 1, PreIndex)), "str b24, [sp, #1]!");
  COMPARE(str(h25, MemOperand(sp, -2, PreIndex)), "str h25, [sp, #-2]!");
  COMPARE(str(s26, MemOperand(sp, 4, PreIndex)), "str s26, [sp, #4]!");
  COMPARE(str(d27, MemOperand(sp, -8, PreIndex)), "str d27, [sp, #-8]!");
  COMPARE(str(q28, MemOperand(sp, 16, PreIndex)), "str q28, [sp, #16]!");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_v_post) {
  SET_UP_ASM();

  COMPARE(ldr(s0, MemOperand(x1, 4, PostIndex)), "ldr s0, [x1], #4");
  COMPARE(ldr(s2, MemOperand(x3, 255, PostIndex)), "ldr s2, [x3], #255");
  COMPARE(ldr(s4, MemOperand(x5, -256, PostIndex)), "ldr s4, [x5], #-256");
  COMPARE(ldr(d6, MemOperand(x7, 8, PostIndex)), "ldr d6, [x7], #8");
  COMPARE(ldr(d8, MemOperand(x9, 255, PostIndex)), "ldr d8, [x9], #255");
  COMPARE(ldr(d10, MemOperand(x11, -256, PostIndex)), "ldr d10, [x11], #-256");

  COMPARE(str(s12, MemOperand(x13, 4, PostIndex)), "str s12, [x13], #4");
  COMPARE(str(s14, MemOperand(x15, 255, PostIndex)), "str s14, [x15], #255");
  COMPARE(str(s16, MemOperand(x17, -256, PostIndex)), "str s16, [x17], #-256");
  COMPARE(str(d18, MemOperand(x19, 8, PostIndex)), "str d18, [x19], #8");
  COMPARE(str(d20, MemOperand(x21, 255, PostIndex)), "str d20, [x21], #255");
  COMPARE(str(d22, MemOperand(x23, -256, PostIndex)), "str d22, [x23], #-256");

  COMPARE(ldr(b0, MemOperand(x1, 4, PostIndex)), "ldr b0, [x1], #4");
  COMPARE(ldr(b2, MemOperand(x3, 255, PostIndex)), "ldr b2, [x3], #255");
  COMPARE(ldr(b4, MemOperand(x5, -256, PostIndex)), "ldr b4, [x5], #-256");
  COMPARE(ldr(h6, MemOperand(x7, 8, PostIndex)), "ldr h6, [x7], #8");
  COMPARE(ldr(h8, MemOperand(x9, 255, PostIndex)), "ldr h8, [x9], #255");
  COMPARE(ldr(h10, MemOperand(x11, -256, PostIndex)), "ldr h10, [x11], #-256");
  COMPARE(ldr(q12, MemOperand(x13, 8, PostIndex)), "ldr q12, [x13], #8");
  COMPARE(ldr(q14, MemOperand(x15, 255, PostIndex)), "ldr q14, [x15], #255");
  COMPARE(ldr(q16, MemOperand(x17, -256, PostIndex)), "ldr q16, [x17], #-256");

  COMPARE(str(b18, MemOperand(x19, 4, PostIndex)), "str b18, [x19], #4");
  COMPARE(str(b20, MemOperand(x21, 255, PostIndex)), "str b20, [x21], #255");
  COMPARE(str(b22, MemOperand(x23, -256, PostIndex)), "str b22, [x23], #-256");
  COMPARE(str(h24, MemOperand(x25, 8, PostIndex)), "str h24, [x25], #8");
  COMPARE(str(h26, MemOperand(x27, 255, PostIndex)), "str h26, [cp], #255");
  COMPARE(str(h28, MemOperand(x29, -256, PostIndex)), "str h28, [fp], #-256");
  COMPARE(str(q30, MemOperand(x1, 8, PostIndex)), "str q30, [x1], #8");
  COMPARE(str(q31, MemOperand(x3, 255, PostIndex)), "str q31, [x3], #255");
  COMPARE(str(q0, MemOperand(x5, -256, PostIndex)), "str q0, [x5], #-256");

  COMPARE(ldr(b24, MemOperand(sp, -1, PreIndex)), "ldr b24, [sp, #-1]!");
  COMPARE(ldr(h25, MemOperand(sp, 2, PreIndex)), "ldr h25, [sp, #2]!");
  COMPARE(ldr(s26, MemOperand(sp, -4, PreIndex)), "ldr s26, [sp, #-4]!");
  COMPARE(ldr(d27, MemOperand(sp, 8, PreIndex)), "ldr d27, [sp, #8]!");
  COMPARE(ldr(q28, MemOperand(sp, -16, PreIndex)), "ldr q28, [sp, #-16]!");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_v_regoffset) {
  SET_UP_ASM();

  COMPARE(ldr(b0, MemOperand(x1, x2)), "ldr b0, [x1, x2]");
  COMPARE(ldr(b1, MemOperand(x2, w3, UXTW)), "ldr b1, [x2, w3, uxtw]");
  COMPARE(ldr(b2, MemOperand(x3, w4, SXTW)), "ldr b2, [x3, w4, sxtw]");
  // We can't assemble this instruction, but we check it disassembles correctly.
  COMPARE(dci(0x3c657883), "ldr b3, [x4, x5, lsl #0]");
  COMPARE(ldr(b30, MemOperand(sp, xzr)), "ldr b30, [sp, xzr]");
  COMPARE(ldr(b31, MemOperand(sp, wzr, UXTW)), "ldr b31, [sp, wzr, uxtw]");

  COMPARE(ldr(h0, MemOperand(x1, x2)), "ldr h0, [x1, x2]");
  COMPARE(ldr(h1, MemOperand(x2, w3, UXTW)), "ldr h1, [x2, w3, uxtw]");
  COMPARE(ldr(h2, MemOperand(x3, w4, SXTW)), "ldr h2, [x3, w4, sxtw]");
  COMPARE(ldr(h3, MemOperand(x4, w5, UXTW, 1)), "ldr h3, [x4, w5, uxtw #1]");
  COMPARE(ldr(h4, MemOperand(x5, w5, SXTW, 1)), "ldr h4, [x5, w5, sxtw #1]");
  COMPARE(ldr(h30, MemOperand(sp, xzr)), "ldr h30, [sp, xzr]");
  COMPARE(ldr(h31, MemOperand(sp, wzr, SXTW, 1)),
          "ldr h31, [sp, wzr, sxtw #1]");

  COMPARE(ldr(s0, MemOperand(x1, x2)), "ldr s0, [x1, x2]");
  COMPARE(ldr(s1, MemOperand(x2, w3, UXTW)), "ldr s1, [x2, w3, uxtw]");
  COMPARE(ldr(s2, MemOperand(x3, w4, SXTW)), "ldr s2, [x3, w4, sxtw]");
  COMPARE(ldr(s3, MemOperand(x4, w5, UXTW, 2)), "ldr s3, [x4, w5, uxtw #2]");
  COMPARE(ldr(s4, MemOperand(x5, w5, SXTW, 2)), "ldr s4, [x5, w5, sxtw #2]");
  COMPARE(ldr(s30, MemOperand(sp, xzr)), "ldr s30, [sp, xzr]");
  COMPARE(ldr(s31, MemOperand(sp, wzr, SXTW, 2)),
          "ldr s31, [sp, wzr, sxtw #2]");

  COMPARE(ldr(d0, MemOperand(x1, x2)), "ldr d0, [x1, x2]");
  COMPARE(ldr(d1, MemOperand(x2, w3, UXTW)), "ldr d1, [x2, w3, uxtw]");
  COMPARE(ldr(d2, MemOperand(x3, w4, SXTW)), "ldr d2, [x3, w4, sxtw]");
  COMPARE(ldr(d3, MemOperand(x4, w5, UXTW, 3)), "ldr d3, [x4, w5, uxtw #3]");
  COMPARE(ldr(d4, MemOperand(x5, w5, SXTW, 3)), "ldr d4, [x5, w5, sxtw #3]");
  COMPARE(ldr(d30, MemOperand(sp, xzr)), "ldr d30, [sp, xzr]");
  COMPARE(ldr(d31, MemOperand(sp, wzr, SXTW, 3)),
          "ldr d31, [sp, wzr, sxtw #3]");

  COMPARE(ldr(q0, MemOperand(x1, x2)), "ldr q0, [x1, x2]");
  COMPARE(ldr(q1, MemOperand(x2, w3, UXTW)), "ldr q1, [x2, w3, uxtw]");
  COMPARE(ldr(q2, MemOperand(x3, w4, SXTW)), "ldr q2, [x3, w4, sxtw]");
  COMPARE(ldr(q3, MemOperand(x4, w5, UXTW, 4)), "ldr q3, [x4, w5, uxtw #4]");
  COMPARE(ldr(q4, MemOperand(x5, w5, SXTW, 4)), "ldr q4, [x5, w5, sxtw #4]");
  COMPARE(ldr(q30, MemOperand(sp, xzr)), "ldr q30, [sp, xzr]");
  COMPARE(ldr(q31, MemOperand(sp, wzr, SXTW, 4)),
          "ldr q31, [sp, wzr, sxtw #4]");

  COMPARE(str(b0, MemOperand(x1, x2)), "str b0, [x1, x2]");
  COMPARE(str(b1, MemOperand(x2, w3, UXTW)), "str b1, [x2, w3, uxtw]");
  COMPARE(str(b2, MemOperand(x3, w4, SXTW)), "str b2, [x3, w4, sxtw]");
  // We can't assemble this instruction, but we check it disassembles correctly.
  COMPARE(dci(0x3c257883), "str b3, [x4, x5, lsl #0]");
  COMPARE(str(b30, MemOperand(sp, xzr)), "str b30, [sp, xzr]");
  COMPARE(str(b31, MemOperand(sp, wzr, UXTW)), "str b31, [sp, wzr, uxtw]");

  COMPARE(str(h0, MemOperand(x1, x2)), "str h0, [x1, x2]");
  COMPARE(str(h1, MemOperand(x2, w3, UXTW)), "str h1, [x2, w3, uxtw]");
  COMPARE(str(h2, MemOperand(x3, w4, SXTW)), "str h2, [x3, w4, sxtw]");
  COMPARE(str(h3, MemOperand(x4, w5, UXTW, 1)), "str h3, [x4, w5, uxtw #1]");
  COMPARE(str(h4, MemOperand(x5, w5, SXTW, 1)), "str h4, [x5, w5, sxtw #1]");
  COMPARE(str(h30, MemOperand(sp, xzr)), "str h30, [sp, xzr]");
  COMPARE(str(h31, MemOperand(sp, wzr, SXTW, 1)),
          "str h31, [sp, wzr, sxtw #1]");

  COMPARE(str(s0, MemOperand(x1, x2)), "str s0, [x1, x2]");
  COMPARE(str(s1, MemOperand(x2, w3, UXTW)), "str s1, [x2, w3, uxtw]");
  COMPARE(str(s2, MemOperand(x3, w4, SXTW)), "str s2, [x3, w4, sxtw]");
  COMPARE(str(s3, MemOperand(x4, w5, UXTW, 2)), "str s3, [x4, w5, uxtw #2]");
  COMPARE(str(s4, MemOperand(x5, w5, SXTW, 2)), "str s4, [x5, w5, sxtw #2]");
  COMPARE(str(s30, MemOperand(sp, xzr)), "str s30, [sp, xzr]");
  COMPARE(str(s31, MemOperand(sp, wzr, SXTW, 2)),
          "str s31, [sp, wzr, sxtw #2]");

  COMPARE(str(d0, MemOperand(x1, x2)), "str d0, [x1, x2]");
  COMPARE(str(d1, MemOperand(x2, w3, UXTW)), "str d1, [x2, w3, uxtw]");
  COMPARE(str(d2, MemOperand(x3, w4, SXTW)), "str d2, [x3, w4, sxtw]");
  COMPARE(str(d3, MemOperand(x4, w5, UXTW, 3)), "str d3, [x4, w5, uxtw #3]");
  COMPARE(str(d4, MemOperand(x5, w5, SXTW, 3)), "str d4, [x5, w5, sxtw #3]");
  COMPARE(str(d30, MemOperand(sp, xzr)), "str d30, [sp, xzr]");
  COMPARE(str(d31, MemOperand(sp, wzr, SXTW, 3)),
          "str d31, [sp, wzr, sxtw #3]");

  COMPARE(str(q0, MemOperand(x1, x2)), "str q0, [x1, x2]");
  COMPARE(str(q1, MemOperand(x2, w3, UXTW)), "str q1, [x2, w3, uxtw]");
  COMPARE(str(q2, MemOperand(x3, w4, SXTW)), "str q2, [x3, w4, sxtw]");
  COMPARE(str(q3, MemOperand(x4, w5, UXTW, 4)), "str q3, [x4, w5, uxtw #4]");
  COMPARE(str(q4, MemOperand(x5, w5, SXTW, 4)), "str q4, [x5, w5, sxtw #4]");
  COMPARE(str(q30, MemOperand(sp, xzr)), "str q30, [sp, xzr]");
  COMPARE(str(q31, MemOperand(sp, wzr, SXTW, 4)),
          "str q31, [sp, wzr, sxtw #4]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_unscaled) {
  SET_UP_ASM();

  COMPARE(ldr(w0, MemOperand(x1, 1)), "ldur w0, [x1, #1]");
  COMPARE(ldr(w2, MemOperand(x3, -1)), "ldur w2, [x3, #-1]");
  COMPARE(ldr(w4, MemOperand(x5, 255)), "ldur w4, [x5, #255]");
  COMPARE(ldr(w6, MemOperand(x7, -256)), "ldur w6, [x7, #-256]");
  COMPARE(ldr(x8, MemOperand(x9, 1)), "ldur x8, [x9, #1]");
  COMPARE(ldr(x10, MemOperand(x11, -1)), "ldur x10, [x11, #-1]");
  COMPARE(ldr(x12, MemOperand(x13, 255)), "ldur x12, [x13, #255]");
  COMPARE(ldr(x14, MemOperand(x15, -256)), "ldur x14, [x15, #-256]");
  COMPARE(str(w16, MemOperand(x17, 1)), "stur w16, [x17, #1]");
  COMPARE(str(w18, MemOperand(x19, -1)), "stur w18, [x19, #-1]");
  COMPARE(str(w20, MemOperand(x21, 255)), "stur w20, [x21, #255]");
  COMPARE(str(w22, MemOperand(x23, -256)), "stur w22, [x23, #-256]");
  COMPARE(str(x24, MemOperand(x25, 1)), "stur x24, [x25, #1]");
  COMPARE(str(x26, MemOperand(x27, -1)), "stur x26, [cp, #-1]");
  COMPARE(str(x28, MemOperand(x29, 255)), "stur x28, [fp, #255]");
  COMPARE(str(x30, MemOperand(x0, -256)), "stur lr, [x0, #-256]");
  COMPARE(ldr(w0, MemOperand(sp, 1)), "ldur w0, [sp, #1]");
  COMPARE(str(x1, MemOperand(sp, -1)), "stur x1, [sp, #-1]");
  COMPARE(ldrb(w2, MemOperand(x3, -2)), "ldurb w2, [x3, #-2]");
  COMPARE(ldrsb(w4, MemOperand(x5, -3)), "ldursb w4, [x5, #-3]");
  COMPARE(ldrsb(x6, MemOperand(x7, -4)), "ldursb x6, [x7, #-4]");
  COMPARE(ldrh(w8, MemOperand(x9, -5)), "ldurh w8, [x9, #-5]");
  COMPARE(ldrsh(w10, MemOperand(x11, -6)), "ldursh w10, [x11, #-6]");
  COMPARE(ldrsh(x12, MemOperand(x13, -7)), "ldursh x12, [x13, #-7]");
  COMPARE(ldrsw(x14, MemOperand(x15, -8)), "ldursw x14, [x15, #-8]");

  COMPARE(ldr(b0, MemOperand(x1, -1)), "ldur b0, [x1, #-1]");
  COMPARE(ldr(h2, MemOperand(x3, -1)), "ldur h2, [x3, #-1]");
  COMPARE(ldr(s4, MemOperand(x5, 255)), "ldur s4, [x5, #255]");
  COMPARE(ldr(d6, MemOperand(x7, -256)), "ldur d6, [x7, #-256]");
  COMPARE(ldr(q8, MemOperand(x9, 1)), "ldur q8, [x9, #1]");
  COMPARE(str(b16, MemOperand(x17, -1)), "stur b16, [x17, #-1]");
  COMPARE(str(h18, MemOperand(x19, -1)), "stur h18, [x19, #-1]");
  COMPARE(str(s20, MemOperand(x21, 255)), "stur s20, [x21, #255]");
  COMPARE(str(d22, MemOperand(x23, -256)), "stur d22, [x23, #-256]");
  COMPARE(str(q24, MemOperand(x25, 1)), "stur q24, [x25, #1]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_pair) {
  SET_UP_ASM();

  COMPARE(ldp(w0, w1, MemOperand(x2)), "ldp w0, w1, [x2]");
  COMPARE(ldp(x3, x4, MemOperand(x5)), "ldp x3, x4, [x5]");
  COMPARE(ldp(w6, w7, MemOperand(x8, 4)), "ldp w6, w7, [x8, #4]");
  COMPARE(ldp(x9, x10, MemOperand(x11, 8)), "ldp x9, x10, [x11, #8]");
  COMPARE(ldp(w12, w13, MemOperand(x14, 252)), "ldp w12, w13, [x14, #252]");
  COMPARE(ldp(x15, x16, MemOperand(x17, 504)), "ldp x15, x16, [x17, #504]");
  COMPARE(ldp(w18, w19, MemOperand(x20, -256)), "ldp w18, w19, [x20, #-256]");
  COMPARE(ldp(x21, x22, MemOperand(x23, -512)), "ldp x21, x22, [x23, #-512]");
  COMPARE(ldp(w24, w25, MemOperand(x26, 252, PreIndex)),
          "ldp w24, w25, [x26, #252]!");
  COMPARE(ldp(cp, x28, MemOperand(fp, 504, PreIndex)),
          "ldp cp, x28, [fp, #504]!");
  COMPARE(ldp(w30, w0, MemOperand(x1, -256, PreIndex)),
          "ldp w30, w0, [x1, #-256]!");
  COMPARE(ldp(x2, x3, MemOperand(x4, -512, PreIndex)),
          "ldp x2, x3, [x4, #-512]!");
  COMPARE(ldp(w5, w6, MemOperand(x7, 252, PostIndex)),
          "ldp w5, w6, [x7], #252");
  COMPARE(ldp(x8, x9, MemOperand(x10, 504, PostIndex)),
          "ldp x8, x9, [x10], #504");
  COMPARE(ldp(w11, w12, MemOperand(x13, -256, PostIndex)),
          "ldp w11, w12, [x13], #-256");
  COMPARE(ldp(x14, x15, MemOperand(x16, -512, PostIndex)),
          "ldp x14, x15, [x16], #-512");

  COMPARE(ldp(s17, s18, MemOperand(x19)), "ldp s17, s18, [x19]");
  COMPARE(ldp(s20, s21, MemOperand(x22, 252)), "ldp s20, s21, [x22, #252]");
  COMPARE(ldp(s23, s24, MemOperand(x25, -256)), "ldp s23, s24, [x25, #-256]");
  COMPARE(ldp(s26, s27, MemOperand(x28, 252, PreIndex)),
          "ldp s26, s27, [x28, #252]!");
  COMPARE(ldp(s29, s30, MemOperand(fp, -256, PreIndex)),
          "ldp s29, s30, [fp, #-256]!");
  COMPARE(ldp(s31, s0, MemOperand(x1, 252, PostIndex)),
          "ldp s31, s0, [x1], #252");
  COMPARE(ldp(s2, s3, MemOperand(x4, -256, PostIndex)),
          "ldp s2, s3, [x4], #-256");
  COMPARE(ldp(d17, d18, MemOperand(x19)), "ldp d17, d18, [x19]");
  COMPARE(ldp(d20, d21, MemOperand(x22, 504)), "ldp d20, d21, [x22, #504]");
  COMPARE(ldp(d23, d24, MemOperand(x25, -512)), "ldp d23, d24, [x25, #-512]");
  COMPARE(ldp(d26, d27, MemOperand(x28, 504, PreIndex)),
          "ldp d26, d27, [x28, #504]!");
  COMPARE(ldp(d29, d30, MemOperand(fp, -512, PreIndex)),
          "ldp d29, d30, [fp, #-512]!");
  COMPARE(ldp(d31, d0, MemOperand(x1, 504, PostIndex)),
          "ldp d31, d0, [x1], #504");
  COMPARE(ldp(d2, d3, MemOperand(x4, -512, PostIndex)),
          "ldp d2, d3, [x4], #-512");

  COMPARE(stp(w0, w1, MemOperand(x2)), "stp w0, w1, [x2]");
  COMPARE(stp(x3, x4, MemOperand(x5)), "stp x3, x4, [x5]");
  COMPARE(stp(w6, w7, MemOperand(x8, 4)), "stp w6, w7, [x8, #4]");
  COMPARE(stp(x9, x10, MemOperand(x11, 8)), "stp x9, x10, [x11, #8]");
  COMPARE(stp(w12, w13, MemOperand(x14, 252)), "stp w12, w13, [x14, #252]");
  COMPARE(stp(x15, x16, MemOperand(x17, 504)), "stp x15, x16, [x17, #504]");
  COMPARE(stp(w18, w19, MemOperand(x20, -256)), "stp w18, w19, [x20, #-256]");
  COMPARE(stp(x21, x22, MemOperand(x23, -512)), "stp x21, x22, [x23, #-512]");
  COMPARE(stp(w24, w25, MemOperand(x26, 252, PreIndex)),
          "stp w24, w25, [x26, #252]!");
  COMPARE(stp(cp, x28, MemOperand(fp, 504, PreIndex)),
          "stp cp, x28, [fp, #504]!");
  COMPARE(stp(w30, w0, MemOperand(x1, -256, PreIndex)),
          "stp w30, w0, [x1, #-256]!");
  COMPARE(stp(x2, x3, MemOperand(x4, -512, PreIndex)),
          "stp x2, x3, [x4, #-512]!");
  COMPARE(stp(w5, w6, MemOperand(x7, 252, PostIndex)),
          "stp w5, w6, [x7], #252");
  COMPARE(stp(x8, x9, MemOperand(x10, 504, PostIndex)),
          "stp x8, x9, [x10], #504");
  COMPARE(stp(w11, w12, MemOperand(x13, -256, PostIndex)),
          "stp w11, w12, [x13], #-256");
  COMPARE(stp(x14, x15, MemOperand(x16, -512, PostIndex)),
          "stp x14, x15, [x16], #-512");

  COMPARE(stp(s17, s18, MemOperand(x19)), "stp s17, s18, [x19]");
  COMPARE(stp(s20, s21, MemOperand(x22, 252)), "stp s20, s21, [x22, #252]");
  COMPARE(stp(s23, s24, MemOperand(x25, -256)), "stp s23, s24, [x25, #-256]");
  COMPARE(stp(s26, s27, MemOperand(x28, 252, PreIndex)),
          "stp s26, s27, [x28, #252]!");
  COMPARE(stp(s29, s30, MemOperand(fp, -256, PreIndex)),
          "stp s29, s30, [fp, #-256]!");
  COMPARE(stp(s31, s0, MemOperand(x1, 252, PostIndex)),
          "stp s31, s0, [x1], #252");
  COMPARE(stp(s2, s3, MemOperand(x4, -256, PostIndex)),
          "stp s2, s3, [x4], #-256");
  COMPARE(stp(d17, d18, MemOperand(x19)), "stp d17, d18, [x19]");
  COMPARE(stp(d20, d21, MemOperand(x22, 504)), "stp d20, d21, [x22, #504]");
  COMPARE(stp(d23, d24, MemOperand(x25, -512)), "stp d23, d24, [x25, #-512]");
  COMPARE(stp(d26, d27, MemOperand(x28, 504, PreIndex)),
          "stp d26, d27, [x28, #504]!");
  COMPARE(stp(d29, d30, MemOperand(fp, -512, PreIndex)),
          "stp d29, d30, [fp, #-512]!");
  COMPARE(stp(d31, d0, MemOperand(x1, 504, PostIndex)),
          "stp d31, d0, [x1], #504");
  COMPARE(stp(d2, d3, MemOperand(x4, -512, PostIndex)),
          "stp d2, d3, [x4], #-512");

  COMPARE(stp(q5, q6, MemOperand(x7)), "stp q5, q6, [x7]");
  COMPARE(stp(q8, q9, MemOperand(x10, 1008)), "stp q8, q9, [x10, #1008]");
  COMPARE(stp(q11, q12, MemOperand(x13, -1024)), "stp q11, q12, [x13, #-1024]");
  COMPARE(stp(q14, q15, MemOperand(x16, 1008, PreIndex)),
          "stp q14, q15, [x16, #1008]!");
  COMPARE(stp(q17, q18, MemOperand(x19, -1024, PreIndex)),
          "stp q17, q18, [x19, #-1024]!");
  COMPARE(stp(q20, q21, MemOperand(x22, 1008, PostIndex)),
          "stp q20, q21, [x22], #1008");
  COMPARE(stp(q23, q24, MemOperand(x25, -1024, PostIndex)),
          "stp q23, q24, [x25], #-1024");

  COMPARE(ldp(w16, w17, MemOperand(x28, 4, PostIndex)),
          "ldp w16, w17, [x28], #4");
  COMPARE(stp(x18, x19, MemOperand(x28, -8, PreIndex)),
          "stp x18, x19, [x28, #-8]!");
  COMPARE(ldp(s30, s31, MemOperand(x28, 12, PostIndex)),
          "ldp s30, s31, [x28], #12");
  COMPARE(stp(d30, d31, MemOperand(x28, -16)), "stp d30, d31, [x28, #-16]");
  COMPARE(ldp(q30, q31, MemOperand(x28, 32, PostIndex)),
          "ldp q30, q31, [x28], #32");

  COMPARE(ldpsw(x0, x1, MemOperand(x2)), "ldpsw x0, x1, [x2]");
  COMPARE(ldpsw(x3, x4, MemOperand(x5, 16)), "ldpsw x3, x4, [x5, #16]");
  COMPARE(ldpsw(x6, x7, MemOperand(x8, -32, PreIndex)),
          "ldpsw x6, x7, [x8, #-32]!");
  COMPARE(ldpsw(x9, x10, MemOperand(x11, 128, PostIndex)),
          "ldpsw x9, x10, [x11], #128");

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_store_acquire_release) {
  SET_UP_MASM();

  COMPARE(ldar(w0, x1), "ldar w0, [x1]");
  COMPARE(ldarb(w2, x3), "ldarb w2, [x3]");
  COMPARE(ldarh(w4, x5), "ldarh w4, [x5]");
  COMPARE(ldaxr(w6, x7), "ldaxr w6, [x7]");
  COMPARE(ldaxrb(w8, x9), "ldaxrb w8, [x9]");
  COMPARE(ldaxrh(w10, x11), "ldaxrh w10, [x11]");
  COMPARE(stlr(w12, x13), "stlr w12, [x13]");
  COMPARE(stlrb(w14, x15), "stlrb w14, [x15]");
  COMPARE(stlrh(w16, x17), "stlrh w16, [x17]");
  COMPARE(stlxr(w18, w19, x20), "stlxr w18, w19, [x20]");
  COMPARE(stlxrb(w21, w22, x23), "stlxrb w21, w22, [x23]");
  COMPARE(stlxrh(w24, w25, x26), "stlxrh w24, w25, [x26]");

  COMPARE(ldarb(wzr, sp), "ldarb wzr, [sp]");
  COMPARE(ldarh(wzr, sp), "ldarh wzr, [sp]");
  COMPARE(ldar(wzr, sp), "ldar wzr, [sp]");
  COMPARE(stlrb(wzr, sp), "stlrb wzr, [sp]");
  COMPARE(stlrh(wzr, sp), "stlrh wzr, [sp]");
  COMPARE(stlr(wzr, sp), "stlr wzr, [sp]");
  COMPARE(ldaxrb(wzr, sp), "ldaxrb wzr, [sp]");
  COMPARE(ldaxrh(wzr, sp), "ldaxrh wzr, [sp]");
  COMPARE(ldaxr(wzr, sp), "ldaxr wzr, [sp]");
  COMPARE(stlxrb(w0, wzr, sp), "stlxrb w0, wzr, [sp]");
  COMPARE(stlxrh(wzr, w1, sp), "stlxrh wzr, w1, [sp]");
  COMPARE(stlxr(w2, wzr, sp), "stlxr w2, wzr, [sp]");

  CpuFeatureScope feature_scope(assm, LSE,
                                CpuFeatureScope::kDontCheckSupported);

  COMPARE(cas(w30, w0, MemOperand(x1)), "cas w30, w0, [x1]");
  COMPARE(cas(w2, w3, MemOperand(sp)), "cas w2, w3, [sp]");
  COMPARE(cas(x4, x5, MemOperand(x6)), "cas x4, x5, [x6]");
  COMPARE(cas(x7, x8, MemOperand(sp)), "cas x7, x8, [sp]");
  COMPARE(casa(w9, w10, MemOperand(x11)), "casa w9, w10, [x11]");
  COMPARE(casa(w12, w13, MemOperand(sp)), "casa w12, w13, [sp]");
  COMPARE(casa(x14, x15, MemOperand(x16)), "casa x14, x15, [x16]");
  COMPARE(casa(x17, x18, MemOperand(sp)), "casa x17, x18, [sp]");
  COMPARE(casl(w19, w20, MemOperand(x21)), "casl w19, w20, [x21]");
  COMPARE(casl(w22, w23, MemOperand(sp)), "casl w22, w23, [sp]");
  COMPARE(casl(x24, x25, MemOperand(x26)), "casl x24, x25, [x26]");
  COMPARE(casl(x27, x28, MemOperand(sp)), "casl cp, x28, [sp]");
  COMPARE(casal(w29, w30, MemOperand(x0)), "casal w29, w30, [x0]");
  COMPARE(casal(w1, w2, MemOperand(sp)), "casal w1, w2, [sp]");
  COMPARE(casal(x3, x4, MemOperand(x5)), "casal x3, x4, [x5]");
  COMPARE(casal(x6, x7, MemOperand(sp)), "casal x6, x7, [sp]");
  COMPARE(casb(w8, w9, MemOperand(x10)), "casb w8, w9, [x10]");
  COMPARE(casb(w11, w12, MemOperand(sp)), "casb w11, w12, [sp]");
  COMPARE(casab(w13, w14, MemOperand(x15)), "casab w13, w14, [x15]");
  COMPARE(casab(w16, w17, MemOperand(sp)), "casab w16, w17, [sp]");
  COMPARE(caslb(w18, w19, MemOperand(x20)), "caslb w18, w19, [x20]");
  COMPARE(caslb(w21, w22, MemOperand(sp)), "caslb w21, w22, [sp]");
  COMPARE(casalb(w23, w24, MemOperand(x25)), "casalb w23, w24, [x25]");
  COMPARE(casalb(w26, w27, MemOperand(sp)), "casalb w26, w27, [sp]");
  COMPARE(cash(w28, w29, MemOperand(x30)), "cash w28, w29, [lr]");
  COMPARE(cash(w0, w1, MemOperand(sp)), "cash w0, w1, [sp]");
  COMPARE(casah(w2, w3, MemOperand(x4)), "casah w2, w3, [x4]");
  COMPARE(casah(w5, w6, MemOperand(sp)), "casah w5, w6, [sp]");
  COMPARE(caslh(w7, w8, MemOperand(x9)), "caslh w7, w8, [x9]");
  COMPARE(caslh(w10, w11, MemOperand(sp)), "caslh w10, w11, [sp]");
  COMPARE(casalh(w12, w13, MemOperand(x14)), "casalh w12, w13, [x14]");
  COMPARE(casalh(w15, w16, MemOperand(sp)), "casalh w15, w16, [sp]");
  COMPARE(casp(w18, w19, w20, w21, MemOperand(x22)),
          "casp w18, w19, w20, w21, [x22]");
  COMPARE(casp(w24, w25, w26, w27, MemOperand(sp)),
          "casp w24, w25, w26, w27, [sp]");
  COMPARE(casp(x28, x29, x0, x1, MemOperand(x2)), "casp x28, fp, x0, x1, [x2]");
  COMPARE(casp(x4, x5, x6, x7, MemOperand(sp)), "casp x4, x5, x6, x7, [sp]");
  COMPARE(caspa(w8, w9, w10, w11, MemOperand(x12)),
          "caspa w8, w9, w10, w11, [x12]");
  COMPARE(caspa(w14, w15, w16, w17, MemOperand(sp)),
          "caspa w14, w15, w16, w17, [sp]");
  COMPARE(caspa(x18, x19, x20, x21, MemOperand(x22)),
          "caspa x18, x19, x20, x21, [x22]");
  COMPARE(caspa(x24, x25, x26, x27, MemOperand(sp)),
          "caspa x24, x25, x26, cp, [sp]");
  COMPARE(caspl(w28, w29, w0, w1, MemOperand(x2)),
          "caspl w28, w29, w0, w1, [x2]");
  COMPARE(caspl(w4, w5, w6, w7, MemOperand(sp)), "caspl w4, w5, w6, w7, [sp]");
  COMPARE(caspl(x8, x9, x10, x11, MemOperand(x12)),
          "caspl x8, x9, x10, x11, [x12]");
  COMPARE(caspl(x14, x15, x16, x17, MemOperand(sp)),
          "caspl x14, x15, x16, x17, [sp]");
  COMPARE(caspal(w18, w19, w20, w21, MemOperand(x22)),
          "caspal w18, w19, w20, w21, [x22]");
  COMPARE(caspal(w24, w25, w26, w27, MemOperand(sp)),
          "caspal w24, w25, w26, w27, [sp]");
  COMPARE(caspal(x28, x29, x0, x1, MemOperand(x2)),
          "caspal x28, fp, x0, x1, [x2]");
  COMPARE(caspal(x4, x5, x6, x7, MemOperand(sp)),
          "caspal x4, x5, x6, x7, [sp]");

  CLEANUP();
}

#define ATOMIC_MEMORY_DISASM_LIST(V, DEF) \
  V(DEF, add, "add")                      \
  V(DEF, clr, "clr")                      \
  V(DEF, eor, "eor")                      \
  V(DEF, set, "set")                      \
  V(DEF, smax, "smax")                    \
  V(DEF, smin, "smin")                    \
  V(DEF, umax, "umax")                    \
  V(DEF, umin, "umin")

#define ATOMIC_MEMORY_DISASM_STORE_X_MODES(V, NAME, STR) \
  V(NAME, STR)                                           \
  V(NAME##l, STR "l")

#define ATOMIC_MEMORY_DISASM_STORE_W_MODES(V, NAME, STR) \
  ATOMIC_MEMORY_DISASM_STORE_X_MODES(V, NAME, STR)       \
  V(NAME##b, STR "b")                                    \
  V(NAME##lb, STR "lb")                                  \
  V(NAME##h, STR "h")                                    \
  V(NAME##lh, STR "lh")

#define ATOMIC_MEMORY_DISASM_LOAD_X_MODES(V, NAME, STR) \
  ATOMIC_MEMORY_DISASM_STORE_X_MODES(V, NAME, STR)      \
  V(NAME##a, STR "a")                                   \
  V(NAME##al, STR "al")

#define ATOMIC_MEMORY_DISASM_LOAD_W_MODES(V, NAME, STR) \
  ATOMIC_MEMORY_DISASM_LOAD_X_MODES(V, NAME, STR)       \
  V(NAME##ab, STR "ab")                                 \
  V(NAME##alb, STR "alb")                               \
  V(NAME##ah, STR "ah")                                 \
  V(NAME##alh, STR "alh")

TEST_F(DisasmArm64Test, atomic_memory) {
  SET_UP_MASM();

  CpuFeatureScope feature_scope(assm, LSE,
                                CpuFeatureScope::kDontCheckSupported);

  // These macros generate tests for all the variations of the atomic memory
  // operations, e.g. ldadd, ldadda, ldaddb, staddl, etc.

#define AM_LOAD_X_TESTS(N, MN)                                     \
  COMPARE(ld##N(x0, x1, MemOperand(x2)), "ld" MN " x0, x1, [x2]"); \
  COMPARE(ld##N(x3, x4, MemOperand(sp)), "ld" MN " x3, x4, [sp]");
#define AM_LOAD_W_TESTS(N, MN)                                     \
  COMPARE(ld##N(w0, w1, MemOperand(x2)), "ld" MN " w0, w1, [x2]"); \
  COMPARE(ld##N(w3, w4, MemOperand(sp)), "ld" MN " w3, w4, [sp]");
#define AM_STORE_X_TESTS(N, MN)                            \
  COMPARE(st##N(x0, MemOperand(x1)), "st" MN " x0, [x1]"); \
  COMPARE(st##N(x2, MemOperand(sp)), "st" MN " x2, [sp]");
#define AM_STORE_W_TESTS(N, MN)                            \
  COMPARE(st##N(w0, MemOperand(x1)), "st" MN " w0, [x1]"); \
  COMPARE(st##N(w2, MemOperand(sp)), "st" MN " w2, [sp]");

  ATOMIC_MEMORY_DISASM_LIST(ATOMIC_MEMORY_DISASM_LOAD_X_MODES, AM_LOAD_X_TESTS)
  ATOMIC_MEMORY_DISASM_LIST(ATOMIC_MEMORY_DISASM_LOAD_W_MODES, AM_LOAD_W_TESTS)
  ATOMIC_MEMORY_DISASM_LIST(ATOMIC_MEMORY_DISASM_STORE_X_MODES,
                            AM_STORE_X_TESTS)
  ATOMIC_MEMORY_DISASM_LIST(ATOMIC_MEMORY_DISASM_STORE_W_MODES,
                            AM_STORE_W_TESTS)

#define AM_SWP_X_TESTS(N, MN)                             \
  COMPARE(N(x0, x1, MemOperand(x2)), MN " x0, x1, [x2]"); \
  COMPARE(N(x3, x4, MemOperand(sp)), MN " x3, x4, [sp]");
#define AM_SWP_W_TESTS(N, MN)                             \
  COMPARE(N(w0, w1, MemOperand(x2)), MN " w0, w1, [x2]"); \
  COMPARE(N(w3, w4, MemOperand(sp)), MN " w3, w4, [sp]");

  ATOMIC_MEMORY_DISASM_LOAD_X_MODES(AM_SWP_X_TESTS, swp, "swp")
  ATOMIC_MEMORY_DISASM_LOAD_W_MODES(AM_SWP_W_TESTS, swp, "swp")

#undef AM_LOAD_X_TESTS
#undef AM_LOAD_W_TESTS
#undef AM_STORE_X_TESTS
#undef AM_STORE_W_TESTS
#undef AM_SWP_X_TESTS
#undef AM_SWP_W_TESTS

  CLEANUP();
}

TEST_F(DisasmArm64Test, load_literal) {
  SET_UP_ASM();

  COMPARE_PREFIX(ldr_pcrel(x10, 0), "ldr x10, pc+0");
  COMPARE_PREFIX(ldr_pcrel(x10, 1), "ldr x10, pc+4");
  COMPARE_PREFIX(ldr_pcrel(d11, 0), "ldr d11, pc+0");
  COMPARE_PREFIX(ldr_pcrel(d11, 1), "ldr d11, pc+4");

  int max_offset = (kMaxLoadLiteralRange >> kLoadLiteralScaleLog2) - 1;
  COMPARE_PREFIX(ldr_pcrel(x0, max_offset), "ldr x0, pc+1048572");
  COMPARE_PREFIX(ldr_pcrel(d0, max_offset), "ldr d0, pc+1048572");

  CLEANUP();
}

TEST_F(DisasmArm64Test, cond_select) {
  SET_UP_ASM();

  COMPARE(csel(w0, w1, w2, eq), "csel w0, w1, w2, eq");
  COMPARE(csel(x3, x4, x5, ne), "csel x3, x4, x5, ne");
  COMPARE(csinc(w6, w7, w8, hs), "csinc w6, w7, w8, hs");
  COMPARE(csinc(x9, x10, x11, lo), "csinc x9, x10, x11, lo");
  COMPARE(csinv(w12, w13, w14, mi), "csinv w12, w13, w14, mi");
  COMPARE(csinv(x15, x16, x17, pl), "csinv x15, x16, x17, pl");
  COMPARE(csneg(w18, w19, w20, vs), "csneg w18, w19, w20, vs");
  COMPARE(csneg(x21, x22, x23, vc), "csneg x21, x22, x23, vc");
  COMPARE(cset(w24, hi), "cset w24, hi");
  COMPARE(cset(x25, ls), "cset x25, ls");
  COMPARE(csetm(w26, ge), "csetm w26, ge");
  COMPARE(csetm(cp, lt), "csetm cp, lt");
  COMPARE(cinc(w28, w29, gt), "cinc w28, w29, gt");
  COMPARE(cinc(lr, x0, le), "cinc lr, x0, le");
  COMPARE(cinv(w1, w2, eq), "cinv w1, w2, eq");
  COMPARE(cinv(x3, x4, ne), "cinv x3, x4, ne");
  COMPARE(cneg(w5, w6, hs), "cneg w5, w6, hs");
  COMPARE(cneg(x7, x8, lo), "cneg x7, x8, lo");

  COMPARE(csel(x0, x1, x2, al), "csel x0, x1, x2, al");
  COMPARE(csel(x1, x2, x3, nv), "csel x1, x2, x3, nv");
  COMPARE(csinc(x2, x3, x4, al), "csinc x2, x3, x4, al");
  COMPARE(csinc(x3, x4, x5, nv), "csinc x3, x4, x5, nv");
  COMPARE(csinv(x4, x5, x6, al), "csinv x4, x5, x6, al");
  COMPARE(csinv(x5, x6, x7, nv), "csinv x5, x6, x7, nv");
  COMPARE(csneg(x6, x7, x8, al), "csneg x6, x7, x8, al");
  COMPARE(csneg(x7, x8, x9, nv), "csneg x7, x8, x9, nv");

  CLEANUP();
}

TEST_F(DisasmArm64Test, cond_select_macro) {
  SET_UP_MASM();

  COMPARE(Csel(w0, w1, -1, eq), "csinv w0, w1, wzr, eq");
  COMPARE(Csel(w2, w3, 0, ne), "csel w2, w3, wzr, ne");
  COMPARE(Csel(w4, w5, 1, hs), "csinc w4, w5, wzr, hs");
  COMPARE(Csel(x6, x7, -1, lo), "csinv x6, x7, xzr, lo");
  COMPARE(Csel(x8, x9, 0, mi), "csel x8, x9, xzr, mi");
  COMPARE(Csel(x10, x11, 1, pl), "csinc x10, x11, xzr, pl");

  CLEANUP();
}

TEST_F(DisasmArm64Test, cond_cmp) {
  SET_UP_ASM();

  COMPARE(ccmn(w0, w1, NZCVFlag, eq), "ccmn w0, w1, #NZCV, eq");
  COMPARE(ccmn(x2, x3, NZCFlag, ne), "ccmn x2, x3, #NZCv, ne");
  COMPARE(ccmp(w4, w5, NZVFlag, hs), "ccmp w4, w5, #NZcV, hs");
  COMPARE(ccmp(x6, x7, NZFlag, lo), "ccmp x6, x7, #NZcv, lo");
  COMPARE(ccmn(w8, 31, NFlag, mi), "ccmn w8, #31, #Nzcv, mi");
  COMPARE(ccmn(x9, 30, NCFlag, pl), "ccmn x9, #30, #NzCv, pl");
  COMPARE(ccmp(w10, 29, NVFlag, vs), "ccmp w10, #29, #NzcV, vs");
  COMPARE(ccmp(x11, 28, NFlag, vc), "ccmp x11, #28, #Nzcv, vc");
  COMPARE(ccmn(w12, w13, NoFlag, al), "ccmn w12, w13, #nzcv, al");
  COMPARE(ccmp(x14, 27, ZVFlag, nv), "ccmp x14, #27, #nZcV, nv");

  CLEANUP();
}

TEST_F(DisasmArm64Test, cond_cmp_macro) {
  SET_UP_MASM();

  COMPARE(Ccmp(w0, -1, VFlag, hi), "ccmn w0, #1, #nzcV, hi");
  COMPARE(Ccmp(x1, -31, CFlag, ge), "ccmn x1, #31, #nzCv, ge");
  COMPARE(Ccmn(w2, -1, CVFlag, gt), "ccmp w2, #1, #nzCV, gt");
  COMPARE(Ccmn(x3, -31, ZCVFlag, ls), "ccmp x3, #31, #nZCV, ls");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fmov_imm) {
  SET_UP_ASM();

  COMPARE(fmov(s0, 1.0f), "fmov s0, #0x70 (1.0000)");
  COMPARE(fmov(s31, -13.0f), "fmov s31, #0xaa (-13.0000)");
  COMPARE(fmov(d1, 1.0), "fmov d1, #0x70 (1.0000)");
  COMPARE(fmov(d29, -13.0), "fmov d29, #0xaa (-13.0000)");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fmov_reg) {
  SET_UP_ASM();

  COMPARE(fmov(w3, s13), "fmov w3, s13");
  COMPARE(fmov(x6, d26), "fmov x6, d26");
  COMPARE(fmov(s11, w30), "fmov s11, w30");
  COMPARE(fmov(d31, x2), "fmov d31, x2");
  COMPARE(fmov(s12, s13), "fmov s12, s13");
  COMPARE(fmov(d22, d23), "fmov d22, d23");
  COMPARE(fmov(v0.D(), 1, x13), "fmov v0.D[1], x13");
  COMPARE(fmov(x13, v0.D(), 1), "fmov x13, v0.D[1]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_dp1) {
  SET_UP_ASM();

  COMPARE(fabs(s0, s1), "fabs s0, s1");
  COMPARE(fabs(s31, s30), "fabs s31, s30");
  COMPARE(fabs(d2, d3), "fabs d2, d3");
  COMPARE(fabs(d31, d30), "fabs d31, d30");
  COMPARE(fneg(s4, s5), "fneg s4, s5");
  COMPARE(fneg(s31, s30), "fneg s31, s30");
  COMPARE(fneg(d6, d7), "fneg d6, d7");
  COMPARE(fneg(d31, d30), "fneg d31, d30");
  COMPARE(fsqrt(s8, s9), "fsqrt s8, s9");
  COMPARE(fsqrt(s31, s30), "fsqrt s31, s30");
  COMPARE(fsqrt(d10, d11), "fsqrt d10, d11");
  COMPARE(fsqrt(d31, d30), "fsqrt d31, d30");
  COMPARE(frinta(s10, s11), "frinta s10, s11");
  COMPARE(frinta(s31, s30), "frinta s31, s30");
  COMPARE(frinta(d12, d13), "frinta d12, d13");
  COMPARE(frinta(d31, d30), "frinta d31, d30");
  COMPARE(frinti(s10, s11), "frinti s10, s11");
  COMPARE(frinti(s31, s30), "frinti s31, s30");
  COMPARE(frinti(d12, d13), "frinti d12, d13");
  COMPARE(frinti(d31, d30), "frinti d31, d30");
  COMPARE(frintm(s10, s11), "frintm s10, s11");
  COMPARE(frintm(s31, s30), "frintm s31, s30");
  COMPARE(frintm(d12, d13), "frintm d12, d13");
  COMPARE(frintm(d31, d30), "frintm d31, d30");
  COMPARE(frintn(s10, s11), "frintn s10, s11");
  COMPARE(frintn(s31, s30), "frintn s31, s30");
  COMPARE(frintn(d12, d13), "frintn d12, d13");
  COMPARE(frintn(d31, d30), "frintn d31, d30");
  COMPARE(frintx(s10, s11), "frintx s10, s11");
  COMPARE(frintx(s31, s30), "frintx s31, s30");
  COMPARE(frintx(d12, d13), "frintx d12, d13");
  COMPARE(frintx(d31, d30), "frintx d31, d30");
  COMPARE(frintp(s10, s11), "frintp s10, s11");
  COMPARE(frintp(s31, s30), "frintp s31, s30");
  COMPARE(frintp(d12, d13), "frintp d12, d13");
  COMPARE(frintp(d31, d30), "frintp d31, d30");
  COMPARE(frintz(s10, s11), "frintz s10, s11");
  COMPARE(frintz(s31, s30), "frintz s31, s30");
  COMPARE(frintz(d12, d13), "frintz d12, d13");
  COMPARE(frintz(d31, d30), "frintz d31, d30");
  COMPARE(fcvt(d14, s15), "fcvt d14, s15");
  COMPARE(fcvt(d31, s31), "fcvt d31, s31");
  COMPARE(fcvt(s0, d1), "fcvt s0, d1");
  COMPARE(fcvt(s2, h3), "fcvt s2, h3");
  COMPARE(fcvt(d4, h5), "fcvt d4, h5");
  COMPARE(fcvt(h6, s7), "fcvt h6, s7");
  COMPARE(fcvt(h8, d9), "fcvt h8, d9");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_dp2) {
  SET_UP_ASM();

  COMPARE(fadd(s0, s1, s2), "fadd s0, s1, s2");
  COMPARE(fadd(d3, d4, d5), "fadd d3, d4, d5");
  COMPARE(fsub(s31, s30, s29), "fsub s31, s30, s29");
  COMPARE(fsub(d31, d30, d29), "fsub d31, d30, d29");
  COMPARE(fmul(s7, s8, s9), "fmul s7, s8, s9");
  COMPARE(fmul(d10, d11, d12), "fmul d10, d11, d12");
  COMPARE(fnmul(s7, s8, s9), "fnmul s7, s8, s9");
  COMPARE(fnmul(d10, d11, d12), "fnmul d10, d11, d12");
  COMPARE(fdiv(s13, s14, s15), "fdiv s13, s14, s15");
  COMPARE(fdiv(d16, d17, d18), "fdiv d16, d17, d18");
  COMPARE(fmax(s19, s20, s21), "fmax s19, s20, s21");
  COMPARE(fmax(d22, d23, d24), "fmax d22, d23, d24");
  COMPARE(fmin(s25, s26, s27), "fmin s25, s26, s27");
  COMPARE(fmin(d28, d29, d30), "fmin d28, d29, d30");
  COMPARE(fmaxnm(s31, s0, s1), "fmaxnm s31, s0, s1");
  COMPARE(fmaxnm(d2, d3, d4), "fmaxnm d2, d3, d4");
  COMPARE(fminnm(s5, s6, s7), "fminnm s5, s6, s7");
  COMPARE(fminnm(d8, d9, d10), "fminnm d8, d9, d10");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_dp3) {
  SET_UP_ASM();

  COMPARE(fmadd(s7, s8, s9, s10), "fmadd s7, s8, s9, s10");
  COMPARE(fmadd(d10, d11, d12, d10), "fmadd d10, d11, d12, d10");
  COMPARE(fmsub(s7, s8, s9, s10), "fmsub s7, s8, s9, s10");
  COMPARE(fmsub(d10, d11, d12, d10), "fmsub d10, d11, d12, d10");

  COMPARE(fnmadd(s7, s8, s9, s10), "fnmadd s7, s8, s9, s10");
  COMPARE(fnmadd(d10, d11, d12, d10), "fnmadd d10, d11, d12, d10");
  COMPARE(fnmsub(s7, s8, s9, s10), "fnmsub s7, s8, s9, s10");
  COMPARE(fnmsub(d10, d11, d12, d10), "fnmsub d10, d11, d12, d10");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_compare) {
  SET_UP_ASM();

  COMPARE(fcmp(s0, s1), "fcmp s0, s1");
  COMPARE(fcmp(s31, s30), "fcmp s31, s30");
  COMPARE(fcmp(d0, d1), "fcmp d0, d1");
  COMPARE(fcmp(d31, d30), "fcmp d31, d30");
  COMPARE(fcmp(s12, 0), "fcmp s12, #0.0");
  COMPARE(fcmp(d12, 0), "fcmp d12, #0.0");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_cond_compare) {
  SET_UP_ASM();

  COMPARE(fccmp(s0, s1, NoFlag, eq), "fccmp s0, s1, #nzcv, eq");
  COMPARE(fccmp(s2, s3, ZVFlag, ne), "fccmp s2, s3, #nZcV, ne");
  COMPARE(fccmp(s30, s16, NCFlag, pl), "fccmp s30, s16, #NzCv, pl");
  COMPARE(fccmp(s31, s31, NZCVFlag, le), "fccmp s31, s31, #NZCV, le");
  COMPARE(fccmp(d4, d5, VFlag, gt), "fccmp d4, d5, #nzcV, gt");
  COMPARE(fccmp(d6, d7, NFlag, vs), "fccmp d6, d7, #Nzcv, vs");
  COMPARE(fccmp(d30, d0, NZFlag, vc), "fccmp d30, d0, #NZcv, vc");
  COMPARE(fccmp(d31, d31, ZFlag, hs), "fccmp d31, d31, #nZcv, hs");
  COMPARE(fccmp(s14, s15, CVFlag, al), "fccmp s14, s15, #nzCV, al");
  COMPARE(fccmp(d16, d17, CFlag, nv), "fccmp d16, d17, #nzCv, nv");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fp_select) {
  SET_UP_ASM();

  COMPARE(fcsel(s0, s1, s2, eq), "fcsel s0, s1, s2, eq")
  COMPARE(fcsel(s31, s31, s30, ne), "fcsel s31, s31, s30, ne");
  COMPARE(fcsel(d0, d1, d2, mi), "fcsel d0, d1, d2, mi");
  COMPARE(fcsel(d31, d30, d31, pl), "fcsel d31, d30, d31, pl");
  COMPARE(fcsel(s14, s15, s16, al), "fcsel s14, s15, s16, al");
  COMPARE(fcsel(d17, d18, d19, nv), "fcsel d17, d18, d19, nv");

  CLEANUP();
}

TEST_F(DisasmArm64Test, fcvt_scvtf_ucvtf) {
  SET_UP_ASM();

  COMPARE(fcvtas(w0, s1), "fcvtas w0, s1");
  COMPARE(fcvtas(x2, s3), "fcvtas x2, s3");
  COMPARE(fcvtas(w4, d5), "fcvtas w4, d5");
  COMPARE(fcvtas(x6, d7), "fcvtas x6, d7");
  COMPARE(fcvtau(w8, s9), "fcvtau w8, s9");
  COMPARE(fcvtau(x10, s11), "fcvtau x10, s11");
  COMPARE(fcvtau(w12, d13), "fcvtau w12, d13");
  COMPARE(fcvtau(x14, d15), "fcvtau x14, d15");
  COMPARE(fcvtns(w0, s1), "fcvtns w0, s1");
  COMPARE(fcvtns(x2, s3), "fcvtns x2, s3");
  COMPARE(fcvtns(w4, d5), "fcvtns w4, d5");
  COMPARE(fcvtns(x6, d7), "fcvtns x6, d7");
  COMPARE(fcvtnu(w8, s9), "fcvtnu w8, s9");
  COMPARE(fcvtnu(x10, s11), "fcvtnu x10, s11");
  COMPARE(fcvtnu(w12, d13), "fcvtnu w12, d13");
  COMPARE(fcvtnu(x14, d15), "fcvtnu x14, d15");
  COMPARE(fcvtzu(x16, d17), "fcvtzu x16, d17");
  COMPARE(fcvtzu(w18, d19), "fcvtzu w18, d19");
  COMPARE(fcvtzs(x20, d21), "fcvtzs x20, d21");
  COMPARE(fcvtzs(w22, d23), "fcvtzs w22, d23");
  COMPARE(fcvtzu(x16, s17), "fcvtzu x16, s17");
  COMPARE(fcvtzu(w18, s19), "fcvtzu w18, s19");
  COMPARE(fcvtzs(x20, s21), "fcvtzs x20, s21");
  COMPARE(fcvtzs(w22, s23), "fcvtzs w22, s23");
  COMPARE(fcvtzs(w2, d1, 1), "fcvtzs w2, d1, #1");
  COMPARE(fcvtzs(w2, s1, 1), "fcvtzs w2, s1, #1");
  COMPARE(fcvtzs(x4, d3, 15), "fcvtzs x4, d3, #15");
  COMPARE(fcvtzs(x4, s3, 15), "fcvtzs x4, s3, #15");
  COMPARE(fcvtzs(w6, d5, 32), "fcvtzs w6, d5, #32");
  COMPARE(fcvtzs(w6, s5, 32), "fcvtzs w6, s5, #32");
  COMPARE(fjcvtzs(w0, d1), "fjcvtzs w0, d1");
  COMPARE(fcvtzu(w2, d1, 1), "fcvtzu w2, d1, #1");
  COMPARE(fcvtzu(w2, s1, 1), "fcvtzu w2, s1, #1");
  COMPARE(fcvtzu(x4, d3, 15), "fcvtzu x4, d3, #15");
  COMPARE(fcvtzu(x4, s3, 15), "fcvtzu x4, s3, #15");
  COMPARE(fcvtzu(w6, d5, 32), "fcvtzu w6, d5, #32");
  COMPARE(fcvtzu(w6, s5, 32), "fcvtzu w6, s5, #32");
  COMPARE(fcvtpu(x24, d25), "fcvtpu x24, d25");
  COMPARE(fcvtpu(w26, d27), "fcvtpu w26, d27");
  COMPARE(fcvtps(x28, d29), "fcvtps x28, d29");
  COMPARE(fcvtps(w30, d31), "fcvtps w30, d31");
  COMPARE(fcvtpu(x0, s1), "fcvtpu x0, s1");
  COMPARE(fcvtpu(w2, s3), "fcvtpu w2, s3");
  COMPARE(fcvtps(x4, s5), "fcvtps x4, s5");
  COMPARE(fcvtps(w6, s7), "fcvtps w6, s7");
  COMPARE(scvtf(d24, w25), "scvtf d24, w25");
  COMPARE(scvtf(s24, w25), "scvtf s24, w25");
  COMPARE(scvtf(d26, x0), "scvtf d26, x0");
  COMPARE(scvtf(s26, x0), "scvtf s26, x0");
  COMPARE(ucvtf(d28, w29), "ucvtf d28, w29");
  COMPARE(ucvtf(s28, w29), "ucvtf s28, w29");
  COMPARE(ucvtf(d0, x1), "ucvtf d0, x1");
  COMPARE(ucvtf(s0, x1), "ucvtf s0, x1");
  COMPARE(ucvtf(d0, x1, 0), "ucvtf d0, x1");
  COMPARE(ucvtf(s0, x1, 0), "ucvtf s0, x1");
  COMPARE(scvtf(d1, x2, 1), "scvtf d1, x2, #1");
  COMPARE(scvtf(s1, x2, 1), "scvtf s1, x2, #1");
  COMPARE(scvtf(d3, x4, 15), "scvtf d3, x4, #15");
  COMPARE(scvtf(s3, x4, 15), "scvtf s3, x4, #15");
  COMPARE(scvtf(d5, x6, 32), "scvtf d5, x6, #32");
  COMPARE(scvtf(s5, x6, 32), "scvtf s5, x6, #32");
  COMPARE(ucvtf(d7, x8, 2), "ucvtf d7, x8, #2");
  COMPARE(ucvtf(s7, x8, 2), "ucvtf s7, x8, #2");
  COMPARE(ucvtf(d9, x10, 16), "ucvtf d9, x10, #16");
  COMPARE(ucvtf(s9, x10, 16), "ucvtf s9, x10, #16");
  COMPARE(ucvtf(d11, x12, 33), "ucvtf d11, x12, #33");
  COMPARE(ucvtf(s11, x12, 33), "ucvtf s11, x12, #33");
  COMPARE(fcvtms(w0, s1), "fcvtms w0, s1");
  COMPARE(fcvtms(x2, s3), "fcvtms x2, s3");
  COMPARE(fcvtms(w4, d5), "fcvtms w4, d5");
  COMPARE(fcvtms(x6, d7), "fcvtms x6, d7");
  COMPARE(fcvtmu(w8, s9), "fcvtmu w8, s9");
  COMPARE(fcvtmu(x10, s11), "fcvtmu x10, s11");
  COMPARE(fcvtmu(w12, d13), "fcvtmu w12, d13");
  COMPARE(fcvtmu(x14, d15), "fcvtmu x14, d15");

  CLEANUP();
}

TEST_F(DisasmArm64Test, system_mrs) {
  SET_UP_ASM();

  COMPARE(mrs(x0, NZCV), "mrs x0, nzcv");
  COMPARE(mrs(lr, NZCV), "mrs lr, nzcv");
  COMPARE(mrs(x15, FPCR), "mrs x15, fpcr");

  CLEANUP();
}

TEST_F(DisasmArm64Test, system_msr) {
  SET_UP_ASM();

  COMPARE(msr(NZCV, x0), "msr nzcv, x0");
  COMPARE(msr(NZCV, x30), "msr nzcv, lr");
  COMPARE(msr(FPCR, x15), "msr fpcr, x15");

  CLEANUP();
}

TEST_F(DisasmArm64Test, system_nop) {
  {
    SET_UP_ASM();
    COMPARE(nop(), "nop");
    CLEANUP();
  }
  {
    SET_UP_MASM();
    COMPARE(Nop(), "nop");
    CLEANUP();
  }
}

TEST_F(DisasmArm64Test, bti) {
  {
    SET_UP_ASM();

    COMPARE(bti(BranchTargetIdentifier::kBti), "bti");
    COMPARE(bti(BranchTargetIdentifier::kBtiCall), "bti c");
    COMPARE(bti(BranchTargetIdentifier::kBtiJump), "bti j");
    COMPARE(bti(BranchTargetIdentifier::kBtiJumpCall), "bti jc");
    COMPARE(hint(BTI), "bti");
    COMPARE(hint(BTI_c), "bti c");
    COMPARE(hint(BTI_j), "bti j");
    COMPARE(hint(BTI_jc), "bti jc");

    CLEANUP();
  }

  {
    SET_UP_MASM();

    Label dummy1, dummy2, dummy3, dummy4;
    COMPARE(Bind(&dummy1, BranchTargetIdentifier::kBti), "bti");
    COMPARE(Bind(&dummy2, BranchTargetIdentifier::kBtiCall), "bti c");
    COMPARE(Bind(&dummy3, BranchTargetIdentifier::kBtiJump), "bti j");
    COMPARE(Bind(&dummy4, BranchTargetIdentifier::kBtiJumpCall), "bti jc");

    CLEANUP();
  }
}

TEST_F(DisasmArm64Test, system_pauth) {
  SET_UP_ASM();

  COMPARE(pacib1716(), "pacib1716");
  COMPARE(pacibsp(), "pacibsp");
  COMPARE(autib1716(), "autib1716");
  COMPARE(autibsp(), "autibsp");

  CLEANUP();
}

TEST_F(DisasmArm64Test, debug) {
  for (int i = 0; i < 2; i++) {
    // Loop runs with and without the simulator code enabled.
    HandleScope scope(isolate());
    uint8_t* buf = static_cast<uint8_t*>(malloc(INSTR_SIZE));
    uint32_t encoding = 0;
    AssemblerOptions options{};
#ifdef USE_SIMULATOR
    options.enable_simulator_code = (i == 1);
#else
    CHECK(!options.enable_simulator_code);
#endif
    Assembler* assm = new Assembler(i_isolate()->allocator(), options,
                                    ExternalAssemblerBuffer(buf, INSTR_SIZE));
    Decoder<DispatchingDecoderVisitor>* decoder =
        new Decoder<DispatchingDecoderVisitor>();
    DisassemblingDecoder* disasm = new DisassemblingDecoder();
    decoder->AppendVisitor(disasm);

    CHECK_EQ(kImmExceptionIsDebug, 0xdeb0);

    // All debug codes should produce the same instruction, and the debug code
    // can be any uint32_t.
    const char* expected_instruction =
        options.enable_simulator_code ? "hlt #0xdeb0" : "brk #0x0";

    COMPARE(debug("message", 0, BREAK), expected_instruction);
    COMPARE(debug("message", 1, BREAK), expected_instruction);
    COMPARE(debug("message", 0xffff, BREAK), expected_instruction);
    COMPARE(debug("message", 0x10000, BREAK), expected_instruction);
    COMPARE(debug("message", 0x7fffffff, BREAK), expected_instruction);
    COMPARE(debug("message", 0x80000000u, BREAK), expected_instruction);
    COMPARE(debug("message", 0xffffffffu, BREAK), expected_instruction);

    CLEANUP();
  }
}

TEST_F(DisasmArm64Test, hlt) {
  SET_UP_ASM();

  COMPARE(hlt(0), "hlt #0x0");
  COMPARE(hlt(1), "hlt #0x1");
  COMPARE(hlt(65535), "hlt #0xffff");

  CLEANUP();
}

TEST_F(DisasmArm64Test, brk) {
  SET_UP_ASM();

  COMPARE(brk(0), "brk #0x0");
  COMPARE(brk(1), "brk #0x1");
  COMPARE(brk(65535), "brk #0xffff");

  CLEANUP();
}

TEST_F(DisasmArm64Test, add_sub_negative) {
  SET_UP_MASM();

  COMPARE(Add(x10, x0, -42), "sub x10, x0, #0x2a (42)");
  COMPARE(Add(x11, x1, -687), "sub x11, x1, #0x2af (687)");
  COMPARE(Add(x12, x2, -0x88), "sub x12, x2, #0x88 (136)");

  COMPARE(Sub(x13, x0, -600), "add x13, x0, #0x258 (600)");
  COMPARE(Sub(x14, x1, -313), "add x14, x1, #0x139 (313)");
  COMPARE(Sub(x15, x2, -0x555), "add x15, x2, #0x555 (1365)");

  COMPARE(Add(w19, w3, -0x344), "sub w19, w3, #0x344 (836)");
  COMPARE(Add(w20, w4, -2000), "sub w20, w4, #0x7d0 (2000)");

  COMPARE(Sub(w21, w3, -0xbc), "add w21, w3, #0xbc (188)");
  COMPARE(Sub(w22, w4, -2000), "add w22, w4, #0x7d0 (2000)");

  COMPARE(Cmp(w0, -1), "cmn w0, #0x1 (1)");
  COMPARE(Cmp(x1, -1), "cmn x1, #0x1 (1)");
  COMPARE(Cmp(w2, -4095), "cmn w2, #0xfff (4095)");
  COMPARE(Cmp(x3, -4095), "cmn x3, #0xfff (4095)");

  COMPARE(Cmn(w0, -1), "cmp w0, #0x1 (1)");
  COMPARE(Cmn(x1, -1), "cmp x1, #0x1 (1)");
  COMPARE(Cmn(w2, -4095), "cmp w2, #0xfff (4095)");
  COMPARE(Cmn(x3, -4095), "cmp x3, #0xfff (4095)");

  CLEANUP();
}

TEST_F(DisasmArm64Test, logical_immediate_move) {
  SET_UP_MASM();

  COMPARE(And(w0, w1, 0), "movz w0, #0x0");
  COMPARE(And(x0, x1, 0), "movz x0, #0x0");
  COMPARE(Orr(w2, w3, 0), "mov w2, w3");
  COMPARE(Orr(x2, x3, 0), "mov x2, x3");
  COMPARE(Eor(w4, w5, 0), "mov w4, w5");
  COMPARE(Eor(x4, x5, 0), "mov x4, x5");
  COMPARE(Bic(w6, w7, 0), "mov w6, w7");
  COMPARE(Bic(x6, x7, 0), "mov x6, x7");
  COMPARE(Orn(w8, w9, 0), "movn w8, #0x0");
  COMPARE(Orn(x8, x9, 0), "movn x8, #0x0");
  COMPARE(Eon(w10, w11, 0), "mvn w10, w11");
  COMPARE(Eon(x10, x11, 0), "mvn x10, x11");

  COMPARE(And(w12, w13, 0xffffffff), "mov w12, w13");
  COMPARE(And(x12, x13, 0xffffffff), "and x12, x13, #0xffffffff");
  COMPARE(And(x12, x13, 0xffffffffffffffff), "mov x12, x13");
  COMPARE(Orr(w14, w15, 0xffffffff), "movn w14, #0x0");
  COMPARE(Orr(x14, x15, 0xffffffff), "orr x14, x15, #0xffffffff");
  COMPARE(Orr(x14, x15, 0xffffffffffffffff), "movn x14, #0x0");
  COMPARE(Eor(w16, w17, 0xffffffff), "mvn w16, w17");
  COMPARE(Eor(x16, x17, 0xffffffff), "eor x16, x17, #0xffffffff");
  COMPARE(Eor(x16, x17, 0xffffffffffffffff), "mvn x16, x17");
  COMPARE(Bic(w18, w19, 0xffffffff), "movz w18, #0x0");
  COMPARE(Bic(x18, x19, 0xffffffff), "and x18, x19, #0xffffffff00000000");
  COMPARE(Bic(x18, x19, 0xffffffffffffffff), "movz x18, #0x0");
  COMPARE(Orn(w20, w21, 0xffffffff), "mov w20, w21");
  COMPARE(Orn(x20, x21, 0xffffffff), "orr x20, x21, #0xffffffff00000000");
  COMPARE(Orn(x20, x21, 0xffffffffffffffff), "mov x20, x21");
  COMPARE(Eon(w22, w23, 0xffffffff), "mov w22, w23");
  COMPARE(Eon(x22, x23, 0xffffffff), "eor x22, x23, #0xffffffff00000000");
  COMPARE(Eon(x22, x23, 0xffffffffffffffff), "mov x22, x23");

  CLEANUP();
}

TEST_F(DisasmArm64Test, barriers) {
  SET_UP_MASM();

  // DMB
  COMPARE(Dmb(FullSystem, BarrierAll), "dmb sy");
  COMPARE(Dmb(FullSystem, BarrierReads), "dmb ld");
  COMPARE(Dmb(FullSystem, BarrierWrites), "dmb st");

  COMPARE(Dmb(InnerShareable, BarrierAll), "dmb ish");
  COMPARE(Dmb(InnerShareable, BarrierReads), "dmb ishld");
  COMPARE(Dmb(InnerShareable, BarrierWrites), "dmb ishst");

  COMPARE(Dmb(NonShareable, BarrierAll), "dmb nsh");
  COMPARE(Dmb(NonShareable, BarrierReads), "dmb nshld");
  COMPARE(Dmb(NonShareable, BarrierWrites), "dmb nshst");

  COMPARE(Dmb(OuterShareable, BarrierAll), "dmb osh");
  COMPARE(Dmb(OuterShareable, BarrierReads), "dmb oshld");
  COMPARE(Dmb(OuterShareable, BarrierWrites), "dmb oshst");

  COMPARE(Dmb(FullSystem, BarrierOther), "dmb sy (0b1100)");
  COMPARE(Dmb(InnerShareable, BarrierOther), "dmb sy (0b1000)");
  COMPARE(Dmb(NonShareable, BarrierOther), "dmb sy (0b0100)");
  COMPARE(Dmb(OuterShareable, BarrierOther), "dmb sy (0b0000)");

  // DSB
  COMPARE(Dsb(FullSystem, BarrierAll), "dsb sy");
  COMPARE(Dsb(FullSystem, BarrierReads), "dsb ld");
  COMPARE(Dsb(FullSystem, BarrierWrites), "dsb st");

  COMPARE(Dsb(InnerShareable, BarrierAll), "dsb ish");
  COMPARE(Dsb(InnerShareable, BarrierReads), "dsb ishld");
  COMPARE(Dsb(InnerShareable, BarrierWrites), "dsb ishst");

  COMPARE(Dsb(NonShareable, BarrierAll), "dsb nsh");
  COMPARE(Dsb(NonShareable, BarrierReads), "dsb nshld");
  COMPARE(Dsb(NonShareable, BarrierWrites), "dsb nshst");

  COMPARE(Dsb(OuterShareable, BarrierAll), "dsb osh");
  COMPARE(Dsb(OuterShareable, BarrierReads), "dsb oshld");
  COMPARE(Dsb(OuterShareable, BarrierWrites), "dsb oshst");

  COMPARE(Dsb(FullSystem, BarrierOther), "dsb sy (0b1100)");
  COMPARE(Dsb(InnerShareable, BarrierOther), "dsb sy (0b1000)");
  COMPARE(Dsb(NonShareable, BarrierOther), "dsb sy (0b0100)");
  COMPARE(Dsb(OuterShareable, BarrierOther), "dsb sy (0b0000)");

  // ISB
  COMPARE(Isb(), "isb");

  // CSDB
  COMPARE(Csdb(), "csdb");

  CLEANUP();
}

#define VLIST2(v) \
  v, VRegister::Create((v.code() + 1) % 32, v.SizeInBits(), v.LaneCount())
#define VLIST3(v) \
  VLIST2(v)       \
  , VRegister::Create((v.code() + 2) % 32, v.SizeInBits(), v.LaneCount())
#define VLIST4(v) \
  VLIST3(v)       \
  , VRegister::Create((v.code() + 3) % 32, v.SizeInBits(), v.LaneCount())

#define NEON_FORMAT_LIST(V) \
  V(V8B(), "8b")            \
  V(V16B(), "16b")          \
  V(V4H(), "4h")            \
  V(V8H(), "8h")            \
  V(V2S(), "2s")            \
  V(V4S(), "4s")            \
  V(V2D(), "2d")

#define NEON_FORMAT_LIST_LP(V)  \
  V(V4H(), "4h", V8B(), "8b")   \
  V(V2S(), "2s", V4H(), "4h")   \
  V(V1D(), "1d", V2S(), "2s")   \
  V(V8H(), "8h", V16B(), "16b") \
  V(V4S(), "4s", V8H(), "8h")   \
  V(V2D(), "2d", V4S(), "4s")

#define NEON_FORMAT_LIST_LW(V) \
  V(V8H(), "8h", V8B(), "8b")  \
  V(V4S(), "4s", V4H(), "4h")  \
  V(V2D(), "2d", V2S(), "2s")

#define NEON_FORMAT_LIST_LW2(V) \
  V(V8H(), "8h", V16B(), "16b") \
  V(V4S(), "4s", V8H(), "8h")   \
  V(V2D(), "2d", V4S(), "4s")

#define NEON_FORMAT_LIST_BHS(V) \
  V(V8B(), "8b")                \
  V(V16B(), "16b")              \
  V(V4H(), "4h")                \
  V(V8H(), "8h")                \
  V(V2S(), "2s")                \
  V(V4S(), "4s")

#define NEON_FORMAT_LIST_HS(V) \
  V(V4H(), "4h")               \
  V(V8H(), "8h")               \
  V(V2S(), "2s")               \
  V(V4S(), "4s")

#define NEON_FORMAT_LIST_FP(V) \
  V(V4H(), "4h")               \
  V(V8H(), "8h")               \
  V(V2S(), "2s")               \
  V(V4S(), "4s")               \
  V(V2D(), "2d")

TEST_F(DisasmArm64Test, neon_load_store_vector) {
  SET_UP_MASM();

#define DISASM_INST(M, S)                                         \
  COMPARE(Ld1(v0.M, MemOperand(x15)), "ld1 {v0." S "}, [x15]");   \
  COMPARE(Ld1(v1.M, v2.M, MemOperand(x16)),                       \
          "ld1 {v1." S ", v2." S "}, [x16]");                     \
  COMPARE(Ld1(v3.M, v4.M, v5.M, MemOperand(x17)),                 \
          "ld1 {v3." S ", v4." S ", v5." S "}, [x17]");           \
  COMPARE(Ld1(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18)),          \
          "ld1 {v6." S ", v7." S ", v8_." S ", v9." S "}, [x18]") \
  COMPARE(Ld1(v30.M, v31.M, v0.M, v1.M, MemOperand(sp)),          \
          "ld1 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp]") \
  COMPARE(Ld2(v1.M, v2.M, MemOperand(x16)),                       \
          "ld2 {v1." S ", v2." S "}, [x16]");                     \
  COMPARE(Ld3(v3.M, v4.M, v5.M, MemOperand(x17)),                 \
          "ld3 {v3." S ", v4." S ", v5." S "}, [x17]");           \
  COMPARE(Ld4(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18)),          \
          "ld4 {v6." S ", v7." S ", v8." S ", v9." S "}, [x18]")  \
  COMPARE(Ld4(v30.M, v31.M, v0.M, v1.M, MemOperand(sp)),          \
          "ld4 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp]") \
  NEON_FORMAT_LIST(DISASM_INST);
#undef DISASM_INST

#define DISASM_INST(M, S)                                                \
  COMPARE(Ld1(v0.M, MemOperand(x15, x20, PostIndex)),                    \
          "ld1 {v0." S "}, [x15], x20");                                 \
  COMPARE(Ld1(v1.M, v2.M, MemOperand(x16, x21, PostIndex)),              \
          "ld1 {v1." S ", v2." S "}, [x16], x21");                       \
  COMPARE(Ld1(v3.M, v4.M, v5.M, MemOperand(x17, x22, PostIndex)),        \
          "ld1 {v3." S ", v4." S ", v5." S "}, [x17], x22");             \
  COMPARE(Ld1(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18, x23, PostIndex)), \
          "ld1 {v6." S ", v7." S ", v8." S ", v9." S "}, [x18], x23")    \
  COMPARE(Ld1(v30.M, v31.M, v0.M, v1.M, MemOperand(sp, x24, PostIndex)), \
          "ld1 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp], x24")   \
  COMPARE(Ld2(v1.M, v2.M, MemOperand(x16, x21, PostIndex)),              \
          "ld2 {v1." S ", v2." S "}, [x16], x21");                       \
  COMPARE(Ld3(v3.M, v4.M, v5.M, MemOperand(x17, x22, PostIndex)),        \
          "ld3 {v3." S ", v4." S ", v5." S "}, [x17], x22");             \
  COMPARE(Ld4(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18, x23, PostIndex)), \
          "ld4 {v6." S ", v7." S ", v8_." S ", v9." S "}, [x18], x23")   \
  COMPARE(Ld4(v30.M, v31.M, v0.M, v1.M, MemOperand(sp, x24, PostIndex)), \
          "ld4 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp], x24")   \
  NEON_FORMAT_LIST(DISASM_INST);
#undef DISASM_INST

  COMPARE(Ld1(v0.V8B(), MemOperand(x15, 8, PostIndex)),
          "ld1 {v0.8b}, [x15], #8");
  COMPARE(Ld1(v1.V16B(), MemOperand(x16, 16, PostIndex)),
          "ld1 {v1.16b}, [x16], #16");
  COMPARE(Ld1(v2.V4H(), v3.V4H(), MemOperand(x17, 16, PostIndex)),
          "ld1 {v2.4h, v3.4h}, [x17], #16");
  COMPARE(Ld1(v4.V8H(), v5.V8H(), MemOperand(x18, 32, PostIndex)),
          "ld1 {v4.8h, v5.8h}, [x18], #32");
  COMPARE(Ld1(v16.V2S(), v17.V2S(), v18.V2S(), MemOperand(x19, 24, PostIndex)),
          "ld1 {v16.2s, v17.2s, v18.2s}, [x19], #24");
  COMPARE(Ld1(v16.V4S(), v17.V4S(), v18.V4S(), MemOperand(x19, 48, PostIndex)),
          "ld1 {v16.4s, v17.4s, v18.4s}, [x19], #48");
  COMPARE(Ld1(v19.V2S(), v20.V2S(), v21.V2S(), v22.V2S(),
              MemOperand(x20, 32, PostIndex)),
          "ld1 {v19.2s, v20.2s, v21.2s, v22.2s}, [x20], #32");
  COMPARE(Ld1(v23.V2D(), v24.V2D(), v25.V2D(), v26.V2D(),
              MemOperand(x21, 64, PostIndex)),
          "ld1 {v23.2d, v24.2d, v25.2d, v26.2d}, [x21], #64");

  COMPARE(Ld2(v2.V4H(), v3.V4H(), MemOperand(x17, 16, PostIndex)),
          "ld2 {v2.4h, v3.4h}, [x17], #16");
  COMPARE(Ld2(v4.V8H(), v5.V8H(), MemOperand(x18, 32, PostIndex)),
          "ld2 {v4.8h, v5.8h}, [x18], #32");
  COMPARE(Ld3(v16.V2S(), v17.V2S(), v18.V2S(), MemOperand(x19, 24, PostIndex)),
          "ld3 {v16.2s, v17.2s, v18.2s}, [x19], #24");
  COMPARE(Ld3(v16.V4S(), v17.V4S(), v18.V4S(), MemOperand(x19, 48, PostIndex)),
          "ld3 {v16.4s, v17.4s, v18.4s}, [x19], #48");
  COMPARE(Ld4(v19.V2S(), v20.V2S(), v21.V2S(), v22.V2S(),
              MemOperand(x20, 32, PostIndex)),
          "ld4 {v19.2s, v20.2s, v21.2s, v22.2s}, [x20], #32");
  COMPARE(Ld4(v23.V2D(), v24.V2D(), v25.V2D(), v26.V2D(),
              MemOperand(x21, 64, PostIndex)),
          "ld4 {v23.2d, v24.2d, v25.2d, v26.2d}, [x21], #64");

  COMPARE(Ld1(v0.V1D(), MemOperand(x16)), "ld1 {v0.1d}, [x16]");
  COMPARE(Ld1(v1.V1D(), v2.V1D(), MemOperand(x17, 16, PostIndex)),
          "ld1 {v1.1d, v2.1d}, [x17], #16");
  COMPARE(Ld1(v3.V1D(), v4.V1D(), v5.V1D(), MemOperand(x18, x19, PostIndex)),
          "ld1 {v3.1d, v4.1d, v5.1d}, [x18], x19");
  COMPARE(Ld1(v30.V1D(), v31.V1D(), v0.V1D(), v1.V1D(),
              MemOperand(x20, 32, PostIndex)),
          "ld1 {v30.1d, v31.1d, v0.1d, v1.1d}, [x20], #32");
  COMPARE(Ld1(d30, d31, d0, d1, MemOperand(x21, x22, PostIndex)),
          "ld1 {v30.1d, v31.1d, v0.1d, v1.1d}, [x21], x22");

#define DISASM_INST(M, S)                                            \
  COMPARE(St1(v20.M, MemOperand(x15)), "st1 {v20." S "}, [x15]");    \
  COMPARE(St1(v21.M, v22.M, MemOperand(x16)),                        \
          "st1 {v21." S ", v22." S "}, [x16]");                      \
  COMPARE(St1(v23.M, v24.M, v25.M, MemOperand(x17)),                 \
          "st1 {v23." S ", v24." S ", v25." S "}, [x17]");           \
  COMPARE(St1(v26.M, v27.M, v28.M, v29.M, MemOperand(x18)),          \
          "st1 {v26." S ", v27." S ", v28." S ", v29." S "}, [x18]") \
  COMPARE(St1(v30.M, v31.M, v0.M, v1.M, MemOperand(sp)),             \
          "st1 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp]")    \
  COMPARE(St2(VLIST2(v21.M), MemOperand(x16)),                       \
          "st2 {v21." S ", v22." S "}, [x16]");                      \
  COMPARE(St3(v23.M, v24.M, v25.M, MemOperand(x17)),                 \
          "st3 {v23." S ", v24." S ", v25." S "}, [x17]");           \
  COMPARE(St4(v30.M, v31.M, v0.M, v1.M, MemOperand(sp)),             \
          "st4 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp]")
  NEON_FORMAT_LIST(DISASM_INST);
#undef DISASM_INST

#define DISASM_INST(M, S)                                                \
  COMPARE(St1(v0.M, MemOperand(x15, x20, PostIndex)),                    \
          "st1 {v0." S "}, [x15], x20");                                 \
  COMPARE(St1(v1.M, v2.M, MemOperand(x16, x21, PostIndex)),              \
          "st1 {v1." S ", v2." S "}, [x16], x21");                       \
  COMPARE(St1(v3.M, v4.M, v5.M, MemOperand(x17, x22, PostIndex)),        \
          "st1 {v3." S ", v4." S ", v5." S "}, [x17], x22");             \
  COMPARE(St1(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18, x23, PostIndex)), \
          "st1 {v6." S ", v7." S ", v8." S ", v9." S "}, [x18], x23")    \
  COMPARE(St1(v30.M, v31.M, v0.M, v1.M, MemOperand(sp, x24, PostIndex)), \
          "st1 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp], x24")   \
  COMPARE(St2(v1.M, v2.M, MemOperand(x16, x21, PostIndex)),              \
          "st2 {v1." S ", v2." S "}, [x16], x21");                       \
  COMPARE(St3(v3.M, v4.M, v5.M, MemOperand(x17, x22, PostIndex)),        \
          "st3 {v3." S ", v4." S ", v5." S "}, [x17], x22");             \
  COMPARE(St4(v6.M, v7.M, v8_.M, v9.M, MemOperand(x18, x23, PostIndex)), \
          "st4 {v6." S ", v7." S ", v8." S ", v9." S "}, [x18], x23")    \
  COMPARE(St4(v30.M, v31.M, v0.M, v1.M, MemOperand(sp, x24, PostIndex)), \
          "st4 {v30." S ", v31." S ", v0." S ", v1." S "}, [sp], x24")
  NEON_FORMAT_LIST(DISASM_INST);
#undef DISASM_INST

  COMPARE(St1(v0.V8B(), MemOperand(x15, 8, PostIndex)),
          "st1 {v0.8b}, [x15], #8");
  COMPARE(St1(v1.V16B(), MemOperand(x16, 16, PostIndex)),
          "st1 {v1.16b}, [x16], #16");
  COMPARE(St1(v2.V4H(), v3.V4H(), MemOperand(x17, 16, PostIndex)),
          "st1 {v2.4h, v3.4h}, [x17], #16");
  COMPARE(St1(v4.V8H(), v5.V8H(), MemOperand(x18, 32, PostIndex)),
          "st1 {v4.8h, v5.8h}, [x18], #32");
  COMPARE(St1(v16.V2S(), v17.V2S(), v18.V2S(), MemOperand(x19, 24, PostIndex)),
          "st1 {v16.2s, v17.2s, v18.2s}, [x19], #24");
  COMPARE(St1(v16.V4S(), v17.V4S(), v18.V4S(), MemOperand(x19, 48, PostIndex)),
          "st1 {v16.4s, v17.4s, v18.4s}, [x19], #48");
  COMPARE(St1(v19.V2S(), v20.V2S(), v21.V2S(), v22.V2S(),
              MemOperand(x20, 32, PostIndex)),
          "st1 {v19.2s, v20.2s, v21.2s, v22.2s}, [x20], #32");
  COMPARE(St1(v23.V2D(), v24.V2D(), v25.V2D(), v26.V2D(),
              MemOperand(x21, 64, PostIndex)),
          "st1 {v23.2d, v24.2d, v25.2d, v26.2d}, [x21], #64");
  COMPARE(St2(v1.V16B(), v2.V16B(), MemOperand(x16, 32, PostIndex)),
          "st2 {v1.16b, v2.16b}, [x16], #32");
  COMPARE(St2(v2.V4H(), v3.V4H(), MemOperand(x17, 16, PostIndex)),
          "st2 {v2.4h, v3.4h}, [x17], #16");
  COMPARE(St2(v4.V8H(), v5.V8H(), MemOperand(x18, 32, PostIndex)),
          "st2 {v4.8h, v5.8h}, [x18], #32");
  COMPARE(St3(v16.V2S(), v17.V2S(), v18.V2S(), MemOperand(x19, 24, PostIndex)),
          "st3 {v16.2s, v17.2s, v18.2s}, [x19], #24");
  COMPARE(St3(v16.V4S(), v17.V4S(), v18.V4S(), MemOperand(x19, 48, PostIndex)),
          "st3 {v16.4s, v17.4s, v18.4s}, [x19], #48");
  COMPARE(St4(v19.V2S(), v20.V2S(), v21.V2S(), v22.V2S(),
              MemOperand(x20, 32, PostIndex)),
          "st4 {v19.2s, v20.2s, v21.2s, v22.2s}, [x20], #32");
  COMPARE(St4(v23.V2D(), v24.V2D(), v25.V2D(), v26.V2D(),
              MemOperand(x21, 64, PostIndex)),
          "st4 {v23.2d, v24.2d, v25.2d, v26.2d}, [x21], #64");

  COMPARE(St1(v0.V1D(), MemOperand(x16)), "st1 {v0.1d}, [x16]");
  COMPARE(St1(v1.V1D(), v2.V1D(), MemOperand(x17, 16, PostIndex)),
          "st1 {v1.1d, v2.1d}, [x17], #16");
  COMPARE(St1(v3.V1D(), v4.V1D(), v5.V1D(), MemOperand(x18, x19, PostIndex)),
          "st1 {v3.1d, v4.1d, v5.1d}, [x18], x19");
  COMPARE(St1(v30.V1D(), v31.V1D(), v0.V1D(), v1.V1D(),
              MemOperand(x20, 32, PostIndex)),
          "st1 {v30.1d, v31.1d, v0.1d, v1.1d}, [x20], #32");
  COMPARE(St1(d30, d31, d0, d1, MemOperand(x21, x22, PostIndex)),
          "st1 {v30.1d, v31.1d, v0.1d, v1.1d}, [x21], x22");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_load_store_vector_unallocated) {
  SET_UP_MASM();

  const char* expected = "unallocated (NEONLoadStoreMultiStruct)";
  // LD[1-4] (multiple structures) (no offset)
  COMPARE(dci(0x0c401000), expected);  // opcode = 0b0001
  COMPARE(dci(0x0c403000), expected);  // opcode = 0b0011
  COMPARE(dci(0x0c405000), expected);  // opcode = 0b0101
  COMPARE(dci(0x0c409000), expected);  // opcode = 0b1001
  COMPARE(dci(0x0c40b000), expected);  // opcode = 0b1011
  COMPARE(dci(0x0c40c000), expected);  // opcode = 0b1100
  COMPARE(dci(0x0c40d000), expected);  // opcode = 0b1101
  COMPARE(dci(0x0c40e000), expected);  // opcode = 0b1110
  COMPARE(dci(0x0c40f000), expected);  // opcode = 0b1111
  COMPARE(dci(0x0c400c00), expected);  // opcode = 0b0000, size:Q = 0b110
  COMPARE(dci(0x0c404c00), expected);  // opcode = 0b0100, size:Q = 0b110
  COMPARE(dci(0x0c408c00), expected);  // opcode = 0b1000, size:Q = 0b110

  // ST[1-4] (multiple structures) (no offset)
  COMPARE(dci(0x0c001000), expected);  // opcode = 0b0001
  COMPARE(dci(0x0c003000), expected);  // opcode = 0b0011
  COMPARE(dci(0x0c005000), expected);  // opcode = 0b0101
  COMPARE(dci(0x0c009000), expected);  // opcode = 0b1001
  COMPARE(dci(0x0c00b000), expected);  // opcode = 0b1011
  COMPARE(dci(0x0c00c000), expected);  // opcode = 0b1100
  COMPARE(dci(0x0c00d000), expected);  // opcode = 0b1101
  COMPARE(dci(0x0c00e000), expected);  // opcode = 0b1110
  COMPARE(dci(0x0c00f000), expected);  // opcode = 0b1111
  COMPARE(dci(0x0c000c00), expected);  // opcode = 0b0000, size:Q = 0b110
  COMPARE(dci(0x0c004c00), expected);  // opcode = 0b0100, size:Q = 0b110
  COMPARE(dci(0x0c008c00), expected);  // opcode = 0b1000, size:Q = 0b110

  expected = "unallocated (NEONLoadStoreMultiStructPostIndex)";
  // LD[1-4] (multiple structures) (post index)
  COMPARE(dci(0x0cc01000), expected);  // opcode = 0b0001
  COMPARE(dci(0x0cc03000), expected);  // opcode = 0b0011
  COMPARE(dci(0x0cc05000), expected);  // opcode = 0b0101
  COMPARE(dci(0x0cc09000), expected);  // opcode = 0b1001
  COMPARE(dci(0x0cc0b000), expected);  // opcode = 0b1011
  COMPARE(dci(0x0cc0c000), expected);  // opcode = 0b1100
  COMPARE(dci(0x0cc0d000), expected);  // opcode = 0b1101
  COMPARE(dci(0x0cc0e000), expected);  // opcode = 0b1110
  COMPARE(dci(0x0cc0f000), expected);  // opcode = 0b1111
  COMPARE(dci(0x0cc00c00), expected);  // opcode = 0b0000, size:Q = 0b110
  COMPARE(dci(0x0cc04c00), expected);  // opcode = 0b0100, size:Q = 0b110
  COMPARE(dci(0x0cc08c00), expected);  // opcode = 0b1000, size:Q = 0b110

  // ST[1-4] (multiple structures) (post index)
  COMPARE(dci(0x0c801000), expected);  // opcode = 0b0001
  COMPARE(dci(0x0c803000), expected);  // opcode = 0b0011
  COMPARE(dci(0x0c805000), expected);  // opcode = 0b0101
  COMPARE(dci(0x0c809000), expected);  // opcode = 0b1001
  COMPARE(dci(0x0c80b000), expected);  // opcode = 0b1011
  COMPARE(dci(0x0c80c000), expected);  // opcode = 0b1100
  COMPARE(dci(0x0c80d000), expected);  // opcode = 0b1101
  COMPARE(dci(0x0c80e000), expected);  // opcode = 0b1110
  COMPARE(dci(0x0c80f000), expected);  // opcode = 0b1111
  COMPARE(dci(0x0c800c00), expected);  // opcode = 0b0000, size:Q = 0b110
  COMPARE(dci(0x0c804c00), expected);  // opcode = 0b0100, size:Q = 0b110
  COMPARE(dci(0x0c808c00), expected);  // opcode = 0b1000, size:Q = 0b110

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_load_store_lane) {
  SET_UP_MASM();

  COMPARE(Ld1(v0.V8B(), 0, MemOperand(x15)), "ld1 {v0.b}[0], [x15]");
  COMPARE(Ld1(v1.V16B(), 1, MemOperand(x16)), "ld1 {v1.b}[1], [x16]");
  COMPARE(Ld1(v2.V4H(), 2, MemOperand(x17)), "ld1 {v2.h}[2], [x17]");
  COMPARE(Ld1(v3.V8H(), 3, MemOperand(x18)), "ld1 {v3.h}[3], [x18]");
  COMPARE(Ld1(v4.V2S(), 0, MemOperand(x19)), "ld1 {v4.s}[0], [x19]");
  COMPARE(Ld1(v5.V4S(), 1, MemOperand(x20)), "ld1 {v5.s}[1], [x20]");
  COMPARE(Ld1(v6.V2D(), 0, MemOperand(x21)), "ld1 {v6.d}[0], [x21]");
  COMPARE(Ld1(v7.B(), 7, MemOperand(x22)), "ld1 {v7.b}[7], [x22]");
  COMPARE(Ld1(v8_.B(), 15, MemOperand(x23)), "ld1 {v8.b}[15], [x23]");
  COMPARE(Ld1(v9.H(), 3, MemOperand(x24)), "ld1 {v9.h}[3], [x24]");
  COMPARE(Ld1(v10.H(), 7, MemOperand(x25)), "ld1 {v10.h}[7], [x25]");
  COMPARE(Ld1(v11.S(), 1, MemOperand(x26)), "ld1 {v11.s}[1], [x26]");
  COMPARE(Ld1(v12.S(), 3, MemOperand(x27)), "ld1 {v12.s}[3], [cp]");
  COMPARE(Ld1(v13.D(), 1, MemOperand(sp)), "ld1 {v13.d}[1], [sp]");

  COMPARE(Ld1(v0.V8B(), 0, MemOperand(x15, x0, PostIndex)),
          "ld1 {v0.b}[0], [x15], x0");
  COMPARE(Ld1(v1.V16B(), 1, MemOperand(x16, 1, PostIndex)),
          "ld1 {v1.b}[1], [x16], #1");
  COMPARE(Ld1(v2.V4H(), 2, MemOperand(x17, 2, PostIndex)),
          "ld1 {v2.h}[2], [x17], #2");
  COMPARE(Ld1(v3.V8H(), 3, MemOperand(x18, x1, PostIndex)),
          "ld1 {v3.h}[3], [x18], x1");
  COMPARE(Ld1(v4.V2S(), 0, MemOperand(x19, x2, PostIndex)),
          "ld1 {v4.s}[0], [x19], x2");
  COMPARE(Ld1(v5.V4S(), 1, MemOperand(x20, 4, PostIndex)),
          "ld1 {v5.s}[1], [x20], #4");
  COMPARE(Ld1(v6.V2D(), 0, MemOperand(x21, 8, PostIndex)),
          "ld1 {v6.d}[0], [x21], #8");
  COMPARE(Ld1(v7.B(), 7, MemOperand(x22, 1, PostIndex)),
          "ld1 {v7.b}[7], [x22], #1");
  COMPARE(Ld1(v8_.B(), 15, MemOperand(x23, x3, PostIndex)),
          "ld1 {v8.b}[15], [x23], x3");
  COMPARE(Ld1(v9.H(), 3, MemOperand(x24, x4, PostIndex)),
          "ld1 {v9.h}[3], [x24], x4");
  COMPARE(Ld1(v10.H(), 7, MemOperand(x25, 2, PostIndex)),
          "ld1 {v10.h}[7], [x25], #2");
  COMPARE(Ld1(v11.S(), 1, MemOperand(x26, 4, PostIndex)),
          "ld1 {v11.s}[1], [x26], #4");
  COMPARE(Ld1(v12.S(), 3, MemOperand(x27, x5, PostIndex)),
          "ld1 {v12.s}[3], [cp], x5");
  COMPARE(Ld1(v12.S(), 3, MemOperand(x27, 4, PostIndex)),
          "ld1 {v12.s}[3], [cp], #4");
  COMPARE(Ld1(v13.D(), 1, MemOperand(sp, x6, PostIndex)),
          "ld1 {v13.d}[1], [sp], x6");
  COMPARE(Ld1(v13.D(), 1, MemOperand(sp, 8, PostIndex)),
          "ld1 {v13.d}[1], [sp], #8");

  COMPARE(Ld2(v0.V8B(), v1.V8B(), 0, MemOperand(x15)),
          "ld2 {v0.b, v1.b}[0], [x15]");
  COMPARE(Ld2(v1.V16B(), v2.V16B(), 1, MemOperand(x16)),
          "ld2 {v1.b, v2.b}[1], [x16]");
  COMPARE(Ld2(v2.V4H(), v3.V4H(), 2, MemOperand(x17)),
          "ld2 {v2.h, v3.h}[2], [x17]");
  COMPARE(Ld2(v3.V8H(), v4.V8H(), 3, MemOperand(x18)),
          "ld2 {v3.h, v4.h}[3], [x18]");
  COMPARE(Ld2(v4.V2S(), v5.V2S(), 0, MemOperand(x19)),
          "ld2 {v4.s, v5.s}[0], [x19]");
  COMPARE(Ld2(v5.V4S(), v6.V4S(), 1, MemOperand(x20)),
          "ld2 {v5.s, v6.s}[1], [x20]");
  COMPARE(Ld2(v6.V2D(), v7.V2D(), 0, MemOperand(x21)),
          "ld2 {v6.d, v7.d}[0], [x21]");
  COMPARE(Ld2(v7.B(), v8_.B(), 7, MemOperand(x22)),
          "ld2 {v7.b, v8.b}[7], [x22]");
  COMPARE(Ld2(v8_.B(), v9.B(), 15, MemOperand(x23)),
          "ld2 {v8.b, v9.b}[15], [x23]");
  COMPARE(Ld2(v9.H(), v10.H(), 3, MemOperand(x24)),
          "ld2 {v9.h, v10.h}[3], [x24]");
  COMPARE(Ld2(v10.H(), v11.H(), 7, MemOperand(x25)),
          "ld2 {v10.h, v11.h}[7], [x25]");
  COMPARE(Ld2(v11.S(), v12.S(), 1, MemOperand(x26)),
          "ld2 {v11.s, v12.s}[1], [x26]");
  COMPARE(Ld2(v12.S(), v13.S(), 3, MemOperand(x27)),
          "ld2 {v12.s, v13.s}[3], [cp]");
  COMPARE(Ld2(v13.D(), v14.D(), 1, MemOperand(sp)),
          "ld2 {v13.d, v14.d}[1], [sp]");

  COMPARE(Ld2(v0.V8B(), v1.V8B(), 0, MemOperand(x15, x0, PostIndex)),
          "ld2 {v0.b, v1.b}[0], [x15], x0");
  COMPARE(Ld2(v1.V16B(), v2.V16B(), 1, MemOperand(x16, 2, PostIndex)),
          "ld2 {v1.b, v2.b}[1], [x16], #2");
  COMPARE(Ld2(v2.V4H(), v3.V4H(), 2, MemOperand(x17, 4, PostIndex)),
          "ld2 {v2.h, v3.h}[2], [x17], #4");
  COMPARE(Ld2(v3.V8H(), v4.V8H(), 3, MemOperand(x18, x1, PostIndex)),
          "ld2 {v3.h, v4.h}[3], [x18], x1");
  COMPARE(Ld2(v4.V2S(), v5.V2S(), 0, MemOperand(x19, x2, PostIndex)),
          "ld2 {v4.s, v5.s}[0], [x19], x2");
  COMPARE(Ld2(v5.V4S(), v6.V4S(), 1, MemOperand(x20, 8, PostIndex)),
          "ld2 {v5.s, v6.s}[1], [x20], #8");
  COMPARE(Ld2(v6.V2D(), v7.V2D(), 0, MemOperand(x21, 16, PostIndex)),
          "ld2 {v6.d, v7.d}[0], [x21], #16");
  COMPARE(Ld2(v7.B(), v8_.B(), 7, MemOperand(x22, 2, PostIndex)),
          "ld2 {v7.b, v8.b}[7], [x22], #2");
  COMPARE(Ld2(v8_.B(), v9.B(), 15, MemOperand(x23, x3, PostIndex)),
          "ld2 {v8.b, v9.b}[15], [x23], x3");
  COMPARE(Ld2(v9.H(), v10.H(), 3, MemOperand(x24, x4, PostIndex)),
          "ld2 {v9.h, v10.h}[3], [x24], x4");
  COMPARE(Ld2(v10.H(), v11.H(), 7, MemOperand(x25, 4, PostIndex)),
          "ld2 {v10.h, v11.h}[7], [x25], #4");
  COMPARE(Ld2(v11.S(), v12.S(), 1, MemOperand(x26, 8, PostIndex)),
          "ld2 {v11.s, v12.s}[1], [x26], #8");
  COMPARE(Ld2(v12.S(), v13.S(), 3, MemOperand(x27, x5, PostIndex)),
          "ld2 {v12.s, v13.s}[3], [cp], x5");
  COMPARE(Ld2(v11.S(), v12.S(), 3, MemOperand(x26, 8, PostIndex)),
          "ld2 {v11.s, v12.s}[3], [x26], #8");
  COMPARE(Ld2(v13.D(), v14.D(), 1, MemOperand(sp, x6, PostIndex)),
          "ld2 {v13.d, v14.d}[1], [sp], x6");
  COMPARE(Ld2(v13.D(), v14.D(), 1, MemOperand(sp, 16, PostIndex)),
          "ld2 {v13.d, v14.d}[1], [sp], #16");

  COMPARE(Ld3(v0.V8B(), v1.V8B(), v2.V8B(), 0, MemOperand(x15)),
          "ld3 {v0.b, v1.b, v2.b}[0], [x15]");
  COMPARE(Ld3(v1.V16B(), v2.V16B(), v3.V16B(), 1, MemOperand(x16)),
          "ld3 {v1.b, v2.b, v3.b}[1], [x16]");
  COMPARE(Ld3(v2.V4H(), v3.V4H(), v4.V4H(), 2, MemOperand(x17)),
          "ld3 {v2.h, v3.h, v4.h}[2], [x17]");
  COMPARE(Ld3(v3.V8H(), v4.V8H(), v5.V8H(), 3, MemOperand(x18)),
          "ld3 {v3.h, v4.h, v5.h}[3], [x18]");
  COMPARE(Ld3(v4.V2S(), v5.V2S(), v6.V2S(), 0, MemOperand(x19)),
          "ld3 {v4.s, v5.s, v6.s}[0], [x19]");
  COMPARE(Ld3(v5.V4S(), v6.V4S(), v7.V4S(), 1, MemOperand(x20)),
          "ld3 {v5.s, v6.s, v7.s}[1], [x20]");
  COMPARE(Ld3(v6.V2D(), v7.V2D(), v8_.V2D(), 0, MemOperand(x21)),
          "ld3 {v6.d, v7.d, v8.d}[0], [x21]");
  COMPARE(Ld3(v7.B(), v8_.B(), v9.B(), 7, MemOperand(x22)),
          "ld3 {v7.b, v8.b, v9.b}[7], [x22]");
  COMPARE(Ld3(v8_.B(), v9.B(), v10.B(), 15, MemOperand(x23)),
          "ld3 {v8.b, v9.b, v10.b}[15], [x23]");
  COMPARE(Ld3(v9.H(), v10.H(), v11.H(), 3, MemOperand(x24)),
          "ld3 {v9.h, v10.h, v11.h}[3], [x24]");
  COMPARE(Ld3(v10.H(), v11.H(), v12.H(), 7, MemOperand(x25)),
          "ld3 {v10.h, v11.h, v12.h}[7], [x25]");
  COMPARE(Ld3(v11.S(), v12.S(), v13.S(), 1, MemOperand(x26)),
          "ld3 {v11.s, v12.s, v13.s}[1], [x26]");
  COMPARE(Ld3(v12.S(), v13.S(), v14.S(), 3, MemOperand(x27)),
          "ld3 {v12.s, v13.s, v14.s}[3], [cp]");
  COMPARE(Ld3(v13.D(), v14.D(), v15.D(), 1, MemOperand(sp)),
          "ld3 {v13.d, v14.d, v15.d}[1], [sp]");

  COMPARE(Ld3(v0.V8B(), v1.V8B(), v2.V8B(), 0, MemOperand(x15, x0, PostIndex)),
          "ld3 {v0.b, v1.b, v2.b}[0], [x15], x0");
  COMPARE(
      Ld3(v1.V16B(), v2.V16B(), v3.V16B(), 1, MemOperand(x16, 3, PostIndex)),
      "ld3 {v1.b, v2.b, v3.b}[1], [x16], #3");
  COMPARE(Ld3(v2.V4H(), v3.V4H(), v4.V4H(), 2, MemOperand(x17, 6, PostIndex)),
          "ld3 {v2.h, v3.h, v4.h}[2], [x17], #6");
  COMPARE(Ld3(v3.V8H(), v4.V8H(), v5.V8H(), 3, MemOperand(x18, x1, PostIndex)),
          "ld3 {v3.h, v4.h, v5.h}[3], [x18], x1");
  COMPARE(Ld3(v4.V2S(), v5.V2S(), v6.V2S(), 0, MemOperand(x19, x2, PostIndex)),
          "ld3 {v4.s, v5.s, v6.s}[0], [x19], x2");
  COMPARE(Ld3(v5.V4S(), v6.V4S(), v7.V4S(), 1, MemOperand(x20, 12, PostIndex)),
          "ld3 {v5.s, v6.s, v7.s}[1], [x20], #12");
  COMPARE(Ld3(v6.V2D(), v7.V2D(), v8_.V2D(), 0, MemOperand(x21, 24, PostIndex)),
          "ld3 {v6.d, v7.d, v8.d}[0], [x21], #24");
  COMPARE(Ld3(v7.B(), v8_.B(), v9.B(), 7, MemOperand(x22, 3, PostIndex)),
          "ld3 {v7.b, v8.b, v9.b}[7], [x22], #3");
  COMPARE(Ld3(v8_.B(), v9.B(), v10.B(), 15, MemOperand(x23, x3, PostIndex)),
          "ld3 {v8.b, v9.b, v10.b}[15], [x23], x3");
  COMPARE(Ld3(v9.H(), v10.H(), v11.H(), 3, MemOperand(x24, x4, PostIndex)),
          "ld3 {v9.h, v10.h, v11.h}[3], [x24], x4");
  COMPARE(Ld3(v10.H(), v11.H(), v12.H(), 7, MemOperand(x25, 6, PostIndex)),
          "ld3 {v10.h, v11.h, v12.h}[7], [x25], #6");
  COMPARE(Ld3(v11.S(), v12.S(), v13.S(), 1, MemOperand(x26, 12, PostIndex)),
          "ld3 {v11.s, v12.s, v13.s}[1], [x26], #12");
  COMPARE(Ld3(v12.S(), v13.S(), v14.S(), 3, MemOperand(x27, x5, PostIndex)),
          "ld3 {v12.s, v13.s, v14.s}[3], [cp], x5");
  COMPARE(Ld3(v12.S(), v13.S(), v14.S(), 3, MemOperand(x27, 12, PostIndex)),
          "ld3 {v12.s, v13.s, v14.s}[3], [cp], #12");
  COMPARE(Ld3(v13.D(), v14.D(), v15.D(), 1, MemOperand(sp, x6, PostIndex)),
          "ld3 {v13.d, v14.d, v15.d}[1], [sp], x6");
  COMPARE(Ld3(v13.D(), v14.D(), v15.D(), 1, MemOperand(sp, 24, PostIndex)),
          "ld3 {v13.d, v14.d, v15.d}[1], [sp], #24");

  COMPARE(Ld4(v0.V8B(), v1.V8B(), v2.V8B(), v3.V8B(), 0, MemOperand(x15)),
          "ld4 {v0.b, v1.b, v2.b, v3.b}[0], [x15]");
  COMPARE(Ld4(v1.V16B(), v2.V16B(), v3.V16B(), v4.V16B(), 1, MemOperand(x16)),
          "ld4 {v1.b, v2.b, v3.b, v4.b}[1], [x16]");
  COMPARE(Ld4(v2.V4H(), v3.V4H(), v4.V4H(), v5.V4H(), 2, MemOperand(x17)),
          "ld4 {v2.h, v3.h, v4.h, v5.h}[2], [x17]");
  COMPARE(Ld4(v3.V8H(), v4.V8H(), v5.V8H(), v6.V8H(), 3, MemOperand(x18)),
          "ld4 {v3.h, v4.h, v5.h, v6.h}[3], [x18]");
  COMPARE(Ld4(v4.V2S(), v5.V2S(), v6.V2S(), v7.V2S(), 0, MemOperand(x19)),
          "ld4 {v4.s, v5.s, v6.s, v7.s}[0], [x19]");
  COMPARE(Ld4(v5.V4S(), v6.V4S(), v7.V4S(), v8_.V4S(), 1, MemOperand(x20)),
          "ld4 {v5.s, v6.s, v7.s, v8.s}[1], [x20]");
  COMPARE(Ld4(v6.V2D(), v7.V2D(), v8_.V2D(), v9.V2D(), 0, MemOperand(x21)),
          "ld4 {v6.d, v7.d, v8.d, v9.d}[0], [x21]");
  COMPARE(Ld4(v7.B(), v8_.B(), v9.B(), v10.B(), 7, MemOperand(x22)),
          "ld4 {v7.b, v8.b, v9.b, v10.b}[7], [x22]");
  COMPARE(Ld4(v8_.B(), v9.B(), v10.B(), v11.B(), 15, MemOperand(x23)),
          "ld4 {v8.b, v9.b, v10.b, v11.b}[15], [x23]");
  COMPARE(Ld4(v9.H(), v10.H(), v11.H(), v12.H(), 3, MemOperand(x24)),
          "ld4 {v9.h, v10.h, v11.h, v12.h}[3], [x24]");
  COMPARE(Ld4(v10.H(), v11.H(), v12.H(), v13.H(), 7, MemOperand(x25)),
          "ld4 {v10.h, v11.h, v12.h, v13.h}[7], [x25]");
  COMPARE(Ld4(v11.S(), v12.S(), v13.S(), v14.S(), 1, MemOperand(x26)),
          "ld4 {v11.s, v12.s, v13.s, v14.s}[1], [x26]");
  COMPARE(Ld4(v12.S(), v13.S(), v14.S(), v15.S(), 3, MemOperand(x27)),
          "ld4 {v12.s, v13.s, v14.s, v15.s}[3], [cp]");
  COMPARE(Ld4(v13.D(), v14.D(), v15.D(), v16.D(), 1, MemOperand(sp)),
          "ld4 {v13.d, v14.d, v15.d, v16.d}[1], [sp]");

  COMPARE(Ld4(v0.V8B(), v1.V8B(), v2.V8B(), v3.V8B(), 0,
              MemOperand(x15, x0, PostIndex)),
          "ld4 {v0.b, v1.b, v2.b, v3.b}[0], [x15], x0");
  COMPARE(Ld4(v1.V16B(), v2.V16B(), v3.V16B(), v4.V16B(), 1,
              MemOperand(x16, 4, PostIndex)),
          "ld4 {v1.b, v2.b, v3.b, v4.b}[1], [x16], #4");
  COMPARE(Ld4(v2.V4H(), v3.V4H(), v4.V4H(), v5.V4H(), 2,
              MemOperand(x17, 8, PostIndex)),
          "ld4 {v2.h, v3.h, v4.h, v5.h}[2], [x17], #8");
  COMPARE(Ld4(v3.V8H(), v4.V8H(), v5.V8H(), v6.V8H(), 3,
              MemOperand(x18, x1, PostIndex)),
          "ld4 {v3.h, v4.h, v5.h, v6.h}[3], [x18], x1");
  COMPARE(Ld4(v4.V2S(), v5.V2S(), v6.V2S(), v7.V2S(), 0,
              MemOperand(x19, x2, PostIndex)),
          "ld4 {v4.s, v5.s, v6.s, v7.s}[0], [x19], x2");
  COMPARE(Ld4(v5.V4S(), v6.V4S(), v7.V4S(), v8_.V4S(), 1,
              MemOperand(x20, 16, PostIndex)),
          "ld4 {v5.s, v6.s, v7.s, v8.s}[1], [x20], #16");
  COMPARE(Ld4(v6.V2D(), v7.V2D(), v8_.V2D(), v9.V2D(), 0,
              MemOperand(x21, 32, PostIndex)),
          "ld4 {v6.d, v7.d, v8.d, v9.d}[0], [x21], #32");
  COMPARE(
      Ld4(v7.B(), v8_.B(), v9.B(), v10.B(), 7, MemOperand(x22, 4, PostIndex)),
      "ld4 {v7.b, v8.b, v9.b, v10.b}[7], [x22], #4");
  COMPARE(Ld4(v8_.B(), v9.B(), v10.B(), v11.B(), 15,
              MemOperand(x23, x3, PostIndex)),
          "ld4 {v8.b, v9.b, v10.b, v11.b}[15], [x23], x3");
  COMPARE(
      Ld4(v9.H(), v10.H(), v11.H(), v12.H(), 3, MemOperand(x24, x4, PostIndex)),
      "ld4 {v9.h, v10.h, v11.h, v12.h}[3], [x24], x4");
  COMPARE(
      Ld4(v10.H(), v11.H(), v12.H(), v13.H(), 7, MemOperand(x25, 8, PostIndex)),
      "ld4 {v10.h, v11.h, v12.h, v13.h}[7], [x25], #8");
  COMPARE(Ld4(v11.S(), v12.S(), v13.S(), v14.S(), 1,
              MemOperand(x26, 16, PostIndex)),
          "ld4 {v11.s, v12.s, v13.s, v14.s}[1], [x26], #16");
  COMPARE(Ld4(v12.S(), v13.S(), v14.S(), v15.S(), 3,
              MemOperand(x27, x5, PostIndex)),
          "ld4 {v12.s, v13.s, v14.s, v15.s}[3], [cp], x5");
  COMPARE(Ld4(v11.S(), v12.S(), v13.S(), v14.S(), 3,
              MemOperand(x26, 16, PostIndex)),
          "ld4 {v11.s, v12.s, v13.s, v14.s}[3], [x26], #16");
  COMPARE(
      Ld4(v13.D(), v14.D(), v15.D(), v16.D(), 1, MemOperand(sp, x6, PostIndex)),
      "ld4 {v13.d, v14.d, v15.d, v16.d}[1], [sp], x6");
  COMPARE(
      Ld4(v13.D(), v14.D(), v15.D(), v16.D(), 1, MemOperand(sp, 32, PostIndex)),
      "ld4 {v13.d, v14.d, v15.d, v16.d}[1], [sp], #32");

  COMPARE(St1(v0.V8B(), 0, MemOperand(x15)), "st1 {v0.b}[0], [x15]");
  COMPARE(St1(v1.V16B(), 1, MemOperand(x16)), "st1 {v1.b}[1], [x16]");
  COMPARE(St1(v2.V4H(), 2, MemOperand(x17)), "st1 {v2.h}[2], [x17]");
  COMPARE(St1(v3.V8H(), 3, MemOperand(x18)), "st1 {v3.h}[3], [x18]");
  COMPARE(St1(v4.V2S(), 0, MemOperand(x19)), "st1 {v4.s}[0], [x19]");
  COMPARE(St1(v5.V4S(), 1, MemOperand(x20)), "st1 {v5.s}[1], [x20]");
  COMPARE(St1(v6.V2D(), 0, MemOperand(x21)), "st1 {v6.d}[0], [x21]");
  COMPARE(St1(v7.B(), 7, MemOperand(x22)), "st1 {v7.b}[7], [x22]");
  COMPARE(St1(v8_.B(), 15, MemOperand(x23)), "st1 {v8.b}[15], [x23]");
  COMPARE(St1(v9.H(), 3, MemOperand(x24)), "st1 {v9.h}[3], [x24]");
  COMPARE(St1(v10.H(), 7, MemOperand(x25)), "st1 {v10.h}[7], [x25]");
  COMPARE(St1(v11.S(), 1, MemOperand(x26)), "st1 {v11.s}[1], [x26]");
  COMPARE(St1(v12.S(), 3, MemOperand(x27)), "st1 {v12.s}[3], [cp]");
  COMPARE(St1(v13.D(), 1, MemOperand(sp)), "st1 {v13.d}[1], [sp]");

  COMPARE(St1(v0.V8B(), 0, MemOperand(x15, x0, PostIndex)),
          "st1 {v0.b}[0], [x15], x0");
  COMPARE(St1(v1.V16B(), 1, MemOperand(x16, 1, PostIndex)),
          "st1 {v1.b}[1], [x16], #1");
  COMPARE(St1(v2.V4H(), 2, MemOperand(x17, 2, PostIndex)),
          "st1 {v2.h}[2], [x17], #2");
  COMPARE(St1(v3.V8H(), 3, MemOperand(x18, x1, PostIndex)),
          "st1 {v3.h}[3], [x18], x1");
  COMPARE(St1(v4.V2S(), 0, MemOperand(x19, x2, PostIndex)),
          "st1 {v4.s}[0], [x19], x2");
  COMPARE(St1(v5.V4S(), 1, MemOperand(x20, 4, PostIndex)),
          "st1 {v5.s}[1], [x20], #4");
  COMPARE(St1(v6.V2D(), 0, MemOperand(x21, 8, PostIndex)),
          "st1 {v6.d}[0], [x21], #8");
  COMPARE(St1(v7.B(), 7, MemOperand(x22, 1, PostIndex)),
          "st1 {v7.b}[7], [x22], #1");
  COMPARE(St1(v8_.B(), 15, MemOperand(x23, x3, PostIndex)),
          "st1 {v8.b}[15], [x23], x3");
  COMPARE(St1(v9.H(), 3, MemOperand(x24, x4, PostIndex)),
          "st1 {v9.h}[3], [x24], x4");
  COMPARE(St1(v10.H(), 7, MemOperand(x25, 2, PostIndex)),
          "st1 {v10.h}[7], [x25], #2");
  COMPARE(St1(v11.S(), 1, MemOperand(x26, 4, PostIndex)),
          "st1 {v11.s}[1], [x26], #4");
  COMPARE(St1(v12.S(), 3, MemOperand(x27, x5, PostIndex)),
          "st1 {v12.s}[3], [cp], x5");
  COMPARE(St1(v13.D(), 1, MemOperand(sp, x6, PostIndex)),
          "st1 {v13.d}[1], [sp], x6");
  COMPARE(St2(v0.V8B(), v1.V8B(), 0, MemOperand(x15, x0, PostIndex)),
          "st2 {v0.b, v1.b}[0], [x15], x0");
  COMPARE(St2(v1.V16B(), v2.V16B(), 1, MemOperand(x16, 2, PostIndex)),
          "st2 {v1.b, v2.b}[1], [x16], #2");
  COMPARE(St2(v2.V4H(), v3.V4H(), 2, MemOperand(x17, 4, PostIndex)),
          "st2 {v2.h, v3.h}[2], [x17], #4");
  COMPARE(St2(v3.V8H(), v4.V8H(), 3, MemOperand(x18, x1, PostIndex)),
          "st2 {v3.h, v4.h}[3], [x18], x1");
  COMPARE(St2(v4.V2S(), v5.V2S(), 0, MemOperand(x19, x2, PostIndex)),
          "st2 {v4.s, v5.s}[0], [x19], x2");
  COMPARE(St2(v5.V4S(), v6.V4S(), 1, MemOperand(x20, 8, PostIndex)),
          "st2 {v5.s, v6.s}[1], [x20], #8");
  COMPARE(St2(v6.V2D(), v7.V2D(), 0, MemOperand(x21, 16, PostIndex)),
          "st2 {v6.d, v7.d}[0], [x21], #16");
  COMPARE(St2(v7.B(), v8_.B(), 7, MemOperand(x22, 2, PostIndex)),
          "st2 {v7.b, v8.b}[7], [x22], #2");
  COMPARE(St2(v8_.B(), v9.B(), 15, MemOperand(x23, x3, PostIndex)),
          "st2 {v8.b, v9.b}[15], [x23], x3");
  COMPARE(St2(v9.H(), v10.H(), 3, MemOperand(x24, x4, PostIndex)),
          "st2 {v9.h, v10.h}[3], [x24], x4");
  COMPARE(St2(v10.H(), v11.H(), 7, MemOperand(x25, 4, PostIndex)),
          "st2 {v10.h, v11.h}[7], [x25], #4");
  COMPARE(St2(v11.S(), v12.S(), 1, MemOperand(x26, 8, PostIndex)),
          "st2 {v11.s, v12.s}[1], [x26], #8");
  COMPARE(St2(v12.S(), v13.S(), 3, MemOperand(x27, x5, PostIndex)),
          "st2 {v12.s, v13.s}[3], [cp], x5");
  COMPARE(St2(v13.D(), v14.D(), 1, MemOperand(sp, x6, PostIndex)),
          "st2 {v13.d, v14.d}[1], [sp], x6");
  COMPARE(St3(VLIST3(v0.V8B()), 0, MemOperand(x15, x0, PostIndex)),
          "st3 {v0.b, v1.b, v2.b}[0], [x15], x0");
  COMPARE(St3(VLIST3(v1.V16B()), 1, MemOperand(x16, 3, PostIndex)),
          "st3 {v1.b, v2.b, v3.b}[1], [x16], #3");
  COMPARE(St3(VLIST3(v2.V4H()), 2, MemOperand(x17, 6, PostIndex)),
          "st3 {v2.h, v3.h, v4.h}[2], [x17], #6");
  COMPARE(St3(VLIST3(v3.V8H()), 3, MemOperand(x18, x1, PostIndex)),
          "st3 {v3.h, v4.h, v5.h}[3], [x18], x1");
  COMPARE(St3(VLIST3(v4.V2S()), 0, MemOperand(x19, x2, PostIndex)),
          "st3 {v4.s, v5.s, v6.s}[0], [x19], x2");
  COMPARE(St3(VLIST3(v5.V4S()), 1, MemOperand(x20, 12, PostIndex)),
          "st3 {v5.s, v6.s, v7.s}[1], [x20], #12");
  COMPARE(St3(VLIST3(v6.V2D()), 0, MemOperand(x21, 24, PostIndex)),
          "st3 {v6.d, v7.d, v8.d}[0], [x21], #24");
  COMPARE(St3(VLIST3(v7.B()), 7, MemOperand(x22, 3, PostIndex)),
          "st3 {v7.b, v8.b, v9.b}[7], [x22], #3");
  COMPARE(St3(VLIST3(v8_.B()), 15, MemOperand(x23, x3, PostIndex)),
          "st3 {v8.b, v9.b, v10.b}[15], [x23], x3");
  COMPARE(St3(VLIST3(v9.H()), 3, MemOperand(x24, x4, PostIndex)),
          "st3 {v9.h, v10.h, v11.h}[3], [x24], x4");
  COMPARE(St3(VLIST3(v10.H()), 7, MemOperand(x25, 6, PostIndex)),
          "st3 {v10.h, v11.h, v12.h}[7], [x25], #6");
  COMPARE(St3(VLIST3(v11.S()), 1, MemOperand(x26, 12, PostIndex)),
          "st3 {v11.s, v12.s, v13.s}[1], [x26], #12");
  COMPARE(St3(VLIST3(v12.S()), 3, MemOperand(x27, x5, PostIndex)),
          "st3 {v12.s, v13.s, v14.s}[3], [cp], x5");
  COMPARE(St3(VLIST3(v13.D()), 1, MemOperand(sp, x6, PostIndex)),
          "st3 {v13.d, v14.d, v15.d}[1], [sp], x6");

  COMPARE(St4(VLIST4(v0.V8B()), 0, MemOperand(x15, x0, PostIndex)),
          "st4 {v0.b, v1.b, v2.b, v3.b}[0], [x15], x0");
  COMPARE(St4(VLIST4(v1.V16B()), 1, MemOperand(x16, 4, PostIndex)),
          "st4 {v1.b, v2.b, v3.b, v4.b}[1], [x16], #4");
  COMPARE(St4(VLIST4(v2.V4H()), 2, MemOperand(x17, 8, PostIndex)),
          "st4 {v2.h, v3.h, v4.h, v5.h}[2], [x17], #8");
  COMPARE(St4(VLIST4(v3.V8H()), 3, MemOperand(x18, x1, PostIndex)),
          "st4 {v3.h, v4.h, v5.h, v6.h}[3], [x18], x1");
  COMPARE(St4(VLIST4(v4.V2S()), 0, MemOperand(x19, x2, PostIndex)),
          "st4 {v4.s, v5.s, v6.s, v7.s}[0], [x19], x2");
  COMPARE(St4(VLIST4(v5.V4S()), 1, MemOperand(x20, 16, PostIndex)),
          "st4 {v5.s, v6.s, v7.s, v8.s}[1], [x20], #16");
  COMPARE(St4(VLIST4(v6.V2D()), 0, MemOperand(x21, 32, PostIndex)),
          "st4 {v6.d, v7.d, v8.d, v9.d}[0], [x21], #32");
  COMPARE(St4(VLIST4(v7.B()), 7, MemOperand(x22, 4, PostIndex)),
          "st4 {v7.b, v8.b, v9.b, v10.b}[7], [x22], #4");
  COMPARE(St4(VLIST4(v8_.B()), 15, MemOperand(x23, x3, PostIndex)),
          "st4 {v8.b, v9.b, v10.b, v11.b}[15], [x23], x3");
  COMPARE(St4(VLIST4(v9.H()), 3, MemOperand(x24, x4, PostIndex)),
          "st4 {v9.h, v10.h, v11.h, v12.h}[3], [x24], x4");
  COMPARE(St4(VLIST4(v10.H()), 7, MemOperand(x25, 8, PostIndex)),
          "st4 {v10.h, v11.h, v12.h, v13.h}[7], [x25], #8");
  COMPARE(St4(VLIST4(v11.S()), 1, MemOperand(x26, 16, PostIndex)),
          "st4 {v11.s, v12.s, v13.s, v14.s}[1], [x26], #16");
  COMPARE(St4(VLIST4(v12.S()), 3, MemOperand(x27, x5, PostIndex)),
          "st4 {v12.s, v13.s, v14.s, v15.s}[3], [cp], x5");
  COMPARE(St4(VLIST4(v13.D()), 1, MemOperand(sp, x6, PostIndex)),
          "st4 {v13.d, v14.d, v15.d, v16.d}[1], [sp], x6");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_load_store_lane_unallocated) {
  SET_UP_MASM();

  const char* expected = "unallocated (NEONLoadStoreSingleStruct)";
  // LD1 (single structure) (no offset)
  COMPARE(dci(0x0d404400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d408800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d409400), expected);  // .d, size<0> = 1, S = 1
  // LD2 (single structure) (no offset)
  COMPARE(dci(0x0d604400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d608800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d609400), expected);  // .d, size<0> = 1, S = 1
  // LD3 (single structure) (no offset)
  COMPARE(dci(0x0d406400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d40a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d40b400), expected);  // .d, size<0> = 1, S = 1
  // LD4 (single structure) (no offset)
  COMPARE(dci(0x0d606400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d60a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d60b400), expected);  // .d, size<0> = 1, S = 1
  // ST1 (single structure) (no offset)
  COMPARE(dci(0x0d004400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d008800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d009400), expected);  // .d, size<0> = 1, S = 1
  // ST2 (single structure) (no offset)
  COMPARE(dci(0x0d204400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d208800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d209400), expected);  // .d, size<0> = 1, S = 1
  // ST3 (single structure) (no offset)
  COMPARE(dci(0x0d006400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d00a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d00b400), expected);  // .d, size<0> = 1, S = 1
  // ST4 (single structure) (no offset)
  COMPARE(dci(0x0d206400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d20a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d20b400), expected);  // .d, size<0> = 1, S = 1

  expected = "unallocated (NEONLoadStoreSingleStructPostIndex)";
  // LD1 (single structure) (post index)
  COMPARE(dci(0x0dc04400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0dc08800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0dc09400), expected);  // .d, size<0> = 1, S = 1
  // LD2 (single structure) (post index)
  COMPARE(dci(0x0de04400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0de08800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0de09400), expected);  // .d, size<0> = 1, S = 1
  // LD3 (single structure) (post index)
  COMPARE(dci(0x0dc06400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0dc0a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0dc0b400), expected);  // .d, size<0> = 1, S = 1
  // LD4 (single structure) (post index)
  COMPARE(dci(0x0de06400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0de0a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0de0b400), expected);  // .d, size<0> = 1, S = 1
  // ST1 (single structure) (post index)
  COMPARE(dci(0x0d804400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d808800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d809400), expected);  // .d, size<0> = 1, S = 1
  // ST2 (single structure) (post index)
  COMPARE(dci(0x0da04400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0da08800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0da09400), expected);  // .d, size<0> = 1, S = 1
  // ST3 (single structure) (post index)
  COMPARE(dci(0x0d806400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0d80a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0d80b400), expected);  // .d, size<0> = 1, S = 1
  // ST4 (single structure) (post index)
  COMPARE(dci(0x0da06400), expected);  // .h, size<0> = 1
  COMPARE(dci(0x0da0a800), expected);  // .s, size<1> = 1
  COMPARE(dci(0x0da0b400), expected);  // .d, size<0> = 1, S = 1

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_load_all_lanes) {
  SET_UP_MASM();

  COMPARE(Ld1r(v14.V8B(), MemOperand(x0)), "ld1r {v14.8b}, [x0]");
  COMPARE(Ld1r(v15.V16B(), MemOperand(x1)), "ld1r {v15.16b}, [x1]");
  COMPARE(Ld1r(v16.V4H(), MemOperand(x2)), "ld1r {v16.4h}, [x2]");
  COMPARE(Ld1r(v17.V8H(), MemOperand(x3)), "ld1r {v17.8h}, [x3]");
  COMPARE(Ld1r(v18.V2S(), MemOperand(x4)), "ld1r {v18.2s}, [x4]");
  COMPARE(Ld1r(v19.V4S(), MemOperand(x5)), "ld1r {v19.4s}, [x5]");
  COMPARE(Ld1r(v20.V2D(), MemOperand(sp)), "ld1r {v20.2d}, [sp]");
  COMPARE(Ld1r(v21.V1D(), MemOperand(x30)), "ld1r {v21.1d}, [lr]");

  COMPARE(Ld1r(v22.V8B(), MemOperand(x6, 1, PostIndex)),
          "ld1r {v22.8b}, [x6], #1");
  COMPARE(Ld1r(v23.V16B(), MemOperand(x7, x16, PostIndex)),
          "ld1r {v23.16b}, [x7], x16");
  COMPARE(Ld1r(v24.V4H(), MemOperand(x8, x17, PostIndex)),
          "ld1r {v24.4h}, [x8], x17");
  COMPARE(Ld1r(v25.V8H(), MemOperand(x9, 2, PostIndex)),
          "ld1r {v25.8h}, [x9], #2");
  COMPARE(Ld1r(v26.V2S(), MemOperand(x10, 4, PostIndex)),
          "ld1r {v26.2s}, [x10], #4");
  COMPARE(Ld1r(v27.V4S(), MemOperand(x11, x18, PostIndex)),
          "ld1r {v27.4s}, [x11], x18");
  COMPARE(Ld1r(v28.V2D(), MemOperand(x12, 8, PostIndex)),
          "ld1r {v28.2d}, [x12], #8");
  COMPARE(Ld1r(v29.V1D(), MemOperand(x13, 8, PostIndex)),
          "ld1r {v29.1d}, [x13], #8");

  COMPARE(Ld2r(v14.V8B(), v15.V8B(), MemOperand(x0)),
          "ld2r {v14.8b, v15.8b}, [x0]");
  COMPARE(Ld2r(v15.V16B(), v16.V16B(), MemOperand(x1)),
          "ld2r {v15.16b, v16.16b}, [x1]");
  COMPARE(Ld2r(v16.V4H(), v17.V4H(), MemOperand(x2)),
          "ld2r {v16.4h, v17.4h}, [x2]");
  COMPARE(Ld2r(v17.V8H(), v18.V8H(), MemOperand(x3)),
          "ld2r {v17.8h, v18.8h}, [x3]");
  COMPARE(Ld2r(v18.V2S(), v19.V2S(), MemOperand(x4)),
          "ld2r {v18.2s, v19.2s}, [x4]");
  COMPARE(Ld2r(v19.V4S(), v20.V4S(), MemOperand(x5)),
          "ld2r {v19.4s, v20.4s}, [x5]");
  COMPARE(Ld2r(v20.V2D(), v21.V2D(), MemOperand(sp)),
          "ld2r {v20.2d, v21.2d}, [sp]");
  COMPARE(Ld2r(v21.V8B(), v22.V8B(), MemOperand(x6, 2, PostIndex)),
          "ld2r {v21.8b, v22.8b}, [x6], #2");
  COMPARE(Ld2r(v22.V16B(), v23.V16B(), MemOperand(x7, x16, PostIndex)),
          "ld2r {v22.16b, v23.16b}, [x7], x16");
  COMPARE(Ld2r(v23.V4H(), v24.V4H(), MemOperand(x8, x17, PostIndex)),
          "ld2r {v23.4h, v24.4h}, [x8], x17");
  COMPARE(Ld2r(v24.V8H(), v25.V8H(), MemOperand(x9, 4, PostIndex)),
          "ld2r {v24.8h, v25.8h}, [x9], #4");
  COMPARE(Ld2r(v25.V2S(), v26.V2S(), MemOperand(x10, 8, PostIndex)),
          "ld2r {v25.2s, v26.2s}, [x10], #8");
  COMPARE(Ld2r(v26.V4S(), v27.V4S(), MemOperand(x11, x18, PostIndex)),
          "ld2r {v26.4s, v27.4s}, [x11], x18");
  COMPARE(Ld2r(v27.V2D(), v28.V2D(), MemOperand(x12, 16, PostIndex)),
          "ld2r {v27.2d, v28.2d}, [x12], #16");

  COMPARE(Ld3r(v14.V8B(), v15.V8B(), v16.V8B(), MemOperand(x0)),
          "ld3r {v14.8b, v15.8b, v16.8b}, [x0]");
  COMPARE(Ld3r(v15.V16B(), v16.V16B(), v17.V16B(), MemOperand(x1)),
          "ld3r {v15.16b, v16.16b, v17.16b}, [x1]");
  COMPARE(Ld3r(v16.V4H(), v17.V4H(), v18.V4H(), MemOperand(x2)),
          "ld3r {v16.4h, v17.4h, v18.4h}, [x2]");
  COMPARE(Ld3r(v17.V8H(), v18.V8H(), v19.V8H(), MemOperand(x3)),
          "ld3r {v17.8h, v18.8h, v19.8h}, [x3]");
  COMPARE(Ld3r(v18.V2S(), v19.V2S(), v20.V2S(), MemOperand(x4)),
          "ld3r {v18.2s, v19.2s, v20.2s}, [x4]");
  COMPARE(Ld3r(v19.V4S(), v20.V4S(), v21.V4S(), MemOperand(x5)),
          "ld3r {v19.4s, v20.4s, v21.4s}, [x5]");
  COMPARE(Ld3r(v20.V2D(), v21.V2D(), v22.V2D(), MemOperand(sp)),
          "ld3r {v20.2d, v21.2d, v22.2d}, [sp]");
  COMPARE(Ld3r(v21.V8B(), v22.V8B(), v23.V8B(), MemOperand(x6, 3, PostIndex)),
          "ld3r {v21.8b, v22.8b, v23.8b}, [x6], #3");
  COMPARE(
      Ld3r(v22.V16B(), v23.V16B(), v24.V16B(), MemOperand(x7, x16, PostIndex)),
      "ld3r {v22.16b, v23.16b, v24.16b}, [x7], x16");
  COMPARE(Ld3r(v23.V4H(), v24.V4H(), v25.V4H(), MemOperand(x8, x17, PostIndex)),
          "ld3r {v23.4h, v24.4h, v25.4h}, [x8], x17");
  COMPARE(Ld3r(v24.V8H(), v25.V8H(), v26.V8H(), MemOperand(x9, 6, PostIndex)),
          "ld3r {v24.8h, v25.8h, v26.8h}, [x9], #6");
  COMPARE(Ld3r(v25.V2S(), v26.V2S(), v27.V2S(), MemOperand(x10, 12, PostIndex)),
          "ld3r {v25.2s, v26.2s, v27.2s}, [x10], #12");
  COMPARE(
      Ld3r(v26.V4S(), v27.V4S(), v28.V4S(), MemOperand(x11, x18, PostIndex)),
      "ld3r {v26.4s, v27.4s, v28.4s}, [x11], x18");
  COMPARE(Ld3r(v27.V2D(), v28.V2D(), v29.V2D(), MemOperand(x12, 24, PostIndex)),
          "ld3r {v27.2d, v28.2d, v29.2d}, [x12], #24");

  COMPARE(Ld4r(v14.V8B(), v15.V8B(), v16.V8B(), v17.V8B(), MemOperand(x0)),
          "ld4r {v14.8b, v15.8b, v16.8b, v17.8b}, [x0]");
  COMPARE(Ld4r(v15.V16B(), v16.V16B(), v17.V16B(), v18.V16B(), MemOperand(x1)),
          "ld4r {v15.16b, v16.16b, v17.16b, v18.16b}, [x1]");
  COMPARE(Ld4r(v16.V4H(), v17.V4H(), v18.V4H(), v19.V4H(), MemOperand(x2)),
          "ld4r {v16.4h, v17.4h, v18.4h, v19.4h}, [x2]");
  COMPARE(Ld4r(v17.V8H(), v18.V8H(), v19.V8H(), v20.V8H(), MemOperand(x3)),
          "ld4r {v17.8h, v18.8h, v19.8h, v20.8h}, [x3]");
  COMPARE(Ld4r(v18.V2S(), v19.V2S(), v20.V2S(), v21.V2S(), MemOperand(x4)),
          "ld4r {v18.2s, v19.2s, v20.2s, v21.2s}, [x4]");
  COMPARE(Ld4r(v19.V4S(), v20.V4S(), v21.V4S(), v22.V4S(), MemOperand(x5)),
          "ld4r {v19.4s, v20.4s, v21.4s, v22.4s}, [x5]");
  COMPARE(Ld4r(v20.V2D(), v21.V2D(), v22.V2D(), v23.V2D(), MemOperand(sp)),
          "ld4r {v20.2d, v21.2d, v22.2d, v23.2d}, [sp]");
  COMPARE(Ld4r(v21.V8B(), v22.V8B(), v23.V8B(), v24.V8B(),
               MemOperand(x6, 4, PostIndex)),
          "ld4r {v21.8b, v22.8b, v23.8b, v24.8b}, [x6], #4");
  COMPARE(Ld4r(v22.V16B(), v23.V16B(), v24.V16B(), v25.V16B(),
               MemOperand(x7, x16, PostIndex)),
          "ld4r {v22.16b, v23.16b, v24.16b, v25.16b}, [x7], x16");
  COMPARE(Ld4r(v23.V4H(), v24.V4H(), v25.V4H(), v26.V4H(),
               MemOperand(x8, x17, PostIndex)),
          "ld4r {v23.4h, v24.4h, v25.4h, v26.4h}, [x8], x17");
  COMPARE(Ld4r(v24.V8H(), v25.V8H(), v26.V8H(), v27.V8H(),
               MemOperand(x9, 8, PostIndex)),
          "ld4r {v24.8h, v25.8h, v26.8h, v27.8h}, [x9], #8");
  COMPARE(Ld4r(v25.V2S(), v26.V2S(), v27.V2S(), v28.V2S(),
               MemOperand(x10, 16, PostIndex)),
          "ld4r {v25.2s, v26.2s, v27.2s, v28.2s}, [x10], #16");
  COMPARE(Ld4r(v26.V4S(), v27.V4S(), v28.V4S(), v29.V4S(),
               MemOperand(x11, x18, PostIndex)),
          "ld4r {v26.4s, v27.4s, v28.4s, v29.4s}, [x11], x18");
  COMPARE(Ld4r(v27.V2D(), v28.V2D(), v29.V2D(), v30.V2D(),
               MemOperand(x12, 32, PostIndex)),
          "ld4r {v27.2d, v28.2d, v29.2d, v30.2d}, [x12], #32");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_load_all_lanes_unallocated) {
  SET_UP_MASM();

  const char* expected = "unallocated (NEONLoadStoreSingleStruct)";
  // LD1R (single structure) (no offset)
  COMPARE(dci(0x0d00c000), expected);  // L = 0
  COMPARE(dci(0x0d40d000), expected);  // S = 1
  // LD2R (single structure) (no offset)
  COMPARE(dci(0x0d20c000), expected);  // L = 0
  COMPARE(dci(0x0d60d000), expected);  // S = 1
  // LD3R (single structure) (no offset)
  COMPARE(dci(0x0d00e000), expected);  // L = 0
  COMPARE(dci(0x0d40f000), expected);  // S = 1
  // LD4R (single structure) (no offset)
  COMPARE(dci(0x0d20e000), expected);  // L = 0
  COMPARE(dci(0x0d60f000), expected);  // S = 1

  expected = "unallocated (NEONLoadStoreSingleStructPostIndex)";
  // LD1R (single structure) (post index)
  COMPARE(dci(0x0d80c000), expected);  // L = 0
  COMPARE(dci(0x0dc0d000), expected);  // S = 1
  // LD2R (single structure) (post index)
  COMPARE(dci(0x0da0c000), expected);  // L = 0
  COMPARE(dci(0x0de0d000), expected);  // S = 1
  // LD3R (single structure) (post index)
  COMPARE(dci(0x0d80e000), expected);  // L = 0
  COMPARE(dci(0x0dc0f000), expected);  // S = 1
  // LD4R (single structure) (post index)
  COMPARE(dci(0x0da0e000), expected);  // L = 0
  COMPARE(dci(0x0de0f000), expected);  // S = 1

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_3same) {
  SET_UP_MASM();

#define DISASM_INST(M, S) \
  COMPARE(Cmeq(v0.M, v1.M, v2.M), "cmeq v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmge(v0.M, v1.M, v2.M), "cmge v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmgt(v0.M, v1.M, v2.M), "cmgt v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmhi(v0.M, v1.M, v2.M), "cmhi v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmhs(v0.M, v1.M, v2.M), "cmhs v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmtst(v0.M, v1.M, v2.M), "cmtst v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Add(v0.M, v1.M, v2.M), "add v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sub(v3.M, v4.M, v5.M), "sub v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sabd(v3.M, v4.M, v5.M), "sabd v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uabd(v3.M, v4.M, v5.M), "uabd v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Saba(v3.M, v4.M, v5.M), "saba v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uaba(v3.M, v4.M, v5.M), "uaba v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Smax(v3.M, v4.M, v5.M), "smax v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Smin(v3.M, v4.M, v5.M), "smin v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Umax(v3.M, v4.M, v5.M), "umax v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Umin(v3.M, v4.M, v5.M), "umin v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Smaxp(v3.M, v4.M, v5.M), "smaxp v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sminp(v3.M, v4.M, v5.M), "sminp v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Umaxp(v3.M, v4.M, v5.M), "umaxp v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uminp(v3.M, v4.M, v5.M), "uminp v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uqadd(v6.M, v7.M, v8_.M), "uqadd v6." S ", v7." S ", v8." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqadd(v9.M, v10.M, v11.M), "sqadd v9." S ", v10." S ", v11." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uqsub(v6.M, v7.M, v8_.M), "uqsub v6." S ", v7." S ", v8." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqsub(v9.M, v10.M, v11.M), "sqsub v9." S ", v10." S ", v11." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sshl(v12.M, v13.M, v14.M), "sshl v12." S ", v13." S ", v14." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Ushl(v15.M, v16.M, v17.M), "ushl v15." S ", v16." S ", v17." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqshl(v18.M, v19.M, v20.M), "sqshl v18." S ", v19." S ", v20." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uqshl(v21.M, v22.M, v23.M), "uqshl v21." S ", v22." S ", v23." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Srshl(v24.M, v25.M, v26.M), "srshl v24." S ", v25." S ", v26." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Urshl(v27.M, v28.M, v29.M), "urshl v27." S ", v28." S ", v29." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqrshl(v30.M, v31.M, v0.M), "sqrshl v30." S ", v31." S ", v0." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uqrshl(v1.M, v2.M, v3.M), "uqrshl v1." S ", v2." S ", v3." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Shadd(v4.M, v5.M, v6.M), "shadd v4." S ", v5." S ", v6." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uhadd(v7.M, v8_.M, v9.M), "uhadd v7." S ", v8." S ", v9." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Srhadd(v10.M, v11.M, v12.M), "srhadd v10." S ", v11." S ", v12." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Urhadd(v13.M, v14.M, v15.M), "urhadd v13." S ", v14." S ", v15." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Shsub(v16.M, v17.M, v18.M), "shsub v16." S ", v17." S ", v18." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uhsub(v19.M, v20.M, v21.M), "uhsub v19." S ", v20." S ", v21." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Addp(v19.M, v20.M, v21.M), "addp v19." S ", v20." S ", v21." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Mla(v19.M, v20.M, v21.M), "mla v19." S ", v20." S ", v21." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Mls(v19.M, v20.M, v21.M), "mls v19." S ", v20." S ", v21." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Mul(v19.M, v20.M, v21.M), "mul v19." S ", v20." S ", v21." S);
  NEON_FORMAT_LIST_BHS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqdmulh(v1.M, v2.M, v3.M), "sqdmulh v1." S ", v2." S ", v3." S);
  NEON_FORMAT_LIST_HS(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Sqrdmulh(v1.M, v2.M, v3.M), "sqrdmulh v1." S ", v2." S ", v3." S);
  NEON_FORMAT_LIST_HS(DISASM_INST)
#undef DISASM_INST

  COMPARE(And(v6.V8B(), v7.V8B(), v8_.V8B()), "and v6.8b, v7.8b, v8.8b");
  COMPARE(And(v6.V16B(), v7.V16B(), v8_.V16B()), "and v6.16b, v7.16b, v8.16b");

  COMPARE(Bic(v6.V8B(), v7.V8B(), v8_.V8B()), "bic v6.8b, v7.8b, v8.8b");
  COMPARE(Bic(v6.V16B(), v7.V16B(), v8_.V16B()), "bic v6.16b, v7.16b, v8.16b");

  COMPARE(Orr(v6.V8B(), v7.V8B(), v8_.V8B()), "orr v6.8b, v7.8b, v8.8b");
  COMPARE(Orr(v6.V16B(), v7.V16B(), v8_.V16B()), "orr v6.16b, v7.16b, v8.16b");

  COMPARE(Orr(v6.V8B(), v7.V8B(), v7.V8B()), "mov v6.8b, v7.8b");
  COMPARE(Orr(v6.V16B(), v7.V16B(), v7.V16B()), "mov v6.16b, v7.16b");

  COMPARE(Mov(v6.V8B(), v8_.V8B()), "mov v6.8b, v8.8b");
  COMPARE(Mov(v6.V16B(), v8_.V16B()), "mov v6.16b, v8.16b");

  COMPARE(Orn(v6.V8B(), v7.V8B(), v8_.V8B()), "orn v6.8b, v7.8b, v8.8b");
  COMPARE(Orn(v6.V16B(), v7.V16B(), v8_.V16B()), "orn v6.16b, v7.16b, v8.16b");

  COMPARE(Eor(v6.V8B(), v7.V8B(), v8_.V8B()), "eor v6.8b, v7.8b, v8.8b");
  COMPARE(Eor(v6.V16B(), v7.V16B(), v8_.V16B()), "eor v6.16b, v7.16b, v8.16b");

  COMPARE(Bif(v6.V8B(), v7.V8B(), v8_.V8B()), "bif v6.8b, v7.8b, v8.8b");
  COMPARE(Bif(v6.V16B(), v7.V16B(), v8_.V16B()), "bif v6.16b, v7.16b, v8.16b");

  COMPARE(Bit(v6.V8B(), v7.V8B(), v8_.V8B()), "bit v6.8b, v7.8b, v8.8b");
  COMPARE(Bit(v6.V16B(), v7.V16B(), v8_.V16B()), "bit v6.16b, v7.16b, v8.16b");

  COMPARE(Bsl(v6.V8B(), v7.V8B(), v8_.V8B()), "bsl v6.8b, v7.8b, v8.8b");
  COMPARE(Bsl(v6.V16B(), v7.V16B(), v8_.V16B()), "bsl v6.16b, v7.16b, v8.16b");

  COMPARE(Pmul(v6.V8B(), v7.V8B(), v8_.V8B()), "pmul v6.8b, v7.8b, v8.8b");
  COMPARE(Pmul(v6.V16B(), v7.V16B(), v8_.V16B()),
          "pmul v6.16b, v7.16b, v8.16b");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_fp_3same) {
  SET_UP_MASM();

#define DISASM_INST(M, S) \
  COMPARE(Fadd(v0.M, v1.M, v2.M), "fadd v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fsub(v3.M, v4.M, v5.M), "fsub v3." S ", v4." S ", v5." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmul(v6.M, v7.M, v8_.M), "fmul v6." S ", v7." S ", v8." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fdiv(v9.M, v10.M, v11.M), "fdiv v9." S ", v10." S ", v11." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmin(v12.M, v13.M, v14.M), "fmin v12." S ", v13." S ", v14." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fminnm(v15.M, v16.M, v17.M), "fminnm v15." S ", v16." S ", v17." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmax(v18.M, v19.M, v20.M), "fmax v18." S ", v19." S ", v20." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmaxnm(v21.M, v22.M, v23.M), "fmaxnm v21." S ", v22." S ", v23." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Frecps(v24.M, v25.M, v26.M), "frecps v24." S ", v25." S ", v26." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S)                                           \
  COMPARE(Frsqrts(v27.M, v28.M, v29.M), "frsqrts v27." S ", v28." S \
                                        ", "                        \
                                        "v29." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmulx(v30.M, v31.M, v0.M), "fmulx v30." S ", v31." S ", v0." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmla(v1.M, v2.M, v3.M), "fmla v1." S ", v2." S ", v3." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmls(v4.M, v5.M, v6.M), "fmls v4." S ", v5." S ", v6." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fabd(v7.M, v8_.M, v9.M), "fabd v7." S ", v8." S ", v9." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Faddp(v10.M, v11.M, v12.M), "faddp v10." S ", v11." S ", v12." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fmaxp(v13.M, v14.M, v15.M), "fmaxp v13." S ", v14." S ", v15." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fminp(v16.M, v17.M, v18.M), "fminp v16." S ", v17." S ", v18." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S)                                           \
  COMPARE(Fmaxnmp(v19.M, v20.M, v21.M), "fmaxnmp v19." S ", v20." S \
                                        ", "                        \
                                        "v21." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S)                                           \
  COMPARE(Fminnmp(v22.M, v23.M, v24.M), "fminnmp v22." S ", v23." S \
                                        ", "                        \
                                        "v24." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fcmeq(v25.M, v26.M, v27.M), "fcmeq v25." S ", v26." S ", v27." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fcmge(v25.M, v26.M, v27.M), "fcmge v25." S ", v26." S ", v27." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Fcmgt(v25.M, v26.M, v27.M), "fcmgt v25." S ", v26." S ", v27." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Facge(v25.M, v26.M, v27.M), "facge v25." S ", v26." S ", v27." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Facgt(v25.M, v26.M, v27.M), "facgt v25." S ", v26." S ", v27." S);
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  CLEANUP();
}

#define NEON_SCALAR_FORMAT_LIST(V) \
  V(B(), "b")                      \
  V(H(), "h")                      \
  V(S(), "s")                      \
  V(D(), "d")

TEST_F(DisasmArm64Test, neon_scalar_3same) {
  SET_UP_MASM();

  // Instructions that only support D-sized scalar operations.
  COMPARE(Add(v0.D(), v1.D(), v2.D()), "add d0, d1, d2");
  COMPARE(Sub(v3.D(), v4.D(), v5.D()), "sub d3, d4, d5");
  COMPARE(Cmeq(v0.D(), v1.D(), v2.D()), "cmeq d0, d1, d2");
  COMPARE(Cmge(v3.D(), v4.D(), v5.D()), "cmge d3, d4, d5");
  COMPARE(Cmgt(v6.D(), v7.D(), v8_.D()), "cmgt d6, d7, d8");
  COMPARE(Cmhi(v0.D(), v1.D(), v2.D()), "cmhi d0, d1, d2");
  COMPARE(Cmhs(v3.D(), v4.D(), v5.D()), "cmhs d3, d4, d5");
  COMPARE(Cmtst(v6.D(), v7.D(), v8_.D()), "cmtst d6, d7, d8");
  COMPARE(Ushl(v6.D(), v7.D(), v8_.D()), "ushl d6, d7, d8");
  COMPARE(Sshl(v6.D(), v7.D(), v8_.D()), "sshl d6, d7, d8");
  COMPARE(Urshl(v9.D(), v10.D(), v11.D()), "urshl d9, d10, d11");
  COMPARE(Srshl(v9.D(), v10.D(), v11.D()), "srshl d9, d10, d11");

  // Instructions that support S and D-sized scalar operations.
  COMPARE(Frecps(v12.S(), v13.S(), v14.S()), "frecps s12, s13, s14");
  COMPARE(Frecps(v15.D(), v16.D(), v17.D()), "frecps d15, d16, d17");
  COMPARE(Frsqrts(v18.S(), v19.S(), v20.S()), "frsqrts s18, s19, s20");
  COMPARE(Frsqrts(v21.D(), v22.D(), v23.D()), "frsqrts d21, d22, d23");
  COMPARE(Fmulx(v12.S(), v13.S(), v14.S()), "fmulx s12, s13, s14");
  COMPARE(Fmulx(v15.D(), v16.D(), v17.D()), "fmulx d15, d16, d17");
  COMPARE(Fcmeq(v12.S(), v13.S(), v14.S()), "fcmeq s12, s13, s14");
  COMPARE(Fcmeq(v15.D(), v16.D(), v17.D()), "fcmeq d15, d16, d17");
  COMPARE(Fcmge(v12.S(), v13.S(), v14.S()), "fcmge s12, s13, s14");
  COMPARE(Fcmge(v15.D(), v16.D(), v17.D()), "fcmge d15, d16, d17");
  COMPARE(Fcmgt(v12.S(), v13.S(), v14.S()), "fcmgt s12, s13, s14");
  COMPARE(Fcmgt(v15.D(), v16.D(), v17.D()), "fcmgt d15, d16, d17");
  COMPARE(Fcmge(v12.S(), v13.S(), v14.S()), "fcmge s12, s13, s14");
  COMPARE(Fcmge(v15.D(), v16.D(), v17.D()), "fcmge d15, d16, d17");
  COMPARE(Facgt(v12.S(), v13.S(), v14.S()), "facgt s12, s13, s14");
  COMPARE(Facgt(v15.D(), v16.D(), v17.D()), "facgt d15, d16, d17");

  // Instructions that support H and S-sized scalar operations.
  COMPARE(Sqdmulh(v12.S(), v13.S(), v14.S()), "sqdmulh s12, s13, s14");
  COMPARE(Sqdmulh(v15.H(), v16.H(), v17.H()), "sqdmulh h15, h16, h17");
  COMPARE(Sqrdmulh(v12.S(), v13.S(), v14.S()), "sqrdmulh s12, s13, s14");
  COMPARE(Sqrdmulh(v15.H(), v16.H(), v17.H()), "sqrdmulh h15, h16, h17");

#define DISASM_INST(M, R) \
  COMPARE(Uqadd(v6.M, v7.M, v8_.M), "uqadd " R "6, " R "7, " R "8");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Uqsub(v9.M, v10.M, v11.M), "uqsub " R "9, " R "10, " R "11");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Sqadd(v12.M, v13.M, v14.M), "sqadd " R "12, " R "13, " R "14");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Sqsub(v15.M, v16.M, v17.M), "sqsub " R "15, " R "16, " R "17");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Uqshl(v18.M, v19.M, v20.M), "uqshl " R "18, " R "19, " R "20");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Sqshl(v21.M, v22.M, v23.M), "sqshl " R "21, " R "22, " R "23");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Uqrshl(v30.M, v31.M, v0.M), "uqrshl " R "30, " R "31, " R "0");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, R) \
  COMPARE(Sqrshl(v1.M, v2.M, v3.M), "sqrshl " R "1, " R "2, " R "3");
  NEON_SCALAR_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_byelement) {
  SET_UP_MASM();

  COMPARE(Mul(v0.V4H(), v1.V4H(), v2.H(), 0), "mul v0.4h, v1.4h, v2.h[0]");
  COMPARE(Mul(v2.V8H(), v3.V8H(), v15.H(), 7), "mul v2.8h, v3.8h, v15.h[7]");
  COMPARE(Mul(v0.V2S(), v1.V2S(), v2.S(), 0), "mul v0.2s, v1.2s, v2.s[0]");
  COMPARE(Mul(v2.V4S(), v3.V4S(), v15.S(), 3), "mul v2.4s, v3.4s, v15.s[3]");

  COMPARE(Mla(v0.V4H(), v1.V4H(), v2.H(), 0), "mla v0.4h, v1.4h, v2.h[0]");
  COMPARE(Mla(v2.V8H(), v3.V8H(), v15.H(), 7), "mla v2.8h, v3.8h, v15.h[7]");
  COMPARE(Mla(v0.V2S(), v1.V2S(), v2.S(), 0), "mla v0.2s, v1.2s, v2.s[0]");
  COMPARE(Mla(v2.V4S(), v3.V4S(), v15.S(), 3), "mla v2.4s, v3.4s, v15.s[3]");

  COMPARE(Mls(v0.V4H(), v1.V4H(), v2.H(), 0), "mls v0.4h, v1.4h, v2.h[0]");
  COMPARE(Mls(v2.V8H(), v3.V8H(), v15.H(), 7), "mls v2.8h, v3.8h, v15.h[7]");
  COMPARE(Mls(v0.V2S(), v1.V2S(), v2.S(), 0), "mls v0.2s, v1.2s, v2.s[0]");
  COMPARE(Mls(v2.V4S(), v3.V4S(), v15.S(), 3), "mls v2.4s, v3.4s, v15.s[3]");

  COMPARE(Sqdmulh(v0.V4H(), v1.V4H(), v2.H(), 0),
          "sqdmulh v0.4h, v1.4h, v2.h[0]");
  COMPARE(Sqdmulh(v2.V8H(), v3.V8H(), v15.H(), 7),
          "sqdmulh v2.8h, v3.8h, v15.h[7]");
  COMPARE(Sqdmulh(v0.V2S(), v1.V2S(), v2.S(), 0),
          "sqdmulh v0.2s, v1.2s, v2.s[0]");
  COMPARE(Sqdmulh(v2.V4S(), v3.V4S(), v15.S(), 3),
          "sqdmulh v2.4s, v3.4s, v15.s[3]");
  COMPARE(Sqdmulh(h0, h1, v2.H(), 0), "sqdmulh h0, h1, v2.h[0]");
  COMPARE(Sqdmulh(s0, s1, v2.S(), 0), "sqdmulh s0, s1, v2.s[0]");

  COMPARE(Sqrdmulh(v0.V4H(), v1.V4H(), v2.H(), 0),
          "sqrdmulh v0.4h, v1.4h, v2.h[0]");
  COMPARE(Sqrdmulh(v2.V8H(), v3.V8H(), v15.H(), 7),
          "sqrdmulh v2.8h, v3.8h, v15.h[7]");
  COMPARE(Sqrdmulh(v0.V2S(), v1.V2S(), v2.S(), 0),
          "sqrdmulh v0.2s, v1.2s, v2.s[0]");
  COMPARE(Sqrdmulh(v2.V4S(), v3.V4S(), v15.S(), 3),
          "sqrdmulh v2.4s, v3.4s, v15.s[3]");
  COMPARE(Sqrdmulh(h0, h1, v2.H(), 0), "sqrdmulh h0, h1, v2.h[0]");
  COMPARE(Sqrdmulh(s0, s1, v2.S(), 0), "sqrdmulh s0, s1, v2.s[0]");

  COMPARE(Smull(v0.V4S(), v1.V4H(), v2.H(), 0), "smull v0.4s, v1.4h, v2.h[0]");
  COMPARE(Smull2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "smull2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Smull(v0.V2D(), v1.V2S(), v2.S(), 0), "smull v0.2d, v1.2s, v2.s[0]");
  COMPARE(Smull2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "smull2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Umull(v0.V4S(), v1.V4H(), v2.H(), 0), "umull v0.4s, v1.4h, v2.h[0]");
  COMPARE(Umull2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "umull2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Umull(v0.V2D(), v1.V2S(), v2.S(), 0), "umull v0.2d, v1.2s, v2.s[0]");
  COMPARE(Umull2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "umull2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Smlal(v0.V4S(), v1.V4H(), v2.H(), 0), "smlal v0.4s, v1.4h, v2.h[0]");
  COMPARE(Smlal2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "smlal2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Smlal(v0.V2D(), v1.V2S(), v2.S(), 0), "smlal v0.2d, v1.2s, v2.s[0]");
  COMPARE(Smlal2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "smlal2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Umlal(v0.V4S(), v1.V4H(), v2.H(), 0), "umlal v0.4s, v1.4h, v2.h[0]");
  COMPARE(Umlal2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "umlal2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Umlal(v0.V2D(), v1.V2S(), v2.S(), 0), "umlal v0.2d, v1.2s, v2.s[0]");
  COMPARE(Umlal2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "umlal2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Smlsl(v0.V4S(), v1.V4H(), v2.H(), 0), "smlsl v0.4s, v1.4h, v2.h[0]");
  COMPARE(Smlsl2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "smlsl2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Smlsl(v0.V2D(), v1.V2S(), v2.S(), 0), "smlsl v0.2d, v1.2s, v2.s[0]");
  COMPARE(Smlsl2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "smlsl2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Umlsl(v0.V4S(), v1.V4H(), v2.H(), 0), "umlsl v0.4s, v1.4h, v2.h[0]");
  COMPARE(Umlsl2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "umlsl2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Umlsl(v0.V2D(), v1.V2S(), v2.S(), 0), "umlsl v0.2d, v1.2s, v2.s[0]");
  COMPARE(Umlsl2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "umlsl2 v2.2d, v3.4s, v4.s[3]");

  COMPARE(Sqdmull(v0.V4S(), v1.V4H(), v2.H(), 0),
          "sqdmull v0.4s, v1.4h, v2.h[0]");
  COMPARE(Sqdmull2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "sqdmull2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Sqdmull(v0.V2D(), v1.V2S(), v2.S(), 0),
          "sqdmull v0.2d, v1.2s, v2.s[0]");
  COMPARE(Sqdmull2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "sqdmull2 v2.2d, v3.4s, v4.s[3]");
  COMPARE(Sqdmull(s0, h1, v2.H(), 0), "sqdmull s0, h1, v2.h[0]");
  COMPARE(Sqdmull(d0, s1, v2.S(), 0), "sqdmull d0, s1, v2.s[0]");

  COMPARE(Sqdmlal(v0.V4S(), v1.V4H(), v2.H(), 0),
          "sqdmlal v0.4s, v1.4h, v2.h[0]");
  COMPARE(Sqdmlal2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "sqdmlal2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Sqdmlal(v0.V2D(), v1.V2S(), v2.S(), 0),
          "sqdmlal v0.2d, v1.2s, v2.s[0]");
  COMPARE(Sqdmlal2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "sqdmlal2 v2.2d, v3.4s, v4.s[3]");
  COMPARE(Sqdmlal(s0, h1, v2.H(), 0), "sqdmlal s0, h1, v2.h[0]");
  COMPARE(Sqdmlal(d0, s1, v2.S(), 0), "sqdmlal d0, s1, v2.s[0]");

  COMPARE(Sqdmlsl(v0.V4S(), v1.V4H(), v2.H(), 0),
          "sqdmlsl v0.4s, v1.4h, v2.h[0]");
  COMPARE(Sqdmlsl2(v2.V4S(), v3.V8H(), v4.H(), 7),
          "sqdmlsl2 v2.4s, v3.8h, v4.h[7]");
  COMPARE(Sqdmlsl(v0.V2D(), v1.V2S(), v2.S(), 0),
          "sqdmlsl v0.2d, v1.2s, v2.s[0]");
  COMPARE(Sqdmlsl2(v2.V2D(), v3.V4S(), v4.S(), 3),
          "sqdmlsl2 v2.2d, v3.4s, v4.s[3]");
  COMPARE(Sqdmlsl(s0, h1, v2.H(), 0), "sqdmlsl s0, h1, v2.h[0]");
  COMPARE(Sqdmlsl(d0, s1, v2.S(), 0), "sqdmlsl d0, s1, v2.s[0]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_fp_byelement) {
  SET_UP_MASM();

  COMPARE(Fmul(v0.V2S(), v1.V2S(), v2.S(), 0), "fmul v0.2s, v1.2s, v2.s[0]");
  COMPARE(Fmul(v2.V4S(), v3.V4S(), v15.S(), 3), "fmul v2.4s, v3.4s, v15.s[3]");
  COMPARE(Fmul(v0.V2D(), v1.V2D(), v2.D(), 0), "fmul v0.2d, v1.2d, v2.d[0]");
  COMPARE(Fmul(d0, d1, v2.D(), 0), "fmul d0, d1, v2.d[0]");
  COMPARE(Fmul(s0, s1, v2.S(), 0), "fmul s0, s1, v2.s[0]");

  COMPARE(Fmla(v0.V2S(), v1.V2S(), v2.S(), 0), "fmla v0.2s, v1.2s, v2.s[0]");
  COMPARE(Fmla(v2.V4S(), v3.V4S(), v15.S(), 3), "fmla v2.4s, v3.4s, v15.s[3]");
  COMPARE(Fmla(v0.V2D(), v1.V2D(), v2.D(), 0), "fmla v0.2d, v1.2d, v2.d[0]");
  COMPARE(Fmla(d0, d1, v2.D(), 0), "fmla d0, d1, v2.d[0]");
  COMPARE(Fmla(s0, s1, v2.S(), 0), "fmla s0, s1, v2.s[0]");

  COMPARE(Fmls(v0.V2S(), v1.V2S(), v2.S(), 0), "fmls v0.2s, v1.2s, v2.s[0]");
  COMPARE(Fmls(v2.V4S(), v3.V4S(), v15.S(), 3), "fmls v2.4s, v3.4s, v15.s[3]");
  COMPARE(Fmls(v0.V2D(), v1.V2D(), v2.D(), 0), "fmls v0.2d, v1.2d, v2.d[0]");
  COMPARE(Fmls(d0, d1, v2.D(), 0), "fmls d0, d1, v2.d[0]");
  COMPARE(Fmls(s0, s1, v2.S(), 0), "fmls s0, s1, v2.s[0]");

  COMPARE(Fmulx(v0.V2S(), v1.V2S(), v2.S(), 0), "fmulx v0.2s, v1.2s, v2.s[0]");
  COMPARE(Fmulx(v2.V4S(), v3.V4S(), v8_.S(), 3), "fmulx v2.4s, v3.4s, v8.s[3]");
  COMPARE(Fmulx(v0.V2D(), v1.V2D(), v2.D(), 0), "fmulx v0.2d, v1.2d, v2.d[0]");
  COMPARE(Fmulx(d0, d1, v2.D(), 0), "fmulx d0, d1, v2.d[0]");
  COMPARE(Fmulx(s0, s1, v2.S(), 0), "fmulx s0, s1, v2.s[0]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_3different) {
  SET_UP_MASM();

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Uaddl(v0.TA, v1.TB, v2.TB), "uaddl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Uaddl2(v0.TA, v1.TB, v2.TB), \
          "uaddl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Uaddw(v0.TA, v1.TA, v2.TB), "uaddw v0." TAS ", v1." TAS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Uaddw2(v0.TA, v1.TA, v2.TB), \
          "uaddw2 v0." TAS ", v1." TAS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Saddl(v0.TA, v1.TB, v2.TB), "saddl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Saddl2(v0.TA, v1.TB, v2.TB), \
          "saddl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Saddw(v0.TA, v1.TA, v2.TB), "saddw v0." TAS ", v1." TAS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Saddw2(v0.TA, v1.TA, v2.TB), \
          "saddw2 v0." TAS ", v1." TAS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Usubl(v0.TA, v1.TB, v2.TB), "usubl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Usubl2(v0.TA, v1.TB, v2.TB), \
          "usubl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Usubw(v0.TA, v1.TA, v2.TB), "usubw v0." TAS ", v1." TAS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Usubw2(v0.TA, v1.TA, v2.TB), \
          "usubw2 v0." TAS ", v1." TAS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Ssubl(v0.TA, v1.TB, v2.TB), "ssubl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Ssubl2(v0.TA, v1.TB, v2.TB), \
          "ssubl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Ssubw(v0.TA, v1.TA, v2.TB), "ssubw v0." TAS ", v1." TAS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Ssubw2(v0.TA, v1.TA, v2.TB), \
          "ssubw2 v0." TAS ", v1." TAS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Sabal(v0.TA, v1.TB, v2.TB), "sabal v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Sabal2(v0.TA, v1.TB, v2.TB), \
          "sabal2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Uabal(v0.TA, v1.TB, v2.TB), "uabal v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Uabal2(v0.TA, v1.TB, v2.TB), \
          "uabal2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Sabdl(v0.TA, v1.TB, v2.TB), "sabdl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Sabdl2(v0.TA, v1.TB, v2.TB), \
          "sabdl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Uabdl(v0.TA, v1.TB, v2.TB), "uabdl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Uabdl2(v0.TA, v1.TB, v2.TB), \
          "uabdl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Smlal(v0.TA, v1.TB, v2.TB), "smlal v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Smlal2(v0.TA, v1.TB, v2.TB), \
          "smlal2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Umlsl(v0.TA, v1.TB, v2.TB), "umlsl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Umlsl2(v0.TA, v1.TB, v2.TB), \
          "umlsl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Smlsl(v0.TA, v1.TB, v2.TB), "smlsl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Smlsl2(v0.TA, v1.TB, v2.TB), \
          "smlsl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Umlsl(v0.TA, v1.TB, v2.TB), "umlsl v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Umlsl2(v0.TA, v1.TB, v2.TB), \
          "umlsl2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Smull(v0.TA, v1.TB, v2.TB), "smull v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Smull2(v0.TA, v1.TB, v2.TB), \
          "smull2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)                             \
  COMPARE(Umull(v0.TA, v1.TB, v2.TB), "umull v0." TAS ", v1." TBS \
                                      ", "                        \
                                      "v2." TBS);
  NEON_FORMAT_LIST_LW(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS)  \
  COMPARE(Umull2(v0.TA, v1.TB, v2.TB), \
          "umull2 v0." TAS ", v1." TBS ", v2." TBS);
  NEON_FORMAT_LIST_LW2(DISASM_INST)
#undef DISASM_INST

  COMPARE(Sqdmull(v0.V4S(), v1.V4H(), v2.V4H()), "sqdmull v0.4s, v1.4h, v2.4h");
  COMPARE(Sqdmull(v1.V2D(), v2.V2S(), v3.V2S()), "sqdmull v1.2d, v2.2s, v3.2s");
  COMPARE(Sqdmull2(v2.V4S(), v3.V8H(), v4.V8H()),
          "sqdmull2 v2.4s, v3.8h, v4.8h");
  COMPARE(Sqdmull2(v3.V2D(), v4.V4S(), v5.V4S()),
          "sqdmull2 v3.2d, v4.4s, v5.4s");
  COMPARE(Sqdmull(s0, h1, h2), "sqdmull s0, h1, h2");
  COMPARE(Sqdmull(d1, s2, s3), "sqdmull d1, s2, s3");

  COMPARE(Sqdmlal(v0.V4S(), v1.V4H(), v2.V4H()), "sqdmlal v0.4s, v1.4h, v2.4h");
  COMPARE(Sqdmlal(v1.V2D(), v2.V2S(), v3.V2S()), "sqdmlal v1.2d, v2.2s, v3.2s");
  COMPARE(Sqdmlal2(v2.V4S(), v3.V8H(), v4.V8H()),
          "sqdmlal2 v2.4s, v3.8h, v4.8h");
  COMPARE(Sqdmlal2(v3.V2D(), v4.V4S(), v5.V4S()),
          "sqdmlal2 v3.2d, v4.4s, v5.4s");
  COMPARE(Sqdmlal(s0, h1, h2), "sqdmlal s0, h1, h2");
  COMPARE(Sqdmlal(d1, s2, s3), "sqdmlal d1, s2, s3");

  COMPARE(Sqdmlsl(v0.V4S(), v1.V4H(), v2.V4H()), "sqdmlsl v0.4s, v1.4h, v2.4h");
  COMPARE(Sqdmlsl(v1.V2D(), v2.V2S(), v3.V2S()), "sqdmlsl v1.2d, v2.2s, v3.2s");
  COMPARE(Sqdmlsl2(v2.V4S(), v3.V8H(), v4.V8H()),
          "sqdmlsl2 v2.4s, v3.8h, v4.8h");
  COMPARE(Sqdmlsl2(v3.V2D(), v4.V4S(), v5.V4S()),
          "sqdmlsl2 v3.2d, v4.4s, v5.4s");
  COMPARE(Sqdmlsl(s0, h1, h2), "sqdmlsl s0, h1, h2");
  COMPARE(Sqdmlsl(d1, s2, s3), "sqdmlsl d1, s2, s3");

  COMPARE(Addhn(v0.V8B(), v1.V8H(), v2.V8H()), "addhn v0.8b, v1.8h, v2.8h");
  COMPARE(Addhn(v1.V4H(), v2.V4S(), v3.V4S()), "addhn v1.4h, v2.4s, v3.4s");
  COMPARE(Addhn(v2.V2S(), v3.V2D(), v4.V2D()), "addhn v2.2s, v3.2d, v4.2d");
  COMPARE(Addhn2(v0.V16B(), v1.V8H(), v5.V8H()), "addhn2 v0.16b, v1.8h, v5.8h");
  COMPARE(Addhn2(v1.V8H(), v2.V4S(), v6.V4S()), "addhn2 v1.8h, v2.4s, v6.4s");
  COMPARE(Addhn2(v2.V4S(), v3.V2D(), v7.V2D()), "addhn2 v2.4s, v3.2d, v7.2d");

  COMPARE(Raddhn(v0.V8B(), v1.V8H(), v2.V8H()), "raddhn v0.8b, v1.8h, v2.8h");
  COMPARE(Raddhn(v1.V4H(), v2.V4S(), v3.V4S()), "raddhn v1.4h, v2.4s, v3.4s");
  COMPARE(Raddhn(v2.V2S(), v3.V2D(), v4.V2D()), "raddhn v2.2s, v3.2d, v4.2d");
  COMPARE(Raddhn2(v0.V16B(), v1.V8H(), v5.V8H()),
          "raddhn2 v0.16b, v1.8h, v5.8h");
  COMPARE(Raddhn2(v1.V8H(), v2.V4S(), v6.V4S()), "raddhn2 v1.8h, v2.4s, v6.4s");
  COMPARE(Raddhn2(v2.V4S(), v3.V2D(), v7.V2D()), "raddhn2 v2.4s, v3.2d, v7.2d");

  COMPARE(Subhn(v1.V4H(), v2.V4S(), v3.V4S()), "subhn v1.4h, v2.4s, v3.4s");
  COMPARE(Subhn(v2.V2S(), v3.V2D(), v4.V2D()), "subhn v2.2s, v3.2d, v4.2d");
  COMPARE(Subhn2(v0.V16B(), v1.V8H(), v5.V8H()), "subhn2 v0.16b, v1.8h, v5.8h");
  COMPARE(Subhn2(v1.V8H(), v2.V4S(), v6.V4S()), "subhn2 v1.8h, v2.4s, v6.4s");
  COMPARE(Subhn2(v2.V4S(), v3.V2D(), v7.V2D()), "subhn2 v2.4s, v3.2d, v7.2d");

  COMPARE(Rsubhn(v0.V8B(), v1.V8H(), v2.V8H()), "rsubhn v0.8b, v1.8h, v2.8h");
  COMPARE(Rsubhn(v1.V4H(), v2.V4S(), v3.V4S()), "rsubhn v1.4h, v2.4s, v3.4s");
  COMPARE(Rsubhn(v2.V2S(), v3.V2D(), v4.V2D()), "rsubhn v2.2s, v3.2d, v4.2d");
  COMPARE(Rsubhn2(v0.V16B(), v1.V8H(), v5.V8H()),
          "rsubhn2 v0.16b, v1.8h, v5.8h");
  COMPARE(Rsubhn2(v1.V8H(), v2.V4S(), v6.V4S()), "rsubhn2 v1.8h, v2.4s, v6.4s");
  COMPARE(Rsubhn2(v2.V4S(), v3.V2D(), v7.V2D()), "rsubhn2 v2.4s, v3.2d, v7.2d");

  COMPARE(Pmull(v0.V8H(), v1.V8B(), v2.V8B()), "pmull v0.8h, v1.8b, v2.8b");
  COMPARE(Pmull2(v2.V8H(), v3.V16B(), v4.V16B()),
          "pmull2 v2.8h, v3.16b, v4.16b");

  {
    CpuFeatureScope feature_scope(assm, PMULL1Q,
                                  CpuFeatureScope::kDontCheckSupported);

    COMPARE(Pmull(v5.V1Q(), v6.V1D(), v7.V1D()), "pmull v5.1q, v6.1d, v7.1d");
    COMPARE(Pmull2(v8.V1Q(), v9.V2D(), v10.V2D()),
            "pmull2 v8.1q, v9.2d, v10.2d");
  }

  {
    CpuFeatureScope feature_scope(assm, DOTPROD,
                                  CpuFeatureScope::kDontCheckSupported);

    COMPARE(Sdot(v11.V2S(), v20.V8B(), v25.V8B()),
            "sdot v11.2s, v20.8b, v25.8b");
    COMPARE(Sdot(v26.V4S(), v5.V16B(), v14.V16B()),
            "sdot v26.4s, v5.16b, v14.16b");
  }

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_perm) {
  SET_UP_MASM();

#define DISASM_INST(M, S) \
  COMPARE(Trn1(v0.M, v1.M, v2.M), "trn1 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Trn2(v0.M, v1.M, v2.M), "trn2 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uzp1(v0.M, v1.M, v2.M), "uzp1 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Uzp2(v0.M, v1.M, v2.M), "uzp2 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Zip1(v0.M, v1.M, v2.M), "zip1 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Zip2(v0.M, v1.M, v2.M), "zip2 v0." S ", v1." S ", v2." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_copy) {
  SET_UP_MASM();

  COMPARE(Ins(v1.V16B(), 4, v5.V16B(), 0), "mov v1.b[4], v5.b[0]");
  COMPARE(Ins(v2.V8B(), 5, v6.V8B(), 1), "mov v2.b[5], v6.b[1]");
  COMPARE(Ins(v3.B(), 6, v7.B(), 2), "mov v3.b[6], v7.b[2]");
  COMPARE(Ins(v4.V8H(), 7, v8_.V8H(), 3), "mov v4.h[7], v8.h[3]");
  COMPARE(Ins(v5.V4H(), 3, v9.V4H(), 0), "mov v5.h[3], v9.h[0]");
  COMPARE(Ins(v6.H(), 6, v1.H(), 1), "mov v6.h[6], v1.h[1]");
  COMPARE(Ins(v7.V4S(), 2, v2.V4S(), 2), "mov v7.s[2], v2.s[2]");
  COMPARE(Ins(v8_.V2S(), 1, v3.V2S(), 0), "mov v8.s[1], v3.s[0]");
  COMPARE(Ins(v9.S(), 0, v4.S(), 1), "mov v9.s[0], v4.s[1]");
  COMPARE(Ins(v1.V2D(), 1, v5.V2D(), 0), "mov v1.d[1], v5.d[0]");
  COMPARE(Ins(v2.D(), 0, v6.D(), 1), "mov v2.d[0], v6.d[1]");

  COMPARE(Mov(v3.V16B(), 4, v7.V16B(), 0), "mov v3.b[4], v7.b[0]");
  COMPARE(Mov(v4.V8B(), 5, v8_.V8B(), 1), "mov v4.b[5], v8.b[1]");
  COMPARE(Mov(v5.B(), 6, v9.B(), 2), "mov v5.b[6], v9.b[2]");
  COMPARE(Mov(v6.V8H(), 7, v1.V8H(), 3), "mov v6.h[7], v1.h[3]");
  COMPARE(Mov(v7.V4H(), 0, v2.V4H(), 0), "mov v7.h[0], v2.h[0]");
  COMPARE(Mov(v8_.H(), 1, v3.H(), 1), "mov v8.h[1], v3.h[1]");
  COMPARE(Mov(v9.V4S(), 2, v4.V4S(), 2), "mov v9.s[2], v4.s[2]");
  COMPARE(Mov(v1.V2S(), 3, v5.V2S(), 0), "mov v1.s[3], v5.s[0]");
  COMPARE(Mov(v2.S(), 0, v6.S(), 1), "mov v2.s[0], v6.s[1]");
  COMPARE(Mov(v3.V2D(), 1, v7.V2D(), 0), "mov v3.d[1], v7.d[0]");
  COMPARE(Mov(v4.D(), 0, v8_.D(), 1), "mov v4.d[0], v8.d[1]");

  COMPARE(Ins(v1.V16B(), 4, w0), "mov v1.b[4], w0");
  COMPARE(Ins(v2.V8B(), 5, w1), "mov v2.b[5], w1");
  COMPARE(Ins(v3.B(), 6, w2), "mov v3.b[6], w2");
  COMPARE(Ins(v4.V8H(), 7, w3), "mov v4.h[7], w3");
  COMPARE(Ins(v5.V4H(), 3, w0), "mov v5.h[3], w0");
  COMPARE(Ins(v6.H(), 6, w1), "mov v6.h[6], w1");
  COMPARE(Ins(v7.V4S(), 2, w2), "mov v7.s[2], w2");
  COMPARE(Ins(v8_.V2S(), 1, w0), "mov v8.s[1], w0");
  COMPARE(Ins(v9.S(), 0, w1), "mov v9.s[0], w1");
  COMPARE(Ins(v1.V2D(), 1, x0), "mov v1.d[1], x0");
  COMPARE(Ins(v2.D(), 0, x1), "mov v2.d[0], x1");

  COMPARE(Mov(v1.V16B(), 4, w0), "mov v1.b[4], w0");
  COMPARE(Mov(v2.V8B(), 5, w1), "mov v2.b[5], w1");
  COMPARE(Mov(v3.B(), 6, w2), "mov v3.b[6], w2");
  COMPARE(Mov(v4.V8H(), 7, w3), "mov v4.h[7], w3");
  COMPARE(Mov(v5.V4H(), 3, w0), "mov v5.h[3], w0");
  COMPARE(Mov(v6.H(), 6, w1), "mov v6.h[6], w1");
  COMPARE(Mov(v7.V4S(), 2, w2), "mov v7.s[2], w2");
  COMPARE(Mov(v8_.V2S(), 1, w0), "mov v8.s[1], w0");
  COMPARE(Mov(v9.S(), 0, w1), "mov v9.s[0], w1");
  COMPARE(Mov(v1.V2D(), 1, x0), "mov v1.d[1], x0");
  COMPARE(Mov(v2.D(), 0, x1), "mov v2.d[0], x1");

  COMPARE(Dup(v5.V8B(), v9.V8B(), 6), "dup v5.8b, v9.b[6]");
  COMPARE(Dup(v6.V16B(), v1.V16B(), 5), "dup v6.16b, v1.b[5]");
  COMPARE(Dup(v7.V4H(), v2.V4H(), 4), "dup v7.4h, v2.h[4]");
  COMPARE(Dup(v8_.V8H(), v3.V8H(), 3), "dup v8.8h, v3.h[3]");
  COMPARE(Dup(v9.V2S(), v4.V2S(), 2), "dup v9.2s, v4.s[2]");
  COMPARE(Dup(v1.V4S(), v5.V4S(), 1), "dup v1.4s, v5.s[1]");
  COMPARE(Dup(v2.V2D(), v6.V2D(), 0), "dup v2.2d, v6.d[0]");

  COMPARE(Dup(v5.B(), v9.B(), 6), "mov b5, v9.b[6]");
  COMPARE(Dup(v7.H(), v2.H(), 4), "mov h7, v2.h[4]");
  COMPARE(Dup(v9.S(), v4.S(), 2), "mov s9, v4.s[2]");
  COMPARE(Dup(v2.D(), v6.D(), 0), "mov d2, v6.d[0]");

  COMPARE(Mov(v5.B(), v9.B(), 6), "mov b5, v9.b[6]");
  COMPARE(Mov(v7.H(), v2.H(), 4), "mov h7, v2.h[4]");
  COMPARE(Mov(v9.S(), v4.S(), 2), "mov s9, v4.s[2]");
  COMPARE(Mov(v2.D(), v6.D(), 0), "mov d2, v6.d[0]");

  COMPARE(Mov(v0.B(), v1.V8B(), 7), "mov b0, v1.b[7]");
  COMPARE(Mov(b2, v3.V16B(), 15), "mov b2, v3.b[15]");
  COMPARE(Mov(v4.H(), v5.V4H(), 3), "mov h4, v5.h[3]");
  COMPARE(Mov(h6, v7.V8H(), 7), "mov h6, v7.h[7]");
  COMPARE(Mov(v8_.S(), v9.V2S(), 1), "mov s8, v9.s[1]");
  COMPARE(Mov(s10, v11.V4S(), 3), "mov s10, v11.s[3]");
  COMPARE(Mov(v12.D(), v13.V2D(), 1), "mov d12, v13.d[1]");

  COMPARE(Dup(v5.V8B(), w0), "dup v5.8b, w0");
  COMPARE(Dup(v6.V16B(), w1), "dup v6.16b, w1");
  COMPARE(Dup(v7.V4H(), w2), "dup v7.4h, w2");
  COMPARE(Dup(v8_.V8H(), w3), "dup v8.8h, w3");
  COMPARE(Dup(v9.V2S(), w4), "dup v9.2s, w4");
  COMPARE(Dup(v1.V4S(), w5), "dup v1.4s, w5");
  COMPARE(Dup(v2.V2D(), x6), "dup v2.2d, x6");

  COMPARE(Smov(w0, v1.V16B(), 4), "smov w0, v1.b[4]");
  COMPARE(Smov(w1, v2.V8B(), 5), "smov w1, v2.b[5]");
  COMPARE(Smov(w2, v3.B(), 6), "smov w2, v3.b[6]");
  COMPARE(Smov(w3, v4.V8H(), 7), "smov w3, v4.h[7]");
  COMPARE(Smov(w0, v5.V4H(), 3), "smov w0, v5.h[3]");
  COMPARE(Smov(w1, v6.H(), 6), "smov w1, v6.h[6]");

  COMPARE(Smov(x0, v1.V16B(), 4), "smov x0, v1.b[4]");
  COMPARE(Smov(x1, v2.V8B(), 5), "smov x1, v2.b[5]");
  COMPARE(Smov(x2, v3.B(), 6), "smov x2, v3.b[6]");
  COMPARE(Smov(x3, v4.V8H(), 7), "smov x3, v4.h[7]");
  COMPARE(Smov(x0, v5.V4H(), 3), "smov x0, v5.h[3]");
  COMPARE(Smov(x1, v6.H(), 6), "smov x1, v6.h[6]");
  COMPARE(Smov(x2, v7.V4S(), 2), "smov x2, v7.s[2]");
  COMPARE(Smov(x0, v8_.V2S(), 1), "smov x0, v8.s[1]");
  COMPARE(Smov(x1, v9.S(), 0), "smov x1, v9.s[0]");

  COMPARE(Umov(w0, v1.V16B(), 4), "umov w0, v1.b[4]");
  COMPARE(Umov(w1, v2.V8B(), 5), "umov w1, v2.b[5]");
  COMPARE(Umov(w2, v3.B(), 6), "umov w2, v3.b[6]");
  COMPARE(Umov(w3, v4.V8H(), 7), "umov w3, v4.h[7]");
  COMPARE(Umov(w0, v5.V4H(), 3), "umov w0, v5.h[3]");
  COMPARE(Umov(w1, v6.H(), 6), "umov w1, v6.h[6]");
  COMPARE(Umov(w2, v7.V4S(), 2), "mov w2, v7.s[2]");
  COMPARE(Umov(w0, v8_.V2S(), 1), "mov w0, v8.s[1]");
  COMPARE(Umov(w1, v9.S(), 0), "mov w1, v9.s[0]");
  COMPARE(Umov(x0, v1.V2D(), 1), "mov x0, v1.d[1]");
  COMPARE(Umov(x1, v2.D(), 0), "mov x1, v2.d[0]");

  COMPARE(Mov(w2, v7.V4S(), 2), "mov w2, v7.s[2]");
  COMPARE(Mov(w0, v8_.V2S(), 1), "mov w0, v8.s[1]");
  COMPARE(Mov(w1, v9.S(), 0), "mov w1, v9.s[0]");
  COMPARE(Mov(x0, v1.V2D(), 1), "mov x0, v1.d[1]");
  COMPARE(Mov(x1, v2.D(), 0), "mov x1, v2.d[0]");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_extract) {
  SET_UP_MASM();

  COMPARE(Ext(v4.V8B(), v5.V8B(), v6.V8B(), 0), "ext v4.8b, v5.8b, v6.8b, #0");
  COMPARE(Ext(v1.V8B(), v2.V8B(), v3.V8B(), 7), "ext v1.8b, v2.8b, v3.8b, #7");
  COMPARE(Ext(v1.V16B(), v2.V16B(), v3.V16B(), 0),
          "ext v1.16b, v2.16b, v3.16b, #0");
  COMPARE(Ext(v1.V16B(), v2.V16B(), v3.V16B(), 15),
          "ext v1.16b, v2.16b, v3.16b, #15");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_table) {
  SET_UP_MASM();

  COMPARE(Tbl(v0.V8B(), v1.V16B(), v2.V8B()), "tbl v0.8b, {v1.16b}, v2.8b");
  COMPARE(Tbl(v3.V8B(), v4.V16B(), v5.V16B(), v6.V8B()),
          "tbl v3.8b, {v4.16b, v5.16b}, v6.8b");
  COMPARE(Tbl(v7.V8B(), v8_.V16B(), v9.V16B(), v10.V16B(), v11.V8B()),
          "tbl v7.8b, {v8.16b, v9.16b, v10.16b}, v11.8b");
  COMPARE(
      Tbl(v12.V8B(), v13.V16B(), v14.V16B(), v15.V16B(), v16.V16B(), v17.V8B()),
      "tbl v12.8b, {v13.16b, v14.16b, v15.16b, v16.16b}, v17.8b");
  COMPARE(Tbl(v18.V16B(), v19.V16B(), v20.V16B()),
          "tbl v18.16b, {v19.16b}, v20.16b");
  COMPARE(Tbl(v21.V16B(), v22.V16B(), v23.V16B(), v24.V16B()),
          "tbl v21.16b, {v22.16b, v23.16b}, v24.16b");
  COMPARE(Tbl(v25.V16B(), v26.V16B(), v27.V16B(), v28.V16B(), v29.V16B()),
          "tbl v25.16b, {v26.16b, v27.16b, v28.16b}, v29.16b");
  COMPARE(
      Tbl(v30.V16B(), v31.V16B(), v0.V16B(), v1.V16B(), v2.V16B(), v3.V16B()),
      "tbl v30.16b, {v31.16b, v0.16b, v1.16b, v2.16b}, v3.16b");

  COMPARE(Tbx(v0.V8B(), v1.V16B(), v2.V8B()), "tbx v0.8b, {v1.16b}, v2.8b");
  COMPARE(Tbx(v3.V8B(), v4.V16B(), v5.V16B(), v6.V8B()),
          "tbx v3.8b, {v4.16b, v5.16b}, v6.8b");
  COMPARE(Tbx(v7.V8B(), v8_.V16B(), v9.V16B(), v10.V16B(), v11.V8B()),
          "tbx v7.8b, {v8.16b, v9.16b, v10.16b}, v11.8b");
  COMPARE(
      Tbx(v12.V8B(), v13.V16B(), v14.V16B(), v15.V16B(), v16.V16B(), v17.V8B()),
      "tbx v12.8b, {v13.16b, v14.16b, v15.16b, v16.16b}, v17.8b");
  COMPARE(Tbx(v18.V16B(), v19.V16B(), v20.V16B()),
          "tbx v18.16b, {v19.16b}, v20.16b");
  COMPARE(Tbx(v21.V16B(), v22.V16B(), v23.V16B(), v24.V16B()),
          "tbx v21.16b, {v22.16b, v23.16b}, v24.16b");
  COMPARE(Tbx(v25.V16B(), v26.V16B(), v27.V16B(), v28.V16B(), v29.V16B()),
          "tbx v25.16b, {v26.16b, v27.16b, v28.16b}, v29.16b");
  COMPARE(
      Tbx(v30.V16B(), v31.V16B(), v0.V16B(), v1.V16B(), v2.V16B(), v3.V16B()),
      "tbx v30.16b, {v31.16b, v0.16b, v1.16b, v2.16b}, v3.16b");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_modimm) {
  SET_UP_MASM();

  COMPARE(Orr(v4.V4H(), 0xaa, 0), "orr v4.4h, #0xaa, lsl #0");
  COMPARE(Orr(v1.V8H(), 0xcc, 8), "orr v1.8h, #0xcc, lsl #8");
  COMPARE(Orr(v4.V2S(), 0xaa, 0), "orr v4.2s, #0xaa, lsl #0");
  COMPARE(Orr(v1.V2S(), 0xcc, 8), "orr v1.2s, #0xcc, lsl #8");
  COMPARE(Orr(v4.V4S(), 0xaa, 16), "orr v4.4s, #0xaa, lsl #16");
  COMPARE(Orr(v1.V4S(), 0xcc, 24), "orr v1.4s, #0xcc, lsl #24");

  COMPARE(Bic(v4.V4H(), 0xaa, 0), "bic v4.4h, #0xaa, lsl #0");
  COMPARE(Bic(v1.V8H(), 0xcc, 8), "bic v1.8h, #0xcc, lsl #8");
  COMPARE(Bic(v4.V2S(), 0xaa, 0), "bic v4.2s, #0xaa, lsl #0");
  COMPARE(Bic(v1.V2S(), 0xcc, 8), "bic v1.2s, #0xcc, lsl #8");
  COMPARE(Bic(v4.V4S(), 0xaa, 16), "bic v4.4s, #0xaa, lsl #16");
  COMPARE(Bic(v1.V4S(), 0xcc, 24), "bic v1.4s, #0xcc, lsl #24");

  COMPARE(Mvni(v4.V4H(), 0xaa, LSL, 0), "mvni v4.4h, #0xaa, lsl #0");
  COMPARE(Mvni(v1.V8H(), 0xcc, LSL, 8), "mvni v1.8h, #0xcc, lsl #8");
  COMPARE(Mvni(v4.V2S(), 0xaa, LSL, 0), "mvni v4.2s, #0xaa, lsl #0");
  COMPARE(Mvni(v1.V2S(), 0xcc, LSL, 8), "mvni v1.2s, #0xcc, lsl #8");
  COMPARE(Mvni(v4.V4S(), 0xaa, LSL, 16), "mvni v4.4s, #0xaa, lsl #16");
  COMPARE(Mvni(v1.V4S(), 0xcc, LSL, 24), "mvni v1.4s, #0xcc, lsl #24");

  COMPARE(Mvni(v4.V2S(), 0xaa, MSL, 8), "mvni v4.2s, #0xaa, msl #8");
  COMPARE(Mvni(v1.V2S(), 0xcc, MSL, 16), "mvni v1.2s, #0xcc, msl #16");
  COMPARE(Mvni(v4.V4S(), 0xaa, MSL, 8), "mvni v4.4s, #0xaa, msl #8");
  COMPARE(Mvni(v1.V4S(), 0xcc, MSL, 16), "mvni v1.4s, #0xcc, msl #16");

  COMPARE(Movi(v4.V8B(), 0xaa), "movi v4.8b, #0xaa");
  COMPARE(Movi(v1.V16B(), 0xcc), "movi v1.16b, #0xcc");

  COMPARE(Movi(v4.V4H(), 0xaa, LSL, 0), "movi v4.4h, #0xaa, lsl #0");
  COMPARE(Movi(v1.V8H(), 0xcc, LSL, 8), "movi v1.8h, #0xcc, lsl #8");

  COMPARE(Movi(v4.V2S(), 0xaa, LSL, 0), "movi v4.2s, #0xaa, lsl #0");
  COMPARE(Movi(v1.V2S(), 0xcc, LSL, 8), "movi v1.2s, #0xcc, lsl #8");
  COMPARE(Movi(v4.V4S(), 0xaa, LSL, 16), "movi v4.4s, #0xaa, lsl #16");
  COMPARE(Movi(v1.V4S(), 0xcc, LSL, 24), "movi v1.4s, #0xcc, lsl #24");

  COMPARE(Movi(v4.V2S(), 0xaa, MSL, 8), "movi v4.2s, #0xaa, msl #8");
  COMPARE(Movi(v1.V2S(), 0xcc, MSL, 16), "movi v1.2s, #0xcc, msl #16");
  COMPARE(Movi(v4.V4S(), 0xaa, MSL, 8), "movi v4.4s, #0xaa, msl #8");
  COMPARE(Movi(v1.V4S(), 0xcc, MSL, 16), "movi v1.4s, #0xcc, msl #16");

  COMPARE(Movi(d2, 0xffff0000ffffff), "movi d2, #0xffff0000ffffff");
  COMPARE(Movi(v1.V2D(), 0xffff0000ffffff), "movi v1.2d, #0xffff0000ffffff");

  COMPARE(Fmov(v0.V2S(), 1.0f), "fmov v0.2s, #0x70 (1.0000)");
  COMPARE(Fmov(v31.V2S(), -13.0f), "fmov v31.2s, #0xaa (-13.0000)");
  COMPARE(Fmov(v0.V4S(), 1.0f), "fmov v0.4s, #0x70 (1.0000)");
  COMPARE(Fmov(v31.V4S(), -13.0f), "fmov v31.4s, #0xaa (-13.0000)");
  COMPARE(Fmov(v1.V2D(), 1.0), "fmov v1.2d, #0x70 (1.0000)");
  COMPARE(Fmov(v29.V2D(), -13.0), "fmov v29.2d, #0xaa (-13.0000)");

  // An unallocated form of fmov.
  COMPARE(dci(0x2f07ffff), "unallocated (NEONModifiedImmediate)");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_2regmisc) {
  SET_UP_MASM();

  COMPARE(Shll(v1.V8H(), v8_.V8B(), 8), "shll v1.8h, v8.8b, #8");
  COMPARE(Shll(v3.V4S(), v1.V4H(), 16), "shll v3.4s, v1.4h, #16");
  COMPARE(Shll(v5.V2D(), v3.V2S(), 32), "shll v5.2d, v3.2s, #32");
  COMPARE(Shll2(v2.V8H(), v9.V16B(), 8), "shll2 v2.8h, v9.16b, #8");
  COMPARE(Shll2(v4.V4S(), v2.V8H(), 16), "shll2 v4.4s, v2.8h, #16");
  COMPARE(Shll2(v6.V2D(), v4.V4S(), 32), "shll2 v6.2d, v4.4s, #32");

  // An unallocated form of shll.
  COMPARE(dci(0x2ee13bff), "unallocated (NEON2RegMisc)");
  // An unallocated form of shll2.
  COMPARE(dci(0x6ee13bff), "unallocated (NEON2RegMisc)");

#define DISASM_INST(M, S) \
  COMPARE(Cmeq(v0.M, v1.M, 0), "cmeq v0." S ", v1." S ", #0");
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmge(v0.M, v1.M, 0), "cmge v0." S ", v1." S ", #0");
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmgt(v0.M, v1.M, 0), "cmgt v0." S ", v1." S ", #0");
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmle(v0.M, v1.M, 0), "cmle v0." S ", v1." S ", #0");
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(M, S) \
  COMPARE(Cmlt(v0.M, v1.M, 0), "cmlt v0." S ", v1." S ", #0");
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Cmeq(v0.D(), v1.D(), 0), "cmeq d0, d1, #0");
  COMPARE(Cmge(v3.D(), v4.D(), 0), "cmge d3, d4, #0");
  COMPARE(Cmgt(v6.D(), v7.D(), 0), "cmgt d6, d7, #0");
  COMPARE(Cmle(v0.D(), v1.D(), 0), "cmle d0, d1, #0");
  COMPARE(Cmlt(v3.D(), v4.D(), 0), "cmlt d3, d4, #0");

#define DISASM_INST(M, S) \
  COMPARE(Fcmeq(v0.M, v1.M, 0), "fcmeq v0." S ", v1." S ", #0.0");
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  COMPARE(Fcmeq(v0.S(), v1.S(), 0), "fcmeq s0, s1, #0.0");
  COMPARE(Fcmeq(v0.D(), v1.D(), 0), "fcmeq d0, d1, #0.0");

#define DISASM_INST(M, S) \
  COMPARE(Fcmge(v0.M, v1.M, 0), "fcmge v0." S ", v1." S ", #0.0");
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  COMPARE(Fcmge(v0.S(), v1.S(), 0), "fcmge s0, s1, #0.0");
  COMPARE(Fcmge(v0.D(), v1.D(), 0), "fcmge d0, d1, #0.0");

#define DISASM_INST(M, S) \
  COMPARE(Fcmgt(v0.M, v1.M, 0), "fcmgt v0." S ", v1." S ", #0.0");
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  COMPARE(Fcmgt(v0.S(), v1.S(), 0), "fcmgt s0, s1, #0.0");
  COMPARE(Fcmgt(v0.D(), v1.D(), 0), "fcmgt d0, d1, #0.0");

#define DISASM_INST(M, S) \
  COMPARE(Fcmle(v0.M, v1.M, 0), "fcmle v0." S ", v1." S ", #0.0");
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  COMPARE(Fcmle(v0.S(), v1.S(), 0), "fcmle s0, s1, #0.0");
  COMPARE(Fcmle(v0.D(), v1.D(), 0), "fcmle d0, d1, #0.0");

#define DISASM_INST(M, S) \
  COMPARE(Fcmlt(v0.M, v1.M, 0), "fcmlt v0." S ", v1." S ", #0.0");
  NEON_FORMAT_LIST_FP(DISASM_INST)
#undef DISASM_INST

  COMPARE(Fcmlt(v0.S(), v1.S(), 0), "fcmlt s0, s1, #0.0");
  COMPARE(Fcmlt(v0.D(), v1.D(), 0), "fcmlt d0, d1, #0.0");

#define DISASM_INST(M, S) COMPARE(Neg(v0.M, v1.M), "neg v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Neg(v0.D(), v1.D()), "neg d0, d1");

#define DISASM_INST(M, S) COMPARE(Sqneg(v0.M, v1.M), "sqneg v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Sqneg(b0, b1), "sqneg b0, b1");
  COMPARE(Sqneg(h1, h2), "sqneg h1, h2");
  COMPARE(Sqneg(s2, s3), "sqneg s2, s3");
  COMPARE(Sqneg(d3, d4), "sqneg d3, d4");

#define DISASM_INST(M, S) COMPARE(Abs(v0.M, v1.M), "abs v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Abs(v0.D(), v1.D()), "abs d0, d1");

#define DISASM_INST(M, S) COMPARE(Sqabs(v0.M, v1.M), "sqabs v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Sqabs(b0, b1), "sqabs b0, b1");
  COMPARE(Sqabs(h1, h2), "sqabs h1, h2");
  COMPARE(Sqabs(s2, s3), "sqabs s2, s3");
  COMPARE(Sqabs(d3, d4), "sqabs d3, d4");

#define DISASM_INST(M, S) COMPARE(Suqadd(v0.M, v1.M), "suqadd v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Suqadd(b0, b1), "suqadd b0, b1");
  COMPARE(Suqadd(h1, h2), "suqadd h1, h2");
  COMPARE(Suqadd(s2, s3), "suqadd s2, s3");
  COMPARE(Suqadd(d3, d4), "suqadd d3, d4");

#define DISASM_INST(M, S) COMPARE(Usqadd(v0.M, v1.M), "usqadd v0." S ", v1." S);
  NEON_FORMAT_LIST(DISASM_INST)
#undef DISASM_INST

  COMPARE(Usqadd(b0, b1), "usqadd b0, b1");
  COMPARE(Usqadd(h1, h2), "usqadd h1, h2");
  COMPARE(Usqadd(s2, s3), "usqadd s2, s3");
  COMPARE(Usqadd(d3, d4), "usqadd d3, d4");

  COMPARE(Xtn(v0.V8B(), v1.V8H()), "xtn v0.8b, v1.8h");
  COMPARE(Xtn(v1.V4H(), v2.V4S()), "xtn v1.4h, v2.4s");
  COMPARE(Xtn(v2.V2S(), v3.V2D()), "xtn v2.2s, v3.2d");
  COMPARE(Xtn2(v0.V16B(), v1.V8H()), "xtn2 v0.16b, v1.8h");
  COMPARE(Xtn2(v1.V8H(), v2.V4S()), "xtn2 v1.8h, v2.4s");
  COMPARE(Xtn2(v2.V4S(), v3.V2D()), "xtn2 v2.4s, v3.2d");

  COMPARE(Sqxtn(v0.V8B(), v1.V8H()), "sqxtn v0.8b, v1.8h");
  COMPARE(Sqxtn(v1.V4H(), v2.V4S()), "sqxtn v1.4h, v2.4s");
  COMPARE(Sqxtn(v2.V2S(), v3.V2D()), "sqxtn v2.2s, v3.2d");
  COMPARE(Sqxtn2(v0.V16B(), v1.V8H()), "sqxtn2 v0.16b, v1.8h");
  COMPARE(Sqxtn2(v1.V8H(), v2.V4S()), "sqxtn2 v1.8h, v2.4s");
  COMPARE(Sqxtn2(v2.V4S(), v3.V2D()), "sqxtn2 v2.4s, v3.2d");
  COMPARE(Sqxtn(b19, h0), "sqxtn b19, h0");
  COMPARE(Sqxtn(h20, s0), "sqxtn h20, s0");
  COMPARE(Sqxtn(s21, d0), "sqxtn s21, d0");

  COMPARE(Uqxtn(v0.V8B(), v1.V8H()), "uqxtn v0.8b, v1.8h");
  COMPARE(Uqxtn(v1.V4H(), v2.V4S()), "uqxtn v1.4h, v2.4s");
  COMPARE(Uqxtn(v2.V2S(), v3.V2D()), "uqxtn v2.2s, v3.2d");
  COMPARE(Uqxtn2(v0.V16B(), v1.V8H()), "uqxtn2 v0.16b, v1.8h");
  COMPARE(Uqxtn2(v1.V8H(), v2.V4S()), "uqxtn2 v1.8h, v2.4s");
  COMPARE(Uqxtn2(v2.V4S(), v3.V2D()), "uqxtn2 v2.4s, v3.2d");
  COMPARE(Uqxtn(b19, h0), "uqxtn b19, h0");
  COMPARE(Uqxtn(h20, s0), "uqxtn h20, s0");
  COMPARE(Uqxtn(s21, d0), "uqxtn s21, d0");

  COMPARE(Sqxtun(v0.V8B(), v1.V8H()), "sqxtun v0.8b, v1.8h");
  COMPARE(Sqxtun(v1.V4H(), v2.V4S()), "sqxtun v1.4h, v2.4s");
  COMPARE(Sqxtun(v2.V2S(), v3.V2D()), "sqxtun v2.2s, v3.2d");
  COMPARE(Sqxtun2(v0.V16B(), v1.V8H()), "sqxtun2 v0.16b, v1.8h");
  COMPARE(Sqxtun2(v1.V8H(), v2.V4S()), "sqxtun2 v1.8h, v2.4s");
  COMPARE(Sqxtun2(v2.V4S(), v3.V2D()), "sqxtun2 v2.4s, v3.2d");
  COMPARE(Sqxtun(b19, h0), "sqxtun b19, h0");
  COMPARE(Sqxtun(h20, s0), "sqxtun h20, s0");
  COMPARE(Sqxtun(s21, d0), "sqxtun s21, d0");

  COMPARE(Cls(v1.V8B(), v8_.V8B()), "cls v1.8b, v8.8b");
  COMPARE(Cls(v2.V16B(), v9.V16B()), "cls v2.16b, v9.16b");
  COMPARE(Cls(v3.V4H(), v1.V4H()), "cls v3.4h, v1.4h");
  COMPARE(Cls(v4.V8H(), v2.V8H()), "cls v4.8h, v2.8h");
  COMPARE(Cls(v5.V2S(), v3.V2S()), "cls v5.2s, v3.2s");
  COMPARE(Cls(v6.V4S(), v4.V4S()), "cls v6.4s, v4.4s");

  COMPARE(Clz(v1.V8B(), v8_.V8B()), "clz v1.8b, v8.8b");
  COMPARE(Clz(v2.V16B(), v9.V16B()), "clz v2.16b, v9.16b");
  COMPARE(Clz(v3.V4H(), v1.V4H()), "clz v3.4h, v1.4h");
  COMPARE(Clz(v4.V8H(), v2.V8H()), "clz v4.8h, v2.8h");
  COMPARE(Clz(v5.V2S(), v3.V2S()), "clz v5.2s, v3.2s");
  COMPARE(Clz(v6.V4S(), v4.V4S()), "clz v6.4s, v4.4s");

  COMPARE(Cnt(v1.V8B(), v8_.V8B()), "cnt v1.8b, v8.8b");
  COMPARE(Cnt(v2.V16B(), v9.V16B()), "cnt v2.16b, v9.16b");

  COMPARE(Mvn(v4.V8B(), v5.V8B()), "mvn v4.8b, v5.8b");
  COMPARE(Mvn(v4.V16B(), v5.V16B()), "mvn v4.16b, v5.16b");

  COMPARE(Not(v4.V8B(), v5.V8B()), "mvn v4.8b, v5.8b");
  COMPARE(Not(v4.V16B(), v5.V16B()), "mvn v4.16b, v5.16b");

  COMPARE(Rev64(v1.V8B(), v8_.V8B()), "rev64 v1.8b, v8.8b");
  COMPARE(Rev64(v2.V16B(), v9.V16B()), "rev64 v2.16b, v9.16b");
  COMPARE(Rev64(v3.V4H(), v1.V4H()), "rev64 v3.4h, v1.4h");
  COMPARE(Rev64(v4.V8H(), v2.V8H()), "rev64 v4.8h, v2.8h");
  COMPARE(Rev64(v5.V2S(), v3.V2S()), "rev64 v5.2s, v3.2s");
  COMPARE(Rev64(v6.V4S(), v4.V4S()), "rev64 v6.4s, v4.4s");

  COMPARE(Rev32(v1.V8B(), v8_.V8B()), "rev32 v1.8b, v8.8b");
  COMPARE(Rev32(v2.V16B(), v9.V16B()), "rev32 v2.16b, v9.16b");
  COMPARE(Rev32(v3.V4H(), v1.V4H()), "rev32 v3.4h, v1.4h");
  COMPARE(Rev32(v4.V8H(), v2.V8H()), "rev32 v4.8h, v2.8h");

  COMPARE(Rev16(v1.V8B(), v8_.V8B()), "rev16 v1.8b, v8.8b");
  COMPARE(Rev16(v2.V16B(), v9.V16B()), "rev16 v2.16b, v9.16b");

  COMPARE(Rbit(v1.V8B(), v8_.V8B()), "rbit v1.8b, v8.8b");
  COMPARE(Rbit(v2.V16B(), v9.V16B()), "rbit v2.16b, v9.16b");

  COMPARE(Ursqrte(v2.V2S(), v9.V2S()), "ursqrte v2.2s, v9.2s");
  COMPARE(Ursqrte(v16.V4S(), v23.V4S()), "ursqrte v16.4s, v23.4s");

  COMPARE(Urecpe(v2.V2S(), v9.V2S()), "urecpe v2.2s, v9.2s");
  COMPARE(Urecpe(v16.V4S(), v23.V4S()), "urecpe v16.4s, v23.4s");

  COMPARE(Frsqrte(v2.V2S(), v9.V2S()), "frsqrte v2.2s, v9.2s");
  COMPARE(Frsqrte(v16.V4S(), v23.V4S()), "frsqrte v16.4s, v23.4s");
  COMPARE(Frsqrte(v2.V2D(), v9.V2D()), "frsqrte v2.2d, v9.2d");
  COMPARE(Frsqrte(v0.S(), v1.S()), "frsqrte s0, s1");
  COMPARE(Frsqrte(v0.D(), v1.D()), "frsqrte d0, d1");

  COMPARE(Frecpe(v2.V2S(), v9.V2S()), "frecpe v2.2s, v9.2s");
  COMPARE(Frecpe(v16.V4S(), v23.V4S()), "frecpe v16.4s, v23.4s");
  COMPARE(Frecpe(v2.V2D(), v9.V2D()), "frecpe v2.2d, v9.2d");
  COMPARE(Frecpe(v0.S(), v1.S()), "frecpe s0, s1");
  COMPARE(Frecpe(v0.D(), v1.D()), "frecpe d0, d1");

  COMPARE(Fabs(v2.V2S(), v9.V2S()), "fabs v2.2s, v9.2s");
  COMPARE(Fabs(v16.V4S(), v23.V4S()), "fabs v16.4s, v23.4s");
  COMPARE(Fabs(v31.V2D(), v30.V2D()), "fabs v31.2d, v30.2d");

  COMPARE(Fneg(v2.V2S(), v9.V2S()), "fneg v2.2s, v9.2s");
  COMPARE(Fneg(v16.V4S(), v23.V4S()), "fneg v16.4s, v23.4s");
  COMPARE(Fneg(v31.V2D(), v30.V2D()), "fneg v31.2d, v30.2d");

  COMPARE(Frintn(v2.V2S(), v9.V2S()), "frintn v2.2s, v9.2s");
  COMPARE(Frintn(v16.V4S(), v23.V4S()), "frintn v16.4s, v23.4s");
  COMPARE(Frintn(v31.V2D(), v30.V2D()), "frintn v31.2d, v30.2d");

  COMPARE(Frinta(v2.V2S(), v9.V2S()), "frinta v2.2s, v9.2s");
  COMPARE(Frinta(v16.V4S(), v23.V4S()), "frinta v16.4s, v23.4s");
  COMPARE(Frinta(v31.V2D(), v30.V2D()), "frinta v31.2d, v30.2d");

  COMPARE(Frintp(v2.V2S(), v9.V2S()), "frintp v2.2s, v9.2s");
  COMPARE(Frintp(v16.V4S(), v23.V4S()), "frintp v16.4s, v23.4s");
  COMPARE(Frintp(v31.V2D(), v30.V2D()), "frintp v31.2d, v30.2d");

  COMPARE(Frintm(v2.V2S(), v9.V2S()), "frintm v2.2s, v9.2s");
  COMPARE(Frintm(v16.V4S(), v23.V4S()), "frintm v16.4s, v23.4s");
  COMPARE(Frintm(v31.V2D(), v30.V2D()), "frintm v31.2d, v30.2d");

  COMPARE(Frintx(v2.V2S(), v9.V2S()), "frintx v2.2s, v9.2s");
  COMPARE(Frintx(v16.V4S(), v23.V4S()), "frintx v16.4s, v23.4s");
  COMPARE(Frintx(v31.V2D(), v30.V2D()), "frintx v31.2d, v30.2d");

  COMPARE(Frintz(v2.V2S(), v9.V2S()), "frintz v2.2s, v9.2s");
  COMPARE(Frintz(v16.V4S(), v23.V4S()), "frintz v16.4s, v23.4s");
  COMPARE(Frintz(v31.V2D(), v30.V2D()), "frintz v31.2d, v30.2d");

  COMPARE(Frinti(v2.V2S(), v9.V2S()), "frinti v2.2s, v9.2s");
  COMPARE(Frinti(v16.V4S(), v23.V4S()), "frinti v16.4s, v23.4s");
  COMPARE(Frinti(v31.V2D(), v30.V2D()), "frinti v31.2d, v30.2d");

  COMPARE(Fsqrt(v3.V2S(), v10.V2S()), "fsqrt v3.2s, v10.2s");
  COMPARE(Fsqrt(v22.V4S(), v11.V4S()), "fsqrt v22.4s, v11.4s");
  COMPARE(Fsqrt(v31.V2D(), v0.V2D()), "fsqrt v31.2d, v0.2d");

  COMPARE(Fcvtns(v4.V2S(), v11.V2S()), "fcvtns v4.2s, v11.2s");
  COMPARE(Fcvtns(v23.V4S(), v12.V4S()), "fcvtns v23.4s, v12.4s");
  COMPARE(Fcvtns(v30.V2D(), v1.V2D()), "fcvtns v30.2d, v1.2d");
  COMPARE(Fcvtnu(v4.V2S(), v11.V2S()), "fcvtnu v4.2s, v11.2s");
  COMPARE(Fcvtnu(v23.V4S(), v12.V4S()), "fcvtnu v23.4s, v12.4s");
  COMPARE(Fcvtnu(v30.V2D(), v1.V2D()), "fcvtnu v30.2d, v1.2d");

  COMPARE(Fcvtps(v4.V2S(), v11.V2S()), "fcvtps v4.2s, v11.2s");
  COMPARE(Fcvtps(v23.V4S(), v12.V4S()), "fcvtps v23.4s, v12.4s");
  COMPARE(Fcvtps(v30.V2D(), v1.V2D()), "fcvtps v30.2d, v1.2d");
  COMPARE(Fcvtpu(v4.V2S(), v11.V2S()), "fcvtpu v4.2s, v11.2s");
  COMPARE(Fcvtpu(v23.V4S(), v12.V4S()), "fcvtpu v23.4s, v12.4s");
  COMPARE(Fcvtpu(v30.V2D(), v1.V2D()), "fcvtpu v30.2d, v1.2d");

  COMPARE(Fcvtms(v4.V2S(), v11.V2S()), "fcvtms v4.2s, v11.2s");
  COMPARE(Fcvtms(v23.V4S(), v12.V4S()), "fcvtms v23.4s, v12.4s");
  COMPARE(Fcvtms(v30.V2D(), v1.V2D()), "fcvtms v30.2d, v1.2d");
  COMPARE(Fcvtmu(v4.V2S(), v11.V2S()), "fcvtmu v4.2s, v11.2s");
  COMPARE(Fcvtmu(v23.V4S(), v12.V4S()), "fcvtmu v23.4s, v12.4s");
  COMPARE(Fcvtmu(v30.V2D(), v1.V2D()), "fcvtmu v30.2d, v1.2d");

  COMPARE(Fcvtzs(v4.V2S(), v11.V2S()), "fcvtzs v4.2s, v11.2s");
  COMPARE(Fcvtzs(v23.V4S(), v12.V4S()), "fcvtzs v23.4s, v12.4s");
  COMPARE(Fcvtzs(v30.V2D(), v1.V2D()), "fcvtzs v30.2d, v1.2d");
  COMPARE(Fcvtzu(v4.V2S(), v11.V2S()), "fcvtzu v4.2s, v11.2s");
  COMPARE(Fcvtzu(v23.V4S(), v12.V4S()), "fcvtzu v23.4s, v12.4s");
  COMPARE(Fcvtzu(v30.V2D(), v1.V2D()), "fcvtzu v30.2d, v1.2d");

  COMPARE(Fcvtas(v4.V2S(), v11.V2S()), "fcvtas v4.2s, v11.2s");
  COMPARE(Fcvtas(v23.V4S(), v12.V4S()), "fcvtas v23.4s, v12.4s");
  COMPARE(Fcvtas(v30.V2D(), v1.V2D()), "fcvtas v30.2d, v1.2d");
  COMPARE(Fcvtau(v4.V2S(), v11.V2S()), "fcvtau v4.2s, v11.2s");
  COMPARE(Fcvtau(v23.V4S(), v12.V4S()), "fcvtau v23.4s, v12.4s");
  COMPARE(Fcvtau(v30.V2D(), v1.V2D()), "fcvtau v30.2d, v1.2d");

  COMPARE(Fcvtns(s0, s1), "fcvtns s0, s1");
  COMPARE(Fcvtns(d2, d3), "fcvtns d2, d3");
  COMPARE(Fcvtnu(s4, s5), "fcvtnu s4, s5");
  COMPARE(Fcvtnu(d6, d7), "fcvtnu d6, d7");
  COMPARE(Fcvtps(s8, s9), "fcvtps s8, s9");
  COMPARE(Fcvtps(d10, d11), "fcvtps d10, d11");
  COMPARE(Fcvtpu(s12, s13), "fcvtpu s12, s13");
  COMPARE(Fcvtpu(d14, d15), "fcvtpu d14, d15");
  COMPARE(Fcvtms(s16, s17), "fcvtms s16, s17");
  COMPARE(Fcvtms(d18, d19), "fcvtms d18, d19");
  COMPARE(Fcvtmu(s20, s21), "fcvtmu s20, s21");
  COMPARE(Fcvtmu(d22, d23), "fcvtmu d22, d23");
  COMPARE(Fcvtzs(s24, s25), "fcvtzs s24, s25");
  COMPARE(Fcvtzs(d26, d27), "fcvtzs d26, d27");
  COMPARE(Fcvtzu(s28, s29), "fcvtzu s28, s29");
  COMPARE(Fcvtzu(d30, d31), "fcvtzu d30, d31");
  COMPARE(Fcvtas(s0, s1), "fcvtas s0, s1");
  COMPARE(Fcvtas(d2, d3), "fcvtas d2, d3");
  COMPARE(Fcvtau(s4, s5), "fcvtau s4, s5");
  COMPARE(Fcvtau(d6, d7), "fcvtau d6, d7");

  COMPARE(Fcvtl(v3.V4S(), v5.V4H()), "fcvtl v3.4s, v5.4h");
  COMPARE(Fcvtl(v7.V2D(), v11.V2S()), "fcvtl v7.2d, v11.2s");
  COMPARE(Fcvtl2(v13.V4S(), v17.V8H()), "fcvtl2 v13.4s, v17.8h");
  COMPARE(Fcvtl2(v23.V2D(), v29.V4S()), "fcvtl2 v23.2d, v29.4s");

  COMPARE(Fcvtn(v3.V4H(), v5.V4S()), "fcvtn v3.4h, v5.4s");
  COMPARE(Fcvtn(v7.V2S(), v11.V2D()), "fcvtn v7.2s, v11.2d");
  COMPARE(Fcvtn2(v13.V8H(), v17.V4S()), "fcvtn2 v13.8h, v17.4s");
  COMPARE(Fcvtn2(v23.V4S(), v29.V2D()), "fcvtn2 v23.4s, v29.2d");

  COMPARE(Fcvtxn(v5.V2S(), v7.V2D()), "fcvtxn v5.2s, v7.2d");
  COMPARE(Fcvtxn2(v8_.V4S(), v13.V2D()), "fcvtxn2 v8.4s, v13.2d");
  COMPARE(Fcvtxn(s17, d31), "fcvtxn s17, d31");

  COMPARE(Frecpx(s0, s1), "frecpx s0, s1");
  COMPARE(Frecpx(s31, s30), "frecpx s31, s30");
  COMPARE(Frecpx(d2, d3), "frecpx d2, d3");
  COMPARE(Frecpx(d31, d30), "frecpx d31, d30");

  COMPARE(Scvtf(v5.V2S(), v3.V2S()), "scvtf v5.2s, v3.2s");
  COMPARE(Scvtf(v6.V4S(), v4.V4S()), "scvtf v6.4s, v4.4s");
  COMPARE(Scvtf(v7.V2D(), v5.V2D()), "scvtf v7.2d, v5.2d");
  COMPARE(Scvtf(s8, s6), "scvtf s8, s6");
  COMPARE(Scvtf(d8, d6), "scvtf d8, d6");

  COMPARE(Ucvtf(v5.V2S(), v3.V2S()), "ucvtf v5.2s, v3.2s");
  COMPARE(Ucvtf(v6.V4S(), v4.V4S()), "ucvtf v6.4s, v4.4s");
  COMPARE(Ucvtf(v7.V2D(), v5.V2D()), "ucvtf v7.2d, v5.2d");
  COMPARE(Ucvtf(s8, s6), "ucvtf s8, s6");
  COMPARE(Ucvtf(d8, d6), "ucvtf d8, d6");

#define DISASM_INST(TA, TAS, TB, TBS) \
  COMPARE(Saddlp(v0.TA, v1.TB), "saddlp v0." TAS ", v1." TBS);
  NEON_FORMAT_LIST_LP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS) \
  COMPARE(Uaddlp(v0.TA, v1.TB), "uaddlp v0." TAS ", v1." TBS);
  NEON_FORMAT_LIST_LP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS) \
  COMPARE(Sadalp(v0.TA, v1.TB), "sadalp v0." TAS ", v1." TBS);
  NEON_FORMAT_LIST_LP(DISASM_INST)
#undef DISASM_INST

#define DISASM_INST(TA, TAS, TB, TBS) \
  COMPARE(Uadalp(v0.TA, v1.TB), "uadalp v0." TAS ", v1." TBS);
  NEON_FORMAT_LIST_LP(DISASM_INST)
#undef DISASM_INST

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_acrosslanes) {
  SET_UP_MASM();

  COMPARE(Smaxv(b4, v5.V8B()), "smaxv b4, v5.8b");
  COMPARE(Smaxv(b4, v5.V16B()), "smaxv b4, v5.16b");
  COMPARE(Smaxv(h4, v5.V4H()), "smaxv h4, v5.4h");
  COMPARE(Smaxv(h4, v5.V8H()), "smaxv h4, v5.8h");
  COMPARE(Smaxv(s4, v5.V4S()), "smaxv s4, v5.4s");

  COMPARE(Sminv(b4, v5.V8B()), "sminv b4, v5.8b");
  COMPARE(Sminv(b4, v5.V16B()), "sminv b4, v5.16b");
  COMPARE(Sminv(h4, v5.V4H()), "sminv h4, v5.4h");
  COMPARE(Sminv(h4, v5.V8H()), "sminv h4, v5.8h");
  COMPARE(Sminv(s4, v5.V4S()), "sminv s4, v5.4s");

  COMPARE(Umaxv(b4, v5.V8B()), "umaxv b4, v5.8b");
  COMPARE(Umaxv(b4, v5.V16B()), "umaxv b4, v5.16b");
  COMPARE(Umaxv(h4, v5.V4H()), "umaxv h4, v5.4h");
  COMPARE(Umaxv(h4, v5.V8H()), "umaxv h4, v5.8h");
  COMPARE(Umaxv(s4, v5.V4S()), "umaxv s4, v5.4s");

  COMPARE(Uminv(b4, v5.V8B()), "uminv b4, v5.8b");
  COMPARE(Uminv(b4, v5.V16B()), "uminv b4, v5.16b");
  COMPARE(Uminv(h4, v5.V4H()), "uminv h4, v5.4h");
  COMPARE(Uminv(h4, v5.V8H()), "uminv h4, v5.8h");
  COMPARE(Uminv(s4, v5.V4S()), "uminv s4, v5.4s");

  COMPARE(Addv(b4, v5.V8B()), "addv b4, v5.8b");
  COMPARE(Addv(b4, v5.V16B()), "addv b4, v5.16b");
  COMPARE(Addv(h4, v5.V4H()), "addv h4, v5.4h");
  COMPARE(Addv(h4, v5.V8H()), "addv h4, v5.8h");
  COMPARE(Addv(s4, v5.V4S()), "addv s4, v5.4s");

  COMPARE(Saddlv(h4, v5.V8B()), "saddlv h4, v5.8b");
  COMPARE(Saddlv(h4, v5.V16B()), "saddlv h4, v5.16b");
  COMPARE(Saddlv(s4, v5.V4H()), "saddlv s4, v5.4h");
  COMPARE(Saddlv(s4, v5.V8H()), "saddlv s4, v5.8h");
  COMPARE(Saddlv(d4, v5.V4S()), "saddlv d4, v5.4s");

  COMPARE(Uaddlv(h4, v5.V8B()), "uaddlv h4, v5.8b");
  COMPARE(Uaddlv(h4, v5.V16B()), "uaddlv h4, v5.16b");
  COMPARE(Uaddlv(s4, v5.V4H()), "uaddlv s4, v5.4h");
  COMPARE(Uaddlv(s4, v5.V8H()), "uaddlv s4, v5.8h");
  COMPARE(Uaddlv(d4, v5.V4S()), "uaddlv d4, v5.4s");

  COMPARE(Fmaxv(s4, v5.V4S()), "fmaxv s4, v5.4s");
  COMPARE(Fminv(s4, v5.V4S()), "fminv s4, v5.4s");
  COMPARE(Fmaxnmv(s4, v5.V4S()), "fmaxnmv s4, v5.4s");
  COMPARE(Fminnmv(s4, v5.V4S()), "fminnmv s4, v5.4s");

  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_scalar_pairwise) {
  SET_UP_MASM();

  COMPARE(Addp(d0, v1.V2D()), "addp d0, v1.2d");
  COMPARE(Faddp(s0, v1.V2S()), "faddp s0, v1.2s");
  COMPARE(Faddp(d2, v3.V2D()), "faddp d2, v3.2d");
  COMPARE(Fmaxp(s4, v5.V2S()), "fmaxp s4, v5.2s");
  COMPARE(Fmaxp(d6, v7.V2D()), "fmaxp d6, v7.2d");
  COMPARE(Fmaxnmp(s8, v9.V2S()), "fmaxnmp s8, v9.2s");
  COMPARE(Fmaxnmp(d10, v11.V2D()), "fmaxnmp d10, v11.2d");
  COMPARE(Fminp(s12, v13.V2S()), "fminp s12, v13.2s");
  COMPARE(Fminp(d14, v15.V2D()), "fminp d14, v15.2d");
  COMPARE(Fminnmp(s16, v17.V2S()), "fminnmp s16, v17.2s");
  COMPARE(Fminnmp(d18, v19.V2D()), "fminnmp d18, v19.2d");
  CLEANUP();
}

TEST_F(DisasmArm64Test, neon_shift_immediate) {
  SET_UP_MASM();

  COMPARE(Sshr(v0.V8B(), v1.V8B(), 1), "sshr v0.8b, v1.8b, #1");
  COMPARE(Sshr(v2.V8B(), v3.V8B(), 8), "sshr v2.8b, v3.8b, #8");
  COMPARE(Sshr(v4.V16B(), v5.V16B(), 1), "sshr v4.16b, v5.16b, #1");
  COMPARE(Sshr(v6.V16B(), v7.V16B(), 8), "sshr v6.16b, v7.16b, #8");
  COMPARE(Sshr(v8_.V4H(), v9.V4H(), 1), "sshr v8.4h, v9.4h, #1");
  COMPARE(Sshr(v10.V4H(), v11.V4H(), 16), "sshr v10.4h, v11.4h, #16");
  COMPARE(Sshr(v12.V8H(), v13.V8H(), 1), "sshr v12.8h, v13.8h, #1");
  COMPARE(Sshr(v14.V8H(), v15.V8H(), 16), "sshr v14.8h, v15.8h, #16");
  COMPARE(Sshr(v16.V2S(), v17.V2S(), 1), "sshr v16.2s, v17.2s, #1");
  COMPARE(Sshr(v18.V2S(), v19.V2S(), 32), "sshr v18.2s, v19.2s, #32");
  COMPARE(Sshr(v20.V4S(), v21.V4S(), 1), "sshr v20.4s, v21.4s, #1");
  COMPARE(Sshr(v22.V4S(), v23.V4S(), 32), "sshr v22.4s, v23.4s, #32");
  COMPARE(Sshr(v28.V2D(), v29.V2D(), 1), "sshr v28.2d, v29.2d, #1");
  COMPARE(Sshr(v30.V2D(), v31.V2D(), 64), "sshr v30.2d, v31.2d, #64");
  COMPARE(Sshr(d0, d1, 7), "sshr d0, d1, #7");

  COMPARE(Ushr(v0.V8B(), v1.V8B(), 1), "ushr v0.8b, v1.8b, #1");
  COMPARE(Ushr(v2.V8B(), v3.V8B(), 8), "ushr v2.8b, v3.8b, #8");
  COMPARE(Ushr(v4.V16B(), v5.V16B(), 1), "ushr v4.16b, v5.16b, #1");
  COMPARE(Ushr(v6.V16B(), v7.V16B(), 8), "ushr v6.16b, v7.16b, #8");
  COMPARE(Ushr(v8_.V4H(), v9.V4H(), 1), "ushr v8.4h, v9.4h, #1");
  COMPARE(Ushr(v10.V4H(), v11.V4H(), 16), "ushr v10.4h, v11.4h, #16");
  COMPARE(Ushr(v12.V8H(), v13.V8H(), 1), "ushr v12.8h, v13.8h, #1");
  COMPARE(Ushr(v14.V8H(), v15.V8H(), 16), "ushr v14.8h, v15.8h, #16");
  COMPARE(Ushr(v16.V2S(), v17.V2S(), 1), "ushr v16.2s, v17.2s, #1");
  COMPARE(Ushr(v18.V2S(), v19.V2S(), 32), "ushr v18.2s, v19.2s, #32");
  COMPARE(Ushr(v20.V4S(), v21.V4S(), 1), "ushr v20.4s, v21.4s, #1");
  COMPARE(Ushr(v22.V4S(), v23.V4S(), 32), "ushr v22.4s, v23.4s, #32");
  COMPARE(Ushr(v28.V2D(), v29.V2D(), 1), "ushr v28.2d, v29.2d, #1");
  COMPARE(Ushr(v30.V2D(), v31.V2D(), 64), "ushr v30.2d, v31.2d, #64");
  COMPARE(Ushr(d0, d1, 7), "ushr d0, d1, #7");

  COMPARE(Srshr(v0.V8B(), v1.V8B(), 1), "srshr v0.8b, v1.8b, #1");
  COMPARE(Srshr(v2.V8B(), v3.V8B(), 8), "srshr v2.8b, v3.8b, #8");
  COMPARE(Srshr(v4.V16B(), v5.V16B(), 1), "srshr v4.16b, v5.16b, #1");
  COMPARE(Srshr(v6.V16B(), v7.V16B(), 8), "srshr v6.16b, v7.16b, #8");
  COMPARE(Srshr(v8_.V4H(), v9.V4H(), 1), "srshr v8.4h, v9.4h, #1");
  COMPARE(Srshr(v10.V4H(), v11.V4H(), 16), "srshr v10.4h, v11.4h, #16");
  COMPARE(Srshr(v12.V8H(), v13.V8H(), 1), "srshr v12.8h, v13.8h, #1");
  COMPARE(Srshr(v14.V8H(), v15.V8H(), 16), "srshr v14.8h, v15.8h, #16");
  COMPARE(Srshr(v16.V2S(), v17.V2S(), 1), "srshr v16.2s, v17.2s, #1");
  COMPARE(Srshr(v18.V2S(), v19.V2S(), 32), "srshr v18.2s, v19.2s, #32");
  COMPARE(Srshr(v20.V4S(), v21.V4S(), 1), "srshr v20.4s, v21.4s, #1");
  COMPARE(Srshr(v22.V4S(), v23.V4S(), 32), "srshr v22.4s, v23.4s, #32");
  COMPARE(Srshr(v28.V2D(), v29.V2D(), 1), "srshr v28.2d, v29.2d, #1");
  COMPARE(Srshr(v30.V2D(), v31.V2D(), 64), "srshr v30.2d, v31.2d, #64");
  COMPARE(Srshr(d0, d1, 7), "srshr d0, d1, #7");

  COMPARE(Urshr(v0.V8B(), v1.V8B(), 1), "urshr v0.8b, v1.8b, #1");
  COMPARE(Urshr(v2.V8B(), v3.V8B(), 8), "urshr v2.8b, v3.8b, #8");
  COMPARE(Urshr(v4.V16B(), v5.V16B(), 1), "urshr v4.16b, v5.16b, #1");
  COMPARE(Urshr(v6.V16B(), v7.V16B(), 8), "urshr v6.16b, v7.16b, #8");
  COMPARE(Urshr(v8_.V4H(), v9.V4H(), 1), "urshr v8.4h, v9.4h, #1");
  COMPARE(Urshr(v10.V4H(), v11.V4H(), 16), "urshr v10.4h, v11.4h, #16");
  COMPARE(Urshr(v12.V8H(), v13.V8H(), 1), "urshr v12.8h, v13.8h, #1");
  COMPARE(Urshr(v14.V8H(), v15.V8H(), 16), "urshr v14.8h, v15.8h, #16");
  COMPARE(Urshr(v16.V2S(), v17.V2S(), 1), "urshr v16.2s, v17.2s, #1");
  COMPARE(Urshr(v18.V2S(), v19.V2S(), 32), "urshr v18.2s, v19.2s, #32");
  COMPARE(Urshr(v20.V4S(), v21.V4S(), 1), "urshr v20.4s, v21.4s, #1");
  COMPARE(Urshr(v22.V4S(), v23.V4S(), 32), "urshr v22.4s, v23.4s, #32");
  COMPARE(Urshr(v28.V2D(), v29.V2D(), 1), "urshr v28.2d, v29.2d, #1");
  COMPARE(Urshr(v30.V2D(), v31.V2D(), 64), "urshr v30.2d, v31.2d, #64");
  COMPARE(Urshr(d0, d1, 7), "urshr d0, d1, #7");

  COMPARE(Srsra(v0.V8B(), v1.V8B(), 1), "srsra v0.8b, v1.8b, #1");
  COMPARE(Srsra(v2.V8B(), v3.V8B(), 8), "srsra v2.8b, v3.8b, #8");
  COMPARE(Srsra(v4.V16B(), v5.V16B(), 1), "srsra v4.16b, v5.16b, #1");
  COMPARE(Srsra(v6.V16B(), v7.V16B(), 8), "srsra v6.16b, v7.16b, #8");
  COMPARE(Srsra(v8_.V4H(), v9.V4H(), 1), "srsra v8.4h, v9.4h, #1");
  COMPARE(Srsra(v10.V4H(), v11.V4H(), 16), "srsra v10.4h, v11.4h, #16");
  COMPARE(Srsra(v12.V8H(), v13.V8H(), 1), "srsra v12.8h, v13.8h, #1");
  COMPARE(Srsra(v14.V8H(), v15.V8H(), 16), "srsra v14.8h, v15.8h, #16");
  COMPARE(Srsra(v16.V2S(), v17.V2S(), 1), "srsra v16.2s, v17.2s, #1");
  COMPARE(Srsra(v18.V2S(), v19.V2S(), 32), "srsra v18.2s, v19.2s, #32");
  COMPARE(Srsra(v20.V4S(), v21.V4S(), 1), "srsra v20.4s, v21.4s, #1");
  COMPARE(Srsra(v22.V4S(), v23.V4S(), 32), "srsra v22.4s, v23.4s, #32");
  COMPARE(Srsra(v28.V2D(), v29.V2D(), 1), "srsra v28.2d, v29.2d, #1");
  COMPARE(Srsra(v30.V2D(), v31.V2D(), 64), "srsra v30.2d, v31.2d, #64");
  COMPARE(Srsra(d0, d1, 7), "srsra d0, d1, #7");

  COMPARE(Ssra(v0.V8B(), v1.V8B(), 1), "ssra v0.8b, v1.8b, #1");
  COMPARE(Ssra(v2.V8B(), v3.V8B(), 8), "ssra v2.8b, v3.8b, #8");
  COMPARE(Ssra(v4.V16B(), v5.V16B(), 1), "ssra v4.16b, v5.16b, #1");
  COMPARE(Ssra(v6.V16B(), v7.V16B(), 8), "ssra v6.16b, v7.16b, #8");
  COMPARE(Ssra(v8_.V4H(), v9.V4H(), 1), "ssra v8.4h, v9.4h, #1");
  COMPARE(Ssra(v10.V4H(), v11.V4H(), 16), "ssra v10.4h, v11.4h, #16");
  COMPARE(Ssra(v12.V8H(), v13.V8H(), 1), "ssra v12.8h, v13.8h, #1");
  COMPARE(Ssra(v14.V8H(), v15.V8H(), 16), "ssra v14.8h, v15.8h, #16");
  COMPARE(Ssra(v16.V2S(), v17.V2S(), 1), "ssra v16.2s, v17.2s, #1");
  COMPARE(Ssra(v18.V2S(), v19.V2S(), 32), "ssra v18.2s, v19.2s, #32");
  COMPARE(Ssra(v20.V4S(), v21.V4S(), 1), "ssra v20.4s, v21.4s, #1");
  COMPARE(Ssra(v22.V4S(), v23.V4S(), 32), "ssra v22.4s, v23.4s, #32");
  COMPARE(Ssra(v28.V2D(), v29.V2D(), 1), "ssra v28.2d, v29.2d, #1");
  COMPARE(Ssra(v30.V2D(), v31.V2D(), 64), "ssra v30.2d, v31.2d, #64");
  COMPARE(Ssra(d0, d1, 7), "ssra d0, d1, #7");

  COMPARE(Ursra(v0.V8B(), v1.V8B(), 1), "ursra v0.8b, v1.8b, #1");
  COMPARE(Ursra(v2.V8B(), v3.V8B(), 8), "ursra v2.8b, v3.8b, #8");
  COMPARE(Ursra(v4.V16B(), v5.V16B(), 1), "ursra v4.16b, v5.16b, #1");
  COMPARE(Ursra(v6.V16B(), v7.V16B(), 8), "ursra v6.16b, v7.16b, #8");
  COMPARE(Ursra(v8_.V4H(), v9.V4H(), 1), "ursra v8.4h, v9.4h, #1");
  COMPARE(Ursra(v10.V4H(), v11.V4H(), 16), "ursra v10.4h, v11.4h, #16");
  COMPARE(Ursra(v12.V8H(), v13.V8H(), 1), "ursra v12.8h, v13.8h, #1");
  COMPARE(Ursra(v14.V8H(), v15.V8H(), 16), "ursra v14.8h, v15.8h, #16");
  COMPARE(Ursra(v16.V2S(), v17.V2S(), 1), "ursra v16.2s, v17.2s, #1");
  COMPARE(Ursra(v18.V2S(), v19.V2S(), 32), "ursra v18.2s, v19.2s, #32");
  COMPARE(Ursra(v20.V4S(), v21.V4S(), 1), "ursra v20.4s, v21.4s, #1");
  COMPARE(Ursra(v22.V4S(), v23.V4S(), 32), "ursra v22.4s, v23.4s, #32");
  COMPARE(Ursra(v28.V2D(), v29.V2D(), 1), "ursra v28.2d, v29.2d, #1");
  COMPARE(Ursra(v30.V2D(), v31.V2D(), 64), "ursra v30.2d, v31.2d, #64");
  COMPARE(Ursra(d0, d1, 7), "ursra d0, d1, #7");

  COMPARE(Usra(v0.V8B(), v1.V8B(), 1), "usra v0.8b, v1.8b, #1");
  COMPARE(Usra(v2.V8B(), v3.V8B(), 8), "usra v2.8b, v3.8b, #8");
  COMPARE(Usra(v4.V16B(), v5.V16B(), 1), "usra v4.16b, v5.16b, #1");
  COMPARE(Usra(v6.V16B(), v7.V16B(), 8), "usra v6.16b, v7.16b, #8");
  COMPARE(Usra(v8_.V4H(), v9.V4H(), 1), "usra v8.4h, v9.4h, #1");
  COMPARE(Usra(v10.V4H(), v11.V4H(), 16), "usra v10.4h, v11.4h, #16");
  COMPARE(Usra(v12.V8H(), v13.V8H(), 1), "usra v12.8h, v13.8h, #1");
  COMPARE(Usra(v14.V8H(), v15.V8H(), 16), "usra v14.8h, v15.8h, #16");
  COMPARE(Usra(v16.V2S(), v17.V2S(), 1), "usra v16.2s, v17.2s, #1");
  COMPARE(Usra(v18.V2S(), v19.V2S(), 32), "usra v18.2s, v19.2s, #32");
  COMPARE(Usra(v20.V4S(), v21.V4S(), 1), "usra v20.4s, v21.4s, #1");
  COMPARE(Usra(v22.V4S(), v23.V4S(), 32), "usra v22.4s, v23.4s, #32");
  COMPARE(Usra(v28.V2D(), v29.V2D(), 1), "usra v28.2d, v29.2d, #1");
  COMPARE(Usra(v30.V2D(), v31.V2D(), 64), "usra v30.2d, v31.2d, #64");
  COMPARE(Usra(d0, d1, 7), "usra d0, d1, #7");

  COMPARE(Sli(v1.V8B(), v8_.V8B(), 1), "sli v1.8b, v8.8b, #1");
  COMPARE(Sli(v2.V16B(), v9.V16B(), 2), "sli v2.16b, v9.16b, #2");
  COMPARE(Sli(v3.V4H(), v1.V4H(), 3), "sli v3.4h, v1.4h, #3");
  COMPARE(Sli(v4.V8H(), v2.V8H(), 4), "sli v4.8h, v2.8h, #4");
  COMPARE(Sli(v5.V2S(), v3.V2S(), 5), "sli v5.2s, v3.2s, #5");
  COMPARE(Sli(v6.V4S(), v4.V4S(), 6), "sli v6.4s, v4.4s, #6");
  COMPARE(Sli(v7.V2D(), v5.V2D(), 7), "sli v7.2d, v5.2d, #7");
  COMPARE(Sli(d8, d6, 8), "sli d8, d6, #8");

  COMPARE(Shl(v1.V8B(), v8_.V8B(), 1), "shl v1.8b, v8.8b, #1");
  COMPARE(Shl(v2.V16B(), v9.V16B(), 2), "shl v2.16b, v9.16b, #2");
  COMPARE(Shl(v3.V4H(), v1.V4H(), 3), "shl v3.4h, v1.4h, #3");
  COMPARE(Shl(v4.V8H(), v2.V8H(), 4), "shl v4.8h, v2.8h, #4");
  COMPARE(Shl(v5.V2S(), v3.V2S(), 5), "shl v5.2s, v3.2s, #5");
  COMPARE(Shl(v6.V4S(), v4.V4S(), 6), "shl v6.4s, v4.4s, #6");
  COMPARE(Shl(v7.V2D(), v5.V2D(), 7), "shl v7.2d, v5.2d, #7");
  COMPARE(Shl(d8, d6, 8), "shl d8, d6, #8");

  COMPARE(Sqshl(v1.V8B(), v8_.V8B(), 1), "sqshl v1.8b, v8.8b, #1");
  COMPARE(Sqshl(v2.V16B(), v9.V16B(), 2), "sqshl v2.16b, v9.16b, #2");
  COMPARE(Sqshl(v3.V4H(), v1.V4H(), 3), "sqshl v3.4h, v1.4h, #3");
  COMPARE(Sqshl(v4.V8H(), v2.V8H(), 4), "sqshl v4.8h, v2.8h, #4");
  COMPARE(Sqshl(v5.V2S(), v3.V2S(), 5), "sqshl v5.2s, v3.2s, #5");
  COMPARE(Sqshl(v6.V4S(), v4.V4S(), 6), "sqshl v6.4s, v4.4s, #6");
  COMPARE(Sqshl(v7.V2D(), v5.V2D(), 7), "sqshl v7.2d, v5.2d, #7");
  COMPARE(Sqshl(b8, b7, 1), "sqshl b8, b7, #1");
  COMPARE(Sqshl(h9, h8, 2), "sqshl h9, h8, #2");
  COMPARE(Sqshl(s10, s9, 3), "sqshl s10, s9, #3");
  COMPARE(Sqshl(d11, d10, 4), "sqshl d11, d10, #4");

  COMPARE(Sqshlu(v1.V8B(), v8_.V8B(), 1), "sqshlu v1.8b, v8.8b, #1");
  COMPARE(Sqshlu(v2.V16B(), v9.V16B(), 2), "sqshlu v2.16b, v9.16b, #2");
  COMPARE(Sqshlu(v3.V4H(), v1.V4H(), 3), "sqshlu v3.4h, v1.4h, #3");
  COMPARE(Sqshlu(v4.V8H(), v2.V8H(), 4), "sqshlu v4.8h, v2.8h, #4");
  COMPARE(Sqshlu(v5.V2S(), v3.V2S(), 5), "sqshlu v5.2s, v3.2s, #5");
  COMPARE(Sqshlu(v6.V4S(), v4.V4S(), 6), "sqshlu v6.4s, v4.4s, #6");
  COMPARE(Sqshlu(v7.V2D(), v5.V2D(), 7), "sqshlu v7.2d, v5.2d, #7");
  COMPARE(Sqshlu(b8, b7, 1), "sqshlu b8, b7, #1");
  COMPARE(Sqshlu(h9, h8, 2), "sqshlu h9, h8, #2");
  COMPARE(Sqshlu(s10, s9, 3), "sqshlu s10, s9, #3");
  COMPARE(Sqshlu(d11, d10, 4), "sqshlu d11, d10, #4");

  COMPARE(Uqshl(v1.V8B(), v8_.V8B(), 1), "uqshl v1.8b, v8.8b, #1");
  COMPARE(Uqshl(v2.V16B(), v9.V16B(), 2), "uqshl v2.16b, v9.16b, #2");
  COMPARE(Uqshl(v3.V4H(), v1.V4H(), 3), "uqshl v3.4h, v1.4h, #3");
  COMPARE(Uqshl(v4.V8H(), v2.V8H(), 4), "uqshl v4.8h, v2.8h, #4");
  COMPARE(Uqshl(v5.V2S(), v3.V2S(), 5), "uqshl v5.2s, v3.2s, #5");
  COMPARE(Uqshl(v6.V4S(), v4.V4S(), 6), "uqshl v6.4s, v4.4s, #6");
  COMPARE(Uqshl(v7.V2D(), v5.V2D(), 7), "uqshl v7.2d, v5.2d, #7");
  COMPARE(Uqshl(b8, b7, 1), "uqshl b8, b7, #1");
  COMPARE(Uqshl(h9, h8, 2), "uqshl h9, h8, #2");
  COMPARE(Uqshl(s10, s9, 3), "uqshl s10, s9, #3");
  COMPARE(Uqshl(d11, d10, 4), "uqshl d11, d10, #4");

  COMPARE(Sshll(v1.V8H(), v8_.V8B(), 1), "sshll v1.8h, v8.8b, #1");
  COMPARE(Sshll(v3.V4S(), v1.V4H(), 3), "sshll v3.4s, v1.4h, #3");
  COMPARE(Sshll(v5.V2D(), v3.V2S(), 5), "sshll v5.2d, v3.2s, #5");
  COMPARE(Sshll2(v2.V8H(), v9.V16B(), 2), "sshll2 v2.8h, v9.16b, #2");
  COMPARE(Sshll2(v4.V4S(), v2.V8H(), 4), "sshll2 v4.4s, v2.8h, #4");
  COMPARE(Sshll2(v6.V2D(), v4.V4S(), 6), "sshll2 v6.2d, v4.4s, #6");

  COMPARE(Sshll(v1.V8H(), v8_.V8B(), 0), "sxtl v1.8h, v8.8b");
  COMPARE(Sshll(v3.V4S(), v1.V4H(), 0), "sxtl v3.4s, v1.4h");
  COMPARE(Sshll(v5.V2D(), v3.V2S(), 0), "sxtl v5.2d, v3.2s");
  COMPARE(Sshll2(v2.V8H(), v9.V16B(), 0), "sxtl2 v2.8h, v9.16b");
  COMPARE(Sshll2(v4.V4S(), v2.V8H(), 0), "sxtl2 v4.4s, v2.8h");
  COMPARE(Sshll2(v6.V2D(), v4.V4S(), 0), "sxtl2 v6.2d, v4.4s");

  COMPARE(Sxtl(v1.V8H(), v8_.V8B()), "sxtl v1.8h, v8.8b");
  COMPARE(Sxtl(v3.V4S(), v1.V4H()), "sxtl v3.4s, v1.4h");
  COMPARE(Sxtl(v5.V2D(), v3.V2S()), "sxtl v5.2d, v3.2s");
  COMPARE(Sxtl2(v2.V8H(), v9.V16B()), "sxtl2 v2.8h, v9.16b");
  COMPARE(Sxtl2(v4.V4S(), v2.V8H()), "sxtl2 v4.4s, v2.8h");
  COMPARE(Sxtl2(v6.V2D(), v4.V4S()), "sxtl2 v6.2d, v4.4s");

  COMPARE(Ushll(v1.V8H(), v8_.V8B(), 1), "ushll v1.8h, v8.8b, #1");
  COMPARE(Ushll(v3.V4S(), v1.V4H(), 3), "ushll v3.4s, v1.4h, #3");
  COMPARE(Ushll(v5.V2D(), v3.V2S(), 5), "ushll v5.2d, v3.2s, #5");
  COMPARE(Ushll2(v2.V8H(), v9.V16B(), 2), "ushll2 v2.8h, v9.16b, #2");
  COMPARE(Ushll2(v4.V4S(), v2.V8H(), 4), "ushll2 v4.4s, v2.8h, #4");
  COMPARE(Ushll2(v6.V2D(), v4.V4S(), 6), "ushll2 v6.2d, v4.4s, #6");

  COMPARE(Ushll(v1.V8H(), v8_.V8B(), 0), "uxtl v1.8h, v8.8b");
  COMPARE(Ushll(v3.V4S(), v1.V4H(), 0), "uxtl v3.4s, v1.4h");
  COMPARE(Ushll(v5.V2D(), v3.V2S(), 0), "uxtl v5.2d, v3.2s");
  COMPARE(Ushll2(v2.V8H(), v9.V16B(), 0), "uxtl2 v2.8h, v9.16b");
  COMPARE(Ushll2(v4.V4S(), v2.V8H(), 0), "uxtl2 v4.4s, v2.8h");
  COMPARE(Ushll2(v6.V2D(), v4.V4S(), 0), "uxtl2 v6.2d, v4.4s");

  COMPARE(Uxtl(v1.V8H(), v8_.V8B()), "uxtl v1.8h, v8.8b");
  COMPARE(Uxtl(v3.V4S(), v1.V4H()), "uxtl v3.4s, v1.4h");
  COMPARE(Uxtl(v5.V2D(), v3.V2S()), "uxtl v5.2d, v3.2s");
  COMPARE(Uxtl2(v2.V8H(), v9.V16B()), "uxtl2 v2.8h, v9.16b");
  COMPARE(Uxtl2(v4.V4S(), v2.V8H()), "uxtl2 v4.4s, v2.8h");
  COMPARE(Uxtl2(v6.V2D(), v4.V4S()), "uxtl2 v6.2d, v4.4s");

  COMPARE(Sri(v1.V8B(), v8_.V8B(), 1), "sri v1.8b, v8.8b, #1");
  COMPARE(Sri(v2.V16B(), v9.V16B(), 2), "sri v2.16b, v9.16b, #2");
  COMPARE(Sri(v3.V4H(), v1.V4H(), 3), "sri v3.4h, v1.4h, #3");
  COMPARE(Sri(v4.V8H(), v2.V8H(), 4), "sri v4.8h, v2.8h, #4");
  COMPARE(Sri(v5.V2S(), v3.V2S(), 5), "sri v5.2s, v3.2s, #5");
  COMPARE(Sri(v6.V4S(), v4.V4S(), 6), "sri v6.4s, v4.4s, #6");
  COMPARE(Sri(v7.V2D(), v5.V2D(), 7), "sri v7.2d, v5.2d, #7");
  COMPARE(Sri(d8, d6, 8), "sri d8, d6, #8");

  COMPARE(Shrn(v0.V8B(), v1.V8H(), 1), "shrn v0.8b, v1.8h, #1");
  COMPARE(Shrn(v1.V4H(), v2.V4S(), 2), "shrn v1.4h, v2.4s, #2");
  COMPARE(Shrn(v2.V2S(), v3.V2D(), 3), "shrn v2.2s, v3.2d, #3");
  COMPARE(Shrn2(v0.V16B(), v1.V8H(), 4), "shrn2 v0.16b, v1.8h, #4");
  COMPARE(Shrn2(v1.V8H(), v2.V4S(), 5), "shrn2 v1.8h, v2.4s, #5");
  COMPARE(Shrn2(v2.V4S(), v3.V2D(), 6), "shrn2 v2.4s, v3.2d, #6");

  COMPARE(Rshrn(v0.V8B(), v1.V8H(), 1), "rshrn v0.8b, v1.8h, #1");
  COMPARE(Rshrn(v1.V4H(), v2.V4S(), 2), "rshrn v1.4h, v2.4s, #2");
  COMPARE(Rshrn(v2.V2S(), v3.V2D(), 3), "rshrn v2.2s, v3.2d, #3");
  COMPARE(Rshrn2(v0.V16B(), v1.V8H(), 4), "rshrn2 v0.16b, v1.8h, #4");
  COMPARE(Rshrn2(v1.V8H(), v2.V4S(), 5), "rshrn2 v1.8h, v2.4s, #5");
  COMPARE(Rshrn2(v2.V4S(), v3.V2D(), 6), "rshrn2 v2.4s, v3.2d, #6");

  COMPARE(Uqshrn(v0.V8B(), v1.V8H(), 1), "uqshrn v0.8b, v1.8h, #1");
  COMPARE(Uqshrn(v1.V4H(), v2.V4S(), 2), "uqshrn v1.4h, v2.4s, #2");
  COMPARE(Uqshrn(v2.V2S(), v3.V2D(), 3), "uqshrn v2.2s, v3.2d, #3");
  COMPARE(Uqshrn2(v0.V16B(), v1.V8H(), 4), "uqshrn2 v0.16b, v1.8h, #4");
  COMPARE(Uqshrn2(v1.V8H(), v2.V4S(), 5), "uqshrn2 v1.8h, v2.4s, #5");
  COMPARE(Uqshrn2(v2.V4S(), v3.V2D(), 6), "uqshrn2 v2.4s, v3.2d, #6");
  COMPARE(Uqshrn(b0, h1, 1), "uqshrn b0, h1, #1");
  COMPARE(Uqshrn(h1, s2, 2), "uqshrn h1, s2, #2");
  COMPARE(Uqshrn(s2, d3, 3), "uqshrn s2, d3, #3");

  COMPARE(Uqrshrn(v0.V8B(), v1.V8H(), 1), "uqrshrn v0.8b, v1.8h, #1");
  COMPARE(Uqrshrn(v1.V4H(), v2.V4S(), 2), "uqrshrn v1.4h, v2.4s, #2");
  COMPARE(Uqrshrn(v2.V2S(), v3.V2D(), 3), "uqrshrn v2.2s, v3.2d, #3");
  COMPARE(Uqrshrn2(v0.V16B(), v1.V8H(), 4), "uqrshrn2 v0.16b, v1.8h, #4");
  COMPARE(Uqrshrn2(v1.V8H(), v2.V4S(), 5), "uqrshrn2 v1.8h, v2.4s, #5");
  COMPARE(Uqrshrn2(v2.V4S(), v3.V2D(), 6), "uqrshrn2 v2.4s, v3.2d, #6");
  COMPARE(Uqrshrn(b0, h1, 1), "uqrshrn b0, h1, #1");
  COMPARE(Uqrshrn(h1, s2, 2), "uqrshrn h1, s2, #2");
  COMPARE(Uqrshrn(s2, d3, 3), "uqrshrn s2, d3, #3");

  COMPARE(Sqshrn(v0.V8B(), v1.V8H(), 1), "sqshrn v0.8b, v1.8h, #1");
  COMPARE(Sqshrn(v1.V4H(), v2.V4S(), 2), "sqshrn v1.4h, v2.4s, #2");
  COMPARE(Sqshrn(v2.V2S(), v3.V2D(), 3), "sqshrn v2.2s, v3.2d, #3");
  COMPARE(Sqshrn2(v0.V16B(), v1.V8H(), 4), "sqshrn2 v0.16b, v1.8h, #4");
  COMPARE(Sqshrn2(v1.V8H(), v2.V4S(), 5), "sqshrn2 v1.8h, v2.4s, #5");
  COMPARE(Sqshrn2(v2.V4S(), v3.V2D(), 6), "sqshrn2 v2.4s, v3.2d, #6");
  COMPARE(Sqshrn(b0, h1, 1), "sqshrn b0, h1, #1");
  COMPARE(Sqshrn(h1, s2, 2), "sqshrn h1, s2, #2");
  COMPARE(Sqshrn(s2, d3, 3), "sqshrn s2, d3, #3");

  COMPARE(Sqrshrn(v0.V8B(), v1.V8H(), 1), "sqrshrn v0.8b, v1.8h, #1");
  COMPARE(Sqrshrn(v1.V4H(), v2.V4S(), 2), "sqrshrn v1.4h, v2.4s, #2");
  COMPARE(Sqrshrn(v2.V2S(), v3.V2D(), 3), "sqrshrn v2.2s, v3.2d, #3");
  COMPARE(Sqrshrn2(v0.V16B(), v1.V8H(), 4), "sqrshrn2 v0.16b, v1.8h, #4");
  COMPARE(Sqrshrn2(v1.V8H(), v2.V4S(), 5), "sqrshrn2 v1.8h, v2.4s, #5");
  COMPARE(Sqrshrn2(v2.V4S(), v3.V2D(), 6), "sqrshrn2 v2.4s, v3.2d, #6");
  COMPARE(Sqrshrn(b0, h1, 1), "sqrshrn b0, h1, #1");
  COMPARE(Sqrshrn(h1, s2, 2), "sqrshrn h1, s2, #2");
  COMPARE(Sqrshrn(s2, d3, 3), "sqrshrn s2, d3, #3");

  COMPARE(Sqshrun(v0.V8B(), v1.V8H(), 1), "sqshrun v0.8b, v1.8h, #1");
  COMPARE(Sqshrun(v1.V4H(), v2.V4S(), 2), "sqshrun v1.4h, v2.4s, #2");
  COMPARE(Sqshrun(v2.V2S(), v3.V2D(), 3), "sqshrun v2.2s, v3.2d, #3");
  COMPARE(Sqshrun2(v0.V16B(), v1.V8H(), 4), "sqshrun2 v0.16b, v1.8h, #4");
  COMPARE(Sqshrun2(v1.V8H(), v2.V4S(), 5), "sqshrun2 v1.8h, v2.4s, #5");
  COMPARE(Sqshrun2(v2.V4S(), v3.V2D(), 6), "sqshrun2 v2.4s, v3.2d, #6");
  COMPARE(Sqshrun(b0, h1, 1), "sqshrun b0, h1, #1");
  COMPARE(Sqshrun(h1, s2, 2), "sqshrun h1, s2, #2");
  COMPARE(Sqshrun(s2, d3, 3), "sqshrun s2, d3, #3");

  COMPARE(Sqrshrun(v0.V8B(), v1.V8H(), 1), "sqrshrun v0.8b, v1.8h, #1");
  COMPARE(Sqrshrun(v1.V4H(), v2.V4S(), 2), "sqrshrun v1.4h, v2.4s, #2");
  COMPARE(Sqrshrun(v2.V2S(), v3.V2D(), 3), "sqrshrun v2.2s, v3.2d, #3");
  COMPARE(Sqrshrun2(v0.V16B(), v1.V8H(), 4), "sqrshrun2 v0.16b, v1.8h, #4");
  COMPARE(Sqrshrun2(v1.V8H(), v2.V4S(), 5), "sqrshrun2 v1.8h, v2.4s, #5");
  COMPARE(Sqrshrun2(v2.V4S(), v3.V2D(), 6), "sqrshrun2 v2.4s, v3.2d, #6");
  COMPARE(Sqrshrun(b0, h1, 1), "sqrshrun b0, h1, #1");
  COMPARE(Sqrshrun(h1, s2, 2), "sqrshrun h1, s2, #2");
  COMPARE(Sqrshrun(s2, d3, 3), "sqrshrun s2, d3, #3");

  COMPARE(Scvtf(v5.V2S(), v3.V2S(), 11), "scvtf v5.2s, v3.2s, #11");
  COMPARE(Scvtf(v6.V4S(), v4.V4S(), 12), "scvtf v6.4s, v4.4s, #12");
  COMPARE(Scvtf(v7.V2D(), v5.V2D(), 33), "scvtf v7.2d, v5.2d, #33");
  COMPARE(Scvtf(s8, s6, 13), "scvtf s8, s6, #13");
  COMPARE(Scvtf(d8, d6, 34), "scvtf d8, d6, #34");

  COMPARE(Ucvtf(v5.V2S(), v3.V2S(), 11), "ucvtf v5.2s, v3.2s, #11");
  COMPARE(Ucvtf(v6.V4S(), v4.V4S(), 12), "ucvtf v6.4s, v4.4s, #12");
  COMPARE(Ucvtf(v7.V2D(), v5.V2D(), 33), "ucvtf v7.2d, v5.2d, #33");
  COMPARE(Ucvtf(s8, s6, 13), "ucvtf s8, s6, #13");
  COMPARE(Ucvtf(d8, d6, 34), "ucvtf d8, d6, #34");

  COMPARE(Fcvtzs(v5.V2S(), v3.V2S(), 11), "fcvtzs v5.2s, v3.2s, #11");
  COMPARE(Fcvtzs(v6.V4S(), v4.V4S(), 12), "fcvtzs v6.4s, v4.4s, #12");
  COMPARE(Fcvtzs(v7.V2D(), v5.V2D(), 33), "fcvtzs v7.2d, v5.2d, #33");
  COMPARE(Fcvtzs(s8, s6, 13), "fcvtzs s8, s6, #13");
  COMPARE(Fcvtzs(d8, d6, 34), "fcvtzs d8, d6, #34");

  COMPARE(Fcvtzu(v5.V2S(), v3.V2S(), 11), "fcvtzu v5.2s, v3.2s, #11");
  COMPARE(Fcvtzu(v6.V4S(), v4.V4S(), 12), "fcvtzu v6.4s, v4.4s, #12");
  COMPARE(Fcvtzu(v7.V2D(), v5.V2D(), 33), "fcvtzu v7.2d, v5.2d, #33");
  COMPARE(Fcvtzu(s8, s6, 13), "fcvtzu s8, s6, #13");
  COMPARE(Fcvtzu(d8, d6, 34), "fcvtzu d8, d6, #34");

  CLEANUP();
}

#undef TEST_
#undef EXP_SIZE
#undef INSTR_SIZE
#undef SET_UP_MASM
#undef SET_UP_ASM
#undef COMPARE
#undef COMPARE_PREFIX
#undef CLEANUP
#undef VLIST2
#undef VLIST3
#undef VLIST4
#undef NEON_FORMAT_LIST
#undef NEON_FORMAT_LIST_LP
#undef NEON_FORMAT_LIST_LW
#undef NEON_FORMAT_LIST_LW2
#undef NEON_FORMAT_LIST_BHS
#undef NEON_FORMAT_LIST_HS
#undef NEON_FORMAT_LIST_FP
#undef NEON_SCALAR_FORMAT_LIST

}  // namespace internal
}  // namespace v8
