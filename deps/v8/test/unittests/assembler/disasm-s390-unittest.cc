// Copyright 2014 the V8 project authors. All rights reserved.
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
//

#include <stdlib.h>

#include "src/codegen/macro-assembler.h"
#include "src/debug/debug.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/frames-inl.h"
#include "src/init/v8.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using DisasmS390Test = TestWithIsolate;

bool DisassembleAndCompare(uint8_t* pc, const char* compare_string) {
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  base::EmbeddedVector<char, 128> disasm_buffer;

  disasm.InstructionDecode(disasm_buffer, pc);

  if (strcmp(compare_string, disasm_buffer.begin()) != 0) {
    fprintf(stderr,
            "expected: \n"
            "%s\n"
            "disassembled: \n"
            "%s\n\n",
            compare_string, disasm_buffer.begin());
    return false;
  }
  return true;
}

// Set up V8 to a state where we can at least run the assembler and
// disassembler. Declare the variables and allocate the data structures used
// in the rest of the macros.
#define SET_UP()                                                  \
  HandleScope scope(isolate());                                   \
  uint8_t* buffer = reinterpret_cast<uint8_t*>(malloc(4 * 1024)); \
  Assembler assm(AssemblerOptions{},                              \
                 ExternalAssemblerBuffer(buffer, 4 * 1024));      \
  bool failure = false;

// This macro assembles one instruction using the preallocated assembler and
// disassembles the generated instruction, comparing the output to the expected
// value. If the comparison fails an error message is printed, but the test
// continues to run until the end.
#define COMPARE(asm_, compare_string)                                        \
  {                                                                          \
    int pc_offset = assm.pc_offset();                                        \
    uint8_t* progcounter = &buffer[pc_offset];                               \
    assm.asm_;                                                               \
    if (!DisassembleAndCompare(progcounter, compare_string)) failure = true; \
  }

// Force emission of any pending literals into a pool.
#define EMIT_PENDING_LITERALS() assm.CheckConstPool(true, false)

// Verify that all invocations of the COMPARE macro passed successfully.
// Exit with a failure if at least one of the tests failed.
#define VERIFY_RUN()                            \
  if (failure) {                                \
    FATAL("S390 Disassembler tests failed.\n"); \
  }

TEST_F(DisasmS390Test, TwoBytes) {
  SET_UP();

  COMPARE(ar(r3, r10), "1a3a           ar\tr3,r10");
  COMPARE(sr(r8, ip), "1b8c           sr\tr8,ip");
  COMPARE(mr_z(r0, r6), "1c06           mr\tr0,r6");
  COMPARE(dr(r0, r5), "1d05           dr\tr0,r5");
  COMPARE(or_z(r4, r2), "1642           or\tr4,r2");
  COMPARE(nr(fp, r9), "14b9           nr\tfp,r9");
  COMPARE(xr(r10, ip), "17ac           xr\tr10,ip");
  COMPARE(lr(r2, r13), "182d           lr\tr2,r13");
  COMPARE(cr_z(r9, r3), "1993           cr\tr9,r3");
  COMPARE(clr(sp, r4), "15f4           clr\tsp,r4");
  COMPARE(bcr(eq, r8), "0788           bcr\t0x8,r8");
  COMPARE(ltr(r10, r1), "12a1           ltr\tr10,r1");
  COMPARE(alr(r6, r8), "1e68           alr\tr6,r8");
  COMPARE(slr(r3, ip), "1f3c           slr\tr3,ip");
  COMPARE(lnr(r4, r1), "1141           lnr\tr4,r1");
  COMPARE(lcr(r0, r3), "1303           lcr\tr0,r3");
  COMPARE(basr(r14, r7), "0de7           basr\tr14,r7");
  COMPARE(ldr(d4, d6), "2846           ldr\td4,d6");

  VERIFY_RUN();
}

