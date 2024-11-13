// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_S390_INTERFACE_DESCRIPTORS_S390_INL_H_
#define V8_CODEGEN_S390_INTERFACE_DESCRIPTORS_S390_INL_H_

#if V8_TARGET_ARCH_S390

#include "src/codegen/interface-descriptors.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(r2, r3, r4, r5, r6);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  auto registers = DoubleRegisterArray(d1, d2, d3, d4, d5, d6, d7);
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
  if (argc >= 1) DCHECK(allocatable_regs.has(r2));
  if (argc >= 2) DCHECK(allocatable_regs.has(r3));
  if (argc >= 3) DCHECK(allocatable_regs.has(r4));
  if (argc >= 4) DCHECK(allocatable_regs.has(r5));
  if (argc >= 5) DCHECK(allocatable_regs.has(r6));
  if (argc >= 6) DCHECK(allocatable_regs.has(r7));
  if (argc >= 7) DCHECK(allocatable_regs.has(r8));
  if (argc >= 8) DCHECK(allocatable_regs.has(r9));
  // Additional arguments are passed on the stack.
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  return RegisterArray(r3, r7, r6, r4, r2, r5, kContextRegister);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return r3; }
// static
constexpr Register LoadDescriptor::NameRegister() { return r4; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return r2; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return r5; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return r3;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return r4; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return r5;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return r6;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return r7;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return r4;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return r3; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return r4; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return r5;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return r6;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return r3; }
// static
constexpr Register StoreDescriptor::NameRegister() { return r4; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return r2; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return r6; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return r5; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return r7; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return r7; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return r2; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return r5; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return r2; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return r5; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  // TODO(v8:11421): Implement on this platform.
  return r5;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() {
  // TODO(v8:11421): Implement on this platform.
  return r6;
}

// static
// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return r2; }

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(r2); }

// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::FlagsRegister() {
  return r4;
}
// static
constexpr Register MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::
    FeedbackVectorRegister() {
  return r7;
}
// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::TemporaryRegister() {
  return r6;
}

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // r2 : number of arguments
  // r3 : the target to call
  return RegisterArray(r3, r2);
}

// static
constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // r3 : the source
  // r2 : the excluded property count
  return RegisterArray(r3, r2);
}

// static
constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // r3 : the source
  // r2 : the excluded property count
  // r4 : the excluded property base
  return RegisterArray(r3, r2, r4);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // r2 : number of arguments (on the stack)
  // r3 : the target to call
  // r6 : arguments list length (untagged)
  // r4 : arguments list (FixedArray)
  return RegisterArray(r3, r2, r6, r4);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // r2 : number of arguments
  // r4 : start index (to support rest parameters)
  // r3 : the target to call
  return RegisterArray(r3, r2, r4);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // r3 : function template info
  // r4 : number of arguments (on the stack)
  return RegisterArray(r3, r4);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // r3 : function template info
  // r4 : number of arguments (on the stack)
  // r5 : topmost script-having context
  return RegisterArray(r3, r4, r5);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // r2: number of arguments (on the stack)
  // r3 : the target to call
  // r4 : the object to spread
  return RegisterArray(r3, r2, r4);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // r3 : the target to call
  // r4 : the arguments list
  return RegisterArray(r3, r4);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // r2 : number of arguments (on the stack)
  // r3 : the target to call
  // r5 : the new target
  // r6 : arguments list length (untagged)
  // r4 : arguments list (FixedArray)
  return RegisterArray(r3, r5, r2, r6, r4);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // r2 : number of arguments
  // r5 : the new target
  // r4 : start index (to support rest parameters)
  // r3 : the target to call
  return RegisterArray(r3, r5, r2, r4);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // r2 : number of arguments (on the stack)
  // r3 : the target to call
  // r5 : the new target
  // r4 : the object to spread
  return RegisterArray(r3, r5, r2, r4);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // r3 : the target to call
  // r5 : the new target
  // r4 : the arguments list
  return RegisterArray(r3, r5, r4);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // r2 : number of arguments
  // r3 : the target to call
  // r5 : the new target
  return RegisterArray(r3, r5, r2);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(r3); }

// static
constexpr auto CompareDescriptor::registers() { return RegisterArray(r3, r2); }

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  return RegisterArray(r3, r2, r4);
}

// static
constexpr auto BinaryOpDescriptor::registers() { return RegisterArray(r3, r2); }

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  return RegisterArray(r3, r2, r4);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  return RegisterArray(r2, r3, r4);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return r3;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return r4;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister() {
  return r5;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return r2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return r4;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return r3;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return r5;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return r2;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(r2,   // argument count
                       r4,   // address of first argument
                       r3);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(
      r2,   // argument count
      r6,   // address of the first argument
      r3,   // constructor to call
      r5,   // new target
      r4);  // allocation site feedback if available, undefined otherwise
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(r3,   // constructor to call
                       r5);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(r2,   // the value to pass to the generator
                       r3);  // the JSGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(r2, r3);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(r8);
}
}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_S390

#endif  // V8_CODEGEN_S390_INTERFACE_DESCRIPTORS_S390_INL_H_
