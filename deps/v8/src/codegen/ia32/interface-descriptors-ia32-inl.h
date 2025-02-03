// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_
#define V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_

#if V8_TARGET_ARCH_IA32

#include "src/codegen/interface-descriptors.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(eax, ecx, edx, edi);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  // xmm0 isn't allocatable.
  auto registers = DoubleRegisterArray(xmm1, xmm2, xmm3, xmm4, xmm5, xmm6);
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
    VerifyArgumentRegisterCount(CallInterfaceDescriptorData* data,
                                int nof_expected_args) {
  RegList allocatable_regs = data->allocatable_registers();
  if (nof_expected_args >= 1) DCHECK(allocatable_regs.has(esi));
  if (nof_expected_args >= 2) DCHECK(allocatable_regs.has(edi));
  // Additional arguments are passed on the stack.
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  return RegisterArray(edi, ecx, edx, esi, kReturnRegister0);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return edx; }
// static
constexpr Register LoadDescriptor::NameRegister() { return ecx; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return eax; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return no_reg; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return edx;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return ecx; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return no_reg;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return ecx;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return no_reg;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return no_reg;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return edx; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return ecx; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return no_reg;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return edi;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return edx; }
// static
constexpr Register StoreDescriptor::NameRegister() { return ecx; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return no_reg; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return no_reg; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() {
  return no_reg;
}

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return no_reg; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return edi; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return ecx; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return eax; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return eax; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return ecx; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return esi;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() {
  return edi;
}

// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return eax; }

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(eax); }

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // eax : number of arguments
  // edi : the target to call
  return RegisterArray(edi, eax);
}

// static
constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // edi : the source
  // eax : the excluded property count
  return RegisterArray(edi, eax);
}

// static
constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // edi : the source
  // eax : the excluded property count
  // ecx : the excluded property base
  return RegisterArray(edi, eax, ecx);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // eax : number of arguments (on the stack)
  // edi : the target to call
  // ecx : arguments list length (untagged)
  // On the stack : arguments list (FixedArray)
  return RegisterArray(edi, eax, ecx);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // eax : number of arguments
  // ecx : start index (to support rest parameters)
  // edi : the target to call
  return RegisterArray(edi, eax, ecx);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // edx : function template info
  // ecx : number of arguments (on the stack)
  return RegisterArray(edx, ecx);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // edx: the function template info
  // ecx: number of arguments (on the stack)
  // edi: topmost script-having context
  return RegisterArray(edx, ecx, edi);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // eax : number of arguments (on the stack)
  // edi : the target to call
  // ecx : the object to spread
  return RegisterArray(edi, eax, ecx);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // edi : the target to call
  // edx : the arguments list
  return RegisterArray(edi, edx);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // eax : number of arguments (on the stack)
  // edi : the target to call
  // edx : the new target
  // ecx : arguments list length (untagged)
  // On the stack : arguments list (FixedArray)
  return RegisterArray(edi, edx, eax, ecx);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // eax : number of arguments
  // edx : the new target
  // ecx : start index (to support rest parameters)
  // edi : the target to call
  return RegisterArray(edi, edx, eax, ecx);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // eax : number of arguments (on the stack)
  // edi : the target to call
  // edx : the new target
  // ecx : the object to spread
  return RegisterArray(edi, edx, eax, ecx);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // edi : the target to call
  // edx : the new target
  // ecx : the arguments list
  return RegisterArray(edi, edx, ecx);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // eax : number of arguments
  // edx : the new target
  // edi : the target to call
  return RegisterArray(edi, edx, eax);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(edx); }

// static
constexpr auto CompareDescriptor::registers() {
  return RegisterArray(edx, eax);
}

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  return RegisterArray(edx, eax, ecx);
}

// static
constexpr auto BinaryOpDescriptor::registers() {
  return RegisterArray(edx, eax);
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  return RegisterArray(edx, eax, ecx);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  return RegisterArray(eax, edx, ecx);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return eax;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return ecx;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister() {
  return edx;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return edi;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return ecx;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return eax;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return edx;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return edi;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(eax,   // argument count
                       ecx,   // address of first argument
                       edi);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(eax,   // argument count
                       ecx);  // address of first argument
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(edi,   // the constructor
                       edx);  // the new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(eax,   // the value to pass to the generator
                       edx);  // the JSGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray();
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(edi);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32

#endif  // V8_CODEGEN_IA32_INTERFACE_DESCRIPTORS_IA32_INL_H_
