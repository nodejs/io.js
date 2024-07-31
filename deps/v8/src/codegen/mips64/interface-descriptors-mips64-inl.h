// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_MIPS64_INTERFACE_DESCRIPTORS_MIPS64_INL_H_
#define V8_CODEGEN_MIPS64_INTERFACE_DESCRIPTORS_MIPS64_INL_H_

#if V8_TARGET_ARCH_MIPS64

#include "src/codegen/interface-descriptors.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(a0, a1, a2, a3, a4);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  auto registers = DoubleRegisterArray(f0, f2, f4, f6, f8, f10, f12);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnRegisterArray() {
  auto registers =
      RegisterArray(kReturnRegister0, kReturnRegister1, kReturnRegister2);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnDoubleRegisterArray() {
  // Padding to have as many double return registers as GP return registers.
  auto registers = DoubleRegisterArray(kFPReturnRegister0, no_dreg, no_dreg);
  return registers;
}

#if DEBUG
template <typename DerivedDescriptor>
void StaticCallInterfaceDescriptor<DerivedDescriptor>::
    VerifyArgumentRegisterCount(CallInterfaceDescriptorData* data, int argc) {
  RegList allocatable_regs = data->allocatable_registers();
  if (argc >= 1) DCHECK(allocatable_regs.has(a0));
  if (argc >= 2) DCHECK(allocatable_regs.has(a1));
  if (argc >= 3) DCHECK(allocatable_regs.has(a2));
  if (argc >= 4) DCHECK(allocatable_regs.has(a3));
  if (argc >= 5) DCHECK(allocatable_regs.has(a4));
  if (argc >= 6) DCHECK(allocatable_regs.has(a5));
  if (argc >= 7) DCHECK(allocatable_regs.has(a6));
  if (argc >= 8) DCHECK(allocatable_regs.has(a7));
  // Additional arguments are passed on the stack.
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  return RegisterArray(a1, a5, a4, a0, a2, v0, a3, kContextRegister);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return a1; }
// static
constexpr Register LoadDescriptor::NameRegister() { return a2; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return a0; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return a3; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return a1;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return a2; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return a3;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return a4;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return a5;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return a2;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return a1; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return a2; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return a3;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return a4;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return a1; }
// static
constexpr Register StoreDescriptor::NameRegister() { return a2; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return a0; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return a4; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return a3; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return a5; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return a5; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return a0; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return a3; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return a0; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return a3; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return a2;
}

// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() { return a3; }

// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return a0; }

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(a0); }

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // a1: target
  // a0: number of arguments
  return RegisterArray(a1, a0);
}

// static
constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // a1 : the source
  // a0 : the excluded property count
  return RegisterArray(a1, a0);
}

// static
constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // a1 : the source
  // a0 : the excluded property count
  // a2 : the excluded property base
  return RegisterArray(a1, a0, a2);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // a0 : number of arguments (on the stack)
  // a1 : the target to call
  // a4 : arguments list length (untagged)
  // a2 : arguments list (FixedArray)
  return RegisterArray(a1, a0, a4, a2);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // a1: the target to call
  // a0: number of arguments
  // a2: start index (to support rest parameters)
  return RegisterArray(a1, a0, a2);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // a1 : function template info
  // a0 : number of arguments (on the stack)
  return RegisterArray(a1, a0);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // a1 : function template info
  // a2 : number of arguments (on the stack)
  // a3 : topmost script-having context
  return RegisterArray(a1, a2, a3);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // a0 : number of arguments (on the stack)
  // a1 : the target to call
  // a2 : the object to spread
  return RegisterArray(a1, a0, a2);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // a1 : the target to call
  // a2 : the arguments list
  return RegisterArray(a1, a2);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // a0 : number of arguments (on the stack)
  // a1 : the target to call
  // a3 : the new target
  // a4 : arguments list length (untagged)
  // a2 : arguments list (FixedArray)
  return RegisterArray(a1, a3, a0, a4, a2);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // a1: the target to call
  // a3: new target
  // a0: number of arguments
  // a2: start index (to support rest parameters)
  return RegisterArray(a1, a3, a0, a2);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // a0 : number of arguments (on the stack)
  // a1 : the target to call
  // a3 : the new target
  // a2 : the object to spread
  return RegisterArray(a1, a3, a0, a2);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // a1 : the target to call
  // a3 : the new target
  // a2 : the arguments list
  return RegisterArray(a1, a3, a2);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // a1: target
  // a3: new target
  // a0: number of arguments
  return RegisterArray(a1, a3, a0);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(a0); }

// static
constexpr auto CompareDescriptor::registers() { return RegisterArray(a1, a0); }

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  // a1: left operand
  // a0: right operand
  // a2: feedback slot
  return RegisterArray(a1, a0, a2);
}

// static
constexpr auto BinaryOpDescriptor::registers() { return RegisterArray(a1, a0); }

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  // a1: left operand
  // a0: right operand
  // a2: feedback slot
  return RegisterArray(a1, a0, a2);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  // a0: left operand
  // a1: right operand
  // a2: feedback slot
  return RegisterArray(a0, a1, a2);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return a1;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return a2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return a1;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::CallDataRegister() {
  return a3;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return a0;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return a2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return a3;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return a0;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  // a0 : argument count
  // a2 : address of first argument
  // a1 : the target callable to be call
  return RegisterArray(a0, a2, a1);
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  // a0 : argument count
  // a4 : address of the first argument
  // a1 : constructor to call
  // a3 : new target
  // a2 : allocation site feedback if available, undefined otherwise
  return RegisterArray(a0, a4, a1, a3, a2);
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(a1,   // constructor to call
                       a3);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  // v0 : the value to pass to the generator
  // a1 : the JSGeneratorObject to resume
  return RegisterArray(v0, a1);
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(a0, a1);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(t0);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_MIPS64

#endif  // V8_CODEGEN_MIPS64_INTERFACE_DESCRIPTORS_MIPS64_INL_H_
