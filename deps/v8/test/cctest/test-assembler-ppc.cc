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

#include "src/init/v8.h"

#include "src/codegen/ppc/assembler-ppc-inl.h"
#include "src/diagnostics/disassembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "test/cctest/cctest.h"
#include "test/common/assembler-tester.h"

namespace v8 {
namespace internal {

// TODO(ppc): Refine these signatures per test case, they can have arbitrary
// return and argument types and arbitrary number of arguments.
using F_iiiii = void*(int x, int p1, int p2, int p3, int p4);
using F_piiii = void*(void* p0, int p1, int p2, int p3, int p4);
using F_ppiii = void*(void* p0, void* p1, int p2, int p3, int p4);
using F_pppii = void*(void* p0, void* p1, void* p2, int p3, int p4);
using F_ippii = void*(int p0, void* p1, void* p2, int p3, int p4);

#define __ assm.

// Simple add parameter 1 to parameter 2 and return
TEST(0) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  __ add(r3, r3, r4);
  __ blr();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  Print(*code);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  intptr_t res = reinterpret_cast<intptr_t>(f.Call(3, 4, 0, 0, 0));
  ::printf("f() = %" V8PRIdPTR "\n", res);
  CHECK_EQ(7, static_cast<int>(res));
}


// Loop 100 times, adding loop counter to result
TEST(1) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label L, C;

  __ mr(r4, r3);
  __ li(r3, Operand::Zero());
  __ b(&C);

  __ bind(&L);
  __ add(r3, r3, r4);
  __ subi(r4, r4, Operand(1));

  __ bind(&C);
  __ cmpi(r4, Operand::Zero());
  __ bne(&L);
  __ blr();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  Print(*code);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  intptr_t res = reinterpret_cast<intptr_t>(f.Call(100, 0, 0, 0, 0));
  ::printf("f() = %" V8PRIdPTR "\n", res);
  CHECK_EQ(5050, static_cast<int>(res));
}


TEST(2) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  Label L, C;

  __ mr(r4, r3);
  __ li(r3, Operand(1));
  __ b(&C);

  __ bind(&L);
#if defined(V8_TARGET_ARCH_PPC64)
  __ mulld(r3, r4, r3);
#else
  __ mullw(r3, r4, r3);
#endif
  __ subi(r4, r4, Operand(1));

  __ bind(&C);
  __ cmpi(r4, Operand::Zero());
  __ bne(&L);
  __ blr();

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
  Print(*code);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  intptr_t res = reinterpret_cast<intptr_t>(f.Call(10, 0, 0, 0, 0));
  ::printf("f() = %" V8PRIdPTR "\n", res);
  CHECK_EQ(3628800, static_cast<int>(res));
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

// build a frame
#if V8_TARGET_ARCH_PPC64
  __ stdu(sp, MemOperand(sp, -32));
  __ std(fp, MemOperand(sp, 24));
#else
  __ stwu(sp, MemOperand(sp, -16));
  __ stw(fp, MemOperand(sp, 12));
#endif
  __ mr(fp, sp);

  // r4 points to our struct
  __ mr(r4, r3);

  // modify field int i of struct
  __ lwz(r3, MemOperand(r4, offsetof(T, i)));
  __ srwi(r5, r3, Operand(1));
  __ stw(r5, MemOperand(r4, offsetof(T, i)));

  // modify field char c of struct
  __ lbz(r5, MemOperand(r4, offsetof(T, c)));
  __ add(r3, r5, r3);
  __ slwi(r5, r5, Operand(2));
  __ stb(r5, MemOperand(r4, offsetof(T, c)));

  // modify field int16_t s of struct
  __ lhz(r5, MemOperand(r4, offsetof(T, s)));
  __ add(r3, r5, r3);
  __ srwi(r5, r5, Operand(3));
  __ sth(r5, MemOperand(r4, offsetof(T, s)));

// restore frame
#if V8_TARGET_ARCH_PPC64
  __ addi(r11, fp, Operand(32));
  __ ld(fp, MemOperand(r11, -8));
#else
  __ addi(r11, fp, Operand(16));
  __ lwz(fp, MemOperand(r11, -4));
