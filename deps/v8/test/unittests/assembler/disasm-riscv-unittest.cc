// Copyright 2021 the V8 project authors. All rights reserved.
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

using DisasmRiscvTest = TestWithIsolate;

bool prev_instr_compact_branch = false;

bool DisassembleAndCompare(uint8_t* pc, const char* compare_string) {
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  base::EmbeddedVector<char, 128> disasm_buffer;

  if (prev_instr_compact_branch) {
    disasm.InstructionDecode(disasm_buffer, pc);
    pc += 4;
  }

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

#define COMPARE_PC_REL(asm_, compare_string, offset)                           \
  {                                                                            \
    int pc_offset = assm.pc_offset();                                          \
    uint8_t* progcounter = &buffer[pc_offset];                                 \
    char str_with_address[100];                                                \
    snprintf(str_with_address, sizeof(str_with_address), "%s -> %p",           \
             compare_string, static_cast<void*>(progcounter + (offset)));      \
    assm.asm_;                                                                 \
    if (!DisassembleAndCompare(progcounter, str_with_address)) failure = true; \
  }

// Verify that all invocations of the COMPARE macro passed successfully.
// Exit with a failure if at least one of the tests failed.
#define VERIFY_RUN()                             \
  if (failure) {                                 \
    FATAL("RISCV Disassembler tests failed.\n"); \
  }

TEST_F(DisasmRiscvTest, Arith) {
  SET_UP();

  // Arithmetic with immediate
  COMPARE(addi(t6, s3, -268), "ef498f93       addi      t6, s3, -268");
  COMPARE(slti(t5, s4, -268), "ef4a2f13       slti      t5, s4, -268");
  COMPARE(sltiu(t4, s5, -268), "ef4abe93       sltiu     t4, s5, -268");
  COMPARE(xori(t3, s6, static_cast<int16_t>(0xfffffef4)),
          "ef4b4e13       xori      t3, s6, 0xfffffef4");
  COMPARE(ori(s11, zero_reg, static_cast<int16_t>(0xfffffef4)),
          "ef406d93       ori       s11, zero_reg, 0xfffffef4");
  COMPARE(andi(s10, ra, static_cast<int16_t>(0xfffffef4)),
          "ef40fd13       andi      s10, ra, 0xfffffef4");
  COMPARE(slli(s9, sp, 17), "01111c93       slli      s9, sp, 17");
  COMPARE(srli(s8, gp, 17), "0111dc13       srli      s8, gp, 17");
  COMPARE(srai(s7, tp, 17), "41125b93       srai      s7, tp, 17");

  // Arithmetic
  COMPARE(add(s6, t0, t4), "01d28b33       add       s6, t0, t4");
  COMPARE(sub(s5, t1, s4), "41430ab3       sub       s5, t1, s4");
  COMPARE(sll(s4, t2, s4), "01439a33       sll       s4, t2, s4");
  COMPARE(slt(s3, fp, s4), "014429b3       slt       s3, fp, s4");
  COMPARE(sltu(s2, s3, t6), "01f9b933       sltu      s2, s3, t6");
  COMPARE(xor_(a7, s4, s4), "014a48b3       xor       a7, s4, s4");
  COMPARE(srl(a6, s5, s4), "014ad833       srl       a6, s5, s4");
  COMPARE(sra(a0, s3, s4), "4149d533       sra       a0, s3, s4");
  COMPARE(or_(a0, s3, s4), "0149e533       or        a0, s3, s4");
  COMPARE(and_(a0, s3, s4), "0149f533       and       a0, s3, s4");
  VERIFY_RUN();
}

TEST_F(DisasmRiscvTest, RVB) {
  SET_UP();

  COMPARE(sh1add(s6, t1, t5), "21e32b33       sh1add    s6, t1, t5");
  COMPARE(sh2add(s7, t1, t2), "20734bb3       sh2add    s7, t1, t2");
  COMPARE(sh3add(s4, t1, t3), "21c36a33       sh3add    s4, t1, t3");

#ifdef V8_TARGET_ARCH_RISCV64
  COMPARE(adduw(s6, t1, t5), "09e30b3b       add.uw    s6, t1, t5");
  COMPARE(zextw(s6, t1), "08030b3b       zext.w    s6, t1");
  COMPARE(sh1adduw(s6, t1, t5), "21e32b3b       sh1add.uw s6, t1, t5");
  COMPARE(sh2adduw(s7, t1, t2), "20734bbb       sh2add.uw s7, t1, t2");
  COMPARE(sh3adduw(s4, t1, t3), "21c36a3b       sh3add.uw s4, t1, t3");
  COMPARE(slliuw(s6, t1, 10), "08a31b1b       slli.uw   s6, t1, 10");
#endif

  COMPARE(andn(a0, s3, s4), "4149f533       andn      a0, s3, s4");
  COMPARE(orn(a0, s3, s4), "4149e533       orn       a0, s3, s4");
  COMPARE(xnor(a0, s3, s4), "4149c533       xnor      a0, s3, s4");

  COMPARE(clz(a0, s3), "60099513       clz       a0, s3");
  COMPARE(ctz(a0, s3), "60199513       ctz       a0, s3");
#ifdef V8_TARGET_ARCH_RISCV64
  COMPARE(clzw(a1, s3), "6009959b       clzw      a1, s3");
  COMPARE(ctzw(a0, s2), "6019151b       ctzw      a0, s2");
#endif
  COMPARE(max(a1, s3, t0), "0a59e5b3       max       a1, s3, t0");
  COMPARE(maxu(a0, s2, a1), "0ab97533       maxu      a0, s2, a1");
  COMPARE(min(a1, s3, fp), "0a89c5b3       min       a1, s3, fp");
  COMPARE(minu(a0, s2, sp), "0a295533       minu      a0, s2, sp");

  COMPARE(sextb(a0, s2), "60491513       sext.b    a0, s2");
  COMPARE(sexth(a1, s3), "60599593       sext.h    a1, s3");
#ifdef V8_TARGET_ARCH_RISCV64
  COMPARE(zexth(a0, s2), "0809453b       zext.h    a0, s2");
  COMPARE(rev8(a0, s2), "6b895513       rev8      a0, s2");
#else
  COMPARE(zexth(a0, s2), "08094533       zext.h    a0, s2");
  COMPARE(rev8(a0, s2), "69895513       rev8      a0, s2");
#endif

  COMPARE(rol(a0, s3, s4), "61499533       rol       a0, s3, s4");
  COMPARE(ror(a0, s3, s4), "6149d533       ror       a0, s3, s4");
  COMPARE(orcb(a0, s3), "2879d513       orc.b     a0, s3");
#ifdef V8_TARGET_ARCH_RISCV64
  COMPARE(rori(a0, s3, 63), "63f9d513       rori      a0, s3, 63");
  COMPARE(roriw(a0, s3, 31), "61f9d51b       roriw     a0, s3, 31");
  COMPARE(rolw(a0, s3, s4), "6149953b       rolw     a0, s3, s4");
  COMPARE(rorw(a0, s3, s4), "6149d53b       rorw     a0, s3, s4");
#else
  COMPARE(rori(a0, s3, 31), "61f9d513       rori      a0, s3, 31");
#endif

  COMPARE(bclr(a0, s2, s1), "48991533       bclr      a0, s2, s1");
  COMPARE(bclri(a0, s1, 3), "48349513       bclri     a0, s1, 3");
  COMPARE(bext(a0, s2, s1), "48995533       bext      a0, s2, s1");
  COMPARE(bexti(a0, s1, 3), "4834d513       bexti     a0, s1, 3");
  COMPARE(binv(a0, s2, s1), "68991533       binv      a0, s2, s1");
  COMPARE(binvi(a0, s1, 3), "68349513       binvi     a0, s1, 3");
  COMPARE(bset(a0, s2, s1), "28991533       bset      a0, s2, s1");
  COMPARE(bseti(a0, s1, 3), "28349513       bseti     a0, s1, 3");
  VERIFY_RUN();
}

TEST_F(DisasmRiscvTest, LD_ST) {
  SET_UP();
  // Loads
  COMPARE(lb(t0, a0, 0), "00050283       lb        t0, 0(a0)");
  COMPARE(lh(t1, a1, -1024), "c0059303       lh        t1, -1024(a1)");
  COMPARE(lw(t2, a2, 100), "06462383       lw        t2, 100(a2)");
  COMPARE(lbu(fp, a3, -512), "e006c403       lbu       fp, -512(a3)");
  COMPARE(lhu(s1, a4, 258), "10275483       lhu       s1, 258(a4)");

  // Stores
  COMPARE(sb(zero_reg, a5, -4), "fe078e23       sb        zero_reg, -4(a5)");
  COMPARE(sh(a6, s2, 4), "01091223       sh        a6, 4(s2)");
  COMPARE(sw(a7, s3, 100), "0719a223       sw        a7, 100(s3)");

  VERIFY_RUN();
}

TEST_F(DisasmRiscvTest, MISC) {
  SET_UP();

  COMPARE(lui(sp, 0x64), "00064137       lui       sp, 0x64");
  COMPARE(auipc(ra, 0x7fe), "007fe097       auipc     ra, 0x7fe");

  // Jumps
  COMPARE_PC_REL(jal(gp, 100), "064001ef       jal       gp, 100", 100);
  COMPARE(jalr(tp, zero_reg, 100),
          "06400267       jalr      tp, 100(zero_reg)");

  // Branches
  COMPARE_PC_REL(beq(fp, a4, -268), "eee40ae3       beq       fp, a4, -268",
                 -268);
  COMPARE_PC_REL(bne(t1, s4, -268), "ef431ae3       bne       t1, s4, -268",
                 -268);
  COMPARE_PC_REL(blt(s3, t4, -268), "efd9cae3       blt       s3, t4, -268",
                 -268);
  COMPARE_PC_REL(bge(t2, sp, -268), "ee23dae3       bge       t2, sp, -268",
                 -268);
  COMPARE_PC_REL(bltu(s6, a1, -268), "eebb6ae3       bltu      s6, a1, -268",
                 -268);
  COMPARE_PC_REL(bgeu(a1, s3, -268), "ef35fae3       bgeu      a1, s3, -268",
                 -268);

  // Memory fences
  COMPARE(fence(PSO | PSR, PSW | PSI), "0690000f       fence or, iw");
  COMPARE(fence_tso(), "8330000f       fence rw, rw");

  // Environment call / break
  COMPARE(ecall(), "00000073       ecall");
  COMPARE(ebreak(), "00100073       ebreak");

  VERIFY_RUN();
}

TEST_F(DisasmRiscvTest, CSR) {
  SET_UP();

  COMPARE(csrrw(a0, csr_fflags, t3), "001e1573       fsflags   a0, t3");
  COMPARE(csrrs(a0, csr_frm, t1), "00232573       csrrs     a0, csr_frm, t1");
  COMPARE(csrrc(a0, csr_fcsr, s3), "0039b573       csrrc     a0, csr_fcsr, s3");
  COMPARE(csrrwi(a0, csr_fflags, 0x10),
          "00185573       csrrwi    a0, csr_fflags, 0x10");
  COMPARE(csrrsi(t3, csr_frm, 0x3),
          "0021ee73       csrrsi    t3, csr_frm, 0x3");
  COMPARE(csrrci(a0, csr_fflags, 0x10),
          "00187573       csrrci    a0, csr_fflags, 0x10");

  VERIFY_RUN();
}

TEST_F(DisasmRiscvTest, ZICOND) {
  SET_UP();

  COMPARE(czero_eqz(a0, s1, t3), "0fc4d533       czero.eqz a0, s1, t3");
  COMPARE(czero_nez(s3, a1, t1), "0e65f9b3       czero.nez s3, a1, t1");

  VERIFY_RUN();
}

#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64I) {
  SET_UP();

  COMPARE(lwu(a0, s3, -268), "ef49e503       lwu       a0, -268(s3)");
  COMPARE(ld(a1, s3, -268), "ef49b583       ld        a1, -268(s3)");
  COMPARE(sd(fp, sp, -268), "ee813a23       sd        fp, -268(sp)");
  COMPARE(addiw(gp, s3, -268), "ef49819b       addiw     gp, s3, -268");
  COMPARE(slliw(tp, s3, 17), "0119921b       slliw     tp, s3, 17");
  COMPARE(srliw(ra, s3, 10), "00a9d09b       srliw     ra, s3, 10");
  COMPARE(sraiw(sp, s3, 17), "4119d11b       sraiw     sp, s3, 17");
  COMPARE(addw(t1, zero_reg, s4), "0140033b       addw      t1, zero_reg, s4");
  COMPARE(subw(t2, s3, s4), "414983bb       subw      t2, s3, s4");
  COMPARE(sllw(s7, s3, s4), "01499bbb       sllw      s7, s3, s4");
  COMPARE(srlw(s10, s3, s4), "0149dd3b       srlw      s10, s3, s4");
  COMPARE(sraw(a7, s3, s4), "4149d8bb       sraw      a7, s3, s4");

  VERIFY_RUN();
}
#endif
TEST_F(DisasmRiscvTest, RV32M) {
  SET_UP();

  COMPARE(mul(a0, s3, t4), "03d98533       mul       a0, s3, t4");
  COMPARE(mulh(a0, s3, t4), "03d99533       mulh      a0, s3, t4");
  COMPARE(mulhsu(a0, s3, t4), "03d9a533       mulhsu    a0, s3, t4");
  COMPARE(mulhu(a0, s3, t4), "03d9b533       mulhu     a0, s3, t4");
  COMPARE(div(a0, s3, t4), "03d9c533       div       a0, s3, t4");
  COMPARE(divu(a0, s3, t4), "03d9d533       divu      a0, s3, t4");
  COMPARE(rem(a0, s3, t4), "03d9e533       rem       a0, s3, t4");
  COMPARE(remu(a0, s3, t4), "03d9f533       remu      a0, s3, t4");

  VERIFY_RUN();
}
#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64M) {
  SET_UP();

  COMPARE(mulw(a0, s3, s4), "0349853b       mulw      a0, s3, s4");
  COMPARE(divw(a0, s3, s4), "0349c53b       divw      a0, s3, s4");
  COMPARE(divuw(a0, s3, s4), "0349d53b       divuw     a0, s3, s4");
  COMPARE(remw(a0, s3, s4), "0349e53b       remw      a0, s3, s4");
  COMPARE(remuw(a0, s3, s4), "0349f53b       remuw     a0, s3, s4");

  VERIFY_RUN();
}
#endif
TEST_F(DisasmRiscvTest, RV32A) {
  SET_UP();
  // RV32A Standard Extension
  COMPARE(lr_w(true, false, a0, s3), "1409a52f       lr.w.aq    a0, (s3)");
  COMPARE(sc_w(true, true, a0, s3, s4),
          "1f49a52f       sc.w.aqrl    a0, s4, (s3)");
  COMPARE(amoswap_w(false, false, a0, s3, s4),
          "0949a52f       amoswap.w a0, s4, (s3)");
  COMPARE(amoadd_w(false, true, a0, s3, s4),
          "0349a52f       amoadd.w.rl a0, s4, (s3)");
  COMPARE(amoxor_w(true, false, a0, s3, s4),
          "2549a52f       amoxor.w.aq a0, s4, (s3)");
  COMPARE(amoand_w(false, false, a0, s3, s4),
          "6149a52f       amoand.w a0, s4, (s3)");
  COMPARE(amoor_w(true, true, a0, s3, s4),
          "4749a52f       amoor.w.aqrl a0, s4, (s3)");
  COMPARE(amomin_w(false, true, a0, s3, s4),
          "8349a52f       amomin.w.rl a0, s4, (s3)");
  COMPARE(amomax_w(true, false, a0, s3, s4),
          "a549a52f       amomax.w.aq a0, s4, (s3)");
  COMPARE(amominu_w(false, false, a0, s3, s4),
          "c149a52f       amominu.w a0, s4, (s3)");
  COMPARE(amomaxu_w(true, true, a0, s3, s4),
          "e749a52f       amomaxu.w.aqrl a0, s4, (s3)");
  VERIFY_RUN();
}
#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64A) {
  SET_UP();

  COMPARE(lr_d(true, true, a0, s3), "1609b52f       lr.d.aqrl a0, (s3)");
  COMPARE(sc_d(false, true, a0, s3, s4), "1b49b52f       sc.d.rl a0, s4, (s3)");
  COMPARE(amoswap_d(true, false, a0, s3, s4),
          "0d49b52f       amoswap.d.aq a0, s4, (s3)");
  COMPARE(amoadd_d(false, false, a0, s3, s4),
          "0149b52f       amoadd.d a0, s4, (s3)");
  COMPARE(amoxor_d(true, false, a0, s3, s4),
          "2549b52f       amoxor.d.aq a0, s4, (s3)");
  COMPARE(amoand_d(true, true, a0, s3, s4),
          "6749b52f       amoand.d.aqrl a0, s4, (s3)");
  COMPARE(amoor_d(false, true, a0, s3, s4),
          "4349b52f       amoor.d.rl a0, s4, (s3)");
  COMPARE(amomin_d(true, true, a0, s3, s4),
          "8749b52f       amomin.d.aqrl a0, s4, (s3)");
  COMPARE(amomax_d(false, true, a0, s3, s4),
          "a349b52f       amoswap.d.rl a0, s4, (s3)");
  COMPARE(amominu_d(true, false, a0, s3, s4),
          "c549b52f       amominu.d.aq a0, s4, (s3)");
  COMPARE(amomaxu_d(false, true, a0, s3, s4),
          "e349b52f       amomaxu.d.rl a0, s4, (s3)");

  VERIFY_RUN();
}
#endif
TEST_F(DisasmRiscvTest, RV32F) {
  SET_UP();
  // RV32F Standard Extension
  COMPARE(flw(fa0, s3, -268), "ef49a507       flw       fa0, -268(s3)");
  COMPARE(fsw(ft7, sp, -268), "ee712a27       fsw       ft7, -268(sp)");
  COMPARE(fmadd_s(fa0, ft8, fa5, fs5),
          "a8fe0543       fmadd.s   fa0, ft8, fa5, fs5");
  COMPARE(fmsub_s(fa0, ft8, fa5, fs5),
          "a8fe0547       fmsub.s   fa0, ft8, fa5, fs5");
  COMPARE(fnmsub_s(fa0, ft8, fa5, fs5),
          "a8fe054b       fnmsub.s   fa0, ft8, fa5, fs5");
  COMPARE(fnmadd_s(fa0, ft8, fa5, fs5),
          "a8fe054f       fnmadd.s   fa0, ft8, fa5, fs5");
  COMPARE(fadd_s(fa0, ft8, fa5), "00fe0553       fadd.s    fa0, ft8, fa5");
  COMPARE(fsub_s(fa0, ft8, fa5), "08fe0553       fsub.s    fa0, ft8, fa5");
  COMPARE(fmul_s(fa0, ft8, fa5), "10fe0553       fmul.s    fa0, ft8, fa5");
  COMPARE(fdiv_s(ft0, ft8, fa5), "18fe0053       fdiv.s    ft0, ft8, fa5");
  COMPARE(fsqrt_s(ft0, ft8), "580e0053       fsqrt.s   ft0, ft8");
  COMPARE(fsgnj_s(ft0, ft8, fa5), "20fe0053       fsgnj.s   ft0, ft8, fa5");
  COMPARE(fsgnjn_s(ft0, ft8, fa5), "20fe1053       fsgnjn.s  ft0, ft8, fa5");
  COMPARE(fsgnjx_s(ft0, ft8, fa5), "20fe2053       fsgnjx.s  ft0, ft8, fa5");
  COMPARE(fmin_s(ft0, ft8, fa5), "28fe0053       fmin.s    ft0, ft8, fa5");
  COMPARE(fmax_s(ft0, ft8, fa5), "28fe1053       fmax.s    ft0, ft8, fa5");
  COMPARE(fcvt_w_s(a0, ft8, RNE), "c00e0553       fcvt.w.s  [RNE] a0, ft8");
  COMPARE(fcvt_wu_s(a0, ft8, RTZ), "c01e1553       fcvt.wu.s [RTZ] a0, ft8");
  COMPARE(fmv_x_w(a0, ft8), "e00e0553       fmv.x.w   a0, ft8");
  COMPARE(feq_s(a0, ft8, fa5), "a0fe2553       feq.s     a0, ft8, fa5");
  COMPARE(flt_s(a0, ft8, fa5), "a0fe1553       flt.s     a0, ft8, fa5");
  COMPARE(fle_s(a0, ft8, fa5), "a0fe0553       fle.s     a0, ft8, fa5");
  COMPARE(fclass_s(a0, ft8), "e00e1553       fclass.s  a0, ft8");
  COMPARE(fcvt_s_w(ft0, s3), "d0098053       fcvt.s.w  ft0, s3");
  COMPARE(fcvt_s_wu(ft0, s3), "d0198053       fcvt.s.wu ft0, s3");
  COMPARE(fmv_w_x(ft0, s3), "f0098053       fmv.w.x   ft0, s3");
  VERIFY_RUN();
}
#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64F) {
  SET_UP();
  // RV64F Standard Extension (in addition to RV32F)
  COMPARE(fcvt_l_s(a0, ft8, RNE), "c02e0553       fcvt.l.s  [RNE] a0, ft8");
  COMPARE(fcvt_lu_s(a0, ft8, RMM), "c03e4553       fcvt.lu.s [RMM] a0, ft8");
  COMPARE(fcvt_s_l(ft0, s3), "d0298053       fcvt.s.l  ft0, s3");
  COMPARE(fcvt_s_lu(ft0, s3), "d0398053       fcvt.s.lu ft0, s3");
  VERIFY_RUN();
}
#endif
TEST_F(DisasmRiscvTest, RV32D) {
  SET_UP();
  // RV32D Standard Extension
  COMPARE(fld(ft0, s3, -268), "ef49b007       fld       ft0, -268(s3)");
  COMPARE(fsd(ft7, sp, -268), "ee713a27       fsd       ft7, -268(sp)");
  COMPARE(fmadd_d(ft0, ft8, fa5, fs5),
          "aafe0043       fmadd.d   ft0, ft8, fa5, fs5");
  COMPARE(fmsub_d(ft0, ft8, fa5, fs1),
          "4afe0047       fmsub.d   ft0, ft8, fa5, fs1");
  COMPARE(fnmsub_d(ft0, ft8, fa5, fs2),
          "92fe004b       fnmsub.d  ft0, ft8, fa5, fs2");
  COMPARE(fnmadd_d(ft0, ft8, fa5, fs3),
          "9afe004f       fnmadd.d  ft0, ft8, fa5, fs3");
  COMPARE(fadd_d(ft0, ft8, fa5), "02fe0053       fadd.d    ft0, ft8, fa5");
  COMPARE(fsub_d(ft0, ft8, fa5), "0afe0053       fsub.d    ft0, ft8, fa5");
  COMPARE(fmul_d(ft0, ft8, fa5), "12fe0053       fmul.d    ft0, ft8, fa5");
  COMPARE(fdiv_d(ft0, ft8, fa5), "1afe0053       fdiv.d    ft0, ft8, fa5");
  COMPARE(fsqrt_d(ft0, ft8), "5a0e0053       fsqrt.d   ft0, ft8");
  COMPARE(fsgnj_d(ft0, ft8, fa5), "22fe0053       fsgnj.d   ft0, ft8, fa5");
  COMPARE(fsgnjn_d(ft0, ft8, fa5), "22fe1053       fsgnjn.d  ft0, ft8, fa5");
  COMPARE(fsgnjx_d(ft0, ft8, fa5), "22fe2053       fsgnjx.d  ft0, ft8, fa5");
  COMPARE(fmin_d(ft0, ft8, fa5), "2afe0053       fmin.d    ft0, ft8, fa5");
  COMPARE(fmax_d(ft0, ft8, fa5), "2afe1053       fmax.d    ft0, ft8, fa5");
  COMPARE(fcvt_s_d(ft0, ft8, RDN), "401e2053       fcvt.s.d  [RDN] ft0, ft8");
  COMPARE(fcvt_d_s(ft0, fa0), "42050053       fcvt.d.s  ft0, fa0");
  COMPARE(feq_d(a0, ft8, fa5), "a2fe2553       feq.d     a0, ft8, fa5");
  COMPARE(flt_d(a0, ft8, fa5), "a2fe1553       flt.d     a0, ft8, fa5");
  COMPARE(fle_d(a0, ft8, fa5), "a2fe0553       fle.d     a0, ft8, fa5");
  COMPARE(fclass_d(a0, ft8), "e20e1553       fclass.d  a0, ft8");
  COMPARE(fcvt_w_d(a0, ft8, RNE), "c20e0553       fcvt.w.d  [RNE] a0, ft8");
  COMPARE(fcvt_wu_d(a0, ft8, RUP), "c21e3553       fcvt.wu.d [RUP] a0, ft8");
  COMPARE(fcvt_d_w(ft0, s3), "d2098053       fcvt.d.w  ft0, s3");
  COMPARE(fcvt_d_wu(ft0, s3), "d2198053       fcvt.d.wu ft0, s3");

  VERIFY_RUN();
}
#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64D) {
  SET_UP();
  // RV64D Standard Extension (in addition to RV32D)
  COMPARE(fcvt_l_d(a0, ft8, RMM), "c22e4553       fcvt.l.d  [RMM] a0, ft8");
  COMPARE(fcvt_lu_d(a0, ft8, RDN), "c23e2553       fcvt.lu.d [RDN] a0, ft8");
  COMPARE(fmv_x_d(a0, ft8), "e20e0553       fmv.x.d   a0, ft8");
  COMPARE(fcvt_d_l(ft0, s3), "d2298053       fcvt.d.l  ft0, s3");
  COMPARE(fcvt_d_lu(ft0, s3), "d2398053       fcvt.d.lu ft0, s3");
  COMPARE(fmv_d_x(ft0, s3), "f2098053       fmv.d.x   ft0, s3");
  VERIFY_RUN();
}
#endif

