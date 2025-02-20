// Copyright 2024 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/codegen/signature.h"
#include "src/execution/frame-constants.h"
#include "src/execution/isolate.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/interpreter/wasm-interpreter-runtime.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

#if V8_ENABLE_WEBASSEMBLY

namespace {
// Helper functions for the GenericJSToWasmInterpreterWrapper.

void PrepareForJsToWasmConversionBuiltinCall(MacroAssembler* masm,
                                             Register current_param_slot,
                                             Register valuetypes_array_ptr,
                                             Register wasm_instance,
                                             Register function_data) {
  UseScratchRegisterScope temps(masm);
  Register GCScanCount = temps.AcquireX();
  // Pushes and puts the values in order onto the stack before builtin calls for
  // the GenericJSToWasmInterpreterWrapper.
  // The last two slots contain tagged objects that need to be visited during
  // GC.
  __ Mov(GCScanCount, 2);
  __ Str(
      GCScanCount,
      MemOperand(
          fp, BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset));
  __ Push(current_param_slot, valuetypes_array_ptr, wasm_instance,
          function_data);
  // We had to prepare the parameters for the Call: we have to put the context
  // into x27.
  Register wasm_trusted_instance = wasm_instance;
  __ LoadTrustedPointerField(
      wasm_trusted_instance,
      FieldMemOperand(wasm_instance, WasmInstanceObject::kTrustedDataOffset),
      kWasmTrustedInstanceDataIndirectPointerTag);
  __ LoadTaggedField(
      kContextRegister,  // cp(x27)
      MemOperand(wasm_trusted_instance,
                 wasm::ObjectAccess::ToTagged(
                     WasmTrustedInstanceData::kNativeContextOffset)));
}

void RestoreAfterJsToWasmConversionBuiltinCall(MacroAssembler* masm,
                                               Register function_data,
                                               Register wasm_instance,
                                               Register valuetypes_array_ptr,
                                               Register current_param_slot) {
  // Pop and load values from the stack in order into the registers after
  // builtin calls for the GenericJSToWasmInterpreterWrapper.
  __ Pop(function_data, wasm_instance, valuetypes_array_ptr,
         current_param_slot);
  __ Str(
      xzr,
      MemOperand(
          fp, BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset));
}

void PrepareForBuiltinCall(MacroAssembler* masm, Register array_start,
                           Register return_count, Register wasm_instance) {
  UseScratchRegisterScope temps(masm);
  Register GCScanCount = temps.AcquireX();
  // Pushes and puts the values in order onto the stack before builtin calls for
  // the GenericJSToWasmInterpreterWrapper.
  __ Mov(GCScanCount, 1);
  __ Str(
      GCScanCount,
      MemOperand(
          fp, BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset));
  // The last slot contains a tagged object that need to be visited during GC.
  __ Push(array_start, return_count, xzr, wasm_instance);
  // We had to prepare the parameters for the Call: we have to put the context
  // into x27.
  Register wasm_trusted_instance = wasm_instance;
  __ LoadTrustedPointerField(
      wasm_trusted_instance,
      FieldMemOperand(wasm_instance, WasmInstanceObject::kTrustedDataOffset),
      kWasmTrustedInstanceDataIndirectPointerTag);
  __ LoadTaggedField(
      kContextRegister,  // cp(x27)
      MemOperand(wasm_trusted_instance,
                 wasm::ObjectAccess::ToTagged(
                     WasmTrustedInstanceData::kNativeContextOffset)));
}

void RestoreAfterBuiltinCall(MacroAssembler* masm, Register wasm_instance,
                             Register return_count, Register array_start) {
  // Pop and load values from the stack in order into the registers after
  // builtin calls for the GenericJSToWasmInterpreterWrapper.
  __ Pop(wasm_instance, xzr, return_count, array_start);
}

void PrepareForWasmToJsConversionBuiltinCall(
    MacroAssembler* masm, Register return_count, Register result_index,
    Register current_return_slot, Register valuetypes_array_ptr,
    Register wasm_instance, Register fixed_array, Register jsarray) {
  UseScratchRegisterScope temps(masm);
  Register GCScanCount = temps.AcquireX();
  // Pushes and puts the values in order onto the stack before builtin calls
  // for the GenericJSToWasmInterpreterWrapper.
  __ Mov(GCScanCount, 3);
  __ Str(
      GCScanCount,
      MemOperand(
          fp, BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset));
  __ Push(return_count, result_index, current_return_slot, valuetypes_array_ptr,
          xzr, wasm_instance, fixed_array, jsarray);
  // Put the context into x27.
  Register wasm_trusted_instance = wasm_instance;
  __ LoadTrustedPointerField(
      wasm_trusted_instance,
      FieldMemOperand(wasm_instance, WasmInstanceObject::kTrustedDataOffset),
      kWasmTrustedInstanceDataIndirectPointerTag);
  __ LoadTaggedField(
      kContextRegister,  // cp(x27)
      MemOperand(wasm_trusted_instance,
                 wasm::ObjectAccess::ToTagged(
                     WasmTrustedInstanceData::kNativeContextOffset)));
}

void RestoreAfterWasmToJsConversionBuiltinCall(
    MacroAssembler* masm, Register jsarray, Register fixed_array,
    Register wasm_instance, Register valuetypes_array_ptr,
    Register current_return_slot, Register result_index,
    Register return_count) {
  // Pop and load values from the stack in order into the registers after
  // builtin calls for the GenericJSToWasmInterpreterWrapper.
  __ Pop(jsarray, fixed_array, wasm_instance, xzr, valuetypes_array_ptr,
         current_return_slot, result_index, return_count);
}

}  // namespace

void Builtins::Generate_WasmInterpreterEntry(MacroAssembler* masm) {
  // Input registers:
  //  x7 (kWasmImplicitArgRegister): wasm_instance
  //  x12: array_start
  //  w15: function_index
  Register array_start = x12;
  Register function_index = x15;

  // Set up the stackframe:
  //
  // fp-0x10  wasm_instance
  // fp-0x08  Marker(StackFrame::WASM_INTERPRETER_ENTRY)
  // fp       Old RBP
  __ EnterFrame(StackFrame::WASM_INTERPRETER_ENTRY);

  __ Str(kWasmImplicitArgRegister, MemOperand(sp, 0));
  __ Push(function_index, array_start);
  __ Mov(kWasmImplicitArgRegister, xzr);
  __ CallRuntime(Runtime::kWasmRunInterpreter, 3);

  // Deconstruct the stack frame.
  __ LeaveFrame(StackFrame::WASM_INTERPRETER_ENTRY);
  __ Ret();
}

void LoadFunctionDataAndWasmInstance(MacroAssembler* masm,
                                     Register function_data,
                                     Register wasm_instance) {
  Register closure = function_data;
  Register shared_function_info = closure;
  __ LoadTaggedField(
      shared_function_info,
      MemOperand(
          closure,
          wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction()));
  closure = no_reg;
  __ LoadTrustedPointerField(
      function_data,
      FieldMemOperand(shared_function_info,
                      SharedFunctionInfo::kTrustedFunctionDataOffset),

      kUnknownIndirectPointerTag);
  shared_function_info = no_reg;

  Register trusted_instance_data = wasm_instance;
#if V8_ENABLE_SANDBOX
  __ DecompressProtected(
      trusted_instance_data,
      MemOperand(function_data,
                 WasmExportedFunctionData::kProtectedInstanceDataOffset -
                     kHeapObjectTag));
#else
  __ LoadTaggedField(
      trusted_instance_data,
      MemOperand(function_data,
                 WasmExportedFunctionData::kProtectedInstanceDataOffset -
                     kHeapObjectTag));
#endif
  __ LoadTaggedField(
      wasm_instance,
      FieldMemOperand(trusted_instance_data,
                      WasmTrustedInstanceData::kInstanceObjectOffset));
}

void LoadFromSignature(MacroAssembler* masm, Register valuetypes_array_ptr,
                       Register return_count, Register param_count) {
  Register signature = valuetypes_array_ptr;
  __ Ldr(return_count,
         MemOperand(signature, wasm::FunctionSig::kReturnCountOffset));
  __ Ldr(param_count,
         MemOperand(signature, wasm::FunctionSig::kParameterCountOffset));
  valuetypes_array_ptr = signature;
  __ Ldr(valuetypes_array_ptr,
         MemOperand(signature, wasm::FunctionSig::kRepsOffset));
}

