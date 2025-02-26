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

#include "src/base/numbers/double.h"
#include "src/base/utils/random-number-generator.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/utils/ostreams.h"
#include "test/cctest/assembler-helper-arm.h"
#include "test/cctest/cctest.h"
#include "test/common/value-helper.h"

namespace v8 {
namespace internal {
namespace test_assembler_arm {

using base::RandomNumberGenerator;

#define __ assm.

TEST(0) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  __ add(r0, r0, Operand(r1));
  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(3, 4, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(7, res);
}


TEST(1) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label L, C;

  __ mov(r1, Operand(r0));
  __ mov(r0, Operand::Zero());
  __ b(&C);

  __ bind(&L);
  __ add(r0, r0, Operand(r1));
  __ sub(r1, r1, Operand(1));

  __ bind(&C);
  __ teq(r1, Operand::Zero());
  __ b(ne, &L);
  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(100, 0, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(5050, res);
}


TEST(2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label L, C;

  __ mov(r1, Operand(r0));
  __ mov(r0, Operand(1));
  __ b(&C);

  __ bind(&L);
  __ mul(r0, r1, r0);
  __ sub(r1, r1, Operand(1));

  __ bind(&C);
  __ teq(r1, Operand::Zero());
  __ b(ne, &L);
  __ mov(pc, Operand(lr));

  // some relocated stuff here, not executed
  __ RecordComment("dead code, just testing relocations");
  __ mov(r0, Operand(isolate->factory()->true_value()));
  __ RecordComment("dead code, just testing immediate operands");
  __ mov(r0, Operand(-1));
  __ mov(r0, Operand(0xFF000000));
  __ mov(r0, Operand(0xF0F0F0F0));
  __ mov(r0, Operand(0xFFF0FFFF));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(10, 0, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(3628800, res);
}


TEST(3) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    int i;
    char c;
    int16_t s;
  };
  T t;

  Assembler assm(AssemblerOptions{});

  __ mov(ip, Operand(sp));
  __ stm(db_w, sp, {r4, fp, lr});
  __ sub(fp, ip, Operand(4));
  __ mov(r4, Operand(r0));
  __ ldr(r0, MemOperand(r4, offsetof(T, i)));
  __ mov(r2, Operand(r0, ASR, 1));
  __ str(r2, MemOperand(r4, offsetof(T, i)));
  __ ldrsb(r2, MemOperand(r4, offsetof(T, c)));
  __ add(r0, r2, Operand(r0));
  __ mov(r2, Operand(r2, LSL, 2));
  __ strb(r2, MemOperand(r4, offsetof(T, c)));
  __ ldrsh(r2, MemOperand(r4, offsetof(T, s)));
  __ add(r0, r2, Operand(r0));
  __ mov(r2, Operand(r2, ASR, 3));
  __ strh(r2, MemOperand(r4, offsetof(T, s)));
  __ ldm(ia_w, sp, {r4, fp, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  t.i = 100000;
  t.c = 10;
  t.s = 1000;
  int res = reinterpret_cast<int>(f.Call(&t, 0, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(101010, res);
  CHECK_EQ(100000/2, t.i);
  CHECK_EQ(10*4, t.c);
  CHECK_EQ(1000/8, t.s);
}


TEST(4) {
  // Test the VFP floating point instructions.
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
    int i;
    double j;
    double m;
    double n;
    float o;
    float p;
    float x;
    float y;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles and floats.
  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(VFPv3)) {
    CpuFeatureScope scope(&assm, VFPv3);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});
    __ sub(fp, ip, Operand(4));

    __ mov(r4, Operand(r0));
    __ vldr(d6, r4, offsetof(T, a));
    __ vldr(d7, r4, offsetof(T, b));
    __ vadd(d5, d6, d7);
    __ vstr(d5, r4, offsetof(T, c));

    __ vmla(d5, d6, d7);
    __ vmls(d5, d5, d6);

    __ vmov(r2, r3, d5);
    __ vmov(d4, r2, r3);
    __ vstr(d4, r4, offsetof(T, b));

    // Load t.x and t.y, switch values, and store back to the struct.
    __ vldr(s0, r4, offsetof(T, x));
    __ vldr(s1, r4, offsetof(T, y));
    __ vmov(s2, s0);
    __ vmov(s0, s1);
    __ vmov(s1, s2);
    __ vstr(s0, r4, offsetof(T, x));
    __ vstr(s1, r4, offsetof(T, y));

    // Move a literal into a register that can be encoded in the instruction.
    __ vmov(d4, base::Double(1.0));
    __ vstr(d4, r4, offsetof(T, e));

    // Move a literal into a register that requires 64 bits to encode.
    // 0x3FF0000010000000 = 1.000000059604644775390625
    __ vmov(d4, base::Double(1.000000059604644775390625));
    __ vstr(d4, r4, offsetof(T, d));

    // Convert from floating point to integer.
    __ vmov(d4, base::Double(2.0));
    __ vcvt_s32_f64(s1, d4);
    __ vstr(s1, r4, offsetof(T, i));

    // Convert from integer to floating point.
    __ mov(lr, Operand(42));
    __ vmov(s1, lr);
    __ vcvt_f64_s32(d4, s1);
    __ vstr(d4, r4, offsetof(T, f));

    // Convert from fixed point to floating point.
    __ mov(lr, Operand(2468));
    __ vmov(s8, lr);
    __ vcvt_f64_s32(d4, 2);
    __ vstr(d4, r4, offsetof(T, j));

    // Test vabs.
    __ vldr(d1, r4, offsetof(T, g));
    __ vabs(d0, d1);
    __ vstr(d0, r4, offsetof(T, g));
    __ vldr(d2, r4, offsetof(T, h));
    __ vabs(d0, d2);
    __ vstr(d0, r4, offsetof(T, h));

    // Test vneg.
    __ vldr(d1, r4, offsetof(T, m));
    __ vneg(d0, d1);
    __ vstr(d0, r4, offsetof(T, m));
    __ vldr(d1, r4, offsetof(T, n));
    __ vneg(d0, d1);
    __ vstr(d0, r4, offsetof(T, n));

    // Test vmov for single-precision immediates.
    __ vmov(s0, Float32(0.25f));
    __ vstr(s0, r4, offsetof(T, o));
    __ vmov(s0, Float32(-16.0f));
    __ vstr(s0, r4, offsetof(T, p));

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    t.a = 1.5;
    t.b = 2.75;
    t.c = 17.17;
    t.d = 0.0;
    t.e = 0.0;
    t.f = 0.0;
    t.g = -2718.2818;
    t.h = 31415926.5;
    t.i = 0;
    t.j = 0;
    t.m = -2718.2818;
    t.n = 123.456;
    t.x = 4.5;
    t.y = 9.0;
    f.Call(&t, 0, 0, 0, 0);
    CHECK_EQ(-16.0f, t.p);
    CHECK_EQ(0.25f, t.o);
    CHECK_EQ(-123.456, t.n);
    CHECK_EQ(2718.2818, t.m);
    CHECK_EQ(2, t.i);
    CHECK_EQ(2718.2818, t.g);
    CHECK_EQ(31415926.5, t.h);
    CHECK_EQ(617.0, t.j);
    CHECK_EQ(42.0, t.f);
    CHECK_EQ(1.0, t.e);
    CHECK_EQ(1.000000059604644775390625, t.d);
    CHECK_EQ(4.25, t.c);
    CHECK_EQ(-4.1875, t.b);
    CHECK_EQ(1.5, t.a);
    CHECK_EQ(4.5f, t.y);
    CHECK_EQ(9.0f, t.x);
  }
}


TEST(5) {
  // Test the ARMv7 bitfield instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatureScope scope(&assm, ARMv7);
    // On entry, r0 = 0xAAAAAAAA = 0b10..10101010.
    __ ubfx(r0, r0, 1, 12);  // 0b00..010101010101 = 0x555
    __ sbfx(r0, r0, 0, 5);   // 0b11..111111110101 = -11
    __ bfc(r0, 1, 3);        // 0b11..111111110001 = -15
    __ mov(r1, Operand(7));
    __ bfi(r0, r1, 3, 3);    // 0b11..111111111001 = -7
    __ mov(pc, Operand(lr));

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    int res = reinterpret_cast<int>(f.Call(0xAAAAAAAA, 0, 0, 0, 0));
    ::printf("f() = %d\n", res);
    CHECK_EQ(-7, res);
  }
}


TEST(6) {
  // Test saturating instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  __ usat(r1, 8, Operand(r0));           // Sat 0xFFFF to 0-255 = 0xFF.
  __ usat(r2, 12, Operand(r0, ASR, 9));  // Sat (0xFFFF>>9) to 0-4095 = 0x7F.
  __ usat(r3, 1, Operand(r0, LSL, 16));  // Sat (0xFFFF<<16) to 0-1 = 0x0.
  __ add(r0, r1, Operand(r2));
  __ add(r0, r0, Operand(r3));
  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(0xFFFF, 0, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(382, res);
}


enum VCVTTypes {
  s32_f64,
  u32_f64
};

static void TestRoundingMode(VCVTTypes types,
                             VFPRoundingMode mode,
                             double value,
                             int expected,
                             bool expected_exception = false) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  Label wrong_exception;

  __ vmrs(r1);
  // Set custom FPSCR.
  __ bic(r2, r1, Operand(kVFPRoundingModeMask | kVFPExceptionMask));
  __ orr(r2, r2, Operand(mode));
  __ vmsr(r2);

  // Load value, convert, and move back result to r0 if everything went well.
  __ vmov(d1, base::Double(value));
  switch (types) {
    case s32_f64:
      __ vcvt_s32_f64(s0, d1, kFPSCRRounding);
      break;

    case u32_f64:
      __ vcvt_u32_f64(s0, d1, kFPSCRRounding);
      break;

    default:
      UNREACHABLE();
  }
  // Check for vfp exceptions
  __ vmrs(r2);
  __ tst(r2, Operand(kVFPExceptionMask));
  // Check that we behaved as expected.
  __ b(&wrong_exception, expected_exception ? eq : ne);
  // There was no exception. Retrieve the result and return.
  __ vmov(r0, s0);
  __ mov(pc, Operand(lr));

  // The exception behaviour is not what we expected.
  // Load a special value and return.
  __ bind(&wrong_exception);
  __ mov(r0, Operand(11223344));
  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(0, 0, 0, 0, 0));
  ::printf("res = %d\n", res);
  CHECK_EQ(expected, res);
}


TEST(7) {
  CcTest::InitializeVM();
  // Test vfp rounding modes.

  // s32_f64 (double to integer).

  TestRoundingMode(s32_f64, RN,  0, 0);
  TestRoundingMode(s32_f64, RN,  0.5, 0);
  TestRoundingMode(s32_f64, RN, -0.5, 0);
  TestRoundingMode(s32_f64, RN,  1.5, 2);
  TestRoundingMode(s32_f64, RN, -1.5, -2);
  TestRoundingMode(s32_f64, RN,  123.7, 124);
  TestRoundingMode(s32_f64, RN, -123.7, -124);
  TestRoundingMode(s32_f64, RN,  123456.2,  123456);
  TestRoundingMode(s32_f64, RN, -123456.2, -123456);
  TestRoundingMode(s32_f64, RN, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(s32_f64, RN, (kMaxInt + 0.49), kMaxInt);
  TestRoundingMode(s32_f64, RN, (kMaxInt + 1.0), kMaxInt, true);
  TestRoundingMode(s32_f64, RN, (kMaxInt + 0.5), kMaxInt, true);
  TestRoundingMode(s32_f64, RN, static_cast<double>(kMinInt), kMinInt);
  TestRoundingMode(s32_f64, RN, (kMinInt - 0.5), kMinInt);
  TestRoundingMode(s32_f64, RN, (kMinInt - 1.0), kMinInt, true);
  TestRoundingMode(s32_f64, RN, (kMinInt - 0.51), kMinInt, true);

  TestRoundingMode(s32_f64, RM,  0, 0);
  TestRoundingMode(s32_f64, RM,  0.5, 0);
  TestRoundingMode(s32_f64, RM, -0.5, -1);
  TestRoundingMode(s32_f64, RM,  123.7, 123);
  TestRoundingMode(s32_f64, RM, -123.7, -124);
  TestRoundingMode(s32_f64, RM,  123456.2,  123456);
  TestRoundingMode(s32_f64, RM, -123456.2, -123457);
  TestRoundingMode(s32_f64, RM, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(s32_f64, RM, (kMaxInt + 0.5), kMaxInt);
  TestRoundingMode(s32_f64, RM, (kMaxInt + 1.0), kMaxInt, true);
  TestRoundingMode(s32_f64, RM, static_cast<double>(kMinInt), kMinInt);
  TestRoundingMode(s32_f64, RM, (kMinInt - 0.5), kMinInt, true);
  TestRoundingMode(s32_f64, RM, (kMinInt + 0.5), kMinInt);

  TestRoundingMode(s32_f64, RZ,  0, 0);
  TestRoundingMode(s32_f64, RZ,  0.5, 0);
  TestRoundingMode(s32_f64, RZ, -0.5, 0);
  TestRoundingMode(s32_f64, RZ,  123.7,  123);
  TestRoundingMode(s32_f64, RZ, -123.7, -123);
  TestRoundingMode(s32_f64, RZ,  123456.2,  123456);
  TestRoundingMode(s32_f64, RZ, -123456.2, -123456);
  TestRoundingMode(s32_f64, RZ, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(s32_f64, RZ, (kMaxInt + 0.5), kMaxInt);
  TestRoundingMode(s32_f64, RZ, (kMaxInt + 1.0), kMaxInt, true);
  TestRoundingMode(s32_f64, RZ, static_cast<double>(kMinInt), kMinInt);
  TestRoundingMode(s32_f64, RZ, (kMinInt - 0.5), kMinInt);
  TestRoundingMode(s32_f64, RZ, (kMinInt - 1.0), kMinInt, true);


  // u32_f64 (double to integer).

  // Negative values.
  TestRoundingMode(u32_f64, RN, -0.5, 0);
  TestRoundingMode(u32_f64, RN, -123456.7, 0, true);
  TestRoundingMode(u32_f64, RN, static_cast<double>(kMinInt), 0, true);
  TestRoundingMode(u32_f64, RN, kMinInt - 1.0, 0, true);

  TestRoundingMode(u32_f64, RM, -0.5, 0, true);
  TestRoundingMode(u32_f64, RM, -123456.7, 0, true);
  TestRoundingMode(u32_f64, RM, static_cast<double>(kMinInt), 0, true);
  TestRoundingMode(u32_f64, RM, kMinInt - 1.0, 0, true);

  TestRoundingMode(u32_f64, RZ, -0.5, 0);
  TestRoundingMode(u32_f64, RZ, -123456.7, 0, true);
  TestRoundingMode(u32_f64, RZ, static_cast<double>(kMinInt), 0, true);
  TestRoundingMode(u32_f64, RZ, kMinInt - 1.0, 0, true);

  // Positive values.
  // kMaxInt is the maximum *signed* integer: 0x7FFFFFFF.
  static const uint32_t kMaxUInt = 0xFFFFFFFFu;
  TestRoundingMode(u32_f64, RZ,  0, 0);
  TestRoundingMode(u32_f64, RZ,  0.5, 0);
  TestRoundingMode(u32_f64, RZ,  123.7,  123);
  TestRoundingMode(u32_f64, RZ,  123456.2,  123456);
  TestRoundingMode(u32_f64, RZ, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(u32_f64, RZ, (kMaxInt + 0.5), kMaxInt);
  TestRoundingMode(u32_f64, RZ, (kMaxInt + 1.0),
                                static_cast<uint32_t>(kMaxInt) + 1);
  TestRoundingMode(u32_f64, RZ, (kMaxUInt + 0.5), kMaxUInt);
  TestRoundingMode(u32_f64, RZ, (kMaxUInt + 1.0), kMaxUInt, true);

  TestRoundingMode(u32_f64, RM,  0, 0);
  TestRoundingMode(u32_f64, RM,  0.5, 0);
  TestRoundingMode(u32_f64, RM,  123.7, 123);
  TestRoundingMode(u32_f64, RM,  123456.2,  123456);
  TestRoundingMode(u32_f64, RM, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(u32_f64, RM, (kMaxInt + 0.5), kMaxInt);
  TestRoundingMode(u32_f64, RM, (kMaxInt + 1.0),
                                static_cast<uint32_t>(kMaxInt) + 1);
  TestRoundingMode(u32_f64, RM, (kMaxUInt + 0.5), kMaxUInt);
  TestRoundingMode(u32_f64, RM, (kMaxUInt + 1.0), kMaxUInt, true);

  TestRoundingMode(u32_f64, RN,  0, 0);
  TestRoundingMode(u32_f64, RN,  0.5, 0);
  TestRoundingMode(u32_f64, RN,  1.5, 2);
  TestRoundingMode(u32_f64, RN,  123.7, 124);
  TestRoundingMode(u32_f64, RN,  123456.2,  123456);
  TestRoundingMode(u32_f64, RN, static_cast<double>(kMaxInt), kMaxInt);
  TestRoundingMode(u32_f64, RN, (kMaxInt + 0.49), kMaxInt);
  TestRoundingMode(u32_f64, RN, (kMaxInt + 0.5),
                                static_cast<uint32_t>(kMaxInt) + 1);
  TestRoundingMode(u32_f64, RN, (kMaxUInt + 0.49), kMaxUInt);
  TestRoundingMode(u32_f64, RN, (kMaxUInt + 0.5), kMaxUInt, true);
  TestRoundingMode(u32_f64, RN, (kMaxUInt + 1.0), kMaxUInt, true);
}


TEST(8) {
  // Test VFP multi load/store with ia_w.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct D {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double g;
    double h;
  };
  D d;

  struct F {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
    float g;
    float h;
  };
  F f;

  // Create a function that uses vldm/vstm to move some double and
  // single precision values around in memory.
  Assembler assm(AssemblerOptions{});

  __ mov(ip, Operand(sp));
  __ stm(db_w, sp, {r4, fp, lr});
  __ sub(fp, ip, Operand(4));

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, a))));
  __ vldm(ia_w, r4, d0, d3);
  __ vldm(ia_w, r4, d4, d7);

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, a))));
  __ vstm(ia_w, r4, d6, d7);
  __ vstm(ia_w, r4, d0, d5);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, a))));
  __ vldm(ia_w, r4, s0, s3);
  __ vldm(ia_w, r4, s4, s7);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, a))));
  __ vstm(ia_w, r4, s6, s7);
  __ vstm(ia_w, r4, s0, s5);

  __ ldm(ia_w, sp, {r4, fp, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto fn = GeneratedCode<F_ppiii>::FromCode(isolate, *code);
  d.a = 1.1;
  d.b = 2.2;
  d.c = 3.3;
  d.d = 4.4;
  d.e = 5.5;
  d.f = 6.6;
  d.g = 7.7;
  d.h = 8.8;

  f.a = 1.0;
  f.b = 2.0;
  f.c = 3.0;
  f.d = 4.0;
  f.e = 5.0;
  f.f = 6.0;
  f.g = 7.0;
  f.h = 8.0;

  fn.Call(&d, &f, 0, 0, 0);

  CHECK_EQ(7.7, d.a);
  CHECK_EQ(8.8, d.b);
  CHECK_EQ(1.1, d.c);
  CHECK_EQ(2.2, d.d);
  CHECK_EQ(3.3, d.e);
  CHECK_EQ(4.4, d.f);
  CHECK_EQ(5.5, d.g);
  CHECK_EQ(6.6, d.h);

  CHECK_EQ(7.0f, f.a);
  CHECK_EQ(8.0f, f.b);
  CHECK_EQ(1.0f, f.c);
  CHECK_EQ(2.0f, f.d);
  CHECK_EQ(3.0f, f.e);
  CHECK_EQ(4.0f, f.f);
  CHECK_EQ(5.0f, f.g);
  CHECK_EQ(6.0f, f.h);
}


TEST(9) {
  // Test VFP multi load/store with ia.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct D {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double g;
    double h;
  };
  D d;

  struct F {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
    float g;
    float h;
  };
  F f;

  // Create a function that uses vldm/vstm to move some double and
  // single precision values around in memory.
  Assembler assm(AssemblerOptions{});

  __ mov(ip, Operand(sp));
  __ stm(db_w, sp, {r4, fp, lr});
  __ sub(fp, ip, Operand(4));

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, a))));
  __ vldm(ia, r4, d0, d3);
  __ add(r4, r4, Operand(4 * 8));
  __ vldm(ia, r4, d4, d7);

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, a))));
  __ vstm(ia, r4, d6, d7);
  __ add(r4, r4, Operand(2 * 8));
  __ vstm(ia, r4, d0, d5);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, a))));
  __ vldm(ia, r4, s0, s3);
  __ add(r4, r4, Operand(4 * 4));
  __ vldm(ia, r4, s4, s7);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, a))));
  __ vstm(ia, r4, s6, s7);
  __ add(r4, r4, Operand(2 * 4));
  __ vstm(ia, r4, s0, s5);

  __ ldm(ia_w, sp, {r4, fp, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto fn = GeneratedCode<F_ppiii>::FromCode(isolate, *code);
  d.a = 1.1;
  d.b = 2.2;
  d.c = 3.3;
  d.d = 4.4;
  d.e = 5.5;
  d.f = 6.6;
  d.g = 7.7;
  d.h = 8.8;

  f.a = 1.0;
  f.b = 2.0;
  f.c = 3.0;
  f.d = 4.0;
  f.e = 5.0;
  f.f = 6.0;
  f.g = 7.0;
  f.h = 8.0;

  fn.Call(&d, &f, 0, 0, 0);

  CHECK_EQ(7.7, d.a);
  CHECK_EQ(8.8, d.b);
  CHECK_EQ(1.1, d.c);
  CHECK_EQ(2.2, d.d);
  CHECK_EQ(3.3, d.e);
  CHECK_EQ(4.4, d.f);
  CHECK_EQ(5.5, d.g);
  CHECK_EQ(6.6, d.h);

  CHECK_EQ(7.0f, f.a);
  CHECK_EQ(8.0f, f.b);
  CHECK_EQ(1.0f, f.c);
  CHECK_EQ(2.0f, f.d);
  CHECK_EQ(3.0f, f.e);
  CHECK_EQ(4.0f, f.f);
  CHECK_EQ(5.0f, f.g);
  CHECK_EQ(6.0f, f.h);
}


TEST(10) {
  // Test VFP multi load/store with db_w.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct D {
    double a;
    double b;
    double c;
    double d;
    double e;
    double f;
    double g;
    double h;
  };
  D d;

  struct F {
    float a;
    float b;
    float c;
    float d;
    float e;
    float f;
    float g;
    float h;
  };
  F f;

  // Create a function that uses vldm/vstm to move some double and
  // single precision values around in memory.
  Assembler assm(AssemblerOptions{});

  __ mov(ip, Operand(sp));
  __ stm(db_w, sp, {r4, fp, lr});
  __ sub(fp, ip, Operand(4));

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, h)) + 8));
  __ vldm(db_w, r4, d4, d7);
  __ vldm(db_w, r4, d0, d3);

  __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(D, h)) + 8));
  __ vstm(db_w, r4, d0, d5);
  __ vstm(db_w, r4, d6, d7);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, h)) + 4));
  __ vldm(db_w, r4, s4, s7);
  __ vldm(db_w, r4, s0, s3);

  __ add(r4, r1, Operand(static_cast<int32_t>(offsetof(F, h)) + 4));
  __ vstm(db_w, r4, s0, s5);
  __ vstm(db_w, r4, s6, s7);

  __ ldm(ia_w, sp, {r4, fp, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto fn = GeneratedCode<F_ppiii>::FromCode(isolate, *code);
  d.a = 1.1;
  d.b = 2.2;
  d.c = 3.3;
  d.d = 4.4;
  d.e = 5.5;
  d.f = 6.6;
  d.g = 7.7;
  d.h = 8.8;

  f.a = 1.0;
  f.b = 2.0;
  f.c = 3.0;
  f.d = 4.0;
  f.e = 5.0;
  f.f = 6.0;
  f.g = 7.0;
  f.h = 8.0;

  fn.Call(&d, &f, 0, 0, 0);

  CHECK_EQ(7.7, d.a);
  CHECK_EQ(8.8, d.b);
  CHECK_EQ(1.1, d.c);
  CHECK_EQ(2.2, d.d);
  CHECK_EQ(3.3, d.e);
  CHECK_EQ(4.4, d.f);
  CHECK_EQ(5.5, d.g);
  CHECK_EQ(6.6, d.h);

  CHECK_EQ(7.0f, f.a);
  CHECK_EQ(8.0f, f.b);
  CHECK_EQ(1.0f, f.c);
  CHECK_EQ(2.0f, f.d);
  CHECK_EQ(3.0f, f.e);
  CHECK_EQ(4.0f, f.f);
  CHECK_EQ(5.0f, f.g);
  CHECK_EQ(6.0f, f.h);
}


TEST(11) {
  // Test instructions using the carry flag.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct I {
    int32_t a;
    int32_t b;
    int32_t c;
    int32_t d;
  };
  I i;

  i.a = 0xABCD0001;
  i.b = 0xABCD0000;

  Assembler assm(AssemblerOptions{});

  // Test HeapObject untagging.
  __ ldr(r1, MemOperand(r0, offsetof(I, a)));
  __ mov(r1, Operand(r1, ASR, 1), SetCC);
  __ adc(r1, r1, Operand(r1), LeaveCC, cs);
  __ str(r1, MemOperand(r0, offsetof(I, a)));

  __ ldr(r2, MemOperand(r0, offsetof(I, b)));
  __ mov(r2, Operand(r2, ASR, 1), SetCC);
  __ adc(r2, r2, Operand(r2), LeaveCC, cs);
  __ str(r2, MemOperand(r0, offsetof(I, b)));

  // Test corner cases.
  __ mov(r1, Operand(0xFFFFFFFF));
  __ mov(r2, Operand::Zero());
  __ mov(r3, Operand(r1, ASR, 1), SetCC);  // Set the carry.
  __ adc(r3, r1, Operand(r2));
  __ str(r3, MemOperand(r0, offsetof(I, c)));

  __ mov(r1, Operand(0xFFFFFFFF));
  __ mov(r2, Operand::Zero());
  __ mov(r3, Operand(r2, ASR, 1), SetCC);  // Unset the carry.
  __ adc(r3, r1, Operand(r2));
  __ str(r3, MemOperand(r0, offsetof(I, d)));

  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  f.Call(&i, 0, 0, 0, 0);

  CHECK_EQ(static_cast<int32_t>(0xABCD0001), i.a);
  CHECK_EQ(static_cast<int32_t>(0xABCD0000) >> 1, i.b);
  CHECK_EQ(0x00000000, i.c);
  CHECK_EQ(static_cast<int32_t>(0xFFFFFFFF), i.d);
}


