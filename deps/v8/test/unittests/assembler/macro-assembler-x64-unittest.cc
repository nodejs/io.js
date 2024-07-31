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

#include <stdlib.h>

#include <cstdint>
#include <cstring>

#include "src/codegen/macro-assembler.h"
#include "src/codegen/x64/assembler-x64-inl.h"
#include "src/codegen/x64/assembler-x64.h"
#include "src/codegen/x64/register-x64.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/simulator.h"
#include "src/heap/factory.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/utils/ostreams.h"
#include "test/common/assembler-tester.h"
#include "test/common/value-helper.h"
#include "test/unittests/test-utils.h"

namespace v8 {
namespace internal {

#define __ masm.

// Test the x64 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.

using MacroAssemblerX64Test = TestWithIsolate;

void PrintCode(Isolate* isolate, CodeDesc desc) {
#ifdef OBJECT_PRINT
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif  // OBJECT_PRINT
}

TEST_F(MacroAssemblerX64Test, TestHardAbort) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  __ Abort(AbortReason::kNoReason);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void>::FromBuffer(isolate(), buffer->start());

  ASSERT_DEATH_IF_SUPPORTED({ f.Call(); }, "abort: no reason");
}

TEST_F(MacroAssemblerX64Test, TestCheck) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  // Fail if the first parameter is 17.
  __ movl(rax, Immediate(17));
  __ cmpl(rax, kCArgRegs[0]);
  __ Check(Condition::not_equal, AbortReason::kNoReason);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
  f.Call(18);
  ASSERT_DEATH_IF_SUPPORTED({ f.Call(17); }, "abort: no reason");
}

#undef __

namespace test_macro_assembler_x64 {

// Test the x64 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.
// The AMD64 calling convention is used, with the first five arguments
// in RSI, RDI, RDX, RCX, R8, and R9, and floating point arguments in
// the XMM registers.  The return value is in RAX.
// This calling convention is used on Linux, with GCC, and on Mac OS,
// with GCC.  A different convention is used on 64-bit windows.

using F0 = int();
using F1 = int(uint64_t*, uint64_t*, uint64_t*);
using F2 = int(uint64_t*, uint64_t*, uint64_t*, uint64_t*);
using F3 = int(int8_t, int8_t*, int8_t*, int8_t*);
using F4 = int(int16_t, int16_t*, int16_t*, int16_t*);
using F5 = int(int32_t, int32_t*, int32_t*, int32_t*);
using F6 = int(int64_t, int64_t*, int64_t*, int64_t*);
using F7 = int(double*, double*, double*);
using F8 = int(float*, float*, float*);
using F9 = int(int16_t*, int32_t*);
using F10 = int(int8_t*, int16_t*);
using F11 = int(uint16_t*, uint32_t*);
using F12 = int(uint8_t*, uint16_t*);

#define __ masm->

static void EntryCode(MacroAssembler* masm) {
  // Smi constant register is callee save.
  __ pushq(kRootRegister);
#ifdef V8_COMPRESS_POINTERS
  __ pushq(kPtrComprCageBaseRegister);
#endif
  __ InitializeRootRegister();
}

static void ExitCode(MacroAssembler* masm) {
#ifdef V8_COMPRESS_POINTERS
  __ popq(kPtrComprCageBaseRegister);
#endif
  __ popq(kRootRegister);
}

TEST_F(MacroAssemblerX64Test, Smi) {
  // clang-format off
  // Check that C++ Smi operations work as expected.
  int64_t test_numbers[] = {
      0, 1, -1, 127, 128, -128, -129, 255, 256, -256, -257,
      Smi::kMaxValue, static_cast<int64_t>(Smi::kMaxValue) + 1,
      Smi::kMinValue, static_cast<int64_t>(Smi::kMinValue) - 1
  };
  // clang-format on
  int test_number_count = 15;
  for (int i = 0; i < test_number_count; i++) {
    int64_t number = test_numbers[i];
    bool is_valid = Smi::IsValid(number);
    bool is_in_range = number >= Smi::kMinValue && number <= Smi::kMaxValue;
    CHECK_EQ(is_in_range, is_valid);
    if (is_valid) {
      Tagged<Smi> smi_from_intptr = Smi::FromIntptr(number);
      if (static_cast<int>(number) == number) {  // Is a 32-bit int.
        Tagged<Smi> smi_from_int = Smi::FromInt(static_cast<int32_t>(number));
        CHECK_EQ(smi_from_int, smi_from_intptr);
      }
      int64_t smi_value = smi_from_intptr.value();
      CHECK_EQ(number, smi_value);
    }
  }
}

static void TestMoveSmi(MacroAssembler* masm, Label* exit, int id,
                        Tagged<Smi> value) {
  __ movl(rax, Immediate(id));
  __ Move(rcx, value);
  __ Move(rdx, static_cast<intptr_t>(value.ptr()));
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, exit);
}

// Test that we can move a Smi value literally into a register.
TEST_F(MacroAssemblerX64Test, SmiMove) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;  // Create a pointer for the __ macro.
  EntryCode(masm);
  Label exit;

  TestMoveSmi(masm, &exit, 1, Smi::zero());
  TestMoveSmi(masm, &exit, 2, Smi::FromInt(127));
  TestMoveSmi(masm, &exit, 3, Smi::FromInt(128));
  TestMoveSmi(masm, &exit, 4, Smi::FromInt(255));
  TestMoveSmi(masm, &exit, 5, Smi::FromInt(256));
  TestMoveSmi(masm, &exit, 6, Smi::FromInt(0xFFFF - 1));
  TestMoveSmi(masm, &exit, 7, Smi::FromInt(0xFFFF));
  TestMoveSmi(masm, &exit, 8, Smi::FromInt(0xFFFF + 1));
  TestMoveSmi(masm, &exit, 9, Smi::FromInt(Smi::kMaxValue));

  TestMoveSmi(masm, &exit, 10, Smi::FromInt(-1));
  TestMoveSmi(masm, &exit, 11, Smi::FromInt(-128));
  TestMoveSmi(masm, &exit, 12, Smi::FromInt(-129));
  TestMoveSmi(masm, &exit, 13, Smi::FromInt(-256));
  TestMoveSmi(masm, &exit, 14, Smi::FromInt(-257));
  TestMoveSmi(masm, &exit, 15, Smi::FromInt(-0xFFFF + 1));
  TestMoveSmi(masm, &exit, 16, Smi::FromInt(-0xFFFF));
  TestMoveSmi(masm, &exit, 17, Smi::FromInt(-0xFFFF - 1));
  TestMoveSmi(masm, &exit, 18, Smi::FromInt(Smi::kMinValue));

  __ xorq(rax, rax);  // Success.
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