void LoadValueTypesArray(MacroAssembler* masm, Register function_data,
                         Register valuetypes_array_ptr, Register return_count,
                         Register param_count, Register signature_data) {
  __ LoadTaggedField(
      signature_data,
      FieldMemOperand(function_data,
                      WasmExportedFunctionData::kPackedArgsSizeOffset));
  __ SmiToInt32(signature_data);

  Register signature = valuetypes_array_ptr;
  __ Ldr(signature,
         MemOperand(function_data,
                    WasmExportedFunctionData::kSigOffset - kHeapObjectTag));
  LoadFromSignature(masm, valuetypes_array_ptr, return_count, param_count);
}

class RegisterAllocator {
 public:
  class Scoped {
   public:
    Scoped(RegisterAllocator* allocator, Register* reg)
        : allocator_(allocator), reg_(reg) {}
    ~Scoped() { allocator_->Free(reg_); }

   private:
    RegisterAllocator* allocator_;
    Register* reg_;
  };

  explicit RegisterAllocator(const CPURegList& registers)
      : initial_(registers), available_(registers) {}
  void Ask(Register* reg) {
    DCHECK_EQ(*reg, no_reg);
    DCHECK(!available_.IsEmpty());
    *reg = available_.PopLowestIndex().X();
    allocated_registers_.push_back(reg);
  }

  void Pinned(const Register& requested, Register* reg) {
    DCHECK(available_.IncludesAliasOf(requested));
    *reg = requested;
    Reserve(requested);
    allocated_registers_.push_back(reg);
  }

  void Free(Register* reg) {
    DCHECK_NE(*reg, no_reg);
    available_.Combine(*reg);
    *reg = no_reg;
    allocated_registers_.erase(
        find(allocated_registers_.begin(), allocated_registers_.end(), reg));
  }

  void Reserve(const Register& reg) {
    if (reg == NoReg) {
      return;
    }
    DCHECK(available_.IncludesAliasOf(reg));
    available_.Remove(reg);
  }

  void Reserve(const Register& reg1, const Register& reg2,
               const Register& reg3 = NoReg, const Register& reg4 = NoReg,
               const Register& reg5 = NoReg, const Register& reg6 = NoReg) {
    Reserve(reg1);
    Reserve(reg2);
    Reserve(reg3);
    Reserve(reg4);
    Reserve(reg5);
    Reserve(reg6);
  }

  bool IsUsed(const Register& reg) {
    return initial_.IncludesAliasOf(reg) && !available_.IncludesAliasOf(reg);
  }

  void ResetExcept(const Register& reg1 = NoReg, const Register& reg2 = NoReg,
                   const Register& reg3 = NoReg, const Register& reg4 = NoReg,
                   const Register& reg5 = NoReg, const Register& reg6 = NoReg,
                   const Register& reg7 = NoReg) {
    available_ = initial_;
    if (reg1 != NoReg) {
      available_.Remove(reg1, reg2, reg3, reg4);
    }
    if (reg5 != NoReg) {
      available_.Remove(reg5, reg6, reg7);
    }
    auto it = allocated_registers_.begin();
    while (it != allocated_registers_.end()) {
      if (available_.IncludesAliasOf(**it)) {
        **it = no_reg;
        allocated_registers_.erase(it);
      } else {
        it++;
      }
    }
  }

  static RegisterAllocator WithAllocatableGeneralRegisters() {
    CPURegList list(kXRegSizeInBits, RegList());
    // Only use registers x0-x15, which are volatile (caller-saved).
    // Mksnapshot would fail to compile the GenericJSToWasmInterpreterWrapper
    // and GenericWasmToJSInterpreterWrapper if they needed more registers.
    list.set_bits(0xffff);  // (The default value is 0x0bf8ffff).
    return RegisterAllocator(list);
  }

 private:
  std::vector<Register*> allocated_registers_;
  const CPURegList initial_;
  CPURegList available_;
};

#define DEFINE_REG(Name)  \
  Register Name = no_reg; \
  regs.Ask(&Name);

#define DEFINE_REG_W(Name) \
  DEFINE_REG(Name);        \
  Name = Name.W();

#define ASSIGN_REG(Name) regs.Ask(&Name);

#define ASSIGN_REG_W(Name) \
  ASSIGN_REG(Name);        \
  Name = Name.W();

#define DEFINE_PINNED(Name, Reg) \
  Register Name = no_reg;        \
  regs.Pinned(Reg, &Name);

#define DEFINE_SCOPED(Name) \
  DEFINE_REG(Name)          \
  RegisterAllocator::Scoped scope_##Name(&regs, &Name);

#define FREE_REG(Name) regs.Free(&Name);

