// Copyright 2009 the V8 project authors. All rights reserved.
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

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "include/v8-function.h"
#include "src/base/numbers/double.h"
#include "src/base/platform/platform.h"
#include "src/base/utils/random-number-generator.h"
#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "src/utils/ostreams.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace v8 {
namespace internal {

// Test the x64 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.
// The AMD64 calling convention is used, with the first six arguments
// in RDI, RSI, RDX, RCX, R8, and R9, and floating point arguments in
// the XMM registers.  The return value is in RAX.
// This calling convention is used on Linux, with GCC, and on Mac OS,
// with GCC.  A different convention is used on 64-bit windows,
// where the first four integer arguments are passed in RCX, RDX, R8 and R9.

using AssemblerX64Test = TestWithIsolate;

using F0 = int();
using F1 = int(int64_t x);
using F2 = int(int64_t x, int64_t y);
using F3 = unsigned(double x);
using F4 = uint64_t(uint64_t* x, uint64_t* y);
using F5 = uint64_t(uint64_t x);

#ifdef _WIN64
static const Register arg1 = rcx;
static const Register arg2 = rdx;
#else
static const Register arg1 = rdi;
static const Register arg2 = rsi;
#endif

#define __ masm.

TEST_F(AssemblerX64Test, AssemblerX64ReturnOperation) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that copies argument 2 and returns it.
  __ movq(rax, arg2);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(2, result);
}

TEST_F(AssemblerX64Test, AssemblerX64StackOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that copies argument 2 and returns it.
  // We compile without stack frame pointers, so the gdb debugger shows
  // incorrect stack frames when debugging this function (which has them).
  __ pushq(rbp);
  __ movq(rbp, rsp);
  __ pushq(arg2);  // Value at (rbp - 8)
  __ pushq(arg2);  // Value at (rbp - 16)
  __ pushq(arg1);  // Value at (rbp - 24)
  __ popq(rax);
  __ popq(rax);
  __ popq(rax);
  __ popq(rbp);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(2, result);
}

TEST_F(AssemblerX64Test, AssemblerX64ArithmeticOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that adds arguments returning the sum.
  __ movq(rax, arg2);
  __ addq(rax, arg1);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(5, result);
}

TEST_F(AssemblerX64Test, AssemblerX64CmpbOperation) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a function that compare argument byte returing 1 if equal else 0.
  // On Windows, it compares rcx with rdx which does not require REX prefix;
  // on Linux, it compares rdi with rsi which requires REX prefix.

  Label done;
  __ movq(rax, Immediate(1));
  __ cmpb(arg1, arg2);
  __ j(equal, &done);
  __ movq(rax, Immediate(0));
  __ bind(&done);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(0x1002, 0x2002);
  CHECK_EQ(1, result);
  result = f.Call(0x1002, 0x2003);
  CHECK_EQ(0, result);
}

TEST_F(AssemblerX64Test, AssemblerX64ImulOperation) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that multiplies arguments returning the high
  // word.
  __ movq(rax, arg2);
  __ imulq(arg1);
  __ movq(rax, rdx);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(0, result);
  result = f.Call(0x100000000l, 0x100000000l);
  CHECK_EQ(1, result);
  result = f.Call(-0x100000000l, 0x100000000l);
  CHECK_EQ(-1, result);
}

TEST_F(AssemblerX64Test, AssemblerX64testbwqOperation) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ pushq(rbx);
  __ pushq(rdi);
  __ pushq(rsi);
  __ pushq(r12);
  __ pushq(r13);
  __ pushq(r14);
  __ pushq(r15);

  // Assemble a simple function that tests testb and testw
  Label bad;
  Label done;

  // Test immediate testb and testw
  __ movq(rax, Immediate(2));
  __ movq(rbx, Immediate(4));
  __ movq(rcx, Immediate(8));
  __ movq(rdx, Immediate(16));
  __ movq(rsi, Immediate(32));
  __ movq(rdi, Immediate(64));
  __ movq(r10, Immediate(128));
  __ movq(r11, Immediate(0));
  __ movq(r12, Immediate(0));
  __ movq(r13, Immediate(0));
  __ testb(rax, Immediate(2));
  __ j(zero, &bad);
  __ testb(rbx, Immediate(4));
  __ j(zero, &bad);
  __ testb(rcx, Immediate(8));
  __ j(zero, &bad);
  __ testb(rdx, Immediate(16));
  __ j(zero, &bad);
  __ testb(rsi, Immediate(32));
  __ j(zero, &bad);
  __ testb(rdi, Immediate(64));
  __ j(zero, &bad);
  __ testb(r10, Immediate(128));
  __ j(zero, &bad);
  __ testw(rax, Immediate(2));
  __ j(zero, &bad);
  __ testw(rbx, Immediate(4));
  __ j(zero, &bad);
  __ testw(rcx, Immediate(8));
  __ j(zero, &bad);
  __ testw(rdx, Immediate(16));
  __ j(zero, &bad);
  __ testw(rsi, Immediate(32));
  __ j(zero, &bad);
  __ testw(rdi, Immediate(64));
  __ j(zero, &bad);
  __ testw(r10, Immediate(128));
  __ j(zero, &bad);

  // Test reg, reg testb and testw
  __ movq(rax, Immediate(2));
  __ movq(rbx, Immediate(2));
  __ testb(rax, rbx);
  __ j(zero, &bad);
  __ movq(rbx, Immediate(4));
  __ movq(rax, Immediate(4));
  __ testb(rbx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(8));
  __ testb(rcx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(16));
  __ testb(rdx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(32));
  __ testb(rsi, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(64));
  __ testb(rdi, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(128));
  __ testb(r10, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(2));
  __ movq(rbx, Immediate(2));
  __ testw(rax, rbx);
  __ j(zero, &bad);
  __ movq(rbx, Immediate(4));
  __ movq(rax, Immediate(4));
  __ testw(rbx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(8));
  __ testw(rcx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(16));
  __ testw(rdx, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(32));
  __ testw(rsi, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(64));
  __ testw(rdi, rax);
  __ j(zero, &bad);
  __ movq(rax, Immediate(128));
  __ testw(r10, rax);
  __ j(zero, &bad);

  // Test diffrrent extended register coding combinations.
  __ movq(rax, Immediate(5));
  __ movq(r11, Immediate(5));
  __ testb(r11, rax);
  __ j(zero, &bad);
  __ testb(rax, r11);
  __ j(zero, &bad);
  __ testw(r11, rax);
  __ j(zero, &bad);
  __ testw(rax, r11);
  __ j(zero, &bad);
  __ movq(r11, Immediate(3));
  __ movq(r12, Immediate(3));
  __ movq(rdi, Immediate(3));
  __ testb(r12, rdi);
  __ j(zero, &bad);
  __ testb(rdi, r12);
  __ j(zero, &bad);
  __ testb(r12, r11);
  __ j(zero, &bad);
  __ testb(r11, r12);
  __ j(zero, &bad);
  __ testw(r12, r11);
  __ j(zero, &bad);
  __ testw(r11, r12);
  __ j(zero, &bad);

  // Test sign-extended imediate tests
  __ movq(r11, Immediate(2));
  __ shlq(r11, Immediate(32));
  __ testq(r11, Immediate(-1));
  __ j(zero, &bad);

  // All tests passed
  __ movq(rax, Immediate(1));
  __ jmp(&done);

  __ bind(&bad);
  __ movq(rax, Immediate(0));
  __ bind(&done);

  __ popq(r15);
  __ popq(r14);
  __ popq(r13);
  __ popq(r12);
  __ popq(rsi);
  __ popq(rdi);
  __ popq(rbx);

  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(0, 0);
  CHECK_EQ(1, result);
}