TEST(12) {
  // Test chaining of label usages within instructions (issue 1644).
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label target;
  __ b(eq, &target);
  __ b(ne, &target);
  __ bind(&target);
  __ nop();
}


TEST(13) {
  // Test VFP instructions using registers d16-d31.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  if (!CpuFeatures::IsSupported(VFP32DREGS)) {
    return;
  }

  struct T {
    double a;
    double b;
    double c;
    double x;
    double y;
    double z;
    double i;
    double j;
    double k;
    uint32_t low;
    uint32_t high;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles and floats.
  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(VFPv3)) {
    CpuFeatureScope scope(&assm, VFPv3);

    __ stm(db_w, sp, {r4, lr});

    // Load a, b, c into d16, d17, d18.
    __ mov(r4, Operand(r0));
    __ vldr(d16, r4, offsetof(T, a));
    __ vldr(d17, r4, offsetof(T, b));
    __ vldr(d18, r4, offsetof(T, c));

    __ vneg(d25, d16);
    __ vadd(d25, d25, d17);
    __ vsub(d25, d25, d18);
    __ vmul(d25, d25, d25);
    __ vdiv(d25, d25, d18);

    __ vmov(d16, d25);
    __ vsqrt(d17, d25);
    __ vneg(d17, d17);
    __ vabs(d17, d17);
    __ vmla(d18, d16, d17);

    // Store d16, d17, d18 into a, b, c.
    __ mov(r4, Operand(r0));
    __ vstr(d16, r4, offsetof(T, a));
    __ vstr(d17, r4, offsetof(T, b));
    __ vstr(d18, r4, offsetof(T, c));

    // Load x, y, z into d29-d31.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, x))));
    __ vldm(ia_w, r4, d29, d31);

    // Swap d29 and d30 via r registers.
    __ vmov(r1, r2, d29);
    __ vmov(d29, d30);
    __ vmov(d30, r1, r2);

    // Convert to and from integer.
    __ vcvt_s32_f64(s1, d31);
    __ vcvt_f64_u32(d31, s1);

    // Store d29-d31 into x, y, z.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, x))));
    __ vstm(ia_w, r4, d29, d31);

    // Move constants into d20, d21, d22 and store into i, j, k.
    __ vmov(d20, base::Double(14.7610017472335499));
    __ vmov(d21, base::Double(16.0));
    __ mov(r1, Operand(372106121));
    __ mov(r2, Operand(1079146608));
    __ vmov(NeonS32, d22, 0, r1);
    __ vmov(NeonS32, d22, 1, r2);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, i))));
    __ vstm(ia_w, r4, d20, d22);
    // Move d22 into low and high.
    __ vmov(NeonS32, r4, d22, 0);
    __ str(r4, MemOperand(r0, offsetof(T, low)));
    __ vmov(NeonS32, r4, d22, 1);
    __ str(r4, MemOperand(r0, offsetof(T, high)));

    __ ldm(ia_w, sp, {r4, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    t.a = 1.5;
    t.b = 2.75;
    t.c = 17.17;
    t.x = 1.5;
    t.y = 2.75;
    t.z = 17.17;
    f.Call(&t, 0, 0, 0, 0);
    CHECK_EQ(14.7610017472335499, t.a);
    CHECK_EQ(3.84200491244266251, t.b);
    CHECK_EQ(73.8818412254460241, t.c);
    CHECK_EQ(2.75, t.x);
    CHECK_EQ(1.5, t.y);
    CHECK_EQ(17.0, t.z);
    CHECK_EQ(14.7610017472335499, t.i);
    CHECK_EQ(16.0, t.j);
    CHECK_EQ(73.8818412254460241, t.k);
    CHECK_EQ(372106121u, t.low);
    CHECK_EQ(1079146608u, t.high);
  }
}


TEST(14) {
  // Test the VFP Canonicalized Nan mode.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double left;
    double right;
    double add_result;
    double sub_result;
    double mul_result;
    double div_result;
  };
  T t;

  // Create a function that makes the four basic operations.
  Assembler assm(AssemblerOptions{});

  // Ensure FPSCR state (as JSEntry does).
  Label fpscr_done;
  __ vmrs(r1);
  __ tst(r1, Operand(kVFPDefaultNaNModeControlBit));
  __ b(ne, &fpscr_done);
  __ orr(r1, r1, Operand(kVFPDefaultNaNModeControlBit));
  __ vmsr(r1);
  __ bind(&fpscr_done);

  __ vldr(d0, r0, offsetof(T, left));
  __ vldr(d1, r0, offsetof(T, right));
  __ vadd(d2, d0, d1);
  __ vstr(d2, r0, offsetof(T, add_result));
  __ vsub(d2, d0, d1);
  __ vstr(d2, r0, offsetof(T, sub_result));
  __ vmul(d2, d0, d1);
  __ vstr(d2, r0, offsetof(T, mul_result));
  __ vdiv(d2, d0, d1);
  __ vstr(d2, r0, offsetof(T, div_result));

  __ mov(pc, Operand(lr));

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  t.left = base::bit_cast<double>(kHoleNanInt64);
  t.right = 1;
  t.add_result = 0;
  t.sub_result = 0;
  t.mul_result = 0;
  t.div_result = 0;
  f.Call(&t, 0, 0, 0, 0);
  const uint32_t kArmNanUpper32 = 0x7FF80000;
  const uint32_t kArmNanLower32 = 0x00000000;
#ifdef DEBUG
  const uint64_t kArmNanInt64 =
      (static_cast<uint64_t>(kArmNanUpper32) << 32) | kArmNanLower32;
  CHECK_NE(kArmNanInt64, kHoleNanInt64);
#endif
  // With VFP2 the sign of the canonicalized Nan is undefined. So
  // we remove the sign bit for the upper tests.
  CHECK_EQ(kArmNanUpper32,
           (base::bit_cast<int64_t>(t.add_result) >> 32) & 0x7FFFFFFF);
  CHECK_EQ(kArmNanLower32, base::bit_cast<int64_t>(t.add_result) & 0xFFFFFFFFu);
  CHECK_EQ(kArmNanUpper32,
           (base::bit_cast<int64_t>(t.sub_result) >> 32) & 0x7FFFFFFF);
  CHECK_EQ(kArmNanLower32, base::bit_cast<int64_t>(t.sub_result) & 0xFFFFFFFFu);
  CHECK_EQ(kArmNanUpper32,
           (base::bit_cast<int64_t>(t.mul_result) >> 32) & 0x7FFFFFFF);
  CHECK_EQ(kArmNanLower32, base::bit_cast<int64_t>(t.mul_result) & 0xFFFFFFFFu);
  CHECK_EQ(kArmNanUpper32,
           (base::bit_cast<int64_t>(t.div_result) >> 32) & 0x7FFFFFFF);
  CHECK_EQ(kArmNanLower32, base::bit_cast<int64_t>(t.div_result) & 0xFFFFFFFFu);
}

#define CHECK_EQ_SPLAT(field, ex) \
  CHECK_EQ(ex, t.field[0]);       \
  CHECK_EQ(ex, t.field[1]);       \
  CHECK_EQ(ex, t.field[2]);       \
  CHECK_EQ(ex, t.field[3]);

#define CHECK_EQ_32X2(field, ex0, ex1) \
  CHECK_EQ(ex0, t.field[0]);           \
  CHECK_EQ(ex1, t.field[1]);

#define CHECK_EQ_32X4(field, ex0, ex1, ex2, ex3) \
  CHECK_EQ(ex0, t.field[0]);                     \
  CHECK_EQ(ex1, t.field[1]);                     \
  CHECK_EQ(ex2, t.field[2]);                     \
  CHECK_EQ(ex3, t.field[3]);

#define CHECK_ESTIMATE(expected, tolerance, value) \
  CHECK_LT((expected) - (tolerance), value);       \
  CHECK_GT((expected) + (tolerance), value);

#define CHECK_ESTIMATE_SPLAT(field, ex, tol) \
  CHECK_ESTIMATE(ex, tol, t.field[0]);       \
  CHECK_ESTIMATE(ex, tol, t.field[1]);       \
  CHECK_ESTIMATE(ex, tol, t.field[2]);       \
  CHECK_ESTIMATE(ex, tol, t.field[3]);

#define INT32_TO_FLOAT(val) \
  std::round(static_cast<float>(base::bit_cast<int32_t>(val)))
