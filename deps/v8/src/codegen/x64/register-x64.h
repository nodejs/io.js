// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_X64_REGISTER_X64_H_
#define V8_CODEGEN_X64_REGISTER_X64_H_

#include "src/codegen/register-base.h"

namespace v8 {
namespace internal {

#define GENERAL_REGISTERS(V) \
  V(rax)                     \
  V(rcx)                     \
  V(rdx)                     \
  V(rbx)                     \
  V(rsp)                     \
  V(rbp)                     \
  V(rsi)                     \
  V(rdi)                     \
  V(r8)                      \
  V(r9)                      \
  V(r10)                     \
  V(r11)                     \
  V(r12)                     \
  V(r13)                     \
  V(r14)                     \
  V(r15)

#define ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(V) \
  V(rax)                                        \
  V(rbx)                                        \
  V(rdx)                                        \
  V(rcx)                                        \
  V(rsi)                                        \
  V(rdi)                                        \
  V(r8)                                         \
  V(r9)                                         \
  V(r11)                                        \
  V(r12)                                        \
  V(r15)

#ifdef V8_COMPRESS_POINTERS
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V)
#else
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V) V(r14)
#endif

#define ALLOCATABLE_GENERAL_REGISTERS(V)  \
  ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(V) \
  MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V)

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class Register : public RegisterBase<Register, kRegAfterLast> {
 public:
  constexpr bool is_byte_register() const { return code() <= 3; }
  // Return the high bit of the register code as a 0 or 1.  Used often
  // when constructing the REX prefix byte.
  constexpr int high_bit() const { return code() >> 3; }
  // Return the 3 low bits of the register code.  Used when encoding registers
  // in modR/M, SIB, and opcode bytes.
  constexpr int low_bits() const { return code() & 0x7; }

 private:
  friend class RegisterBase<Register, kRegAfterLast>;
  explicit constexpr Register(int code) : RegisterBase(code) {}
};

// Register that store tagged value. Tagged value is in compressed form when
// pointer compression is enabled.
class TaggedRegister {
 public:
  explicit TaggedRegister(Register reg) : reg_(reg) {}
  Register reg() { return reg_; }

 private:
  Register reg_;
};

ASSERT_TRIVIALLY_COPYABLE(Register);
static_assert(sizeof(Register) <= sizeof(int),
              "Register can efficiently be passed by value");

// Assign |source| value to |no_reg| and return the |source|'s previous value.
inline Register ReassignRegister(Register& source) {
  Register result = source;
  source = Register::no_reg();
  return result;
}