void TestSmiCompare(MacroAssembler* masm, Label* exit, int id, int x, int y) {
  __ Move(rcx, Smi::FromInt(x));
  __ movq(r8, rcx);
  __ Move(rdx, Smi::FromInt(y));
  __ movq(r9, rdx);
  __ SmiCompare(rcx, rdx);
  if (x < y) {
    __ movl(rax, Immediate(id + 1));
    __ j(greater_equal, exit);
  } else if (x > y) {
    __ movl(rax, Immediate(id + 2));
    __ j(less_equal, exit);
  } else {
    CHECK_EQ(x, y);
    __ movl(rax, Immediate(id + 3));
    __ j(not_equal, exit);
  }
  // In this build config we clobber SMIs to stress test consumers, thus
  // SmiCompare can actually change unused bits.
#ifndef ENABLE_SLOW_DCHECKS
  __ movl(rax, Immediate(id + 4));
  __ cmpq(rcx, r8);
  __ j(not_equal, exit);
  __ incq(rax);
  __ cmpq(rdx, r9);
  __ j(not_equal, exit);
#endif

  if (x != y) {
    __ SmiCompare(rdx, rcx);
    if (y < x) {
      __ movl(rax, Immediate(id + 9));
      __ j(greater_equal, exit);
    } else {
      CHECK(y > x);
      __ movl(rax, Immediate(id + 10));
      __ j(less_equal, exit);
    }
  } else {
    __ cmpq(rcx, rcx);
    __ movl(rax, Immediate(id + 11));
    __ j(not_equal, exit);
#ifndef ENABLE_SLOW_DCHECKS
    __ incq(rax);
    __ cmpq(rcx, r8);
    __ j(not_equal, exit);
#endif
  }
}

// Test that we can compare smis for equality (and more).
TEST_F(MacroAssemblerX64Test, SmiCompare) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer(2 * Assembler::kDefaultBufferSize);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;

  TestSmiCompare(masm, &exit, 0x10, 0, 0);
  TestSmiCompare(masm, &exit, 0x20, 0, 1);
  TestSmiCompare(masm, &exit, 0x30, 1, 0);
  TestSmiCompare(masm, &exit, 0x40, 1, 1);
  TestSmiCompare(masm, &exit, 0x50, 0, -1);
  TestSmiCompare(masm, &exit, 0x60, -1, 0);
  TestSmiCompare(masm, &exit, 0x70, -1, -1);
  TestSmiCompare(masm, &exit, 0x80, 0, Smi::kMinValue);
  TestSmiCompare(masm, &exit, 0x90, Smi::kMinValue, 0);
  TestSmiCompare(masm, &exit, 0xA0, 0, Smi::kMaxValue);
  TestSmiCompare(masm, &exit, 0xB0, Smi::kMaxValue, 0);
  TestSmiCompare(masm, &exit, 0xC0, -1, Smi::kMinValue);
  TestSmiCompare(masm, &exit, 0xD0, Smi::kMinValue, -1);
  TestSmiCompare(masm, &exit, 0xE0, -1, Smi::kMaxValue);
  TestSmiCompare(masm, &exit, 0xF0, Smi::kMaxValue, -1);
  TestSmiCompare(masm, &exit, 0x100, Smi::kMinValue, Smi::kMinValue);
  TestSmiCompare(masm, &exit, 0x110, Smi::kMinValue, Smi::kMaxValue);
  TestSmiCompare(masm, &exit, 0x120, Smi::kMaxValue, Smi::kMinValue);
  TestSmiCompare(masm, &exit, 0x130, Smi::kMaxValue, Smi::kMaxValue);

  __ xorq(rax, rax);  // Success.
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

TEST_F(MacroAssemblerX64Test, SmiTag) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;

  __ movq(rax, Immediate(1));  // Test number.
  __ movq(rcx, Immediate(0));
  __ SmiTag(rcx);
  __ Move(rdx, Smi::zero().ptr());
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(2));  // Test number.
  __ movq(rcx, Immediate(1024));
  __ SmiTag(rcx);
  __ Move(rdx, Smi::FromInt(1024).ptr());
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(3));  // Test number.
  __ movq(rcx, Immediate(-1));
  __ SmiTag(rcx);
  __ Move(rdx, Smi::FromInt(-1).ptr());
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(4));  // Test number.
  __ movq(rcx, Immediate(Smi::kMaxValue));
  __ SmiTag(rcx);
  __ Move(rdx, Smi::FromInt(Smi::kMaxValue).ptr());
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(5));  // Test number.
  __ movq(rcx, Immediate(Smi::kMinValue));
  __ SmiTag(rcx);
  __ Move(rdx, Smi::FromInt(Smi::kMinValue).ptr());
  __ cmp_tagged(rcx, rdx);
  __ j(not_equal, &exit);

  // Different target register.

  __ movq(rax, Immediate(6));  // Test number.
  __ movq(rcx, Immediate(0));
  __ SmiTag(r8, rcx);
  __ Move(rdx, Smi::zero().ptr());
  __ cmp_tagged(r8, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(7));  // Test number.
  __ movq(rcx, Immediate(1024));
  __ SmiTag(r8, rcx);
  __ Move(rdx, Smi::FromInt(1024).ptr());
  __ cmp_tagged(r8, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(8));  // Test number.
  __ movq(rcx, Immediate(-1));
  __ SmiTag(r8, rcx);
  __ Move(rdx, Smi::FromInt(-1).ptr());
  __ cmp_tagged(r8, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(9));  // Test number.
  __ movq(rcx, Immediate(Smi::kMaxValue));
  __ SmiTag(r8, rcx);
  __ Move(rdx, Smi::FromInt(Smi::kMaxValue).ptr());
  __ cmp_tagged(r8, rdx);
  __ j(not_equal, &exit);

  __ movq(rax, Immediate(10));  // Test number.
  __ movq(rcx, Immediate(Smi::kMinValue));
  __ SmiTag(r8, rcx);
  __ Move(rdx, Smi::FromInt(Smi::kMinValue).ptr());
  __ cmp_tagged(r8, rdx);
  __ j(not_equal, &exit);

  __ xorq(rax, rax);  // Success.
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

TEST_F(MacroAssemblerX64Test, SmiCheck) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;
  Condition cond;

  __ movl(rax, Immediate(1));  // Test number.

  // CheckSmi

  __ movl(rcx, Immediate(0));
  __ SmiTag(rcx);
  cond = masm->CheckSmi(rcx);
  __ j(NegateCondition(cond), &exit);

  __ incq(rax);
  __ xorq(rcx, Immediate(kSmiTagMask));
  cond = masm->CheckSmi(rcx);
  __ j(cond, &exit);

  __ incq(rax);
  __ movl(rcx, Immediate(-1));
  __ SmiTag(rcx);
  cond = masm->CheckSmi(rcx);
  __ j(NegateCondition(cond), &exit);

  __ incq(rax);
  __ xorq(rcx, Immediate(kSmiTagMask));
  cond = masm->CheckSmi(rcx);
  __ j(cond, &exit);

  __ incq(rax);
  __ movl(rcx, Immediate(Smi::kMaxValue));
  __ SmiTag(rcx);
  cond = masm->CheckSmi(rcx);
  __ j(NegateCondition(cond), &exit);

  __ incq(rax);
  __ xorq(rcx, Immediate(kSmiTagMask));
  cond = masm->CheckSmi(rcx);
  __ j(cond, &exit);

  __ incq(rax);
  __ movl(rcx, Immediate(Smi::kMinValue));
  __ SmiTag(rcx);
  cond = masm->CheckSmi(rcx);
  __ j(NegateCondition(cond), &exit);

  __ incq(rax);
  __ xorq(rcx, Immediate(kSmiTagMask));
  cond = masm->CheckSmi(rcx);
  __ j(cond, &exit);

  // Success
  __ xorq(rax, rax);

  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