#define UINT32_TO_FLOAT(val) \
  std::round(static_cast<float>(base::bit_cast<uint32_t>(val)))

TEST(15) {
  // Test the Neon instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint32_t src0;
    uint32_t src1;
    uint32_t src2;
    uint32_t src3;
    uint32_t src4;
    uint32_t src5;
    uint32_t src6;
    uint32_t src7;
    uint32_t dst0;
    uint32_t dst1;
    uint32_t dst2;
    uint32_t dst3;
    uint32_t dst4;
    uint32_t dst5;
    uint32_t dst6;
    uint32_t dst7;
    uint32_t srcA0;
    uint32_t srcA1;
    uint32_t dstA0;
    uint32_t dstA1;
    uint32_t dstA2;
    uint32_t dstA3;
    uint32_t lane_test[4];
    uint64_t vmov_to_scalar1, vmov_to_scalar2;
    uint32_t vmov_from_scalar_s8, vmov_from_scalar_u8;
    uint32_t vmov_from_scalar_s16, vmov_from_scalar_u16;
    uint32_t vmov_from_scalar_32;
    uint32_t vmov[4], vmvn[4];
    uint32_t vmovl_s8[4], vmovl_u16[4], vmovl_s32[4];
    uint32_t vqmovn_s8[2], vqmovn_u16[2], vqmovn_s32[2];
    int32_t vcvt_s32_f32[4];
    uint32_t vcvt_u32_f32[4];
    float vcvt_f32_s32[4], vcvt_f32_u32[4];
    uint32_t vdup8[4], vdup16[4], vdup32[4];
    float vabsf[4], vnegf[4];
    uint32_t vclt0_s8[4], vclt0_s16[4], vclt0_s32[4];
    uint32_t vabs_s8[4], vabs_s16[4], vabs_s32[4];
    uint32_t vneg_s8[4], vneg_s16[4], vneg_s32[4];
    uint32_t veor[4], vand[4], vorr[4];
    float vdupf[4], vaddf[4], vpaddf[2], vsubf[4], vmulf[4];
    uint32_t vdupf_16[2], vdupf_8[4];
    uint32_t vmin_s8[4], vmin_u16[4], vmin_s32[4];
    uint32_t vmax_s8[4], vmax_u16[4], vmax_s32[4];
    uint32_t vpadd_i8[2], vpadd_i16[2], vpadd_i32[2];
    uint32_t vpadal_s8[4], vpadal_s16[4], vpadal_s32[4];
    uint32_t vpadal_u8[4], vpadal_u16[4], vpadal_u32[4];
    uint32_t vpaddl_s8[4], vpaddl_s16[4], vpaddl_s32[4];
    uint32_t vpaddl_u8[4], vpaddl_u16[4], vpaddl_u32[4];
    uint32_t vpmin_s8[2], vpmin_u16[2], vpmin_s32[2];
    uint32_t vpmax_s8[2], vpmax_u16[2], vpmax_s32[2];
    uint32_t vadd8[4], vadd16[4], vadd32[4];
    uint32_t vqadd_s8[4], vqadd_u16[4], vqadd_s32[4];
    uint32_t vsub8[4], vsub16[4], vsub32[4];
    uint32_t vqsub_u8[4], vqsub_s16[4], vqsub_u32[4];
    uint32_t vmul8[4], vmul16[4], vmul32[4];
    uint32_t vshl8[4], vshl16[4], vshl32[4];
    uint32_t vshr_s8[4], vshr_u16[4], vshr_s32[4];
    uint32_t vshr_s8_d[2], vshr_u16_d[2], vshr_s32_d[2];
    uint32_t vsli_64[2], vsri_64[2], vsli_32[2], vsri_32[2];
    uint32_t vsra_64[2], vsra_32[2], vsra_16[2];
    uint32_t vceq[4], vceqf[4], vcgef[4], vcgtf[4];
    uint32_t vcge_s8[4], vcge_u16[4], vcge_s32[4];
    uint32_t vcgt_s8[4], vcgt_u16[4], vcgt_s32[4];
    float vrecpe[4], vrecps[4], vrsqrte[4], vrsqrts[4];
    float vminf[4], vmaxf[4];
    uint32_t vtst[4], vbsl[4];
    uint32_t vext[4];
    uint32_t vzip8a[4], vzip8b[4], vzip16a[4], vzip16b[4], vzip32a[4],
        vzip32b[4];
    uint32_t vzipd8a[2], vzipd8b[2], vzipd16a[2], vzipd16b[2];
    uint32_t vuzp8a[4], vuzp8b[4], vuzp16a[4], vuzp16b[4], vuzp32a[4],
        vuzp32b[4];
    uint32_t vuzpd8a[2], vuzpd8b[2], vuzpd16a[2], vuzpd16b[2];
    uint32_t vrev64_32[4], vrev64_16[4], vrev64_8[4];
    uint32_t vrev32_16[4], vrev32_8[4], vrev16_8[4];
    uint32_t vtrn8a[4], vtrn8b[4], vtrn16a[4], vtrn16b[4], vtrn32a[4],
        vtrn32b[4];
    uint32_t vtrnd8a[2], vtrnd8b[2], vtrnd16a[2], vtrnd16b[2], vtrnd32a[2],
        vtrnd32b[2];
    uint32_t vtbl[2], vtbx[2];
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles, floats, and SIMD values.
  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(NEON)) {
    CpuFeatureScope scope(&assm, NEON);

    __ stm(db_w, sp, {r4, r5, lr});
    // Move 32 bytes with neon.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, src0))));
    __ vld1(Neon8, NeonListOperand(d0, 4), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, dst0))));
    __ vst1(Neon8, NeonListOperand(d0, 4), NeonMemOperand(r4));

    // Expand 8 bytes into 8 words(16 bits).
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, srcA0))));
    __ vld1(Neon8, NeonListOperand(d0), NeonMemOperand(r4));
    __ vmovl(NeonU8, q0, d0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, dstA0))));
    __ vst1(Neon8, NeonListOperand(d0, 2), NeonMemOperand(r4));

    // The same expansion, but with different source and destination registers.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, srcA0))));
    __ vld1(Neon8, NeonListOperand(d1), NeonMemOperand(r4));
    __ vmovl(NeonS8, q1, d1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmovl_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vmovl(NeonU16, q2, d3);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmovl_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vmovl(NeonS32, q3, d4);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmovl_s32))));
    __ vst1(Neon8, NeonListOperand(q3), NeonMemOperand(r4));
    // Narrow what we widened.
    __ vqmovn(NeonU16, NeonU16, d0, q2);
    __ vstr(d0, r0, offsetof(T, vqmovn_u16));
    __ vmov(d1, d0);
    __ vqmovn(NeonS8, NeonS8, d2, q0);
    __ vstr(d2, r0, offsetof(T, vqmovn_s8));
    __ vqmovn(NeonS32, NeonS32, d4, q3);
    __ vstr(d4, r0, offsetof(T, vqmovn_s32));

    // ARM core register to scalar.
    __ mov(r4, Operand(0xFFFFFFF8));
    __ vmov(d0, base::Double(0.0));
    __ vmov(NeonS8, d0, 1, r4);
    __ vmov(NeonS16, d0, 1, r4);
    __ vmov(NeonS32, d0, 1, r4);
    __ vstr(d0, r0, offsetof(T, vmov_to_scalar1));
    __ vmov(d0, base::Double(0.0));
    __ vmov(NeonS8, d0, 3, r4);
    __ vmov(NeonS16, d0, 3, r4);
    __ vstr(d0, r0, offsetof(T, vmov_to_scalar2));

    // Scalar to ARM core register.
    __ mov(r4, Operand(0xFFFFFF00));
    __ mov(r5, Operand(0xFFFFFFFF));
    __ vmov(d0, r4, r5);
    __ vmov(NeonS8, r4, d0, 1);
    __ str(r4, MemOperand(r0, offsetof(T, vmov_from_scalar_s8)));
    __ vmov(NeonU8, r4, d0, 1);
    __ str(r4, MemOperand(r0, offsetof(T, vmov_from_scalar_u8)));
    __ vmov(NeonS16, r4, d0, 1);
    __ str(r4, MemOperand(r0, offsetof(T, vmov_from_scalar_s16)));
    __ vmov(NeonU16, r4, d0, 1);
    __ str(r4, MemOperand(r0, offsetof(T, vmov_from_scalar_u16)));
    __ vmov(NeonS32, r4, d0, 1);
    __ str(r4, MemOperand(r0, offsetof(T, vmov_from_scalar_32)));

    // vmov for q-registers.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmov))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vmvn.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmvn(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmvn))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vcvt for q-registers.
    __ vmov(s0, Float32(-1.5f));
    __ vmov(s1, Float32(-1.0f));
    __ vmov(s2, Float32(1.0f));
    __ vmov(s3, Float32(1.5f));
    __ vcvt_s32_f32(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcvt_s32_f32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vcvt_u32_f32(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcvt_u32_f32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(kMinInt));
    __ mov(r5, Operand(kMaxInt));
    __ vmov(d0, r4, r5);
    __ mov(r4, Operand(kMaxUInt32));
    __ mov(r5, Operand(kMinInt + 1));
    __ vmov(d1, r4, r5);  // q0 = [kMinInt, kMaxInt, kMaxUInt32, kMinInt + 1]
    __ vcvt_f32_s32(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcvt_f32_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vcvt_f32_u32(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcvt_f32_u32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vdup (from register).
    __ mov(r4, Operand(0xA));
    __ vdup(Neon8, q0, r4);
    __ vdup(Neon16, q1, r4);
    __ vdup(Neon32, q2, r4);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vdup8))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vdup16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vdup32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vdup (from scalar).
    __ vmov(s0, Float32(-1.0f));
    __ vdup(Neon32, q1, d0, 0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vdupf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vdup(Neon16, d2, d0, 1);
    __ vstr(d2, r0, offsetof(T, vdupf_16));
    __ vdup(Neon8, q1, d0, 3);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vdupf_8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vabs (float).
    __ vmov(s0, Float32(-1.0f));
    __ vmov(s1, Float32(-0.0f));
    __ vmov(s2, Float32(0.0f));
    __ vmov(s3, Float32(1.0f));
    __ vabs(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vabsf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vneg (float).
    __ vneg(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vnegf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vabs (integer).
    __ mov(r4, Operand(0x7F7F7F7F));
    __ mov(r5, Operand(0x01010101));
    __ vmov(d0, r4, r5);
    __ mov(r4, Operand(0xFFFFFFFF));
    __ mov(r5, Operand(0x80808080));
    __ vmov(d1, r4, r5);
    __ vabs(Neon8, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vabs_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vabs(Neon16, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vabs_s16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vabs(Neon32, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vabs_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vneg (integer).
    __ vneg(Neon8, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vneg_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vneg(Neon16, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vneg_s16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vneg(Neon32, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vneg_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // veor.
    __ mov(r4, Operand(0xAA));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x55));
    __ vdup(Neon16, q1, r4);
    __ veor(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, veor))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vand.
    __ mov(r4, Operand(0xFF));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0xFE));
    __ vdup(Neon16, q1, r4);
    __ vand(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vand))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vorr.
    __ mov(r4, Operand(0xAA));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x55));
    __ vdup(Neon16, q1, r4);
    __ vorr(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vorr))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vmin (float).
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vmov(s4, Float32(1.0f));
    __ vdup(Neon32, q1, d2, 0);
    __ vmin(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vminf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vmax (float).
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vmov(s4, Float32(1.0f));
    __ vdup(Neon32, q1, d2, 0);
    __ vmax(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmaxf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vadd (float).
    __ vmov(s4, Float32(1.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vdup(Neon32, q1, d2, 0);
    __ vadd(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vaddf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vpadd (float).
    __ vmov(s0, Float32(1.0f));
    __ vmov(s1, Float32(2.0f));
    __ vmov(s2, Float32(3.0f));
    __ vmov(s3, Float32(4.0f));
    __ vpadd(d2, d0, d1);
    __ vstr(d2, r0, offsetof(T, vpaddf));
    // vsub (float).
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vmov(s4, Float32(1.0f));
    __ vdup(Neon32, q1, d2, 0);
    __ vsub(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vsubf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vmul (float).
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vdup(Neon32, q1, d2, 0);
    __ vmul(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmulf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vrecpe.
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vrecpe(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrecpe))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vrecps.
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vmov(s4, Float32(1.5f));
    __ vdup(Neon32, q1, d2, 0);
    __ vrecps(q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrecps))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vrsqrte.
    __ vmov(s4, Float32(4.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vrsqrte(q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrsqrte))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vrsqrts.
    __ vmov(s4, Float32(2.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vmov(s4, Float32(2.5f));
    __ vdup(Neon32, q1, d2, 0);
    __ vrsqrts(q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrsqrts))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vceq (float).
    __ vmov(s4, Float32(1.0f));
    __ vdup(Neon32, q0, d2, 0);
    __ vdup(Neon32, q1, d2, 0);
    __ vceq(q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vceqf))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    // vcge (float).
    __ vmov(s0, Float32(1.0f));
    __ vmov(s1, Float32(-1.0f));
    __ vmov(s2, Float32(-0.0f));
    __ vmov(s3, Float32(0.0f));
    __ vdup(Neon32, q1, d1, 1);
    __ vcge(q2, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcgef))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vcgt(q2, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcgtf))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vmin/vmax integer.
    __ mov(r4, Operand(0x03));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vmin(NeonS8, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmin_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vmax(NeonS8, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmax_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ mov(r4, Operand(0xFF));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vmin(NeonU16, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmin_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vmax(NeonU16, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmax_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ mov(r4, Operand(0xFF));
    __ vdup(Neon32, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vmin(NeonS32, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmin_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vmax(NeonS32, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmax_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vpadd integer.
    __ mov(r4, Operand(0x03));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vpadd(Neon8, d0, d0, d2);
    __ vstr(d0, r0, offsetof(T, vpadd_i8));
    __ vpadd(Neon16, d0, d0, d2);
    __ vstr(d0, r0, offsetof(T, vpadd_i16));
    __ vpadd(Neon32, d0, d0, d2);
    __ vstr(d0, r0, offsetof(T, vpadd_i32));

    // vpadal signed.
    __ mov(r4, Operand(0x81));
    __ vdup(Neon8, q0, r4);

    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonS8, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonS16, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_s16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonS32, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vpadal unsigned.
    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonU8, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_u8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonU16, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q2, r4);
    __ vpadal(NeonU32, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpadal_u32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vpaddl signed.
    __ mov(r4, Operand(0x81));
    __ vdup(Neon8, q0, r4);

    __ vpaddl(NeonS8, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ vpaddl(NeonS16, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_s16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ vpaddl(NeonS32, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vpaddl unsigned.
    __ vpaddl(NeonU8, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_u8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ vpaddl(NeonU16, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    __ vpaddl(NeonU32, q2, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vpaddl_u32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vpmin/vpmax integer.
    __ mov(r4, Operand(0x03));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vpmin(NeonS8, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmin_s8));
    __ vpmax(NeonS8, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmax_s8));
    __ mov(r4, Operand(0xFFFF));
    __ vdup(Neon32, q0, r4);
    __ vdup(Neon16, q1, r4);
    __ vpmin(NeonU16, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmin_u16));
    __ vpmax(NeonU16, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmax_u16));
    __ mov(r4, Operand(0xFF));
    __ veor(q0, q0, q0);
    __ vmov(s0, r4);
    __ vdup(Neon8, q1, r4);
    __ vpmin(NeonS32, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmin_s32));
    __ vpmax(NeonS32, d4, d0, d2);
    __ vstr(d4, r0, offsetof(T, vpmax_s32));

    // vadd (integer).
    __ mov(r4, Operand(0x81));
    __ vdup(Neon8, q0, r4);
    __ mov(r4, Operand(0x82));
    __ vdup(Neon8, q1, r4);
    __ vadd(Neon8, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vadd8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x8001));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x8002));
    __ vdup(Neon16, q1, r4);
    __ vadd(Neon16, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vadd16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x80000001));
    __ vdup(Neon32, q0, r4);
    __ mov(r4, Operand(0x80000002));
    __ vdup(Neon32, q1, r4);
    __ vadd(Neon32, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vadd32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vqadd.
    __ mov(r4, Operand(0x81));
    __ vdup(Neon8, q0, r4);
    __ mov(r4, Operand(0x82));
    __ vdup(Neon8, q1, r4);
    __ vqadd(NeonS8, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqadd_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x8000));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon16, q1, r4);
    __ vqadd(NeonU16, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqadd_u16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x80000001));
    __ vdup(Neon32, q0, r4);
    __ mov(r4, Operand(0x80000002));
    __ vdup(Neon32, q1, r4);
    __ vqadd(NeonS32, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqadd_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vsub (integer).
    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q0, r4);
    __ mov(r4, Operand(0x03));
    __ vdup(Neon8, q1, r4);
    __ vsub(Neon8, q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vsub8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x0001));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x0003));
    __ vdup(Neon16, q1, r4);
    __ vsub(Neon16, q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vsub16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x00000001));
    __ vdup(Neon32, q0, r4);
    __ mov(r4, Operand(0x00000003));
    __ vdup(Neon32, q1, r4);
    __ vsub(Neon32, q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vsub32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vqsub.
    __ mov(r4, Operand(0x7F));
    __ vdup(Neon8, q0, r4);
    __ mov(r4, Operand(0x3F));
    __ vdup(Neon8, q1, r4);
    __ vqsub(NeonU8, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqsub_u8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x8000));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x7FFF));
    __ vdup(Neon16, q1, r4);
    __ vqsub(NeonS16, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqsub_s16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x80000001));
    __ vdup(Neon32, q0, r4);
    __ mov(r4, Operand(0x80000000));
    __ vdup(Neon32, q1, r4);
    __ vqsub(NeonU32, q1, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vqsub_u32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vmul (integer).
    __ mov(r4, Operand(0x02));
    __ vdup(Neon8, q0, r4);
    __ vmul(Neon8, q1, q0, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmul8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x0002));
    __ vdup(Neon16, q0, r4);
    __ vmul(Neon16, q1, q0, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmul16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ mov(r4, Operand(0x00000002));
    __ vdup(Neon32, q0, r4);
    __ vmul(Neon32, q1, q0, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vmul32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vshl.
    __ mov(r4, Operand(0x55));
    __ vdup(Neon8, q0, r4);
    __ vshl(NeonS8, q1, q0, 1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshl8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vshl(NeonU16, q1, q0, 9);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshl16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vshl(NeonS32, q1, q0, 17);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshl32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vshr.s, vshr.u.
    __ mov(r4, Operand(0x80));
    __ vdup(Neon8, q0, r4);
    __ vshr(NeonS8, q1, q0, 1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vshr(NeonU16, q1, q0, 9);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_u16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vshr(NeonS32, q1, q0, 17);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vshr.s, vshr.u with d registers.
    __ mov(r4, Operand(0x80));
    __ vdup(Neon8, q0, r4);
    __ vshr(NeonS8, d1, d0, 1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_s8_d))));
    __ vst1(Neon8, NeonListOperand(d1), NeonMemOperand(r4));
    __ vshr(NeonU16, d2, d0, 9);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_u16_d))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vshr(NeonS32, d2, d0, 17);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vshr_s32_d))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vsli, vsri.
    __ mov(r4, Operand(0xFFFFFFFF));
    __ mov(r5, Operand(0x1));
    __ vmov(d0, r4, r5);
    __ vmov(d1, r5, r5);
    __ vsli(Neon64, d1, d0, 32);
    __ vstr(d1, r0, offsetof(T, vsli_64));
    __ vmov(d0, r5, r4);
    __ vmov(d1, r5, r5);
    __ vsri(Neon64, d1, d0, 32);
    __ vstr(d1, r0, offsetof(T, vsri_64));
    __ vmov(d0, r4, r5);
    __ vmov(d1, r5, r5);
    __ vsli(Neon32, d1, d0, 16);
    __ vstr(d1, r0, offsetof(T, vsli_32));
    __ vmov(d0, r5, r4);
    __ vmov(d1, r5, r5);
    __ vsri(Neon32, d1, d0, 16);
    __ vstr(d1, r0, offsetof(T, vsri_32));

    // vsra.
    __ vmov(d0, r4, r5);
    // Check same dst and src registers.
    __ vsra(NeonU64, d0, d0, 1);
    __ vstr(d0, r0, offsetof(T, vsra_64));
    __ vmov(d0, r4, r5);
    __ vmov(d1, r5, r4);
    __ vsra(NeonS32, d1, d0, 16);
    __ vstr(d1, r0, offsetof(T, vsra_32));
    __ vmov(d0, r4, r5);
    __ vmov(d1, r5, r4);
    __ vsra(NeonU16, d1, d0, 2);
    __ vstr(d1, r0, offsetof(T, vsra_16));

    // vceq.
    __ mov(r4, Operand(0x03));
    __ vdup(Neon8, q0, r4);
    __ vdup(Neon16, q1, r4);
    __ vceq(Neon8, q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vceq))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vcge/vcgt (integer).
    __ mov(r4, Operand(0x03));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vcge(NeonS8, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcge_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vcgt(NeonS8, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcgt_s8))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ mov(r4, Operand(0xFF));
    __ vdup(Neon16, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vcge(NeonU16, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcge_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vcgt(NeonU16, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcgt_u16))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ mov(r4, Operand(0xFF));
    __ vdup(Neon32, q0, r4);
    __ vdup(Neon8, q1, r4);
    __ vcge(NeonS32, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcge_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));
    __ vcgt(NeonS32, q2, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vcgt_s32))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vclt #0 (signed integer).
    __ mov(r4, Operand(0x7F80807F));
    __ mov(r5, Operand(0xFF01FF01));
    __ vmov(d0, r4, r5);
    __ mov(r4, Operand(0xFF0000FF));
    __ mov(r5, Operand(0x00800080));
    __ vmov(d1, r4, r5);

    __ vclt(Neon8, q1, q0, 0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vclt0_s8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    __ vclt(Neon16, q1, q0, 0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vclt0_s16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    __ vclt(Neon32, q1, q0, 0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vclt0_s32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vtst.
    __ mov(r4, Operand(0x03));
    __ vdup(Neon8, q0, r4);
    __ mov(r4, Operand(0x02));
    __ vdup(Neon16, q1, r4);
    __ vtst(Neon8, q1, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtst))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vbsl.
    __ mov(r4, Operand(0x00FF));
    __ vdup(Neon16, q0, r4);
    __ mov(r4, Operand(0x01));
    __ vdup(Neon8, q1, r4);
    __ mov(r4, Operand(0x02));
    __ vdup(Neon8, q2, r4);
    __ vbsl(q0, q1, q2);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vbsl))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));

    // vext.
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vext(q2, q0, q1, 3);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vext))));
    __ vst1(Neon8, NeonListOperand(q2), NeonMemOperand(r4));

    // vzip (q-register).
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vzip(Neon8, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip8a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip8b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vzip(Neon16, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip16a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip16b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vzip(Neon32, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip32a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vzip32b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vzip (d-register).
    __ vldr(d2, r0, offsetof(T, lane_test));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vzip(Neon8, d0, d1);
    __ vstr(d0, r0, offsetof(T, vzipd8a));
    __ vstr(d1, r0, offsetof(T, vzipd8b));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vzip(Neon16, d0, d1);
    __ vstr(d0, r0, offsetof(T, vzipd16a));
    __ vstr(d1, r0, offsetof(T, vzipd16b));

    // vuzp (q-register).
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vuzp(Neon8, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp8a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp8b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vuzp(Neon16, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp16a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp16b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vuzp(Neon32, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp32a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vuzp32b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vuzp (d-register).
    __ vldr(d2, r0, offsetof(T, lane_test));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vuzp(Neon8, d0, d1);
    __ vstr(d0, r0, offsetof(T, vuzpd8a));
    __ vstr(d1, r0, offsetof(T, vuzpd8b));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vuzp(Neon16, d0, d1);
    __ vstr(d0, r0, offsetof(T, vuzpd16a));
    __ vstr(d1, r0, offsetof(T, vuzpd16b));

    // vtrn (q-register).
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vtrn(Neon8, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn8a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn8b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vtrn(Neon16, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn16a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn16b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vmov(q1, q0);
    __ vtrn(Neon32, q0, q1);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn32a))));
    __ vst1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vtrn32b))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vtrn (d-register).
    __ vldr(d2, r0, offsetof(T, lane_test));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vtrn(Neon8, d0, d1);
    __ vstr(d0, r0, offsetof(T, vtrnd8a));
    __ vstr(d1, r0, offsetof(T, vtrnd8b));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vtrn(Neon16, d0, d1);
    __ vstr(d0, r0, offsetof(T, vtrnd16a));
    __ vstr(d1, r0, offsetof(T, vtrnd16b));
    __ vmov(d0, d2);
    __ vmov(d1, d2);
    __ vtrn(Neon32, d0, d1);
    __ vstr(d0, r0, offsetof(T, vtrnd32a));
    __ vstr(d1, r0, offsetof(T, vtrnd32b));

    // vrev64/32/16
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, lane_test))));
    __ vld1(Neon8, NeonListOperand(q0), NeonMemOperand(r4));
    __ vrev64(Neon32, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev64_32))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vrev64(Neon16, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev64_16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vrev64(Neon8, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev64_8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vrev32(Neon16, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev32_16))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vrev32(Neon8, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev32_8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));
    __ vrev16(Neon8, q1, q0);
    __ add(r4, r0, Operand(static_cast<int32_t>(offsetof(T, vrev16_8))));
    __ vst1(Neon8, NeonListOperand(q1), NeonMemOperand(r4));

    // vtb[l/x].
    __ mov(r4, Operand(0x06040200));
    __ mov(r5, Operand(0xFF050301));
    __ vmov(d2, r4, r5);  // d2 = ff05030106040200
    __ vtbl(d0, NeonListOperand(d2, 1), d2);
    __ vstr(d0, r0, offsetof(T, vtbl));
    __ vtbx(d2, NeonListOperand(d2, 1), d2);
    __ vstr(d2, r0, offsetof(T, vtbx));

    // Restore and return.
    __ ldm(ia_w, sp, {r4, r5, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    t.src0 = 0x01020304;
    t.src1 = 0x11121314;
    t.src2 = 0x21222324;
    t.src3 = 0x31323334;
    t.src4 = 0x41424344;
    t.src5 = 0x51525354;
    t.src6 = 0x61626364;
    t.src7 = 0x71727374;
    t.dst0 = 0;
    t.dst1 = 0;
    t.dst2 = 0;
    t.dst3 = 0;
    t.dst4 = 0;
    t.dst5 = 0;
    t.dst6 = 0;
    t.dst7 = 0;
    t.srcA0 = 0x41424344;
    t.srcA1 = 0x81828384;
    t.dstA0 = 0;
    t.dstA1 = 0;
    t.dstA2 = 0;
    t.dstA3 = 0;
    t.lane_test[0] = 0x03020100;
    t.lane_test[1] = 0x07060504;
    t.lane_test[2] = 0x0B0A0908;
    t.lane_test[3] = 0x0F0E0D0C;
    f.Call(&t, 0, 0, 0, 0);

    CHECK_EQ(0x01020304u, t.dst0);
    CHECK_EQ(0x11121314u, t.dst1);
    CHECK_EQ(0x21222324u, t.dst2);
    CHECK_EQ(0x31323334u, t.dst3);
    CHECK_EQ(0x41424344u, t.dst4);
    CHECK_EQ(0x51525354u, t.dst5);
    CHECK_EQ(0x61626364u, t.dst6);
    CHECK_EQ(0x71727374u, t.dst7);
    CHECK_EQ(0x00430044u, t.dstA0);
    CHECK_EQ(0x00410042u, t.dstA1);
    CHECK_EQ(0x00830084u, t.dstA2);
    CHECK_EQ(0x00810082u, t.dstA3);

    CHECK_EQ_32X4(vmovl_s8, 0x00430044u, 0x00410042u, 0xFF83FF84u, 0xFF81FF82u);
    CHECK_EQ_32X4(vmovl_u16, 0xFF84u, 0xFF83u, 0xFF82u, 0xFF81u);
    CHECK_EQ_32X4(vmovl_s32, 0xFF84u, 0x0u, 0xFF83u, 0x0u);
    CHECK_EQ_32X2(vqmovn_u16, 0xFF83FF84u, 0xFF81FF82u);
    CHECK_EQ_32X2(vqmovn_s8, 0x81828384u, 0x81828384u);
    CHECK_EQ_32X2(vqmovn_s32, 0xFF84u, 0xFF83u);

    CHECK_EQ(0xFFFFFFF8FFF8F800u, t.vmov_to_scalar1);
    CHECK_EQ(0xFFF80000F8000000u, t.vmov_to_scalar2);
    CHECK_EQ(0xFFFFFFFFu, t.vmov_from_scalar_s8);
    CHECK_EQ(0xFFu, t.vmov_from_scalar_u8);
    CHECK_EQ(0xFFFFFFFFu, t.vmov_from_scalar_s16);
    CHECK_EQ(0xFFFFu, t.vmov_from_scalar_u16);
    CHECK_EQ(0xFFFFFFFFu, t.vmov_from_scalar_32);

    CHECK_EQ_32X4(vmov, 0x03020100u, 0x07060504u, 0x0B0A0908u, 0x0F0E0D0Cu);
    CHECK_EQ_32X4(vmvn, 0xFCFDFEFFu, 0xF8F9FAFBu, 0xF4F5F6F7u, 0xF0F1F2F3u);

    CHECK_EQ_SPLAT(vdup8, 0x0A0A0A0Au);
    CHECK_EQ_SPLAT(vdup16, 0x000A000Au);
    CHECK_EQ_SPLAT(vdup32, 0x0000000Au);
    CHECK_EQ_SPLAT(vdupf, -1.0);  // bit pattern is 0xBF800000.
    CHECK_EQ_32X2(vdupf_16, 0xBF80BF80u, 0xBF80BF80u);
    CHECK_EQ_SPLAT(vdupf_8, 0xBFBFBFBFu);

    // src: [-1, -1, 1, 1]
    CHECK_EQ_32X4(vcvt_s32_f32, -1, -1, 1, 1);
    CHECK_EQ_32X4(vcvt_u32_f32, 0u, 0u, 1u, 1u);
    // src: [kMinInt, kMaxInt, kMaxUInt32, kMinInt + 1]
    CHECK_EQ_32X4(vcvt_f32_s32, INT32_TO_FLOAT(kMinInt),
                  INT32_TO_FLOAT(kMaxInt), INT32_TO_FLOAT(kMaxUInt32),
                  INT32_TO_FLOAT(kMinInt + 1));
    CHECK_EQ_32X4(vcvt_f32_u32, UINT32_TO_FLOAT(kMinInt),
                  UINT32_TO_FLOAT(kMaxInt), UINT32_TO_FLOAT(kMaxUInt32),
                  UINT32_TO_FLOAT(kMinInt + 1));

    CHECK_EQ_32X4(vclt0_s8, 0x00FFFF00u, 0xFF00FF00u, 0xFF0000FFu, 0x00FF00FFu);
    CHECK_EQ_32X4(vclt0_s16, 0x0000FFFF, 0xFFFFFFFFu, 0xFFFF0000u, 0x00000000u);
    CHECK_EQ_32X4(vclt0_s32, 0x00000000u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                  0x00000000u);

    CHECK_EQ_32X4(vabsf, 1.0, 0.0, 0.0, 1.0);
    CHECK_EQ_32X4(vnegf, 1.0, 0.0, -0.0, -1.0);
    // src: [0x7F7F7F7F, 0x01010101, 0xFFFFFFFF, 0x80808080]
    CHECK_EQ_32X4(vabs_s8, 0x7F7F7F7Fu, 0x01010101u, 0x01010101u, 0x80808080u);
    CHECK_EQ_32X4(vabs_s16, 0x7F7F7F7Fu, 0x01010101u, 0x00010001u, 0x7F807F80u);
    CHECK_EQ_32X4(vabs_s32, 0x7F7F7F7Fu, 0x01010101u, 0x00000001u, 0x7F7F7F80u);
    CHECK_EQ_32X4(vneg_s8, 0x81818181u, 0xFFFFFFFFu, 0x01010101u, 0x80808080u);
    CHECK_EQ_32X4(vneg_s16, 0x80818081u, 0xFEFFFEFFu, 0x00010001u, 0x7F807F80u);
    CHECK_EQ_32X4(vneg_s32, 0x80808081u, 0xFEFEFEFFu, 0x00000001u, 0x7F7F7F80u);

    CHECK_EQ_SPLAT(veor, 0x00FF00FFu);
    CHECK_EQ_SPLAT(vand, 0x00FE00FEu);
    CHECK_EQ_SPLAT(vorr, 0x00FF00FFu);
    CHECK_EQ_SPLAT(vaddf, 2.0);
    CHECK_EQ_32X2(vpaddf, 3.0, 7.0);
    CHECK_EQ_SPLAT(vminf, 1.0);
    CHECK_EQ_SPLAT(vmaxf, 2.0);
    CHECK_EQ_SPLAT(vsubf, -1.0);
    CHECK_EQ_SPLAT(vmulf, 4.0);
    CHECK_ESTIMATE_SPLAT(vrecpe, 0.5f, 0.1f);  // 1 / 2
    CHECK_EQ_SPLAT(vrecps, -1.0f);   // 2 - (2 * 1.5)
    CHECK_ESTIMATE_SPLAT(vrsqrte, 0.5f, 0.1f);  // 1 / sqrt(4)
    CHECK_EQ_SPLAT(vrsqrts, -1.0f);  // (3 - (2 * 2.5)) / 2
    CHECK_EQ_SPLAT(vceqf, 0xFFFFFFFFu);
    // [0] >= [-1, 1, -0, 0]
    CHECK_EQ_32X4(vcgef, 0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu);
    CHECK_EQ_32X4(vcgtf, 0u, 0xFFFFFFFFu, 0u, 0u);
    // [0, 3, 0, 3, ...] and [3, 3, 3, 3, ...]
    CHECK_EQ_SPLAT(vmin_s8, 0x00030003u);
    CHECK_EQ_SPLAT(vmax_s8, 0x03030303u);
    // [0x00FF, 0x00FF, ...] and [0xFFFF, 0xFFFF, ...]
    CHECK_EQ_SPLAT(vmin_u16, 0x00FF00FFu);
    CHECK_EQ_SPLAT(vmax_u16, 0xFFFFFFFFu);
    // [0x000000FF, 0x000000FF, ...] and [0xFFFFFFFF, 0xFFFFFFFF, ...]
    CHECK_EQ_SPLAT(vmin_s32, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vmax_s32, 0xFFu);
    // [0, 3, 0, 3, ...] and [3, 3, 3, 3, ...]
    CHECK_EQ_32X2(vpadd_i8, 0x03030303u, 0x06060606u);
    CHECK_EQ_32X2(vpadd_i16, 0x0C0C0606u, 0x06060606u);
    CHECK_EQ_32X2(vpadd_i32, 0x12120C0Cu, 0x06060606u);

    CHECK_EQ_32X4(vpadal_s8, 0x30003, 0x30003, 0x30003, 0x30003);
    CHECK_EQ_32X4(vpadal_s16, 0x1000403, 0x1000403, 0x1000403, 0x1000403);
    CHECK_EQ_32X4(vpadal_s32, 0x4040403, 0x1010100, 0x4040403, 0x1010100);

    CHECK_EQ_32X4(vpadal_u8, 0x2030203, 0x2030203, 0x2030203, 0x2030203);
    CHECK_EQ_32X4(vpadal_u16, 0x1020403, 0x1020403, 0x1020403, 0x1020403);
    CHECK_EQ_32X4(vpadal_u32, 0x4040403, 0x1010102, 0x4040403, 0x1010102);

    CHECK_EQ_32X4(vpaddl_s8, 0xFF02FF02, 0xFF02FF02, 0xFF02FF02, 0xFF02FF02);
    CHECK_EQ_32X4(vpaddl_s16, 0xFFFF0302, 0xFFFF0302, 0xFFFF0302, 0xFFFF0302);
    CHECK_EQ_32X4(vpaddl_s32, 0x03030302, 0xFFFFFFFF, 0x03030302, 0xFFFFFFFF);

    CHECK_EQ_32X4(vpaddl_u8, 0x01020102, 0x01020102, 0x01020102, 0x01020102);
    CHECK_EQ_32X4(vpaddl_u16, 0x00010302, 0x00010302, 0x00010302, 0x00010302);
    CHECK_EQ_32X4(vpaddl_u32, 0x03030302, 0x00000001, 0x03030302, 0x00000001);

    CHECK_EQ_32X2(vpmin_s8, 0x00000000u, 0x03030303u);
    CHECK_EQ_32X2(vpmax_s8, 0x03030303u, 0x03030303u);
    // [0, ffff, 0, ffff] and [ffff, ffff]
    CHECK_EQ_32X2(vpmin_u16, 0x00000000u, 0xFFFFFFFFu);
    CHECK_EQ_32X2(vpmax_u16, 0xFFFFFFFFu, 0xFFFFFFFFu);
    // [0x000000FF, 0x00000000u] and [0xFFFFFFFF, 0xFFFFFFFF, ...]
    CHECK_EQ_32X2(vpmin_s32, 0x00u, 0xFFFFFFFFu);
    CHECK_EQ_32X2(vpmax_s32, 0xFFu, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vadd8, 0x03030303u);
    CHECK_EQ_SPLAT(vadd16, 0x00030003u);
    CHECK_EQ_SPLAT(vadd32, 0x00000003u);
    CHECK_EQ_SPLAT(vqadd_s8, 0x80808080u);
    CHECK_EQ_SPLAT(vqadd_u16, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vqadd_s32, 0x80000000u);
    CHECK_EQ_SPLAT(vqsub_u8, 0x00000000u);
    CHECK_EQ_SPLAT(vqsub_s16, 0x7FFF7FFFu);
    CHECK_EQ_SPLAT(vqsub_u32, 0x00000000u);
    CHECK_EQ_SPLAT(vsub8, 0xFEFEFEFEu);
    CHECK_EQ_SPLAT(vsub16, 0xFFFEFFFEu);
    CHECK_EQ_SPLAT(vsub32, 0xFFFFFFFEu);
    CHECK_EQ_SPLAT(vmul8, 0x04040404u);
    CHECK_EQ_SPLAT(vmul16, 0x00040004u);
    CHECK_EQ_SPLAT(vmul32, 0x00000004u);
    CHECK_EQ_SPLAT(vshl8, 0xAAAAAAAAu);
    CHECK_EQ_SPLAT(vshl16, 0xAA00AA00u);
    CHECK_EQ_SPLAT(vshl32, 0xAAAA0000u);
    CHECK_EQ_SPLAT(vshr_s8, 0xC0C0C0C0u);
    CHECK_EQ_SPLAT(vshr_u16, 0x00400040u);
    CHECK_EQ_SPLAT(vshr_s32, 0xFFFFC040u);
    CHECK_EQ_32X2(vshr_s8_d, 0xC0C0C0C0u, 0xC0C0C0C0u);
    CHECK_EQ_32X2(vshr_u16_d, 0x00400040u, 0x00400040u);
    CHECK_EQ_32X2(vshr_s32_d, 0xFFFFC040u, 0xFFFFC040u);
    CHECK_EQ_32X2(vsli_64, 0x01u, 0xFFFFFFFFu);
    CHECK_EQ_32X2(vsri_64, 0xFFFFFFFFu, 0x01u);
    CHECK_EQ_32X2(vsli_32, 0xFFFF0001u, 0x00010001u);
    CHECK_EQ_32X2(vsri_32, 0x00000000u, 0x0000FFFFu);
    CHECK_EQ_32X2(vsra_64, 0xFFFFFFFEu, 0x2);
    CHECK_EQ_32X2(vsra_32, 0x0, 0xFFFFFFFFu);
    CHECK_EQ_32X2(vsra_16, 0x3FFF4000, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vceq, 0x00FF00FFu);
    // [0, 3, 0, 3, ...] >= [3, 3, 3, 3, ...]
    CHECK_EQ_SPLAT(vcge_s8, 0x00FF00FFu);
    CHECK_EQ_SPLAT(vcgt_s8, 0u);
    // [0x00FF, 0x00FF, ...] >= [0xFFFF, 0xFFFF, ...]
    CHECK_EQ_SPLAT(vcge_u16, 0u);
    CHECK_EQ_SPLAT(vcgt_u16, 0u);
    // [0x000000FF, 0x000000FF, ...] >= [0xFFFFFFFF, 0xFFFFFFFF, ...]
    CHECK_EQ_SPLAT(vcge_s32, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vcgt_s32, 0xFFFFFFFFu);
    CHECK_EQ_SPLAT(vtst, 0x00FF00FFu);
    CHECK_EQ_SPLAT(vbsl, 0x02010201u);

    CHECK_EQ_32X4(vext, 0x06050403u, 0x0A090807u, 0x0E0D0C0Bu, 0x0201000Fu);

    CHECK_EQ_32X4(vzip8a, 0x01010000u, 0x03030202u, 0x05050404u, 0x07070606u);
    CHECK_EQ_32X4(vzip8b, 0x09090808u, 0x0B0B0A0Au, 0x0D0D0C0Cu, 0x0F0F0E0Eu);
    CHECK_EQ_32X4(vzip16a, 0x01000100u, 0x03020302u, 0x05040504u, 0x07060706u);
    CHECK_EQ_32X4(vzip16b, 0x09080908u, 0x0B0A0B0Au, 0x0D0C0D0Cu, 0x0F0E0F0Eu);
    CHECK_EQ_32X4(vzip32a, 0x03020100u, 0x03020100u, 0x07060504u, 0x07060504u);
    CHECK_EQ_32X4(vzip32b, 0x0B0A0908u, 0x0B0A0908u, 0x0F0E0D0Cu, 0x0F0E0D0Cu);

    CHECK_EQ_32X2(vzipd8a, 0x01010000u, 0x03030202u);
    CHECK_EQ_32X2(vzipd8b, 0x05050404u, 0x07070606u);
    CHECK_EQ_32X2(vzipd16a, 0x01000100u, 0x03020302u);
    CHECK_EQ_32X2(vzipd16b, 0x05040504u, 0x07060706u);

    CHECK_EQ_32X4(vuzp8a, 0x06040200u, 0x0E0C0A08u, 0x06040200u, 0x0E0C0A08u);
    CHECK_EQ_32X4(vuzp8b, 0x07050301u, 0x0F0D0B09u, 0x07050301u, 0x0F0D0B09u);
    CHECK_EQ_32X4(vuzp16a, 0x05040100u, 0x0D0C0908u, 0x05040100u, 0x0D0C0908u);
    CHECK_EQ_32X4(vuzp16b, 0x07060302u, 0x0F0E0B0Au, 0x07060302u, 0x0F0E0B0Au);
    CHECK_EQ_32X4(vuzp32a, 0x03020100u, 0x0B0A0908u, 0x03020100u, 0x0B0A0908u);
    CHECK_EQ_32X4(vuzp32b, 0x07060504u, 0x0F0E0D0Cu, 0x07060504u, 0x0F0E0D0Cu);

    CHECK_EQ_32X2(vuzpd8a, 0x06040200u, 0x06040200u);
    CHECK_EQ_32X2(vuzpd8b, 0x07050301u, 0x07050301u);
    CHECK_EQ_32X2(vuzpd16a, 0x05040100u, 0x05040100u);
    CHECK_EQ_32X2(vuzpd16b, 0x07060302u, 0x07060302u);

    CHECK_EQ_32X4(vtrn8a, 0x02020000u, 0x06060404u, 0x0A0A0808u, 0x0E0E0C0Cu);
    CHECK_EQ_32X4(vtrn8b, 0x03030101u, 0x07070505u, 0x0B0B0909u, 0x0F0F0D0Du);
    CHECK_EQ_32X4(vtrn16a, 0x01000100u, 0x05040504u, 0x09080908u, 0x0D0C0D0Cu);
    CHECK_EQ_32X4(vtrn16b, 0x03020302u, 0x07060706u, 0x0B0A0B0Au, 0x0F0E0F0Eu);
    CHECK_EQ_32X4(vtrn32a, 0x03020100u, 0x03020100u, 0x0B0A0908u, 0x0B0A0908u);
    CHECK_EQ_32X4(vtrn32b, 0x07060504u, 0x07060504u, 0x0F0E0D0Cu, 0x0F0E0D0Cu);

    CHECK_EQ_32X2(vtrnd8a, 0x02020000u, 0x06060404u);
    CHECK_EQ_32X2(vtrnd8b, 0x03030101u, 0x07070505u);
    CHECK_EQ_32X2(vtrnd16a, 0x01000100u, 0x05040504u);
    CHECK_EQ_32X2(vtrnd16b, 0x03020302u, 0x07060706u);
    CHECK_EQ_32X2(vtrnd32a, 0x03020100u, 0x03020100u);
    CHECK_EQ_32X2(vtrnd32b, 0x07060504u, 0x07060504u);

    // src: 0 1 2 3  4 5 6 7  8 9 a b  c d e f (little endian)
    CHECK_EQ_32X4(vrev64_32, 0x07060504u, 0x03020100u, 0x0F0E0D0Cu,
                  0x0B0A0908u);
    CHECK_EQ_32X4(vrev64_16, 0x05040706u, 0x01000302u, 0x0D0C0F0Eu,
                  0x09080B0Au);
    CHECK_EQ_32X4(vrev64_8, 0x04050607u, 0x00010203u, 0x0C0D0E0Fu, 0x08090A0Bu);
    CHECK_EQ_32X4(vrev32_16, 0x01000302u, 0x05040706u, 0x09080B0Au,
                  0x0D0C0F0Eu);
    CHECK_EQ_32X4(vrev32_8, 0x00010203u, 0x04050607u, 0x08090A0Bu, 0x0C0D0E0Fu);
    CHECK_EQ_32X4(vrev16_8, 0x02030001u, 0x06070405u, 0x0A0B0809u, 0x0E0F0C0Du);

    CHECK_EQ(0x05010400u, t.vtbl[0]);
    CHECK_EQ(0x00030602u, t.vtbl[1]);
    CHECK_EQ(0x05010400u, t.vtbx[0]);
    CHECK_EQ(0xFF030602u, t.vtbx[1]);
  }
}