TEST_F(AssemblerX64Test, AssemblerX64XchglOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(rax, Operand(arg1, 0));
  __ movq(r11, Operand(arg2, 0));
  __ xchgl(rax, r11);
  __ movq(Operand(arg1, 0), rax);
  __ movq(Operand(arg2, 0), r11);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t left = 0x1000'0000'2000'0000;
  uint64_t right = 0x3000'0000'4000'0000;
  auto f = GeneratedCode<F4>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(&left, &right);
  CHECK_EQ(0x0000'0000'4000'0000, left);
  CHECK_EQ(0x0000'0000'2000'0000, right);
  USE(result);
}

TEST_F(AssemblerX64Test, AssemblerX64OrlOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(rax, Operand(arg2, 0));
  __ orl(Operand(arg1, 0), rax);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t left = 0x1000'0000'2000'0000;
  uint64_t right = 0x3000'0000'4000'0000;
  auto f = GeneratedCode<F4>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(&left, &right);
  CHECK_EQ(0x1000'0000'6000'0000, left);
  USE(result);
}

TEST_F(AssemblerX64Test, AssemblerX64RollOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(rax, arg1);
  __ roll(rax, Immediate(1));
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t src = 0x1000'0000'C000'0000;
  auto f = GeneratedCode<F5>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(src);
  CHECK_EQ(0x0000'0000'8000'0001, result);
}

TEST_F(AssemblerX64Test, AssemblerX64SublOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(rax, Operand(arg2, 0));
  __ subl(Operand(arg1, 0), rax);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t left = 0x1000'0000'2000'0000;
  uint64_t right = 0x3000'0000'4000'0000;
  auto f = GeneratedCode<F4>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(&left, &right);
  CHECK_EQ(0x1000'0000'E000'0000, left);
  USE(result);
}

TEST_F(AssemblerX64Test, AssemblerX64TestlOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Set rax with the ZF flag of the testl instruction.
  Label done;
  __ movq(rax, Immediate(1));
  __ movq(r11, Operand(arg2, 0));
  __ testl(Operand(arg1, 0), r11);
  __ j(zero, &done, Label::kNear);
  __ movq(rax, Immediate(0));
  __ bind(&done);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t left = 0x1000'0000'2000'0000;
  uint64_t right = 0x3000'0000'0000'0000;
  auto f = GeneratedCode<F4>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(&left, &right);
  CHECK_EQ(1u, result);
}

TEST_F(AssemblerX64Test, AssemblerX64TestwOperations) {
  using F = uint16_t(uint16_t * x);

  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Set rax with the ZF flag of the testl instruction.
  Label done;
  __ movq(rax, Immediate(1));
  __ testw(Operand(arg1, 0), Immediate(0xF0F0));
  __ j(not_zero, &done, Label::kNear);
  __ movq(rax, Immediate(0));
  __ bind(&done);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint16_t operand = 0x8000;
  auto f = GeneratedCode<F>::FromBuffer(i_isolate(), buffer->start());
  uint16_t result = f.Call(&operand);
  CHECK_EQ(1u, result);
}

TEST_F(AssemblerX64Test, AssemblerX64XorlOperations) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(rax, Operand(arg2, 0));
  __ xorl(Operand(arg1, 0), rax);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  uint64_t left = 0x1000'0000'2000'0000;
  uint64_t right = 0x3000'0000'6000'0000;
  auto f = GeneratedCode<F4>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(&left, &right);
  CHECK_EQ(0x1000'0000'4000'0000, left);
  USE(result);
}

TEST_F(AssemblerX64Test, AssemblerX64MemoryOperands) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that copies argument 2 and returns it.
  __ pushq(rbp);
  __ movq(rbp, rsp);

  __ pushq(arg2);  // Value at (rbp - 8)
  __ pushq(arg2);  // Value at (rbp - 16)
  __ pushq(arg1);  // Value at (rbp - 24)

  const int kStackElementSize = 8;
  __ movq(rax, Operand(rbp, -3 * kStackElementSize));
  __ popq(arg2);
  __ popq(arg2);
  __ popq(arg2);
  __ popq(rbp);
  __ nop();
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(3, result);
}

TEST_F(AssemblerX64Test, AssemblerX64ControlFlow) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble a simple function that copies argument 1 and returns it.
  __ pushq(rbp);

  __ movq(rbp, rsp);
  __ movq(rax, arg1);
  Label target;
  __ jmp(&target);
  __ movq(rax, arg2);
  __ bind(&target);
  __ popq(rbp);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call(3, 2);
  CHECK_EQ(3, result);
}

TEST_F(AssemblerX64Test, AssemblerX64LoopImmediates) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  // Assemble two loops using rax as counter, and verify the ending counts.
  Label Fail;
  __ movq(rax, Immediate(-3));
  Label Loop1_test;
  Label Loop1_body;
  __ jmp(&Loop1_test);
  __ bind(&Loop1_body);
  __ addq(rax, Immediate(7));
  __ bind(&Loop1_test);
  __ cmpq(rax, Immediate(20));
  __ j(less_equal, &Loop1_body);
  // Did the loop terminate with the expected value?
  __ cmpq(rax, Immediate(25));
  __ j(not_equal, &Fail);

  Label Loop2_test;
  Label Loop2_body;
  __ movq(rax, Immediate(0x11FEED00));
  __ jmp(&Loop2_test);
  __ bind(&Loop2_body);
  __ addq(rax, Immediate(-0x1100));
  __ bind(&Loop2_test);
  __ cmpq(rax, Immediate(0x11FE8000));
  __ j(greater, &Loop2_body);
  // Did the loop terminate with the expected value?
  __ cmpq(rax, Immediate(0x11FE7600));
  __ j(not_equal, &Fail);

  __ movq(rax, Immediate(1));
  __ ret(0);
  __ bind(&Fail);
  __ movq(rax, Immediate(0));
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(1, result);
}

TEST_F(AssemblerX64Test, OperandRegisterDependency) {
  int offsets[4] = {0, 1, 0xFED, 0xBEEFCAD};
  for (int i = 0; i < 4; i++) {
    int offset = offsets[i];
    CHECK(Operand(rax, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rax, offset).AddressUsesRegister(r8));
    CHECK(!Operand(rax, offset).AddressUsesRegister(rcx));

    CHECK(Operand(rax, rax, times_1, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rax, rax, times_1, offset).AddressUsesRegister(r8));
    CHECK(!Operand(rax, rax, times_1, offset).AddressUsesRegister(rcx));

    CHECK(Operand(rax, rcx, times_1, offset).AddressUsesRegister(rax));
    CHECK(Operand(rax, rcx, times_1, offset).AddressUsesRegister(rcx));
    CHECK(!Operand(rax, rcx, times_1, offset).AddressUsesRegister(r8));
    CHECK(!Operand(rax, rcx, times_1, offset).AddressUsesRegister(r9));
    CHECK(!Operand(rax, rcx, times_1, offset).AddressUsesRegister(rdx));
    CHECK(!Operand(rax, rcx, times_1, offset).AddressUsesRegister(rsp));

    CHECK(Operand(rsp, offset).AddressUsesRegister(rsp));
    CHECK(!Operand(rsp, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rsp, offset).AddressUsesRegister(r15));

    CHECK(Operand(rbp, offset).AddressUsesRegister(rbp));
    CHECK(!Operand(rbp, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rbp, offset).AddressUsesRegister(r13));

    CHECK(Operand(rbp, rax, times_1, offset).AddressUsesRegister(rbp));
    CHECK(Operand(rbp, rax, times_1, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rbp, rax, times_1, offset).AddressUsesRegister(rcx));
    CHECK(!Operand(rbp, rax, times_1, offset).AddressUsesRegister(r13));
    CHECK(!Operand(rbp, rax, times_1, offset).AddressUsesRegister(r8));
    CHECK(!Operand(rbp, rax, times_1, offset).AddressUsesRegister(rsp));

    CHECK(Operand(rsp, rbp, times_1, offset).AddressUsesRegister(rsp));
    CHECK(Operand(rsp, rbp, times_1, offset).AddressUsesRegister(rbp));
    CHECK(!Operand(rsp, rbp, times_1, offset).AddressUsesRegister(rax));
    CHECK(!Operand(rsp, rbp, times_1, offset).AddressUsesRegister(r15));
    CHECK(!Operand(rsp, rbp, times_1, offset).AddressUsesRegister(r13));
  }
}

TEST_F(AssemblerX64Test, AssemblerX64LabelChaining) {
  // Test chaining of label usages within instructions (issue 1644).

  Assembler masm(AssemblerOptions{});

  Label target;
  __ j(equal, &target);
  __ j(not_equal, &target);
  __ bind(&target);
  __ nop();
}

TEST_F(AssemblerX64Test, AssemblerMultiByteNop) {
  uint8_t buffer[1024];
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  __ pushq(rbx);
  __ pushq(rcx);
  __ pushq(rdx);
  __ pushq(rdi);
  __ pushq(rsi);
  __ movq(rax, Immediate(1));
  __ movq(rbx, Immediate(2));
  __ movq(rcx, Immediate(3));
  __ movq(rdx, Immediate(4));
  __ movq(rdi, Immediate(5));
  __ movq(rsi, Immediate(6));
  for (int i = 0; i < 16; i++) {
    int before = masm.pc_offset();
    __ Nop(i);
    CHECK_EQ(masm.pc_offset() - before, i);
  }

  Label fail;
  __ cmpq(rax, Immediate(1));
  __ j(not_equal, &fail);
  __ cmpq(rbx, Immediate(2));
  __ j(not_equal, &fail);
  __ cmpq(rcx, Immediate(3));
  __ j(not_equal, &fail);
  __ cmpq(rdx, Immediate(4));
  __ j(not_equal, &fail);
  __ cmpq(rdi, Immediate(5));
  __ j(not_equal, &fail);
  __ cmpq(rsi, Immediate(6));
  __ j(not_equal, &fail);
  __ movq(rax, Immediate(42));
  __ popq(rsi);
  __ popq(rdi);
  __ popq(rdx);
  __ popq(rcx);
  __ popq(rbx);
  __ ret(0);
  __ bind(&fail);
  __ movq(rax, Immediate(13));
  __ popq(rsi);
  __ popq(rdi);
  __ popq(rdx);
  __ popq(rcx);
  __ popq(rbx);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F0>::FromCode(isolate, *code);
  int res = f.Call();
  CHECK_EQ(42, res);
}

#ifdef __GNUC__
#define ELEMENT_COUNT 4u

void DoSSE2(const v8::FunctionCallbackInfo<v8::Value>& info) {
  CHECK(i::ValidateCallbackInfo(info));
  v8::Isolate* isolate = info.GetIsolate();
  v8::HandleScope scope(isolate);
  v8::Local<v8::Context> context = isolate->GetCurrentContext();
  uint8_t buffer[1024];

  CHECK(info[0]->IsArray());
  v8::Local<v8::Array> vec = v8::Local<v8::Array>::Cast(info[0]);
  CHECK_EQ(ELEMENT_COUNT, vec->Length());

  Isolate* i_isolate = reinterpret_cast<v8::internal::Isolate*>(isolate);
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));

  // Remove return address from the stack for fix stack frame alignment.
  __ popq(rcx);

  // Store input vector on the stack.
  for (unsigned i = 0; i < ELEMENT_COUNT; i++) {
    __ movl(rax, Immediate(vec->Get(context, i)
                               .ToLocalChecked()
                               ->Int32Value(context)
                               .FromJust()));
    __ shlq(rax, Immediate(0x20));
    __ orq(rax, Immediate(vec->Get(context, ++i)
                              .ToLocalChecked()
                              ->Int32Value(context)
                              .FromJust()));
    __ pushq(rax);
  }

  // Read vector into a xmm register.
  __ xorps(xmm0, xmm0);
  __ movdqa(xmm0, Operand(rsp, 0));
  // Create mask and store it in the return register.
  __ movmskps(rax, xmm0);

  // Remove unused data from the stack.
  __ addq(rsp, Immediate(ELEMENT_COUNT * sizeof(int32_t)));
  // Restore return address.
  __ pushq(rcx);

  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(i_isolate, desc, CodeKind::FOR_TESTING).Build();

  auto f = GeneratedCode<F0>::FromCode(i_isolate, *code);
  int res = f.Call();
  info.GetReturnValue().Set(v8::Integer::New(isolate, res));
}

TEST_F(AssemblerX64Test, StackAlignmentForSSE2) {
  CHECK_EQ(0, v8::base::OS::ActivationFrameAlignment() % 16);

  v8::Isolate* isolate = v8_isolate();
  v8::HandleScope handle_scope(isolate);
  v8::Local<v8::ObjectTemplate> global_template =
      v8::ObjectTemplate::New(isolate);
  global_template->Set(isolate, "do_sse2",
                       v8::FunctionTemplate::New(isolate, DoSSE2));

  v8::Local<v8::Context> context =
      v8::Context::New(isolate, nullptr, global_template);
  v8::Context::Scope context_scope(context);
  TryRunJS(
      "function foo(vec) {"
      "  return do_sse2(vec);"
      "}");

  v8::Local<v8::Object> global_object = context->Global();
  v8::Local<v8::Function> foo = v8::Local<v8::Function>::Cast(
      global_object->Get(context, NewString("foo")).ToLocalChecked());

  int32_t vec[ELEMENT_COUNT] = {-1, 1, 1, 1};
  v8::Local<v8::Array> v8_vec = v8::Array::New(isolate, ELEMENT_COUNT);
  for (unsigned i = 0; i < ELEMENT_COUNT; i++) {
    v8_vec->Set(context, i, v8::Number::New(isolate, vec[i])).FromJust();
  }

  v8::Local<v8::Value> args[] = {v8_vec};
  v8::Local<v8::Value> result =
      foo->Call(context, global_object, 1, args).ToLocalChecked();

  // The mask should be 0b1000.
  CHECK_EQ(8, result->Int32Value(context).FromJust());
}

#undef ELEMENT_COUNT
#endif  // __GNUC__

TEST_F(AssemblerX64Test, AssemblerX64Extractps) {
  if (!CpuFeatures::IsSupported(SSE4_1)) return;

  uint8_t buffer[256];
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope2(&masm, SSE4_1);
    __ extractps(rax, xmm0, 0x1);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F3>::FromCode(isolate, *code);
  uint64_t value1 = 0x1234'5678'8765'4321;
  CHECK_EQ(0x12345678u, f.Call(base::uint64_to_double(value1)));
  uint64_t value2 = 0x8765'4321'1234'5678;
  CHECK_EQ(0x87654321u, f.Call(base::uint64_to_double(value2)));
}

using F6 = int(float x, float y);
TEST_F(AssemblerX64Test, AssemblerX64SSE) {
  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[256];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    __ shufps(xmm0, xmm0, 0x0);  // brocast first argument
    __ shufps(xmm1, xmm1, 0x0);  // brocast second argument
    __ movaps(xmm2, xmm1);
    __ addps(xmm2, xmm0);
    __ mulps(xmm2, xmm1);
    __ subps(xmm2, xmm0);
    __ divps(xmm2, xmm1);
    __ cvttss2si(rax, xmm2);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F6>::FromCode(isolate, *code);
  CHECK_EQ(2, f.Call(1.0, 2.0));
}

TEST_F(AssemblerX64Test, AssemblerX64SSE3) {
  if (!CpuFeatures::IsSupported(SSE3)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[256];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, SSE3);
    __ shufps(xmm0, xmm0, 0x0);  // brocast first argument
    __ shufps(xmm1, xmm1, 0x0);  // brocast second argument
    __ haddps(xmm1, xmm0);
    __ cvttss2si(rax, xmm1);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F6>::FromCode(isolate, *code);
  CHECK_EQ(4, f.Call(1.0, 2.0));
}