#endif
  __ mr(sp, r11);
  __ blr();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  Print(*code);
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  t.i = 100000;
  t.c = 10;
  t.s = 1000;
  intptr_t res = reinterpret_cast<intptr_t>(f.Call(&t, 0, 0, 0, 0));
  ::printf("f() = %" V8PRIdPTR "\n", res);
  CHECK_EQ(101010, static_cast<int>(res));
  CHECK_EQ(100000 / 2, t.i);
  CHECK_EQ(10 * 4, t.c);
  CHECK_EQ(1000 / 8, t.s);
}

#if 0
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
    double m;
    double n;
    float x;
    float y;
  };
  T t;

  // Create a function that accepts &t, and loads, manipulates, and stores
  // the doubles and floats.
  Assembler assm(AssemblerOptions{});
  Label L, C;

  if (CpuFeatures::IsSupported(VFP3)) {
    CpuFeatures::Scope scope(VFP3);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});
    __ sub(fp, ip, Operand(4));

    __ mov(r4, Operand(r0));
    __ vldr(d6, r4, offsetof(T, a));
    __ vldr(d7, r4, offsetof(T, b));
    __ vadd(d5, d6, d7);
    __ vstr(d5, r4, offsetof(T, c));

    __ vmov(r2, r3, d5);
    __ vmov(d4, r2, r3);
    __ vstr(d4, r4, offsetof(T, b));

    // Load t.x and t.y, switch values, and store back to the struct.
    __ vldr(s0, r4, offsetof(T, x));
    __ vldr(s31, r4, offsetof(T, y));
    __ vmov(s16, s0);
    __ vmov(s0, s31);
    __ vmov(s31, s16);
    __ vstr(s0, r4, offsetof(T, x));
    __ vstr(s31, r4, offsetof(T, y));

    // Move a literal into a register that can be encoded in the instruction.
    __ vmov(d4, 1.0);
    __ vstr(d4, r4, offsetof(T, e));

    // Move a literal into a register that requires 64 bits to encode.
    // 0x3FF0000010000000 = 1.000000059604644775390625
    __ vmov(d4, 1.000000059604644775390625);
    __ vstr(d4, r4, offsetof(T, d));

    // Convert from floating point to integer.
    __ vmov(d4, 2.0);
    __ vcvt_s32_f64(s31, d4);
    __ vstr(s31, r4, offsetof(T, i));

    // Convert from integer to floating point.
    __ mov(lr, Operand(42));
    __ vmov(s31, lr);
    __ vcvt_f64_s32(d4, s31);
    __ vstr(d4, r4, offsetof(T, f));

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

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
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
    t.m = -2718.2818;
    t.n = 123.456;
    t.x = 4.5;
    t.y = 9.0;
    f.Call(&t, 0, 0, 0, 0);
    CHECK_EQ(4.5, t.y);
    CHECK_EQ(9.0, t.x);
    CHECK_EQ(-123.456, t.n);
    CHECK_EQ(2718.2818, t.m);
    CHECK_EQ(2, t.i);
    CHECK_EQ(2718.2818, t.g);
    CHECK_EQ(31415926.5, t.h);
    CHECK_EQ(42.0, t.f);
    CHECK_EQ(1.0, t.e);
    CHECK_EQ(1.000000059604644775390625, t.d);
    CHECK_EQ(4.25, t.c);
    CHECK_EQ(4.25, t.b);
    CHECK_EQ(1.5, t.a);
  }
}