TEST(16) {
  // Test the pkh, uxtb, uxtab and uxtb16 instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint32_t src0;
    uint32_t src1;
    uint32_t src2;
    uint32_t dst0;
    uint32_t dst1;
    uint32_t dst2;
    uint32_t dst3;
    uint32_t dst4;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles and floats.
  Assembler assm(AssemblerOptions{});

  __ stm(db_w, sp, {r4, lr});

  __ mov(r4, Operand(r0));
  __ ldr(r0, MemOperand(r4, offsetof(T, src0)));
  __ ldr(r1, MemOperand(r4, offsetof(T, src1)));

  __ pkhbt(r2, r0, Operand(r1, LSL, 8));
  __ str(r2, MemOperand(r4, offsetof(T, dst0)));

  __ pkhtb(r2, r0, Operand(r1, ASR, 8));
  __ str(r2, MemOperand(r4, offsetof(T, dst1)));

  __ uxtb16(r2, r0, 8);
  __ str(r2, MemOperand(r4, offsetof(T, dst2)));

  __ uxtb(r2, r0, 8);
  __ str(r2, MemOperand(r4, offsetof(T, dst3)));

  __ ldr(r0, MemOperand(r4, offsetof(T, src2)));
  __ uxtab(r2, r0, r1, 8);
  __ str(r2, MemOperand(r4, offsetof(T, dst4)));

  __ ldm(ia_w, sp, {r4, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  t.src0 = 0x01020304;
  t.src1 = 0x11121314;
  t.src2 = 0x11121300;
  t.dst0 = 0;
  t.dst1 = 0;
  t.dst2 = 0;
  t.dst3 = 0;
  t.dst4 = 0;
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(0x12130304u, t.dst0);
  CHECK_EQ(0x01021213u, t.dst1);
  CHECK_EQ(0x00010003u, t.dst2);
  CHECK_EQ(0x00000003u, t.dst3);
  CHECK_EQ(0x11121313u, t.dst4);
}


TEST(17) {
  // Test generating labels at high addresses.
  // Should not assert.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  // Generate a code segment that will be longer than 2^24 bytes.
  Assembler assm(AssemblerOptions{});
  for (size_t i = 0; i < 1 << 23 ; ++i) {  // 2^23
    __ nop();
  }

  Label target;
  __ b(eq, &target);
  __ bind(&target);
  __ nop();
}

#define TEST_SDIV(expected_, dividend_, divisor_) \
  t.dividend = dividend_;                         \
  t.divisor = divisor_;                           \
  t.result = 0;                                   \
  f.Call(&t, 0, 0, 0, 0);                         \
  CHECK_EQ(expected_, t.result);

TEST(sdiv) {
  // Test the sdiv.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  struct T {
    int32_t dividend;
    int32_t divisor;
    int32_t result;
  } t;

  if (CpuFeatures::IsSupported(SUDIV)) {
    CpuFeatureScope scope(&assm, SUDIV);

    __ mov(r3, Operand(r0));

    __ ldr(r0, MemOperand(r3, offsetof(T, dividend)));
    __ ldr(r1, MemOperand(r3, offsetof(T, divisor)));

    __ sdiv(r2, r0, r1);
    __ str(r2, MemOperand(r3, offsetof(T, result)));

  __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    TEST_SDIV(0, kMinInt, 0);
    TEST_SDIV(0, 1024, 0);
    TEST_SDIV(1073741824, kMinInt, -2);
    TEST_SDIV(kMinInt, kMinInt, -1);
    TEST_SDIV(5, 10, 2);
    TEST_SDIV(3, 10, 3);
    TEST_SDIV(-5, 10, -2);
    TEST_SDIV(-3, 10, -3);
    TEST_SDIV(-5, -10, 2);
    TEST_SDIV(-3, -10, 3);
    TEST_SDIV(5, -10, -2);
    TEST_SDIV(3, -10, -3);
  }
}


#undef TEST_SDIV

#define TEST_UDIV(expected_, dividend_, divisor_) \
  t.dividend = dividend_;                         \
  t.divisor = divisor_;                           \
  t.result = 0;                                   \
  f.Call(&t, 0, 0, 0, 0);                         \
  CHECK_EQ(expected_, t.result);

TEST(udiv) {
  // Test the udiv.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  struct T {
    uint32_t dividend;
    uint32_t divisor;
    uint32_t result;
  } t;

  if (CpuFeatures::IsSupported(SUDIV)) {
    CpuFeatureScope scope(&assm, SUDIV);

    __ mov(r3, Operand(r0));

    __ ldr(r0, MemOperand(r3, offsetof(T, dividend)));
    __ ldr(r1, MemOperand(r3, offsetof(T, divisor)));

    __ sdiv(r2, r0, r1);
    __ str(r2, MemOperand(r3, offsetof(T, result)));

    __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    TEST_UDIV(0u, 0, 0);
    TEST_UDIV(0u, 1024, 0);
    TEST_UDIV(5u, 10, 2);
    TEST_UDIV(3u, 10, 3);
  }
}


