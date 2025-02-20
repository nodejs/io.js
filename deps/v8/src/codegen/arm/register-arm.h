// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM_REGISTER_ARM_H_
#define V8_CODEGEN_ARM_REGISTER_ARM_H_

#include "src/codegen/register-base.h"

namespace v8 {
namespace internal {

// clang-format off
#define GENERAL_REGISTERS(V)                              \
  V(r0)  V(r1)  V(r2)  V(r3)  V(r4)  V(r5)  V(r6)  V(r7)  \
  V(r8)  V(r9)  V(r10) V(fp)  V(ip)  V(sp)  V(lr)  V(pc)

#define ALLOCATABLE_GENERAL_REGISTERS(V)                  \
  V(r0)  V(r1)  V(r2)  V(r3)  V(r4)  V(r5)  V(r6)  V(r7)  \
  V(r8)  V(r9)

#define FLOAT_REGISTERS(V)                                \
  V(s0)  V(s1)  V(s2)  V(s3)  V(s4)  V(s5)  V(s6)  V(s7)  \
  V(s8)  V(s9)  V(s10) V(s11) V(s12) V(s13) V(s14) V(s15) \
  V(s16) V(s17) V(s18) V(s19) V(s20) V(s21) V(s22) V(s23) \
  V(s24) V(s25) V(s26) V(s27) V(s28) V(s29) V(s30) V(s31)

#define LOW_DOUBLE_REGISTERS(V)                           \
  V(d0)  V(d1)  V(d2)  V(d3)  V(d4)  V(d5)  V(d6)  V(d7)  \
  V(d8)  V(d9)  V(d10) V(d11) V(d12) V(d13) V(d14) V(d15)

#define NON_LOW_DOUBLE_REGISTERS(V)                       \
  V(d16) V(d17) V(d18) V(d19) V(d20) V(d21) V(d22) V(d23) \
  V(d24) V(d25) V(d26) V(d27) V(d28) V(d29) V(d30) V(d31)

#define DOUBLE_REGISTERS(V) \
  LOW_DOUBLE_REGISTERS(V) NON_LOW_DOUBLE_REGISTERS(V)

#define SIMD128_REGISTERS(V)                              \
  V(q0)  V(q1)  V(q2)  V(q3)  V(q4)  V(q5)  V(q6)  V(q7)  \
  V(q8)  V(q9)  V(q10) V(q11) V(q12) V(q13) V(q14) V(q15)

#define ALLOCATABLE_DOUBLE_REGISTERS(V)                   \
  V(d0)  V(d1)  V(d2)  V(d3)  V(d4)  V(d5)  V(d6)  V(d7)  \
  V(d8)  V(d9)  V(d10) V(d11) V(d12)                      \
  V(d16) V(d17) V(d18) V(d19) V(d20) V(d21) V(d22) V(d23) \
  V(d24) V(d25) V(d26) V(d27) V(d28) V(d29) V(d30) V(d31)

#define ALLOCATABLE_NO_VFP32_DOUBLE_REGISTERS(V)          \
  V(d0)  V(d1)  V(d2)  V(d3)  V(d4)  V(d5)  V(d6)  V(d7)  \
  V(d8)  V(d9)  V(d10) V(d11) V(d12) V(d15)

#define C_REGISTERS(V)                                            \
  V(cr0)  V(cr1)  V(cr2)  V(cr3)  V(cr4)  V(cr5)  V(cr6)  V(cr7)  \
  V(cr8)  V(cr9)  V(cr10) V(cr11) V(cr12) V(cr15)
// clang-format on

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class Register : public RegisterBase<Register, kRegAfterLast> {
  friend class RegisterBase;

  explicit constexpr Register(int code) : RegisterBase(code) {}
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

// r7: context register
#define DECLARE_REGISTER(R) \
  constexpr Register R = Register::from_code(kRegCode_##R);
GENERAL_REGISTERS(DECLARE_REGISTER)
#undef DECLARE_REGISTER
constexpr Register no_reg = Register::no_reg();

// ARM calling convention
constexpr Register kCArgRegs[] = {r0, r1, r2, r3};
static const int kRegisterPassedArguments = arraysize(kCArgRegs);
// The hardfloat calling convention passes double arguments in registers d0-d7.
static const int kDoubleRegisterPassedArguments = 8;

// Returns the number of padding slots needed for stack pointer alignment.
constexpr int ArgumentPaddingSlots(int argument_count) {
  // No argument padding required.
  return 0;
}

constexpr AliasingKind kFPAliasing = AliasingKind::kCombine;
constexpr bool kSimdMaskRegisters = false;

enum SwVfpRegisterCode {
#define REGISTER_CODE(R) kSwVfpCode_##R,
  FLOAT_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kSwVfpAfterLast
};

// Representation of a list of non-overlapping VFP registers. This list
// represents the data layout of VFP registers as a bitfield:
//   S registers cover 1 bit
//   D registers cover 2 bits
//   Q registers cover 4 bits
//
// This way, we make sure no registers in the list ever overlap. However, a list
// may represent multiple different sets of registers,
// e.g. [d0 s2 s3] <=> [s0 s1 d1].
using VfpRegList = uint64_t;

// Single word VFP register.
class SwVfpRegister : public RegisterBase<SwVfpRegister, kSwVfpAfterLast> {
 public:
  static constexpr int kSizeInBytes = 4;

  static void split_code(int reg_code, int* vm, int* m) {
    DCHECK(from_code(reg_code).is_valid());
    *m = reg_code & 0x1;
    *vm = reg_code >> 1;
  }
  void split_code(int* vm, int* m) const { split_code(code(), vm, m); }
  VfpRegList ToVfpRegList() const {
    // Each bit in the list corresponds to a S register.
    return uint64_t{0x1} << code();
  }

 private:
  friend class RegisterBase;
  explicit constexpr SwVfpRegister(int code) : RegisterBase(code) {}
};

ASSERT_TRIVIALLY_COPYABLE(SwVfpRegister);
static_assert(sizeof(SwVfpRegister) <= sizeof(int),
              "SwVfpRegister can efficiently be passed by value");

using FloatRegister = SwVfpRegister;

enum DoubleRegisterCode {
#define REGISTER_CODE(R) kDoubleCode_##R,
  DOUBLE_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kDoubleAfterLast
};

// Double word VFP register.
class DwVfpRegister : public RegisterBase<DwVfpRegister, kDoubleAfterLast> {
 public:
  static constexpr int kSizeInBytes = 8;

  // This function differs from kNumRegisters by returning the number of double
  // registers supported by the current CPU, while kNumRegisters always returns
  // 32.
  inline static int SupportedRegisterCount();

  static void split_code(int reg_code, int* vm, int* m) {
    DCHECK(from_code(reg_code).is_valid());
    *m = (reg_code & 0x10) >> 4;
    *vm = reg_code & 0x0F;
  }
  void split_code(int* vm, int* m) const { split_code(code(), vm, m); }
  VfpRegList ToVfpRegList() const {
    // A D register overlaps two S registers.
    return uint64_t{0x3} << (code() * 2);
  }

 private:
  friend class RegisterBase;
  friend class LowDwVfpRegister;
  explicit constexpr DwVfpRegister(int code) : RegisterBase(code) {}
};

ASSERT_TRIVIALLY_COPYABLE(DwVfpRegister);
static_assert(sizeof(DwVfpRegister) <= sizeof(int),
              "DwVfpRegister can efficiently be passed by value");

using DoubleRegister = DwVfpRegister;

// Double word VFP register d0-15.
class LowDwVfpRegister
    : public RegisterBase<LowDwVfpRegister, kDoubleCode_d16> {
 public:
  constexpr operator DwVfpRegister() const { return DwVfpRegister(code()); }

  SwVfpRegister low() const { return SwVfpRegister::from_code(code() * 2); }
  SwVfpRegister high() const {
    return SwVfpRegister::from_code(code() * 2 + 1);
  }
  VfpRegList ToVfpRegList() const {
    // A D register overlaps two S registers.
    return uint64_t{0x3} << (code() * 2);
  }

 private:
  friend class RegisterBase;
  explicit constexpr LowDwVfpRegister(int code) : RegisterBase(code) {}
};

enum Simd128RegisterCode {
#define REGISTER_CODE(R) kSimd128Code_##R,
  SIMD128_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kSimd128AfterLast
};

// Quad word NEON register.
class QwNeonRegister : public RegisterBase<QwNeonRegister, kSimd128AfterLast> {
 public:
  static void split_code(int reg_code, int* vm, int* m) {
    V8_ASSUME(reg_code >= 0 && reg_code < kNumRegisters);
    int encoded_code = reg_code << 1;
    *m = (encoded_code & 0x10) >> 4;
    *vm = encoded_code & 0x0F;
  }
  void split_code(int* vm, int* m) const { split_code(code(), vm, m); }
  DwVfpRegister low() const { return DwVfpRegister::from_code(code() * 2); }
  DwVfpRegister high() const {
    return DwVfpRegister::from_code(code() * 2 + 1);
  }
  VfpRegList ToVfpRegList() const {
    // A Q register overlaps four S registers.
    return uint64_t{0xf} << (code() * 4);
  }

 private:
  friend class RegisterBase;
  explicit constexpr QwNeonRegister(int code) : RegisterBase(code) {}
};

using QuadRegister = QwNeonRegister;

using Simd128Register = QwNeonRegister;

enum CRegisterCode {
#define REGISTER_CODE(R) kCCode_##R,
  C_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kCAfterLast
};

// Coprocessor register
class CRegister : public RegisterBase<CRegister, kCAfterLast> {
  friend class RegisterBase;
  explicit constexpr CRegister(int code) : RegisterBase(code) {}
};

// Support for the VFP registers s0 to s31 (d0 to d15).
// Note that "s(N):s(N+1)" is the same as "d(N/2)".
#define DECLARE_FLOAT_REGISTER(R) \
  constexpr SwVfpRegister R = SwVfpRegister::from_code(kSwVfpCode_##R);
FLOAT_REGISTERS(DECLARE_FLOAT_REGISTER)
#undef DECLARE_FLOAT_REGISTER

#define DECLARE_LOW_DOUBLE_REGISTER(R) \
  constexpr LowDwVfpRegister R = LowDwVfpRegister::from_code(kDoubleCode_##R);
LOW_DOUBLE_REGISTERS(DECLARE_LOW_DOUBLE_REGISTER)
#undef DECLARE_LOW_DOUBLE_REGISTER

#define DECLARE_DOUBLE_REGISTER(R) \
  constexpr DwVfpRegister R = DwVfpRegister::from_code(kDoubleCode_##R);
NON_LOW_DOUBLE_REGISTERS(DECLARE_DOUBLE_REGISTER)
#undef DECLARE_DOUBLE_REGISTER

constexpr DwVfpRegister no_dreg = DwVfpRegister::no_reg();

#define DECLARE_SIMD128_REGISTER(R) \
  constexpr Simd128Register R = Simd128Register::from_code(kSimd128Code_##R);
SIMD128_REGISTERS(DECLARE_SIMD128_REGISTER)
#undef DECLARE_SIMD128_REGISTER

// Aliases for double registers.
constexpr LowDwVfpRegister kFirstCalleeSavedDoubleReg = d8;
constexpr LowDwVfpRegister kLastCalleeSavedDoubleReg = d15;
constexpr LowDwVfpRegister kDoubleRegZero = d13;

constexpr CRegister no_creg = CRegister::no_reg();

#define DECLARE_C_REGISTER(R) \
  constexpr CRegister R = CRegister::from_code(kCCode_##R);
C_REGISTERS(DECLARE_C_REGISTER)
#undef DECLARE_C_REGISTER

// Define {RegisterName} methods for the register types.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(SwVfpRegister, FLOAT_REGISTERS)
DEFINE_REGISTER_NAMES(DwVfpRegister, DOUBLE_REGISTERS)
DEFINE_REGISTER_NAMES(LowDwVfpRegister, LOW_DOUBLE_REGISTERS)
DEFINE_REGISTER_NAMES(QwNeonRegister, SIMD128_REGISTERS)
DEFINE_REGISTER_NAMES(CRegister, C_REGISTERS)

// Give alias names to registers for calling conventions.
constexpr Register kStackPointerRegister = sp;
constexpr Register kReturnRegister0 = r0;
constexpr Register kReturnRegister1 = r1;
constexpr Register kReturnRegister2 = r2;
constexpr Register kJSFunctionRegister = r1;
constexpr Register kContextRegister = r7;
constexpr Register kAllocateSizeRegister = r1;
constexpr Register kInterpreterAccumulatorRegister = r0;
constexpr Register kInterpreterBytecodeOffsetRegister = r5;
constexpr Register kInterpreterBytecodeArrayRegister = r6;
constexpr Register kInterpreterDispatchTableRegister = r8;

constexpr Register kJavaScriptCallArgCountRegister = r0;
constexpr Register kJavaScriptCallCodeStartRegister = r2;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = r3;
constexpr Register kJavaScriptCallExtraArg1Register = r2;
// DispatchHandle is only needed for the sandbox which is not available on
// Arm32.
constexpr Register kJavaScriptCallDispatchHandleRegister = no_reg;

constexpr Register kRuntimeCallFunctionRegister = r1;
constexpr Register kRuntimeCallArgCountRegister = r0;
constexpr Register kRuntimeCallArgvRegister = r2;
constexpr Register kWasmImplicitArgRegister = r3;
constexpr Register kWasmCompileLazyFuncIndexRegister = r4;

// Give alias names to registers
constexpr Register cp = r7;              // JavaScript context pointer.
constexpr Register r11 = fp;
constexpr Register kRootRegister = r10;  // Roots array pointer.

constexpr DoubleRegister kFPReturnRegister0 = d0;

constexpr Register kMaglevExtraScratchRegister = r9;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_ARM_REGISTER_ARM_H_
