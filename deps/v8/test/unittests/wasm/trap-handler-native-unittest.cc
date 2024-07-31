// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/v8config.h"

#if V8_OS_LINUX || V8_OS_FREEBSD
#include <signal.h>
#include <ucontext.h>
#elif V8_OS_DARWIN
#include <signal.h>
#include <sys/ucontext.h>
#elif V8_OS_WIN
#include <windows.h>
#endif

#include "testing/gtest/include/gtest/gtest.h"

#if V8_OS_POSIX
#include "include/v8-wasm-trap-handler-posix.h"
#elif V8_OS_WIN
#include "include/v8-wasm-trap-handler-win.h"
#endif
#include "src/base/page-allocator.h"
#include "src/base/vector.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/execution/simulator.h"
#include "src/objects/backing-store.h"
#include "src/trap-handler/trap-handler.h"
#include "src/utils/allocation.h"
#include "src/wasm/wasm-engine.h"
#include "test/common/assembler-tester.h"
#include "test/unittests/test-utils.h"

#if V8_TRAP_HANDLER_SUPPORTED

#if V8_HOST_ARCH_ARM64 && (!V8_OS_LINUX && !V8_OS_DARWIN)
#error Unsupported platform
#endif

namespace v8 {
namespace internal {
namespace wasm {

namespace {
#if V8_HOST_ARCH_X64
constexpr Register scratch = r10;
#endif
bool g_test_handler_executed = false;
#if V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD
struct sigaction g_old_segv_action;
struct sigaction g_old_other_action;  // FPE or TRAP, depending on x64 or arm64.
struct sigaction g_old_bus_action;    // We get SIGBUS on Mac sometimes.
#elif V8_OS_WIN
void* g_registered_handler = nullptr;
#endif

// The recovery address allows us to recover from an intentional crash.
Address g_recovery_address;
// Flag to indicate if the test handler should call the trap handler as a first
// chance handler.
bool g_use_as_first_chance_handler = false;
}  // namespace

#define __ masm.

enum TrapHandlerStyle : int {
  // The test uses the default trap handler of V8.
  kDefault = 0,
  // The test installs the trap handler callback in its own test handler.
  kCallback = 1
};

std::string PrintTrapHandlerTestParam(
    ::testing::TestParamInfo<TrapHandlerStyle> info) {
  switch (info.param) {
    case kDefault:
      return "DefaultTrapHandler";
    case kCallback:
      return "Callback";
  }
  UNREACHABLE();
}

class TrapHandlerTest : public TestWithIsolate,
                        public ::testing::WithParamInterface<TrapHandlerStyle> {
 protected:
  void SetUp() override {
    InstallFallbackHandler();
    SetupTrapHandler(GetParam());
    backing_store_ = BackingStore::AllocateWasmMemory(
        i_isolate(), 1, 1, WasmMemoryFlag::kWasmMemory32,
        SharedFlag::kNotShared);
    CHECK(backing_store_);
    EXPECT_TRUE(backing_store_->has_guard_regions());
    // The allocated backing store ends with a guard page.
    crash_address_ = reinterpret_cast<Address>(backing_store_->buffer_start()) +
                     backing_store_->byte_length() + 32;
    // Allocate a buffer for the generated code.
    buffer_ = AllocateAssemblerBuffer(AssemblerBase::kDefaultBufferSize,
                                      GetRandomMmapAddr());

    InitRecoveryCode();
  }

  void InstallFallbackHandler() {
#if V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD
    // Set up a signal handler to recover from the expected crash.
    struct sigaction action;
    action.sa_sigaction = SignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    // SIGSEGV happens for wasm oob memory accesses on Linux.
    EXPECT_EQ(0, sigaction(SIGSEGV, &action, &g_old_segv_action));
    // SIGBUS happens for wasm oob memory accesses on macOS.
    EXPECT_EQ(0, sigaction(SIGBUS, &action, &g_old_bus_action));
#if V8_HOST_ARCH_X64
    // SIGFPE to simulate crashes which are not handled by the trap handler.
    EXPECT_EQ(0, sigaction(SIGFPE, &action, &g_old_other_action));
#elif V8_HOST_ARCH_ARM64
    // SIGTRAP to simulate crashes which are not handled by the trap handler.
    EXPECT_EQ(0, sigaction(SIGTRAP, &action, &g_old_other_action));
#elif V8_HOST_ARCH_LOONG64
    // SIGTRAP to simulate crashes which are not handled by the trap handler.
    EXPECT_EQ(0, sigaction(SIGTRAP, &action, &g_old_other_action));
#elif V8_HOST_ARCH_RISCV64
    // SIGTRAP to simulate crashes which are not handled by the trap handler.
    EXPECT_EQ(0, sigaction(SIGTRAP, &action, &g_old_other_action));
#else
#error Unsupported platform
#endif
#elif V8_OS_WIN
    g_registered_handler =
        AddVectoredExceptionHandler(/*first=*/0, TestHandler);
#endif
  }

  void TearDown() override {
    // We should always have left wasm code.
    EXPECT_TRUE(!GetThreadInWasmFlag());
    buffer_.reset();
    recovery_buffer_.reset();
    backing_store_.reset();

    // Clean up the trap handler
    trap_handler::RemoveTrapHandler();
    if (!g_test_handler_executed) {
#if V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD
      // The test handler cleans up the signal handler setup in the test. If the
      // test handler was not called, we have to do the cleanup ourselves.
      EXPECT_EQ(0, sigaction(SIGSEGV, &g_old_segv_action, nullptr));
      EXPECT_EQ(0, sigaction(SIGBUS, &g_old_bus_action, nullptr));
#if V8_HOST_ARCH_X64
      EXPECT_EQ(0, sigaction(SIGFPE, &g_old_other_action, nullptr));
#elif V8_HOST_ARCH_ARM64
      EXPECT_EQ(0, sigaction(SIGTRAP, &g_old_other_action, nullptr));
#elif V8_HOST_ARCH_LOONG64
      EXPECT_EQ(0, sigaction(SIGTRAP, &g_old_other_action, nullptr));
#elif V8_HOST_ARCH_RISCV64
      EXPECT_EQ(0, sigaction(SIGTRAP, &g_old_other_action, nullptr));
#else
#error Unsupported platform
#endif
#elif V8_OS_WIN
      RemoveVectoredExceptionHandler(g_registered_handler);
      g_registered_handler = nullptr;
#endif
    }
  }

  void InitRecoveryCode() {
    // Create a code snippet where we can jump to to recover from a signal or
    // exception. The code snippet only consists of a return statement.
    recovery_buffer_ = AllocateAssemblerBuffer(
        AssemblerBase::kDefaultBufferSize, GetRandomMmapAddr());

    MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                        recovery_buffer_->CreateView());
    int recovery_offset = __ pc_offset();
#if V8_HOST_ARCH_X64
    __ Pop(scratch);
#endif
    __ Ret();
    CodeDesc desc;
    masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);
    recovery_buffer_->MakeExecutable();
    g_recovery_address =
        reinterpret_cast<Address>(desc.buffer + recovery_offset);
  }

#if V8_OS_LINUX || V8_OS_DARWIN || V8_OS_FREEBSD
  static void SignalHandler(int signal, siginfo_t* info, void* context) {
    if (g_use_as_first_chance_handler) {
      if (v8::TryHandleWebAssemblyTrapPosix(signal, info, context)) {
        return;
      }
    }

    // Reset the signal handler, to avoid that this signal handler is called
    // repeatedly.
    sigaction(SIGSEGV, &g_old_segv_action, nullptr);
#if V8_HOST_ARCH_X64
    sigaction(SIGFPE, &g_old_other_action, nullptr);
#elif V8_HOST_ARCH_ARM64
    sigaction(SIGTRAP, &g_old_other_action, nullptr);
#elif V8_HOST_ARCH_LOONG64
    sigaction(SIGTRAP, &g_old_other_action, nullptr);
#elif V8_HOST_ARCH_RISCV64
    sigaction(SIGTRAP, &g_old_other_action, nullptr);
#else
#error Unsupported platform
#endif
    sigaction(SIGBUS, &g_old_bus_action, nullptr);

    g_test_handler_executed = true;
    // Set the $rip to the recovery code.
    ucontext_t* uc = reinterpret_cast<ucontext_t*>(context);
#if V8_OS_DARWIN && V8_HOST_ARCH_ARM64
    uc->uc_mcontext->__ss.__pc = g_recovery_address;
#elif V8_OS_DARWIN && V8_HOST_ARCH_X64
    uc->uc_mcontext->__ss.__rip = g_recovery_address;
#elif V8_OS_LINUX && V8_HOST_ARCH_ARM64
    uc->uc_mcontext.pc = g_recovery_address;
#elif V8_OS_LINUX && V8_HOST_ARCH_LOONG64
    uc->uc_mcontext.__pc = g_recovery_address;
#elif V8_OS_LINUX && V8_HOST_ARCH_RISCV64
    uc->uc_mcontext.__gregs[REG_PC] = g_recovery_address;
#elif V8_OS_LINUX && V8_HOST_ARCH_X64
    uc->uc_mcontext.gregs[REG_RIP] = g_recovery_address;
#elif V8_OS_FREEBSD
    uc->uc_mcontext.mc_rip = g_recovery_address;
#else
#error Unsupported platform
#endif
  }
#endif

#if V8_OS_WIN
  static LONG WINAPI TestHandler(EXCEPTION_POINTERS* exception) {
    if (g_use_as_first_chance_handler) {
      if (v8::TryHandleWebAssemblyTrapWindows(exception)) {
        return EXCEPTION_CONTINUE_EXECUTION;
      }
    }
    RemoveVectoredExceptionHandler(g_registered_handler);
    g_registered_handler = nullptr;
    g_test_handler_executed = true;
    exception->ContextRecord->Rip = g_recovery_address;
    return EXCEPTION_CONTINUE_EXECUTION;
  }
#endif