#undef TEST_UDIV


TEST(smmla) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ smmla(r1, r1, r2, r3);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt(), z = rng->NextInt();
    f.Call(&r, x, y, z, 0);
    CHECK_EQ(base::bits::SignedMulHighAndAdd32(x, y, z), r);
  }
}


TEST(smmul) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ smmul(r1, r1, r2);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt();
    f.Call(&r, x, y, 0, 0);
    CHECK_EQ(base::bits::SignedMulHigh32(x, y), r);
  }
}


TEST(sxtb) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ sxtb(r1, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt();
    f.Call(&r, x, 0, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<int8_t>(x)), r);
  }
}


TEST(sxtab) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ sxtab(r1, r2, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt();
    f.Call(&r, x, y, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<int8_t>(x)) + y, r);
  }
}


TEST(sxth) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ sxth(r1, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt();
    f.Call(&r, x, 0, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<int16_t>(x)), r);
  }
}


TEST(sxtah) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ sxtah(r1, r2, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt();
    f.Call(&r, x, y, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<int16_t>(x)) + y, r);
  }
}


TEST(uxtb) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ uxtb(r1, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt();
    f.Call(&r, x, 0, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<uint8_t>(x)), r);
  }
}


TEST(uxtab) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ uxtab(r1, r2, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt();
    f.Call(&r, x, y, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<uint8_t>(x)) + y, r);
  }
}