void TestSmiIndex(MacroAssembler* masm, Label* exit, int id, int x) {
  __ movl(rax, Immediate(id));

  for (int i = 0; i < 8; i++) {
    __ Move(rcx, Smi::FromInt(x));
    SmiIndex index = masm->SmiToIndex(rdx, rcx, i);
    CHECK(index.reg == rcx || index.reg == rdx);
    __ shlq(index.reg, Immediate(index.scale));
    __ Move(r8, static_cast<intptr_t>(x) << i);
    __ cmpq(index.reg, r8);
    __ j(not_equal, exit);
    __ incq(rax);
    __ Move(rcx, Smi::FromInt(x));
    index = masm->SmiToIndex(rcx, rcx, i);
    CHECK(index.reg == rcx);
    __ shlq(rcx, Immediate(index.scale));
    __ Move(r8, static_cast<intptr_t>(x) << i);
    __ cmpq(rcx, r8);
    __ j(not_equal, exit);
    __ incq(rax);
  }
}

TEST_F(MacroAssemblerX64Test, EmbeddedObj) {
#ifdef V8_COMPRESS_POINTERS
  v8_flags.compact_on_every_full_gc = true;

  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;
  Handle<HeapObject> old_array = isolate->factory()->NewFixedArray(2000);
  Handle<HeapObject> my_array = isolate->factory()->NewFixedArray(1000);
  __ Move(rcx, my_array, RelocInfo::COMPRESSED_EMBEDDED_OBJECT);
  __ Move(rax, old_array, RelocInfo::FULL_EMBEDDED_OBJECT);
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  Handle<Code> code =
      Factory::CodeBuilder(isolate, desc, CodeKind::FOR_TESTING).Build();
#ifdef OBJECT_PRINT
  StdoutStream os;
  Print(*code, os);
#endif
  using myF0 = Address();
  auto f = GeneratedCode<myF0>::FromAddress(isolate, code->instruction_start());
  Tagged<Object> result = Tagged<Object>(f.Call());
  CHECK_EQ(old_array->ptr(), result.ptr());

  // Collect garbage to ensure reloc info can be walked by the heap.
  InvokeMajorGC();
  InvokeMajorGC();
  InvokeMajorGC();

  PtrComprCageBase cage_base(isolate);

  // Test the user-facing reloc interface.
  const int mode_mask = RelocInfo::EmbeddedObjectModeMask();
  for (RelocIterator it(*code, mode_mask); !it.done(); it.next()) {
    RelocInfo::Mode mode = it.rinfo()->rmode();
    if (RelocInfo::IsCompressedEmbeddedObject(mode)) {
      CHECK_EQ(*my_array, it.rinfo()->target_object(cage_base));
    } else {
      CHECK(RelocInfo::IsFullEmbeddedObject(mode));
      CHECK_EQ(*old_array, it.rinfo()->target_object(cage_base));
    }
  }
#endif  // V8_COMPRESS_POINTERS
}

TEST_F(MacroAssemblerX64Test, SmiIndex) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer(2 * Assembler::kDefaultBufferSize);
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;

  TestSmiIndex(masm, &exit, 0x10, 0);
  TestSmiIndex(masm, &exit, 0x20, 1);
  TestSmiIndex(masm, &exit, 0x30, 100);
  TestSmiIndex(masm, &exit, 0x40, 1000);
  TestSmiIndex(masm, &exit, 0x50, Smi::kMaxValue);

  __ xorq(rax, rax);  // Success.
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

