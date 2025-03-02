// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/compiler/linkage.h"

#include "src/builtins/builtins-descriptors.h"
#include "src/codegen/assembler-inl.h"
#include "src/codegen/macro-assembler.h"
#include "src/codegen/optimized-compilation-info.h"
#include "src/compiler/frame.h"
#include "src/compiler/globals.h"
#include "src/compiler/osr.h"
#include "src/compiler/pipeline.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/wasm-compiler-definitions.h"
#endif

namespace v8 {
namespace internal {
namespace compiler {

namespace {

// Offsets from callee to caller frame, in slots.
constexpr int kFirstCallerSlotOffset = 1;
constexpr int kNoCallerSlotOffset = 0;

inline LinkageLocation regloc(Register reg, MachineType type) {
  return LinkageLocation::ForRegister(reg.code(), type);
}

inline LinkageLocation regloc(DoubleRegister reg, MachineType type) {
  return LinkageLocation::ForRegister(reg.code(), type);
}

}  // namespace


std::ostream& operator<<(std::ostream& os, const CallDescriptor::Kind& k) {
  switch (k) {
    case CallDescriptor::kCallCodeObject:
      os << "Code";
      break;
    case CallDescriptor::kCallJSFunction:
      os << "JS";
      break;
    case CallDescriptor::kCallAddress:
      os << "Addr";
      break;
#if V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallWasmCapiFunction:
      os << "WasmExit";
      break;
    case CallDescriptor::kCallWasmFunction:
      os << "WasmFunction";
      break;
    case CallDescriptor::kCallWasmFunctionIndirect:
      os << "WasmFunctionIndirect";
      break;
    case CallDescriptor::kCallWasmImportWrapper:
      os << "WasmImportWrapper";
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case CallDescriptor::kCallBuiltinPointer:
      os << "BuiltinPointer";
      break;
  }
  return os;
}


std::ostream& operator<<(std::ostream& os, const CallDescriptor& d) {
  // TODO(svenpanne) Output properties etc. and be less cryptic.
  return os << d.kind() << ":" << d.debug_name() << ":r" << d.ReturnCount()
            << "s" << d.ParameterSlotCount() << "i" << d.InputCount() << "f"
            << d.FrameStateCount();
}

MachineSignature* CallDescriptor::GetMachineSignature(Zone* zone) const {
  size_t param_count = ParameterCount();
  size_t return_count = ReturnCount();
  MachineType* types =
      zone->AllocateArray<MachineType>(param_count + return_count);
  int current = 0;
  for (size_t i = 0; i < return_count; ++i) {
    types[current++] = GetReturnType(i);
  }
  for (size_t i = 0; i < param_count; ++i) {
    types[current++] = GetParameterType(i);
  }
  return zone->New<MachineSignature>(return_count, param_count, types);
}

int CallDescriptor::GetStackParameterDelta(
    CallDescriptor const* tail_caller) const {
  // In the IsTailCallForTierUp case, the callee has
  // identical linkage and runtime arguments to the caller, thus the stack
  // parameter delta is 0. We don't explicitly pass the runtime arguments as
  // inputs to the TailCall node, since they already exist on the stack.
  if (IsTailCallForTierUp()) return 0;

  // Add padding if necessary before computing the stack parameter delta.
  int callee_slots_above_sp = AddArgumentPaddingSlots(GetOffsetToReturns());
  int tail_caller_slots_above_sp =
      AddArgumentPaddingSlots(tail_caller->GetOffsetToReturns());
  int stack_param_delta = callee_slots_above_sp - tail_caller_slots_above_sp;
  DCHECK(!ShouldPadArguments(stack_param_delta));
  return stack_param_delta;
}

int CallDescriptor::GetOffsetToFirstUnusedStackSlot() const {
  int offset = kFirstCallerSlotOffset;
  for (size_t i = 0; i < InputCount(); ++i) {
    LinkageLocation operand = GetInputLocation(i);
    if (!operand.IsRegister()) {
      DCHECK(operand.IsCallerFrameSlot());
      int slot_offset = -operand.GetLocation();
      offset = std::max(offset, slot_offset + operand.GetSizeInPointers());
    }
  }
  return offset;
}

int CallDescriptor::GetOffsetToReturns() const {
  // Find the return slot with the least offset relative to the callee.
  int offset = kNoCallerSlotOffset;
  for (size_t i = 0; i < ReturnCount(); ++i) {
    LinkageLocation operand = GetReturnLocation(i);
    if (!operand.IsRegister()) {
      DCHECK(operand.IsCallerFrameSlot());
      int slot_offset = -operand.GetLocation();
      offset = std::min(offset, slot_offset);
    }
  }
  // If there was a return slot, return the offset minus 1 slot.
  if (offset != kNoCallerSlotOffset) {
    return offset - 1;
  }

  // Otherwise, return the first slot after the parameters area, including
  // optional padding slots.
  int last_argument_slot = GetOffsetToFirstUnusedStackSlot() - 1;
  offset = AddArgumentPaddingSlots(last_argument_slot);

  DCHECK_IMPLIES(offset == 0, ParameterSlotCount() == 0);
  return offset;
}

uint32_t CallDescriptor::GetTaggedParameterSlots() const {
  uint32_t count = 0;
  uint32_t untagged_count = 0;
  uint32_t first_offset = kMaxInt;
  for (size_t i = 0; i < InputCount(); ++i) {
    LinkageLocation operand = GetInputLocation(i);
    if (!operand.IsRegister()) {
      if (operand.GetType().IsTagged()) {
        ++count;
        // Caller frame slots have negative indices and start at -1. Flip it
        // back to a positive offset (to be added to the frame's SP to find the
        // slot).
        int slot_offset = -operand.GetLocation() - 1;
        DCHECK_GE(slot_offset, 0);
        first_offset =
            std::min(first_offset, static_cast<uint32_t>(slot_offset));
      } else {
        untagged_count += operand.GetSizeInPointers();
      }
    }
  }
  if (count == 0) {
    // If we don't have any tagged parameter slots, still initialize the offset
    // to point past the untagged parameter slots, so that
    // offset + count == stack slot count.
    first_offset = untagged_count;
  }
  DCHECK(first_offset != kMaxInt);
  return (first_offset << 16) | (count & 0xFFFFu);
}

bool CallDescriptor::CanTailCall(const CallDescriptor* callee) const {
  if (ReturnCount() != callee->ReturnCount()) return false;
  const int stack_returns_delta =
      GetOffsetToReturns() - callee->GetOffsetToReturns();
  for (size_t i = 0; i < ReturnCount(); ++i) {
    if (GetReturnLocation(i).IsCallerFrameSlot() &&
        callee->GetReturnLocation(i).IsCallerFrameSlot()) {
      if (GetReturnLocation(i).AsCallerFrameSlot() + stack_returns_delta !=
          callee->GetReturnLocation(i).AsCallerFrameSlot()) {
        return false;
      }
    } else if (!LinkageLocation::IsSameLocation(GetReturnLocation(i),
                                                callee->GetReturnLocation(i))) {
      return false;
    }
  }
  return true;
}

// TODO(jkummerow, sigurds): Arguably frame size calculation should be
// keyed on code/frame type, not on CallDescriptor kind. Think about a
// good way to organize this logic.
int CallDescriptor::CalculateFixedFrameSize(CodeKind code_kind) const {
  switch (kind_) {
    case kCallJSFunction:
      return StandardFrameConstants::kFixedSlotCount;
    case kCallAddress:
#if V8_ENABLE_WEBASSEMBLY
      if (code_kind == CodeKind::C_WASM_ENTRY) {
        return CWasmEntryFrameConstants::kFixedSlotCount;
      }
#endif  // V8_ENABLE_WEBASSEMBLY
      return CommonFrameConstants::kFixedSlotCountAboveFp +
             CommonFrameConstants::kCPSlotCount;
    case kCallCodeObject:
    case kCallBuiltinPointer:
      return TypedFrameConstants::kFixedSlotCount;
#if V8_ENABLE_WEBASSEMBLY
    case kCallWasmFunction:
    case kCallWasmFunctionIndirect:
    case kCallWasmImportWrapper:
      return WasmFrameConstants::kFixedSlotCount;
    case kCallWasmCapiFunction:
      return WasmExitFrameConstants::kFixedSlotCount;
#endif  // V8_ENABLE_WEBASSEMBLY
  }
  UNREACHABLE();
}

uint64_t CallDescriptor::signature_hash() const {
#if V8_ENABLE_WEBASSEMBLY
  DCHECK_EQ(kind_, kCallWasmFunctionIndirect);
#endif
  return signature_hash_;
}

EncodedCSignature CallDescriptor::ToEncodedCSignature() const {
  int parameter_count = static_cast<int>(ParameterCount());
  EncodedCSignature sig(parameter_count);
  CHECK_LT(parameter_count, EncodedCSignature::kInvalidParamCount);

  for (int i = 0; i < parameter_count; ++i) {
    if (IsFloatingPoint(GetParameterType(i).representation())) {
      sig.SetFloat(i);
    }
  }
  if (ReturnCount() > 0) {
    DCHECK_EQ(1, ReturnCount());
    if (IsFloatingPoint(GetReturnType(0).representation())) {
      if (GetReturnType(0).representation() ==
          MachineRepresentation::kFloat64) {
        sig.SetReturnFloat64();
      } else {
        sig.SetReturnFloat32();
      }
    }
  }
  return sig;
}

void CallDescriptor::ComputeParamCounts() const {
  gp_param_count_ = 0;
  fp_param_count_ = 0;
  for (size_t i = 0; i < ParameterCount(); ++i) {
    if (IsFloatingPoint(GetParameterType(i).representation())) {
      ++fp_param_count_.value();
    } else {
      ++gp_param_count_.value();
    }
  }
}

#if V8_ENABLE_WEBASSEMBLY
namespace {
CallDescriptor* ReplaceTypeInCallDescriptorWith(
    Zone* zone, const CallDescriptor* call_descriptor, size_t num_replacements,
    MachineType from, MachineType to) {
  // The last parameter may be the special callable parameter. In that case we
  // have to preserve it as the last parameter, i.e. we allocate it in the new
  // location signature again in the same register.
  bool extra_callable_param =
      (call_descriptor->GetInputLocation(call_descriptor->InputCount() - 1) ==
       LinkageLocation::ForRegister(kJSFunctionRegister.code(),
                                    MachineType::TaggedPointer()));

  size_t return_count = call_descriptor->ReturnCount();
  // To recover the function parameter count, disregard the instance parameter,
  // and the extra callable parameter if present.
  size_t parameter_count =
      call_descriptor->ParameterCount() - (extra_callable_param ? 2 : 1);

  // Precompute if the descriptor contains {from}.
  bool needs_change = false;
  for (size_t i = 0; !needs_change && i < return_count; i++) {
    needs_change = call_descriptor->GetReturnType(i) == from;
  }
  for (size_t i = 1; !needs_change && i < parameter_count + 1; i++) {
    needs_change = call_descriptor->GetParameterType(i) == from;
  }
  if (!needs_change) return const_cast<CallDescriptor*>(call_descriptor);

  std::vector<MachineType> reps;

  for (size_t i = 0, limit = return_count; i < limit; i++) {
    MachineType initial_type = call_descriptor->GetReturnType(i);
    if (initial_type == from) {
      for (size_t j = 0; j < num_replacements; j++) reps.push_back(to);
      return_count += num_replacements - 1;
    } else {
      reps.push_back(initial_type);
    }
  }

  // Disregard the instance (first) parameter.
  for (size_t i = 1, limit = parameter_count + 1; i < limit; i++) {
    MachineType initial_type = call_descriptor->GetParameterType(i);
    if (initial_type == from) {
      for (size_t j = 0; j < num_replacements; j++) reps.push_back(to);
      parameter_count += num_replacements - 1;
    } else {
      reps.push_back(initial_type);
    }
  }

  MachineSignature sig(return_count, parameter_count, reps.data());

  int parameter_slots;
  int return_slots;
  LocationSignature* location_sig = BuildLocations(
      zone, &sig, extra_callable_param, &parameter_slots, &return_slots);

  return zone->New<CallDescriptor>(               // --
      call_descriptor->kind(),                    // kind
      call_descriptor->tag(),                     // tag
      call_descriptor->GetInputType(0),           // target MachineType
      call_descriptor->GetInputLocation(0),       // target location
      location_sig,                               // location_sig
      parameter_slots,                            // parameter slot count
      call_descriptor->properties(),              // properties
      call_descriptor->CalleeSavedRegisters(),    // callee-saved registers
      call_descriptor->CalleeSavedFPRegisters(),  // callee-saved fp regs
      call_descriptor->flags(),                   // flags
      call_descriptor->debug_name(),              // debug name
      call_descriptor->GetStackArgumentOrder(),   // stack order
      call_descriptor->AllocatableRegisters(),    // allocatable registers
      return_slots,                               // return slot count
      call_descriptor->IsIndirectWasmFunctionCall()
          ? call_descriptor->signature_hash()
          : kInvalidWasmSignatureHash);  // signature hash
}
}  // namespace

CallDescriptor* GetI32WasmCallDescriptor(
    Zone* zone, const CallDescriptor* call_descriptor) {
  return ReplaceTypeInCallDescriptorWith(
      zone, call_descriptor, 2, MachineType::Int64(), MachineType::Int32());
}
#endif

CallDescriptor* Linkage::ComputeIncoming(Zone* zone,
                                         OptimizedCompilationInfo* info) {
#if V8_ENABLE_WEBASSEMBLY
  DCHECK(info->IsOptimizing() || info->IsWasm());
#else
  DCHECK(info->IsOptimizing());
#endif  // V8_ENABLE_WEBASSEMBLY
  if (!info->closure().is_null()) {
    // If we are compiling a JS function, use a JS call descriptor,
    // plus the receiver.
    DCHECK(info->has_bytecode_array());
    DCHECK_EQ(info->closure()
                  ->shared()
                  ->internal_formal_parameter_count_with_receiver(),
              info->bytecode_array()->parameter_count());
    return GetJSCallDescriptor(zone, info->is_osr(),
                               info->bytecode_array()->parameter_count(),
                               CallDescriptor::kCanUseRoots);
  }
  return nullptr;  // TODO(titzer): ?
}


// static
bool Linkage::NeedsFrameStateInput(Runtime::FunctionId function) {
  switch (function) {
    // Most runtime functions need a FrameState. A few chosen ones that we know
    // not to call into arbitrary JavaScript, not to throw, and not to lazily
    // deoptimize are allowlisted here and can be called without a FrameState.
    case Runtime::kAbort:
    case Runtime::kAllocateInOldGeneration:
    case Runtime::kCreateIterResultObject:
    case Runtime::kGrowableSharedArrayBufferByteLength:
    case Runtime::kIncBlockCounter:
    case Runtime::kNewClosure:
    case Runtime::kNewClosure_Tenured:
    case Runtime::kNewFunctionContext:
    case Runtime::kPushBlockContext:
    case Runtime::kPushCatchContext:
    case Runtime::kStringEqual:
    case Runtime::kStringLessThan:
    case Runtime::kStringLessThanOrEqual:
    case Runtime::kStringGreaterThan:
    case Runtime::kStringGreaterThanOrEqual:
    case Runtime::kToFastProperties:  // TODO(conradw): Is it safe?
    case Runtime::kTraceEnter:
    case Runtime::kTraceExit:
      return false;

    // Some inline intrinsics are also safe to call without a FrameState.
    case Runtime::kInlineCreateIterResultObject:
    case Runtime::kInlineIncBlockCounter:
    case Runtime::kInlineGeneratorClose:
    case Runtime::kInlineGeneratorGetResumeMode:
    case Runtime::kInlineCreateJSGeneratorObject:
      return false;

    default:
      break;
  }

  // For safety, default to needing a FrameState unless allowlisted.
  return true;
}

CallDescriptor* Linkage::GetRuntimeCallDescriptor(
    Zone* zone, Runtime::FunctionId function_id, int js_parameter_count,
    Operator::Properties properties, CallDescriptor::Flags flags,
    LazyDeoptOnThrow lazy_deopt_on_throw) {
  const Runtime::Function* function = Runtime::FunctionForId(function_id);
  const int return_count = function->result_size;
  const char* debug_name = function->name;

  if (lazy_deopt_on_throw == LazyDeoptOnThrow::kNo &&
      !Linkage::NeedsFrameStateInput(function_id)) {
    flags = static_cast<CallDescriptor::Flags>(
        flags & ~CallDescriptor::kNeedsFrameState);
  }

  DCHECK_IMPLIES(lazy_deopt_on_throw == LazyDeoptOnThrow::kYes,
                 flags & CallDescriptor::kNeedsFrameState);

  return GetCEntryStubCallDescriptor(zone, return_count, js_parameter_count,
                                     debug_name, properties, flags);
}

CallDescriptor* Linkage::GetCEntryStubCallDescriptor(
    Zone* zone, int return_count, int js_parameter_count,
    const char* debug_name, Operator::Properties properties,
    CallDescriptor::Flags flags, StackArgumentOrder stack_order) {
  const size_t function_count = 1;
  const size_t num_args_count = 1;
  const size_t context_count = 1;
  const size_t parameter_count = function_count +
                                 static_cast<size_t>(js_parameter_count) +
                                 num_args_count + context_count;

  LocationSignature::Builder locations(zone, static_cast<size_t>(return_count),
                                       static_cast<size_t>(parameter_count));

  // Add returns.
  if (locations.return_count_ > 0) {
    locations.AddReturn(regloc(kReturnRegister0, MachineType::AnyTagged()));
  }
  if (locations.return_count_ > 1) {
    locations.AddReturn(regloc(kReturnRegister1, MachineType::AnyTagged()));
  }
  if (locations.return_count_ > 2) {
    locations.AddReturn(regloc(kReturnRegister2, MachineType::AnyTagged()));
  }

  // All parameters to the runtime call go on the stack.
  for (int i = 0; i < js_parameter_count; i++) {
    locations.AddParam(LinkageLocation::ForCallerFrameSlot(
        i - js_parameter_count, MachineType::AnyTagged()));
  }
  // Add runtime function itself.
  locations.AddParam(
      regloc(kRuntimeCallFunctionRegister, MachineType::Pointer()));

  // Add runtime call argument count.
  locations.AddParam(
      regloc(kRuntimeCallArgCountRegister, MachineType::Int32()));

  // Add context.
  locations.AddParam(regloc(kContextRegister, MachineType::AnyTagged()));

  // The target for runtime calls is a code object.
  MachineType target_type = MachineType::AnyTagged();
  LinkageLocation target_loc =
      LinkageLocation::ForAnyRegister(MachineType::AnyTagged());
  return zone->New<CallDescriptor>(     // --
      CallDescriptor::kCallCodeObject,  // kind
      kDefaultCodeEntrypointTag,        // tag
      target_type,                      // target MachineType
      target_loc,                       // target location
      locations.Get(),                  // location_sig
      js_parameter_count,               // stack_parameter_count
      properties,                       // properties
      kNoCalleeSaved,                   // callee-saved
      kNoCalleeSavedFp,                 // callee-saved fp
      flags,                            // flags
      debug_name,                       // debug name
      stack_order);                     // stack order
}

CallDescriptor* Linkage::GetJSCallDescriptor(Zone* zone, bool is_osr,
                                             int js_parameter_count,
                                             CallDescriptor::Flags flags,
                                             Operator::Properties properties) {
  const size_t return_count = 1;
  const size_t context_count = 1;
  const size_t new_target_count = 1;
  const size_t num_args_count = 1;
  const size_t dispatch_handle_count =
      V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE_BOOL ? 1 : 0;
  const size_t parameter_count = js_parameter_count + new_target_count +
                                 num_args_count + dispatch_handle_count +
                                 context_count;

  // The JSCallDescriptor must be compatible both with the interface descriptor
  // of JS builtins and with the general JS calling convention (as defined by
  // the JSTrampolineDescriptor). The JS builtin descriptors are already
  // statically asserted to be compatible with the JS calling convention, so
  // here we just ensure compatibility with the JS builtin descriptors.
  DCHECK_EQ(parameter_count, kJSBuiltinBaseParameterCount + js_parameter_count);

  LocationSignature::Builder locations(zone, return_count, parameter_count);

  // All JS calls have exactly one return value.
  locations.AddReturn(regloc(kReturnRegister0, MachineType::AnyTagged()));

  // All parameters to JS calls go on the stack.
  for (int i = 0; i < js_parameter_count; i++) {
    int spill_slot_index = -i - 1;
    locations.AddParam(LinkageLocation::ForCallerFrameSlot(
        spill_slot_index, MachineType::AnyTagged()));
  }

  // Add JavaScript call new target value.
  locations.AddParam(
      regloc(kJavaScriptCallNewTargetRegister, MachineType::AnyTagged()));

  // Add JavaScript call argument count.
  locations.AddParam(
      regloc(kJavaScriptCallArgCountRegister, MachineType::Int32()));

#ifdef V8_JS_LINKAGE_INCLUDES_DISPATCH_HANDLE
  // Add dispatch handle.
  locations.AddParam(
      regloc(kJavaScriptCallDispatchHandleRegister, MachineType::Int32()));
#endif

  // Add context.
  locations.AddParam(regloc(kContextRegister, MachineType::AnyTagged()));

  // The target for JS function calls is the JSFunction object.
  MachineType target_type = MachineType::AnyTagged();
  // When entering into an OSR function from unoptimized code the JSFunction
  // is not in a register, but it is on the stack in the marker spill slot.
  // For kind == JSDescKind::kBuiltin, we should still use the regular
  // kJSFunctionRegister, so that frame attribution for stack traces works.
  LinkageLocation target_loc = is_osr
                                   ? LinkageLocation::ForSavedCallerFunction()
                                   : regloc(kJSFunctionRegister, target_type);
  CallDescriptor::Kind descriptor_kind = CallDescriptor::kCallJSFunction;
  return zone->New<CallDescriptor>(  // --
      descriptor_kind,               // kind
      kJSEntrypointTag,              // tag
      target_type,                   // target MachineType
      target_loc,                    // target location
      locations.Get(),               // location_sig
      js_parameter_count,            // stack_parameter_count
      properties,                    // properties
      kNoCalleeSaved,                // callee-saved
      kNoCalleeSavedFp,              // callee-saved fp
      flags,                         // flags
      "js-call");                    // debug name
}

// TODO(turbofan): cache call descriptors for code stub calls.
// TODO(jgruber): Clean up stack parameter count handling. The descriptor
// already knows the formal stack parameter count and ideally only additional
// stack parameters should be passed into this method. All call-sites should
// be audited for correctness (e.g. many used to assume a stack parameter count
// of 0).
CallDescriptor* Linkage::GetStubCallDescriptor(
    Zone* zone, const CallInterfaceDescriptor& descriptor,
    int stack_parameter_count, CallDescriptor::Flags flags,
    Operator::Properties properties, StubCallMode stub_mode) {
  const int register_parameter_count = descriptor.GetRegisterParameterCount();
  const int js_parameter_count =
      register_parameter_count + stack_parameter_count;
  const int context_count = descriptor.HasContextParameter() ? 1 : 0;
  const size_t parameter_count =
      static_cast<size_t>(js_parameter_count + context_count);

  DCHECK_GE(stack_parameter_count, descriptor.GetStackParameterCount());

  int return_count = descriptor.GetReturnCount();
  LocationSignature::Builder locations(zone, return_count, parameter_count);

  // Add returns.
  for (int i = 0; i < return_count; i++) {
    MachineType type = descriptor.GetReturnType(static_cast<int>(i));
    if (IsFloatingPoint(type.representation())) {
      DoubleRegister reg = descriptor.GetDoubleRegisterReturn(i);
      locations.AddReturn(regloc(reg, type));
    } else {
      Register reg = descriptor.GetRegisterReturn(i);
      locations.AddReturn(regloc(reg, type));
    }
  }

  // Add parameters in registers and on the stack.
  for (int i = 0; i < js_parameter_count; i++) {
    if (i < register_parameter_count) {
      // The first parameters go in registers.
      MachineType type = descriptor.GetParameterType(i);
      if (IsFloatingPoint(type.representation())) {
        DoubleRegister reg = descriptor.GetDoubleRegisterParameter(i);
        locations.AddParam(regloc(reg, type));
      } else {
        Register reg = descriptor.GetRegisterParameter(i);
        locations.AddParam(regloc(reg, type));
      }
    } else {
      // The rest of the parameters go on the stack.
      int stack_slot = i - register_parameter_count - stack_parameter_count;
      locations.AddParam(LinkageLocation::ForCallerFrameSlot(
          stack_slot, i < descriptor.GetParameterCount()
                          ? descriptor.GetParameterType(i)
                          : MachineType::AnyTagged()));
    }
  }

  // Add context.
  if (context_count) {
    locations.AddParam(regloc(kContextRegister, MachineType::AnyTagged()));
  }

  // The target for stub calls depends on the requested mode.
  CallDescriptor::Kind kind;
  MachineType target_type;
  switch (stub_mode) {
    case StubCallMode::kCallCodeObject:
      kind = CallDescriptor::kCallCodeObject;
      target_type = MachineType::AnyTagged();
      break;
#if V8_ENABLE_WEBASSEMBLY
    case StubCallMode::kCallWasmRuntimeStub:
      kind = CallDescriptor::kCallWasmFunction;
      target_type = MachineType::Pointer();
      break;
#endif  // V8_ENABLE_WEBASSEMBLY
    case StubCallMode::kCallBuiltinPointer:
      kind = CallDescriptor::kCallBuiltinPointer;
      target_type = MachineType::AnyTagged();
      break;
  }

  RegList allocatable_registers = descriptor.allocatable_registers();
  RegList callee_saved_registers = kNoCalleeSaved;
  if (descriptor.CalleeSaveRegisters()) {
    callee_saved_registers = allocatable_registers;
    DCHECK(!callee_saved_registers.is_empty());
  }
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);
  return zone->New<CallDescriptor>(          // --
      kind,                                  // kind
      descriptor.tag(),                      // tag
      target_type,                           // target MachineType
      target_loc,                            // target location
      locations.Get(),                       // location_sig
      stack_parameter_count,                 // stack_parameter_count
      properties,                            // properties
      callee_saved_registers,                // callee-saved registers
      kNoCalleeSavedFp,                      // callee-saved fp
      CallDescriptor::kCanUseRoots | flags,  // flags
      descriptor.DebugName(),                // debug name
      descriptor.GetStackArgumentOrder(),    // stack order
      allocatable_registers);
}

// static
CallDescriptor* Linkage::GetBytecodeDispatchCallDescriptor(
    Zone* zone, const CallInterfaceDescriptor& descriptor,
    int stack_parameter_count) {
  const int register_parameter_count = descriptor.GetRegisterParameterCount();
  const int parameter_count = register_parameter_count + stack_parameter_count;

  DCHECK_EQ(descriptor.GetReturnCount(), 1);
  LocationSignature::Builder locations(zone, 1, parameter_count);

  locations.AddReturn(regloc(kReturnRegister0, descriptor.GetReturnType(0)));

  // Add parameters in registers and on the stack.
  for (int i = 0; i < parameter_count; i++) {
    if (i < register_parameter_count) {
      // The first parameters go in registers.
      Register reg = descriptor.GetRegisterParameter(i);
      MachineType type = descriptor.GetParameterType(i);
      locations.AddParam(regloc(reg, type));
    } else {
      // The rest of the parameters go on the stack.
      int stack_slot = i - register_parameter_count - stack_parameter_count;
      locations.AddParam(LinkageLocation::ForCallerFrameSlot(
          stack_slot, MachineType::AnyTagged()));
    }
  }

  // The target for interpreter dispatches is a code entry address.
  MachineType target_type = MachineType::Pointer();
  LinkageLocation target_loc = LinkageLocation::ForAnyRegister(target_type);
  const CallDescriptor::Flags kFlags =
      CallDescriptor::kCanUseRoots | CallDescriptor::kFixedTargetRegister;
  return zone->New<CallDescriptor>(   // --
      CallDescriptor::kCallAddress,   // kind
      kBytecodeHandlerEntrypointTag,  // tag
      target_type,                    // target MachineType
      target_loc,                     // target location
      locations.Get(),                // location_sig
      stack_parameter_count,          // stack_parameter_count
      Operator::kNoProperties,        // properties
      kNoCalleeSaved,                 // callee-saved registers
      kNoCalleeSavedFp,               // callee-saved fp
      kFlags,                         // flags
      descriptor.DebugName());
}

LinkageLocation Linkage::GetOsrValueLocation(int index) const {
  CHECK(incoming_->IsJSFunctionCall());
  int parameter_count_with_receiver =
      static_cast<int>(incoming_->JSParameterCount());
  int first_stack_slot =
      OsrHelper::FirstStackSlotIndex(parameter_count_with_receiver - 1);

  if (index == kOsrContextSpillSlotIndex) {
    int context_index =
        Linkage::GetJSCallContextParamIndex(parameter_count_with_receiver);
    return GetParameterLocation(context_index);
  } else if (index >= first_stack_slot) {
    // Local variable stored in this (callee) stack.
    int spill_index =
        index - first_stack_slot + StandardFrameConstants::kFixedSlotCount;
    return LinkageLocation::ForCalleeFrameSlot(spill_index,
                                               MachineType::AnyTagged());
  } else {
    // Parameter. Use the assigned location from the incoming call descriptor.
    return GetParameterLocation(index);
  }
}

namespace {
inline bool IsTaggedReg(const LinkageLocation& loc, Register reg) {
  return loc.IsRegister() && loc.AsRegister() == reg.code() &&
         loc.GetType().representation() ==
             MachineRepresentation::kTaggedPointer;
}
}  // namespace

bool Linkage::ParameterHasSecondaryLocation(int index) const {
  // TODO(titzer): this should be configurable, not call-type specific.
  if (incoming_->IsJSFunctionCall()) {
    LinkageLocation loc = GetParameterLocation(index);
    return IsTaggedReg(loc, kJSFunctionRegister) ||
           IsTaggedReg(loc, kContextRegister);
  }
#if V8_ENABLE_WEBASSEMBLY
  if (incoming_->IsAnyWasmFunctionCall()) {
    LinkageLocation loc = GetParameterLocation(index);
    return IsTaggedReg(loc, kWasmImplicitArgRegister);
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  return false;
}

LinkageLocation Linkage::GetParameterSecondaryLocation(int index) const {
  // TODO(titzer): these constants are necessary due to offset/slot# mismatch
  static const int kJSContextSlot = 2 + StandardFrameConstants::kCPSlotCount;
  static const int kJSFunctionSlot = 3 + StandardFrameConstants::kCPSlotCount;

  DCHECK(ParameterHasSecondaryLocation(index));
  LinkageLocation loc = GetParameterLocation(index);

  // TODO(titzer): this should be configurable, not call-type specific.
  if (incoming_->IsJSFunctionCall()) {
    if (IsTaggedReg(loc, kJSFunctionRegister)) {
      return LinkageLocation::ForCalleeFrameSlot(kJSFunctionSlot,
                                                 MachineType::AnyTagged());
    } else {
      DCHECK(IsTaggedReg(loc, kContextRegister));
      return LinkageLocation::ForCalleeFrameSlot(kJSContextSlot,
                                                 MachineType::AnyTagged());
    }
  }
#if V8_ENABLE_WEBASSEMBLY
  static const int kWasmInstanceDataSlot =
      3 + StandardFrameConstants::kCPSlotCount;
  if (incoming_->IsAnyWasmFunctionCall()) {
    DCHECK(IsTaggedReg(loc, kWasmImplicitArgRegister));
    return LinkageLocation::ForCalleeFrameSlot(kWasmInstanceDataSlot,
                                               MachineType::AnyTagged());
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  UNREACHABLE();
}


}  // namespace compiler
}  // namespace internal
}  // namespace v8
