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

#include <iostream>

#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/init/v8.h"
#include "test/cctest/cctest.h"

namespace v8 {
namespace internal {

// Define these function prototypes to match JSEntryFunction in execution.cc.
// TODO(LOONG64): Refine these signatures per test case.
using F1 = void*(int x, int p1, int p2, int p3, int p4);
using F2 = void*(int x, int y, int p2, int p3, int p4);
using F3 = void*(void* p, int p1, int p2, int p3, int p4);
using F4 = void*(int64_t x, int64_t y, int64_t p2, int64_t p3, int64_t p4);
using F5 = void*(void* p0, void* p1, int p2, int p3, int p4);

#define __ assm.
// v0->a2, v1->a3
TEST(LA0) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Addition.
  __ addi_d(a2, a0, 0xC);

  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0xAB0, 0, 0, 0, 0));
  CHECK_EQ(0xABCL, res);
}

TEST(LA1) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label L, C;

  __ ori(a1, a0, 0);
  __ ori(a2, zero_reg, 0);
  __ b(&C);

  __ bind(&L);
  __ add_d(a2, a2, a1);
  __ addi_d(a1, a1, -1);

  __ bind(&C);
  __ ori(a3, a1, 0);

  __ Branch(&L, ne, a3, Operand((int64_t)0));

  __ or_(a0, a2, zero_reg);
  __ or_(a1, a3, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(50, 0, 0, 0, 0));
  CHECK_EQ(1275L, res);
}

TEST(LA2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  __ ori(a4, zero_reg, 0);  // 00000000
  __ lu12i_w(a4, 0x12345);  // 12345000
  __ ori(a4, a4, 0);        // 12345000
  __ ori(a2, a4, 0xF0F);    // 12345F0F
  __ Branch(&error, ne, a2, Operand(0x12345F0F));

  __ ori(a4, zero_reg, 0);
  __ lu32i_d(a4, 0x12345);  // 1 2345 0000 0000
  __ ori(a4, a4, 0xFFF);    // 1 2345 0000 0FFF
  __ addi_d(a2, a4, 1);
  __ Branch(&error, ne, a2, Operand(0x1234500001000));

  __ ori(a4, zero_reg, 0);
  __ lu52i_d(a4, zero_reg, 0x123);  // 1230 0000 0000 0000
  __ ori(a4, a4, 0xFFF);            // 123F 0000 0000 0FFF
  __ addi_d(a2, a4, 1);             // 1230 0000 0000 1000
  __ Branch(&error, ne, a2, Operand(0x1230000000001000));

  __ li(a2, 0x31415926);
  __ b(&exit);

  __ bind(&error);
  __ li(a2, 0x666);

  __ bind(&exit);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

TEST(LA3) {
  // Test 32bit calculate instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  __ li(a4, 0x00000004);
  __ li(a5, 0x00001234);
  __ li(a6, 0x12345678);
  __ li(a7, 0x7FFFFFFF);
  __ li(t0, static_cast<int32_t>(0xFFFFFFFC));
  __ li(t1, static_cast<int32_t>(0xFFFFEDCC));
  __ li(t2, static_cast<int32_t>(0xEDCBA988));
  __ li(t3, static_cast<int32_t>(0x80000000));

  __ ori(a2, zero_reg, 0);  // 0x00000000
  __ add_w(a2, a4, a5);     // 0x00001238
  __ sub_w(a2, a2, a4);     // 0x00001234
  __ Branch(&error, ne, a2, Operand(0x00001234));
  __ ori(a3, zero_reg, 0);  // 0x00000000
  __ add_w(a3, a7, a4);  // 32bit addu result is sign-extended into 64bit reg.
  __ Branch(&error, ne, a3, Operand(0xFFFFFFFF80000003));

  __ sub_w(a3, t3, a4);  // 0x7FFFFFFC
  __ Branch(&error, ne, a3, Operand(0x7FFFFFFC));

  __ ori(a2, zero_reg, 0);         // 0x00000000
  __ ori(a3, zero_reg, 0);         // 0x00000000
  __ addi_w(a2, zero_reg, 0x421);  // 0x00007421
  __ addi_w(a2, a2, -0x1);         // 0x00007420
  __ addi_w(a2, a2, -0x20);        // 0x00007400
  __ Branch(&error, ne, a2, Operand(0x0000400));
  __ addi_w(a3, a7, 0x1);  // 0x80000000 - result is sign-extended.
  __ Branch(&error, ne, a3, Operand(0xFFFFFFFF80000000));

  __ ori(a2, zero_reg, 0);   // 0x00000000
  __ ori(a3, zero_reg, 0);   // 0x00000000
  __ alsl_w(a2, a6, a4, 3);  // 0xFFFFFFFF91A2B3C4
  __ alsl_w(a2, a2, a4, 2);  // 0x468ACF14
  __ Branch(&error, ne, a2, Operand(0x468acf14));
  __ ori(a0, zero_reg, 31);
  __ alsl_wu(a3, a6, a4, 3);  // 0x91A2B3C4
  __ alsl_wu(a3, a3, a7, 1);  // 0xFFFFFFFFA3456787
  __ Branch(&error, ne, a3, Operand(0xA3456787));

  __ ori(a2, zero_reg, 0);
  __ ori(a3, zero_reg, 0);
  __ mul_w(a2, a5, a7);
  __ div_w(a2, a2, a4);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFFFFFFB73));
  __ mul_w(a3, a4, t1);
  __ Branch(&error, ne, a3, Operand(0xFFFFFFFFFFFFB730));
  __ div_w(a3, t3, a4);
  __ Branch(&error, ne, a3, Operand(0xFFFFFFFFE0000000));

  __ ori(a2, zero_reg, 0);
  __ mulh_w(a2, a4, t1);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFFFFFFFFF));
  __ mulh_w(a2, a4, a6);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ mulh_wu(a2, a4, t1);
  __ Branch(&error, ne, a2, Operand(0x3));
  __ mulh_wu(a2, a4, a6);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ mulw_d_w(a2, a4, t1);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFFFFFB730));
  __ mulw_d_w(a2, a4, a6);
  __ Branch(&error, ne, a2, Operand(0x48D159E0));

  __ ori(a2, zero_reg, 0);
  __ mulw_d_wu(a2, a4, t1);
  __ Branch(&error, ne, a2, Operand(0x3FFFFB730));  //========0xFFFFB730
  __ ori(a2, zero_reg, 81);
  __ mulw_d_wu(a2, a4, a6);
  __ Branch(&error, ne, a2, Operand(0x48D159E0));

  __ ori(a2, zero_reg, 0);
  __ div_wu(a2, a7, a5);
  __ Branch(&error, ne, a2, Operand(0x70821));
  __ div_wu(a2, t0, a5);
  __ Branch(&error, ne, a2, Operand(0xE1042));
  __ div_wu(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0x1));

  __ ori(a2, zero_reg, 0);
  __ mod_w(a2, a6, a5);
  __ Branch(&error, ne, a2, Operand(0xDA8));
  __ ori(a2, zero_reg, 0);
  __ mod_w(a2, t2, a5);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFFFFFF258));
  __ ori(a2, zero_reg, 0);
  __ mod_w(a2, t2, t1);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFFFFFF258));

  __ ori(a2, zero_reg, 0);
  __ mod_wu(a2, a6, a5);
  __ Branch(&error, ne, a2, Operand(0xDA8));
  __ mod_wu(a2, t2, a5);
  __ Branch(&error, ne, a2, Operand(0xF0));
  __ mod_wu(a2, t2, t1);
  __ Branch(&error, ne, a2, Operand(0xFFFFFFFFEDCBA988));

  __ li(a2, 0x31415926);
  __ b(&exit);

  __ bind(&error);
  __ li(a2, 0x666);

  __ bind(&exit);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

TEST(LA4) {
  // Test 64bit calculate instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  __ li(a4, 0x17312);
  __ li(a5, 0x1012131415161718);
  __ li(a6, 0x51F4B764A26E7412);
  __ li(a7, 0x7FFFFFFFFFFFFFFF);
  __ li(t0, static_cast<int64_t>(0xFFFFFFFFFFFFF547));
  __ li(t1, static_cast<int64_t>(0xDF6B8F35A10E205C));
  __ li(t2, static_cast<int64_t>(0x81F25A87C4236841));
  __ li(t3, static_cast<int64_t>(0x8000000000000000));

  __ ori(a2, zero_reg, 0);
  __ add_d(a2, a4, a5);
  __ sub_d(a2, a2, a4);
  __ Branch(&error, ne, a2, Operand(0x1012131415161718));
  __ ori(a3, zero_reg, 0);
  __ add_d(a3, a6, a7);  //溢出
  __ Branch(&error, ne, a3, Operand(0xd1f4b764a26e7411));
  __ sub_d(a3, t3, a4);  //溢出
  __ Branch(&error, ne, a3, Operand(0x7ffffffffffe8cee));

  __ ori(a2, zero_reg, 0);
  __ addi_d(a2, a5, 0x412);  //正值
  __ Branch(&error, ne, a2, Operand(0x1012131415161b2a));
  __ addi_d(a2, a7, 0x547);  //负值
  __ Branch(&error, ne, a2, Operand(0x8000000000000546));

  __ ori(t4, zero_reg, 0);
  __ addu16i_d(a2, t4, 0x1234);
  __ Branch(&error, ne, a2, Operand(0x12340000));
  __ addu16i_d(a2, a2, 0x9876);
  __ Branch(&error, ne, a2, Operand(0xffffffffaaaa0000));

  __ ori(a2, zero_reg, 0);
  __ alsl_d(a2, t2, t0, 3);
  __ Branch(&error, ne, a2, Operand(0xf92d43e211b374f));

  __ ori(a2, zero_reg, 0);
  __ mul_d(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(0xdbe6a8729a547fb0));
  __ mul_d(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0x57ad69f40f870584));
  __ mul_d(a2, a4, t0);
  __ Branch(&error, ne, a2, Operand(0xfffffffff07523fe));

  __ ori(a2, zero_reg, 0);
  __ mulh_d(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(0x52514c6c6b54467));
  __ mulh_d(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0x15d));

  __ ori(a2, zero_reg, 0);
  __ mulh_du(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(0x52514c6c6b54467));
  __ mulh_du(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0xdf6b8f35a10e1700));
  __ mulh_du(a2, a4, t0);
  __ Branch(&error, ne, a2, Operand(0x17311));

  __ ori(a2, zero_reg, 0);
  __ div_d(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ div_d(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ div_d(a2, t1, a4);
  __ Branch(&error, ne, a2, Operand(0xffffe985f631e6d9));

  __ ori(a2, zero_reg, 0);
  __ div_du(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ div_du(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0x1));
  __ div_du(a2, t1, a4);
  __ Branch(&error, ne, a2, Operand(0x9a22ffd3973d));

  __ ori(a2, zero_reg, 0);
  __ mod_d(a2, a6, a4);
  __ Branch(&error, ne, a2, Operand(0x13558));
  __ mod_d(a2, t2, t0);
  __ Branch(&error, ne, a2, Operand(0xfffffffffffffb0a));
  __ mod_d(a2, t1, a4);
  __ Branch(&error, ne, a2, Operand(0xffffffffffff6a1a));

  __ ori(a2, zero_reg, 0);
  __ mod_du(a2, a6, a4);
  __ Branch(&error, ne, a2, Operand(0x13558));
  __ mod_du(a2, t2, t0);
  __ Branch(&error, ne, a2, Operand(0x81f25a87c4236841));
  __ mod_du(a2, t1, a4);
  __ Branch(&error, ne, a2, Operand(0x1712));

  // Everything was correctly executed. Load the expected result.
  __ li(a2, 0x31415926);
  __ b(&exit);

  __ bind(&error);
  __ li(a2, 0x666);
  // Got an error. Return a wrong result.

  __ bind(&exit);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

TEST(LA5) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  __ li(a4, 0x17312);
  __ li(a5, 0x1012131415161718);
  __ li(a6, 0x51F4B764A26E7412);
  __ li(a7, 0x7FFFFFFFFFFFFFFF);
  __ li(t0, static_cast<int64_t>(0xFFFFFFFFFFFFF547));
  __ li(t1, static_cast<int64_t>(0xDF6B8F35A10E205C));
  __ li(t2, static_cast<int64_t>(0x81F25A87C4236841));
  __ li(t3, static_cast<int64_t>(0x8000000000000000));

  __ ori(a2, zero_reg, 0);
  __ slt(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(0x1));
  __ slt(a2, a7, t0);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ slt(a2, t1, t1);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ sltu(a2, a5, a6);
  __ Branch(&error, ne, a2, Operand(0x1));
  __ sltu(a2, a7, t0);
  __ Branch(&error, ne, a2, Operand(0x1));
  __ sltu(a2, t1, t1);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ slti(a2, a5, 0x123);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ slti(a2, t0, 0x123);
  __ Branch(&error, ne, a2, Operand(0x1));

  __ ori(a2, zero_reg, 0);
  __ sltui(a2, a5, 0x123);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));
  __ sltui(a2, t0, 0x123);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ and_(a2, a4, a5);
  __ Branch(&error, ne, a2, Operand(0x1310));
  __ and_(a2, a6, a7);
  __ Branch(&error, ne, a2, Operand(0x51F4B764A26E7412));

  __ ori(a2, zero_reg, 0);
  __ or_(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0xfffffffffffff55f));
  __ or_(a2, t2, t3);
  __ Branch(&error, ne, a2, Operand(0x81f25a87c4236841));

  __ ori(a2, zero_reg, 0);
  __ nor(a2, a4, a5);
  __ Branch(&error, ne, a2, Operand(0xefedecebeae888e5));
  __ nor(a2, a6, a7);
  __ Branch(&error, ne, a2, Operand(0x8000000000000000));

  __ ori(a2, zero_reg, 0);
  __ xor_(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0x209470ca5ef1d51b));
  __ xor_(a2, t2, t3);
  __ Branch(&error, ne, a2, Operand(0x1f25a87c4236841));

  __ ori(a2, zero_reg, 0);
  __ andn(a2, a4, a5);
  __ Branch(&error, ne, a2, Operand(0x16002));
  __ andn(a2, a6, a7);
  __ Branch(&error, ne, a2, Operand(static_cast<int64_t>(0)));

  __ ori(a2, zero_reg, 0);
  __ orn(a2, t0, t1);
  __ Branch(&error, ne, a2, Operand(0xffffffffffffffe7));
  __ orn(a2, t2, t3);
  __ Branch(&error, ne, a2, Operand(0xffffffffffffffff));

  __ ori(a2, zero_reg, 0);
  __ andi(a2, a4, 0x123);
  __ Branch(&error, ne, a2, Operand(0x102));
  __ andi(a2, a6, 0xDCB);
  __ Branch(&error, ne, a2, Operand(0x402));

  __ ori(a2, zero_reg, 0);
  __ xori(a2, t0, 0x123);
  __ Branch(&error, ne, a2, Operand(0xfffffffffffff464));
  __ xori(a2, t2, 0xDCB);
  __ Branch(&error, ne, a2, Operand(0x81f25a87c423658a));

  // Everything was correctly executed. Load the expected result.
  __ li(a2, 0x31415926);
  __ b(&exit);

  __ bind(&error);
  // Got an error. Return a wrong result.
  __ li(a2, 0x666);

  __ bind(&exit);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

TEST(LA6) {
  // Test loads and stores instruction.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct T {
    int64_t si1;
    int64_t si2;
    int64_t si3;
    int64_t result_ld_b_si1;
    int64_t result_ld_b_si2;
    int64_t result_ld_h_si1;
    int64_t result_ld_h_si2;
    int64_t result_ld_w_si1;
    int64_t result_ld_w_si2;
    int64_t result_ld_d_si1;
    int64_t result_ld_d_si3;
    int64_t result_ld_bu_si2;
    int64_t result_ld_hu_si2;
    int64_t result_ld_wu_si2;
    int64_t result_st_b;
    int64_t result_st_h;
    int64_t result_st_w;
  };
  T t;

  // Ld_b
  __ Ld_b(a4, MemOperand(a0, offsetof(T, si1)));
  __ St_d(a4, MemOperand(a0, offsetof(T, result_ld_b_si1)));

  __ Ld_b(a4, MemOperand(a0, offsetof(T, si2)));
  __ St_d(a4, MemOperand(a0, offsetof(T, result_ld_b_si2)));

  // Ld_h
  __ Ld_h(a5, MemOperand(a0, offsetof(T, si1)));
  __ St_d(a5, MemOperand(a0, offsetof(T, result_ld_h_si1)));

  __ Ld_h(a5, MemOperand(a0, offsetof(T, si2)));
  __ St_d(a5, MemOperand(a0, offsetof(T, result_ld_h_si2)));

  // Ld_w
  __ Ld_w(a6, MemOperand(a0, offsetof(T, si1)));
  __ St_d(a6, MemOperand(a0, offsetof(T, result_ld_w_si1)));

  __ Ld_w(a6, MemOperand(a0, offsetof(T, si2)));
  __ St_d(a6, MemOperand(a0, offsetof(T, result_ld_w_si2)));

  // Ld_d
  __ Ld_d(a7, MemOperand(a0, offsetof(T, si1)));
  __ St_d(a7, MemOperand(a0, offsetof(T, result_ld_d_si1)));

  __ Ld_d(a7, MemOperand(a0, offsetof(T, si3)));
  __ St_d(a7, MemOperand(a0, offsetof(T, result_ld_d_si3)));

  // Ld_bu
  __ Ld_bu(t0, MemOperand(a0, offsetof(T, si2)));
  __ St_d(t0, MemOperand(a0, offsetof(T, result_ld_bu_si2)));

  // Ld_hu
  __ Ld_hu(t1, MemOperand(a0, offsetof(T, si2)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_ld_hu_si2)));

  // Ld_wu
  __ Ld_wu(t2, MemOperand(a0, offsetof(T, si2)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_ld_wu_si2)));

  // St
  __ li(t4, 0x11111111);

  // St_b
  __ Ld_d(t5, MemOperand(a0, offsetof(T, si3)));
  __ St_d(t5, MemOperand(a0, offsetof(T, result_st_b)));
  __ St_b(t4, MemOperand(a0, offsetof(T, result_st_b)));

  // St_h
  __ Ld_d(t6, MemOperand(a0, offsetof(T, si3)));
  __ St_d(t6, MemOperand(a0, offsetof(T, result_st_h)));
  __ St_h(t4, MemOperand(a0, offsetof(T, result_st_h)));

  // St_w
  __ Ld_d(t7, MemOperand(a0, offsetof(T, si3)));
  __ St_d(t7, MemOperand(a0, offsetof(T, result_st_w)));
  __ St_w(t4, MemOperand(a0, offsetof(T, result_st_w)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.si1 = 0x11223344;
  t.si2 = 0x99AABBCC;
  t.si3 = 0x1122334455667788;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int64_t>(0x44), t.result_ld_b_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFFFFFFFCC), t.result_ld_b_si2);

  CHECK_EQ(static_cast<int64_t>(0x3344), t.result_ld_h_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFFFFFBBCC), t.result_ld_h_si2);

  CHECK_EQ(static_cast<int64_t>(0x11223344), t.result_ld_w_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFF99AABBCC), t.result_ld_w_si2);

  CHECK_EQ(static_cast<int64_t>(0x11223344), t.result_ld_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x1122334455667788), t.result_ld_d_si3);

  CHECK_EQ(static_cast<int64_t>(0xCC), t.result_ld_bu_si2);
  CHECK_EQ(static_cast<int64_t>(0xBBCC), t.result_ld_hu_si2);
  CHECK_EQ(static_cast<int64_t>(0x99AABBCC), t.result_ld_wu_si2);

  CHECK_EQ(static_cast<int64_t>(0x1122334455667711), t.result_st_b);
  CHECK_EQ(static_cast<int64_t>(0x1122334455661111), t.result_st_h);
  CHECK_EQ(static_cast<int64_t>(0x1122334411111111), t.result_st_w);
}