using F7 = int(double x, double y, double z);
TEST_F(AssemblerX64Test, AssemblerX64FMA_sd) {
  if (!CpuFeatures::IsSupported(FMA3)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, FMA3);
    Label exit;
    // argument in xmm0, xmm1 and xmm2
    // xmm0 * xmm1 + xmm2
    __ movaps(xmm3, xmm0);
    __ mulsd(xmm3, xmm1);
    __ addsd(xmm3, xmm2);  // Expected result in xmm3

    __ AllocateStackSpace(kDoubleSize);  // For memory operand
    // vfmadd132sd
    __ movl(rax, Immediate(1));  // Test number
    __ movaps(xmm8, xmm0);
    __ vfmadd132sd(xmm8, xmm2, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfmadd213sd(xmm8, xmm0, xmm2);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfmadd231sd(xmm8, xmm0, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfmadd132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfmadd132sd(xmm8, xmm2, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movsd(Operand(rsp, 0), xmm2);
    __ vfmadd213sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfmadd231sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // xmm0 * xmm1 - xmm2
    __ movaps(xmm3, xmm0);
    __ mulsd(xmm3, xmm1);
    __ subsd(xmm3, xmm2);  // Expected result in xmm3

    // vfmsub132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfmsub132sd(xmm8, xmm2, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfmsub213sd(xmm8, xmm0, xmm2);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfmsub231sd(xmm8, xmm0, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfmsub132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfmsub132sd(xmm8, xmm2, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movsd(Operand(rsp, 0), xmm2);
    __ vfmsub213sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfmsub231sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // - xmm0 * xmm1 + xmm2
    __ movaps(xmm3, xmm0);
    __ mulsd(xmm3, xmm1);
    __ Move(xmm4, static_cast<uint64_t>(1) << 63);
    __ xorpd(xmm3, xmm4);
    __ addsd(xmm3, xmm2);  // Expected result in xmm3

    // vfnmadd132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfnmadd132sd(xmm8, xmm2, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfnmadd213sd(xmm8, xmm0, xmm2);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfnmadd231sd(xmm8, xmm0, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfnmadd132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfnmadd132sd(xmm8, xmm2, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movsd(Operand(rsp, 0), xmm2);
    __ vfnmadd213sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfnmadd231sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // - xmm0 * xmm1 - xmm2
    __ movaps(xmm3, xmm0);
    __ mulsd(xmm3, xmm1);
    __ Move(xmm4, static_cast<uint64_t>(1) << 63);
    __ xorpd(xmm3, xmm4);
    __ subsd(xmm3, xmm2);  // Expected result in xmm3

    // vfnmsub132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfnmsub132sd(xmm8, xmm2, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfnmsub213sd(xmm8, xmm0, xmm2);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfnmsub231sd(xmm8, xmm0, xmm1);
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfnmsub132sd
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfnmsub132sd(xmm8, xmm2, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub213sd
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movsd(Operand(rsp, 0), xmm2);
    __ vfnmsub213sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub231sd
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movsd(Operand(rsp, 0), xmm1);
    __ vfnmsub231sd(xmm8, xmm0, Operand(rsp, 0));
    __ ucomisd(xmm8, xmm3);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ addq(rsp, Immediate(kDoubleSize));
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F7>::FromCode(isolate, *code);
  CHECK_EQ(
      0, f.Call(0.000092662107262076, -2.460774966188315, -1.0958787393627414));
}

using F8 = int(float x, float y, float z);
TEST_F(AssemblerX64Test, AssemblerX64FMA_ss) {
  if (!CpuFeatures::IsSupported(FMA3)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, FMA3);
    Label exit;
    // arguments in xmm0, xmm1 and xmm2
    // xmm0 * xmm1 + xmm2
    __ movaps(xmm3, xmm0);
    __ mulss(xmm3, xmm1);
    __ addss(xmm3, xmm2);  // Expected result in xmm3

    __ AllocateStackSpace(kDoubleSize);  // For memory operand
    // vfmadd132ss
    __ movl(rax, Immediate(1));  // Test number
    __ movaps(xmm8, xmm0);
    __ vfmadd132ss(xmm8, xmm2, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfmadd213ss(xmm8, xmm0, xmm2);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfmadd231ss(xmm8, xmm0, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfmadd132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfmadd132ss(xmm8, xmm2, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movss(Operand(rsp, 0), xmm2);
    __ vfmadd213ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfmadd231ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // xmm0 * xmm1 - xmm2
    __ movaps(xmm3, xmm0);
    __ mulss(xmm3, xmm1);
    __ subss(xmm3, xmm2);  // Expected result in xmm3

    // vfmsub132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfmsub132ss(xmm8, xmm2, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfmsub213ss(xmm8, xmm0, xmm2);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfmsub231ss(xmm8, xmm0, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfmsub132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfmsub132ss(xmm8, xmm2, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movss(Operand(rsp, 0), xmm2);
    __ vfmsub213ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfmsub231ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // - xmm0 * xmm1 + xmm2
    __ movaps(xmm3, xmm0);
    __ mulss(xmm3, xmm1);
    __ Move(xmm4, static_cast<uint32_t>(1) << 31);
    __ xorps(xmm3, xmm4);
    __ addss(xmm3, xmm2);  // Expected result in xmm3

    // vfnmadd132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfnmadd132ss(xmm8, xmm2, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmadd213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfnmadd213ss(xmm8, xmm0, xmm2);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfnmadd231ss(xmm8, xmm0, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfnmadd132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfnmadd132ss(xmm8, xmm2, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movss(Operand(rsp, 0), xmm2);
    __ vfnmadd213ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmadd231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfnmadd231ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // - xmm0 * xmm1 - xmm2
    __ movaps(xmm3, xmm0);
    __ mulss(xmm3, xmm1);
    __ Move(xmm4, static_cast<uint32_t>(1) << 31);
    __ xorps(xmm3, xmm4);
    __ subss(xmm3, xmm2);  // Expected result in xmm3

    // vfnmsub132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ vfnmsub132ss(xmm8, xmm2, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfmsub213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ vfnmsub213ss(xmm8, xmm0, xmm2);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ vfnmsub231ss(xmm8, xmm0, xmm1);
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    // vfnmsub132ss
    __ incq(rax);
    __ movaps(xmm8, xmm0);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfnmsub132ss(xmm8, xmm2, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub213ss
    __ incq(rax);
    __ movaps(xmm8, xmm1);
    __ movss(Operand(rsp, 0), xmm2);
    __ vfnmsub213ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);
    // vfnmsub231ss
    __ incq(rax);
    __ movaps(xmm8, xmm2);
    __ movss(Operand(rsp, 0), xmm1);
    __ vfnmsub231ss(xmm8, xmm0, Operand(rsp, 0));
    __ ucomiss(xmm8, xmm3);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ addq(rsp, Immediate(kDoubleSize));
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F8>::FromCode(isolate, *code);
  CHECK_EQ(0, f.Call(9.26621069e-05f, -2.4607749f, -1.09587872f));
}

TEST_F(AssemblerX64Test, AssemblerX64SSE_ss) {
  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    Label exit;
    // arguments in xmm0, xmm1 and xmm2
    __ movl(rax, Immediate(0));

    __ movaps(xmm3, xmm0);
    __ maxss(xmm3, xmm1);
    __ ucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(1));

    __ movaps(xmm3, xmm1);
    __ minss(xmm3, xmm2);
    __ ucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(2));

    __ movaps(xmm3, xmm2);
    __ subss(xmm3, xmm1);
    __ ucomiss(xmm3, xmm0);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(3));

    __ movaps(xmm3, xmm0);
    __ addss(xmm3, xmm1);
    __ ucomiss(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(4));

    __ movaps(xmm3, xmm0);
    __ mulss(xmm3, xmm1);
    __ ucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(5));

    __ movaps(xmm3, xmm0);
    __ divss(xmm3, xmm1);
    __ mulss(xmm3, xmm2);
    __ mulss(xmm3, xmm1);
    __ ucomiss(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(6));

    // result in eax
    __ bind(&exit);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F8>::FromCode(isolate, *code);
  int res = f.Call(1.0f, 2.0f, 3.0f);
  PrintF("f(1,2,3) = %d\n", res);
  CHECK_EQ(6, res);
}

TEST_F(AssemblerX64Test, AssemblerX64AVX_ss) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope avx_scope(&masm, AVX);
    Label exit;
    // arguments in xmm0, xmm1 and xmm2
    __ subq(rsp, Immediate(kDoubleSize * 2));  // For memory operand

    __ movl(rdx, Immediate(0xC2F64000));  // -123.125
    __ vmovd(xmm4, rdx);
    __ vmovss(Operand(rsp, 0), xmm4);
    __ vmovss(xmm5, Operand(rsp, 0));
    __ vmovaps(xmm6, xmm5);
    __ vmovd(rcx, xmm6);
    __ cmpl(rcx, rdx);
    __ movl(rax, Immediate(9));
    __ j(not_equal, &exit);

    __ movl(rax, Immediate(0));
    __ vmaxss(xmm3, xmm0, xmm1);
    __ vucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(1));

    __ vminss(xmm3, xmm1, xmm2);
    __ vucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(2));

    __ vsubss(xmm3, xmm2, xmm1);
    __ vucomiss(xmm3, xmm0);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(3));

    __ vaddss(xmm3, xmm0, xmm1);
    __ vucomiss(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(4));

    __ vmulss(xmm3, xmm0, xmm1);
    __ vucomiss(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(5));

    __ vdivss(xmm3, xmm0, xmm1);
    __ vmulss(xmm3, xmm3, xmm2);
    __ vmulss(xmm3, xmm3, xmm1);
    __ vucomiss(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(6));

    // result in eax
    __ bind(&exit);
    __ addq(rsp, Immediate(kDoubleSize * 2));
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F8>::FromCode(isolate, *code);
  int res = f.Call(1.0f, 2.0f, 3.0f);
  PrintF("f(1,2,3) = %d\n", res);
  CHECK_EQ(6, res);
}

