// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MIPS64_REGISTER_MIPS64_H_
#define V8_CODEGEN_MIPS64_REGISTER_MIPS64_H_

#include "src/codegen/mips64/constants-mips64.h"
#include "src/codegen/register-base.h"

namespace v8 {
namespace internal {

// clang-format off
#define GENERAL_REGISTERS(V)                              \
  V(zero_reg)  V(at)  V(v0)  V(v1)  V(a0)  V(a1)  V(a2)  V(a3)  \
  V(a4)  V(a5)  V(a6)  V(a7)  V(t0)  V(t1)  V(t2)  V(t3)  \
  V(s0)  V(s1)  V(s2)  V(s3)  V(s4)  V(s5)  V(s6)  V(s7)  V(t8)  V(t9) \
  V(k0)  V(k1)  V(gp)  V(sp)  V(fp)  V(ra)

#define ALLOCATABLE_GENERAL_REGISTERS(V) \
  V(a0)  V(a1)  V(a2)  V(a3) \
  V(a4)  V(a5)  V(a6)  V(a7)  V(t0)  V(t1)  V(t2)  V(t3)  V(s7) \
  V(v0)  V(v1)

#define DOUBLE_REGISTERS(V)                               \
  V(f0)  V(f1)  V(f2)  V(f3)  V(f4)  V(f5)  V(f6)  V(f7)  \
  V(f8)  V(f9)  V(f10) V(f11) V(f12) V(f13) V(f14) V(f15) \
  V(f16) V(f17) V(f18) V(f19) V(f20) V(f21) V(f22) V(f23) \
  V(f24) V(f25) V(f26) V(f27) V(f28) V(f29) V(f30) V(f31)

// Currently, MIPS64 just use even float point register, except
// for C function param registers.
#define DOUBLE_USE_REGISTERS(V)                           \
  V(f0)  V(f2)  V(f4)  V(f6)  V(f8)  V(f10) V(f12) V(f13) \
  V(f14) V(f15) V(f16) V(f17) V(f18) V(f19) V(f20) V(f22) \
  V(f24) V(f26) V(f28) V(f30)

#define FLOAT_REGISTERS DOUBLE_REGISTERS
#define SIMD128_REGISTERS(V)                              \
  V(w0)  V(w1)  V(w2)  V(w3)  V(w4)  V(w5)  V(w6)  V(w7)  \
  V(w8)  V(w9)  V(w10) V(w11) V(w12) V(w13) V(w14) V(w15) \
  V(w16) V(w17) V(w18) V(w19) V(w20) V(w21) V(w22) V(w23) \
  V(w24) V(w25) V(w26) V(w27) V(w28) V(w29) V(w30) V(w31)

#define ALLOCATABLE_DOUBLE_REGISTERS(V)                   \
  V(f0)  V(f2)  V(f4)  V(f6)  V(f8)  V(f10) V(f12) V(f14) \
  V(f16) V(f18) V(f20) V(f22) V(f24) V(f26)
// clang-format on

// Note that the bit values must match those used in actual instruction
// encoding.
const int kNumRegs = 32;

// CPU Registers.
//
// 1) We would prefer to use an enum, but enum values are assignment-
// compatible with int, which has caused code-generation bugs.
//
// 2) We would prefer to use a class instead of a struct but we don't like
// the register initialization to depend on the particular initialization
// order (which appears to be different on OS X, Linux, and Windows for the
// installed versions of C++ we tried). Using a struct permits C-style
// "initialization". Also, the Register objects cannot be const as this
// forces initialization stubs in MSVC, making us dependent on initialization
// order.
//
// 3) By not using an enum, we are possibly preventing the compiler from
// doing certain constant folds, which may significantly reduce the
// code generated for some assembly instructions (because they boil down
// to a few constants). If this is a problem, we could change the code
// such that we use an enum in optimized mode, and the struct in debug
// mode. This way we get the compile-time error checking in debug mode
// and best performance in optimized code.

// -----------------------------------------------------------------------------
// Implementation of Register and FPURegister.

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class Register : public RegisterBase<Register, kRegAfterLast> {
 public:
#if defined(V8_TARGET_LITTLE_ENDIAN)
  static constexpr int kMantissaOffset = 0;
  static constexpr int kExponentOffset = 4;
#elif defined(V8_TARGET_BIG_ENDIAN)
  static constexpr int kMantissaOffset = 4;
  static constexpr int kExponentOffset = 0;
#else
#error Unknown endianness
#endif

 private:
  friend class RegisterBase;
  explicit constexpr Register(int code) : RegisterBase(code) {}
};

// s7: context register
// s3: scratch register
// s4: scratch register 2
#define DECLARE_REGISTER(R) \
  constexpr Register R = Register::from_code(kRegCode_##R);
GENERAL_REGISTERS(DECLARE_REGISTER)
#undef DECLARE_REGISTER

constexpr Register no_reg = Register::no_reg();

int ToNumber(Register reg);

Register ToRegister(int num);

// Assign |source| value to |no_reg| and return the |source|'s previous value.
inline Register ReassignRegister(Register& source) {
  Register result = source;
  source = Register::no_reg();
  return result;
}

// Returns the number of padding slots needed for stack pointer alignment.
constexpr int ArgumentPaddingSlots(int argument_count) {
  // No argument padding required.
  return 0;
}

constexpr AliasingKind kFPAliasing = AliasingKind::kOverlap;
constexpr bool kSimdMaskRegisters = false;

enum MSARegisterCode {
#define REGISTER_CODE(R) kMsaCode_##R,
  SIMD128_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kMsaAfterLast
};

// MIPS SIMD (MSA) register
class MSARegister : public RegisterBase<MSARegister, kMsaAfterLast> {
  friend class RegisterBase;
  explicit constexpr MSARegister(int code) : RegisterBase(code) {}
};

enum DoubleRegisterCode {
#define REGISTER_CODE(R) kDoubleCode_##R,
  DOUBLE_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kDoubleAfterLast
};

// Coprocessor register.
class FPURegister : public RegisterBase<FPURegister, kDoubleAfterLast> {
 public:
  // TODO(plind): Warning, inconsistent numbering here. kNumFPURegisters refers
  // to number of 32-bit FPU regs, but kNumAllocatableRegisters refers to
  // number of Double regs (64-bit regs, or FPU-reg-pairs).

  FPURegister low() const {
    // TODO(plind): Create DCHECK for FR=0 mode. This usage suspect for FR=1.
    // Find low reg of a Double-reg pair, which is the reg itself.
    DCHECK_EQ(code() % 2, 0);  // Specified Double reg must be even.
    return FPURegister::from_code(code());
  }
  FPURegister high() const {
    // TODO(plind): Create DCHECK for FR=0 mode. This usage illegal in FR=1.
    // Find high reg of a Doubel-reg pair, which is reg + 1.
    DCHECK_EQ(code() % 2, 0);  // Specified Double reg must be even.
    return FPURegister::from_code(code() + 1);
  }

  MSARegister toW() const { return MSARegister::from_code(code()); }

 private:
  friend class RegisterBase;
  explicit constexpr FPURegister(int code) : RegisterBase(code) {}
};

// A few double registers are reserved: one as a scratch register and one to
// hold 0.0.
//  f28: 0.0
//  f30: scratch register.

// V8 now supports the O32 ABI, and the FPU Registers are organized as 32
// 32-bit registers, f0 through f31. When used as 'double' they are used
// in pairs, starting with the even numbered register. So a double operation
// on f0 really uses f0 and f1.
// (Modern mips hardware also supports 32 64-bit registers, via setting
// (privileged) Status Register FR bit to 1. This is used by the N32 ABI,
// but it is not in common use. Someday we will want to support this in v8.)

// For O32 ABI, Floats and Doubles refer to same set of 32 32-bit registers.
using FloatRegister = FPURegister;

using DoubleRegister = FPURegister;

#define DECLARE_DOUBLE_REGISTER(R) \
  constexpr DoubleRegister R = DoubleRegister::from_code(kDoubleCode_##R);
DOUBLE_REGISTERS(DECLARE_DOUBLE_REGISTER)
#undef DECLARE_DOUBLE_REGISTER

constexpr DoubleRegister no_dreg = DoubleRegister::no_reg();

// SIMD registers.
using Simd128Register = MSARegister;

#define DECLARE_SIMD128_REGISTER(R) \
  constexpr Simd128Register R = Simd128Register::from_code(kMsaCode_##R);
SIMD128_REGISTERS(DECLARE_SIMD128_REGISTER)
#undef DECLARE_SIMD128_REGISTER

const Simd128Register no_msareg = Simd128Register::no_reg();

// Register aliases.
// cp is assumed to be a callee saved register.
constexpr Register kRootRegister = s6;
constexpr Register cp = s7;
constexpr Register kScratchReg = s3;
constexpr Register kScratchReg2 = s4;
constexpr DoubleRegister kScratchDoubleReg = f30;
// FPU zero reg is often used to hold 0.0, but it's not hardwired to 0.0.
constexpr DoubleRegister kDoubleRegZero = f28;
// Used on mips64r6 for compare operations.
// We use the last non-callee saved odd register for N64 ABI
constexpr DoubleRegister kDoubleCompareReg = f23;
// MSA zero and scratch regs must have the same numbers as FPU zero and scratch
// MSA zero reg is often used to hold 0, but it's not hardwired to 0.
constexpr Simd128Register kSimd128RegZero = w28;
constexpr Simd128Register kSimd128ScratchReg = w30;

// FPU (coprocessor 1) control registers.
// Currently only FCSR (#31) is implemented.
struct FPUControlRegister {
  bool is_valid() const { return reg_code == kFCSRRegister; }
  bool is(FPUControlRegister creg) const { return reg_code == creg.reg_code; }
  int code() const {
    DCHECK(is_valid());
    return reg_code;
  }
  int bit() const {
    DCHECK(is_valid());
    return 1 << reg_code;
  }
  void setcode(int f) {
    reg_code = f;
    DCHECK(is_valid());
  }
  // Unfortunately we can't make this private in a struct.
  int reg_code;
};

constexpr FPUControlRegister no_fpucreg = {kInvalidFPUControlRegister};
constexpr FPUControlRegister FCSR = {kFCSRRegister};

// MSA control registers
struct MSAControlRegister {
  bool is_valid() const {
    return (reg_code == kMSAIRRegister) || (reg_code == kMSACSRRegister);
  }
  bool is(MSAControlRegister creg) const { return reg_code == creg.reg_code; }
  int code() const {
    DCHECK(is_valid());
    return reg_code;
  }
  int bit() const {
    DCHECK(is_valid());
    return 1 << reg_code;
  }
  void setcode(int f) {
    reg_code = f;
    DCHECK(is_valid());
  }
  // Unfortunately we can't make this private in a struct.
  int reg_code;
};

constexpr MSAControlRegister no_msacreg = {kInvalidMSAControlRegister};
constexpr MSAControlRegister MSAIR = {kMSAIRRegister};
constexpr MSAControlRegister MSACSR = {kMSACSRRegister};

// Define {RegisterName} methods for the register types.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(FPURegister, DOUBLE_REGISTERS)
DEFINE_REGISTER_NAMES(MSARegister, SIMD128_REGISTERS)

constexpr Register kCArgRegs[] = {a0, a1, a2, a3, a4, a5, a6, a7};
constexpr int kRegisterPassedArguments = arraysize(kCArgRegs);
constexpr int kFPRegisterPassedArguments = 8;

constexpr Register kReturnRegister0 = v0;
constexpr Register kReturnRegister1 = v1;
constexpr Register kReturnRegister2 = a0;
constexpr Register kJSFunctionRegister = a1;
constexpr Register kContextRegister = s7;
constexpr Register kAllocateSizeRegister = a0;
constexpr Register kInterpreterAccumulatorRegister = v0;
constexpr Register kInterpreterBytecodeOffsetRegister = t0;
constexpr Register kInterpreterBytecodeArrayRegister = t1;
constexpr Register kInterpreterDispatchTableRegister = t2;

constexpr Register kJavaScriptCallArgCountRegister = a0;
constexpr Register kJavaScriptCallCodeStartRegister = a2;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = a3;
constexpr Register kJavaScriptCallExtraArg1Register = a2;

constexpr Register kRuntimeCallFunctionRegister = a1;
constexpr Register kRuntimeCallArgCountRegister = a0;
constexpr Register kRuntimeCallArgvRegister = a2;
constexpr Register kWasmInstanceRegister = a0;
constexpr Register kWasmCompileLazyFuncIndexRegister = t0;

constexpr DoubleRegister kFPReturnRegister0 = f0;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_MIPS64_REGISTER_MIPS64_H_