TEST(LA7) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct T {
    int64_t si1;
    int64_t si2;
    int64_t si3;
    int64_t result_ldx_b_si1;
    int64_t result_ldx_b_si2;
    int64_t result_ldx_h_si1;
    int64_t result_ldx_h_si2;
    int64_t result_ldx_w_si1;
    int64_t result_ldx_w_si2;
    int64_t result_ldx_d_si1;
    int64_t result_ldx_d_si3;
    int64_t result_ldx_bu_si2;
    int64_t result_ldx_hu_si2;
    int64_t result_ldx_wu_si2;
    int64_t result_stx_b;
    int64_t result_stx_h;
    int64_t result_stx_w;
  };
  T t;

  // ldx_b
  __ li(a2, static_cast<int64_t>(offsetof(T, si1)));
  __ Ld_b(a4, MemOperand(a0, a2));
  __ St_d(a4, MemOperand(a0, offsetof(T, result_ldx_b_si1)));

  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_b(a4, MemOperand(a0, a2));
  __ St_d(a4, MemOperand(a0, offsetof(T, result_ldx_b_si2)));

  // ldx_h
  __ li(a2, static_cast<int64_t>(offsetof(T, si1)));
  __ Ld_h(a5, MemOperand(a0, a2));
  __ St_d(a5, MemOperand(a0, offsetof(T, result_ldx_h_si1)));

  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_h(a5, MemOperand(a0, a2));
  __ St_d(a5, MemOperand(a0, offsetof(T, result_ldx_h_si2)));

  // ldx_w
  __ li(a2, static_cast<int64_t>(offsetof(T, si1)));
  __ Ld_w(a6, MemOperand(a0, a2));
  __ St_d(a6, MemOperand(a0, offsetof(T, result_ldx_w_si1)));

  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_w(a6, MemOperand(a0, a2));
  __ St_d(a6, MemOperand(a0, offsetof(T, result_ldx_w_si2)));

  // Ld_d
  __ li(a2, static_cast<int64_t>(offsetof(T, si1)));
  __ Ld_d(a7, MemOperand(a0, a2));
  __ St_d(a7, MemOperand(a0, offsetof(T, result_ldx_d_si1)));

  __ li(a2, static_cast<int64_t>(offsetof(T, si3)));
  __ Ld_d(a7, MemOperand(a0, a2));
  __ St_d(a7, MemOperand(a0, offsetof(T, result_ldx_d_si3)));

  // Ld_bu
  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_bu(t0, MemOperand(a0, a2));
  __ St_d(t0, MemOperand(a0, offsetof(T, result_ldx_bu_si2)));

  // Ld_hu
  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_hu(t1, MemOperand(a0, a2));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_ldx_hu_si2)));

  // Ld_wu
  __ li(a2, static_cast<int64_t>(offsetof(T, si2)));
  __ Ld_wu(t2, MemOperand(a0, a2));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_ldx_wu_si2)));

  // St
  __ li(t4, 0x11111111);

  // St_b
  __ Ld_d(t5, MemOperand(a0, offsetof(T, si3)));
  __ St_d(t5, MemOperand(a0, offsetof(T, result_stx_b)));
  __ li(a2, static_cast<int64_t>(offsetof(T, result_stx_b)));
  __ St_b(t4, MemOperand(a0, a2));

  // St_h
  __ Ld_d(t6, MemOperand(a0, offsetof(T, si3)));
  __ St_d(t6, MemOperand(a0, offsetof(T, result_stx_h)));
  __ li(a2, static_cast<int64_t>(offsetof(T, result_stx_h)));
  __ St_h(t4, MemOperand(a0, a2));

  // St_w
  __ Ld_d(t7, MemOperand(a0, offsetof(T, si3)));
  __ li(a2, static_cast<int64_t>(offsetof(T, result_stx_w)));
  __ St_d(t7, MemOperand(a0, a2));
  __ li(a3, static_cast<int64_t>(offsetof(T, result_stx_w)));
  __ St_w(t4, MemOperand(a0, a3));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.si1 = 0x11223344;
  t.si2 = 0x99AABBCC;
  t.si3 = 0x1122334455667788;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int64_t>(0x44), t.result_ldx_b_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFFFFFFFCC), t.result_ldx_b_si2);

  CHECK_EQ(static_cast<int64_t>(0x3344), t.result_ldx_h_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFFFFFBBCC), t.result_ldx_h_si2);

  CHECK_EQ(static_cast<int64_t>(0x11223344), t.result_ldx_w_si1);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFF99AABBCC), t.result_ldx_w_si2);

  CHECK_EQ(static_cast<int64_t>(0x11223344), t.result_ldx_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x1122334455667788), t.result_ldx_d_si3);

  CHECK_EQ(static_cast<int64_t>(0xCC), t.result_ldx_bu_si2);
  CHECK_EQ(static_cast<int64_t>(0xBBCC), t.result_ldx_hu_si2);
  CHECK_EQ(static_cast<int64_t>(0x99AABBCC), t.result_ldx_wu_si2);

  CHECK_EQ(static_cast<int64_t>(0x1122334455667711), t.result_stx_b);
  CHECK_EQ(static_cast<int64_t>(0x1122334455661111), t.result_stx_h);
  CHECK_EQ(static_cast<int64_t>(0x1122334411111111), t.result_stx_w);
}

TEST(LDPTR_STPTR) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  int64_t test[10];

  __ ldptr_w(a4, a0, 0);
  __ stptr_d(a4, a0, 24);  // test[3]

  __ ldptr_w(a5, a0, 8);   // test[1]
  __ stptr_d(a5, a0, 32);  // test[4]

  __ ldptr_d(a6, a0, 16);  // test[2]
  __ stptr_d(a6, a0, 40);  // test[5]

  __ li(t0, 0x11111111);

  __ stptr_d(a6, a0, 48);  // test[6]
  __ stptr_w(t0, a0, 48);  // test[6]

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test[0] = 0x11223344;
  test[1] = 0x99AABBCC;
  test[2] = 0x1122334455667788;
  f.Call(&test, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int64_t>(0x11223344), test[3]);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFF99AABBCC), test[4]);
  CHECK_EQ(static_cast<int64_t>(0x1122334455667788), test[5]);
  CHECK_EQ(static_cast<int64_t>(0x1122334411111111), test[6]);
}

TEST(LA8) {
  // Test 32bit shift instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    int32_t input;
    int32_t result_sll_w_0;
    int32_t result_sll_w_8;
    int32_t result_sll_w_10;
    int32_t result_sll_w_31;
    int32_t result_srl_w_0;
    int32_t result_srl_w_8;
    int32_t result_srl_w_10;
    int32_t result_srl_w_31;
    int32_t result_sra_w_0;
    int32_t result_sra_w_8;
    int32_t result_sra_w_10;
    int32_t result_sra_w_31;
    int32_t result_rotr_w_0;
    int32_t result_rotr_w_8;
    int32_t result_slli_w_0;
    int32_t result_slli_w_8;
    int32_t result_slli_w_10;
    int32_t result_slli_w_31;
    int32_t result_srli_w_0;
    int32_t result_srli_w_8;
    int32_t result_srli_w_10;
    int32_t result_srli_w_31;
    int32_t result_srai_w_0;
    int32_t result_srai_w_8;
    int32_t result_srai_w_10;
    int32_t result_srai_w_31;
    int32_t result_rotri_w_0;
    int32_t result_rotri_w_8;
    int32_t result_rotri_w_10;
    int32_t result_rotri_w_31;
  };
  T t;
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ Ld_w(a4, MemOperand(a0, offsetof(T, input)));

  // sll_w
  __ li(a5, 0);
  __ sll_w(t0, a4, a5);
  __ li(a5, 0x8);
  __ sll_w(t1, a4, a5);
  __ li(a5, 0xA);
  __ sll_w(t2, a4, a5);
  __ li(a5, 0x1F);
  __ sll_w(t3, a4, a5);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_sll_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_sll_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_sll_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_sll_w_31)));

  // srl_w
  __ li(a5, 0x0);
  __ srl_w(t0, a4, a5);
  __ li(a5, 0x8);
  __ srl_w(t1, a4, a5);
  __ li(a5, 0xA);
  __ srl_w(t2, a4, a5);
  __ li(a5, 0x1F);
  __ srl_w(t3, a4, a5);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_srl_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_srl_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_srl_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_srl_w_31)));

  // sra_w
  __ li(a5, 0x0);
  __ sra_w(t0, a4, a5);
  __ li(a5, 0x8);
  __ sra_w(t1, a4, a5);

  __ li(a6, static_cast<int32_t>(0x80000000));
  __ add_w(a6, a6, a4);
  __ li(a5, 0xA);
  __ sra_w(t2, a6, a5);
  __ li(a5, 0x1F);
  __ sra_w(t3, a6, a5);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_sra_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_sra_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_sra_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_sra_w_31)));

  // rotr
  __ li(a5, 0x0);
  __ rotr_w(t0, a4, a5);
  __ li(a6, 0x8);
  __ rotr_w(t1, a4, a6);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_rotr_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_rotr_w_8)));

  // slli_w
  __ slli_w(t0, a4, 0);
  __ slli_w(t1, a4, 0x8);
  __ slli_w(t2, a4, 0xA);
  __ slli_w(t3, a4, 0x1F);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_slli_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_slli_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_slli_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_slli_w_31)));

  // srli_w
  __ srli_w(t0, a4, 0);
  __ srli_w(t1, a4, 0x8);
  __ srli_w(t2, a4, 0xA);
  __ srli_w(t3, a4, 0x1F);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_srli_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_srli_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_srli_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_srli_w_31)));

  // srai_w
  __ srai_w(t0, a4, 0);
  __ srai_w(t1, a4, 0x8);

  __ li(a6, static_cast<int32_t>(0x80000000));
  __ add_w(a6, a6, a4);
  __ srai_w(t2, a6, 0xA);
  __ srai_w(t3, a6, 0x1F);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_srai_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_srai_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_srai_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_srai_w_31)));

  // rotri_w
  __ rotri_w(t0, a4, 0);
  __ rotri_w(t1, a4, 0x8);
  __ rotri_w(t2, a4, 0xA);
  __ rotri_w(t3, a4, 0x1F);

  __ St_w(t0, MemOperand(a0, offsetof(T, result_rotri_w_0)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_rotri_w_8)));
  __ St_w(t2, MemOperand(a0, offsetof(T, result_rotri_w_10)));
  __ St_w(t3, MemOperand(a0, offsetof(T, result_rotri_w_31)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.input = 0x12345678;
  f.Call(&t, 0x0, 0, 0, 0);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_sll_w_0);
  CHECK_EQ(static_cast<int32_t>(0x34567800), t.result_sll_w_8);
  CHECK_EQ(static_cast<int32_t>(0xD159E000), t.result_sll_w_10);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_sll_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_srl_w_0);
  CHECK_EQ(static_cast<int32_t>(0x123456), t.result_srl_w_8);
  CHECK_EQ(static_cast<int32_t>(0x48D15), t.result_srl_w_10);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_srl_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_sra_w_0);
  CHECK_EQ(static_cast<int32_t>(0x123456), t.result_sra_w_8);
  CHECK_EQ(static_cast<int32_t>(0xFFE48D15), t.result_sra_w_10);
  CHECK_EQ(static_cast<int32_t>(0xFFFFFFFF), t.result_sra_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_rotr_w_0);
  CHECK_EQ(static_cast<int32_t>(0x78123456), t.result_rotr_w_8);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_slli_w_0);
  CHECK_EQ(static_cast<int32_t>(0x34567800), t.result_slli_w_8);
  CHECK_EQ(static_cast<int32_t>(0xD159E000), t.result_slli_w_10);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_slli_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_srli_w_0);
  CHECK_EQ(static_cast<int32_t>(0x123456), t.result_srli_w_8);
  CHECK_EQ(static_cast<int32_t>(0x48D15), t.result_srli_w_10);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_srli_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_srai_w_0);
  CHECK_EQ(static_cast<int32_t>(0x123456), t.result_srai_w_8);
  CHECK_EQ(static_cast<int32_t>(0xFFE48D15), t.result_srai_w_10);
  CHECK_EQ(static_cast<int32_t>(0xFFFFFFFF), t.result_srai_w_31);

  CHECK_EQ(static_cast<int32_t>(0x12345678), t.result_rotri_w_0);
  CHECK_EQ(static_cast<int32_t>(0x78123456), t.result_rotri_w_8);
  CHECK_EQ(static_cast<int32_t>(0x9E048D15), t.result_rotri_w_10);
  CHECK_EQ(static_cast<int32_t>(0x2468ACF0), t.result_rotri_w_31);
}