TEST_F(AssemblerX64Test, AssemblerX64AVX_sd) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  Assembler masm(AssemblerOptions{},
                 ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope avx_scope(&masm, AVX);
    Label exit;
    // arguments in xmm0, xmm1 and xmm2
    __ subq(rsp, Immediate(kDoubleSize * 2));  // For memory operand
    __ movl(rax, Immediate(0));

    __ vmaxsd(xmm4, xmm0, xmm1);
    __ vmovsd(Operand(rsp, kDoubleSize), xmm4);
    __ vmovsd(xmm5, Operand(rsp, kDoubleSize));
    __ vmovsd(xmm6, xmm6, xmm5);
    __ vmovapd(xmm3, xmm6);

    // Test vcvtss2sd & vcvtsd2ss
    __ movl(rax, Immediate(9));
    __ movq(rdx, uint64_t{0x426D1A0000000000});
    __ movq(Operand(rsp, 0), rdx);
    __ vcvtsd2ss(xmm6, xmm6, Operand(rsp, 0));
    __ vcvtss2sd(xmm7, xmm6, xmm6);
    __ vcvtsd2ss(xmm8, xmm7, xmm7);
    __ vmovss(Operand(rsp, 0), xmm8);
    __ vcvtss2sd(xmm9, xmm8, Operand(rsp, 0));
    __ vmovq(rcx, xmm9);
    __ cmpq(rcx, rdx);
    __ j(not_equal, &exit);

    // Test vcvttsd2si
    __ movl(rax, Immediate(10));
    __ movl(rdx, Immediate(123));
    __ vcvtlsi2sd(xmm6, xmm6, rdx);
    __ vcvttsd2si(rcx, xmm6);
    __ cmpl(rcx, rdx);
    __ j(not_equal, &exit);
    __ xorl(rcx, rcx);
    __ vmovsd(Operand(rsp, 0), xmm6);
    __ vcvttsd2si(rcx, Operand(rsp, 0));
    __ cmpl(rcx, rdx);
    __ j(not_equal, &exit);

    // Test vcvttsd2siq
    __ movl(rax, Immediate(11));
    __ movq(rdx, uint64_t{0x426D1A94A2000000});  // 1.0e12
    __ vmovq(xmm6, rdx);
    __ vcvttsd2siq(rcx, xmm6);
    __ movq(rdx, uint64_t{1000000000000});
    __ cmpq(rcx, rdx);
    __ j(not_equal, &exit);
    __ xorq(rcx, rcx);
    __ vmovsd(Operand(rsp, 0), xmm6);
    __ vcvttsd2siq(rcx, Operand(rsp, 0));
    __ cmpq(rcx, rdx);
    __ j(not_equal, &exit);

    // Test vmovmskpd
    __ movl(rax, Immediate(12));
    __ movq(rdx, uint64_t{0x426D1A94A2000000});  // 1.0e12
    __ vmovq(xmm6, rdx);
    __ movq(rdx, uint64_t{0xC26D1A94A2000000});  // -1.0e12
    __ vmovq(xmm7, rdx);
    __ shufps(xmm6, xmm7, 0x44);
    __ vmovmskpd(rdx, xmm6);
    __ cmpl(rdx, Immediate(2));
    __ j(not_equal, &exit);

    // Test vpcmpeqd
    __ movq(rdx, uint64_t{0x0123456789ABCDEF});
    __ movq(rcx, uint64_t{0x0123456788888888});
    __ vmovq(xmm6, rdx);
    __ vmovq(xmm7, rcx);
    __ vpcmpeqd(xmm8, xmm6, xmm7);
    __ vmovq(rdx, xmm8);
    __ movq(rcx, uint64_t{0xFFFFFFFF00000000});
    __ cmpq(rcx, rdx);
    __ movl(rax, Immediate(13));
    __ j(not_equal, &exit);

    // Test vpsllq, vpsrlq
    __ movl(rax, Immediate(13));
    __ movq(rdx, uint64_t{0x0123456789ABCDEF});
    __ vmovq(xmm6, rdx);
    __ vpsrlq(xmm7, xmm6, 4);
    __ vmovq(rdx, xmm7);
    __ movq(rcx, uint64_t{0x00123456789ABCDE});
    __ cmpq(rdx, rcx);
    __ j(not_equal, &exit);
    __ vpsllq(xmm7, xmm6, 12);
    __ vmovq(rdx, xmm7);
    __ movq(rcx, uint64_t{0x3456789ABCDEF000});
    __ cmpq(rdx, rcx);
    __ j(not_equal, &exit);

    // Test vandpd, vorpd, vxorpd
    __ movl(rax, Immediate(14));
    __ movl(rdx, Immediate(0x00FF00FF));
    __ movl(rcx, Immediate(0x0F0F0F0F));
    __ vmovd(xmm4, rdx);
    __ vmovd(xmm5, rcx);
    __ vandpd(xmm6, xmm4, xmm5);
    __ vmovd(rdx, xmm6);
    __ cmpl(rdx, Immediate(0x000F000F));
    __ j(not_equal, &exit);
    __ vorpd(xmm6, xmm4, xmm5);
    __ vmovd(rdx, xmm6);
    __ cmpl(rdx, Immediate(0x0FFF0FFF));
    __ j(not_equal, &exit);
    __ vxorpd(xmm6, xmm4, xmm5);
    __ vmovd(rdx, xmm6);
    __ cmpl(rdx, Immediate(0x0FF00FF0));
    __ j(not_equal, &exit);

    // Test vsqrtsd
    __ movl(rax, Immediate(15));
    __ movq(rdx, uint64_t{0x4004000000000000});  // 2.5
    __ vmovq(xmm4, rdx);
    __ vmulsd(xmm5, xmm4, xmm4);
    __ vmovsd(Operand(rsp, 0), xmm5);
    __ vsqrtsd(xmm6, xmm5, xmm5);
    __ vmovq(rcx, xmm6);
    __ cmpq(rcx, rdx);
    __ j(not_equal, &exit);
    __ vsqrtsd(xmm7, xmm7, Operand(rsp, 0));
    __ vmovq(rcx, xmm7);
    __ cmpq(rcx, rdx);
    __ j(not_equal, &exit);

    // Test vroundsd
    __ movl(rax, Immediate(16));
    __ movq(rdx, uint64_t{0x4002000000000000});  // 2.25
    __ vmovq(xmm4, rdx);
    __ vroundsd(xmm5, xmm4, xmm4, kRoundUp);
    __ movq(rcx, uint64_t{0x4008000000000000});  // 3.0
    __ vmovq(xmm6, rcx);
    __ vucomisd(xmm5, xmm6);
    __ j(not_equal, &exit);

    // Test vcvtlsi2sd
    __ movl(rax, Immediate(17));
    __ movl(rdx, Immediate(6));
    __ movq(rcx, uint64_t{0x4018000000000000});  // 6.0
    __ vmovq(xmm5, rcx);
    __ vcvtlsi2sd(xmm6, xmm6, rdx);
    __ vucomisd(xmm5, xmm6);
    __ j(not_equal, &exit);
    __ movl(Operand(rsp, 0), rdx);
    __ vcvtlsi2sd(xmm7, xmm7, Operand(rsp, 0));
    __ vucomisd(xmm5, xmm6);
    __ j(not_equal, &exit);

    // Test vcvtqsi2sd
    __ movl(rax, Immediate(18));
    __ movq(rdx, uint64_t{0x2000000000000000});  // 2 << 0x3C
    __ movq(rcx, uint64_t{0x43C0000000000000});
    __ vmovq(xmm5, rcx);
    __ vcvtqsi2sd(xmm6, xmm6, rdx);
    __ vucomisd(xmm5, xmm6);
    __ j(not_equal, &exit);

    // Test vcvtsd2si
    __ movl(rax, Immediate(19));
    __ movq(rdx, uint64_t{0x4018000000000000});  // 6.0
    __ vmovq(xmm5, rdx);
    __ vcvtsd2si(rcx, xmm5);
    __ cmpl(rcx, Immediate(6));
    __ j(not_equal, &exit);

    __ movq(rdx, uint64_t{0x3FF0000000000000});  // 1.0
    __ vmovq(xmm7, rdx);
    __ vmulsd(xmm1, xmm1, xmm7);
    __ movq(Operand(rsp, 0), rdx);
    __ vmovq(xmm6, Operand(rsp, 0));
    __ vmulsd(xmm1, xmm1, xmm6);

    __ vucomisd(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(1));

    __ vminsd(xmm3, xmm1, xmm2);
    __ vucomisd(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(2));

    __ vsubsd(xmm3, xmm2, xmm1);
    __ vucomisd(xmm3, xmm0);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(3));

    __ vaddsd(xmm3, xmm0, xmm1);
    __ vucomisd(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(4));

    __ vmulsd(xmm3, xmm0, xmm1);
    __ vucomisd(xmm3, xmm1);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(5));

    __ vdivsd(xmm3, xmm0, xmm1);
    __ vmulsd(xmm3, xmm3, xmm2);
    __ vmulsd(xmm3, xmm3, xmm1);
    __ vucomisd(xmm3, xmm2);
    __ j(parity_even, &exit);
    __ j(not_equal, &exit);
    __ movl(rax, Immediate(6));

    // result in eax
    __ bind(&exit);
    __ addq(rsp, Immediate(kDoubleSize * 2));
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F7>::FromCode(isolate, *code);
  int res = f.Call(1.0, 2.0, 3.0);
  PrintF("f(1,2,3) = %d\n", res);
  CHECK_EQ(6, res);
}

TEST_F(AssemblerX64Test, AssemblerX64BMI1) {
  if (!CpuFeatures::IsSupported(BMI1)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[1024];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, BMI1);
    Label exit;

    __ movq(rcx, uint64_t{0x1122334455667788});  // source operand
    __ pushq(rcx);                               // For memory operand

    // andn
    __ movq(rdx, uint64_t{0x1000000020000000});

    __ movl(rax, Immediate(1));  // Test number
    __ andnq(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x0122334455667788});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ andnq(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0122334455667788});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ andnl(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x0000000055667788});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ andnl(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000055667788});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // bextr
    __ movq(rdx, uint64_t{0x0000000000002808});

    __ incq(rax);
    __ bextrq(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000003344556677});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bextrq(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000003344556677});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bextrl(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000000556677});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bextrl(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000000556677});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // blsi
    __ incq(rax);
    __ blsiq(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000008});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsiq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000008});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsil(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000008});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsil(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000008});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // blsmsk
    __ incq(rax);
    __ blsmskq(r8, rcx);
    __ movq(r9, uint64_t{0x000000000000000F});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsmskq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x000000000000000F});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsmskl(r8, rcx);
    __ movq(r9, uint64_t{0x000000000000000F});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsmskl(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x000000000000000F});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // blsr
    __ incq(rax);
    __ blsrq(r8, rcx);
    __ movq(r9, uint64_t{0x1122334455667780});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsrq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x1122334455667780});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsrl(r8, rcx);
    __ movq(r9, uint64_t{0x0000000055667780});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ blsrl(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000055667780});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // tzcnt
    __ incq(rax);
    __ tzcntq(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ tzcntq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ tzcntl(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ tzcntl(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ popq(rcx);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F0>::FromCode(isolate, *code);
  CHECK_EQ(0, f.Call());
}

TEST_F(AssemblerX64Test, AssemblerX64LZCNT) {
  if (!CpuFeatures::IsSupported(LZCNT)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[256];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, LZCNT);
    Label exit;

    __ movq(rcx, uint64_t{0x1122334455667788});  // source operand
    __ pushq(rcx);                               // For memory operand

    __ movl(rax, Immediate(1));  // Test number
    __ lzcntq(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ lzcntq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000003});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ lzcntl(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000001});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ lzcntl(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000001});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ popq(rcx);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F0>::FromCode(isolate, *code);
  CHECK_EQ(0, f.Call());
}

TEST_F(AssemblerX64Test, AssemblerX64POPCNT) {
  if (!CpuFeatures::IsSupported(POPCNT)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[256];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, POPCNT);
    Label exit;

    __ movq(rcx, uint64_t{0x1111111111111100});  // source operand
    __ pushq(rcx);                               // For memory operand

    __ movl(rax, Immediate(1));  // Test number
    __ popcntq(r8, rcx);
    __ movq(r9, uint64_t{0x000000000000000E});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ popcntq(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x000000000000000E});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ popcntl(r8, rcx);
    __ movq(r9, uint64_t{0x0000000000000006});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ popcntl(r8, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000000000006});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ popq(rcx);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F0>::FromCode(isolate, *code);
  CHECK_EQ(0, f.Call());
}