TEST(5) {
  // Test the ARMv7 bitfield instructions.
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatures::Scope scope(ARMv7);
    // On entry, r0 = 0xAAAAAAAA = 0b10..10101010.
    __ ubfx(r0, r0, 1, 12);  // 0b00..010101010101 = 0x555
    __ sbfx(r0, r0, 0, 5);   // 0b11..111111110101 = -11
    __ bfc(r0, 1, 3);        // 0b11..111111110001 = -15
    __ mov(r1, Operand(7));
    __ bfi(r0, r1, 3, 3);    // 0b11..111111111001 = -7
    __ mov(pc, Operand(lr));

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
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

  if (CpuFeatures::IsSupported(ARMv7)) {
    CpuFeatures::Scope scope(ARMv7);
    __ usat(r1, 8, Operand(r0));           // Sat 0xFFFF to 0-255 = 0xFF.
    __ usat(r2, 12, Operand(r0, ASR, 9));  // Sat (0xFFFF>>9) to 0-4095 = 0x7F.
    __ usat(r3, 1, Operand(r0, LSL, 16));  // Sat (0xFFFF<<16) to 0-1 = 0x0.
    __ addi(r0, r1, Operand(r2));
    __ addi(r0, r0, Operand(r3));
    __ mov(pc, Operand(lr));

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    int res = reinterpret_cast<int>(f.Call(0xFFFF, 0, 0, 0, 0));
    ::printf("f() = %d\n", res);
    CHECK_EQ(382, res);
  }
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
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});

  if (CpuFeatures::IsSupported(VFP3)) {
    CpuFeatures::Scope scope(VFP3);

    Label wrong_exception;

    __ vmrs(r1);
    // Set custom FPSCR.
    __ bic(r2, r1, Operand(kVFPRoundingModeMask | kVFPExceptionMask));
    __ orr(r2, r2, Operand(mode));
    __ vmsr(r2);

    // Load value, convert, and move back result to r0 if everything went well.
    __ vmov(d1, value);
    switch (types) {
      case s32_f64:
        __ vcvt_s32_f64(s0, d1, kFPSCRRounding);
        break;

      case u32_f64:
        __ vcvt_u32_f64(s0, d1, kFPSCRRounding);
        break;

      default:
        UNREACHABLE();
        break;
    }
    // Check for vfp exceptions
    __ vmrs(r2);
    __ tst(r2, Operand(kVFPExceptionMask));
    // Check that we behaved as expected.
    __ b(&wrong_exception,
         expected_exception ? eq : ne);
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
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
#endif
    auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
    int res = reinterpret_cast<int>(f.Call(0, 0, 0, 0, 0));
    ::printf("res = %d\n", res);
    CHECK_EQ(expected, res);
  }
}


TEST(7) {
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

  if (CpuFeatures::IsSupported(VFP2)) {
    CpuFeatures::Scope scope(VFP2);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});
    __ sub(fp, ip, Operand(4));

    __ addi(r4, r0, Operand(offsetof(D, a)));
    __ vldm(ia_w, r4, d0, d3);
    __ vldm(ia_w, r4, d4, d7);

    __ addi(r4, r0, Operand(offsetof(D, a)));
    __ vstm(ia_w, r4, d6, d7);
    __ vstm(ia_w, r4, d0, d5);

    __ addi(r4, r1, Operand(offsetof(F, a)));
    __ vldm(ia_w, r4, s0, s3);
    __ vldm(ia_w, r4, s4, s7);

    __ addi(r4, r1, Operand(offsetof(F, a)));
    __ vstm(ia_w, r4, s6, s7);
    __ vstm(ia_w, r4, s0, s5);

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
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

    CHECK_EQ(7.0, f.a);
    CHECK_EQ(8.0, f.b);
    CHECK_EQ(1.0, f.c);
    CHECK_EQ(2.0, f.d);
    CHECK_EQ(3.0, f.e);
    CHECK_EQ(4.0, f.f);
    CHECK_EQ(5.0, f.g);
    CHECK_EQ(6.0, f.h);
  }
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

  if (CpuFeatures::IsSupported(VFP2)) {
    CpuFeatures::Scope scope(VFP2);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});
    __ sub(fp, ip, Operand(4));

    __ addi(r4, r0, Operand(offsetof(D, a)));
    __ vldm(ia, r4, d0, d3);
    __ addi(r4, r4, Operand(4 * 8));
    __ vldm(ia, r4, d4, d7);

    __ addi(r4, r0, Operand(offsetof(D, a)));
    __ vstm(ia, r4, d6, d7);
    __ addi(r4, r4, Operand(2 * 8));
    __ vstm(ia, r4, d0, d5);

    __ addi(r4, r1, Operand(offsetof(F, a)));
    __ vldm(ia, r4, s0, s3);
    __ addi(r4, r4, Operand(4 * 4));
    __ vldm(ia, r4, s4, s7);

    __ addi(r4, r1, Operand(offsetof(F, a)));
    __ vstm(ia, r4, s6, s7);
    __ addi(r4, r4, Operand(2 * 4));
    __ vstm(ia, r4, s0, s5);

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
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

    CHECK_EQ(7.0, f.a);
    CHECK_EQ(8.0, f.b);
    CHECK_EQ(1.0, f.c);
    CHECK_EQ(2.0, f.d);
    CHECK_EQ(3.0, f.e);
    CHECK_EQ(4.0, f.f);
    CHECK_EQ(5.0, f.g);
    CHECK_EQ(6.0, f.h);
  }
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

  if (CpuFeatures::IsSupported(VFP2)) {
    CpuFeatures::Scope scope(VFP2);

    __ mov(ip, Operand(sp));
    __ stm(db_w, sp, {r4, fp, lr});
    __ sub(fp, ip, Operand(4));

    __ addi(r4, r0, Operand(offsetof(D, h) + 8));
    __ vldm(db_w, r4, d4, d7);
    __ vldm(db_w, r4, d0, d3);

    __ addi(r4, r0, Operand(offsetof(D, h) + 8));
    __ vstm(db_w, r4, d0, d5);
    __ vstm(db_w, r4, d6, d7);

    __ addi(r4, r1, Operand(offsetof(F, h) + 4));
    __ vldm(db_w, r4, s4, s7);
    __ vldm(db_w, r4, s0, s3);

    __ addi(r4, r1, Operand(offsetof(F, h) + 4));
    __ vstm(db_w, r4, s0, s5);
    __ vstm(db_w, r4, s6, s7);

    __ ldm(ia_w, sp, {r4, fp, pc});

    CodeDesc desc;
    assm.GetCode(isolate, &desc);
    Tagged<Object> code = isolate->heap()->CreateCode(
        desc,
        CodeKind::FOR_TESTING,
        Handle<Code>())->ToObjectChecked();
    CHECK(code->IsCode());
#ifdef DEBUG
    Cast<Code>(code)->Print();
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

    CHECK_EQ(7.0, f.a);
    CHECK_EQ(8.0, f.b);
    CHECK_EQ(1.0, f.c);
    CHECK_EQ(2.0, f.d);
    CHECK_EQ(3.0, f.e);
    CHECK_EQ(4.0, f.f);
    CHECK_EQ(5.0, f.g);
    CHECK_EQ(6.0, f.h);
  }
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
  Tagged<Object> code = isolate->heap()->CreateCode(
      desc,
      CodeKind::FOR_TESTING,
      Handle<Code>())->ToObjectChecked();
  CHECK(code->IsCode());