TEST(LA9) {
  // Test 64bit shift instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    int64_t input;
    int64_t result_sll_d_0;
    int64_t result_sll_d_13;
    int64_t result_sll_d_30;
    int64_t result_sll_d_63;
    int64_t result_srl_d_0;
    int64_t result_srl_d_13;
    int64_t result_srl_d_30;
    int64_t result_srl_d_63;
    int64_t result_sra_d_0;
    int64_t result_sra_d_13;
    int64_t result_sra_d_30;
    int64_t result_sra_d_63;
    int64_t result_rotr_d_0;
    int64_t result_rotr_d_13;
    int64_t result_slli_d_0;
    int64_t result_slli_d_13;
    int64_t result_slli_d_30;
    int64_t result_slli_d_63;
    int64_t result_srli_d_0;
    int64_t result_srli_d_13;
    int64_t result_srli_d_30;
    int64_t result_srli_d_63;
    int64_t result_srai_d_0;
    int64_t result_srai_d_13;
    int64_t result_srai_d_30;
    int64_t result_srai_d_63;
    int64_t result_rotri_d_0;
    int64_t result_rotri_d_13;
    int64_t result_rotri_d_30;
    int64_t result_rotri_d_63;
  };

  T t;
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ Ld_d(a4, MemOperand(a0, offsetof(T, input)));

  // sll_d
  __ li(a5, 0);
  __ sll_d(t0, a4, a5);
  __ li(a5, 0xD);
  __ sll_d(t1, a4, a5);
  __ li(a5, 0x1E);
  __ sll_d(t2, a4, a5);
  __ li(a5, 0x3F);
  __ sll_d(t3, a4, a5);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_sll_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_sll_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_sll_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_sll_d_63)));

  // srl_d
  __ li(a5, 0x0);
  __ srl_d(t0, a4, a5);
  __ li(a5, 0xD);
  __ srl_d(t1, a4, a5);
  __ li(a5, 0x1E);
  __ srl_d(t2, a4, a5);
  __ li(a5, 0x3F);
  __ srl_d(t3, a4, a5);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_srl_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_srl_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_srl_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_srl_d_63)));

  // sra_d
  __ li(a5, 0x0);
  __ sra_d(t0, a4, a5);
  __ li(a5, 0xD);
  __ sra_d(t1, a4, a5);

  __ li(a6, static_cast<int64_t>(0x8000000000000000));
  __ add_d(a6, a6, a4);
  __ li(a5, 0x1E);
  __ sra_d(t2, a6, a5);
  __ li(a5, 0x3F);
  __ sra_d(t3, a6, a5);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_sra_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_sra_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_sra_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_sra_d_63)));

  // rotr
  __ li(a5, 0x0);
  __ rotr_d(t0, a4, a5);
  __ li(a6, 0xD);
  __ rotr_d(t1, a4, a6);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_rotr_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_rotr_d_13)));

  // slli_d
  __ slli_d(t0, a4, 0);
  __ slli_d(t1, a4, 0xD);
  __ slli_d(t2, a4, 0x1E);
  __ slli_d(t3, a4, 0x3F);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_slli_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_slli_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_slli_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_slli_d_63)));

  // srli_d
  __ srli_d(t0, a4, 0);
  __ srli_d(t1, a4, 0xD);
  __ srli_d(t2, a4, 0x1E);
  __ srli_d(t3, a4, 0x3F);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_srli_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_srli_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_srli_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_srli_d_63)));

  // srai_d
  __ srai_d(t0, a4, 0);
  __ srai_d(t1, a4, 0xD);

  __ li(a6, static_cast<int64_t>(0x8000000000000000));
  __ add_d(a6, a6, a4);
  __ srai_d(t2, a6, 0x1E);
  __ srai_d(t3, a6, 0x3F);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_srai_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_srai_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_srai_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_srai_d_63)));

  // rotri_d
  __ rotri_d(t0, a4, 0);
  __ rotri_d(t1, a4, 0xD);
  __ rotri_d(t2, a4, 0x1E);
  __ rotri_d(t3, a4, 0x3F);

  __ St_d(t0, MemOperand(a0, offsetof(T, result_rotri_d_0)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_rotri_d_13)));
  __ St_d(t2, MemOperand(a0, offsetof(T, result_rotri_d_30)));
  __ St_d(t3, MemOperand(a0, offsetof(T, result_rotri_d_63)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.input = 0x51F4B764A26E7412;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_sll_d_0);
  CHECK_EQ(static_cast<int64_t>(0x96ec944dce824000), t.result_sll_d_13);
  CHECK_EQ(static_cast<int64_t>(0x289b9d0480000000), t.result_sll_d_30);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_sll_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_srl_d_0);
  CHECK_EQ(static_cast<int64_t>(0x28fa5bb251373), t.result_srl_d_13);
  CHECK_EQ(static_cast<int64_t>(0x147d2dd92), t.result_srl_d_30);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_srl_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_sra_d_0);
  CHECK_EQ(static_cast<int64_t>(0x28fa5bb251373), t.result_sra_d_13);
  CHECK_EQ(static_cast<int64_t>(0xffffffff47d2dd92), t.result_sra_d_30);
  CHECK_EQ(static_cast<int64_t>(0xffffffffffffffff), t.result_sra_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_rotr_d_0);
  CHECK_EQ(static_cast<int64_t>(0xa0928fa5bb251373), t.result_rotr_d_13);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_slli_d_0);
  CHECK_EQ(static_cast<int64_t>(0x96ec944dce824000), t.result_slli_d_13);
  CHECK_EQ(static_cast<int64_t>(0x289b9d0480000000), t.result_slli_d_30);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_slli_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_srli_d_0);
  CHECK_EQ(static_cast<int64_t>(0x28fa5bb251373), t.result_srli_d_13);
  CHECK_EQ(static_cast<int64_t>(0x147d2dd92), t.result_srli_d_30);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_srli_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_srai_d_0);
  CHECK_EQ(static_cast<int64_t>(0x28fa5bb251373), t.result_srai_d_13);
  CHECK_EQ(static_cast<int64_t>(0xffffffff47d2dd92), t.result_srai_d_30);
  CHECK_EQ(static_cast<int64_t>(0xffffffffffffffff), t.result_srai_d_63);

  CHECK_EQ(static_cast<int64_t>(0x51f4b764a26e7412), t.result_rotri_d_0);
  CHECK_EQ(static_cast<int64_t>(0xa0928fa5bb251373), t.result_rotri_d_13);
  CHECK_EQ(static_cast<int64_t>(0x89b9d04947d2dd92), t.result_rotri_d_30);
  CHECK_EQ(static_cast<int64_t>(0xa3e96ec944dce824), t.result_rotri_d_63);
}

TEST(LA10) {
  // Test 32bit bit operation instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct T {
    int64_t si1;
    int64_t si2;
    int32_t result_ext_w_b_si1;
    int32_t result_ext_w_b_si2;
    int32_t result_ext_w_h_si1;
    int32_t result_ext_w_h_si2;
    int32_t result_clo_w_si1;
    int32_t result_clo_w_si2;
    int32_t result_clz_w_si1;
    int32_t result_clz_w_si2;
    int32_t result_cto_w_si1;
    int32_t result_cto_w_si2;
    int32_t result_ctz_w_si1;
    int32_t result_ctz_w_si2;
    int32_t result_bytepick_w_si1;
    int32_t result_bytepick_w_si2;
    int32_t result_revb_2h_si1;
    int32_t result_revb_2h_si2;
    int32_t result_bitrev_4b_si1;
    int32_t result_bitrev_4b_si2;
    int32_t result_bitrev_w_si1;
    int32_t result_bitrev_w_si2;
    int32_t result_bstrins_w_si1;
    int32_t result_bstrins_w_si2;
    int32_t result_bstrpick_w_si1;
    int32_t result_bstrpick_w_si2;
  };
  T t;

  __ Ld_d(a4, MemOperand(a0, offsetof(T, si1)));
  __ Ld_d(a5, MemOperand(a0, offsetof(T, si2)));

  // ext_w_b
  __ ext_w_b(t0, a4);
  __ ext_w_b(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_ext_w_b_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_ext_w_b_si2)));

  // ext_w_h
  __ ext_w_h(t0, a4);
  __ ext_w_h(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_ext_w_h_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_ext_w_h_si2)));

  /*    //clo_w
    __ clo_w(t0, a4);
    __ clo_w(t1, a5);
    __ St_w(t0, MemOperand(a0, offsetof(T, result_clo_w_si1)));
    __ St_w(t1, MemOperand(a0, offsetof(T, result_clo_w_si2)));*/

  // clz_w
  __ clz_w(t0, a4);
  __ clz_w(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_clz_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_clz_w_si2)));

  /*    //cto_w
    __ cto_w(t0, a4);
    __ cto_w(t1, a5);
    __ St_w(t0, MemOperand(a0, offsetof(T, result_cto_w_si1)));
    __ St_w(t1, MemOperand(a0, offsetof(T, result_cto_w_si2)));*/

  // ctz_w
  __ ctz_w(t0, a4);
  __ ctz_w(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_ctz_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_ctz_w_si2)));

  // bytepick_w
  __ bytepick_w(t0, a4, a5, 0);
  __ bytepick_w(t1, a5, a4, 2);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_bytepick_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_bytepick_w_si2)));

  // revb_2h
  __ revb_2h(t0, a4);
  __ revb_2h(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_revb_2h_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_revb_2h_si2)));

  // bitrev
  __ bitrev_4b(t0, a4);
  __ bitrev_4b(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_bitrev_4b_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_bitrev_4b_si2)));

  // bitrev_w
  __ bitrev_w(t0, a4);
  __ bitrev_w(t1, a5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_bitrev_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_bitrev_w_si2)));

  // bstrins
  __ or_(t0, zero_reg, zero_reg);
  __ or_(t1, zero_reg, zero_reg);
  __ bstrins_w(t0, a4, 0xD, 0x4);
  __ bstrins_w(t1, a5, 0x16, 0x5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_bstrins_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_bstrins_w_si2)));

  // bstrpick
  __ or_(t0, zero_reg, zero_reg);
  __ or_(t1, zero_reg, zero_reg);
  __ bstrpick_w(t0, a4, 0xD, 0x4);
  __ bstrpick_w(t1, a5, 0x16, 0x5);
  __ St_w(t0, MemOperand(a0, offsetof(T, result_bstrpick_w_si1)));
  __ St_w(t1, MemOperand(a0, offsetof(T, result_bstrpick_w_si2)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.si1 = 0x51F4B764A26E7412;
  t.si2 = 0x81F25A87C423B891;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int32_t>(0x12), t.result_ext_w_b_si1);
  CHECK_EQ(static_cast<int32_t>(0xffffff91), t.result_ext_w_b_si2);
  CHECK_EQ(static_cast<int32_t>(0x7412), t.result_ext_w_h_si1);
  CHECK_EQ(static_cast<int32_t>(0xffffb891), t.result_ext_w_h_si2);
  //    CHECK_EQ(static_cast<int32_t>(0x1), t.result_clo_w_si1);
  //    CHECK_EQ(static_cast<int32_t>(0x2), t.result_clo_w_si2);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_clz_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_clz_w_si2);
  //    CHECK_EQ(static_cast<int32_t>(0x0), t.result_cto_w_si1);
  //    CHECK_EQ(static_cast<int32_t>(0x1), t.result_cto_w_si2);
  CHECK_EQ(static_cast<int32_t>(0x1), t.result_ctz_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x0), t.result_ctz_w_si2);
  CHECK_EQ(static_cast<int32_t>(0xc423b891), t.result_bytepick_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x7412c423),
           t.result_bytepick_w_si2);  // 0xffffc423
  CHECK_EQ(static_cast<int32_t>(0x6ea21274), t.result_revb_2h_si1);
  CHECK_EQ(static_cast<int32_t>(0x23c491b8), t.result_revb_2h_si2);
  CHECK_EQ(static_cast<int32_t>(0x45762e48), t.result_bitrev_4b_si1);
  CHECK_EQ(static_cast<int32_t>(0x23c41d89), t.result_bitrev_4b_si2);
  CHECK_EQ(static_cast<int32_t>(0x482e7645), t.result_bitrev_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x891dc423), t.result_bitrev_w_si2);
  CHECK_EQ(static_cast<int32_t>(0x120), t.result_bstrins_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x771220), t.result_bstrins_w_si2);
  CHECK_EQ(static_cast<int32_t>(0x341), t.result_bstrpick_w_si1);
  CHECK_EQ(static_cast<int32_t>(0x11dc4), t.result_bstrpick_w_si2);
}

TEST(LA11) {
  // Test 64bit bit operation instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct T {
    int64_t si1;
    int64_t si2;
    int64_t result_clo_d_si1;
    int64_t result_clo_d_si2;
    int64_t result_clz_d_si1;
    int64_t result_clz_d_si2;
    int64_t result_cto_d_si1;
    int64_t result_cto_d_si2;
    int64_t result_ctz_d_si1;
    int64_t result_ctz_d_si2;
    int64_t result_bytepick_d_si1;
    int64_t result_bytepick_d_si2;
    int64_t result_revb_4h_si1;
    int64_t result_revb_4h_si2;
    int64_t result_revb_2w_si1;
    int64_t result_revb_2w_si2;
    int64_t result_revb_d_si1;
    int64_t result_revb_d_si2;
    int64_t result_revh_2w_si1;
    int64_t result_revh_2w_si2;
    int64_t result_revh_d_si1;
    int64_t result_revh_d_si2;
    int64_t result_bitrev_8b_si1;
    int64_t result_bitrev_8b_si2;
    int64_t result_bitrev_d_si1;
    int64_t result_bitrev_d_si2;
    int64_t result_bstrins_d_si1;
    int64_t result_bstrins_d_si2;
    int64_t result_bstrpick_d_si1;
    int64_t result_bstrpick_d_si2;
    int64_t result_maskeqz_si1;
    int64_t result_maskeqz_si2;
    int64_t result_masknez_si1;
    int64_t result_masknez_si2;
  };

  T t;

  __ Ld_d(a4, MemOperand(a0, offsetof(T, si1)));
  __ Ld_d(a5, MemOperand(a0, offsetof(T, si2)));

  /*    //clo_d
    __ clo_d(t0, a4);
    __ clo_d(t1, a5);
    __ St_w(t0, MemOperand(a0, offsetof(T, result_clo_d_si1)));
    __ St_w(t1, MemOperand(a0, offsetof(T, result_clo_d_si2)));*/

  // clz_d
  __ or_(t0, zero_reg, zero_reg);
  __ clz_d(t0, a4);
  __ clz_d(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_clz_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_clz_d_si2)));

  /*    //cto_d
    __ cto_d(t0, a4);
    __ cto_d(t1, a5);
    __ St_w(t0, MemOperand(a0, offsetof(T, result_cto_d_si1)));
    __ St_w(t1, MemOperand(a0, offsetof(T, result_cto_d_si2)));*/

  // ctz_d
  __ ctz_d(t0, a4);
  __ ctz_d(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_ctz_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_ctz_d_si2)));

  // bytepick_d
  __ bytepick_d(t0, a4, a5, 0);
  __ bytepick_d(t1, a5, a4, 5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_bytepick_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_bytepick_d_si2)));

  // revb_4h
  __ revb_4h(t0, a4);
  __ revb_4h(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_revb_4h_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_revb_4h_si2)));

  // revb_2w
  __ revb_2w(t0, a4);
  __ revb_2w(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_revb_2w_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_revb_2w_si2)));

  // revb_d
  __ revb_d(t0, a4);
  __ revb_d(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_revb_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_revb_d_si2)));

  // revh_2w
  __ revh_2w(t0, a4);
  __ revh_2w(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_revh_2w_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_revh_2w_si2)));

  // revh_d
  __ revh_d(t0, a4);
  __ revh_d(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_revh_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_revh_d_si2)));

  // bitrev_8b
  __ bitrev_8b(t0, a4);
  __ bitrev_8b(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_bitrev_8b_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_bitrev_8b_si2)));

  // bitrev_d
  __ bitrev_d(t0, a4);
  __ bitrev_d(t1, a5);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_bitrev_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_bitrev_d_si2)));

  // bstrins_d
  __ or_(t0, zero_reg, zero_reg);
  __ or_(t1, zero_reg, zero_reg);
  __ bstrins_d(t0, a4, 5, 0);
  __ bstrins_d(t1, a5, 39, 12);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_bstrins_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_bstrins_d_si2)));

  // bstrpick_d
  __ or_(t0, zero_reg, zero_reg);
  __ or_(t1, zero_reg, zero_reg);
  __ bstrpick_d(t0, a4, 5, 0);
  __ bstrpick_d(t1, a5, 63, 48);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_bstrpick_d_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_bstrpick_d_si2)));

  // maskeqz
  __ maskeqz(t0, a4, a4);
  __ maskeqz(t1, a5, zero_reg);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_maskeqz_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_maskeqz_si2)));

  // masknez
  __ masknez(t0, a4, a4);
  __ masknez(t1, a5, zero_reg);
  __ St_d(t0, MemOperand(a0, offsetof(T, result_masknez_si1)));
  __ St_d(t1, MemOperand(a0, offsetof(T, result_masknez_si2)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.si1 = 0x10C021098B710CDE;
  t.si2 = 0xFB8017FF781A15C3;
  f.Call(&t, 0, 0, 0, 0);

  //    CHECK_EQ(static_cast<int64_t>(0x0), t.result_clo_d_si1);
  //    CHECK_EQ(static_cast<int64_t>(0x5), t.result_clo_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x3), t.result_clz_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_clz_d_si2);
  //    CHECK_EQ(static_cast<int64_t>(0x0), t.result_cto_d_si1);
  //    CHECK_EQ(static_cast<int64_t>(0x2), t.result_cto_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x1), t.result_ctz_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x0), t.result_ctz_d_si2);
  CHECK_EQ(static_cast<int64_t>(0xfb8017ff781a15c3), t.result_bytepick_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x710cdefb8017ff78), t.result_bytepick_d_si2);
  CHECK_EQ(static_cast<int64_t>(0xc0100921718bde0c), t.result_revb_4h_si1);
  CHECK_EQ(static_cast<int64_t>(0x80fbff171a78c315), t.result_revb_4h_si2);
  CHECK_EQ(static_cast<int64_t>(0x921c010de0c718b), t.result_revb_2w_si1);
  CHECK_EQ(static_cast<int64_t>(0xff1780fbc3151a78), t.result_revb_2w_si2);
  CHECK_EQ(static_cast<int64_t>(0xde0c718b0921c010), t.result_revb_d_si1);
  CHECK_EQ(static_cast<int64_t>(0xc3151a78ff1780fb), t.result_revb_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x210910c00cde8b71), t.result_revh_2w_si1);
  CHECK_EQ(static_cast<int64_t>(0x17fffb8015c3781a), t.result_revh_2w_si2);
  CHECK_EQ(static_cast<int64_t>(0xcde8b71210910c0), t.result_revh_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x15c3781a17fffb80), t.result_revh_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x8038490d18e307b), t.result_bitrev_8b_si1);
  CHECK_EQ(static_cast<int64_t>(0xdf01e8ff1e58a8c3), t.result_bitrev_8b_si2);
  CHECK_EQ(static_cast<int64_t>(0x7b308ed190840308), t.result_bitrev_d_si1);
  CHECK_EQ(static_cast<int64_t>(0xc3a8581effe801df), t.result_bitrev_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x1e), t.result_bstrins_d_si1);
  CHECK_EQ(static_cast<int64_t>(0x81a15c3000), t.result_bstrins_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x1e), t.result_bstrpick_d_si1);
  CHECK_EQ(static_cast<int64_t>(0xfb80), t.result_bstrpick_d_si2);
  CHECK_EQ(static_cast<int64_t>(0x10C021098B710CDE), t.result_maskeqz_si1);
  CHECK_EQ(static_cast<int64_t>(0), t.result_maskeqz_si2);
  CHECK_EQ(static_cast<int64_t>(0), t.result_masknez_si1);
  CHECK_EQ(static_cast<int64_t>(0xFB8017FF781A15C3), t.result_masknez_si2);
}

