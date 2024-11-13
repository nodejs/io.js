// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_
#define V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_

#if V8_TARGET_ARCH_X64

#include "src/codegen/interface-descriptors.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(rax, rbx, rcx, rdx, rdi);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  auto registers =
      DoubleRegisterArray(xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6);
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
  if (nof_expected_args >= 1) DCHECK(allocatable_regs.has(kCArgRegs[0]));
  if (nof_expected_args >= 2) DCHECK(allocatable_regs.has(kCArgRegs[1]));
  if (nof_expected_args >= 3) DCHECK(allocatable_regs.has(kCArgRegs[2]));
  if (nof_expected_args >= 4) DCHECK(allocatable_regs.has(kCArgRegs[3]));
  // Additional arguments are passed on the stack.
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
#ifdef V8_TARGET_OS_WIN
  return RegisterArray(rdi, r8, rcx, rax, r9, rdx, rsi);
#else
  return RegisterArray(rdi, rbx, rdx, rcx, rax, rsi);
#endif  // V8_TARGET_OS_WIN
}

#ifdef V8_IS_TSAN
// static
constexpr auto TSANStoreDescriptor::registers() {
  return RegisterArray(kCArgRegs[0], kCArgRegs[1], kReturnRegister0);
}

// static
constexpr auto TSANLoadDescriptor::registers() {
  return RegisterArray(kCArgRegs[0], kReturnRegister0);
}
#endif  // V8_IS_TSAN

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return rdx; }
// static
constexpr Register LoadDescriptor::NameRegister() { return rcx; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return rax; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return rbx; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return rdx;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return rcx; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return rbx;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return rdi;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return r8;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return rcx;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return rdx; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return rcx; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return rbx;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return rdi;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return rdx; }
// static
constexpr Register StoreDescriptor::NameRegister() { return rcx; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return rax; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return rdi; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return rbx; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return r11; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return r11; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return rcx; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return rbx; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return rax; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return rbx; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return rbx;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() {
  return rcx;
}

// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::FlagsRegister() {
  return r8;
}
// static
constexpr Register MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::
    FeedbackVectorRegister() {
  return r9;
}
// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::TemporaryRegister() {
  return r11;
}

// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return rax; }

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(rax); }

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // rax : number of arguments
  // rdi : the target to call
  return RegisterArray(rdi, rax);
}
// static
constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // rdi : the source
  // rax : the excluded property count
  return RegisterArray(rdi, rax);
}

// static
constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // rdi : the source
  // rax : the excluded property count
  // rcx : the excluded property base
  return RegisterArray(rdi, rax, rcx);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // rax : number of arguments (on the stack)
  // rdi : the target to call
  // rcx : arguments list length (untagged)
  // rbx : arguments list (FixedArray)
  return RegisterArray(rdi, rax, rcx, rbx);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // rax : number of arguments
  // rcx : start index (to support rest parameters)
  // rdi : the target to call
  return RegisterArray(rdi, rax, rcx);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // rdx: the function template info
  // rcx: number of arguments (on the stack)
  return RegisterArray(rdx, rcx);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // rdx: the function template info
  // rcx: number of arguments (on the stack)
  // rdi: topmost script-having context
  return RegisterArray(rdx, rcx, rdi);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // rax : number of arguments (on the stack)
  // rdi : the target to call
  // rbx : the object to spread
  return RegisterArray(rdi, rax, rbx);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // rdi : the target to call
  // rbx : the arguments list
  return RegisterArray(rdi, rbx);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // rax : number of arguments (on the stack)
  // rdi : the target to call
  // rdx : the new target
  // rcx : arguments list length (untagged)
  // rbx : arguments list (FixedArray)
  return RegisterArray(rdi, rdx, rax, rcx, rbx);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // rax : number of arguments
  // rdx : the new target
  // rcx : start index (to support rest parameters)
  // rdi : the target to call
  return RegisterArray(rdi, rdx, rax, rcx);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // rax : number of arguments (on the stack)
  // rdi : the target to call
  // rdx : the new target
  // rbx : the object to spread
  return RegisterArray(rdi, rdx, rax, rbx);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // rdi : the target to call
  // rdx : the new target
  // rbx : the arguments list
  return RegisterArray(rdi, rdx, rbx);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // rax : number of arguments
  // rdx : the new target
  // rdi : the target to call
  return RegisterArray(rdi, rdx, rax);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(rdx); }

// static
constexpr auto CompareDescriptor::registers() {
  return RegisterArray(rdx, rax);
}

// static
constexpr auto BinaryOpDescriptor::registers() {
  return RegisterArray(rdx, rax);
}

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  return RegisterArray(rdx, rax, rbx);
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  return RegisterArray(rdx, rax, rbx);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  return RegisterArray(rax, rdx, rbx);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return rdx;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return rcx;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister() {
  return rbx;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return rdi;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return rcx;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return rbx;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return rdx;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return r8;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(rax,   // argument count
                       rbx,   // address of first argument
                       rdi);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(
      rax,   // argument count
      rcx,   // address of first argument
      rdi,   // constructor to call
      rdx,   // new target
      rbx);  // allocation site feedback if available, undefined otherwise
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(rdi,   // constructor to call
                       rdx);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(
      rax,   // the value to pass to the generator
      rdx);  // the JSGeneratorObject / JSAsyncGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(kCArgRegs[0], kCArgRegs[1]);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(rdi);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_X64

#endif  // V8_CODEGEN_X64_INTERFACE_DESCRIPTORS_X64_INL_H_