// TODO(paolosev@microsoft.com): this should be converted into a Torque builtin,
// like it was done for GenericJSToWasmWrapper.
void Builtins::Generate_GenericJSToWasmInterpreterWrapper(
    MacroAssembler* masm) {
  auto regs = RegisterAllocator::WithAllocatableGeneralRegisters();
  // Set up the stackframe.
  __ EnterFrame(StackFrame::JS_TO_WASM);

  // -------------------------------------------
  // Compute offsets and prepare for GC.
  // -------------------------------------------
  // GenericJSToWasmInterpreterWrapperFrame:
  // fp-N     Args/retvals array for Wasm call
  // ...       ...
  // fp-0x58  SignatureData
  // fp-0x50  CurrentIndex
  // fp-0x48  ArgRetsIsArgs
  // fp-0x40  ArgRetsAddress
  // fp-0x38  ValueTypesArray
  // fp-0x30  SigReps
  // fp-0x28  ReturnCount
  // fp-0x20  ParamCount
  // fp-0x18  InParamCount
  // fp-0x10  GCScanSlotCount
  // fp-0x08  Marker(StackFrame::JS_TO_WASM)
  // fp       Old RBP

  constexpr int kMarkerOffset =
      BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset +
      kSystemPointerSize;
  // The number of parameters passed to this function.
  constexpr int kInParamCountOffset =
      BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset -
      kSystemPointerSize;
  // The number of parameters according to the signature.
  constexpr int kParamCountOffset =
      BuiltinWasmInterpreterWrapperConstants::kParamCountOffset;
  constexpr int kReturnCountOffset =
      BuiltinWasmInterpreterWrapperConstants::kReturnCountOffset;
  constexpr int kSigRepsOffset =
      BuiltinWasmInterpreterWrapperConstants::kSigRepsOffset;
  constexpr int kValueTypesArrayStartOffset =
      BuiltinWasmInterpreterWrapperConstants::kValueTypesArrayStartOffset;
  // Array for arguments and return values. They will be scanned by GC.
  constexpr int kArgRetsAddressOffset =
      BuiltinWasmInterpreterWrapperConstants::kArgRetsAddressOffset;
  // Arg/Return arrays use the same stack address. So, we should keep a flag
  // whether we are using the array for args or returns. (1 = Args, 0 = Rets)
  constexpr int kArgRetsIsArgsOffset =
      BuiltinWasmInterpreterWrapperConstants::kArgRetsIsArgsOffset;
  // The index of the argument being converted.
  constexpr int kCurrentIndexOffset =
      BuiltinWasmInterpreterWrapperConstants::kCurrentIndexOffset;
  // Precomputed signature data, a uint32_t with the format:
  // bit 0-14: PackedArgsSize
  // bit 15:   HasRefArgs
  // bit 16:   HasRefRets
  constexpr int kSignatureDataOffset =
      BuiltinWasmInterpreterWrapperConstants::kSignatureDataOffset;
  // We set and use this slot only when moving parameters into the parameter
  // registers (so no GC scan is needed).
  constexpr int kNumSpillSlots =
      (kMarkerOffset - kSignatureDataOffset) / kSystemPointerSize;
  constexpr int kNum16BytesAlignedSpillSlots = 2 * ((kNumSpillSlots + 1) / 2);

  __ Sub(sp, sp, Immediate(kNum16BytesAlignedSpillSlots * kSystemPointerSize));
  // Put the in_parameter count on the stack, we only  need it at the very end
  // when we pop the parameters off the stack.
  __ Sub(kJavaScriptCallArgCountRegister, kJavaScriptCallArgCountRegister, 1);
  __ Str(kJavaScriptCallArgCountRegister, MemOperand(fp, kInParamCountOffset));

  // -------------------------------------------
  // Load the Wasm exported function data and the Wasm instance.
  // -------------------------------------------
  DEFINE_PINNED(function_data, kJSFunctionRegister);    // x1
  DEFINE_PINNED(wasm_instance, kWasmImplicitArgRegister);  // x7
  LoadFunctionDataAndWasmInstance(masm, function_data, wasm_instance);

  regs.ResetExcept(function_data, wasm_instance);

  // -------------------------------------------
  // Load values from the signature.
  // -------------------------------------------

  // Param should be x0 for calling Runtime in the conversion loop.
  DEFINE_PINNED(param, x0);
  // These registers stays alive until we load params to param registers.
  // To prevent aliasing assign higher register here.
  DEFINE_PINNED(valuetypes_array_ptr, x11);

  DEFINE_REG(return_count);
  DEFINE_REG(param_count);
  DEFINE_REG(signature_data);
  DEFINE_REG(scratch);

  // -------------------------------------------
  // Load values from the signature.
  // -------------------------------------------
  LoadValueTypesArray(masm, function_data, valuetypes_array_ptr, return_count,
                      param_count, signature_data);
  __ Str(signature_data, MemOperand(fp, kSignatureDataOffset));
  Register array_size = signature_data;
  __ And(array_size, array_size,
         Immediate(wasm::WasmInterpreterRuntime::PackedArgsSizeField::kMask));
  // -------------------------------------------
  // Store signature-related values to the stack.
  // -------------------------------------------
  // We store values on the stack to restore them after function calls.
  // We cannot push values onto the stack right before the wasm call. The wasm
  // function expects the parameters, that didn't fit into the registers, on the
  // top of the stack.
  __ Str(param_count, MemOperand(fp, kParamCountOffset));
  __ Str(return_count, MemOperand(fp, kReturnCountOffset));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kSigRepsOffset));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));

  // -------------------------------------------
  // Allocate array for args and return value.
  // -------------------------------------------

  // Leave space for WasmInstance.
  __ Add(array_size, array_size, Immediate(kSystemPointerSize));
  // Ensure that the array is 16-bytes aligned.
  __ Add(scratch, array_size, Immediate(8));
  __ And(array_size, scratch, Immediate(-16));

  DEFINE_PINNED(array_start, x12);
  __ Sub(array_start, sp, array_size);
  __ Mov(sp, array_start);

  __ Mov(scratch, 1);
  __ Str(scratch, MemOperand(fp, kArgRetsIsArgsOffset));

  __ Str(xzr, MemOperand(fp, kCurrentIndexOffset));

  // Set the current_param_slot to point to the start of the section, after the
  // WasmInstance object.
  DEFINE_PINNED(current_param_slot, x13);
  __ Add(current_param_slot, array_start, Immediate(kSystemPointerSize));
  __ Str(current_param_slot, MemOperand(fp, kArgRetsAddressOffset));

  Label prepare_for_wasm_call;
  __ Cmp(param_count, 0);

  // IF we have 0 params: jump through parameter handling.
  __ B(&prepare_for_wasm_call, eq);

  // Create a section on the stack to pass the evaluated parameters to the
  // interpreter and to receive the results. This section represents the array
  // expected as argument by the Runtime_WasmRunInterpreter.
  // Arguments are stored one after the other without holes, starting at the
  // beginning of the array, and the interpreter puts the returned values in the
  // same array, also starting at the beginning.

  // Loop through the params starting with the first.
  // 'fp + kFPOnStackSize + kPCOnStackSize + kReceiverOnStackSize' points to the
  // first JS parameter we are processing.

  // We have to check the types of the params. The ValueType array contains
  // first the return then the param types.

  // Set the ValueType array pointer to point to the first parameter.
  constexpr int kValueTypeSize = sizeof(wasm::ValueType);
  static_assert(kValueTypeSize == 4);
  const int32_t kValueTypeSizeLog2 = log2(kValueTypeSize);
  // Set the ValueType array pointer to point to the first parameter.
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr,
         Operand(return_count, LSL, kValueTypeSizeLog2));

  DEFINE_REG(current_index);
  __ Mov(current_index, xzr);

  // -------------------------------------------
  // Param evaluation loop.
  // -------------------------------------------
  Label loop_through_params;
  __ bind(&loop_through_params);

  constexpr int kReceiverOnStackSize = kSystemPointerSize;
  constexpr int kArgsOffset =
      kFPOnStackSize + kPCOnStackSize + kReceiverOnStackSize;
  // Read JS argument into 'param'.
  __ Add(scratch, fp, kArgsOffset);
  __ Ldr(param,
         MemOperand(scratch, current_index, LSL, kSystemPointerSizeLog2));
  __ Str(current_index, MemOperand(fp, kCurrentIndexOffset));

  DEFINE_REG_W(valuetype);
  __ Ldr(valuetype,
         MemOperand(valuetypes_array_ptr, wasm::ValueType::bit_field_offset()));

  // -------------------------------------------
  // Param conversion.
  // -------------------------------------------
  // If param is a Smi we can easily convert it. Otherwise we'll call a builtin
  // for conversion.
  Label param_conversion_done;
  Label check_ref_param;
  Label convert_param;
  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&check_ref_param, ne);
  __ JumpIfNotSmi(param, &convert_param);

  // Change the param from Smi to int32.
  __ SmiUntag(param);
  // Place the param into the proper slot in Integer section.
  __ Str(param, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(sizeof(int32_t)));
  __ jmp(&param_conversion_done);

  Label handle_ref_param;
  __ bind(&check_ref_param);

  // wasm::ValueKind::kRefNull is not representable as a cmp immediate operand.
  __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
  __ B(&handle_ref_param, eq);
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
  __ B(&convert_param, ne);

  // Place the reference param into the proper slot.
  __ bind(&handle_ref_param);
  // Make sure slot for ref args are 64-bit aligned.
  __ And(scratch, current_param_slot, Immediate(0x04));
  __ Add(current_param_slot, current_param_slot, scratch);
  __ Str(param, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(kSystemPointerSize));

  // -------------------------------------------
  // Param conversion done.
  // -------------------------------------------
  __ bind(&param_conversion_done);

  __ Add(valuetypes_array_ptr, valuetypes_array_ptr, kValueTypeSize);

  __ Ldr(current_index, MemOperand(fp, kCurrentIndexOffset));
  __ Ldr(scratch, MemOperand(fp, kParamCountOffset));
  __ Add(current_index, current_index, 1);
  __ cmp(current_index, scratch);
  __ B(&loop_through_params, lt);
  __ Str(current_index, MemOperand(fp, kCurrentIndexOffset));
  __ jmp(&prepare_for_wasm_call);

  // -------------------------------------------
  // Param conversion builtins.
  // -------------------------------------------
  __ bind(&convert_param);
  // The order of pushes is important. We want the heap objects, that should be
  // scanned by GC, to be on the top of the stack.
  // We have to set the indicating value for the GC to the number of values on
  // the top of the stack that have to be scanned before calling the builtin
  // function.
  // We don't need the JS context for these builtin calls.
  // The builtin expects the parameter to be in register param = rax.

  PrepareForJsToWasmConversionBuiltinCall(masm, current_param_slot,
                                          valuetypes_array_ptr, wasm_instance,
                                          function_data);

  Label param_kWasmI32_not_smi, param_kWasmI64, param_kWasmF32, param_kWasmF64,
      throw_type_error;

  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&param_kWasmI32_not_smi, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
  __ B(&param_kWasmI64, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
  __ B(&param_kWasmF32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
  __ B(&param_kWasmF64, eq);

  __ cmp(valuetype, Immediate(wasm::kWasmS128.raw_bit_field()));
  // Simd arguments cannot be passed from JavaScript.
  __ B(&throw_type_error, eq);

  // Invalid type.
  __ DebugBreak();

  __ bind(&param_kWasmI32_not_smi);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedNonSmiToInt32),
          RelocInfo::CODE_TARGET);
  // Param is the result of the builtin.
  RestoreAfterJsToWasmConversionBuiltinCall(masm, function_data, wasm_instance,
                                            valuetypes_array_ptr,
                                            current_param_slot);
  __ Str(param, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(sizeof(int32_t)));
  __ jmp(&param_conversion_done);

  __ bind(&param_kWasmI64);
  __ Call(BUILTIN_CODE(masm->isolate(), BigIntToI64), RelocInfo::CODE_TARGET);
  RestoreAfterJsToWasmConversionBuiltinCall(masm, function_data, wasm_instance,
                                            valuetypes_array_ptr,
                                            current_param_slot);
  __ Str(param, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(sizeof(int64_t)));
  __ jmp(&param_conversion_done);

  __ bind(&param_kWasmF32);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedToFloat32),
          RelocInfo::CODE_TARGET);
  RestoreAfterJsToWasmConversionBuiltinCall(masm, function_data, wasm_instance,
                                            valuetypes_array_ptr,
                                            current_param_slot);
  __ Str(kFPReturnRegister0, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(sizeof(float)));
  __ jmp(&param_conversion_done);

  __ bind(&param_kWasmF64);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedToFloat64),
          RelocInfo::CODE_TARGET);
  RestoreAfterJsToWasmConversionBuiltinCall(masm, function_data, wasm_instance,
                                            valuetypes_array_ptr,
                                            current_param_slot);
  __ Str(kFPReturnRegister0, MemOperand(current_param_slot, 0));
  __ Add(current_param_slot, current_param_slot, Immediate(sizeof(double)));
  __ jmp(&param_conversion_done);

  __ bind(&throw_type_error);
  // CallRuntime expects kRootRegister (x26) to contain the root.
  __ CallRuntime(Runtime::kWasmThrowJSTypeError);
  __ DebugBreak();  // Should not return.

  // -------------------------------------------
  // Prepare for the Wasm call.
  // -------------------------------------------

  regs.ResetExcept(function_data, wasm_instance, array_start, scratch);

  __ bind(&prepare_for_wasm_call);

  // Set thread_in_wasm_flag.
  DEFINE_REG_W(scratch32);
  __ Ldr(scratch, MemOperand(kRootRegister,
                             Isolate::thread_in_wasm_flag_address_offset()));
  __ Mov(scratch32, 1);  // 32 bit.
  __ Str(scratch32, MemOperand(scratch, 0));

  DEFINE_PINNED(function_index, w15);
  __ Ldr(
      function_index,
      MemOperand(function_data, WasmExportedFunctionData::kFunctionIndexOffset -
                                    kHeapObjectTag));
  // We pass function_index as Smi.

  // One tagged object (the wasm_instance) to be visited if there is a GC
  // during the call.
  constexpr int kWasmCallGCScanSlotCount = 1;
  __ Mov(scratch, kWasmCallGCScanSlotCount);
  __ Str(
      scratch,
      MemOperand(
          fp, BuiltinWasmInterpreterWrapperConstants::kGCScanSlotCountOffset));

  // -------------------------------------------
  // Call the Wasm function.
  // -------------------------------------------

  // Here array_start == sp.
  __ Str(wasm_instance, MemOperand(sp));
  // Skip wasm_instance.
  __ Ldr(array_start, MemOperand(fp, kArgRetsAddressOffset));
  // Here array_start == sp + kSystemPointerSize.
  __ Call(BUILTIN_CODE(masm->isolate(), WasmInterpreterEntry),
          RelocInfo::CODE_TARGET);
  __ Ldr(wasm_instance, MemOperand(sp));
  __ Ldr(array_start, MemOperand(fp, kArgRetsAddressOffset));

  __ Str(xzr, MemOperand(fp, kArgRetsIsArgsOffset));

  // Unset thread_in_wasm_flag.
  __ Ldr(scratch, MemOperand(kRootRegister,
                             Isolate::thread_in_wasm_flag_address_offset()));
  __ Str(wzr, MemOperand(scratch, 0));  // 32 bit.

  regs.ResetExcept(wasm_instance, array_start, scratch);

  // -------------------------------------------
  // Return handling.
  // -------------------------------------------
  DEFINE_PINNED(return_value, kReturnRegister0);  // x0
  ASSIGN_REG(return_count);
  __ Ldr(return_count, MemOperand(fp, kReturnCountOffset));

  // All return values are already in the packed array.
  __ Str(return_count,
         MemOperand(
             fp, BuiltinWasmInterpreterWrapperConstants::kCurrentIndexOffset));

  DEFINE_PINNED(fixed_array, x14);
  __ Mov(fixed_array, xzr);
  DEFINE_PINNED(jsarray, x15);
  __ Mov(jsarray, xzr);

  Label all_results_conversion_done, start_return_conversion, return_jsarray;

  __ cmp(return_count, 1);
  __ B(&start_return_conversion, eq);
  __ B(&return_jsarray, gt);

  // If no return value, load undefined.
  __ LoadRoot(return_value, RootIndex::kUndefinedValue);
  __ jmp(&all_results_conversion_done);

  // If we have more than one return value, we need to return a JSArray.
  __ bind(&return_jsarray);
  PrepareForBuiltinCall(masm, array_start, return_count, wasm_instance);
  __ Mov(return_value, return_count);
  __ SmiTag(return_value);

  // Create JSArray to hold results.
  __ Call(BUILTIN_CODE(masm->isolate(), WasmAllocateJSArray),
          RelocInfo::CODE_TARGET);
  __ Mov(jsarray, return_value);

  RestoreAfterBuiltinCall(masm, wasm_instance, return_count, array_start);
  __ LoadTaggedField(fixed_array, MemOperand(jsarray, JSArray::kElementsOffset -
                                                          kHeapObjectTag));

  __ bind(&start_return_conversion);
  Register current_return_slot = array_start;

  DEFINE_PINNED(result_index, x13);
  __ Mov(result_index, xzr);

  // -------------------------------------------
  // Return conversions.
  // -------------------------------------------
  Label convert_return_value;
  __ bind(&convert_return_value);
  // We have to make sure that the kGCScanSlotCount is set correctly when we
  // call the builtins for conversion. For these builtins it's the same as for
  // the Wasm call, that is, kGCScanSlotCount = 0, so we don't have to reset it.
  // We don't need the JS context for these builtin calls.

  ASSIGN_REG(valuetypes_array_ptr);
  __ Ldr(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));

  // The first valuetype of the array is the return's valuetype.
  ASSIGN_REG_W(valuetype);
  __ Ldr(valuetype,
         MemOperand(valuetypes_array_ptr, wasm::ValueType::bit_field_offset()));

  Label return_kWasmI32, return_kWasmI64, return_kWasmF32, return_kWasmF64,
      return_kWasmRef;

  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&return_kWasmI32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
  __ B(&return_kWasmI64, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
  __ B(&return_kWasmF32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
  __ B(&return_kWasmF64, eq);

  {
    __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
    __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
    __ B(&return_kWasmRef, eq);
    __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
    __ B(&return_kWasmRef, eq);

    // Invalid type. Wasm cannot return Simd results to JavaScript.
    __ DebugBreak();
  }

  Label return_value_done;

  Label to_heapnumber;
  {
    __ bind(&return_kWasmI32);
    __ Ldr(return_value, MemOperand(current_return_slot, 0));
    __ Add(current_return_slot, current_return_slot,
           Immediate(sizeof(int32_t)));
    // If pointer compression is disabled, we can convert the return to a smi.
    if (SmiValuesAre32Bits()) {
      __ SmiTag(return_value);
    } else {
      // Double the return value to test if it can be a Smi.
      __ Adds(wzr, return_value.W(), return_value.W());
      // If there was overflow, convert the return value to a HeapNumber.
      __ B(&to_heapnumber, vs);
      // If there was no overflow, we can convert to Smi.
      __ SmiTag(return_value);
    }
  }
  __ jmp(&return_value_done);

  // Handle the conversion of the I32 return value to HeapNumber when it cannot
  // be a smi.
  __ bind(&to_heapnumber);

  PrepareForWasmToJsConversionBuiltinCall(
      masm, return_count, result_index, current_return_slot,
      valuetypes_array_ptr, wasm_instance, fixed_array, jsarray);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmInt32ToHeapNumber),
          RelocInfo::CODE_TARGET);
  RestoreAfterWasmToJsConversionBuiltinCall(
      masm, jsarray, fixed_array, wasm_instance, valuetypes_array_ptr,
      current_return_slot, result_index, return_count);
  __ jmp(&return_value_done);

  __ bind(&return_kWasmI64);
  __ Ldr(return_value, MemOperand(current_return_slot, 0));
  __ Add(current_return_slot, current_return_slot, Immediate(sizeof(int64_t)));
  PrepareForWasmToJsConversionBuiltinCall(
      masm, return_count, result_index, current_return_slot,
      valuetypes_array_ptr, wasm_instance, fixed_array, jsarray);
  __ Call(BUILTIN_CODE(masm->isolate(), I64ToBigInt), RelocInfo::CODE_TARGET);
  RestoreAfterWasmToJsConversionBuiltinCall(
      masm, jsarray, fixed_array, wasm_instance, valuetypes_array_ptr,
      current_return_slot, result_index, return_count);
  __ jmp(&return_value_done);

  __ bind(&return_kWasmF32);
  __ Ldr(v0, MemOperand(current_return_slot, 0));
  __ Add(current_return_slot, current_return_slot, Immediate(sizeof(float)));
  PrepareForWasmToJsConversionBuiltinCall(
      masm, return_count, result_index, current_return_slot,
      valuetypes_array_ptr, wasm_instance, fixed_array, jsarray);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmFloat32ToNumber),
          RelocInfo::CODE_TARGET);
  RestoreAfterWasmToJsConversionBuiltinCall(
      masm, jsarray, fixed_array, wasm_instance, valuetypes_array_ptr,
      current_return_slot, result_index, return_count);
  __ jmp(&return_value_done);

  __ bind(&return_kWasmF64);
  __ Ldr(d0, MemOperand(current_return_slot, 0));
  __ Add(current_return_slot, current_return_slot, Immediate(sizeof(double)));
  PrepareForWasmToJsConversionBuiltinCall(
      masm, return_count, result_index, current_return_slot,
      valuetypes_array_ptr, wasm_instance, fixed_array, jsarray);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmFloat64ToNumber),
          RelocInfo::CODE_TARGET);
  RestoreAfterWasmToJsConversionBuiltinCall(
      masm, jsarray, fixed_array, wasm_instance, valuetypes_array_ptr,
      current_return_slot, result_index, return_count);
  __ jmp(&return_value_done);

  __ bind(&return_kWasmRef);
  // Make sure slot for ref args are 64-bit aligned.
  __ And(scratch, current_return_slot, Immediate(0x04));
  __ Add(current_return_slot, current_return_slot, scratch);
  __ Ldr(return_value, MemOperand(current_return_slot, 0));
  __ Add(current_return_slot, current_return_slot,
         Immediate(kSystemPointerSize));
  // It might be cleaner to call Builtins_WasmFuncRefToJS here to extract
  // func.external from the ref object if the type is kWasmFuncRef.

  Label next_return_value;

  __ bind(&return_value_done);
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr, Immediate(kValueTypeSize));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));
  __ cmp(fixed_array, xzr);
  __ B(&next_return_value, eq);

  // Store result in JSArray
  DEFINE_REG(array_items);
  __ Add(array_items, fixed_array,
         OFFSET_OF_DATA_START(FixedArray) - kHeapObjectTag);
  __ StoreTaggedField(return_value, MemOperand(array_items, result_index, LSL,
                                               kTaggedSizeLog2));

  __ bind(&next_return_value);
  __ Add(result_index, result_index, 1);
  __ cmp(result_index, return_count);
  __ B(&convert_return_value, lt);

  __ bind(&all_results_conversion_done);
  ASSIGN_REG(param_count);
  __ Ldr(param_count, MemOperand(fp, kParamCountOffset));  // ???

  Label do_return;
  __ cmp(fixed_array, xzr);
  __ B(&do_return, eq);
  // The result is jsarray.
  __ Mov(return_value, jsarray);

  __ bind(&do_return);
  // Calculate the number of parameters we have to pop off the stack. This
  // number is max(in_param_count, param_count).
  DEFINE_REG(in_param_count);
  __ Ldr(in_param_count, MemOperand(fp, kInParamCountOffset));
  __ cmp(param_count, in_param_count);
  __ csel(param_count, in_param_count, param_count, lt);

  // -------------------------------------------
  // Deconstruct the stack frame.
  // -------------------------------------------
  __ LeaveFrame(StackFrame::JS_TO_WASM);

  // We have to remove the caller frame slots:
  //  - JS arguments
  //  - the receiver
  // and transfer the control to the return address (the return address is
  // expected to be on the top of the stack).
  // We cannot use just the ret instruction for this, because we cannot pass the
  // number of slots to remove in a Register as an argument.
  __ DropArguments(param_count);
  __ Ret(lr);
}

