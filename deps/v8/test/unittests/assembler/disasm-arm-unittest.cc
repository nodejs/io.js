// Copyright 2011 the V8 project authors. All rights reserved.
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

#include <cinttypes>
#include <cstdlib>

// The C++ style guide recommends using <re2> instead of <regex>. However, the
// former isn't available in V8.
#include <regex>  // NOLINT(build/c++11)

#include "src/base/numbers/double.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/debug/debug.h"
#include "src/diagnostics/disasm.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/frames-inl.h"
#include "src/init/v8.h"
#include "src/objects/objects-inl.h"
#include "src/utils/boxed-float.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

using DisasmArmTest = TestWithIsolate;

enum UseRegex { kRawString, kRegexString };

template <typename... S>
bool DisassembleAndCompare(uint8_t* begin, UseRegex use_regex,
                           S... expected_strings) {
  disasm::NameConverter converter;
  disasm::Disassembler disasm(converter);
  base::EmbeddedVector<char, 128> buffer;

  std::vector<std::string> expected_disassembly = {expected_strings...};
  size_t n_expected = expected_disassembly.size();
  uint8_t* end = begin + (n_expected * kInstrSize);

  std::vector<std::string> disassembly;
  for (uint8_t* pc = begin; pc < end;) {
    pc += disasm.InstructionDecode(buffer, pc);
    disassembly.emplace_back(buffer.begin());
  }

  bool test_passed = true;

  for (size_t i = 0; i < disassembly.size(); i++) {
    if (use_regex == kRawString) {
      if (expected_disassembly[i] != disassembly[i]) {
        fprintf(stderr,
                "expected: \n"
                "%s\n"
                "disassembled: \n"
                "%s\n\n",
                expected_disassembly[i].c_str(), disassembly[i].c_str());
        test_passed = false;
      }
    } else {
      DCHECK_EQ(use_regex, kRegexString);
      if (!std::regex_match(disassembly[i],
                            std::regex(expected_disassembly[i]))) {
        fprintf(stderr,
                "expected (regex): \n"
                "%s\n"
                "disassembled: \n"
                "%s\n\n",
                expected_disassembly[i].c_str(), disassembly[i].c_str());
        test_passed = false;
      }
    }
  }

  // Fail after printing expected disassembly if we expected a different number
  // of instructions.
  if (disassembly.size() != expected_disassembly.size()) {
    return false;
  }

  return test_passed;
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
#define BASE_COMPARE(asm_, use_regex, ...)                             \
  {                                                                    \
    int pc_offset = assm.pc_offset();                                  \
    uint8_t* progcounter = &buffer[pc_offset];                         \
    assm.asm_;                                                         \
    if (!DisassembleAndCompare(progcounter, use_regex, __VA_ARGS__)) { \
      failure = true;                                                  \
    }                                                                  \
  }

#define COMPARE(asm_, ...) BASE_COMPARE(asm_, kRawString, __VA_ARGS__)

#define COMPARE_REGEX(asm_, ...) BASE_COMPARE(asm_, kRegexString, __VA_ARGS__)

// Force emission of any pending literals into a pool.
#define EMIT_PENDING_LITERALS() assm.CheckConstPool(true, false)

// Verify that all invocations of the COMPARE macro passed successfully.
// Exit with a failure if at least one of the tests failed.
#define VERIFY_RUN()                           \
  if (failure) {                               \
    FATAL("ARM Disassembler tests failed.\n"); \
  }

// clang-format off


TEST_F(DisasmArmTest, Type0) {
  SET_UP();

  COMPARE(and_(r0, r1, Operand(r2)),
          "e0010002       and r0, r1, r2");
  COMPARE(and_(r1, r2, Operand(r3), LeaveCC),
          "e0021003       and r1, r2, r3");
  COMPARE(and_(r2, r3, Operand(r4), SetCC),
          "e0132004       ands r2, r3, r4");
  COMPARE(and_(r3, r4, Operand(r5), LeaveCC, eq),
          "00043005       andeq r3, r4, r5");

  COMPARE(eor(r4, r5, Operand(r6, LSL, 0)),
          "e0254006       eor r4, r5, r6");
  COMPARE(eor(r4, r5, Operand(r7, LSL, 1), SetCC),
          "e0354087       eors r4, r5, r7, lsl #1");
  COMPARE(eor(r4, r5, Operand(r8, LSL, 2), LeaveCC, ne),
          "10254108       eorne r4, r5, r8, lsl #2");
  COMPARE(eor(r4, r5, Operand(r9, LSL, 3), SetCC, cs),
          "20354189       eorcss r4, r5, r9, lsl #3");

  COMPARE(sub(r5, r6, Operand(r10, LSL, 31), LeaveCC, hs),
          "20465f8a       subcs r5, r6, r10, lsl #31");
  COMPARE(sub(r5, r6, Operand(r10, LSL, 30), SetCC, cc),
          "30565f0a       subccs r5, r6, r10, lsl #30");
  COMPARE(sub(r5, r6, Operand(r10, LSL, 24), LeaveCC, lo),
          "30465c0a       subcc r5, r6, r10, lsl #24");
  COMPARE(sub(r5, r6, Operand(r10, LSL, 16), SetCC, mi),
          "4056580a       submis r5, r6, r10, lsl #16");

  COMPARE(rsb(r6, r7, Operand(fp)),
          "e067600b       rsb r6, r7, fp");
  COMPARE(rsb(r6, r7, Operand(fp, LSR, 1)),
          "e06760ab       rsb r6, r7, fp, lsr #1");
  COMPARE(rsb(r6, r7, Operand(fp, LSR, 0), SetCC),
          "e077602b       rsbs r6, r7, fp, lsr #32");
  COMPARE(rsb(r6, r7, Operand(fp, LSR, 31), LeaveCC, pl),
          "50676fab       rsbpl r6, r7, fp, lsr #31");

  COMPARE(add(r7, r8, Operand(ip, ASR, 1)),
          "e08870cc       add r7, r8, ip, asr #1");
  COMPARE(add(r7, r8, Operand(ip, ASR, 0)),
          "e088704c       add r7, r8, ip, asr #32");
  COMPARE(add(r7, r8, Operand(ip), SetCC),
          "e098700c       adds r7, r8, ip");
  COMPARE(add(r7, r8, Operand(ip, ASR, 31), SetCC, vs),
          "60987fcc       addvss r7, r8, ip, asr #31");

  COMPARE(adc(r7, fp, Operand(ip, ASR, 5)),
          "e0ab72cc       adc r7, fp, ip, asr #5");
  COMPARE(adc(r4, ip, Operand(ip, ASR, 1), LeaveCC, vc),
          "70ac40cc       adcvc r4, ip, ip, asr #1");
  COMPARE(adc(r5, sp, Operand(ip), SetCC),
          "e0bd500c       adcs r5, sp, ip");
  COMPARE(adc(r8, lr, Operand(ip, ASR, 31), SetCC, vc),
          "70be8fcc       adcvcs r8, lr, ip, asr #31");

  COMPARE(sbc(r7, r1, Operand(ip, ROR, 1), LeaveCC, hi),
          "80c170ec       sbchi r7, r1, ip, ror #1");
  COMPARE(sbc(r7, r9, Operand(ip, ROR, 4)),
          "e0c9726c       sbc r7, r9, ip, ror #4");
  COMPARE(sbc(r7, r10, Operand(ip), SetCC),
          "e0da700c       sbcs r7, r10, ip");
  COMPARE(sbc(r7, ip, Operand(ip, ROR, 31), SetCC, hi),
          "80dc7fec       sbchis r7, ip, ip, ror #31");

  COMPARE(rsc(r7, r8, Operand(ip, LSL, r0)),
          "e0e8701c       rsc r7, r8, ip, lsl r0");
  COMPARE(rsc(r7, r8, Operand(ip, LSL, r1)),
          "e0e8711c       rsc r7, r8, ip, lsl r1");
  COMPARE(rsc(r7, r8, Operand(ip), SetCC),
          "e0f8700c       rscs r7, r8, ip");
  COMPARE(rsc(r7, r8, Operand(ip, LSL, r3), SetCC, ls),
          "90f8731c       rsclss r7, r8, ip, lsl r3");

  COMPARE(tst(r7, Operand(r5, ASR, ip), ge),
          "a1170c55       tstge r7, r5, asr ip");
  COMPARE(tst(r7, Operand(r6, ASR, sp)),
          "e1170d56       tst r7, r6, asr sp");
  COMPARE(tst(r7, Operand(r7), ge),
          "a1170007       tstge r7, r7");
  COMPARE(tst(r7, Operand(r8, ASR, fp), ge),
          "a1170b58       tstge r7, r8, asr fp");

  COMPARE(teq(r7, Operand(r5, ROR, r0), lt),
          "b1370075       teqlt r7, r5, ror r0");
  COMPARE(teq(r7, Operand(r6, ROR, lr)),
          "e1370e76       teq r7, r6, ror lr");
  COMPARE(teq(r7, Operand(r7), lt),
          "b1370007       teqlt r7, r7");
  COMPARE(teq(r7, Operand(r8, ROR, r1)),
          "e1370178       teq r7, r8, ror r1");

  COMPARE(cmp(r7, Operand(r4)),
          "e1570004       cmp r7, r4");
  COMPARE(cmp(r7, Operand(r6, LSL, 1), gt),
          "c1570086       cmpgt r7, r6, lsl #1");
  COMPARE(cmp(r7, Operand(r8, LSR, 3), gt),
          "c15701a8       cmpgt r7, r8, lsr #3");
  COMPARE(cmp(r7, Operand(r8, ASR, 19)),
          "e15709c8       cmp r7, r8, asr #19");

  COMPARE(cmn(r0, Operand(r4)),
          "e1700004       cmn r0, r4");
  COMPARE(cmn(r1, Operand(r6, ROR, 1)),
          "e17100e6       cmn r1, r6, ror #1");
  COMPARE(cmn(r2, Operand(r8)),
          "e1720008       cmn r2, r8");
  COMPARE(cmn(r3, Operand(fp), le),
          "d173000b       cmnle r3, fp");

  COMPARE(orr(r7, r8, Operand(lr), LeaveCC, al),
          "e188700e       orr r7, r8, lr");
  COMPARE(orr(r7, r8, Operand(fp)),
          "e188700b       orr r7, r8, fp");
  COMPARE(orr(r7, r8, Operand(sp), SetCC),
          "e198700d       orrs r7, r8, sp");
  COMPARE(orr(r7, r8, Operand(ip), SetCC, al),
          "e198700c       orrs r7, r8, ip");

  COMPARE(mov(r0, Operand(r1), LeaveCC, eq),
          "01a00001       moveq r0, r1");
  COMPARE(mov(r0, Operand(r2)),
          "e1a00002       mov r0, r2");
  COMPARE(mov(r0, Operand(r3), SetCC),
          "e1b00003       movs r0, r3");
  COMPARE(mov(r0, Operand(r4), SetCC, pl),
          "51b00004       movpls r0, r4");

  COMPARE(bic(r0, lr, Operand(r1), LeaveCC, vs),
          "61ce0001       bicvs r0, lr, r1");
  COMPARE(bic(r0, r9, Operand(r2), LeaveCC, vc),
          "71c90002       bicvc r0, r9, r2");
  COMPARE(bic(r0, r5, Operand(r3), SetCC),
          "e1d50003       bics r0, r5, r3");
  COMPARE(bic(r0, r1, Operand(r4), SetCC, pl),
          "51d10004       bicpls r0, r1, r4");

  COMPARE(mvn(r10, Operand(r1)),
          "e1e0a001       mvn r10, r1");
  COMPARE(mvn(r9, Operand(r2)),
          "e1e09002       mvn r9, r2");
  COMPARE(mvn(r0, Operand(r3), SetCC),
          "e1f00003       mvns r0, r3");
  COMPARE(mvn(r5, Operand(r4), SetCC, cc),
          "31f05004       mvnccs r5, r4");

  // Instructions autotransformed by the assembler.
  // mov -> mvn.
  COMPARE(mov(r3, Operand(-1), LeaveCC, al),
          "e3e03000       mvn r3, #0");
  COMPARE(mov(r4, Operand(-2), SetCC, al),
          "e3f04001       mvns r4, #1");
  COMPARE(mov(r5, Operand(0x0FFFFFF0), SetCC, ne),
          "13f052ff       mvnnes r5, #-268435441");
  COMPARE(mov(r6, Operand(-1), LeaveCC, ne),
          "13e06000       mvnne r6, #0");

  // mvn -> mov.
  COMPARE(mvn(r3, Operand(-1), LeaveCC, al),
          "e3a03000       mov r3, #0");
  COMPARE(mvn(r4, Operand(-2), SetCC, al),
          "e3b04001       movs r4, #1");
  COMPARE(mvn(r5, Operand(0x0FFFFFF0), SetCC, ne),
          "13b052ff       movnes r5, #-268435441");
  COMPARE(mvn(r6, Operand(-1), LeaveCC, ne),
          "13a06000       movne r6, #0");

  // mov -> movw.
  if (CpuFeatures::IsSupported(ARMv7)) {
    COMPARE(mov(r5, Operand(0x01234), LeaveCC, ne),
            "13015234       movwne r5, #4660");
    COMPARE(eor(r5, r4, Operand(0x1234), LeaveCC, ne),
            "13015234       movwne r5, #4660",
            "10245005       eorne r5, r4, r5");
    // Movw can't do setcc, so first move to r5, then the following instruction
    // sets the flags. Mov immediate with setcc is pretty strange anyway.
    COMPARE(mov(r5, Operand(0x01234), SetCC, ne),
            "13015234       movwne r5, #4660",
            "11b05005       movnes r5, r5");
    // Emit a literal pool now, otherwise this could be dumped later, in the
    // middle of a different test.
    EMIT_PENDING_LITERALS();

    // The eor does the setcc so we get a movw here.
    COMPARE(eor(r5, r4, Operand(0x1234), SetCC, ne),
            "13015234       movwne r5, #4660",
            "10345005       eornes r5, r4, r5");

    COMPARE(movt(r5, 0x4321, ne),
            "13445321       movtne r5, #17185");
    COMPARE(movw(r5, 0xABCD, eq),
            "030a5bcd       movweq r5, #43981");
  }

  // Eor doesn't have an eor-negative variant, but we can do an mvn followed by
  // an eor to get the same effect.
  COMPARE(eor(r5, r4, Operand(0xFFFFFF34), SetCC, ne),
          "13e050cb       mvnne r5, #203",
          "10345005       eornes r5, r4, r5");

  // and <-> bic.
  COMPARE(and_(r3, r5, Operand(0xFC03FFFF)),
          "e3c537ff       bic r3, r5, #66846720");
  COMPARE(bic(r3, r5, Operand(0xFC03FFFF)),
          "e20537ff       and r3, r5, #66846720");

  // sub <-> add.
  COMPARE(add(r3, r5, Operand(-1024)),
          "e2453b01       sub r3, r5, #1024");
  COMPARE(sub(r3, r5, Operand(-1024)),
          "e2853b01       add r3, r5, #1024");

  // cmp <-> cmn.
  COMPARE(cmp(r3, Operand(-1024)),
          "e3730b01       cmn r3, #1024");
  COMPARE(cmn(r3, Operand(-1024)),
          "e3530b01       cmp r3, #1024");

  // Miscellaneous instructions encoded as type 0.
  COMPARE(blx(ip),
          "e12fff3c       blx ip");
  COMPARE(bkpt(0),
          "e1200070       bkpt 0");
  COMPARE(bkpt(0xFFFF),
          "e12fff7f       bkpt 65535");
  COMPARE(clz(r6, r7),
          "e16f6f17       clz r6, r7");

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, Type1) {
  SET_UP();

  COMPARE(and_(r0, r1, Operand(0x00000000)),
          "e2010000       and r0, r1, #0");
  COMPARE(and_(r1, r2, Operand(0x00000001), LeaveCC),
          "e2021001       and r1, r2, #1");
  COMPARE(and_(r2, r3, Operand(0x00000010), SetCC),
          "e2132010       ands r2, r3, #16");
  COMPARE(and_(r3, r4, Operand(0x00000100), LeaveCC, eq),
          "02043c01       andeq r3, r4, #256");
  COMPARE(and_(r4, r5, Operand(0x00001000), SetCC, ne),
          "12154a01       andnes r4, r5, #4096");

  COMPARE(eor(r4, r5, Operand(0x00001000)),
          "e2254a01       eor r4, r5, #4096");
  COMPARE(eor(r4, r4, Operand(0x00010000), LeaveCC),
          "e2244801       eor r4, r4, #65536");
  COMPARE(eor(r4, r3, Operand(0x00100000), SetCC),
          "e2334601       eors r4, r3, #1048576");
  COMPARE(eor(r4, r2, Operand(0x01000000), LeaveCC, cs),
          "22224401       eorcs r4, r2, #16777216");
  COMPARE(eor(r4, r1, Operand(0x10000000), SetCC, cc),
          "32314201       eorccs r4, r1, #268435456");

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, Type3) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatureScope scope(&assm, ARMv7);
    COMPARE(ubfx(r0, r1, 5, 10),
            "e7e902d1       ubfx r0, r1, #5, #10");
    COMPARE(ubfx(r1, r0, 5, 10),
            "e7e912d0       ubfx r1, r0, #5, #10");
    COMPARE(ubfx(r0, r1, 31, 1),
            "e7e00fd1       ubfx r0, r1, #31, #1");
    COMPARE(ubfx(r1, r0, 31, 1),
            "e7e01fd0       ubfx r1, r0, #31, #1");

    COMPARE(sbfx(r0, r1, 5, 10),
            "e7a902d1       sbfx r0, r1, #5, #10");
    COMPARE(sbfx(r1, r0, 5, 10),
            "e7a912d0       sbfx r1, r0, #5, #10");
    COMPARE(sbfx(r0, r1, 31, 1),
            "e7a00fd1       sbfx r0, r1, #31, #1");
    COMPARE(sbfx(r1, r0, 31, 1),
            "e7a01fd0       sbfx r1, r0, #31, #1");

    COMPARE(bfc(r0, 5, 10),
            "e7ce029f       bfc r0, #5, #10");
    COMPARE(bfc(r1, 5, 10),
            "e7ce129f       bfc r1, #5, #10");
    COMPARE(bfc(r0, 31, 1),
            "e7df0f9f       bfc r0, #31, #1");
    COMPARE(bfc(r1, 31, 1),
            "e7df1f9f       bfc r1, #31, #1");

    COMPARE(bfi(r0, r1, 5, 10),
            "e7ce0291       bfi r0, r1, #5, #10");
    COMPARE(bfi(r1, r0, 5, 10),
            "e7ce1290       bfi r1, r0, #5, #10");
    COMPARE(bfi(r0, r1, 31, 1),
            "e7df0f91       bfi r0, r1, #31, #1");
    COMPARE(bfi(r1, r0, 31, 1),
            "e7df1f90       bfi r1, r0, #31, #1");

    COMPARE(pkhbt(r3, r4, Operand(r5, LSL, 17)),
            "e6843895       pkhbt r3, r4, r5, lsl #17");
    COMPARE(pkhtb(r3, r4, Operand(r5, ASR, 17)),
            "e68438d5       pkhtb r3, r4, r5, asr #17");

    COMPARE(sxtb(r1, r7, 0, eq), "06af1077       sxtbeq r1, r7");
    COMPARE(sxtb(r0, r0, 8, ne), "16af0470       sxtbne r0, r0, ror #8");
    COMPARE(sxtb(r9, r10, 16), "e6af987a       sxtb r9, r10, ror #16");
    COMPARE(sxtb(r4, r3, 24), "e6af4c73       sxtb r4, r3, ror #24");

    COMPARE(sxtab(r3, r4, r5), "e6a43075       sxtab r3, r4, r5");

    COMPARE(sxth(r5, r0), "e6bf5070       sxth r5, r0");
    COMPARE(sxth(r5, r9, 8), "e6bf5479       sxth r5, r9, ror #8");
    COMPARE(sxth(r5, r9, 16, hi), "86bf5879       sxthhi r5, r9, ror #16");
    COMPARE(sxth(r8, r9, 24, cc), "36bf8c79       sxthcc r8, r9, ror #24");

    COMPARE(sxtah(r3, r4, r5, 16), "e6b43875       sxtah r3, r4, r5, ror #16");

    COMPARE(uxtb(r9, r10), "e6ef907a       uxtb r9, r10");
    COMPARE(uxtb(r3, r4, 8), "e6ef3474       uxtb r3, r4, ror #8");

    COMPARE(uxtab(r3, r4, r5, 8), "e6e43475       uxtab r3, r4, r5, ror #8");

    COMPARE(uxtb16(r3, r4, 8), "e6cf3474       uxtb16 r3, r4, ror #8");

    COMPARE(uxth(r9, r10), "e6ff907a       uxth r9, r10");
    COMPARE(uxth(r3, r4, 8), "e6ff3474       uxth r3, r4, ror #8");

    COMPARE(uxtah(r3, r4, r5, 24), "e6f43c75       uxtah r3, r4, r5, ror #24");

    COMPARE(rbit(r1, r2), "e6ff1f32       rbit r1, r2");
    COMPARE(rbit(r10, ip), "e6ffaf3c       rbit r10, ip");

    COMPARE(rev(r1, r2), "e6bf1f32       rev r1, r2");
    COMPARE(rev(r10, ip), "e6bfaf3c       rev r10, ip");
  }

  COMPARE(usat(r0, 1, Operand(r1)),
          "e6e10011       usat r0, #1, r1");
  COMPARE(usat(r2, 7, Operand(lr)),
          "e6e7201e       usat r2, #7, lr");
  COMPARE(usat(r3, 31, Operand(r4, LSL, 31)),
          "e6ff3f94       usat r3, #31, r4, lsl #31");
  COMPARE(usat(r8, 0, Operand(r5, ASR, 17)),
          "e6e088d5       usat r8, #0, r5, asr #17");

  COMPARE(smmla(r0, r1, r2, r3), "e7503211       smmla r0, r1, r2, r3");
  COMPARE(smmla(r10, r9, r8, r7), "e75a7819       smmla r10, r9, r8, r7");

  COMPARE(smmul(r0, r1, r2), "e750f211       smmul r0, r1, r2");
  COMPARE(smmul(r8, r9, r10), "e758fa19       smmul r8, r9, r10");

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, msr_mrs_disasm) {
  SET_UP();

  SRegisterFieldMask CPSR_all = CPSR_f | CPSR_s | CPSR_x | CPSR_c;
  SRegisterFieldMask SPSR_all = SPSR_f | SPSR_s | SPSR_x | SPSR_c;

  COMPARE(msr(CPSR_f, Operand(r0)),       "e128f000       msr CPSR_f, r0");
  COMPARE(msr(CPSR_s, Operand(r1)),       "e124f001       msr CPSR_s, r1");
  COMPARE(msr(CPSR_x, Operand(r2)),       "e122f002       msr CPSR_x, r2");
  COMPARE(msr(CPSR_c, Operand(r3)),       "e121f003       msr CPSR_c, r3");
  COMPARE(msr(CPSR_all, Operand(ip)),     "e12ff00c       msr CPSR_fsxc, ip");
  COMPARE(msr(SPSR_f, Operand(r0)),       "e168f000       msr SPSR_f, r0");
  COMPARE(msr(SPSR_s, Operand(r1)),       "e164f001       msr SPSR_s, r1");
  COMPARE(msr(SPSR_x, Operand(r2)),       "e162f002       msr SPSR_x, r2");
  COMPARE(msr(SPSR_c, Operand(r3)),       "e161f003       msr SPSR_c, r3");
  COMPARE(msr(SPSR_all, Operand(ip)),     "e16ff00c       msr SPSR_fsxc, ip");
  COMPARE(msr(CPSR_f, Operand(r0), eq),   "0128f000       msreq CPSR_f, r0");
  COMPARE(msr(CPSR_s, Operand(r1), ne),   "1124f001       msrne CPSR_s, r1");
  COMPARE(msr(CPSR_x, Operand(r2), cs),   "2122f002       msrcs CPSR_x, r2");
  COMPARE(msr(CPSR_c, Operand(r3), cc),   "3121f003       msrcc CPSR_c, r3");
  COMPARE(msr(CPSR_all, Operand(ip), mi), "412ff00c       msrmi CPSR_fsxc, ip");
  COMPARE(msr(SPSR_f, Operand(r0), pl),   "5168f000       msrpl SPSR_f, r0");
  COMPARE(msr(SPSR_s, Operand(r1), vs),   "6164f001       msrvs SPSR_s, r1");
  COMPARE(msr(SPSR_x, Operand(r2), vc),   "7162f002       msrvc SPSR_x, r2");
  COMPARE(msr(SPSR_c, Operand(r3), hi),   "8161f003       msrhi SPSR_c, r3");
  COMPARE(msr(SPSR_all, Operand(ip), ls), "916ff00c       msrls SPSR_fsxc, ip");

  // Other combinations of mask bits.
  COMPARE(msr(CPSR_s | CPSR_x, Operand(r4)),
          "e126f004       msr CPSR_sx, r4");
  COMPARE(msr(SPSR_s | SPSR_x | SPSR_c, Operand(r5)),
          "e167f005       msr SPSR_sxc, r5");
  COMPARE(msr(SPSR_s | SPSR_c, Operand(r6)),
          "e165f006       msr SPSR_sc, r6");
  COMPARE(msr(SPSR_f | SPSR_c, Operand(r7)),
          "e169f007       msr SPSR_fc, r7");
  // MSR with no mask is UNPREDICTABLE, and checked by the assembler, but check
  // that the disassembler does something sensible.
  COMPARE(dd(0xE120F008), "e120f008       msr CPSR_(none), r8");

  COMPARE(mrs(r0, CPSR),     "e10f0000       mrs r0, CPSR");
  COMPARE(mrs(r1, SPSR),     "e14f1000       mrs r1, SPSR");
  COMPARE(mrs(r2, CPSR, ge), "a10f2000       mrsge r2, CPSR");
  COMPARE(mrs(r3, SPSR, lt), "b14f3000       mrslt r3, SPSR");

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, Vfp) {
  SET_UP();

  if (CpuFeatures::IsSupported(VFPv3)) {
    CpuFeatureScope scope(&assm, VFPv3);
    COMPARE(vmov(d0, r2, r3),
            "ec432b10       vmov d0, r2, r3");
    COMPARE(vmov(r2, r3, d0),
            "ec532b10       vmov r2, r3, d0");
    COMPARE(vmov(r4, ip, d1),
            "ec5c4b11       vmov r4, ip, d1");
    COMPARE(vmov(d0, d1),
            "eeb00b41       vmov.f64 d0, d1");
    COMPARE(vmov(d3, d3, eq),
            "0eb03b43       vmoveq.f64 d3, d3");

    COMPARE(vmov(s0, s31),
            "eeb00a6f       vmov.f32 s0, s31");
    COMPARE(vmov(s31, s0),
            "eef0fa40       vmov.f32 s31, s0");
    COMPARE(vmov(r0, s0),
            "ee100a10       vmov r0, s0");
    COMPARE(vmov(r10, s31),
            "ee1faa90       vmov r10, s31");
    COMPARE(vmov(s0, r0),
            "ee000a10       vmov s0, r0");
    COMPARE(vmov(s31, r10),
            "ee0faa90       vmov s31, r10");

    COMPARE(vabs(d0, d1),
            "eeb00bc1       vabs.f64 d0, d1");
    COMPARE(vabs(d3, d4, mi),
            "4eb03bc4       vabsmi.f64 d3, d4");

    COMPARE(vabs(s0, s1),
            "eeb00ae0       vabs.f32 s0, s1");
    COMPARE(vabs(s3, s4, mi),
            "4ef01ac2       vabsmi.f32 s3, s4");

    COMPARE(vneg(d0, d1),
            "eeb10b41       vneg.f64 d0, d1");
    COMPARE(vneg(d3, d4, mi),
            "4eb13b44       vnegmi.f64 d3, d4");

    COMPARE(vneg(s0, s1),
            "eeb10a60       vneg.f32 s0, s1");
    COMPARE(vneg(s3, s4, mi),
            "4ef11a42       vnegmi.f32 s3, s4");

    COMPARE(vadd(d0, d1, d2),
            "ee310b02       vadd.f64 d0, d1, d2");
    COMPARE(vadd(d3, d4, d5, mi),
            "4e343b05       vaddmi.f64 d3, d4, d5");

    COMPARE(vadd(s0, s1, s2),
            "ee300a81       vadd.f32 s0, s1, s2");
    COMPARE(vadd(s3, s4, s5, mi),
            "4e721a22       vaddmi.f32 s3, s4, s5");

    COMPARE(vsub(d0, d1, d2),
            "ee310b42       vsub.f64 d0, d1, d2");
    COMPARE(vsub(d3, d4, d5, ne),
            "1e343b45       vsubne.f64 d3, d4, d5");

    COMPARE(vsub(s0, s1, s2),
            "ee300ac1       vsub.f32 s0, s1, s2");
    COMPARE(vsub(s3, s4, s5, ne),
            "1e721a62       vsubne.f32 s3, s4, s5");

    COMPARE(vmul(d2, d1, d0),
            "ee212b00       vmul.f64 d2, d1, d0");
    COMPARE(vmul(d6, d4, d5, cc),
            "3e246b05       vmulcc.f64 d6, d4, d5");

    COMPARE(vmul(s2, s1, s0),
            "ee201a80       vmul.f32 s2, s1, s0");
    COMPARE(vmul(s6, s4, s5, cc),
            "3e223a22       vmulcc.f32 s6, s4, s5");

    COMPARE(vdiv(d2, d2, d2),
            "ee822b02       vdiv.f64 d2, d2, d2");
    COMPARE(vdiv(d6, d7, d7, hi),
            "8e876b07       vdivhi.f64 d6, d7, d7");

    COMPARE(vdiv(s2, s2, s2),
            "ee811a01       vdiv.f32 s2, s2, s2");
    COMPARE(vdiv(s6, s7, s7, hi),
            "8e833aa3       vdivhi.f32 s6, s7, s7");

    COMPARE(vcmp(d0, d1),
            "eeb40b41       vcmp.f64 d0, d1");
    COMPARE(vcmp(d0, 0.0),
            "eeb50b40       vcmp.f64 d0, #0.0");

    COMPARE(vcmp(s0, s1),
            "eeb40a60       vcmp.f32 s0, s1");
    COMPARE(vcmp(s0, 0.0f),
            "eeb50a40       vcmp.f32 s0, #0.0");

    COMPARE(vsqrt(d0, d0),
            "eeb10bc0       vsqrt.f64 d0, d0");
    COMPARE(vsqrt(d2, d3, ne),
            "1eb12bc3       vsqrtne.f64 d2, d3");

    COMPARE(vsqrt(s0, s0),
            "eeb10ac0       vsqrt.f32 s0, s0");
    COMPARE(vsqrt(s2, s3, ne),
            "1eb11ae1       vsqrtne.f32 s2, s3");

    COMPARE(vmov(d0, base::Double(1.0)),
            "eeb70b00       vmov.f64 d0, #1");
    COMPARE(vmov(d2, base::Double(-13.0)),
            "eeba2b0a       vmov.f64 d2, #-13");

    COMPARE(vmov(s1, Float32(-1.0f)),
            "eeff0a00       vmov.f32 s1, #-1");
    COMPARE(vmov(s3, Float32(13.0f)),
            "eef21a0a       vmov.f32 s3, #13");

    COMPARE(vmov(NeonS32, d0, 0, r0),
            "ee000b10       vmov.32 d0[0], r0");
    COMPARE(vmov(NeonS32, d0, 1, r0),
            "ee200b10       vmov.32 d0[1], r0");

    COMPARE(vmov(NeonS32, r2, d15, 0),
            "ee1f2b10       vmov.32 r2, d15[0]");
    COMPARE(vmov(NeonS32, r3, d14, 1),
            "ee3e3b10       vmov.32 r3, d14[1]");

    COMPARE(vldr(s0, r0, 0),
            "ed900a00       vldr s0, [r0 + 4*0]");
    COMPARE(vldr(s1, r1, 4),
            "edd10a01       vldr s1, [r1 + 4*1]");
    COMPARE(vldr(s15, r4, 16),
            "edd47a04       vldr s15, [r4 + 4*4]");
    COMPARE(vldr(s16, r5, 20),
            "ed958a05       vldr s16, [r5 + 4*5]");
    COMPARE(vldr(s31, r10, 1020),
            "eddafaff       vldr s31, [r10 + 4*255]");
    COMPARE(vldr(s31, ip, 1020),
            "eddcfaff       vldr s31, [ip + 4*255]");

    COMPARE(vstr(s0, r0, 0),
            "ed800a00       vstr s0, [r0 + 4*0]");
    COMPARE(vstr(s1, r1, 4),
            "edc10a01       vstr s1, [r1 + 4*1]");
    COMPARE(vstr(s15, r8, 8),
            "edc87a02       vstr s15, [r8 + 4*2]");
    COMPARE(vstr(s16, r9, 12),
            "ed898a03       vstr s16, [r9 + 4*3]");
    COMPARE(vstr(s31, r10, 1020),
            "edcafaff       vstr s31, [r10 + 4*255]");

    COMPARE(vldr(d0, r0, 0),
            "ed900b00       vldr d0, [r0 + 4*0]");
    COMPARE(vldr(d1, r1, 4),
            "ed911b01       vldr d1, [r1 + 4*1]");
    COMPARE(vldr(d15, r10, 1020),
            "ed9afbff       vldr d15, [r10 + 4*255]");
    COMPARE(vstr(d0, r0, 0),
            "ed800b00       vstr d0, [r0 + 4*0]");
    COMPARE(vstr(d1, r1, 4),
            "ed811b01       vstr d1, [r1 + 4*1]");
    COMPARE(vstr(d15, r10, 1020),
            "ed8afbff       vstr d15, [r10 + 4*255]");

    COMPARE(vmsr(r5),
            "eee15a10       vmsr FPSCR, r5");
    COMPARE(vmsr(r10, pl),
            "5ee1aa10       vmsrpl FPSCR, r10");
    COMPARE(vmsr(pc),
            "eee1fa10       vmsr FPSCR, APSR");
    COMPARE(vmrs(r5),
            "eef15a10       vmrs r5, FPSCR");
    COMPARE(vmrs(r10, ge),
            "aef1aa10       vmrsge r10, FPSCR");
    COMPARE(vmrs(pc),
            "eef1fa10       vmrs APSR, FPSCR");

    COMPARE(vstm(ia, r0, d1, d3),
            "ec801b06       vstmia r0, {d1-d3}");
    COMPARE(vldm(ia, r1, d2, d5),
            "ec912b08       vldmia r1, {d2-d5}");
    COMPARE(vstm(ia, r2, d0, d15),
            "ec820b20       vstmia r2, {d0-d15}");
    COMPARE(vldm(ia, r3, d0, d15),
            "ec930b20       vldmia r3, {d0-d15}");
    COMPARE(vstm(ia, r4, s1, s3),
            "ecc40a03       vstmia r4, {s1-s3}");
    COMPARE(vldm(ia, r5, s2, s5),
            "ec951a04       vldmia r5, {s2-s5}");
    COMPARE(vstm(ia, r6, s0, s31),
            "ec860a20       vstmia r6, {s0-s31}");
    COMPARE(vldm(ia, r7, s0, s31),
            "ec970a20       vldmia r7, {s0-s31}");

    COMPARE(vmla(d2, d1, d0),
            "ee012b00       vmla.f64 d2, d1, d0");
    COMPARE(vmla(d6, d4, d5, cc),
            "3e046b05       vmlacc.f64 d6, d4, d5");

    COMPARE(vmla(s2, s1, s0),
            "ee001a80       vmla.f32 s2, s1, s0");
    COMPARE(vmla(s6, s4, s5, cc),
            "3e023a22       vmlacc.f32 s6, s4, s5");

    COMPARE(vmls(d2, d1, d0),
            "ee012b40       vmls.f64 d2, d1, d0");
    COMPARE(vmls(d6, d4, d5, cc),
            "3e046b45       vmlscc.f64 d6, d4, d5");

    COMPARE(vmls(s2, s1, s0),
            "ee001ac0       vmls.f32 s2, s1, s0");
    COMPARE(vmls(s6, s4, s5, cc),
            "3e023a62       vmlscc.f32 s6, s4, s5");

    COMPARE(vcvt_f32_f64(s31, d15),
            "eef7fbcf       vcvt.f32.f64 s31, d15");
    COMPARE(vcvt_f32_s32(s30, s29),
            "eeb8faee       vcvt.f32.s32 s30, s29");
    COMPARE(vcvt_f64_f32(d14, s28),
            "eeb7eace       vcvt.f64.f32 d14, s28");
    COMPARE(vcvt_f64_s32(d13, s27),
            "eeb8dbed       vcvt.f64.s32 d13, s27");
    COMPARE(vcvt_f64_u32(d12, s26),
            "eeb8cb4d       vcvt.f64.u32 d12, s26");
    COMPARE(vcvt_s32_f32(s25, s24),
            "eefdcacc       vcvt.s32.f32 s25, s24");
    COMPARE(vcvt_s32_f64(s23, d11),
            "eefdbbcb       vcvt.s32.f64 s23, d11");
    COMPARE(vcvt_u32_f32(s22, s21),
            "eebcbaea       vcvt.u32.f32 s22, s21");
    COMPARE(vcvt_u32_f64(s20, d10),
            "eebcabca       vcvt.u32.f64 s20, d10");

    COMPARE(vcvt_f64_s32(d9, 2),
            "eeba9bcf       vcvt.f64.s32 d9, d9, #2");

    if (CpuFeatures::IsSupported(VFP32DREGS)) {
      CpuFeatureScope scope(&assm, VFP32DREGS);
      COMPARE(vmov(d3, d27),
              "eeb03b6b       vmov.f64 d3, d27");
      COMPARE(vmov(d18, d7),
              "eef02b47       vmov.f64 d18, d7");
      COMPARE(vmov(d18, r2, r3),
              "ec432b32       vmov d18, r2, r3");
      COMPARE(vmov(r2, r3, d18),
              "ec532b32       vmov r2, r3, d18");
      COMPARE(vmov(d20, d31),
              "eef04b6f       vmov.f64 d20, d31");

      COMPARE(vabs(d16, d31),
              "eef00bef       vabs.f64 d16, d31");

      COMPARE(vneg(d16, d31),
              "eef10b6f       vneg.f64 d16, d31");

      COMPARE(vadd(d16, d17, d18),
              "ee710ba2       vadd.f64 d16, d17, d18");

      COMPARE(vsub(d16, d17, d18),
              "ee710be2       vsub.f64 d16, d17, d18");

      COMPARE(vmul(d16, d17, d18),
              "ee610ba2       vmul.f64 d16, d17, d18");

      COMPARE(vdiv(d16, d17, d18),
              "eec10ba2       vdiv.f64 d16, d17, d18");

      COMPARE(vcmp(d16, d17),
              "eef40b61       vcmp.f64 d16, d17");
      COMPARE(vcmp(d16, 0.0),
              "eef50b40       vcmp.f64 d16, #0.0");

      COMPARE(vsqrt(d16, d17),
              "eef10be1       vsqrt.f64 d16, d17");

      COMPARE(vmov(d30, base::Double(16.0)),
              "eef3eb00       vmov.f64 d30, #16");

      COMPARE(vmov(NeonS32, d31, 0, r7),
              "ee0f7b90       vmov.32 d31[0], r7");
      COMPARE(vmov(NeonS32, d31, 1, r7),
              "ee2f7b90       vmov.32 d31[1], r7");

      COMPARE(vldr(d25, r0, 0),
              "edd09b00       vldr d25, [r0 + 4*0]");
      COMPARE(vldr(d26, r1, 4),
              "edd1ab01       vldr d26, [r1 + 4*1]");
      COMPARE(vldr(d31, r10, 1020),
              "eddafbff       vldr d31, [r10 + 4*255]");

      COMPARE(vstr(d16, r0, 0),
              "edc00b00       vstr d16, [r0 + 4*0]");
      COMPARE(vstr(d17, r1, 4),
              "edc11b01       vstr d17, [r1 + 4*1]");
      COMPARE(vstr(d31, r10, 1020),
              "edcafbff       vstr d31, [r10 + 4*255]");

      COMPARE(vstm(ia, r0, d16, d31),
              "ecc00b20       vstmia r0, {d16-d31}");
      COMPARE(vldm(ia, r3, d16, d31),
              "ecd30b20       vldmia r3, {d16-d31}");
      COMPARE(vstm(ia, r0, d23, d27),
              "ecc07b0a       vstmia r0, {d23-d27}");
      COMPARE(vldm(ia, r3, d23, d27),
              "ecd37b0a       vldmia r3, {d23-d27}");

      COMPARE(vmla(d16, d17, d18),
              "ee410ba2       vmla.f64 d16, d17, d18");

      COMPARE(vcvt_f32_f64(s0, d31),
              "eeb70bef       vcvt.f32.f64 s0, d31");
      COMPARE(vcvt_f32_s32(s1, s2),
              "eef80ac1       vcvt.f32.s32 s1, s2");
      COMPARE(vcvt_f64_f32(d30, s3),
              "eef7eae1       vcvt.f64.f32 d30, s3");
      COMPARE(vcvt_f64_s32(d29, s4),
              "eef8dbc2       vcvt.f64.s32 d29, s4");
      COMPARE(vcvt_f64_u32(d28, s5),
              "eef8cb62       vcvt.f64.u32 d28, s5");
      COMPARE(vcvt_s32_f32(s6, s7),
              "eebd3ae3       vcvt.s32.f32 s6, s7");
      COMPARE(vcvt_s32_f64(s8, d27),
              "eebd4beb       vcvt.s32.f64 s8, d27");
      COMPARE(vcvt_u32_f32(s9, s10),
              "eefc4ac5       vcvt.u32.f32 s9, s10");
      COMPARE(vcvt_u32_f64(s11, d26),
              "eefc5bea       vcvt.u32.f64 s11, d26");

      COMPARE(vcvt_f64_s32(d25, 2),
              "eefa9bcf       vcvt.f64.s32 d25, d25, #2");
    }
  }

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, ARMv8_vrintX_disasm) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);
    COMPARE(vrinta(d0, d0), "feb80b40       vrinta.f64.f64 d0, d0");
    COMPARE(vrinta(d2, d3), "feb82b43       vrinta.f64.f64 d2, d3");

    COMPARE(vrintp(d0, d0), "feba0b40       vrintp.f64.f64 d0, d0");
    COMPARE(vrintp(d2, d3), "feba2b43       vrintp.f64.f64 d2, d3");

    COMPARE(vrintn(d0, d0), "feb90b40       vrintn.f64.f64 d0, d0");
    COMPARE(vrintn(d2, d3), "feb92b43       vrintn.f64.f64 d2, d3");

    COMPARE(vrintm(d0, d0), "febb0b40       vrintm.f64.f64 d0, d0");
    COMPARE(vrintm(d2, d3), "febb2b43       vrintm.f64.f64 d2, d3");

    COMPARE(vrintz(d0, d0), "eeb60bc0       vrintz.f64.f64 d0, d0");
    COMPARE(vrintz(d2, d3, ne), "1eb62bc3       vrintzne.f64.f64 d2, d3");

    // Advanced SIMD
    COMPARE(vrintm(NeonS32, q0, q3), "f3ba06c6       vrintm.f32 q0, q3");
    COMPARE(vrintn(NeonS32, q0, q3), "f3ba0446       vrintn.f32 q0, q3");
    COMPARE(vrintp(NeonS32, q0, q3), "f3ba07c6       vrintp.f32 q0, q3");
    COMPARE(vrintz(NeonS32, q0, q3), "f3ba05c6       vrintz.f32 q0, q3");
  }

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, ARMv8_vminmax_disasm) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);
    COMPARE(vmaxnm(d0, d1, d2), "fe810b02       vmaxnm.f64 d0, d1, d2");
    COMPARE(vminnm(d3, d4, d5), "fe843b45       vminnm.f64 d3, d4, d5");
    COMPARE(vmaxnm(s6, s7, s8), "fe833a84       vmaxnm.f32 s6, s7, s8");
    COMPARE(vminnm(s9, s10, s11), "fec54a65       vminnm.f32 s9, s10, s11");
  }

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, ARMv8_vselX_disasm) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);
    // Native instructions.
    COMPARE(vsel(eq, d0, d1, d2),
            "fe010b02       vseleq.f64 d0, d1, d2");
    COMPARE(vsel(eq, s0, s1, s2),
            "fe000a81       vseleq.f32 s0, s1, s2");
    COMPARE(vsel(ge, d0, d1, d2),
            "fe210b02       vselge.f64 d0, d1, d2");
    COMPARE(vsel(ge, s0, s1, s2),
            "fe200a81       vselge.f32 s0, s1, s2");
    COMPARE(vsel(gt, d0, d1, d2),
            "fe310b02       vselgt.f64 d0, d1, d2");
    COMPARE(vsel(gt, s0, s1, s2),
            "fe300a81       vselgt.f32 s0, s1, s2");
    COMPARE(vsel(vs, d0, d1, d2),
            "fe110b02       vselvs.f64 d0, d1, d2");
    COMPARE(vsel(vs, s0, s1, s2),
            "fe100a81       vselvs.f32 s0, s1, s2");

    // Inverted conditions (and swapped inputs).
    COMPARE(vsel(ne, d0, d1, d2),
            "fe020b01       vseleq.f64 d0, d2, d1");
    COMPARE(vsel(ne, s0, s1, s2),
            "fe010a20       vseleq.f32 s0, s2, s1");
    COMPARE(vsel(lt, d0, d1, d2),
            "fe220b01       vselge.f64 d0, d2, d1");
    COMPARE(vsel(lt, s0, s1, s2),
            "fe210a20       vselge.f32 s0, s2, s1");
    COMPARE(vsel(le, d0, d1, d2),
            "fe320b01       vselgt.f64 d0, d2, d1");
    COMPARE(vsel(le, s0, s1, s2),
            "fe310a20       vselgt.f32 s0, s2, s1");
    COMPARE(vsel(vc, d0, d1, d2),
            "fe120b01       vselvs.f64 d0, d2, d1");
    COMPARE(vsel(vc, s0, s1, s2),
            "fe110a20       vselvs.f32 s0, s2, s1");
  }

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, Neon) {
  SET_UP();

  if (CpuFeatures::IsSupported(NEON)) {
    CpuFeatureScope scope(&assm, NEON);
      COMPARE(vld1(Neon8, NeonListOperand(d4, 1), NeonMemOperand(r1)),
              "f421470f       vld1.8 {d4}, [r1]");
      COMPARE(vld1(Neon8, NeonListOperand(d4, 2), NeonMemOperand(r1)),
              "f4214a0f       vld1.8 {d4, d5}, [r1]");
      COMPARE(vld1(Neon8, NeonListOperand(d4, 3), NeonMemOperand(r1)),
              "f421460f       vld1.8 {d4, d5, d6}, [r1]");
      COMPARE(vld1(Neon8, NeonListOperand(d4, 4), NeonMemOperand(r1)),
              "f421420f       vld1.8 {d4, d5, d6, d7}, [r1]");
      COMPARE(vld1s(Neon32, NeonListOperand(d4, 1), 0, NeonMemOperand(r1)),
              "f4a1480f       vld1.32 {d4[0]}, [r1]");
      COMPARE(vld1s(Neon16, NeonListOperand(d4, 1), 3, NeonMemOperand(r1)),
              "f4a144cf       vld1.16 {d4[3]}, [r1]");
      COMPARE(vld1r(Neon8, NeonListOperand(d4, 1), NeonMemOperand(r1)),
              "f4a14c0f       vld1.8 {d4}, [r1]");
      COMPARE(vld1r(Neon16, NeonListOperand(d4, 2), NeonMemOperand(r1)),
              "f4a14c6f       vld1.16 {d4, d5}, [r1]");
      COMPARE(vst1(Neon8, NeonListOperand(d4, 1), NeonMemOperand(r6)),
              "f406470f       vst1.8 {d4}, [r6]")
      COMPARE(vst1(Neon8, NeonListOperand(q4), NeonMemOperand(r6)),
              "f4068a0f       vst1.8 {d8, d9}, [r6]")
      COMPARE(vst1(Neon8, NeonListOperand(d4, 3), NeonMemOperand(r6)),
              "f406460f       vst1.8 {d4, d5, d6}, [r6]")
      COMPARE(vst1(Neon16, NeonListOperand(d17, 4), NeonMemOperand(r9)),
              "f449124f       vst1.16 {d17, d18, d19, d20}, [r9]");
      COMPARE(vst1s(Neon8, NeonListOperand(d4), 1, NeonMemOperand(r1)),
              "f481402f       vst1.8 {d4[1]}, [r1]");
      COMPARE(vst1s(Neon16, NeonListOperand(d4), 2, NeonMemOperand(r1)),
              "f481448f       vst1.16 {d4[2]}, [r1]");
      COMPARE(vst1s(Neon32, NeonListOperand(d4), 0, NeonMemOperand(r1)),
              "f481480f       vst1.32 {d4[0]}, [r1]");
      COMPARE(vmovl(NeonU8, q3, d1), "f3886a11       vmovl.u8 q3, d1");
      COMPARE(vmovl(NeonU8, q4, d2), "f3888a12       vmovl.u8 q4, d2");
      COMPARE(vmovl(NeonS16, q4, d2), "f2908a12       vmovl.s16 q4, d2");
      COMPARE(vmovl(NeonU32, q4, d2), "f3a08a12       vmovl.u32 q4, d2");

      COMPARE(vqmovn(NeonU8, NeonU8, d16, q8),
              "f3f202e0       vqmovn.u16 d16, q8");
      COMPARE(vqmovn(NeonS16, NeonS16, d16, q8),
              "f3f602a0       vqmovn.s32 d16, q8");
      COMPARE(vqmovn(NeonU32, NeonU32, d2, q4),
              "f3ba22c8       vqmovn.u64 d2, q4");
      COMPARE(vqmovn(NeonU32, NeonS32, d2, q4),
              "f3ba2248       vqmovun.s64 d2, q4");

      COMPARE(vmov(NeonS8, d0, 0, r0), "ee400b10       vmov.8 d0[0], r0");
      COMPARE(vmov(NeonU8, d1, 1, r1), "ee411b30       vmov.8 d1[1], r1");
      COMPARE(vmov(NeonS8, d2, 2, r2), "ee422b50       vmov.8 d2[2], r2");
      COMPARE(vmov(NeonU8, d3, 3, r8), "ee438b70       vmov.8 d3[3], r8");
      COMPARE(vmov(NeonU8, d3, 3, ip), "ee43cb70       vmov.8 d3[3], ip");
      COMPARE(vmov(NeonS8, d4, 4, r0), "ee640b10       vmov.8 d4[4], r0");
      COMPARE(vmov(NeonU8, d5, 5, r1), "ee651b30       vmov.8 d5[5], r1");
      COMPARE(vmov(NeonS8, d6, 6, r2), "ee662b50       vmov.8 d6[6], r2");
      COMPARE(vmov(NeonU8, d7, 7, r8), "ee678b70       vmov.8 d7[7], r8");
      COMPARE(vmov(NeonS16, d0, 0, r0), "ee000b30       vmov.16 d0[0], r0");
      COMPARE(vmov(NeonS16, d1, 1, r1), "ee011b70       vmov.16 d1[1], r1");
      COMPARE(vmov(NeonS16, d2, 2, r2), "ee222b30       vmov.16 d2[2], r2");
      COMPARE(vmov(NeonS16, d3, 3, r7), "ee237b70       vmov.16 d3[3], r7");
      COMPARE(vmov(NeonS16, d3, 3, ip), "ee23cb70       vmov.16 d3[3], ip");
      COMPARE(vmov(NeonS32, d0, 0, r0), "ee000b10       vmov.32 d0[0], r0");
      COMPARE(vmov(NeonU32, d0, 1, r0), "ee200b10       vmov.32 d0[1], r0");

      COMPARE(vmov(NeonS8, r0, d0, 0), "ee500b10       vmov.s8 r0, d0[0]");
      COMPARE(vmov(NeonU8, r1, d1, 1), "eed11b30       vmov.u8 r1, d1[1]");
      COMPARE(vmov(NeonS8, r2, d2, 2), "ee522b50       vmov.s8 r2, d2[2]");
      COMPARE(vmov(NeonU8, r8, d3, 3), "eed38b70       vmov.u8 r8, d3[3]");
      COMPARE(vmov(NeonS8, r0, d4, 4), "ee740b10       vmov.s8 r0, d4[4]");
      COMPARE(vmov(NeonU8, r1, d5, 5), "eef51b30       vmov.u8 r1, d5[5]");
      COMPARE(vmov(NeonS8, r2, d6, 6), "ee762b50       vmov.s8 r2, d6[6]");
      COMPARE(vmov(NeonU8, r8, d7, 7), "eef78b70       vmov.u8 r8, d7[7]");
      COMPARE(vmov(NeonU8, ip, d7, 7), "eef7cb70       vmov.u8 ip, d7[7]");
      COMPARE(vmov(NeonS16, r0, d0, 0), "ee100b30       vmov.s16 r0, d0[0]");
      COMPARE(vmov(NeonU16, r1, d1, 1), "ee911b70       vmov.u16 r1, d1[1]");
      COMPARE(vmov(NeonS16, r2, d2, 2), "ee322b30       vmov.s16 r2, d2[2]");
      COMPARE(vmov(NeonU16, r7, d3, 3), "eeb37b70       vmov.u16 r7, d3[3]");
      COMPARE(vmov(NeonS32, r2, d15, 0), "ee1f2b10       vmov.32 r2, d15[0]");
      COMPARE(vmov(NeonS32, r3, d14, 1), "ee3e3b10       vmov.32 r3, d14[1]");

      COMPARE(vmov(q0, q15),
              "f22e01fe       vmov q0, q15");
      COMPARE(vmov(q8, q9),
              "f26201f2       vmov q8, q9");
      COMPARE(vmov(q1, 0),
              "f2802050       vmov.i32 q1, 0");
      COMPARE(vmov(q1, 0x0000001200000012),
              "f2812052       vmov.i32 q1, 18");
      COMPARE(vmov(q0, 0xffffffffffffffff),
              "f3870e5f       vmov.i8 q0, 255");
      COMPARE(vmov(d0, 0xffffffffffffffff),
              "f3870e1f       vmov.i8 q0, 255");
      COMPARE(vmvn(q0, q15),
              "f3b005ee       vmvn q0, q15");
      COMPARE(vmvn(q8, q9),
              "f3f005e2       vmvn q8, q9");
      COMPARE(vswp(d0, d31),
              "f3b2002f       vswp d0, d31");
      COMPARE(vswp(d16, d14),
              "f3f2000e       vswp d16, d14");
      COMPARE(vswp(q0, q15),
              "f3b2006e       vswp q0, q15");
      COMPARE(vswp(q8, q9),
              "f3f20062       vswp q8, q9");
      COMPARE(vdup(Neon8, q0, r0),
              "eee00b10       vdup.8 q0, r0");
      COMPARE(vdup(Neon16, q1, r4),
              "eea24b30       vdup.16 q1, r4");
      COMPARE(vdup(Neon32, q15, r1),
              "eeae1b90       vdup.32 q15, r1");
      COMPARE(vdup(Neon32, q0, d1, 1),
              "f3bc0c41       vdup.32 q0, d1[1]");
      COMPARE(vdup(Neon32, q15, d1, 0),
              "f3f4ec41       vdup.32 q15, d1[0]");
      COMPARE(vdup(Neon16, q7, d8, 3),
              "f3beec48       vdup.16 q7, d8[3]");
      COMPARE(vdup(Neon32, d0, d30, 0),
              "f3b40c2e       vdup.32 d0, d30[0]");
      COMPARE(vcvt_f32_s32(q15, q1),
              "f3fbe642       vcvt.f32.s32 q15, q1");
      COMPARE(vcvt_f32_u32(q8, q9),
              "f3fb06e2       vcvt.f32.u32 q8, q9");
      COMPARE(vcvt_s32_f32(q15, q1),
              "f3fbe742       vcvt.s32.f32 q15, q1");
      COMPARE(vcvt_u32_f32(q8, q9),
              "f3fb07e2       vcvt.u32.f32 q8, q9");
      COMPARE(vclt(Neon8, q1, q3, 0),
              "f3b12246       vclt.s8 q1, q3, #0");
      COMPARE(vclt(Neon16, q1, q3, 0),
              "f3b52246       vclt.s16 q1, q3, #0");
      COMPARE(vclt(Neon32, q1, q3, 0),
              "f3b92246       vclt.s32 q1, q3, #0");
      COMPARE(vabs(q0, q1),
              "f3b90742       vabs.f32 q0, q1");
      COMPARE(vabs(Neon8, q6, q7),
              "f3b1c34e       vabs.s8 q6, q7");
      COMPARE(vabs(Neon16, q0, q1),
              "f3b50342       vabs.s16 q0, q1");
      COMPARE(vabs(Neon32, q0, q1),
              "f3b90342       vabs.s32 q0, q1");
      COMPARE(vneg(q0, q1),
              "f3b907c2       vneg.f32 q0, q1");
      COMPARE(vneg(Neon8, q6, q7),
              "f3b1c3ce       vneg.s8 q6, q7");
      COMPARE(vneg(Neon16, q0, q1),
              "f3b503c2       vneg.s16 q0, q1");
      COMPARE(vneg(Neon32, q0, q1),
              "f3b903c2       vneg.s32 q0, q1");
      COMPARE(veor(d0, d1, d2),
              "f3010112       veor d0, d1, d2");
      COMPARE(veor(d0, d30, d31),
              "f30e01bf       veor d0, d30, d31");
      COMPARE(veor(q0, q1, q2),
              "f3020154       veor q0, q1, q2");
      COMPARE(veor(q15, q0, q8),
              "f340e170       veor q15, q0, q8");
      COMPARE(vand(q15, q0, q8),
              "f240e170       vand q15, q0, q8");
      COMPARE(vbic(q15, q0, q8),
              "f250e170       vbic q15, q0, q8");
      COMPARE(vorr(q15, q0, q8),
              "f260e170       vorr q15, q0, q8");
      COMPARE(vmin(q15, q0, q8),
              "f260ef60       vmin.f32 q15, q0, q8");
      COMPARE(vmax(q15, q0, q8),
              "f240ef60       vmax.f32 q15, q0, q8");
      COMPARE(vmax(NeonS8, q0, q1, q2),
              "f2020644       vmax.s8 q0, q1, q2");
      COMPARE(vmin(NeonU16, q1, q2, q8),
              "f3142670       vmin.u16 q1, q2, q8");
      COMPARE(vmax(NeonS32, q15, q0, q8),
              "f260e660       vmax.s32 q15, q0, q8");
      COMPARE(vpadd(d0, d1, d2),
              "f3010d02       vpadd.f32 d0, d1, d2");
      COMPARE(vpadd(Neon8, d0, d1, d2),
              "f2010b12       vpadd.i8 d0, d1, d2");
      COMPARE(vpadd(Neon16, d0, d1, d2),
              "f2110b12       vpadd.i16 d0, d1, d2");
      COMPARE(vpadd(Neon32, d0, d1, d2),
              "f2210b12       vpadd.i32 d0, d1, d2");
      COMPARE(vpadal(NeonS8, q0, q1),
              "f3b00642       vpadal.s8 q0, q1");
      COMPARE(vpadal(NeonS16, q0, q1),
              "f3b40642       vpadal.s16 q0, q1");
      COMPARE(vpadal(NeonS32, q0, q1),
              "f3b80642       vpadal.s32 q0, q1");
      COMPARE(vpadal(NeonU8, q14, q15),
              "f3f0c6ee       vpadal.u8 q14, q15");
      COMPARE(vpadal(NeonU16, q14, q15),
              "f3f4c6ee       vpadal.u16 q14, q15");
      COMPARE(vpadal(NeonU32, q14, q15),
              "f3f8c6ee       vpadal.u32 q14, q15");
      COMPARE(vpaddl(NeonS8, q0, q1),
              "f3b00242       vpaddl.s8 q0, q1");
      COMPARE(vpaddl(NeonS16, q0, q1),
              "f3b40242       vpaddl.s16 q0, q1");
      COMPARE(vpaddl(NeonS32, q0, q1),
              "f3b80242       vpaddl.s32 q0, q1");
      COMPARE(vpaddl(NeonU8, q14, q15),
              "f3f0c2ee       vpaddl.u8 q14, q15");
      COMPARE(vpaddl(NeonU16, q14, q15),
              "f3f4c2ee       vpaddl.u16 q14, q15");
      COMPARE(vpaddl(NeonU32, q14, q15),
              "f3f8c2ee       vpaddl.u32 q14, q15");
      COMPARE(vpmax(NeonS8, d0, d1, d2),
              "f2010a02       vpmax.s8 d0, d1, d2");
      COMPARE(vpmin(NeonU16, d1, d2, d8),
              "f3121a18       vpmin.u16 d1, d2, d8");
      COMPARE(vpmax(NeonS32, d15, d0, d8),
              "f220fa08       vpmax.s32 d15, d0, d8");
      COMPARE(vadd(q15, q0, q8),
              "f240ed60       vadd.f32 q15, q0, q8");
      COMPARE(vadd(Neon8, q0, q1, q2),
              "f2020844       vadd.i8 q0, q1, q2");
      COMPARE(vadd(Neon16, q1, q2, q8),
              "f2142860       vadd.i16 q1, q2, q8");
      COMPARE(vadd(Neon32, q15, q0, q8),
              "f260e860       vadd.i32 q15, q0, q8");
      COMPARE(vqadd(NeonU8, q0, q1, q2),
              "f3020054       vqadd.u8 q0, q1, q2");
      COMPARE(vqadd(NeonS16, q1, q2, q8),
              "f2142070       vqadd.s16 q1, q2, q8");
      COMPARE(vqadd(NeonU32, q15, q0, q8),
              "f360e070       vqadd.u32 q15, q0, q8");
      COMPARE(vsub(q15, q0, q8),
              "f260ed60       vsub.f32 q15, q0, q8");
      COMPARE(vsub(Neon8, q0, q1, q2),
              "f3020844       vsub.i8 q0, q1, q2");
      COMPARE(vsub(Neon16, q1, q2, q8),
              "f3142860       vsub.i16 q1, q2, q8");
      COMPARE(vsub(Neon32, q15, q0, q8),
              "f360e860       vsub.i32 q15, q0, q8");
      COMPARE(vqsub(NeonU8, q0, q1, q2),
              "f3020254       vqsub.u8 q0, q1, q2");
      COMPARE(vqsub(NeonS16, q1, q2, q8),
              "f2142270       vqsub.s16 q1, q2, q8");
      COMPARE(vqsub(NeonU32, q15, q0, q8),
              "f360e270       vqsub.u32 q15, q0, q8");
      COMPARE(vmul(q0, q1, q2),
              "f3020d54       vmul.f32 q0, q1, q2");
      COMPARE(vmul(Neon8, q0, q1, q2),
              "f2020954       vmul.i8 q0, q1, q2");
      COMPARE(vmul(Neon16, q1, q2, q8),
              "f2142970       vmul.i16 q1, q2, q8");
      COMPARE(vmul(Neon32, q15, q0, q8),
              "f260e970       vmul.i32 q15, q0, q8");

      COMPARE(vqrdmulh(NeonS16, q0, q1, q8),
              "f3120b60       vqrdmulh.s16 q0, q1, q8");
      COMPARE(vqrdmulh(NeonS32, q15, q0, q8),
              "f360eb60       vqrdmulh.s32 q15, q0, q8");

      COMPARE(vmull(NeonU8, q15, d0, d8),
              "f3c0ec08       vmull.u8 q15, d0, d8");
      COMPARE(vmull(NeonS16, q15, d0, d8),
              "f2d0ec08       vmull.s16 q15, d0, d8");
      COMPARE(vmull(NeonU32, q15, d0, d8),
              "f3e0ec08       vmull.u32 q15, d0, d8");
      COMPARE(vmlal(NeonU32, q15, d0, d8),
              "f3e0e808       vmlal.u32 q15, d0, d8");

      COMPARE(vshl(NeonS8, q15, q0, 6),
              "f2cee550       vshl.i8 q15, q0, #6");
      COMPARE(vshl(NeonU16, q15, q0, 10),
              "f2dae550       vshl.i16 q15, q0, #10");
      COMPARE(vshl(NeonS32, q15, q0, 17),
              "f2f1e550       vshl.i32 q15, q0, #17");
      COMPARE(vshl(NeonS64, q15, q0, 40),
              "f2e8e5d0       vshl.i64 q15, q0, #40");
      COMPARE(vshl(NeonS8, q15, q0, q1),
              "f242e440       vshl.s8 q15, q0, q1");
      COMPARE(vshl(NeonU16, q15, q2, q3),
              "f356e444       vshl.u16 q15, q2, q3");
      COMPARE(vshl(NeonS32, q15, q4, q5),
              "f26ae448       vshl.s32 q15, q4, q5");
      COMPARE(vshr(NeonS8, q15, q0, 6),
              "f2cae050       vshr.s8 q15, q0, #6");
      COMPARE(vshr(NeonU16, q15, q0, 10),
              "f3d6e050       vshr.u16 q15, q0, #10");
      COMPARE(vshr(NeonS32, q15, q0, 17),
              "f2efe050       vshr.s32 q15, q0, #17");
      COMPARE(vshr(NeonS64, q15, q0, 40),
              "f2d8e0d0       vshr.s64 q15, q0, #40");
      COMPARE(vshr(NeonU64, q15, q0, 40),
              "f3d8e0d0       vshr.u64 q15, q0, #40");
      COMPARE(vsli(Neon64, d2, d0, 32),
              "f3a02590       vsli.64 d2, d0, #32");
      COMPARE(vsli(Neon32, d7, d8, 17),
              "f3b17518       vsli.32 d7, d8, #17");
      COMPARE(vsri(Neon64, d2, d0, 32),
              "f3a02490       vsri.64 d2, d0, #32");
      COMPARE(vsri(Neon16, d7, d8, 8),
              "f3987418       vsri.16 d7, d8, #8");
      COMPARE(vrecpe(q15, q0),
              "f3fbe540       vrecpe.f32 q15, q0");
      COMPARE(vrecps(q15, q0, q8),
              "f240ef70       vrecps.f32 q15, q0, q8");
      COMPARE(vrsqrte(q15, q0),
              "f3fbe5c0       vrsqrte.f32 q15, q0");
      COMPARE(vrsqrts(q15, q0, q8),
              "f260ef70       vrsqrts.f32 q15, q0, q8");
      COMPARE(vtst(Neon8, q0, q1, q2),
              "f2020854       vtst.i8 q0, q1, q2");
      COMPARE(vtst(Neon16, q1, q2, q8),
              "f2142870       vtst.i16 q1, q2, q8");
      COMPARE(vtst(Neon32, q15, q0, q8),
              "f260e870       vtst.i32 q15, q0, q8");
      COMPARE(vceq(q0, q1, q2),
              "f2020e44       vceq.f32 q0, q1, q2");
      COMPARE(vcge(q0, q1, q2),
              "f3020e44       vcge.f32 q0, q1, q2");
      COMPARE(vcgt(q0, q1, q2),
              "f3220e44       vcgt.f32 q0, q1, q2");
      COMPARE(vceq(Neon8, q0, q1, q2),
              "f3020854       vceq.i8 q0, q1, q2");
      COMPARE(vceq(Neon16, q1, q2, q8),
              "f3142870       vceq.i16 q1, q2, q8");
      COMPARE(vceq(Neon32, q15, q0, q8),
              "f360e870       vceq.i32 q15, q0, q8");
      COMPARE(vcge(NeonS8, q0, q1, q2),
              "f2020354       vcge.s8 q0, q1, q2");
      COMPARE(vcge(NeonU16, q1, q2, q8),
              "f3142370       vcge.u16 q1, q2, q8");
      COMPARE(vcge(NeonS32, q15, q0, q8),
              "f260e370       vcge.s32 q15, q0, q8");
      COMPARE(vcgt(NeonS8, q0, q1, q2),
              "f2020344       vcgt.s8 q0, q1, q2");
      COMPARE(vcgt(NeonU16, q1, q2, q8),
              "f3142360       vcgt.u16 q1, q2, q8");
      COMPARE(vcgt(NeonS32, q15, q0, q8),
              "f260e360       vcgt.s32 q15, q0, q8");
      COMPARE(vrhadd(NeonU8, q0, q1, q2),
              "f3020144       vrhadd.u8 q0, q1, q2");
      COMPARE(vrhadd(NeonU16, q1, q2, q8),
              "f3142160       vrhadd.u16 q1, q2, q8");
      COMPARE(vrhadd(NeonU32, q15, q0, q8),
              "f360e160       vrhadd.u32 q15, q0, q8");
      COMPARE(vbsl(q0, q1, q2),
              "f3120154       vbsl q0, q1, q2");
      COMPARE(vbsl(q15, q0, q8),
              "f350e170       vbsl q15, q0, q8");
      COMPARE(vext(q15, q0, q8, 3),
              "f2f0e360       vext.8 q15, q0, q8, #3");
      COMPARE(vzip(Neon16, d15, d0),
              "f3b6f180       vzip.16 d15, d0");
      COMPARE(vzip(Neon16, q15, q0),
              "f3f6e1c0       vzip.16 q15, q0");
      COMPARE(vuzp(Neon16, d15, d0),
              "f3b6f100       vuzp.16 d15, d0");
      COMPARE(vuzp(Neon16, q15, q0),
              "f3f6e140       vuzp.16 q15, q0");
      COMPARE(vrev16(Neon8, q15, q0),
              "f3f0e140       vrev16.8 q15, q0");
      COMPARE(vrev32(Neon8, q15, q0),
              "f3f0e0c0       vrev32.8 q15, q0");
      COMPARE(vrev64(Neon8, q15, q0),
              "f3f0e040       vrev64.8 q15, q0");
      COMPARE(vtrn(Neon16, d15, d0),
              "f3b6f080       vtrn.16 d15, d0");
      COMPARE(vtrn(Neon16, q15, q0),
              "f3f6e0c0       vtrn.16 q15, q0");
      COMPARE(vtbl(d0, NeonListOperand(d1, 1), d2),
              "f3b10802       vtbl.8 d0, {d1}, d2");
      COMPARE(vtbl(d31, NeonListOperand(d0, 2), d4),
              "f3f0f904       vtbl.8 d31, {d0, d1}, d4");
      COMPARE(vtbl(d15, NeonListOperand(d1, 3), d5),
              "f3b1fa05       vtbl.8 d15, {d1, d2, d3}, d5");
      COMPARE(vtbl(d15, NeonListOperand(d1, 4), d5),
              "f3b1fb05       vtbl.8 d15, {d1, d2, d3, d4}, d5");
      COMPARE(vtbx(d0, NeonListOperand(d1, 1), d2),
              "f3b10842       vtbx.8 d0, {d1}, d2");
      COMPARE(vtbx(d31, NeonListOperand(d0, 2), d4),
              "f3f0f944       vtbx.8 d31, {d0, d1}, d4");
      COMPARE(vtbx(d15, NeonListOperand(d1, 3), d5),
              "f3b1fa45       vtbx.8 d15, {d1, d2, d3}, d5");
      COMPARE(vtbx(d15, NeonListOperand(d1, 4), d5),
              "f3b1fb45       vtbx.8 d15, {d1, d2, d3, d4}, d5");
      COMPARE(vcnt(q1, q2),
              "f3b02544       vcnt.8 q1, q2");
  }

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, LoadStore) {
  SET_UP();

  COMPARE(ldrb(r0, MemOperand(r1)),
          "e5d10000       ldrb r0, [r1, #+0]");
  COMPARE(ldrb(r2, MemOperand(r3, 42)),
          "e5d3202a       ldrb r2, [r3, #+42]");
  COMPARE(ldrb(r4, MemOperand(r5, -42)),
          "e555402a       ldrb r4, [r5, #-42]");
  COMPARE(ldrb(r6, MemOperand(r7, 42, PostIndex)),
          "e4d7602a       ldrb r6, [r7], #+42");
  COMPARE(ldrb(r8, MemOperand(r9, -42, PostIndex)),
          "e459802a       ldrb r8, [r9], #-42");
  COMPARE(ldrb(r10, MemOperand(fp, 42, PreIndex)),
          "e5fba02a       ldrb r10, [fp, #+42]!");
  COMPARE(ldrb(ip, MemOperand(sp, -42, PreIndex)),
          "e57dc02a       ldrb ip, [sp, #-42]!");
  COMPARE(ldrb(r0, MemOperand(r1, r2)),
          "e7d10002       ldrb r0, [r1, +r2]");
  COMPARE(ldrb(r0, MemOperand(r1, r2, NegOffset)),
          "e7510002       ldrb r0, [r1, -r2]");
  COMPARE(ldrb(r0, MemOperand(r1, r2, PostIndex)),
          "e6d10002       ldrb r0, [r1], +r2");
  COMPARE(ldrb(r0, MemOperand(r1, r2, NegPostIndex)),
          "e6510002       ldrb r0, [r1], -r2");
  COMPARE(ldrb(r0, MemOperand(r1, r2, PreIndex)),
          "e7f10002       ldrb r0, [r1, +r2]!");
  COMPARE(ldrb(r0, MemOperand(r1, r2, NegPreIndex)),
          "e7710002       ldrb r0, [r1, -r2]!");

  COMPARE(strb(r0, MemOperand(r1)),
          "e5c10000       strb r0, [r1, #+0]");
  COMPARE(strb(r2, MemOperand(r3, 42)),
          "e5c3202a       strb r2, [r3, #+42]");
  COMPARE(strb(r4, MemOperand(r5, -42)),
          "e545402a       strb r4, [r5, #-42]");
  COMPARE(strb(r6, MemOperand(r7, 42, PostIndex)),
          "e4c7602a       strb r6, [r7], #+42");
  COMPARE(strb(r8, MemOperand(r9, -42, PostIndex)),
          "e449802a       strb r8, [r9], #-42");
  COMPARE(strb(r10, MemOperand(fp, 42, PreIndex)),
          "e5eba02a       strb r10, [fp, #+42]!");
  COMPARE(strb(ip, MemOperand(sp, -42, PreIndex)),
          "e56dc02a       strb ip, [sp, #-42]!");
  COMPARE(strb(r0, MemOperand(r1, r2)),
          "e7c10002       strb r0, [r1, +r2]");
  COMPARE(strb(r0, MemOperand(r1, r2, NegOffset)),
          "e7410002       strb r0, [r1, -r2]");
  COMPARE(strb(r0, MemOperand(r1, r2, PostIndex)),
          "e6c10002       strb r0, [r1], +r2");
  COMPARE(strb(r0, MemOperand(r1, r2, NegPostIndex)),
          "e6410002       strb r0, [r1], -r2");
  COMPARE(strb(r0, MemOperand(r1, r2, PreIndex)),
          "e7e10002       strb r0, [r1, +r2]!");
  COMPARE(strb(r0, MemOperand(r1, r2, NegPreIndex)),
          "e7610002       strb r0, [r1, -r2]!");

  COMPARE(ldrh(r0, MemOperand(r1)),
          "e1d100b0       ldrh r0, [r1, #+0]");
  COMPARE(ldrh(r2, MemOperand(r3, 42)),
          "e1d322ba       ldrh r2, [r3, #+42]");
  COMPARE(ldrh(r4, MemOperand(r5, -42)),
          "e15542ba       ldrh r4, [r5, #-42]");
  COMPARE(ldrh(r6, MemOperand(r7, 42, PostIndex)),
          "e0d762ba       ldrh r6, [r7], #+42");
  COMPARE(ldrh(r8, MemOperand(r9, -42, PostIndex)),
          "e05982ba       ldrh r8, [r9], #-42");
  COMPARE(ldrh(r10, MemOperand(fp, 42, PreIndex)),
          "e1fba2ba       ldrh r10, [fp, #+42]!");
  COMPARE(ldrh(ip, MemOperand(sp, -42, PreIndex)),
          "e17dc2ba       ldrh ip, [sp, #-42]!");
  COMPARE(ldrh(r0, MemOperand(r1, r2)),
          "e19100b2       ldrh r0, [r1, +r2]");
  COMPARE(ldrh(r0, MemOperand(r1, r2, NegOffset)),
          "e11100b2       ldrh r0, [r1, -r2]");
  COMPARE(ldrh(r0, MemOperand(r1, r2, PostIndex)),
          "e09100b2       ldrh r0, [r1], +r2");
  COMPARE(ldrh(r0, MemOperand(r1, r2, NegPostIndex)),
          "e01100b2       ldrh r0, [r1], -r2");
  COMPARE(ldrh(r0, MemOperand(r1, r2, PreIndex)),
          "e1b100b2       ldrh r0, [r1, +r2]!");
  COMPARE(ldrh(r0, MemOperand(r1, r2, NegPreIndex)),
          "e13100b2       ldrh r0, [r1, -r2]!");

  COMPARE(strh(r0, MemOperand(r1)),
          "e1c100b0       strh r0, [r1, #+0]");
  COMPARE(strh(r2, MemOperand(r3, 42)),
          "e1c322ba       strh r2, [r3, #+42]");
  COMPARE(strh(r4, MemOperand(r5, -42)),
          "e14542ba       strh r4, [r5, #-42]");
  COMPARE(strh(r6, MemOperand(r7, 42, PostIndex)),
          "e0c762ba       strh r6, [r7], #+42");
  COMPARE(strh(r8, MemOperand(r9, -42, PostIndex)),
          "e04982ba       strh r8, [r9], #-42");
  COMPARE(strh(r10, MemOperand(fp, 42, PreIndex)),
          "e1eba2ba       strh r10, [fp, #+42]!");
  COMPARE(strh(ip, MemOperand(sp, -42, PreIndex)),
          "e16dc2ba       strh ip, [sp, #-42]!");
  COMPARE(strh(r0, MemOperand(r1, r2)),
          "e18100b2       strh r0, [r1, +r2]");
  COMPARE(strh(r0, MemOperand(r1, r2, NegOffset)),
          "e10100b2       strh r0, [r1, -r2]");
  COMPARE(strh(r0, MemOperand(r1, r2, PostIndex)),
          "e08100b2       strh r0, [r1], +r2");
  COMPARE(strh(r0, MemOperand(r1, r2, NegPostIndex)),
          "e00100b2       strh r0, [r1], -r2");
  COMPARE(strh(r0, MemOperand(r1, r2, PreIndex)),
          "e1a100b2       strh r0, [r1, +r2]!");
  COMPARE(strh(r0, MemOperand(r1, r2, NegPreIndex)),
          "e12100b2       strh r0, [r1, -r2]!");

  COMPARE(ldr(r0, MemOperand(r1)),
          "e5910000       ldr r0, [r1, #+0]");
  COMPARE(ldr(r2, MemOperand(r3, 42)),
          "e593202a       ldr r2, [r3, #+42]");
  COMPARE(ldr(r4, MemOperand(r5, -42)),
          "e515402a       ldr r4, [r5, #-42]");
  COMPARE(ldr(r6, MemOperand(r7, 42, PostIndex)),
          "e497602a       ldr r6, [r7], #+42");
  COMPARE(ldr(r8, MemOperand(r9, -42, PostIndex)),
          "e419802a       ldr r8, [r9], #-42");
  COMPARE(ldr(r10, MemOperand(fp, 42, PreIndex)),
          "e5bba02a       ldr r10, [fp, #+42]!");
  COMPARE(ldr(ip, MemOperand(sp, -42, PreIndex)),
          "e53dc02a       ldr ip, [sp, #-42]!");
  COMPARE(ldr(r0, MemOperand(r1, r2)),
          "e7910002       ldr r0, [r1, +r2]");
  COMPARE(ldr(r0, MemOperand(r1, r2, NegOffset)),
          "e7110002       ldr r0, [r1, -r2]");
  COMPARE(ldr(r0, MemOperand(r1, r2, PostIndex)),
          "e6910002       ldr r0, [r1], +r2");
  COMPARE(ldr(r0, MemOperand(r1, r2, NegPostIndex)),
          "e6110002       ldr r0, [r1], -r2");
  COMPARE(ldr(r0, MemOperand(r1, r2, PreIndex)),
          "e7b10002       ldr r0, [r1, +r2]!");
  COMPARE(ldr(r0, MemOperand(r1, r2, NegPreIndex)),
          "e7310002       ldr r0, [r1, -r2]!");

  COMPARE(str(r0, MemOperand(r1)),
          "e5810000       str r0, [r1, #+0]");
  COMPARE(str(r2, MemOperand(r3, 42)),
          "e583202a       str r2, [r3, #+42]");
  COMPARE(str(r4, MemOperand(r5, -42)),
          "e505402a       str r4, [r5, #-42]");
  COMPARE(str(r6, MemOperand(r7, 42, PostIndex)),
          "e487602a       str r6, [r7], #+42");
  COMPARE(str(r8, MemOperand(r9, -42, PostIndex)),
          "e409802a       str r8, [r9], #-42");
  COMPARE(str(r10, MemOperand(fp, 42, PreIndex)),
          "e5aba02a       str r10, [fp, #+42]!");
  COMPARE(str(ip, MemOperand(sp, -42, PreIndex)),
          "e52dc02a       str ip, [sp, #-42]!");
  COMPARE(str(r0, MemOperand(r1, r2)),
          "e7810002       str r0, [r1, +r2]");
  COMPARE(str(r0, MemOperand(r1, r2, NegOffset)),
          "e7010002       str r0, [r1, -r2]");
  COMPARE(str(r0, MemOperand(r1, r2, PostIndex)),
          "e6810002       str r0, [r1], +r2");
  COMPARE(str(r0, MemOperand(r1, r2, NegPostIndex)),
          "e6010002       str r0, [r1], -r2");
  COMPARE(str(r0, MemOperand(r1, r2, PreIndex)),
          "e7a10002       str r0, [r1, +r2]!");
  COMPARE(str(r0, MemOperand(r1, r2, NegPreIndex)),
          "e7210002       str r0, [r1, -r2]!");

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatureScope scope(&assm, ARMv7);
    COMPARE(ldrd(r0, r1, MemOperand(r1)),
            "e1c100d0       ldrd r0, [r1, #+0]");
    COMPARE(ldrd(r2, r3, MemOperand(r3, 127)),
            "e1c327df       ldrd r2, [r3, #+127]");
    COMPARE(ldrd(r4, r5, MemOperand(r5, -127)),
            "e14547df       ldrd r4, [r5, #-127]");
    COMPARE(ldrd(r6, r7, MemOperand(r7, 127, PostIndex)),
            "e0c767df       ldrd r6, [r7], #+127");
    COMPARE(ldrd(r8, r9, MemOperand(r9, -127, PostIndex)),
            "e04987df       ldrd r8, [r9], #-127");
    COMPARE(ldrd(r10, fp, MemOperand(fp, 127, PreIndex)),
            "e1eba7df       ldrd r10, [fp, #+127]!");
    COMPARE(ldrd(ip, sp, MemOperand(sp, -127, PreIndex)),
            "e16dc7df       ldrd ip, [sp, #-127]!");

    COMPARE(strd(r0, r1, MemOperand(r1)),
            "e1c100f0       strd r0, [r1, #+0]");
    COMPARE(strd(r2, r3, MemOperand(r3, 127)),
            "e1c327ff       strd r2, [r3, #+127]");
    COMPARE(strd(r4, r5, MemOperand(r5, -127)),
            "e14547ff       strd r4, [r5, #-127]");
    COMPARE(strd(r6, r7, MemOperand(r7, 127, PostIndex)),
            "e0c767ff       strd r6, [r7], #+127");
    COMPARE(strd(r8, r9, MemOperand(r9, -127, PostIndex)),
            "e04987ff       strd r8, [r9], #-127");
    COMPARE(strd(r10, fp, MemOperand(fp, 127, PreIndex)),
            "e1eba7ff       strd r10, [fp, #+127]!");
    COMPARE(strd(ip, sp, MemOperand(sp, -127, PreIndex)),
            "e16dc7ff       strd ip, [sp, #-127]!");

    COMPARE(pld(MemOperand(r1, 0)),
            "f5d1f000       pld [r1]");
    COMPARE(pld(MemOperand(ip, 64)),
            "f5dcf040       pld [ip, #+64]");
    COMPARE(pld(MemOperand(r2, 128)),
            "f5d2f080       pld [r2, #+128]");
  }

  // Test out-of-bound immediates.
  COMPARE(ldrb(r6, MemOperand(r7, 42 << 12)),
          "e3a06a2a       mov r6, #172032",
          "e7d76006       ldrb r6, [r7, +r6]");
  COMPARE(ldrh(r6, MemOperand(r7, 42 << 8, PostIndex)),
          "e3a06c2a       mov r6, #10752",
          "e09760b6       ldrh r6, [r7], +r6");
  // Make sure ip is used if the destination is the same as the base.
  COMPARE(ldr(r8, MemOperand(r8, 42 << 12, PreIndex)),
          "e3a0ca2a       mov ip, #172032",
          "e7b8800c       ldr r8, [r8, +ip]!");
  COMPARE(strb(r6, MemOperand(r7, 42 << 12)),
          "e3a0ca2a       mov ip, #172032",
          "e7c7600c       strb r6, [r7, +ip]");
  COMPARE(strh(r6, MemOperand(r7, 42 << 8, PostIndex)),
          "e3a0cc2a       mov ip, #10752",
          "e08760bc       strh r6, [r7], +ip");
  COMPARE(str(r6, MemOperand(r7, 42 << 12, PreIndex)),
          "e3a0ca2a       mov ip, #172032",
          "e7a7600c       str r6, [r7, +ip]!");

  // Test scaled operands for instructions that do not support it natively.
  COMPARE(ldrh(r0, MemOperand(r1, r2, LSL, 2)),
          "e1a00102       mov r0, r2, lsl #2",
          "e19100b0       ldrh r0, [r1, +r0]");
  COMPARE(strh(r3, MemOperand(r4, r5, LSR, 3)),
          "e1a0c1a5       mov ip, r5, lsr #3",
          "e18430bc       strh r3, [r4, +ip]");
  // Make sure ip is used if the destination is the same as the base.
  COMPARE(ldrsb(r6, MemOperand(r6, r8, ASR, 4)),
          "e1a0c248       mov ip, r8, asr #4",
          "e19660dc       ldrsb r6, [r6, +ip]");
  COMPARE(ldrsh(r9, MemOperand(sp, r10, ROR, 5)),
          "e1a092ea       mov r9, r10, ror #5",
          "e19d90f9       ldrsh r9, [sp, +r9]");

  VERIFY_RUN();
}