  void SetupTrapHandler(TrapHandlerStyle style) {
    bool use_default_handler = style == kDefault;
    g_use_as_first_chance_handler = !use_default_handler;
    CHECK(v8::V8::EnableWebAssemblyTrapHandler(use_default_handler));
  }

 public:
  void GenerateSetThreadInWasmFlagCode(MacroAssembler* masm) {
#if V8_HOST_ARCH_X64
    masm->Move(scratch,
               i_isolate()->thread_local_top()->thread_in_wasm_flag_address_,
               RelocInfo::NO_INFO);
    masm->movl(MemOperand(scratch, 0), Immediate(1));
#elif V8_HOST_ARCH_ARM64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.AcquireX();
    masm->Mov(addr,
              i_isolate()->thread_local_top()->thread_in_wasm_flag_address_);
    Register one = temps.AcquireX();
    masm->Mov(one, 1);
    masm->Str(one, MemOperand(addr));
#elif V8_HOST_ARCH_LOONG64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.Acquire();
    masm->li(
        addr,
        static_cast<int64_t>(
            i_isolate()->thread_local_top()->thread_in_wasm_flag_address_));
    Register one = temps.Acquire();
    masm->li(one, 1);
    masm->St_d(one, MemOperand(addr, 0));
#elif V8_HOST_ARCH_RISCV64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.Acquire();
    masm->li(
        addr,
        static_cast<int64_t>(
            i_isolate()->thread_local_top()->thread_in_wasm_flag_address_));
    Register one = temps.Acquire();
    masm->li(one, 1);
    masm->StoreWord(one, MemOperand(addr, 0));
#else
#error Unsupported platform
#endif
  }