#define DECLARE_REGISTER(R) \
  constexpr Register R = Register::from_code(kRegCode_##R);
GENERAL_REGISTERS(DECLARE_REGISTER)
#undef DECLARE_REGISTER
constexpr Register no_reg = Register::no_reg();

constexpr int kNumRegs = 16;

#ifdef V8_TARGET_OS_WIN
// Windows calling convention
constexpr Register kCArgRegs[] = {rcx, rdx, r8, r9};

// The Windows 64 ABI always reserves spill slots on the stack for the four
// register arguments even if the function takes fewer than four arguments.
// These stack slots are sometimes called 'home space', sometimes 'shadow
// store' in Microsoft documentation, see
// https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention.
constexpr int kWindowsHomeStackSlots = 4;
#else
// AMD64 calling convention
constexpr Register kCArgRegs[] = {rdi, rsi, rdx, rcx, r8, r9};
#endif  // V8_TARGET_OS_WIN

constexpr int kRegisterPassedArguments = arraysize(kCArgRegs);

#define DOUBLE_REGISTERS(V) \
  V(xmm0)                   \
  V(xmm1)                   \
  V(xmm2)                   \
  V(xmm3)                   \
  V(xmm4)                   \
  V(xmm5)                   \
  V(xmm6)                   \
  V(xmm7)                   \
  V(xmm8)                   \
  V(xmm9)                   \
  V(xmm10)                  \
  V(xmm11)                  \
  V(xmm12)                  \
  V(xmm13)                  \
  V(xmm14)                  \
  V(xmm15)

#define FLOAT_REGISTERS DOUBLE_REGISTERS
#define SIMD128_REGISTERS DOUBLE_REGISTERS

#define ALLOCATABLE_DOUBLE_REGISTERS(V) \
  V(xmm0)                               \
  V(xmm1)                               \
  V(xmm2)                               \
  V(xmm3)                               \
  V(xmm4)                               \
  V(xmm5)                               \
  V(xmm6)                               \
  V(xmm7)                               \
  V(xmm8)                               \
  V(xmm9)                               \
  V(xmm10)                              \
  V(xmm11)                              \
  V(xmm12)                              \
  V(xmm13)                              \
  V(xmm14)

#define YMM_REGISTERS(V) \
  V(ymm0)                \
  V(ymm1)                \
  V(ymm2)                \
  V(ymm3)                \
  V(ymm4)                \
  V(ymm5)                \
  V(ymm6)                \
  V(ymm7)                \
  V(ymm8)                \
  V(ymm9)                \
  V(ymm10)               \
  V(ymm11)               \
  V(ymm12)               \
  V(ymm13)               \
  V(ymm14)               \
  V(ymm15)

// Returns the number of padding slots needed for stack pointer alignment.
constexpr int ArgumentPaddingSlots(int argument_count) {
  // No argument padding required.
  return 0;
}

constexpr AliasingKind kFPAliasing = AliasingKind::kOverlap;
constexpr bool kSimdMaskRegisters = false;

enum DoubleRegisterCode {
#define REGISTER_CODE(R) kDoubleCode_##R,
  DOUBLE_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kDoubleAfterLast
};

enum YMMRegisterCode {
#define REGISTER_CODE(R) kYMMCode_##R,
  YMM_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kYMMAfterLast
};
static_assert(static_cast<int>(kDoubleAfterLast) ==
                  static_cast<int>(kYMMAfterLast),
              "The number of XMM register codes must match the number of YMM "
              "register codes");

class XMMRegister : public RegisterBase<XMMRegister, kDoubleAfterLast> {
 public:
  // Return the high bit of the register code as a 0 or 1.  Used often
  // when constructing the REX prefix byte.
  int high_bit() const { return code() >> 3; }
  // Return the 3 low bits of the register code.  Used when encoding registers
  // in modR/M, SIB, and opcode bytes.
  int low_bits() const { return code() & 0x7; }

 protected:
  friend class RegisterBase<XMMRegister, kDoubleAfterLast>;
  explicit constexpr XMMRegister(int code) : RegisterBase(code) {}
};

ASSERT_TRIVIALLY_COPYABLE(XMMRegister);
static_assert(sizeof(XMMRegister) <= sizeof(int),
              "XMMRegister can efficiently be passed by value");

class YMMRegister : public XMMRegister {
 public:
  static constexpr YMMRegister from_code(int code) {
    V8_ASSUME(code >= 0 && code < XMMRegister::kNumRegisters);
    return YMMRegister(code);
  }

 private:
  friend class XMMRegister;
  explicit constexpr YMMRegister(int code) : XMMRegister(code) {}
};

ASSERT_TRIVIALLY_COPYABLE(YMMRegister);
static_assert(sizeof(YMMRegister) <= sizeof(int),
              "YMMRegister can efficiently be passed by value");

using FloatRegister = XMMRegister;

using DoubleRegister = XMMRegister;

using Simd128Register = XMMRegister;

using Simd256Register = YMMRegister;

#define DECLARE_REGISTER(R) \
  constexpr DoubleRegister R = DoubleRegister::from_code(kDoubleCode_##R);
DOUBLE_REGISTERS(DECLARE_REGISTER)
#undef DECLARE_REGISTER
constexpr DoubleRegister no_dreg = DoubleRegister::no_reg();

#define DECLARE_REGISTER(R) \
  constexpr YMMRegister R = YMMRegister::from_code(kYMMCode_##R);
YMM_REGISTERS(DECLARE_REGISTER)
#undef DECLARE_REGISTER

// Define {RegisterName} methods for the register types.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(XMMRegister, DOUBLE_REGISTERS)
DEFINE_REGISTER_NAMES(YMMRegister, YMM_REGISTERS)

// Give alias names to registers for calling conventions.
constexpr Register kStackPointerRegister = rsp;
constexpr Register kReturnRegister0 = rax;
constexpr Register kReturnRegister1 = rdx;
constexpr Register kReturnRegister2 = r8;
constexpr Register kJSFunctionRegister = rdi;
constexpr Register kContextRegister = rsi;
constexpr Register kAllocateSizeRegister = rdx;
constexpr Register kInterpreterAccumulatorRegister = rax;
constexpr Register kInterpreterBytecodeOffsetRegister = r9;
constexpr Register kInterpreterBytecodeArrayRegister = r12;
constexpr Register kInterpreterDispatchTableRegister = r15;

constexpr Register kJavaScriptCallArgCountRegister = rax;
constexpr Register kJavaScriptCallCodeStartRegister = rcx;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = rdx;
constexpr Register kJavaScriptCallExtraArg1Register = rbx;

constexpr Register kRuntimeCallFunctionRegister = rbx;
constexpr Register kRuntimeCallArgCountRegister = rax;
constexpr Register kRuntimeCallArgvRegister = r15;
// TODO(14499): Rename to kWasmInstanceDataRegister.
constexpr Register kWasmInstanceRegister = rsi;
constexpr Register kWasmTrapHandlerFaultAddressRegister = r10;

// Default scratch register used by MacroAssembler (and other code that needs
// a spare register). The register isn't callee save, and not used by the
// function calling convention.
constexpr Register kScratchRegister = r10;
constexpr XMMRegister kScratchDoubleReg = xmm15;
constexpr YMMRegister kScratchSimd256Reg = ymm15;
constexpr Register kRootRegister = r13;  // callee save
#ifdef V8_COMPRESS_POINTERS
constexpr Register kPtrComprCageBaseRegister = r14;  // callee save
#else
constexpr Register kPtrComprCageBaseRegister = no_reg;
#endif

constexpr DoubleRegister kFPReturnRegister0 = xmm0;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_X64_REGISTER_X64_H_