TEST_F(DisasmRiscvTest, PSEUDO) {
  SET_UP();
  // pseodu instructions according to rISCV assembly programmer's handbook
  COMPARE(nop(), "00000013       nop");
  COMPARE(RV_li(t6, -12), "ff400f93       li        t6, -12");
  COMPARE(mv(t0, a4), "00070293       mv        t0, a4");
  COMPARE(not_(t0, a5), "fff7c293       not       t0, a5");
  COMPARE(neg(ra, a6), "410000b3       neg       ra, a6");
#ifdef V8_TARGET_ARCH_RISCV64
  COMPARE(negw(t2, fp), "408003bb       negw      t2, fp");
  COMPARE(sext_w(t0, s1), "0004829b       sext.w    t0, s1");
#endif
  COMPARE(seqz(sp, s2), "00193113       seqz      sp, s2");
  COMPARE(snez(fp, s3), "01303433       snez      fp, s3");
  COMPARE(sltz(a0, t5), "000f2533       sltz      a0, t5");
  COMPARE(sgtz(a1, t4), "01d025b3       sgtz      a1, t4");

  COMPARE(fmv_s(fa0, fs4), "214a0553       fmv.s     fa0, fs4");
  COMPARE(fabs_s(fa1, fs3), "2139a5d3       fabs.s    fa1, fs3");
  COMPARE(fneg_s(fa2, fs5), "215a9653       fneg.s    fa2, fs5");
  COMPARE(fmv_d(fa3, fs2), "232906d3       fmv.d     fa3, fs2");
  COMPARE(fabs_d(fs0, fs2), "23292453       fabs.d    fs0, fs2");
  COMPARE(fneg_d(fs1, fs1), "229494d3       fneg.d    fs1, fs1");

  COMPARE_PC_REL(j(-1024), "c01ff06f       j         -1024", -1024);
  COMPARE_PC_REL(jal(32), "020000ef       jal       32", 32);
  COMPARE(jr(a1), "00058067       jr        a1");
  COMPARE(jalr(a1), "000580e7       jalr      a1");
  COMPARE(ret(), "00008067       ret");
  // COMPARE(call(int32_t offset);

  COMPARE(rdinstret(t0), "c02022f3       rdinstret t0");
  COMPARE(rdinstreth(a0), "c8202573       rdinstreth a0");
  COMPARE(rdcycle(a4), "c0002773       rdcycle   a4");
  COMPARE(rdcycleh(a5), "c80027f3       rdcycleh  a5");
  COMPARE(rdtime(a3), "c01026f3       rdtime    a3");
  COMPARE(rdtimeh(t2), "c81023f3       rdtimeh   t2");

  COMPARE(csrr(t3, csr_cycle), "c0002e73       rdcycle   t3");
  COMPARE(csrw(csr_instret, a1), "c0259073       csrw      csr_instret, a1");
  COMPARE(csrs(csr_timeh, a2), "c8162073       csrs      csr_timeh, a2");
  COMPARE(csrc(csr_cycleh, t1), "c8033073       csrc      csr_cycleh, t1");

  COMPARE(csrwi(csr_time, 0xf), "c017d073       csrwi     csr_time, 0xf");
  COMPARE(csrsi(csr_cycleh, 0x1), "c800e073       csrsi     csr_cycleh, 0x1");
  COMPARE(csrci(csr_instreth, 0x15),
          "c82af073       csrci     csr_instreth, 0x15");

  COMPARE(frcsr(t4), "00302ef3       frcsr     t4");
  COMPARE(fscsr(t1, a1), "00359373       fscsr     t1, a1");
  COMPARE(fscsr(a4), "00371073       fscsr     a4");

  COMPARE(frrm(t2), "002023f3       frrm      t2");
  COMPARE(fsrm(t0, a1), "002592f3       fsrm      t0, a1");
  COMPARE(fsrm(a5), "00279073       fsrm      a5");

  COMPARE(frflags(s5), "00102af3       frflags   s5");
  COMPARE(fsflags(s2, t1), "00131973       fsflags   s2, t1");
  COMPARE(fsflags(s1), "00149073       fsflags   s1");

  VERIFY_RUN();
}
#ifdef V8_TARGET_ARCH_RISCV64
TEST_F(DisasmRiscvTest, RV64C) {
  i::v8_flags.riscv_c_extension = true;
  SET_UP();

  COMPARE(c_nop(), "00000001       nop");
  COMPARE(c_addi(s3, -25), "0000199d       addi      s3, s3, -25");
  COMPARE(c_addiw(gp, -30), "00003189       addiw     gp, gp, -30");
  COMPARE(c_addi16sp(-432), "00007161       addi      sp, sp, -432");
  COMPARE(c_addi4spn(a1, 924), "00000f6c       addi       a1, sp, 924");
  COMPARE(c_li(t6, -15), "00005fc5       li        t6, -15");
  COMPARE(c_lui(s1, 0xf4), "000074d1       lui       s1, 0xffff4");
  COMPARE(c_slli(s9, 60), "00001cf2       slli      s9, s9, 60");
  COMPARE(c_fldsp(fa1, 360), "000035b6       fld       fa1, 360(sp)");
  COMPARE(c_lwsp(s7, 244), "00005bde       lw        s7, 244(sp)");
  COMPARE(c_ldsp(s6, 344), "00006b76       ld        s6, 344(sp)");

  COMPARE(c_jr(a1), "00008582       jr        a1");
  COMPARE(c_mv(t0, a4), "000082ba       mv        t0, a4");
  COMPARE(c_ebreak(), "00009002       ebreak");
  COMPARE(c_jalr(a1), "00009582       jalr      a1");
  COMPARE(c_add(s6, t0), "00009b16       add       s6, s6, t0");
  COMPARE(c_sub(s1, a0), "00008c89       sub       s1, s1, a0");
  COMPARE(c_xor(s1, a0), "00008ca9       xor       s1, s1, a0");
  COMPARE(c_or(s1, a0), "00008cc9       or       s1, s1, a0");
  COMPARE(c_and(s1, a0), "00008ce9       and       s1, s1, a0");
  COMPARE(c_subw(s1, a0), "00009c89       subw       s1, s1, a0");
  COMPARE(c_addw(a0, a1), "00009d2d       addw       a0, a0, a1");

  COMPARE(c_fsdsp(fa4, 232), "0000b5ba       fsd       fa4, 232(sp)");
  COMPARE(c_swsp(s6, 180), "0000db5a       sw        s6, 180(sp)");
  COMPARE(c_sdsp(a4, 216), "0000edba       sd        a4, 216(sp)");

  COMPARE(c_lw(a2, s1, 24), "00004c90       lw       a2, 24(s1)");
  COMPARE(c_ld(a2, s1, 24), "00006c90       ld       a2, 24(s1)");
  COMPARE(c_fld(fa1, s1, 24), "00002c8c       fld       fa1, 24(s1)");

  COMPARE(c_sw(a2, s1, 24), "0000cc90       sw       a2, 24(s1)");
  COMPARE(c_sd(a2, s1, 24), "0000ec90       sd       a2, 24(s1)");
  COMPARE(c_fsd(fa1, s1, 24), "0000ac8c       fsd       fa1, 24(s1)");

  COMPARE(c_j(-12), "0000bfd5       j       -12");

  COMPARE(c_beqz(a0, -12), "0000d975       beqz       a0, x0, -12");
  COMPARE(c_bnez(a0, -12), "0000f975       bnez       a0, x0, -12");
  COMPARE(c_srli(a1, 24), "000081e1       srli       a1, a1, 24");
  COMPARE(c_andi(a1, 24), "000089e1       andi       a1, a1, 24");
  COMPARE(c_srai(a1, 24), "000085e1       srai       a1, a1, 24");

  VERIFY_RUN();
}
#endif
/*
TEST_F(DisasmRiscvTest,  Previleged) {
  SET_UP();
  // Privileged
  COMPARE(uret(), "");
  COMPARE(sret(), "");
  COMPARE(mret(), "");
  COMPARE(wfi(), "");
  COMPARE(sfence_vma(s3, s4), "");
  VERIFY_RUN();
}
*/

