// Copyright 2009 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_EXECUTION_SIMULATOR_H_
#define V8_EXECUTION_SIMULATOR_H_

#include "src/common/globals.h"
#include "src/objects/code.h"

#if !defined(USE_SIMULATOR)
#include "src/base/platform/platform.h"
#include "src/execution/isolate.h"
#include "src/utils/utils.h"
#endif

#if V8_TARGET_ARCH_IA32 || V8_TARGET_ARCH_X64
// No simulator for ia32 or x64.
#elif V8_TARGET_ARCH_ARM64
#include "src/execution/arm64/simulator-arm64.h"
#elif V8_TARGET_ARCH_ARM
#include "src/execution/arm/simulator-arm.h"
#elif V8_TARGET_ARCH_PPC || V8_TARGET_ARCH_PPC64
#include "src/execution/ppc/simulator-ppc.h"
#elif V8_TARGET_ARCH_MIPS64
#include "src/execution/mips64/simulator-mips64.h"
#elif V8_TARGET_ARCH_LOONG64
#include "src/execution/loong64/simulator-loong64.h"
#elif V8_TARGET_ARCH_S390
#include "src/execution/s390/simulator-s390.h"
#elif V8_TARGET_ARCH_RISCV32 || V8_TARGET_ARCH_RISCV64
#include "src/execution/riscv/simulator-riscv.h"
#else
#error Unsupported target architecture.
#endif

namespace v8 {
namespace internal {

#if defined(USE_SIMULATOR)
// Running with a simulator.

// The simulator has its own stack. Thus it has a different stack limit from
// the C-based native code.  The JS-based limit normally points near the end of
// the simulator stack.  When the C-based limit is exhausted we reflect that by
// lowering the JS-based limit as well, to make stack checks trigger.
class SimulatorStack : public v8::internal::AllStatic {
 public:
  static inline uintptr_t JsLimitFromCLimit(v8::internal::Isolate* isolate,
                                            uintptr_t c_limit) {
    return Simulator::current(isolate)->StackLimit(c_limit);
  }

  static inline base::Vector<uint8_t> GetCurrentStackView(
      v8::internal::Isolate* isolate) {
    return Simulator::current(isolate)->GetCurrentStackView();
  }

  // When running on the simulator, we should leave the C stack limits alone
  // when switching stacks for Wasm.
  static inline bool ShouldSwitchCStackForWasmStackSwitching() { return false; }

  // Returns the current stack address on the simulator stack frame.
  // The returned address is comparable with JS stack address.
  static inline uintptr_t RegisterJSStackComparableAddress(
      v8::internal::Isolate* isolate) {
    // The value of |kPlaceHolder| is actually not used.  It just occupies a
    // single word on the stack frame of the simulator.
    const uintptr_t kPlaceHolder = 0x4A535350u;  // "JSSP" in ASCII
    return Simulator::current(isolate)->PushAddress(kPlaceHolder);
  }

  static inline void UnregisterJSStackComparableAddress(
      v8::internal::Isolate* isolate) {
    Simulator::current(isolate)->PopAddress();
  }
};

#else  // defined(USE_SIMULATOR)
// Running without a simulator on a native platform.

// The stack limit beyond which we will throw stack overflow errors in
// generated code. Because generated code uses the C stack, we just use
// the C stack limit.
class SimulatorStack : public v8::internal::AllStatic {
 public:
  static inline uintptr_t JsLimitFromCLimit(v8::internal::Isolate* isolate,
                                            uintptr_t c_limit) {
    USE(isolate);
    return c_limit;
  }

  static inline base::Vector<uint8_t> GetCurrentStackView(
      v8::internal::Isolate* isolate) {
    uintptr_t limit = isolate->stack_guard()->real_jslimit();
    uintptr_t stack_start = base::Stack::GetStackStart();
    DCHECK_LE(limit, stack_start);
    size_t size = stack_start - limit;
    return base::VectorOf(reinterpret_cast<uint8_t*>(limit), size);
  }

  // When running on real hardware, we should also switch the C stack limit
  // when switching stacks for Wasm.
  static inline bool ShouldSwitchCStackForWasmStackSwitching() { return true; }

  // Returns the current stack address on the native stack frame.
  // The returned address is comparable with JS stack address.
  static inline uintptr_t RegisterJSStackComparableAddress(
      v8::internal::Isolate* isolate) {
    USE(isolate);
    return internal::GetCurrentStackPosition();
  }