static void TestLoadLiteral(uint8_t* buffer, Assembler* assm, bool* failure,
                            int offset) {
  int pc_offset = assm->pc_offset();
  uint8_t *progcounter = &buffer[pc_offset];
  assm->ldr_pcrel(r0, offset);

  const char *expected_string_template =
    (offset >= 0) ?
    "e59f0%03x       ldr r0, [pc, #+%d] (addr 0x%08" PRIxPTR ")" :
    "e51f0%03x       ldr r0, [pc, #%d] (addr 0x%08" PRIxPTR ")";
  char expected_string[80];
  snprintf(expected_string, sizeof(expected_string), expected_string_template,
    abs(offset), offset,
    reinterpret_cast<uintptr_t>(
      progcounter + Instruction::kPcLoadDelta + offset));
  if (!DisassembleAndCompare(progcounter, kRawString, expected_string)) {
    *failure = true;
  }
}


TEST_F(DisasmArmTest, LoadLiteral) {
  SET_UP();

  TestLoadLiteral(buffer, &assm, &failure, 0);
  TestLoadLiteral(buffer, &assm, &failure, 1);
  TestLoadLiteral(buffer, &assm, &failure, 4);
  TestLoadLiteral(buffer, &assm, &failure, 4095);
  TestLoadLiteral(buffer, &assm, &failure, -1);
  TestLoadLiteral(buffer, &assm, &failure, -4);
  TestLoadLiteral(buffer, &assm, &failure, -4095);

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, Barrier) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatureScope scope(&assm, ARMv7);

    COMPARE(dmb(OSHLD),
            "f57ff051       dmb oshld");
    COMPARE(dmb(OSHST),
            "f57ff052       dmb oshst");
    COMPARE(dmb(OSH),
            "f57ff053       dmb osh");
    COMPARE(dmb(NSHLD),
            "f57ff055       dmb nshld");
    COMPARE(dmb(NSHST),
            "f57ff056       dmb nshst");
    COMPARE(dmb(NSH),
            "f57ff057       dmb nsh");
    COMPARE(dmb(ISHLD),
            "f57ff059       dmb ishld");
    COMPARE(dmb(ISHST),
            "f57ff05a       dmb ishst");
    COMPARE(dmb(ISH),
            "f57ff05b       dmb ish");
    COMPARE(dmb(LD),
            "f57ff05d       dmb ld");
    COMPARE(dmb(ST),
            "f57ff05e       dmb st");
    COMPARE(dmb(SY),
            "f57ff05f       dmb sy");

    COMPARE(dsb(ISH),
            "f57ff04b       dsb ish");

    COMPARE(isb(SY),
            "f57ff06f       isb sy");
  } else {
    // ARMv6 uses CP15 to implement barriers. The BarrierOption argument is
    // ignored.
    COMPARE(dmb(ISH),
            "ee070fba       mcr (CP15DMB)");
    COMPARE(dsb(OSH),
            "ee070f9a       mcr (CP15DSB)");
    COMPARE(isb(SY),
            "ee070f95       mcr (CP15ISB)");
  }

  // ARMv6 barriers.
  // Details available in ARM DDI 0406C.b, B3-1750.
  COMPARE(mcr(p15, 0, r0, cr7, cr10, 5), "ee070fba       mcr (CP15DMB)");
  COMPARE(mcr(p15, 0, r0, cr7, cr10, 4), "ee070f9a       mcr (CP15DSB)");
  COMPARE(mcr(p15, 0, r0, cr7, cr5, 4), "ee070f95       mcr (CP15ISB)");
  // Rt is ignored.
  COMPARE(mcr(p15, 0, lr, cr7, cr10, 5), "ee07efba       mcr (CP15DMB)");
  COMPARE(mcr(p15, 0, lr, cr7, cr10, 4), "ee07ef9a       mcr (CP15DSB)");
  COMPARE(mcr(p15, 0, lr, cr7, cr5, 4), "ee07ef95       mcr (CP15ISB)");
  // The mcr instruction can be conditional.
  COMPARE(mcr(p15, 0, r0, cr7, cr10, 5, eq), "0e070fba       mcreq (CP15DMB)");
  COMPARE(mcr(p15, 0, r0, cr7, cr10, 4, ne), "1e070f9a       mcrne (CP15DSB)");
  COMPARE(mcr(p15, 0, r0, cr7, cr5, 4, mi), "4e070f95       mcrmi (CP15ISB)");

  // Conditional speculation barrier.
  COMPARE(csdb(), "e320f014       csdb");

  VERIFY_RUN();
}


