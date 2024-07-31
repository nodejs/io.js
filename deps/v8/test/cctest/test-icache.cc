// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/execution/simulator.h"
#include "src/handles/handles-inl.h"
#include "test/cctest/cctest.h"
#include "test/common/assembler-tester.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/code-space-access.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {
namespace test_icache {

using F0 = int(int);

#define __ masm.

static constexpr int kNumInstr = 100;
static constexpr int kNumIterations = 5;
static constexpr int kBufferSize = 8 * KB;

static void FloodWithInc(Isolate* isolate, TestingAssemblerBuffer* buffer) {
  MacroAssembler masm(isolate, CodeObjectRequired::kYes, buffer->CreateView());
#if V8_TARGET_ARCH_IA32
  __ mov(eax, Operand(esp, kSystemPointerSize));
  for (int i = 0; i < kNumInstr; ++i) {
    __ add(eax, Immediate(1));
  }
#elif V8_TARGET_ARCH_X64
  __ movl(rax, kCArgRegs[0]);
  for (int i = 0; i < kNumInstr; ++i) {
    __ addl(rax, Immediate(1));
  }
#elif V8_TARGET_ARCH_ARM64
  __ CodeEntry();
  for (int i = 0; i < kNumInstr; ++i) {
    __ Add(x0, x0, Operand(1));
  }
#elif V8_TARGET_ARCH_ARM
  for (int i = 0; i < kNumInstr; ++i) {
    __ add(r0, r0, Operand(1));
  }
#elif V8_TARGET_ARCH_MIPS
  __ mov(v0, a0);
  for (int i = 0; i < kNumInstr; ++i) {
    __ Addu(v0, v0, Operand(1));
  }
#elif V8_TARGET_ARCH_MIPS64
  __ mov(v0, a0);
  for (int i = 0; i < kNumInstr; ++i) {
    __ Addu(v0, v0, Operand(1));
  }
#elif V8_TARGET_ARCH_LOONG64
  for (int i = 0; i < kNumInstr; ++i) {
    __ Add_w(a0, a0, Operand(1));
  }
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
  for (int i = 0; i < kNumInstr; ++i) {
    __ addi(r3, r3, Operand(1));
  }
#elif V8_TARGET_ARCH_S390
  for (int i = 0; i < kNumInstr; ++i) {
    __ agfi(r2, Operand(1));
  }
#elif V8_TARGET_ARCH_RISCV32
  for (int i = 0; i < kNumInstr; ++i) {
    __ Add32(a0, a0, Operand(1));
  }
#elif V8_TARGET_ARCH_RISCV64
  for (int i = 0; i < kNumInstr; ++i) {
    __ Add32(a0, a0, Operand(1));
  }
#else
#error Unsupported architecture
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
}

static void FloodWithNop(Isolate* isolate, TestingAssemblerBuffer* buffer) {
  MacroAssembler masm(isolate, CodeObjectRequired::kYes, buffer->CreateView());
#if V8_TARGET_ARCH_IA32
  __ mov(eax, Operand(esp, kSystemPointerSize));
#elif V8_TARGET_ARCH_X64
  __ movl(rax, kCArgRegs[0]);
#elif V8_TARGET_ARCH_ARM64
  __ CodeEntry();
#elif V8_TARGET_ARCH_MIPS
  __ mov(v0, a0);
#elif V8_TARGET_ARCH_MIPS64
  __ mov(v0, a0);
#endif
  for (int i = 0; i < kNumInstr; ++i) {
    __ nop();
  }
  __ Ret();
  CodeDesc desc;
  masm.GetCode(isolate, &desc);
}

// Order of operation for this test case:
//   exec -> perm(RW) -> patch -> flush -> perm(RX) -> exec
TEST(TestFlushICacheOfWritable) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  for (int i = 0; i < kNumIterations; ++i) {
    auto buffer = AllocateAssemblerBuffer(kBufferSize);

    // Allow calling the function from C++.
    auto f = GeneratedCode<F0>::FromBuffer(isolate, buffer->start());

    {
      AssemblerBufferWriteScope rw_buffer_scope(*buffer);
      FloodWithInc(isolate, buffer.get());
      FlushInstructionCache(buffer->start(), buffer->size());
    }
    CHECK_EQ(23 + kNumInstr, f.Call(23));  // Call into generated code.

    {
      AssemblerBufferWriteScope rw_buffer_scope(*buffer);
      FloodWithNop(isolate, buffer.get());
      FlushInstructionCache(buffer->start(), buffer->size());
    }
    CHECK_EQ(23, f.Call(23));  // Call into generated code.
  }
}

