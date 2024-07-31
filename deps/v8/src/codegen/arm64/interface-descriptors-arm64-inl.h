// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_
#define V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_

#if V8_TARGET_ARCH_ARM64

#include "src/base/template-utils.h"
#include "src/codegen/interface-descriptors.h"
#include "src/execution/frames.h"

namespace v8 {
namespace internal {

constexpr auto CallInterfaceDescriptor::DefaultRegisterArray() {
  auto registers = RegisterArray(x0, x1, x2, x3, x4);
  static_assert(registers.size() == kMaxBuiltinRegisterParams);
  return registers;
}

constexpr auto CallInterfaceDescriptor::DefaultDoubleRegisterArray() {
  auto registers = DoubleRegisterArray(d0, d1, d2, d3, d4, d5, d6);
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
  if (argc >= 1) DCHECK(allocatable_regs.has(x0));
  if (argc >= 2) DCHECK(allocatable_regs.has(x1));
  if (argc >= 3) DCHECK(allocatable_regs.has(x2));
  if (argc >= 4) DCHECK(allocatable_regs.has(x3));
  if (argc >= 5) DCHECK(allocatable_regs.has(x4));
  if (argc >= 6) DCHECK(allocatable_regs.has(x5));
  if (argc >= 7) DCHECK(allocatable_regs.has(x6));
  if (argc >= 8) DCHECK(allocatable_regs.has(x7));
}
#endif  // DEBUG

// static
constexpr auto WriteBarrierDescriptor::registers() {
  // TODO(leszeks): Remove x7 which is just there for padding.
  return RegisterArray(x1, x5, x4, x2, x0, x3, kContextRegister, x7);
}

// static
constexpr Register LoadDescriptor::ReceiverRegister() { return x1; }
// static
constexpr Register LoadDescriptor::NameRegister() { return x2; }
// static
constexpr Register LoadDescriptor::SlotRegister() { return x0; }

// static
constexpr Register LoadWithVectorDescriptor::VectorRegister() { return x3; }

// static
constexpr Register KeyedLoadBaselineDescriptor::ReceiverRegister() {
  return x1;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::NameRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedLoadBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedLoadWithVectorDescriptor::VectorRegister() {
  return x3;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::EnumIndexRegister() {
  return x4;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::CacheTypeRegister() {
  return x5;
}

// static
constexpr Register EnumeratedKeyedLoadBaselineDescriptor::SlotRegister() {
  return x2;
}

// static
constexpr Register KeyedHasICBaselineDescriptor::ReceiverRegister() {
  return kInterpreterAccumulatorRegister;
}
// static
constexpr Register KeyedHasICBaselineDescriptor::NameRegister() { return x1; }
// static
constexpr Register KeyedHasICBaselineDescriptor::SlotRegister() { return x2; }

// static
constexpr Register KeyedHasICWithVectorDescriptor::VectorRegister() {
  return x3;
}

// static
constexpr Register
LoadWithReceiverAndVectorDescriptor::LookupStartObjectRegister() {
  return x4;
}

// static
constexpr Register StoreDescriptor::ReceiverRegister() { return x1; }
// static
constexpr Register StoreDescriptor::NameRegister() { return x2; }
// static
constexpr Register StoreDescriptor::ValueRegister() { return x0; }
// static
constexpr Register StoreDescriptor::SlotRegister() { return x4; }

// static
constexpr Register StoreWithVectorDescriptor::VectorRegister() { return x3; }

// static
constexpr Register DefineKeyedOwnDescriptor::FlagsRegister() { return x5; }

// static
constexpr Register StoreTransitionDescriptor::MapRegister() { return x5; }

// static
constexpr Register ApiGetterDescriptor::HolderRegister() { return x0; }
// static
constexpr Register ApiGetterDescriptor::CallbackRegister() { return x3; }

// static
constexpr Register GrowArrayElementsDescriptor::ObjectRegister() { return x0; }
// static
constexpr Register GrowArrayElementsDescriptor::KeyRegister() { return x3; }

// static
constexpr Register BaselineLeaveFrameDescriptor::ParamsSizeRegister() {
  return x3;
}
// static
constexpr Register BaselineLeaveFrameDescriptor::WeightRegister() { return x4; }

// static
// static
constexpr Register TypeConversionDescriptor::ArgumentRegister() { return x0; }

// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::FlagsRegister() {
  return x8;
}
// static
constexpr Register MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::
    FeedbackVectorRegister() {
  return x9;
}
// static
constexpr Register
MaglevOptimizeCodeOrTailCallOptimizedCodeSlotDescriptor::TemporaryRegister() {
  return x4;
}

// static
constexpr auto TypeofDescriptor::registers() { return RegisterArray(x0); }

// static
constexpr auto CallTrampolineDescriptor::registers() {
  // x1: target
  // x0: number of arguments
  return RegisterArray(x1, x0);
}

constexpr auto CopyDataPropertiesWithExcludedPropertiesDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
  return RegisterArray(x1, x0);
}

constexpr auto
CopyDataPropertiesWithExcludedPropertiesOnStackDescriptor::registers() {
  // r1 : the source
  // r0 : the excluded property count
  // x2 : the excluded property base
  return RegisterArray(x1, x0, x2);
}

// static
constexpr auto CallVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
  return RegisterArray(x1, x0, x4, x2);
}

// static
constexpr auto CallForwardVarargsDescriptor::registers() {
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
  return RegisterArray(x1, x0, x2);
}

// static
constexpr auto CallFunctionTemplateDescriptor::registers() {
  // x1 : function template info
  // x2 : number of arguments (on the stack)
  return RegisterArray(x1, x2);
}

// static
constexpr auto CallFunctionTemplateGenericDescriptor::registers() {
  // x1 : function template info
  // x2 : number of arguments (on the stack)
  // x3 : topmost script-having context
  return RegisterArray(x1, x2, x3);
}

// static
constexpr auto CallWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x2 : the object to spread
  return RegisterArray(x1, x0, x2);
}

// static
constexpr auto CallWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x2 : the arguments list
  return RegisterArray(x1, x2);
}

