// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_IA32_REGISTER_IA32_H_
#define V8_CODEGEN_IA32_REGISTER_IA32_H_

#include "src/codegen/register-base.h"

namespace v8 {
namespace internal {

#define GENERAL_REGISTERS(V) \
  V(eax)                     \
  V(ecx)                     \
  V(edx)                     \
  V(ebx)                     \
  V(esp)                     \
  V(ebp)                     \
  V(esi)                     \
  V(edi)

#define ALLOCATABLE_GENERAL_REGISTERS(V) \
  V(eax)                                 \
  V(ecx)                                 \
  V(edx)                                 \
  V(esi)                                 \
  V(edi)

#define DOUBLE_REGISTERS(V) \
  V(xmm0)                   \
  V(xmm1)                   \
  V(xmm2)                   \
  V(xmm3)                   \
  V(xmm4)                   \
  V(xmm5)                   \
  V(xmm6)                   \
  V(xmm7)

#define FLOAT_REGISTERS DOUBLE_REGISTERS
#define SIMD128_REGISTERS DOUBLE_REGISTERS

#define ALLOCATABLE_DOUBLE_REGISTERS(V) \
  V(xmm1)                               \
  V(xmm2)                               \
  V(xmm3)                               \
  V(xmm4)                               \
  V(xmm5)                               \
  V(xmm6)                               \
  V(xmm7)

enum RegisterCode {
#define REGISTER_CODE(R) kRegCode_##R,
  GENERAL_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kRegAfterLast
};

class Register : public RegisterBase<Register, kRegAfterLast> {
 public:
  bool is_byte_register() const { return code() <= 3; }

 private:
  friend class RegisterBase<Register, kRegAfterLast>;
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

// Returns the number of padding slots needed for stack pointer alignment.
constexpr int ArgumentPaddingSlots(int argument_count) {
  // No argument padding required.
  return 0;
}

constexpr AliasingKind kFPAliasing = AliasingKind::kOverlap;
constexpr bool kSimdMaskRegisters = false;

enum DoubleCode {
#define REGISTER_CODE(R) kDoubleCode_##R,
  DOUBLE_REGISTERS(REGISTER_CODE)
#undef REGISTER_CODE
      kDoubleAfterLast
};

class XMMRegister : public RegisterBase<XMMRegister, kDoubleAfterLast> {
  friend class RegisterBase<XMMRegister, kDoubleAfterLast>;
  explicit constexpr XMMRegister(int code) : RegisterBase(code) {}
};

using FloatRegister = XMMRegister;

using DoubleRegister = XMMRegister;

using Simd128Register = XMMRegister;

#define DEFINE_REGISTER(R) \
  constexpr DoubleRegister R = DoubleRegister::from_code(kDoubleCode_##R);
DOUBLE_REGISTERS(DEFINE_REGISTER)
#undef DEFINE_REGISTER
constexpr DoubleRegister no_dreg = DoubleRegister::no_reg();

// Note that the bit values must match those used in actual instruction encoding
constexpr int kNumRegs = 8;

// Define {RegisterName} methods for the register types.
DEFINE_REGISTER_NAMES(Register, GENERAL_REGISTERS)
DEFINE_REGISTER_NAMES(XMMRegister, DOUBLE_REGISTERS)

// Give alias names to registers for calling conventions.
constexpr Register kReturnRegister0 = eax;
constexpr Register kReturnRegister1 = edx;
constexpr Register kReturnRegister2 = edi;
constexpr Register kJSFunctionRegister = edi;
constexpr Register kContextRegister = esi;
constexpr Register kAllocateSizeRegister = edx;
constexpr Register kInterpreterAccumulatorRegister = eax;
constexpr Register kInterpreterBytecodeOffsetRegister = edx;
constexpr Register kInterpreterBytecodeArrayRegister = edi;
constexpr Register kInterpreterDispatchTableRegister = esi;

constexpr Register kJavaScriptCallArgCountRegister = eax;
constexpr Register kJavaScriptCallCodeStartRegister = ecx;
constexpr Register kJavaScriptCallTargetRegister = kJSFunctionRegister;
constexpr Register kJavaScriptCallNewTargetRegister = edx;

// The ExtraArg1Register not part of the real JS calling convention and is
// mostly there to simplify consistent interface descriptor definitions across
// platforms. Note that on ia32 it aliases kJavaScriptCallCodeStartRegister.
constexpr Register kJavaScriptCallExtraArg1Register = ecx;

constexpr Register kRuntimeCallFunctionRegister = edx;
constexpr Register kRuntimeCallArgCountRegister = eax;
constexpr Register kRuntimeCallArgvRegister = ecx;
constexpr Register kWasmImplicitArgRegister = esi;
constexpr Register kWasmCompileLazyFuncIndexRegister = edi;

constexpr Register kRootRegister = ebx;

constexpr DoubleRegister kFPReturnRegister0 = xmm1;  // xmm0 isn't allocatable.

}  // namespace internal
}  // namespace v8

#endif  // V8_CODEGEN_IA32_REGISTER_IA32_H_