  static inline void UnregisterJSStackComparableAddress(
      v8::internal::Isolate* isolate) {
    USE(isolate);
  }
};

#endif  // defined(USE_SIMULATOR)

// Use this class either as {GeneratedCode<ret, arg1, arg2>} or
// {GeneratedCode<ret(arg1, arg2)>} (see specialization below).
template <typename Return, typename... Args>
class GeneratedCode {
 public:
  using Signature = Return(Args...);

  static GeneratedCode FromAddress(Isolate* isolate, Address addr) {
    return GeneratedCode(isolate, reinterpret_cast<Signature*>(addr));
  }

  static GeneratedCode FromBuffer(Isolate* isolate, uint8_t* buffer) {
    return GeneratedCode(isolate, reinterpret_cast<Signature*>(buffer));
  }

  static GeneratedCode FromCode(Isolate* isolate, Tagged<Code> code) {
    return FromAddress(isolate, code->instruction_start());
  }

#ifdef USE_SIMULATOR
  // Defined in simulator-base.h.
  Return Call(Args... args) {
// Starboard is a platform abstraction interface that also include Windows
// platforms like UWP.
#if defined(V8_TARGET_OS_WIN) && !defined(V8_OS_WIN) && \
    !defined(V8_OS_STARBOARD) && !defined(V8_TARGET_ARCH_ARM)
    FATAL(
        "Generated code execution not possible during cross-compilation."
        "Also, generic C function calls are not implemented on 32-bit arm "
        "yet.");
#endif  // defined(V8_TARGET_OS_WIN) && !defined(V8_OS_WIN) &&
        // !defined(V8_OS_STARBOARD) && !defined(V8_TARGET_ARCH_ARM)
    return Simulator::current(isolate_)->template Call<Return>(
        reinterpret_cast<Address>(fn_ptr_), args...);
  }
#else

  DISABLE_CFI_ICALL Return Call(Args... args) {
    // When running without a simulator we call the entry directly.
// Starboard is a platform abstraction interface that also include Windows
// platforms like UWP.
#if defined(V8_TARGET_OS_WIN) && !defined(V8_OS_WIN) && \
    !defined(V8_OS_STARBOARD)
    FATAL("Generated code execution not possible during cross-compilation.");
#endif  // defined(V8_TARGET_OS_WIN) && !defined(V8_OS_WIN)
#if ABI_USES_FUNCTION_DESCRIPTORS
#if V8_OS_ZOS
    // z/OS ABI requires function descriptors (FD). Artificially create a pseudo
    // FD to ensure correct dispatch to generated code.
    void* function_desc[2] = {0, reinterpret_cast<void*>(fn_ptr_)};
    asm volatile(" stg 5,%0 " : "=m"(function_desc[0])::"r5");
    Signature* fn = reinterpret_cast<Signature*>(function_desc);
    return fn(args...);
#else
    // AIX ABI requires function descriptors (FD).  Artificially create a pseudo
    // FD to ensure correct dispatch to generated code.  The 'volatile'
    // declaration is required to avoid the compiler from not observing the
    // alias of the pseudo FD to the function pointer, and hence, optimizing the
    // pseudo FD declaration/initialization away.
    volatile Address function_desc[] = {reinterpret_cast<Address>(fn_ptr_), 0,
                                        0};
    Signature* fn = reinterpret_cast<Signature*>(function_desc);
    return fn(args...);
#endif  // V8_OS_ZOS
#else
    return fn_ptr_(args...);
#endif  // ABI_USES_FUNCTION_DESCRIPTORS
  }
#endif  // USE_SIMULATOR

 private:
  friend class GeneratedCode<Return(Args...)>;
  Isolate* isolate_;
  Signature* fn_ptr_;
  GeneratedCode(Isolate* isolate, Signature* fn_ptr)
      : isolate_(isolate), fn_ptr_(fn_ptr) {}
};

// Allow to use {GeneratedCode<ret(arg1, arg2)>} instead of
// {GeneratedCode<ret, arg1, arg2>}.
template <typename Return, typename... Args>
class GeneratedCode<Return(Args...)> : public GeneratedCode<Return, Args...> {
 public:
  // Automatically convert from {GeneratedCode<ret, arg1, arg2>} to
  // {GeneratedCode<ret(arg1, arg2)>}.
  GeneratedCode(GeneratedCode<Return, Args...> other)
      : GeneratedCode<Return, Args...>(other.isolate_, other.fn_ptr_) {}
};

}  // namespace internal
}  // namespace v8

#endif  // V8_EXECUTION_SIMULATOR_H_
