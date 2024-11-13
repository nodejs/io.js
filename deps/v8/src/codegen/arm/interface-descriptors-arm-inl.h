// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM_INTERFACE_DESCRIPTORS_ARM_INL_H_
#define V8_CODEGEN_ARM_INTERFACE_DESCRIPTORS_ARM_INL_H_

#if V8_TARGET_ARCH_ARM

#include "src/codegen/interface-descriptors.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(r0, r1, r2, r3, r4);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  // Construct the std::array explicitly here because on arm, the registers d0,
  // d1, ... are not of type DoubleRegister but only support implicit casting to
  // DoubleRegister. For template resolution, however, implicit casting is not
  // sufficient.
  std::array<DoubleRegister, 7> registers{d0, d1, d2, d3, d4, d5, d6};
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnRegisterArray() {
  auto registers =
      RegisterArray(kReturnRegister0, kReturnRegister1, kReturnRegister2);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultReturnDoubleRegisterArray() {
  // Construct the std::array explicitly here because on arm, the registers d0,
  // d1, ... are not of type DoubleRegister but only support implicit casting to
  // DoubleRegister. For template resolution, however, implicit casting is not
  // sufficient.
  // Padding to have as many double return registers as GP return registers.
  std::array<DoubleRegister, 3> registers{kFPReturnRegister0, no_dreg, no_dreg};
  return registers;
}

#if DEBUG
template <typename DerivedDescriptor>
void StaticCallInterfaceDescriptor<DerivedDescriptor>::
    VerifyArgumentRegisterCount(CallInterfaceDescriptorData* data, int argc) {
  RegList allocatable_regs = data->allocatable_registers();
  if (argc >= 1) DCHECK(allocatable_regs.has(r0));
  if (argc >= 2) DCHECK(allocatable_regs.has(r1));
  if (argc >= 3) DCHECK(allocatable_regs.has(r2));
  if (argc >= 4) DCHECK(allocatable_regs.has(r3));
  if (argc >= 5) DCHECK(allocatable_regs.has(r4));
  if (argc >= 6) DCHECK(allocatable_regs.has(r5));
  if (argc >= 7) DCHECK(allocatable_regs.has(r6));
  if (argc >= 8) DCHECK(allocatable_regs.has(r7));
  // Additional arguments are passed on the stack.
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  return RegisterArray(r1, r5, r4, r2, r0, r3, kContextRegister);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return r1; }
// static
constexpr Register LoadDescriptor::NameRegister() { return r2; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return r0; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return r3; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return r1;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return r2; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return r3;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return r4;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return r5;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return r2;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return r1; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return r2; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return r3;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return r4;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return r1; }
// static
constexpr Register StoreDescriptor::NameRegister() { return r2; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return r0; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return r4; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return r3; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return r5; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return r5; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return r0; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return r3; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return r0; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return r3; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return r3;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() { return r4; }

// static
// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return r0; }

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(r0); }

// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::FlagsRegister() {
  return r2;
}
// static
constexpr Register MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::
    FeedbackVectorRegister() {
  return r5;
}
// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::TemporaryRegister() {
  return r4;
}

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // r0 : number of arguments
  // r1 : the target to call
  return RegisterArray(r1, r0);
}

// static
constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // r0 : the source
  // r1 : the excluded property count
  return RegisterArray(r1, r0);
}

// static
constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // r0 : the source
  // r1 : the excluded property count
  // r2 : the excluded property base
  return RegisterArray(r1, r0, r2);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // r0 : number of arguments (on the stack)
  // r1 : the target to call
  // r4 : arguments list length (untagged)
  // r2 : arguments list (FixedArray)
  return RegisterArray(r1, r0, r4, r2);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // r0 : number of arguments
  // r2 : start index (to support rest parameters)
  // r1 : the target to call
  return RegisterArray(r1, r0, r2);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // r1 : function template info
  // r2 : number of arguments (on the stack)
  return RegisterArray(r1, r2);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // r1 : function template info
  // r2 : number of arguments (on the stack)
  // r3 : topmost script-having context
  return RegisterArray(r1, r2, r3);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // r0 : number of arguments (on the stack)
  // r1 : the target to call
  // r2 : the object to spread
  return RegisterArray(r1, r0, r2);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // r1 : the target to call
  // r2 : the arguments list
  return RegisterArray(r1, r2);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // r0 : number of arguments (on the stack)
  // r1 : the target to call
  // r3 : the new target
  // r4 : arguments list length (untagged)
  // r2 : arguments list (FixedArray)
  return RegisterArray(r1, r3, r0, r4, r2);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // r0 : number of arguments
  // r3 : the new target
  // r2 : start index (to support rest parameters)
  // r1 : the target to call
  return RegisterArray(r1, r3, r0, r2);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // r0 : number of arguments (on the stack)
  // r1 : the target to call
  // r3 : the new target
  // r2 : the object to spread
  return RegisterArray(r1, r3, r0, r2);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // r1 : the target to call
  // r3 : the new target
  // r2 : the arguments list
  return RegisterArray(r1, r3, r2);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // r0 : number of arguments
  // r1 : the target to call
  // r3 : the new target
  return RegisterArray(r1, r3, r0);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(r1); }

// static
constexpr auto CompareDescriptor::registers() { return RegisterArray(r1, r0); }

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  // r1: left operand
  // r0: right operand
  // r2: feedback slot
  return RegisterArray(r1, r0, r2);
}

// static
constexpr auto BinaryOpDescriptor::registers() { return RegisterArray(r1, r0); }

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  // r1: left operand
  // r0: right operand
  // r2: feedback slot
  return RegisterArray(r1, r0, r2);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  // r0: left operand
  // r1: right operand
  // r2: feedback slot
  return RegisterArray(r0, r1, r2);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return r1;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return r2;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister() {
  return r3;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return r0;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return r2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return r1;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return r3;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return r0;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(r0,   // argument count
                       r2,   // address of first argument
                       r1);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(
      r0,   // argument count
      r4,   // address of the first argument
      r1,   // constructor to call
      r3,   // new target
      r2);  // allocation site feedback if available, undefined otherwise
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(r1,   // constructor to call
                       r3);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(r0,   // the value to pass to the generator
                       r1);  // the JSGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(r0, r1);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(r8);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM

#endif  // V8_CODEGEN_ARM_INTERFACE_DESCRIPTORS_ARM_INL_H_
