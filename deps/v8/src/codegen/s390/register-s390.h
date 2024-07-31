// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_S390_REGISTER_S390_H_
#define V8_CODEGEN_S390_REGISTER_S390_H_

#include "src/codegen/register-base.h"

namespace v8 {
namespace internal {

// clang-format off
#define GENERAL_REGISTERS(V)                              \
  V(r0)  V(r1)  V(r2)  V(r3)  V(r4)  V(r5)  V(r6)  V(r7)  \
  V(r8)  V(r9)  V(r10) V(fp) V(ip) V(r13) V(r14) V(sp)

#define ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(V)                  \
  V(r2)  V(r3)  V(r4)  V(r5)  V(r6)  V(r7)                \
  V(r8)  V(r13)

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V)
#else
#define MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V) V(r9)
#endif

#define ALLOCATABLE_GENERAL_REGISTERS(V)  \
  ALWAYS_ALLOCATABLE_GENERAL_REGISTERS(V) \
  MAYBE_ALLOCATABLE_GENERAL_REGISTERS(V)

#define DOUBLE_REGISTERS(V)                               \
  V(d0)  V(d1)  V(d2)  V(d3)  V(d4)  V(d5)  V(d6)  V(d7)  \
  V(d8)  V(d9)  V(d10) V(d11) V(d12) V(d13) V(d14) V(d15)

#define FLOAT_REGISTERS DOUBLE_REGISTERS
#define SIMD128_REGISTERS DOUBLE_REGISTERS

#define ALLOCATABLE_DOUBLE_REGISTERS(V)                   \
  V(d1)  V(d2)  V(d3)  V(d4)  V(d5)  V(d6)  V(d7)         \
  V(d8)  V(d9)  V(d10) V(d11) V(d12) V(d15) V(d0)

#define C_REGISTERS(V)                                            \
  V(cr0)  V(cr1)  V(cr2)  V(cr3)  V(cr4)  V(cr5)  V(cr6)  V(cr7)  \
  V(cr8)  V(cr9)  V(cr10) V(cr11) V(cr12) V(cr15)
// clang-format on

// The following constants describe the stack frame linkage area as
// defined by the ABI.

#if V8_TARGET_ARCH_S390X
// [0] Back Chain
// [1] Reserved for compiler use
// [2] GPR 2
// [3] GPR 3
// ...
// [15] GPR 15
// [16] FPR 0
// [17] FPR 2
// [18] FPR 4
// [19] FPR 6
const int kNumRequiredStackFrameSlots = 20;
const int kStackFrameRASlot = 14;
const int kStackFrameSPSlot = 15;
const int kStackFrameExtraParamSlot = 20;
#else
// [0] Back Chain
// [1] Reserved for compiler use
// [2] GPR 2
// [3] GPR 3
// ...
// [15] GPR 15
// [16..17] FPR 0
// [18..19] FPR 2
// [20..21] FPR 4
// [22..23] FPR 6
const int kNumRequiredStackFrameSlots = 24;
const int kStackFrameRASlot = 14;
const int kStackFrameSPSlot = 15;
const int kStackFrameExtraParamSlot = 24;
#endif

// zLinux ABI requires caller frames to include sufficient space for
// callee preserved register save area.
#if V8_TARGET_ARCH_S390X
const int kCalleeRegisterSaveAreaSize = 160;
#elif V8_TARGET_ARCH_S390
const int kCalleeRegisterSaveAreaSize = 96;
#else
const int kCalleeRegisterSaveAreaSize = 0;
#endif

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class Register : public RegisterBase<Register, kRegAfterLast> {
 public:
#if V8_TARGET_LITTLE_ENDIAN
  static constexpr int kMantissaOffset = 0;
  static constexpr int kExponentOffset = 4;
#else
  static constexpr int kMantissaOffset = 4;
  static constexpr int kExponentOffset = 0;
#endif

 private:
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

#define DEFINE_REGISTER(R) \
  constexpr Register R = Register::from_code(kRegCode_##R);
GENERAL_REGISTERS(DEFINE_REGISTER)
#undef DEFINE_REGISTER
constexpr Register no_reg = Register::no_reg();

// Register aliases
constexpr Register kRootRegister = r10;  // Roots array pointer.
#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
constexpr Register kPtrComprCageBaseRegister = r9;  // callee save
#else
constexpr Register kPtrComprCageBaseRegister = kRootRegister;
#endif
constexpr Register cp = r13;             // JavaScript context pointer.

// s390x calling convention
constexpr Register kCArgRegs[] = {r2, r3, r4, r5, r6};
static const int kRegisterPassedArguments = arraysize(kCArgRegs);

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

// Double word VFP register.
class DoubleRegister : public RegisterBase<DoubleRegister, kDoubleAfterLast> {
 public:
  // A few double registers are reserved: one as a scratch register and one to
  // hold 0.0, that does not fit in the immediate field of vmov instructions.
  // d14: 0.0
  // d15: scratch register.
  static constexpr int kSizeInBytes = 8;

  // This function differs from kNumRegisters by returning the number of double
  // registers supported by the current CPU, while kNumRegisters always returns
  // 32.
  inline static int SupportedRegisterCount();

 private:
  friend class RegisterBase;

  explicit constexpr DoubleRegister(int code) : RegisterBase(code) {}
};

ASSERT_TRIVIALLY_COPYABLE(DoubleRegister);
static_assert(sizeof(DoubleRegister) <= sizeof(int),
              "DoubleRegister can efficiently be passed by value");

using FloatRegister = DoubleRegister;

// TODO(john.yan) Define SIMD registers.
using Simd128Register = DoubleRegister;

#define DEFINE_REGISTER(R) \
  constexpr DoubleRegister R = DoubleRegister::from_code(kDoubleCode_##R);
DOUBLE_REGISTERS(DEFINE_REGISTER)
#undef DEFINE_REGISTER
constexpr DoubleRegister no_dreg = DoubleRegister::no_reg();

constexpr DoubleRegister kDoubleRegZero = d14;
constexpr DoubleRegister kScratchDoubleReg = d13;

Register ToRegister(int num);

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

constexpr CRegister no_creg = CRegister::no_reg();
#define DECLARE_C_REGISTER(R) \
  constexpr CRegister R = CRegister::from_code(kCCode_##R);
C_REGISTERS(DECLARE_C_REGISTER)
#undef DECLARE_C_REGISTER

// Define {RegisterName} methods for the register types.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(DoubleRegister, DOUBLE_REGISTERS)

// Give alias names to registers for calling conventions.
constexpr Register kReturnRegister0 = r2;
constexpr Register kReturnRegister1 = r3;
constexpr Register kReturnRegister2 = r4;
constexpr Register kJSFunctionRegister = r3;
constexpr Register kContextRegister = r13;
constexpr Register kAllocateSizeRegister = r3;
constexpr Register kInterpreterAccumulatorRegister = r2;
constexpr Register kInterpreterBytecodeOffsetRegister = r6;
constexpr Register kInterpreterBytecodeArrayRegister = r7;
constexpr Register kInterpreterDispatchTableRegister = r8;

constexpr Register kJavaScriptCallArgCountRegister = r2;
constexpr Register kJavaScriptCallCodeStartRegister = r4;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = r5;
constexpr Register kJavaScriptCallExtraArg1Register = r4;

constexpr Register kRuntimeCallFunctionRegister = r3;
constexpr Register kRuntimeCallArgCountRegister = r2;
constexpr Register kRuntimeCallArgvRegister = r4;
constexpr Register kWasmInstanceRegister = r6;
constexpr Register kWasmCompileLazyFuncIndexRegister = r7;

constexpr DoubleRegister kFPReturnRegister0 = d0;

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_S390_REGISTER_S390_H_