  void GenerateResetThreadInWasmFlagCode(MacroAssembler* masm) {
#if V8_HOST_ARCH_X64
    masm->Move(scratch,
               i_isolate()->thread_local_top()->thread_in_wasm_flag_address_,
               RelocInfo::NO_INFO);
    masm->movl(MemOperand(scratch, 0), Immediate(0));
#elif V8_HOST_ARCH_ARM64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.AcquireX();
    masm->Mov(addr,
              i_isolate()->thread_local_top()->thread_in_wasm_flag_address_);
    masm->Str(xzr, MemOperand(addr));
#elif V8_HOST_ARCH_LOONG64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.Acquire();
    masm->li(
        addr,
        static_cast<int64_t>(
            i_isolate()->thread_local_top()->thread_in_wasm_flag_address_));
    masm->St_d(zero_reg, MemOperand(addr, 0));
#elif V8_HOST_ARCH_RISCV64
    UseScratchRegisterScope temps(masm);
    Register addr = temps.Acquire();
    masm->li(
        addr,
        static_cast<int64_t>(
            i_isolate()->thread_local_top()->thread_in_wasm_flag_address_));
    masm->StoreWord(zero_reg, MemOperand(addr, 0));
#else
#error Unsupported platform
#endif
  }

  bool GetThreadInWasmFlag() {
    return *reinterpret_cast<int*>(
        trap_handler::GetThreadInWasmThreadLocalAddress());
  }