TEST_F(MacroAssemblerX64Test, OperandOffset) {
  uint32_t data[256];
  for (uint32_t i = 0; i < 256; i++) {
    data[i] = i * 0x01010101;
  }

  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  Label exit;

  EntryCode(masm);
  __ pushq(r13);
  __ pushq(r14);
  __ pushq(rbx);
  __ pushq(rbp);
  __ pushq(Immediate(0x100));  // <-- rbp
  __ movq(rbp, rsp);
  __ pushq(Immediate(0x101));
  __ pushq(Immediate(0x102));
  __ pushq(Immediate(0x103));
  __ pushq(Immediate(0x104));
  __ pushq(Immediate(0x105));  // <-- rbx
  __ pushq(Immediate(0x106));
  __ pushq(Immediate(0x107));
  __ pushq(Immediate(0x108));
  __ pushq(Immediate(0x109));  // <-- rsp
  // rbp = rsp[9]
  // r15 = rsp[3]
  // rbx = rsp[5]
  // r13 = rsp[7]
  __ leaq(r14, Operand(rsp, 3 * kSystemPointerSize));
  __ leaq(r13, Operand(rbp, -3 * kSystemPointerSize));
  __ leaq(rbx, Operand(rbp, -5 * kSystemPointerSize));
  __ movl(rcx, Immediate(2));
  __ Move(r8, reinterpret_cast<Address>(&data[128]), RelocInfo::NO_INFO);
  __ movl(rax, Immediate(1));

  Operand sp0 = Operand(rsp, 0);

  // Test 1.
  __ movl(rdx, sp0);  // Sanity check.
  __ cmpl(rdx, Immediate(0x109));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Test 2.
  // Zero to non-zero displacement.
  __ movl(rdx, Operand(sp0, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x107));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand sp2 = Operand(rsp, 2 * kSystemPointerSize);

  // Test 3.
  __ movl(rdx, sp2);  // Sanity check.
  __ cmpl(rdx, Immediate(0x107));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(sp2, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x105));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Non-zero to zero displacement.
  __ movl(rdx, Operand(sp2, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x109));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand sp2c2 =
      Operand(rsp, rcx, times_system_pointer_size, 2 * kSystemPointerSize);

  // Test 6.
  __ movl(rdx, sp2c2);  // Sanity check.
  __ cmpl(rdx, Immediate(0x105));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(sp2c2, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x103));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Non-zero to zero displacement.
  __ movl(rdx, Operand(sp2c2, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x107));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bp0 = Operand(rbp, 0);

  // Test 9.
  __ movl(rdx, bp0);  // Sanity check.
  __ cmpl(rdx, Immediate(0x100));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Zero to non-zero displacement.
  __ movl(rdx, Operand(bp0, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x102));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bp2 = Operand(rbp, -2 * kSystemPointerSize);

  // Test 11.
  __ movl(rdx, bp2);  // Sanity check.
  __ cmpl(rdx, Immediate(0x102));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Non-zero to zero displacement.
  __ movl(rdx, Operand(bp2, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x100));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bp2, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x104));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bp2c4 =
      Operand(rbp, rcx, times_system_pointer_size, -4 * kSystemPointerSize);

  // Test 14:
  __ movl(rdx, bp2c4);  // Sanity check.
  __ cmpl(rdx, Immediate(0x102));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bp2c4, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x100));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bp2c4, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x104));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bx0 = Operand(rbx, 0);

  // Test 17.
  __ movl(rdx, bx0);  // Sanity check.
  __ cmpl(rdx, Immediate(0x105));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bx0, 5 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x100));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bx0, -4 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x109));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bx2 = Operand(rbx, 2 * kSystemPointerSize);

  // Test 20.
  __ movl(rdx, bx2);  // Sanity check.
  __ cmpl(rdx, Immediate(0x103));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bx2, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x101));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Non-zero to zero displacement.
  __ movl(rdx, Operand(bx2, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x105));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand bx2c2 =
      Operand(rbx, rcx, times_system_pointer_size, -2 * kSystemPointerSize);

  // Test 23.
  __ movl(rdx, bx2c2);  // Sanity check.
  __ cmpl(rdx, Immediate(0x105));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bx2c2, 2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x103));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(bx2c2, -2 * kSystemPointerSize));
  __ cmpl(rdx, Immediate(0x107));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand r80 = Operand(r8, 0);

  // Test 26.
  __ movl(rdx, r80);  // Sanity check.
  __ cmpl(rdx, Immediate(0x80808080));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, -8 * kIntSize));
  __ cmpl(rdx, Immediate(0x78787878));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, 8 * kIntSize));
  __ cmpl(rdx, Immediate(0x88888888));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, -64 * kIntSize));
  __ cmpl(rdx, Immediate(0x40404040));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, 64 * kIntSize));
  __ cmpl(rdx, Immediate(0xC0C0C0C0));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand r88 = Operand(r8, 8 * kIntSize);

  // Test 31.
  __ movl(rdx, r88);  // Sanity check.
  __ cmpl(rdx, Immediate(0x88888888));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r88, -8 * kIntSize));
  __ cmpl(rdx, Immediate(0x80808080));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r88, 8 * kIntSize));
  __ cmpl(rdx, Immediate(0x90909090));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r88, -64 * kIntSize));
  __ cmpl(rdx, Immediate(0x48484848));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r88, 64 * kIntSize));
  __ cmpl(rdx, Immediate(0xC8C8C8C8));
  __ j(not_equal, &exit);
  __ incq(rax);

  Operand r864 = Operand(r8, 64 * kIntSize);

  // Test 36.
  __ movl(rdx, r864);  // Sanity check.
  __ cmpl(rdx, Immediate(0xC0C0C0C0));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r864, -8 * kIntSize));
  __ cmpl(rdx, Immediate(0xB8B8B8B8));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r864, 8 * kIntSize));
  __ cmpl(rdx, Immediate(0xC8C8C8C8));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r864, -64 * kIntSize));
  __ cmpl(rdx, Immediate(0x80808080));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r864, 32 * kIntSize));
  __ cmpl(rdx, Immediate(0xE0E0E0E0));
  __ j(not_equal, &exit);
  __ incq(rax);

  // 32-bit offset to 8-bit offset.
  __ movl(rdx, Operand(r864, -60 * kIntSize));
  __ cmpl(rdx, Immediate(0x84848484));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r864, 60 * kIntSize));
  __ cmpl(rdx, Immediate(0xFCFCFCFC));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Test unaligned offsets.

  // Test 43.
  __ movl(rdx, Operand(r80, 2));
  __ cmpl(rdx, Immediate(0x81818080));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, -2));
  __ cmpl(rdx, Immediate(0x80807F7F));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, 126));
  __ cmpl(rdx, Immediate(0xA0A09F9F));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, -126));
  __ cmpl(rdx, Immediate(0x61616060));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, 254));
  __ cmpl(rdx, Immediate(0xC0C0BFBF));
  __ j(not_equal, &exit);
  __ incq(rax);

  __ movl(rdx, Operand(r80, -254));
  __ cmpl(rdx, Immediate(0x41414040));
  __ j(not_equal, &exit);
  __ incq(rax);

  // Success.

  __ movl(rax, Immediate(0));
  __ bind(&exit);
  __ leaq(rsp, Operand(rbp, kSystemPointerSize));
  __ popq(rbp);
  __ popq(rbx);
  __ popq(r14);
  __ popq(r13);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