uint64_t run_beq(int64_t value1, int64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ beq(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BEQ) {
  CcTest::InitializeVM();
  struct TestCaseBeq {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBeq tc[] = {
    // value1, value2, offset, expected_res
    {       0,      0,    -6,          0x3 },
    {       1,      1,    -3,         0x30 },
    {      -2,     -2,     3,        0x300 },
    {       3,     -3,     6,            0 },
    {       4,      4,     6,        0x700 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBeq);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_beq(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bne(int64_t value1, int64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bne(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BNE) {
  CcTest::InitializeVM();
  struct TestCaseBne {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBne tc[] = {
    // value1, value2, offset, expected_res
    {       1,     -1,    -6,          0x3 },
    {       2,     -2,    -3,         0x30 },
    {       3,     -3,     3,        0x300 },
    {       4,     -4,     6,        0x700 },
    {       0,      0,     6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBne);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bne(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_blt(int64_t value1, int64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ blt(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BLT) {
  CcTest::InitializeVM();
  struct TestCaseBlt {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBlt tc[] = {
    // value1, value2, offset, expected_res
    {      -1,      1,    -6,          0x3 },
    {      -2,      2,    -3,         0x30 },
    {      -3,      3,     3,        0x300 },
    {      -4,      4,     6,        0x700 },
    {       5,     -5,     6,            0 },
    {       0,      0,     6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBlt);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_blt(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bge(uint64_t value1, uint64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bge(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BGE) {
  CcTest::InitializeVM();
  struct TestCaseBge {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBge tc[] = {
    // value1, value2, offset, expected_res
    {       0,      0,    -6,          0x3 },
    {       1,      1,    -3,         0x30 },
    {       2,     -2,     3,        0x300 },
    {       3,     -3,     6,        0x700 },
    {      -4,      4,     6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBge);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bge(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bltu(int64_t value1, int64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bltu(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BLTU) {
  CcTest::InitializeVM();
  struct TestCaseBltu {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBltu tc[] = {
    // value1, value2, offset, expected_res
    {       0,      1,    -6,          0x3 },
    {       1,     -1,    -3,         0x30 },
    {       2,     -2,     3,        0x300 },
    {       3,     -3,     6,        0x700 },
    {       4,      4,     6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBltu);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bltu(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bgeu(int64_t value1, int64_t value2, int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bgeu(a0, a1, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value1, value2, 0, 0, 0));

  return res;
}

TEST(BGEU) {
  CcTest::InitializeVM();
  struct TestCaseBgeu {
    int64_t value1;
    int64_t value2;
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBgeu tc[] = {
    // value1, value2, offset, expected_res
    {       0,      0,    -6,          0x3 },
    {      -1,      1,    -3,         0x30 },
    {      -2,      2,     3,        0x300 },
    {      -3,      3,     6,        0x700 },
    {       4,     -4,     6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBgeu);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bgeu(tc[i].value1, tc[i].value2, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_beqz(int64_t value, int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(&L);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ beqz(a0, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(&L);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value, 0, 0, 0, 0));

  return res;
}

TEST(BEQZ) {
  CcTest::InitializeVM();
  struct TestCaseBeqz {
    int64_t value;
    int32_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBeqz tc[] = {
    // value, offset, expected_res
    {      0,     -6,          0x3 },
    {      0,     -3,         0x30 },
    {      0,      3,        0x300 },
    {      0,      6,        0x700 },
    {      1,      6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBeqz);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_beqz(tc[i].value, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bnez_b(int64_t value, int32_t offset) {
  // bnez, b.
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0l);
  __ b(&main_block);
  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ b(5);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ b(2);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bnez(a0, offset);
  __ bind(&L);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ b(-4);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ b(-7);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value, 0, 0, 0, 0));

  return res;
}

TEST(BNEZ_B) {
  CcTest::InitializeVM();
  struct TestCaseBnez {
    int64_t value;
    int32_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBnez tc[] = {
    // value, offset, expected_res
    {      1,     -6,          0x3 },
    {     -2,     -3,         0x30 },
    {      3,      3,        0x300 },
    {     -4,      6,        0x700 },
    {      0,      6,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBnez);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bnez_b(tc[i].value, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bl(int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block;
  __ li(a2, 0l);
  __ Push(ra);  // Push is implemented by two instructions, addi_d and st_d
  __ b(&main_block);

  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ jirl(zero_reg, ra, 0);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ jirl(zero_reg, ra, 0);

  // Block 3 (Main)
  __ bind(&main_block);
  __ bl(offset);
  __ or_(a0, a2, zero_reg);
  __ Pop(ra);  // Pop is implemented by two instructions, ld_d and addi_d.
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ jirl(zero_reg, ra, 0);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(BL) {
  CcTest::InitializeVM();
  struct TestCaseBl {
    int32_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBl tc[] = {
    // offset, expected_res
    {     -6,          0x3 },
    {     -3,         0x30 },
    {      5,        0x300 },
    {      8,        0x700 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBl);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bl(tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

TEST(PCADD) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;
  __ Push(ra);

  // pcaddi
  __ li(a4, 0x1FFFFC);
  __ li(a5, 0);
  __ li(a6, static_cast<int32_t>(0xFFE00000));

  __ bl(1);
  __ pcaddi(a3, 0x7FFFF);
  __ add_d(a2, ra, a4);
  __ Branch(&error, ne, a2, Operand(a3));

  __ bl(1);
  __ pcaddi(a3, 0);
  __ add_d(a2, ra, a5);
  __ Branch(&error, ne, a2, Operand(a3));

  __ bl(1);
  __ pcaddi(a3, 0x80000);
  __ add_d(a2, ra, a6);
  __ Branch(&error, ne, a2, Operand(a3));

  // pcaddu12i
  __ li(a4, 0x7FFFF000);
  __ li(a5, 0);
  __ li(a6, static_cast<int32_t>(0x80000000));

  __ bl(1);
  __ pcaddu12i(a2, 0x7FFFF);
  __ add_d(a3, ra, a4);
  __ Branch(&error, ne, a2, Operand(a3));
  __ bl(1);
  __ pcaddu12i(a2, 0);
  __ add_d(a3, ra, a5);
  __ Branch(&error, ne, a2, Operand(a3));
  __ bl(1);
  __ pcaddu12i(a2, 0x80000);
  __ add_d(a3, ra, a6);
  __ Branch(&error, ne, a2, Operand(a3));

  // pcaddu18i
  __ li(a4, 0x1FFFFC0000);
  __ li(a5, 0);
  __ li(a6, static_cast<int64_t>(0xFFFFFFE000000000));

  __ bl(1);
  __ pcaddu18i(a2, 0x7FFFF);
  __ add_d(a3, ra, a4);
  __ Branch(&error, ne, a2, Operand(a3));

  __ bl(1);
  __ pcaddu18i(a2, 0);
  __ add_d(a3, ra, a5);
  __ Branch(&error, ne, a2, Operand(a3));

  __ bl(1);
  __ pcaddu18i(a2, 0x80000);
  __ add_d(a3, ra, a6);
  __ Branch(&error, ne, a2, Operand(a3));

  // pcalau12i
  __ li(a4, 0x7FFFF000);
  __ li(a5, 0);
  __ li(a6, static_cast<int32_t>(0x80000000));
  __ li(a7, static_cast<int64_t>(0xFFFFFFFFFFFFF000));

  __ bl(1);
  __ pcalau12i(a3, 0x7FFFF);
  __ add_d(a2, ra, a4);
  __ and_(t0, a2, a7);
  __ and_(t1, a3, a7);
  __ Branch(&error, ne, t0, Operand(t1));

  __ bl(1);
  __ pcalau12i(a3, 0);
  __ add_d(a2, ra, a5);
  __ and_(t0, a2, a7);
  __ and_(t1, a3, a7);
  __ Branch(&error, ne, t0, Operand(t1));

  __ bl(1);
  __ pcalau12i(a2, 0x80000);
  __ add_d(a3, ra, a6);
  __ and_(t0, a2, a7);
  __ and_(t1, a3, a7);
  __ Branch(&error, ne, t0, Operand(t1));

  __ li(a0, 0x31415926);
  __ b(&exit);

  __ bind(&error);
  __ li(a0, 0x666);

  __ bind(&exit);
  __ Pop(ra);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}

uint64_t run_jirl(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block;
  __ li(a2, 0l);
  __ Push(ra);
  __ b(&main_block);

  // Block 1
  __ addi_d(a2, a2, 0x1);
  __ addi_d(a2, a2, 0x2);
  __ jirl(zero_reg, ra, 0);

  // Block 2
  __ addi_d(a2, a2, 0x10);
  __ addi_d(a2, a2, 0x20);
  __ jirl(zero_reg, ra, 0);

  // Block 3 (Main)
  __ bind(&main_block);
  __ pcaddi(a3, 1);
  __ jirl(ra, a3, offset);
  __ or_(a0, a2, zero_reg);
  __ Pop(ra);  // Pop is implemented by two instructions, ld_d and addi_d.
  __ jirl(zero_reg, ra, 0);

  // Block 4
  __ addi_d(a2, a2, 0x100);
  __ addi_d(a2, a2, 0x200);
  __ jirl(zero_reg, ra, 0);

  // Block 5
  __ addi_d(a2, a2, 0x300);
  __ addi_d(a2, a2, 0x400);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(JIRL) {
  CcTest::InitializeVM();
  struct TestCaseJirl {
    int16_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseJirl tc[] = {
    // offset, expected_res
    {     -7,          0x3 },
    {     -4,         0x30 },
    {      5,        0x300 },
    {      8,        0x700 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseJirl);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_jirl(tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

TEST(LA12) {
  // Test floating point calculate instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double result_fadd_d;
    double result_fsub_d;
    double result_fmul_d;
    double result_fdiv_d;
    double result_fmadd_d;
    double result_fmsub_d;
    double result_fnmadd_d;
    double result_fnmsub_d;
    double result_fsqrt_d;
    double result_frecip_d;
    double result_frsqrt_d;
    double result_fscaleb_d;
    double result_flogb_d;
    double result_fcopysign_d;
    double result_fclass_d;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Double precision floating point instructions.
  __ Fld_d(f8, MemOperand(a0, offsetof(T, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(T, b)));

  __ fneg_d(f10, f8);
  __ fadd_d(f11, f9, f10);
  __ Fst_d(f11, MemOperand(a0, offsetof(T, result_fadd_d)));
  __ fabs_d(f11, f11);
  __ fsub_d(f12, f11, f9);
  __ Fst_d(f12, MemOperand(a0, offsetof(T, result_fsub_d)));

  __ Fld_d(f13, MemOperand(a0, offsetof(T, c)));
  __ Fld_d(f14, MemOperand(a0, offsetof(T, d)));
  __ Fld_d(f15, MemOperand(a0, offsetof(T, e)));

  __ fmin_d(f16, f13, f14);
  __ fmul_d(f17, f15, f16);
  __ Fst_d(f17, MemOperand(a0, offsetof(T, result_fmul_d)));
  __ fmax_d(f18, f13, f14);
  __ fdiv_d(f19, f15, f18);
  __ Fst_d(f19, MemOperand(a0, offsetof(T, result_fdiv_d)));

  __ fmina_d(f16, f13, f14);
  __ fmadd_d(f18, f17, f15, f16);
  __ Fst_d(f18, MemOperand(a0, offsetof(T, result_fmadd_d)));
  __ fnmadd_d(f19, f17, f15, f16);
  __ Fst_d(f19, MemOperand(a0, offsetof(T, result_fnmadd_d)));
  __ fmaxa_d(f16, f13, f14);
  __ fmsub_d(f20, f17, f15, f16);
  __ Fst_d(f20, MemOperand(a0, offsetof(T, result_fmsub_d)));
  __ fnmsub_d(f21, f17, f15, f16);
  __ Fst_d(f21, MemOperand(a0, offsetof(T, result_fnmsub_d)));

  __ Fld_d(f8, MemOperand(a0, offsetof(T, f)));
  __ fsqrt_d(f10, f8);
  __ Fst_d(f10, MemOperand(a0, offsetof(T, result_fsqrt_d)));
  //__ frecip_d(f11, f10);
  //__ frsqrt_d(f12, f8);
  //__ Fst_d(f11, MemOperand(a0, offsetof(T, result_frecip_d)));
  //__ Fst_d(f12, MemOperand(a0, offsetof(T, result_frsqrt_d)));

  /*__ fscaleb_d(f16, f13, f15);
  __ flogb_d(f17, f15);
  __ fcopysign_d(f18, f8, f9);
  __ fclass_d(f19, f9);
  __ Fst_d(f16, MemOperand(a0, offsetof(T, result_fscaleb_d)));
  __ Fst_d(f17, MemOperand(a0, offsetof(T, result_flogb_d)));
  __ Fst_d(f18, MemOperand(a0, offsetof(T, result_fcopysign_d)));
  __ Fst_d(f19, MemOperand(a0, offsetof(T, result_fclass_d)));*/

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  // Double test values.
  t.a = 1.5e14;
  t.b = -2.75e11;
  t.c = 1.5;
  t.d = -2.75;
  t.e = 120.0;
  t.f = 120.44;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<double>(-1.502750e14), t.result_fadd_d);
  CHECK_EQ(static_cast<double>(1.505500e14), t.result_fsub_d);
  CHECK_EQ(static_cast<double>(-3.300000e02), t.result_fmul_d);
  CHECK_EQ(static_cast<double>(8.000000e01), t.result_fdiv_d);
  CHECK_EQ(static_cast<double>(-3.959850e04), t.result_fmadd_d);
  CHECK_EQ(static_cast<double>(-3.959725e04), t.result_fmsub_d);
  CHECK_EQ(static_cast<double>(3.959850e04), t.result_fnmadd_d);
  CHECK_EQ(static_cast<double>(3.959725e04), t.result_fnmsub_d);
  CHECK_EQ(static_cast<double>(10.97451593465515908537), t.result_fsqrt_d);
  // CHECK_EQ(static_cast<double>( 8.164965e-08), t.result_frecip_d);
  // CHECK_EQ(static_cast<double>( 8.164966e-08), t.result_frsqrt_d);
  // CHECK_EQ(static_cast<double>(), t.result_fscaleb_d);
  // CHECK_EQ(static_cast<double>( 6.906891), t.result_flogb_d);
  // CHECK_EQ(static_cast<double>( 2.75e11), t.result_fcopysign_d);
  // CHECK_EQ(static_cast<double>(), t.result_fclass_d);
}

TEST(LA13) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    float a;
    float b;
    float c;
    float d;
    float e;
    float result_fadd_s;
    float result_fsub_s;
    float result_fmul_s;
    float result_fdiv_s;
    float result_fmadd_s;
    float result_fmsub_s;
    float result_fnmadd_s;
    float result_fnmsub_s;
    float result_fsqrt_s;
    float result_frecip_s;
    float result_frsqrt_s;
    float result_fscaleb_s;
    float result_flogb_s;
    float result_fcopysign_s;
    float result_fclass_s;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Float precision floating point instructions.
  __ Fld_s(f8, MemOperand(a0, offsetof(T, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(T, b)));

  __ fneg_s(f10, f8);
  __ fadd_s(f11, f9, f10);
  __ Fst_s(f11, MemOperand(a0, offsetof(T, result_fadd_s)));
  __ fabs_s(f11, f11);
  __ fsub_s(f12, f11, f9);
  __ Fst_s(f12, MemOperand(a0, offsetof(T, result_fsub_s)));

  __ Fld_s(f13, MemOperand(a0, offsetof(T, c)));
  __ Fld_s(f14, MemOperand(a0, offsetof(T, d)));
  __ Fld_s(f15, MemOperand(a0, offsetof(T, e)));

  __ fmin_s(f16, f13, f14);
  __ fmul_s(f17, f15, f16);
  __ Fst_s(f17, MemOperand(a0, offsetof(T, result_fmul_s)));
  __ fmax_s(f18, f13, f14);
  __ fdiv_s(f19, f15, f18);
  __ Fst_s(f19, MemOperand(a0, offsetof(T, result_fdiv_s)));

  __ fmina_s(f16, f13, f14);
  __ fmadd_s(f18, f17, f15, f16);
  __ Fst_s(f18, MemOperand(a0, offsetof(T, result_fmadd_s)));
  __ fnmadd_s(f19, f17, f15, f16);
  __ Fst_s(f19, MemOperand(a0, offsetof(T, result_fnmadd_s)));
  __ fmaxa_s(f16, f13, f14);
  __ fmsub_s(f20, f17, f15, f16);
  __ Fst_s(f20, MemOperand(a0, offsetof(T, result_fmsub_s)));
  __ fnmsub_s(f21, f17, f15, f16);
  __ Fst_s(f21, MemOperand(a0, offsetof(T, result_fnmsub_s)));

  __ fsqrt_s(f10, f8);
  //__ frecip_s(f11, f10);
  //__ frsqrt_s(f12, f8);
  __ Fst_s(f10, MemOperand(a0, offsetof(T, result_fsqrt_s)));
  //__ Fst_s(f11, MemOperand(a0, offsetof(T, result_frecip_s)));
  //__ Fst_s(f12, MemOperand(a0, offsetof(T, result_frsqrt_s)));

  /*__ fscaleb_s(f16, f13, f15);
  __ flogb_s(f17, f15);
  __ fcopysign_s(f18, f8, f9);
  __ fclass_s(f19, f9);
  __ Fst_s(f16, MemOperand(a0, offsetof(T, result_fscaleb_s)));
  __ Fst_s(f17, MemOperand(a0, offsetof(T, result_flogb_s)));
  __ Fst_s(f18, MemOperand(a0, offsetof(T, result_fcopysign_s)));
  __ Fst_s(f19, MemOperand(a0, offsetof(T, result_fclass_s)));*/
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  // Float test values.
  t.a = 1.5e6;
  t.b = -2.75e4;
  t.c = 1.5;
  t.d = -2.75;
  t.e = 120.0;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<float>(-1.527500e06), t.result_fadd_s);
  CHECK_EQ(static_cast<float>(1.555000e06), t.result_fsub_s);
  CHECK_EQ(static_cast<float>(-3.300000e02), t.result_fmul_s);
  CHECK_EQ(static_cast<float>(8.000000e01), t.result_fdiv_s);
  CHECK_EQ(static_cast<float>(-3.959850e04), t.result_fmadd_s);
  CHECK_EQ(static_cast<float>(-3.959725e04), t.result_fmsub_s);
  CHECK_EQ(static_cast<float>(3.959850e04), t.result_fnmadd_s);
  CHECK_EQ(static_cast<float>(3.959725e04), t.result_fnmsub_s);
  CHECK_EQ(static_cast<float>(1224.744873), t.result_fsqrt_s);
  // CHECK_EQ(static_cast<float>( 8.164966e-04), t.result_frecip_s);
  // CHECK_EQ(static_cast<float>( 8.164966e-04), t.result_frsqrt_s);
  // CHECK_EQ(static_cast<float>(), t.result_fscaleb_s);
  // CHECK_EQ(static_cast<float>( 6.906890), t.result_flogb_s);
  // CHECK_EQ(static_cast<float>( 2.75e4), t.result_fcopysign_s);
  // CHECK_EQ(static_cast<float>(), t.result_fclass_s);
}

TEST(FCMP_COND) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double dTrue;
    double dFalse;
    double dOp1;
    double dOp2;
    double dCaf;
    double dCun;
    double dCeq;
    double dCueq;
    double dClt;
    double dCult;
    double dCle;
    double dCule;
    double dCne;
    double dCor;
    double dCune;
    double dSaf;
    double dSun;
    double dSeq;
    double dSueq;
    double dSlt;
    double dSult;
    double dSle;
    double dSule;
    double dSne;
    double dSor;
    double dSune;
    float fTrue;
    float fFalse;
    float fOp1;
    float fOp2;
    float fCaf;
    float fCun;
    float fCeq;
    float fCueq;
    float fClt;
    float fCult;
    float fCle;
    float fCule;
    float fCne;
    float fCor;
    float fCune;
    float fSaf;
    float fSun;
    float fSeq;
    float fSueq;
    float fSlt;
    float fSult;
    float fSle;
    float fSule;
    float fSne;
    float fSor;
    float fSune;
  };

  TestFloat test;

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, dOp1)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, dOp2)));

  __ Fld_s(f10, MemOperand(a0, offsetof(TestFloat, fOp1)));
  __ Fld_s(f11, MemOperand(a0, offsetof(TestFloat, fOp2)));

  __ Fld_d(f12, MemOperand(a0, offsetof(TestFloat, dFalse)));
  __ Fld_d(f13, MemOperand(a0, offsetof(TestFloat, dTrue)));

  __ Fld_s(f14, MemOperand(a0, offsetof(TestFloat, fFalse)));
  __ Fld_s(f15, MemOperand(a0, offsetof(TestFloat, fTrue)));

  __ fcmp_cond_d(CAF, f8, f9, FCC0);
  __ fcmp_cond_s(CAF, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCaf)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCaf)));

  __ fcmp_cond_d(CUN, f8, f9, FCC0);
  __ fcmp_cond_s(CUN, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCun)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCun)));

  __ fcmp_cond_d(CEQ, f8, f9, FCC0);
  __ fcmp_cond_s(CEQ, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCeq)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCeq)));

  __ fcmp_cond_d(CUEQ, f8, f9, FCC0);
  __ fcmp_cond_s(CUEQ, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCueq)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCueq)));

  __ fcmp_cond_d(CLT, f8, f9, FCC0);
  __ fcmp_cond_s(CLT, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dClt)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fClt)));

  __ fcmp_cond_d(CULT, f8, f9, FCC0);
  __ fcmp_cond_s(CULT, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCult)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCult)));

  __ fcmp_cond_d(CLE, f8, f9, FCC0);
  __ fcmp_cond_s(CLE, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCle)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCle)));

  __ fcmp_cond_d(CULE, f8, f9, FCC0);
  __ fcmp_cond_s(CULE, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCule)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCule)));

  __ fcmp_cond_d(CNE, f8, f9, FCC0);
  __ fcmp_cond_s(CNE, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCne)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCne)));

  __ fcmp_cond_d(COR, f8, f9, FCC0);
  __ fcmp_cond_s(COR, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCor)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCor)));

  __ fcmp_cond_d(CUNE, f8, f9, FCC0);
  __ fcmp_cond_s(CUNE, f10, f11, FCC1);
  __ fsel(FCC0, f16, f12, f13);
  __ fsel(FCC1, f17, f14, f15);
  __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dCune)));
  __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fCune)));

  /*  __ fcmp_cond_d(SAF, f8, f9, FCC0);
    __ fcmp_cond_s(SAF, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSaf)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSaf)));

    __ fcmp_cond_d(SUN, f8, f9, FCC0);
    __ fcmp_cond_s(SUN, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSun)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSun)));

    __ fcmp_cond_d(SEQ, f8, f9, FCC0);
    __ fcmp_cond_s(SEQ, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSeq)));
    __ Fst_f(f17, MemOperand(a0, offsetof(TestFloat, fSeq)));

    __ fcmp_cond_d(SUEQ, f8, f9, FCC0);
    __ fcmp_cond_s(SUEQ, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSueq)));
    __ Fst_f(f17, MemOperand(a0, offsetof(TestFloat, fSueq)));

    __ fcmp_cond_d(SLT, f8, f9, FCC0);
    __ fcmp_cond_s(SLT, f10, f11, FCC1);
    __ fsel(f16, f12, f13, FCC0);
    __ fsel(f17, f14, f15, FCC1);
    __ Fld_d(f16, MemOperand(a0, offsetof(TestFloat, dSlt)));
    __ Fst_d(f17, MemOperand(a0, offsetof(TestFloat, fSlt)));

    __ fcmp_cond_d(SULT, f8, f9, FCC0);
    __ fcmp_cond_s(SULT, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSult)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSult)));

    __ fcmp_cond_d(SLE, f8, f9, FCC0);
    __ fcmp_cond_s(SLE, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSle)));
    __ Fst_f(f17, MemOperand(a0, offsetof(TestFloat, fSle)));

    __ fcmp_cond_d(SULE, f8, f9, FCC0);
    __ fcmp_cond_s(SULE, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSule)));
    __ Fst_f(f17, MemOperand(a0, offsetof(TestFloat, fSule)));

    __ fcmp_cond_d(SNE, f8, f9, FCC0);
    __ fcmp_cond_s(SNE, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSne)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSne)));

    __ fcmp_cond_d(SOR, f8, f9, FCC0);
    __ fcmp_cond_s(SOR, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSor)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSor)));

    __ fcmp_cond_d(SUNE, f8, f9, FCC0);
    __ fcmp_cond_s(SUNE, f10, f11, FCC1);
    __ fsel(FCC0, f16, f12, f13);
    __ fsel(FCC1, f17, f14, f15);
    __ Fst_d(f16, MemOperand(a0, offsetof(TestFloat, dSune)));
    __ Fst_s(f17, MemOperand(a0, offsetof(TestFloat, fSune)));*/

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.dTrue = 1234.0;
  test.dFalse = 0.0;
  test.fTrue = 12.0;
  test.fFalse = 0.0;

  test.dOp1 = 2.0;
  test.dOp2 = 3.0;
  test.fOp1 = 2.0;
  test.fOp2 = 3.0;
  f.Call(&test, 0, 0, 0, 0);

  CHECK_EQ(test.dCaf, test.dFalse);
  CHECK_EQ(test.fCaf, test.fFalse);
  CHECK_EQ(test.dCun, test.dFalse);
  CHECK_EQ(test.fCun, test.fFalse);
  CHECK_EQ(test.dCeq, test.dFalse);
  CHECK_EQ(test.fCeq, test.fFalse);
  CHECK_EQ(test.dCueq, test.dFalse);
  CHECK_EQ(test.fCueq, test.fFalse);
  CHECK_EQ(test.dClt, test.dTrue);
  CHECK_EQ(test.fClt, test.fTrue);
  CHECK_EQ(test.dCult, test.dTrue);
  CHECK_EQ(test.fCult, test.fTrue);
  CHECK_EQ(test.dCle, test.dTrue);
  CHECK_EQ(test.fCle, test.fTrue);
  CHECK_EQ(test.dCule, test.dTrue);
  CHECK_EQ(test.fCule, test.fTrue);
  CHECK_EQ(test.dCne, test.dTrue);
  CHECK_EQ(test.fCne, test.fTrue);
  CHECK_EQ(test.dCor, test.dTrue);
  CHECK_EQ(test.fCor, test.fTrue);
  CHECK_EQ(test.dCune, test.dTrue);
  CHECK_EQ(test.fCune, test.fTrue);
  /*  CHECK_EQ(test.dSaf, test.dFalse);
    CHECK_EQ(test.fSaf, test.fFalse);
    CHECK_EQ(test.dSun, test.dFalse);
    CHECK_EQ(test.fSun, test.fFalse);
    CHECK_EQ(test.dSeq, test.dFalse);
    CHECK_EQ(test.fSeq, test.fFalse);
    CHECK_EQ(test.dSueq, test.dFalse);
    CHECK_EQ(test.fSueq, test.fFalse);
    CHECK_EQ(test.dClt, test.dTrue);
    CHECK_EQ(test.fClt, test.fTrue);
    CHECK_EQ(test.dCult, test.dTrue);
    CHECK_EQ(test.fCult, test.fTrue);
    CHECK_EQ(test.dSle, test.dTrue);
    CHECK_EQ(test.fSle, test.fTrue);
    CHECK_EQ(test.dSule, test.dTrue);
    CHECK_EQ(test.fSule, test.fTrue);
    CHECK_EQ(test.dSne, test.dTrue);
    CHECK_EQ(test.fSne, test.fTrue);
    CHECK_EQ(test.dSor, test.dTrue);
    CHECK_EQ(test.fSor, test.fTrue);
    CHECK_EQ(test.dSune, test.dTrue);
    CHECK_EQ(test.fSune, test.fTrue);*/

  test.dOp1 = std::numeric_limits<double>::max();
  test.dOp2 = std::numeric_limits<double>::min();
  test.fOp1 = std::numeric_limits<float>::min();
  test.fOp2 = -std::numeric_limits<float>::max();
  f.Call(&test, 0, 0, 0, 0);

  CHECK_EQ(test.dCaf, test.dFalse);
  CHECK_EQ(test.fCaf, test.fFalse);
  CHECK_EQ(test.dCun, test.dFalse);
  CHECK_EQ(test.fCun, test.fFalse);
  CHECK_EQ(test.dCeq, test.dFalse);
  CHECK_EQ(test.fCeq, test.fFalse);
  CHECK_EQ(test.dCueq, test.dFalse);
  CHECK_EQ(test.fCueq, test.fFalse);
  CHECK_EQ(test.dClt, test.dFalse);
  CHECK_EQ(test.fClt, test.fFalse);
  CHECK_EQ(test.dCult, test.dFalse);
  CHECK_EQ(test.fCult, test.fFalse);
  CHECK_EQ(test.dCle, test.dFalse);
  CHECK_EQ(test.fCle, test.fFalse);
  CHECK_EQ(test.dCule, test.dFalse);
  CHECK_EQ(test.fCule, test.fFalse);
  CHECK_EQ(test.dCne, test.dTrue);
  CHECK_EQ(test.fCne, test.fTrue);
  CHECK_EQ(test.dCor, test.dTrue);
  CHECK_EQ(test.fCor, test.fTrue);
  CHECK_EQ(test.dCune, test.dTrue);
  CHECK_EQ(test.fCune, test.fTrue);
  /*  CHECK_EQ(test.dSaf, test.dFalse);
    CHECK_EQ(test.fSaf, test.fFalse);
    CHECK_EQ(test.dSun, test.dFalse);
    CHECK_EQ(test.fSun, test.fFalse);
    CHECK_EQ(test.dSeq, test.dFalse);
    CHECK_EQ(test.fSeq, test.fFalse);
    CHECK_EQ(test.dSueq, test.dFalse);
    CHECK_EQ(test.fSueq, test.fFalse);
    CHECK_EQ(test.dSlt, test.dFalse);
    CHECK_EQ(test.fSlt, test.fFalse);
    CHECK_EQ(test.dSult, test.dFalse);
    CHECK_EQ(test.fSult, test.fFalse);
    CHECK_EQ(test.dSle, test.dFalse);
    CHECK_EQ(test.fSle, test.fFalse);
    CHECK_EQ(test.dSule, test.dFalse);
    CHECK_EQ(test.fSule, test.fFalse);
    CHECK_EQ(test.dSne, test.dTrue);
    CHECK_EQ(test.fSne, test.fTrue);
    CHECK_EQ(test.dSor, test.dTrue);
    CHECK_EQ(test.fSor, test.fTrue);
    CHECK_EQ(test.dSune, test.dTrue);
    CHECK_EQ(test.fSune, test.fTrue);*/

  test.dOp1 = std::numeric_limits<double>::quiet_NaN();
  test.dOp2 = 0.0;
  test.fOp1 = std::numeric_limits<float>::quiet_NaN();
  test.fOp2 = 0.0;
  f.Call(&test, 0, 0, 0, 0);

  CHECK_EQ(test.dCaf, test.dFalse);
  CHECK_EQ(test.fCaf, test.fFalse);
  CHECK_EQ(test.dCun, test.dTrue);
  CHECK_EQ(test.fCun, test.fTrue);
  CHECK_EQ(test.dCeq, test.dFalse);
  CHECK_EQ(test.fCeq, test.fFalse);
  CHECK_EQ(test.dCueq, test.dTrue);
  CHECK_EQ(test.fCueq, test.fTrue);
  CHECK_EQ(test.dClt, test.dFalse);
  CHECK_EQ(test.fClt, test.fFalse);
  CHECK_EQ(test.dCult, test.dTrue);
  CHECK_EQ(test.fCult, test.fTrue);
  CHECK_EQ(test.dCle, test.dFalse);
  CHECK_EQ(test.fCle, test.fFalse);
  CHECK_EQ(test.dCule, test.dTrue);
  CHECK_EQ(test.fCule, test.fTrue);
  CHECK_EQ(test.dCne, test.dFalse);
  CHECK_EQ(test.fCne, test.fFalse);
  CHECK_EQ(test.dCor, test.dFalse);
  CHECK_EQ(test.fCor, test.fFalse);
  CHECK_EQ(test.dCune, test.dTrue);
  CHECK_EQ(test.fCune, test.fTrue);
  /*  CHECK_EQ(test.dSaf, test.dTrue);
    CHECK_EQ(test.fSaf, test.fTrue);
    CHECK_EQ(test.dSun, test.dTrue);
    CHECK_EQ(test.fSun, test.fTrue);
    CHECK_EQ(test.dSeq, test.dFalse);
    CHECK_EQ(test.fSeq, test.fFalse);
    CHECK_EQ(test.dSueq, test.dTrue);
    CHECK_EQ(test.fSueq, test.fTrue);
    CHECK_EQ(test.dSlt, test.dFalse);
    CHECK_EQ(test.fSlt, test.fFalse);
    CHECK_EQ(test.dSult, test.dTrue);
    CHECK_EQ(test.fSult, test.fTrue);
    CHECK_EQ(test.dSle, test.dFalse);
    CHECK_EQ(test.fSle, test.fFalse);
    CHECK_EQ(test.dSule, test.dTrue);
    CHECK_EQ(test.fSule, test.fTrue);
    CHECK_EQ(test.dSne, test.dFalse);
    CHECK_EQ(test.fSne, test.fFalse);
    CHECK_EQ(test.dSor, test.dFalse);
    CHECK_EQ(test.fSor, test.fFalse);
    CHECK_EQ(test.dSune, test.dTrue);
    CHECK_EQ(test.fSune, test.fTrue);*/
}