#ifdef DEBUG
  Cast<Code>(code)->Print();
#endif
  auto f = GeneratedCode<F_piiii>::FromCode(isolate, *code);
  f.Call(&i, 0, 0, 0, 0);

  CHECK_EQ(0xABCD0001, i.a);
  CHECK_EQ(static_cast<int32_t>(0xABCD0000) >> 1, i.b);
  CHECK_EQ(0x00000000, i.c);
  CHECK_EQ(0xFFFFFFFF, i.d);
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
#endif

TEST(WordSizedVectorInstructions) {
  CcTest::InitializeVM();
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  Assembler assm(AssemblerOptions{});
  // Testing word sized vector operations.
  __ li(r0, Operand(5));  // v0 = {5, 5, 5, 5}
  __ mtvsrd(v0, r0);
  __ vspltw(v0, v0, Operand(1));

  // Integer
  __ vadduwm(v1, v0, v0);   // v1 = {10, 10, 10, 10}
  __ vmuluwm(v2, v0, v1);   // v2 = {50, 50, 50, 50}
  __ vsubuhm(v3, v2, v0);   // v3 = {45, 45, 45, 45}
  __ vslw(v4, v2, v0);      // v4 = {1600, 1600, 1600, 1600}
  __ vsrw(v5, v2, v0);      // v5 = {1, 1, 1, 1}
  __ vmaxsw(v4, v5, v4);    // v4 = unchanged
  __ vcmpgtuw(v5, v2, v3);  // v5 = all 1s
  __ vand(v4, v4, v5);      // v4 = unchanged
  // FP
  __ xvcvsxwsp(v1, v1);    // v1 = Converted to SP
  __ xvcvsxwsp(v4, v4);    // v4 = Converted to SP
  __ xvdivsp(v4, v4, v1);  // v4 = {160, 160, 160, 160}
  // Integer
  __ xvcvspuxws(v4, v4);  // v4 = Converted to Int
  __ vor(v0, v4, v3);     // v0 = {173, 173, 173, 173}

  __ vupkhsw(v0, v0);  // v0 = {173, 173}
  __ mfvsrd(r3, v0);
  __ blr();

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef DEBUG
  Print(*code);
#endif
  auto f = GeneratedCode<F_iiiii>::FromCode(isolate, *code);
  intptr_t res = reinterpret_cast<intptr_t>(f.Call(0, 0, 0, 0, 0));
  ::printf("f() = %" V8PRIdPTR "\n", res);
  CHECK_EQ(173, static_cast<int>(res));
}

#undef __

}  // namespace internal
}  // namespace v8