void TestFloat32x4Abs(MacroAssembler* masm, Label* exit, float x, float y,
                      float z, float w) {
  __ AllocateStackSpace(kSimd128Size);

  __ Move(xmm1, x);
  __ Movss(Operand(rsp, 0 * kFloatSize), xmm1);
  __ Move(xmm2, y);
  __ Movss(Operand(rsp, 1 * kFloatSize), xmm2);
  __ Move(xmm3, z);
  __ Movss(Operand(rsp, 2 * kFloatSize), xmm3);
  __ Move(xmm4, w);
  __ Movss(Operand(rsp, 3 * kFloatSize), xmm4);
  __ Movups(xmm0, Operand(rsp, 0));

  __ Absps(xmm0, xmm0, kScratchRegister);
  __ Movups(Operand(rsp, 0), xmm0);

  __ incq(rax);
  __ Move(xmm1, fabsf(x));
  __ Ucomiss(xmm1, Operand(rsp, 0 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm2, fabsf(y));
  __ Ucomiss(xmm2, Operand(rsp, 1 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm3, fabsf(z));
  __ Ucomiss(xmm3, Operand(rsp, 2 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm4, fabsf(w));
  __ Ucomiss(xmm4, Operand(rsp, 3 * kFloatSize));
  __ j(not_equal, exit);

  __ addq(rsp, Immediate(kSimd128Size));
}

void TestFloat32x4Neg(MacroAssembler* masm, Label* exit, float x, float y,
                      float z, float w) {
  __ AllocateStackSpace(kSimd128Size);

  __ Move(xmm1, x);
  __ Movss(Operand(rsp, 0 * kFloatSize), xmm1);
  __ Move(xmm2, y);
  __ Movss(Operand(rsp, 1 * kFloatSize), xmm2);
  __ Move(xmm3, z);
  __ Movss(Operand(rsp, 2 * kFloatSize), xmm3);
  __ Move(xmm4, w);
  __ Movss(Operand(rsp, 3 * kFloatSize), xmm4);
  __ Movups(xmm0, Operand(rsp, 0));

  __ Negps(xmm0, xmm0, kScratchRegister);
  __ Movups(Operand(rsp, 0), xmm0);

  __ incq(rax);
  __ Move(xmm1, -x);
  __ Ucomiss(xmm1, Operand(rsp, 0 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm2, -y);
  __ Ucomiss(xmm2, Operand(rsp, 1 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm3, -z);
  __ Ucomiss(xmm3, Operand(rsp, 2 * kFloatSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm4, -w);
  __ Ucomiss(xmm4, Operand(rsp, 3 * kFloatSize));
  __ j(not_equal, exit);

  __ addq(rsp, Immediate(kSimd128Size));
}

void TestFloat64x2Abs(MacroAssembler* masm, Label* exit, double x, double y) {
  __ AllocateStackSpace(kSimd128Size);

  __ Move(xmm1, x);
  __ Movsd(Operand(rsp, 0 * kDoubleSize), xmm1);
  __ Move(xmm2, y);
  __ Movsd(Operand(rsp, 1 * kDoubleSize), xmm2);
  __ movupd(xmm0, Operand(rsp, 0));

  __ Abspd(xmm0, xmm0, kScratchRegister);
  __ movupd(Operand(rsp, 0), xmm0);

  __ incq(rax);
  __ Move(xmm1, fabs(x));
  __ Ucomisd(xmm1, Operand(rsp, 0 * kDoubleSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm2, fabs(y));
  __ Ucomisd(xmm2, Operand(rsp, 1 * kDoubleSize));
  __ j(not_equal, exit);

  __ addq(rsp, Immediate(kSimd128Size));
}

void TestFloat64x2Neg(MacroAssembler* masm, Label* exit, double x, double y) {
  __ AllocateStackSpace(kSimd128Size);

  __ Move(xmm1, x);
  __ Movsd(Operand(rsp, 0 * kDoubleSize), xmm1);
  __ Move(xmm2, y);
  __ Movsd(Operand(rsp, 1 * kDoubleSize), xmm2);
  __ movupd(xmm0, Operand(rsp, 0));

  __ Negpd(xmm0, xmm0, kScratchRegister);
  __ movupd(Operand(rsp, 0), xmm0);

  __ incq(rax);
  __ Move(xmm1, -x);
  __ Ucomisd(xmm1, Operand(rsp, 0 * kDoubleSize));
  __ j(not_equal, exit);
  __ incq(rax);
  __ Move(xmm2, -y);
  __ Ucomisd(xmm2, Operand(rsp, 1 * kDoubleSize));
  __ j(not_equal, exit);

  __ addq(rsp, Immediate(kSimd128Size));
}

TEST_F(MacroAssemblerX64Test, SIMDMacros) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;
  EntryCode(masm);
  Label exit;

  __ xorq(rax, rax);
  TestFloat32x4Abs(masm, &exit, 1.5, -1.5, 0.5, -0.5);
  TestFloat32x4Neg(masm, &exit, 1.5, -1.5, 0.5, -0.5);
  TestFloat64x2Abs(masm, &exit, 1.75, -1.75);
  TestFloat64x2Neg(masm, &exit, 1.75, -1.75);

  __ xorq(rax, rax);  // Success.
  __ bind(&exit);
  ExitCode(masm);
  __ ret(0);

  CodeDesc desc;
  masm->GetCode(isolate, &desc);
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F0>::FromBuffer(i_isolate(), buffer->start());
  int result = f.Call();
  CHECK_EQ(0, result);
}

TEST_F(MacroAssemblerX64Test, S256Select) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());

  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister mask = ymm1;
  const YMMRegister src1 = ymm2;
  const YMMRegister src2 = ymm3;
  const YMMRegister tmp = ymm4;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load src1, src2, mask
  __ vmovdqu(src1, Operand(kCArgRegs[0], 0));
  __ vmovdqu(src2, Operand(kCArgRegs[1], 0));
  __ vmovdqu(mask, Operand(kCArgRegs[2], 0));
  // Bitselect
  __ S256Select(dst, mask, src1, src2, tmp);
  // Store result
  __ vmovdqu(Operand(kCArgRegs[3], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F2>::FromBuffer(i_isolate(), buffer->start());

  std::vector<std::array<uint64_t, 12>> test_cases = {
      {0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA,
       0xAAAAAAAAAAAAAAAA, 0xBBBBBBBBBBBBBBBB, 0xBBBBBBBBBBBBBBBB,
       0xBBBBBBBBBBBBBBBB, 0xBBBBBBBBBBBBBBBB, 0x00112345F00FFFFF,
       0x10112021BBAABBAA, 0x0000000000000000, 0x0000000000000000},
      {0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA,
       0xAAAAAAAAAAAAAAAA, 0xBBBBBBBBBBBBBBBB, 0xBBBBBBBBBBBBBBBB,
       0xBBBBBBBBBBBBBBBB, 0xBBBBBBBBBBBBBBBB, 0x1111111111111111,
       0x1111111111111111, 0x0123456789ABCDEF, 0xFEDCBA9876543210},
      {0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA, 0xAAAAAAAAAAAAAAAA,
       0xAAAAAAAAAAAAAAAA, 0x5555555555555555, 0x5555555555555555,
       0x5555555555555555, 0x5555555555555555, 0x0123456789ABCDEF,
       0xFEDCBA9876543210, 0x55555555AAAAAAAA, 0x00000000FFFFFFFF},
      {0x499602D2499602D2, 0x499602D2499602D2, 0x1234567812345678,
       0x1234567812345678, 0xB669FD2EB669FD2E, 0xB669FD2EB669FD2E,
       0x90ABCDEF90ABCDEF, 0x90ABCDEF90ABCDEF, 0xCDEFCDEFCDEFCDEF,
       0xCDEFCDEFCDEFCDEF, 0xCDEFCDEFCDEFCDEF, 0xCDEFCDEFCDEFCDEF}};

  uint64_t v1[4];
  uint64_t v2[4];
  uint64_t c[4];
  uint64_t output[4];

  for (const auto& arr : test_cases) {
    v1[0] = arr[0];
    v1[1] = arr[1];
    v1[2] = arr[2];
    v1[3] = arr[3];

    v2[0] = arr[4];
    v2[1] = arr[5];
    v2[2] = arr[6];
    v2[3] = arr[7];

    c[0] = arr[8];
    c[1] = arr[9];
    c[2] = arr[10];
    c[3] = arr[11];

    f.Call(v1, v2, c, output);

    for (int i = 0; i < 4; i++) {
      CHECK_EQ(output[i], (v1[i] & c[i]) | (v2[i] & ~c[i]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, AreAliased) {
  DCHECK(!AreAliased(rax));
  DCHECK(!AreAliased(rax, no_reg));
  DCHECK(!AreAliased(no_reg, rax, no_reg));

  DCHECK(AreAliased(rax, rax));
  DCHECK(!AreAliased(no_reg, no_reg));

  DCHECK(!AreAliased(rax, rbx, rcx, rdx, no_reg));
  DCHECK(AreAliased(rax, rbx, rcx, rdx, rax, no_reg));

  // no_regs are allowed in
  DCHECK(!AreAliased(rax, no_reg, rbx, no_reg, rcx, no_reg, rdx, no_reg));
  DCHECK(AreAliased(rax, no_reg, rbx, no_reg, rcx, no_reg, rdx, rax, no_reg));
}

TEST_F(MacroAssemblerX64Test, DeoptExitSizeIsFixed) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate, v8::internal::CodeObjectRequired::kYes,
                      buffer->CreateView());

  static_assert(static_cast<int>(kFirstDeoptimizeKind) == 0);
  for (int i = 0; i < kDeoptimizeKindCount; i++) {
    DeoptimizeKind kind = static_cast<DeoptimizeKind>(i);
    Label before_exit;

    masm.bind(&before_exit);
    if (kind == DeoptimizeKind::kLazy) {
      masm.CodeEntry();
    }
    Builtin target = Deoptimizer::GetDeoptimizationEntry(kind);
    masm.CallForDeoptimization(target, 42, &before_exit, kind, &before_exit,
                               nullptr);
    CHECK_EQ(masm.SizeOfCodeGeneratedSince(&before_exit),
             kind == DeoptimizeKind::kLazy ? Deoptimizer::kLazyDeoptExitSize
                                           : Deoptimizer::kEagerDeoptExitSize);
  }
}

TEST_F(MacroAssemblerX64Test, I64x2Mul) {
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const XMMRegister dst = xmm0;
  const XMMRegister lhs = xmm1;
  const XMMRegister rhs = xmm2;
  const XMMRegister tmp1 = xmm3;
  const XMMRegister tmp2 = xmm4;

  // Load array
  __ movdqu(lhs, Operand(kCArgRegs[0], 0));
  __ movdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ I64x2Mul(dst, lhs, rhs, tmp1, tmp2);
  // Store result array
  __ movdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F1>::FromBuffer(i_isolate(), buffer->start());

  constexpr uint64_t uint64_max = std::numeric_limits<uint64_t>::max();

  std::vector<std::array<uint64_t, 4>> test_cases = {
      {1, 2, 3, 4},
      {324, 25, 124, 62346},
      {345, 263, 2346, 3468},
      {0, 0, 0, 0},
      {uint64_max, uint64_max, uint64_max, uint64_max}};

  uint64_t left[2];
  uint64_t right[2];
  uint64_t output[2];

  for (const auto& arr : test_cases) {
    left[0] = arr[0];
    left[1] = arr[1];
    right[0] = arr[2];
    right[1] = arr[3];

    f.Call(left, right, output);
    CHECK_EQ(output[0], left[0] * right[0]);
    CHECK_EQ(output[1], left[1] * right[1]);
  }
}

TEST_F(MacroAssemblerX64Test, I64x4Mul) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister lhs = ymm1;
  const YMMRegister rhs = ymm2;
  const YMMRegister tmp1 = ymm3;
  const YMMRegister tmp2 = ymm4;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ I64x4Mul(dst, lhs, rhs, tmp1, tmp2);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F1>::FromBuffer(i_isolate(), buffer->start());

  constexpr uint64_t uint64_max = std::numeric_limits<uint64_t>::max();

  std::vector<std::array<uint64_t, 8>> test_cases = {
      {1, 2, 3, 4, 5, 6, 7, 8},
      {324, 25, 124, 62346, 2356, 236, 12534, 6346},
      {345, 263, 2346, 3468, 2346, 1264, 236, 236},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {uint64_max, uint64_max, uint64_max, uint64_max, uint64_max, uint64_max,
       uint64_max, uint64_max}};

  uint64_t left[4];
  uint64_t right[4];
  uint64_t output[4];

  for (const auto& arr : test_cases) {
    left[0] = arr[0];
    left[1] = arr[1];
    left[2] = arr[2];
    left[3] = arr[3];
    right[0] = arr[4];
    right[1] = arr[5];
    right[2] = arr[6];
    right[3] = arr[7];

    f.Call(left, right, output);
    CHECK_EQ(output[0], left[0] * right[0]);
    CHECK_EQ(output[1], left[1] * right[1]);
    CHECK_EQ(output[2], left[2] * right[2]);
    CHECK_EQ(output[3], left[3] * right[3]);
  }
}

#define TEST_ISLPAT(name, lane_size, lane_num, Fn)                            \
  TEST_F(MacroAssemblerX64Test, name) {                                       \
    if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2))    \
      return;                                                                 \
    Isolate* isolate = i_isolate();                                           \
    HandleScope handles(isolate);                                             \
    auto buffer = AllocateAssemblerBuffer();                                  \
    MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes, \
                             buffer->CreateView());                           \
    MacroAssembler* masm = &assembler;                                        \
    CpuFeatureScope avx_scope(masm, AVX);                                     \
    CpuFeatureScope avx2_scope(masm, AVX2);                                   \
                                                                              \
    /* src is register */                                                     \
    __ name(ymm0, kCArgRegs[0]);                                              \
    __ vmovdqu(Operand(kCArgRegs[2], 0), ymm0);                               \
                                                                              \
    /* src is address*/                                                       \
    __ name(ymm0, Operand(kCArgRegs[1], 0));                                  \
    __ vmovdqu(Operand(kCArgRegs[3], 0), ymm0);                               \
    __ ret(0);                                                                \
                                                                              \
    CodeDesc desc;                                                            \
    __ GetCode(i_isolate(), &desc);                                           \
                                                                              \
    PrintCode(isolate, desc);                                                 \
    buffer->MakeExecutable();                                                 \
    /* Call the function from C++. */                                         \
    auto f = GeneratedCode<Fn>::FromBuffer(i_isolate(), buffer->start());     \
                                                                              \
    FOR_INT##lane_size##_INPUTS(input) {                                      \
      int##lane_size##_t* input_addr = &input;                                \
      int##lane_size##_t output1[lane_num];                                   \
      int##lane_size##_t output2[lane_num];                                   \
                                                                              \
      f.Call(input, input_addr, output1, output2);                            \
                                                                              \
      for (int i = 0; i < lane_num; ++i) {                                    \
        CHECK_EQ(input, output1[i]);                                          \
        CHECK_EQ(input, output2[i]);                                          \
      }                                                                       \
    }                                                                         \
  }

TEST_ISLPAT(I8x32Splat, 8, 32, F3)
TEST_ISLPAT(I16x16Splat, 16, 16, F4)
TEST_ISLPAT(I32x8Splat, 32, 8, F5)
TEST_ISLPAT(I64x4Splat, 64, 4, F6)

#undef TEST_ISLPAT

TEST_F(MacroAssemblerX64Test, F64x4Min) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister lhs = ymm1;
  const YMMRegister rhs = ymm2;
  const YMMRegister tmp = ymm3;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ F64x4Min(dst, lhs, rhs, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

#ifdef OBJECT_PRINT
  Handle<Code> code =
      Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F7>::FromBuffer(i_isolate(), buffer->start());

  constexpr double double_max = std::numeric_limits<double>::max();
  constexpr double double_min = std::numeric_limits<double>::min();

  std::vector<std::array<double, 8>> test_cases = {
      {1, 2, 7, 8, 5, 6, 3, 4},
      {32.4, 2.5, 12.4, 62.346, 235.6, 2.36, 1253.4, 63.46},
      {34.5, 2.63, 234.6, 34.68, 234.6, 1.264, 23.6, 2.36},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {double_min, double_min, double_max, double_max, double_max, double_max,
       double_min, double_min}};

  double left[4];
  double right[4];
  double output[4];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 4; i++) {
      left[i] = arr[i];
      right[i] = arr[i + 4];
    }

    f.Call(left, right, output);
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(output[i], std::min(left[i], right[i]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, F64x4Max) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister lhs = ymm1;
  const YMMRegister rhs = ymm2;
  const YMMRegister tmp = ymm3;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ F64x4Max(dst, lhs, rhs, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

#ifdef OBJECT_PRINT
  Handle<Code> code =
      Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F7>::FromBuffer(i_isolate(), buffer->start());

  constexpr double double_max = std::numeric_limits<double>::max();
  constexpr double double_min = std::numeric_limits<double>::min();

  std::vector<std::array<double, 8>> test_cases = {
      {1, 2, 7, 8, 5, 6, 3, 4},
      {32.4, 2.5, 12.4, 62.346, 235.6, 2.36, 1253.4, 63.46},
      {34.5, 2.63, 234.6, 34.68, 234.6, 1.264, 23.6, 2.36},
      {0, 0, 0, 0, 0, 0, 0, 0},
      {double_min, double_min, double_max, double_max, double_max, double_max,
       double_min, double_min}};

  double left[4];
  double right[4];
  double output[4];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 4; i++) {
      left[i] = arr[i];
      right[i] = arr[i + 4];
    }

    f.Call(left, right, output);
    for (int i = 0; i < 4; i++) {
      CHECK_EQ(output[i], std::max(left[i], right[i]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, F32x8Min) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister lhs = ymm1;
  const YMMRegister rhs = ymm2;
  const YMMRegister tmp = ymm3;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ F32x8Min(dst, lhs, rhs, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

#ifdef OBJECT_PRINT
  Handle<Code> code =
      Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F8>::FromBuffer(i_isolate(), buffer->start());

  constexpr float float_max = std::numeric_limits<float>::max();
  constexpr float float_min = std::numeric_limits<float>::min();

  std::vector<std::array<float, 16>> test_cases = {
      {1, 2, 3, 4, 5, 6, 7, 8, 5, 6, 7, 8, 1, 2, 3, 4},
      {32.4, 2.5, 12.4, 62.346, 235.6, 2.36, 1253.4, 63.46, 235.6, 2.36, 1253.4,
       63.46, 32.4, 2.5, 12.4, 62.346},
      {34.5, 2.63, 234.6, 34.68, 234.6, 1.264, 23.6, 2.36, 234.6, 1.264, 23.6,
       2.36, 34.5, 2.63, 234.6, 34.68},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {float_min, float_min, float_max, float_max, float_max, float_max,
       float_min, float_min, float_max, float_max, float_min, float_min,
       float_min, float_min, float_max, float_max}};

  float left[8];
  float right[8];
  float output[8];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 8; i++) {
      left[i] = arr[i];
      right[i] = arr[i + 8];
    }

    f.Call(left, right, output);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(output[i], std::min(left[i], right[i]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, F32x8Max) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister lhs = ymm1;
  const YMMRegister rhs = ymm2;
  const YMMRegister tmp = ymm3;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));
  // Calculation
  __ F32x8Max(dst, lhs, rhs, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

#ifdef OBJECT_PRINT
  Handle<Code> code =
      Factory::CodeBuilder(i_isolate(), desc, CodeKind::FOR_TESTING).Build();
  StdoutStream os;
  Print(*code, os);
#endif
  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F8>::FromBuffer(i_isolate(), buffer->start());

  constexpr float float_max = std::numeric_limits<float>::max();
  constexpr float float_min = std::numeric_limits<float>::min();

  std::vector<std::array<float, 16>> test_cases = {
      {1, 2, 3, 4, 5, 6, 7, 8, 5, 6, 7, 8, 1, 2, 3, 4},
      {32.4, 2.5, 12.4, 62.346, 235.6, 2.36, 1253.4, 63.46, 235.6, 2.36, 1253.4,
       63.46, 32.4, 2.5, 12.4, 62.346},
      {34.5, 2.63, 234.6, 34.68, 234.6, 1.264, 23.6, 2.36, 234.6, 1.264, 23.6,
       2.36, 34.5, 2.63, 234.6, 34.68},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {float_min, float_min, float_max, float_max, float_max, float_max,
       float_min, float_min, float_max, float_max, float_min, float_min,
       float_min, float_min, float_max, float_max}};

  float left[8];
  float right[8];
  float output[8];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 8; i++) {
      left[i] = arr[i];
      right[i] = arr[i + 8];
    }

    f.Call(left, right, output);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(output[i], std::max(left[i], right[i]));
    }
  }
}

namespace {

template <typename S, typename T, typename OpType = T (*)(S, S)>
void RunExtMulTest(Isolate* isolate, OpType expected_op) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;

  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const XMMRegister lhs = xmm1;
  const XMMRegister rhs = xmm2;
  const YMMRegister tmp = ymm3;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(lhs, Operand(kCArgRegs[0], 0));
  __ vmovdqu(rhs, Operand(kCArgRegs[1], 0));

  bool is_signed = std::is_signed_v<T>;
  // Calculation
  switch (sizeof(T)) {
    case 8:
      __ I64x4ExtMul(dst, lhs, rhs, tmp, is_signed);
      break;
    case 4:
      __ I32x8ExtMul(dst, lhs, rhs, tmp, is_signed);
      break;
    case 2:
      __ I16x16ExtMul(dst, lhs, rhs, tmp, is_signed);
      break;
    default:
      UNREACHABLE();
  }

  // Store result array
  __ vmovdqu(Operand(kCArgRegs[2], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(isolate, &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F1>::FromBuffer(isolate, buffer->start());

  uint64_t left[2];
  uint64_t right[2];
  uint64_t output[4];
  constexpr int lanes = kSimd128Size / sizeof(S);
  T* g = reinterpret_cast<T*>(output);
  for (S x : compiler::ValueHelper::GetVector<S>()) {
    for (S y : compiler::ValueHelper::GetVector<S>()) {
      left[0] = 0;
      right[0] = 0;
      uint64_t mask = (static_cast<uint64_t>(1) << sizeof(S) * 8) - 1;
      uint64_t lane_x = static_cast<uint64_t>(x) & mask;
      uint64_t lane_y = static_cast<uint64_t>(y) & mask;
      for (int i = 0; i < lanes / 2; i++) {
        left[0] = left[0] | (lane_x << 8 * sizeof(S) * i);
        right[0] = right[0] | (lane_y << 8 * sizeof(S) * i);
      }
      left[1] = left[0];
      right[1] = right[0];

      f.Call(left, right, output);

      T expected = expected_op(x, y);
      for (int i = 0; i < lanes; i++) {
        CHECK_EQ(expected, g[i]);
      }
    }
  }
}

}  // namespace

TEST_F(MacroAssemblerX64Test, I16x16ExtMulI8x16S) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<int8_t, int16_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I16x16ExtMulI8x16U) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<uint8_t, uint16_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I32x8ExtMulI16x8S) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<int16_t, int32_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I32x8ExtMulI16x8U) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<uint16_t, uint32_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I64x4ExtMulI32x4S) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<int32_t, int64_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I64x4ExtMulI32x4U) {
  Isolate* isolate = i_isolate();
  RunExtMulTest<uint32_t, uint64_t>(isolate, MultiplyLong);
}