TEST(FCVT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float fcvt_d_s_in;
    double fcvt_s_d_in;
    double fcvt_d_s_out;
    float fcvt_s_d_out;
    int fcsr;
  };
  TestFloat test;
  __ xor_(a4, a4, a4);
  __ xor_(a5, a5, a5);
  __ Ld_w(a4, MemOperand(a0, offsetof(TestFloat, fcsr)));
  __ movfcsr2gr(a5);
  __ movgr2fcsr(a4);
  __ Fld_s(f8, MemOperand(a0, offsetof(TestFloat, fcvt_d_s_in)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, fcvt_s_d_in)));
  __ fcvt_d_s(f10, f8);
  __ fcvt_s_d(f11, f9);
  __ Fst_d(f10, MemOperand(a0, offsetof(TestFloat, fcvt_d_s_out)));
  __ Fst_s(f11, MemOperand(a0, offsetof(TestFloat, fcvt_s_d_out)));
  __ movgr2fcsr(a5);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.fcsr = kRoundToNearest;

  test.fcvt_d_s_in = -0.51;
  test.fcvt_s_d_in = -0.51;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.fcvt_d_s_out, static_cast<double>(test.fcvt_d_s_in));
  CHECK_EQ(test.fcvt_s_d_out, static_cast<float>(test.fcvt_s_d_in));

  test.fcvt_d_s_in = 0.49;
  test.fcvt_s_d_in = 0.49;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.fcvt_d_s_out, static_cast<double>(test.fcvt_d_s_in));
  CHECK_EQ(test.fcvt_s_d_out, static_cast<float>(test.fcvt_s_d_in));

  test.fcvt_d_s_in = std::numeric_limits<float>::max();
  test.fcvt_s_d_in = std::numeric_limits<double>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.fcvt_d_s_out, static_cast<double>(test.fcvt_d_s_in));
  CHECK_EQ(test.fcvt_s_d_out, static_cast<float>(test.fcvt_s_d_in));

  test.fcvt_d_s_in = -std::numeric_limits<float>::max();
  test.fcvt_s_d_in = -std::numeric_limits<double>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.fcvt_d_s_out, static_cast<double>(test.fcvt_d_s_in));
  CHECK_EQ(test.fcvt_s_d_out, static_cast<float>(test.fcvt_s_d_in));

  test.fcvt_d_s_in = std::numeric_limits<float>::min();
  test.fcvt_s_d_in = std::numeric_limits<double>::min();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.fcvt_d_s_out, static_cast<double>(test.fcvt_d_s_in));
  CHECK_EQ(test.fcvt_s_d_out, static_cast<float>(test.fcvt_s_d_in));
}