void Builtins::Generate_WasmInterpreterCWasmEntry(MacroAssembler* masm) {
  Label invoke, handler_entry, exit;

  __ EnterFrame(StackFrame::C_WASM_ENTRY);

  // Space to store c_entry_fp and current sp (used by exception handler).
  __ Push(xzr, xzr);

  {
    NoRootArrayScope no_root_array(masm);

    // Push callee saved registers.
    __ Push(d14, d15);
    __ Push(d12, d13);
    __ Push(d10, d11);
    __ Push(d8, d9);
    __ Push(x27, x28);
    __ Push(x25, x26);
    __ Push(x23, x24);
    __ Push(x21, x22);
    __ Push(x19, x20);

    // Set up the reserved register for 0.0.
    __ Fmov(fp_zero, 0.0);

    // Initialize the root register.
    __ Mov(kRootRegister, x2);

#ifdef V8_COMPRESS_POINTERS_IN_SHARED_CAGE
    // Initialize the pointer cage base register.
    __ LoadRootRelative(kPtrComprCageBaseRegister,
                        IsolateData::cage_base_offset());
#endif
  }

  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Mov(scratch, sp);
    __ Str(scratch,
           MemOperand(fp, WasmInterpreterCWasmEntryConstants::kSPFPOffset));
  }

  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                        masm->isolate()));
  __ Ldr(x10, MemOperand(x11));  // x10 = C entry FP.

  __ Str(x10,
         MemOperand(fp, WasmInterpreterCWasmEntryConstants::kCEntryFPOffset));

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the pending exception.
  __ B(&invoke);

  // Prevent the constant pool from being emitted between the record of the
  // handler_entry position and the first instruction of the sequence here.
  // There is no risk because Assembler::Emit() emits the instruction before
  // checking for constant pool emission, but we do not want to depend on
  // that.
  {
    Assembler::BlockPoolsScope block_pools(masm);

    __ BindExceptionHandler(&handler_entry);

    // Store the current pc as the handler offset. It's used later to create the
    // handler table.
    masm->isolate()->builtins()->SetCWasmInterpreterEntryHandlerOffset(
        handler_entry.pos());
  }
  __ B(&exit);

  // Invoke: Link this frame into the handler chain.
  __ Bind(&invoke);

  // Link the current handler as the next handler.
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
  __ Ldr(x10, MemOperand(x11));
  __ Push(padreg, x10);

  // Set this new handler as the current one.
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Mov(scratch, sp);
    __ Str(scratch, MemOperand(x11));
  }

  // Invoke the JS function through the GenericWasmToJSInterpreterWrapper.
  __ Call(BUILTIN_CODE(masm->isolate(), GenericWasmToJSInterpreterWrapper),
          RelocInfo::CODE_TARGET);

  // Pop the stack handler and unlink this frame from the handler chain.
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");
  __ Pop(x10, padreg);
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
  __ Drop(StackHandlerConstants::kSlotCount - 2);
  __ Str(x10, MemOperand(x11));

  __ Bind(&exit);

  // Pop callee saved registers.
  __ Pop(x20, x19);
  __ Pop(x22, x21);
  __ Pop(x24, x23);
  __ Pop(x26, x25);
  __ Pop(x28, x27);
  __ Pop(d9, d8);
  __ Pop(d11, d10);
  __ Pop(d13, d12);
  __ Pop(d15, d14);

  // Deconstruct the stack frame.
  __ LeaveFrame(StackFrame::C_WASM_ENTRY);
  __ Ret();
}