TEST(uxth) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ uxth(r1, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt();
    f.Call(&r, x, 0, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<uint16_t>(x)), r);
  }
}


TEST(uxtah) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  RandomNumberGenerator* const rng = isolate->random_number_generator();
  Assembler assm(AssemblerOptions{});
  __ uxtah(r1, r2, r1);
  __ str(r1, MemOperand(r0));
  __ bx(lr);
  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  for (size_t i = 0; i < 128; ++i) {
    int32_t r, x = rng->NextInt(), y = rng->NextInt();
    f.Call(&r, x, y, 0, 0);
    CHECK_EQ(static_cast<int32_t>(static_cast<uint16_t>(x)) + y, r);
  }
}

#define TEST_RBIT(expected_, input_) \
  t.input = input_;                  \
  t.result = 0;                      \
  f.Call(&t, 0, 0, 0, 0);            \
  CHECK_EQ(static_cast<uint32_t>(expected_), t.result);

TEST(rbit) {
  CcTest::InitializeVM();
  Isolate* const isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatureScope scope(&assm, ARMv7);

    struct T {
      uint32_t input;
      uint32_t result;
    };
    T t;

    __ ldr(r1, MemOperand(r0, offsetof(T, input)));
    __ rbit(r1, r1);
    __ str(r1, MemOperand(r0, offsetof(T, result)));
    __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

#ifdef OBJECT_PRINT
    Print(*code, std::cout);
#endif

    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
    TEST_RBIT(0xFFFFFFFF, 0xFFFFFFFF);
    TEST_RBIT(0x00000000, 0x00000000);
    TEST_RBIT(0xFFFF0000, 0x0000FFFF);
    TEST_RBIT(0xFF00FF00, 0x00FF00FF);
    TEST_RBIT(0xF0F0F0F0, 0x0F0F0F0F);
    TEST_RBIT(0x1E6A2C48, 0x12345678);
  }
}


TEST(code_relative_offset) {
  // Test extracting the offset of a label from the beginning of the code
  // in a register.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  // Initialize a code object that will contain the code.
  Handle<HeapObject> code_object(ReadOnlyRoots(isolate).self_reference_marker(),
                                 isolate);

  Assembler assm(AssemblerOptions{});

  Label start, target_away, target_faraway;

  __ stm(db_w, sp, {r4, r5, lr});

  // r3 is used as the address zero, the test will crash when we load it.
  __ mov(r3, Operand::Zero());

  // r5 will be a pointer to the start of the code.
  __ mov(r5, Operand(code_object));
  __ mov_label_offset(r4, &start);

  __ mov_label_offset(r1, &target_faraway);
  __ str(r1, MemOperand(sp, kPointerSize, NegPreIndex));

  __ mov_label_offset(r1, &target_away);

  // Jump straight to 'target_away' the first time and use the relative
  // position the second time. This covers the case when extracting the
  // position of a label which is linked.
  __ mov(r2, Operand::Zero());
  __ bind(&start);
  __ cmp(r2, Operand::Zero());
  __ b(eq, &target_away);
  __ add(pc, r5, r1);
  // Emit invalid instructions to push the label between 2^8 and 2^16
  // instructions away. The test will crash if they are reached.
  for (int i = 0; i < (1 << 10); i++) {
    __ ldr(r3, MemOperand(r3));
  }
  __ bind(&target_away);
  // This will be hit twice: r0 = r0 + 5 + 5.
  __ add(r0, r0, Operand(5));

  __ ldr(r1, MemOperand(sp, kPointerSize, PostIndex), ne);
  __ add(pc, r5, r4, LeaveCC, ne);

  __ mov(r2, Operand(1));
  __ b(&start);
  // Emit invalid instructions to push the label between 2^16 and 2^24
  // instructions away. The test will crash if they are reached.
  for (int i = 0; i < (1 << 21); i++) {
    __ ldr(r3, MemOperand(r3));
  }
  __ bind(&target_faraway);
  // r0 = r0 + 5 + 5 + 11
  __ add(r0, r0, Operand(11));

  __ ldm(ia_w, sp, {r4, r5, pc});

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code = Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING)
                          .set_self_reference(code_object)
                          .Build();
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  int res = reinterpret_cast<int>(f.Call(21, 0, 0, 0, 0));
  ::printf("f() = %d\n", res);
  CHECK_EQ(42, res);
}

TEST(msr_mrs) {
  // Test msr and mrs.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  // Create a helper function:
  //  void TestMsrMrs(uint32_t nzcv,
  //                  uint32_t * result_conditionals,
  //                  uint32_t * result_mrs);
  __ msr(CPSR_f, Operand(r0));

  // Test that the condition flags have taken effect.
  __ mov(r3, Operand(0));
  __ orr(r3, r3, Operand(1 << 31), LeaveCC, mi);  // N
  __ orr(r3, r3, Operand(1 << 30), LeaveCC, eq);  // Z
  __ orr(r3, r3, Operand(1 << 29), LeaveCC, cs);  // C
  __ orr(r3, r3, Operand(1 << 28), LeaveCC, vs);  // V
  __ str(r3, MemOperand(r1));

  // Also check mrs, ignoring everything other than the flags.
  __ mrs(r3, CPSR);
  __ and_(r3, r3, Operand(kSpecialCondition));
  __ str(r3, MemOperand(r2));

  __ bx(lr);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_ippii>::FromCode(isolate, *code);

#define CHECK_MSR_MRS(n, z, c, v)                                  \
  do {                                                             \
    uint32_t nzcv = (n << 31) | (z << 30) | (c << 29) | (v << 28); \
    uint32_t result_conditionals = -1;                             \
    uint32_t result_mrs = -1;                                      \
    f.Call(nzcv, &result_conditionals, &result_mrs, 0, 0);         \
    CHECK_EQ(nzcv, result_conditionals);                           \
    CHECK_EQ(nzcv, result_mrs);                                    \
  } while (0);

  //            N  Z  C  V
  CHECK_MSR_MRS(0, 0, 0, 0);
  CHECK_MSR_MRS(0, 0, 0, 1);
  CHECK_MSR_MRS(0, 0, 1, 0);
  CHECK_MSR_MRS(0, 0, 1, 1);
  CHECK_MSR_MRS(0, 1, 0, 0);
  CHECK_MSR_MRS(0, 1, 0, 1);
  CHECK_MSR_MRS(0, 1, 1, 0);
  CHECK_MSR_MRS(0, 1, 1, 1);
  CHECK_MSR_MRS(1, 0, 0, 0);
  CHECK_MSR_MRS(1, 0, 0, 1);
  CHECK_MSR_MRS(1, 0, 1, 0);
  CHECK_MSR_MRS(1, 0, 1, 1);
  CHECK_MSR_MRS(1, 1, 0, 0);
  CHECK_MSR_MRS(1, 1, 0, 1);
  CHECK_MSR_MRS(1, 1, 1, 0);
  CHECK_MSR_MRS(1, 1, 1, 1);

#undef CHECK_MSR_MRS
}

TEST(ARMv8_float32_vrintX) {
  // Test the vrintX floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    float input;
    float ar;
    float nr;
    float mr;
    float pr;
    float zr;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the floats.
  Assembler assm(AssemblerOptions{});


  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});

    __ mov(r4, Operand(r0));

    // Test vrinta
    __ vldr(s6, r4, offsetof(T, input));
    __ vrinta(s5, s6);
    __ vstr(s5, r4, offsetof(T, ar));

    // Test vrintn
    __ vldr(s6, r4, offsetof(T, input));
    __ vrintn(s5, s6);
    __ vstr(s5, r4, offsetof(T, nr));

    // Test vrintp
    __ vldr(s6, r4, offsetof(T, input));
    __ vrintp(s5, s6);
    __ vstr(s5, r4, offsetof(T, pr));

    // Test vrintm
    __ vldr(s6, r4, offsetof(T, input));
    __ vrintm(s5, s6);
    __ vstr(s5, r4, offsetof(T, mr));

    // Test vrintz
    __ vldr(s6, r4, offsetof(T, input));
    __ vrintz(s5, s6);
    __ vstr(s5, r4, offsetof(T, zr));

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);

#define CHECK_VRINT(input_val, ares, nres, mres, pres, zres) \
  t.input = input_val;                                       \
  f.Call(&t, 0, 0, 0, 0);                                    \
  CHECK_EQ(ares, t.ar);                                      \
  CHECK_EQ(nres, t.nr);                                      \
  CHECK_EQ(mres, t.mr);                                      \
  CHECK_EQ(pres, t.pr);                                      \
  CHECK_EQ(zres, t.zr);

    CHECK_VRINT(-0.5, -1.0, -0.0, -1.0, -0.0, -0.0)
    CHECK_VRINT(-0.6, -1.0, -1.0, -1.0, -0.0, -0.0)
    CHECK_VRINT(-1.1, -1.0, -1.0, -2.0, -1.0, -1.0)
    CHECK_VRINT(0.5, 1.0, 0.0, 0.0, 1.0, 0.0)
    CHECK_VRINT(0.6, 1.0, 1.0, 0.0, 1.0, 0.0)
    CHECK_VRINT(1.1, 1.0, 1.0, 1.0, 2.0, 1.0)
    float inf = std::numeric_limits<float>::infinity();
    CHECK_VRINT(inf, inf, inf, inf, inf, inf)
    CHECK_VRINT(-inf, -inf, -inf, -inf, -inf, -inf)
    CHECK_VRINT(-0.0, -0.0, -0.0, -0.0, -0.0, -0.0)

    // Check NaN propagation.
    float nan = std::numeric_limits<float>::quiet_NaN();
    t.input = nan;
    f.Call(&t, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<int32_t>(nan), base::bit_cast<int32_t>(t.ar));
    CHECK_EQ(base::bit_cast<int32_t>(nan), base::bit_cast<int32_t>(t.nr));
    CHECK_EQ(base::bit_cast<int32_t>(nan), base::bit_cast<int32_t>(t.mr));
    CHECK_EQ(base::bit_cast<int32_t>(nan), base::bit_cast<int32_t>(t.pr));
    CHECK_EQ(base::bit_cast<int32_t>(nan), base::bit_cast<int32_t>(t.zr));

#undef CHECK_VRINT
  }
}


TEST(ARMv8_vrintX) {
  // Test the vrintX floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    double input;
    double ar;
    double nr;
    double mr;
    double pr;
    double zr;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles and floats.
  Assembler assm(AssemblerOptions{});


  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});

    __ mov(r4, Operand(r0));

    // Test vrinta
    __ vldr(d6, r4, offsetof(T, input));
    __ vrinta(d5, d6);
    __ vstr(d5, r4, offsetof(T, ar));

    // Test vrintn
    __ vldr(d6, r4, offsetof(T, input));
    __ vrintn(d5, d6);
    __ vstr(d5, r4, offsetof(T, nr));

    // Test vrintp
    __ vldr(d6, r4, offsetof(T, input));
    __ vrintp(d5, d6);
    __ vstr(d5, r4, offsetof(T, pr));

    // Test vrintm
    __ vldr(d6, r4, offsetof(T, input));
    __ vrintm(d5, d6);
    __ vstr(d5, r4, offsetof(T, mr));

    // Test vrintz
    __ vldr(d6, r4, offsetof(T, input));
    __ vrintz(d5, d6);
    __ vstr(d5, r4, offsetof(T, zr));

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);

#define CHECK_VRINT(input_val, ares, nres, mres, pres, zres) \
  t.input = input_val;                                       \
  f.Call(&t, 0, 0, 0, 0);                                    \
  CHECK_EQ(ares, t.ar);                                      \
  CHECK_EQ(nres, t.nr);                                      \
  CHECK_EQ(mres, t.mr);                                      \
  CHECK_EQ(pres, t.pr);                                      \
  CHECK_EQ(zres, t.zr);

    CHECK_VRINT(-0.5, -1.0, -0.0, -1.0, -0.0, -0.0)
    CHECK_VRINT(-0.6, -1.0, -1.0, -1.0, -0.0, -0.0)
    CHECK_VRINT(-1.1, -1.0, -1.0, -2.0, -1.0, -1.0)
    CHECK_VRINT(0.5, 1.0, 0.0, 0.0, 1.0, 0.0)
    CHECK_VRINT(0.6, 1.0, 1.0, 0.0, 1.0, 0.0)
    CHECK_VRINT(1.1, 1.0, 1.0, 1.0, 2.0, 1.0)
    double inf = std::numeric_limits<double>::infinity();
    CHECK_VRINT(inf, inf, inf, inf, inf, inf)
    CHECK_VRINT(-inf, -inf, -inf, -inf, -inf, -inf)
    CHECK_VRINT(-0.0, -0.0, -0.0, -0.0, -0.0, -0.0)

    // Check NaN propagation.
    double nan = std::numeric_limits<double>::quiet_NaN();
    t.input = nan;
    f.Call(&t, 0, 0, 0, 0);
    CHECK_EQ(base::bit_cast<int64_t>(nan), base::bit_cast<int64_t>(t.ar));
    CHECK_EQ(base::bit_cast<int64_t>(nan), base::bit_cast<int64_t>(t.nr));
    CHECK_EQ(base::bit_cast<int64_t>(nan), base::bit_cast<int64_t>(t.mr));
    CHECK_EQ(base::bit_cast<int64_t>(nan), base::bit_cast<int64_t>(t.pr));
    CHECK_EQ(base::bit_cast<int64_t>(nan), base::bit_cast<int64_t>(t.zr));

#undef CHECK_VRINT
  }
}

TEST(ARMv8_vsel) {
  // Test the vsel floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  // Used to indicate whether a condition passed or failed.
  static constexpr float kResultPass = 1.0f;
  static constexpr float kResultFail = -kResultPass;

  struct ResultsF32 {
    float vseleq_;
    float vselge_;
    float vselgt_;
    float vselvs_;

    // The following conditions aren't architecturally supported, but the
    // assembler implements them by swapping the inputs.
    float vselne_;
    float vsellt_;
    float vselle_;
    float vselvc_;
  };

  struct ResultsF64 {
    double vseleq_;
    double vselge_;
    double vselgt_;
    double vselvs_;

    // The following conditions aren't architecturally supported, but the
    // assembler implements them by swapping the inputs.
    double vselne_;
    double vsellt_;
    double vselle_;
    double vselvc_;
  };

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);

    // Create a helper function:
    //  void TestVsel(uint32_t nzcv,
    //                ResultsF32* results_f32,
    //                ResultsF64* results_f64);
    __ msr(CPSR_f, Operand(r0));

    __ vmov(s1, Float32(kResultPass));
    __ vmov(s2, Float32(kResultFail));

    __ vsel(eq, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vseleq_));
    __ vsel(ge, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselge_));
    __ vsel(gt, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselgt_));
    __ vsel(vs, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselvs_));

    __ vsel(ne, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselne_));
    __ vsel(lt, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vsellt_));
    __ vsel(le, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselle_));
    __ vsel(vc, s0, s1, s2);
    __ vstr(s0, r1, offsetof(ResultsF32, vselvc_));

    __ vmov(d1, base::Double(kResultPass));
    __ vmov(d2, base::Double(kResultFail));

    __ vsel(eq, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vseleq_));
    __ vsel(ge, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselge_));
    __ vsel(gt, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselgt_));
    __ vsel(vs, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselvs_));

    __ vsel(ne, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselne_));
    __ vsel(lt, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vsellt_));
    __ vsel(le, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselle_));
    __ vsel(vc, d0, d1, d2);
    __ vstr(d0, r2, offsetof(ResultsF64, vselvc_));

    __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_ippii>::FromCode(isolate, *code);

    static_assert(kResultPass == -kResultFail);
#define CHECK_VSEL(n, z, c, v, vseleq, vselge, vselgt, vselvs)     \
  do {                                                             \
    ResultsF32 results_f32;                                        \
    ResultsF64 results_f64;                                        \
    uint32_t nzcv = (n << 31) | (z << 30) | (c << 29) | (v << 28); \
    f.Call(nzcv, &results_f32, &results_f64, 0, 0);                \
    CHECK_EQ(vseleq, results_f32.vseleq_);                         \
    CHECK_EQ(vselge, results_f32.vselge_);                         \
    CHECK_EQ(vselgt, results_f32.vselgt_);                         \
    CHECK_EQ(vselvs, results_f32.vselvs_);                         \
    CHECK_EQ(-vseleq, results_f32.vselne_);                        \
    CHECK_EQ(-vselge, results_f32.vsellt_);                        \
    CHECK_EQ(-vselgt, results_f32.vselle_);                        \
    CHECK_EQ(-vselvs, results_f32.vselvc_);                        \
    CHECK_EQ(vseleq, results_f64.vseleq_);                         \
    CHECK_EQ(vselge, results_f64.vselge_);                         \
    CHECK_EQ(vselgt, results_f64.vselgt_);                         \
    CHECK_EQ(vselvs, results_f64.vselvs_);                         \
    CHECK_EQ(-vseleq, results_f64.vselne_);                        \
    CHECK_EQ(-vselge, results_f64.vsellt_);                        \
    CHECK_EQ(-vselgt, results_f64.vselle_);                        \
    CHECK_EQ(-vselvs, results_f64.vselvc_);                        \
  } while (0);

    //         N  Z  C  V  vseleq       vselge       vselgt       vselvs
    CHECK_VSEL(0, 0, 0, 0, kResultFail, kResultPass, kResultPass, kResultFail);
    CHECK_VSEL(0, 0, 0, 1, kResultFail, kResultFail, kResultFail, kResultPass);
    CHECK_VSEL(0, 0, 1, 0, kResultFail, kResultPass, kResultPass, kResultFail);
    CHECK_VSEL(0, 0, 1, 1, kResultFail, kResultFail, kResultFail, kResultPass);
    CHECK_VSEL(0, 1, 0, 0, kResultPass, kResultPass, kResultFail, kResultFail);
    CHECK_VSEL(0, 1, 0, 1, kResultPass, kResultFail, kResultFail, kResultPass);
    CHECK_VSEL(0, 1, 1, 0, kResultPass, kResultPass, kResultFail, kResultFail);
    CHECK_VSEL(0, 1, 1, 1, kResultPass, kResultFail, kResultFail, kResultPass);
    CHECK_VSEL(1, 0, 0, 0, kResultFail, kResultFail, kResultFail, kResultFail);
    CHECK_VSEL(1, 0, 0, 1, kResultFail, kResultPass, kResultPass, kResultPass);
    CHECK_VSEL(1, 0, 1, 0, kResultFail, kResultFail, kResultFail, kResultFail);
    CHECK_VSEL(1, 0, 1, 1, kResultFail, kResultPass, kResultPass, kResultPass);
    CHECK_VSEL(1, 1, 0, 0, kResultPass, kResultFail, kResultFail, kResultFail);
    CHECK_VSEL(1, 1, 0, 1, kResultPass, kResultPass, kResultFail, kResultPass);
    CHECK_VSEL(1, 1, 1, 0, kResultPass, kResultFail, kResultFail, kResultFail);
    CHECK_VSEL(1, 1, 1, 1, kResultPass, kResultPass, kResultFail, kResultPass);