TEST(FFINT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    int32_t ffint_s_w_in;
    int64_t ffint_s_l_in;
    int32_t ffint_d_w_in;
    int64_t ffint_d_l_in;
    float ffint_s_w_out;
    float ffint_s_l_out;
    double ffint_d_w_out;
    double ffint_d_l_out;
    int fcsr;
  };
  TestFloat test;
  __ xor_(a4, a4, a4);
  __ xor_(a5, a5, a5);
  __ Ld_w(a4, MemOperand(a0, offsetof(TestFloat, fcsr)));
  __ movfcsr2gr(a5);
  __ movgr2fcsr(a4);
  __ Fld_s(f8, MemOperand(a0, offsetof(TestFloat, ffint_s_w_in)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, ffint_s_l_in)));
  __ Fld_s(f10, MemOperand(a0, offsetof(TestFloat, ffint_d_w_in)));
  __ Fld_d(f11, MemOperand(a0, offsetof(TestFloat, ffint_d_l_in)));
  __ ffint_s_w(f12, f8);
  __ ffint_s_l(f13, f9);
  __ ffint_d_w(f14, f10);
  __ ffint_d_l(f15, f11);
  __ Fst_s(f12, MemOperand(a0, offsetof(TestFloat, ffint_s_w_out)));
  __ Fst_s(f13, MemOperand(a0, offsetof(TestFloat, ffint_s_l_out)));
  __ Fst_d(f14, MemOperand(a0, offsetof(TestFloat, ffint_d_w_out)));
  __ Fst_d(f15, MemOperand(a0, offsetof(TestFloat, ffint_d_l_out)));
  __ movgr2fcsr(a5);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.fcsr = kRoundToNearest;

  test.ffint_s_w_in = -1;
  test.ffint_s_l_in = -1;
  test.ffint_d_w_in = -1;
  test.ffint_d_l_in = -1;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.ffint_s_w_out, static_cast<float>(test.ffint_s_w_in));
  CHECK_EQ(test.ffint_s_l_out, static_cast<float>(test.ffint_s_l_in));
  CHECK_EQ(test.ffint_d_w_out, static_cast<double>(test.ffint_d_w_in));
  CHECK_EQ(test.ffint_d_l_out, static_cast<double>(test.ffint_d_l_in));

  test.ffint_s_w_in = 1;
  test.ffint_s_l_in = 1;
  test.ffint_d_w_in = 1;
  test.ffint_d_l_in = 1;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.ffint_s_w_out, static_cast<float>(test.ffint_s_w_in));
  CHECK_EQ(test.ffint_s_l_out, static_cast<float>(test.ffint_s_l_in));
  CHECK_EQ(test.ffint_d_w_out, static_cast<double>(test.ffint_d_w_in));
  CHECK_EQ(test.ffint_d_l_out, static_cast<double>(test.ffint_d_l_in));

  test.ffint_s_w_in = std::numeric_limits<int32_t>::max();
  test.ffint_s_l_in = std::numeric_limits<int64_t>::max();
  test.ffint_d_w_in = std::numeric_limits<int32_t>::max();
  test.ffint_d_l_in = std::numeric_limits<int64_t>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.ffint_s_w_out, static_cast<float>(test.ffint_s_w_in));
  CHECK_EQ(test.ffint_s_l_out, static_cast<float>(test.ffint_s_l_in));
  CHECK_EQ(test.ffint_d_w_out, static_cast<double>(test.ffint_d_w_in));
  CHECK_EQ(test.ffint_d_l_out, static_cast<double>(test.ffint_d_l_in));

  test.ffint_s_w_in = std::numeric_limits<int32_t>::min();
  test.ffint_s_l_in = std::numeric_limits<int64_t>::min();
  test.ffint_d_w_in = std::numeric_limits<int32_t>::min();
  test.ffint_d_l_in = std::numeric_limits<int64_t>::min();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.ffint_s_w_out, static_cast<float>(test.ffint_s_w_in));
  CHECK_EQ(test.ffint_s_l_out, static_cast<float>(test.ffint_s_l_in));
  CHECK_EQ(test.ffint_d_w_out, static_cast<double>(test.ffint_d_w_in));
  CHECK_EQ(test.ffint_d_l_out, static_cast<double>(test.ffint_d_l_in));
}

TEST(FTINT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    int32_t c;
    int32_t d;
    int64_t e;
    int64_t f;
    int fcsr;
  };
  Test test;

  const int kTableLength = 9;
  // clang-format off
  double inputs_d[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  float inputs_s[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  double outputs_RN_W[kTableLength] = {
      3.0, 4.0, 4.0, -3.0, -4.0, -4.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_RN_L[kTableLength] = {
      3.0, 4.0, 4.0, -3.0, -4.0, -4.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  double outputs_RZ_W[kTableLength] = {
      3.0, 3.0, 3.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_RZ_L[kTableLength] = {
      3.0, 3.0, 3.0, -3.0, -3.0, -3.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  double outputs_RP_W[kTableLength] = {
      4.0, 4.0, 4.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_RP_L[kTableLength] = {
      4.0, 4.0, 4.0, -3.0, -3.0, -3.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  double outputs_RM_W[kTableLength] = {
      3.0, 3.0, 3.0, -4.0, -4.0, -4.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_RM_L[kTableLength] = {
      3.0, 3.0, 3.0, -4.0, -4.0, -4.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  // clang-format on

  int fcsr_inputs[4] = {kRoundToNearest, kRoundToZero, kRoundToPlusInf,
                        kRoundToMinusInf};
  double* outputs[8] = {
      outputs_RN_W, outputs_RN_L, outputs_RZ_W, outputs_RZ_L,
      outputs_RP_W, outputs_RP_L, outputs_RM_W, outputs_RM_L,
  };

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ xor_(a5, a5, a5);
  __ Ld_w(a5, MemOperand(a0, offsetof(Test, fcsr)));
  __ movfcsr2gr(a4);
  __ movgr2fcsr(a5);
  __ ftint_w_d(f10, f8);
  __ ftint_w_s(f11, f9);
  __ ftint_l_d(f12, f8);
  __ ftint_l_s(f13, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(Test, f)));
  __ movgr2fcsr(a4);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int j = 0; j < 4; j++) {
    test.fcsr = fcsr_inputs[j];
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_d[i];
      test.b = inputs_s[i];
      f.Call(&test, 0, 0, 0, 0);
      CHECK_EQ(test.c, outputs[2 * j][i]);
      CHECK_EQ(test.d, outputs[2 * j][i]);
      CHECK_EQ(test.e, outputs[2 * j + 1][i]);
      CHECK_EQ(test.f, outputs[2 * j + 1][i]);
    }
  }
}

TEST(FTINTRM) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    int32_t c;
    int32_t d;
    int64_t e;
    int64_t f;
  };
  Test test;

  const int kTableLength = 9;

  // clang-format off
  double inputs_d[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  float inputs_s[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  double outputs_w[kTableLength] = {
      3.0, 3.0, 3.0, -4.0, -4.0, -4.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_l[kTableLength] = {
      3.0, 3.0, 3.0, -4.0, -4.0, -4.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ ftintrm_w_d(f10, f8);
  __ ftintrm_w_s(f11, f9);
  __ ftintrm_l_d(f12, f8);
  __ ftintrm_l_s(f13, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(Test, f)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_d[i];
    test.b = inputs_s[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.c, outputs_w[i]);
    CHECK_EQ(test.d, outputs_w[i]);
    CHECK_EQ(test.e, outputs_l[i]);
    CHECK_EQ(test.f, outputs_l[i]);
  }
}

TEST(FTINTRP) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    int32_t c;
    int32_t d;
    int64_t e;
    int64_t f;
  };
  Test test;

  const int kTableLength = 9;

  // clang-format off
  double inputs_d[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  float inputs_s[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  double outputs_w[kTableLength] = {
      4.0, 4.0, 4.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_l[kTableLength] = {
      4.0, 4.0, 4.0, -3.0, -3.0, -3.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ ftintrp_w_d(f10, f8);
  __ ftintrp_w_s(f11, f9);
  __ ftintrp_l_d(f12, f8);
  __ ftintrp_l_s(f13, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(Test, f)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_d[i];
    test.b = inputs_s[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.c, outputs_w[i]);
    CHECK_EQ(test.d, outputs_w[i]);
    CHECK_EQ(test.e, outputs_l[i]);
    CHECK_EQ(test.f, outputs_l[i]);
  }
}

TEST(FTINTRZ) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    int32_t c;
    int32_t d;
    int64_t e;
    int64_t f;
  };
  Test test;

  const int kTableLength = 9;

  // clang-format off
  double inputs_d[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  float inputs_s[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  double outputs_w[kTableLength] = {
      3.0, 3.0, 3.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_l[kTableLength] = {
      3.0, 3.0, 3.0, -3.0, -3.0, -3.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ ftintrz_w_d(f10, f8);
  __ ftintrz_w_s(f11, f9);
  __ ftintrz_l_d(f12, f8);
  __ ftintrz_l_s(f13, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(Test, f)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_d[i];
    test.b = inputs_s[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.c, outputs_w[i]);
    CHECK_EQ(test.d, outputs_w[i]);
    CHECK_EQ(test.e, outputs_l[i]);
    CHECK_EQ(test.f, outputs_l[i]);
  }
}

TEST(FTINTRNE) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    int32_t c;
    int32_t d;
    int64_t e;
    int64_t f;
  };
  Test test;

  const int kTableLength = 9;

  // clang-format off
  double inputs_d[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  float inputs_s[kTableLength] = {
      3.1, 3.6, 3.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
  };
  double outputs_w[kTableLength] = {
      3.0, 4.0, 4.0, -3.0, -4.0, -4.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};
  double outputs_l[kTableLength] = {
      3.0, 4.0, 4.0, -3.0, -4.0, -4.0,
      2147483648.0, 0,
      static_cast<double>(kFPU64InvalidResult)};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ ftintrne_w_d(f10, f8);
  __ ftintrne_w_s(f11, f9);
  __ ftintrne_l_d(f12, f8);
  __ ftintrne_l_s(f13, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(Test, f)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_d[i];
    test.b = inputs_s[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.c, outputs_w[i]);
    CHECK_EQ(test.d, outputs_w[i]);
    CHECK_EQ(test.e, outputs_l[i]);
    CHECK_EQ(test.f, outputs_l[i]);
  }
}

TEST(FRINT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    float b;
    double c;
    float d;
    int fcsr;
  };
  Test test;

  const int kTableLength = 32;

  // clang-format off
  double inputs_d[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E+308, 6.27463370218383111104242366943E-307,
      309485009821345068724781056.89,
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<double>::max() - 0.1,
      std::numeric_limits<double>::infinity()
      };
  float inputs_s[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E+38, 6.27463370218383111104242366943E-37,
      309485009821345068724781056.89,
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<float>::lowest() + 0.6,
      std::numeric_limits<float>::infinity()
      };
  float outputs_RN_S[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<float>::lowest() + 1,
      std::numeric_limits<float>::infinity()
      };
  double outputs_RN_D[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity()
  };
  float outputs_RZ_S[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<float>::lowest() + 1,
      std::numeric_limits<float>::infinity()
  };
  double outputs_RZ_D[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<double>::max() - 1,
      std::numeric_limits<double>::infinity()
  };
  float outputs_RP_S[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 1,
      309485009821345068724781057.0,
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<float>::lowest() + 1,
      std::numeric_limits<float>::infinity()
  };
  double outputs_RP_D[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 1,
      309485009821345068724781057.0,
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity()
  };
  float outputs_RM_S[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<float>::lowest() + 1,
      std::numeric_limits<float>::infinity()
  };
  double outputs_RM_D[kTableLength] = {
      18446744073709551617.0, 4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0,
      std::numeric_limits<double>::max(),
      std::numeric_limits<double>::infinity()
  };
  // clang-format on

  int fcsr_inputs[4] = {kRoundToNearest, kRoundToZero, kRoundToPlusInf,
                        kRoundToMinusInf};
  double* outputs_d[4] = {outputs_RN_D, outputs_RZ_D, outputs_RP_D,
                          outputs_RM_D};
  float* outputs_s[4] = {outputs_RN_S, outputs_RZ_S, outputs_RP_S,
                         outputs_RM_S};

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(Test, b)));
  __ xor_(a5, a5, a5);
  __ Ld_w(a5, MemOperand(a0, offsetof(Test, fcsr)));
  __ movfcsr2gr(a4);
  __ movgr2fcsr(a5);
  __ frint_d(f10, f8);
  __ frint_s(f11, f9);
  __ Fst_d(f10, MemOperand(a0, offsetof(Test, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(Test, d)));
  __ movgr2fcsr(a4);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int j = 0; j < 4; j++) {
    test.fcsr = fcsr_inputs[j];
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_d[i];
      test.b = inputs_s[i];
      f.Call(&test, 0, 0, 0, 0);
      CHECK_EQ(test.c, outputs_d[j][i]);
      CHECK_EQ(test.d, outputs_s[j][i]);
    }
  }
}

TEST(FMOV) {
  const int kTableLength = 7;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    float b;
    double c;
    float d;
  };

  TestFloat test;

  // clang-format off
  double inputs_D[kTableLength] = {
    5.3, -5.3, 0.29, -0.29, 0,
  std::numeric_limits<double>::max(),
  -std::numeric_limits<double>::max()
  };
  float inputs_S[kTableLength] = {
    4.8, -4.8, 0.29, -0.29, 0,
  std::numeric_limits<float>::max(),
  -std::numeric_limits<float>::max()
  };

  double outputs_D[kTableLength] = {
    5.3, -5.3, 0.29, -0.29, 0,
  std::numeric_limits<double>::max(),
  -std::numeric_limits<double>::max()
  };

  float outputs_S[kTableLength] = {
    4.8, -4.8, 0.29, -0.29, 0,
  std::numeric_limits<float>::max(),
  -std::numeric_limits<float>::max()
  };
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ fmov_d(f10, f8);
  __ fmov_s(f11, f9);
  __ Fst_d(f10, MemOperand(a0, offsetof(TestFloat, c)));
  __ Fst_s(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.b = inputs_S[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.c, outputs_D[i]);
    CHECK_EQ(test.d, outputs_S[i]);
  }
}

TEST(LA14) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    double c;
    double d;
    int64_t high;
    int64_t low;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ Fld_d(f8, MemOperand(a0, offsetof(T, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(T, b)));

  __ movfr2gr_s(a4, f8);
  __ movfrh2gr_s(a5, f8);
  __ movfr2gr_d(a6, f9);

  __ movgr2fr_w(f9, a4);
  __ movgr2frh_w(f9, a5);
  __ movgr2fr_d(f8, a6);

  __ Fst_d(f8, MemOperand(a0, offsetof(T, a)));
  __ Fst_d(f9, MemOperand(a0, offsetof(T, c)));

  __ Fld_d(f8, MemOperand(a0, offsetof(T, d)));
  __ movfrh2gr_s(a4, f8);
  __ movfr2gr_s(a5, f8);

  __ St_d(a4, MemOperand(a0, offsetof(T, high)));
  __ St_d(a5, MemOperand(a0, offsetof(T, low)));

  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  t.a = 1.5e22;
  t.b = 2.75e11;
  t.c = 17.17;
  t.d = -2.75e11;
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(2.75e11, t.a);
  CHECK_EQ(2.75e11, t.b);
  CHECK_EQ(1.5e22, t.c);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFC25001D1L), t.high);
  CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFBF800000L), t.low);

  t.a = -1.5e22;
  t.b = -2.75e11;
  t.c = 17.17;
  t.d = 274999868928.0;
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(-2.75e11, t.a);
  CHECK_EQ(-2.75e11, t.b);
  CHECK_EQ(-1.5e22, t.c);
  CHECK_EQ(static_cast<int64_t>(0x425001D1L), t.high);
  CHECK_EQ(static_cast<int64_t>(0x3F800000L), t.low);
}

uint64_t run_bceqz(int fcc_value, int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0);
  __ li(t0, fcc_value);
  __ b(&main_block);
  // Block 1
  for (int32_t i = -104; i <= -55; ++i) {
    __ addi_d(a2, a2, 0x1);
  }
  __ b(&L);

  // Block 2
  for (int32_t i = -53; i <= -4; ++i) {
    __ addi_d(a2, a2, 0x10);
  }
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ movcf2gr(t1, FCC0);
  __ movgr2cf(FCC0, t0);
  __ bceqz(FCC0, offset);
  __ bind(&L);
  __ movgr2cf(FCC0, t1);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  for (int32_t i = 4; i <= 53; ++i) {
    __ addi_d(a2, a2, 0x100);
  }
  __ b(&L);

  // Block 5
  for (int32_t i = 55; i <= 104; ++i) {
    __ addi_d(a2, a2, 0x300);
  }
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(BCEQZ) {
  CcTest::InitializeVM();
  struct TestCaseBceqz {
    int fcc;
    int32_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBceqz tc[] = {
    // fcc, offset, expected_res
    {    0,    -90,         0x24 },
    {    0,    -27,        0x180 },
    {    0,     47,        0x700 },
    {    0,     70,       0x6900 },
    {    1,    -27,            0 },
    {    1,     47,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBceqz);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bceqz(tc[i].fcc, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

uint64_t run_bcnez(int fcc_value, int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block, L;
  __ li(a2, 0);
  __ li(t0, fcc_value);
  __ b(&main_block);
  // Block 1
  for (int32_t i = -104; i <= -55; ++i) {
    __ addi_d(a2, a2, 0x1);
  }
  __ b(&L);

  // Block 2
  for (int32_t i = -53; i <= -4; ++i) {
    __ addi_d(a2, a2, 0x10);
  }
  __ b(&L);

  // Block 3 (Main)
  __ bind(&main_block);
  __ movcf2gr(t1, FCC0);
  __ movgr2cf(FCC0, t0);
  __ bcnez(FCC0, offset);
  __ bind(&L);
  __ movgr2cf(FCC0, t1);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  // Block 4
  for (int32_t i = 4; i <= 53; ++i) {
    __ addi_d(a2, a2, 0x100);
  }
  __ b(&L);

  // Block 5
  for (int32_t i = 55; i <= 104; ++i) {
    __ addi_d(a2, a2, 0x300);
  }
  __ b(&L);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(BCNEZ) {
  CcTest::InitializeVM();
  struct TestCaseBcnez {
    int fcc;
    int32_t offset;
    uint64_t expected_res;
  };

  // clang-format off
  struct TestCaseBcnez tc[] = {
    // fcc, offset, expected_res
    {    1,    -90,         0x24 },
    {    1,    -27,        0x180 },
    {    1,     47,        0x700 },
    {    1,     70,       0x6900 },
    {    0,    -27,            0 },
    {    0,     47,            0 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBcnez);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    uint64_t res = run_bcnez(tc[i].fcc, tc[i].offset);
    CHECK_EQ(tc[i].expected_res, res);
  }
}

TEST(jump_tables1) {
  // Test jump tables with forward jumps.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 512;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  __ addi_d(sp, sp, -8);
  __ St_d(ra, MemOperand(sp, 0));
  __ Align(8);

  Label done;
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);
    __ pcaddi(ra, 2);
    __ slli_d(t7, a0, 3);
    __ add_d(t7, t7, ra);
    __ Ld_d(t7, MemOperand(t7, 4 * kInstrSize));
    __ jirl(zero_reg, t7, 0);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lu12i_w(a2, (values[i] >> 12) & 0xFFFFF);
    __ ori(a2, a2, values[i] & 0xFFF);
    __ b(&done);
    __ nop();
  }

  __ bind(&done);
  __ Ld_d(ra, MemOperand(sp, 0));
  __ addi_d(sp, sp, 8);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CHECK_EQ(0, assm.UnboundLabelsCount());

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ((values[i]), static_cast<int>(res));
  }
}

TEST(jump_tables2) {
  // Test jump tables with backward jumps.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 512;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  __ addi_d(sp, sp, -8);
  __ St_d(ra, MemOperand(sp, 0));

  Label done, dispatch;
  __ b(&dispatch);
  __ nop();

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lu12i_w(a2, (values[i] >> 12) & 0xFFFFF);
    __ ori(a2, a2, values[i] & 0xFFF);
    __ b(&done);
    __ nop();
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);
    __ pcaddi(ra, 2);
    __ slli_d(t7, a0, 3);
    __ add_d(t7, t7, ra);
    __ Ld_d(t7, MemOperand(t7, 4 * kInstrSize));
    __ jirl(zero_reg, t7, 0);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  __ bind(&done);
  __ Ld_d(ra, MemOperand(sp, 0));
  __ addi_d(sp, sp, 8);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    int64_t res = reinterpret_cast<int64_t>(f.Call(i, 0, 0, 0, 0));
    ::printf("f(%d) = %" PRId64 "\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST(jump_tables3) {
  // Test jump tables with backward jumps and embedded heap objects.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 512;
  Handle<Object> values[kNumCases];
  for (int i = 0; i < kNumCases; ++i) {
    double value = isolate->random_number_generator()->NextDouble();
    values[i] = isolate->factory()->NewHeapNumber<AllocationType::kOld>(value);
  }
  Label labels[kNumCases];
  Tagged<Object> obj;
  int64_t imm64;

  __ addi_d(sp, sp, -8);
  __ St_d(ra, MemOperand(sp, 0));

  Label done, dispatch;
  __ b(&dispatch);
  __ nop();

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    obj = *values[i];
    imm64 = obj.ptr();
    __ lu12i_w(a2, (imm64 >> 12) & 0xFFFFF);
    __ ori(a2, a2, imm64 & 0xFFF);
    __ lu32i_d(a2, (imm64 >> 32) & 0xFFFFF);
    __ lu52i_d(a2, a2, (imm64 >> 52) & 0xFFF);
    __ b(&done);
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);
    __ pcaddi(ra, 2);
    __ slli_d(t7, a0, 3);  // In delay slot.
    __ add_d(t7, t7, ra);
    __ Ld_d(t7, MemOperand(t7, 4 * kInstrSize));
    __ jirl(zero_reg, t7, 0);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }
  __ bind(&done);
  __ Ld_d(ra, MemOperand(sp, 0));
  __ addi_d(sp, sp, 8);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    Handle<Object> result(
        Tagged<Object>(reinterpret_cast<Address>(f.Call(i, 0, 0, 0, 0))),
        isolate);
#ifdef OBJECT_PRINT
    ::printf("f(%d) = ", i);
    Print(*result);
    ::printf("\n");
#endif
    CHECK(values[i].is_identical_to(result));
  }
}

uint64_t run_li_macro(int64_t imm, LiFlags mode, int32_t num_instr = 0) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label code_start;
  __ bind(&code_start);
  __ li(a2, imm, mode);
  if (num_instr > 0) {
    CHECK_EQ(assm.InstructionsGeneratedSince(&code_start), num_instr);
    CHECK_EQ(__ InstrCountForLi64Bit(imm), num_instr);
  }
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(li_macro) {
  CcTest::InitializeVM();

  // Test li macro-instruction for border cases.

  struct TestCase_li {
    uint64_t imm;
    int32_t num_instr;
  };
  // clang-format off
  struct TestCase_li tc[] = {
      //              imm, num_instr
      {0xFFFFFFFFFFFFF800,         1},  // min_int12
      // The test case above generates addi_d instruction.
      // This is int12 value and we can load it using just addi_d.
      {             0x800,         1},  // max_int12 + 1
      // Generates ori
      // max_int12 + 1 is not int12 but is uint12, just use ori.
      {0xFFFFFFFFFFFFF7FF,         2},  // min_int12 - 1
      // Generates lu12i + ori
      // We load int32 value using lu12i_w + ori.
      {             0x801,         1},  // max_int12 + 2
      // Generates ori
      // Also an uint1 value, use ori.
      {        0x00001000,         1},  // max_uint12 + 1
      // Generates lu12i_w
      // Low 12 bits are 0, load value using lu12i_w.
      {        0x00001001,         2},  // max_uint12 + 2
      // Generates lu12i_w + ori
      // We have to generate two instructions in this case.
      {0x00000000FFFFFFFF,         2},  // max_uint32
      // addi_w + lu32i_d
      {0x00000000FFFFFFFE,         2},  // max_uint32 - 1
      // addi_w + lu32i_d
      {0xFFFFFFFF80000000,         1},  // min_int32
      // lu12i_w
      {0x0000000080000000,         2},  // max_int32 + 1
      // lu12i_w + lu32i_d
      {0xFFFF0000FFFF8765,         3},
      // lu12i_w + ori + lu32i_d
      {0x1234ABCD87654321,         4},
      // lu12i_w + ori + lu32i_d + lu52i_d
      {0xFFFF789100000000,         2},
      // xor + lu32i_d
      {0xF12F789100000000,         3},
      // xor + lu32i_d + lu52i_d
      {0xF120000000000800,         2},
      // ori + lu52i_d
      {0xFFF0000000000000,         1},
      // lu52i_d
      {0xF100000000000000,         1},
      {0x0122000000000000,         2},
      {0x1234FFFF77654321,         4},
      {0x1230000077654321,         3},
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCase_li);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].imm,
             run_li_macro(tc[i].imm, OPTIMIZE_SIZE, tc[i].num_instr));
    CHECK_EQ(tc[i].imm, run_li_macro(tc[i].imm, CONSTANT_SIZE));
    if (is_int48(tc[i].imm)) {
      CHECK_EQ(tc[i].imm, run_li_macro(tc[i].imm, ADDRESS_LOAD));
    }
  }
}