TEST_F(DisasmArmTest, LoadStoreExclusive) {
  SET_UP();

  COMPARE(ldrexb(r0, r1), "e1d10f9f       ldrexb r0, [r1]");
  COMPARE(strexb(r0, r1, r2), "e1c20f91       strexb r0, r1, [r2]");
  COMPARE(ldrexh(r0, r1), "e1f10f9f       ldrexh r0, [r1]");
  COMPARE(strexh(r0, r1, r2), "e1e20f91       strexh r0, r1, [r2]");
  COMPARE(ldrex(r0, r1), "e1910f9f       ldrex r0, [r1]");
  COMPARE(strex(r0, r1, r2), "e1820f91       strex r0, r1, [r2]");
  COMPARE(ldrexd(r0, r1, r2), "e1b20f9f       ldrexd r0, [r2]");
  COMPARE(strexd(r0, r2, r3, r4),
          "e1a40f92       strexd r0, r2, [r4]");

  VERIFY_RUN();
}

TEST_F(DisasmArmTest, SplitAddImmediate) {
  SET_UP();

  if (CpuFeatures::IsSupported(ARMv7)) {
    // Reuse the destination as a scratch.
    COMPARE(add(r0, r1, Operand(0x12345678)),
            "e3050678       movw r0, #22136",
            "e3410234       movt r0, #4660",
            "e0810000       add r0, r1, r0");

    // Use ip as a scratch.
    COMPARE(add(r0, r0, Operand(0x12345678)),
            "e305c678       movw ip, #22136",
            "e341c234       movt ip, #4660",
            "e080000c       add r0, r0, ip");
  } else {
    // Reuse the destination as a scratch.
    COMPARE_REGEX(add(r0, r1, Operand(0x12345678)),
                  "e59f0[0-9a-f]{3}       "
                      "ldr r0, \\[pc, #\\+[0-9]+\\] \\(addr 0x[0-9a-f]{8}\\)",
                  "e0810000       add r0, r1, r0");

    // Use ip as a scratch.
    COMPARE_REGEX(add(r0, r0, Operand(0x12345678)),
                  "e59fc[0-9a-f]{3}       "
                      "ldr ip, \\[pc, #\\+[0-9]+\\] \\(addr 0x[0-9a-f]{8}\\)",
                  "e080000c       add r0, r0, ip");
  }

  // If ip is not available, split the operation into multiple additions.
  {
    UseScratchRegisterScope temps(&assm);
    Register reserved = temps.Acquire();
    USE(reserved);
    COMPARE(add(r2, r2, Operand(0x12345678)),
            "e2822f9e       add r2, r2, #632",
            "e2822b15       add r2, r2, #21504",
            "e282278d       add r2, r2, #36962304",
            "e2822201       add r2, r2, #268435456");
  }

  VERIFY_RUN();
}

}  // namespace internal
}  // namespace v8