#undef CHECK_VSEL
  }
}

TEST(ARMv8_vminmax_f64) {
  // Test the vminnm and vmaxnm floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  struct Inputs {
    double left_;
    double right_;
  };

  struct Results {
    double vminnm_;
    double vmaxnm_;
  };

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);

    // Create a helper function:
    //  void TestVminmax(const Inputs* inputs,
    //                   Results* results);
    __ vldr(d1, r0, offsetof(Inputs, left_));
    __ vldr(d2, r0, offsetof(Inputs, right_));

    __ vminnm(d0, d1, d2);
    __ vstr(d0, r1, offsetof(Results, vminnm_));
    __ vmaxnm(d0, d1, d2);
    __ vstr(d0, r1, offsetof(Results, vmaxnm_));

    __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_ppiii>::FromCode(isolate, *code);

#define CHECK_VMINMAX(left, right, vminnm, vmaxnm)                  \
  do {                                                              \
    Inputs inputs = {left, right};                                  \
    Results results;                                                \
    f.Call(&inputs, &results, 0, 0, 0);                             \
    /* Use a base::bit_cast to correctly identify -0.0 and NaNs. */ \
    CHECK_EQ(base::bit_cast<uint64_t>(vminnm),                      \
             base::bit_cast<uint64_t>(results.vminnm_));            \
    CHECK_EQ(base::bit_cast<uint64_t>(vmaxnm),                      \
             base::bit_cast<uint64_t>(results.vmaxnm_));            \
  } while (0);

    double nan_a = base::bit_cast<double>(UINT64_C(0x7FF8000000000001));
    double nan_b = base::bit_cast<double>(UINT64_C(0x7FF8000000000002));

    CHECK_VMINMAX(1.0, -1.0, -1.0, 1.0);
    CHECK_VMINMAX(-1.0, 1.0, -1.0, 1.0);
    CHECK_VMINMAX(0.0, -1.0, -1.0, 0.0);
    CHECK_VMINMAX(-1.0, 0.0, -1.0, 0.0);
    CHECK_VMINMAX(-0.0, -1.0, -1.0, -0.0);
    CHECK_VMINMAX(-1.0, -0.0, -1.0, -0.0);
    CHECK_VMINMAX(0.0, 1.0, 0.0, 1.0);
    CHECK_VMINMAX(1.0, 0.0, 0.0, 1.0);

    CHECK_VMINMAX(0.0, 0.0, 0.0, 0.0);
    CHECK_VMINMAX(-0.0, -0.0, -0.0, -0.0);
    CHECK_VMINMAX(-0.0, 0.0, -0.0, 0.0);
    CHECK_VMINMAX(0.0, -0.0, -0.0, 0.0);

    CHECK_VMINMAX(0.0, nan_a, 0.0, 0.0);
    CHECK_VMINMAX(nan_a, 0.0, 0.0, 0.0);
    CHECK_VMINMAX(nan_a, nan_b, nan_a, nan_a);
    CHECK_VMINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_VMINMAX
  }
}

TEST(ARMv8_vminmax_f32) {
  // Test the vminnm and vmaxnm floating point instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  struct Inputs {
    float left_;
    float right_;
  };

  struct Results {
    float vminnm_;
    float vmaxnm_;
  };

  if (CpuFeatures::IsSupported(ARMv8)) {
    CpuFeatureScope scope(&assm, ARMv8);

    // Create a helper function:
    //  void TestVminmax(const Inputs* inputs,
    //                   Results* results);
    __ vldr(s1, r0, offsetof(Inputs, left_));
    __ vldr(s2, r0, offsetof(Inputs, right_));

    __ vminnm(s0, s1, s2);
    __ vstr(s0, r1, offsetof(Results, vminnm_));
    __ vmaxnm(s0, s1, s2);
    __ vstr(s0, r1, offsetof(Results, vmaxnm_));

    __ bx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_ppiii>::FromCode(isolate, *code);

#define CHECK_VMINMAX(left, right, vminnm, vmaxnm)                  \
  do {                                                              \
    Inputs inputs = {left, right};                                  \
    Results results;                                                \
    f.Call(&inputs, &results, 0, 0, 0);                             \
    /* Use a base::bit_cast to correctly identify -0.0 and NaNs. */ \
    CHECK_EQ(base::bit_cast<uint32_t>(vminnm),                      \
             base::bit_cast<uint32_t>(results.vminnm_));            \
    CHECK_EQ(base::bit_cast<uint32_t>(vmaxnm),                      \
             base::bit_cast<uint32_t>(results.vmaxnm_));            \
  } while (0);

    float nan_a = base::bit_cast<float>(UINT32_C(0x7FC00001));
    float nan_b = base::bit_cast<float>(UINT32_C(0x7FC00002));

    CHECK_VMINMAX(1.0f, -1.0f, -1.0f, 1.0f);
    CHECK_VMINMAX(-1.0f, 1.0f, -1.0f, 1.0f);
    CHECK_VMINMAX(0.0f, -1.0f, -1.0f, 0.0f);
    CHECK_VMINMAX(-1.0f, 0.0f, -1.0f, 0.0f);
    CHECK_VMINMAX(-0.0f, -1.0f, -1.0f, -0.0f);
    CHECK_VMINMAX(-1.0f, -0.0f, -1.0f, -0.0f);
    CHECK_VMINMAX(0.0f, 1.0f, 0.0f, 1.0f);
    CHECK_VMINMAX(1.0f, 0.0f, 0.0f, 1.0f);

    CHECK_VMINMAX(0.0f, 0.0f, 0.0f, 0.0f);
    CHECK_VMINMAX(-0.0f, -0.0f, -0.0f, -0.0f);
    CHECK_VMINMAX(-0.0f, 0.0f, -0.0f, 0.0f);
    CHECK_VMINMAX(0.0f, -0.0f, -0.0f, 0.0f);

    CHECK_VMINMAX(0.0f, nan_a, 0.0f, 0.0f);
    CHECK_VMINMAX(nan_a, 0.0f, 0.0f, 0.0f);
    CHECK_VMINMAX(nan_a, nan_b, nan_a, nan_a);
    CHECK_VMINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_VMINMAX
  }
}

template <typename T, typename Inputs, typename Results>
static GeneratedCode<F_ppiii> GenerateMacroFloatMinMax(
    MacroAssembler* assm_ptr) {
  MacroAssembler& assm = *assm_ptr;

  T a = T::from_code(0);  // d0/s0
  T b = T::from_code(1);  // d1/s1
  T c = T::from_code(2);  // d2/s2

  // Create a helper function:
  //  void TestFloatMinMax(const Inputs* inputs,
  //                       Results* results);
  Label ool_min_abc, ool_min_aab, ool_min_aba;
  Label ool_max_abc, ool_max_aab, ool_max_aba;

  Label done_min_abc, done_min_aab, done_min_aba;
  Label done_max_abc, done_max_aab, done_max_aba;

  // a = min(b, c);
  __ vldr(b, r0, offsetof(Inputs, left_));
  __ vldr(c, r0, offsetof(Inputs, right_));
  __ FloatMin(a, b, c, &ool_min_abc);
  __ bind(&done_min_abc);
  __ vstr(a, r1, offsetof(Results, min_abc_));

  // a = min(a, b);
  __ vldr(a, r0, offsetof(Inputs, left_));
  __ vldr(b, r0, offsetof(Inputs, right_));
  __ FloatMin(a, a, b, &ool_min_aab);
  __ bind(&done_min_aab);
  __ vstr(a, r1, offsetof(Results, min_aab_));

  // a = min(b, a);
  __ vldr(b, r0, offsetof(Inputs, left_));
  __ vldr(a, r0, offsetof(Inputs, right_));
  __ FloatMin(a, b, a, &ool_min_aba);
  __ bind(&done_min_aba);
  __ vstr(a, r1, offsetof(Results, min_aba_));

  // a = max(b, c);
  __ vldr(b, r0, offsetof(Inputs, left_));
  __ vldr(c, r0, offsetof(Inputs, right_));
  __ FloatMax(a, b, c, &ool_max_abc);
  __ bind(&done_max_abc);
  __ vstr(a, r1, offsetof(Results, max_abc_));

  // a = max(a, b);
  __ vldr(a, r0, offsetof(Inputs, left_));
  __ vldr(b, r0, offsetof(Inputs, right_));
  __ FloatMax(a, a, b, &ool_max_aab);
  __ bind(&done_max_aab);
  __ vstr(a, r1, offsetof(Results, max_aab_));

  // a = max(b, a);
  __ vldr(b, r0, offsetof(Inputs, left_));
  __ vldr(a, r0, offsetof(Inputs, right_));
  __ FloatMax(a, b, a, &ool_max_aba);
  __ bind(&done_max_aba);
  __ vstr(a, r1, offsetof(Results, max_aba_));

  __ bx(lr);

  // Generate out-of-line cases.
  __ bind(&ool_min_abc);
  __ FloatMinOutOfLine(a, b, c);
  __ b(&done_min_abc);

  __ bind(&ool_min_aab);
  __ FloatMinOutOfLine(a, a, b);
  __ b(&done_min_aab);

  __ bind(&ool_min_aba);
  __ FloatMinOutOfLine(a, b, a);
  __ b(&done_min_aba);

  __ bind(&ool_max_abc);
  __ FloatMaxOutOfLine(a, b, c);
  __ b(&done_max_abc);

  __ bind(&ool_max_aab);
  __ FloatMaxOutOfLine(a, a, b);
  __ b(&done_max_aab);

  __ bind(&ool_max_aba);
  __ FloatMaxOutOfLine(a, b, a);
  __ b(&done_max_aba);

  CodeDesc desc;
  assm.GetCode(assm.isolate(), &desc);
  Handle<Code> code =
      Factory::CodeBuilder(assm.isolate(), desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  return GeneratedCode<F_ppiii>::FromCode(assm.isolate(), *code);
}

TEST(macro_float_minmax_f64) {
  // Test the FloatMin and FloatMax macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, CodeObjectRequired::kYes);

  struct Inputs {
    double left_;
    double right_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro assembler.
    double min_abc_;
    double min_aab_;
    double min_aba_;
    double max_abc_;
    double max_aab_;
    double max_aba_;
  };

  auto f = GenerateMacroFloatMinMax<DwVfpRegister, Inputs, Results>(&assm);

#define CHECK_MINMAX(left, right, min, max)                         \
  do {                                                              \
    Inputs inputs = {left, right};                                  \
    Results results;                                                \
    f.Call(&inputs, &results, 0, 0, 0);                             \
    /* Use a base::bit_cast to correctly identify -0.0 and NaNs. */ \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_abc_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_aab_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(min),                         \
             base::bit_cast<uint64_t>(results.min_aba_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_abc_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_aab_));           \
    CHECK_EQ(base::bit_cast<uint64_t>(max),                         \
             base::bit_cast<uint64_t>(results.max_aba_));           \
  } while (0)

  double nan_a = base::bit_cast<double>(UINT64_C(0x7FF8000000000001));
  double nan_b = base::bit_cast<double>(UINT64_C(0x7FF8000000000002));

  CHECK_MINMAX(1.0, -1.0, -1.0, 1.0);
  CHECK_MINMAX(-1.0, 1.0, -1.0, 1.0);
  CHECK_MINMAX(0.0, -1.0, -1.0, 0.0);
  CHECK_MINMAX(-1.0, 0.0, -1.0, 0.0);
  CHECK_MINMAX(-0.0, -1.0, -1.0, -0.0);
  CHECK_MINMAX(-1.0, -0.0, -1.0, -0.0);
  CHECK_MINMAX(0.0, 1.0, 0.0, 1.0);
  CHECK_MINMAX(1.0, 0.0, 0.0, 1.0);

  CHECK_MINMAX(0.0, 0.0, 0.0, 0.0);
  CHECK_MINMAX(-0.0, -0.0, -0.0, -0.0);
  CHECK_MINMAX(-0.0, 0.0, -0.0, 0.0);
  CHECK_MINMAX(0.0, -0.0, -0.0, 0.0);

  CHECK_MINMAX(0.0, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

TEST(macro_float_minmax_f32) {
  // Test the FloatMin and FloatMax macros.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  MacroAssembler assm(isolate, CodeObjectRequired::kYes);

  struct Inputs {
    float left_;
    float right_;
  };

  struct Results {
    // Check all register aliasing possibilities in order to exercise all
    // code-paths in the macro assembler.
    float min_abc_;
    float min_aab_;
    float min_aba_;
    float max_abc_;
    float max_aab_;
    float max_aba_;
  };

  auto f = GenerateMacroFloatMinMax<SwVfpRegister, Inputs, Results>(&assm);

#define CHECK_MINMAX(left, right, min, max)                         \
  do {                                                              \
    Inputs inputs = {left, right};                                  \
    Results results;                                                \
    f.Call(&inputs, &results, 0, 0, 0);                             \
    /* Use a base::bit_cast to correctly identify -0.0 and NaNs. */ \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                         \
             base::bit_cast<uint32_t>(results.min_abc_));           \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                         \
             base::bit_cast<uint32_t>(results.min_aab_));           \
    CHECK_EQ(base::bit_cast<uint32_t>(min),                         \
             base::bit_cast<uint32_t>(results.min_aba_));           \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                         \
             base::bit_cast<uint32_t>(results.max_abc_));           \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                         \
             base::bit_cast<uint32_t>(results.max_aab_));           \
    CHECK_EQ(base::bit_cast<uint32_t>(max),                         \
             base::bit_cast<uint32_t>(results.max_aba_));           \
  } while (0)

  float nan_a = base::bit_cast<float>(UINT32_C(0x7FC00001));
  float nan_b = base::bit_cast<float>(UINT32_C(0x7FC00002));

  CHECK_MINMAX(1.0f, -1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(-1.0f, 1.0f, -1.0f, 1.0f);
  CHECK_MINMAX(0.0f, -1.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-1.0f, 0.0f, -1.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -1.0f, -1.0f, -0.0f);
  CHECK_MINMAX(-1.0f, -0.0f, -1.0f, -0.0f);
  CHECK_MINMAX(0.0f, 1.0f, 0.0f, 1.0f);
  CHECK_MINMAX(1.0f, 0.0f, 0.0f, 1.0f);

  CHECK_MINMAX(0.0f, 0.0f, 0.0f, 0.0f);
  CHECK_MINMAX(-0.0f, -0.0f, -0.0f, -0.0f);
  CHECK_MINMAX(-0.0f, 0.0f, -0.0f, 0.0f);
  CHECK_MINMAX(0.0f, -0.0f, -0.0f, 0.0f);

  CHECK_MINMAX(0.0f, nan_a, nan_a, nan_a);
  CHECK_MINMAX(nan_a, 0.0f, nan_a, nan_a);
  CHECK_MINMAX(nan_a, nan_b, nan_a, nan_a);
  CHECK_MINMAX(nan_b, nan_a, nan_b, nan_b);

#undef CHECK_MINMAX
}

TEST(unaligned_loads) {
  // All supported ARM targets allow unaligned accesses.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  struct T {
    uint32_t ldrh;
    uint32_t ldrsh;
    uint32_t ldr;
  };
  T t;

  Assembler assm(AssemblerOptions{});
  __ ldrh(ip, MemOperand(r1, r2));
  __ str(ip, MemOperand(r0, offsetof(T, ldrh)));
  __ ldrsh(ip, MemOperand(r1, r2));
  __ str(ip, MemOperand(r0, offsetof(T, ldrsh)));
  __ ldr(ip, MemOperand(r1, r2));
  __ str(ip, MemOperand(r0, offsetof(T, ldr)));
  __ bx(lr);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_ppiii>::FromCode(isolate, *code);