TEST(FMIN_FMAX) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    double b;
    float c;
    float d;
    double e;
    double f;
    float g;
    float h;
  };

  TestFloat test;
  const double dnan = std::numeric_limits<double>::quiet_NaN();
  const double dinf = std::numeric_limits<double>::infinity();
  const double dminf = -std::numeric_limits<double>::infinity();
  const float fnan = std::numeric_limits<float>::quiet_NaN();
  const float finf = std::numeric_limits<float>::infinity();
  const float fminf = -std::numeric_limits<float>::infinity();
  const int kTableLength = 13;

  // clang-format off
  double inputsa[kTableLength] = {2.0,  3.0,  dnan, 3.0,   -0.0, 0.0, dinf,
                                  dnan, 42.0, dinf, dminf, dinf, dnan};
  double inputsb[kTableLength] = {3.0,  2.0,  3.0,  dnan, 0.0,   -0.0, dnan,
                                  dinf, dinf, 42.0, dinf, dminf, dnan};
  double outputsdmin[kTableLength] = {2.0,   2.0,   3.0,  3.0,  -0.0,
                                      -0.0,  dinf,  dinf, 42.0, 42.0,
                                      dminf, dminf, dnan};
  double outputsdmax[kTableLength] = {3.0,  3.0,  3.0,  3.0,  0.0,  0.0, dinf,
                                      dinf, dinf, dinf, dinf, dinf, dnan};

  float inputsc[kTableLength] = {2.0,  3.0,  fnan, 3.0,   -0.0, 0.0, finf,
                                 fnan, 42.0, finf, fminf, finf, fnan};
  float inputsd[kTableLength] = {3.0,  2.0,  3.0,  fnan, 0.0,   -0.0, fnan,
                                 finf, finf, 42.0, finf, fminf, fnan};
  float outputsfmin[kTableLength] = {2.0,   2.0,   3.0,  3.0,  -0.0,
                                     -0.0,  finf,  finf, 42.0, 42.0,
                                     fminf, fminf, fnan};
  float outputsfmax[kTableLength] = {3.0,  3.0,  3.0,  3.0,  0.0,  0.0, finf,
                                     finf, finf, finf, finf, finf, fnan};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ Fld_s(f10, MemOperand(a0, offsetof(TestFloat, c)));
  __ Fld_s(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ fmin_d(f12, f8, f9);
  __ fmax_d(f13, f8, f9);
  __ fmin_s(f14, f10, f11);
  __ fmax_s(f15, f10, f11);
  __ Fst_d(f12, MemOperand(a0, offsetof(TestFloat, e)));
  __ Fst_d(f13, MemOperand(a0, offsetof(TestFloat, f)));
  __ Fst_s(f14, MemOperand(a0, offsetof(TestFloat, g)));
  __ Fst_s(f15, MemOperand(a0, offsetof(TestFloat, h)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 4; i < kTableLength; i++) {
    test.a = inputsa[i];
    test.b = inputsb[i];
    test.c = inputsc[i];
    test.d = inputsd[i];

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(0, memcmp(&test.e, &outputsdmin[i], sizeof(test.e)));
    CHECK_EQ(0, memcmp(&test.f, &outputsdmax[i], sizeof(test.f)));
    CHECK_EQ(0, memcmp(&test.g, &outputsfmin[i], sizeof(test.g)));
    CHECK_EQ(0, memcmp(&test.h, &outputsfmax[i], sizeof(test.h)));
  }
}

TEST(FMINA_FMAXA) {
  const int kTableLength = 23;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  const double dnan = std::numeric_limits<double>::quiet_NaN();
  const double dinf = std::numeric_limits<double>::infinity();
  const double dminf = -std::numeric_limits<double>::infinity();
  const float fnan = std::numeric_limits<float>::quiet_NaN();
  const float finf = std::numeric_limits<float>::infinity();
  const float fminf = std::numeric_limits<float>::infinity();

  struct TestFloat {
    double a;
    double b;
    double resd1;
    double resd2;
    float c;
    float d;
    float resf1;
    float resf2;
  };

  TestFloat test;
  // clang-format off
  double inputsa[kTableLength] = {
        5.3,  4.8, 6.1,  9.8, 9.8,  9.8,  -10.0, -8.9, -9.8,  -10.0, -8.9, -9.8,
    dnan, 3.0, -0.0, 0.0, dinf, dnan, 42.0,  dinf, dminf, dinf,  dnan};
  double inputsb[kTableLength] = {
        4.8, 5.3,  6.1, -10.0, -8.9, -9.8, 9.8,  9.8,  9.8,  -9.8,  -11.2, -9.8,
    3.0, dnan, 0.0, -0.0,  dnan, dinf, dinf, 42.0, dinf, dminf, dnan};
  double resd1[kTableLength] = {
        4.8, 4.8, 6.1,  9.8,  -8.9, -9.8, 9.8,  -8.9, -9.8,  -9.8,  -8.9, -9.8,
    3.0, 3.0, -0.0, -0.0, dinf, dinf, 42.0, 42.0, dminf, dminf, dnan};
  double resd2[kTableLength] = {
        5.3, 5.3, 6.1, -10.0, 9.8,  9.8,  -10.0, 9.8,  9.8,  -10.0, -11.2, -9.8,
    3.0, 3.0, 0.0, 0.0,   dinf, dinf, dinf,  dinf, dinf, dinf,  dnan};
  float inputsc[kTableLength] = {
        5.3,  4.8, 6.1,  9.8, 9.8,  9.8,  -10.0, -8.9, -9.8,  -10.0, -8.9, -9.8,
    fnan, 3.0, -0.0, 0.0, finf, fnan, 42.0,  finf, fminf, finf,  fnan};
  float inputsd[kTableLength] = {
        4.8,  5.3,  6.1, -10.0, -8.9,  -9.8, 9.8, 9.8, 9.8,  -9.8, -11.2, -9.8,
    3.0,  fnan, -0.0, 0.0, fnan, finf, finf, 42.0, finf, fminf, fnan};
  float resf1[kTableLength] = {
        4.8, 4.8, 6.1,  9.8,  -8.9, -9.8, 9.8,  -8.9, -9.8,  -9.8,  -8.9, -9.8,
    3.0, 3.0, -0.0, -0.0, finf, finf, 42.0, 42.0, fminf, fminf, fnan};
  float resf2[kTableLength] = {
        5.3, 5.3, 6.1, -10.0, 9.8,  9.8,  -10.0, 9.8,  9.8,  -10.0, -11.2, -9.8,
    3.0, 3.0, 0.0, 0.0,   finf, finf, finf,  finf, finf, finf,  fnan};
  // clang-format on

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ Fld_s(f10, MemOperand(a0, offsetof(TestFloat, c)));
  __ Fld_s(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ fmina_d(f12, f8, f9);
  __ fmaxa_d(f13, f8, f9);
  __ fmina_s(f14, f10, f11);
  __ fmaxa_s(f15, f10, f11);
  __ Fst_d(f12, MemOperand(a0, offsetof(TestFloat, resd1)));
  __ Fst_d(f13, MemOperand(a0, offsetof(TestFloat, resd2)));
  __ Fst_s(f14, MemOperand(a0, offsetof(TestFloat, resf1)));
  __ Fst_s(f15, MemOperand(a0, offsetof(TestFloat, resf2)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputsa[i];
    test.b = inputsb[i];
    test.c = inputsc[i];
    test.d = inputsd[i];
    f.Call(&test, 0, 0, 0, 0);
    if (i < kTableLength - 1) {
      CHECK_EQ(test.resd1, resd1[i]);
      CHECK_EQ(test.resd2, resd2[i]);
      CHECK_EQ(test.resf1, resf1[i]);
      CHECK_EQ(test.resf2, resf2[i]);
    } else {
      CHECK(std::isnan(test.resd1));
      CHECK(std::isnan(test.resd2));
      CHECK(std::isnan(test.resf1));
      CHECK(std::isnan(test.resf2));
    }
  }
}

TEST(FADD) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    double b;
    double c;
    float d;
    float e;
    float f;
  };

  TestFloat test;

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ fadd_d(f10, f8, f9);
  __ Fst_d(f10, MemOperand(a0, offsetof(TestFloat, c)));

  __ Fld_s(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ Fld_s(f12, MemOperand(a0, offsetof(TestFloat, e)));
  __ fadd_s(f13, f11, f12);
  __ Fst_s(f13, MemOperand(a0, offsetof(TestFloat, f)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.a = 2.0;
  test.b = 3.0;
  test.d = 2.0;
  test.e = 3.0;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.c, 5.0);
  CHECK_EQ(test.f, 5.0);

  test.a = std::numeric_limits<double>::max();
  test.b = -std::numeric_limits<double>::max();  // lowest()
  test.d = std::numeric_limits<float>::max();
  test.e = -std::numeric_limits<float>::max();  // lowest()
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.c, 0.0);
  CHECK_EQ(test.f, 0.0);

  test.a = std::numeric_limits<double>::max();
  test.b = std::numeric_limits<double>::max();
  test.d = std::numeric_limits<float>::max();
  test.e = std::numeric_limits<float>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(!std::isfinite(test.c));
  CHECK(!std::isfinite(test.f));

  test.a = 5.0;
  test.b = std::numeric_limits<double>::signaling_NaN();
  test.d = 5.0;
  test.e = std::numeric_limits<float>::signaling_NaN();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.c));
  CHECK(std::isnan(test.f));
}