TEST_F(AssemblerX64Test, AssemblerX64BMI2) {
  if (!CpuFeatures::IsSupported(BMI2)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[2048];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope fscope(&masm, BMI2);
    Label exit;
    __ pushq(rbx);                               // save rbx
    __ movq(rcx, uint64_t{0x1122334455667788});  // source operand
    __ pushq(rcx);                               // For memory operand

    // bzhi
    __ movq(rdx, uint64_t{0x0000000000000009});

    __ movl(rax, Immediate(1));  // Test number
    __ bzhiq(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000000000188});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bzhiq(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000000000188});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bzhil(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000000000188});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ bzhil(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000000000188});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // mulx
    __ movq(rdx, uint64_t{0x0000000000001000});

    __ incq(rax);
    __ mulxq(r8, r9, rcx);
    __ movq(rbx, uint64_t{0x0000000000000112});  // expected result
    __ cmpq(r8, rbx);
    __ j(not_equal, &exit);
    __ movq(rbx, uint64_t{0x2334455667788000});  // expected result
    __ cmpq(r9, rbx);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ mulxq(r8, r9, Operand(rsp, 0));
    __ movq(rbx, uint64_t{0x0000000000000112});  // expected result
    __ cmpq(r8, rbx);
    __ j(not_equal, &exit);
    __ movq(rbx, uint64_t{0x2334455667788000});  // expected result
    __ cmpq(r9, rbx);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ mulxl(r8, r9, rcx);
    __ movq(rbx, uint64_t{0x0000000000000556});  // expected result
    __ cmpq(r8, rbx);
    __ j(not_equal, &exit);
    __ movq(rbx, uint64_t{0x0000000067788000});  // expected result
    __ cmpq(r9, rbx);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ mulxl(r8, r9, Operand(rsp, 0));
    __ movq(rbx, uint64_t{0x0000000000000556});  // expected result
    __ cmpq(r8, rbx);
    __ j(not_equal, &exit);
    __ movq(rbx, uint64_t{0x0000000067788000});  // expected result
    __ cmpq(r9, rbx);
    __ j(not_equal, &exit);

    // pdep
    __ movq(rdx, uint64_t{0xFFFFFFFFFFFFFFF0});

    __ incq(rax);
    __ pdepq(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x1122334455667400});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pdepq(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x1122334455667400});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pdepl(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x0000000055667400});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pdepl(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000055667400});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // pext
    __ movq(rdx, uint64_t{0xFFFFFFFFFFFFFFF0});

    __ incq(rax);
    __ pextq(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x0000000003FFFFFE});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pextq(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x0000000003FFFFFE});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pextl(r8, rdx, rcx);
    __ movq(r9, uint64_t{0x000000000000FFFE});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ pextl(r8, rdx, Operand(rsp, 0));
    __ movq(r9, uint64_t{0x000000000000FFFE});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // sarx
    __ movq(rdx, uint64_t{0x0000000000000004});

    __ incq(rax);
    __ sarxq(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ sarxq(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ sarxl(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000005566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ sarxl(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000005566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // shlx
    __ movq(rdx, uint64_t{0x0000000000000004});

    __ incq(rax);
    __ shlxq(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x1223344556677880});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shlxq(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x1223344556677880});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shlxl(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000056677880});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shlxl(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000056677880});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // shrx
    __ movq(rdx, uint64_t{0x0000000000000004});

    __ incq(rax);
    __ shrxq(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shrxq(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shrxl(r8, rcx, rdx);
    __ movq(r9, uint64_t{0x0000000005566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ shrxl(r8, Operand(rsp, 0), rdx);
    __ movq(r9, uint64_t{0x0000000005566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    // rorx
    __ incq(rax);
    __ rorxq(r8, rcx, 0x4);
    __ movq(r9, uint64_t{0x8112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ rorxq(r8, Operand(rsp, 0), 0x4);
    __ movq(r9, uint64_t{0x8112233445566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ rorxl(r8, rcx, 0x4);
    __ movq(r9, uint64_t{0x0000000085566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ incq(rax);
    __ rorxl(r8, Operand(rsp, 0), 0x4);
    __ movq(r9, uint64_t{0x0000000085566778});  // expected result
    __ cmpq(r8, r9);
    __ j(not_equal, &exit);

    __ xorl(rax, rax);
    __ bind(&exit);
    __ popq(rcx);
    __ popq(rbx);
    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F0>::FromCode(isolate, *code);
  CHECK_EQ(0, f.Call());
}

TEST_F(AssemblerX64Test, AssemblerX64JumpTables1) {
  // Test jump tables with forward jumps.

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 512;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  Label done, table;
  __ leaq(arg2, Operand(&table));
  __ jmp(Operand(arg2, arg1, times_8, 0));
  __ ud2();
  __ bind(&table);
  for (int i = 0; i < kNumCases; ++i) {
    __ dq(&labels[i]);
  }

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ movq(rax, Immediate(values[i]));
    __ jmp(&done);
  }

  __ bind(&done);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif

  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    int res = f.Call(i);
    PrintF("f(%d) = %d\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST_F(AssemblerX64Test, AssemblerX64JumpTables2) {
  // Test jump tables with backwards jumps.

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes);

  const int kNumCases = 512;
  int values[kNumCases];
  isolate->random_number_generator()->NextBytes(values, sizeof(values));
  Label labels[kNumCases];

  Label done, table;
  __ leaq(arg2, Operand(&table));
  __ jmp(Operand(arg2, arg1, times_8, 0));
  __ ud2();

  for (int i = 0; i < kNumCases; ++i) {
    __ bind(&labels[i]);
    __ movq(rax, Immediate(values[i]));
    __ jmp(&done);
  }

  __ bind(&done);
  __ ret(0);

  __ bind(&table);
  for (int i = 0; i < kNumCases; ++i) {
    __ dq(&labels[i]);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  Print(*code, std::cout);
#endif

  auto f = GeneratedCode<F1>::FromCode(isolate, *code);
  for (int i = 0; i < kNumCases; ++i) {
    int res = f.Call(i);
    PrintF("f(%d) = %d\n", i, res);
    CHECK_EQ(values[i], res);
  }
}

TEST_F(AssemblerX64Test, AssemblerX64PslldWithXmm15) {
  auto buffer = AllocateAssemblerBuffer();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());

  __ movq(xmm15, arg1);
  __ pslld(xmm15, 1);
  __ movq(rax, xmm15);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(i_isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<F5>::FromBuffer(i_isolate(), buffer->start());
  uint64_t result = f.Call(uint64_t{0x1122334455667788});
  CHECK_EQ(uint64_t{0x22446688AACCEF10}, result);
}

using F9 = float(float x, float y);
TEST_F(AssemblerX64Test, AssemblerX64vmovups) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  Isolate* isolate = i_isolate();
  HandleScope scope(isolate);
  uint8_t buffer[256];
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      ExternalAssemblerBuffer(buffer, sizeof(buffer)));
  {
    CpuFeatureScope avx_scope(&masm, AVX);
    __ shufps(xmm0, xmm0, 0x0);  // brocast first argument
    __ shufps(xmm1, xmm1, 0x0);  // brocast second argument
    // copy xmm1 to xmm0 through the stack to test the "vmovups reg, mem".
    __ AllocateStackSpace(kSimd128Size);
    __ vmovups(Operand(rsp, 0), xmm1);
    __ vmovups(xmm0, Operand(rsp, 0));
    __ addq(rsp, Immediate(kSimd128Size));

    __ ret(0);
  }

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif

  auto f = GeneratedCode<F9>::FromCode(isolate, *code);
  CHECK_EQ(-1.5, f.Call(1.5, -1.5));
}

TEST_F(AssemblerX64Test, AssemblerX64Regmove256bit) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX);

  __ vmovdqa(ymm0, ymm1);
  __ vmovdqa(ymm4, Operand(rbx, rcx, times_4, 10000));
  __ vmovdqu(ymm10, ymm11);
  __ vmovdqu(ymm9, Operand(rbx, rcx, times_4, 10000));
  __ vmovdqu(Operand(rbx, rcx, times_4, 10000), ymm0);
  __ vmovaps(ymm3, ymm1);
  __ vmovups(Operand(rcx, rdx, times_4, 10000), ymm2);
  __ vmovapd(ymm0, ymm5);
  __ vmovupd(ymm6, Operand(r8, r9, times_4, 10000));
  __ vbroadcastss(ymm7, Operand(rbx, rcx, times_4, 10000));
  __ vbroadcastsd(ymm6, Operand(rbx, rcx, times_4, 10000));
  __ vmovddup(ymm3, ymm2);
  __ vmovddup(ymm4, Operand(rbx, rcx, times_4, 10000));
  __ vmovshdup(ymm1, ymm2);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {
      // VMOVDQA
      // vmovdqa ymm0,ymm1
      0xC5, 0xFD, 0x6F, 0xC1,
      // vmovdqa ymm4,YMMWORD PTR [rbx+rcx*4+0x2710]
      0xC5, 0xFD, 0x6F, 0xA4, 0x8B, 0x10, 0x27, 0x00, 0x00,

      // VMOVDQU
      // vmovdqu ymm10,ymm11
      0xC4, 0x41, 0x7E, 0x7F, 0xDA,
      // vmovdqu ymm9,YMMWORD PTR [rbx+rcx*4+0x2710]
      0xC5, 0x7E, 0x6F, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00,
      // vmovdqu YMMWORD PTR [rbx+rcx*4+0x2710],ymm0
      0xC5, 0xFE, 0x7F, 0x84, 0x8B, 0x10, 0x27, 0x00, 0x00,

      // vmovaps ymm3, ymm1
      0xC5, 0xFC, 0x28, 0xD9,
      // vmovups YMMWORD PTR [rcx+rdx*4+0x2710], ymm2
      0xC5, 0xFC, 0x11, 0x94, 0x91, 0x10, 0x27, 0x00, 0x00,
      // vmovapd ymm0, ymm5
      0xC5, 0xFD, 0x28, 0xC5,
      // vmovupd ymm6, YMMWORD PTR [r8+r9*4+0x2710]
      0xC4, 0x81, 0x7D, 0x10, 0xB4, 0x88, 0x10, 0x27, 0x00, 0x00,

      // vbroadcastss ymm7, DWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x18, 0xbc, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vbroadcastsd ymm6, QWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x19, 0xb4, 0x8b, 0x10, 0x27, 0x00, 0x00,

      // vmovddup ymm3, ymm2
      0xc5, 0xff, 0x12, 0xda,
      // vmovddup ymm4, YMMWORD PTR [rbx+rcx*4+0x2710]
      0xc5, 0xff, 0x12, 0xa4, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vmovshdup ymm1, ymm2
      0xc5, 0xfe, 0x16, 0xca};

  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64AVX2Op256bit) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX2);

  __ vpshufd(ymm1, ymm2, 85);
  __ vpshufd(ymm1, Operand(rbx, rcx, times_4, 10000), 85);
  __ vpshuflw(ymm9, ymm10, 85);
  __ vpshuflw(ymm9, Operand(rbx, rcx, times_4, 10000), 85);
  __ vpshufhw(ymm1, ymm2, 85);
  __ vpshufhw(ymm1, Operand(rbx, rcx, times_4, 10000), 85);
  __ vpblendw(ymm2, ymm3, ymm4, 23);
  __ vpblendw(ymm2, ymm3, Operand(rbx, rcx, times_4, 10000), 23);
  __ vpblendvb(ymm1, ymm2, ymm3, ymm4);
  __ vpalignr(ymm10, ymm11, ymm12, 4);
  __ vpalignr(ymm10, ymm11, Operand(rbx, rcx, times_4, 10000), 4);
  __ vbroadcastss(ymm7, xmm0);
  __ vbroadcastsd(ymm6, xmm5);
  __ vpbroadcastb(ymm2, xmm1);
  __ vpbroadcastb(ymm3, Operand(rbx, rcx, times_4, 10000));
  __ vpbroadcastw(ymm15, xmm4);
  __ vpbroadcastw(ymm5, Operand(rbx, rcx, times_4, 10000));
  __ vpmovsxbw(ymm6, xmm5);
  __ vpmovsxwd(ymm1, Operand(rbx, rcx, times_4, 10000));
  __ vpmovsxdq(ymm14, xmm6);
  __ vpmovzxbw(ymm0, Operand(rbx, rcx, times_4, 10000));
  __ vpmovzxbd(ymm14, xmm6);
  __ vpmovzxwd(ymm7, Operand(rbx, rcx, times_4, 10000));
  __ vpmovzxdq(ymm8, xmm6);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {
      // vpshufd ymm1, ymm2, 85
      0xC5, 0xFD, 0x70, 0xCA, 0x55,
      // vpshufd ymm1,YMMWORD PTR [rbx+rcx*4+0x2710], 85
      0xC5, 0xFD, 0x70, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x55,
      // vpshuflw ymm9, ymm10, 85,
      0xC4, 0x41, 0x7F, 0x70, 0xCA, 0x55,
      // vpshuflw ymm9,YMMWORD PTR [rbx+rcx*4+0x2710], 85
      0xC5, 0x7F, 0x70, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x55,
      // vpshufhw ymm1, ymm2, 85
      0xC5, 0xFE, 0x70, 0xCA, 0x55,
      // vpshufhw ymm1,YMMWORD PTR [rbx+rcx*4+0x2710], 85
      0xC5, 0xFE, 0x70, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x55,
      // vpblendw ymm2, ymm3, ymm4, 23
      0xC4, 0xE3, 0x65, 0x0E, 0xD4, 0x17,
      // vpblendw ymm2, ymm3, YMMWORD PTR [rbx+rcx*4+0x2710], 23
      0xC4, 0xE3, 0x65, 0x0E, 0x94, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x17,
      // vpblendvb ymm1, ymm2, ymm3, ymm4
      0xC4, 0xE3, 0x6D, 0x4C, 0xCB, 0x40,
      // vpalignr ymm10, ymm11, ymm12, 4
      0xC4, 0x43, 0x25, 0x0F, 0xD4, 0x04,
      // vpalignr ymm10, ymm11, YMMWORD PTR [rbx+rcx*4+0x2710], 4
      0xC4, 0x63, 0x25, 0x0F, 0x94, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x04,
      // vbroadcastss ymm7, xmm0
      0xc4, 0xe2, 0x7d, 0x18, 0xf8,
      // vbroadcastsd ymm7, xmm0
      0xc4, 0xe2, 0x7d, 0x19, 0xf5,
      // vpbroadcastb ymm2, xmm1
      0xc4, 0xe2, 0x7d, 0x78, 0xd1,
      // vpbroadcastb ymm3, BYTE PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x78, 0x9c, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpbroadcastw ymm15, xmm4
      0xc4, 0x62, 0x7d, 0x79, 0xfc,
      // vpbroadcastw ymm5, WORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x79, 0xac, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpmovsxbw ymm6, xmm5
      0xc4, 0xe2, 0x7d, 0x20, 0xf5,
      // vpmovsxwd ymm1, XMMWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x23, 0x8c, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpmovsxdq ymm14, xmm6
      0xc4, 0x62, 0x7d, 0x25, 0xf6,
      // vpmovzxbw ymm0, XMMWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x30, 0x84, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpmovzxbd ymm14 xmm6
      0xc4, 0x62, 0x7d, 0x31, 0xf6,
      // vpmovzxwd ymm7, XMMWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0xe2, 0x7d, 0x33, 0xbc, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpmovzxdq ymm8, xmm6
      0xc4, 0x62, 0x7d, 0x35, 0xc6};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64FloatingPoint256bit) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX);

  __ vandpd(ymm1, ymm3, ymm5);
  __ vminpd(ymm2, ymm3, Operand(r8, r9, times_4, 10000));
  __ vsqrtps(ymm0, ymm1);
  __ vunpcklps(ymm2, ymm3, ymm14);
  __ vsubps(ymm10, ymm11, ymm12);
  __ vroundps(ymm9, ymm2, kRoundUp);
  __ vroundpd(ymm9, ymm2, kRoundToNearest);
  __ vhaddps(ymm1, ymm2, ymm3);
  __ vhaddps(ymm0, ymm1, Operand(rbx, rcx, times_4, 10000));
  __ vblendvps(ymm0, ymm3, ymm5, ymm9);
  __ vblendvpd(ymm7, ymm4, ymm3, ymm1);
  __ vshufps(ymm3, ymm1, ymm2, 0x75);
  __ vsqrtpd(ymm1, ymm2);
  __ vsqrtpd(ymm1, Operand(rbx, rcx, times_4, 10000));
  __ vcvtpd2ps(xmm1, ymm2);
  __ vcvtpd2ps(xmm2, Operand256(rbx, rcx, times_4, 10000));
  __ vcvtps2dq(ymm3, ymm4);
  __ vcvtps2dq(ymm5, Operand(rbx, rcx, times_4, 10000));
  __ vcvttpd2dq(xmm6, ymm8);
  __ vcvttpd2dq(xmm10, Operand256(rbx, rcx, times_4, 10000));
  __ vcvtdq2pd(ymm1, xmm2);
  __ vcvtdq2pd(ymm1, Operand(rbx, rcx, times_4, 10000));
  __ vcvttps2dq(ymm3, ymm2);
  __ vcvttps2dq(ymm3, Operand256(rbx, rcx, times_4, 10000));
  __ vperm2f128(ymm1, ymm2, ymm3, 2);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {// vandpd ymm1, ymm3, ymm5
                        0xC5, 0xE5, 0x54, 0xCD,
                        // vminpd ymm2, ymm3, YMMWORD PTR [r8+r9*4+0x2710]
                        0xC4, 0x81, 0x65, 0x5D, 0x94, 0x88, 0x10, 0x27, 0x00,
                        0x00,
                        // VSQRTPS
                        0xC5, 0xFC, 0x51, 0xC1,
                        // VUNPCKLPS
                        0xC4, 0xC1, 0x64, 0x14, 0xD6,
                        // VSUBPS
                        0xC4, 0x41, 0x24, 0x5C, 0xD4,
                        // vroundps ymm9, ymm2, 0xA
                        0xC4, 0x63, 0x7D, 0x08, 0xCA, 0x0A,
                        // vroundpd ymm9, ymm2, 0x8
                        0xC4, 0x63, 0x7D, 0x09, 0xCA, 0x08,
                        // VHADDPS ymm1, ymm2, ymm3
                        0xC5, 0xEF, 0x7C, 0xCB,
                        // VHADDPS ymm0, ymm1, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xc5, 0xf7, 0x7c, 0x84, 0x8b, 0x10, 0x27, 0x00, 0x00,
                        // vblendvps ymm0, ymm3, ymm5, ymm9
                        0xC4, 0xE3, 0x65, 0x4A, 0xC5, 0x90,
                        // vblendvpd ymm7, ymm4, ymm3, ymm1
                        0xC4, 0xE3, 0x5D, 0x4B, 0xFB, 0x10,
                        // vshufps ymm3, ymm1, ymm2, 0x75
                        0xC5, 0xF4, 0xC6, 0xDA, 0x75,
                        // vsqrtpd ymm1, ymm2
                        0xC5, 0xFD, 0x51, 0xCA,
                        // vsqrtpd ymm1, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0xFD, 0x51, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vcvtpd2ps xmm1, ymm2
                        0xC5, 0xFD, 0x5A, 0xCA,
                        // vcvtpd2ps xmm2, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0xFD, 0x5A, 0x94, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vcvtps2dq ymm3, ymm4
                        0xC5, 0xFD, 0x5B, 0xDC,
                        // vcvtps2dq ymm5, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0xFD, 0x5B, 0xAC, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vcvttpd2dq xmm6, ymm8
                        0xC4, 0xC1, 0x7D, 0xE6, 0xF0,
                        // vcvttpd2dq xmm10, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0x7D, 0xE6, 0x94, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vcvtdq2pd ymm1, xmm2
                        0xC5, 0xFE, 0xE6, 0xCA,
                        // vcvtdq2pd ymm1, XMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0xFE, 0xE6, 0x8C, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vcvttps2dq ymm3, ymm2
                        0xC5, 0xFE, 0x5B, 0xDA,
                        // vcvttps2dq ymm3, YMMWORD PTR [rbx+rcx*4+0x2710]
                        0xC5, 0xFE, 0x5B, 0x9C, 0x8B, 0x10, 0x27, 0x00, 0x00,
                        // vperm2f128 ymm1, ymm2, ymm3, 2
                        0xc4, 0xe3, 0x6d, 0x06, 0xcb, 0x02};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64Integer256bit) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX2);

  // SSE2_AVX_INSTRUCTION
  __ vpunpcklbw(ymm9, ymm2, ymm0);
  __ vpacksswb(ymm8, ymm3, ymm1);
  __ vpcmpgtw(ymm2, ymm7, ymm9);
  __ vpand(ymm2, ymm3, ymm4);
  __ vpmaxsw(ymm10, ymm11, Operand(rbx, rcx, times_4, 10000));
  __ vpaddb(ymm1, ymm2, ymm3);
  __ vpsraw(ymm7, ymm1, xmm4);
  __ vpsllq(ymm3, ymm2, xmm1);

  // SSSE3_AVX_INSTRUCTION
  __ vpshufb(ymm1, ymm2, ymm3);
  __ vphaddw(ymm8, ymm9, Operand(rbx, rcx, times_4, 10000));
  __ vpmaddubsw(ymm5, ymm7, ymm9);
  __ vpsignd(ymm7, ymm0, ymm1);
  __ vpmulhrsw(ymm4, ymm3, ymm1);
  __ vpabsb(ymm1, ymm2);
  __ vpabsb(ymm3, Operand(rbx, rcx, times_4, 10000));
  __ vpabsw(ymm6, ymm5);
  __ vpabsd(ymm7, ymm10);

  // SSE4_AVX_INSTRUCTION
  __ vpmuldq(ymm1, ymm5, ymm6);
  __ vpcmpeqq(ymm0, ymm2, ymm3);
  __ vpackusdw(ymm4, ymm2, ymm0);
  __ vpminud(ymm8, ymm9, Operand(rbx, rcx, times_4, 10000));
  __ vpmaxsb(ymm3, ymm4, ymm7);
  __ vpmulld(ymm6, ymm5, ymm3);

  // SSE4_2_AVX_INSTRUCTION
  __ vpcmpgtq(ymm3, ymm2, ymm0);

  __ vpermq(ymm8, Operand(rbx, rcx, times_4, 10000), 0x1E);
  __ vpermq(ymm5, ymm3, 0xD8);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {
      // SSE2_AVX_INSTRUCTION
      // vpunpcklbw ymm9, ymm2, ymm0
      0xC5, 0x6D, 0x60, 0xC8,
      // vpacksswb ymm8, ymm3, ymm1
      0xC5, 0x65, 0x63, 0xC1,
      // vpcmpgtw ymm2, ymm7, ymm9
      0xC4, 0xC1, 0x45, 0x65, 0xD1,
      // vpand ymm2, ymm3, ymm4
      0xC5, 0xE5, 0xDB, 0xD4,
      // vpmaxsw ymm10, ymm11, YMMWORD PTR [rbx+rcx*4+0x2710]
      0xC5, 0x25, 0xEE, 0x94, 0x8B, 0x10, 0x27, 0x00, 0x00,
      // vpaddb ymm1, ymm2, ymm3
      0xC5, 0xED, 0xFC, 0xCB,
      // vpsraw ymm7, ymm1, xmm4
      0xC5, 0xF5, 0xE1, 0xFC,
      // vpsllq ymm3, ymm2, xmm1
      0xC5, 0xED, 0xF3, 0xD9,

      // SSSE3_AVX_INSTRUCTION
      // vpshufb ymm1, ymm2, ymm3
      0xC4, 0xE2, 0x6D, 0x00, 0xCB,
      // vphaddw ymm8, ymm9, YMMWORD PTR [rbx+rcx*4+0x2710]
      0xC4, 0x62, 0x35, 0x01, 0x84, 0x8B, 0x10, 0x27, 0x00, 0x00,
      // vpmaddubsw ymm5, ymm7, ymm9
      0xC4, 0xC2, 0x45, 0x04, 0xE9,
      // vpsignd ymm7, ymm0, ymm1
      0xC4, 0xE2, 0x7D, 0x0A, 0xF9,
      // vpmulhrsw ymm4, ymm3, ymm1
      0xC4, 0xE2, 0x65, 0x0B, 0xE1,
      // vpabsb ymm1, ymm2
      0xC4, 0xE2, 0x7D, 0x1C, 0xCA,
      // vpabsb ymm3, YMMWORD PTR [rbx+rcx+0x2710]
      0xC4, 0xE2, 0x7D, 0x1C, 0x9C, 0x8b, 0x10, 0x27, 0x00, 0x00,
      // vpabsw ymm6, ymm5
      0xC4, 0xE2, 0x7D, 0x1D, 0xF5,
      // vpabsd ymm7, ymm10
      0xC4, 0xC2, 0x7D, 0x1E, 0xFA,

      // SSE4_AVX_INSTRUCTION
      // vpmuldq ymm1, ymm5, ymm6
      0xC4, 0xE2, 0x55, 0x28, 0xCE,
      // vpcmpeqq ymm0, ymm2, ymm3
      0xC4, 0xE2, 0x6D, 0x29, 0xC3,
      // vpackusdw ymm4, ymm2, ymm0
      0xC4, 0xE2, 0x6D, 0x2B, 0xE0,
      // vpminud ymm8, ymm9, YMMWORD PTR [rbx+rcx*4+0x2710]
      0xC4, 0x62, 0x35, 0x3B, 0x84, 0x8B, 0x10, 0x27, 0x0, 0x0,
      // vpmaxsb ymm3, ymm4, ymm7
      0xC4, 0xE2, 0x5D, 0x3C, 0xDF,
      // vpmulld ymm6, ymm5, ymm3
      0xC4, 0xE2, 0x55, 0x40, 0xF3,

      // SSE4_2_AVX_INSTRUCTION
      // vpcmpgtq ymm3, ymm2, ymm0
      0xC4, 0xE2, 0x6D, 0x37, 0xD8,

      // vpermq ymm8, YMMWORD PTR [rbx+rcx*4+0x2710], 0x1e
      0xC4, 0x63, 0xFD, 0x00, 0x84, 0x8B, 0x10, 0x27, 0x00, 0x00, 0x1E,
      // vpermq ymm5, ymm3, 0xD8
      0xC4, 0xE3, 0xFD, 0x00, 0xEB, 0xD8};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64CmpOperations256bit) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX);

  __ vcmpeqps(ymm1, ymm2, ymm4);
  __ vcmpltpd(ymm4, ymm7, Operand(rcx, rdx, times_4, 10000));
  __ vcmpleps(ymm9, ymm8, Operand(r8, r11, times_8, 10000));
  __ vcmpunordpd(ymm3, ymm7, ymm8);
  __ vcmpneqps(ymm3, ymm5, ymm9);
  __ vcmpnltpd(ymm10, ymm12, Operand(r12, r11, times_4, 10000));
  __ vcmpnleps(ymm9, ymm11, Operand(r10, r9, times_8, 10000));
  __ vcmpgepd(ymm13, ymm3, ymm12);
  __ vptest(ymm7, ymm1);
  __ vptest(ymm10, Operand(rbx, rcx, times_4, 10000));

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {
      // vcmpeqps ymm1, ymm2, ymm4
      0xC5, 0xEC, 0xC2, 0xCC, 0x00,
      // vcmpltpd ymm4, ymm7, YMMWORD PTR [rcx+rdx*4+0x2710]
      0xC5, 0xC5, 0xC2, 0xA4, 0x91, 0x10, 0x27, 0x00, 0x00, 0x01,
      // vcmpleps ymm9, ymm8, YMMWORD PTR [r8+r11*8+0x2710]
      0xC4, 0x01, 0x3C, 0xC2, 0x8C, 0xD8, 0x10, 0x27, 0x00, 0x00, 0x02,
      // vcmpunordpd ymm3, ymm7, ymm8
      0xC4, 0xC1, 0x45, 0xC2, 0xD8, 0x03,
      // vcmpneqps ymm3, ymm5, ymm9
      0xC4, 0xC1, 0x54, 0xC2, 0xD9, 0x04,
      // vcmpnltpd ymm10, ymm12, YMMWORD PTR [r12+r11*4+0x2710]
      0xC4, 0x01, 0x1D, 0xC2, 0x94, 0x9C, 0x10, 0x27, 0x00, 0x00, 0x05,
      // vcmpnleps ymm9, ymm11, YMMWORD PTR [r10+r9*8+0x2710]
      0xC4, 0x01, 0x24, 0xC2, 0x8C, 0xCA, 0x10, 0x27, 0x00, 0x00, 0x06,
      // vcmpgepd ymm13, ymm3, ymm12
      0xC4, 0x41, 0x65, 0xC2, 0xEC, 0x0D,
      // vptest ymm7, ymm1
      0xc4, 0xe2, 0x7d, 0x17, 0xf9,
      // vptest ymm10, YMMWORD PTR [rbx+rcx*4+0x2710]
      0xc4, 0x62, 0x7d, 0x17, 0x94, 0x8b, 0x10, 0x27, 0x00, 0x00};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64FMA256bit) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, FMA3);

  __ vfmadd132ps(ymm1, ymm2, ymm4);
  __ vfmadd213ps(ymm3, ymm5, ymm9);
  __ vfmadd231ps(ymm1, ymm3, ymm5);
  __ vfnmadd132ps(ymm1, ymm2, ymm4);
  __ vfnmadd213ps(ymm3, ymm5, ymm9);
  __ vfnmadd231ps(ymm1, ymm3, ymm5);

  __ vfmadd132ps(ymm1, ymm2, Operand(rcx, rdx, times_4, 10000));
  __ vfmadd213ps(ymm3, ymm5, Operand(r8, r11, times_8, 10000));
  __ vfmadd231ps(ymm1, ymm3, Operand(r12, r11, times_4, 10000));
  __ vfnmadd132ps(ymm1, ymm2, Operand(rcx, rdx, times_4, 10000));
  __ vfnmadd213ps(ymm3, ymm5, Operand(r8, r11, times_8, 10000));
  __ vfnmadd231ps(ymm1, ymm3, Operand(r12, r11, times_4, 10000));

  __ vfmadd132pd(ymm1, ymm2, ymm4);
  __ vfmadd213pd(ymm3, ymm5, ymm9);
  __ vfmadd231pd(ymm1, ymm3, ymm5);
  __ vfnmadd132pd(ymm1, ymm2, ymm4);
  __ vfnmadd213pd(ymm3, ymm5, ymm9);
  __ vfnmadd231pd(ymm1, ymm3, ymm5);

  __ vfmadd132pd(ymm1, ymm2, Operand(rcx, rdx, times_4, 10000));
  __ vfmadd213pd(ymm3, ymm5, Operand(r8, r11, times_8, 10000));
  __ vfmadd231pd(ymm1, ymm3, Operand(r12, r11, times_4, 10000));
  __ vfnmadd132pd(ymm1, ymm2, Operand(rcx, rdx, times_4, 10000));
  __ vfnmadd213pd(ymm3, ymm5, Operand(r8, r11, times_8, 10000));
  __ vfnmadd231pd(ymm1, ymm3, Operand(r12, r11, times_4, 10000));

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {
      // vfmadd132ps ymm1, ymm2, ymm4
      0xC4, 0xE2, 0x6D, 0x98, 0xCC,
      // vfmadd213ps ymm3, ymm5, ymm9
      0xC4, 0xC2, 0x55, 0xA8, 0xD9,
      // vfmadd231ps ymm1, ymm3, ymm5
      0xC4, 0xE2, 0x65, 0xB8, 0xCD,
      // vfnmadd132ps ymm1, ymm2, ymm4
      0xC4, 0xE2, 0x6D, 0x9C, 0xCC,
      // vfnmadd213ps ymm3, ymm5, ymm9
      0xC4, 0xC2, 0x55, 0xAC, 0xD9,
      // vfnmadd231ps ymm1, ymm3, ymm5
      0xC4, 0xE2, 0x65, 0xBC, 0xCD,
      // vfmadd132ps ymm1, ymm2, YMMWORD PTR [rcx+rdx*4+0x2710]
      0xC4, 0xE2, 0x6D, 0x98, 0x8C, 0x91, 0x10, 0x27, 0x00, 0x00,
      // vfmadd213ps ymm3, ymm5, YMMWORD PTR [r8+r11*8+0x2710]
      0xC4, 0x82, 0x55, 0xA8, 0x9C, 0xD8, 0x10, 0x27, 0x00, 0x00,
      // vfmadd231ps ymm1, ymm3, YMMWORD PTR [r12+r11*4+0x2710]
      0xC4, 0x82, 0x65, 0xB8, 0x8C, 0x9C, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd132ps ymm1, ymm2, YMMWORD PTR [rcx+rdx*4+0x2710]
      0xC4, 0xE2, 0x6D, 0x9C, 0x8C, 0x91, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd213ps ymm3, ymm5, YMMWORD PTR [r8+r11*8+0x2710]
      0xC4, 0x82, 0x55, 0xAC, 0x9C, 0xD8, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd231ps ymm1, ymm3, YMMWORD PTR [r12+r11*4+0x2710]
      0xC4, 0x82, 0x65, 0xBC, 0x8C, 0x9C, 0x10, 0x27, 0x00, 0x00,
      // vfmadd132pd ymm1, ymm2, ymm4
      0xC4, 0xE2, 0xED, 0x98, 0xCC,
      // vfmadd213pd ymm3, ymm5, ymm9
      0xC4, 0xC2, 0xD5, 0xA8, 0xD9,
      // vfmadd231pd ymm1, ymm3, ymm5
      0xC4, 0xE2, 0xE5, 0xB8, 0xCD,
      // vfnmadd132pd ymm1, ymm2, ymm4
      0xC4, 0xE2, 0xED, 0x9C, 0xCC,
      // vfnmadd213pd ymm3, ymm5, ymm9
      0xC4, 0xC2, 0xD5, 0xAC, 0xD9,
      // vfnmadd231pd ymm1, ymm3, ymm5
      0xC4, 0xE2, 0xE5, 0xBC, 0xCD,
      // vfmadd132pd ymm1, ymm2, YMMWORD PTR [rcx+rdx*4+0x2710]
      0xC4, 0xE2, 0xED, 0x98, 0x8C, 0x91, 0x10, 0x27, 0x00, 0x00,
      // vfmadd213pd ymm3, ymm5, YMMWORD PTR [r8+r11*8+0x2710]
      0xC4, 0x82, 0xD5, 0xA8, 0x9C, 0xD8, 0x10, 0x27, 0x00, 0x00,
      // vfmadd231pd ymm1, ymm3, YMMWORD PTR [r12+r11*4+0x2710]
      0xC4, 0x82, 0xE5, 0xB8, 0x8C, 0x9C, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd132pd ymm1, ymm2, YMMWORD PTR [rcx+rdx*4+0x2710]
      0xC4, 0xE2, 0xED, 0x9C, 0x8C, 0x91, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd213pd ymm3, ymm5, YMMWORD PTR [r8+r11*8+0x2710]
      0xC4, 0x82, 0xD5, 0xAC, 0x9C, 0xD8, 0x10, 0x27, 0x00, 0x00,
      // vfnmadd231pd ymm1, ymm3, YMMWORD PTR [r12+r11*4+0x2710]
      0xC4, 0x82, 0xE5, 0xBC, 0x8C, 0x9C, 0x10, 0x27, 0x00, 0x00};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64ShiftImm128bit) {
  if (!CpuFeatures::IsSupported(AVX)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX);

  __ vpsrlw(xmm8, xmm2, 4);
  __ vpsrld(xmm11, xmm2, 4);
  __ vpsrlq(xmm1, xmm2, 4);
  __ vpsraw(xmm10, xmm8, 4);
  __ vpsrad(xmm6, xmm7, 4);
  __ vpsllw(xmm1, xmm4, 4);
  __ vpslld(xmm3, xmm2, 4);
  __ vpsllq(xmm6, xmm9, 4);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {// vpsrlw xmm8,xmm2,0x4
                        0XC5, 0xB9, 0x71, 0xD2, 0x04,
                        // vpsrld xmm11,xmm2,0x4
                        0xC5, 0xA1, 0x72, 0xD2, 0x04,
                        // vpsrlq xmm1,xmm2,0x4
                        0xC5, 0xF1, 0x73, 0xD2, 0x04,
                        // vpsraw xmm10,xmm8,0x4
                        0xC4, 0xC1, 0x29, 0x71, 0xE0, 0x04,
                        // vpsrad xmm6,xmm7,0x4
                        0xC5, 0xC9, 0x72, 0xE7, 0x04,
                        // vpsllw xmm1,xmm4,0x4
                        0xC5, 0xF1, 0x71, 0xF4, 0x04,
                        // vpslld xmm3,xmm2,0x4
                        0xC5, 0xE1, 0x72, 0xF2, 0x04,
                        // vpsllq xmm6,xmm9,0x4
                        0xC4, 0xC1, 0x49, 0x73, 0xF1, 0x04};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64ShiftImm256bit) {
  if (!CpuFeatures::IsSupported(AVX2)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX2);

  __ vpsrlw(ymm0, ymm2, 4);
  __ vpsrld(ymm11, ymm2, 4);
  __ vpsrlq(ymm1, ymm2, 4);
  __ vpsraw(ymm10, ymm8, 4);
  __ vpsrad(ymm6, ymm7, 4);
  __ vpsllw(ymm1, ymm4, 4);
  __ vpslld(ymm3, ymm2, 4);
  __ vpsllq(ymm6, ymm9, 4);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {// vpsrlw ymm0,ymm2,0x4
                        0XC5, 0xFD, 0x71, 0xD2, 0x04,
                        // vpsrld ymm11,ymm2,0x4
                        0xC5, 0xA5, 0x72, 0xD2, 0x04,
                        // vpsrlq ymm1,ymm2,0x4
                        0xC5, 0xF5, 0x73, 0xD2, 0x04,
                        // vpsraw ymm10,ymm8,0x4
                        0xC4, 0xC1, 0x2D, 0x71, 0xE0, 0x04,
                        // vpsrad ymm6,ymm7,0x4
                        0xC5, 0xCD, 0x72, 0xE7, 0x04,
                        // vpsllw ymm1,ymm4,0x4
                        0xC5, 0xF5, 0x71, 0xF4, 0x04,
                        // vpslld ymm3,ymm2,0x4
                        0xC5, 0xE5, 0x72, 0xF2, 0x04,
                        // vpsllq ymm6,ymm9,0x4
                        0xC4, 0xC1, 0x4D, 0x73, 0xF1, 0x04};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64BinOp256bit) {
  {
    if (!CpuFeatures::IsSupported(AVX)) return;
    auto buffer = AllocateAssemblerBuffer();
    Isolate* isolate = i_isolate();
    Assembler masm(AssemblerOptions{}, buffer->CreateView());
    CpuFeatureScope fscope(&masm, AVX);

    //  add
    __ vaddps(ymm0, ymm1, ymm2);
    __ vaddpd(ymm3, ymm4, ymm5);

    // sub
    __ vsubps(ymm0, ymm1, ymm2);
    __ vsubpd(ymm3, ymm4, ymm5);

    // mul
    __ vmulps(ymm0, ymm1, ymm2);
    __ vmulpd(ymm3, ymm4, ymm5);

    // div
    __ vdivps(ymm0, ymm1, ymm2);
    __ vdivpd(ymm3, ymm4, ymm5);

    CodeDesc desc;
    masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
    DirectHandle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    StdoutStream os;
    Print(*code, os);
#endif

    uint8_t expected[] = {// vaddps ymm0,ymm1,ymm2
                          0xc5, 0xf4, 0x58, 0xc2,
                          // vaddpd ymm3,ymm4,ymm5
                          0xc5, 0xdd, 0x58, 0xdd,
                          // vsubps ymm0,ymm1,ymm2
                          0xc5, 0xf4, 0x5c, 0xc2,
                          // vsubpd ymm3,ymm4,ymm5
                          0xc5, 0xdd, 0x5c, 0xdd,
                          // vmulps ymm0,ymm1,ymm2
                          0xc5, 0xf4, 0x59, 0xc2,
                          // vmulpd ymm3,ymm4,ymm5
                          0xc5, 0xdd, 0x59, 0xdd,
                          // vdivps ymm0,ymm1,ymm2
                          0xc5, 0xf4, 0x5e, 0xc2,
                          // vdivpd ymm3,ymm4,ymm5
                          0xc5, 0xdd, 0x5e, 0xdd};
    CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
  }

  {
    if (!CpuFeatures::IsSupported(AVX2)) return;

    auto buffer = AllocateAssemblerBuffer();
    Isolate* isolate = i_isolate();
    Assembler masm(AssemblerOptions{}, buffer->CreateView());
    CpuFeatureScope fscope(&masm, AVX2);

    //  add
    __ vpaddb(ymm6, ymm7, ymm8);
    __ vpaddw(ymm9, ymm10, ymm11);
    __ vpaddd(ymm12, ymm13, ymm14);
    __ vpaddq(ymm15, ymm1, ymm2);

    // sub

    __ vpsubb(ymm6, ymm7, ymm8);
    __ vpsubw(ymm9, ymm10, ymm11);
    __ vpsubd(ymm12, ymm13, ymm14);
    __ vpsubq(ymm15, ymm1, ymm2);

    // mul, exclude I64x4

    __ vpmullw(ymm6, ymm7, ymm8);
    __ vpmulld(ymm15, ymm1, ymm2);

    CodeDesc desc;
    masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
    DirectHandle<Code> code =
        Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
    StdoutStream os;
    Print(*code, os);
#endif

    uint8_t expected[] = {// vpaddb ymm6,ymm7,ymm8
                          0xc4, 0xc1, 0x45, 0xfc, 0xf0,
                          // vpaddw ymm9,ymm10,ymm11
                          0xc4, 0x41, 0x2d, 0xfd, 0xcb,
                          // vpaddd ymm12,ymm13,ymm14
                          0xc4, 0x41, 0x15, 0xfe, 0xe6,
                          // vpaddq ymm15,ymm1,ymm2
                          0xc5, 0x75, 0xd4, 0xfa,
                          // vpsubb ymm6,ymm7,ymm8
                          0xc4, 0xc1, 0x45, 0xf8, 0xf0,
                          // vpsubw ymm9,ymm10,ymm11
                          0xc4, 0x41, 0x2d, 0xf9, 0xcb,
                          // vpsubd ymm12,ymm13,ymm14
                          0xc4, 0x41, 0x15, 0xfa, 0xe6,
                          // vpsubq ymm15,ymm1,ymm2
                          0xc5, 0x75, 0xfb, 0xfa,
                          // vpmullw ymm6,ymm7,ymm8
                          0xc4, 0xc1, 0x45, 0xd5, 0xf0,
                          // vpmulld ymm15,ymm1,ymm2
                          0xc4, 0x62, 0x75, 0x40, 0xfa};
    CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
  }
}