#ifndef V8_TARGET_LITTLE_ENDIAN
#error This test assumes a little-endian layout.
#endif
  uint64_t data = UINT64_C(0x84838281807F7E7D);
  f.Call(&t, &data, 0, 0, 0);
  CHECK_EQ(0x00007E7Du, t.ldrh);
  CHECK_EQ(0x00007E7Du, t.ldrsh);
  CHECK_EQ(0x807F7E7Du, t.ldr);
  f.Call(&t, &data, 1, 0, 0);
  CHECK_EQ(0x00007F7Eu, t.ldrh);
  CHECK_EQ(0x00007F7Eu, t.ldrsh);
  CHECK_EQ(0x81807F7Eu, t.ldr);
  f.Call(&t, &data, 2, 0, 0);
  CHECK_EQ(0x0000807Fu, t.ldrh);
  CHECK_EQ(0xFFFF807Fu, t.ldrsh);
  CHECK_EQ(0x8281807Fu, t.ldr);
  f.Call(&t, &data, 3, 0, 0);
  CHECK_EQ(0x00008180u, t.ldrh);
  CHECK_EQ(0xFFFF8180u, t.ldrsh);
  CHECK_EQ(0x83828180u, t.ldr);
}

TEST(unaligned_stores) {
  // All supported ARM targets allow unaligned accesses.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  __ strh(r3, MemOperand(r0, r2));
  __ str(r3, MemOperand(r1, r2));
  __ bx(lr);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_ppiii>::FromCode(isolate, *code);

#ifndef V8_TARGET_LITTLE_ENDIAN
#error This test assumes a little-endian layout.
#endif
  {
    uint64_t strh = 0;
    uint64_t str = 0;
    f.Call(&strh, &str, 0, 0xFEDCBA98, 0);
    CHECK_EQ(UINT64_C(0x000000000000BA98), strh);
    CHECK_EQ(UINT64_C(0x00000000FEDCBA98), str);
  }
  {
    uint64_t strh = 0;
    uint64_t str = 0;
    f.Call(&strh, &str, 1, 0xFEDCBA98, 0);
    CHECK_EQ(UINT64_C(0x0000000000BA9800), strh);
    CHECK_EQ(UINT64_C(0x000000FEDCBA9800), str);
  }
  {
    uint64_t strh = 0;
    uint64_t str = 0;
    f.Call(&strh, &str, 2, 0xFEDCBA98, 0);
    CHECK_EQ(UINT64_C(0x00000000BA980000), strh);
    CHECK_EQ(UINT64_C(0x0000FEDCBA980000), str);
  }
  {
    uint64_t strh = 0;
    uint64_t str = 0;
    f.Call(&strh, &str, 3, 0xFEDCBA98, 0);
    CHECK_EQ(UINT64_C(0x000000BA98000000), strh);
    CHECK_EQ(UINT64_C(0x00FEDCBA98000000), str);
  }
}

TEST(vswp) {
  if (!CpuFeatures::IsSupported(NEON)) return;

  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);
  Assembler assm(AssemblerOptions{});

  struct T {
    uint64_t vswp_d0;
    uint64_t vswp_d1;
    uint64_t vswp_d30;
    uint64_t vswp_d31;
    uint32_t vswp_q4[4];
    uint32_t vswp_q5[4];
  };
  T t;

  __ stm(db_w, sp, {r4, r5, r6, r7, lr});

  uint64_t one = base::bit_cast<uint64_t>(1.0);
  __ mov(r5, Operand(one >> 32));
  __ mov(r4, Operand(one & 0xFFFFFFFF));
  uint64_t minus_one = base::bit_cast<uint64_t>(-1.0);
  __ mov(r7, Operand(minus_one >> 32));
  __ mov(r6, Operand(minus_one & 0xFFFFFFFF));

  __ vmov(d0, r4, r5);  // d0 = 1.0
  __ vmov(d1, r6, r7);  // d1 = -1.0
  __ vswp(d0, d1);
  __ vstr(d0, r0, offsetof(T, vswp_d0));
  __ vstr(d1, r0, offsetof(T, vswp_d1));

  if (CpuFeatures::IsSupported(VFP32DREGS)) {
    __ vmov(d30, r4, r5);  // d30 = 1.0
    __ vmov(d31, r6, r7);  // d31 = -1.0
    __ vswp(d30, d31);
    __ vstr(d30, r0, offsetof(T, vswp_d30));
    __ vstr(d31, r0, offsetof(T, vswp_d31));
  }

  // q-register swap.
  const uint32_t test_1 = 0x01234567;
  const uint32_t test_2 = 0x89ABCDEF;
  __ mov(r4, Operand(test_1));
  __ mov(r5, Operand(test_2));
  __ vdup(Neon32, q4, r4);
  __ vdup(Neon32, q5, r5);
  __ vswp(q4, q5);
  __ add(r6, r0, Operand(static_cast<int32_t>(offsetof(T, vswp_q4))));
  __ vst1(Neon8, NeonListOperand(q4), NeonMemOperand(r6));
  __ add(r6, r0, Operand(static_cast<int32_t>(offsetof(T, vswp_q5))));
  __ vst1(Neon8, NeonListOperand(q5), NeonMemOperand(r6));

  __ ldm(ia_w, sp, {r4, r5, r6, r7, pc});
  __ bx(lr);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  StdoutStream os;
  Print(*code, os);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  f.Call(&t, 0, 0, 0, 0);
  CHECK_EQ(minus_one, t.vswp_d0);
  CHECK_EQ(one, t.vswp_d1);
  if (CpuFeatures::IsSupported(VFP32DREGS)) {
    CHECK_EQ(minus_one, t.vswp_d30);
    CHECK_EQ(one, t.vswp_d31);
  }
  CHECK_EQ(t.vswp_q4[0], test_2);
  CHECK_EQ(t.vswp_q4[1], test_2);
  CHECK_EQ(t.vswp_q4[2], test_2);
  CHECK_EQ(t.vswp_q4[3], test_2);
  CHECK_EQ(t.vswp_q5[0], test_1);
  CHECK_EQ(t.vswp_q5[1], test_1);
  CHECK_EQ(t.vswp_q5[2], test_1);
  CHECK_EQ(t.vswp_q5[3], test_1);
}

TEST(regress4292_b) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label end;
  __ mov(r0, Operand(isolate->factory()->infinity_value()));
  for (int i = 0; i < 1020; ++i) {
    __ b(hi, &end);
  }
  __ bind(&end);
}


TEST(regress4292_bl) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label end;
  __ mov(r0, Operand(isolate->factory()->infinity_value()));
  for (int i = 0; i < 1020; ++i) {
    __ bl(hi, &end);
  }
  __ bind(&end);
}


TEST(regress4292_blx) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label end;
  __ mov(r0, Operand(isolate->factory()->infinity_value()));
  for (int i = 0; i < 1020; ++i) {
    __ blx(&end);
  }
  __ bind(&end);
}


TEST(regress4292_CheckConstPool) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  __ mov(r0, Operand(isolate->factory()->infinity_value()));
  __ BlockConstPoolFor(1019);
  for (int i = 0; i < 1019; ++i) __ nop();
  __ vldr(d0, MemOperand(r0, 0));
}

TEST(use_scratch_register_scope) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  // The assembler should have ip as a scratch by default.
  CHECK_EQ(*assm.GetScratchRegisterList(), RegList{ip});

  {
    UseScratchRegisterScope temps(&assm);
    CHECK_EQ(*assm.GetScratchRegisterList(), RegList{ip});

    Register scratch = temps.Acquire();
    CHECK_EQ(scratch.code(), ip.code());
    CHECK_EQ(*assm.GetScratchRegisterList(), RegList{});
  }

  CHECK_EQ(*assm.GetScratchRegisterList(), RegList{ip});
}

TEST(use_scratch_vfp_register_scope) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  VfpRegList orig_scratches = *assm.GetScratchVfpRegisterList();

  if (CpuFeatures::IsSupported(VFP32DREGS)) {
    CHECK_EQ(orig_scratches, d14.ToVfpRegList() | d15.ToVfpRegList());
  } else {
    CHECK_EQ(orig_scratches, d14.ToVfpRegList());
  }

  // Test each configuration of scratch registers we can have at the same time.

  {
    UseScratchRegisterScope temps(&assm);

    SwVfpRegister s1_scratch = temps.AcquireS();
    CHECK_EQ(s1_scratch, s28);

    SwVfpRegister s2_scratch = temps.AcquireS();
    CHECK_EQ(s2_scratch, s29);

    if (CpuFeatures::IsSupported(VFP32DREGS)) {
      SwVfpRegister s3_scratch = temps.AcquireS();
      CHECK_EQ(s3_scratch, s30);

      SwVfpRegister s4_scratch = temps.AcquireS();
      CHECK_EQ(s4_scratch, s31);
    }
  }

  CHECK_EQ(*assm.GetScratchVfpRegisterList(), orig_scratches);

  {
    UseScratchRegisterScope temps(&assm);

    SwVfpRegister s1_scratch = temps.AcquireS();
    CHECK_EQ(s1_scratch, s28);

    SwVfpRegister s2_scratch = temps.AcquireS();
    CHECK_EQ(s2_scratch, s29);

    if (CpuFeatures::IsSupported(VFP32DREGS)) {
      DwVfpRegister d_scratch = temps.AcquireD();
      CHECK_EQ(d_scratch, d15);
    }
  }

  CHECK_EQ(*assm.GetScratchVfpRegisterList(), orig_scratches);

  {
    UseScratchRegisterScope temps(&assm);

    DwVfpRegister d_scratch = temps.AcquireD();
    CHECK_EQ(d_scratch, d14);

    if (CpuFeatures::IsSupported(VFP32DREGS)) {
      SwVfpRegister s1_scratch = temps.AcquireS();
      CHECK_EQ(s1_scratch, s30);

      SwVfpRegister s2_scratch = temps.AcquireS();
      CHECK_EQ(s2_scratch, s31);
    }
  }

  CHECK_EQ(*assm.GetScratchVfpRegisterList(), orig_scratches);

  {
    UseScratchRegisterScope temps(&assm);

    DwVfpRegister d1_scratch = temps.AcquireD();
    CHECK_EQ(d1_scratch, d14);

    if (CpuFeatures::IsSupported(VFP32DREGS)) {
      DwVfpRegister d2_scratch = temps.AcquireD();
      CHECK_EQ(d2_scratch, d15);
    }
  }

  CHECK_EQ(*assm.GetScratchVfpRegisterList(), orig_scratches);

  if (CpuFeatures::IsSupported(NEON)) {
    UseScratchRegisterScope temps(&assm);

    QwNeonRegister q_scratch = temps.AcquireQ();
    CHECK_EQ(q_scratch, q7);
  }

  CHECK_EQ(*assm.GetScratchVfpRegisterList(), orig_scratches);
}

TEST(split_add_immediate) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  {
    Assembler assm(AssemblerOptions{});
    __ mov(r1, r0);
    // Reuse the destination as a scratch.
    __ add(r0, r1, Operand(0x12345678));
    __ blx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    uint32_t res = reinterpret_cast<int>(f.Call(0, 0, 0, 0, 0));
    ::printf("f() = 0x%x\n", res);
    CHECK_EQ(0x12345678, res);
  }

  {
    Assembler assm(AssemblerOptions{});
    // Use ip as a scratch.
    __ add(r0, r0, Operand(0x12345678));
    __ blx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    uint32_t res = reinterpret_cast<int>(f.Call(0, 0, 0, 0, 0));
    ::printf("f() = 0x%x\n", res);
    CHECK_EQ(0x12345678, res);
  }

  {
    Assembler assm(AssemblerOptions{});
    UseScratchRegisterScope temps(&assm);
    Register reserved = temps.Acquire();
    USE(reserved);
    // If ip is not available, split the operation into multiple additions.
    __ add(r0, r0, Operand(0x12345678));
    __ blx(lr);

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Handle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
    StdoutStream os;
    Print(*code, os);
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    uint32_t res = reinterpret_cast<int>(f.Call(0, 0, 0, 0, 0));
    ::printf("f() = 0x%x\n", res);
    CHECK_EQ(0x12345678, res);
  }
}

namespace {

std::vector<Float32> Float32Inputs() {
  std::vector<Float32> inputs;
  FOR_FLOAT32_INPUTS(f) {
    inputs.push_back(Float32::FromBits(base::bit_cast<uint32_t>(f)));
  }
  FOR_UINT32_INPUTS(bits) { inputs.push_back(Float32::FromBits(bits)); }
  return inputs;
}

std::vector<Float64> Float64Inputs() {
  std::vector<Float64> inputs;
  FOR_FLOAT64_INPUTS(f) {
    inputs.push_back(Float64::FromBits(base::bit_cast<uint64_t>(f)));
  }
  FOR_UINT64_INPUTS(bits) { inputs.push_back(Float64::FromBits(bits)); }
  return inputs;
}

}  // namespace

TEST(vabs_32) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto f = AssembleCode<F_iiiii>(isolate, [](Assembler& assm) {
    __ vmov(s0, r0);
    __ vabs(s0, s0);
    __ vmov(r0, s0);
  });

  for (Float32 f32 : Float32Inputs()) {
    Float32 res = Float32::FromBits(
        reinterpret_cast<uint32_t>(f.Call(f32.get_bits(), 0, 0, 0, 0)));
    Float32 exp = Float32::FromBits(f32.get_bits() & ~(1 << 31));
    CHECK_EQ(exp.get_bits(), res.get_bits());
  }
}

TEST(vabs_64) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto f = AssembleCode<F_iiiii>(isolate, [](Assembler& assm) {
    __ vmov(d0, r0, r1);
    __ vabs(d0, d0);
    __ vmov(r1, r0, d0);
  });

  for (Float64 f64 : Float64Inputs()) {
    uint32_t p0 = static_cast<uint32_t>(f64.get_bits());
    uint32_t p1 = static_cast<uint32_t>(f64.get_bits() >> 32);
    uint32_t res = reinterpret_cast<uint32_t>(f.Call(p0, p1, 0, 0, 0));
    Float64 exp = Float64::FromBits(f64.get_bits() & ~(1ull << 63));
    // We just get back the top word, so only compare that one.
    CHECK_EQ(exp.get_bits() >> 32, res);
  }
}

TEST(vneg_32) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto f = AssembleCode<F_iiiii>(isolate, [](Assembler& assm) {
    __ vmov(s0, r0);
    __ vneg(s0, s0);
    __ vmov(r0, s0);
  });

  for (Float32 f32 : Float32Inputs()) {
    Float32 res = Float32::FromBits(
        reinterpret_cast<uint32_t>(f.Call(f32.get_bits(), 0, 0, 0, 0)));
    Float32 exp = Float32::FromBits(f32.get_bits() ^ (1 << 31));
    CHECK_EQ(exp.get_bits(), res.get_bits());
  }
}

TEST(vneg_64) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto f = AssembleCode<F_iiiii>(isolate, [](Assembler& assm) {
    __ vmov(d0, r0, r1);
    __ vneg(d0, d0);
    __ vmov(r1, r0, d0);
  });

  for (Float64 f64 : Float64Inputs()) {
    uint32_t p0 = static_cast<uint32_t>(f64.get_bits());
    uint32_t p1 = static_cast<uint32_t>(f64.get_bits() >> 32);
    uint32_t res = reinterpret_cast<uint32_t>(f.Call(p0, p1, 0, 0, 0));
    Float64 exp = Float64::FromBits(f64.get_bits() ^ (1ull << 63));
    // We just get back the top word, so only compare that one.
    CHECK_EQ(exp.get_bits() >> 32, res);
  }
}

TEST(move_pair) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  auto f = AssembleCode<F_piiii>(isolate, [](MacroAssembler& assm) {
    RegList used_callee_saved = {r4, r5, r6, r7, r8};
    __ stm(db_w, sp, used_callee_saved);

    // Save output register bank pointer to r8.
    __ mov(r8, r0);

    __ mov(r0, Operand(0xabababab));
    __ mov(r1, Operand(0xbabababa));
    __ mov(r2, Operand(0x12341234));
    __ mov(r3, Operand(0x43214321));

    // No overlap:
    //  r4 <- r0
    //  r5 <- r1
    __ MovePair(r4, r0, r5, r1);

    // Overlap but we can swap moves:
    //  r2 <- r0
    //  r6 <- r2
    __ MovePair(r2, r0, r6, r2);

    // Overlap but can be done:
    //  r7 <- r3
    //  r3 <- r0
    __ MovePair(r7, r3, r3, r0);

    // Swap.
    //  r0 <- r1
    //  r1 <- r0
    __ MovePair(r0, r1, r1, r0);

    // Fill the fake register bank.
    __ str(r0, MemOperand(r8, 0 * kPointerSize));
    __ str(r1, MemOperand(r8, 1 * kPointerSize));
    __ str(r2, MemOperand(r8, 2 * kPointerSize));
    __ str(r3, MemOperand(r8, 3 * kPointerSize));
    __ str(r4, MemOperand(r8, 4 * kPointerSize));
    __ str(r5, MemOperand(r8, 5 * kPointerSize));
    __ str(r6, MemOperand(r8, 6 * kPointerSize));
    __ str(r7, MemOperand(r8, 7 * kPointerSize));

    __ ldm(ia_w, sp, used_callee_saved);
  });

  // Create a fake register bank.
  uint32_t r[] = {0, 0, 0, 0, 0, 0, 0, 0};
  f.Call(r, 0, 0, 0, 0);

  //  r4 <- r0
  //  r5 <- r1
  CHECK_EQ(0xabababab, r[4]);
  CHECK_EQ(0xbabababa, r[5]);

  //  r2 <- r0
  //  r6 <- r2
  CHECK_EQ(0xabababab, r[2]);
  CHECK_EQ(0x12341234, r[6]);

  //  r7 <- r3
  //  r3 <- r0
  CHECK_EQ(0x43214321, r[7]);
  CHECK_EQ(0xabababab, r[3]);

  // r0 and r1 should be swapped.
  CHECK_EQ(0xbabababa, r[0]);
  CHECK_EQ(0xabababab, r[1]);
}


#undef __

}  // namespace test_assembler_arm
}  // namespace internal
}  // namespace v8
