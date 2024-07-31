// Copyright 2012 the V8 project authors. All rights reserved.
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
// TODO(mips64): Refine these signatures per test case.
using F1 = void*(int x, int p1, int p2, int p3, int p4);
using F2 = void*(int x, int y, int p2, int p3, int p4);
using F3 = void*(void* p, int p1, int p2, int p3, int p4);
using F4 = void*(int64_t x, int64_t y, int64_t p2, int64_t p3, int64_t p4);
using F5 = void*(void* p0, void* p1, int p2, int p3, int p4);

#define __ assm.

TEST(MIPS0) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Addition.
  __ addu(v0, a0, a1);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0xAB0, 0xC, 0, 0, 0));
  CHECK_EQ(0xABCL, res);
}


TEST(MIPS1) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label L, C;

  __ mov(a1, a0);
  __ li(v0, 0l);
  __ b(&C);
  __ nop();

  __ bind(&L);
  __ addu(v0, v0, a1);
  __ addiu(a1, a1, -1);

  __ bind(&C);
  __ xori(v1, a1, 0);
  __ Branch(&L, ne, v1, Operand((int64_t)0));
  __ nop();

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(50, 0, 0, 0, 0));
  CHECK_EQ(1275L, res);
}


TEST(MIPS2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label exit, error;

  // ----- Test all instructions.

  // Test lui, ori, and addiu, used in the li pseudo-instruction.
  // This way we can then safely load registers with chosen values.

  __ ori(a4, zero_reg, 0);
  __ lui(a4, 0x1234);
  __ ori(a4, a4, 0);
  __ ori(a4, a4, 0x0F0F);
  __ ori(a4, a4, 0xF0F0);
  __ addiu(a5, a4, 1);
  __ addiu(a6, a5, -0x10);

  // Load values in temporary registers.
  __ li(a4, 0x00000004);
  __ li(a5, 0x00001234);
  __ li(a6, 0x12345678);
  __ li(a7, 0x7FFFFFFF);
  __ li(t0, 0xFFFFFFFC);
  __ li(t1, 0xFFFFEDCC);
  __ li(t2, 0xEDCBA988);
  __ li(t3, 0x80000000);

  // SPECIAL class.
  __ srl(v0, a6, 8);    // 0x00123456
  __ sll(v0, v0, 11);   // 0x91A2B000
  __ sra(v0, v0, 3);    // 0xF2345600
  __ srav(v0, v0, a4);  // 0xFF234560
  __ sllv(v0, v0, a4);  // 0xF2345600
  __ srlv(v0, v0, a4);  // 0x0F234560
  __ Branch(&error, ne, v0, Operand(0x0F234560));
  __ nop();

  __ addu(v0, a4, a5);  // 0x00001238
  __ subu(v0, v0, a4);  // 0x00001234
  __ Branch(&error, ne, v0, Operand(0x00001234));
  __ nop();
  __ addu(v1, a7, a4);  // 32bit addu result is sign-extended into 64bit reg.
  __ Branch(&error, ne, v1, Operand(0xFFFFFFFF80000003));
  __ nop();
  __ subu(v1, t3, a4);  // 0x7FFFFFFC
  __ Branch(&error, ne, v1, Operand(0x7FFFFFFC));
  __ nop();

  __ and_(v0, a5, a6);  // 0x0000000000001230
  __ or_(v0, v0, a5);   // 0x0000000000001234
  __ xor_(v0, v0, a6);  // 0x000000001234444C
  __ nor(v0, v0, a6);   // 0xFFFFFFFFEDCBA987
  __ Branch(&error, ne, v0, Operand(0xFFFFFFFFEDCBA983));
  __ nop();

  // Shift both 32bit number to left, to preserve meaning of next comparison.
  __ dsll32(a7, a7, 0);
  __ dsll32(t3, t3, 0);

  __ slt(v0, t3, a7);
  __ Branch(&error, ne, v0, Operand(0x1));
  __ nop();
  __ sltu(v0, t3, a7);
  __ Branch(&error, ne, v0, Operand(zero_reg));
  __ nop();

  // Restore original values in registers.
  __ dsrl32(a7, a7, 0);
  __ dsrl32(t3, t3, 0);
  // End of SPECIAL class.

  __ addiu(v0, zero_reg, 0x7421);  // 0x00007421
  __ addiu(v0, v0, -0x1);          // 0x00007420
  __ addiu(v0, v0, -0x20);         // 0x00007400
  __ Branch(&error, ne, v0, Operand(0x00007400));
  __ nop();
  __ addiu(v1, a7, 0x1);  // 0x80000000 - result is sign-extended.
  __ Branch(&error, ne, v1, Operand(0xFFFFFFFF80000000));
  __ nop();

  __ slti(v0, a5, 0x00002000);  // 0x1
  __ slti(v0, v0, 0xFFFF8000);  // 0x0
  __ Branch(&error, ne, v0, Operand(zero_reg));
  __ nop();
  __ sltiu(v0, a5, 0x00002000);  // 0x1
  __ sltiu(v0, v0, 0x00008000);  // 0x1
  __ Branch(&error, ne, v0, Operand(0x1));
  __ nop();

  __ andi(v0, a5, 0xF0F0);  // 0x00001030
  __ ori(v0, v0, 0x8A00);   // 0x00009A30
  __ xori(v0, v0, 0x83CC);  // 0x000019FC
  __ Branch(&error, ne, v0, Operand(0x000019FC));
  __ nop();
  __ lui(v1, 0x8123);  // Result is sign-extended into 64bit register.
  __ Branch(&error, ne, v1, Operand(0xFFFFFFFF81230000));
  __ nop();

  // Bit twiddling instructions & conditional moves.
  // Uses a4-t3 as set above.
  __ Clz(v0, a4);       // 29
  __ Clz(v1, a5);       // 19
  __ addu(v0, v0, v1);  // 48
  __ Clz(v1, a6);       // 3
  __ addu(v0, v0, v1);  // 51
  __ Clz(v1, t3);       // 0
  __ addu(v0, v0, v1);  // 51
  __ Branch(&error, ne, v0, Operand(51));
  __ Movn(a0, a7, a4);  // Move a0<-a7 (a4 is NOT 0).
  __ Ins(a0, a5, 12, 8);  // 0x7FF34FFF
  __ Branch(&error, ne, a0, Operand(0x7FF34FFF));
  __ Movz(a0, t2, t3);    // a0 not updated (t3 is NOT 0).
  __ Ext(a1, a0, 8, 12);  // 0x34F
  __ Branch(&error, ne, a1, Operand(0x34F));
  __ Movz(a0, t2, v1);    // a0<-t2, v0 is 0, from 8 instr back.
  __ Branch(&error, ne, a0, Operand(t2));

  // Everything was correctly executed. Load the expected result.
  __ li(v0, 0x31415926);
  __ b(&exit);
  __ nop();

  __ bind(&error);
  // Got an error. Return a wrong result.
  __ li(v0, 666);

  __ bind(&exit);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  int64_t res = reinterpret_cast<int64_t>(f.Call(0xAB0, 0xC, 0, 0, 0));

  CHECK_EQ(0x31415926L, res);
}


TEST(MIPS3) {
  // Test floating point instructions.
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
    double g;
    double h;
    double i;
    float fa;
    float fb;
    float fc;
    float fd;
    float fe;
    float ff;
    float fg;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Double precision floating point instructions.
  __ Ldc1(f4, MemOperand(a0, offsetof(T, a)));
  __ Ldc1(f6, MemOperand(a0, offsetof(T, b)));
  __ add_d(f8, f4, f6);
  __ Sdc1(f8, MemOperand(a0, offsetof(T, c)));  // c = a + b.

  __ mov_d(f10, f8);  // c
  __ neg_d(f12, f6);  // -b
  __ sub_d(f10, f10, f12);
  __ Sdc1(f10, MemOperand(a0, offsetof(T, d)));  // d = c - (-b).

  __ Sdc1(f4, MemOperand(a0, offsetof(T, b)));  // b = a.

  __ li(a4, 120);
  __ mtc1(a4, f14);
  __ cvt_d_w(f14, f14);   // f14 = 120.0.
  __ mul_d(f10, f10, f14);
  __ Sdc1(f10, MemOperand(a0, offsetof(T, e)));  // e = d * 120 = 1.8066e16.

  __ div_d(f12, f10, f4);
  __ Sdc1(f12, MemOperand(a0, offsetof(T, f)));  // f = e / a = 120.44.

  __ sqrt_d(f14, f12);
  __ Sdc1(f14, MemOperand(a0, offsetof(T, g)));
  // g = sqrt(f) = 10.97451593465515908537

  if (kArchVariant == kMips64r2) {
    __ Ldc1(f4, MemOperand(a0, offsetof(T, h)));
    __ Ldc1(f6, MemOperand(a0, offsetof(T, i)));
    __ Madd_d(f14, f6, f4, f6, f8);
    __ Sdc1(f14, MemOperand(a0, offsetof(T, h)));
  }

  // Single precision floating point instructions.
  __ Lwc1(f4, MemOperand(a0, offsetof(T, fa)));
  __ Lwc1(f6, MemOperand(a0, offsetof(T, fb)));
  __ add_s(f8, f4, f6);
  __ Swc1(f8, MemOperand(a0, offsetof(T, fc)));  // fc = fa + fb.

  __ neg_s(f10, f6);  // -fb
  __ sub_s(f10, f8, f10);
  __ Swc1(f10, MemOperand(a0, offsetof(T, fd)));  // fd = fc - (-fb).

  __ Swc1(f4, MemOperand(a0, offsetof(T, fb)));  // fb = fa.

  __ li(t0, 120);
  __ mtc1(t0, f14);
  __ cvt_s_w(f14, f14);   // f14 = 120.0.
  __ mul_s(f10, f10, f14);
  __ Swc1(f10, MemOperand(a0, offsetof(T, fe)));  // fe = fd * 120

  __ div_s(f12, f10, f4);
  __ Swc1(f12, MemOperand(a0, offsetof(T, ff)));  // ff = fe / fa

  __ sqrt_s(f14, f12);
  __ Swc1(f14, MemOperand(a0, offsetof(T, fg)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  // Double test values.
  t.a = 1.5e14;
  t.b = 2.75e11;
  t.c = 0.0;
  t.d = 0.0;
  t.e = 0.0;
  t.f = 0.0;
  t.h = 1.5;
  t.i = 2.75;
  // Single test values.
  t.fa = 1.5e6;
  t.fb = 2.75e4;
  t.fc = 0.0;
  t.fd = 0.0;
  t.fe = 0.0;
  t.ff = 0.0;
  f.Call(&t, 0, 0, 0, 0);
  // Expected double results.
  CHECK_EQ(1.5e14, t.a);
  CHECK_EQ(1.5e14, t.b);
  CHECK_EQ(1.50275e14, t.c);
  CHECK_EQ(1.50550e14, t.d);
  CHECK_EQ(1.8066e16, t.e);
  CHECK_EQ(120.44, t.f);
  CHECK_EQ(10.97451593465515908537, t.g);
  if (kArchVariant == kMips64r2) {
    CHECK_EQ(6.875, t.h);
  }
  // Expected single results.
  CHECK_EQ(1.5e6, t.fa);
  CHECK_EQ(1.5e6, t.fb);
  CHECK_EQ(1.5275e06, t.fc);
  CHECK_EQ(1.5550e06, t.fd);
  CHECK_EQ(1.866e08, t.fe);
  CHECK_EQ(124.40000152587890625, t.ff);
  CHECK_EQ(11.1534748077392578125, t.fg);
}


TEST(MIPS4) {
  // Test moves between floating point and integer registers.
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

  __ Ldc1(f4, MemOperand(a0, offsetof(T, a)));
  __ Ldc1(f5, MemOperand(a0, offsetof(T, b)));

  // Swap f4 and f5, by using 3 integer registers, a4-a6,
  // both two 32-bit chunks, and one 64-bit chunk.
  // mXhc1 is mips32/64-r2 only, not r1,
  // but we will not support r1 in practice.
  __ mfc1(a4, f4);
  __ mfhc1(a5, f4);
  __ dmfc1(a6, f5);

  __ mtc1(a4, f5);
  __ mthc1(a5, f5);
  __ dmtc1(a6, f4);

  // Store the swapped f4 and f5 back to memory.
  __ Sdc1(f4, MemOperand(a0, offsetof(T, a)));
  __ Sdc1(f5, MemOperand(a0, offsetof(T, c)));

  // Test sign extension of move operations from coprocessor.
  __ Ldc1(f4, MemOperand(a0, offsetof(T, d)));
  __ mfhc1(a4, f4);
  __ mfc1(a5, f4);

  __ Sd(a4, MemOperand(a0, offsetof(T, high)));
  __ Sd(a5, MemOperand(a0, offsetof(T, low)));

  __ jr(ra);
  __ nop();

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
}


TEST(MIPS5) {
  // Test conversions between doubles and integers.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double b;
    int i;
    int j;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Load all structure elements to registers.
  __ Ldc1(f4, MemOperand(a0, offsetof(T, a)));
  __ Ldc1(f6, MemOperand(a0, offsetof(T, b)));
  __ Lw(a4, MemOperand(a0, offsetof(T, i)));
  __ Lw(a5, MemOperand(a0, offsetof(T, j)));

  // Convert double in f4 to int in element i.
  __ cvt_w_d(f8, f4);
  __ mfc1(a6, f8);
  __ Sw(a6, MemOperand(a0, offsetof(T, i)));

  // Convert double in f6 to int in element j.
  __ cvt_w_d(f10, f6);
  __ mfc1(a7, f10);
  __ Sw(a7, MemOperand(a0, offsetof(T, j)));

  // Convert int in original i (a4) to double in a.
  __ mtc1(a4, f12);
  __ cvt_d_w(f0, f12);
  __ Sdc1(f0, MemOperand(a0, offsetof(T, a)));

  // Convert int in original j (a5) to double in b.
  __ mtc1(a5, f14);
  __ cvt_d_w(f2, f14);
  __ Sdc1(f2, MemOperand(a0, offsetof(T, b)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.a = 1.5e4;
  t.b = 2.75e8;
  t.i = 12345678;
  t.j = -100000;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(12345678.0, t.a);
  CHECK_EQ(-100000.0, t.b);
  CHECK_EQ(15000, t.i);
  CHECK_EQ(275000000, t.j);
}


TEST(MIPS6) {
  // Test simple memory loads and stores.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint32_t ui;
    int32_t si;
    int32_t r1;
    int32_t r2;
    int32_t r3;
    int32_t r4;
    int32_t r5;
    int32_t r6;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Basic word load/store.
  __ Lw(a4, MemOperand(a0, offsetof(T, ui)));
  __ Sw(a4, MemOperand(a0, offsetof(T, r1)));

  // lh with positive data.
  __ Lh(a5, MemOperand(a0, offsetof(T, ui)));
  __ Sw(a5, MemOperand(a0, offsetof(T, r2)));

  // lh with negative data.
  __ Lh(a6, MemOperand(a0, offsetof(T, si)));
  __ Sw(a6, MemOperand(a0, offsetof(T, r3)));

  // lhu with negative data.
  __ Lhu(a7, MemOperand(a0, offsetof(T, si)));
  __ Sw(a7, MemOperand(a0, offsetof(T, r4)));

  // Lb with negative data.
  __ Lb(t0, MemOperand(a0, offsetof(T, si)));
  __ Sw(t0, MemOperand(a0, offsetof(T, r5)));

  // sh writes only 1/2 of word.
  __ lui(t1, 0x3333);
  __ ori(t1, t1, 0x3333);
  __ Sw(t1, MemOperand(a0, offsetof(T, r6)));
  __ Lhu(t1, MemOperand(a0, offsetof(T, si)));
  __ Sh(t1, MemOperand(a0, offsetof(T, r6)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.ui = 0x11223344;
  t.si = 0x99AABBCC;
  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int32_t>(0x11223344), t.r1);
  if (kArchEndian == kLittle)  {
    CHECK_EQ(static_cast<int32_t>(0x3344), t.r2);
    CHECK_EQ(static_cast<int32_t>(0xFFFFBBCC), t.r3);
    CHECK_EQ(static_cast<int32_t>(0x0000BBCC), t.r4);
    CHECK_EQ(static_cast<int32_t>(0xFFFFFFCC), t.r5);
    CHECK_EQ(static_cast<int32_t>(0x3333BBCC), t.r6);
  } else {
    CHECK_EQ(static_cast<int32_t>(0x1122), t.r2);
    CHECK_EQ(static_cast<int32_t>(0xFFFF99AA), t.r3);
    CHECK_EQ(static_cast<int32_t>(0x000099AA), t.r4);
    CHECK_EQ(static_cast<int32_t>(0xFFFFFF99), t.r5);
    CHECK_EQ(static_cast<int32_t>(0x99AA3333), t.r6);
  }
}


TEST(MIPS7) {
  // Test floating point compare and branch instructions.
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
    int32_t result;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles t.a ... t.f.
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label neither_is_nan, less_than, outa_here;

  __ Ldc1(f4, MemOperand(a0, offsetof(T, a)));
  __ Ldc1(f6, MemOperand(a0, offsetof(T, b)));
  if (kArchVariant != kMips64r6) {
    __ c(UN, D, f4, f6);
    __ bc1f(&neither_is_nan);
  } else {
    __ cmp(UN, L, f2, f4, f6);
    __ bc1eqz(&neither_is_nan, f2);
  }
  __ nop();
  __ Sw(zero_reg, MemOperand(a0, offsetof(T, result)));
  __ Branch(&outa_here);

  __ bind(&neither_is_nan);

  if (kArchVariant == kMips64r6) {
    __ cmp(OLT, L, f2, f6, f4);
    __ bc1nez(&less_than, f2);
  } else {
    __ c(OLT, D, f6, f4, 2);
    __ bc1t(&less_than, 2);
  }

  __ nop();
  __ Sw(zero_reg, MemOperand(a0, offsetof(T, result)));
  __ Branch(&outa_here);

  __ bind(&less_than);
  __ Addu(a4, zero_reg, Operand(1));
  __ Sw(a4, MemOperand(a0, offsetof(T, result)));  // Set true.

  // This test-case should have additional tests.

  __ bind(&outa_here);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.a = 1.5e14;
  t.b = 2.75e11;
  t.c = 2.0;
  t.d = -4.0;
  t.e = 0.0;
  t.f = 0.0;
  t.result = 0;
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(1.5e14, t.a);
  CHECK_EQ(2.75e11, t.b);
  CHECK_EQ(1, t.result);
}


TEST(MIPS8) {
  if (kArchVariant == kMips64r2) {
    // Test ROTR and ROTRV instructions.
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);

    struct T {
      int32_t input;
      int32_t result_rotr_4;
      int32_t result_rotr_8;
      int32_t result_rotr_12;
      int32_t result_rotr_16;
      int32_t result_rotr_20;
      int32_t result_rotr_24;
      int32_t result_rotr_28;
      int32_t result_rotrv_4;
      int32_t result_rotrv_8;
      int32_t result_rotrv_12;
      int32_t result_rotrv_16;
      int32_t result_rotrv_20;
      int32_t result_rotrv_24;
      int32_t result_rotrv_28;
    };
    T t;

    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    // Basic word load.
    __ Lw(a4, MemOperand(a0, offsetof(T, input)));

    // ROTR instruction (called through the Ror macro).
    __ Ror(a5, a4, 0x0004);
    __ Ror(a6, a4, 0x0008);
    __ Ror(a7, a4, 0x000C);
    __ Ror(t0, a4, 0x0010);
    __ Ror(t1, a4, 0x0014);
    __ Ror(t2, a4, 0x0018);
    __ Ror(t3, a4, 0x001C);

    // Basic word store.
    __ Sw(a5, MemOperand(a0, offsetof(T, result_rotr_4)));
    __ Sw(a6, MemOperand(a0, offsetof(T, result_rotr_8)));
    __ Sw(a7, MemOperand(a0, offsetof(T, result_rotr_12)));
    __ Sw(t0, MemOperand(a0, offsetof(T, result_rotr_16)));
    __ Sw(t1, MemOperand(a0, offsetof(T, result_rotr_20)));
    __ Sw(t2, MemOperand(a0, offsetof(T, result_rotr_24)));
    __ Sw(t3, MemOperand(a0, offsetof(T, result_rotr_28)));

    // ROTRV instruction (called through the Ror macro).
    __ li(t3, 0x0004);
    __ Ror(a5, a4, t3);
    __ li(t3, 0x0008);
    __ Ror(a6, a4, t3);
    __ li(t3, 0x000C);
    __ Ror(a7, a4, t3);
    __ li(t3, 0x0010);
    __ Ror(t0, a4, t3);
    __ li(t3, 0x0014);
    __ Ror(t1, a4, t3);
    __ li(t3, 0x0018);
    __ Ror(t2, a4, t3);
    __ li(t3, 0x001C);
    __ Ror(t3, a4, t3);

    // Basic word store.
    __ Sw(a5, MemOperand(a0, offsetof(T, result_rotrv_4)));
    __ Sw(a6, MemOperand(a0, offsetof(T, result_rotrv_8)));
    __ Sw(a7, MemOperand(a0, offsetof(T, result_rotrv_12)));
    __ Sw(t0, MemOperand(a0, offsetof(T, result_rotrv_16)));
    __ Sw(t1, MemOperand(a0, offsetof(T, result_rotrv_20)));
    __ Sw(t2, MemOperand(a0, offsetof(T, result_rotrv_24)));
    __ Sw(t3, MemOperand(a0, offsetof(T, result_rotrv_28)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    t.input = 0x12345678;
    f.Call(&t, 0x0, 0, 0, 0);
    CHECK_EQ(static_cast<int32_t>(0x81234567), t.result_rotr_4);
    CHECK_EQ(static_cast<int32_t>(0x78123456), t.result_rotr_8);
    CHECK_EQ(static_cast<int32_t>(0x67812345), t.result_rotr_12);
    CHECK_EQ(static_cast<int32_t>(0x56781234), t.result_rotr_16);
    CHECK_EQ(static_cast<int32_t>(0x45678123), t.result_rotr_20);
    CHECK_EQ(static_cast<int32_t>(0x34567812), t.result_rotr_24);
    CHECK_EQ(static_cast<int32_t>(0x23456781), t.result_rotr_28);

    CHECK_EQ(static_cast<int32_t>(0x81234567), t.result_rotrv_4);
    CHECK_EQ(static_cast<int32_t>(0x78123456), t.result_rotrv_8);
    CHECK_EQ(static_cast<int32_t>(0x67812345), t.result_rotrv_12);
    CHECK_EQ(static_cast<int32_t>(0x56781234), t.result_rotrv_16);
    CHECK_EQ(static_cast<int32_t>(0x45678123), t.result_rotrv_20);
    CHECK_EQ(static_cast<int32_t>(0x34567812), t.result_rotrv_24);
    CHECK_EQ(static_cast<int32_t>(0x23456781), t.result_rotrv_28);
  }
}


TEST(MIPS9) {
  // Test BRANCH improvements.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label exit, exit2, exit3;

  __ Branch(&exit, ge, a0, Operand(zero_reg));
  __ Branch(&exit2, ge, a0, Operand(0x00001FFF));
  __ Branch(&exit3, ge, a0, Operand(0x0001FFFF));

  __ bind(&exit);
  __ bind(&exit2);
  __ bind(&exit3);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  USE(code);
}


TEST(MIPS10) {
  // Test conversions between doubles and long integers.
  // Test hos the long ints map to FP regs pairs.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double a;
    double a_converted;
    double b;
    int32_t dbl_mant;
    int32_t dbl_exp;
    int32_t long_hi;
    int32_t long_lo;
    int64_t long_as_int64;
    int32_t b_long_hi;
    int32_t b_long_lo;
    int64_t b_long_as_int64;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  if (kArchVariant == kMips64r2) {
    // Rewritten for FR=1 FPU mode:
    //  -  32 FP regs of 64-bits each, no odd/even pairs.
    //  -  Note that cvt_l_d/cvt_d_l ARE legal in FR=1 mode.
    // Load all structure elements to registers.
    __ Ldc1(f0, MemOperand(a0, offsetof(T, a)));

    // Save the raw bits of the double.
    __ mfc1(a4, f0);
    __ mfhc1(a5, f0);
    __ Sw(a4, MemOperand(a0, offsetof(T, dbl_mant)));
    __ Sw(a5, MemOperand(a0, offsetof(T, dbl_exp)));

    // Convert double in f0 to long, save hi/lo parts.
    __ cvt_l_d(f0, f0);
    __ mfc1(a4, f0);  // f0 LS 32 bits of long.
    __ mfhc1(a5, f0);  // f0 MS 32 bits of long.
    __ Sw(a4, MemOperand(a0, offsetof(T, long_lo)));
    __ Sw(a5, MemOperand(a0, offsetof(T, long_hi)));

    // Combine the high/low ints, convert back to double.
    __ dsll32(a6, a5, 0);  // Move a5 to high bits of a6.
    __ or_(a6, a6, a4);
    __ dmtc1(a6, f1);
    __ cvt_d_l(f1, f1);
    __ Sdc1(f1, MemOperand(a0, offsetof(T, a_converted)));

    // Convert the b long integers to double b.
    __ Lw(a4, MemOperand(a0, offsetof(T, b_long_lo)));
    __ Lw(a5, MemOperand(a0, offsetof(T, b_long_hi)));
    __ mtc1(a4, f8);  // f8 LS 32-bits.
    __ mthc1(a5, f8);  // f8 MS 32-bits.
    __ cvt_d_l(f10, f8);
    __ Sdc1(f10, MemOperand(a0, offsetof(T, b)));

    // Convert double b back to long-int.
    __ Ldc1(f31, MemOperand(a0, offsetof(T, b)));
    __ cvt_l_d(f31, f31);
    __ dmfc1(a7, f31);
    __ Sd(a7, MemOperand(a0, offsetof(T, b_long_as_int64)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    t.a = 2.147483647e9;       // 0x7FFFFFFF -> 0x41DFFFFFFFC00000 as double.
    t.b_long_hi = 0x000000FF;  // 0xFF00FF00FF -> 0x426FE01FE01FE000 as double.
    t.b_long_lo = 0x00FF00FF;
    f.Call(&t, 0, 0, 0, 0);

    CHECK_EQ(static_cast<int32_t>(0x41DFFFFF), t.dbl_exp);
    CHECK_EQ(static_cast<int32_t>(0xFFC00000), t.dbl_mant);
    CHECK_EQ(0, t.long_hi);
    CHECK_EQ(static_cast<int32_t>(0x7FFFFFFF), t.long_lo);
    CHECK_EQ(2.147483647e9, t.a_converted);

    // 0xFF00FF00FF -> 1.095233372415e12.
    CHECK_EQ(1.095233372415e12, t.b);
    CHECK_EQ(static_cast<int64_t>(0xFF00FF00FF), t.b_long_as_int64);
  }
}


TEST(MIPS11) {
  // Do not run test on MIPS64r6, as these instructions are removed.
  if (kArchVariant != kMips64r6) {
    // Test LWL, LWR, SWL and SWR instructions.
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);

    struct T {
      int32_t reg_init;
      int32_t mem_init;
      int32_t lwl_0;
      int32_t lwl_1;
      int32_t lwl_2;
      int32_t lwl_3;
      int32_t lwr_0;
      int32_t lwr_1;
      int32_t lwr_2;
      int32_t lwr_3;
      int32_t swl_0;
      int32_t swl_1;
      int32_t swl_2;
      int32_t swl_3;
      int32_t swr_0;
      int32_t swr_1;
      int32_t swr_2;
      int32_t swr_3;
    };
    T t;

    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    // Test all combinations of LWL and vAddr.
    __ Lw(a4, MemOperand(a0, offsetof(T, reg_init)));
    __ lwl(a4, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a4, MemOperand(a0, offsetof(T, lwl_0)));

    __ Lw(a5, MemOperand(a0, offsetof(T, reg_init)));
    __ lwl(a5, MemOperand(a0, offsetof(T, mem_init) + 1));
    __ Sw(a5, MemOperand(a0, offsetof(T, lwl_1)));

    __ Lw(a6, MemOperand(a0, offsetof(T, reg_init)));
    __ lwl(a6, MemOperand(a0, offsetof(T, mem_init) + 2));
    __ Sw(a6, MemOperand(a0, offsetof(T, lwl_2)));

    __ Lw(a7, MemOperand(a0, offsetof(T, reg_init)));
    __ lwl(a7, MemOperand(a0, offsetof(T, mem_init) + 3));
    __ Sw(a7, MemOperand(a0, offsetof(T, lwl_3)));

    // Test all combinations of LWR and vAddr.
    __ Lw(a4, MemOperand(a0, offsetof(T, reg_init)));
    __ lwr(a4, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a4, MemOperand(a0, offsetof(T, lwr_0)));

    __ Lw(a5, MemOperand(a0, offsetof(T, reg_init)));
    __ lwr(a5, MemOperand(a0, offsetof(T, mem_init) + 1));
    __ Sw(a5, MemOperand(a0, offsetof(T, lwr_1)));

    __ Lw(a6, MemOperand(a0, offsetof(T, reg_init)));
    __ lwr(a6, MemOperand(a0, offsetof(T, mem_init) + 2));
    __ Sw(a6, MemOperand(a0, offsetof(T, lwr_2)));

    __ Lw(a7, MemOperand(a0, offsetof(T, reg_init)));
    __ lwr(a7, MemOperand(a0, offsetof(T, mem_init) + 3));
    __ Sw(a7, MemOperand(a0, offsetof(T, lwr_3)));

    // Test all combinations of SWL and vAddr.
    __ Lw(a4, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a4, MemOperand(a0, offsetof(T, swl_0)));
    __ Lw(a4, MemOperand(a0, offsetof(T, reg_init)));
    __ swl(a4, MemOperand(a0, offsetof(T, swl_0)));

    __ Lw(a5, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a5, MemOperand(a0, offsetof(T, swl_1)));
    __ Lw(a5, MemOperand(a0, offsetof(T, reg_init)));
    __ swl(a5, MemOperand(a0, offsetof(T, swl_1) + 1));

    __ Lw(a6, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a6, MemOperand(a0, offsetof(T, swl_2)));
    __ Lw(a6, MemOperand(a0, offsetof(T, reg_init)));
    __ swl(a6, MemOperand(a0, offsetof(T, swl_2) + 2));

    __ Lw(a7, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a7, MemOperand(a0, offsetof(T, swl_3)));
    __ Lw(a7, MemOperand(a0, offsetof(T, reg_init)));
    __ swl(a7, MemOperand(a0, offsetof(T, swl_3) + 3));

    // Test all combinations of SWR and vAddr.
    __ Lw(a4, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a4, MemOperand(a0, offsetof(T, swr_0)));
    __ Lw(a4, MemOperand(a0, offsetof(T, reg_init)));
    __ swr(a4, MemOperand(a0, offsetof(T, swr_0)));

    __ Lw(a5, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a5, MemOperand(a0, offsetof(T, swr_1)));
    __ Lw(a5, MemOperand(a0, offsetof(T, reg_init)));
    __ swr(a5, MemOperand(a0, offsetof(T, swr_1) + 1));

    __ Lw(a6, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a6, MemOperand(a0, offsetof(T, swr_2)));
    __ Lw(a6, MemOperand(a0, offsetof(T, reg_init)));
    __ swr(a6, MemOperand(a0, offsetof(T, swr_2) + 2));

    __ Lw(a7, MemOperand(a0, offsetof(T, mem_init)));
    __ Sw(a7, MemOperand(a0, offsetof(T, swr_3)));
    __ Lw(a7, MemOperand(a0, offsetof(T, reg_init)));
    __ swr(a7, MemOperand(a0, offsetof(T, swr_3) + 3));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    t.reg_init = 0xAABBCCDD;
    t.mem_init = 0x11223344;

    f.Call(&t, 0, 0, 0, 0);

    if (kArchEndian == kLittle) {
      CHECK_EQ(static_cast<int32_t>(0x44BBCCDD), t.lwl_0);
      CHECK_EQ(static_cast<int32_t>(0x3344CCDD), t.lwl_1);
      CHECK_EQ(static_cast<int32_t>(0x223344DD), t.lwl_2);
      CHECK_EQ(static_cast<int32_t>(0x11223344), t.lwl_3);

      CHECK_EQ(static_cast<int32_t>(0x11223344), t.lwr_0);
      CHECK_EQ(static_cast<int32_t>(0xAA112233), t.lwr_1);
      CHECK_EQ(static_cast<int32_t>(0xAABB1122), t.lwr_2);
      CHECK_EQ(static_cast<int32_t>(0xAABBCC11), t.lwr_3);

      CHECK_EQ(static_cast<int32_t>(0x112233AA), t.swl_0);
      CHECK_EQ(static_cast<int32_t>(0x1122AABB), t.swl_1);
      CHECK_EQ(static_cast<int32_t>(0x11AABBCC), t.swl_2);
      CHECK_EQ(static_cast<int32_t>(0xAABBCCDD), t.swl_3);

      CHECK_EQ(static_cast<int32_t>(0xAABBCCDD), t.swr_0);
      CHECK_EQ(static_cast<int32_t>(0xBBCCDD44), t.swr_1);
      CHECK_EQ(static_cast<int32_t>(0xCCDD3344), t.swr_2);
      CHECK_EQ(static_cast<int32_t>(0xDD223344), t.swr_3);
    } else {
      CHECK_EQ(static_cast<int32_t>(0x11223344), t.lwl_0);
      CHECK_EQ(static_cast<int32_t>(0x223344DD), t.lwl_1);
      CHECK_EQ(static_cast<int32_t>(0x3344CCDD), t.lwl_2);
      CHECK_EQ(static_cast<int32_t>(0x44BBCCDD), t.lwl_3);

      CHECK_EQ(static_cast<int32_t>(0xAABBCC11), t.lwr_0);
      CHECK_EQ(static_cast<int32_t>(0xAABB1122), t.lwr_1);
      CHECK_EQ(static_cast<int32_t>(0xAA112233), t.lwr_2);
      CHECK_EQ(static_cast<int32_t>(0x11223344), t.lwr_3);

      CHECK_EQ(static_cast<int32_t>(0xAABBCCDD), t.swl_0);
      CHECK_EQ(static_cast<int32_t>(0x11AABBCC), t.swl_1);
      CHECK_EQ(static_cast<int32_t>(0x1122AABB), t.swl_2);
      CHECK_EQ(static_cast<int32_t>(0x112233AA), t.swl_3);

      CHECK_EQ(static_cast<int32_t>(0xDD223344), t.swr_0);
      CHECK_EQ(static_cast<int32_t>(0xCCDD3344), t.swr_1);
      CHECK_EQ(static_cast<int32_t>(0xBBCCDD44), t.swr_2);
      CHECK_EQ(static_cast<int32_t>(0xAABBCCDD), t.swr_3);
    }
  }
}


TEST(MIPS12) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    int32_t x;
    int32_t y;
    int32_t y1;
    int32_t y2;
    int32_t y3;
    int32_t y4;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ mov(t2, fp);  // Save frame pointer.
  __ mov(fp, a0);  // Access struct T by fp.
  __ Lw(a4, MemOperand(a0, offsetof(T, y)));
  __ Lw(a7, MemOperand(a0, offsetof(T, y4)));

  __ addu(a5, a4, a7);
  __ subu(t0, a4, a7);
  __ nop();
  __ push(a4);  // These instructions disappear after opt.
  __ Pop();
  __ addu(a4, a4, a4);
  __ nop();
  __ Pop();     // These instructions disappear after opt.
  __ push(a7);
  __ nop();
  __ push(a7);  // These instructions disappear after opt.
  __ pop(a7);
  __ nop();
  __ push(a7);
  __ pop(t0);
  __ nop();
  __ Sw(a4, MemOperand(fp, offsetof(T, y)));
  __ Lw(a4, MemOperand(fp, offsetof(T, y)));
  __ nop();
  __ Sw(a4, MemOperand(fp, offsetof(T, y)));
  __ Lw(a5, MemOperand(fp, offsetof(T, y)));
  __ nop();
  __ push(a5);
  __ Lw(a5, MemOperand(fp, offsetof(T, y)));
  __ pop(a5);
  __ nop();
  __ push(a5);
  __ Lw(a6, MemOperand(fp, offsetof(T, y)));
  __ pop(a5);
  __ nop();
  __ push(a5);
  __ Lw(a6, MemOperand(fp, offsetof(T, y)));
  __ pop(a6);
  __ nop();
  __ push(a6);
  __ Lw(a6, MemOperand(fp, offsetof(T, y)));
  __ pop(a5);
  __ nop();
  __ push(a5);
  __ Lw(a6, MemOperand(fp, offsetof(T, y)));
  __ pop(a7);
  __ nop();

  __ mov(fp, t2);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.x = 1;
  t.y = 2;
  t.y1 = 3;
  t.y2 = 4;
  t.y3 = 0XBABA;
  t.y4 = 0xDEDA;

  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(3, t.y1);
}


TEST(MIPS13) {
  // Test Cvt_d_uw and Trunc_uw_d macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double cvt_big_out;
    double cvt_small_out;
    uint32_t trunc_big_out;
    uint32_t trunc_small_out;
    uint32_t cvt_big_in;
    uint32_t cvt_small_in;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ Sw(a4, MemOperand(a0, offsetof(T, cvt_small_in)));
  __ Cvt_d_uw(f10, a4);
  __ Sdc1(f10, MemOperand(a0, offsetof(T, cvt_small_out)));

  __ Trunc_uw_d(f10, f10, f4);
  __ Swc1(f10, MemOperand(a0, offsetof(T, trunc_small_out)));

  __ Sw(a4, MemOperand(a0, offsetof(T, cvt_big_in)));
  __ Cvt_d_uw(f8, a4);
  __ Sdc1(f8, MemOperand(a0, offsetof(T, cvt_big_out)));

  __ Trunc_uw_d(f8, f8, f4);
  __ Swc1(f8, MemOperand(a0, offsetof(T, trunc_big_out)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  t.cvt_big_in = 0xFFFFFFFF;
  t.cvt_small_in  = 333;

  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(t.cvt_big_out, static_cast<double>(t.cvt_big_in));
  CHECK_EQ(t.cvt_small_out, static_cast<double>(t.cvt_small_in));

  CHECK_EQ(static_cast<int>(t.trunc_big_out), static_cast<int>(t.cvt_big_in));
  CHECK_EQ(static_cast<int>(t.trunc_small_out),
           static_cast<int>(t.cvt_small_in));
}


TEST(MIPS14) {
  // Test round, floor, ceil, trunc, cvt.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

#define ROUND_STRUCT_ELEMENT(x) \
  uint32_t x##_isNaN2008; \
  int32_t x##_up_out; \
  int32_t x##_down_out; \
  int32_t neg_##x##_up_out; \
  int32_t neg_##x##_down_out; \
  uint32_t x##_err1_out; \
  uint32_t x##_err2_out; \
  uint32_t x##_err3_out; \
  uint32_t x##_err4_out; \
  int32_t x##_invalid_result;

  struct T {
    double round_up_in;
    double round_down_in;
    double neg_round_up_in;
    double neg_round_down_in;
    double err1_in;
    double err2_in;
    double err3_in;
    double err4_in;

    ROUND_STRUCT_ELEMENT(round)
    ROUND_STRUCT_ELEMENT(floor)
    ROUND_STRUCT_ELEMENT(ceil)
    ROUND_STRUCT_ELEMENT(trunc)
    ROUND_STRUCT_ELEMENT(cvt)
  };
  T t;

#undef ROUND_STRUCT_ELEMENT

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Save FCSR.
  __ cfc1(a1, FCSR);
  // Disable FPU exceptions.
  __ ctc1(zero_reg, FCSR);
#define RUN_ROUND_TEST(x)                                       \
  __ cfc1(t0, FCSR);                                            \
  __ Sw(t0, MemOperand(a0, offsetof(T, x##_isNaN2008)));        \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, round_up_in)));        \
  __ x##_w_d(f0, f0);                                           \
  __ Swc1(f0, MemOperand(a0, offsetof(T, x##_up_out)));         \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, round_down_in)));      \
  __ x##_w_d(f0, f0);                                           \
  __ Swc1(f0, MemOperand(a0, offsetof(T, x##_down_out)));       \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, neg_round_up_in)));    \
  __ x##_w_d(f0, f0);                                           \
  __ Swc1(f0, MemOperand(a0, offsetof(T, neg_##x##_up_out)));   \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, neg_round_down_in)));  \
  __ x##_w_d(f0, f0);                                           \
  __ Swc1(f0, MemOperand(a0, offsetof(T, neg_##x##_down_out))); \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, err1_in)));            \
  __ ctc1(zero_reg, FCSR);                                      \
  __ x##_w_d(f0, f0);                                           \
  __ cfc1(a2, FCSR);                                            \
  __ Sw(a2, MemOperand(a0, offsetof(T, x##_err1_out)));         \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, err2_in)));            \
  __ ctc1(zero_reg, FCSR);                                      \
  __ x##_w_d(f0, f0);                                           \
  __ cfc1(a2, FCSR);                                            \
  __ Sw(a2, MemOperand(a0, offsetof(T, x##_err2_out)));         \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, err3_in)));            \
  __ ctc1(zero_reg, FCSR);                                      \
  __ x##_w_d(f0, f0);                                           \
  __ cfc1(a2, FCSR);                                            \
  __ Sw(a2, MemOperand(a0, offsetof(T, x##_err3_out)));         \
                                                                \
  __ Ldc1(f0, MemOperand(a0, offsetof(T, err4_in)));            \
  __ ctc1(zero_reg, FCSR);                                      \
  __ x##_w_d(f0, f0);                                           \
  __ cfc1(a2, FCSR);                                            \
  __ Sw(a2, MemOperand(a0, offsetof(T, x##_err4_out)));         \
  __ Swc1(f0, MemOperand(a0, offsetof(T, x##_invalid_result)));

  RUN_ROUND_TEST(round)
  RUN_ROUND_TEST(floor)
  RUN_ROUND_TEST(ceil)
  RUN_ROUND_TEST(trunc)
  RUN_ROUND_TEST(cvt)

  // Restore FCSR.
  __ ctc1(a1, FCSR);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  t.round_up_in = 123.51;
  t.round_down_in = 123.49;
  t.neg_round_up_in = -123.5;
  t.neg_round_down_in = -123.49;
  t.err1_in = 123.51;
  t.err2_in = 1;
  t.err3_in = static_cast<double>(1) + 0xFFFFFFFF;
  t.err4_in = NAN;

  f.Call(&t, 0, 0, 0, 0);

#define GET_FPU_ERR(x) (static_cast<int>(x & kFCSRFlagMask))
#define CHECK_NAN2008(x) (x & kFCSRNaN2008FlagMask)
#define CHECK_ROUND_RESULT(type) \
  CHECK(GET_FPU_ERR(t.type##_err1_out) & kFCSRInexactFlagMask); \
  CHECK_EQ(0, GET_FPU_ERR(t.type##_err2_out)); \
  CHECK(GET_FPU_ERR(t.type##_err3_out) & kFCSRInvalidOpFlagMask); \
  CHECK(GET_FPU_ERR(t.type##_err4_out) & kFCSRInvalidOpFlagMask); \
  if (CHECK_NAN2008(t.type##_isNaN2008) && kArchVariant == kMips64r6) { \
    CHECK_EQ(static_cast<int32_t>(0), t.type##_invalid_result);\
  } else { \
    CHECK_EQ(static_cast<int32_t>(kFPUInvalidResult), t.type##_invalid_result);\
  }

  CHECK_ROUND_RESULT(round);
  CHECK_ROUND_RESULT(floor);
  CHECK_ROUND_RESULT(ceil);
  CHECK_ROUND_RESULT(cvt);
}


TEST(MIPS15) {
  // Test chaining of label usages within instructions (issue 1644).
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  Label target;
  __ beq(v0, v1, &target);
  __ nop();
  __ bne(v0, v1, &target);
  __ nop();
  __ bind(&target);
  __ nop();
}


// ----- mips64 tests -----------------------------------------------

TEST(MIPS16) {
  // Test 64-bit memory loads and stores.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    int64_t r1;
    int64_t r2;
    int64_t r3;
    int64_t r4;
    int64_t r5;
    int64_t r6;
    int64_t r7;
    int64_t r8;
    int64_t r9;
    int64_t r10;
    int64_t r11;
    int64_t r12;
    uint32_t ui;
    int32_t si;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // Basic 32-bit word load/store, with un-signed data.
  __ Lw(a4, MemOperand(a0, offsetof(T, ui)));
  __ Sw(a4, MemOperand(a0, offsetof(T, r1)));

  // Check that the data got zero-extended into 64-bit a4.
  __ Sd(a4, MemOperand(a0, offsetof(T, r2)));

  // Basic 32-bit word load/store, with SIGNED data.
  __ Lw(a5, MemOperand(a0, offsetof(T, si)));
  __ Sw(a5, MemOperand(a0, offsetof(T, r3)));

  // Check that the data got sign-extended into 64-bit a4.
  __ Sd(a5, MemOperand(a0, offsetof(T, r4)));

  // 32-bit UNSIGNED word load/store, with SIGNED data.
  __ Lwu(a6, MemOperand(a0, offsetof(T, si)));
  __ Sw(a6, MemOperand(a0, offsetof(T, r5)));

  // Check that the data got zero-extended into 64-bit a4.
  __ Sd(a6, MemOperand(a0, offsetof(T, r6)));

  // lh with positive data.
  __ Lh(a5, MemOperand(a0, offsetof(T, ui)));
  __ Sw(a5, MemOperand(a0, offsetof(T, r7)));

  // lh with negative data.
  __ Lh(a6, MemOperand(a0, offsetof(T, si)));
  __ Sw(a6, MemOperand(a0, offsetof(T, r8)));

  // lhu with negative data.
  __ Lhu(a7, MemOperand(a0, offsetof(T, si)));
  __ Sw(a7, MemOperand(a0, offsetof(T, r9)));

  // Lb with negative data.
  __ Lb(t0, MemOperand(a0, offsetof(T, si)));
  __ Sw(t0, MemOperand(a0, offsetof(T, r10)));

  // sh writes only 1/2 of word.
  __ Lw(a4, MemOperand(a0, offsetof(T, ui)));
  __ Sh(a4, MemOperand(a0, offsetof(T, r11)));
  __ Lw(a4, MemOperand(a0, offsetof(T, si)));
  __ Sh(a4, MemOperand(a0, offsetof(T, r12)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  t.ui = 0x44332211;
  t.si = 0x99AABBCC;
  t.r1 = 0x5555555555555555;
  t.r2 = 0x5555555555555555;
  t.r3 = 0x5555555555555555;
  t.r4 = 0x5555555555555555;
  t.r5 = 0x5555555555555555;
  t.r6 = 0x5555555555555555;
  t.r7 = 0x5555555555555555;
  t.r8 = 0x5555555555555555;
  t.r9 = 0x5555555555555555;
  t.r10 = 0x5555555555555555;
  t.r11 = 0x5555555555555555;
  t.r12 = 0x5555555555555555;

  f.Call(&t, 0, 0, 0, 0);

  if (kArchEndian == kLittle) {
    // Unsigned data, 32 & 64
    CHECK_EQ(static_cast<int64_t>(0x5555555544332211L), t.r1);  // lw, sw.
    CHECK_EQ(static_cast<int64_t>(0x0000000044332211L), t.r2);  // sd.

    // Signed data, 32 & 64.
    CHECK_EQ(static_cast<int64_t>(0x5555555599AABBCCL), t.r3);  // lw, sw.
    CHECK_EQ(static_cast<int64_t>(0xFFFFFFFF99AABBCCL), t.r4);  // sd.

    // Signed data, 32 & 64.
    CHECK_EQ(static_cast<int64_t>(0x5555555599AABBCCL), t.r5);  // lwu, sw.
    CHECK_EQ(static_cast<int64_t>(0x0000000099AABBCCL), t.r6);  // sd.

    // lh with unsigned and signed data.
    CHECK_EQ(static_cast<int64_t>(0x5555555500002211L), t.r7);  // lh, sw.
    CHECK_EQ(static_cast<int64_t>(0x55555555FFFFBBCCL), t.r8);  // lh, sw.

    // lhu with signed data.
    CHECK_EQ(static_cast<int64_t>(0x555555550000BBCCL), t.r9);  // lhu, sw.

    // lb with signed data.
    CHECK_EQ(static_cast<int64_t>(0x55555555FFFFFFCCL), t.r10);  // lb, sw.

    // sh with unsigned and signed data.
    CHECK_EQ(static_cast<int64_t>(0x5555555555552211L), t.r11);  // lw, sh.
    CHECK_EQ(static_cast<int64_t>(0x555555555555BBCCL), t.r12);  // lw, sh.
  } else {
    // Unsigned data, 32 & 64
    CHECK_EQ(static_cast<int64_t>(0x4433221155555555L), t.r1);  // lw, sw.
    CHECK_EQ(static_cast<int64_t>(0x0000000044332211L), t.r2);  // sd.

    // Signed data, 32 & 64.
    CHECK_EQ(static_cast<int64_t>(0x99AABBCC55555555L), t.r3);  // lw, sw.
    CHECK_EQ(static_cast<int64_t>(0xFFFFFFFF99AABBCCL), t.r4);  // sd.

    // Signed data, 32 & 64.
    CHECK_EQ(static_cast<int64_t>(0x99AABBCC55555555L), t.r5);  // lwu, sw.
    CHECK_EQ(static_cast<int64_t>(0x0000000099AABBCCL), t.r6);  // sd.

    // lh with unsigned and signed data.
    CHECK_EQ(static_cast<int64_t>(0x0000443355555555L), t.r7);  // lh, sw.
    CHECK_EQ(static_cast<int64_t>(0xFFFF99AA55555555L), t.r8);  // lh, sw.

    // lhu with signed data.
    CHECK_EQ(static_cast<int64_t>(0x000099AA55555555L), t.r9);  // lhu, sw.

    // lb with signed data.
    CHECK_EQ(static_cast<int64_t>(0xFFFFFF9955555555L), t.r10);  // lb, sw.

    // sh with unsigned and signed data.
    CHECK_EQ(static_cast<int64_t>(0x2211555555555555L), t.r11);  // lw, sh.
    CHECK_EQ(static_cast<int64_t>(0xBBCC555555555555L), t.r12);  // lw, sh.
  }
}


// ----------------------mips64r6 specific tests----------------------
TEST(seleqz_selnez) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct Test {
      int a;
      int b;
      int c;
      int d;
      double e;
      double f;
      double g;
      double h;
      float i;
      float j;
      float k;
      float l;
    };

    Test test;
    // Integer part of test.
    __ addiu(t1, zero_reg, 1);                      // t1 = 1
    __ seleqz(t3, t1, zero_reg);                    // t3 = 1
    __ Sw(t3, MemOperand(a0, offsetof(Test, a)));   // a = 1
    __ seleqz(t2, t1, t1);                          // t2 = 0
    __ Sw(t2, MemOperand(a0, offsetof(Test, b)));   // b = 0
    __ selnez(t3, t1, zero_reg);                    // t3 = 1;
    __ Sw(t3, MemOperand(a0, offsetof(Test, c)));   // c = 0
    __ selnez(t3, t1, t1);                          // t3 = 1
    __ Sw(t3, MemOperand(a0, offsetof(Test, d)));   // d = 1
    // Floating point part of test.
    __ Ldc1(f0, MemOperand(a0, offsetof(Test, e)));   // src
    __ Ldc1(f2, MemOperand(a0, offsetof(Test, f)));   // test
    __ Lwc1(f8, MemOperand(a0, offsetof(Test, i)));   // src
    __ Lwc1(f10, MemOperand(a0, offsetof(Test, j)));  // test
    __ seleqz_d(f4, f0, f2);
    __ selnez_d(f6, f0, f2);
    __ seleqz_s(f12, f8, f10);
    __ selnez_s(f14, f8, f10);
    __ Sdc1(f4, MemOperand(a0, offsetof(Test, g)));   // src
    __ Sdc1(f6, MemOperand(a0, offsetof(Test, h)));   // src
    __ Swc1(f12, MemOperand(a0, offsetof(Test, k)));  // src
    __ Swc1(f14, MemOperand(a0, offsetof(Test, l)));  // src
    __ jr(ra);
    __ nop();
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(1, test.a);
    CHECK_EQ(0, test.b);
    CHECK_EQ(0, test.c);
    CHECK_EQ(1, test.d);

    const int test_size = 3;
    const int input_size = 5;

    double inputs_D[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    double outputs_D[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    double tests_D[test_size*2] = {2.8, 2.9, -2.8, -2.9,
      18446744073709551616.0, 18446744073709555712.0};
    float inputs_S[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    float outputs_S[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    float tests_S[test_size*2] = {2.9, 2.8, -2.9, -2.8,
      18446744073709551616.0, 18446746272732807168.0};
    for (int j=0; j < test_size; j+=2) {
      for (int i=0; i < input_size; i++) {
        test.e = inputs_D[i];
        test.f = tests_D[j];
        test.i = inputs_S[i];
        test.j = tests_S[j];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(outputs_D[i], test.g);
        CHECK_EQ(0, test.h);
        CHECK_EQ(outputs_S[i], test.k);
        CHECK_EQ(0, test.l);

        test.f = tests_D[j+1];
        test.j = tests_S[j+1];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(0, test.g);
        CHECK_EQ(outputs_D[i], test.h);
        CHECK_EQ(0, test.k);
        CHECK_EQ(outputs_S[i], test.l);
      }
    }
  }
}



TEST(min_max) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      double a;
      double b;
      double c;
      double d;
      float e;
      float f;
      float g;
      float h;
    };

    TestFloat test;
    const double dnan = std::numeric_limits<double>::quiet_NaN();
    const double dinf = std::numeric_limits<double>::infinity();
    const double dminf = -std::numeric_limits<double>::infinity();
    const float fnan = std::numeric_limits<float>::quiet_NaN();
    const float finf = std::numeric_limits<float>::infinity();
    const float fminf = std::numeric_limits<float>::infinity();
    const int kTableLength = 13;
    double inputsa[kTableLength] = {2.0,  3.0,  dnan, 3.0,   -0.0, 0.0, dinf,
                                    dnan, 42.0, dinf, dminf, dinf, dnan};
    double inputsb[kTableLength] = {3.0,  2.0,  3.0,  dnan, 0.0,   -0.0, dnan,
                                    dinf, dinf, 42.0, dinf, dminf, dnan};
    double outputsdmin[kTableLength] = {2.0,   2.0,   3.0,  3.0,  -0.0,
                                        -0.0,  dinf,  dinf, 42.0, 42.0,
                                        dminf, dminf, dnan};
    double outputsdmax[kTableLength] = {3.0,  3.0,  3.0,  3.0,  0.0,  0.0, dinf,
                                        dinf, dinf, dinf, dinf, dinf, dnan};

    float inputse[kTableLength] = {2.0,  3.0,  fnan, 3.0,   -0.0, 0.0, finf,
                                   fnan, 42.0, finf, fminf, finf, fnan};
    float inputsf[kTableLength] = {3.0,  2.0,  3.0,  fnan, 0.0,   -0.0, fnan,
                                   finf, finf, 42.0, finf, fminf, fnan};
    float outputsfmin[kTableLength] = {2.0,   2.0,   3.0,  3.0,  -0.0,
                                       -0.0,  finf,  finf, 42.0, 42.0,
                                       fminf, fminf, fnan};
    float outputsfmax[kTableLength] = {3.0,  3.0,  3.0,  3.0,  0.0,  0.0, finf,
                                       finf, finf, finf, finf, finf, fnan};

    __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
    __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, b)));
    __ Lwc1(f2, MemOperand(a0, offsetof(TestFloat, e)));
    __ Lwc1(f6, MemOperand(a0, offsetof(TestFloat, f)));
    __ min_d(f10, f4, f8);
    __ max_d(f12, f4, f8);
    __ min_s(f14, f2, f6);
    __ max_s(f16, f2, f6);
    __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, c)));
    __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, d)));
    __ Swc1(f14, MemOperand(a0, offsetof(TestFloat, g)));
    __ Swc1(f16, MemOperand(a0, offsetof(TestFloat, h)));
    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 4; i < kTableLength; i++) {
      test.a = inputsa[i];
      test.b = inputsb[i];
      test.e = inputse[i];
      test.f = inputsf[i];

      f.Call(&test, 0, 0, 0, 0);

      CHECK_EQ(0, memcmp(&test.c, &outputsdmin[i], sizeof(test.c)));
      CHECK_EQ(0, memcmp(&test.d, &outputsdmax[i], sizeof(test.d)));
      CHECK_EQ(0, memcmp(&test.g, &outputsfmin[i], sizeof(test.g)));
      CHECK_EQ(0, memcmp(&test.h, &outputsfmax[i], sizeof(test.h)));
    }
  }
}


TEST(rint_d)  {
  if (kArchVariant == kMips64r6) {
    const int kTableLength = 30;
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      double a;
      double b;
      int fcsr;
    };

    TestFloat test;
    double inputs[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E+308, 6.27463370218383111104242366943E-307,
      309485009821345068724781056.89,
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    double outputs_RN[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    double outputs_RZ[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    double outputs_RP[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 1,
      309485009821345068724781057.0,
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    double outputs_RM[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E147,
      1.7976931348623157E308, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    int fcsr_inputs[4] =
      {kRoundToNearest, kRoundToZero, kRoundToPlusInf, kRoundToMinusInf};
    double* outputs[4] = {outputs_RN, outputs_RZ, outputs_RP, outputs_RM};
    __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
    __ Lw(t0, MemOperand(a0, offsetof(TestFloat, fcsr)));
    __ ctc1(t0, FCSR);
    __ rint_d(f8, f4);
    __ Sdc1(f8, MemOperand(a0, offsetof(TestFloat, b)));
    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);

    for (int j = 0; j < 4; j++) {
      test.fcsr = fcsr_inputs[j];
      for (int i = 0; i < kTableLength; i++) {
        test.a = inputs[i];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.b, outputs[j][i]);
      }
    }
  }
}


TEST(sel) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct Test {
      double dd;
      double ds;
      double dt;
      float fd;
      float fs;
      float ft;
    };

    Test test;
    __ Ldc1(f0, MemOperand(a0, offsetof(Test, dd)));   // test
    __ Ldc1(f2, MemOperand(a0, offsetof(Test, ds)));   // src1
    __ Ldc1(f4, MemOperand(a0, offsetof(Test, dt)));   // src2
    __ Lwc1(f6, MemOperand(a0, offsetof(Test, fd)));   // test
    __ Lwc1(f8, MemOperand(a0, offsetof(Test, fs)));   // src1
    __ Lwc1(f10, MemOperand(a0, offsetof(Test, ft)));  // src2
    __ sel_d(f0, f2, f4);
    __ sel_s(f6, f8, f10);
    __ Sdc1(f0, MemOperand(a0, offsetof(Test, dd)));
    __ Swc1(f6, MemOperand(a0, offsetof(Test, fd)));
    __ jr(ra);
    __ nop();
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);

    const int test_size = 3;
    const int input_size = 5;

    double inputs_dt[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    double inputs_ds[input_size] = {0.1, 69.88, -91.325,
      18446744073709551625.0, -18446744073709551625.0};
    float inputs_ft[input_size] = {0.0, 65.2, -70.32,
      18446744073709551621.0, -18446744073709551621.0};
    float inputs_fs[input_size] = {0.1, 69.88, -91.325,
      18446744073709551625.0, -18446744073709551625.0};
    double tests_D[test_size*2] = {2.8, 2.9, -2.8, -2.9,
      18446744073709551616.0, 18446744073709555712.0};
    float tests_S[test_size*2] = {2.9, 2.8, -2.9, -2.8,
      18446744073709551616.0, 18446746272732807168.0};
    for (int j=0; j < test_size; j+=2) {
      for (int i=0; i < input_size; i++) {
        test.dt = inputs_dt[i];
        test.dd = tests_D[j];
        test.ds = inputs_ds[i];
        test.ft = inputs_ft[i];
        test.fd = tests_S[j];
        test.fs = inputs_fs[i];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.dd, inputs_ds[i]);
        CHECK_EQ(test.fd, inputs_fs[i]);

        test.dd = tests_D[j+1];
        test.fd = tests_S[j+1];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.dd, inputs_dt[i]);
        CHECK_EQ(test.fd, inputs_ft[i]);
      }
    }
  }
}


TEST(rint_s)  {
  if (kArchVariant == kMips64r6) {
    const int kTableLength = 30;
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      float a;
      float b;
      int fcsr;
    };

    TestFloat test;
    float inputs[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E+38, 6.27463370218383111104242366943E-37,
      309485009821345068724781056.89,
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    float outputs_RN[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    float outputs_RZ[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    float outputs_RP[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 1,
      309485009821345068724781057.0,
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    float outputs_RM[kTableLength] = {18446744073709551617.0,
      4503599627370496.0, -4503599627370496.0,
      1.26782468584154733584017312973E30, 1.44860108245951772690707170478E37,
      1.7976931348623157E38, 0,
      309485009821345068724781057.0,
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      37778931862957161709568.0, 37778931862957161709569.0,
      37778931862957161709580.0, 37778931862957161709581.0,
      37778931862957161709582.0, 37778931862957161709583.0,
      37778931862957161709584.0, 37778931862957161709585.0,
      37778931862957161709586.0, 37778931862957161709587.0};
    int fcsr_inputs[4] =
      {kRoundToNearest, kRoundToZero, kRoundToPlusInf, kRoundToMinusInf};
    float* outputs[4] = {outputs_RN, outputs_RZ, outputs_RP, outputs_RM};
    __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
    __ Lw(t0, MemOperand(a0, offsetof(TestFloat, fcsr)));
    __ cfc1(t1, FCSR);
    __ ctc1(t0, FCSR);
    __ rint_s(f8, f4);
    __ Swc1(f8, MemOperand(a0, offsetof(TestFloat, b)));
    __ ctc1(t1, FCSR);
    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);

    for (int j = 0; j < 4; j++) {
      test.fcsr = fcsr_inputs[j];
      for (int i = 0; i < kTableLength; i++) {
        test.a = inputs[i];
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.b, outputs[j][i]);
      }
    }
  }
}


TEST(mina_maxa) {
  if (kArchVariant == kMips64r6) {
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
      double resd;
      double resd1;
      float c;
      float d;
      float resf;
      float resf1;
    };

    TestFloat test;
    double inputsa[kTableLength] = {
        5.3,  4.8, 6.1,  9.8, 9.8,  9.8,  -10.0, -8.9, -9.8,  -10.0, -8.9, -9.8,
        dnan, 3.0, -0.0, 0.0, dinf, dnan, 42.0,  dinf, dminf, dinf,  dnan};
    double inputsb[kTableLength] = {
        4.8, 5.3,  6.1, -10.0, -8.9, -9.8, 9.8,  9.8,  9.8,  -9.8,  -11.2, -9.8,
        3.0, dnan, 0.0, -0.0,  dnan, dinf, dinf, 42.0, dinf, dminf, dnan};
    double resd[kTableLength] = {
        4.8, 4.8, 6.1,  9.8,  -8.9, -9.8, 9.8,  -8.9, -9.8,  -9.8,  -8.9, -9.8,
        3.0, 3.0, -0.0, -0.0, dinf, dinf, 42.0, 42.0, dminf, dminf, dnan};
    double resd1[kTableLength] = {
        5.3, 5.3, 6.1, -10.0, 9.8,  9.8,  -10.0, 9.8,  9.8,  -10.0, -11.2, -9.8,
        3.0, 3.0, 0.0, 0.0,   dinf, dinf, dinf,  dinf, dinf, dinf,  dnan};
    float inputsc[kTableLength] = {
        5.3,  4.8, 6.1,  9.8, 9.8,  9.8,  -10.0, -8.9, -9.8,  -10.0, -8.9, -9.8,
        fnan, 3.0, -0.0, 0.0, finf, fnan, 42.0,  finf, fminf, finf,  fnan};
    float inputsd[kTableLength] = {4.8,  5.3,  6.1,  -10.0, -8.9,  -9.8,
                                   9.8,  9.8,  9.8,  -9.8,  -11.2, -9.8,
                                   3.0,  fnan, -0.0, 0.0,   fnan,  finf,
                                   finf, 42.0, finf, fminf, fnan};
    float resf[kTableLength] = {
        4.8, 4.8, 6.1,  9.8,  -8.9, -9.8, 9.8,  -8.9, -9.8,  -9.8,  -8.9, -9.8,
        3.0, 3.0, -0.0, -0.0, finf, finf, 42.0, 42.0, fminf, fminf, fnan};
    float resf1[kTableLength] = {
        5.3, 5.3, 6.1, -10.0, 9.8,  9.8,  -10.0, 9.8,  9.8,  -10.0, -11.2, -9.8,
        3.0, 3.0, 0.0, 0.0,   finf, finf, finf,  finf, finf, finf,  fnan};

    __ Ldc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
    __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, b)));
    __ Lwc1(f8, MemOperand(a0, offsetof(TestFloat, c)));
    __ Lwc1(f10, MemOperand(a0, offsetof(TestFloat, d)));
    __ mina_d(f6, f2, f4);
    __ mina_s(f12, f8, f10);
    __ maxa_d(f14, f2, f4);
    __ maxa_s(f16, f8, f10);
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, resf)));
    __ Sdc1(f6, MemOperand(a0, offsetof(TestFloat, resd)));
    __ Swc1(f16, MemOperand(a0, offsetof(TestFloat, resf1)));
    __ Sdc1(f14, MemOperand(a0, offsetof(TestFloat, resd1)));
    __ jr(ra);
    __ nop();

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
        CHECK_EQ(test.resd, resd[i]);
        CHECK_EQ(test.resf, resf[i]);
        CHECK_EQ(test.resd1, resd1[i]);
        CHECK_EQ(test.resf1, resf1[i]);
      } else {
        CHECK(std::isnan(test.resd));
        CHECK(std::isnan(test.resf));
        CHECK(std::isnan(test.resd1));
        CHECK(std::isnan(test.resf1));
      }
    }
  }
}



// ----------------------mips64r2 specific tests----------------------
TEST(trunc_l) {
  if (kArchVariant == kMips64r2) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    const double dFPU64InvalidResult = static_cast<double>(kFPU64InvalidResult);
    struct Test {
      uint32_t isNaN2008;
      double a;
      float b;
      int64_t c;  // a trunc result
      int64_t d;  // b trunc result
    };
    const int kTableLength = 15;
    double inputs_D[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
        };
    float inputs_S[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
        };
    double outputs[kTableLength] = {
        2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
        -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
        2147483648.0, dFPU64InvalidResult,
        dFPU64InvalidResult};
    double outputsNaN2008[kTableLength] = {
        2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
        -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
        2147483648.0, dFPU64InvalidResult,
        dFPU64InvalidResult};

    __ cfc1(t1, FCSR);
    __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
    __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
    __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
    __ trunc_l_d(f8, f4);
    __ trunc_l_s(f10, f6);
    __ Sdc1(f8, MemOperand(a0, offsetof(Test, c)));
    __ Sdc1(f10, MemOperand(a0, offsetof(Test, d)));
    __ jr(ra);
    __ nop();
    Test test;
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_D[i];
      test.b = inputs_S[i];
      f.Call(&test, 0, 0, 0, 0);
      if ((test.isNaN2008 & kFCSRNaN2008FlagMask) &&
              kArchVariant == kMips64r6) {
        CHECK_EQ(test.c, outputsNaN2008[i]);
      } else {
        CHECK_EQ(test.c, outputs[i]);
      }
      CHECK_EQ(test.d, test.c);
    }
  }
}


TEST(movz_movn) {
  if (kArchVariant == kMips64r2) {
    const int kTableLength = 4;
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      int64_t rt;
      double a;
      double b;
      double bold;
      double b1;
      double bold1;
      float c;
      float d;
      float dold;
      float d1;
      float dold1;
    };

    TestFloat test;
    double inputs_D[kTableLength] = {
      5.3, -5.3, 5.3, -2.9
    };
    double inputs_S[kTableLength] = {
      4.8, 4.8, -4.8, -0.29
    };

    float outputs_S[kTableLength] = {
      4.8, 4.8, -4.8, -0.29
    };
    double outputs_D[kTableLength] = {
      5.3, -5.3, 5.3, -2.9
    };

    __ Ldc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
    __ Lwc1(f6, MemOperand(a0, offsetof(TestFloat, c)));
    __ Ld(t0, MemOperand(a0, offsetof(TestFloat, rt)));
    __ Move(f12, 0.0);
    __ Move(f10, 0.0);
    __ Move(f16, 0.0);
    __ Move(f14, 0.0);
    __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, bold)));
    __ Swc1(f10, MemOperand(a0, offsetof(TestFloat, dold)));
    __ Sdc1(f16, MemOperand(a0, offsetof(TestFloat, bold1)));
    __ Swc1(f14, MemOperand(a0, offsetof(TestFloat, dold1)));
    __ movz_s(f10, f6, t0);
    __ movz_d(f12, f2, t0);
    __ movn_s(f14, f6, t0);
    __ movn_d(f16, f2, t0);
    __ Swc1(f10, MemOperand(a0, offsetof(TestFloat, d)));
    __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, b)));
    __ Swc1(f14, MemOperand(a0, offsetof(TestFloat, d1)));
    __ Sdc1(f16, MemOperand(a0, offsetof(TestFloat, b1)));
    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_D[i];
      test.c = inputs_S[i];

      test.rt = 1;
      f.Call(&test, 0, 0, 0, 0);
      CHECK_EQ(test.b, test.bold);
      CHECK_EQ(test.d, test.dold);
      CHECK_EQ(test.b1, outputs_D[i]);
      CHECK_EQ(test.d1, outputs_S[i]);

      test.rt = 0;
      f.Call(&test, 0, 0, 0, 0);
      CHECK_EQ(test.b, outputs_D[i]);
      CHECK_EQ(test.d, outputs_S[i]);
      CHECK_EQ(test.b1, test.bold1);
      CHECK_EQ(test.d1, test.dold1);
    }
  }
}


TEST(movt_movd) {
  if (kArchVariant == kMips64r2) {
    const int kTableLength = 4;
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    struct TestFloat {
      double srcd;
      double dstd;
      double dstdold;
      double dstd1;
      double dstdold1;
      float srcf;
      float dstf;
      float dstfold;
      float dstf1;
      float dstfold1;
      int32_t cc;
      int32_t fcsr;
    };

    TestFloat test;
    double inputs_D[kTableLength] = {
      5.3, -5.3, 20.8, -2.9
    };
    double inputs_S[kTableLength] = {
      4.88, 4.8, -4.8, -0.29
    };

    float outputs_S[kTableLength] = {
      4.88, 4.8, -4.8, -0.29
    };
    double outputs_D[kTableLength] = {
      5.3, -5.3, 20.8, -2.9
    };
    int condition_flags[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    for (int i = 0; i < kTableLength; i++) {
      test.srcd = inputs_D[i];
      test.srcf = inputs_S[i];

      for (int j = 0; j< 8; j++) {
        test.cc = condition_flags[j];
        if (test.cc == 0) {
          test.fcsr = 1 << 23;
        } else {
          test.fcsr = 1 << (24+condition_flags[j]);
        }
        HandleScope scope(isolate);
        MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
        __ Ldc1(f2, MemOperand(a0, offsetof(TestFloat, srcd)));
        __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, srcf)));
        __ Lw(t1, MemOperand(a0, offsetof(TestFloat, fcsr)));
        __ cfc1(t0, FCSR);
        __ ctc1(t1, FCSR);
        __ li(t2, 0x0l);
        __ mtc1(t2, f12);
        __ mtc1(t2, f10);
        __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, dstdold)));
        __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, dstfold)));
        __ movt_s(f12, f4, test.cc);
        __ movt_d(f10, f2, test.cc);
        __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, dstf)));
        __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, dstd)));
        __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, dstdold1)));
        __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, dstfold1)));
        __ movf_s(f12, f4, test.cc);
        __ movf_d(f10, f2, test.cc);
        __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, dstf1)));
        __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, dstd1)));
        __ ctc1(t0, FCSR);
        __ jr(ra);
        __ nop();

        CodeDesc desc;
        assm.GetCode(isolate, &desc);
        Handle<Code> code =
            Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
        auto f = GeneratedCode<F3>::FromCode(isolate, *code);

        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.dstf, outputs_S[i]);
        CHECK_EQ(test.dstd, outputs_D[i]);
        CHECK_EQ(test.dstf1, test.dstfold1);
        CHECK_EQ(test.dstd1, test.dstdold1);
        test.fcsr = 0;
        f.Call(&test, 0, 0, 0, 0);
        CHECK_EQ(test.dstf, test.dstfold);
        CHECK_EQ(test.dstd, test.dstdold);
        CHECK_EQ(test.dstf1, outputs_S[i]);
        CHECK_EQ(test.dstd1, outputs_D[i]);
      }
    }
  }
}



// ----------------------tests for all archs--------------------------
TEST(cvt_w_d) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double a;
    int32_t b;
    int fcsr;
  };
  const int kTableLength = 24;
  double inputs[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483637.0, 2147483638.0, 2147483639.0,
      2147483640.0, 2147483641.0, 2147483642.0,
      2147483643.0, 2147483644.0, 2147483645.0,
      2147483646.0, 2147483647.0, 2147483653.0
      };
  double outputs_RN[kTableLength] = {
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      2147483637.0, 2147483638.0, 2147483639.0,
      2147483640.0, 2147483641.0, 2147483642.0,
      2147483643.0, 2147483644.0, 2147483645.0,
      2147483646.0, 2147483647.0, kFPUInvalidResult};
  double outputs_RZ[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      2147483637.0, 2147483638.0, 2147483639.0,
      2147483640.0, 2147483641.0, 2147483642.0,
      2147483643.0, 2147483644.0, 2147483645.0,
      2147483646.0, 2147483647.0, kFPUInvalidResult};
  double outputs_RP[kTableLength] = {
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      2147483637.0, 2147483638.0, 2147483639.0,
      2147483640.0, 2147483641.0, 2147483642.0,
      2147483643.0, 2147483644.0, 2147483645.0,
      2147483646.0, 2147483647.0, kFPUInvalidResult};
  double outputs_RM[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      2147483637.0, 2147483638.0, 2147483639.0,
      2147483640.0, 2147483641.0, 2147483642.0,
      2147483643.0, 2147483644.0, 2147483645.0,
      2147483646.0, 2147483647.0, kFPUInvalidResult};
  int fcsr_inputs[4] =
      {kRoundToNearest, kRoundToZero, kRoundToPlusInf, kRoundToMinusInf};
  double* outputs[4] = {outputs_RN, outputs_RZ, outputs_RP, outputs_RM};
  __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
  __ Lw(t0, MemOperand(a0, offsetof(Test, fcsr)));
  __ cfc1(t1, FCSR);
  __ ctc1(t0, FCSR);
  __ cvt_w_d(f8, f4);
  __ Swc1(f8, MemOperand(a0, offsetof(Test, b)));
  __ ctc1(t1, FCSR);
  __ jr(ra);
  __ nop();
  Test test;
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int j = 0; j < 4; j++) {
    test.fcsr = fcsr_inputs[j];
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs[i];
      f.Call(&test, 0, 0, 0, 0);
      CHECK_EQ(test.b, outputs[j][i]);
    }
  }
}


TEST(trunc_w) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    uint32_t isNaN2008;
    double a;
    float b;
    int32_t c;  // a trunc result
    int32_t d;  // b trunc result
  };
  const int kTableLength = 15;
  double inputs_D[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  float inputs_S[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity()
      };
  double outputs[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, kFPUInvalidResult,
      kFPUInvalidResult};
  double outputsNaN2008[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult,
      0,
      kFPUInvalidResult};

  __ cfc1(t1, FCSR);
  __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
  __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
  __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
  __ trunc_w_d(f8, f4);
  __ trunc_w_s(f10, f6);
  __ Swc1(f8, MemOperand(a0, offsetof(Test, c)));
  __ Swc1(f10, MemOperand(a0, offsetof(Test, d)));
  __ jr(ra);
  __ nop();
  Test test;
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.b = inputs_S[i];
    f.Call(&test, 0, 0, 0, 0);
    if ((test.isNaN2008 & kFCSRNaN2008FlagMask) && kArchVariant == kMips64r6) {
      CHECK_EQ(test.c, outputsNaN2008[i]);
    } else {
      CHECK_EQ(test.c, outputs[i]);
    }
    CHECK_EQ(test.d, test.c);
  }
}


TEST(round_w) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    uint32_t isNaN2008;
    double a;
    float b;
    int32_t c;  // a trunc result
    int32_t d;  // b trunc result
  };
  const int kTableLength = 15;
  double inputs_D[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  float inputs_S[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity()
      };
  double outputs[kTableLength] = {
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      kFPUInvalidResult, kFPUInvalidResult,
      kFPUInvalidResult};
  double outputsNaN2008[kTableLength] = {
      2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
      -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
      kFPUInvalidResult, 0,
      kFPUInvalidResult};

  __ cfc1(t1, FCSR);
  __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
  __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
  __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
  __ round_w_d(f8, f4);
  __ round_w_s(f10, f6);
  __ Swc1(f8, MemOperand(a0, offsetof(Test, c)));
  __ Swc1(f10, MemOperand(a0, offsetof(Test, d)));
  __ jr(ra);
  __ nop();
  Test test;
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.b = inputs_S[i];
    f.Call(&test, 0, 0, 0, 0);
    if ((test.isNaN2008 & kFCSRNaN2008FlagMask) && kArchVariant == kMips64r6) {
      CHECK_EQ(test.c, outputsNaN2008[i]);
    } else {
      CHECK_EQ(test.c, outputs[i]);
    }
    CHECK_EQ(test.d, test.c);
  }
}


TEST(round_l) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    const double dFPU64InvalidResult = static_cast<double>(kFPU64InvalidResult);
    struct Test {
      uint32_t isNaN2008;
      double a;
      float b;
      int64_t c;
      int64_t d;
    };
    const int kTableLength = 15;
    double inputs_D[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
        };
    float inputs_S[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
        };
    double outputs[kTableLength] = {
        2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
        -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
        2147483648.0, dFPU64InvalidResult,
        dFPU64InvalidResult};
    double outputsNaN2008[kTableLength] = {
        2.0, 3.0, 2.0, 3.0, 4.0, 4.0,
        -2.0, -3.0, -2.0, -3.0, -4.0, -4.0,
        2147483648.0,
        0,
        dFPU64InvalidResult};

    __ cfc1(t1, FCSR);
    __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
    __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
    __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
    __ round_l_d(f8, f4);
    __ round_l_s(f10, f6);
    __ Sdc1(f8, MemOperand(a0, offsetof(Test, c)));
    __ Sdc1(f10, MemOperand(a0, offsetof(Test, d)));
    __ jr(ra);
    __ nop();
    Test test;
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_D[i];
      test.b = inputs_S[i];
      f.Call(&test, 0, 0, 0, 0);
      if ((test.isNaN2008 & kFCSRNaN2008FlagMask) &&
              kArchVariant == kMips64r6) {
        CHECK_EQ(test.c, outputsNaN2008[i]);
      } else {
        CHECK_EQ(test.c, outputs[i]);
      }
      CHECK_EQ(test.d, test.c);
    }
}


TEST(sub) {
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
  __ Lwc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
  __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, b)));
  __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, c)));
  __ Ldc1(f10, MemOperand(a0, offsetof(TestFloat, d)));
  __ sub_s(f6, f2, f4);
  __ sub_d(f12, f8, f10);
  __ Swc1(f6, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ jr(ra);
  __ nop();

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


TEST(sqrt_rsqrt_recip) {
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
    float resultS;
    float resultS1;
    float resultS2;
    double c;
    double resultD;
    double resultD1;
    double resultD2;
  };
  TestFloat test;

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

  __ Lwc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
  __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, c)));
  __ sqrt_s(f6, f2);
  __ sqrt_d(f12, f8);
  __ rsqrt_d(f14, f8);
  __ rsqrt_s(f16, f2);
  __ recip_d(f18, f8);
  __ recip_s(f4, f2);
  __ Swc1(f6, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ Swc1(f16, MemOperand(a0, offsetof(TestFloat, resultS1)));
  __ Sdc1(f14, MemOperand(a0, offsetof(TestFloat, resultD1)));
  __ Swc1(f4, MemOperand(a0, offsetof(TestFloat, resultS2)));
  __ Sdc1(f18, MemOperand(a0, offsetof(TestFloat, resultD2)));
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  for (int i = 0; i < kTableLength; i++) {
    float f1;
    double d1;
    test.a = inputs_S[i];
    test.c = inputs_D[i];

    f.Call(&test, 0, 0, 0, 0);

    CHECK_EQ(test.resultS, outputs_S[i]);
    CHECK_EQ(test.resultD, outputs_D[i]);

    if (i != 0) {
      f1 = test.resultS1 - 1.0F/outputs_S[i];
      f1 = (f1 < 0) ? f1 : -f1;
      CHECK(f1 <= deltaFloat);
      d1 = test.resultD1 - 1.0L/outputs_D[i];
      d1 = (d1 < 0) ? d1 : -d1;
      CHECK(d1 <= deltaDouble);
      f1 = test.resultS2 - 1.0F/inputs_S[i];
      f1 = (f1 < 0) ? f1 : -f1;
      CHECK(f1 <= deltaFloat);
      d1 = test.resultD2 - 1.0L/inputs_D[i];
      d1 = (d1 < 0) ? d1 : -d1;
      CHECK(d1 <= deltaDouble);
    } else {
      CHECK_EQ(test.resultS1, 1.0F/outputs_S[i]);
      CHECK_EQ(test.resultD1, 1.0L/outputs_D[i]);
      CHECK_EQ(test.resultS2, 1.0F/inputs_S[i]);
      CHECK_EQ(test.resultD2, 1.0L/inputs_D[i]);
    }
  }
}


TEST(neg) {
  const int kTableLength = 2;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float a;
    float resultS;
    double c;
    double resultD;
  };

  TestFloat test;
  double inputs_D[kTableLength] = {
    4.0, -2.0
  };

  double outputs_D[kTableLength] = {
    -4.0, 2.0
  };
  float inputs_S[kTableLength] = {
    4.0, -2.0
  };

  float outputs_S[kTableLength] = {
    -4.0, 2.0
  };
  __ Lwc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
  __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, c)));
  __ neg_s(f6, f2);
  __ neg_d(f12, f8);
  __ Swc1(f6, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_S[i];
    test.c = inputs_D[i];
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.resultS, outputs_S[i]);
    CHECK_EQ(test.resultD, outputs_D[i]);
  }
}



TEST(mul) {
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

  __ Lwc1(f2, MemOperand(a0, offsetof(TestFloat, a)));
  __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, b)));
  __ Ldc1(f6, MemOperand(a0, offsetof(TestFloat, c)));
  __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, d)));
  __ mul_s(f10, f2, f4);
  __ mul_d(f12, f6, f8);
  __ Swc1(f10, MemOperand(a0, offsetof(TestFloat, resultS)));
  __ Sdc1(f12, MemOperand(a0, offsetof(TestFloat, resultD)));
  __ jr(ra);
  __ nop();

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
    CHECK_EQ(test.resultS, inputfs_S[i]*inputft_S[i]);
    CHECK_EQ(test.resultD, inputfs_D[i]*inputft_D[i]);
  }
}


TEST(mov) {
  const int kTableLength = 4;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    double b;
    float c;
    float d;
  };

  TestFloat test;
  double inputs_D[kTableLength] = {
    5.3, -5.3, 5.3, -2.9
  };
  double inputs_S[kTableLength] = {
    4.8, 4.8, -4.8, -0.29
  };

  float outputs_S[kTableLength] = {
    4.8, 4.8, -4.8, -0.29
  };
  double outputs_D[kTableLength] = {
    5.3, -5.3, 5.3, -2.9
  };

  __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
  __ Lwc1(f6, MemOperand(a0, offsetof(TestFloat, c)));
  __ mov_s(f8, f6);
  __ mov_d(f10, f4);
  __ Swc1(f8, MemOperand(a0, offsetof(TestFloat, d)));
  __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, b)));
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.c = inputs_S[i];

    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.b, outputs_D[i]);
    CHECK_EQ(test.d, outputs_S[i]);
  }
}


TEST(floor_w) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    uint32_t isNaN2008;
    double a;
    float b;
    int32_t c;  // a floor result
    int32_t d;  // b floor result
  };
  const int kTableLength = 15;
  double inputs_D[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  float inputs_S[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity()
      };
  double outputs[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      kFPUInvalidResult, kFPUInvalidResult,
      kFPUInvalidResult};
  double outputsNaN2008[kTableLength] = {
      2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
      -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
      kFPUInvalidResult,
      0,
      kFPUInvalidResult};

  __ cfc1(t1, FCSR);
  __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
  __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
  __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
  __ floor_w_d(f8, f4);
  __ floor_w_s(f10, f6);
  __ Swc1(f8, MemOperand(a0, offsetof(Test, c)));
  __ Swc1(f10, MemOperand(a0, offsetof(Test, d)));
  __ jr(ra);
  __ nop();
  Test test;
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.b = inputs_S[i];
    f.Call(&test, 0, 0, 0, 0);
    if ((test.isNaN2008 & kFCSRNaN2008FlagMask) && kArchVariant == kMips64r6) {
      CHECK_EQ(test.c, outputsNaN2008[i]);
    } else {
      CHECK_EQ(test.c, outputs[i]);
    }
    CHECK_EQ(test.d, test.c);
  }
}


TEST(floor_l) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    const double dFPU64InvalidResult = static_cast<double>(kFPU64InvalidResult);
    struct Test {
      uint32_t isNaN2008;
      double a;
      float b;
      int64_t c;
      int64_t d;
    };
    const int kTableLength = 15;
    double inputs_D[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
        };
    float inputs_S[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
        };
    double outputs[kTableLength] = {
        2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
        -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
        2147483648.0, dFPU64InvalidResult,
        dFPU64InvalidResult};
    double outputsNaN2008[kTableLength] = {
        2.0, 2.0, 2.0, 3.0, 3.0, 3.0,
        -3.0, -3.0, -3.0, -4.0, -4.0, -4.0,
        2147483648.0,
        0,
        dFPU64InvalidResult};

    __ cfc1(t1, FCSR);
    __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
    __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
    __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
    __ floor_l_d(f8, f4);
    __ floor_l_s(f10, f6);
    __ Sdc1(f8, MemOperand(a0, offsetof(Test, c)));
    __ Sdc1(f10, MemOperand(a0, offsetof(Test, d)));
    __ jr(ra);
    __ nop();
    Test test;
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_D[i];
      test.b = inputs_S[i];
      f.Call(&test, 0, 0, 0, 0);
      if ((test.isNaN2008 & kFCSRNaN2008FlagMask) &&
              kArchVariant == kMips64r6) {
        CHECK_EQ(test.c, outputsNaN2008[i]);
      } else {
        CHECK_EQ(test.c, outputs[i]);
      }
      CHECK_EQ(test.d, test.c);
    }
}


TEST(ceil_w) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    uint32_t isNaN2008;
    double a;
    float b;
    int32_t c;  // a floor result
    int32_t d;  // b floor result
  };
  const int kTableLength = 15;
  double inputs_D[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<double>::quiet_NaN(),
      std::numeric_limits<double>::infinity()
      };
  float inputs_S[kTableLength] = {
      2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
      -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
      2147483648.0,
      std::numeric_limits<float>::quiet_NaN(),
      std::numeric_limits<float>::infinity()
      };
  double outputs[kTableLength] = {
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult, kFPUInvalidResult,
      kFPUInvalidResult};
  double outputsNaN2008[kTableLength] = {
      3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
      -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
      kFPUInvalidResult,
      0,
      kFPUInvalidResult};

  __ cfc1(t1, FCSR);
  __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
  __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
  __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
  __ ceil_w_d(f8, f4);
  __ ceil_w_s(f10, f6);
  __ Swc1(f8, MemOperand(a0, offsetof(Test, c)));
  __ Swc1(f10, MemOperand(a0, offsetof(Test, d)));
  __ jr(ra);
  __ nop();
  Test test;
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  for (int i = 0; i < kTableLength; i++) {
    test.a = inputs_D[i];
    test.b = inputs_S[i];
    f.Call(&test, 0, 0, 0, 0);
    if ((test.isNaN2008 & kFCSRNaN2008FlagMask) && kArchVariant == kMips64r6) {
      CHECK_EQ(test.c, outputsNaN2008[i]);
    } else {
      CHECK_EQ(test.c, outputs[i]);
    }
    CHECK_EQ(test.d, test.c);
  }
}


TEST(ceil_l) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    const double dFPU64InvalidResult = static_cast<double>(kFPU64InvalidResult);
    struct Test {
      uint32_t isNaN2008;
      double a;
      float b;
      int64_t c;
      int64_t d;
    };
    const int kTableLength = 15;
    double inputs_D[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity()
        };
    float inputs_S[kTableLength] = {
        2.1, 2.6, 2.5, 3.1, 3.6, 3.5,
        -2.1, -2.6, -2.5, -3.1, -3.6, -3.5,
        2147483648.0,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
        };
    double outputs[kTableLength] = {
        3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
        -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
        2147483648.0, dFPU64InvalidResult,
        dFPU64InvalidResult};
    double outputsNaN2008[kTableLength] = {
        3.0, 3.0, 3.0, 4.0, 4.0, 4.0,
        -2.0, -2.0, -2.0, -3.0, -3.0, -3.0,
        2147483648.0,
        0,
        dFPU64InvalidResult};

    __ cfc1(t1, FCSR);
    __ Sw(t1, MemOperand(a0, offsetof(Test, isNaN2008)));
    __ Ldc1(f4, MemOperand(a0, offsetof(Test, a)));
    __ Lwc1(f6, MemOperand(a0, offsetof(Test, b)));
    __ ceil_l_d(f8, f4);
    __ ceil_l_s(f10, f6);
    __ Sdc1(f8, MemOperand(a0, offsetof(Test, c)));
    __ Sdc1(f10, MemOperand(a0, offsetof(Test, d)));
    __ jr(ra);
    __ nop();
    Test test;
    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    for (int i = 0; i < kTableLength; i++) {
      test.a = inputs_D[i];
      test.b = inputs_S[i];
      f.Call(&test, 0, 0, 0, 0);
      if ((test.isNaN2008 & kFCSRNaN2008FlagMask) &&
              kArchVariant == kMips64r6) {
        CHECK_EQ(test.c, outputsNaN2008[i]);
      } else {
        CHECK_EQ(test.c, outputs[i]);
      }
      CHECK_EQ(test.d, test.c);
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

  __ daddiu(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));
  __ Align(8);

  Label done;
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ nal();
    __ dsll(at, a0, 3);  // In delay slot.
    __ daddu(at, at, ra);
    __ Ld(at, MemOperand(at, 4 * kInstrSize));
    __ jr(at);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lui(v0, (values[i] >> 16) & 0xFFFF);
    __ ori(v0, v0, values[i] & 0xFFFF);
    __ b(&done);
    __ nop();
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ daddiu(sp, sp, 8);
  __ jr(ra);
  __ nop();

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
    CHECK_EQ(values[i], static_cast<int>(res));
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

  __ daddiu(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));

  Label done, dispatch;
  __ b(&dispatch);
  __ nop();

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ lui(v0, (values[i] >> 16) & 0xFFFF);
    __ ori(v0, v0, values[i] & 0xFFFF);
    __ b(&done);
    __ nop();
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ nal();
    __ dsll(at, a0, 3);  // In delay slot.
    __ daddu(at, at, ra);
    __ Ld(at, MemOperand(at, 4 * kInstrSize));
    __ jr(at);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ daddiu(sp, sp, 8);
  __ jr(ra);
  __ nop();

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

  __ daddiu(sp, sp, -8);
  __ Sd(ra, MemOperand(sp));

  Label done, dispatch;
  __ b(&dispatch);
  __ nop();


  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    obj = *values[i];
    imm64 = obj.ptr();
    __ lui(v0, (imm64 >> 32) & kImm16Mask);
    __ ori(v0, v0, (imm64 >> 16) & kImm16Mask);
    __ dsll(v0, v0, 16);
    __ ori(v0, v0, imm64 & kImm16Mask);
    __ b(&done);
    __ nop();
  }

  __ Align(8);
  __ bind(&dispatch);
  {
    __ BlockTrampolinePoolFor(kNumCases * 2 + 6);

    __ nal();
    __ dsll(at, a0, 3);  // In delay slot.
    __ daddu(at, at, ra);
    __ Ld(at, MemOperand(at, 4 * kInstrSize));
    __ jr(at);
    __ nop();
    for (int i = 0; i < kNumCases; ++i) {
      __ dd(&labels[i]);
    }
  }

  __ bind(&done);
  __ Ld(ra, MemOperand(sp));
  __ daddiu(sp, sp, 8);
  __ jr(ra);
  __ nop();

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


TEST(BITSWAP) {
  // Test BITSWAP
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);

    struct T {
      int64_t r1;
      int64_t r2;
      int64_t r3;
      int64_t r4;
      int64_t r5;
      int64_t r6;
    };
    T t;

    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    __ Ld(a4, MemOperand(a0, offsetof(T, r1)));
    __ nop();
    __ bitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r1)));

    __ Ld(a4, MemOperand(a0, offsetof(T, r2)));
    __ nop();
    __ bitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r2)));

    __ Ld(a4, MemOperand(a0, offsetof(T, r3)));
    __ nop();
    __ bitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r3)));

    __ Ld(a4, MemOperand(a0, offsetof(T, r4)));
    __ nop();
    __ bitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r4)));

    __ Ld(a4, MemOperand(a0, offsetof(T, r5)));
    __ nop();
    __ dbitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r5)));

    __ Ld(a4, MemOperand(a0, offsetof(T, r6)));
    __ nop();
    __ dbitswap(a6, a4);
    __ Sd(a6, MemOperand(a0, offsetof(T, r6)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    t.r1 = 0x00102100781A15C3;
    t.r2 = 0x001021008B71FCDE;
    t.r3 = 0xFF8017FF781A15C3;
    t.r4 = 0xFF8017FF8B71FCDE;
    t.r5 = 0x10C021098B71FCDE;
    t.r6 = 0xFB8017FF781A15C3;
    f.Call(&t, 0, 0, 0, 0);

    CHECK_EQ(static_cast<int64_t>(0x000000001E58A8C3L), t.r1);
    CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFD18E3F7BL), t.r2);
    CHECK_EQ(static_cast<int64_t>(0x000000001E58A8C3L), t.r3);
    CHECK_EQ(static_cast<int64_t>(0xFFFFFFFFD18E3F7BL), t.r4);
    CHECK_EQ(static_cast<int64_t>(0x08038490D18E3F7BL), t.r5);
    CHECK_EQ(static_cast<int64_t>(0xDF01E8FF1E58A8C3L), t.r6);
  }
}


TEST(class_fmt) {
  if (kArchVariant == kMips64r6) {
    // Test CLASS.fmt instruction.
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);

    struct T {
      double dSignalingNan;
      double dQuietNan;
      double dNegInf;
      double dNegNorm;
      double dNegSubnorm;
      double dNegZero;
      double dPosInf;
      double dPosNorm;
      double dPosSubnorm;
      double dPosZero;
      float  fSignalingNan;
      float  fQuietNan;
      float  fNegInf;
      float  fNegNorm;
      float  fNegSubnorm;
      float  fNegZero;
      float  fPosInf;
      float  fPosNorm;
      float  fPosSubnorm;
      float fPosZero;
    };
    T t;

    // Create a function that accepts &t, and loads, manipulates, and stores
    // the doubles t.a ... t.f.
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dSignalingNan)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dSignalingNan)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dQuietNan)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dQuietNan)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dNegInf)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dNegInf)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dNegNorm)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dNegNorm)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dNegSubnorm)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dNegSubnorm)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dNegZero)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dNegZero)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dPosInf)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dPosInf)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dPosNorm)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dPosNorm)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dPosSubnorm)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dPosSubnorm)));

    __ Ldc1(f4, MemOperand(a0, offsetof(T, dPosZero)));
    __ class_d(f6, f4);
    __ Sdc1(f6, MemOperand(a0, offsetof(T, dPosZero)));

    // Testing instruction CLASS.S
    __ Lwc1(f4, MemOperand(a0, offsetof(T, fSignalingNan)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fSignalingNan)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fQuietNan)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fQuietNan)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fNegInf)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fNegInf)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fNegNorm)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fNegNorm)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fNegSubnorm)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fNegSubnorm)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fNegZero)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fNegZero)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fPosInf)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fPosInf)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fPosNorm)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fPosNorm)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fPosSubnorm)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fPosSubnorm)));

    __ Lwc1(f4, MemOperand(a0, offsetof(T, fPosZero)));
    __ class_s(f6, f4);
    __ Swc1(f6, MemOperand(a0, offsetof(T, fPosZero)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);

    // Double test values.
    t.dSignalingNan =  std::numeric_limits<double>::signaling_NaN();
    t.dQuietNan = std::numeric_limits<double>::quiet_NaN();
    t.dNegInf       = -1.0 / 0.0;
    t.dNegNorm      = -5.0;
    t.dNegSubnorm   = -DBL_MIN / 2.0;
    t.dNegZero      = -0.0;
    t.dPosInf       = 2.0 / 0.0;
    t.dPosNorm      = 275.35;
    t.dPosSubnorm   = DBL_MIN / 2.0;
    t.dPosZero      = +0.0;
    // Float test values

    t.fSignalingNan = std::numeric_limits<float>::signaling_NaN();
    t.fQuietNan     = std::numeric_limits<float>::quiet_NaN();
    t.fNegInf       = -0.5/0.0;
    t.fNegNorm      = -FLT_MIN;
    t.fNegSubnorm   = -FLT_MIN / 1.5;
    t.fNegZero      = -0.0;
    t.fPosInf       = 100000.0 / 0.0;
    t.fPosNorm      = FLT_MAX;
    t.fPosSubnorm   = FLT_MIN / 20.0;
    t.fPosZero      = +0.0;

    f.Call(&t, 0, 0, 0, 0);
    // Expected double results.
    CHECK_EQ(base::bit_cast<int64_t>(t.dSignalingNan), 0x001);
    CHECK_EQ(base::bit_cast<int64_t>(t.dQuietNan), 0x002);
    CHECK_EQ(base::bit_cast<int64_t>(t.dNegInf), 0x004);
    CHECK_EQ(base::bit_cast<int64_t>(t.dNegNorm), 0x008);
    CHECK_EQ(base::bit_cast<int64_t>(t.dNegSubnorm), 0x010);
    CHECK_EQ(base::bit_cast<int64_t>(t.dNegZero), 0x020);
    CHECK_EQ(base::bit_cast<int64_t>(t.dPosInf), 0x040);
    CHECK_EQ(base::bit_cast<int64_t>(t.dPosNorm), 0x080);
    CHECK_EQ(base::bit_cast<int64_t>(t.dPosSubnorm), 0x100);
    CHECK_EQ(base::bit_cast<int64_t>(t.dPosZero), 0x200);

    // Expected float results.
    CHECK_EQ(base::bit_cast<int32_t>(t.fSignalingNan), 0x001);
    CHECK_EQ(base::bit_cast<int32_t>(t.fQuietNan), 0x002);
    CHECK_EQ(base::bit_cast<int32_t>(t.fNegInf), 0x004);
    CHECK_EQ(base::bit_cast<int32_t>(t.fNegNorm), 0x008);
    CHECK_EQ(base::bit_cast<int32_t>(t.fNegSubnorm), 0x010);
    CHECK_EQ(base::bit_cast<int32_t>(t.fNegZero), 0x020);
    CHECK_EQ(base::bit_cast<int32_t>(t.fPosInf), 0x040);
    CHECK_EQ(base::bit_cast<int32_t>(t.fPosNorm), 0x080);
    CHECK_EQ(base::bit_cast<int32_t>(t.fPosSubnorm), 0x100);
    CHECK_EQ(base::bit_cast<int32_t>(t.fPosZero), 0x200);
  }
}


TEST(ABS) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    int64_t fir;
    double a;
    float b;
    double fcsr;
  };

  TestFloat test;

  // Save FIR.
  __ cfc1(a1, FCSR);
  __ Sd(a1, MemOperand(a0, offsetof(TestFloat, fcsr)));
  // Disable FPU exceptions.
  __ ctc1(zero_reg, FCSR);

  __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
  __ abs_d(f10, f4);
  __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, a)));

  __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, b)));
  __ abs_s(f10, f4);
  __ Swc1(f10, MemOperand(a0, offsetof(TestFloat, b)));

  // Restore FCSR.
  __ ctc1(a1, FCSR);

  __ jr(ra);
  __ nop();

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
  test.a = -std::numeric_limits<double>::max()
          / std::numeric_limits<double>::min();
  test.b = -std::numeric_limits<float>::max()
          / std::numeric_limits<float>::min();
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.a, std::numeric_limits<double>::max()
                 / std::numeric_limits<double>::min());
  CHECK_EQ(test.b, std::numeric_limits<float>::max()
                 / std::numeric_limits<float>::min());

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


TEST(ADD_FMT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    double a;
    double b;
    double c;
    float fa;
    float fb;
    float fc;
  };

  TestFloat test;

  __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, a)));
  __ Ldc1(f8, MemOperand(a0, offsetof(TestFloat, b)));
  __ add_d(f10, f8, f4);
  __ Sdc1(f10, MemOperand(a0, offsetof(TestFloat, c)));

  __ Lwc1(f4, MemOperand(a0, offsetof(TestFloat, fa)));
  __ Lwc1(f8, MemOperand(a0, offsetof(TestFloat, fb)));
  __ add_s(f10, f8, f4);
  __ Swc1(f10, MemOperand(a0, offsetof(TestFloat, fc)));

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  test.a = 2.0;
  test.b = 3.0;
  test.fa = 2.0;
  test.fb = 3.0;
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.c, 5.0);
  CHECK_EQ(test.fc, 5.0);

  test.a = std::numeric_limits<double>::max();
  test.b = -std::numeric_limits<double>::max();  // lowest()
  test.fa = std::numeric_limits<float>::max();
  test.fb = -std::numeric_limits<float>::max();  // lowest()
  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.c, 0.0);
  CHECK_EQ(test.fc, 0.0);

  test.a = std::numeric_limits<double>::max();
  test.b = std::numeric_limits<double>::max();
  test.fa = std::numeric_limits<float>::max();
  test.fb = std::numeric_limits<float>::max();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(!std::isfinite(test.c));
  CHECK(!std::isfinite(test.fc));

  test.a = 5.0;
  test.b = std::numeric_limits<double>::signaling_NaN();
  test.fa = 5.0;
  test.fb = std::numeric_limits<float>::signaling_NaN();
  f.Call(&test, 0, 0, 0, 0);
  CHECK(std::isnan(test.c));
  CHECK(std::isnan(test.fc));
}


TEST(C_COND_FMT) {
  if (kArchVariant == kMips64r2) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      double dOp1;
      double dOp2;
      uint32_t dF;
      uint32_t dUn;
      uint32_t dEq;
      uint32_t dUeq;
      uint32_t dOlt;
      uint32_t dUlt;
      uint32_t dOle;
      uint32_t dUle;
      float fOp1;
      float fOp2;
      uint32_t fF;
      uint32_t fUn;
      uint32_t fEq;
      uint32_t fUeq;
      uint32_t fOlt;
      uint32_t fUlt;
      uint32_t fOle;
      uint32_t fUle;
    };

    TestFloat test;

    __ li(t1, 1);

    __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, dOp1)));
    __ Ldc1(f6, MemOperand(a0, offsetof(TestFloat, dOp2)));

    __ Lwc1(f14, MemOperand(a0, offsetof(TestFloat, fOp1)));
    __ Lwc1(f16, MemOperand(a0, offsetof(TestFloat, fOp2)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(F, f4, f6, 0);
    __ c_s(F, f14, f16, 2);
    __ movt(t2, t1, 0);
    __ movt(t3, t1, 2);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dF)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fF)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(UN, f4, f6, 2);
    __ c_s(UN, f14, f16, 4);
    __ movt(t2, t1, 2);
    __ movt(t3, t1, 4);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dUn)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fUn)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(EQ, f4, f6, 4);
    __ c_s(EQ, f14, f16, 6);
    __ movt(t2, t1, 4);
    __ movt(t3, t1, 6);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dEq)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fEq)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(UEQ, f4, f6, 6);
    __ c_s(UEQ, f14, f16, 0);
    __ movt(t2, t1, 6);
    __ movt(t3, t1, 0);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dUeq)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fUeq)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(OLT, f4, f6, 0);
    __ c_s(OLT, f14, f16, 2);
    __ movt(t2, t1, 0);
    __ movt(t3, t1, 2);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dOlt)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fOlt)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(ULT, f4, f6, 2);
    __ c_s(ULT, f14, f16, 4);
    __ movt(t2, t1, 2);
    __ movt(t3, t1, 4);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dUlt)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fUlt)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(OLE, f4, f6, 4);
    __ c_s(OLE, f14, f16, 6);
    __ movt(t2, t1, 4);
    __ movt(t3, t1, 6);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dOle)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fOle)));

    __ mov(t2, zero_reg);
    __ mov(t3, zero_reg);
    __ c_d(ULE, f4, f6, 6);
    __ c_s(ULE, f14, f16, 0);
    __ movt(t2, t1, 6);
    __ movt(t3, t1, 0);
    __ Sw(t2, MemOperand(a0, offsetof(TestFloat, dUle)));
    __ Sw(t3, MemOperand(a0, offsetof(TestFloat, fUle)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    test.dOp1 = 2.0;
    test.dOp2 = 3.0;
    test.fOp1 = 2.0;
    test.fOp2 = 3.0;
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.dF, 0U);
    CHECK_EQ(test.dUn, 0U);
    CHECK_EQ(test.dEq, 0U);
    CHECK_EQ(test.dUeq, 0U);
    CHECK_EQ(test.dOlt, 1U);
    CHECK_EQ(test.dUlt, 1U);
    CHECK_EQ(test.dOle, 1U);
    CHECK_EQ(test.dUle, 1U);
    CHECK_EQ(test.fF, 0U);
    CHECK_EQ(test.fUn, 0U);
    CHECK_EQ(test.fEq, 0U);
    CHECK_EQ(test.fUeq, 0U);
    CHECK_EQ(test.fOlt, 1U);
    CHECK_EQ(test.fUlt, 1U);
    CHECK_EQ(test.fOle, 1U);
    CHECK_EQ(test.fUle, 1U);

    test.dOp1 = std::numeric_limits<double>::max();
    test.dOp2 = std::numeric_limits<double>::min();
    test.fOp1 = std::numeric_limits<float>::min();
    test.fOp2 = -std::numeric_limits<float>::max();  // lowest()
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.dF, 0U);
    CHECK_EQ(test.dUn, 0U);
    CHECK_EQ(test.dEq, 0U);
    CHECK_EQ(test.dUeq, 0U);
    CHECK_EQ(test.dOlt, 0U);
    CHECK_EQ(test.dUlt, 0U);
    CHECK_EQ(test.dOle, 0U);
    CHECK_EQ(test.dUle, 0U);
    CHECK_EQ(test.fF, 0U);
    CHECK_EQ(test.fUn, 0U);
    CHECK_EQ(test.fEq, 0U);
    CHECK_EQ(test.fUeq, 0U);
    CHECK_EQ(test.fOlt, 0U);
    CHECK_EQ(test.fUlt, 0U);
    CHECK_EQ(test.fOle, 0U);
    CHECK_EQ(test.fUle, 0U);

    test.dOp1 = -std::numeric_limits<double>::max();  // lowest()
    test.dOp2 = -std::numeric_limits<double>::max();  // lowest()
    test.fOp1 = std::numeric_limits<float>::max();
    test.fOp2 = std::numeric_limits<float>::max();
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.dF, 0U);
    CHECK_EQ(test.dUn, 0U);
    CHECK_EQ(test.dEq, 1U);
    CHECK_EQ(test.dUeq, 1U);
    CHECK_EQ(test.dOlt, 0U);
    CHECK_EQ(test.dUlt, 0U);
    CHECK_EQ(test.dOle, 1U);
    CHECK_EQ(test.dUle, 1U);
    CHECK_EQ(test.fF, 0U);
    CHECK_EQ(test.fUn, 0U);
    CHECK_EQ(test.fEq, 1U);
    CHECK_EQ(test.fUeq, 1U);
    CHECK_EQ(test.fOlt, 0U);
    CHECK_EQ(test.fUlt, 0U);
    CHECK_EQ(test.fOle, 1U);
    CHECK_EQ(test.fUle, 1U);

    test.dOp1 = std::numeric_limits<double>::quiet_NaN();
    test.dOp2 = 0.0;
    test.fOp1 = std::numeric_limits<float>::quiet_NaN();
    test.fOp2 = 0.0;
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(test.dF, 0U);
    CHECK_EQ(test.dUn, 1U);
    CHECK_EQ(test.dEq, 0U);
    CHECK_EQ(test.dUeq, 1U);
    CHECK_EQ(test.dOlt, 0U);
    CHECK_EQ(test.dUlt, 1U);
    CHECK_EQ(test.dOle, 0U);
    CHECK_EQ(test.dUle, 1U);
    CHECK_EQ(test.fF, 0U);
    CHECK_EQ(test.fUn, 1U);
    CHECK_EQ(test.fEq, 0U);
    CHECK_EQ(test.fUeq, 1U);
    CHECK_EQ(test.fOlt, 0U);
    CHECK_EQ(test.fUlt, 1U);
    CHECK_EQ(test.fOle, 0U);
    CHECK_EQ(test.fUle, 1U);
  }
}


TEST(CMP_COND_FMT) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();
    Isolate* isolate = CcTest::i_isolate();
    HandleScope scope(isolate);
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

    struct TestFloat {
      double dOp1;
      double dOp2;
      double dF;
      double dUn;
      double dEq;
      double dUeq;
      double dOlt;
      double dUlt;
      double dOle;
      double dUle;
      double dOr;
      double dUne;
      double dNe;
      float fOp1;
      float fOp2;
      float fF;
      float fUn;
      float fEq;
      float fUeq;
      float fOlt;
      float fUlt;
      float fOle;
      float fUle;
      float fOr;
      float fUne;
      float fNe;
    };

    TestFloat test;

    __ li(t1, 1);

    __ Ldc1(f4, MemOperand(a0, offsetof(TestFloat, dOp1)));
    __ Ldc1(f6, MemOperand(a0, offsetof(TestFloat, dOp2)));

    __ Lwc1(f14, MemOperand(a0, offsetof(TestFloat, fOp1)));
    __ Lwc1(f16, MemOperand(a0, offsetof(TestFloat, fOp2)));

    __ cmp_d(F, f2, f4, f6);
    __ cmp_s(F, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dF)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fF)));

    __ cmp_d(UN, f2, f4, f6);
    __ cmp_s(UN, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dUn)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fUn)));

    __ cmp_d(EQ, f2, f4, f6);
    __ cmp_s(EQ, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dEq)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fEq)));

    __ cmp_d(UEQ, f2, f4, f6);
    __ cmp_s(UEQ, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dUeq)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fUeq)));

    __ cmp_d(LT, f2, f4, f6);
    __ cmp_s(LT, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dOlt)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fOlt)));

    __ cmp_d(ULT, f2, f4, f6);
    __ cmp_s(ULT, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dUlt)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fUlt)));

    __ cmp_d(LE, f2, f4, f6);
    __ cmp_s(LE, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dOle)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fOle)));

    __ cmp_d(ULE, f2, f4, f6);
    __ cmp_s(ULE, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dUle)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fUle)));

    __ cmp_d(ORD, f2, f4, f6);
    __ cmp_s(ORD, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dOr)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fOr)));

    __ cmp_d(UNE, f2, f4, f6);
    __ cmp_s(UNE, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dUne)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fUne)));

    __ cmp_d(NE, f2, f4, f6);
    __ cmp_s(NE, f12, f14, f16);
    __ Sdc1(f2, MemOperand(a0, offsetof(TestFloat, dNe)));
    __ Swc1(f12, MemOperand(a0, offsetof(TestFloat, fNe)));

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    uint64_t dTrue  = 0xFFFFFFFFFFFFFFFF;
    uint64_t dFalse = 0x0000000000000000;
    uint32_t fTrue  = 0xFFFFFFFF;
    uint32_t fFalse = 0x00000000;

    test.dOp1 = 2.0;
    test.dOp2 = 3.0;
    test.fOp1 = 2.0;
    test.fOp2 = 3.0;
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dF), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUn), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dEq), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUeq), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOlt), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUlt), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOle), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUle), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOr), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUne), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dNe), dTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fF), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUn), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fEq), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUeq), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOlt), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUlt), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOle), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUle), fTrue);

    test.dOp1 = std::numeric_limits<double>::max();
    test.dOp2 = std::numeric_limits<double>::min();
    test.fOp1 = std::numeric_limits<float>::min();
    test.fOp2 = -std::numeric_limits<float>::max();  // lowest()
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dF), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUn), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dEq), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUeq), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOlt), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUlt), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOle), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUle), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOr), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUne), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dNe), dTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fF), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUn), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fEq), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUeq), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOlt), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUlt), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOle), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUle), fFalse);

    test.dOp1 = -std::numeric_limits<double>::max();  // lowest()
    test.dOp2 = -std::numeric_limits<double>::max();  // lowest()
    test.fOp1 = std::numeric_limits<float>::max();
    test.fOp2 = std::numeric_limits<float>::max();
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dF), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUn), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dEq), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUeq), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOlt), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUlt), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOle), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUle), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOr), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUne), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dNe), dFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fF), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUn), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fEq), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUeq), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOlt), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUlt), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOle), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUle), fTrue);

    test.dOp1 = std::numeric_limits<double>::quiet_NaN();
    test.dOp2 = 0.0;
    test.fOp1 = std::numeric_limits<float>::quiet_NaN();
    test.fOp2 = 0.0;
    f.Call(&test, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dF), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUn), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dEq), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUeq), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOlt), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUlt), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOle), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUle), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dOr), dFalse);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dUne), dTrue);
    CHECK_EQ(base::bit_cast<uint64_t>(test.dNe), dFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fF), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUn), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fEq), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUeq), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOlt), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUlt), fTrue);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fOle), fFalse);
    CHECK_EQ(base::bit_cast<uint32_t>(test.fUle), fTrue);
  }
}


TEST(CVT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct TestFloat {
    float    cvt_d_s_in;
    double   cvt_d_s_out;
    int32_t  cvt_d_w_in;
    double   cvt_d_w_out;
    int64_t  cvt_d_l_in;
    double   cvt_d_l_out;

    float    cvt_l_s_in;
    int64_t  cvt_l_s_out;
    double   cvt_l_d_in;
    int64_t  cvt_l_d_out;

    double   cvt_s_d_in;
    float    cvt_s_d_out;
    int32_t  cvt_s_w_in;
    float    cvt_s_w_out;
    int64_t  cvt_s_l_in;
    float    cvt_s_l_out;

    float    cvt_w_s_in;
    int32_t  cvt_w_s_out;
    double   cvt_w_d_in;
    int32_t  cvt_w_d_out;
  };

  TestFloat test;

  // Save FCSR.
  __ cfc1(a1, FCSR);
  // Disable FPU exceptions.
  __ ctc1(zero_reg, FCSR);

#define GENERATE_CVT_TEST(x, y, z) \
  __ y##c1(f0, MemOperand(a0, offsetof(TestFloat, x##_in))); \
  __ x(f0, f0); \
  __ nop(); \
  __ z##c1(f0, MemOperand(a0, offsetof(TestFloat, x##_out)));

  GENERATE_CVT_TEST(cvt_d_s, lw, sd)
  GENERATE_CVT_TEST(cvt_d_w, lw, sd)
  GENERATE_CVT_TEST(cvt_d_l, ld, sd)

  GENERATE_CVT_TEST(cvt_l_s, lw, sd)
  GENERATE_CVT_TEST(cvt_l_d, ld, sd)

  GENERATE_CVT_TEST(cvt_s_d, ld, sw)
  GENERATE_CVT_TEST(cvt_s_w, lw, sw)
  GENERATE_CVT_TEST(cvt_s_l, ld, sw)

  GENERATE_CVT_TEST(cvt_w_s, lw, sw)
  GENERATE_CVT_TEST(cvt_w_d, ld, sw)

  // Restore FCSR.
  __ ctc1(a1, FCSR);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  test.cvt_d_s_in = -0.51;
  test.cvt_d_w_in = -1;
  test.cvt_d_l_in = -1;
  test.cvt_l_s_in = -0.51;
  test.cvt_l_d_in = -0.51;
  test.cvt_s_d_in = -0.51;
  test.cvt_s_w_in = -1;
  test.cvt_s_l_in = -1;
  test.cvt_w_s_in = -0.51;
  test.cvt_w_d_in = -0.51;

  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.cvt_d_s_out, static_cast<double>(test.cvt_d_s_in));
  CHECK_EQ(test.cvt_d_w_out, static_cast<double>(test.cvt_d_w_in));
  CHECK_EQ(test.cvt_d_l_out, static_cast<double>(test.cvt_d_l_in));
  CHECK_EQ(-1, test.cvt_l_s_out);
  CHECK_EQ(-1, test.cvt_l_d_out);
  CHECK_EQ(test.cvt_s_d_out, static_cast<float>(test.cvt_s_d_in));
  CHECK_EQ(test.cvt_s_w_out, static_cast<float>(test.cvt_s_w_in));
  CHECK_EQ(test.cvt_s_l_out, static_cast<float>(test.cvt_s_l_in));
  CHECK_EQ(-1, test.cvt_w_s_out);
  CHECK_EQ(-1, test.cvt_w_d_out);

  test.cvt_d_s_in = 0.49;
  test.cvt_d_w_in = 1;
  test.cvt_d_l_in = 1;
  test.cvt_l_s_in = 0.49;
  test.cvt_l_d_in = 0.49;
  test.cvt_s_d_in = 0.49;
  test.cvt_s_w_in = 1;
  test.cvt_s_l_in = 1;
  test.cvt_w_s_in = 0.49;
  test.cvt_w_d_in = 0.49;

  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.cvt_d_s_out, static_cast<double>(test.cvt_d_s_in));
  CHECK_EQ(test.cvt_d_w_out, static_cast<double>(test.cvt_d_w_in));
  CHECK_EQ(test.cvt_d_l_out, static_cast<double>(test.cvt_d_l_in));
  CHECK_EQ(0, test.cvt_l_s_out);
  CHECK_EQ(0, test.cvt_l_d_out);
  CHECK_EQ(test.cvt_s_d_out, static_cast<float>(test.cvt_s_d_in));
  CHECK_EQ(test.cvt_s_w_out, static_cast<float>(test.cvt_s_w_in));
  CHECK_EQ(test.cvt_s_l_out, static_cast<float>(test.cvt_s_l_in));
  CHECK_EQ(0, test.cvt_w_s_out);
  CHECK_EQ(0, test.cvt_w_d_out);

  test.cvt_d_s_in = std::numeric_limits<float>::max();
  test.cvt_d_w_in = std::numeric_limits<int32_t>::max();
  test.cvt_d_l_in = std::numeric_limits<int64_t>::max();
  test.cvt_l_s_in = std::numeric_limits<float>::max();
  test.cvt_l_d_in = std::numeric_limits<double>::max();
  test.cvt_s_d_in = std::numeric_limits<double>::max();
  test.cvt_s_w_in = std::numeric_limits<int32_t>::max();
  test.cvt_s_l_in = std::numeric_limits<int64_t>::max();
  test.cvt_w_s_in = std::numeric_limits<float>::max();
  test.cvt_w_d_in = std::numeric_limits<double>::max();

  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.cvt_d_s_out, static_cast<double>(test.cvt_d_s_in));
  CHECK_EQ(test.cvt_d_w_out, static_cast<double>(test.cvt_d_w_in));
  CHECK_EQ(test.cvt_d_l_out, static_cast<double>(test.cvt_d_l_in));
  CHECK_EQ(test.cvt_l_s_out, std::numeric_limits<int64_t>::max());
  CHECK_EQ(test.cvt_l_d_out, std::numeric_limits<int64_t>::max());
  CHECK_EQ(test.cvt_s_d_out, static_cast<float>(test.cvt_s_d_in));
  CHECK_EQ(test.cvt_s_w_out, static_cast<float>(test.cvt_s_w_in));
  CHECK_EQ(test.cvt_s_l_out, static_cast<float>(test.cvt_s_l_in));
  CHECK_EQ(test.cvt_w_s_out, std::numeric_limits<int32_t>::max());
  CHECK_EQ(test.cvt_w_d_out, std::numeric_limits<int32_t>::max());


  test.cvt_d_s_in = -std::numeric_limits<float>::max();   // lowest()
  test.cvt_d_w_in = std::numeric_limits<int32_t>::min();  // lowest()
  test.cvt_d_l_in = std::numeric_limits<int64_t>::min();  // lowest()
  test.cvt_l_s_in = -std::numeric_limits<float>::max();   // lowest()
  test.cvt_l_d_in = -std::numeric_limits<double>::max();  // lowest()
  test.cvt_s_d_in = -std::numeric_limits<double>::max();  // lowest()
  test.cvt_s_w_in = std::numeric_limits<int32_t>::min();  // lowest()
  test.cvt_s_l_in = std::numeric_limits<int64_t>::min();  // lowest()
  test.cvt_w_s_in = -std::numeric_limits<float>::max();   // lowest()
  test.cvt_w_d_in = -std::numeric_limits<double>::max();  // lowest()

  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.cvt_d_s_out, static_cast<double>(test.cvt_d_s_in));
  CHECK_EQ(test.cvt_d_w_out, static_cast<double>(test.cvt_d_w_in));
  CHECK_EQ(test.cvt_d_l_out, static_cast<double>(test.cvt_d_l_in));
  // The returned value when converting from fixed-point to float-point
  // is not consistent between board, simulator and specification
  // in this test case, therefore modifying the test
  CHECK(test.cvt_l_s_out == std::numeric_limits<int64_t>::min() ||
       test.cvt_l_s_out == std::numeric_limits<int64_t>::max());
  CHECK(test.cvt_l_d_out == std::numeric_limits<int64_t>::min() ||
        test.cvt_l_d_out == std::numeric_limits<int64_t>::max());
  CHECK_EQ(test.cvt_s_d_out, static_cast<float>(test.cvt_s_d_in));
  CHECK_EQ(test.cvt_s_w_out, static_cast<float>(test.cvt_s_w_in));
  CHECK_EQ(test.cvt_s_l_out, static_cast<float>(test.cvt_s_l_in));
  CHECK(test.cvt_w_s_out == std::numeric_limits<int32_t>::min() ||
        test.cvt_w_s_out == std::numeric_limits<int32_t>::max());
  CHECK(test.cvt_w_d_out == std::numeric_limits<int32_t>::min() ||
        test.cvt_w_d_out == std::numeric_limits<int32_t>::max());


  test.cvt_d_s_in = std::numeric_limits<float>::min();
  test.cvt_d_w_in = std::numeric_limits<int32_t>::min();
  test.cvt_d_l_in = std::numeric_limits<int64_t>::min();
  test.cvt_l_s_in = std::numeric_limits<float>::min();
  test.cvt_l_d_in = std::numeric_limits<double>::min();
  test.cvt_s_d_in = std::numeric_limits<double>::min();
  test.cvt_s_w_in = std::numeric_limits<int32_t>::min();
  test.cvt_s_l_in = std::numeric_limits<int64_t>::min();
  test.cvt_w_s_in = std::numeric_limits<float>::min();
  test.cvt_w_d_in = std::numeric_limits<double>::min();

  f.Call(&test, 0, 0, 0, 0);
  CHECK_EQ(test.cvt_d_s_out, static_cast<double>(test.cvt_d_s_in));
  CHECK_EQ(test.cvt_d_w_out, static_cast<double>(test.cvt_d_w_in));
  CHECK_EQ(test.cvt_d_l_out, static_cast<double>(test.cvt_d_l_in));
  CHECK_EQ(0, test.cvt_l_s_out);
  CHECK_EQ(0, test.cvt_l_d_out);
  CHECK_EQ(test.cvt_s_d_out, static_cast<float>(test.cvt_s_d_in));
  CHECK_EQ(test.cvt_s_w_out, static_cast<float>(test.cvt_s_w_in));
  CHECK_EQ(test.cvt_s_l_out, static_cast<float>(test.cvt_s_l_in));
  CHECK_EQ(0, test.cvt_w_s_out);
  CHECK_EQ(0, test.cvt_w_d_out);
}


TEST(DIV_FMT) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  struct Test {
    double dOp1;
    double dOp2;
    double dRes;
    float  fOp1;
    float  fOp2;
    float  fRes;
  };

  Test test;

  // Save FCSR.
  __ cfc1(a1, FCSR);
  // Disable FPU exceptions.
  __ ctc1(zero_reg, FCSR);

  __ Ldc1(f4, MemOperand(a0, offsetof(Test, dOp1)));
  __ Ldc1(f2, MemOperand(a0, offsetof(Test, dOp2)));
  __ nop();
  __ div_d(f6, f4, f2);
  __ Sdc1(f6, MemOperand(a0, offsetof(Test, dRes)));

  __ Lwc1(f4, MemOperand(a0, offsetof(Test, fOp1)));
  __ Lwc1(f2, MemOperand(a0, offsetof(Test, fOp2)));
  __ nop();
  __ div_s(f6, f4, f2);
  __ Swc1(f6, MemOperand(a0, offsetof(Test, fRes)));

  // Restore FCSR.
  __ ctc1(a1, FCSR);

  __ jr(ra);
  __ nop();
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&test, 0, 0, 0, 0);

  const int test_size = 3;

  double dOp1[test_size] = {
    5.0,
    DBL_MAX,
    DBL_MAX,
  };
  double dOp2[test_size] = {
    2.0,
    2.0,
    -DBL_MAX,
  };
  double dRes[test_size] = {
    2.5,
    DBL_MAX / 2.0,
    -1.0,
  };
  float fOp1[test_size] = {
    5.0,
    FLT_MAX,
    FLT_MAX,
  };
  float fOp2[test_size] = {
    2.0,
    2.0,
    -FLT_MAX,
  };
  float fRes[test_size] = {
    2.5,
    FLT_MAX / 2.0,
    -1.0,
  };

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


uint64_t run_align(uint64_t rs_value, uint64_t rt_value, uint8_t bp) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ align(v0, a0, a1, bp);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F4>::FromCode(isolate, *code);

  uint64_t res =
      reinterpret_cast<uint64_t>(f.Call(rs_value, rt_value, 0, 0, 0));

  return res;
}


TEST(r6_align) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseAlign {
      uint64_t  rs_value;
      uint64_t  rt_value;
      uint8_t   bp;
      uint64_t  expected_res;
    };

    // clang-format off
    struct TestCaseAlign tc[] = {
      // rs_value,    rt_value,    bp, expected_res
      {  0x11223344,  0xAABBCCDD,   0, 0xFFFFFFFFAABBCCDD },
      {  0x11223344,  0xAABBCCDD,   1, 0xFFFFFFFFBBCCDD11 },
      {  0x11223344,  0xAABBCCDD,   2, 0xFFFFFFFFCCDD1122 },
      {  0x11223344,  0xAABBCCDD,   3, 0xFFFFFFFFDD112233 },
    };
    // clang-format on

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseAlign);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      CHECK_EQ(tc[i].expected_res, run_align(tc[i].rs_value,
                                              tc[i].rt_value,
                                              tc[i].bp));
    }
  }
}


uint64_t run_dalign(uint64_t rs_value, uint64_t rt_value, uint8_t bp) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ dalign(v0, a0, a1, bp);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F4>::FromCode(isolate, *code);
  uint64_t res =
      reinterpret_cast<uint64_t>(f.Call(rs_value, rt_value, 0, 0, 0));

  return res;
}


TEST(r6_dalign) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseDalign {
      uint64_t  rs_value;
      uint64_t  rt_value;
      uint8_t   bp;
      uint64_t  expected_res;
    };

    // clang-format off
    struct TestCaseDalign tc[] = {
      // rs_value,           rt_value,            bp, expected_res
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   0, 0xAABBCCDDEEFF8899 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   1, 0xBBCCDDEEFF889911 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   2, 0xCCDDEEFF88991122 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   3, 0xDDEEFF8899112233 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   4, 0xEEFF889911223344 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   5, 0xFF88991122334455 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   6, 0x8899112233445566 },
      { 0x1122334455667700,  0xAABBCCDDEEFF8899,   7, 0x9911223344556677 }
    };
    // clang-format on

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseDalign);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      CHECK_EQ(tc[i].expected_res, run_dalign(tc[i].rs_value,
                                              tc[i].rt_value,
                                              tc[i].bp));
    }
  }
}


uint64_t PC;  // The program counter.

uint64_t run_aluipc(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ aluipc(v0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  PC = (uint64_t)code->instruction_start();  // Set the program counter.

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_aluipc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseAluipc {
      int16_t   offset;
    };

    struct TestCaseAluipc tc[] = {
      // offset
      { -32768 },   // 0x8000
      {     -1 },   // 0xFFFF
      {      0 },
      {      1 },
      {  32767 },   // 0x7FFF
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseAluipc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      PC = 0;
      uint64_t res = run_aluipc(tc[i].offset);
      // Now, the program_counter (PC) is set.
      uint64_t expected_res = ~0x0FFFF & (PC + (tc[i].offset << 16));
      CHECK_EQ(expected_res, res);
    }
  }
}


uint64_t run_auipc(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ auipc(v0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  PC = (uint64_t)code->instruction_start();  // Set the program counter.

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_auipc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseAuipc {
      int16_t   offset;
    };

    struct TestCaseAuipc tc[] = {
      // offset
      { -32768 },   // 0x8000
      {     -1 },   // 0xFFFF
      {      0 },
      {      1 },
      {  32767 },   // 0x7FFF
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseAuipc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      PC = 0;
      uint64_t res = run_auipc(tc[i].offset);
      // Now, the program_counter (PC) is set.
      uint64_t expected_res = PC + (tc[i].offset << 16);
      CHECK_EQ(expected_res, res);
    }
  }
}


uint64_t run_aui(uint64_t rs, uint16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(t0, rs);
  __ aui(v0, t0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


uint64_t run_daui(uint64_t rs, uint16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(t0, rs);
  __ daui(v0, t0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


uint64_t run_dahi(uint64_t rs, uint16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(v0, rs);
  __ dahi(v0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


uint64_t run_dati(uint64_t rs, uint16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(v0, rs);
  __ dati(v0, offset);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_aui_family) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseAui {
      uint64_t   rs;
      uint16_t   offset;
      uint64_t   ref_res;
    };

    // AUI test cases.
    struct TestCaseAui aui_tc[] = {
        {0xFFFEFFFF, 0x1, 0xFFFFFFFFFFFFFFFF},
        {0xFFFFFFFF, 0x0, 0xFFFFFFFFFFFFFFFF},
        {0, 0xFFFF, 0xFFFFFFFFFFFF0000},
        {0x0008FFFF, 0xFFF7, 0xFFFFFFFFFFFFFFFF},
        {32767, 32767, 0x000000007FFF7FFF},
        {0x00000000FFFFFFFF, 0x1, 0x000000000000FFFF},
        {0xFFFFFFFF, 0xFFFF, 0xFFFFFFFFFFFEFFFF},
    };

    size_t nr_test_cases = sizeof(aui_tc) / sizeof(TestCaseAui);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_aui(aui_tc[i].rs, aui_tc[i].offset);
      CHECK_EQ(aui_tc[i].ref_res, res);
    }

    // DAUI test cases.
    struct TestCaseAui daui_tc[] = {
        {0xFFFFFFFFFFFEFFFF, 0x1, 0xFFFFFFFFFFFFFFFF},
        {0xFFFFFFFFFFFFFFFF, 0x0, 0xFFFFFFFFFFFFFFFF},
        {0, 0xFFFF, 0xFFFFFFFFFFFF0000},
        {0x0008FFFF, 0xFFF7, 0xFFFFFFFFFFFFFFFF},
        {32767, 32767, 0x000000007FFF7FFF},
        {0x00000000FFFFFFFF, 0x1, 0x000000010000FFFF},
        {0xFFFFFFFF, 0xFFFF, 0x00000000FFFEFFFF},
    };

    nr_test_cases = sizeof(daui_tc) / sizeof(TestCaseAui);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_daui(daui_tc[i].rs, daui_tc[i].offset);
      CHECK_EQ(daui_tc[i].ref_res, res);
    }

    // DATI test cases.
    struct TestCaseAui dati_tc[] = {
        {0xFFFFFFFFFFFEFFFF, 0x1, 0x0000FFFFFFFEFFFF},
        {0xFFFFFFFFFFFFFFFF, 0x0, 0xFFFFFFFFFFFFFFFF},
        {0, 0xFFFF, 0xFFFF000000000000},
        {0x0008FFFF, 0xFFF7, 0xFFF700000008FFFF},
        {32767, 32767, 0x7FFF000000007FFF},
        {0x00000000FFFFFFFF, 0x1, 0x00010000FFFFFFFF},
        {0xFFFFFFFFFFFF, 0xFFFF, 0xFFFFFFFFFFFFFFFF},
    };

    nr_test_cases = sizeof(dati_tc) / sizeof(TestCaseAui);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_dati(dati_tc[i].rs, dati_tc[i].offset);
      CHECK_EQ(dati_tc[i].ref_res, res);
    }

    // DAHI test cases.
    struct TestCaseAui dahi_tc[] = {
        {0xFFFFFFFEFFFFFFFF, 0x1, 0xFFFFFFFFFFFFFFFF},
        {0xFFFFFFFFFFFFFFFF, 0x0, 0xFFFFFFFFFFFFFFFF},
        {0, 0xFFFF, 0xFFFFFFFF00000000},
    };

    nr_test_cases = sizeof(dahi_tc) / sizeof(TestCaseAui);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_dahi(dahi_tc[i].rs, dahi_tc[i].offset);
      CHECK_EQ(dahi_tc[i].ref_res, res);
    }
  }
}

uint64_t run_li_macro(uint64_t imm, LiFlags mode, int32_t num_instr = 0) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label code_start;
  __ bind(&code_start);
  __ li(v0, imm, mode);
  if (num_instr > 0) {
    CHECK_EQ(assm.InstructionsGeneratedSince(&code_start), num_instr);
  }
  __ jr(ra);
  __ nop();

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
    int32_t r2_num_instr;
    int32_t r6_num_instr;
  };

  // We call li(v0, imm) to test cases listed below.
  struct TestCase_li tc[] = {
      //              imm, r2_num_instr, r6_num_instr
      {0xFFFFFFFFFFFF8000, 1, 1},  // min_int16
      // The test case above generates daddiu instruction.
      // This is int16 value and we can load it using just daddiu.
      {0x8000, 1, 1},  // max_int16 + 1
      // Generates ori
      // max_int16 + 1 is not int16 but is uint16, just use ori.
      {0xFFFFFFFFFFFF7FFF, 2, 2},  // min_int16 - 1
      // Generates lui + ori
      // We load int32 value using lui + ori.
      {0x8001, 1, 1},  // max_int16 + 2
      // Generates ori
      // Also an uint16 value, use ori.
      {0x00010000, 1, 1},  // max_uint16 + 1
      // Generates lui
      // Low 16 bits are 0, load value using lui.
      {0x00010001, 2, 2},  // max_uint16 + 2
      // Generates lui + ori
      // We have to generate two instructions in this case.
      {0x00000000FFFFFFFF, 2, 2},  // max_uint32
      // r2 - daddiu + dsrl32
      // r6 - daddiu + dahi
      {0x00000000FFFFFFFE, 3, 2},  // max_uint32 - 1
      // r2 - lui + ori + dsll
      // r6 - daddiu + dahi
      {0x00FFFF000000FFFE, 3, 3},
      // ori + dsll32 + ori
      {0x00000001FFFFFFFE, 4, 2},  // max_uint32 << 1
      // r2 - lui + ori + dsll + ori
      // r6 - daddiu + dahi
      {0x0000FFFFFFFFFFFE, 4, 2},  // max_uint48 - 1
      // r2 - daddiu + dsll32 + ori + dsubu
      // Loading imm directly would require ori + dsll + ori + dsll + ori.
      // Optimized by loading -imm and using dsubu to get imm.
      // r6 - daddiu + dati
      {0xFFFFFFFF00000000, 2, 2},  // max_uint32 << 32
      // r2 - daddiu + dsll32
      // r6 - ori + dahi
      // We need ori to clear register before loading value using dahi.
      {0xFFFFFFFF80000000, 1, 1},  // min_int32
      // The test case above generates lui instruction.
      {0x0000000080000000, 2, 2},  // max_int32 + 1
      // r2 - ori + dsll
      // r6 - lui + dahi
      {0x0000800000000000, 2, 2},
      // ori + dsll32
      {0xFFFF800000000000, 2, 2},
      // r2 - daddiu + dsll32
      // r6 - ori + dahi
      {0xFFFF80000000FFFF, 3, 2},
      // r2 - daddiu + dsll32 + ori
      // r6 - ori + dahi
      {0xFFFFFF123000FFFF, 3, 3},
      // daddiu + dsll + ori
      {0xFFFF00000000FFFF, 3, 2},
      // r2 - daddiu + dsll32 + ori
      // r6 - ori + dati
      {0xFFFF8000FFFF0000, 3, 2},
      // r2 - lui + ori + dsll
      // r6 - lui + dahi
      {0x0000FFFFFFFF0000, 4, 2},
      // r2 - ori + dsll + ori + dsll
      // r6 - lui + dati
      {0x1234FFFF80000000, 3, 2},
      // r2 - lui + ori + dsll
      // r6 - lui + dati
      {0x1234FFFF80010000, 5, 2},
      // r2 - lui + ori + dsll + ori + dsll
      // r6 - lui + dati
      {0xFFFF8000FFFF8000, 2, 2},
      // r2 - daddiu + dinsu
      // r6 - daddiu + dahi
      {0xFFFF0000FFFF8000, 4, 3},
      // r2 - ori + dsll32 + ori + dsubu
      // Loading imm directly would require lui + dsll + ori + dsll + ori.
      // Optimized by loading -imm and using dsubu to get imm.
      // r6 - daddiu + dahi + dati
      {0x8000000080000000, 2, 2},
      // lui + dinsu
      {0xABCD0000ABCD0000, 2, 2},
      // lui + dinsu
      {0x8000800080008000, 3, 3},
      // lui + ori + dinsu
      {0xABCD1234ABCD1234, 3, 3},
      // The test case above generates lui + ori + dinsu instruction sequence.
      {0xFFFF800080008000, 4, 3},
      // r2 - lui + ori + dsll + ori
      // r6 - lui + ori + dahi
      {0xFFFFABCD, 3, 2},
      // r2 - ori + dsll + ori
      // r6 - daddiu + dahi
      {0x1FFFFABCD, 4, 2},
      // r2 - lui + ori + dsll + ori
      // r6 - daddiu + dahi
      {0xFFFFFFFFABCD, 4, 2},
      // r2 - daddiu + dsll32 + ori + dsubu
      // Loading imm directly would require ori + dsll + ori + dsll + ori.
      // Optimized by loading -imm and using dsubu to get imm.
      // r6 - daddiu + dati
      {0x1FFFFFFFFABCD, 4, 2},
      // r2 - daddiu + dsll32 + ori + dsubu
      // Loading imm directly would require lui + ori + dsll + ori + dsll + ori.
      // Optimized by loading -imm and using dsubu to get imm.
      // r6 - daddiu + dati
      {0xFFFF7FFF80010000, 5, 2},
      // r2 - lui + ori + dsll + ori + dsll
      // r6 - lui + dahi
      // Here lui sets high 32 bits to 1 so dahi can be used to get target
      // value.
      {0x00007FFF7FFF0000, 3, 2},
      // r2 - lui + ori + dsll
      // r6 - lui + dahi
      // High 32 bits are not set so dahi can be used to get target value.
      {0xFFFF7FFF7FFF0000, 5, 3},
      // r2 - lui + ori + dsll + ori + dsll
      // r6 - lui + dahi + dati
      // High 32 bits are not set so just dahi can't be used to get target
      // value.
      {0x00007FFF80010000, 3, 3},
      // r2 - lui + ori + dsll
      // r6 - lui + ori + dsll
      // High 32 bits are set so can't just use lui + dahi to get target value.
      {0x1234ABCD87654321, 6, 4},
      // The test case above generates:
      // r2 - lui + ori + dsll + ori + dsll + ori instruction sequence,
      // r6 - lui + ori + dahi + dati.
      // Load using full instruction sequence.
      {0xFFFF0000FFFFFFFF, 3, 3},
      // r2 - ori + dsll32 + nor
      // Loading imm directly would require lui + dsll + ori + dsll + ori.
      // Optimized by loading ~imm and using nor to get imm. Loading -imm would
      // require one instruction more.
      // r6 - daddiu + dahi + dati
  };

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCase_li);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    if (kArchVariant == kMips64r2) {
      CHECK_EQ(tc[i].imm,
               run_li_macro(tc[i].imm, OPTIMIZE_SIZE, tc[i].r2_num_instr));
    } else {
      CHECK_EQ(tc[i].imm,
               run_li_macro(tc[i].imm, OPTIMIZE_SIZE, tc[i].r6_num_instr));
    }
    CHECK_EQ(tc[i].imm, run_li_macro(tc[i].imm, CONSTANT_SIZE));
    if (is_int48(tc[i].imm)) {
      CHECK_EQ(tc[i].imm, run_li_macro(tc[i].imm, ADDRESS_LOAD));
    }
  }
}


uint64_t run_lwpc(int offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // 256k instructions; 2^8k
  // addiu t3, a4, 0xFFFF;  (0x250FFFFF)
  // ...
  // addiu t0, a4, 0x0000;  (0x250C0000)
  uint32_t addiu_start_1 = 0x25000000;
  for (int32_t i = 0xFFFFF; i >= 0xC0000; --i) {
    uint32_t addiu_new = addiu_start_1 + i;
    __ dd(addiu_new);
  }

  __ lwpc(t8, offset);  // offset 0; 0xEF080000 (t8 register)
  __ mov(v0, t8);

  // 256k instructions; 2^8k
  // addiu a4, a4, 0x0000;  (0x25080000)
  // ...
  // addiu a7, a4, 0xFFFF;  (0x250BFFFF)
  uint32_t addiu_start_2 = 0x25000000;
  for (int32_t i = 0x80000; i <= 0xBFFFF; ++i) {
    uint32_t addiu_new = addiu_start_2 + i;
    __ dd(addiu_new);
  }

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_lwpc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseLwpc {
      int       offset;
      uint64_t  expected_res;
    };

    // clang-format off
    struct TestCaseLwpc tc[] = {
      // offset,   expected_res
      { -262144,   0x250FFFFF         },   // offset 0x40000
      {      -4,   0x250C0003         },
      {      -1,   0x250C0000         },
      {       0,   0xFFFFFFFFEF080000 },
      {       1,   0x03001025         },   // mov(v0, t8)
      {       2,   0x25080000         },
      {       4,   0x25080002         },
      {  262143,   0x250BFFFD         },   // offset 0x3FFFF
    };
    // clang-format on

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLwpc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_lwpc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


uint64_t run_lwupc(int offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // 256k instructions; 2^8k
  // addiu t3, a4, 0xFFFF;  (0x250FFFFF)
  // ...
  // addiu t0, a4, 0x0000;  (0x250C0000)
  uint32_t addiu_start_1 = 0x25000000;
  for (int32_t i = 0xFFFFF; i >= 0xC0000; --i) {
    uint32_t addiu_new = addiu_start_1 + i;
    __ dd(addiu_new);
  }

  __ lwupc(t8, offset);  // offset 0; 0xEF080000 (t8 register)
  __ mov(v0, t8);

  // 256k instructions; 2^8k
  // addiu a4, a4, 0x0000;  (0x25080000)
  // ...
  // addiu a7, a4, 0xFFFF;  (0x250BFFFF)
  uint32_t addiu_start_2 = 0x25000000;
  for (int32_t i = 0x80000; i <= 0xBFFFF; ++i) {
    uint32_t addiu_new = addiu_start_2 + i;
    __ dd(addiu_new);
  }

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_lwupc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseLwupc {
      int       offset;
      uint64_t  expected_res;
    };

    // clang-format off
    struct TestCaseLwupc tc[] = {
      // offset,    expected_res
      { -262144,    0x250FFFFF },   // offset 0x40000
      {      -4,    0x250C0003 },
      {      -1,    0x250C0000 },
      {       0,    0xEF100000 },
      {       1,    0x03001025 },   // mov(v0, t8)
      {       2,    0x25080000 },
      {       4,    0x25080002 },
      {  262143,    0x250BFFFD },   // offset 0x3FFFF
    };
    // clang-format on

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLwupc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_lwupc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


uint64_t run_jic(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label stop_execution;
  __ push(ra);
  __ li(v0, 0l);
  __ li(t1, 0x66);

  __ addiu(v0, v0, 0x1);        // <-- offset = -32
  __ addiu(v0, v0, 0x2);
  __ addiu(v0, v0, 0x10);
  __ addiu(v0, v0, 0x20);
  __ beq(v0, t1, &stop_execution);
  __ nop();

  __ nal();  // t0 <- program counter
  __ mov(t0, ra);
  __ jic(t0, offset);

  __ addiu(v0, v0, 0x100);
  __ addiu(v0, v0, 0x200);
  __ addiu(v0, v0, 0x1000);
  __ addiu(v0, v0, 0x2000);   // <--- offset = 16
  __ pop(ra);
  __ jr(ra);
  __ nop();

  __ bind(&stop_execution);
  __ pop(ra);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_jic) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseJic {
      // As rt will be used t0 register which will have value of
      // the program counter for the jic instruction.
      int16_t   offset;
      uint32_t  expected_res;
    };

    struct TestCaseJic tc[] = {
      // offset,   expected_result
      {      16,            0x2033 },
      {       4,            0x3333 },
      {     -32,              0x66 },
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseJic);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_jic(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


uint64_t run_beqzc(int32_t value, int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label stop_execution;
  __ li(v0, 0l);
  __ li(t1, 0x66);

  __ addiu(v0, v0, 0x1);        // <-- offset = -8
  __ addiu(v0, v0, 0x2);
  __ addiu(v0, v0, 0x10);
  __ addiu(v0, v0, 0x20);
  __ beq(v0, t1, &stop_execution);
  __ nop();

  __ beqzc(a0, offset);

  __ addiu(v0, v0,    0x1);
  __ addiu(v0, v0,  0x100);
  __ addiu(v0, v0,  0x200);
  __ addiu(v0, v0, 0x1000);
  __ addiu(v0, v0, 0x2000);   // <--- offset = 4
  __ jr(ra);
  __ nop();

  __ bind(&stop_execution);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(value, 0, 0, 0, 0));

  return res;
}


TEST(r6_beqzc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseBeqzc {
      uint32_t  value;
      int32_t   offset;
      uint32_t  expected_res;
    };

    // clang-format off
    struct TestCaseBeqzc tc[] = {
      //    value,    offset,   expected_res
      {       0x0,        -8,           0x66 },
      {       0x0,         0,         0x3334 },
      {       0x0,         1,         0x3333 },
      {     0xABC,         1,         0x3334 },
      {       0x0,         4,         0x2033 },
    };
    // clang-format on

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBeqzc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_beqzc(tc[i].value, tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}

void load_elements_of_vector(MacroAssembler* assm_ptr,
                             const uint64_t elements[], MSARegister w,
                             Register t0, Register t1) {
  MacroAssembler& assm = *assm_ptr;
  __ li(t0, static_cast<uint32_t>(elements[0] & 0xFFFFFFFF));
  __ li(t1, static_cast<uint32_t>((elements[0] >> 32) & 0xFFFFFFFF));
  __ insert_w(w, 0, t0);
  __ insert_w(w, 1, t1);
  __ li(t0, static_cast<uint32_t>(elements[1] & 0xFFFFFFFF));
  __ li(t1, static_cast<uint32_t>((elements[1] >> 32) & 0xFFFFFFFF));
  __ insert_w(w, 2, t0);
  __ insert_w(w, 3, t1);
}

inline void store_elements_of_vector(MacroAssembler* assm_ptr, MSARegister w,
                                     Register a) {
  MacroAssembler& assm = *assm_ptr;
  __ st_d(w, MemOperand(a, 0));
}

union msa_reg_t {
  uint8_t b[16];
  uint16_t h[8];
  uint32_t w[4];
  uint64_t d[2];
};

struct TestCaseMsaBranch {
  uint64_t wt_lo;
  uint64_t wt_hi;
};

template <typename Branch>
void run_bz_bnz(TestCaseMsaBranch* input, Branch GenerateBranch,
                bool branched) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);

  struct T {
    uint64_t ws_lo;
    uint64_t ws_hi;
    uint64_t wd_lo;
    uint64_t wd_hi;
  };
  T t = {0x20B9CC4F1A83E0C5, 0xA27E1B5F2F5BB18A, 0x0000000000000000,
         0x0000000000000000};
  msa_reg_t res;
  Label do_not_move_w0_to_w2;

  load_elements_of_vector(&assm, &t.ws_lo, w0, t0, t1);
  load_elements_of_vector(&assm, &t.wd_lo, w2, t0, t1);
  load_elements_of_vector(&assm, &input->wt_lo, w1, t0, t1);
  GenerateBranch(assm, do_not_move_w0_to_w2);
  __ nop();
  __ move_v(w2, w0);

  __ bind(&do_not_move_w0_to_w2);
  store_elements_of_vector(&assm, w2, a0);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);
  if (branched) {
    CHECK_EQ(t.wd_lo, res.d[0]);
    CHECK_EQ(t.wd_hi, res.d[1]);
  } else {
    CHECK_EQ(t.ws_lo, res.d[0]);
    CHECK_EQ(t.ws_hi, res.d[1]);
  }
}

TEST(MSA_bz_bnz) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  TestCaseMsaBranch tz_v[] = {
      {0x0, 0x0}, {0xABC, 0x0}, {0x0, 0xABC}, {0xABC, 0xABC}};
  for (unsigned i = 0; i < arraysize(tz_v); ++i) {
    run_bz_bnz(
        &tz_v[i],
        [](MacroAssembler& assm, Label& br_target) { __ bz_v(w1, &br_target); },
        tz_v[i].wt_lo == 0 && tz_v[i].wt_hi == 0);
  }

#define TEST_BZ_DF(input_array, lanes, instruction, int_type)         \
  for (unsigned i = 0; i < arraysize(input_array); ++i) {             \
    int j;                                                            \
    int_type* element = reinterpret_cast<int_type*>(&input_array[i]); \
    for (j = 0; j < lanes; ++j) {                                     \
      if (element[j] == 0) {                                          \
        break;                                                        \
      }                                                               \
    }                                                                 \
    run_bz_bnz(&input_array[i],                                       \
               [](MacroAssembler& assm, Label& br_target) {           \
                 __ instruction(w1, &br_target);                      \
               },                                                     \
               j != lanes);                                           \
  }
  TestCaseMsaBranch tz_b[] = {{0x0, 0x0},
                              {0xBC0000, 0x0},
                              {0x0, 0xAB000000000000CD},
                              {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BZ_DF(tz_b, kMSALanesByte, bz_b, int8_t)

  TestCaseMsaBranch tz_h[] = {{0x0, 0x0},
                              {0xBCDE0000, 0x0},
                              {0x0, 0xABCD00000000ABCD},
                              {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BZ_DF(tz_h, kMSALanesHalf, bz_h, int16_t)

  TestCaseMsaBranch tz_w[] = {{0x0, 0x0},
                              {0xBCDE123400000000, 0x0},
                              {0x0, 0x000000001234ABCD},
                              {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BZ_DF(tz_w, kMSALanesWord, bz_w, int32_t)

  TestCaseMsaBranch tz_d[] = {{0x0, 0x0},
                              {0xBCDE0000, 0x0},
                              {0x0, 0xABCD00000000ABCD},
                              {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BZ_DF(tz_d, kMSALanesDword, bz_d, int64_t)
#undef TEST_BZ_DF

  TestCaseMsaBranch tnz_v[] = {
      {0x0, 0x0}, {0xABC, 0x0}, {0x0, 0xABC}, {0xABC, 0xABC}};
  for (unsigned i = 0; i < arraysize(tnz_v); ++i) {
    run_bz_bnz(&tnz_v[i],
               [](MacroAssembler& assm, Label& br_target) {
                 __ bnz_v(w1, &br_target);
               },
               tnz_v[i].wt_lo != 0 || tnz_v[i].wt_hi != 0);
  }

#define TEST_BNZ_DF(input_array, lanes, instruction, int_type)        \
  for (unsigned i = 0; i < arraysize(input_array); ++i) {             \
    int j;                                                            \
    int_type* element = reinterpret_cast<int_type*>(&input_array[i]); \
    for (j = 0; j < lanes; ++j) {                                     \
      if (element[j] == 0) {                                          \
        break;                                                        \
      }                                                               \
    }                                                                 \
    run_bz_bnz(&input_array[i],                                       \
               [](MacroAssembler& assm, Label& br_target) {           \
                 __ instruction(w1, &br_target);                      \
               },                                                     \
               j == lanes);                                           \
  }
  TestCaseMsaBranch tnz_b[] = {{0x0, 0x0},
                               {0xBC0000, 0x0},
                               {0x0, 0xAB000000000000CD},
                               {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BNZ_DF(tnz_b, 16, bnz_b, int8_t)

  TestCaseMsaBranch tnz_h[] = {{0x0, 0x0},
                               {0xBCDE0000, 0x0},
                               {0x0, 0xABCD00000000ABCD},
                               {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BNZ_DF(tnz_h, 8, bnz_h, int16_t)

  TestCaseMsaBranch tnz_w[] = {{0x0, 0x0},
                               {0xBCDE123400000000, 0x0},
                               {0x0, 0x000000001234ABCD},
                               {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BNZ_DF(tnz_w, 4, bnz_w, int32_t)

  TestCaseMsaBranch tnz_d[] = {{0x0, 0x0},
                               {0xBCDE0000, 0x0},
                               {0x0, 0xABCD00000000ABCD},
                               {0x123456789ABCDEF0, 0xAAAAAAAAAAAAAAAA}};
  TEST_BNZ_DF(tnz_d, 2, bnz_d, int64_t)
#undef TEST_BNZ_DF
}

uint64_t run_jialc(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label main_block;
  __ push(ra);
  __ li(v0, 0l);
  __ beq(v0, v0, &main_block);
  __ nop();

  // Block 1
  __ addiu(v0, v0, 0x1);        // <-- offset = -40
  __ addiu(v0, v0, 0x2);
  __ jr(ra);
  __ nop();

  // Block 2
  __ addiu(v0, v0, 0x10);        // <-- offset = -24
  __ addiu(v0, v0, 0x20);
  __ jr(ra);
  __ nop();

  // Block 3 (Main)
  __ bind(&main_block);
  __ nal();  // t0 <- program counter
  __ mov(t0, ra);
  __ jialc(t0, offset);
  __ addiu(v0, v0, 0x4);
  __ pop(ra);
  __ jr(ra);
  __ nop();

  // Block 4
  __ addiu(v0, v0, 0x100);      // <-- offset = 20
  __ addiu(v0, v0, 0x200);
  __ jr(ra);
  __ nop();

  // Block 5
  __ addiu(v0, v0, 0x1000);     // <--- offset = 36
  __ addiu(v0, v0, 0x2000);
  __ jr(ra);
  __ nop();


  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_jialc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseJialc {
      // As rt will be used t0 register which will have value of
      // the program counter for the jialc instruction.
      int16_t   offset;
      uint32_t  expected_res;
    };

    struct TestCaseJialc tc[] = {
      // offset,   expected_res
      {     -40,            0x7 },
      {     -24,           0x34 },
      {      20,          0x304 },
      {      36,         0x3004 }
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseJialc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_jialc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


uint64_t run_addiupc(int32_t imm19) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ addiupc(v0, imm19);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);
  PC = (uint64_t)code->instruction_start();  // Set the program counter.

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_addiupc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseAddiupc {
      int32_t   imm19;
    };

    struct TestCaseAddiupc tc[] = {
      //  imm19
      { -262144 },   // 0x40000
      {      -1 },   // 0x7FFFF
      {       0 },
      {       1 },   // 0x00001
      {  262143 }    // 0x3FFFF
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseAddiupc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      PC = 0;
      uint64_t res = run_addiupc(tc[i].imm19);
      // Now, the program_counter (PC) is set.
      uint64_t expected_res = PC + (tc[i].imm19 << 2);
      CHECK_EQ(expected_res, res);
    }
  }
}


uint64_t run_ldpc(int offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  // 256k instructions; 2 * 2^7k = 2^8k
  // addiu t3, a4, 0xFFFF;  (0x250FFFFF)
  // ...
  // addiu t0, a4, 0x0000;  (0x250C0000)
  uint32_t addiu_start_1 = 0x25000000;
  for (int32_t i = 0xFFFFF; i >= 0xC0000; --i) {
    uint32_t addiu_new = addiu_start_1 + i;
    __ dd(addiu_new);
  }

  __ ldpc(t8, offset);  // offset 0; 0xEF080000 (t8 register)
  __ mov(v0, t8);

  // 256k instructions; 2 * 2^7k = 2^8k
  // addiu a4, a4, 0x0000;  (0x25080000)
  // ...
  // addiu a7, a4, 0xFFFF;  (0x250BFFFF)
  uint32_t addiu_start_2 = 0x25000000;
  for (int32_t i = 0x80000; i <= 0xBFFFF; ++i) {
    uint32_t addiu_new = addiu_start_2 + i;
    __ dd(addiu_new);
  }

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_ldpc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseLdpc {
      int       offset;
      uint64_t  expected_res;
    };

    auto doubleword = [](uint32_t word2, uint32_t word1) {
      if (kArchEndian == kLittle)
        return (static_cast<uint64_t>(word2) << 32) + word1;
      else
        return (static_cast<uint64_t>(word1) << 32) + word2;
    };

    TestCaseLdpc tc[] = {
        // offset,  expected_res
        {-131072, doubleword(0x250FFFFE, 0x250FFFFF)},
        {-4, doubleword(0x250C0006, 0x250C0007)},
        {-1, doubleword(0x250C0000, 0x250C0001)},
        {0, doubleword(0x03001025, 0xEF180000)},
        {1, doubleword(0x25080001, 0x25080000)},
        {4, doubleword(0x25080007, 0x25080006)},
        {131071, doubleword(0x250BFFFD, 0x250BFFFC)},
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseLdpc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      uint64_t res = run_ldpc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


int64_t run_bc(int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label continue_1, stop_execution;
  __ push(ra);
  __ li(v0, 0l);
  __ li(t8, 0l);
  __ li(t9, 2);   // Condition for the stopping execution.

  for (int32_t i = -100; i <= -11; ++i) {
    __ addiu(v0, v0, 1);
  }

  __ addiu(t8, t8, 1);              // -10

  __ beq(t8, t9, &stop_execution);  // -9
  __ nop();                         // -8
  __ beq(t8, t8, &continue_1);      // -7
  __ nop();                         // -6

  __ bind(&stop_execution);
  __ pop(ra);                       // -5, -4
  __ jr(ra);                        // -3
  __ nop();                         // -2

  __ bind(&continue_1);
  __ bc(offset);                    // -1

  for (int32_t i = 0; i <= 99; ++i) {
    __ addiu(v0, v0, 1);
  }

  __ pop(ra);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_bc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseBc {
      int32_t   offset;
      int64_t   expected_res;
    };

    struct TestCaseBc tc[] = {
      //    offset,   expected_result
      {       -100,   (abs(-100) - 10) * 2      },
      {        -11,   (abs(-100) - 10 + 1)      },
      {          0,   (abs(-100) - 10 + 1 + 99) },
      {          1,   (abs(-100) - 10 + 99)     },
      {         99,   (abs(-100) - 10 + 1)      },
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      int64_t res = run_bc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


int64_t run_balc(int32_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label continue_1;
  __ push(ra);
  __ li(v0, 0l);
  __ li(t8, 0l);
  __ li(t9, 2);   // Condition for stopping execution.

  __ beq(t8, t8, &continue_1);
  __ nop();

  uint32_t instruction_addiu = 0x24420001;  // addiu v0, v0, 1
  for (int32_t i = -117; i <= -57; ++i) {
    __ dd(instruction_addiu);
  }
  __ jr(ra);                        // -56
  __ nop();                         // -55

  for (int32_t i = -54; i <= -4; ++i) {
    __ dd(instruction_addiu);
  }
  __ jr(ra);                        // -3
  __ nop();                         // -2

  __ bind(&continue_1);
  __ balc(offset);                    // -1

  __ pop(ra);                         // 0, 1
  __ jr(ra);                          // 2
  __ nop();                           // 3

  for (int32_t i = 4; i <= 44; ++i) {
    __ dd(instruction_addiu);
  }
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  int64_t res = reinterpret_cast<int64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(r6_balc) {
  if (kArchVariant == kMips64r6) {
    CcTest::InitializeVM();

    struct TestCaseBalc {
      int32_t   offset;
      int64_t   expected_res;
    };

    struct TestCaseBalc tc[] = {
      //  offset,   expected_result
      {     -117,   61  },
      {      -54,   51  },
      {        0,   0   },
      {        4,   41  },
    };

    size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBalc);
    for (size_t i = 0; i < nr_test_cases; ++i) {
      int64_t res = run_balc(tc[i].offset);
      CHECK_EQ(tc[i].expected_res, res);
    }
  }
}


uint64_t run_dsll(uint64_t rt_value, uint16_t sa_value) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ dsll(v0, a0, sa_value);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F4>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(rt_value, 0, 0, 0, 0));

  return res;
}


TEST(dsll) {
  CcTest::InitializeVM();

  struct TestCaseDsll {
    uint64_t  rt_value;
    uint16_t  sa_value;
    uint64_t  expected_res;
  };

  // clang-format off
  struct TestCaseDsll tc[] = {
    // rt_value,           sa_value, expected_res
    {  0xFFFFFFFFFFFFFFFF,    0,      0xFFFFFFFFFFFFFFFF },
    {  0xFFFFFFFFFFFFFFFF,   16,      0xFFFFFFFFFFFF0000 },
    {  0xFFFFFFFFFFFFFFFF,   31,      0xFFFFFFFF80000000 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseDsll);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].expected_res,
            run_dsll(tc[i].rt_value, tc[i].sa_value));
  }
}


uint64_t run_bal(int16_t offset) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ mov(t0, ra);
  __ bal(offset);       // Equivalent for "BGEZAL zero_reg, offset".
  __ nop();

  __ mov(ra, t0);
  __ jr(ra);
  __ nop();

  __ li(v0, 1);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}


TEST(bal) {
  CcTest::InitializeVM();

  struct TestCaseBal {
    int16_t  offset;
    uint64_t  expected_res;
  };

  // clang-format off
  struct TestCaseBal tc[] = {
    // offset, expected_res
    {       4,            1 },
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseBal);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].expected_res, run_bal(tc[i].offset));
  }
}


TEST(Trampoline) {
  // Private member of Assembler class.
  static const int kMaxBranchOffset = (1 << (18 - 1)) - 1;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  Label done;
  size_t nr_calls = kMaxBranchOffset / (2 * kInstrSize) + 2;

  for (size_t i = 0; i < nr_calls; ++i) {
    __ BranchShort(&done, eq, a0, Operand(a1));
  }
  __ bind(&done);
  __ Ret(USE_DELAY_SLOT);
  __ mov(v0, zero_reg);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  int64_t res = reinterpret_cast<int64_t>(f.Call(42, 42, 0, 0, 0));
  CHECK_EQ(0, res);
}

TEST(Trampoline_with_massive_unbound_labels) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumSlots =
      MacroAssembler::kMaxBranchOffset / MacroAssembler::kTrampolineSlotsSize;
  Label labels[kNumSlots];

  {
    MacroAssembler::BlockTrampolinePoolScope block_trampoline_pool(&assm);
    for (int i = 0; i < kNumSlots; i++) {
      __ Branch(&labels[i]);
    }
  }

  __ bind(&labels[0]);
}

static void DummyFunction(Object result) {}

TEST(Call_with_trampoline) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  int next_buffer_check_ = v8_flags.force_long_branches
                               ? kMaxInt
                               : MacroAssembler::kMaxBranchOffset -
                                     MacroAssembler::kTrampolineSlotsSize * 16;

  Label done;
  __ Branch(&done);
  next_buffer_check_ -= MacroAssembler::kTrampolineSlotsSize;

  int num_nops = (next_buffer_check_ - __ pc_offset()) / kInstrSize - 1;
  for (int i = 0; i < num_nops; i++) {
    __ nop();
  }

  int pc_offset_before = __ pc_offset();
  {
    // There should be a trampoline after this Call
    __ Call(FUNCTION_ADDR(DummyFunction), RelocInfo::EXTERNAL_REFERENCE);
  }
  int pc_offset_after = __ pc_offset();
  int safepoint_pc_offset = __ pc_offset_for_safepoint();

  // Without trampoline, the Call emits no more than 8 instructions, otherwise
  // more than 8 instructions will be generated.
  int num_instrs = 8;
  // pc_offset_after records the offset after trampoline.
  CHECK_GT(pc_offset_after - pc_offset_before, num_instrs * kInstrSize);
  // safepoint_pc_offset records the offset before trampoline.
  CHECK_LE(safepoint_pc_offset - pc_offset_before, num_instrs * kInstrSize);

  __ bind(&done);
}

template <class T>
struct TestCaseMaddMsub {
  T fr, fs, ft, fd_add, fd_sub;
};

template <typename T, typename F>
void helper_madd_msub_maddf_msubf(F func) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  T x = std::sqrt(static_cast<T>(2.0));
  T y = std::sqrt(static_cast<T>(3.0));
  T z = std::sqrt(static_cast<T>(5.0));
  T x2 = 11.11, y2 = 22.22, z2 = 33.33;
  TestCaseMaddMsub<T> test_cases[] = {
      {x, y, z, 0.0, 0.0},
      {x, y, -z, 0.0, 0.0},
      {x, -y, z, 0.0, 0.0},
      {x, -y, -z, 0.0, 0.0},
      {-x, y, z, 0.0, 0.0},
      {-x, y, -z, 0.0, 0.0},
      {-x, -y, z, 0.0, 0.0},
      {-x, -y, -z, 0.0, 0.0},
      {-3.14, 0.2345, -123.000056, 0.0, 0.0},
      {7.3, -23.257, -357.1357, 0.0, 0.0},
      {x2, y2, z2, 0.0, 0.0},
      {x2, y2, -z2, 0.0, 0.0},
      {x2, -y2, z2, 0.0, 0.0},
      {x2, -y2, -z2, 0.0, 0.0},
      {-x2, y2, z2, 0.0, 0.0},
      {-x2, y2, -z2, 0.0, 0.0},
      {-x2, -y2, z2, 0.0, 0.0},
      {-x2, -y2, -z2, 0.0, 0.0},
  };

  if (std::is_same<T, float>::value) {
    __ Lwc1(f4, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fr)));
    __ Lwc1(f6, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fs)));
    __ Lwc1(f8, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, ft)));
    __ Lwc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fr)));
  } else if (std::is_same<T, double>::value) {
    __ Ldc1(f4, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fr)));
    __ Ldc1(f6, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fs)));
    __ Ldc1(f8, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, ft)));
    __ Ldc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<T>, fr)));
  } else {
    UNREACHABLE();
  }

  func(assm);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  const size_t kTableLength = sizeof(test_cases) / sizeof(TestCaseMaddMsub<T>);
  TestCaseMaddMsub<T> tc;
  for (size_t i = 0; i < kTableLength; i++) {
    tc.fr = test_cases[i].fr;
    tc.fs = test_cases[i].fs;
    tc.ft = test_cases[i].ft;

    f.Call(&tc, 0, 0, 0, 0);

    T res_sub;
    T res_add;
    if (kArchVariant != kMips64r6) {
      res_add = tc.fr + (tc.fs * tc.ft);
      res_sub = (tc.fs * tc.ft) - tc.fr;
    } else {
      res_add = std::fma(tc.fs, tc.ft, tc.fr);
      res_sub = std::fma(-tc.fs, tc.ft, tc.fr);
    }

    CHECK_EQ(tc.fd_add, res_add);
    CHECK_EQ(tc.fd_sub, res_sub);
  }
}

TEST(madd_msub_s) {
  if (kArchVariant == kMips64r6) return;
  helper_madd_msub_maddf_msubf<float>([](MacroAssembler& assm) {
    __ Madd_s(f10, f4, f6, f8, f12);
    __ Swc1(f10, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_add)));
    __ Msub_s(f16, f4, f6, f8, f12);
    __ Swc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_sub)));
  });
}

TEST(madd_msub_d) {
  if (kArchVariant == kMips64r6) return;
  helper_madd_msub_maddf_msubf<double>([](MacroAssembler& assm) {
    __ Madd_d(f10, f4, f6, f8, f12);
    __ Sdc1(f10, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_add)));
    __ Msub_d(f16, f4, f6, f8, f12);
    __ Sdc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_sub)));
  });
}

TEST(maddf_msubf_s) {
  if (kArchVariant != kMips64r6) return;
  helper_madd_msub_maddf_msubf<float>([](MacroAssembler& assm) {
    __ maddf_s(f4, f6, f8);
    __ Swc1(f4, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_add)));
    __ msubf_s(f16, f6, f8);
    __ Swc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<float>, fd_sub)));
  });
}

TEST(maddf_msubf_d) {
  if (kArchVariant != kMips64r6) return;
  helper_madd_msub_maddf_msubf<double>([](MacroAssembler& assm) {
    __ maddf_d(f4, f6, f8);
    __ Sdc1(f4, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_add)));
    __ msubf_d(f16, f6, f8);
    __ Sdc1(f16, MemOperand(a0, offsetof(TestCaseMaddMsub<double>, fd_sub)));
  });
}

uint64_t run_Subu(uint64_t imm, int32_t num_instr) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label code_start;
  __ bind(&code_start);
  __ Subu(v0, zero_reg, Operand(imm));
  CHECK_EQ(assm.InstructionsGeneratedSince(&code_start), num_instr);
  __ jr(ra);
  __ nop();

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

TEST(Subu) {
  CcTest::InitializeVM();

  // Test Subu macro-instruction for min_int16 and max_int16 border cases.
  // For subtracting int16 immediate values we use addiu.

  struct TestCaseSubu {
    uint64_t imm;
    uint64_t expected_res;
    int32_t num_instr;
  };

  // We call Subu(v0, zero_reg, imm) to test cases listed below.
  // 0 - imm = expected_res
  // clang-format off
  struct TestCaseSubu tc[] = {
      //              imm, expected_res, num_instr
      {0xFFFFFFFFFFFF8000,       0x8000,         2},  // min_int16
      // The test case above generates ori + addu instruction sequence.
      // We can't have just addiu because -min_int16 > max_int16 so use
      // register. We can load min_int16 to at register with addiu and then
      // subtract at with subu, but now we use ori + addu because -min_int16 can
      // be loaded using ori.
      {0x8000,       0xFFFFFFFFFFFF8000,         1},  // max_int16 + 1
      // Generates addiu
      // max_int16 + 1 is not int16 but -(max_int16 + 1) is, just use addiu.
      {0xFFFFFFFFFFFF7FFF,       0x8001,         2},  // min_int16 - 1
      // Generates ori + addu
      // To load this value to at we need two instructions and another one to
      // subtract, lui + ori + subu. But we can load -value to at using just
      // ori and then add at register with addu.
      {0x8001,       0xFFFFFFFFFFFF7FFF,         2},  // max_int16 + 2
      // Generates ori + subu
      // Not int16 but is uint16, load value to at with ori and subtract with
      // subu.
      {0x00010000,   0xFFFFFFFFFFFF0000,         2},
      // Generates lui + subu
      // Load value using lui to at and subtract with subu.
      {0x00010001,   0xFFFFFFFFFFFEFFFF,         3},
      // Generates lui + ori + subu
      // We have to generate three instructions in this case.
      {0x7FFFFFFF,   0xFFFFFFFF80000001,         3},  // max_int32
      // Generates lui + ori + subu
      {0xFFFFFFFF80000000, 0xFFFFFFFF80000000,   2},  // min_int32
      // The test case above generates lui + subu intruction sequence.
      // The result of 0 - min_int32 eqauls max_int32 + 1, which wraps around to
      // min_int32 again.
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseSubu);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].expected_res, run_Subu(tc[i].imm, tc[i].num_instr));
  }
}

uint64_t run_Dsubu(uint64_t imm, int32_t num_instr) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  Label code_start;
  __ bind(&code_start);
  __ Dsubu(v0, zero_reg, Operand(imm));
  CHECK_EQ(assm.InstructionsGeneratedSince(&code_start), num_instr);
  __ jr(ra);
  __ nop();

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

TEST(Dsubu) {
  CcTest::InitializeVM();

  // Test Dsubu macro-instruction for min_int16 and max_int16 border cases.
  // For subtracting int16 immediate values we use daddiu.

  struct TestCaseDsubu {
    uint64_t imm;
    uint64_t expected_res;
    int32_t num_instr;
  };

  // We call Dsubu(v0, zero_reg, imm) to test cases listed below.
  // 0 - imm = expected_res
  // clang-format off
  struct TestCaseDsubu tc[] = {
      //        imm, expected_res, num_instr
      {0xFFFFFFFFFFFF8000, 0x8000, 2},  // min_int16
      // The test case above generates daddiu + dsubu instruction sequence.
      // We can't have just daddiu because -min_int16 > max_int16 so use
      // register, but we can load min_int16 to at register with daddiu and then
      // subtract at with dsubu.
      {0x8000, 0xFFFFFFFFFFFF8000, 1},  // max_int16 + 1
      // Generates daddiu
      // max_int16 + 1 is not int16 but -(max_int16 + 1) is, just use daddiu.
      {0xFFFFFFFFFFFF7FFF, 0x8001, 2},  // min_int16 - 1
      // Generates ori + daddu
      // To load this value to at we need two instructions and another one to
      // subtract, lui + ori + dsubu. But we can load -value to at using just
      // ori and then dadd at register with daddu.
      {0x8001, 0xFFFFFFFFFFFF7FFF, 2},  // max_int16 + 2
      // Generates ori + dsubu
      // Not int16 but is uint16, load value to at with ori and subtract with
      // dsubu.
      {0x00010000, 0xFFFFFFFFFFFF0000, 2},
      // Generates lui + dsubu
      // Load value using lui to at and subtract with dsubu.
      {0x00010001, 0xFFFFFFFFFFFEFFFF, 3},
      // Generates lui + ori + dsubu
      // We have to generate three instructions in this case.
      {0x7FFFFFFF, 0xFFFFFFFF80000001, 3},  // max_int32
      // Generates lui + ori + dsubu
      {0xFFFFFFFF80000000, 0x0000000080000000, 2},  // min_int32
      // Generates lui + dsubu
      // The result of 0 - min_int32 eqauls max_int32 + 1, which fits into a 64
      // bit register, Dsubu gives a different result here.
      {0x7FFFFFFFFFFFFFFF, 0x8000000000000001, 3},  // max_int64
      // r2 - Generates daddiu + dsrl + dsubu
      // r6 - Generates daddiu + dati + dsubu
      {0x8000000000000000, 0x8000000000000000, 3},  // min_int64
      // The test case above generates:
      // r2 - daddiu + dsll32 + dsubu instruction sequence,
      // r6 - ori + dati + dsubu.
      // The result of 0 - min_int64 eqauls max_int64 + 1, which wraps around to
      // min_int64 again.
      {0xFFFF0000FFFFFFFF, 0x0000FFFF00000001, 4},
      // The test case above generates:
      // r2 - ori + dsll32 + ori + daddu instruction sequence,
      // r6 - daddiu + dahi + dati + dsubu.
      // For r2 loading imm would take more instructions than loading -imm so we
      // can load -imm and add with daddu.
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseDsubu);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].expected_res, run_Dsubu(tc[i].imm, tc[i].num_instr));
  }
}

uint64_t run_Dins(uint64_t imm, uint64_t source, uint16_t pos, uint16_t size) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(v0, imm);
  __ li(t0, source);
  __ Dins(v0, t0, pos, size);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(Dins) {
  CcTest::InitializeVM();

  // Test Dins macro-instruction.

  struct TestCaseDins {
    uint64_t imm;
    uint64_t source;
    uint16_t pos;
    uint16_t size;
    uint64_t expected_res;
  };

  // We load imm to v0 and source to t0 and then call
  // Dins(v0, t0, pos, size) to test cases listed below.
  // clang-format off
  struct TestCaseDins tc[] = {
      // imm, source, pos, size, expected_res
      {0x5555555555555555, 0x1ABCDEF01, 31, 1, 0x55555555D5555555},
      {0x5555555555555555, 0x1ABCDEF02, 30, 2, 0x5555555595555555},
      {0x201234567, 0x1FABCDEFF, 0, 32, 0x2FABCDEFF},
      {0x201234567, 0x7FABCDEFF, 31, 2, 0x381234567},
      {0x800000000, 0x7FABCDEFF, 0, 33, 0x9FABCDEFF},
      {0x1234, 0xABCDABCDABCDABCD, 0, 64, 0xABCDABCDABCDABCD},
      {0xABCD, 0xABCEABCF, 32, 1, 0x10000ABCD},
      {0xABCD, 0xABCEABCF, 63, 1, 0x800000000000ABCD},
      {0x10000ABCD, 0xABC1ABC2ABC3ABC4, 32, 32, 0xABC3ABC40000ABCD},
  };
  // clang-format on

  size_t nr_test_cases = sizeof(tc) / sizeof(TestCaseDins);
  for (size_t i = 0; i < nr_test_cases; ++i) {
    CHECK_EQ(tc[i].expected_res,
             run_Dins(tc[i].imm, tc[i].source, tc[i].pos, tc[i].size));
  }
}

uint64_t run_Ins(uint64_t imm, uint64_t source, uint16_t pos, uint16_t size) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(v0, imm);
  __ li(t0, source);
  __ Ins(v0, t0, pos, size);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(Ins) {
  CcTest::InitializeVM();

  //       run_Ins(rt_value, rs_value, pos, size),
  //       expected_result
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFFABCDEF01, 31, 1),
           0xFFFFFFFFD5555555);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFFABCDEF02, 30, 2),
           0xFFFFFFFF95555555);
  CHECK_EQ(run_Ins(0x0000000001234567, 0xFFFFFFFFFABCDEFF, 0, 32),
           0xFFFFFFFFFABCDEFF);

  // Results with positive sign.
  CHECK_EQ(run_Ins(0x0000000055555550, 0xFFFFFFFF80000001, 0, 1),
           0x0000000055555551);
  CHECK_EQ(run_Ins(0x0000000055555555, 0x0000000040000001, 0, 32),
           0x0000000040000001);
  CHECK_EQ(run_Ins(0x0000000055555555, 0x0000000020000001, 1, 31),
           0x0000000040000003);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80700001, 8, 24),
           0x0000000070000155);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80007001, 16, 16),
           0x0000000070015555);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80000071, 24, 8),
           0x0000000071555555);
  CHECK_EQ(run_Ins(0x0000000075555555, 0x0000000040000000, 31, 1),
           0x0000000075555555);

  // Results with negative sign.
  CHECK_EQ(run_Ins(0xFFFFFFFF85555550, 0xFFFFFFFF80000001, 0, 1),
           0xFFFFFFFF85555551);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80000001, 0, 32),
           0xFFFFFFFF80000001);
  CHECK_EQ(run_Ins(0x0000000055555555, 0x0000000040000001, 1, 31),
           0xFFFFFFFF80000003);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80800001, 8, 24),
           0xFFFFFFFF80000155);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80008001, 16, 16),
           0xFFFFFFFF80015555);
  CHECK_EQ(run_Ins(0x0000000055555555, 0xFFFFFFFF80000081, 24, 8),
           0xFFFFFFFF81555555);
  CHECK_EQ(run_Ins(0x0000000075555555, 0x0000000000000001, 31, 1),
           0xFFFFFFFFF5555555);
}

uint64_t run_Ext(uint64_t source, uint16_t pos, uint16_t size) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);

  __ li(v0, 0xFFFFFFFFFFFFFFFF);
  __ li(t0, source);
  __ Ext(v0, t0, pos, size);
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  auto f = GeneratedCode<F2>::FromCode(isolate, *code);

  uint64_t res = reinterpret_cast<uint64_t>(f.Call(0, 0, 0, 0, 0));

  return res;
}

TEST(Ext) {
  CcTest::InitializeVM();

  // Source values with negative sign.
  //       run_Ext(rs_value, pos, size), expected_result
  CHECK_EQ(run_Ext(0xFFFFFFFF80000001, 0, 1), 0x0000000000000001);
  CHECK_EQ(run_Ext(0xFFFFFFFF80000001, 0, 32), 0xFFFFFFFF80000001);
  CHECK_EQ(run_Ext(0xFFFFFFFF80000002, 1, 31), 0x0000000040000001);
  CHECK_EQ(run_Ext(0xFFFFFFFF80000100, 8, 24), 0x0000000000800001);
  CHECK_EQ(run_Ext(0xFFFFFFFF80010000, 16, 16), 0x0000000000008001);
  CHECK_EQ(run_Ext(0xFFFFFFFF81000000, 24, 8), 0x0000000000000081);
  CHECK_EQ(run_Ext(0xFFFFFFFF80000000, 31, 1), 0x0000000000000001);

  // Source values with positive sign.
  CHECK_EQ(run_Ext(0x0000000000000001, 0, 1), 0x0000000000000001);
  CHECK_EQ(run_Ext(0x0000000040000001, 0, 32), 0x0000000040000001);
  CHECK_EQ(run_Ext(0x0000000040000002, 1, 31), 0x0000000020000001);
  CHECK_EQ(run_Ext(0x0000000040000100, 8, 24), 0x0000000000400001);
  CHECK_EQ(run_Ext(0x0000000040010000, 16, 16), 0x0000000000004001);
  CHECK_EQ(run_Ext(0x0000000041000000, 24, 8), 0x0000000000000041);
  CHECK_EQ(run_Ext(0x0000000040000000, 31, 1), 0x0000000000000000);
}

TEST(MSA_fill_copy) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint64_t u8;
    uint64_t u16;
    uint64_t u32;
    uint64_t s8;
    uint64_t s16;
    uint64_t s32;
    uint64_t s64;
  };
  T t;

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  {
    CpuFeatureScope fscope(&assm, MIPS_SIMD);

    __ li(t0, 0x9E7689ACA512B683);

    __ fill_b(w0, t0);
    __ fill_h(w2, t0);
    __ fill_w(w4, t0);
    __ fill_d(w6, t0);
    __ copy_u_b(t1, w0, 11);
    __ sd(t1, MemOperand(a0, offsetof(T, u8)));
    __ copy_u_h(t1, w2, 6);
    __ sd(t1, MemOperand(a0, offsetof(T, u16)));
    __ copy_u_w(t1, w4, 3);
    __ sd(t1, MemOperand(a0, offsetof(T, u32)));

    __ copy_s_b(t1, w0, 8);
    __ sd(t1, MemOperand(a0, offsetof(T, s8)));
    __ copy_s_h(t1, w2, 5);
    __ sd(t1, MemOperand(a0, offsetof(T, s16)));
    __ copy_s_w(t1, w4, 1);
    __ sd(t1, MemOperand(a0, offsetof(T, s32)));
    __ copy_s_d(t1, w6, 0);
    __ sd(t1, MemOperand(a0, offsetof(T, s64)));

    __ jr(ra);
    __ nop();
  }

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&t, 0, 0, 0, 0);

  CHECK_EQ(0x83u, t.u8);
  CHECK_EQ(0xB683u, t.u16);
  CHECK_EQ(0xA512B683u, t.u32);
  CHECK_EQ(0xFFFFFFFFFFFFFF83u, t.s8);
  CHECK_EQ(0xFFFFFFFFFFFFB683u, t.s16);
  CHECK_EQ(0xFFFFFFFFA512B683u, t.s32);
  CHECK_EQ(0x9E7689ACA512B683u, t.s64);
}

TEST(MSA_fill_copy_2) {
  // Similar to MSA_fill_copy test, but also check overlaping between MSA and
  // FPU registers with same numbers
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint64_t d0;
    uint64_t d1;
  };
  T t[2];

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  {
    CpuFeatureScope fscope(&assm, MIPS_SIMD);

    __ li(t0, 0xAAAAAAAAAAAAAAAA);
    __ li(t1, 0x5555555555555555);

    __ fill_d(w0, t0);
    __ fill_d(w2, t0);

    __ Move(f0, t1);
    __ Move(f2, t1);

#define STORE_MSA_REG(w_reg, base, scratch)          \
  __ copy_s_d(scratch, w_reg, 0);                    \
  __ sd(scratch, MemOperand(base, offsetof(T, d0))); \
  __ copy_s_d(scratch, w_reg, 1);                    \
  __ sd(scratch, MemOperand(base, offsetof(T, d1)));

    STORE_MSA_REG(w0, a0, t2)
    STORE_MSA_REG(w2, a1, t2)
#undef STORE_MSA_REG

    __ jr(ra);
    __ nop();
  }

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F5>::FromCode(isolate, *code);

  f.Call(&t[0], &t[1], 0, 0, 0);

  CHECK_EQ(0x5555555555555555, t[0].d0);
  CHECK_EQ(0xAAAAAAAAAAAAAAAA, t[0].d1);
  CHECK_EQ(0x5555555555555555, t[1].d0);
  CHECK_EQ(0xAAAAAAAAAAAAAAAA, t[1].d1);
}

TEST(MSA_fill_copy_3) {
  // Similar to MSA_fill_copy test, but also check overlaping between MSA and
  // FPU registers with same numbers
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint64_t d0;
    uint64_t d1;
  };
  T t[2];

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  {
    CpuFeatureScope fscope(&assm, MIPS_SIMD);

    __ li(t0, 0xAAAAAAAAAAAAAAAA);
    __ li(t1, 0x5555555555555555);

    __ Move(f0, t0);
    __ Move(f2, t0);

    __ fill_d(w0, t1);
    __ fill_d(w2, t1);

    __ Sdc1(f0, MemOperand(a0, offsetof(T, d0)));
    __ Sdc1(f2, MemOperand(a1, offsetof(T, d0)));

    __ jr(ra);
    __ nop();
  }

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F5>::FromCode(isolate, *code);

  f.Call(&t[0], &t[1], 0, 0, 0);

  CHECK_EQ(0x5555555555555555, t[0].d0);
  CHECK_EQ(0x5555555555555555, t[1].d0);
}


template <typename T>
void run_msa_insert(int64_t rs_value, int n, msa_reg_t* w) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);

  __ li(t0, -1);
  __ li(t1, rs_value);
  __ fill_w(w0, t0);

  if (std::is_same<T, int8_t>::value) {
    DCHECK_LT(n, 16);
    __ insert_b(w0, n, t1);
  } else if (std::is_same<T, int16_t>::value) {
    DCHECK_LT(n, 8);
    __ insert_h(w0, n, t1);
  } else if (std::is_same<T, int32_t>::value) {
    DCHECK_LT(n, 4);
    __ insert_w(w0, n, t1);
  } else if (std::is_same<T, int64_t>::value) {
    DCHECK_LT(n, 2);
    __ insert_d(w0, n, t1);
  } else {
    UNREACHABLE();
  }

  store_elements_of_vector(&assm, w0, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(w, 0, 0, 0, 0);
}

TEST(MSA_insert) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseInsert {
    uint64_t input;
    int n;
    uint64_t exp_res_lo;
    uint64_t exp_res_hi;
  };

  // clang-format off
  struct TestCaseInsert tc_b[] = {
    // input, n,          exp_res_lo,          exp_res_hi
    {  0xA2, 13, 0xFFFFFFFFFFFFFFFFu, 0xFFFFA2FFFFFFFFFFu},
    {  0x73, 10, 0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFFF73FFFFu},
    {0x3494,  5, 0xFFFF94FFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu},
    {0xA6B8,  1, 0xFFFFFFFFFFFFB8FFu, 0xFFFFFFFFFFFFFFFFu}
  };
  // clang-format off

  for (size_t i = 0; i < sizeof(tc_b) / sizeof(TestCaseInsert); ++i) {
    msa_reg_t res;
    run_msa_insert<int8_t>(tc_b[i].input, tc_b[i].n, &res);
    CHECK_EQ(tc_b[i].exp_res_lo, res.d[0]);
    CHECK_EQ(tc_b[i].exp_res_hi, res.d[1]);
  }

  // clang-format off
  struct TestCaseInsert tc_h[] = {
    // input, n,          exp_res_lo,          exp_res_hi
    {0x85A2,  7, 0xFFFFFFFFFFFFFFFFu, 0x85A2FFFFFFFFFFFFu},
    {0xE873,  5, 0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFFE873FFFFu},
    {0x3494,  3, 0x3494FFFFFFFFFFFFu, 0xFFFFFFFFFFFFFFFFu},
    {0xA6B8,  1, 0xFFFFFFFFA6B8FFFFu, 0xFFFFFFFFFFFFFFFFu}
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc_h) / sizeof(TestCaseInsert); ++i) {
    msa_reg_t res;
    run_msa_insert<int16_t>(tc_h[i].input, tc_h[i].n, &res);
    CHECK_EQ(tc_h[i].exp_res_lo, res.d[0]);
    CHECK_EQ(tc_h[i].exp_res_hi, res.d[1]);
  }

  // clang-format off
  struct TestCaseInsert tc_w[] = {
    //     input, n,          exp_res_lo,          exp_res_hi
    {0xD2F085A2u, 3, 0xFFFFFFFFFFFFFFFFu, 0xD2F085A2FFFFFFFFu},
    {0x4567E873u, 2, 0xFFFFFFFFFFFFFFFFu, 0xFFFFFFFF4567E873u},
    {0xACDB3494u, 1, 0xACDB3494FFFFFFFFu, 0xFFFFFFFFFFFFFFFFu},
    {0x89ABA6B8u, 0, 0xFFFFFFFF89ABA6B8u, 0xFFFFFFFFFFFFFFFFu}
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc_w) / sizeof(TestCaseInsert); ++i) {
    msa_reg_t res;
    run_msa_insert<int32_t>(tc_w[i].input, tc_w[i].n, &res);
    CHECK_EQ(tc_w[i].exp_res_lo, res.d[0]);
    CHECK_EQ(tc_w[i].exp_res_hi, res.d[1]);
  }

  // clang-format off
  struct TestCaseInsert tc_d[] = {
    //            input, n,          exp_res_lo,          exp_res_hi
    {0xF35862E13E38F8B0, 1, 0xFFFFFFFFFFFFFFFFu, 0xF35862E13E38F8B0},
    {0x4F41FFDEF2BFE636, 0,  0x4F41FFDEF2BFE636, 0xFFFFFFFFFFFFFFFFu}
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseInsert); ++i) {
    msa_reg_t res;
    run_msa_insert<int64_t>(tc_d[i].input, tc_d[i].n, &res);
    CHECK_EQ(tc_d[i].exp_res_lo, res.d[0]);
    CHECK_EQ(tc_d[i].exp_res_hi, res.d[1]);
  }
}

void run_msa_ctc_cfc(uint64_t value) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);

  MSAControlRegister msareg = {kMSACSRRegister};
  __ li(t0, value);
  __ li(t2, 0l);
  __ cfcmsa(t1, msareg);
  __ ctcmsa(msareg, t0);
  __ cfcmsa(t2, msareg);
  __ ctcmsa(msareg, t1);
  __ sd(t2, MemOperand(a0, 0));
  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  uint64_t res;
  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(
      base::bit_cast<uint64_t>(static_cast<int64_t>(
          base::bit_cast<int32_t>(static_cast<uint32_t>(value & 0x0167FFFF)))),
      res);
}

TEST(MSA_move_v) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint64_t ws_lo;
    uint64_t ws_hi;
    uint64_t wd_lo;
    uint64_t wd_hi;
  };
  T t[] = {{0x20B9CC4F1A83E0C5, 0xA27E1B5F2F5BB18A, 0x1E86678B52F8E1FF,
            0x706E51290AC76FB9},
           {0x4414AED7883FFD18, 0x047D183A06B67016, 0x4EF258CF8D822870,
            0x2686B73484C2E843},
           {0xD38FF9D048884FFC, 0x6DC63A57C0943CA7, 0x8520CA2F3E97C426,
            0xA9913868FB819C59}};

  for (unsigned i = 0; i < arraysize(t); ++i) {
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    CpuFeatureScope fscope(&assm, MIPS_SIMD);

    load_elements_of_vector(&assm, &t[i].ws_lo, w0, t0, t1);
    load_elements_of_vector(&assm, &t[i].wd_lo, w2, t0, t1);
    __ move_v(w2, w0);
    store_elements_of_vector(&assm, w2, a0);

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
    Print(*code);
#endif
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    f.Call(&t[i].wd_lo, 0, 0, 0, 0);
    CHECK_EQ(t[i].ws_lo, t[i].wd_lo);
    CHECK_EQ(t[i].ws_hi, t[i].wd_hi);
  }
}

template <typename ExpectFunc, typename OperFunc>
void run_msa_sldi(OperFunc GenerateOperation,
                  ExpectFunc GenerateExpectedResult) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint64_t ws_lo;
    uint64_t ws_hi;
    uint64_t wd_lo;
    uint64_t wd_hi;
  };
  T t[] = {{0x20B9CC4F1A83E0C5, 0xA27E1B5F2F5BB18A, 0x1E86678B52F8E1FF,
            0x706E51290AC76FB9},
           {0x4414AED7883FFD18, 0x047D183A06B67016, 0x4EF258CF8D822870,
            0x2686B73484C2E843},
           {0xD38FF9D048884FFC, 0x6DC63A57C0943CA7, 0x8520CA2F3E97C426,
            0xA9913868FB819C59}};
  uint64_t res[2];

  for (unsigned i = 0; i < arraysize(t); ++i) {
    MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
    CpuFeatureScope fscope(&assm, MIPS_SIMD);
    load_elements_of_vector(&assm, &t[i].ws_lo, w0, t0, t1);
    load_elements_of_vector(&assm, &t[i].wd_lo, w2, t0, t1);
    GenerateOperation(assm);
    store_elements_of_vector(&assm, w2, a0);

    __ jr(ra);
    __ nop();

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
    Print(*code);
#endif
    auto f = GeneratedCode<F3>::FromCode(isolate, *code);
    f.Call(&res[0], 0, 0, 0, 0);
    GenerateExpectedResult(reinterpret_cast<uint8_t*>(&t[i].ws_lo),
                           reinterpret_cast<uint8_t*>(&t[i].wd_lo));
    CHECK_EQ(res[0], t[i].wd_lo);
    CHECK_EQ(res[1], t[i].wd_hi);
  }
}

TEST(MSA_sldi) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;
  CcTest::InitializeVM();

#define SLDI_DF(s, k)                \
  uint8_t v[32];                     \
  for (unsigned i = 0; i < s; i++) { \
    v[i] = ws[s * k + i];            \
    v[i + s] = wd[s * k + i];        \
  }                                  \
  for (unsigned i = 0; i < s; i++) { \
    wd[s * k + i] = v[i + n];        \
  }

  for (int n = 0; n < 16; ++n) {
    run_msa_sldi([n](MacroAssembler& assm) { __ sldi_b(w2, w0, n); },
                 [n](uint8_t* ws, uint8_t* wd) {
                   SLDI_DF(kMSARegSize / sizeof(int8_t) / kBitsPerByte, 0)
                 });
  }

  for (int n = 0; n < 8; ++n) {
    run_msa_sldi([n](MacroAssembler& assm) { __ sldi_h(w2, w0, n); },
                 [n](uint8_t* ws, uint8_t* wd) {
                   for (int k = 0; k < 2; ++k) {
                     SLDI_DF(kMSARegSize / sizeof(int16_t) / kBitsPerByte, k)
                   }
                 });
  }

  for (int n = 0; n < 4; ++n) {
    run_msa_sldi([n](MacroAssembler& assm) { __ sldi_w(w2, w0, n); },
                 [n](uint8_t* ws, uint8_t* wd) {
                   for (int k = 0; k < 4; ++k) {
                     SLDI_DF(kMSARegSize / sizeof(int32_t) / kBitsPerByte, k)
                   }
                 });
  }

  for (int n = 0; n < 2; ++n) {
    run_msa_sldi([n](MacroAssembler& assm) { __ sldi_d(w2, w0, n); },
                 [n](uint8_t* ws, uint8_t* wd) {
                   for (int k = 0; k < 8; ++k) {
                     SLDI_DF(kMSARegSize / sizeof(int64_t) / kBitsPerByte, k)
                   }
                 });
  }
#undef SLDI_DF
}

TEST(MSA_cfc_ctc) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const uint64_t mask_without_cause = 0xFFFFFFFFFF9C0FFF;
  const uint64_t mask_always_zero = 0x0167FFFF;
  const uint64_t mask_enables = 0x0000000000000F80;
  uint64_t test_case[] = {0x30C6F6352D5EDE31, 0xEFC9FED507955425,
                          0x64F2A3FF15B7DBE3, 0x6AA069352BF8BC37,
                          0x7EA7AB2AE6AAE923, 0xA10F5D4C24D0F68D,
                          0x6DD14C9441AFA84C, 0xC366373B2D6BF64F,
                          0x6B35FB04925014BD, 0x9E3EA39A4DBA7E61};
  for (unsigned i = 0; i < arraysize(test_case); i++) {
    // Setting enable bits and corresponding cause bits could result in
    // exception raised and this prevents that from happening
    test_case[i] = (~test_case[i] & mask_enables) << 5 |
                   (test_case[i] & mask_without_cause);
    run_msa_ctc_cfc(test_case[i] & mask_always_zero);
  }
}

struct ExpResShf {
  uint8_t i8;
  uint64_t lo;
  uint64_t hi;
};

void run_msa_i8(SecondaryField opcode, uint64_t ws_lo, uint64_t ws_hi,
                uint8_t i8) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;
  uint64_t wd_lo = 0xF35862E13E38F8B0;
  uint64_t wd_hi = 0x4F41FFDEF2BFE636;

#define LOAD_W_REG(lo, hi, w_reg) \
  __ li(t0, lo);                  \
  __ li(t1, hi);                  \
  __ insert_d(w_reg, 0, t0);      \
  __ insert_d(w_reg, 1, t1);

  LOAD_W_REG(ws_lo, ws_hi, w0)

  switch (opcode) {
    case ANDI_B:
      __ andi_b(w2, w0, i8);
      break;
    case ORI_B:
      __ ori_b(w2, w0, i8);
      break;
    case NORI_B:
      __ nori_b(w2, w0, i8);
      break;
    case XORI_B:
      __ xori_b(w2, w0, i8);
      break;
    case BMNZI_B:
      LOAD_W_REG(wd_lo, wd_hi, w2);
      __ bmnzi_b(w2, w0, i8);
      break;
    case BMZI_B:
      LOAD_W_REG(wd_lo, wd_hi, w2);
      __ bmzi_b(w2, w0, i8);
      break;
    case BSELI_B:
      LOAD_W_REG(wd_lo, wd_hi, w2);
      __ bseli_b(w2, w0, i8);
      break;
    case SHF_B:
      __ shf_b(w2, w0, i8);
      break;
    case SHF_H:
      __ shf_h(w2, w0, i8);
      break;
    case SHF_W:
      __ shf_w(w2, w0, i8);
      break;
    default:
      UNREACHABLE();
  }

  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

#undef LOAD_W_REG

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  uint64_t mask = i8 * 0x0101010101010101ull;
  switch (opcode) {
    case ANDI_B:
      CHECK_EQ(ws_lo & mask, res.d[0]);
      CHECK_EQ(ws_hi & mask, res.d[1]);
      break;
    case ORI_B:
      CHECK_EQ(ws_lo | mask, res.d[0]);
      CHECK_EQ(ws_hi | mask, res.d[1]);
      break;
    case NORI_B:
      CHECK_EQ(~(ws_lo | mask), res.d[0]);
      CHECK_EQ(~(ws_hi | mask), res.d[1]);
      break;
    case XORI_B:
      CHECK_EQ(ws_lo ^ mask, res.d[0]);
      CHECK_EQ(ws_hi ^ mask, res.d[1]);
      break;
    case BMNZI_B:
      CHECK_EQ((ws_lo & mask) | (wd_lo & ~mask), res.d[0]);
      CHECK_EQ((ws_hi & mask) | (wd_hi & ~mask), res.d[1]);
      break;
    case BMZI_B:
      CHECK_EQ((ws_lo & ~mask) | (wd_lo & mask), res.d[0]);
      CHECK_EQ((ws_hi & ~mask) | (wd_hi & mask), res.d[1]);
      break;
    case BSELI_B:
      CHECK_EQ((ws_lo & ~wd_lo) | (mask & wd_lo), res.d[0]);
      CHECK_EQ((ws_hi & ~wd_hi) | (mask & wd_hi), res.d[1]);
      break;
    case SHF_B: {
      struct ExpResShf exp_b[] = {
          //  i8,             exp_lo,             exp_hi
          {0xFFu, 0x11111111B9B9B9B9, 0xF7F7F7F7C8C8C8C8},
          {0x0u, 0x62626262DFDFDFDF, 0xD6D6D6D6C8C8C8C8},
          {0xE4u, 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636},
          {0x1Bu, 0x1B756911C3D9A7B9, 0xAE94A5F79C8AEFC8},
          {0xB1u, 0x662B6253E8C4DF12, 0x0D3AD6803F8BC88B},
          {0x4Eu, 0x62E1F358F8B03E38, 0xFFDE4F41E636F2BF},
          {0x27u, 0x1B697511C3A7D9B9, 0xAEA594F79CEF8AC8}};
      for (size_t i = 0; i < sizeof(exp_b) / sizeof(ExpResShf); ++i) {
        if (exp_b[i].i8 == i8) {
          CHECK_EQ(exp_b[i].lo, res.d[0]);
          CHECK_EQ(exp_b[i].hi, res.d[1]);
        }
      }
    } break;
    case SHF_H: {
      struct ExpResShf exp_h[] = {
          //  i8,             exp_lo,             exp_hi
          {0xFFu, 0x1169116911691169, 0xF7A5F7A5F7A5F7A5},
          {0x0u, 0x12DF12DF12DF12DF, 0x8BC88BC88BC88BC8},
          {0xE4u, 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636},
          {0x1Bu, 0xD9C3B9A7751B1169, 0x8A9CC8EF94AEF7A5},
          {0xB1u, 0x53622B6612DFC4E8, 0x80D63A0D8BC88B3F},
          {0x4Eu, 0x3E38F8B0F35862E1, 0xF2BFE6364F41FFDE},
          {0x27u, 0xD9C3751BB9A71169, 0x8A9C94AEC8EFF7A5}};
      for (size_t i = 0; i < sizeof(exp_h) / sizeof(ExpResShf); ++i) {
        if (exp_h[i].i8 == i8) {
          CHECK_EQ(exp_h[i].lo, res.d[0]);
          CHECK_EQ(exp_h[i].hi, res.d[1]);
        }
      }
    } break;
    case SHF_W: {
      struct ExpResShf exp_w[] = {
          //  i8,             exp_lo,             exp_hi
          {0xFFu, 0xF7A594AEF7A594AE, 0xF7A594AEF7A594AE},
          {0x0u, 0xC4E812DFC4E812DF, 0xC4E812DFC4E812DF},
          {0xE4u, 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636},
          {0x1Bu, 0xC8EF8A9CF7A594AE, 0xB9A7D9C31169751B},
          {0xB1u, 0xC4E812DF2B665362, 0x8B3F8BC83A0D80D6},
          {0x4Eu, 0x4F41FFDEF2BFE636, 0xF35862E13E38F8B0},
          {0x27u, 0x1169751BF7A594AE, 0xB9A7D9C3C8EF8A9C}};
      for (size_t i = 0; i < sizeof(exp_w) / sizeof(ExpResShf); ++i) {
        if (exp_w[i].i8 == i8) {
          CHECK_EQ(exp_w[i].lo, res.d[0]);
          CHECK_EQ(exp_w[i].hi, res.d[1]);
        }
      }
    } break;
    default:
      UNREACHABLE();
  }
}

struct TestCaseMsaI8 {
  uint64_t input_lo;
  uint64_t input_hi;
  uint8_t i8;
};

TEST(MSA_andi_ori_nori_xori) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // clang-format off
  struct TestCaseMsaI8 tc[] = {
    //         input_lo,           input_hi,    i8
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0xFFu},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x0u},
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x3Bu},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0xD9u}
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI8); ++i) {
    run_msa_i8(ANDI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(ORI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(NORI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(XORI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
  }
}

TEST(MSA_bmnzi_bmzi_bseli) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // clang-format off
  struct TestCaseMsaI8 tc[] = {
    //         input_lo,           input_hi,    i8
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0xFFu},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x0u},
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x3Bu},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0xD9u}
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI8); ++i) {
    run_msa_i8(BMNZI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(BMZI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(BSELI_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
  }
}

TEST(MSA_shf) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // clang-format off
  struct TestCaseMsaI8 tc[] = {
      //          input_lo,           input_hi,    i8
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0xFFu},  // 3333
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x0u},   // 0000
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0xE4u},  // 3210
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x1Bu},  // 0123
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0xB1u},  // 2301
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0x4Eu},  // 1032
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x27u}   // 0213
  };
  // clang-format on

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI8); ++i) {
    run_msa_i8(SHF_B, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(SHF_H, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
    run_msa_i8(SHF_W, tc[i].input_lo, tc[i].input_hi, tc[i].i8);
  }
}

struct TestCaseMsaI5 {
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint32_t i5;
};

template <typename InstFunc, typename OperFunc>
void run_msa_i5(struct TestCaseMsaI5* input, bool i5_sign_ext,
                InstFunc GenerateI5InstructionFunc,
                OperFunc GenerateOperationFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;
  int32_t i5 =
      i5_sign_ext ? static_cast<int32_t>(input->i5 << 27) >> 27 : input->i5;

  load_elements_of_vector(&assm, &(input->ws_lo), w0, t0, t1);

  GenerateI5InstructionFunc(assm, i5);

  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(GenerateOperationFunc(input->ws_lo, input->i5), res.d[0]);
  CHECK_EQ(GenerateOperationFunc(input->ws_hi, input->i5), res.d[1]);
}

TEST(MSA_addvi_subvi) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // clang-format off
  struct TestCaseMsaI5 tc[] = {
    //            ws_lo,              ws_hi,         i5
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x0000001F},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x0000000F},
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x00000005},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x00000010},
    {0xFFAB807F807FFFCD, 0x7F23FF80FF567F80, 0x0000000F},
    {0x80FFEFFF7F12807F, 0x807F80FF7FDEFF78, 0x00000010}
  };
// clang-format on

#define ADDVI_DF(lanes, mask)                               \
  uint64_t res = 0;                                         \
  for (int i = 0; i < lanes / 2; ++i) {                     \
    int shift = (kMSARegSize / lanes) * i;                  \
    res |= ((((ws >> shift) & mask) + i5) & mask) << shift; \
  }                                                         \
  return res

#define SUBVI_DF(lanes, mask)                               \
  uint64_t res = 0;                                         \
  for (int i = 0; i < lanes / 2; ++i) {                     \
    int shift = (kMSARegSize / lanes) * i;                  \
    res |= ((((ws >> shift) & mask) - i5) & mask) << shift; \
  }                                                         \
  return res

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI5); ++i) {
    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ addvi_b(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { ADDVI_DF(kMSALanesByte, UINT8_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ addvi_h(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { ADDVI_DF(kMSALanesHalf, UINT16_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ addvi_w(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { ADDVI_DF(kMSALanesWord, UINT32_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ addvi_d(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { ADDVI_DF(kMSALanesDword, UINT64_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ subvi_b(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { SUBVI_DF(kMSALanesByte, UINT8_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ subvi_h(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { SUBVI_DF(kMSALanesHalf, UINT16_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ subvi_w(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { SUBVI_DF(kMSALanesWord, UINT32_MAX); });

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ subvi_d(w2, w0, i5); },
        [](uint64_t ws, uint32_t i5) { SUBVI_DF(kMSALanesDword, UINT64_MAX); });
  }
#undef ADDVI_DF
#undef SUBVI_DF
}

TEST(MSA_maxi_mini) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // clang-format off
  struct TestCaseMsaI5 tc[] = {
    //            ws_lo,              ws_hi,         i5
    {0x7F80FF3480FF7F00, 0x8D7FFF80FF7F6780, 0x0000001F},
    {0x7F80FF3480FF7F00, 0x8D7FFF80FF7F6780, 0x0000000F},
    {0x7F80FF3480FF7F00, 0x8D7FFF80FF7F6780, 0x00000010},
    {0x80007FFF91DAFFFF, 0x7FFF8000FFFF5678, 0x0000001F},
    {0x80007FFF91DAFFFF, 0x7FFF8000FFFF5678, 0x0000000F},
    {0x80007FFF91DAFFFF, 0x7FFF8000FFFF5678, 0x00000010},
    {0x7FFFFFFF80000000, 0x12345678FFFFFFFF, 0x0000001F},
    {0x7FFFFFFF80000000, 0x12345678FFFFFFFF, 0x0000000F},
    {0x7FFFFFFF80000000, 0x12345678FFFFFFFF, 0x00000010},
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x0000001F},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x0000000F},
    {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0x00000010},
    {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x00000015},
    {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x00000009},
    {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0x00000003}
  };
// clang-format on

#define MAXI_MINI_S_DF(lanes, mask, func)                                     \
  [](uint64_t ws, uint32_t ui5) {                                             \
    uint64_t res = 0;                                                         \
    int64_t i5 = ArithmeticShiftRight(static_cast<int64_t>(ui5) << 59, 59);   \
    int elem_size = kMSARegSize / lanes;                                      \
    for (int i = 0; i < lanes / 2; ++i) {                                     \
      int shift = elem_size * i;                                              \
      int64_t elem =                                                          \
          static_cast<int64_t>(((ws >> shift) & mask) << (64 - elem_size)) >> \
          (64 - elem_size);                                                   \
      res |= static_cast<uint64_t>(func(elem, i5) & mask) << shift;           \
    }                                                                         \
    return res;                                                               \
  }

#define MAXI_MINI_U_DF(lanes, mask, func)                              \
  [](uint64_t ws, uint32_t ui5) {                                      \
    uint64_t res = 0;                                                  \
    int elem_size = kMSARegSize / lanes;                               \
    for (int i = 0; i < lanes / 2; ++i) {                              \
      int shift = elem_size * i;                                       \
      uint64_t elem = (ws >> shift) & mask;                            \
      res |= (func(elem, static_cast<uint64_t>(ui5)) & mask) << shift; \
    }                                                                  \
    return res;                                                        \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI5); ++i) {
    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_s_b(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesByte, UINT8_MAX, std::max));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_s_h(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesHalf, UINT16_MAX, std::max));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_s_w(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesWord, UINT32_MAX, std::max));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_s_d(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesDword, UINT64_MAX, std::max));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ mini_s_b(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesByte, UINT8_MAX, std::min));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ mini_s_h(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesHalf, UINT16_MAX, std::min));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ mini_s_w(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesWord, UINT32_MAX, std::min));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ mini_s_d(w2, w0, i5); },
        MAXI_MINI_S_DF(kMSALanesDword, UINT64_MAX, std::min));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_u_b(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesByte, UINT8_MAX, std::max));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_u_h(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesHalf, UINT16_MAX, std::max));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_u_w(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesWord, UINT32_MAX, std::max));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ maxi_u_d(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesDword, UINT64_MAX, std::max));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ mini_u_b(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesByte, UINT8_MAX, std::min));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ mini_u_h(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesHalf, UINT16_MAX, std::min));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ mini_u_w(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesWord, UINT32_MAX, std::min));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ mini_u_d(w2, w0, i5); },
        MAXI_MINI_U_DF(kMSALanesDword, UINT64_MAX, std::min));
  }
#undef MAXI_MINI_S_DF
#undef MAXI_MINI_U_DF
}

TEST(MSA_ceqi_clti_clei) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaI5 tc[] = {
      {0xFF69751BB9A7D9C3, 0xF7A594AEC8FF8A9C, 0x0000001F},
      {0xE669FFFFB9A7D9C3, 0xF7A594AEFFFF8A9C, 0x0000001F},
      {0xFFFFFFFFB9A7D9C3, 0xF7A594AEFFFFFFFF, 0x0000001F},
      {0x2B0B5362C4E812DF, 0x3A0D80D68B3F0BC8, 0x0000000B},
      {0x2B66000BC4E812DF, 0x3A0D000B8B3F8BC8, 0x0000000B},
      {0x0000000BC4E812DF, 0x3A0D80D60000000B, 0x0000000B},
      {0xF38062E13E38F8B0, 0x8041FFDEF2BFE636, 0x00000010},
      {0xF35880003E38F8B0, 0x4F41FFDEF2BF8000, 0x00000010},
      {0xF35862E180000000, 0x80000000F2BFE636, 0x00000010},
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x00000015},
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x00000009},
      {0xF30062E13E38F800, 0x4F00FFDEF2BF0036, 0x00000000}};

#define CEQI_CLTI_CLEI_S_DF(lanes, mask, func)                                \
  [](uint64_t ws, uint32_t ui5) {                                             \
    uint64_t res = 0;                                                         \
    int elem_size = kMSARegSize / lanes;                                      \
    int64_t i5 = ArithmeticShiftRight(static_cast<int64_t>(ui5) << 59, 59);   \
    for (int i = 0; i < lanes / 2; ++i) {                                     \
      int shift = elem_size * i;                                              \
      int64_t elem =                                                          \
          static_cast<int64_t>(((ws >> shift) & mask) << (64 - elem_size)) >> \
          (64 - elem_size);                                                   \
      res |= static_cast<uint64_t>((func)&mask) << shift;                     \
    }                                                                         \
    return res;                                                               \
  }

#define CEQI_CLTI_CLEI_U_DF(lanes, mask, func) \
  [](uint64_t ws, uint64_t ui5) {              \
    uint64_t res = 0;                          \
    int elem_size = kMSARegSize / lanes;       \
    for (int i = 0; i < lanes / 2; ++i) {      \
      int shift = elem_size * i;               \
      uint64_t elem = (ws >> shift) & mask;    \
      res |= ((func)&mask) << shift;           \
    }                                          \
    return res;                                \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaI5); ++i) {
    run_msa_i5(&tc[i], true,
               [](MacroAssembler& assm, int32_t i5) { __ ceqi_b(w2, w0, i5); },
               CEQI_CLTI_CLEI_S_DF(kMSALanesByte, UINT8_MAX,
                                   !Compare(elem, i5) ? -1u : 0u));

    run_msa_i5(&tc[i], true,
               [](MacroAssembler& assm, int32_t i5) { __ ceqi_h(w2, w0, i5); },
               CEQI_CLTI_CLEI_S_DF(kMSALanesHalf, UINT16_MAX,
                                   !Compare(elem, i5) ? -1u : 0u));

    run_msa_i5(&tc[i], true,
               [](MacroAssembler& assm, int32_t i5) { __ ceqi_w(w2, w0, i5); },
               CEQI_CLTI_CLEI_S_DF(kMSALanesWord, UINT32_MAX,
                                   !Compare(elem, i5) ? -1u : 0u));

    run_msa_i5(&tc[i], true,
               [](MacroAssembler& assm, int32_t i5) { __ ceqi_d(w2, w0, i5); },
               CEQI_CLTI_CLEI_S_DF(kMSALanesDword, UINT64_MAX,
                                   !Compare(elem, i5) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clti_s_b(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesByte, UINT8_MAX,
                            (Compare(elem, i5) == -1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clti_s_h(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesHalf, UINT16_MAX,
                            (Compare(elem, i5) == -1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clti_s_w(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesWord, UINT32_MAX,
                            (Compare(elem, i5) == -1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clti_s_d(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesDword, UINT64_MAX,
                            (Compare(elem, i5) == -1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clei_s_b(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesByte, UINT8_MAX,
                            (Compare(elem, i5) != 1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clei_s_h(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesHalf, UINT16_MAX,
                            (Compare(elem, i5) != 1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clei_s_w(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesWord, UINT32_MAX,
                            (Compare(elem, i5) != 1) ? -1u : 0u));

    run_msa_i5(
        &tc[i], true,
        [](MacroAssembler& assm, int32_t i5) { __ clei_s_d(w2, w0, i5); },
        CEQI_CLTI_CLEI_S_DF(kMSALanesDword, UINT64_MAX,
                            (Compare(elem, i5) != 1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clti_u_b(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesByte, UINT8_MAX,
                            (Compare(elem, ui5) == -1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clti_u_h(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesHalf, UINT16_MAX,
                            (Compare(elem, ui5) == -1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clti_u_w(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesWord, UINT32_MAX,
                            (Compare(elem, ui5) == -1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clti_u_d(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesDword, UINT64_MAX,
                            (Compare(elem, ui5) == -1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clei_u_b(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesByte, UINT8_MAX,
                            (Compare(elem, ui5) != 1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clei_u_h(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesHalf, UINT16_MAX,
                            (Compare(elem, ui5) != 1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clei_u_w(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesWord, UINT32_MAX,
                            (Compare(elem, ui5) != 1) ? -1ull : 0ull));

    run_msa_i5(
        &tc[i], false,
        [](MacroAssembler& assm, int32_t i5) { __ clei_u_d(w2, w0, i5); },
        CEQI_CLTI_CLEI_U_DF(kMSALanesDword, UINT64_MAX,
                            (Compare(elem, ui5) != 1) ? -1ull : 0ull));
  }
#undef CEQI_CLTI_CLEI_S_DF
#undef CEQI_CLTI_CLEI_U_DF
}

struct TestCaseMsa2R {
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint64_t exp_res_lo;
  uint64_t exp_res_hi;
};

template <typename Func>
void run_msa_2r(const struct TestCaseMsa2R* input,
                Func Generate2RInstructionFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  load_elements_of_vector(&assm, reinterpret_cast<const uint64_t*>(input), w0,
                          t0, t1);
  Generate2RInstructionFunc(assm);
  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(input->exp_res_lo, res.d[0]);
  CHECK_EQ(input->exp_res_hi, res.d[1]);
}

TEST(MSA_pcnt) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2R tc_b[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0808080808080808, 0x0808080808080808},
                                 {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C,
                                  0x0204050405050504, 0x0704030503070304},
                                 {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8,
                                  0x0404040303040207, 0x0403010504060403},
                                 {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636,
                                  0x0603030405030503, 0x0502080605070504}};

  struct TestCaseMsa2R tc_h[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0010001000100010, 0x0010001000100010},
                                 {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C,
                                  0x00060009000A0009, 0x000B0008000A0007},
                                 {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8,
                                  0x0008000700070009, 0x00070006000A0007},
                                 {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636,
                                  0x0009000700080008, 0x0007000E000C0009}};

  struct TestCaseMsa2R tc_w[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0000002000000020, 0x0000002000000020},
                                 {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C,
                                  0x0000000F00000013, 0x0000001300000011},
                                 {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8,
                                  0x0000000F00000010, 0x0000000D00000011},
                                 {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636,
                                  0x0000001000000010, 0x0000001500000015}};

  struct TestCaseMsa2R tc_d[] = {
      // ws_lo, ws_hi, exp_res_lo, exp_res_hi
      {0x0000000000000000, 0x0000000000000000, 0, 0},
      {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x40, 0x40},
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x22, 0x24},
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x1F, 0x1E},
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0x20, 0x2A}};

  for (size_t i = 0; i < sizeof(tc_b) / sizeof(TestCaseMsa2R); ++i) {
    run_msa_2r(&tc_b[i], [](MacroAssembler& assm) { __ pcnt_b(w2, w0); });
    run_msa_2r(&tc_h[i], [](MacroAssembler& assm) { __ pcnt_h(w2, w0); });
    run_msa_2r(&tc_w[i], [](MacroAssembler& assm) { __ pcnt_w(w2, w0); });
    run_msa_2r(&tc_d[i], [](MacroAssembler& assm) { __ pcnt_d(w2, w0); });
  }
}

TEST(MSA_nlzc) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2R tc_b[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000,
                                  0x0808080808080808, 0x0808080808080808},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0, 0},
                                 {0x1169350B07030100, 0x7F011402381F0A6C,
                                  0x0301020405060708, 0x0107030602030401},
                                 {0x010806003478121F, 0x03013016073F7B08,
                                  0x0704050802010303, 0x0607020305020104},
                                 {0x0168321100083803, 0x07113F03013F1676,
                                  0x0701020308040206, 0x0503020607020301}};

  struct TestCaseMsa2R tc_h[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000,
                                  0x0010001000100010, 0x0010001000100010},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0, 0},
                                 {0x00010007000A003C, 0x37A5001E00010002,
                                  0x000F000D000C000A, 0x0002000B000F000E},
                                 {0x0026066200780EDF, 0x003D0003000F00C8,
                                  0x000A000500090004, 0x000A000E000C0008},
                                 {0x335807E100480030, 0x01410FDE12BF5636,
                                  0x000200050009000A, 0x0007000400030001}};

  struct TestCaseMsa2R tc_w[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0x0000000000000000, 0x0000000000000000,
                                  0x0000002000000020, 0x0000002000000020},
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0, 0},
                                 {0x00000005000007C3, 0x000014AE00006A9C,
                                  0x0000001D00000015, 0x0000001300000011},
                                 {0x00009362000112DF, 0x000380D6003F8BC8,
                                  0x000000100000000F, 0x0000000E0000000A},
                                 {0x135862E17E38F8B0, 0x0061FFDE03BFE636,
                                  0x0000000300000001, 0x0000000900000006}};

  struct TestCaseMsa2R tc_d[] = {
      // ws_lo, ws_hi, exp_res_lo, exp_res_hi
      {0x0000000000000000, 0x0000000000000000, 0x40, 0x40},
      {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0, 0},
      {0x000000000000014E, 0x00000000000176DA, 0x37, 0x2F},
      {0x00000062C4E812DF, 0x000065D68B3F8BC8, 0x19, 0x11},
      {0x00000000E338F8B0, 0x0754534ACAB32654, 0x20, 0x5}};

  for (size_t i = 0; i < sizeof(tc_b) / sizeof(TestCaseMsa2R); ++i) {
    run_msa_2r(&tc_b[i], [](MacroAssembler& assm) { __ nlzc_b(w2, w0); });
    run_msa_2r(&tc_h[i], [](MacroAssembler& assm) { __ nlzc_h(w2, w0); });
    run_msa_2r(&tc_w[i], [](MacroAssembler& assm) { __ nlzc_w(w2, w0); });
    run_msa_2r(&tc_d[i], [](MacroAssembler& assm) { __ nlzc_d(w2, w0); });
  }
}

TEST(MSA_nloc) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2R tc_b[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0808080808080808, 0x0808080808080808},
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xEE96CAF4F8FCFEFF, 0x80FEEBFDC7E0F593,
                                  0x0301020405060708, 0x0107030602030401},
                                 {0xFEF7F9FFCB87EDE0, 0xFCFECFE9F8C084F7,
                                  0x0704050802010303, 0x0607020305020104},
                                 {0xFE97CDEEFFF7C7FC, 0xF8EEC0FCFEC0E989,
                                  0x0701020308040206, 0x0503020607020301}};

  struct TestCaseMsa2R tc_h[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0010001000100010, 0x0010001000100010},
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xFFFEFFF8FFF5FFC3, 0xC85AFFE1FFFEFFFD,
                                  0x000F000D000C000A, 0x0002000B000F000E},
                                 {0xFFD9F99DFF87F120, 0xFFC2FFFCFFF0FF37,
                                  0x000A000500090004, 0x000A000E000C0008},
                                 {0xCCA7F81EFFB7FFCF, 0xFEBEF021ED40A9C9,
                                  0x000200050009000A, 0x0007000400030001}};

  struct TestCaseMsa2R tc_w[] = {// ws_lo, ws_hi, exp_res_lo, exp_res_hi
                                 {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
                                  0x0000002000000020, 0x0000002000000020},
                                 {0x0000000000000000, 0x0000000000000000, 0, 0},
                                 {0xFFFFFFFAFFFFF83C, 0xFFFFEB51FFFF9563,
                                  0x0000001D00000015, 0x0000001300000011},
                                 {0xFFFF6C9DFFFEED20, 0xFFFC7F29FFC07437,
                                  0x000000100000000F, 0x0000000E0000000A},
                                 {0xECA79D1E81C7074F, 0xFF9E0021FC4019C9,
                                  0x0000000300000001, 0x0000000900000006}};

  struct TestCaseMsa2R tc_d[] = {
      // ws_lo, ws_hi, exp_res_lo, exp_res_hi
      {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0x40, 0x40},
      {0x0000000000000000, 0x0000000000000000, 0, 0},
      {0xFFFFFFFFFFFFFEB1, 0xFFFFFFFFFFFE8925, 0x37, 0x2F},
      {0xFFFFFF9D3B17ED20, 0xFFFF9A2974C07437, 0x19, 0x11},
      {0xFFFFFFFF1CC7074F, 0xF8ABACB5354CD9AB, 0x20, 0x5}};

  for (size_t i = 0; i < sizeof(tc_b) / sizeof(TestCaseMsa2R); ++i) {
    run_msa_2r(&tc_b[i], [](MacroAssembler& assm) { __ nloc_b(w2, w0); });
    run_msa_2r(&tc_h[i], [](MacroAssembler& assm) { __ nloc_h(w2, w0); });
    run_msa_2r(&tc_w[i], [](MacroAssembler& assm) { __ nloc_w(w2, w0); });
    run_msa_2r(&tc_d[i], [](MacroAssembler& assm) { __ nloc_d(w2, w0); });
  }
}

struct TestCaseMsa2RF_F_U {
  float ws1;
  float ws2;
  float ws3;
  float ws4;
  uint32_t exp_res_1;
  uint32_t exp_res_2;
  uint32_t exp_res_3;
  uint32_t exp_res_4;
};

struct TestCaseMsa2RF_D_U {
  double ws1;
  double ws2;
  uint64_t exp_res_1;
  uint64_t exp_res_2;
};

TEST(MSA_fclass) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

#define BIT(n) (0x1 << n)
#define SNAN_BIT BIT(0)
#define QNAN_BIT BIT(1)
#define NEG_INFINITY_BIT BIT((2))
#define NEG_NORMAL_BIT BIT(3)
#define NEG_SUBNORMAL_BIT BIT(4)
#define NEG_ZERO_BIT BIT(5)
#define POS_INFINITY_BIT BIT(6)
#define POS_NORMAL_BIT BIT(7)
#define POS_SUBNORMAL_BIT BIT(8)
#define POS_ZERO_BIT BIT(9)

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa2RF_F_U tc_s[] = {
      {1.f, -0.00001, 208e10f, -34.8e-30f, POS_NORMAL_BIT, NEG_NORMAL_BIT,
       POS_NORMAL_BIT, NEG_NORMAL_BIT},
      {inf_float, -inf_float, 0, -0.f, POS_INFINITY_BIT, NEG_INFINITY_BIT,
       POS_ZERO_BIT, NEG_ZERO_BIT},
      {3.036e-40f, -6.392e-43f, 1.41e-45f, -1.17e-38f, POS_SUBNORMAL_BIT,
       NEG_SUBNORMAL_BIT, POS_SUBNORMAL_BIT, NEG_SUBNORMAL_BIT}};

  const struct TestCaseMsa2RF_D_U tc_d[] = {
      {1., -0.00000001, POS_NORMAL_BIT, NEG_NORMAL_BIT},
      {208e10, -34.8e-300, POS_NORMAL_BIT, NEG_NORMAL_BIT},
      {inf_double, -inf_double, POS_INFINITY_BIT, NEG_INFINITY_BIT},
      {0, -0., POS_ZERO_BIT, NEG_ZERO_BIT},
      {1.036e-308, -6.392e-309, POS_SUBNORMAL_BIT, NEG_SUBNORMAL_BIT},
      {1.41e-323, -3.17e208, POS_SUBNORMAL_BIT, NEG_NORMAL_BIT}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ fclass_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ fclass_d(w2, w0); });
  }

#undef BIT
#undef SNAN_BIT
#undef QNAN_BIT
#undef NEG_INFINITY_BIT
#undef NEG_NORMAL_BIT
#undef NEG_SUBNORMAL_BIT
#undef NEG_ZERO_BIT
#undef POS_INFINITY_BIT
#undef POS_NORMAL_BIT
#undef POS_SUBNORMAL_BIT
#undef POS_ZERO_BIT
}

struct TestCaseMsa2RF_F_I {
  float ws1;
  float ws2;
  float ws3;
  float ws4;
  int32_t exp_res_1;
  int32_t exp_res_2;
  int32_t exp_res_3;
  int32_t exp_res_4;
};

struct TestCaseMsa2RF_D_I {
  double ws1;
  double ws2;
  int64_t exp_res_1;
  int64_t exp_res_2;
};

TEST(MSA_ftrunc_s) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const float qNaN_float = std::numeric_limits<float>::quiet_NaN();
  const double inf_double = std::numeric_limits<double>::infinity();
  const double qNaN_double = std::numeric_limits<double>::quiet_NaN();
  const int32_t max_int32 = std::numeric_limits<int32_t>::max();
  const int32_t min_int32 = std::numeric_limits<int32_t>::min();
  const int64_t max_int64 = std::numeric_limits<int64_t>::max();
  const int64_t min_int64 = std::numeric_limits<int64_t>::min();

  const struct TestCaseMsa2RF_F_I tc_s[] = {
      {inf_float, 2.345f, -324.9235f, 30004.51f, max_int32, 2, -324, 30004},
      {-inf_float, -0.983f, 0.0832f, static_cast<float>(max_int32) * 3.f,
       min_int32, 0, 0, max_int32},
      {-23.125f, qNaN_float, 2 * static_cast<float>(min_int32), -0.f, -23, 0,
       min_int32, 0}};

  const struct TestCaseMsa2RF_D_I tc_d[] = {
      {inf_double, 2.345, max_int64, 2},
      {-324.9235, 246569139.51, -324, 246569139},
      {-inf_double, -0.983, min_int64, 0},
      {0.0832, 6 * static_cast<double>(max_int64), 0, max_int64},
      {-21453889872.94, qNaN_double, -21453889872, 0},
      {2 * static_cast<double>(min_int64), -0., min_int64, 0}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_I); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ftrunc_s_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_I); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ftrunc_s_d(w2, w0); });
  }
}

TEST(MSA_ftrunc_u) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const float qNaN_float = std::numeric_limits<float>::quiet_NaN();
  const double inf_double = std::numeric_limits<double>::infinity();
  const double qNaN_double = std::numeric_limits<double>::quiet_NaN();
  const uint32_t max_uint32 = std::numeric_limits<uint32_t>::max();
  const uint64_t max_uint64 = std::numeric_limits<uint64_t>::max();

  const struct TestCaseMsa2RF_F_U tc_s[] = {
      {inf_float, 2.345f, -324.9235f, 30004.51f, max_uint32, 2, 0, 30004},
      {-inf_float, 0.983f, 0.0832f, static_cast<float>(max_uint32) * 3., 0, 0,
       0, max_uint32},
      {23.125f, qNaN_float, -0.982, -0.f, 23, 0, 0, 0}};

  const struct TestCaseMsa2RF_D_U tc_d[] = {
      {inf_double, 2.345, max_uint64, 2},
      {-324.9235, 246569139.51, 0, 246569139},
      {-inf_double, -0.983, 0, 0},
      {0.0832, 6 * static_cast<double>(max_uint64), 0, max_uint64},
      {21453889872.94, qNaN_double, 21453889872, 0},
      {0.9889, -0., 0, 0}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ftrunc_u_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ftrunc_u_d(w2, w0); });
  }
}

struct TestCaseMsa2RF_F_F {
  float ws1;
  float ws2;
  float ws3;
  float ws4;
  float exp_res_1;
  float exp_res_2;
  float exp_res_3;
  float exp_res_4;
};

struct TestCaseMsa2RF_D_D {
  double ws1;
  double ws2;
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_fsqrt) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa2RF_F_F tc_s[] = {
      {81.f, 576.f, inf_float, -0.f, 9.f, 24.f, inf_float, -0.f}};

  const struct TestCaseMsa2RF_D_D tc_d[] = {{81., inf_double, 9., inf_double},
                                            {331776., -0., 576, -0.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ fsqrt_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ fsqrt_d(w2, w0); });
  }
}

TEST(MSA_frsqrt) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa2RF_F_F tc_s[] = {
      {81.f, 576.f, inf_float, -0.f, 1.f / 9.f, 1.f / 24.f, 0.f, -inf_float},
      {0.f, 1.f / 576.f, 1.f / 81.f, 1.f / 4.f, inf_float, 24.f, 9.f, 2.f}};

  const struct TestCaseMsa2RF_D_D tc_d[] = {
      {81., inf_double, 1. / 9., 0.},
      {331776., -0., 1. / 576., -inf_double},
      {0., 1. / 81, inf_double, 9.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ frsqrt_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ frsqrt_d(w2, w0); });
  }
}

TEST(MSA_frcp) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa2RF_F_F tc_s[] = {
      {12.f, 576.f, inf_float, -0.f, 1.f / 12.f, 1.f / 576.f, 0.f, -inf_float},
      {0.f, 1.f / 576.f, -inf_float, 1.f / 400.f, inf_float, 576.f, -0.f,
       400.f}};

  const struct TestCaseMsa2RF_D_D tc_d[] = {
      {81., inf_double, 1. / 81., 0.},
      {331777., -0., 1. / 331777., -inf_double},
      {0., 1. / 80, inf_double, 80.},
      {1. / 40000., -inf_double, 40000., -0.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ frcp_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ frcp_d(w2, w0); });
  }
}

void test_frint_s(size_t data_size, TestCaseMsa2RF_F_F tc_d[],
                  int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_F_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ frint_w(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

void test_frint_d(size_t data_size, TestCaseMsa2RF_D_D tc_d[],
                  int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_D_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ frint_d(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

TEST(MSA_frint) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2RF_F_F tc_s1[] = {
      {0.f, 4.51f, 1.49f, -12.51f, 0.f, 5.f, 1.f, -13.f},
      {-1.32f, -23.38f, 2.8f, -32.6f, -1.f, -23.f, 3.f, -33.f}};

  struct TestCaseMsa2RF_D_D tc_d1[] = {{0., 4.51, 0., 5.},
                                       {1.49, -12.51, 1., -13.},
                                       {-1.32, -23.38, -1., -23.},
                                       {2.8, -32.6, 3., -33.}};

  test_frint_s(sizeof(tc_s1), tc_s1, kRoundToNearest);
  test_frint_d(sizeof(tc_d1), tc_d1, kRoundToNearest);

  struct TestCaseMsa2RF_F_F tc_s2[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0.f, 4.f, 1.f, -12.f},
      {-1.f, -23.38f, 2.8f, -32.6f, -1.f, -23.f, 2.f, -32.f}};

  struct TestCaseMsa2RF_D_D tc_d2[] = {{0., 4.5, 0., 4.},
                                       {1.49, -12.51, 1., -12.},
                                       {-1., -23.38, -1., -23.},
                                       {2.8, -32.6, 2., -32.}};

  test_frint_s(sizeof(tc_s2), tc_s2, kRoundToZero);
  test_frint_d(sizeof(tc_d2), tc_d2, kRoundToZero);

  struct TestCaseMsa2RF_F_F tc_s3[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0.f, 5.f, 2.f, -12.f},
      {-1.f, -23.38f, 2.8f, -32.6f, -1.f, -23.f, 3.f, -32.f}};

  struct TestCaseMsa2RF_D_D tc_d3[] = {{0., 4.5, 0., 5.},
                                       {1.49, -12.51, 2., -12.},
                                       {-1., -23.38, -1., -23.},
                                       {2.8, -32.6, 3., -32.}};

  test_frint_s(sizeof(tc_s3), tc_s3, kRoundToPlusInf);
  test_frint_d(sizeof(tc_d3), tc_d3, kRoundToPlusInf);

  struct TestCaseMsa2RF_F_F tc_s4[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0.f, 4.f, 1.f, -13.f},
      {-1.f, -23.38f, 2.8f, -32.6f, -1.f, -24.f, 2.f, -33.f}};

  struct TestCaseMsa2RF_D_D tc_d4[] = {{0., 4.5, 0., 4.},
                                       {1.49, -12.51, 1., -13.},
                                       {-1., -23.38, -1., -24.},
                                       {2.8, -32.6, 2., -33.}};

  test_frint_s(sizeof(tc_s4), tc_s4, kRoundToMinusInf);
  test_frint_d(sizeof(tc_d4), tc_d4, kRoundToMinusInf);
}

TEST(MSA_flog2) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  struct TestCaseMsa2RF_F_F tc_s[] = {
      {std::ldexp(0.58f, -48), std::ldexp(0.5f, 110), std::ldexp(1.11f, -130),
       inf_float, -49.f, 109.f, -130.f, inf_float},
      {0.f, -0.f, std::ldexp(0.89f, -12), std::ldexp(0.32f, 126), -inf_float,
       -inf_float, -13.f, 124.f}};

  struct TestCaseMsa2RF_D_D tc_d[] = {
      {std::ldexp(0.58, -48), std::ldexp(0.5, 110), -49., 109.},
      {std::ldexp(1.11, -1050), inf_double, -1050., inf_double},
      {0., -0., -inf_double, -inf_double},
      {std::ldexp(0.32, 1021), std::ldexp(1.23, -123), 1019., -123.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_F_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ flog2_w(w2, w0); });
  }

  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_D_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ flog2_d(w2, w0); });
  }
}

void test_ftint_s_s(size_t data_size, TestCaseMsa2RF_F_I tc_d[],
                    int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_F_I); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ ftint_s_w(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

void test_ftint_s_d(size_t data_size, TestCaseMsa2RF_D_I tc_d[],
                    int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_D_I); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ ftint_s_d(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

TEST(MSA_ftint_s) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();
  const int32_t int32_max = std::numeric_limits<int32_t>::max();
  const int32_t int32_min = std::numeric_limits<int32_t>::min();
  const int64_t int64_max = std::numeric_limits<int64_t>::max();
  const int64_t int64_min = std::numeric_limits<int64_t>::min();

  struct TestCaseMsa2RF_F_I tc_s1[] = {
      {0.f, 4.51f, 1.49f, -12.51f, 0, 5, 1, -13},
      {-0.32f, -23.38f, 2.8f, -32.6f, 0, -23, 3, -33},
      {inf_float, -inf_float, 3.f * int32_min, 4.f * int32_max, int32_max,
       int32_min, int32_min, int32_max}};

  struct TestCaseMsa2RF_D_I tc_d1[] = {
      {0., 4.51, 0, 5},
      {1.49, -12.51, 1, -13},
      {-0.32, -23.38, 0, -23},
      {2.8, -32.6, 3, -33},
      {inf_double, -inf_double, int64_max, int64_min},
      {33.23 * int64_min, 4000. * int64_max, int64_min, int64_max}};

  test_ftint_s_s(sizeof(tc_s1), tc_s1, kRoundToNearest);
  test_ftint_s_d(sizeof(tc_d1), tc_d1, kRoundToNearest);

  struct TestCaseMsa2RF_F_I tc_s2[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 4, 1, -12},
      {-0.f, -23.38f, 2.8f, -32.6f, -0, -23, 2, -32},
      {inf_float, -inf_float, 3.f * int32_min, 4.f * int32_max, int32_max,
       int32_min, int32_min, int32_max}};

  struct TestCaseMsa2RF_D_I tc_d2[] = {
      {0., 4.5, 0, 4},
      {1.49, -12.51, 1, -12},
      {-0., -23.38, -0, -23},
      {2.8, -32.6, 2, -32},
      {inf_double, -inf_double, int64_max, int64_min},
      {33.23 * int64_min, 4000. * int64_max, int64_min, int64_max}};

  test_ftint_s_s(sizeof(tc_s2), tc_s2, kRoundToZero);
  test_ftint_s_d(sizeof(tc_d2), tc_d2, kRoundToZero);

  struct TestCaseMsa2RF_F_I tc_s3[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 5, 2, -12},
      {-0.f, -23.38f, 2.8f, -32.6f, -0, -23, 3, -32},
      {inf_float, -inf_float, 3.f * int32_min, 4.f * int32_max, int32_max,
       int32_min, int32_min, int32_max}};

  struct TestCaseMsa2RF_D_I tc_d3[] = {
      {0., 4.5, 0, 5},
      {1.49, -12.51, 2, -12},
      {-0., -23.38, -0, -23},
      {2.8, -32.6, 3, -32},
      {inf_double, -inf_double, int64_max, int64_min},
      {33.23 * int64_min, 4000. * int64_max, int64_min, int64_max}};

  test_ftint_s_s(sizeof(tc_s3), tc_s3, kRoundToPlusInf);
  test_ftint_s_d(sizeof(tc_d3), tc_d3, kRoundToPlusInf);

  struct TestCaseMsa2RF_F_I tc_s4[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 4, 1, -13},
      {-0.f, -23.38f, 2.8f, -32.6f, -0, -24, 2, -33},
      {inf_float, -inf_float, 3.f * int32_min, 4.f * int32_max, int32_max,
       int32_min, int32_min, int32_max}};

  struct TestCaseMsa2RF_D_I tc_d4[] = {
      {0., 4.5, 0, 4},
      {1.49, -12.51, 1, -13},
      {-0., -23.38, -0, -24},
      {2.8, -32.6, 2, -33},
      {inf_double, -inf_double, int64_max, int64_min},
      {33.23 * int64_min, 4000. * int64_max, int64_min, int64_max}};

  test_ftint_s_s(sizeof(tc_s4), tc_s4, kRoundToMinusInf);
  test_ftint_s_d(sizeof(tc_d4), tc_d4, kRoundToMinusInf);
}

void test_ftint_u_s(size_t data_size, TestCaseMsa2RF_F_U tc_d[],
                    int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_F_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ ftint_u_w(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

void test_ftint_u_d(size_t data_size, TestCaseMsa2RF_D_U tc_d[],
                    int rounding_mode) {
  for (size_t i = 0; i < data_size / sizeof(TestCaseMsa2RF_D_U); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [&rounding_mode](MacroAssembler& assm) {
                 MSAControlRegister msareg = {kMSACSRRegister};
                 __ li(t0, static_cast<uint32_t>(rounding_mode));
                 __ cfcmsa(t1, msareg);
                 __ ctcmsa(msareg, t0);
                 __ ftint_u_d(w2, w0);
                 __ ctcmsa(msareg, t1);
               });
  }
}

TEST(MSA_ftint_u) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();
  const uint32_t uint32_max = std::numeric_limits<uint32_t>::max();
  const uint64_t uint64_max = std::numeric_limits<uint64_t>::max();

  struct TestCaseMsa2RF_F_U tc_s1[] = {
      {0.f, 4.51f, 1.49f, -12.51f, 0, 5, 1, 0},
      {-0.32f, 23.38f, 2.8f, 32.6f, 0, 23, 3, 33},
      {inf_float, -inf_float, 0, 4.f * uint32_max, uint32_max, 0, 0,
       uint32_max}};

  struct TestCaseMsa2RF_D_U tc_d1[] = {
      {0., 4.51, 0, 5},
      {1.49, -12.51, 1, 0},
      {-0.32, 23.38, 0, 23},
      {2.8, 32.6, 3, 33},
      {inf_double, -inf_double, uint64_max, 0},
      {-0., 4000. * uint64_max, 0, uint64_max}};

  test_ftint_u_s(sizeof(tc_s1), tc_s1, kRoundToNearest);
  test_ftint_u_d(sizeof(tc_d1), tc_d1, kRoundToNearest);

  struct TestCaseMsa2RF_F_U tc_s2[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 4, 1, 0},
      {-0.f, 23.38f, 2.8f, 32.6f, 0, 23, 2, 32},
      {inf_float, -inf_float, 0., 4.f * uint32_max, uint32_max, 0, 0,
       uint32_max}};

  struct TestCaseMsa2RF_D_U tc_d2[] = {
      {0., 4.5, 0, 4},
      {1.49, -12.51, 1, 0},
      {-0., 23.38, 0, 23},
      {2.8, 32.6, 2, 32},
      {inf_double, -inf_double, uint64_max, 0},
      {-0.2345, 4000. * uint64_max, 0, uint64_max}};

  test_ftint_u_s(sizeof(tc_s2), tc_s2, kRoundToZero);
  test_ftint_u_d(sizeof(tc_d2), tc_d2, kRoundToZero);

  struct TestCaseMsa2RF_F_U tc_s3[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 5, 2, 0},
      {-0.f, 23.38f, 2.8f, 32.6f, 0, 24, 3, 33},
      {inf_float, -inf_float, 0, 4.f * uint32_max, uint32_max, 0, 0,
       uint32_max}};

  struct TestCaseMsa2RF_D_U tc_d3[] = {
      {0., 4.5, 0, 5},
      {1.49, -12.51, 2, 0},
      {-0., 23.38, -0, 24},
      {2.8, 32.6, 3, 33},
      {inf_double, -inf_double, uint64_max, 0},
      {-0.5252, 4000. * uint64_max, 0, uint64_max}};

  test_ftint_u_s(sizeof(tc_s3), tc_s3, kRoundToPlusInf);
  test_ftint_u_d(sizeof(tc_d3), tc_d3, kRoundToPlusInf);

  struct TestCaseMsa2RF_F_U tc_s4[] = {
      {0.f, 4.5f, 1.49f, -12.51f, 0, 4, 1, 0},
      {-0.f, 23.38f, 2.8f, 32.6f, 0, 23, 2, 32},
      {inf_float, -inf_float, 0, 4.f * uint32_max, uint32_max, 0, 0,
       uint32_max}};

  struct TestCaseMsa2RF_D_U tc_d4[] = {
      {0., 4.5, 0, 4},
      {1.49, -12.51, 1, 0},
      {-0., 23.38, -0, 23},
      {2.8, 32.6, 2, 32},
      {inf_double, -inf_double, uint64_max, 0},
      {-0.098797, 4000. * uint64_max, 0, uint64_max}};

  test_ftint_u_s(sizeof(tc_s4), tc_s4, kRoundToMinusInf);
  test_ftint_u_d(sizeof(tc_d4), tc_d4, kRoundToMinusInf);
}

struct TestCaseMsa2RF_U_F {
  uint32_t ws1;
  uint32_t ws2;
  uint32_t ws3;
  uint32_t ws4;
  float exp_res_1;
  float exp_res_2;
  float exp_res_3;
  float exp_res_4;
};

struct TestCaseMsa2RF_U_D {
  uint64_t ws1;
  uint64_t ws2;
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_ffint_u) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2RF_U_F tc_s[] = {
      {0, 345, 234, 1000, 0.f, 345.f, 234.f, 1000.f}};

  struct TestCaseMsa2RF_U_D tc_d[] = {{0, 345, 0., 345.},
                                      {234, 1000, 234., 1000.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_U_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ffint_u_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_U_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ffint_u_d(w2, w0); });
  }
}

struct TestCaseMsa2RF_I_F {
  int32_t ws1;
  int32_t ws2;
  int32_t ws3;
  int32_t ws4;
  float exp_res_1;
  float exp_res_2;
  float exp_res_3;
  float exp_res_4;
};

struct TestCaseMsa2RF_I_D {
  int64_t ws1;
  int64_t ws2;
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_ffint_s) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2RF_I_F tc_s[] = {
      {0, 345, -234, 1000, 0.f, 345.f, -234.f, 1000.f}};

  struct TestCaseMsa2RF_I_D tc_d[] = {{0, 345, 0., 345.},
                                      {-234, 1000, -234., 1000.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_I_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ffint_s_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_I_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ffint_s_d(w2, w0); });
  }
}

struct TestCaseMsa2RF_U16_F {
  uint16_t ws1;
  uint16_t ws2;
  uint16_t ws3;
  uint16_t ws4;
  uint16_t ws5;
  uint16_t ws6;
  uint16_t ws7;
  uint16_t ws8;
  float exp_res_1;
  float exp_res_2;
  float exp_res_3;
  float exp_res_4;
};

struct TestCaseMsa2RF_F_D {
  float ws1;
  float ws2;
  float ws3;
  float ws4;
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_fexupl) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  struct TestCaseMsa2RF_U16_F tc_s[] = {
      {1, 2, 0x7C00, 0x0C00, 0, 0x7C00, 0xFC00, 0x8000, 0.f, inf_float,
       -inf_float, -0.f},
      {0xFC00, 0xFFFF, 0x00FF, 0x8000, 0x81FE, 0x8000, 0x0345, 0xAAAA,
       -3.0398368835e-5f, -0.f, 4.9889088e-5f, -5.2062988281e-2f},
      {3, 4, 0x5555, 6, 0x2AAA, 0x8700, 0x7777, 0x6A8B, 5.2062988281e-2f,
       -1.06811523458e-4f, 3.0576e4f, 3.35e3f}};

  struct TestCaseMsa2RF_F_D tc_d[] = {
      {0.f, 123.456f, inf_float, -0.f, inf_double, -0.},
      {-inf_float, -3.f, 0.f, -inf_float, 0., -inf_double},
      {2.3f, 3., 1.37747639043129518071e-41f, -3.22084585277826e35f,
       1.37747639043129518071e-41, -3.22084585277826e35}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_U16_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ fexupl_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_F_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ fexupl_d(w2, w0); });
  }
}

TEST(MSA_fexupr) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const double inf_double = std::numeric_limits<double>::infinity();

  struct TestCaseMsa2RF_U16_F tc_s[] = {
      {0, 0x7C00, 0xFC00, 0x8000, 1, 2, 0x7C00, 0x0C00, 0.f, inf_float,
       -inf_float, -0.f},
      {0x81FE, 0x8000, 0x0345, 0xAAAA, 0xFC00, 0xFFFF, 0x00FF, 0x8000,
       -3.0398368835e-5f, -0.f, 4.9889088e-5f, -5.2062988281e-2f},
      {0x2AAA, 0x8700, 0x7777, 0x6A8B, 3, 4, 0x5555, 6, 5.2062988281e-2f,
       -1.06811523458e-4f, 3.0576e4f, 3.35e3f}};

  struct TestCaseMsa2RF_F_D tc_d[] = {
      {inf_float, -0.f, 0.f, 123.456f, inf_double, -0.},
      {0.f, -inf_float, -inf_float, -3.f, 0., -inf_double},
      {1.37747639043129518071e-41f, -3.22084585277826e35f, 2.3f, 3.,
       1.37747639043129518071e-41, -3.22084585277826e35}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_U16_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ fexupr_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_F_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ fexupr_d(w2, w0); });
  }
}

struct TestCaseMsa2RF_U32_D {
  uint32_t ws1;
  uint32_t ws2;
  uint32_t ws3;
  uint32_t ws4;
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_ffql) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2RF_U16_F tc_s[] = {{0, 3, 0xFFFF, 0x8000, 0x8000, 0xE000,
                                         0x0FF0, 0, -1.f, -0.25f,
                                         0.12451171875f, 0.f}};

  struct TestCaseMsa2RF_U32_D tc_d[] = {
      {0, 45, 0x80000000, 0xE0000000, -1., -0.25},
      {0x28379, 0xAAAA5555, 0x024903D3, 0, 17.853239085525274277e-3, 0.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_U16_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ffql_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_U32_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ffql_d(w2, w0); });
  }
}

TEST(MSA_ffqr) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsa2RF_U16_F tc_s[] = {{0x8000, 0xE000, 0x0FF0, 0, 0, 3,
                                         0xFFFF, 0x8000, -1.f, -0.25f,
                                         0.12451171875f, 0.f}};

  struct TestCaseMsa2RF_U32_D tc_d[] = {
      {0x80000000, 0xE0000000, 0, 45, -1., -0.25},
      {0x024903D3, 0, 0x28379, 0xAAAA5555, 17.853239085525274277e-3, 0.}};

  for (size_t i = 0; i < sizeof(tc_s) / sizeof(TestCaseMsa2RF_U16_F); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_s[i]),
               [](MacroAssembler& assm) { __ ffqr_w(w2, w0); });
  }
  for (size_t i = 0; i < sizeof(tc_d) / sizeof(TestCaseMsa2RF_U32_D); ++i) {
    run_msa_2r(reinterpret_cast<const TestCaseMsa2R*>(&tc_d[i]),
               [](MacroAssembler& assm) { __ ffqr_d(w2, w0); });
  }
}

struct TestCaseMsaVector {
  uint64_t wd_lo;
  uint64_t wd_hi;
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint64_t wt_lo;
  uint64_t wt_hi;
};

template <typename InstFunc, typename OperFunc>
void run_msa_vector(struct TestCaseMsaVector* input,
                    InstFunc GenerateVectorInstructionFunc,
                    OperFunc GenerateOperationFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  load_elements_of_vector(&assm, &(input->ws_lo), w0, t0, t1);
  load_elements_of_vector(&assm, &(input->wt_lo), w2, t0, t1);
  load_elements_of_vector(&assm, &(input->wd_lo), w4, t0, t1);

  GenerateVectorInstructionFunc(assm);

  store_elements_of_vector(&assm, w4, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(GenerateOperationFunc(input->wd_lo, input->ws_lo, input->wt_lo),
           res.d[0]);
  CHECK_EQ(GenerateOperationFunc(input->wd_hi, input->ws_hi, input->wt_hi),
           res.d[1]);
}

TEST(MSA_vector) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaVector tc[] = {
      // wd_lo, wd_hi, ws_lo, ws_hi, wt_lo, wt_hi
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0xDCD39D91F9057627,
       0x64BE4F6DBE9CAA51, 0x6B23DE1A687D9CB9, 0x49547AAD691DA4CA},
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0x401614523D830549,
       0xD7C46D613F50EDDD, 0x52284CBC60A1562B, 0x1756ED510D8849CD},
      {0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 0xD6E2D2EBCB40D72F,
       0x13A619AFCE67B079, 0x36CCE284343E40F9, 0xB4E8F44FD148BF7F}};

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaVector); ++i) {
    run_msa_vector(
        &tc[i], [](MacroAssembler& assm) { __ and_v(w4, w0, w2); },
        [](uint64_t wd, uint64_t ws, uint64_t wt) { return ws & wt; });
    run_msa_vector(
        &tc[i], [](MacroAssembler& assm) { __ or_v(w4, w0, w2); },
        [](uint64_t wd, uint64_t ws, uint64_t wt) { return ws | wt; });
    run_msa_vector(
        &tc[i], [](MacroAssembler& assm) { __ nor_v(w4, w0, w2); },
        [](uint64_t wd, uint64_t ws, uint64_t wt) { return ~(ws | wt); });
    run_msa_vector(
        &tc[i], [](MacroAssembler& assm) { __ xor_v(w4, w0, w2); },
        [](uint64_t wd, uint64_t ws, uint64_t wt) { return ws ^ wt; });
    run_msa_vector(&tc[i], [](MacroAssembler& assm) { __ bmnz_v(w4, w0, w2); },
                   [](uint64_t wd, uint64_t ws, uint64_t wt) {
                     return (ws & wt) | (wd & ~wt);
                   });
    run_msa_vector(&tc[i], [](MacroAssembler& assm) { __ bmz_v(w4, w0, w2); },
                   [](uint64_t wd, uint64_t ws, uint64_t wt) {
                     return (ws & ~wt) | (wd & wt);
                   });
    run_msa_vector(&tc[i], [](MacroAssembler& assm) { __ bsel_v(w4, w0, w2); },
                   [](uint64_t wd, uint64_t ws, uint64_t wt) {
                     return (ws & ~wd) | (wt & wd);
                   });
  }
}

struct TestCaseMsaBit {
  uint64_t wd_lo;
  uint64_t wd_hi;
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint32_t m;
};

template <typename InstFunc, typename OperFunc>
void run_msa_bit(struct TestCaseMsaBit* input, InstFunc GenerateInstructionFunc,
                 OperFunc GenerateOperationFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  load_elements_of_vector(&assm, &(input->ws_lo), w0, t0, t1);
  load_elements_of_vector(&assm, &(input->wd_lo), w2, t0, t1);

  GenerateInstructionFunc(assm, input->m);

  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(GenerateOperationFunc(input->wd_lo, input->ws_lo, input->m),
           res.d[0]);
  CHECK_EQ(GenerateOperationFunc(input->wd_hi, input->ws_hi, input->m),
           res.d[1]);
}

TEST(MSA_slli_srai_srli) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaBit tc[] = {
      // wd_lo, wd_hi     ws_lo,              ws_hi, m
      {0, 0, 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 3},
      {0, 0, 0x64BE4F6DBE9CAA51, 0x6B23DE1A687D9CB9, 5},
      {0, 0, 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 9},
      {0, 0, 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 13},
      {0, 0, 0x566BE7BA4365B70A, 0x01EBBC1937D76CB4, 21},
      {0, 0, 0x380E2DEB9D3F8AAE, 0x017E0DE0BCC6CA42, 30},
      {0, 0, 0xA46A3A9BCB43F4E5, 0x1C62C8473BDFCFFB, 45},
      {0, 0, 0xF6759D85F23B5A2B, 0x5C042AE42C6D12C1, 61}};

#define SLLI_SRLI_DF(lanes, mask, func)      \
  [](uint64_t wd, uint64_t ws, uint32_t m) { \
    uint64_t res = 0;                        \
    int elem_size = kMSARegSize / lanes;     \
    for (int i = 0; i < lanes / 2; ++i) {    \
      int shift = elem_size * i;             \
      uint64_t elem = (ws >> shift) & mask;  \
      res |= ((func)&mask) << shift;         \
    }                                        \
    return res;                              \
  }

#define SRAI_DF(lanes, mask, func)                                            \
  [](uint64_t wd, uint64_t ws, uint32_t m) {                                  \
    uint64_t res = 0;                                                         \
    int elem_size = kMSARegSize / lanes;                                      \
    for (int i = 0; i < lanes / 2; ++i) {                                     \
      int shift = elem_size * i;                                              \
      int64_t elem =                                                          \
          static_cast<int64_t>(((ws >> shift) & mask) << (64 - elem_size)) >> \
          (64 - elem_size);                                                   \
      res |= static_cast<uint64_t>((func)&mask) << shift;                     \
    }                                                                         \
    return res;                                                               \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaBit); ++i) {
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ slli_b(w2, w0, m % 8); },
        SLLI_SRLI_DF(kMSALanesByte, UINT8_MAX, (elem << (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ slli_h(w2, w0, m % 16); },
        SLLI_SRLI_DF(kMSALanesHalf, UINT16_MAX, (elem << (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ slli_w(w2, w0, m % 32); },
        SLLI_SRLI_DF(kMSALanesWord, UINT32_MAX, (elem << (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ slli_d(w2, w0, m % 64); },
        SLLI_SRLI_DF(kMSALanesDword, UINT64_MAX, (elem << (m % elem_size))));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srli_b(w2, w0, m % 8); },
        SLLI_SRLI_DF(kMSALanesByte, UINT8_MAX, (elem >> (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srli_h(w2, w0, m % 16); },
        SLLI_SRLI_DF(kMSALanesHalf, UINT16_MAX, (elem >> (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srli_w(w2, w0, m % 32); },
        SLLI_SRLI_DF(kMSALanesWord, UINT32_MAX, (elem >> (m % elem_size))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srli_d(w2, w0, m % 64); },
        SLLI_SRLI_DF(kMSALanesDword, UINT64_MAX, (elem >> (m % elem_size))));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srlri_b(w2, w0, m % 8); },
        SLLI_SRLI_DF(
            kMSALanesByte, UINT8_MAX,
            (elem >> (m % elem_size)) + ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srlri_h(w2, w0, m % 16); },
        SLLI_SRLI_DF(
            kMSALanesHalf, UINT16_MAX,
            (elem >> (m % elem_size)) + ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srlri_w(w2, w0, m % 32); },
        SLLI_SRLI_DF(
            kMSALanesWord, UINT32_MAX,
            (elem >> (m % elem_size)) + ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srlri_d(w2, w0, m % 64); },
        SLLI_SRLI_DF(
            kMSALanesDword, UINT64_MAX,
            (elem >> (m % elem_size)) + ((elem >> (m % elem_size - 1)) & 0x1)));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srai_b(w2, w0, m % 8); },
        SRAI_DF(kMSALanesByte, UINT8_MAX,
                ArithmeticShiftRight(elem, m % elem_size)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srai_h(w2, w0, m % 16); },
        SRAI_DF(kMSALanesHalf, UINT16_MAX,
                ArithmeticShiftRight(elem, m % elem_size)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srai_w(w2, w0, m % 32); },
        SRAI_DF(kMSALanesWord, UINT32_MAX,
                ArithmeticShiftRight(elem, m % elem_size)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srai_d(w2, w0, m % 64); },
        SRAI_DF(kMSALanesDword, UINT64_MAX,
                ArithmeticShiftRight(elem, m % elem_size)));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srari_b(w2, w0, m % 8); },
        SRAI_DF(kMSALanesByte, UINT8_MAX,
                ArithmeticShiftRight(elem, m % elem_size) +
                    ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srari_h(w2, w0, m % 16); },
        SRAI_DF(kMSALanesHalf, UINT16_MAX,
                ArithmeticShiftRight(elem, m % elem_size) +
                    ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srari_w(w2, w0, m % 32); },
        SRAI_DF(kMSALanesWord, UINT32_MAX,
                ArithmeticShiftRight(elem, m % elem_size) +
                    ((elem >> (m % elem_size - 1)) & 0x1)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ srari_d(w2, w0, m % 64); },
        SRAI_DF(kMSALanesDword, UINT64_MAX,
                ArithmeticShiftRight(elem, m % elem_size) +
                    ((elem >> (m % elem_size - 1)) & 0x1)));
  }
#undef SLLI_SRLI_DF
#undef SRAI_DF
}

TEST(MSA_bclri_bseti_bnegi) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaBit tc[] = {
      // wd_lo, wd_hi,    ws_lo,              ws_hi, m
      {0, 0, 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 3},
      {0, 0, 0x64BE4F6DBE9CAA51, 0x6B23DE1A687D9CB9, 5},
      {0, 0, 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 9},
      {0, 0, 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 13},
      {0, 0, 0x566BE7BA4365B70A, 0x01EBBC1937D76CB4, 21},
      {0, 0, 0x380E2DEB9D3F8AAE, 0x017E0DE0BCC6CA42, 30},
      {0, 0, 0xA46A3A9BCB43F4E5, 0x1C62C8473BDFCFFB, 45},
      {0, 0, 0xF6759D85F23B5A2B, 0x5C042AE42C6D12C1, 61}};

#define BCLRI_BSETI_BNEGI_DF(lanes, mask, func) \
  [](uint64_t wd, uint64_t ws, uint32_t m) {    \
    uint64_t res = 0;                           \
    int elem_size = kMSARegSize / lanes;        \
    for (int i = 0; i < lanes / 2; ++i) {       \
      int shift = elem_size * i;                \
      uint64_t elem = (ws >> shift) & mask;     \
      res |= ((func)&mask) << shift;            \
    }                                           \
    return res;                                 \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaBit); ++i) {
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bclri_b(w2, w0, m % 8); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesByte, UINT8_MAX,
                             (~(1ull << (m % elem_size)) & elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bclri_h(w2, w0, m % 16); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesHalf, UINT16_MAX,
                             (~(1ull << (m % elem_size)) & elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bclri_w(w2, w0, m % 32); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesWord, UINT32_MAX,
                             (~(1ull << (m % elem_size)) & elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bclri_d(w2, w0, m % 64); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesDword, UINT64_MAX,
                             (~(1ull << (m % elem_size)) & elem)));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bseti_b(w2, w0, m % 8); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesByte, UINT8_MAX,
                             ((1ull << (m % elem_size)) | elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bseti_h(w2, w0, m % 16); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesHalf, UINT16_MAX,
                             ((1ull << (m % elem_size)) | elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bseti_w(w2, w0, m % 32); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesWord, UINT32_MAX,
                             ((1ull << (m % elem_size)) | elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bseti_d(w2, w0, m % 64); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesDword, UINT64_MAX,
                             ((1ull << (m % elem_size)) | elem)));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bnegi_b(w2, w0, m % 8); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesByte, UINT8_MAX,
                             ((1ull << (m % elem_size)) ^ elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bnegi_h(w2, w0, m % 16); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesHalf, UINT16_MAX,
                             ((1ull << (m % elem_size)) ^ elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bnegi_w(w2, w0, m % 32); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesWord, UINT32_MAX,
                             ((1ull << (m % elem_size)) ^ elem)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ bnegi_d(w2, w0, m % 64); },
        BCLRI_BSETI_BNEGI_DF(kMSALanesDword, UINT64_MAX,
                             ((1ull << (m % elem_size)) ^ elem)));
  }
#undef BCLRI_BSETI_BNEGI_DF
}

TEST(MSA_binsli_binsri) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaBit tc[] = {// wd_lo, wd_hi, ws_lo, ws_hi, m
                                {0x53F4457553BBD5B4, 0x5FB8250EACC296B2,
                                 0xF35862E13E38F8B0, 0x4F41FFDEF2BFE636, 3},
                                {0xF61BFDB0F312E6FC, 0xC9437568DD1EA925,
                                 0x64BE4F6DBE9CAA51, 0x6B23DE1A687D9CB9, 5},
                                {0x53F4457553BBD5B4, 0x5FB8250EACC296B2,
                                 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 9},
                                {0xF61BFDB0F312E6FC, 0xC9437568DD1EA925,
                                 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 13},
                                {0x53F4457553BBD5B4, 0x5FB8250EACC296B2,
                                 0x566BE7BA4365B70A, 0x01EBBC1937D76CB4, 21},
                                {0xF61BFDB0F312E6FC, 0xC9437568DD1EA925,
                                 0x380E2DEB9D3F8AAE, 0x017E0DE0BCC6CA42, 30},
                                {0x53F4457553BBD5B4, 0x5FB8250EACC296B2,
                                 0xA46A3A9BCB43F4E5, 0x1C62C8473BDFCFFB, 45},
                                {0xF61BFDB0F312E6FC, 0xC9437568DD1EA925,
                                 0xF6759D85F23B5A2B, 0x5C042AE42C6D12C1, 61}};

#define BINSLI_BINSRI_DF(lanes, mask, func)             \
  [](uint64_t wd, uint64_t ws, uint32_t m) {            \
    uint64_t res = 0;                                   \
    int elem_size = kMSARegSize / lanes;                \
    int bits = m % elem_size + 1;                       \
    for (int i = 0; i < lanes / 2; ++i) {               \
      int shift = elem_size * i;                        \
      uint64_t ws_elem = (ws >> shift) & mask;          \
      if (bits == elem_size) {                          \
        res |= (ws_elem & mask) << shift;               \
      } else {                                          \
        uint64_t r_mask = (1ull << bits) - 1;           \
        uint64_t l_mask = r_mask << (elem_size - bits); \
        USE(l_mask);                                    \
        uint64_t wd_elem = (wd >> shift) & mask;        \
        res |= ((func)&mask) << shift;                  \
      }                                                 \
    }                                                   \
    return res;                                         \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaBit); ++i) {
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsli_b(w2, w0, m % 8); },
        BINSLI_BINSRI_DF(kMSALanesByte, UINT8_MAX,
                         ((ws_elem & l_mask) | (wd_elem & ~l_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsli_h(w2, w0, m % 16); },
        BINSLI_BINSRI_DF(kMSALanesHalf, UINT16_MAX,
                         ((ws_elem & l_mask) | (wd_elem & ~l_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsli_w(w2, w0, m % 32); },
        BINSLI_BINSRI_DF(kMSALanesWord, UINT32_MAX,
                         ((ws_elem & l_mask) | (wd_elem & ~l_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsli_d(w2, w0, m % 64); },
        BINSLI_BINSRI_DF(kMSALanesDword, UINT64_MAX,
                         ((ws_elem & l_mask) | (wd_elem & ~l_mask))));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsri_b(w2, w0, m % 8); },
        BINSLI_BINSRI_DF(kMSALanesByte, UINT8_MAX,
                         ((ws_elem & r_mask) | (wd_elem & ~r_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsri_h(w2, w0, m % 16); },
        BINSLI_BINSRI_DF(kMSALanesHalf, UINT16_MAX,
                         ((ws_elem & r_mask) | (wd_elem & ~r_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsri_w(w2, w0, m % 32); },
        BINSLI_BINSRI_DF(kMSALanesWord, UINT32_MAX,
                         ((ws_elem & r_mask) | (wd_elem & ~r_mask))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ binsri_d(w2, w0, m % 64); },
        BINSLI_BINSRI_DF(kMSALanesDword, UINT64_MAX,
                         ((ws_elem & r_mask) | (wd_elem & ~r_mask))));
  }
#undef BINSLI_BINSRI_DF
}

TEST(MSA_sat_s_sat_u) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  struct TestCaseMsaBit tc[] = {
      // wd_lo, wd_hi,    ws_lo,              ws_hi, m
      {0, 0, 0xF35862E13E3808B0, 0x4F41FFDEF2BFE636, 3},
      {0, 0, 0x64BE4F6DBE9CAA51, 0x6B23DE1A687D9CB9, 5},
      {0, 0, 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 9},
      {0, 0, 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 13},
      {0, 0, 0x566BE7BA4365B70A, 0x01EBBC1937D76CB4, 21},
      {0, 0, 0x380E2DEB9D3F8AAE, 0x017E0DE0BCC6CA42, 30},
      {0, 0, 0xA46A3A9BCB43F4E5, 0x1C62C8473BDFCFFB, 45},
      {0, 0, 0xF6759D85F23B5A2B, 0x5C042AE42C6D12C1, 61}};

#define SAT_DF(lanes, mask, func)                                              \
  [](uint64_t wd, uint64_t ws, uint32_t m) {                                   \
    uint64_t res = 0;                                                          \
    int elem_size = kMSARegSize / lanes;                                       \
    m %= elem_size;                                                            \
    for (int i = 0; i < lanes / 2; ++i) {                                      \
      int shift = elem_size * i;                                               \
      uint64_t elem_u64 = (ws >> shift) & mask;                                \
      int64_t elem_i64 = static_cast<int64_t>(elem_u64 << (64 - elem_size)) >> \
                         (64 - elem_size);                                     \
      USE(elem_i64);                                                           \
      res |= ((func)&mask) << shift;                                           \
    }                                                                          \
    return res;                                                                \
  }

#define M_MAX_INT(x) static_cast<int64_t>((1LL << ((x)-1)) - 1)
#define M_MIN_INT(x) static_cast<int64_t>(-(1LL << ((x)-1)))
#define M_MAX_UINT(x) static_cast<uint64_t>(-1ULL >> (64 - (x)))

  for (size_t i = 0; i < sizeof(tc) / sizeof(TestCaseMsaBit); ++i) {
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_u_b(w2, w0, m % 8); },
        SAT_DF(kMSALanesByte, UINT8_MAX,
               (elem_u64 < M_MAX_UINT(m + 1) ? elem_u64 : M_MAX_UINT(m + 1))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_u_h(w2, w0, m % 16); },
        SAT_DF(kMSALanesHalf, UINT16_MAX,
               (elem_u64 < M_MAX_UINT(m + 1) ? elem_u64 : M_MAX_UINT(m + 1))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_u_w(w2, w0, m % 32); },
        SAT_DF(kMSALanesWord, UINT32_MAX,
               (elem_u64 < M_MAX_UINT(m + 1) ? elem_u64 : M_MAX_UINT(m + 1))));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_u_d(w2, w0, m % 64); },
        SAT_DF(kMSALanesDword, UINT64_MAX,
               (elem_u64 < M_MAX_UINT(m + 1) ? elem_u64 : M_MAX_UINT(m + 1))));

    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_s_b(w2, w0, m % 8); },
        SAT_DF(
            kMSALanesByte, UINT8_MAX,
            (elem_i64 < M_MIN_INT(m + 1)
                 ? M_MIN_INT(m + 1)
                 : elem_i64 > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1) : elem_i64)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_s_h(w2, w0, m % 16); },
        SAT_DF(
            kMSALanesHalf, UINT16_MAX,
            (elem_i64 < M_MIN_INT(m + 1)
                 ? M_MIN_INT(m + 1)
                 : elem_i64 > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1) : elem_i64)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_s_w(w2, w0, m % 32); },
        SAT_DF(
            kMSALanesWord, UINT32_MAX,
            (elem_i64 < M_MIN_INT(m + 1)
                 ? M_MIN_INT(m + 1)
                 : elem_i64 > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1) : elem_i64)));
    run_msa_bit(
        &tc[i],
        [](MacroAssembler& assm, uint32_t m) { __ sat_s_d(w2, w0, m % 64); },
        SAT_DF(
            kMSALanesDword, UINT64_MAX,
            (elem_i64 < M_MIN_INT(m + 1)
                 ? M_MIN_INT(m + 1)
                 : elem_i64 > M_MAX_INT(m + 1) ? M_MAX_INT(m + 1) : elem_i64)));
  }

#undef SAT_DF
#undef M_MAX_INT
#undef M_MIN_INT
#undef M_MAX_UINT
}

template <typename InstFunc, typename OperFunc>
void run_msa_i10(int32_t input, InstFunc GenerateVectorInstructionFunc,
                 OperFunc GenerateOperationFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  GenerateVectorInstructionFunc(assm, input);

  store_elements_of_vector(&assm, w0, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(GenerateOperationFunc(input), res.d[0]);
  CHECK_EQ(GenerateOperationFunc(input), res.d[1]);
}

TEST(MSA_ldi) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  // signed 10bit integers: -512 .. 511
  int32_t tc[] = {0, -1, 1, 256, -256, -178, 352, -512, 511};

#define LDI_DF(lanes, mask)                                        \
  [](int32_t s10) {                                                \
    uint64_t res = 0;                                              \
    int elem_size = kMSARegSize / lanes;                           \
    int64_t s10_64 =                                               \
        ArithmeticShiftRight(static_cast<int64_t>(s10) << 54, 54); \
    for (int i = 0; i < lanes / 2; ++i) {                          \
      int shift = elem_size * i;                                   \
      res |= static_cast<uint64_t>(s10_64 & mask) << shift;        \
    }                                                              \
    return res;                                                    \
  }

  for (size_t i = 0; i < sizeof(tc) / sizeof(int32_t); ++i) {
    run_msa_i10(tc[i],
                [](MacroAssembler& assm, int32_t s10) { __ ldi_b(w0, s10); },
                LDI_DF(kMSALanesByte, UINT8_MAX));
    run_msa_i10(tc[i],
                [](MacroAssembler& assm, int32_t s10) { __ ldi_h(w0, s10); },
                LDI_DF(kMSALanesHalf, UINT16_MAX));
    run_msa_i10(tc[i],
                [](MacroAssembler& assm, int32_t s10) { __ ldi_w(w0, s10); },
                LDI_DF(kMSALanesWord, UINT32_MAX));
    run_msa_i10(tc[i],
                [](MacroAssembler& assm, int32_t s10) { __ ldi_d(w0, s10); },
                LDI_DF(kMSALanesDword, UINT64_MAX));
  }
#undef LDI_DF
}

template <typename T, typename InstFunc>
void run_msa_mi10(InstFunc GenerateVectorInstructionFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  T in_test_vector[1024];
  T out_test_vector[1024];

  T* in_array_middle = in_test_vector + arraysize(in_test_vector) / 2;
  T* out_array_middle = out_test_vector + arraysize(out_test_vector) / 2;

  v8::base::RandomNumberGenerator rand_gen(v8_flags.random_seed);
  for (unsigned int i = 0; i < arraysize(in_test_vector); i++) {
    in_test_vector[i] = static_cast<T>(rand_gen.NextInt());
    out_test_vector[i] = 0;
  }

  GenerateVectorInstructionFunc(assm);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F5>::FromCode(isolate, *code);

  f.Call(in_array_middle, out_array_middle, 0, 0, 0);

  CHECK_EQ(memcmp(in_test_vector, out_test_vector, arraysize(in_test_vector)),
           0);
}

TEST(MSA_load_store_vector) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  run_msa_mi10<uint8_t>([](MacroAssembler& assm) {
    for (int i = -512; i < 512; i += 16) {
      __ ld_b(w0, MemOperand(a0, i));
      __ st_b(w0, MemOperand(a1, i));
    }
  });
  run_msa_mi10<uint16_t>([](MacroAssembler& assm) {
    for (int i = -512; i < 512; i += 8) {
      __ ld_h(w0, MemOperand(a0, i));
      __ st_h(w0, MemOperand(a1, i));
    }
  });
  run_msa_mi10<uint32_t>([](MacroAssembler& assm) {
    for (int i = -512; i < 512; i += 4) {
      __ ld_w(w0, MemOperand(a0, i));
      __ st_w(w0, MemOperand(a1, i));
    }
  });
  run_msa_mi10<uint64_t>([](MacroAssembler& assm) {
    for (int i = -512; i < 512; i += 2) {
      __ ld_d(w0, MemOperand(a0, i));
      __ st_d(w0, MemOperand(a1, i));
    }
  });
}

struct TestCaseMsa3R {
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint64_t wt_lo;
  uint64_t wt_hi;
  uint64_t wd_lo;
  uint64_t wd_hi;
};

static const uint64_t Unpredictable = 0x312014017725ll;

template <typename InstFunc, typename OperFunc>
void run_msa_3r(struct TestCaseMsa3R* input, InstFunc GenerateI5InstructionFunc,
                OperFunc GenerateOperationFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  load_elements_of_vector(&assm, &(input->wt_lo), w0, t0, t1);
  load_elements_of_vector(&assm, &(input->ws_lo), w1, t0, t1);
  load_elements_of_vector(&assm, &(input->wd_lo), w2, t0, t1);

  GenerateI5InstructionFunc(assm);

  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  GenerateOperationFunc(&input->ws_lo, &input->wt_lo, &input->wd_lo);
  if (input->wd_lo != Unpredictable) {
    CHECK_EQ(input->wd_lo, res.d[0]);
  }
  if (input->wd_hi != Unpredictable) {
    CHECK_EQ(input->wd_hi, res.d[1]);
  }
}

TEST(MSA_3R_instructions) {
  if (kArchVariant == kMips64r6 || !CpuFeatures::IsSupported(MIPS_SIMD)) return;

  CcTest::InitializeVM();

  struct TestCaseMsa3R tc[] = {
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x1169751BB9A7D9C3,
       0xF7A594AEC8EF8A9C, 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C},
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x2B665362C4E812DF,
       0x3A0D80D68B3F8BC8, 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8},
      {0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C, 0x1169751BB9A7D9C3,
       0xF7A594AEC8EF8A9C, 0x1169751BB9A7D9C3, 0xF7A594AEC8EF8A9C},
      {0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8, 0x2B665362C4E812DF,
       0x3A0D80D68B3F8BC8, 0x2B665362C4E812DF, 0x3A0D80D68B3F8BC8},
      {0xFFAB807F807FFFCD, 0x7F23FF80FF567F80, 0xFFAB807F807FFFCD,
       0x7F23FF80FF567F80, 0xFFAB807F807FFFCD, 0x7F23FF80FF567F80},
      {0x80FFEFFF7F12807F, 0x807F80FF7FDEFF78, 0x80FFEFFF7F12807F,
       0x807F80FF7FDEFF78, 0x80FFEFFF7F12807F, 0x807F80FF7FDEFF78},
      {0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
       0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF},
      {0x0000000000000000, 0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF,
       0x0000000000000000, 0x0000000000000000, 0xFFFFFFFFFFFFFFFF},
      {0xFFFF0000FFFF0000, 0xFFFF0000FFFF0000, 0xFFFF0000FFFF0000,
       0xFFFF0000FFFF0000, 0xFFFF0000FFFF0000, 0xFFFF0000FFFF0000},
      {0xFF00FF00FF00FF00, 0xFF00FF00FF00FF00, 0xFF00FF00FF00FF00,
       0xFF00FF00FF00FF00, 0xFF00FF00FF00FF00, 0xFF00FF00FF00FF00},
      {0xF0F0F0F0F0F0F0F0, 0xF0F0F0F0F0F0F0F0, 0xF0F0F0F0F0F0F0F0,
       0xF0F0F0F0F0F0F0F0, 0xF0F0F0F0F0F0F0F0, 0xF0F0F0F0F0F0F0F0},
      {0xFF0000FFFF0000FF, 0xFF0000FFFF0000FF, 0xFF0000FFFF0000FF,
       0xFF0000FFFF0000FF, 0xFF0000FFFF0000FF, 0xFF0000FFFF0000FF},
      {0xFFFF00000000FFFF, 0xFFFF00000000FFFF, 0xFFFF00000000FFFF,
       0xFFFF00000000FFFF, 0xFFFF00000000FFFF, 0xFFFF00000000FFFF}};

#define SLL_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                  \
  for (int i = 0; i < 2; i++) {                                            \
    uint64_t res = 0;                                                      \
    for (int j = 0; j < lanes / 2; ++j) {                                  \
      uint64_t shift = size_in_bits * j;                                   \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                  \
      T shift_op = static_cast<T>((wt[i] >> shift) & mask) % size_in_bits; \
      res |= (static_cast<uint64_t>(src_op << shift_op) & mask) << shift;  \
    }                                                                      \
    wd[i] = res;                                                           \
  }

#define SRA_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      int shift_op = ((wt[i] >> shift) & mask) % size_in_bits;               \
      res |= (static_cast<uint64_t>(ArithmeticShiftRight(src_op, shift_op) & \
                                    mask))                                   \
             << shift;                                                       \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define SRL_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      res |= (static_cast<uint64_t>(src_op >> shift_op) & mask) << shift;    \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define BCRL_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      T r = (static_cast<T>(~(1ull << shift_op)) & src_op) & mask;           \
      res |= static_cast<uint64_t>(r) << shift;                              \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define BSET_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      T r = (static_cast<T>(1ull << shift_op) | src_op) & mask;              \
      res |= static_cast<uint64_t>(r) << shift;                              \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define BNEG_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      T r = (static_cast<T>(1ull << shift_op) ^ src_op) & mask;              \
      res |= static_cast<uint64_t>(r) << shift;                              \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define BINSL_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                     \
      T wd_op = static_cast<T>((wd[i] >> shift) & mask);                     \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      int64_t bits = shift_op + 1;                                           \
      T r;                                                                   \
      if (bits == size_in_bits) {                                            \
        r = static_cast<T>(ws_op);                                           \
      } else {                                                               \
        uint64_t mask2 = ((1ull << bits) - 1) << (size_in_bits - bits);      \
        r = static_cast<T>((static_cast<T>(mask2) & ws_op) |                 \
                           (static_cast<T>(~mask2) & wd_op));                \
      }                                                                      \
      res |= static_cast<uint64_t>(r) << shift;                              \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define BINSR_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                     \
      T wd_op = static_cast<T>((wd[i] >> shift) & mask);                     \
      T shift_op = static_cast<T>(((wt[i] >> shift) & mask) % size_in_bits); \
      int64_t bits = shift_op + 1;                                           \
      T r;                                                                   \
      if (bits == size_in_bits) {                                            \
        r = static_cast<T>(ws_op);                                           \
      } else {                                                               \
        uint64_t mask2 = (1ull << bits) - 1;                                 \
        r = static_cast<T>((static_cast<T>(mask2) & ws_op) |                 \
                           (static_cast<T>(~mask2) & wd_op));                \
      }                                                                      \
      res |= static_cast<uint64_t>(r) << shift;                              \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define ADDV_DF(T, lanes, mask)                                      \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(ws_op + wt_op) & mask) << shift; \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define SUBV_DF(T, lanes, mask)                                      \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(ws_op - wt_op) & mask) << shift; \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define MAX_DF(T, lanes, mask)                                         \
  int size_in_bits = kMSARegSize / lanes;                              \
  for (int i = 0; i < 2; i++) {                                        \
    uint64_t res = 0;                                                  \
    for (int j = 0; j < lanes / 2; ++j) {                              \
      uint64_t shift = size_in_bits * j;                               \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);               \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);               \
      res |= (static_cast<uint64_t>(std::max<T>(ws_op, wt_op)) & mask) \
             << shift;                                                 \
    }                                                                  \
    wd[i] = res;                                                       \
  }

#define MIN_DF(T, lanes, mask)                                         \
  int size_in_bits = kMSARegSize / lanes;                              \
  for (int i = 0; i < 2; i++) {                                        \
    uint64_t res = 0;                                                  \
    for (int j = 0; j < lanes / 2; ++j) {                              \
      uint64_t shift = size_in_bits * j;                               \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);               \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);               \
      res |= (static_cast<uint64_t>(std::min<T>(ws_op, wt_op)) & mask) \
             << shift;                                                 \
    }                                                                  \
    wd[i] = res;                                                       \
  }

#define MAXA_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                     \
  for (int i = 0; i < 2; i++) {                                               \
    uint64_t res = 0;                                                         \
    for (int j = 0; j < lanes / 2; ++j) {                                     \
      uint64_t shift = size_in_bits * j;                                      \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                      \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                      \
      res |=                                                                  \
          (static_cast<uint64_t>(Nabs(ws_op) < Nabs(wt_op) ? ws_op : wt_op) & \
           mask)                                                              \
          << shift;                                                           \
    }                                                                         \
    wd[i] = res;                                                              \
  }

#define MINA_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                     \
  for (int i = 0; i < 2; i++) {                                               \
    uint64_t res = 0;                                                         \
    for (int j = 0; j < lanes / 2; ++j) {                                     \
      uint64_t shift = size_in_bits * j;                                      \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                      \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                      \
      res |=                                                                  \
          (static_cast<uint64_t>(Nabs(ws_op) > Nabs(wt_op) ? ws_op : wt_op) & \
           mask)                                                              \
          << shift;                                                           \
    }                                                                         \
    wd[i] = res;                                                              \
  }

#define CEQ_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                     \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                     \
      res |= (static_cast<uint64_t>(!Compare(ws_op, wt_op) ? -1ull : 0ull) & \
              mask)                                                          \
             << shift;                                                       \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define CLT_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                   \
  for (int i = 0; i < 2; i++) {                                             \
    uint64_t res = 0;                                                       \
    for (int j = 0; j < lanes / 2; ++j) {                                   \
      uint64_t shift = size_in_bits * j;                                    \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                    \
      res |= (static_cast<uint64_t>((Compare(ws_op, wt_op) == -1) ? -1ull   \
                                                                  : 0ull) & \
              mask)                                                         \
             << shift;                                                      \
    }                                                                       \
    wd[i] = res;                                                            \
  }

#define CLE_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                  \
  for (int i = 0; i < 2; i++) {                                            \
    uint64_t res = 0;                                                      \
    for (int j = 0; j < lanes / 2; ++j) {                                  \
      uint64_t shift = size_in_bits * j;                                   \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                   \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                   \
      res |= (static_cast<uint64_t>((Compare(ws_op, wt_op) != 1) ? -1ull   \
                                                                 : 0ull) & \
              mask)                                                        \
             << shift;                                                     \
    }                                                                      \
    wd[i] = res;                                                           \
  }

#define ADD_A_DF(T, lanes, mask)                                               \
  int size_in_bits = kMSARegSize / lanes;                                      \
  for (int i = 0; i < 2; i++) {                                                \
    uint64_t res = 0;                                                          \
    for (int j = 0; j < lanes / 2; ++j) {                                      \
      uint64_t shift = size_in_bits * j;                                       \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                       \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                       \
      res |= (static_cast<uint64_t>(Abs(ws_op) + Abs(wt_op)) & mask) << shift; \
    }                                                                          \
    wd[i] = res;                                                               \
  }

#define ADDS_A_DF(T, lanes, mask)                              \
  int size_in_bits = kMSARegSize / lanes;                      \
  for (int i = 0; i < 2; i++) {                                \
    uint64_t res = 0;                                          \
    for (int j = 0; j < lanes / 2; ++j) {                      \
      uint64_t shift = size_in_bits * j;                       \
      T ws_op = Nabs(static_cast<T>((ws[i] >> shift) & mask)); \
      T wt_op = Nabs(static_cast<T>((wt[i] >> shift) & mask)); \
      T r;                                                     \
      if (ws_op < -std::numeric_limits<T>::max() - wt_op) {    \
        r = std::numeric_limits<T>::max();                     \
      } else {                                                 \
        r = -(ws_op + wt_op);                                  \
      }                                                        \
      res |= (static_cast<uint64_t>(r) & mask) << shift;       \
    }                                                          \
    wd[i] = res;                                               \
  }

#define ADDS_DF(T, lanes, mask)                                        \
  int size_in_bits = kMSARegSize / lanes;                              \
  for (int i = 0; i < 2; i++) {                                        \
    uint64_t res = 0;                                                  \
    for (int j = 0; j < lanes / 2; ++j) {                              \
      uint64_t shift = size_in_bits * j;                               \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);               \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);               \
      res |= (static_cast<uint64_t>(SaturateAdd(ws_op, wt_op)) & mask) \
             << shift;                                                 \
    }                                                                  \
    wd[i] = res;                                                       \
  }

#define AVE_DF(T, lanes, mask)                                       \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(                                 \
                 ((wt_op & ws_op) + ((ws_op ^ wt_op) >> 1)) & mask)) \
             << shift;                                               \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define AVER_DF(T, lanes, mask)                                      \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(                                 \
                 ((wt_op | ws_op) - ((ws_op ^ wt_op) >> 1)) & mask)) \
             << shift;                                               \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define SUBS_DF(T, lanes, mask)                                        \
  int size_in_bits = kMSARegSize / lanes;                              \
  for (int i = 0; i < 2; i++) {                                        \
    uint64_t res = 0;                                                  \
    for (int j = 0; j < lanes / 2; ++j) {                              \
      uint64_t shift = size_in_bits * j;                               \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);               \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);               \
      res |= (static_cast<uint64_t>(SaturateSub(ws_op, wt_op)) & mask) \
             << shift;                                                 \
    }                                                                  \
    wd[i] = res;                                                       \
  }

#define SUBSUS_U_DF(T, lanes, mask)                           \
  using uT = typename std::make_unsigned<T>::type;            \
  int size_in_bits = kMSARegSize / lanes;                     \
  for (int i = 0; i < 2; i++) {                               \
    uint64_t res = 0;                                         \
    for (int j = 0; j < lanes / 2; ++j) {                     \
      uint64_t shift = size_in_bits * j;                      \
      uT ws_op = static_cast<uT>((ws[i] >> shift) & mask);    \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);      \
      T r;                                                    \
      if (wt_op > 0) {                                        \
        uT wtu = static_cast<uT>(wt_op);                      \
        if (wtu > ws_op) {                                    \
          r = 0;                                              \
        } else {                                              \
          r = static_cast<T>(ws_op - wtu);                    \
        }                                                     \
      } else {                                                \
        if (ws_op > std::numeric_limits<uT>::max() + wt_op) { \
          r = static_cast<T>(std::numeric_limits<uT>::max()); \
        } else {                                              \
          r = static_cast<T>(ws_op - wt_op);                  \
        }                                                     \
      }                                                       \
      res |= (static_cast<uint64_t>(r) & mask) << shift;      \
    }                                                         \
    wd[i] = res;                                              \
  }

#define SUBSUU_S_DF(T, lanes, mask)                        \
  using uT = typename std::make_unsigned<T>::type;         \
  int size_in_bits = kMSARegSize / lanes;                  \
  for (int i = 0; i < 2; i++) {                            \
    uint64_t res = 0;                                      \
    for (int j = 0; j < lanes / 2; ++j) {                  \
      uint64_t shift = size_in_bits * j;                   \
      uT ws_op = static_cast<uT>((ws[i] >> shift) & mask); \
      uT wt_op = static_cast<uT>((wt[i] >> shift) & mask); \
      uT wdu;                                              \
      T r;                                                 \
      if (ws_op > wt_op) {                                 \
        wdu = ws_op - wt_op;                               \
        if (wdu > std::numeric_limits<T>::max()) {         \
          r = std::numeric_limits<T>::max();               \
        } else {                                           \
          r = static_cast<T>(wdu);                         \
        }                                                  \
      } else {                                             \
        wdu = wt_op - ws_op;                               \
        CHECK(-std::numeric_limits<T>::max() ==            \
              std::numeric_limits<T>::min() + 1);          \
        if (wdu <= std::numeric_limits<T>::max()) {        \
          r = -static_cast<T>(wdu);                        \
        } else {                                           \
          r = std::numeric_limits<T>::min();               \
        }                                                  \
      }                                                    \
      res |= (static_cast<uint64_t>(r) & mask) << shift;   \
    }                                                      \
    wd[i] = res;                                           \
  }

#define ASUB_S_DF(T, lanes, mask)                                         \
  int size_in_bits = kMSARegSize / lanes;                                 \
  for (int i = 0; i < 2; i++) {                                           \
    uint64_t res = 0;                                                     \
    for (int j = 0; j < lanes / 2; ++j) {                                 \
      uint64_t shift = size_in_bits * j;                                  \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                  \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                  \
      res |= (static_cast<uint64_t>(Abs(ws_op - wt_op)) & mask) << shift; \
    }                                                                     \
    wd[i] = res;                                                          \
  }

#define ASUB_U_DF(T, lanes, mask)                                    \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(ws_op > wt_op ? ws_op - wt_op    \
                                                  : wt_op - ws_op) & \
              mask)                                                  \
             << shift;                                               \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define MULV_DF(T, lanes, mask)                                      \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      res |= (static_cast<uint64_t>(ws_op * wt_op) & mask) << shift; \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define MADDV_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                     \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                     \
      T wd_op = static_cast<T>((wd[i] >> shift) & mask);                     \
      res |= (static_cast<uint64_t>(wd_op + ws_op * wt_op) & mask) << shift; \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define MSUBV_DF(T, lanes, mask)                                             \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                     \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                     \
      T wd_op = static_cast<T>((wd[i] >> shift) & mask);                     \
      res |= (static_cast<uint64_t>(wd_op - ws_op * wt_op) & mask) << shift; \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define DIV_DF(T, lanes, mask)                                       \
  int size_in_bits = kMSARegSize / lanes;                            \
  for (int i = 0; i < 2; i++) {                                      \
    uint64_t res = 0;                                                \
    for (int j = 0; j < lanes / 2; ++j) {                            \
      uint64_t shift = size_in_bits * j;                             \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);             \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);             \
      if (wt_op == 0) {                                              \
        res = Unpredictable;                                         \
        break;                                                       \
      }                                                              \
      res |= (static_cast<uint64_t>(ws_op / wt_op) & mask) << shift; \
    }                                                                \
    wd[i] = res;                                                     \
  }

#define MOD_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                   \
  for (int i = 0; i < 2; i++) {                                             \
    uint64_t res = 0;                                                       \
    for (int j = 0; j < lanes / 2; ++j) {                                   \
      uint64_t shift = size_in_bits * j;                                    \
      T ws_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      T wt_op = static_cast<T>((wt[i] >> shift) & mask);                    \
      if (wt_op == 0) {                                                     \
        res = Unpredictable;                                                \
        break;                                                              \
      }                                                                     \
      res |= (static_cast<uint64_t>(wt_op != 0 ? ws_op % wt_op : 0) & mask) \
             << shift;                                                      \
    }                                                                       \
    wd[i] = res;                                                            \
  }

#define SRAR_DF(T, lanes, mask)                                              \
  int size_in_bits = kMSARegSize / lanes;                                    \
  for (int i = 0; i < 2; i++) {                                              \
    uint64_t res = 0;                                                        \
    for (int j = 0; j < lanes / 2; ++j) {                                    \
      uint64_t shift = size_in_bits * j;                                     \
      T src_op = static_cast<T>((ws[i] >> shift) & mask);                    \
      int shift_op = ((wt[i] >> shift) & mask) % size_in_bits;               \
      uint32_t bit = shift_op == 0 ? 0 : src_op >> (shift_op - 1) & 1;       \
      res |= (static_cast<uint64_t>(ArithmeticShiftRight(src_op, shift_op) + \
                                    bit) &                                   \
              mask)                                                          \
             << shift;                                                       \
    }                                                                        \
    wd[i] = res;                                                             \
  }

#define PCKEV_DF(T, lanes, mask)        \
  T* ws_p = reinterpret_cast<T*>(ws);   \
  T* wt_p = reinterpret_cast<T*>(wt);   \
  T* wd_p = reinterpret_cast<T*>(wd);   \
  for (int i = 0; i < lanes / 2; ++i) { \
    wd_p[i] = wt_p[2 * i];              \
    wd_p[i + lanes / 2] = ws_p[2 * i];  \
  }

#define PCKOD_DF(T, lanes, mask)           \
  T* ws_p = reinterpret_cast<T*>(ws);      \
  T* wt_p = reinterpret_cast<T*>(wt);      \
  T* wd_p = reinterpret_cast<T*>(wd);      \
  for (int i = 0; i < lanes / 2; ++i) {    \
    wd_p[i] = wt_p[2 * i + 1];             \
    wd_p[i + lanes / 2] = ws_p[2 * i + 1]; \
  }

#define ILVL_DF(T, lanes, mask)            \
  T* ws_p = reinterpret_cast<T*>(ws);      \
  T* wt_p = reinterpret_cast<T*>(wt);      \
  T* wd_p = reinterpret_cast<T*>(wd);      \
  for (int i = 0; i < lanes / 2; ++i) {    \
    wd_p[2 * i] = wt_p[i + lanes / 2];     \
    wd_p[2 * i + 1] = ws_p[i + lanes / 2]; \
  }

#define ILVR_DF(T, lanes, mask)         \
  T* ws_p = reinterpret_cast<T*>(ws);   \
  T* wt_p = reinterpret_cast<T*>(wt);   \
  T* wd_p = reinterpret_cast<T*>(wd);   \
  for (int i = 0; i < lanes / 2; ++i) { \
    wd_p[2 * i] = wt_p[i];              \
    wd_p[2 * i + 1] = ws_p[i];          \
  }

#define ILVEV_DF(T, lanes, mask)        \
  T* ws_p = reinterpret_cast<T*>(ws);   \
  T* wt_p = reinterpret_cast<T*>(wt);   \
  T* wd_p = reinterpret_cast<T*>(wd);   \
  for (int i = 0; i < lanes / 2; ++i) { \
    wd_p[2 * i] = wt_p[2 * i];          \
    wd_p[2 * i + 1] = ws_p[2 * i];      \
  }

#define ILVOD_DF(T, lanes, mask)        \
  T* ws_p = reinterpret_cast<T*>(ws);   \
  T* wt_p = reinterpret_cast<T*>(wt);   \
  T* wd_p = reinterpret_cast<T*>(wd);   \
  for (int i = 0; i < lanes / 2; ++i) { \
    wd_p[2 * i] = wt_p[2 * i + 1];      \
    wd_p[2 * i + 1] = ws_p[2 * i + 1];  \
  }

#define VSHF_DF(T, lanes, mask)                        \
  T* ws_p = reinterpret_cast<T*>(ws);                  \
  T* wt_p = reinterpret_cast<T*>(wt);                  \
  T* wd_p = reinterpret_cast<T*>(wd);                  \
  const int mask_not_valid = 0xC0;                     \
  const int mask_6bits = 0x3F;                         \
  for (int i = 0; i < lanes; ++i) {                    \
    if ((wd_p[i] & mask_not_valid)) {                  \
      wd_p[i] = 0;                                     \
    } else {                                           \
      int k = (wd_p[i] & mask_6bits) % (lanes * 2);    \
      wd_p[i] = k > lanes ? ws_p[k - lanes] : wt_p[k]; \
    }                                                  \
  }

#define HADD_DF(T, T_small, lanes)                                           \
  T_small* ws_p = reinterpret_cast<T_small*>(ws);                            \
  T_small* wt_p = reinterpret_cast<T_small*>(wt);                            \
  T* wd_p = reinterpret_cast<T*>(wd);                                        \
  for (int i = 0; i < lanes; ++i) {                                          \
    wd_p[i] = static_cast<T>(ws_p[2 * i + 1]) + static_cast<T>(wt_p[2 * i]); \
  }

#define HSUB_DF(T, T_small, lanes)                                           \
  T_small* ws_p = reinterpret_cast<T_small*>(ws);                            \
  T_small* wt_p = reinterpret_cast<T_small*>(wt);                            \
  T* wd_p = reinterpret_cast<T*>(wd);                                        \
  for (int i = 0; i < lanes; ++i) {                                          \
    wd_p[i] = static_cast<T>(ws_p[2 * i + 1]) - static_cast<T>(wt_p[2 * i]); \
  }

#define TEST_CASE(V)                                              \
  V(sll_b, SLL_DF, uint8_t, kMSALanesByte, UINT8_MAX)             \
  V(sll_h, SLL_DF, uint16_t, kMSALanesHalf, UINT16_MAX)           \
  V(sll_w, SLL_DF, uint32_t, kMSALanesWord, UINT32_MAX)           \
  V(sll_d, SLL_DF, uint64_t, kMSALanesDword, UINT64_MAX)          \
  V(srl_b, SRL_DF, uint8_t, kMSALanesByte, UINT8_MAX)             \
  V(srl_h, SRL_DF, uint16_t, kMSALanesHalf, UINT16_MAX)           \
  V(srl_w, SRL_DF, uint32_t, kMSALanesWord, UINT32_MAX)           \
  V(srl_d, SRL_DF, uint64_t, kMSALanesDword, UINT64_MAX)          \
  V(bclr_b, BCRL_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(bclr_h, BCRL_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(bclr_w, BCRL_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(bclr_d, BCRL_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(bset_b, BSET_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(bset_h, BSET_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(bset_w, BSET_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(bset_d, BSET_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(bneg_b, BNEG_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(bneg_h, BNEG_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(bneg_w, BNEG_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(bneg_d, BNEG_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(binsl_b, BINSL_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(binsl_h, BINSL_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(binsl_w, BINSL_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(binsl_d, BINSL_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(binsr_b, BINSR_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(binsr_h, BINSR_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(binsr_w, BINSR_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(binsr_d, BINSR_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(addv_b, ADDV_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(addv_h, ADDV_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(addv_w, ADDV_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(addv_d, ADDV_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(subv_b, SUBV_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(subv_h, SUBV_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(subv_w, SUBV_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(subv_d, SUBV_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(max_s_b, MAX_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(max_s_h, MAX_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(max_s_w, MAX_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(max_s_d, MAX_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(max_u_b, MAX_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(max_u_h, MAX_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(max_u_w, MAX_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(max_u_d, MAX_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(min_s_b, MIN_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(min_s_h, MIN_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(min_s_w, MIN_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(min_s_d, MIN_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(min_u_b, MIN_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(min_u_h, MIN_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(min_u_w, MIN_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(min_u_d, MIN_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(max_a_b, MAXA_DF, int8_t, kMSALanesByte, UINT8_MAX)           \
  V(max_a_h, MAXA_DF, int16_t, kMSALanesHalf, UINT16_MAX)         \
  V(max_a_w, MAXA_DF, int32_t, kMSALanesWord, UINT32_MAX)         \
  V(max_a_d, MAXA_DF, int64_t, kMSALanesDword, UINT64_MAX)        \
  V(min_a_b, MINA_DF, int8_t, kMSALanesByte, UINT8_MAX)           \
  V(min_a_h, MINA_DF, int16_t, kMSALanesHalf, UINT16_MAX)         \
  V(min_a_w, MINA_DF, int32_t, kMSALanesWord, UINT32_MAX)         \
  V(min_a_d, MINA_DF, int64_t, kMSALanesDword, UINT64_MAX)        \
  V(ceq_b, CEQ_DF, uint8_t, kMSALanesByte, UINT8_MAX)             \
  V(ceq_h, CEQ_DF, uint16_t, kMSALanesHalf, UINT16_MAX)           \
  V(ceq_w, CEQ_DF, uint32_t, kMSALanesWord, UINT32_MAX)           \
  V(ceq_d, CEQ_DF, uint64_t, kMSALanesDword, UINT64_MAX)          \
  V(clt_s_b, CLT_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(clt_s_h, CLT_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(clt_s_w, CLT_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(clt_s_d, CLT_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(clt_u_b, CLT_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(clt_u_h, CLT_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(clt_u_w, CLT_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(clt_u_d, CLT_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(cle_s_b, CLE_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(cle_s_h, CLE_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(cle_s_w, CLE_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(cle_s_d, CLE_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(cle_u_b, CLE_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(cle_u_h, CLE_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(cle_u_w, CLE_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(cle_u_d, CLE_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(add_a_b, ADD_A_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(add_a_h, ADD_A_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(add_a_w, ADD_A_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(add_a_d, ADD_A_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(adds_a_b, ADDS_A_DF, int8_t, kMSALanesByte, UINT8_MAX)        \
  V(adds_a_h, ADDS_A_DF, int16_t, kMSALanesHalf, UINT16_MAX)      \
  V(adds_a_w, ADDS_A_DF, int32_t, kMSALanesWord, UINT32_MAX)      \
  V(adds_a_d, ADDS_A_DF, int64_t, kMSALanesDword, UINT64_MAX)     \
  V(adds_s_b, ADDS_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(adds_s_h, ADDS_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(adds_s_w, ADDS_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(adds_s_d, ADDS_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(adds_u_b, ADDS_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(adds_u_h, ADDS_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(adds_u_w, ADDS_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(adds_u_d, ADDS_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(ave_s_b, AVE_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(ave_s_h, AVE_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(ave_s_w, AVE_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(ave_s_d, AVE_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(ave_u_b, AVE_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(ave_u_h, AVE_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(ave_u_w, AVE_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(ave_u_d, AVE_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(aver_s_b, AVER_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(aver_s_h, AVER_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(aver_s_w, AVER_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(aver_s_d, AVER_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(aver_u_b, AVER_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(aver_u_h, AVER_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(aver_u_w, AVER_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(aver_u_d, AVER_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(subs_s_b, SUBS_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(subs_s_h, SUBS_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(subs_s_w, SUBS_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(subs_s_d, SUBS_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(subs_u_b, SUBS_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(subs_u_h, SUBS_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(subs_u_w, SUBS_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(subs_u_d, SUBS_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(subsus_u_b, SUBSUS_U_DF, int8_t, kMSALanesByte, UINT8_MAX)    \
  V(subsus_u_h, SUBSUS_U_DF, int16_t, kMSALanesHalf, UINT16_MAX)  \
  V(subsus_u_w, SUBSUS_U_DF, int32_t, kMSALanesWord, UINT32_MAX)  \
  V(subsus_u_d, SUBSUS_U_DF, int64_t, kMSALanesDword, UINT64_MAX) \
  V(subsuu_s_b, SUBSUU_S_DF, int8_t, kMSALanesByte, UINT8_MAX)    \
  V(subsuu_s_h, SUBSUU_S_DF, int16_t, kMSALanesHalf, UINT16_MAX)  \
  V(subsuu_s_w, SUBSUU_S_DF, int32_t, kMSALanesWord, UINT32_MAX)  \
  V(subsuu_s_d, SUBSUU_S_DF, int64_t, kMSALanesDword, UINT64_MAX) \
  V(asub_s_b, ASUB_S_DF, int8_t, kMSALanesByte, UINT8_MAX)        \
  V(asub_s_h, ASUB_S_DF, int16_t, kMSALanesHalf, UINT16_MAX)      \
  V(asub_s_w, ASUB_S_DF, int32_t, kMSALanesWord, UINT32_MAX)      \
  V(asub_s_d, ASUB_S_DF, int64_t, kMSALanesDword, UINT64_MAX)     \
  V(asub_u_b, ASUB_U_DF, uint8_t, kMSALanesByte, UINT8_MAX)       \
  V(asub_u_h, ASUB_U_DF, uint16_t, kMSALanesHalf, UINT16_MAX)     \
  V(asub_u_w, ASUB_U_DF, uint32_t, kMSALanesWord, UINT32_MAX)     \
  V(asub_u_d, ASUB_U_DF, uint64_t, kMSALanesDword, UINT64_MAX)    \
  V(mulv_b, MULV_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(mulv_h, MULV_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(mulv_w, MULV_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(mulv_d, MULV_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(maddv_b, MADDV_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(maddv_h, MADDV_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(maddv_w, MADDV_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(maddv_d, MADDV_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(msubv_b, MSUBV_DF, int8_t, kMSALanesByte, UINT8_MAX)          \
  V(msubv_h, MSUBV_DF, int16_t, kMSALanesHalf, UINT16_MAX)        \
  V(msubv_w, MSUBV_DF, int32_t, kMSALanesWord, UINT32_MAX)        \
  V(msubv_d, MSUBV_DF, int64_t, kMSALanesDword, UINT64_MAX)       \
  V(div_s_b, DIV_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(div_s_h, DIV_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(div_s_w, DIV_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(div_s_d, DIV_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(div_u_b, DIV_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(div_u_h, DIV_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(div_u_w, DIV_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(div_u_d, DIV_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(mod_s_b, MOD_DF, int8_t, kMSALanesByte, UINT8_MAX)            \
  V(mod_s_h, MOD_DF, int16_t, kMSALanesHalf, UINT16_MAX)          \
  V(mod_s_w, MOD_DF, int32_t, kMSALanesWord, UINT32_MAX)          \
  V(mod_s_d, MOD_DF, int64_t, kMSALanesDword, UINT64_MAX)         \
  V(mod_u_b, MOD_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(mod_u_h, MOD_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(mod_u_w, MOD_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(mod_u_d, MOD_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(srlr_b, SRAR_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(srlr_h, SRAR_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(srlr_w, SRAR_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(srlr_d, SRAR_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(pckev_b, PCKEV_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(pckev_h, PCKEV_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(pckev_w, PCKEV_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(pckev_d, PCKEV_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(pckod_b, PCKOD_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(pckod_h, PCKOD_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(pckod_w, PCKOD_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(pckod_d, PCKOD_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(ilvl_b, ILVL_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(ilvl_h, ILVL_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(ilvl_w, ILVL_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(ilvl_d, ILVL_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(ilvr_b, ILVR_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(ilvr_h, ILVR_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(ilvr_w, ILVR_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(ilvr_d, ILVR_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(ilvev_b, ILVEV_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(ilvev_h, ILVEV_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(ilvev_w, ILVEV_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(ilvev_d, ILVEV_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(ilvod_b, ILVOD_DF, uint8_t, kMSALanesByte, UINT8_MAX)         \
  V(ilvod_h, ILVOD_DF, uint16_t, kMSALanesHalf, UINT16_MAX)       \
  V(ilvod_w, ILVOD_DF, uint32_t, kMSALanesWord, UINT32_MAX)       \
  V(ilvod_d, ILVOD_DF, uint64_t, kMSALanesDword, UINT64_MAX)      \
  V(vshf_b, VSHF_DF, uint8_t, kMSALanesByte, UINT8_MAX)           \
  V(vshf_h, VSHF_DF, uint16_t, kMSALanesHalf, UINT16_MAX)         \
  V(vshf_w, VSHF_DF, uint32_t, kMSALanesWord, UINT32_MAX)         \
  V(vshf_d, VSHF_DF, uint64_t, kMSALanesDword, UINT64_MAX)        \
  V(hadd_s_h, HADD_DF, int16_t, int8_t, kMSALanesHalf)            \
  V(hadd_s_w, HADD_DF, int32_t, int16_t, kMSALanesWord)           \
  V(hadd_s_d, HADD_DF, int64_t, int32_t, kMSALanesDword)          \
  V(hadd_u_h, HADD_DF, uint16_t, uint8_t, kMSALanesHalf)          \
  V(hadd_u_w, HADD_DF, uint32_t, uint16_t, kMSALanesWord)         \
  V(hadd_u_d, HADD_DF, uint64_t, uint32_t, kMSALanesDword)        \
  V(hsub_s_h, HSUB_DF, int16_t, int8_t, kMSALanesHalf)            \
  V(hsub_s_w, HSUB_DF, int32_t, int16_t, kMSALanesWord)           \
  V(hsub_s_d, HSUB_DF, int64_t, int32_t, kMSALanesDword)          \
  V(hsub_u_h, HSUB_DF, uint16_t, uint8_t, kMSALanesHalf)          \
  V(hsub_u_w, HSUB_DF, uint32_t, uint16_t, kMSALanesWord)         \
  V(hsub_u_d, HSUB_DF, uint64_t, uint32_t, kMSALanesDword)

#define RUN_TEST(instr, verify, type, lanes, mask)                       \
  run_msa_3r(&tc[i], [](MacroAssembler& assm) { __ instr(w2, w1, w0); }, \
             [](uint64_t* ws, uint64_t* wt, uint64_t* wd) {              \
               verify(type, lanes, mask);                                \
             });

  for (size_t i = 0; i < arraysize(tc); ++i) {
    TEST_CASE(RUN_TEST)
  }

#define RUN_TEST2(instr, verify, type, lanes, mask)                      \
  for (unsigned i = 0; i < arraysize(tc); i++) {                         \
    for (unsigned j = 0; j < 3; j++) {                                   \
      for (unsigned k = 0; k < lanes; k++) {                             \
        type* element = reinterpret_cast<type*>(&tc[i]);                 \
        element[k + j * lanes] &= std::numeric_limits<type>::max();      \
      }                                                                  \
    }                                                                    \
  }                                                                      \
  run_msa_3r(&tc[i], [](MacroAssembler& assm) { __ instr(w2, w1, w0); }, \
             [](uint64_t* ws, uint64_t* wt, uint64_t* wd) {              \
               verify(type, lanes, mask);                                \
             });

#define TEST_CASE2(V)                                    \
  V(sra_b, SRA_DF, int8_t, kMSALanesByte, UINT8_MAX)     \
  V(sra_h, SRA_DF, int16_t, kMSALanesHalf, UINT16_MAX)   \
  V(sra_w, SRA_DF, int32_t, kMSALanesWord, UINT32_MAX)   \
  V(sra_d, SRA_DF, int64_t, kMSALanesDword, UINT64_MAX)  \
  V(srar_b, SRAR_DF, int8_t, kMSALanesByte, UINT8_MAX)   \
  V(srar_h, SRAR_DF, int16_t, kMSALanesHalf, UINT16_MAX) \
  V(srar_w, SRAR_DF, int32_t, kMSALanesWord, UINT32_MAX) \
  V(srar_d, SRAR_DF, int64_t, kMSALanesDword, UINT64_MAX)

  for (size_t i = 0; i < arraysize(tc); ++i) {
    TEST_CASE2(RUN_TEST2)
  }

#undef TEST_CASE
#undef TEST_CASE2
#undef RUN_TEST
#undef RUN_TEST2
#undef SLL_DF
#undef SRL_DF
#undef SRA_DF
#undef BCRL_DF
#undef BSET_DF
#undef BNEG_DF
#undef BINSL_DF
#undef BINSR_DF
#undef ADDV_DF
#undef SUBV_DF
#undef MAX_DF
#undef MIN_DF
#undef MAXA_DF
#undef MINA_DF
#undef CEQ_DF
#undef CLT_DF
#undef CLE_DF
#undef ADD_A_DF
#undef ADDS_A_DF
#undef ADDS_DF
#undef AVE_DF
#undef AVER_DF
#undef SUBS_DF
#undef SUBSUS_U_DF
#undef SUBSUU_S_DF
#undef ASUB_S_DF
#undef ASUB_U_DF
#undef MULV_DF
#undef MADDV_DF
#undef MSUBV_DF
#undef DIV_DF
#undef MOD_DF
#undef SRAR_DF
#undef PCKEV_DF
#undef PCKOD_DF
#undef ILVL_DF
#undef ILVR_DF
#undef ILVEV_DF
#undef ILVOD_DF
#undef VSHF_DF
#undef HADD_DF
#undef HSUB_DF
}

struct TestCaseMsa3RF {
  uint64_t ws_lo;
  uint64_t ws_hi;
  uint64_t wt_lo;
  uint64_t wt_hi;
  uint64_t wd_lo;
  uint64_t wd_hi;
};

struct ExpectedResult_MSA3RF {
  uint64_t exp_res_lo;
  uint64_t exp_res_hi;
};

template <typename Func>
void run_msa_3rf(const struct TestCaseMsa3RF* input,
                 const struct ExpectedResult_MSA3RF* output,
                 Func Generate2RInstructionFunc) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, v8::internal::CodeObjectRequired::kYes);
  CpuFeatureScope fscope(&assm, MIPS_SIMD);
  msa_reg_t res;

  load_elements_of_vector(
      &assm, reinterpret_cast<const uint64_t*>(&input->ws_lo), w0, t0, t1);
  load_elements_of_vector(
      &assm, reinterpret_cast<const uint64_t*>(&input->wt_lo), w1, t0, t1);
  load_elements_of_vector(
      &assm, reinterpret_cast<const uint64_t*>(&input->wd_lo), w2, t0, t1);
  Generate2RInstructionFunc(assm);
  store_elements_of_vector(&assm, w2, a0);

  __ jr(ra);
  __ nop();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code);
#endif
  auto f = GeneratedCode<F3>::FromCode(isolate, *code);

  f.Call(&res, 0, 0, 0, 0);

  CHECK_EQ(output->exp_res_lo, res.d[0]);
  CHECK_EQ(output->exp_res_hi, res.d[1]);
}

struct TestCaseMsa3RF_F {
  float ws_1, ws_2, ws_3, ws_4;
  float wt_1, wt_2, wt_3, wt_4;
  float wd_1, wd_2, wd_3, wd_4;
};
struct ExpRes_32I {
  int32_t exp_res_1;
  int32_t exp_res_2;
  int32_t exp_res_3;
  int32_t exp_res_4;
};

struct TestCaseMsa3RF_D {
  double ws_lo, ws_hi;
  double wt_lo, wt_hi;
  double wd_lo, wd_hi;
};
struct ExpRes_64I {
  int64_t exp_res_lo;
  int64_t exp_res_hi;
};

TEST(MSA_floating_point_quiet_compare) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float qnan_f = std::numeric_limits<float>::quiet_NaN();
  const double qnan_d = std::numeric_limits<double>::quiet_NaN();
  const float inf_f = std::numeric_limits<float>::infinity();
  const double inf_d = std::numeric_limits<double>::infinity();
  const int32_t ones = -1;

  const struct TestCaseMsa3RF_F tc_w[]{
      {qnan_f, -qnan_f, inf_f, 2.14e9f,  // ws
       qnan_f, 0.f, qnan_f, -2.14e9f,    // wt
       0, 0, 0, 0},                      // wd
      {inf_f, -inf_f, -3.4e38f, 1.5e-45f, -inf_f, -inf_f, -inf_f, inf_f, 0, 0,
       0, 0},
      {0.f, 19.871e24f, -1.5e-45f, -1.5e-45f, -19.871e24f, 19.871e24f, 1.5e-45f,
       -1.5e-45f, 0, 0, 0, 0}};

  const struct TestCaseMsa3RF_D tc_d[]{
      // ws_lo, ws_hi, wt_lo, wt_hi, wd_lo, wd_hi
      {qnan_d, -qnan_d, qnan_f, 0., 0, 0},
      {inf_d, 9.22e18, qnan_d, -9.22e18, 0, 0},
      {inf_d, inf_d, -inf_d, inf_d, 0, 0},
      {-2.3e-308, 5e-324, -inf_d, inf_d, 0, 0},
      {0., 24.1e87, -1.6e308, 24.1e87, 0, 0},
      {-5e-324, -5e-324, 5e-324, -5e-324, 0, 0}};

  const struct ExpectedResult_MSA3RF exp_res_fcaf = {0, 0};
  const struct ExpRes_32I exp_res_fcun_w[] = {
      {ones, ones, ones, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
  const struct ExpRes_64I exp_res_fcun_d[] = {{ones, ones}, {ones, 0}, {0, 0},
                                              {0, 0},       {0, 0},    {0, 0}};
  const struct ExpRes_32I exp_res_fceq_w[] = {
      {0, 0, 0, 0}, {0, ones, 0, 0}, {0, ones, 0, ones}};
  const struct ExpRes_64I exp_res_fceq_d[] = {{0, 0}, {0, 0},    {0, ones},
                                              {0, 0}, {0, ones}, {0, ones}};
  const struct ExpRes_32I exp_res_fcueq_w[] = {
      {ones, ones, ones, 0}, {0, ones, 0, 0}, {0, ones, 0, ones}};
  const struct ExpRes_64I exp_res_fcueq_d[] = {
      {ones, ones}, {ones, 0}, {0, ones}, {0, 0}, {0, ones}, {0, ones}};
  const struct ExpRes_32I exp_res_fclt_w[] = {
      {0, 0, 0, 0}, {0, 0, 0, ones}, {0, 0, ones, 0}};
  const struct ExpRes_64I exp_res_fclt_d[] = {{0, 0},    {0, 0}, {0, 0},
                                              {0, ones}, {0, 0}, {ones, 0}};
  const struct ExpRes_32I exp_res_fcult_w[] = {
      {ones, ones, ones, 0}, {0, 0, 0, ones}, {0, 0, ones, 0}};
  const struct ExpRes_64I exp_res_fcult_d[] = {
      {ones, ones}, {ones, 0}, {0, 0}, {0, ones}, {0, 0}, {ones, 0}};
  const struct ExpRes_32I exp_res_fcle_w[] = {
      {0, 0, 0, 0}, {0, ones, 0, ones}, {0, ones, ones, ones}};
  const struct ExpRes_64I exp_res_fcle_d[] = {
      {0, 0}, {0, 0}, {0, ones}, {0, ones}, {0, ones}, {ones, ones}};
  const struct ExpRes_32I exp_res_fcule_w[] = {
      {ones, ones, ones, 0}, {0, ones, 0, ones}, {0, ones, ones, ones}};
  const struct ExpRes_64I exp_res_fcule_d[] = {
      {ones, ones}, {ones, 0}, {0, ones}, {0, ones}, {0, ones}, {ones, ones}};
  const struct ExpRes_32I exp_res_fcor_w[] = {
      {0, 0, 0, ones}, {ones, ones, ones, ones}, {ones, ones, ones, ones}};
  const struct ExpRes_64I exp_res_fcor_d[] = {{0, 0},       {0, ones},
                                              {ones, ones}, {ones, ones},
                                              {ones, ones}, {ones, ones}};
  const struct ExpRes_32I exp_res_fcune_w[] = {
      {ones, ones, ones, ones}, {ones, 0, ones, ones}, {ones, 0, ones, 0}};
  const struct ExpRes_64I exp_res_fcune_d[] = {{ones, ones}, {ones, ones},
                                               {ones, 0},    {ones, ones},
                                               {ones, 0},    {ones, 0}};
  const struct ExpRes_32I exp_res_fcne_w[] = {
      {0, 0, 0, ones}, {ones, 0, ones, ones}, {ones, 0, ones, 0}};
  const struct ExpRes_64I exp_res_fcne_d[] = {
      {0, 0}, {0, ones}, {ones, 0}, {ones, ones}, {ones, 0}, {ones, 0}};

#define TEST_FP_QUIET_COMPARE_W(instruction, src, exp_res)                    \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

#define TEST_FP_QUIET_COMPARE_D(instruction, src, exp_res)                    \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    TEST_FP_QUIET_COMPARE_W(fcaf_w, &tc_w[i], &exp_res_fcaf)
    TEST_FP_QUIET_COMPARE_W(fcun_w, &tc_w[i], &exp_res_fcun_w[i])
    TEST_FP_QUIET_COMPARE_W(fceq_w, &tc_w[i], &exp_res_fceq_w[i])
    TEST_FP_QUIET_COMPARE_W(fcueq_w, &tc_w[i], &exp_res_fcueq_w[i])
    TEST_FP_QUIET_COMPARE_W(fclt_w, &tc_w[i], &exp_res_fclt_w[i])
    TEST_FP_QUIET_COMPARE_W(fcult_w, &tc_w[i], &exp_res_fcult_w[i])
    TEST_FP_QUIET_COMPARE_W(fcle_w, &tc_w[i], &exp_res_fcle_w[i])
    TEST_FP_QUIET_COMPARE_W(fcule_w, &tc_w[i], &exp_res_fcule_w[i])
    TEST_FP_QUIET_COMPARE_W(fcor_w, &tc_w[i], &exp_res_fcor_w[i])
    TEST_FP_QUIET_COMPARE_W(fcune_w, &tc_w[i], &exp_res_fcune_w[i])
    TEST_FP_QUIET_COMPARE_W(fcne_w, &tc_w[i], &exp_res_fcne_w[i])
  }
  for (uint64_t i = 0; i < arraysize(tc_d); i++) {
    TEST_FP_QUIET_COMPARE_D(fcaf_d, &tc_d[i], &exp_res_fcaf)
    TEST_FP_QUIET_COMPARE_D(fcun_d, &tc_d[i], &exp_res_fcun_d[i])
    TEST_FP_QUIET_COMPARE_D(fceq_d, &tc_d[i], &exp_res_fceq_d[i])
    TEST_FP_QUIET_COMPARE_D(fcueq_d, &tc_d[i], &exp_res_fcueq_d[i])
    TEST_FP_QUIET_COMPARE_D(fclt_d, &tc_d[i], &exp_res_fclt_d[i])
    TEST_FP_QUIET_COMPARE_D(fcult_d, &tc_d[i], &exp_res_fcult_d[i])
    TEST_FP_QUIET_COMPARE_D(fcle_d, &tc_d[i], &exp_res_fcle_d[i])
    TEST_FP_QUIET_COMPARE_D(fcule_d, &tc_d[i], &exp_res_fcule_d[i])
    TEST_FP_QUIET_COMPARE_D(fcor_d, &tc_d[i], &exp_res_fcor_d[i])
    TEST_FP_QUIET_COMPARE_D(fcune_d, &tc_d[i], &exp_res_fcune_d[i])
    TEST_FP_QUIET_COMPARE_D(fcne_d, &tc_d[i], &exp_res_fcne_d[i])
  }
#undef TEST_FP_QUIET_COMPARE_W
#undef TEST_FP_QUIET_COMPARE_D
}

template <typename T>
inline const T* fadd_function(const T* src1, const T* src2, const T* src3,
                              T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = src1[i] + src2[i];
  }
  return dst;
}
template <typename T>
inline const T* fsub_function(const T* src1, const T* src2, const T* src3,
                              T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = src1[i] - src2[i];
  }
  return dst;
}
template <typename T>
inline const T* fmul_function(const T* src1, const T* src2, const T* src3,
                              T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = src1[i] * src2[i];
  }
  return dst;
}
template <typename T>
inline const T* fdiv_function(const T* src1, const T* src2, const T* src3,
                              T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = src1[i] / src2[i];
  }
  return dst;
}
template <typename T>
inline const T* fmadd_function(const T* src1, const T* src2, const T* src3,
                               T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = std::fma(src1[i], src2[i], src3[i]);
  }
  return dst;
}
template <typename T>
inline const T* fmsub_function(const T* src1, const T* src2, const T* src3,
                               T* dst) {
  for (uint64_t i = 0; i < kMSALanesByte / sizeof(T); i++) {
    dst[i] = std::fma(src1[i], -src2[i], src3[i]);
  }
  return dst;
}

TEST(MSA_floating_point_arithmetic) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_f = std::numeric_limits<float>::infinity();
  const double inf_d = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa3RF_F tc_w[] = {
      {0.3, -2.14e13f, inf_f, 0.f,                     // ws
       -inf_f, std::sqrt(8.e-26f), -23.e34, -2.14e9f,  // wt
       -1e30f, 4.6e12f, 0, 2.14e9f},                   // wd
      {3.4e38f, -1.2e-38f, 1e19f, -1e19f, 3.4e38f, 1.2e-38f, -1e19f, -1e-19f,
       3.4e38f, 1.2e-38f * 3, 3.4e38f, -4e19f},
      {-3e-31f, 3e10f, 1e25f, 123.f, 1e-14f, 1e-34f, 4e25f, 321.f, 3e-17f,
       2e-24f, 2.f, -123456.f}};

  const struct TestCaseMsa3RF_D tc_d[] = {
      // ws_lo, ws_hi, wt_lo, wt_hi, wd_lo, wd_hi
      {0.3, -2.14e103, -inf_d, std::sqrt(8.e-206), -1e30, 4.6e102},
      {inf_d, 0., -23.e304, -2.104e9, 0, 2.104e9},
      {3.4e307, -1.2e-307, 3.4e307, 1.2e-307, 3.4e307, 1.2e-307 * 3},
      {1e154, -1e154, -1e154, -1e-154, 2.9e38, -4e19},
      {-3e-301, 3e100, 1e-104, 1e-304, 3e-107, 2e-204},
      {1e205, 123., 4e205, 321., 2., -123456.}};

  struct ExpectedResult_MSA3RF dst_container;

#define FP_ARITHMETIC_DF_W(instr, function, src1, src2, src3)           \
  run_msa_3rf(                                                          \
      reinterpret_cast<const struct TestCaseMsa3RF*>(src1),             \
      reinterpret_cast<const struct ExpectedResult_MSA3RF*>(function(   \
          src1, src2, src3, reinterpret_cast<float*>(&dst_container))), \
      [](MacroAssembler& assm) { __ instr(w2, w0, w1); });

#define FP_ARITHMETIC_DF_D(instr, function, src1, src2, src3)            \
  run_msa_3rf(                                                           \
      reinterpret_cast<const struct TestCaseMsa3RF*>(src1),              \
      reinterpret_cast<const struct ExpectedResult_MSA3RF*>(function(    \
          src1, src2, src3, reinterpret_cast<double*>(&dst_container))), \
      [](MacroAssembler& assm) { __ instr(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    FP_ARITHMETIC_DF_W(fadd_w, fadd_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
    FP_ARITHMETIC_DF_W(fsub_w, fsub_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
    FP_ARITHMETIC_DF_W(fmul_w, fmul_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
    FP_ARITHMETIC_DF_W(fdiv_w, fdiv_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
    FP_ARITHMETIC_DF_W(fmadd_w, fmadd_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
    FP_ARITHMETIC_DF_W(fmsub_w, fmsub_function, &tc_w[i].ws_1, &tc_w[i].wt_1,
                       &tc_w[i].wd_1)
  }
  for (uint64_t i = 0; i < arraysize(tc_d); i++) {
    FP_ARITHMETIC_DF_D(fadd_d, fadd_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
    FP_ARITHMETIC_DF_D(fsub_d, fsub_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
    FP_ARITHMETIC_DF_D(fmul_d, fmul_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
    FP_ARITHMETIC_DF_D(fdiv_d, fdiv_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
    FP_ARITHMETIC_DF_D(fmadd_d, fmadd_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
    FP_ARITHMETIC_DF_D(fmsub_d, fmsub_function, &tc_d[i].ws_lo, &tc_d[i].wt_lo,
                       &tc_d[i].wd_lo)
  }
#undef FP_ARITHMETIC_DF_W
#undef FP_ARITHMETIC_DF_D
}

struct ExpRes_F {
  float exp_res_1;
  float exp_res_2;
  float exp_res_3;
  float exp_res_4;
};

struct ExpRes_D {
  double exp_res_1;
  double exp_res_2;
};

TEST(MSA_fmin_fmin_a_fmax_fmax_a) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_f = std::numeric_limits<float>::infinity();
  const double inf_d = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa3RF_F tc_w[] = {
      {0.3f, -2.14e13f, inf_f, -0.f,                    // ws
       -inf_f, -std::sqrt(8.e26f), -23.e34f, -2.14e9f,  // wt
       0, 0, 0, 0},                                     // wd
      {3.4e38f, 1.2e-41f, 1e19f, 1e19f,                 // ws
       3.4e38f, -1.1e-41f, -1e-42f, -1e29f,             // wt
       0, 0, 0, 0}};                                    // wd

  const struct TestCaseMsa3RF_D tc_d[] = {
      // ws_lo, ws_hi, wt_lo, wt_hi, wd_lo, wd_hi
      {0.3, -2.14e103, -inf_d, -std::sqrt(8e206), 0, 0},
      {inf_d, -0., -23e304, -2.14e90, 0, 0},
      {3.4e307, 1.2e-320, 3.4e307, -1.1e-320, 0, 0},
      {1e154, 1e154, -1e-321, -1e174, 0, 0}};

  const struct ExpRes_F exp_res_fmax_w[] = {{0.3f, -2.14e13f, inf_f, -0.f},
                                            {3.4e38f, 1.2e-41f, 1e19f, 1e19f}};
  const struct ExpRes_F exp_res_fmax_a_w[] = {
      {-inf_f, -std::sqrt(8e26f), inf_f, -2.14e9f},
      {3.4e38f, 1.2e-41f, 1e19f, -1e29f}};
  const struct ExpRes_F exp_res_fmin_w[] = {
      {-inf_f, -std::sqrt(8.e26f), -23e34f, -2.14e9f},
      {3.4e38f, -1.1e-41f, -1e-42f, -1e29f}};
  const struct ExpRes_F exp_res_fmin_a_w[] = {
      {0.3, -2.14e13f, -23.e34f, -0.f}, {3.4e38f, -1.1e-41f, -1e-42f, 1e19f}};

  const struct ExpRes_D exp_res_fmax_d[] = {
      {0.3, -2.14e103}, {inf_d, -0.}, {3.4e307, 1.2e-320}, {1e154, 1e154}};
  const struct ExpRes_D exp_res_fmax_a_d[] = {{-inf_d, -std::sqrt(8e206)},
                                              {inf_d, -2.14e90},
                                              {3.4e307, 1.2e-320},
                                              {1e154, -1e174}};
  const struct ExpRes_D exp_res_fmin_d[] = {{-inf_d, -std::sqrt(8e206)},
                                            {-23e304, -2.14e90},
                                            {3.4e307, -1.1e-320},
                                            {-1e-321, -1e174}};
  const struct ExpRes_D exp_res_fmin_a_d[] = {
      {0.3, -2.14e103}, {-23e304, -0.}, {3.4e307, -1.1e-320}, {-1e-321, 1e154}};

#define TEST_FP_MIN_MAX_W(instruction, src, exp_res)                          \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

#define TEST_FP_MIN_MAX_D(instruction, src, exp_res)                          \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    TEST_FP_MIN_MAX_W(fmax_w, &tc_w[i], &exp_res_fmax_w[i])
    TEST_FP_MIN_MAX_W(fmax_a_w, &tc_w[i], &exp_res_fmax_a_w[i])
    TEST_FP_MIN_MAX_W(fmin_w, &tc_w[i], &exp_res_fmin_w[i])
    TEST_FP_MIN_MAX_W(fmin_a_w, &tc_w[i], &exp_res_fmin_a_w[i])
  }

  for (uint64_t i = 0; i < arraysize(tc_d); i++) {
    TEST_FP_MIN_MAX_D(fmax_d, &tc_d[i], &exp_res_fmax_d[i])
    TEST_FP_MIN_MAX_D(fmax_a_d, &tc_d[i], &exp_res_fmax_a_d[i])
    TEST_FP_MIN_MAX_D(fmin_d, &tc_d[i], &exp_res_fmin_d[i])
    TEST_FP_MIN_MAX_D(fmin_a_d, &tc_d[i], &exp_res_fmin_a_d[i])
  }
#undef TEST_FP_MIN_MAX_W
#undef TEST_FP_MIN_MAX_D
}

struct TestCaseMsa3RF_16I {
  int16_t ws_1, ws_2, ws_3, ws_4, ws_5, ws_6, ws_7, ws_8;
  int16_t wt_1, wt_2, wt_3, wt_4, wt_5, wt_6, wt_7, wt_8;
  int16_t wd_1, wd_2, wd_3, wd_4, wd_5, wd_6, wd_7, wd_8;
};
struct ExpRes_16I {
  int16_t exp_res_1;
  int16_t exp_res_2;
  int16_t exp_res_3;
  int16_t exp_res_4;
  int16_t exp_res_5;
  int16_t exp_res_6;
  int16_t exp_res_7;
  int16_t exp_res_8;
};

struct TestCaseMsa3RF_32I {
  int32_t ws_1, ws_2, ws_3, ws_4;
  int32_t wt_1, wt_2, wt_3, wt_4;
  int32_t wd_1, wd_2, wd_3, wd_4;
};

TEST(MSA_fixed_point_arithmetic) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const struct TestCaseMsa3RF tc_h[]{
      {0x800080007FFF7FFF, 0xE1ED8000FAD3863A, 0x80007FFF00AF7FFF,
       0x800015A77FFFA0EB, 0x7FFF800080007FFF, 0x80007FFF1F207364},
      {0x800080007FFF006A, 0x002AFFC4329AD87B, 0x80007FFF7FFF00F3,
       0xFFECFFB4D0D7F429, 0x80007FFF80007C33, 0x54AC6BBCE53B8C91}};

  const struct TestCaseMsa3RF tc_w[]{
      {0x8000000080000000, 0x7FFFFFFF7FFFFFFF, 0x800000007FFFFFFF,
       0x00001FF37FFFFFFF, 0x7FFFFFFF80000000, 0x800000007FFFFFFF},
      {0xE1ED035580000000, 0xFAD3863AED462C0B, 0x8000000015A70AEC,
       0x7FFFFFFFA0EBD354, 0x800000007FFFFFFF, 0xD0D7F4291F207364},
      {0x8000000080000000, 0x7FFFFFFF0000DA1F, 0x800000007FFFFFFF,
       0x7FFFFFFF00F39C3B, 0x800000007FFFFFFF, 0x800000007C33F2FD},
      {0x0000AC33FFFF329A, 0x54AC6BBCE53BD87B, 0xFFFFE2B4D0D7F429,
       0x0355ED462C0B1FF3, 0xB5DEB625939DD3F9, 0xE642ADFA69519596}};

  const struct ExpectedResult_MSA3RF exp_res_mul_q_h[] = {
      {0x7FFF800100AE7FFE, 0x1E13EA59FAD35A74},
      {0x7FFF80017FFE0000, 0xFFFF0000ED5B03A7}};
  const struct ExpectedResult_MSA3RF exp_res_madd_q_h[] = {
      {0x7FFF800080AE7FFF, 0x9E136A5819F37FFF},
      {0x00000000FFFE7C33, 0x54AB6BBCD2969038}};
  const struct ExpectedResult_MSA3RF exp_res_msub_q_h[] = {
      {0xFFFFFFFF80000000, 0x80007FFF244C18EF},
      {0x80007FFF80007C32, 0x54AC6BBBF7DF88E9}};
  const struct ExpectedResult_MSA3RF exp_res_mulr_q_h[] = {
      {0x7FFF800100AF7FFE, 0x1E13EA59FAD35A75},
      {0x7FFF80017FFE0001, 0x00000000ED5B03A8}};
  const struct ExpectedResult_MSA3RF exp_res_maddr_q_h[] = {
      {0x7FFF800080AF7FFF, 0x9E136A5819F37FFF},
      {0x00000000FFFE7C34, 0x54AC6BBCD2969039}};
  const struct ExpectedResult_MSA3RF exp_res_msubr_q_h[] = {
      {0xFFFFFFFF80000001, 0x80007FFF244D18EF},
      {0x80007FFF80007C32, 0x54AC6BBCF7E088E9}};

  const struct ExpectedResult_MSA3RF exp_res_mul_q_w[] = {
      {0x7FFFFFFF80000001, 0x00001FF27FFFFFFE},
      {0x1E12FCABEA58F514, 0xFAD3863A0DE8DEE1},
      {0x7FFFFFFF80000001, 0x7FFFFFFE0000019F},
      {0xFFFFFFFF00004BAB, 0x0234E1FBF6CA3EE0}};
  const struct ExpectedResult_MSA3RF exp_res_madd_q_w[] = {
      {0x7FFFFFFF80000000, 0x80001FF27FFFFFFF},
      {0x9E12FCAB6A58F513, 0xCBAB7A632D095245},
      {0x0000000000000000, 0xFFFFFFFE7C33F49C},
      {0xB5DEB624939E1FA4, 0xE8778FF5601BD476}};
  const struct ExpectedResult_MSA3RF exp_res_msub_q_w[] = {
      {0xFFFFFFFFFFFFFFFF, 0x8000000000000000},
      {0x800000007FFFFFFF, 0xD6046DEE11379482},
      {0x800000007FFFFFFF, 0x800000007C33F15D},
      {0xB5DEB625939D884D, 0xE40DCBFE728756B5}};
  const struct ExpectedResult_MSA3RF exp_res_mulr_q_w[] = {
      {0x7FFFFFFF80000001, 0x00001FF37FFFFFFE},
      {0x1E12FCABEA58F514, 0xFAD3863A0DE8DEE2},
      {0x7FFFFFFF80000001, 0x7FFFFFFE0000019F},
      {0x0000000000004BAC, 0x0234E1FCF6CA3EE1}};
  const struct ExpectedResult_MSA3RF exp_res_maddr_q_w[] = {
      {0x7FFFFFFF80000000, 0x80001FF37FFFFFFF},
      {0x9E12FCAB6A58F513, 0xCBAB7A632D095246},
      {0x0000000000000000, 0xFFFFFFFE7C33F49C},
      {0xB5DEB625939E1FA5, 0xE8778FF6601BD477}};
  const struct ExpectedResult_MSA3RF exp_res_msubr_q_w[] = {
      {0xFFFFFFFFFFFFFFFF, 0x8000000000000001},
      {0x800000007FFFFFFF, 0xD6046DEF11379482},
      {0x800000007FFFFFFF, 0x800000007C33F15E},
      {0xB5DEB625939D884D, 0xE40DCBFE728756B5}};

#define TEST_FIXED_POINT_DF_H(instruction, src, exp_res) \
  run_msa_3rf((src), (exp_res),                          \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

#define TEST_FIXED_POINT_DF_W(instruction, src, exp_res) \
  run_msa_3rf((src), (exp_res),                          \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_h); i++) {
    TEST_FIXED_POINT_DF_H(mul_q_h, &tc_h[i], &exp_res_mul_q_h[i])
    TEST_FIXED_POINT_DF_H(madd_q_h, &tc_h[i], &exp_res_madd_q_h[i])
    TEST_FIXED_POINT_DF_H(msub_q_h, &tc_h[i], &exp_res_msub_q_h[i])
    TEST_FIXED_POINT_DF_H(mulr_q_h, &tc_h[i], &exp_res_mulr_q_h[i])
    TEST_FIXED_POINT_DF_H(maddr_q_h, &tc_h[i], &exp_res_maddr_q_h[i])
    TEST_FIXED_POINT_DF_H(msubr_q_h, &tc_h[i], &exp_res_msubr_q_h[i])
  }

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    TEST_FIXED_POINT_DF_W(mul_q_w, &tc_w[i], &exp_res_mul_q_w[i])
    TEST_FIXED_POINT_DF_W(madd_q_w, &tc_w[i], &exp_res_madd_q_w[i])
    TEST_FIXED_POINT_DF_W(msub_q_w, &tc_w[i], &exp_res_msub_q_w[i])
    TEST_FIXED_POINT_DF_W(mulr_q_w, &tc_w[i], &exp_res_mulr_q_w[i])
    TEST_FIXED_POINT_DF_W(maddr_q_w, &tc_w[i], &exp_res_maddr_q_w[i])
    TEST_FIXED_POINT_DF_W(msubr_q_w, &tc_w[i], &exp_res_msubr_q_w[i])
  }
#undef TEST_FIXED_POINT_DF_H
#undef TEST_FIXED_POINT_DF_W
}

TEST(MSA_fexdo) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float inf_float = std::numeric_limits<float>::infinity();
  const float nan_float = std::numeric_limits<float>::quiet_NaN();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa3RF_F tc_w[] = {
      // ws_1, ws_2, ws_3, ws_4, wt_1, wt_2, wt_3, wt_4, wd_1, wd_2, wd_3, wd_4
      {inf_float, nan_float, 66505.f, 65504.f, 6.2e-5f, 5e-5f, -32.42f,
       -inf_float, 0, 0, 0, 0},
      {-0.f, 0.f, 123.567f, -765.321f, -6e-8f, 5.9e-8f, 1e-7f, -1e-20f, 0, 0, 0,
       0},
      {1e-36f, 1e20f, -1e20f, 2e-20f, 6e-8f, -2.9e-8f, -66505.f, -65504.f, 0, 0,
       0, 0}};

  const struct TestCaseMsa3RF_D tc_d[] = {
      // ws_lo, ws_hi, wt_lo, wt_hi, wd_lo, wd_hi
      {inf_double, -1234., 4e38, 3.4e38, 0, 0},
      {1.2e-38, 1.1e-39, -38.92f, -inf_double, 0, 0},
      {-0., 0., 123.567e31, -765.321e33, 0, 0},
      {-1.5e-45, 1.3e-45, 1e-42, -1e-200, 0, 0},
      {1e-202, 1e158, -1e159, 1e14, 0, 0},
      {1.5e-42, 1.3e-46, -123.567e31, 765.321e33, 0, 0}};

  const struct ExpRes_16I exp_res_fexdo_w[] = {
      {static_cast<int16_t>(0x0410), static_cast<int16_t>(0x0347),
       static_cast<int16_t>(0xD00D), static_cast<int16_t>(0xFC00),
       static_cast<int16_t>(0x7C00), static_cast<int16_t>(0x7DFF),
       static_cast<int16_t>(0x7C00), static_cast<int16_t>(0x7BFF)},
      {static_cast<int16_t>(0x8001), static_cast<int16_t>(0x0001),
       static_cast<int16_t>(0x0002), static_cast<int16_t>(0x8000),
       static_cast<int16_t>(0x8000), static_cast<int16_t>(0x0000),
       static_cast<int16_t>(0x57B9), static_cast<int16_t>(0xE1FB)},
      {static_cast<int16_t>(0x0001), static_cast<int16_t>(0x8000),
       static_cast<int16_t>(0xFC00), static_cast<int16_t>(0xFBFF),
       static_cast<int16_t>(0x0000), static_cast<int16_t>(0x7C00),
       static_cast<int16_t>(0xFC00), static_cast<int16_t>(0x0000)}};

  const struct ExpRes_32I exp_res_fexdo_d[] = {
      {base::bit_cast<int32_t>(0x7F800000), base::bit_cast<int32_t>(0x7F7FC99E),
       base::bit_cast<int32_t>(0x7F800000),
       base::bit_cast<int32_t>(0xC49A4000)},
      {base::bit_cast<int32_t>(0xC21BAE14), base::bit_cast<int32_t>(0xFF800000),
       base::bit_cast<int32_t>(0x0082AB1E),
       base::bit_cast<int32_t>(0x000BFA5A)},
      {base::bit_cast<int32_t>(0x7673B164), base::bit_cast<int32_t>(0xFB13653D),
       base::bit_cast<int32_t>(0x80000000),
       base::bit_cast<int32_t>(0x00000000)},
      {base::bit_cast<int32_t>(0x000002CA), base::bit_cast<int32_t>(0x80000000),
       base::bit_cast<int32_t>(0x80000001),
       base::bit_cast<int32_t>(0x00000001)},
      {base::bit_cast<int32_t>(0xFF800000), base::bit_cast<int32_t>(0x56B5E621),
       base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x7F800000)},
      {base::bit_cast<int32_t>(0xF673B164), base::bit_cast<int32_t>(0x7B13653D),
       base::bit_cast<int32_t>(0x0000042E),
       base::bit_cast<int32_t>(0x00000000)}};

#define TEST_FEXDO_H(instruction, src, exp_res)                               \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

#define TEST_FEXDO_W(instruction, src, exp_res)                               \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    TEST_FEXDO_H(fexdo_h, &tc_w[i], &exp_res_fexdo_w[i])
  }

  for (uint64_t i = 0; i < arraysize(tc_d); i++) {
    TEST_FEXDO_W(fexdo_w, &tc_d[i], &exp_res_fexdo_d[i])
  }

#undef TEST_FEXDO_H
#undef TEST_FEXDO_W
}

TEST(MSA_ftq) {
  if ((kArchVariant != kMips64r6) || !CpuFeatures::IsSupported(MIPS_SIMD))
    return;

  CcTest::InitializeVM();

  const float nan_float = std::numeric_limits<float>::quiet_NaN();
  const float inf_float = std::numeric_limits<float>::infinity();
  const double nan_double = std::numeric_limits<double>::quiet_NaN();
  const double inf_double = std::numeric_limits<double>::infinity();

  const struct TestCaseMsa3RF_F tc_w[] = {
      {1.f, -0.999f, 1.5f, -31e-6, 1e-7, -0.598, 0.0023, -0.f, 0, 0, 0, 0},
      {100.f, -102.f, -1.1f, 1.3f, 0.f, -1.f, 0.9999f, -0.000322, 0, 0, 0, 0},
      {nan_float, inf_float, -inf_float, -nan_float, -1e-40, 3e-44, 8.3e36,
       -0.00003, 0, 0, 0, 0}};

  const struct TestCaseMsa3RF_D tc_d[] = {
      {1., -0.999, 1.5, -31e-6, 0, 0},
      {1e-7, -0.598, 0.0023, -0.f, 0, 0},
      {100.f, -102.f, -1.1f, 1.3f, 0, 0},
      {0.f, -1.f, 0.9999f, -0.000322, 0, 0},
      {nan_double, inf_double, -inf_double, -nan_double, 0, 0},
      {-3e306, 2e-307, 9e307, 2e-307, 0, 0}};

  const struct ExpRes_16I exp_res_ftq_w[] = {
      {static_cast<int16_t>(0x0000), static_cast<int16_t>(0xB375),
       static_cast<int16_t>(0x004B), static_cast<int16_t>(0x0000),
       static_cast<int16_t>(0x7FFF), static_cast<int16_t>(0x8021),
       static_cast<int16_t>(0x7FFF), static_cast<int16_t>(0xFFFF)},
      {static_cast<int16_t>(0x0000), static_cast<int16_t>(0x8000),
       static_cast<int16_t>(0x7FFD), static_cast<int16_t>(0xFFF5),
       static_cast<int16_t>(0x7FFF), static_cast<int16_t>(0x8000),
       static_cast<int16_t>(0x8000), static_cast<int16_t>(0x7FFF)},
      {static_cast<int16_t>(0x0000), static_cast<int16_t>(0x0000),
       static_cast<int16_t>(0x7FFF), static_cast<int16_t>(0xFFFF),
       static_cast<int16_t>(0x0000), static_cast<int16_t>(0x7FFF),
       static_cast<int16_t>(0x8000), static_cast<int16_t>(0x0000)}};

  const struct ExpRes_32I exp_res_ftq_d[] = {
      {base::bit_cast<int32_t>(0x7FFFFFFF), base::bit_cast<int32_t>(0xFFFEFBF4),
       base::bit_cast<int32_t>(0x7FFFFFFF),
       base::bit_cast<int32_t>(0x8020C49C)},
      {base::bit_cast<int32_t>(0x004B5DCC), base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x000000D7),
       base::bit_cast<int32_t>(0xB374BC6A)},
      {base::bit_cast<int32_t>(0x80000000), base::bit_cast<int32_t>(0x7FFFFFFF),
       base::bit_cast<int32_t>(0x7FFFFFFF),
       base::bit_cast<int32_t>(0x80000000)},
      {base::bit_cast<int32_t>(0x7FFCB900), base::bit_cast<int32_t>(0xFFF572DE),
       base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x80000000)},
      {base::bit_cast<int32_t>(0x80000000), base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x7FFFFFFF)},
      {base::bit_cast<int32_t>(0x7FFFFFFF), base::bit_cast<int32_t>(0x00000000),
       base::bit_cast<int32_t>(0x80000000),
       base::bit_cast<int32_t>(0x00000000)}};

#define TEST_FTQ_H(instruction, src, exp_res)                                 \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

#define TEST_FTQ_W(instruction, src, exp_res)                                 \
  run_msa_3rf(reinterpret_cast<const struct TestCaseMsa3RF*>(src),            \
              reinterpret_cast<const struct ExpectedResult_MSA3RF*>(exp_res), \
              [](MacroAssembler& assm) { __ instruction(w2, w0, w1); });

  for (uint64_t i = 0; i < arraysize(tc_w); i++) {
    TEST_FTQ_H(ftq_h, &tc_w[i], &exp_res_ftq_w[i])
  }

  for (uint64_t i = 0; i < arraysize(tc_d); i++) {
    TEST_FTQ_W(ftq_w, &tc_d[i], &exp_res_ftq_d[i])
  }

#undef TEST_FTQ_H
#undef TEST_FTQ_W
}

#undef __

}  // namespace internal
}  // namespace v8