TEST_F(AssemblerX64Test, F16C) {
  if (!CpuFeatures::IsSupported(F16C)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, F16C);

  __ vcvtph2ps(ymm0, xmm1);
  __ vcvtph2ps(xmm2, xmm3);
  __ vcvtps2ph(xmm4, ymm5, 0);
  __ vcvtps2ph(xmm6, xmm7, 0);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);

  uint8_t expected[] = {// vcvtph2ps ymm0,xmm1,
                        0xc4, 0xe2, 0x7d, 0x13, 0xc1,
                        // vcvtph2ps xymm2,xmm3,
                        0xc4, 0xe2, 0x79, 0x13, 0xd3,
                        // vcvtps2ph xmm4,ymm5,0x0
                        0xc4, 0xe3, 0x7d, 0x1d, 0xec, 0x00,
                        // vcvtps2ph xmm6,xmm7,0x0
                        0xc4, 0xe3, 0x79, 0x1d, 0xfe, 0x00};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64AVXVNNI) {
  if (!CpuFeatures::IsSupported(AVX_VNNI)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX_VNNI);

  __ vpdpbusd(xmm1, xmm2, xmm3);
  __ vpdpbusd(ymm8, ymm11, ymm9);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {// vpdpbusd xmm1, xmm2, xmm3
                        0xc4, 0xe2, 0x69, 0x50, 0xcb,
                        // vpdpbusd ymm8, ymm11, ymm9
                        0xc4, 0x42, 0x25, 0x50, 0xc1};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, AssemblerX64AVXVNNIINT8) {
  if (!CpuFeatures::IsSupported(AVX_VNNI_INT8)) return;

  auto buffer = AllocateAssemblerBuffer();
  Isolate* isolate = i_isolate();
  Assembler masm(AssemblerOptions{}, buffer->CreateView());
  CpuFeatureScope fscope(&masm, AVX_VNNI_INT8);

  __ vpdpbssd(xmm12, xmm13, xmm14);
  __ vpdpbssd(ymm12, ymm13, ymm14);

  CodeDesc desc;
  masm.GetCode(isolate, &desc);
#ifdef OBJECT_PRINT
  DirectHandle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif

  uint8_t expected[] = {// vpdpbssd xmm12, xmm13, xmm14
                        0xc4, 0x42, 0x13, 0x50, 0xe6,
                        // vpdpbssd ymm12, ymm13, ymm14
                        0xc4, 0x42, 0x17, 0x50, 0xe6};
  CHECK_EQ(0, memcmp(expected, desc.buffer, sizeof(expected)));
}