#if V8_TARGET_ARCH_ARM || V8_TARGET_ARCH_ARM64
// Note that this order of operations is not supported on ARM32/64 because on
// some older ARM32/64 kernels there is a bug which causes an access error on
// cache flush instructions to trigger access error on non-writable memory.
// See https://bugs.chromium.org/p/v8/issues/detail?id=8157
//
// Also note that this requires {kBufferSize == 8 * KB} to reproduce.
//
// The order of operations in V8 is akin to {TestFlushICacheOfWritable} above.
// It is hence OK to disable the below test on some architectures. Only the
// above test case should remain enabled on all architectures.
#define CONDITIONAL_TEST DISABLED_TEST
#else
#define CONDITIONAL_TEST TEST
#endif

// Order of operation for this test case:
//   exec -> perm(RW) -> patch -> perm(RX) -> flush -> exec
CONDITIONAL_TEST(TestFlushICacheOfExecutable) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  for (int i = 0; i < kNumIterations; ++i) {
    auto buffer = AllocateAssemblerBuffer(kBufferSize);

    // Allow calling the function from C++.
    auto f = GeneratedCode<F0>::FromBuffer(isolate, buffer->start());

    CHECK(SetPermissions(GetPlatformPageAllocator(), buffer->start(),
                         buffer->size(), v8::PageAllocator::kReadWrite));
    FloodWithInc(isolate, buffer.get());
    CHECK(SetPermissions(GetPlatformPageAllocator(), buffer->start(),
                         buffer->size(), v8::PageAllocator::kReadExecute));
    FlushInstructionCache(buffer->start(), buffer->size());
    CHECK_EQ(23 + kNumInstr, f.Call(23));  // Call into generated code.
    CHECK(SetPermissions(GetPlatformPageAllocator(), buffer->start(),
                         buffer->size(), v8::PageAllocator::kReadWrite));
    FloodWithNop(isolate, buffer.get());
    CHECK(SetPermissions(GetPlatformPageAllocator(), buffer->start(),
                         buffer->size(), v8::PageAllocator::kReadExecute));
    FlushInstructionCache(buffer->start(), buffer->size());
    CHECK_EQ(23, f.Call(23));  // Call into generated code.
  }
}

#undef CONDITIONAL_TEST

#if V8_ENABLE_WEBASSEMBLY
// Order of operation for this test case:
//   perm(RWX) -> exec -> patch -> flush -> exec
TEST(TestFlushICacheOfWritableAndExecutable) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope handles(isolate);

  for (int i = 0; i < kNumIterations; ++i) {
    auto buffer = AllocateAssemblerBuffer(kBufferSize, nullptr,
                                          JitPermission::kMapAsJittable);

    // Allow calling the function from C++.
    auto f = GeneratedCode<F0>::FromBuffer(isolate, buffer->start());

    buffer->MakeWritableAndExecutable();

    {
      RwxMemoryWriteScopeForTesting rw_scope;
      FloodWithInc(isolate, buffer.get());
      FlushInstructionCache(buffer->start(), buffer->size());
    }
    CHECK_EQ(23 + kNumInstr, f.Call(23));  // Call into generated code.
    {
      RwxMemoryWriteScopeForTesting rw_scope;
      FloodWithNop(isolate, buffer.get());
      FlushInstructionCache(buffer->start(), buffer->size());
    }
    CHECK_EQ(23, f.Call(23));  // Call into generated code.
  }
}
#endif  // V8_ENABLE_WEBASSEMBLY

#undef __

}  // namespace test_icache
}  // namespace internal
}  // namespace v8