  // Execute the code in buffer.
  void ExecuteBuffer() {
    buffer_->MakeExecutable();
    GeneratedCode<void>::FromAddress(
        i_isolate(), reinterpret_cast<Address>(buffer_->start()))
        .Call();
    EXPECT_FALSE(g_test_handler_executed);
  }

  // Execute the code in buffer. We expect a crash which we recover from in the
  // test handler.
  void ExecuteExpectCrash(TestingAssemblerBuffer* buffer,
                          bool check_wasm_flag = true) {
    EXPECT_FALSE(g_test_handler_executed);
    buffer->MakeExecutable();
    GeneratedCode<void>::FromAddress(i_isolate(),
                                     reinterpret_cast<Address>(buffer->start()))
        .Call();
    EXPECT_TRUE(g_test_handler_executed);
    g_test_handler_executed = false;
    if (check_wasm_flag) {
      EXPECT_FALSE(GetThreadInWasmFlag());
    }
  }

  bool test_handler_executed() { return g_test_handler_executed; }

  // The backing store used for testing the trap handler.
  std::unique_ptr<BackingStore> backing_store_;

  // Address within the guard region of the wasm memory. Accessing this memory
  // address causes a signal or exception.
  Address crash_address_;

  // Buffer for generated code.
  std::unique_ptr<TestingAssemblerBuffer> buffer_;
  // Buffer for the code for the landing pad of the test handler.
  std::unique_ptr<TestingAssemblerBuffer> recovery_buffer_;
};

// TODO(almuthanna): These tests were skipped because they cause a crash when
// they are ran on Fuchsia. This issue should be solved later on
// Ticket: https://crbug.com/1028617
#if !defined(V8_TARGET_OS_FUCHSIA)
TEST_P(TrapHandlerTest, TestTrapHandlerRecovery) {
  // Test that the wasm trap handler can recover a memory access violation in
  // wasm code (we fake the wasm code and the access violation).
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  GenerateSetThreadInWasmFlagCode(&masm);
  __ Move(scratch, crash_address_, RelocInfo::NO_INFO);
  uint32_t crash_offset = __ pc_offset();
  __ testl(MemOperand(scratch, 0), Immediate(1));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.AcquireX();
  __ Mov(scratch, crash_address_);
  uint32_t crash_offset = __ pc_offset();
  __ Ldr(scratch, MemOperand(scratch));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_LOONG64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ Ld_d(scratch, MemOperand(scratch, 0));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_RISCV64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ LoadWord(scratch, MemOperand(scratch, 0));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{crash_offset};
  trap_handler::RegisterHandlerData(reinterpret_cast<Address>(desc.buffer),
                                    desc.instr_size, 1, &protected_instruction);

  uintptr_t landing_pad =
      reinterpret_cast<uintptr_t>(buffer_->start()) + recovery_offset;
  trap_handler::SetLandingPad(landing_pad);
  ExecuteBuffer();
  trap_handler::SetLandingPad(0);
}

TEST_P(TrapHandlerTest, TestReleaseHandlerData) {
  // Test that after we release handler data in the trap handler, it cannot
  // recover from the specific memory access violation anymore.
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  GenerateSetThreadInWasmFlagCode(&masm);
  __ Move(scratch, crash_address_, RelocInfo::NO_INFO);
  uint32_t crash_offset = __ pc_offset();
  __ testl(MemOperand(scratch, 0), Immediate(1));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.AcquireX();
  __ Mov(scratch, crash_address_);
  uint32_t crash_offset = __ pc_offset();
  __ Ldr(scratch, MemOperand(scratch));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_LOONG64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ Ld_d(scratch, MemOperand(scratch, 0));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_RISCV64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ LoadWord(scratch, MemOperand(scratch, 0));
  uint32_t recovery_offset = __ pc_offset();
  GenerateResetThreadInWasmFlagCode(&masm);
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{crash_offset};
  int handler_id = trap_handler::RegisterHandlerData(
      reinterpret_cast<Address>(desc.buffer), desc.instr_size, 1,
      &protected_instruction);

  uintptr_t landing_pad =
      reinterpret_cast<uintptr_t>(buffer_->start()) + recovery_offset;
  trap_handler::SetLandingPad(landing_pad);
  ExecuteBuffer();
  // Deregister from the trap handler. The trap handler should not do the
  // recovery now.
  trap_handler::ReleaseHandlerData(handler_id);

  ExecuteExpectCrash(buffer_.get());
  trap_handler::SetLandingPad(0);
}

TEST_P(TrapHandlerTest, TestNoThreadInWasmFlag) {
  // That that if the thread_in_wasm flag is not set, the trap handler does not
  // get active.
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  __ Move(scratch, crash_address_, RelocInfo::NO_INFO);
  uint32_t crash_offset = __ pc_offset();
  __ testl(MemOperand(scratch, 0), Immediate(1));
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.AcquireX();
  __ Mov(scratch, crash_address_);
  uint32_t crash_offset = __ pc_offset();
  __ Ldr(scratch, MemOperand(scratch));
#elif V8_HOST_ARCH_LOONG64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ Ld_d(scratch, MemOperand(scratch, 0));
#elif V8_HOST_ARCH_RISCV64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ LoadWord(scratch, MemOperand(scratch, 0));
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{crash_offset};
  trap_handler::RegisterHandlerData(reinterpret_cast<Address>(desc.buffer),
                                    desc.instr_size, 1, &protected_instruction);

  ExecuteExpectCrash(buffer_.get());
}

TEST_P(TrapHandlerTest, TestCrashInWasmNoProtectedInstruction) {
  // Test that if the crash in wasm happened at an instruction which is not
  // protected, then the trap handler does not handle it.
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  GenerateSetThreadInWasmFlagCode(&masm);
  uint32_t no_crash_offset = __ pc_offset();
  __ Move(scratch, crash_address_, RelocInfo::NO_INFO);
  __ testl(MemOperand(scratch, 0), Immediate(1));
  GenerateResetThreadInWasmFlagCode(&masm);
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.AcquireX();
  uint32_t no_crash_offset = __ pc_offset();
  __ Mov(scratch, crash_address_);
  __ Ldr(scratch, MemOperand(scratch));
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_LOONG64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  uint32_t no_crash_offset = __ pc_offset();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  __ Ld_d(scratch, MemOperand(scratch, 0));
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_RISCV64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  uint32_t no_crash_offset = __ pc_offset();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  __ LoadWord(scratch, MemOperand(scratch, 0));
  GenerateResetThreadInWasmFlagCode(&masm);
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{no_crash_offset};
  trap_handler::RegisterHandlerData(reinterpret_cast<Address>(desc.buffer),
                                    desc.instr_size, 1, &protected_instruction);

  ExecuteExpectCrash(buffer_.get());
}

TEST_P(TrapHandlerTest, TestCrashInWasmWrongCrashType) {
  // Test that if the crash reason is not a memory access violation, then the
  // wasm trap handler does not handle it.
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  GenerateSetThreadInWasmFlagCode(&masm);
  __ xorq(scratch, scratch);
  uint32_t crash_offset = __ pc_offset();
  __ divq(scratch);
  GenerateResetThreadInWasmFlagCode(&masm);
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  uint32_t crash_offset = __ pc_offset();
  __ Trap();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_LOONG64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  uint32_t crash_offset = __ pc_offset();
  __ Trap();
  GenerateResetThreadInWasmFlagCode(&masm);
#elif V8_HOST_ARCH_RISCV64
  GenerateSetThreadInWasmFlagCode(&masm);
  UseScratchRegisterScope temps(&masm);
  uint32_t crash_offset = __ pc_offset();
  __ Trap();
  GenerateResetThreadInWasmFlagCode(&masm);
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{crash_offset};
  trap_handler::RegisterHandlerData(reinterpret_cast<Address>(desc.buffer),
                                    desc.instr_size, 1, &protected_instruction);

#if V8_OS_POSIX
  // On Posix, the V8 default trap handler does not register for SIGFPE,
  // therefore the thread-in-wasm flag is never reset in this test. We
  // therefore do not check the value of this flag.
  bool check_wasm_flag = GetParam() != kDefault;
#elif V8_OS_WIN
  // On Windows, the trap handler returns immediately if not an exception of
  // interest.
  bool check_wasm_flag = false;
#else
  bool check_wasm_flag = true;
#endif
  ExecuteExpectCrash(buffer_.get(), check_wasm_flag);
  if (!check_wasm_flag) {
    // Reset the thread-in-wasm flag because it was probably not reset in the
    // trap handler.
    *trap_handler::GetThreadInWasmThreadLocalAddress() = 0;
  }
}
#endif

class CodeRunner : public v8::base::Thread {
 public:
  CodeRunner(TrapHandlerTest* test, TestingAssemblerBuffer* buffer)
      : Thread(Options("CodeRunner")), test_(test), buffer_(buffer) {}