TEST_F(MacroAssemblerX64Test, I32x8ExtAddPairwiseI16x16S) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister src = ymm1;
  const YMMRegister tmp = ymm2;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(src, Operand(kCArgRegs[0], 0));
  // Calculation
  __ I32x8ExtAddPairwiseI16x16S(dst, src, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[1], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F9>::FromBuffer(i_isolate(), buffer->start());

  constexpr int16_t int16_max = std::numeric_limits<int16_t>::max();

  std::vector<std::array<int16_t, 16>> test_cases = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 110, 111, 112, 113, 114, 115},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {-1, 2, 3, -4, -5, 6, 7, -8, -9, 10, 11, -12, -13, 14, 15, -16},
      {int16_max, int16_max, int16_max, int16_max, int16_max, int16_max,
       int16_max, int16_max, int16_max, int16_max, int16_max, int16_max,
       int16_max, int16_max, int16_max, int16_max}};

  int16_t input[16];
  int32_t output[8];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 16; i++) {
      input[i] = arr[i];
    }
    f.Call(input, output);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(output[i], (int32_t)(input[2 * i] + input[2 * i + 1]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, I16x16ExtAddPairwiseI8x32S) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister src = ymm1;
  const YMMRegister tmp = ymm2;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(src, Operand(kCArgRegs[0], 0));
  // Calculation
  __ I16x16ExtAddPairwiseI8x32S(dst, src, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[1], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F10>::FromBuffer(i_isolate(), buffer->start());

  constexpr int8_t int8_max = std::numeric_limits<int8_t>::max();

  std::vector<std::array<int8_t, 32>> test_cases = {
      {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
       16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {-1,  2,  3,  -4,  -5,  6,  7,  -8,  -9,  10, 11, -12, -13, 14, 15, -16,
       -17, 18, 19, -20, -21, 22, 23, -24, -25, 26, 27, -28, -29, 30, 31},
      {int8_max, int8_max, int8_max, int8_max, int8_max, int8_max, int8_max,
       int8_max, int8_max, int8_max, int8_max, int8_max, int8_max, int8_max,
       int8_max, int8_max, int8_max, int8_max, int8_max, int8_max, int8_max,
       int8_max, int8_max, int8_max, int8_max, int8_max, int8_max, int8_max,
       int8_max, int8_max, int8_max, int8_max}};

  int8_t input[32];
  int16_t output[16];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 32; i++) {
      input[i] = arr[i];
    }
    f.Call(input, output);
    for (int i = 0; i < 16; i++) {
      CHECK_EQ(output[i], (int16_t)(input[2 * i] + input[2 * i + 1]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, I32x8ExtAddPairwiseI16x16U) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister src = ymm1;
  const YMMRegister tmp = ymm2;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(src, Operand(kCArgRegs[0], 0));
  // Calculation
  __ I32x8ExtAddPairwiseI16x16U(dst, src, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[1], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F11>::FromBuffer(i_isolate(), buffer->start());

  constexpr uint16_t uint16_max = std::numeric_limits<uint16_t>::max();

  std::vector<std::array<uint16_t, 16>> test_cases = {
      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
      {10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 110, 111, 112, 113, 114, 115},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {uint16_max, uint16_max, uint16_max, uint16_max, uint16_max, uint16_max,
       uint16_max, uint16_max, uint16_max, uint16_max, uint16_max, uint16_max,
       uint16_max, uint16_max, uint16_max, uint16_max}};

  uint16_t input[16];
  uint32_t output[8];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 16; i++) {
      input[i] = arr[i];
    }
    f.Call(input, output);
    for (int i = 0; i < 8; i++) {
      CHECK_EQ(output[i], (uint32_t)(input[2 * i] + input[2 * i + 1]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, I16x16ExtAddPairwiseI8x32U) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;

  const YMMRegister dst = ymm0;
  const YMMRegister src = ymm1;
  const YMMRegister tmp = ymm2;

  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  // Load array
  __ vmovdqu(src, Operand(kCArgRegs[0], 0));
  // Calculation
  __ I16x16ExtAddPairwiseI8x32U(dst, src, tmp);
  // Store result array
  __ vmovdqu(Operand(kCArgRegs[1], 0), dst);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(i_isolate(), desc);

  buffer->MakeExecutable();
  // Call the function from C++.
  auto f = GeneratedCode<F12>::FromBuffer(i_isolate(), buffer->start());

  constexpr uint8_t uint8_max = std::numeric_limits<uint8_t>::max();

  std::vector<std::array<uint8_t, 32>> test_cases = {
      {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
       16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31},
      {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
      {uint8_max, uint8_max, uint8_max, uint8_max, uint8_max, uint8_max,
       uint8_max, uint8_max, uint8_max, uint8_max, uint8_max, uint8_max,
       uint8_max, uint8_max, uint8_max, uint8_max, uint8_max, uint8_max,
       uint8_max, uint8_max, uint8_max, uint8_max, uint8_max, uint8_max,
       uint8_max, uint8_max, uint8_max, uint8_max, uint8_max, uint8_max,
       uint8_max, uint8_max}};

  uint8_t input[32];
  uint16_t output[16];

  for (const auto& arr : test_cases) {
    for (int i = 0; i < 32; i++) {
      input[i] = arr[i];
    }
    f.Call(input, output);
    for (int i = 0; i < 16; i++) {
      CHECK_EQ(output[i], (uint16_t)(input[2 * i] + input[2 * i + 1]));
    }
  }
}

TEST_F(MacroAssemblerX64Test, F64x4Splat) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;
  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  __ vmovsd(xmm1, Operand(kCArgRegs[0], 0));
  __ F64x4Splat(ymm2, xmm1);
  __ vmovdqu(Operand(kCArgRegs[1], 0), ymm2);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);
  buffer->MakeExecutable();
  /* Call the function from C++. */
  using F = int(double*, double*);
  auto f = GeneratedCode<F>::FromBuffer(i_isolate(), buffer->start());
  constexpr int kLaneNum = 4;
  double output[kLaneNum];
  FOR_FLOAT64_INPUTS(input) {
    f.Call(&input, output);
    for (int i = 0; i < kLaneNum; ++i) {
      CHECK_EQ(0, std::memcmp(&input, &output[i], sizeof(double)));
    }
  }
}

TEST_F(MacroAssemblerX64Test, F32x8Splat) {
  if (!CpuFeatures::IsSupported(AVX) || !CpuFeatures::IsSupported(AVX2)) return;
  Isolate* isolate = i_isolate();
  HandleScope handles(isolate);
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler assembler(isolate, v8::internal::CodeObjectRequired::kYes,
                           buffer->CreateView());
  MacroAssembler* masm = &assembler;
  CpuFeatureScope avx_scope(masm, AVX);
  CpuFeatureScope avx2_scope(masm, AVX2);

  __ vmovss(xmm1, Operand(kCArgRegs[0], 0));
  __ F32x8Splat(ymm2, xmm1);
  __ vmovdqu(Operand(kCArgRegs[1], 0), ymm2);
  __ ret(0);

  CodeDesc desc;
  __ GetCode(i_isolate(), &desc);

  PrintCode(isolate, desc);
  buffer->MakeExecutable();
  /* Call the function from C++. */
  using F = int(float*, float*);
  auto f = GeneratedCode<F>::FromBuffer(i_isolate(), buffer->start());
  constexpr int kLaneNum = 8;
  float output[kLaneNum];
  FOR_FLOAT32_INPUTS(input) {
    f.Call(&input, output);
    for (int i = 0; i < kLaneNum; ++i) {
      CHECK_EQ(0, std::memcmp(&input, &output[i], sizeof(float)));
    }
  }
}

#undef __

}  // namespace test_macro_assembler_x64
}  // namespace internal
}  // namespace v8
