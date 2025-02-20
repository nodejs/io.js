// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/macro-assembler.h"
#include "src/execution/simulator.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {

#define __ masm.

// Test the ia32 assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.

class MacroAssemblerTest : public TestWithIsolate {};

TEST_F(MacroAssemblerTest, TestHardAbort) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  // Initialize the root register, as we need it for `Abort()`. Since `Abort()`
  // does not return properly, we don't need to restore `kRootRegister`, even
  // though it's a callee-saved register.
  __ LoadAddress(kRootRegister, ExternalReference::isolate_root(isolate()));
  __ set_root_array_available(true);
  __ set_abort_hard(true);

  __ Abort(AbortReason::kNoReason);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void>::FromBuffer(isolate(), buffer->start());

  ASSERT_DEATH_IF_SUPPORTED(
      { f.Call(); }, v8_flags.debug_code ? "abort: no reason" : "");
}

TEST_F(MacroAssemblerTest, TestCheck) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  // Initialize the root register, as we need it for `Check()`.
  // Save the value in `kRootRegister` to restore it later after the call. In
  // some configurations `kRootRegister` is callee-saved for C++.
  __ mov(ecx, kRootRegister);
  __ LoadAddress(kRootRegister, ExternalReference::isolate_root(isolate()));
  __ set_root_array_available(true);
  __ set_abort_hard(true);

  // Fail if the first parameter is 17.
  __ mov(eax, 17);
  __ cmp(eax, Operand(esp, 4));  // compare with 1st parameter.
  __ Check(Condition::not_equal, AbortReason::kNoReason);
  // Restore the original value of `kRootRegister`.
  __ mov(kRootRegister, ecx);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
  f.Call(18);
  ASSERT_DEATH_IF_SUPPORTED(
      { f.Call(17); }, v8_flags.debug_code ? "abort: no reason" : "");
}

TEST_F(MacroAssemblerTest, TestPCRelLea) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  // Initialize the root register, as we need it for `Check()`.
  // Save the value in `kRootRegister` to restore it later after the call. In
  // some configurations `kRootRegister` is callee-saved for C++.
  __ mov(edi, kRootRegister);
  __ LoadAddress(kRootRegister, ExternalReference::isolate_root(isolate()));
  __ set_root_array_available(true);
  __ set_abort_hard(true);

  Label pt;
  __ LoadLabelAddress(ecx, &pt);
  __ mov(eax, 42);
  __ call(ecx);
  __ cmp(eax, 56);
  __ Check(Condition::equal, AbortReason::kNoReason);
  // Restore the original value of `kRootRegister`.
  __ mov(kRootRegister, edi);
  __ ret(0);
  __ bind(&pt);
  __ mov(eax, 56);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
}

TEST_F(MacroAssemblerTest, TestDefinedPCRelLea) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  // Initialize the root register, as we need it for `Check()`.
  // Save the value in `kRootRegister` to restore it later after the call. In
  // some configurations `kRootRegister` is callee-saved for C++.
  __ mov(edi, kRootRegister);
  __ LoadAddress(kRootRegister, ExternalReference::isolate_root(isolate()));
  __ set_root_array_available(true);
  __ set_abort_hard(true);

  Label pt, start;
  __ jmp(&start);
  __ bind(&pt);
  __ mov(eax, 56);
  __ ret(0);
  __ bind(&start);
  __ LoadLabelAddress(ecx, &pt);
  __ mov(eax, 42);
  __ call(ecx);
  __ cmp(eax, 56);
  __ Check(Condition::equal, AbortReason::kNoReason);
  // Restore the original value of `kRootRegister`.
  __ mov(kRootRegister, edi);
  __ ret(0);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
}

#undef __

}  // namespace internal
}  // namespace v8