  void Run() override { test_->ExecuteExpectCrash(buffer_); }

 private:
  TrapHandlerTest* test_;
  TestingAssemblerBuffer* buffer_;
};

// TODO(almuthanna): This test was skipped because it causes a crash when it is
// ran on Fuchsia. This issue should be solved later on
// Ticket: https://crbug.com/1028617
#if !defined(V8_TARGET_OS_FUCHSIA)
TEST_P(TrapHandlerTest, TestCrashInOtherThread) {
  // Test setup:
  // The current thread enters wasm land (sets the thread_in_wasm flag)
  // A second thread crashes at a protected instruction without having the flag
  // set.
  MacroAssembler masm(nullptr, AssemblerOptions{}, CodeObjectRequired::kNo,
                      buffer_->CreateView());
#if V8_HOST_ARCH_X64
  __ Push(scratch);
  __ Move(scratch, crash_address_, RelocInfo::NO_INFO);
  uint32_t crash_offset = __ pc_offset();
  __ testl(MemOperand(scratch, 0), Immediate(1));
  __ Pop(scratch);
#elif V8_HOST_ARCH_ARM64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.AcquireX();
  __ Mov(scratch, crash_address_);
  uint32_t crash_offset = __ pc_offset();
  __ Ldr(scratch, MemOperand(scratch));
#elif V8_HOST_ARCH_LOONG64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ Ld_d(scratch, MemOperand(scratch, 0));
#elif V8_HOST_ARCH_RISCV64
  UseScratchRegisterScope temps(&masm);
  Register scratch = temps.Acquire();
  __ li(scratch, static_cast<int64_t>(crash_address_));
  uint32_t crash_offset = __ pc_offset();
  __ LoadWord(scratch, MemOperand(scratch, 0));
#else
#error Unsupported platform
#endif
  __ Ret();
  CodeDesc desc;
  masm.GetCode(static_cast<LocalIsolate*>(nullptr), &desc);

  trap_handler::ProtectedInstructionData protected_instruction{crash_offset};
  trap_handler::RegisterHandlerData(reinterpret_cast<Address>(desc.buffer),
                                    desc.instr_size, 1, &protected_instruction);

  CodeRunner runner(this, buffer_.get());
  EXPECT_FALSE(GetThreadInWasmFlag());
  // Set the thread-in-wasm flag manually in this thread.
  *trap_handler::GetThreadInWasmThreadLocalAddress() = 1;
  EXPECT_TRUE(runner.Start());
  runner.Join();
  EXPECT_TRUE(GetThreadInWasmFlag());
  // Reset the thread-in-wasm flag.
  *trap_handler::GetThreadInWasmThreadLocalAddress() = 0;
}
#endif

#if !V8_OS_FUCHSIA
INSTANTIATE_TEST_SUITE_P(Traps, TrapHandlerTest,
                         ::testing::Values(kDefault, kCallback),
                         PrintTrapHandlerTestParam);
#endif  // !V8_OS_FUCHSIA

#undef __
}  // namespace wasm
}  // namespace internal
}  // namespace v8

#endif