TEST_F(DisasmRiscvTest, RVV) {
  if (!CpuFeatures::IsSupported(RISCV_SIMD)) return;
  SET_UP();
  COMPARE(VU.set(kScratchReg, E64, m1),
          "018079d7       vsetvli   s3, zero_reg, E64, m1");
  COMPARE(vl(v2, a0, 0, VSew::E8), "02050107       vle8.v    v2, (a0)");
  COMPARE(vl(v2, a0, 0, VSew::E16), "02055107       vle16.v    v2, (a0)");
  COMPARE(vl(v2, a0, 0, VSew::E32), "02056107       vle32.v    v2, (a0)");
  COMPARE(vl(v2, a0, 0, VSew::E64), "02057107       vle64.v    v2, (a0)");

  COMPARE(vadd_vv(v0, v0, v1), "02008057       vadd.vv   v0, v0, v1");
  COMPARE(vadd_vx(v0, v1, t0), "0212c057       vadd.vx   v0, v1, t0");
  COMPARE(vadd_vi(v0, v1, 3), "0211b057       vadd.vi   v0, v1, 3");
  COMPARE(vsub_vv(v2, v3, v4), "0a320157       vsub.vv   v2, v3, v4");
  COMPARE(vsub_vx(v2, v3, a4), "0a374157       vsub.vx   v2, v3, a4");
  COMPARE(vsadd_vv(v0, v0, v1), "86008057       vsadd.vv  v0, v0, v1");
  COMPARE(vsadd_vx(v4, v5, t1), "86534257       vsadd.vx  v4, v5, t1");
  COMPARE(vsadd_vi(v6, v7, 5), "8672b357       vsadd.vi  v6, v7, 5");
  COMPARE(vssub_vv(v2, v3, v4), "8e320157       vssub.vv  v2, v3, v4");
  COMPARE(vssub_vx(v2, v3, t4), "8e3ec157       vssub.vx  v2, v3, t4");
  COMPARE(vor_vv(v21, v31, v9), "2bf48ad7       vor.vv    v21, v31, v9");
  COMPARE(vor_vx(v19, v29, s7), "2bdbc9d7       vor.vx    v19, v29, s7");
  COMPARE(vor_vi(v17, v28, 7), "2bc3b8d7       vor.vi    v17, v28, 7");
  COMPARE(vxor_vv(v21, v31, v9), "2ff48ad7       vxor.vv   v21, v31, v9");
  COMPARE(vxor_vx(v19, v29, s7), "2fdbc9d7       vxor.vx   v19, v29, s7");
  COMPARE(vxor_vi(v17, v28, 7), "2fc3b8d7       vxor.vi   v17, v28, 7");
  COMPARE(vand_vv(v21, v31, v9), "27f48ad7       vand.vv   v21, v31, v9");
  COMPARE(vand_vx(v19, v29, s7), "27dbc9d7       vand.vx   v19, v29, s7");
  COMPARE(vand_vi(v17, v28, 7), "27c3b8d7       vand.vi   v17, v28, 7");
  COMPARE(vmseq_vv(v17, v28, v29), "63ce88d7       vmseq.vv  v17, v28, v29");
  COMPARE(vmsne_vv(v17, v28, v29), "67ce88d7       vmsne.vv  v17, v28, v29");
  COMPARE(vmseq_vx(v17, v28, t2), "63c3c8d7       vmseq.vx  v17, v28, t2");
  COMPARE(vmsne_vx(v17, v28, t6), "67cfc8d7       vmsne.vx  v17, v28, t6");
  COMPARE(vmseq_vi(v17, v28, 7), "63c3b8d7       vmseq.vi  v17, v28, 7");
  COMPARE(vmsne_vi(v17, v28, 7), "67c3b8d7       vmsne.vi  v17, v28, 7");
  COMPARE(vmsltu_vv(v17, v28, v14), "6bc708d7       vmsltu.vv v17, v28, v14");
  COMPARE(vmsltu_vx(v17, v28, a5), "6bc7c8d7       vmsltu.vx v17, v28, a5");
  COMPARE(vmslt_vv(v17, v28, v14), "6fc708d7       vmslt.vv  v17, v28, v14");
  COMPARE(vmslt_vx(v17, v28, a5), "6fc7c8d7       vmslt.vx  v17, v28, a5");
  COMPARE(vmsleu_vv(v17, v28, v14), "73c708d7       vmsleu.vv v17, v28, v14");
  COMPARE(vmsleu_vx(v17, v28, a5), "73c7c8d7       vmsleu.vx v17, v28, a5");
  COMPARE(vmsleu_vi(v17, v28, 5), "73c2b8d7       vmsleu.vi v17, v28, 5");
  COMPARE(vmsle_vv(v17, v28, v14), "77c708d7       vmsle.vv  v17, v28, v14");
  COMPARE(vmsle_vx(v17, v28, a5), "77c7c8d7       vmsle.vx  v17, v28, a5");
  COMPARE(vmsle_vi(v17, v28, 5), "77c2b8d7       vmsle.vi  v17, v28, 5");
  COMPARE(vmsgt_vx(v17, v28, a5), "7fc7c8d7       vmsgt.vx  v17, v28, a5");
  COMPARE(vmsgt_vi(v17, v28, 5), "7fc2b8d7       vmsgt.vi  v17, v28, 5");
  COMPARE(vmsgtu_vx(v17, v28, a5), "7bc7c8d7       vmsgtu.vx v17, v28, a5");
  COMPARE(vmsgtu_vi(v17, v28, 5), "7bc2b8d7       vmsgtu.vi v17, v28, 5");
  COMPARE(vadc_vv(v7, v9, v6), "406483d7       vadc.vvm  v7, v6, v9");
  COMPARE(vadc_vx(v7, t6, v9), "409fc3d7       vadc.vxm  v7, v9, t6");
  COMPARE(vadc_vi(v7, 5, v9), "4092b3d7       vadc.vim  v7, v9, 5");

  COMPARE(vfadd_vv(v17, v14, v28), "02ee18d7       vfadd.vv  v17, v14, v28");
  COMPARE(vfsub_vv(v17, v14, v28), "0aee18d7       vfsub.vv  v17, v14, v28");
  COMPARE(vfmul_vv(v17, v14, v28), "92ee18d7       vfmul.vv  v17, v14, v28");
  COMPARE(vfdiv_vv(v17, v14, v28), "82ee18d7       vfdiv.vv  v17, v14, v28");
  COMPARE(vfmv_vf(v17, fa5), "5e07d8d7       vfmv.v.f  v17, fa5");
  COMPARE(vfmv_fs(fa5, v17), "431017d7       vfmv.f.s  fa5, v17");

  // Vector Single-Width Floating-Point Fused Multiply-Add Instructions
  COMPARE(vfmadd_vv(v17, v14, v28), "a3c718d7       vfmadd.vv v17, v14, v28");
  COMPARE(vfnmadd_vv(v17, v14, v28), "a7c718d7       vfnmadd.vv v17, v14, v28");
  COMPARE(vfmsub_vv(v17, v14, v28), "abc718d7       vfmsub.vv v17, v14, v28");
  COMPARE(vfnmsub_vv(v17, v14, v28), "afc718d7       vfnmsub.vv v17, v14, v28");
  COMPARE(vfmacc_vv(v17, v14, v28), "b3c718d7       vfmacc.vv v17, v14, v28");
  COMPARE(vfnmacc_vv(v17, v14, v28), "b7c718d7       vfnmacc.vv v17, v14, v28");
  COMPARE(vfmsac_vv(v17, v14, v28), "bbc718d7       vfmsac.vv v17, v14, v28");
  COMPARE(vfnmsac_vv(v17, v14, v28), "bfc718d7       vfnmsac.vv v17, v14, v28");
  COMPARE(vfmadd_vf(v17, fa5, v28), "a3c7d8d7       vfmadd.vf v17, fa5, v28");
  COMPARE(vfnmadd_vf(v17, fa5, v28), "a7c7d8d7       vfnmadd.vf v17, fa5, v28");
  COMPARE(vfmsub_vf(v17, fa5, v28), "abc7d8d7       vfmsub.vf v17, fa5, v28");
  COMPARE(vfnmsub_vf(v17, fa5, v28), "afc7d8d7       vfnmsub.vf v17, fa5, v28");
  COMPARE(vfmacc_vf(v17, fa5, v28), "b3c7d8d7       vfmacc.vf v17, fa5, v28");
  COMPARE(vfnmacc_vf(v17, fa5, v28), "b7c7d8d7       vfnmacc.vf v17, fa5, v28");
  COMPARE(vfmsac_vf(v17, fa5, v28), "bbc7d8d7       vfmsac.vf v17, fa5, v28");
  COMPARE(vfnmsac_vf(v17, fa5, v28), "bfc7d8d7       vfnmsac.vf v17, fa5, v28");

  // Vector Narrowing Fixed-Point Clip Instructions
  COMPARE(vnclip_vi(v17, v14, 5), "bee2b8d7       vnclip.wi v17, v14, 5");
  COMPARE(vnclip_vx(v17, v14, a5), "bee7c8d7       vnclip.wx v17, v14, a5");
  COMPARE(vnclip_vv(v17, v14, v28), "beee08d7       vnclip.wv v17, v14, v28");
  COMPARE(vnclipu_vi(v17, v14, 5), "bae2b8d7       vnclipu.wi v17, v14, 5");
  COMPARE(vnclipu_vx(v17, v14, a5), "bae7c8d7       vnclipu.wx v17, v14, a5");
  COMPARE(vnclipu_vv(v17, v14, v28), "baee08d7       vnclipu.wv v17, v14, v28");

  // Vector Integer Extension
  COMPARE(vzext_vf8(v17, v14), "4ae128d7       vzext.vf8 v17, v14");
  COMPARE(vsext_vf8(v17, v14), "4ae1a8d7       vsext.vf8 v17, v14");
  COMPARE(vzext_vf4(v17, v14), "4ae228d7       vzext.vf4 v17, v14");
  COMPARE(vsext_vf4(v17, v14), "4ae2a8d7       vsext.vf4 v17, v14");
  COMPARE(vzext_vf2(v17, v14), "4ae328d7       vzext.vf2 v17, v14");
  COMPARE(vsext_vf2(v17, v14), "4ae3a8d7       vsext.vf2 v17, v14");

  // Vector Mask Instructions
  COMPARE(vfirst_m(a5, v17), "4318a7d7       vfirst.m  a5, v17");
  COMPARE(vcpop_m(a5, v17), "431827d7       vcpop.m   a5, v17");

  COMPARE(vfsqrt_v(v17, v28), "4fc018d7       vfsqrt.v  v17, v28")
  COMPARE(vfrsqrt7_v(v17, v28), "4fc218d7       vfrsqrt7.v v17, v28")
  COMPARE(vfrec7_v(v17, v28), "4fc298d7       vfrec7.v  v17, v28")
  COMPARE(vfclass_v(v17, v28), "4fc818d7       vfclass.v  v17, v28")

  // Vector Widening Floating-Point Add/Subtract Instructions
  COMPARE(vfwadd_vv(v17, v14, v28), "c2ee18d7       vfwadd.vv v17, v14, v28");
  COMPARE(vfwsub_vv(v17, v14, v28), "caee18d7       vfwsub.vv v17, v14, v28");
  COMPARE(vfwadd_wv(v17, v14, v28), "d2ee18d7       vfwadd.wv v17, v14, v28");
  COMPARE(vfwsub_wv(v17, v14, v28), "daee18d7       vfwsub.wv v17, v14, v28");
  COMPARE(vfwadd_vf(v17, v28, fa5), "c3c7d8d7       vfwadd.vf v17, v28, fa5");
  COMPARE(vfwsub_vf(v17, v28, fa5), "cbc7d8d7       vfwsub.vf v17, v28, fa5");
  COMPARE(vfwadd_wf(v17, v28, fa5), "d3c7d8d7       vfwadd.wf v17, v28, fa5");
  COMPARE(vfwsub_wf(v17, v28, fa5), "dbc7d8d7       vfwsub.wf v17, v28, fa5");

  // Vector Widening Floating-Point Reduction Instructions
  COMPARE(vfwredusum_vs(v17, v14, v28),
          "c6ee18d7       vfwredusum.vs v17, v14, v28");
  COMPARE(vfwredosum_vs(v17, v14, v28),
          "ceee18d7       vfwredosum.vs v17, v14, v28");

  // Vector Widening Floating-Point Multiply
  COMPARE(vfwmul_vv(v17, v14, v28), "e2ee18d7       vfwmul.vv v17, v14, v28");
  COMPARE(vfwmul_vf(v17, v28, fa5), "e3c7d8d7       vfwmul.vf v17, v28, fa5");

  // Vector Widening Floating-Point Fused Multiply-Add Instructions
  COMPARE(vfwmacc_vv(v17, v14, v28), "f3c718d7       vfwmacc.vv v17, v14, v28");
  COMPARE(vfwnmacc_vv(v17, v14, v28),
          "f7c718d7       vfwnmacc.vv v17, v14, v28");
  COMPARE(vfwmsac_vv(v17, v14, v28), "fbc718d7       vfwmsac.vv v17, v14, v28");
  COMPARE(vfwnmsac_vv(v17, v14, v28),
          "ffc718d7       vfwnmsac.vv v17, v14, v28");
  COMPARE(vfwmacc_vf(v17, fa5, v28), "f3c7d8d7       vfwmacc.vf v17, fa5, v28");
  COMPARE(vfwnmacc_vf(v17, fa5, v28),
          "f7c7d8d7       vfwnmacc.vf v17, fa5, v28");
  COMPARE(vfwmsac_vf(v17, fa5, v28), "fbc7d8d7       vfwmsac.vf v17, fa5, v28");
  COMPARE(vfwnmsac_vf(v17, fa5, v28),
          "ffc7d8d7       vfwnmsac.vf v17, fa5, v28");

  VERIFY_RUN();
}

}  // namespace internal
}  // namespace v8