TEST_F(DisasmS390Test, FourBytes) {
  SET_UP();

  COMPARE(aghi(r5, Operand(1)), "a75b0001       aghi\tr5,1");
  COMPARE(lghi(r6, Operand(8)), "a7690008       lghi\tr6,8");
  COMPARE(mghi(r1, Operand(2)), "a71d0002       mghi\tr1,2");
  COMPARE(cghi(r3, Operand(7)), "a73f0007       cghi\tr3,7");
  COMPARE(iihh(r10, Operand(8)), "a5a00008       iihh\tr10,8");
  COMPARE(iihl(r9, Operand(10)), "a591000a       iihl\tr9,10");
  COMPARE(iilh(r0, Operand(40)), "a5020028       iilh\tr0,40");
  COMPARE(iill(r6, Operand(19)), "a5630013       iill\tr6,19");
  COMPARE(oill(r9, Operand(9)), "a59b0009       oill\tr9,9");
  COMPARE(tmll(r4, Operand(7)), "a7410007       tmll\tr4,7");
  COMPARE(stm(r2, r5, MemOperand(r9, 44)), "9025902c       stm\tr2,r5,44(r9)");
  COMPARE(lm(r8, r0, MemOperand(sp, 88)), "9880f058       lm\tr8,r0,88(sp)");
  COMPARE(nill(r7, Operand(30)), "a577001e       nill\tr7,30");
  COMPARE(nilh(r8, Operand(4)), "a5860004       nilh\tr8,4");
  COMPARE(ah(r9, MemOperand(r5, r4, 4)), "4a954004       ah\tr9,4(r5,r4)");
  COMPARE(sh(r8, MemOperand(r1, r2, 6)), "4b812006       sh\tr8,6(r1,r2)");
  COMPARE(mh(r5, MemOperand(r9, r8, 7)), "4c598007       mh\tr5,7(r9,r8)");

  VERIFY_RUN();
}

