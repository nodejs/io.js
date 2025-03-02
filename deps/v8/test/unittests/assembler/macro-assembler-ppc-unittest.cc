// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/macro-assembler.h"
#include "src/codegen/ppc/assembler-ppc-inl.h"
#include "src/execution/simulator.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"
#include "testing/gtest-support.h"

namespace v8 {
namespace internal {

#define __ masm.

// Test the ppc assembler by compiling some simple functions into
// a buffer and executing them.  These tests do not initialize the
// V8 library, create a context, or use any V8 objects.

class MacroAssemblerTest : public TestWithIsolate {};

TEST_F(MacroAssemblerTest, TestHardAbort) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  __ Abort(AbortReason::kNoReason);

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  // We need an isolate here to execute in the simulator.
  auto f = GeneratedCode<void>::FromBuffer(isolate(), buffer->start());

  ASSERT_DEATH_IF_SUPPORTED(
      { f.Call(); }, v8_flags.debug_code ? "abort: no reason" : "");
}

TEST_F(MacroAssemblerTest, TestCheck) {
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);

  // Fail if the first parameter is 17.
  __ mov(r4, Operand(17));
  __ cmp(r3, r4);  // 1st parameter is in {r3}.
  __ Check(Condition::ne, AbortReason::kNoReason);
  __ Ret();

  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  // We need an isolate here to execute in the simulator.
  auto f = GeneratedCode<void, int>::FromBuffer(isolate(), buffer->start());

  f.Call(0);
  f.Call(18);
  ASSERT_DEATH_IF_SUPPORTED(
      { f.Call(17); }, v8_flags.debug_code ? "abort: no reason" : "");
}

TEST_F(MacroAssemblerTest, ReverseBitsU64) {
  struct {
    uint64_t expected;
    uint64_t input;
  } values[] = {
      {0x0000000000000000, 0x0000000000000000},
      {0xffffffffffffffff, 0xffffffffffffffff},
      {0x8000000000000000, 0x0000000000000001},
      {0x0000000000000001, 0x8000000000000000},
      {0x800066aa22cc4488, 0x1122334455660001},
      {0x1122334455660001, 0x800066aa22cc4488},
      {0xffffffff00000000, 0x00000000ffffffff},
      {0x00000000ffffffff, 0xffffffff00000000},
      {0xff01020304050607, 0xe060a020c04080ff},
      {0xe060a020c04080ff, 0xff01020304050607},
  };
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);
  __ Push(r4, r5);
  __ ReverseBitsU64(r3, r3, r4, r5);
  __ Pop(r4, r5);
  __ Ret();
  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f =
      GeneratedCode<uint64_t, uint64_t>::FromBuffer(isolate(), buffer->start());
  for (unsigned int i = 0; i < (sizeof(values) / sizeof(values[0])); i++) {
    CHECK_EQ(values[i].expected, f.Call(values[i].input));
  }
}

TEST_F(MacroAssemblerTest, ReverseBitsU32) {
  struct {
    uint64_t expected;
    uint64_t input;
  } values[] = {
      {0x00000000, 0x00000000}, {0xffffffff, 0xffffffff},
      {0x00000001, 0x80000000}, {0x80000000, 0x00000001},
      {0x22334455, 0xaa22cc44}, {0xaa22cc44, 0x22334455},
  };
  auto buffer = AllocateAssemblerBuffer();
  MacroAssembler masm(isolate(), AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer->CreateView());
  __ set_root_array_available(false);
  __ set_abort_hard(true);
  __ Push(r4, r5);
  __ ReverseBitsU32(r3, r3, r4, r5);
  __ Pop(r4, r5);
  __ Ret();
  CodeDesc desc;
  masm.GetCode(isolate(), &desc);
  buffer->MakeExecutable();
  auto f =
      GeneratedCode<uint64_t, uint64_t>::FromBuffer(isolate(), buffer->start());
  for (unsigned int i = 0; i < (sizeof(values) / sizeof(values[0])); i++) {
    CHECK_EQ(values[i].expected, f.Call(values[i].input));
  }
}

#undef __

}  // namespace internal
}  // namespace v8