// static
constexpr auto ConstructVarargsDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x4 : arguments list length (untagged)
  // x2 : arguments list (FixedArray)
  return RegisterArray(x1, x3, x0, x4, x2);
}

// static
constexpr auto ConstructForwardVarargsDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  // x2: start index (to supported rest parameters)
  return RegisterArray(x1, x3, x0, x2);
}

// static
constexpr auto ConstructWithSpreadDescriptor::registers() {
  // x0 : number of arguments (on the stack)
  // x1 : the target to call
  // x3 : the new target
  // x2 : the object to spread
  return RegisterArray(x1, x3, x0, x2);
}

// static
constexpr auto ConstructWithArrayLikeDescriptor::registers() {
  // x1 : the target to call
  // x3 : the new target
  // x2 : the arguments list
  return RegisterArray(x1, x3, x2);
}

// static
constexpr auto ConstructStubDescriptor::registers() {
  // x3: new target
  // x1: target
  // x0: number of arguments
  return RegisterArray(x1, x3, x0);
}

// static
constexpr auto AbortDescriptor::registers() { return RegisterArray(x1); }

// static
constexpr auto CompareDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  return RegisterArray(x1, x0);
}

// static
constexpr auto Compare_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
  return RegisterArray(x1, x0, x2);
}

// static
constexpr auto BinaryOpDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  return RegisterArray(x1, x0);
}

// static
constexpr auto BinaryOp_BaselineDescriptor::registers() {
  // x1: left operand
  // x0: right operand
  // x2: feedback slot
  return RegisterArray(x1, x0, x2);
}

// static
constexpr auto BinarySmiOp_BaselineDescriptor::registers() {
  // x0: left operand
  // x1: right operand
  // x2: feedback slot
  return RegisterArray(x0, x1, x2);
}

// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister() {
  return x1;
}
// static
constexpr Register
CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister() {
  return x2;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::CallDataRegister() {
  return x3;
}
// static
constexpr Register CallApiCallbackOptimizedDescriptor::HolderRegister() {
  return x0;
}

// static
constexpr Register
CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister() {
  return x2;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::TopmostScriptHavingContextRegister() {
  return x1;
}
// static
constexpr Register
CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister() {
  return x3;
}
// static
constexpr Register CallApiCallbackGenericDescriptor::HolderRegister() {
  return x0;
}

// static
constexpr auto InterpreterDispatchDescriptor::registers() {
  return RegisterArray(
      kInterpreterAccumulatorRegister, kInterpreterBytecodeOffsetRegister,
      kInterpreterBytecodeArrayRegister, kInterpreterDispatchTableRegister);
}

// static
constexpr auto InterpreterPushArgsThenCallDescriptor::registers() {
  return RegisterArray(x0,   // argument count
                       x2,   // address of first argument
                       x1);  // the target callable to be call
}

// static
constexpr auto InterpreterPushArgsThenConstructDescriptor::registers() {
  return RegisterArray(
      x0,   // argument count
      x4,   // address of the first argument
      x1,   // constructor to call
      x3,   // new target
      x2);  // allocation site feedback if available, undefined otherwise
}

// static
constexpr auto ConstructForwardAllArgsDescriptor::registers() {
  return RegisterArray(x1,   // constructor to call
                       x3);  // new target
}

// static
constexpr auto ResumeGeneratorDescriptor::registers() {
  return RegisterArray(x0,   // the value to pass to the generator
                       x1);  // the JSGeneratorObject to resume
}

// static
constexpr auto RunMicrotasksEntryDescriptor::registers() {
  return RegisterArray(x0, x1);
}

constexpr auto WasmJSToWasmWrapperDescriptor::registers() {
  // Arbitrarily picked register.
  return RegisterArray(x8);
}

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM64

#endif  // V8_CODEGEN_ARM64_INTERFACE_DESCRIPTORS_ARM64_INL_H_