TEST_F(DisasmS390Test, SixBytes) {
  SET_UP();

  COMPARE(llihf(ip, Operand(90000)), "c0ce00015f90   llihf\tip,90000");
  COMPARE(agsi(MemOperand(r9, 1000), Operand(70)),
          "eb4693e8007a   agsi\t1000(r9),70");
  COMPARE(clgfi(r7, Operand(80)), "c27e00000050   clgfi\tr7,80");
  COMPARE(cgfi(r8, Operand(10)), "c28c0000000a   cgfi\tr8,10");
  COMPARE(xihf(fp, Operand(8)), "c0b600000008   xihf\tfp,8");
  COMPARE(sllg(r0, r1, r2), "eb012000000d   sllg\tr0,r1,0(r2)");
  COMPARE(sllg(r0, r1, Operand(10)), "eb01000a000d   sllg\tr0,r1,10(r0)");
  COMPARE(srlg(r1, r3, Operand(10)), "eb13000a000c   srlg\tr1,r3,10(r0)");
  COMPARE(srlg(r1, r3, r10), "eb13a000000c   srlg\tr1,r3,0(r10)");
  COMPARE(slag(r1, r3, Operand(2)), "eb130002000b   slag\tr1,r3,2(r0)");
  COMPARE(slag(r1, r3, r2), "eb132000000b   slag\tr1,r3,0(r2)");
  COMPARE(srag(r1, r3, r2), "eb132000000a   srag\tr1,r3,0(r2)");
  COMPARE(srag(r1, r3, Operand(2)), "eb130002000a   srag\tr1,r3,2(r0)");
  COMPARE(risbg(r1, r2, Operand(3), Operand(5), Operand(2)),
          "ec1203050255   risbg\tr1,r2,3,5,2");
  COMPARE(risbgn(r1, r2, Operand(3), Operand(5), Operand(2)),
          "ec1203050259   risbgn\tr1,r2,3,5,2");
  COMPARE(stmg(r3, r4, MemOperand(sp, 10)),
          "eb34f00a0024   stmg\tr3,r4,10(sp)");
  COMPARE(ltg(r1, MemOperand(r4, sp, 10)), "e314f00a0002   ltg\tr1,10(r4,sp)");
  COMPARE(lgh(r8, MemOperand(r1, 8888)), "e38012b80215   lgh\tr8,8888(r1)");
  COMPARE(ag(r4, MemOperand(r9, r4, 2046)),
          "e34947fe0008   ag\tr4,2046(r9,r4)");
  COMPARE(agf(r1, MemOperand(r3, sp, 9)), "e313f0090018   agf\tr1,9(r3,sp)");
  COMPARE(sg(r9, MemOperand(r5, 15)), "e390500f0009   sg\tr9,15(r5)");
  COMPARE(ng(r7, MemOperand(r5, r6, 1000)),
          "e37563e80080   ng\tr7,1000(r5,r6)");
  COMPARE(og(r2, MemOperand(r8, r0, 1000)),
          "e32803e80081   og\tr2,1000(r8,r0)");
  COMPARE(xg(r9, MemOperand(r3, 8888)), "e39032b80282   xg\tr9,8888(r3)");
  COMPARE(ng(r0, MemOperand(r9, r3, 900)), "e30933840080   ng\tr0,900(r9,r3)");
  COMPARE(og(r3, MemOperand(r8, r2, 8888)),
          "e33822b80281   og\tr3,8888(r8,r2)");
  COMPARE(xg(r9, MemOperand(r3, 15)), "e390300f0082   xg\tr9,15(r3)");
  COMPARE(cg(r0, MemOperand(r5, r4, 4)), "e30540040020   cg\tr0,4(r5,r4)");
  COMPARE(lg(r1, MemOperand(r7, r8, 90)), "e317805a0004   lg\tr1,90(r7,r8)");
  COMPARE(lgf(r1, MemOperand(sp, 15)), "e310f00f0014   lgf\tr1,15(sp)");
  COMPARE(llgf(r0, MemOperand(r3, r4, 8)), "e30340080016   llgf\tr0,8(r3,r4)");
  COMPARE(alg(r8, MemOperand(r4, 11)), "e380400b000a   alg\tr8,11(r4)");
  COMPARE(slg(r1, MemOperand(r5, r6, 11)), "e315600b000b   slg\tr1,11(r5,r6)");
  COMPARE(sgf(r0, MemOperand(r4, r5, 8888)),
          "e30452b80219   sgf\tr0,8888(r4,r5)");
  COMPARE(llgh(r4, MemOperand(r1, 8000)), "e3401f400191   llgh\tr4,8000(r1)");
  COMPARE(llgc(r0, MemOperand(r4, r5, 30)),
          "e304501e0090   llgc\tr0,30(r4,r5)");
  COMPARE(lgb(r9, MemOperand(r8, r7, 10)), "e398700a0077   lgb\tr9,10(r8,r7)");
  COMPARE(stg(r0, MemOperand(r9, 10)), "e300900a0024   stg\tr0,10(r9)");
  COMPARE(mvghi(MemOperand(r7, 25), Operand(100)),
          "e54870190064   mvghi\t25(r7),100");
  COMPARE(algfi(r1, Operand(34250)), "c21a000085ca   algfi\tr1,34250");
  COMPARE(slgfi(r1, Operand(87654321)), "c21405397fb1   slgfi\tr1,87654321");
  COMPARE(nihf(r2, Operand(8888)), "c02a000022b8   nihf\tr2,8888");
  COMPARE(oihf(r6, Operand(9000)), "c06c00002328   oihf\tr6,9000");
  COMPARE(msgfi(r6, Operand(90000)), "c26000015f90   msgfi\tr6,90000");
  COMPARE(iihf(r6, Operand(9)), "c06800000009   iihf\tr6,9");
  COMPARE(srlk(r1, r3, r2), "eb13200000de   srlk\tr1,r3,0(r2)");
  COMPARE(srlk(r1, r3, Operand(2)), "eb13000200de   srlk\tr1,r3,2(r0)");
  COMPARE(lmy(r9, r10, MemOperand(r8, 100)),
          "eb9a80640098   lmy\tr9,r10,100(r8)");
  COMPARE(lmg(r7, r8, MemOperand(r9, 100)),
          "eb7890640004   lmg\tr7,r8,100(r9)");
  COMPARE(lay(fp, MemOperand(sp, 8000)), "e3b0ff400171   lay\tfp,8000(sp)");
  COMPARE(cliy(MemOperand(sp, 80), Operand(80)),
          "eb50f0500055   cliy\t80(sp),80");
  COMPARE(tmy(MemOperand(r0, 20), Operand(10)),
          "eb0a00140051   tmy\t20(r0),10");
  COMPARE(clg(r9, MemOperand(r6, r7, 19)), "e39670130021   clg\tr9,19(r6,r7)");
  COMPARE(bctg(r8, MemOperand(sp, 10)), "e380f00a0046   bctg\tr8,10(sp)");
  COMPARE(icy(r2, MemOperand(r3, 2)), "e32030020073   icy\tr2,2(r3)");
  COMPARE(mvc(MemOperand(r9, 9), MemOperand(r3, 15), Operand(10)),
          "d20a9009300f   mvc\t9(10,r9),15(r3)");
  COMPARE(nilf(r0, Operand(8000)), "c00b00001f40   nilf\tr0,8000");
  COMPARE(oilf(r9, Operand(1000)), "c09d000003e8   oilf\tr9,1000");

  VERIFY_RUN();
}

#undef SET_UP
#undef COMPARE
#undef EMIT_PENDING_LITERALS
#undef VERIFY_RUN

}  // namespace internal
}  // namespace v8