void Builtins::Generate_GenericWasmToJSInterpreterWrapper(
    MacroAssembler* masm) {
  auto regs = RegisterAllocator::WithAllocatableGeneralRegisters();

  DEFINE_PINNED(target_js_function, x0);
  DEFINE_PINNED(packed_args, x1);
  DEFINE_PINNED(signature, x3);
  DEFINE_PINNED(callable, x5);

  // Set up the stackframe.
  __ EnterFrame(StackFrame::WASM_TO_JS);

  // -------------------------------------------
  // Compute offsets and prepare for GC.
  // -------------------------------------------
  // GenericJSToWasmInterpreterWrapperFrame:
  // sp = fp-N receiver                      ^
  // ...       JS arg 0                      |
  // ...       ...                           | Tagged
  // ...       JS arg n-1                    | objects
  // ...       (padding if num args is odd)  |
  // fp-0x60   context                       |
  // fp-0x58   callable                      v
  // -------------------------------------------
  // fp-0x50   current_param_offset/current_result_offset
  // fp-0x48   valuetypes_array_ptr
  //
  // fp-0x40   param_index/return_index
  // fp-0x38   signature
  //
  // fp-0x30   param_count
  // fp-0x28   return_count
  //
  // fp-0x20   packed_array
  // fp-0x18   GC_SP (not used on arm64)
  //
  // fp-0x10   GCScanSlotLimit
  // fp-0x08   Marker(StackFrame::WASM_TO_JS)
  //
  // fp        Old fp
  // fp+0x08   return address

  static_assert(WasmToJSInterpreterFrameConstants::kGCSPOffset ==
                WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset -
                    kSystemPointerSize);
  constexpr int kPackedArrayOffset =
      WasmToJSInterpreterFrameConstants::kGCSPOffset - kSystemPointerSize;
  constexpr int kReturnCountOffset = kPackedArrayOffset - kSystemPointerSize;
  constexpr int kParamCountOffset = kReturnCountOffset - kSystemPointerSize;
  constexpr int kSignatureOffset = kParamCountOffset - kSystemPointerSize;
  constexpr int kParamIndexOffset = kSignatureOffset - kSystemPointerSize;
  // Reuse this slot when iterating over return values.
  constexpr int kResultIndexOffset = kParamIndexOffset;
  constexpr int kValueTypesArrayStartOffset =
      kParamIndexOffset - kSystemPointerSize;
  constexpr int kCurrentParamOffset =
      kValueTypesArrayStartOffset - kSystemPointerSize;
  // Reuse this slot when iterating over return values.
  constexpr int kCurrentResultOffset = kCurrentParamOffset;
  constexpr int kCallableOffset = kCurrentParamOffset - kSystemPointerSize;
  constexpr int kContextOffset = kCallableOffset - kSystemPointerSize;
  constexpr int kNumSpillSlots =
      (WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset -
       kCurrentParamOffset) /
      kSystemPointerSize;
  static_assert((kNumSpillSlots % 2) == 0);  // 16-bytes aligned.

  __ Sub(sp, sp, Immediate(kNumSpillSlots * kSystemPointerSize));

  __ Str(packed_args, MemOperand(fp, kPackedArrayOffset));

  // Not used on arm64.
  __ Str(xzr, MemOperand(fp, WasmToJSInterpreterFrameConstants::kGCSPOffset));

  __ Str(xzr,
         MemOperand(fp,
                    WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset));

  DEFINE_REG(shared_function_info);
  __ LoadTaggedField(
      shared_function_info,
      MemOperand(
          target_js_function,
          wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction()));

  // Set the context of the function; the call has to run in the function
  // context.
  DEFINE_REG(context);
  __ LoadTaggedField(
      context, FieldMemOperand(target_js_function, JSFunction::kContextOffset));
  __ Mov(cp, context);

  // Load global receiver if sloppy else use undefined.
  Label receiver_undefined;
  Label calculate_js_function_arity;
  DEFINE_REG(receiver);
  DEFINE_REG(flags);
  __ Ldr(flags, FieldMemOperand(shared_function_info,
                                SharedFunctionInfo::kFlagsOffset));
  __ Tst(flags, Immediate(SharedFunctionInfo::IsNativeBit::kMask |
                          SharedFunctionInfo::IsStrictBit::kMask));
  FREE_REG(flags);
  __ B(&receiver_undefined, ne);
  __ LoadGlobalProxy(receiver);
  __ B(&calculate_js_function_arity);

  __ bind(&receiver_undefined);
  __ LoadRoot(receiver, RootIndex::kUndefinedValue);

  __ bind(&calculate_js_function_arity);

  // Load values from the signature.
  DEFINE_REG(return_count);
  DEFINE_REG(param_count);
  __ Str(signature, MemOperand(fp, kSignatureOffset));
  Register valuetypes_array_ptr = signature;
  LoadFromSignature(masm, valuetypes_array_ptr, return_count, param_count);
  __ Str(param_count, MemOperand(fp, kParamCountOffset));
  FREE_REG(shared_function_info);

  // Make room to pass the args and the receiver.
  DEFINE_REG(array_size);
  DEFINE_REG(scratch);

  // Points to the end of the stack frame area that contains tagged objects
  // that need to be visited during GC.
  __ Mov(scratch, sp);
  __ Str(scratch,
         MemOperand(fp,
                    WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset));

  // Store callable and context.
  __ Push(callable, cp);
  __ Add(array_size, param_count, Immediate(1));
  // Ensure that the array is 16-bytes aligned.
  __ Add(scratch, array_size, Immediate(1));
  __ And(array_size, scratch, Immediate(-2));
  __ Sub(sp, sp, Operand(array_size, LSL, kSystemPointerSizeLog2));

  // Make sure that the padding slot (if present) is reset to zero. The other
  // slots will be initialized with the arguments.
  __ Sub(scratch, array_size, Immediate(1));
  __ Str(xzr, MemOperand(sp, scratch, LSL, kSystemPointerSizeLog2));
  FREE_REG(array_size);

  DEFINE_REG(param_index);
  __ Mov(param_index, xzr);

  // Store the receiver at the top of the stack.
  __ Str(receiver, MemOperand(sp, 0));

  // -------------------------------------------
  // Store signature-related values to the stack.
  // -------------------------------------------
  // We store values on the stack to restore them after function calls.
  __ Str(return_count, MemOperand(fp, kReturnCountOffset));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));

  Label prepare_for_js_call;
  __ Cmp(param_count, 0);
  // If we have 0 params: jump through parameter handling.
  __ B(&prepare_for_js_call, eq);

  // Loop through the params starting with the first.
  DEFINE_REG(current_param_slot_offset);
  __ Mov(current_param_slot_offset, Immediate(0));

  // We have to check the types of the params. The ValueType array contains
  // first the return then the param types.

  // Set the ValueType array pointer to point to the first parameter.
  constexpr int kValueTypeSize = sizeof(wasm::ValueType);
  static_assert(kValueTypeSize == 4);
  const int32_t kValueTypeSizeLog2 = log2(kValueTypeSize);
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr,
         Operand(return_count, LSL, kValueTypeSizeLog2));
  DEFINE_REG_W(valuetype);

  // -------------------------------------------
  // Copy reference type params first and initialize the stack for JS arguments.
  // -------------------------------------------

  // Heap pointers for ref type values in packed_args can be invalidated if GC
  // is triggered when converting wasm numbers to JS numbers and allocating
  // heap numbers. So, we have to move them to the stack first.
  Register param = target_js_function;  // x0
  {
    Label loop_copy_param_ref, load_ref_param, set_and_move;

    __ bind(&loop_copy_param_ref);
    __ Ldr(valuetype, MemOperand(valuetypes_array_ptr,
                                 wasm::ValueType::bit_field_offset()));
    __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
    __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
    __ B(&load_ref_param, eq);
    __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
    __ B(&load_ref_param, eq);

    // Initialize non-ref type slots to zero since they can be visited by GC
    // when converting wasm numbers into heap numbers.
    __ Mov(param, Smi::zero());

    Label inc_param_32bit;
    __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
    __ B(&inc_param_32bit, eq);
    __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
    __ B(&inc_param_32bit, eq);

    Label inc_param_64bit;
    __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
    __ B(&inc_param_64bit, eq);
    __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
    __ B(&inc_param_64bit, eq);

    // Invalid type. Wasm cannot pass Simd arguments to JavaScript.
    __ DebugBreak();

    __ bind(&inc_param_32bit);
    __ Add(current_param_slot_offset, current_param_slot_offset,
           Immediate(sizeof(int32_t)));
    __ B(&set_and_move);

    __ bind(&inc_param_64bit);
    __ Add(current_param_slot_offset, current_param_slot_offset,
           Immediate(sizeof(int64_t)));
    __ B(&set_and_move);

    __ bind(&load_ref_param);
    // No need to align packed_args for ref values in wasm-to-js, because the
    // alignment is only required for GC code that visits the stack, and in this
    // case we are storing into the stack only heap (or Smi) objects, always
    // aligned.
    __ Ldr(param, MemOperand(packed_args, current_param_slot_offset));
    __ Add(current_param_slot_offset, current_param_slot_offset,
           Immediate(kSystemPointerSize));

    __ bind(&set_and_move);
    __ Add(param_index, param_index, 1);
    // Pre-increment param_index to skip receiver slot.
    __ Str(param, MemOperand(sp, param_index, LSL, kSystemPointerSizeLog2));
    __ Add(valuetypes_array_ptr, valuetypes_array_ptr,
           Immediate(kValueTypeSize));
    __ Cmp(param_index, param_count);
    __ B(&loop_copy_param_ref, lt);
  }

  // Reset pointers for the second param conversion loop.
  __ Ldr(return_count, MemOperand(fp, kReturnCountOffset));
  __ Ldr(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr,
         Operand(return_count, LSL, kValueTypeSizeLog2));
  __ Mov(current_param_slot_offset, xzr);
  __ Mov(param_index, xzr);

  // -------------------------------------------
  // Param evaluation loop.
  // -------------------------------------------
  Label loop_through_params;
  __ bind(&loop_through_params);

  __ Ldr(valuetype,
         MemOperand(valuetypes_array_ptr, wasm::ValueType::bit_field_offset()));

  // -------------------------------------------
  // Param conversion.
  // -------------------------------------------
  // If param is a Smi we can easily convert it. Otherwise we'll call a builtin
  // for conversion.
  Label param_conversion_done, check_ref_param, skip_ref_param, convert_param;
  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&check_ref_param, ne);

  // I32 param: change to Smi.
  __ Ldr(param.W(), MemOperand(packed_args, current_param_slot_offset));

  // If pointer compression is disabled, we can convert to a smi.
  if (SmiValuesAre32Bits()) {
    __ SmiTag(param);
  } else {
    // Double the return value to test if it can be a Smi.
    __ Adds(wzr, param.W(), param.W());
    // If there was overflow, convert the return value to a HeapNumber.
    __ B(&convert_param, vs);
    // If there was no overflow, we can convert to Smi.
    __ SmiTag(param);
  }

  // Place the param into the proper slot.
  // Pre-increment param_index to skip the receiver slot.
  __ Add(param_index, param_index, 1);
  __ Str(param, MemOperand(sp, param_index, LSL, kSystemPointerSizeLog2));
  __ Add(current_param_slot_offset, current_param_slot_offset, sizeof(int32_t));

  __ B(&param_conversion_done);

  // Skip Ref params. We already copied reference params in the first loop.
  __ bind(&check_ref_param);
  __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
  __ B(&skip_ref_param, eq);
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
  __ B(&convert_param, ne);

  __ bind(&skip_ref_param);
  __ Add(param_index, param_index, 1);
  __ Add(current_param_slot_offset, current_param_slot_offset,
         Immediate(kSystemPointerSize));
  __ B(&param_conversion_done);

  // -------------------------------------------------
  // Param conversion builtins (Wasm type -> JS type).
  // -------------------------------------------------
  __ bind(&convert_param);

  // Prepare for builtin call.

  // Need to specify how many heap objects, that should be scanned by GC, are
  // on the top of the stack. (Only the context).
  // The builtin expects the parameter to be in register param = rax.

  __ Str(param_index, MemOperand(fp, kParamIndexOffset));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));
  __ Str(current_param_slot_offset, MemOperand(fp, kCurrentParamOffset));

  Label param_kWasmI32_not_smi;
  Label param_kWasmI64;
  Label param_kWasmF32;
  Label param_kWasmF64;
  Label finish_param_conversion;

  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&param_kWasmI32_not_smi, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
  __ B(&param_kWasmI64, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
  __ B(&param_kWasmF32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
  __ B(&param_kWasmF64, eq);

  // Invalid type. Wasm cannot pass Simd arguments to JavaScript.
  __ DebugBreak();

  Register increment = scratch;
  __ bind(&param_kWasmI32_not_smi);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmInt32ToHeapNumber),
          RelocInfo::CODE_TARGET);
  // Param is the result of the builtin.
  __ Mov(increment, Immediate(sizeof(int32_t)));
  __ jmp(&finish_param_conversion);

  __ bind(&param_kWasmI64);
  __ Ldr(param, MemOperand(packed_args, current_param_slot_offset));
  __ Call(BUILTIN_CODE(masm->isolate(), I64ToBigInt), RelocInfo::CODE_TARGET);
  __ Mov(increment, Immediate(sizeof(int64_t)));
  __ jmp(&finish_param_conversion);

  __ bind(&param_kWasmF32);
  __ Ldr(v0, MemOperand(packed_args, current_param_slot_offset));
  __ Call(BUILTIN_CODE(masm->isolate(), WasmFloat32ToNumber),
          RelocInfo::CODE_TARGET);
  __ Mov(increment, Immediate(sizeof(float)));
  __ jmp(&finish_param_conversion);

  __ bind(&param_kWasmF64);
  __ Ldr(d0, MemOperand(packed_args, current_param_slot_offset));
  __ Call(BUILTIN_CODE(masm->isolate(), WasmFloat64ToNumber),
          RelocInfo::CODE_TARGET);
  __ Mov(increment, Immediate(sizeof(double)));

  // Restore after builtin call.
  __ bind(&finish_param_conversion);

  __ Ldr(current_param_slot_offset, MemOperand(fp, kCurrentParamOffset));
  __ Add(current_param_slot_offset, current_param_slot_offset, increment);
  __ Ldr(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));
  __ Ldr(param_index, MemOperand(fp, kParamIndexOffset));
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(param_count, MemOperand(fp, kParamCountOffset));

  __ Add(param_index, param_index, 1);
  __ Str(param, MemOperand(sp, param_index, LSL, kSystemPointerSizeLog2));

  // -------------------------------------------
  // Param conversion done.
  // -------------------------------------------
  __ bind(&param_conversion_done);

  __ Add(valuetypes_array_ptr, valuetypes_array_ptr, Immediate(kValueTypeSize));
  __ Cmp(param_index, param_count);
  __ B(&loop_through_params, lt);

  // -------------------------------------------
  // Prepare for the function call.
  // -------------------------------------------
  __ bind(&prepare_for_js_call);

  // Reset thread_in_wasm_flag.
  __ Ldr(scratch, MemOperand(kRootRegister,
                             Isolate::thread_in_wasm_flag_address_offset()));
  __ Str(wzr, MemOperand(scratch, 0));  // 32 bit.

  regs.ResetExcept(param, packed_args, valuetypes_array_ptr, context,
                   return_count, valuetype, scratch);

  // -------------------------------------------
  // Call the JS function.
  // -------------------------------------------
  // Call_ReceiverIsAny expects the arguments in the stack in this order:
  // sp + (n * 0x08)  JS arg n-1
  // ...              ...
  // sp + 0x08        JS arg 0
  // sp               Receiver
  //
  // It also expects two arguments passed in registers:
  // x0: number of arguments + 1 (receiver)
  // x1: target (JSFunction|JSBoundFunction|...)

  // x0: Receiver.
  __ Ldr(x0, MemOperand(fp, kParamCountOffset));
  __ Add(x0, x0, 1);  // Add 1 to count receiver.

  // x1: callable.
  __ Ldr(kJSFunctionRegister, MemOperand(fp, kCallableOffset));

  __ Call(BUILTIN_CODE(masm->isolate(), Call_ReceiverIsAny),
          RelocInfo::CODE_TARGET);

  __ Ldr(cp, MemOperand(fp, kContextOffset));

  // The JS function returns its result in register x0.
  Register return_reg = kReturnRegister0;

  // No slots to visit during GC.
  __ Mov(scratch, sp);
  __ Str(scratch,
         MemOperand(fp,
                    WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset));

  // -------------------------------------------
  // Return handling.
  // -------------------------------------------
  __ Ldr(return_count, MemOperand(fp, kReturnCountOffset));
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(signature, MemOperand(fp, kSignatureOffset));
  __ Ldr(valuetypes_array_ptr,
         MemOperand(signature, wasm::FunctionSig::kRepsOffset));

  DEFINE_REG(result_index);
  __ Mov(result_index, xzr);
  DEFINE_REG(current_result_offset);
  __ Mov(current_result_offset, xzr);

  // If we have return values, convert them from JS types back to Wasm types.
  Label convert_return;
  Label return_done;
  Label all_done;
  Label loop_copy_return_refs;
  __ cmp(return_count, Immediate(1));
  __ B(&all_done, lt);
  __ B(&convert_return, eq);

  // We have multiple results. Convert the result into a FixedArray.
  DEFINE_REG(fixed_array);
  __ Mov(fixed_array, xzr);

  // The builtin expects three args:
  // x0: object.
  // x1: return_count as Smi.
  // x27 (cp): context.
  __ Ldr(x1, MemOperand(fp, kReturnCountOffset));
  __ Add(x1, x1, x1);

  // One tagged object at the top of the stack (the context).
  __ Add(scratch, sp, Immediate(kSystemPointerSize));
  __ Str(scratch,
         MemOperand(fp,
                    WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset));

  __ Call(BUILTIN_CODE(masm->isolate(), IterableToFixedArrayForWasm),
          RelocInfo::CODE_TARGET);
  __ Mov(fixed_array, kReturnRegister0);

  // Store fixed_array at the second top of the stack (in place of callable).
  __ Str(fixed_array, MemOperand(sp, kSystemPointerSize));
  __ Add(scratch, sp, Immediate(2 * kSystemPointerSize));
  __ Str(scratch,
         MemOperand(fp,
                    WasmToJSInterpreterFrameConstants::kGCScanSlotLimitOffset));

  __ Ldr(return_count, MemOperand(fp, kReturnCountOffset));
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(signature, MemOperand(fp, kSignatureOffset));
  __ Ldr(valuetypes_array_ptr,
         MemOperand(signature, wasm::FunctionSig::kRepsOffset));
  __ Mov(result_index, xzr);
  __ Mov(current_result_offset, xzr);

  __ Add(scratch, fixed_array,
         OFFSET_OF_DATA_START(FixedArray) - kHeapObjectTag);
  __ LoadTaggedField(return_reg,
                     MemOperand(scratch, result_index, LSL, kTaggedSizeLog2));

  // -------------------------------------------
  // Return conversions (JS type -> Wasm type).
  // -------------------------------------------
  __ bind(&convert_return);

  // Save registers in the stack before the builtin call.
  __ Str(current_result_offset, MemOperand(fp, kCurrentResultOffset));
  __ Str(result_index, MemOperand(fp, kResultIndexOffset));
  __ Str(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));

  // The builtin expects the parameter to be in register param = x0.

  // The first valuetype of the array is the return's valuetype.
  __ Ldr(valuetype,
         MemOperand(valuetypes_array_ptr, wasm::ValueType::bit_field_offset()));

  Label return_kWasmI32;
  Label return_kWasmI32_not_smi;
  Label return_kWasmI64;
  Label return_kWasmF32;
  Label return_kWasmF64;
  Label return_kWasmRef;

  // Prepare for builtin call.

  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&return_kWasmI32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
  __ B(&return_kWasmI64, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
  __ B(&return_kWasmF32, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
  __ B(&return_kWasmF64, eq);

  __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
  __ B(&return_kWasmRef, eq);
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
  __ B(&return_kWasmRef, eq);

  // Invalid type. JavaScript cannot return Simd results to WebAssembly.
  __ DebugBreak();

  __ bind(&return_kWasmI32);
  __ JumpIfNotSmi(return_reg, &return_kWasmI32_not_smi);
  // Change the param from Smi to int32.
  __ SmiUntag(return_reg);
  __ AssertZeroExtended(return_reg);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Str(return_reg.W(), MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(int32_t)));
  __ jmp(&return_done);

  __ bind(&return_kWasmI32_not_smi);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedNonSmiToInt32),
          RelocInfo::CODE_TARGET);
  __ AssertZeroExtended(return_reg);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(current_result_offset, MemOperand(fp, kCurrentResultOffset));
  __ Str(return_reg.W(), MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(int32_t)));
  __ jmp(&return_done);

  __ bind(&return_kWasmI64);
  __ Call(BUILTIN_CODE(masm->isolate(), BigIntToI64), RelocInfo::CODE_TARGET);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(current_result_offset, MemOperand(fp, kCurrentResultOffset));
  __ Str(return_reg, MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(int64_t)));
  __ jmp(&return_done);

  __ bind(&return_kWasmF32);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedToFloat32),
          RelocInfo::CODE_TARGET);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(current_result_offset, MemOperand(fp, kCurrentResultOffset));
  __ Str(kFPReturnRegister0, MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(float)));
  __ jmp(&return_done);

  __ bind(&return_kWasmF64);
  __ Call(BUILTIN_CODE(masm->isolate(), WasmTaggedToFloat64),
          RelocInfo::CODE_TARGET);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(current_result_offset, MemOperand(fp, kCurrentResultOffset));
  __ Str(kFPReturnRegister0, MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(double)));
  __ jmp(&return_done);

  __ bind(&return_kWasmRef);
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Str(return_reg,
         MemOperand(packed_args, result_index, LSL, kSystemPointerSizeLog2));
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(double)));

  // A result converted.
  __ bind(&return_done);

  // Restore after builtin call
  __ Ldr(cp, MemOperand(sp, 0));
  __ Ldr(fixed_array, MemOperand(sp, kSystemPointerSize));
  __ Ldr(valuetypes_array_ptr, MemOperand(fp, kValueTypesArrayStartOffset));
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr, Immediate(kValueTypeSize));
  __ Ldr(result_index, MemOperand(fp, kResultIndexOffset));
  __ Add(result_index, result_index, Immediate(1));
  __ Ldr(scratch, MemOperand(fp, kReturnCountOffset));
  __ cmp(result_index, scratch);  // result_index == return_count?
  __ B(&loop_copy_return_refs, ge);

  __ Add(scratch, fixed_array,
         OFFSET_OF_DATA_START(FixedArray) - kHeapObjectTag);
  __ LoadTaggedField(return_reg,
                     MemOperand(scratch, result_index, LSL, kTaggedSizeLog2));
  __ jmp(&convert_return);

  // -------------------------------------------
  // Update refs after calling all builtins.
  // -------------------------------------------

  // Some builtin calls for return value conversion may trigger GC, and some
  // heap pointers of ref types might become invalid in the conversion loop.
  // Thus, copy the ref values again after finishing all the conversions.
  __ bind(&loop_copy_return_refs);

  // If there is only one return value, there should be no heap pointer in the
  // packed_args while calling any builtin. So, we don't need to update refs.
  __ Ldr(return_count, MemOperand(fp, kReturnCountOffset));
  __ cmp(return_count, Immediate(1));
  __ B(&all_done, eq);

  Label copy_return_if_ref, copy_return_ref, done_copy_return_ref;
  __ Ldr(packed_args, MemOperand(fp, kPackedArrayOffset));
  __ Ldr(signature, MemOperand(fp, kSignatureOffset));
  __ Ldr(valuetypes_array_ptr,
         MemOperand(signature, wasm::FunctionSig::kRepsOffset));
  __ Mov(result_index, xzr);
  __ Mov(current_result_offset, xzr);

  // Copy if the current return value is a ref type.
  __ bind(&copy_return_if_ref);
  __ Ldr(valuetype,
         MemOperand(valuetypes_array_ptr, wasm::ValueType::bit_field_offset()));

  __ And(valuetype, valuetype, Immediate(wasm::kWasmValueKindBitsMask));
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRefNull));
  __ B(&copy_return_ref, eq);
  __ cmp(valuetype, Immediate(wasm::ValueKind::kRef));
  __ B(&copy_return_ref, eq);

  Label inc_result_32bit;
  __ cmp(valuetype, Immediate(wasm::kWasmI32.raw_bit_field()));
  __ B(&inc_result_32bit, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF32.raw_bit_field()));
  __ B(&inc_result_32bit, eq);

  Label inc_result_64bit;
  __ cmp(valuetype, Immediate(wasm::kWasmI64.raw_bit_field()));
  __ B(&inc_result_64bit, eq);
  __ cmp(valuetype, Immediate(wasm::kWasmF64.raw_bit_field()));
  __ B(&inc_result_64bit, eq);

  // Invalid type. JavaScript cannot return Simd values to WebAssembly.
  __ DebugBreak();

  __ bind(&inc_result_32bit);
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(int32_t)));
  __ jmp(&done_copy_return_ref);

  __ bind(&inc_result_64bit);
  __ Add(current_result_offset, current_result_offset,
         Immediate(sizeof(int64_t)));
  __ jmp(&done_copy_return_ref);

  __ bind(&copy_return_ref);
  __ Add(scratch, fixed_array,
         OFFSET_OF_DATA_START(FixedArray) - kHeapObjectTag);
  __ LoadTaggedField(return_reg,
                     MemOperand(scratch, result_index, LSL, kTaggedSizeLog2));
  __ Str(return_reg, MemOperand(packed_args, current_result_offset));
  __ Add(current_result_offset, current_result_offset,
         Immediate(kSystemPointerSize));

  // Move pointers.
  __ bind(&done_copy_return_ref);
  __ Add(valuetypes_array_ptr, valuetypes_array_ptr, Immediate(kValueTypeSize));
  __ Add(result_index, result_index, Immediate(1));
  __ cmp(result_index, return_count);
  __ B(&copy_return_if_ref, lt);

  // -------------------------------------------
  // All done.
  // -------------------------------------------

  __ bind(&all_done);
  // Set thread_in_wasm_flag.
  DEFINE_REG_W(scratch32);
  __ Ldr(scratch, MemOperand(kRootRegister,
                             Isolate::thread_in_wasm_flag_address_offset()));
  __ Mov(scratch32, Immediate(1));
  __ Str(scratch32, MemOperand(scratch, 0));  // 32 bit.

  // Deconstruct the stack frame.
  __ LeaveFrame(StackFrame::WASM_TO_JS);

  __ Mov(x0, xzr);
  __ Ret(lr);
}

#endif  // V8_ENABLE_WEBASSEMBLY

#undef __

}  // namespace internal
}  // namespace v8