TEST_F(AssemblerX64Test, CpuFeatures_ProbeImpl) {
  // Support for a newer extension implies support for the older extensions.
  CHECK_IMPLIES(CpuFeatures::IsSupported(FMA3), CpuFeatures::IsSupported(AVX));
  CHECK_IMPLIES(CpuFeatures::IsSupported(AVX_VNNI_INT8),
                CpuFeatures::IsSupported(AVX));
  CHECK_IMPLIES(CpuFeatures::IsSupported(AVX_VNNI),
                CpuFeatures::IsSupported(AVX));
  CHECK_IMPLIES(CpuFeatures::IsSupported(AVX2), CpuFeatures::IsSupported(AVX));
  CHECK_IMPLIES(CpuFeatures::IsSupported(AVX),
                CpuFeatures::IsSupported(SSE4_2));
  CHECK_IMPLIES(CpuFeatures::IsSupported(SSE4_2),
                CpuFeatures::IsSupported(SSE4_1));
  CHECK_IMPLIES(CpuFeatures::IsSupported(SSE4_1),
                CpuFeatures::IsSupported(SSSE3));
  CHECK_IMPLIES(CpuFeatures::IsSupported(SSSE3),
                CpuFeatures::IsSupported(SSE3));

  // Check the reverse, if an older extension is not supported, a newer
  // extension cannot be supported.
  CHECK_IMPLIES(!CpuFeatures::IsSupported(SSE3),
                !CpuFeatures::IsSupported(SSSE3));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(SSSE3),
                !CpuFeatures::IsSupported(SSE4_1));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(SSE4_1),
                !CpuFeatures::IsSupported(SSE4_2));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(SSE4_2),
                !CpuFeatures::IsSupported(AVX));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(AVX),
                !CpuFeatures::IsSupported(AVX2));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(AVX),
                !CpuFeatures::IsSupported(AVX_VNNI));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(AVX),
                !CpuFeatures::IsSupported(AVX_VNNI_INT8));
  CHECK_IMPLIES(!CpuFeatures::IsSupported(AVX),
                !CpuFeatures::IsSupported(FMA3));
}

#undef __

}  // namespace internal
}  // namespace v8