TEST(FSUB) {
  const int kTableLength = 12;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float a;
    float b;
    float resultS;
    double c;
    double d;
    double resultD;
  };

  TestFloat test;

  // clang-format off
  double inputfs_D[kTableLength] = {
    5.3, 4.8, 2.9, -5.3, -4.8, -2.9,
    5.3, 4.8, 2.9, -5.3, -4.8, -2.9
  };
  double inputft_D[kTableLength] = {
    4.8, 5.3, 2.9, 4.8, 5.3, 2.9,
    -4.8, -5.3, -2.9, -4.8, -5.3, -2.9
  };
  double outputs_D[kTableLength] = {
    0.5, -0.5, 0.0, -10.1, -10.1, -5.8,
    10.1, 10.1, 5.8, -0.5, 0.5, 0.0
  };
  float inputfs_S[kTableLength] = {
    5.3, 4.8, 2.9, -5.3, -4.8, -2.9,
    5.3, 4.8, 2.9, -5.3, -4.8, -2.9
  };
  float inputft_S[kTableLength] = {
    4.8, 5.3, 2.9, 4.8, 5.3, 2.9,
    -4.8, -5.3, -2.9, -4.8, -5.3, -2.9
  };
  float outputs_S[kTableLength] = {
    0.5, -0.5, 0.0, -10.1, -10.1, -5.8,
    10.1, 10.1, 5.8, -0.5, 0.5, 0.0
  };
  // clang-format on

  __ Fld_s(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ Fld_d(f10, MemOperand(a0, offsetof(TestFloat, c)));
  __ Fld_d(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ fsub_s(f12, f8, f9);
  __ fsub_d(f13, f10, f11);
  __ Fst_s(f12, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Fst_d(f13, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputfs_S[i];
    test.b = inputft_S[i];
    test.c = inputfs_D[i];
    test.d = inputft_D[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.resultS, outputs_S[i]);
    CHECK_EQ(test.resultD, outputs_D[i]);
  }
}

TEST(FMUL) {
  const int kTableLength = 4;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float a;
    float b;
    float resultS;
    double c;
    double d;
    double resultD;
  };

  TestFloat test;
  // clang-format off
  double inputfs_D[kTableLength] = {
    5.3, -5.3, 5.3, -2.9
  };
  double inputft_D[kTableLength] = {
    4.8, 4.8, -4.8, -0.29
  };

  float inputfs_S[kTableLength] = {
    5.3, -5.3, 5.3, -2.9
  };
  float inputft_S[kTableLength] = {
    4.8, 4.8, -4.8, -0.29
  };
  // clang-format on
  __ Fld_s(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ Fld_d(f10, MemOperand(a0, offsetof(TestFloat, c)));
  __ Fld_d(f11, MemOperand(a0, offsetof(TestFloat, d)));
  __ fmul_s(f12, f8, f9);
  __ fmul_d(f13, f10, f11);
  __ Fst_s(f12, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Fst_d(f13, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputfs_S[i];
    test.b = inputft_S[i];
    test.c = inputfs_D[i];
    test.d = inputft_D[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.resultS, inputfs_S[i] * inputft_S[i]);
    CHECK_EQ(test.resultD, inputfs_D[i] * inputft_D[i]);
  }
}

TEST(FDIV) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double dOp1;
    double dOp2;
    double dRes;
    float fOp1;
    float fOp2;
    float fRes;
  };

  Test test;

  __ movfcsr2gr(a4);
  __ movgr2fcsr(zero_reg);

  __ Fld_d(f8, MemOperand(a0, offsetof(Test, dOp1)));
  __ Fld_d(f9, MemOperand(a0, offsetof(Test, dOp2)));
  __ Fld_s(f10, MemOperand(a0, offsetof(Test, fOp1)));
  __ Fld_s(f11, MemOperand(a0, offsetof(Test, fOp2)));
  __ fdiv_d(f12, f8, f9);
  __ fdiv_s(f13, f10, f11);
  __ Fst_d(f12, MemOperand(a0, offsetof(Test, dRes)));
  __ Fst_s(f13, MemOperand(a0, offsetof(Test, fRes)));

  __ movgr2fcsr(a4);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  f.Call(&test, 0, 0, 0, 0);
  const int test_size = 3;
  // clang-format off
  double dOp1[test_size] = {
    5.0,  DBL_MAX,  DBL_MAX};

  double dOp2[test_size] = {
    2.0,  2.0,  -DBL_MAX};

  double dRes[test_size] = {
    2.5,  DBL_MAX / 2.0,  -1.0};

  float fOp1[test_size] = {
    5.0,  FLT_MAX,  FLT_MAX};

  float fOp2[test_size] = {
    2.0,  2.0,  -FLT_MAX};

  float fRes[test_size] = {
    2.5,  FLT_MAX / 2.0,  -1.0};
  // clang-format on

  for (int i = 0; i < test_size; i++) {
    test.dOp1 = dOp1[i];
    test.dOp2 = dOp2[i];
    test.fOp1 = fOp1[i];
    test.fOp2 = fOp2[i];

    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.dRes, dRes[i]);
    CHECK_EQ(test.fRes, fRes[i]);
  }

  test.dOp1 = DBL_MAX;
  test.dOp2 = -0.0;
  test.fOp1 = FLT_MAX;
  test.fOp2 = -0.0;

  f.Call(&test, 0, 0, 0, 0);
  CHECK(!std::isfinite(test.dRes));
  CHECK(!std::isfinite(test.fRes));

  test.dOp1 = 0.0;
  test.dOp2 = -0.0;
  test.fOp1 = 0.0;
  test.fOp2 = -0.0;

  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.dRes));
  CHECK(std::isnan(test.fRes));

  test.dOp1 = std::numeric_limits<double>::quiet_NaN();
  test.dOp2 = -5.0;
  test.fOp1 = std::numeric_limits<float>::quiet_NaN();
  test.fOp2 = -5.0;

  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.dRes));
  CHECK(std::isnan(test.fRes));
}

TEST(FABS) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    float b;
  };

  TestFloat test;

  __ movfcsr2gr(a4);
  __ movgr2fcsr(zero_reg);

  __ Fld_d(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_s(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ fabs_d(f10, f8);
  __ fabs_s(f11, f9);
  __ Fst_d(f10, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fst_s(f11, MemOperand(a0, offsetof(TestFloat, b)));

  __ movgr2fcsr(a4);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.a = -2.0;
  test.b = -2.0;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, 2.0);
  CHECK_EQ(test.b, 2.0);

  test.a = 2.0;
  test.b = 2.0;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, 2.0);
  CHECK_EQ(test.b, 2.0);

  // Testing biggest positive number
  test.a = std::numeric_limits<double>::max();
  test.b = std::numeric_limits<float>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, std::numeric_limits<double>::max());
  CHECK_EQ(test.b, std::numeric_limits<float>::max());

  // Testing smallest negative number
  test.a = -std::numeric_limits<double>::max();  // lowest()
  test.b = -std::numeric_limits<float>::max();   // lowest()
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, std::numeric_limits<double>::max());
  CHECK_EQ(test.b, std::numeric_limits<float>::max());

  // Testing smallest positive number
  test.a = -std::numeric_limits<double>::min();
  test.b = -std::numeric_limits<float>::min();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, std::numeric_limits<double>::min());
  CHECK_EQ(test.b, std::numeric_limits<float>::min());

  // Testing infinity
  test.a =
      -std::numeric_limits<double>::max() / std::numeric_limits<double>::min();
  test.b =
      -std::numeric_limits<float>::max() / std::numeric_limits<float>::min();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, std::numeric_limits<double>::max() /
                       std::numeric_limits<double>::min());
  CHECK_EQ(test.b, std::numeric_limits<float>::max() /
                       std::numeric_limits<float>::min());

  test.a = std::numeric_limits<double>::quiet_NaN();
  test.b = std::numeric_limits<float>::quiet_NaN();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.a));
  CHECK(std::isnan(test.b));

  test.a = std::numeric_limits<double>::signaling_NaN();
  test.b = std::numeric_limits<float>::signaling_NaN();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.a));
  CHECK(std::isnan(test.b));
}

template <class T>
struct TestCaseMaddMsub {
  T fj, fk, fa, fd_fmadd, fd_fmsub, fd_fnmadd, fd_fnmsub;
};

template <typename T, typename F>
void helper_fmadd_fmsub_fnmadd_fnmsub(F func) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  T x = std::sqrt(static_cast<T>(2.0));
  T y = std::sqrt(static_cast<T>(3.0));
  T z = std::sqrt(static_cast<T>(5.0));
  T x2 = 11.11, y2 = 22.22, z2 = 33.33;
  // clang-format off
  TestCaseMaddMsub<T> test_cases[] = {
      {x, y, z, 0.0, 0.0, 0.0, 0.0},
      {x, y, -z, 0.0, 0.0, 0.0, 0.0},
      {x, -y, z, 0.0, 0.0, 0.0, 0.0},
      {x, -y, -z, 0.0, 0.0, 0.0, 0.0},
      {-x, y, z, 0.0, 0.0, 0.0, 0.0},
      {-x, y, -z, 0.0, 0.0, 0.0, 0.0},
      {-x, -y, z, 0.0, 0.0, 0.0, 0.0},
      {-x, -y, -z, 0.0, 0.0, 0.0, 0.0},
      {-3.14, 0.2345, -123.000056, 0.0, 0.0, 0.0, 0.0},
      {7.3, -23.257, -357.1357, 0.0, 0.0, 0.0, 0.0},
      {x2, y2, z2, 0.0, 0.0, 0.0, 0.0},
      {x2, y2, -z2, 0.0, 0.0, 0.0, 0.0},
      {x2, -y2, z2, 0.0, 0.0, 0.0, 0.0},
      {x2, -y2, -z2, 0.0, 0.0, 0.0, 0.0},
      {-x2, y2, z2, 0.0, 0.0, 0.0, 0.0},
      {-x2, y2, -z2, 0.0, 0.0, 0.0, 0.0},
      {-x2, -y2, z2, 0.0, 0.0, 0.0, 0.0},
      {-x2, -y2, -z2, 0.0, 0.0, 0.0, 0.0},
  };
  // clang-format on
  if (std::is_same<T, float>::value) {
    __ Fld_s(f8, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fj)));
    __ Fld_s(f9, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fk)));
    __ Fld_s(f10, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fa)));
  } else if (std::is_same<T, double>::value) {
    __ Fld_d(f8, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fj)));
    __ Fld_d(f9, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fk)));
    __ Fld_d(f10, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fa)));
  } else {
    UNREACHABLE();
  }

  func(assm);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  const size_t kTableLength = sizeof(test_cases) / sizeof(TestCaseMaddMsub<T>);
  TestCaseMaddMsub<T> tc;
  for (size_t i = 0; i < kTableLength; i++) {
    tc.fj = test_cases[i].fj;
    tc.fk = test_cases[i].fk;
    tc.fa = test_cases[i].fa;

    f.Call(&tc, 0, 0, 0, 0);

    T res_fmadd;
    T res_fmsub;
    T res_fnmadd;
    T res_fnmsub;
    res_fmadd = std::fma(tc.fj, tc.fk, tc.fa);
    res_fmsub = std::fma(tc.fj, tc.fk, -tc.fa);
    res_fnmadd = -std::fma(tc.fj, tc.fk, tc.fa);
    res_fnmsub = -std::fma(tc.fj, tc.fk, -tc.fa);

    CHECK_EQ(tc.fd_fmadd, res_fmadd);
    CHECK_EQ(tc.fd_fmsub, res_fmsub);
    CHECK_EQ(tc.fd_fnmadd, res_fnmadd);
    CHECK_EQ(tc.fd_fnmsub, res_fnmsub);
  }
}

TEST(FMADD_FMSUB_FNMADD_FNMSUB_S) {
  helper_fmadd_fmsub_fnmadd_fnmsub<float>([](MacroAssembler& assm) {
    __ fmadd_s(f11, f8, f9, f10);
    __ Fst_s(f11, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_fmadd)));
    __ fmsub_s(f12, f8, f9, f10);
    __ Fst_s(f12, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_fmsub)));
    __ fnmadd_s(f13, f8, f9, f10);
    __ Fst_s(f13, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_fnmadd)));
    __ fnmsub_s(f14, f8, f9, f10);
    __ Fst_s(f14, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_fnmsub)));
  });
}

TEST(FMADD_FMSUB_FNMADD_FNMSUB_D) {
  helper_fmadd_fmsub_fnmadd_fnmsub<double>([](MacroAssembler& assm) {
    __ fmadd_d(f11, f8, f9, f10);
    __ Fst_d(f11, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_fmadd)));
    __ fmsub_d(f12, f8, f9, f10);
    __ Fst_d(f12, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_fmsub)));
    __ fnmadd_d(f13, f8, f9, f10);
    __ Fst_d(f13,
             MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_fnmadd)));
    __ fnmsub_d(f14, f8, f9, f10);
    __ Fst_d(f14,
             MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_fnmsub)));
  });
}

/*
TEST(FSQRT_FRSQRT_FRECIP) {
  const int kTableLength = 4;
  const double deltaDouble = 2E-15;
  const float deltaFloat = 2E-7;
  const float sqrt2_s = sqrt(2);
  const double sqrt2_d = sqrt(2);
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float a;
    float resultS1;
    float resultS2;
    float resultS3;
    double b;
    double resultD1;
    double resultD2;
    double resultD3;
  };
  TestFloat test;
  // clang-format off
  double inputs_D[kTableLength] = {
    0.0L, 4.0L, 2.0L, 4e-28L
  };

  double outputs_D[kTableLength] = {
    0.0L, 2.0L, sqrt2_d, 2e-14L
  };
  float inputs_S[kTableLength] = {
    0.0, 4.0, 2.0, 4e-28
  };

  float outputs_S[kTableLength] = {
    0.0, 2.0, sqrt2_s, 2e-14
  };
  // clang-format on
  __ Fld_s(f8, MemOperand(a0, offsetof(TestFloat, a)));
  __ Fld_d(f9, MemOperand(a0, offsetof(TestFloat, b)));
  __ fsqrt_s(f10, f8);
  __ fsqrt_d(f11, f9);
  __ frsqrt_s(f12, f8);
  __ frsqrt_d(f13, f9);
  __ frecip_s(f14, f8);
  __ frecip_d(f15, f9);
  __ Fst_s(f10, MemOperand(a0, offsetof(TestFloat, resultS1)));
  __ Fst_d(f11, MemOperand(a0, offsetof(TestFloat, resultD1)));
  __ Fst_s(f12, MemOperand(a0, offsetof(TestFloat, resultS2)));
  __ Fst_d(f13, MemOperand(a0, offsetof(TestFloat, resultD2)));
  __ Fst_s(f14, MemOperand(a0, offsetof(TestFloat, resultS3)));
  __ Fst_d(f15, MemOperand(a0, offsetof(TestFloat, resultD3)));
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
                 Factory::CodeBuilder(isolate, desc, CodeKind::STUB).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  for (int i = 0; i < kTableLength; i++) {
    float f1;
    double d1;
    test.a = inputs_S[i];
    test.b = inputs_D[i];

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(test.resultS1, outputs_S[i]);
    CHECK_EQ(test.resultD1, outputs_D[i]);

    if (i != 0) {
      f1 = test.resultS2 - 1.0F/outputs_S[i];
      f1 = (f1 < 0) ? f1 : -f1;
      CHECK(f1 <= deltaFloat);
      d1 = test.resultD2 - 1.0L/outputs_D[i];
      d1 = (d1 < 0) ? d1 : -d1;
      CHECK(d1 <= deltaDouble);
      f1 = test.resultS3 - 1.0F/inputs_S[i];
      f1 = (f1 < 0) ? f1 : -f1;
      CHECK(f1 <= deltaFloat);
      d1 = test.resultD3 - 1.0L/inputs_D[i];
      d1 = (d1 < 0) ? d1 : -d1;
      CHECK(d1 <= deltaDouble);
    } else {
      CHECK_EQ(test.resultS2, 1.0F/outputs_S[i]);
      CHECK_EQ(test.resultD2, 1.0L/outputs_D[i]);
      CHECK_EQ(test.resultS3, 1.0F/inputs_S[i]);
      CHECK_EQ(test.resultD3, 1.0L/inputs_D[i]);
    }
  }
}*/

TEST(LA15) {
  // Test chaining of label usages within instructions (issue 1644).
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  Label target;
  __ beq(a0, a1, &target);
  __ nop();
  __ bne(a0, a1, &target);
  __ nop();
  __ bind(&target);
  __ nop();
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  f.Call(1, 1, 0, 0, 0);
}

TEST(Trampoline) {
  static const int kMaxBranchOffset = (1 << (18 - 1)) - 1;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label done;
  size_t nr_calls = kMaxBranchOffset / kInstrSize + 5;

  __ xor_(a2, a2, a2);
  __ BranchShort(&done, eq, a0, Operand(a1));
  for (size_t i = 0; i < nr_calls; ++i) {
    __ addi_d(a2, a2, 1);
  }
  __ bind(&done);
  __ or_(a0, a2, zero_reg);
  __ jirl(zero_reg, ra, 0);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  int64_t res = reinterpret_cast<int64_t>(f.Call(42, 42, 0, 0, 0));
  CHECK_EQ(0, res);
}

#undef __

}  // namespace internal
}  // namespace v8
