// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_IA32

#include "src/api/api-arguments.h"
#include "src/base/bits-iterator.h"
#include "src/base/iterator.h"
#include "src/builtins/builtins-descriptors.h"
#include "src/builtins/builtins-inl.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/interface-descriptors-inl.h"
// For interpreter_entry_return_pc_offset. TODO(jkummerow): Drop.
#include "src/codegen/macro-assembler-inl.h"
#include "src/codegen/register-configuration.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/frame-constants.h"
#include "src/execution/frames.h"
#include "src/heap/heap-inl.h"
#include "src/logging/counters.h"
#include "src/objects/cell.h"
#include "src/objects/foreign.h"
#include "src/objects/heap-number.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/object-access.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

void Builtins::Generate_Adaptor(MacroAssembler* masm, Address address) {
  __ Move(kJavaScriptCallExtraArg1Register,
          Immediate(ExternalReference::Create(address)));
  __ TailCallBuiltin(Builtin::kAdaptorWithBuiltinExitFrame);
}

namespace {

constexpr int kReceiverOnStackSize = kSystemPointerSize;

enum class ArgumentsElementType {
  kRaw,    // Push arguments as they are.
  kHandle  // Dereference arguments before pushing.
};

void Generate_PushArguments(MacroAssembler* masm, Register array, Register argc,
                            Register scratch1, Register scratch2,
                            ArgumentsElementType element_type) {
  DCHECK(!AreAliased(array, argc, scratch1, scratch2));
  Register counter = scratch1;
  Label loop, entry;
  __ lea(counter, Operand(argc, -kJSArgcReceiverSlots));
  __ jmp(&entry);
  __ bind(&loop);
  Operand value(array, counter, times_system_pointer_size, 0);
  if (element_type == ArgumentsElementType::kHandle) {
    DCHECK(scratch2 != no_reg);
    __ mov(scratch2, value);
    value = Operand(scratch2, 0);
  }
  __ Push(value);
  __ bind(&entry);
  __ dec(counter);
  __ j(greater_equal, &loop, Label::kNear);
}

void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax: number of arguments
  //  -- edi: constructor function
  //  -- edx: new target
  //  -- esi: context
  // -----------------------------------

  Label stack_overflow;

  __ StackOverflowCheck(eax, ecx, &stack_overflow);

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);

    // Preserve the incoming parameters on the stack.
    __ push(esi);
    __ push(eax);

    // TODO(victorgomes): When the arguments adaptor is completely removed, we
    // should get the formal parameter count and copy the arguments in its
    // correct position (including any undefined), instead of delaying this to
    // InvokeFunction.

    // Set up pointer to first argument (skip receiver).
    __ lea(esi, Operand(ebp, StandardFrameConstants::kFixedFrameSizeAboveFp +
                                 kSystemPointerSize));
    // Copy arguments to the expression stack.
    // esi: Pointer to start of arguments.
    // eax: Number of arguments.
    Generate_PushArguments(masm, esi, eax, ecx, no_reg,
                           ArgumentsElementType::kRaw);
    // The receiver for the builtin/api call.
    __ PushRoot(RootIndex::kTheHoleValue);

    // Call the function.
    // eax: number of arguments (untagged)
    // edi: constructor function
    // edx: new target
    // Reload context from the frame.
    __ mov(esi, Operand(ebp, ConstructFrameConstants::kContextOffset));
    __ InvokeFunction(edi, edx, eax, InvokeType::kCall);

    // Restore context from the frame.
    __ mov(esi, Operand(ebp, ConstructFrameConstants::kContextOffset));
    // Restore arguments count from the frame.
    __ mov(edx, Operand(ebp, ConstructFrameConstants::kLengthOffset));
    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  __ DropArguments(edx, ecx);
  __ ret(0);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ int3();  // This should be unreachable.
  }
}

}  // namespace

// The construct stub for ES5 constructor functions and ES6 class constructors.
void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax: number of arguments (untagged)
  //  -- edi: constructor function
  //  -- edx: new target
  //  -- esi: context
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  FrameScope scope(masm, StackFrame::MANUAL);
  // Enter a construct frame.
  __ EnterFrame(StackFrame::CONSTRUCT);

  Label post_instantiation_deopt_entry, not_create_implicit_receiver;

  // Preserve the incoming parameters on the stack.
  __ Push(esi);
  __ Push(eax);
  __ Push(edi);
  __ PushRoot(RootIndex::kTheHoleValue);
  __ Push(edx);

  // ----------- S t a t e -------------
  //  --         sp[0*kSystemPointerSize]: new target
  //  --         sp[1*kSystemPointerSize]: padding
  //  -- edi and sp[2*kSystemPointerSize]: constructor function
  //  --         sp[3*kSystemPointerSize]: argument count
  //  --         sp[4*kSystemPointerSize]: context
  // -----------------------------------

  __ mov(eax, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ mov(eax, FieldOperand(eax, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(eax);
  __ JumpIfIsInRange(
      eax, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor), ecx,
      &not_create_implicit_receiver, Label::kNear);

  // If not derived class constructor: Allocate the new receiver object.
  __ CallBuiltin(Builtin::kFastNewObject);
  __ jmp(&post_instantiation_deopt_entry, Label::kNear);

  // Else: use TheHoleValue as receiver for constructor call
  __ bind(&not_create_implicit_receiver);
  __ LoadRoot(eax, RootIndex::kTheHoleValue);

  // ----------- S t a t e -------------
  //  --                         eax: implicit receiver
  //  -- Slot 4 / sp[0*kSystemPointerSize]: new target
  //  -- Slot 3 / sp[1*kSystemPointerSize]: padding
  //  -- Slot 2 / sp[2*kSystemPointerSize]: constructor function
  //  -- Slot 1 / sp[3*kSystemPointerSize]: number of arguments
  //  -- Slot 0 / sp[4*kSystemPointerSize]: context
  // -----------------------------------
  // Deoptimizer enters here.
  masm->isolate()->heap()->SetConstructStubCreateDeoptPCOffset(
      masm->pc_offset());
  __ bind(&post_instantiation_deopt_entry);

  // Restore new target.
  __ Pop(edx);

  // Push the allocated receiver to the stack.
  __ Push(eax);

  // We need two copies because we may have to return the original one
  // and the calling conventions dictate that the called function pops the
  // receiver. The second copy is pushed after the arguments, we saved in xmm0
  // since eax needs to store the number of arguments before
  // InvokingFunction.
  __ movd(xmm0, eax);

  // Set up pointer to first argument (skip receiver).
  __ lea(edi, Operand(ebp, StandardFrameConstants::kFixedFrameSizeAboveFp +
                               kSystemPointerSize));

  // Restore argument count.
  __ mov(eax, Operand(ebp, ConstructFrameConstants::kLengthOffset));

  // Check if we have enough stack space to push all arguments.
  // Argument count in eax. Clobbers ecx.
  Label stack_overflow;
  __ StackOverflowCheck(eax, ecx, &stack_overflow);

  // TODO(victorgomes): When the arguments adaptor is completely removed, we
  // should get the formal parameter count and copy the arguments in its
  // correct position (including any undefined), instead of delaying this to
  // InvokeFunction.

  // Copy arguments to the expression stack.
  // edi: Pointer to start of arguments.
  // eax: Number of arguments.
  Generate_PushArguments(masm, edi, eax, ecx, no_reg,
                         ArgumentsElementType::kRaw);

  // Push implicit receiver.
  __ movd(ecx, xmm0);
  __ Push(ecx);

  // Restore and and call the constructor function.
  __ mov(edi, Operand(ebp, ConstructFrameConstants::kConstructorOffset));
  __ InvokeFunction(edi, edx, eax, InvokeType::kCall);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.

  Label check_result, use_receiver, do_throw, leave_and_return;
  // If the result is undefined, we jump out to using the implicit receiver.
  __ JumpIfNotRoot(eax, RootIndex::kUndefinedValue, &check_result,
                   Label::kNear);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ mov(eax, Operand(esp, 0 * kSystemPointerSize));
  __ JumpIfRoot(eax, RootIndex::kTheHoleValue, &do_throw);

  __ bind(&leave_and_return);
  // Restore arguments count from the frame.
  __ mov(edx, Operand(ebp, ConstructFrameConstants::kLengthOffset));
  __ LeaveFrame(StackFrame::CONSTRUCT);

  // Remove caller arguments from the stack and return.
  __ DropArguments(edx, ecx);
  __ ret(0);

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_result);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(eax, &use_receiver, Label::kNear);

  // If the type of the result (stored in its map) is less than
  // FIRST_JS_RECEIVER_TYPE, it is not an object in the ECMA sense.
  static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
  __ CmpObjectType(eax, FIRST_JS_RECEIVER_TYPE, ecx);
  __ j(above_equal, &leave_and_return, Label::kNear);
  __ jmp(&use_receiver, Label::kNear);

  __ bind(&do_throw);
  // Restore context from the frame.
  __ mov(esi, Operand(ebp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  // This should be unreachable.
  __ int3();

  __ bind(&stack_overflow);
  // Restore context from the frame.
  __ mov(esi, Operand(ebp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowStackOverflow);
  // This should be unreachable.
  __ int3();
}

void Builtins::Generate_JSBuiltinsConstructStub(MacroAssembler* masm) {
  Generate_JSBuiltinsConstructStubHelper(masm);
}

void Builtins::Generate_ConstructedNonConstructable(MacroAssembler* masm) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  __ push(edi);
  __ CallRuntime(Runtime::kThrowConstructedNonConstructable);
}

namespace {

// Called with the native C calling convention. The corresponding function
// signature is either:
//
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, Address new_target, Address target,
//       Address receiver, intptr_t argc, Address** argv)>;
// or
//   using JSEntryFunction = GeneratedCode<Address(
//       Address root_register_value, MicrotaskQueue* microtask_queue)>;
void Generate_JSEntryVariant(MacroAssembler* masm, StackFrame::Type type,
                             Builtin entry_trampoline) {
  Label invoke, handler_entry, exit;
  Label not_outermost_js, not_outermost_js_2;

  {
    NoRootArrayScope uninitialized_root_register(masm);

    // Set up frame.
    __ push(ebp);
    __ mov(ebp, esp);

    // Push marker in two places.
    __ push(Immediate(StackFrame::TypeToMarker(type)));
    // Reserve a slot for the context. It is filled after the root register has
    // been set up.
    __ AllocateStackSpace(kSystemPointerSize);
    // Save callee-saved registers (C calling conventions).
    __ push(edi);
    __ push(esi);
    __ push(ebx);

    // Initialize the root register based on the given Isolate* argument.
    // C calling convention. The first argument is passed on the stack.
    __ mov(kRootRegister,
           Operand(ebp, EntryFrameConstants::kRootRegisterValueOffset));
  }

  // Save copies of the top frame descriptor on the stack.
  ExternalReference c_entry_fp = ExternalReference::Create(
      IsolateAddressId::kCEntryFPAddress, masm->isolate());
  __ push(__ ExternalReferenceAsOperand(c_entry_fp, edi));

  __ push(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));

  __ push(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));

  // Clear c_entry_fp, now we've pushed its previous value to the stack.
  // If the c_entry_fp is not already zero and we don't clear it, the
  // StackFrameIteratorForProfiler will assume we are executing C++ and miss the
  // JS frames on top.
  __ mov(__ ExternalReferenceAsOperand(c_entry_fp, edi), Immediate(0));
  __ mov(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP),
         Immediate(0));
  __ mov(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC),
         Immediate(0));

  // Store the context address in the previously-reserved slot.
  ExternalReference context_address = ExternalReference::Create(
      IsolateAddressId::kContextAddress, masm->isolate());
  __ mov(edi, __ ExternalReferenceAsOperand(context_address, edi));
  static constexpr int kOffsetToContextSlot = -2 * kSystemPointerSize;
  __ mov(Operand(ebp, kOffsetToContextSlot), edi);

  // If this is the outermost JS call, set js_entry_sp value.
  ExternalReference js_entry_sp = ExternalReference::Create(
      IsolateAddressId::kJSEntrySPAddress, masm->isolate());
  __ cmp(__ ExternalReferenceAsOperand(js_entry_sp, edi), Immediate(0));
  __ j(not_equal, &not_outermost_js, Label::kNear);
  __ mov(__ ExternalReferenceAsOperand(js_entry_sp, edi), ebp);
  __ push(Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ jmp(&invoke, Label::kNear);
  __ bind(&not_outermost_js);
  __ push(Immediate(StackFrame::INNER_JSENTRY_FRAME));

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the exception.
  __ jmp(&invoke);
  __ bind(&handler_entry);

  // Store the current pc as the handler offset. It's used later to create the
  // handler table.
  masm->isolate()->builtins()->SetJSEntryHandlerOffset(handler_entry.pos());

  // Caught exception: Store result (exception) in the exception
  // field in the JSEnv and return a failure sentinel.
  ExternalReference exception = ExternalReference::Create(
      IsolateAddressId::kExceptionAddress, masm->isolate());
  __ mov(__ ExternalReferenceAsOperand(exception, edi), eax);

  __ Move(eax, masm->isolate()->factory()->exception());
  __ jmp(&exit);

  // Invoke: Link this frame into the handler chain.
  __ bind(&invoke);
  __ PushStackHandler(edi);

  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return.
  __ CallBuiltin(entry_trampoline);

  // Unlink this frame from the handler chain.
  __ PopStackHandler(edi);

  __ bind(&exit);

  // Check if the current stack frame is marked as the outermost JS frame.
  __ pop(edi);
  __ cmp(edi, Immediate(StackFrame::OUTERMOST_JSENTRY_FRAME));
  __ j(not_equal, &not_outermost_js_2);
  __ mov(__ ExternalReferenceAsOperand(js_entry_sp, edi), Immediate(0));
  __ bind(&not_outermost_js_2);

  // Restore the top frame descriptor from the stack.
  __ pop(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerPC));
  __ pop(__ ExternalReferenceAsOperand(IsolateFieldId::kFastCCallCallerFP));
  __ pop(__ ExternalReferenceAsOperand(c_entry_fp, edi));

  // Restore callee-saved registers (C calling conventions).
  __ pop(ebx);
  __ pop(esi);
  __ pop(edi);
  __ add(esp, Immediate(2 * kSystemPointerSize));  // remove markers

  // Restore frame pointer and return.
  __ pop(ebp);
  __ ret(0);
}

}  // namespace

void Builtins::Generate_JSEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY, Builtin::kJSEntryTrampoline);
}

void Builtins::Generate_JSConstructEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::CONSTRUCT_ENTRY,
                          Builtin::kJSConstructEntryTrampoline);
}

void Builtins::Generate_JSRunMicrotasksEntry(MacroAssembler* masm) {
  Generate_JSEntryVariant(masm, StackFrame::ENTRY,
                          Builtin::kRunMicrotasksTrampoline);
}

static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);

    const Register scratch1 = edx;
    const Register scratch2 = edi;

    // Setup the context (we need to use the caller context from the isolate).
    ExternalReference context_address = ExternalReference::Create(
        IsolateAddressId::kContextAddress, masm->isolate());
    __ mov(esi, __ ExternalReferenceAsOperand(context_address, scratch1));

    // Load the previous frame pointer (edx) to access C arguments
    __ mov(scratch1, Operand(ebp, 0));

    // Push the function.
    __ push(Operand(scratch1, EntryFrameConstants::kFunctionArgOffset));

    // Load the number of arguments and setup pointer to the arguments.
    __ mov(eax, Operand(scratch1, EntryFrameConstants::kArgcOffset));
    __ mov(scratch1, Operand(scratch1, EntryFrameConstants::kArgvOffset));

    // Check if we have enough stack space to push all arguments.
    // Argument count in eax. Clobbers ecx.
    Label enough_stack_space, stack_overflow;
    __ StackOverflowCheck(eax, ecx, &stack_overflow);
    __ jmp(&enough_stack_space);

    __ bind(&stack_overflow);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    // This should be unreachable.
    __ int3();

    __ bind(&enough_stack_space);

    // Copy arguments to the stack.
    // scratch1 (edx): Pointer to start of arguments.
    // eax: Number of arguments.
    Generate_PushArguments(masm, scratch1, eax, ecx, scratch2,
                           ArgumentsElementType::kHandle);

    // Load the previous frame pointer to access C arguments
    __ mov(scratch2, Operand(ebp, 0));

    // Push the receiver onto the stack.
    __ push(Operand(scratch2, EntryFrameConstants::kReceiverArgOffset));

    // Get the new.target and function from the frame.
    __ mov(edx, Operand(scratch2, EntryFrameConstants::kNewTargetArgOffset));
    __ mov(edi, Operand(scratch2, EntryFrameConstants::kFunctionArgOffset));

    // Invoke the code.
    Builtin builtin = is_construct ? Builtin::kConstruct : Builtins::Call();
    __ CallBuiltin(builtin);

    // Exit the internal frame. Notice that this also removes the empty.
    // context and the function left on the stack by the code
    // invocation.
  }
  __ ret(0);
}

void Builtins::Generate_JSEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, false);
}

void Builtins::Generate_JSConstructEntryTrampoline(MacroAssembler* masm) {
  Generate_JSEntryTrampolineHelper(masm, true);
}

void Builtins::Generate_RunMicrotasksTrampoline(MacroAssembler* masm) {
  // This expects two C++ function parameters passed by Invoke() in
  // execution.cc.
  //   r1: microtask_queue
  __ mov(RunMicrotasksDescriptor::MicrotaskQueueRegister(),
         Operand(ebp, EntryFrameConstants::kMicrotaskQueueArgOffset));
  __ TailCallBuiltin(Builtin::kRunMicrotasks);
}

static void GetSharedFunctionInfoBytecode(MacroAssembler* masm,
                                          Register sfi_data,
                                          Register scratch1) {
  Label done;

  __ CmpObjectType(sfi_data, INTERPRETER_DATA_TYPE, scratch1);
  __ j(not_equal, &done, Label::kNear);
  __ mov(sfi_data,
         FieldOperand(sfi_data, InterpreterData::kBytecodeArrayOffset));

  __ bind(&done);
}

static void AssertCodeIsBaseline(MacroAssembler* masm, Register code,
                                 Register scratch) {
  DCHECK(!AreAliased(code, scratch));
  // Verify that the code kind is baseline code via the CodeKind.
  __ mov(scratch, FieldOperand(code, Code::kFlagsOffset));
  __ DecodeField<Code::KindField>(scratch);
  __ cmp(scratch, Immediate(static_cast<int>(CodeKind::BASELINE)));
  __ Assert(equal, AbortReason::kExpectedBaselineData);
}

static void GetSharedFunctionInfoBytecodeOrBaseline(
    MacroAssembler* masm, Register sfi, Register bytecode, Register scratch1,
    Label* is_baseline, Label* is_unavailable) {
  ASM_CODE_COMMENT(masm);
  Label done;

  Register data = bytecode;
  __ mov(data,
         FieldOperand(sfi, SharedFunctionInfo::kTrustedFunctionDataOffset));

  __ LoadMap(scratch1, data);

#ifndef V8_JITLESS
  __ CmpInstanceType(scratch1, CODE_TYPE);
  if (v8_flags.debug_code) {
    Label not_baseline;
    __ j(not_equal, &not_baseline);
    AssertCodeIsBaseline(masm, data, scratch1);
    __ j(equal, is_baseline);
    __ bind(&not_baseline);
  } else {
    __ j(equal, is_baseline);
  }
#endif  // !V8_JITLESS

  __ CmpInstanceType(scratch1, BYTECODE_ARRAY_TYPE);
  __ j(equal, &done, Label::kNear);

  __ CmpInstanceType(scratch1, INTERPRETER_DATA_TYPE);
  __ j(not_equal, is_unavailable);
  __ mov(data, FieldOperand(data, InterpreterData::kBytecodeArrayOffset));

  __ bind(&done);
}

// static
void Builtins::Generate_ResumeGeneratorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax    : the value to pass to the generator
  //  -- edx    : the JSGeneratorObject to resume
  //  -- esp[0] : return address
  // -----------------------------------
  // Store input value into generator object.
  __ mov(FieldOperand(edx, JSGeneratorObject::kInputOrDebugPosOffset), eax);
  Register object = WriteBarrierDescriptor::ObjectRegister();
  __ mov(object, edx);
  __ RecordWriteField(object, JSGeneratorObject::kInputOrDebugPosOffset, eax,
                      WriteBarrierDescriptor::SlotAddressRegister(),
                      SaveFPRegsMode::kIgnore);
  // Check that edx is still valid, RecordWrite might have clobbered it.
  __ AssertGeneratorObject(edx);

  // Load suspended function and context.
  __ mov(edi, FieldOperand(edx, JSGeneratorObject::kFunctionOffset));
  __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));

  // Flood function if we are stepping.
  Label prepare_step_in_if_stepping, prepare_step_in_suspended_generator;
  Label stepping_prepared;
  ExternalReference debug_hook =
      ExternalReference::debug_hook_on_function_call_address(masm->isolate());
  __ cmpb(__ ExternalReferenceAsOperand(debug_hook, ecx), Immediate(0));
  __ j(not_equal, &prepare_step_in_if_stepping);

  // Flood function if we need to continue stepping in the suspended generator.
  ExternalReference debug_suspended_generator =
      ExternalReference::debug_suspended_generator_address(masm->isolate());
  __ cmp(edx, __ ExternalReferenceAsOperand(debug_suspended_generator, ecx));
  __ j(equal, &prepare_step_in_suspended_generator);
  __ bind(&stepping_prepared);

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
  __ CompareStackLimit(esp, StackLimitKind::kRealStackLimit);
  __ j(below, &stack_overflow);

  // Pop return address.
  __ PopReturnAddressTo(eax);

  // ----------- S t a t e -------------
  //  -- eax    : return address
  //  -- edx    : the JSGeneratorObject to resume
  //  -- edi    : generator function
  //  -- esi    : generator context
  // -----------------------------------

  {
    __ movd(xmm0, ebx);

    // Copy the function arguments from the generator object's register file.
    __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ movzx_w(ecx, FieldOperand(
                        ecx, SharedFunctionInfo::kFormalParameterCountOffset));
    __ dec(ecx);  // Exclude receiver.
    __ mov(ebx,
           FieldOperand(edx, JSGeneratorObject::kParametersAndRegistersOffset));
    {
      Label done_loop, loop;
      __ bind(&loop);
      __ dec(ecx);
      __ j(less, &done_loop);
      __ Push(
          FieldOperand(ebx, ecx, times_tagged_size, FixedArray::kHeaderSize));
      __ jmp(&loop);
      __ bind(&done_loop);
    }

    // Push receiver.
    __ Push(FieldOperand(edx, JSGeneratorObject::kReceiverOffset));

    // Restore registers.
    __ mov(edi, FieldOperand(edx, JSGeneratorObject::kFunctionOffset));
    __ movd(ebx, xmm0);
  }

  // Underlying function needs to have bytecode available.
  if (v8_flags.debug_code) {
    Label is_baseline, is_unavailable, ok;
    __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ Push(eax);
    GetSharedFunctionInfoBytecodeOrBaseline(masm, ecx, ecx, eax, &is_baseline,
                                            &is_unavailable);
    __ Pop(eax);
    __ jmp(&ok);

    __ bind(&is_unavailable);
    __ Abort(AbortReason::kMissingBytecodeArray);

    __ bind(&is_baseline);
    __ Pop(eax);
    __ CmpObjectType(ecx, CODE_TYPE, ecx);
    __ Assert(equal, AbortReason::kMissingBytecodeArray);

    __ bind(&ok);
  }

  // Resume (Ignition/TurboFan) generator object.
  {
    __ PushReturnAddressFrom(eax);
    __ mov(eax, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
    __ movzx_w(eax, FieldOperand(
                        eax, SharedFunctionInfo::kFormalParameterCountOffset));
    // We abuse new.target both to indicate that this is a resume call and to
    // pass in the generator object.  In ordinary calls, new.target is always
    // undefined because generator functions are non-constructable.
    __ JumpJSFunction(edi);
  }

  __ bind(&prepare_step_in_if_stepping);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(edx);
    __ Push(edi);
    // Push hole as receiver since we do not use it for stepping.
    __ PushRoot(RootIndex::kTheHoleValue);
    __ CallRuntime(Runtime::kDebugOnFunctionCall);
    __ Pop(edx);
    __ mov(edi, FieldOperand(edx, JSGeneratorObject::kFunctionOffset));
  }
  __ jmp(&stepping_prepared);

  __ bind(&prepare_step_in_suspended_generator);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(edx);
    __ CallRuntime(Runtime::kDebugPrepareStepInSuspendedGenerator);
    __ Pop(edx);
    __ mov(edi, FieldOperand(edx, JSGeneratorObject::kFunctionOffset));
  }
  __ jmp(&stepping_prepared);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ int3();  // This should be unreachable.
  }
}

static void LeaveInterpreterFrame(MacroAssembler* masm, Register scratch1,
                                  Register scratch2) {
  ASM_CODE_COMMENT(masm);
  Register params_size = scratch1;
  // Get the size of the formal parameters (in bytes).
  __ mov(params_size,
         Operand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ movzx_w(params_size,
             FieldOperand(params_size, BytecodeArray::kParameterSizeOffset));

  Register actual_params_size = scratch2;
  // Compute the size of the actual parameters (in bytes).
  __ mov(actual_params_size, Operand(ebp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  __ cmp(params_size, actual_params_size);
  __ cmov(kLessThan, params_size, actual_params_size);

  // Leave the frame (also dropping the register file).
  __ leave();

  // Drop receiver + arguments.
  __ DropArguments(params_size, scratch2);
}

// Advance the current bytecode offset. This simulates what all bytecode
// handlers do upon completion of the underlying operation. Will bail out to a
// label if the bytecode (without prefix) is a return bytecode. Will not advance
// the bytecode offset if the current bytecode is a JumpLoop, instead just
// re-executing the JumpLoop to jump to the correct bytecode.
static void AdvanceBytecodeOffsetOrReturn(MacroAssembler* masm,
                                          Register bytecode_array,
                                          Register bytecode_offset,
                                          Register scratch1, Register scratch2,
                                          Register scratch3, Label* if_return) {
  ASM_CODE_COMMENT(masm);
  Register bytecode_size_table = scratch1;
  Register bytecode = scratch2;

  // The bytecode offset value will be increased by one in wide and extra wide
  // cases. In the case of having a wide or extra wide JumpLoop bytecode, we
  // will restore the original bytecode. In order to simplify the code, we have
  // a backup of it.
  Register original_bytecode_offset = scratch3;
  DCHECK(!AreAliased(bytecode_array, bytecode_offset, bytecode_size_table,
                     bytecode, original_bytecode_offset));
  __ Move(bytecode_size_table,
          Immediate(ExternalReference::bytecode_size_table_address()));

  // Load the current bytecode.
  __ movzx_b(bytecode, Operand(bytecode_array, bytecode_offset, times_1, 0));
  __ Move(original_bytecode_offset, bytecode_offset);

  // Check if the bytecode is a Wide or ExtraWide prefix bytecode.
  Label process_bytecode, extra_wide;
  static_assert(0 == static_cast<int>(interpreter::Bytecode::kWide));
  static_assert(1 == static_cast<int>(interpreter::Bytecode::kExtraWide));
  static_assert(2 == static_cast<int>(interpreter::Bytecode::kDebugBreakWide));
  static_assert(3 ==
                static_cast<int>(interpreter::Bytecode::kDebugBreakExtraWide));
  __ cmp(bytecode, Immediate(0x3));
  __ j(above, &process_bytecode, Label::kNear);
  // The code to load the next bytecode is common to both wide and extra wide.
  // We can hoist them up here. inc has to happen before test since it
  // modifies the ZF flag.
  __ inc(bytecode_offset);
  __ test(bytecode, Immediate(0x1));
  __ movzx_b(bytecode, Operand(bytecode_array, bytecode_offset, times_1, 0));
  __ j(not_equal, &extra_wide, Label::kNear);

  // Load the next bytecode and update table to the wide scaled table.
  __ add(bytecode_size_table,
         Immediate(kByteSize * interpreter::Bytecodes::kBytecodeCount));
  __ jmp(&process_bytecode, Label::kNear);

  __ bind(&extra_wide);
  // Update table to the extra wide scaled table.
  __ add(bytecode_size_table,
         Immediate(2 * kByteSize * interpreter::Bytecodes::kBytecodeCount));

  __ bind(&process_bytecode);

// Bailout to the return label if this is a return bytecode.
#define JUMP_IF_EQUAL(NAME)                                            \
  __ cmp(bytecode,                                                     \
         Immediate(static_cast<int>(interpreter::Bytecode::k##NAME))); \
  __ j(equal, if_return);
  RETURN_BYTECODE_LIST(JUMP_IF_EQUAL)
#undef JUMP_IF_EQUAL

  // If this is a JumpLoop, re-execute it to perform the jump to the beginning
  // of the loop.
  Label end, not_jump_loop;
  __ cmp(bytecode,
         Immediate(static_cast<int>(interpreter::Bytecode::kJumpLoop)));
  __ j(not_equal, &not_jump_loop, Label::kNear);
  // If this is a wide or extra wide JumpLoop, we need to restore the original
  // bytecode_offset since we might have increased it to skip the wide /
  // extra-wide prefix bytecode.
  __ Move(bytecode_offset, original_bytecode_offset);
  __ jmp(&end, Label::kNear);

  __ bind(&not_jump_loop);
  // Otherwise, load the size of the current bytecode and advance the offset.
  __ movzx_b(bytecode_size_table,
             Operand(bytecode_size_table, bytecode, times_1, 0));
  __ add(bytecode_offset, bytecode_size_table);

  __ bind(&end);
}

namespace {

void ResetSharedFunctionInfoAge(MacroAssembler* masm, Register sfi) {
  __ mov_w(FieldOperand(sfi, SharedFunctionInfo::kAgeOffset), Immediate(0));
}

void ResetJSFunctionAge(MacroAssembler* masm, Register js_function,
                        Register scratch) {
  const Register shared_function_info(scratch);
  __ Move(shared_function_info,
          FieldOperand(js_function, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, shared_function_info);
}

void ResetFeedbackVectorOsrUrgency(MacroAssembler* masm,
                                   Register feedback_vector, Register scratch) {
  __ mov_b(scratch,
           FieldOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
  __ and_(scratch, Immediate(~FeedbackVector::OsrUrgencyBits::kMask));
  __ mov_b(FieldOperand(feedback_vector, FeedbackVector::kOsrStateOffset),
           scratch);
}

}  // namespace

// Generate code for entering a JS function with the interpreter.
// On entry to the function the receiver and arguments have been pushed on the
// stack left to right.
//
// The live registers are:
//   o eax: actual argument count
//   o edi: the JS function object being called
//   o edx: the incoming new target or generator object
//   o esi: our context
//   o ebp: the caller's frame pointer
//   o esp: stack pointer (pointing to return address)
//
// The function builds an interpreter frame. See InterpreterFrameConstants in
// frame-constants.h for its layout.
void Builtins::Generate_InterpreterEntryTrampoline(
    MacroAssembler* masm, InterpreterEntryTrampolineMode mode) {
  __ movd(xmm0, eax);  // Spill actual argument count.

  __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));

  // The bytecode array could have been flushed from the shared function info,
  // if so, call into CompileLazy.
  Label is_baseline, compile_lazy;
  GetSharedFunctionInfoBytecodeOrBaseline(masm, ecx, ecx, eax, &is_baseline,
                                          &compile_lazy);

  Label push_stack_frame;
  Register feedback_vector = ecx;
  Register closure = edi;
  Register scratch = eax;
  __ LoadFeedbackVector(feedback_vector, closure, scratch, &push_stack_frame,
                        Label::kNear);

#ifndef V8_JITLESS
  // If feedback vector is valid, check for optimized code and update invocation
  // count. Load the optimization state from the feedback vector and re-use the
  // register.
  Label flags_need_processing;
  Register flags = ecx;
  XMMRegister saved_feedback_vector = xmm1;
  __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      flags, saved_feedback_vector, CodeKind::INTERPRETED_FUNCTION,
      &flags_need_processing);

  // Reload the feedback vector.
  __ movd(feedback_vector, saved_feedback_vector);

  ResetFeedbackVectorOsrUrgency(masm, feedback_vector, scratch);

  // Increment the invocation count.
  __ inc(FieldOperand(feedback_vector, FeedbackVector::kInvocationCountOffset));

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set
  // up the frame (that is done below).
#else
  // Note: By omitting the above code in jitless mode we also disable:
  // - kFlagsLogNextExecution: only used for logging/profiling; and
  // - kInvocationCountOffset: only used for tiering heuristics and code
  //   coverage.
#endif  // !V8_JITLESS

  __ bind(&push_stack_frame);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ push(ebp);  // Caller's frame pointer.
  __ mov(ebp, esp);
  __ push(kContextRegister);               // Callee's context.
  __ push(kJavaScriptCallTargetRegister);  // Callee's JS function.
  __ movd(kJavaScriptCallArgCountRegister, xmm0);
  __ push(kJavaScriptCallArgCountRegister);  // Actual argument count.

  // Get the bytecode array from the function object and load it into
  // kInterpreterBytecodeArrayRegister.
  __ mov(eax, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, eax);
  __ mov(kInterpreterBytecodeArrayRegister,
         FieldOperand(eax, SharedFunctionInfo::kTrustedFunctionDataOffset));
  GetSharedFunctionInfoBytecode(masm, kInterpreterBytecodeArrayRegister, eax);

  // Check function data field is actually a BytecodeArray object.
  if (v8_flags.debug_code) {
    __ AssertNotSmi(kInterpreterBytecodeArrayRegister);
    __ CmpObjectType(kInterpreterBytecodeArrayRegister, BYTECODE_ARRAY_TYPE,
                     eax);
    __ Assert(
        equal,
        AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
  }

  // Push bytecode array.
  __ push(kInterpreterBytecodeArrayRegister);
  // Push Smi tagged initial bytecode array offset.
  __ push(Immediate(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag)));
  __ push(feedback_vector);

  // Allocate the local and temporary register file on the stack.
  Label stack_overflow;
  {
    // Load frame size from the BytecodeArray object.
    Register frame_size = ecx;
    __ mov(frame_size, FieldOperand(kInterpreterBytecodeArrayRegister,
                                    BytecodeArray::kFrameSizeOffset));

    // Do a stack check to ensure we don't go over the limit.
    __ mov(eax, esp);
    __ sub(eax, frame_size);
    __ CompareStackLimit(eax, StackLimitKind::kRealStackLimit);
    __ j(below, &stack_overflow);

    // If ok, push undefined as the initial value for all register file entries.
    Label loop_header;
    Label loop_check;
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ jmp(&loop_check);
    __ bind(&loop_header);
    // TODO(rmcilroy): Consider doing more than one push per loop iteration.
    __ push(kInterpreterAccumulatorRegister);
    // Continue loop if not done.
    __ bind(&loop_check);
    __ sub(frame_size, Immediate(kSystemPointerSize));
    __ j(greater_equal, &loop_header);
  }

  // If the bytecode array has a valid incoming new target or generator object
  // register, initialize it with incoming value which was passed in edx.
  Label no_incoming_new_target_or_generator_register;
  __ mov(ecx, FieldOperand(
                  kInterpreterBytecodeArrayRegister,
                  BytecodeArray::kIncomingNewTargetOrGeneratorRegisterOffset));
  __ test(ecx, ecx);
  __ j(zero, &no_incoming_new_target_or_generator_register);
  __ mov(Operand(ebp, ecx, times_system_pointer_size, 0), edx);
  __ bind(&no_incoming_new_target_or_generator_register);

  // Perform interrupt stack check.
  // TODO(solanes): Merge with the real stack limit check above.
  Label stack_check_interrupt, after_stack_check_interrupt;
  __ CompareStackLimit(esp, StackLimitKind::kInterruptStackLimit);
  __ j(below, &stack_check_interrupt);
  __ bind(&after_stack_check_interrupt);

  // The accumulator is already loaded with undefined.

  __ mov(kInterpreterBytecodeOffsetRegister,
         Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));

  // Load the dispatch table into a register and dispatch to the bytecode
  // handler at the current bytecode offset.
  Label do_dispatch;
  __ bind(&do_dispatch);
  __ Move(kInterpreterDispatchTableRegister,
          Immediate(ExternalReference::interpreter_dispatch_table_address(
              masm->isolate())));
  __ movzx_b(ecx, Operand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister, times_1, 0));
  __ mov(kJavaScriptCallCodeStartRegister,
         Operand(kInterpreterDispatchTableRegister, ecx,
                 times_system_pointer_size, 0));
  __ call(kJavaScriptCallCodeStartRegister);

  __ RecordComment("--- InterpreterEntryReturnPC point ---");
  if (mode == InterpreterEntryTrampolineMode::kDefault) {
    masm->isolate()->heap()->SetInterpreterEntryReturnPCOffset(
        masm->pc_offset());
  } else {
    DCHECK_EQ(mode, InterpreterEntryTrampolineMode::kForProfiling);
    // Both versions must be the same up to this point otherwise the builtins
    // will not be interchangable.
    CHECK_EQ(
        masm->isolate()->heap()->interpreter_entry_return_pc_offset().value(),
        masm->pc_offset());
  }

  // Any returns to the entry trampoline are either due to the return bytecode
  // or the interpreter tail calling a builtin and then a dispatch.

  // Get bytecode array and bytecode offset from the stack frame.
  __ mov(kInterpreterBytecodeArrayRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ mov(kInterpreterBytecodeOffsetRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);

  // Either return, or advance to the next bytecode and dispatch.
  Label do_return;
  __ Push(eax);
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, ecx,
                                kInterpreterDispatchTableRegister, eax,
                                &do_return);
  __ Pop(eax);
  __ jmp(&do_dispatch);

  __ bind(&do_return);
  __ Pop(eax);
  // The return value is in eax.
  LeaveInterpreterFrame(masm, edx, ecx);
  __ ret(0);

  __ bind(&stack_check_interrupt);
  // Modify the bytecode offset in the stack to be kFunctionEntryBytecodeOffset
  // for the call to the StackGuard.
  __ mov(Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp),
         Immediate(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                                kFunctionEntryBytecodeOffset)));
  __ CallRuntime(Runtime::kStackGuard);

  // After the call, restore the bytecode array, bytecode offset and accumulator
  // registers again. Also, restore the bytecode offset in the stack to its
  // previous value.
  __ mov(kInterpreterBytecodeArrayRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ mov(kInterpreterBytecodeOffsetRegister,
         Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);

  // It's ok to clobber kInterpreterBytecodeOffsetRegister since we are setting
  // it again after continuing.
  __ SmiTag(kInterpreterBytecodeOffsetRegister);
  __ mov(Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp),
         kInterpreterBytecodeOffsetRegister);

  __ jmp(&after_stack_check_interrupt);

#ifndef V8_JITLESS
  __ bind(&flags_need_processing);
  {
    // Restore actual argument count.
    __ movd(eax, xmm0);
    __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, xmm1);
  }

  __ bind(&compile_lazy);
  // Restore actual argument count.
  __ movd(eax, xmm0);
  __ GenerateTailCallToReturnedCode(Runtime::kCompileLazy);

  __ bind(&is_baseline);
  {
    __ movd(xmm2, ecx);  // Save baseline data.
    // Load the feedback vector from the closure.
    __ mov(feedback_vector,
           FieldOperand(closure, JSFunction::kFeedbackCellOffset));
    __ mov(feedback_vector,
           FieldOperand(feedback_vector, FeedbackCell::kValueOffset));

    Label install_baseline_code;
    // Check if feedback vector is valid. If not, call prepare for baseline to
    // allocate it.
    __ LoadMap(eax, feedback_vector);
    __ CmpInstanceType(eax, FEEDBACK_VECTOR_TYPE);
    __ j(not_equal, &install_baseline_code);

    // Check the tiering state.
    __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, xmm1, CodeKind::BASELINE, &flags_need_processing);

    // Load the baseline code into the closure.
    __ movd(ecx, xmm2);
    static_assert(kJavaScriptCallCodeStartRegister == ecx, "ABI mismatch");
    __ push(edx);  // Spill.
    __ push(ecx);
    __ Push(xmm0, eax);  // Save the argument count (currently in xmm0).
    __ ReplaceClosureCodeWithOptimizedCode(ecx, closure, eax, ecx);
    __ pop(eax);  // Restore the argument count.
    __ pop(ecx);
    __ pop(edx);
    __ JumpCodeObject(ecx);

    __ bind(&install_baseline_code);
    __ movd(eax, xmm0);  // Recover argument count.
    __ GenerateTailCallToReturnedCode(Runtime::kInstallBaselineCode);
  }
#endif  // !V8_JITLESS

  __ bind(&stack_overflow);
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ int3();  // Should not return.
}

static void GenerateInterpreterPushArgs(MacroAssembler* masm,
                                        Register array_limit,
                                        Register start_address) {
  // ----------- S t a t e -------------
  //  -- start_address : Pointer to the last argument in the args array.
  //  -- array_limit : Pointer to one before the first argument in the
  //                   args array.
  // -----------------------------------
  ASM_CODE_COMMENT(masm);
  Label loop_header, loop_check;
  __ jmp(&loop_check);
  __ bind(&loop_header);
  __ Push(Operand(array_limit, 0));
  __ bind(&loop_check);
  __ add(array_limit, Immediate(kSystemPointerSize));
  __ cmp(array_limit, start_address);
  __ j(below_equal, &loop_header, Label::kNear);
}

// static
void Builtins::Generate_InterpreterPushArgsThenCallImpl(
    MacroAssembler* masm, ConvertReceiverMode receiver_mode,
    InterpreterPushArgsMode mode) {
  DCHECK(mode != InterpreterPushArgsMode::kArrayFunction);
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- ecx : the address of the first argument to be pushed. Subsequent
  //           arguments should be consecutive above this, in the same order as
  //           they are to be pushed onto the stack.
  //  -- edi : the target to call (can be any Object).
  // -----------------------------------

  const Register scratch = edx;
  const Register argv = ecx;

  Label stack_overflow;
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ dec(eax);
  }

  // Add a stack check before pushing the arguments.
  __ StackOverflowCheck(eax, scratch, &stack_overflow, true);
  __ movd(xmm0, eax);  // Spill number of arguments.

  // Compute the expected number of arguments.
  __ mov(scratch, eax);
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ dec(scratch);  // Exclude receiver.
  }

  // Pop return address to allow tail-call after pushing arguments.
  __ PopReturnAddressTo(eax);

  // Find the address of the last argument.
  __ shl(scratch, kSystemPointerSizeLog2);
  __ neg(scratch);
  __ add(scratch, argv);

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ movd(xmm1, scratch);
    GenerateInterpreterPushArgs(masm, scratch, argv);
    // Pass the spread in the register ecx.
    __ movd(ecx, xmm1);
    __ mov(ecx, Operand(ecx, 0));
  } else {
    GenerateInterpreterPushArgs(masm, scratch, argv);
  }

  // Push "undefined" as the receiver arg if we need to.
  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    __ PushRoot(RootIndex::kUndefinedValue);
  }

  __ PushReturnAddressFrom(eax);
  __ movd(eax, xmm0);  // Restore number of arguments.

  // Call the target.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ TailCallBuiltin(Builtin::kCallWithSpread);
  } else {
    __ TailCallBuiltin(Builtins::Call(receiver_mode));
  }

  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);

    // This should be unreachable.
    __ int3();
  }
}

namespace {

// This function modifies start_addr, and only reads the contents of num_args
// register. scratch1 and scratch2 are used as temporary registers.
void Generate_InterpreterPushZeroAndArgsAndReturnAddress(
    MacroAssembler* masm, Register num_args, Register start_addr,
    Register scratch1, Register scratch2, int num_slots_to_move,
    Label* stack_overflow) {
  // We have to move return address and the temporary registers above it
  // before we can copy arguments onto the stack. To achieve this:
  // Step 1: Increment the stack pointer by num_args + 1 for receiver (if it is
  // not included in argc already). Step 2: Move the return address and values
  // around it to the top of stack. Step 3: Copy the arguments into the correct
  // locations.
  //  current stack    =====>    required stack layout
  // |             |            | return addr   | (2) <-- esp (1)
  // |             |            | addtl. slot   |
  // |             |            | arg N         | (3)
  // |             |            | ....          |
  // |             |            | arg 1         |
  // | return addr | <-- esp    | arg 0         |
  // | addtl. slot |            | receiver slot |

  // Check for stack overflow before we increment the stack pointer.
  __ StackOverflowCheck(num_args, scratch1, stack_overflow, true);

  // Step 1 - Update the stack pointer.

  __ lea(scratch1, Operand(num_args, times_system_pointer_size, 0));
  __ AllocateStackSpace(scratch1);

  // Step 2 move return_address and slots around it to the correct locations.
  // Move from top to bottom, otherwise we may overwrite when num_args = 0 or 1,
  // basically when the source and destination overlap. We at least need one
  // extra slot for receiver, so no extra checks are required to avoid copy.
  for (int i = 0; i < num_slots_to_move + 1; i++) {
    __ mov(scratch1, Operand(esp, num_args, times_system_pointer_size,
                             i * kSystemPointerSize));
    __ mov(Operand(esp, i * kSystemPointerSize), scratch1);
  }

  // Step 3 copy arguments to correct locations.
  // Slot meant for receiver contains return address. Reset it so that
  // we will not incorrectly interpret return address as an object.
  __ mov(Operand(esp, (num_slots_to_move + 1) * kSystemPointerSize),
         Immediate(0));
  __ mov(scratch1, Immediate(0));

  Label loop_header, loop_check;
  __ jmp(&loop_check);
  __ bind(&loop_header);
  __ mov(scratch2, Operand(start_addr, 0));
  __ mov(Operand(esp, scratch1, times_system_pointer_size,
                 (num_slots_to_move + 1) * kSystemPointerSize),
         scratch2);
  __ sub(start_addr, Immediate(kSystemPointerSize));
  __ bind(&loop_check);
  __ inc(scratch1);
  __ cmp(scratch1, eax);
  __ j(less, &loop_header, Label::kNear);
}

}  // anonymous namespace

// static
void Builtins::Generate_InterpreterPushArgsThenConstructImpl(
    MacroAssembler* masm, InterpreterPushArgsMode mode) {
  // ----------- S t a t e -------------
  //  -- eax     : the number of arguments
  //  -- ecx     : the address of the first argument to be pushed. Subsequent
  //               arguments should be consecutive above this, in the same order
  //               as they are to be pushed onto the stack.
  //  -- esp[0]  : return address
  //  -- esp[4]  : allocation site feedback (if available or undefined)
  //  -- esp[8]  : the new target
  //  -- esp[12] : the constructor
  // -----------------------------------
  Label stack_overflow;

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // The spread argument should not be pushed.
    __ dec(eax);
  }

  // Push arguments and move return address and stack spill slots to the top of
  // stack. The eax register is readonly. The ecx register will be modified. edx
  // and edi are used as scratch registers.
  Generate_InterpreterPushZeroAndArgsAndReturnAddress(
      masm, eax, ecx, edx, edi,
      InterpreterPushArgsThenConstructDescriptor::GetStackParameterCount(),
      &stack_overflow);

  // Call the appropriate constructor. eax and ecx already contain intended
  // values, remaining registers still need to be initialized from the stack.

  if (mode == InterpreterPushArgsMode::kArrayFunction) {
    // Tail call to the array construct stub (still in the caller context at
    // this point).

    __ movd(xmm0, eax);  // Spill number of arguments.
    __ PopReturnAddressTo(eax);
    __ Pop(kJavaScriptCallExtraArg1Register);
    __ Pop(kJavaScriptCallNewTargetRegister);
    __ Pop(kJavaScriptCallTargetRegister);
    __ PushReturnAddressFrom(eax);

    __ AssertFunction(kJavaScriptCallTargetRegister, eax);
    __ AssertUndefinedOrAllocationSite(kJavaScriptCallExtraArg1Register, eax);

    __ movd(eax, xmm0);  // Reload number of arguments.
    __ TailCallBuiltin(Builtin::kArrayConstructorImpl);
  } else if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ movd(xmm0, eax);  // Spill number of arguments.
    __ PopReturnAddressTo(eax);
    __ Drop(1);  // The allocation site is unused.
    __ Pop(kJavaScriptCallNewTargetRegister);
    __ Pop(kJavaScriptCallTargetRegister);
    // Pass the spread in the register ecx, overwriting ecx.
    __ mov(ecx, Operand(ecx, 0));
    __ PushReturnAddressFrom(eax);
    __ movd(eax, xmm0);  // Reload number of arguments.
    __ TailCallBuiltin(Builtin::kConstructWithSpread);
  } else {
    DCHECK_EQ(InterpreterPushArgsMode::kOther, mode);
    __ PopReturnAddressTo(ecx);
    __ Drop(1);  // The allocation site is unused.
    __ Pop(kJavaScriptCallNewTargetRegister);
    __ Pop(kJavaScriptCallTargetRegister);
    __ PushReturnAddressFrom(ecx);

    __ TailCallBuiltin(Builtin::kConstruct);
  }

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  __ int3();
}

namespace {
void LoadFramePointer(MacroAssembler* masm, Register to,
                      Builtins::ForwardWhichFrame which_frame) {
  switch (which_frame) {
    case Builtins::ForwardWhichFrame::kCurrentFrame:
      __ mov(to, ebp);
      break;
    case Builtins::ForwardWhichFrame::kParentFrame:
      __ mov(to, Operand(ebp, StandardFrameConstants::kCallerFPOffset));
      break;
  }
}
}  // namespace

// static
void Builtins::Generate_ConstructForwardAllArgsImpl(
    MacroAssembler* masm, ForwardWhichFrame which_frame) {
  // ----------- S t a t e -------------
  //  -- edx     : the new target
  //  -- edi     : the constructor
  //  -- esp[0]  : return address
  // -----------------------------------
  Label stack_overflow;

  // Load the frame into ecx.
  LoadFramePointer(masm, ecx, which_frame);

  // Load the argument count into eax.
  __ mov(eax, Operand(ecx, StandardFrameConstants::kArgCOffset));

  // The following stack surgery is performed to forward arguments from the
  // interpreted frame.
  //
  //  current stack    =====>    required stack layout
  // |             |            | saved new target  | (2)
  // |             |            | saved constructor | (2)
  // |             |            | return addr       | (3) <-- esp (1)
  // |             |            | arg N             | (5)
  // |             |            | ....              | (5)
  // |             |            | arg 0             | (5)
  // | return addr | <-- esp    | 0 (receiver)      | (4)
  //
  // The saved new target and constructor are popped to their respective
  // registers before calling the Construct builtin.

  // Step 1
  //
  // Update the stack pointer, using ecx as a scratch register.
  __ StackOverflowCheck(eax, ecx, &stack_overflow, true);
  __ lea(ecx, Operand(eax, times_system_pointer_size, 0));
  __ AllocateStackSpace(ecx);

  // Step 2
  //
  // Save the new target and constructor on the stack so they can be used as
  // scratch registers.
  __ Push(edi);
  __ Push(edx);

  // Step 3
  //
  // Move the return address. Stack address computations have to be offset by
  // the saved constructor and new target on the stack.
  constexpr int spilledConstructorAndNewTargetOffset = 2 * kSystemPointerSize;
  __ mov(edx, Operand(esp, eax, times_system_pointer_size,
                      spilledConstructorAndNewTargetOffset));
  __ mov(Operand(esp, spilledConstructorAndNewTargetOffset), edx);

  // Step 4
  // Push a 0 for the receiver to be allocated.
  __ mov(
      Operand(esp, kSystemPointerSize + spilledConstructorAndNewTargetOffset),
      Immediate(0));

  // Step 5
  //
  // Forward the arguments from the frame.

  // First reload the frame pointer into ecx.
  LoadFramePointer(masm, ecx, which_frame);

  // Point ecx to the base of the arguments, excluding the receiver.
  __ add(ecx, Immediate((StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
                        kSystemPointerSize));
  {
    // Copy the arguments.
    Register counter = edx;
    Register scratch = edi;

    Label loop, entry;
    __ mov(counter, eax);
    __ jmp(&entry);
    __ bind(&loop);
    // The source frame's argument is offset by -kSystemPointerSize because the
    // counter with an argument count inclusive of the receiver.
    __ mov(scratch, Operand(ecx, counter, times_system_pointer_size,
                            -kSystemPointerSize));
    // Similarly, the target frame's argument is offset by +kSystemPointerSize
    // because we pushed a 0 for the receiver to be allocated.
    __ mov(Operand(esp, counter, times_system_pointer_size,
                   kSystemPointerSize + spilledConstructorAndNewTargetOffset),
           scratch);
    __ bind(&entry);
    __ dec(counter);
    __ j(greater_equal, &loop, Label::kNear);
  }

  // Pop the saved constructor and new target, then call the appropriate
  // constructor. eax already contains the argument count.
  __ Pop(kJavaScriptCallNewTargetRegister);
  __ Pop(kJavaScriptCallTargetRegister);
  __ TailCallBuiltin(Builtin::kConstruct);

  __ bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    __ int3();
  }
}

namespace {

void NewImplicitReceiver(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  // -- eax : argument count
  // -- edi : constructor to call
  // -- edx : new target (checked to be a JSFunction)
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- [arguments without receiver]
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer

  Register implicit_receiver = ecx;

  // Save live registers.
  __ SmiTag(eax);
  __ Push(eax);  // Number of arguments
  __ Push(edx);  // NewTarget
  __ Push(edi);  // Target
  __ CallBuiltin(Builtin::kFastNewObject);
  // Save result.
  __ mov(implicit_receiver, eax);
  // Restore live registers.
  __ Pop(edi);
  __ Pop(edx);
  __ Pop(eax);
  __ SmiUntag(eax);

  // Patch implicit receiver (in arguments)
  __ mov(Operand(esp, 0 /* first argument */), implicit_receiver);
  // Patch second implicit (in construct frame)
  __ mov(Operand(ebp, FastConstructFrameConstants::kImplicitReceiverOffset),
         implicit_receiver);

  // Restore context.
  __ mov(esi, Operand(ebp, FastConstructFrameConstants::kContextOffset));
}

}  // namespace

// static
void Builtins::Generate_InterpreterPushArgsThenFastConstructFunction(
    MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax     : the number of arguments
  //  -- ecx     : the address of the first argument to be pushed. Subsequent
  //               arguments should be consecutive above this, in the same order
  //               as they are to be pushed onto the stack.
  //  -- esi     : the context
  //  -- esp[0]  : return address
  //  -- esp[4]  : allocation site feedback (if available or undefined)
  //  -- esp[8]  : the new target
  //  -- esp[12] : the constructor (checked to be a JSFunction)
  // -----------------------------------

  // Load constructor.
  __ mov(edi, Operand(esp, 3 * kSystemPointerSize));
  __ AssertFunction(edi, edx);

  // Check if target has a [[Construct]] internal method.
  Label non_constructor;
  // Load constructor.
  __ LoadMap(edx, edi);
  __ test_b(FieldOperand(edx, Map::kBitFieldOffset),
            Immediate(Map::Bits1::IsConstructorBit::kMask));
  __ j(zero, &non_constructor);

  // Add a stack check before pushing arguments.
  Label stack_overflow;
  __ StackOverflowCheck(eax, edx, &stack_overflow, true);

  // Spill number of arguments.
  __ movd(xmm0, eax);

  // Load NewTarget.
  __ mov(edx, Operand(esp, 2 * kSystemPointerSize));

  // Drop stub arguments from the stack.
  __ PopReturnAddressTo(eax);
  __ Drop(3);  // The allocation site is unused.
  __ PushReturnAddressFrom(eax);

  // Enter a construct frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterFrame(StackFrame::FAST_CONSTRUCT);
  __ Push(esi);
  // Implicit receiver stored in the construct frame.
  __ PushRoot(RootIndex::kTheHoleValue);

  // Push arguments + implicit receiver
  __ movd(eax, xmm0);  // Recover number of arguments.
  // Find the address of the last argument.
  __ lea(esi, Operand(eax, times_system_pointer_size,
                      -kJSArgcReceiverSlots * kSystemPointerSize));
  __ neg(esi);
  __ add(esi, ecx);
  GenerateInterpreterPushArgs(masm, esi, ecx);
  __ PushRoot(RootIndex::kTheHoleValue);

  // Restore context.
  __ mov(esi, Operand(ebp, FastConstructFrameConstants::kContextOffset));

  // Check if it is a builtin call.
  Label builtin_call;
  __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ test(FieldOperand(ecx, SharedFunctionInfo::kFlagsOffset),
          Immediate(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ j(not_zero, &builtin_call);

  // Check if we need to create an implicit receiver.
  Label not_create_implicit_receiver;
  __ mov(ecx, FieldOperand(ecx, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(ecx);
  __ JumpIfIsInRange(
      ecx, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor), ecx,
      &not_create_implicit_receiver, Label::kNear);
  NewImplicitReceiver(masm);
  __ bind(&not_create_implicit_receiver);

  // Call the constructor.
  __ InvokeFunction(edi, edx, eax, InvokeType::kCall);

  // ----------- S t a t e -------------
  //  -- eax     constructor result
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------

  // Store offset of return address for deoptimizer.
  masm->isolate()->heap()->SetConstructStubInvokeDeoptPCOffset(
      masm->pc_offset());

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.

  Label check_result, use_receiver, do_throw, leave_and_return;
  // If the result is undefined, we jump out to using the implicit receiver.
  __ JumpIfNotRoot(eax, RootIndex::kUndefinedValue, &check_result,
                   Label::kNear);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ bind(&use_receiver);
  __ mov(eax, Operand(esp, 0 * kSystemPointerSize));
  __ JumpIfRoot(eax, RootIndex::kTheHoleValue, &do_throw);

  __ bind(&leave_and_return);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ ret(0);

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_result);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(eax, &use_receiver, Label::kNear);

  // If the type of the result (stored in its map) is less than
  // FIRST_JS_RECEIVER_TYPE, it is not an object in the ECMA sense.
  static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
  __ CmpObjectType(eax, FIRST_JS_RECEIVER_TYPE, ecx);
  __ j(above_equal, &leave_and_return, Label::kNear);
  __ jmp(&use_receiver, Label::kNear);

  __ bind(&do_throw);
  // Restore context from the frame.
  __ mov(esi, Operand(ebp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  // This should be unreachable.
  __ int3();

  __ bind(&builtin_call);
  __ InvokeFunction(edi, edx, eax, InvokeType::kCall);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ ret(0);

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);

  // Throw stack overflow exception.
  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
  // This should be unreachable.
  __ int3();
}

static void Generate_InterpreterEnterBytecode(MacroAssembler* masm) {
  // Set the return address to the correct point in the interpreter entry
  // trampoline.
  Label builtin_trampoline, trampoline_loaded;
  Tagged<Smi> interpreter_entry_return_pc_offset(
      masm->isolate()->heap()->interpreter_entry_return_pc_offset());
  DCHECK_NE(interpreter_entry_return_pc_offset, Smi::zero());

  static constexpr Register scratch = ecx;

  // If the SFI function_data is an InterpreterData, the function will have a
  // custom copy of the interpreter entry trampoline for profiling. If so,
  // get the custom trampoline, otherwise grab the entry address of the global
  // trampoline.
  __ mov(scratch, Operand(ebp, StandardFrameConstants::kFunctionOffset));
  __ mov(scratch, FieldOperand(scratch, JSFunction::kSharedFunctionInfoOffset));
  __ mov(scratch,
         FieldOperand(scratch, SharedFunctionInfo::kTrustedFunctionDataOffset));
  __ Push(eax);
  __ CmpObjectType(scratch, INTERPRETER_DATA_TYPE, eax);
  __ j(not_equal, &builtin_trampoline, Label::kNear);

  __ mov(scratch,
         FieldOperand(scratch, InterpreterData::kInterpreterTrampolineOffset));
  __ LoadCodeInstructionStart(scratch, scratch);
  __ jmp(&trampoline_loaded, Label::kNear);

  __ bind(&builtin_trampoline);
  __ mov(scratch,
         __ ExternalReferenceAsOperand(
             ExternalReference::
                 address_of_interpreter_entry_trampoline_instruction_start(
                     masm->isolate()),
             scratch));

  __ bind(&trampoline_loaded);
  __ Pop(eax);
  __ add(scratch, Immediate(interpreter_entry_return_pc_offset.value()));
  __ push(scratch);

  // Initialize the dispatch table register.
  __ Move(kInterpreterDispatchTableRegister,
          Immediate(ExternalReference::interpreter_dispatch_table_address(
              masm->isolate())));

  // Get the bytecode array pointer from the frame.
  __ mov(kInterpreterBytecodeArrayRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));

  if (v8_flags.debug_code) {
    // Check function data field is actually a BytecodeArray object.
    __ AssertNotSmi(kInterpreterBytecodeArrayRegister);
    __ CmpObjectType(kInterpreterBytecodeArrayRegister, BYTECODE_ARRAY_TYPE,
                     scratch);
    __ Assert(
        equal,
        AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
  }

  // Get the target bytecode offset from the frame.
  __ mov(kInterpreterBytecodeOffsetRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);

  if (v8_flags.debug_code) {
    Label okay;
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));
    __ j(greater_equal, &okay, Label::kNear);
    __ int3();
    __ bind(&okay);
  }

  // Dispatch to the target bytecode.
  __ movzx_b(scratch, Operand(kInterpreterBytecodeArrayRegister,
                              kInterpreterBytecodeOffsetRegister, times_1, 0));
  __ mov(kJavaScriptCallCodeStartRegister,
         Operand(kInterpreterDispatchTableRegister, scratch,
                 times_system_pointer_size, 0));
  __ jmp(kJavaScriptCallCodeStartRegister);
}

void Builtins::Generate_InterpreterEnterAtNextBytecode(MacroAssembler* masm) {
  // Get bytecode array and bytecode offset from the stack frame.
  __ mov(kInterpreterBytecodeArrayRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ mov(kInterpreterBytecodeOffsetRegister,
         Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);

  Label enter_bytecode, function_entry_bytecode;
  __ cmp(kInterpreterBytecodeOffsetRegister,
         Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag +
                   kFunctionEntryBytecodeOffset));
  __ j(equal, &function_entry_bytecode);

  // Advance to the next bytecode.
  Label if_return;
  __ Push(eax);
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, ecx, esi,
                                eax, &if_return);
  __ Pop(eax);

  __ bind(&enter_bytecode);
  // Convert new bytecode offset to a Smi and save in the stackframe.
  __ mov(ecx, kInterpreterBytecodeOffsetRegister);
  __ SmiTag(ecx);
  __ mov(Operand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp), ecx);

  Generate_InterpreterEnterBytecode(masm);

  __ bind(&function_entry_bytecode);
  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset. Detect this case and advance to the first
  // actual bytecode.
  __ mov(kInterpreterBytecodeOffsetRegister,
         Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ jmp(&enter_bytecode);

  // We should never take the if_return path.
  __ bind(&if_return);
  // No need to pop eax here since we will be aborting anyway.
  __ Abort(AbortReason::kInvalidBytecodeAdvance);
}

void Builtins::Generate_InterpreterEnterAtBytecode(MacroAssembler* masm) {
  Generate_InterpreterEnterBytecode(masm);
}

// static
void Builtins::Generate_BaselineOutOfLinePrologue(MacroAssembler* masm) {
  auto descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtin::kBaselineOutOfLinePrologue);
  Register arg_count = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kJavaScriptCallArgCount);
  Register frame_size = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kStackFrameSize);

  // Save argument count and bytecode array.
  XMMRegister saved_arg_count = xmm0;
  XMMRegister saved_bytecode_array = xmm1;
  XMMRegister saved_frame_size = xmm2;
  XMMRegister saved_feedback_cell = xmm3;
  XMMRegister saved_feedback_vector = xmm4;
  __ movd(saved_arg_count, arg_count);
  __ movd(saved_frame_size, frame_size);

  // Use the arg count (eax) as the scratch register.
  Register scratch = arg_count;

  // Load the feedback cell and vector from the closure.
  Register closure = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kClosure);
  Register feedback_cell = ecx;
  __ mov(feedback_cell, FieldOperand(closure, JSFunction::kFeedbackCellOffset));
  __ movd(saved_feedback_cell, feedback_cell);
  Register feedback_vector = ecx;
  __ mov(feedback_vector,
         FieldOperand(feedback_cell, FeedbackCell::kValueOffset));
  __ AssertFeedbackVector(feedback_vector, scratch);
  feedback_cell = no_reg;

  // Load the optimization state from the feedback vector and re-use the
  // register.
  Label flags_need_processing;
  Register flags = ecx;
  __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      flags, saved_feedback_vector, CodeKind::BASELINE, &flags_need_processing);

  // Reload the feedback vector.
  __ movd(feedback_vector, saved_feedback_vector);

  {
    DCHECK_EQ(arg_count, eax);
    ResetFeedbackVectorOsrUrgency(masm, feedback_vector, eax);
    __ movd(arg_count, saved_arg_count);  // Restore eax.
  }

  // Increment the invocation count.
  __ inc(FieldOperand(feedback_vector, FeedbackVector::kInvocationCountOffset));

  XMMRegister return_address = xmm5;
  // Save the return address, so that we can push it to the end of the newly
  // set-up frame once we're done setting it up.
  __ PopReturnAddressTo(return_address, scratch);
  // The bytecode array was pushed to the stack by the caller.
  __ Pop(saved_bytecode_array, scratch);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  {
    ASM_CODE_COMMENT_STRING(masm, "Frame Setup");
    __ EnterFrame(StackFrame::BASELINE);

    __ Push(descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kCalleeContext));  // Callee's
                                                                // context.
    Register callee_js_function = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kClosure);
    DCHECK_EQ(callee_js_function, kJavaScriptCallTargetRegister);
    DCHECK_EQ(callee_js_function, kJSFunctionRegister);
    ResetJSFunctionAge(masm, callee_js_function, scratch);
    __ Push(callee_js_function);        // Callee's JS function.
    __ Push(saved_arg_count, scratch);  // Push actual argument count.

    // We'll use the bytecode for both code age/OSR resetting, and pushing onto
    // the frame, so load it into a register.
    __ Push(saved_bytecode_array, scratch);
    __ Push(saved_feedback_cell, scratch);
    __ Push(saved_feedback_vector, scratch);
  }

  Label call_stack_guard;
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    //
    // TODO(v8:11429): Backport this folded check to the
    // InterpreterEntryTrampoline.
    __ movd(frame_size, saved_frame_size);
    __ Move(scratch, esp);
    DCHECK_NE(frame_size, kJavaScriptCallNewTargetRegister);
    __ sub(scratch, frame_size);
    __ CompareStackLimit(scratch, StackLimitKind::kInterruptStackLimit);
    __ j(below, &call_stack_guard);
  }

  // Push the return address back onto the stack for return.
  __ PushReturnAddressFrom(return_address, scratch);
  // Return to caller pushed pc, without any frame teardown.
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ Ret();

  __ bind(&flags_need_processing);
  {
    ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
    // Drop the return address and bytecode array, rebalancing the return stack
    // buffer by using JumpMode::kPushAndReturn. We can't leave the slot and
    // overwrite it on return since we may do a runtime call along the way that
    // requires the stack to only contain valid frames.
    __ Drop(2);
    __ movd(arg_count, saved_arg_count);  // Restore actual argument count.
    __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, saved_feedback_vector);
    __ Trap();
  }

  __ bind(&call_stack_guard);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
    {
      // Push the baseline code return address now, as if it had been pushed by
      // the call to this builtin.
      __ PushReturnAddressFrom(return_address, scratch);
      FrameScope frame_scope(masm, StackFrame::INTERNAL);
      // Save incoming new target or generator
      __ Push(kJavaScriptCallNewTargetRegister);
      __ SmiTag(frame_size);
      __ Push(frame_size);
      __ CallRuntime(Runtime::kStackGuardWithGap, 1);
      __ Pop(kJavaScriptCallNewTargetRegister);
    }

    // Return to caller pushed pc, without any frame teardown.
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ Ret();
  }
}

// static
void Builtins::Generate_BaselineOutOfLinePrologueDeopt(MacroAssembler* masm) {
  // We're here because we got deopted during BaselineOutOfLinePrologue's stack
  // check. Undo all its frame creation and call into the interpreter instead.

  // Drop the feedback vector.
  __ Pop(ecx);
  // Drop bytecode offset (was the feedback vector but got replaced during
  // deopt).
  __ Pop(ecx);
  // Drop bytecode array
  __ Pop(ecx);

  // argc.
  __ Pop(kJavaScriptCallArgCountRegister);
  // Closure.
  __ Pop(kJavaScriptCallTargetRegister);
  // Context.
  __ Pop(kContextRegister);

  // Drop frame pointer
  __ LeaveFrame(StackFrame::BASELINE);

  // Enter the interpreter.
  __ TailCallBuiltin(Builtin::kInterpreterEntryTrampoline);
}

namespace {
void Generate_ContinueToBuiltinHelper(MacroAssembler* masm,
                                      bool java_script_builtin,
                                      bool with_result) {
  const RegisterConfiguration* config(RegisterConfiguration::Default());
  int allocatable_register_count = config->num_allocatable_general_registers();
  if (with_result) {
    if (java_script_builtin) {
      // xmm0 is not included in the allocateable registers.
      __ movd(xmm0, eax);
    } else {
      // Overwrite the hole inserted by the deoptimizer with the return value
      // from the LAZY deopt point.
      __ mov(
          Operand(esp, config->num_allocatable_general_registers() *
                               kSystemPointerSize +
                           BuiltinContinuationFrameConstants::kFixedFrameSize),
          eax);
    }
  }

  // Replace the builtin index Smi on the stack with the start address of the
  // builtin loaded from the builtins table. The ret below will return to this
  // address.
  int offset_to_builtin_index = allocatable_register_count * kSystemPointerSize;
  __ mov(eax, Operand(esp, offset_to_builtin_index));
  __ LoadEntryFromBuiltinIndex(eax, eax);
  __ mov(Operand(esp, offset_to_builtin_index), eax);

  for (int i = allocatable_register_count - 1; i >= 0; --i) {
    int code = config->GetAllocatableGeneralCode(i);
    __ pop(Register::from_code(code));
    if (java_script_builtin && code == kJavaScriptCallArgCountRegister.code()) {
      __ SmiUntag(Register::from_code(code));
    }
  }
  if (with_result && java_script_builtin) {
    // Overwrite the hole inserted by the deoptimizer with the return value from
    // the LAZY deopt point. eax contains the arguments count, the return value
    // from LAZY is always the last argument.
    __ movd(Operand(esp, eax, times_system_pointer_size,
                    BuiltinContinuationFrameConstants::kFixedFrameSize -
                        kJSArgcReceiverSlots * kSystemPointerSize),
            xmm0);
  }
  __ mov(
      ebp,
      Operand(esp, BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp));
  const int offsetToPC =
      BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp -
      kSystemPointerSize;
  __ pop(Operand(esp, offsetToPC));
  __ Drop(offsetToPC / kSystemPointerSize);
  __ ret(0);
}
}  // namespace

void Builtins::Generate_ContinueToCodeStubBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, false);
}

void Builtins::Generate_ContinueToCodeStubBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, false, true);
}

void Builtins::Generate_ContinueToJavaScriptBuiltin(MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, false);
}

void Builtins::Generate_ContinueToJavaScriptBuiltinWithResult(
    MacroAssembler* masm) {
  Generate_ContinueToBuiltinHelper(masm, true, true);
}

void Builtins::Generate_NotifyDeoptimized(MacroAssembler* masm) {
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kNotifyDeoptimized);
    // Tear down internal frame.
  }

  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), eax.code());
  __ mov(eax, Operand(esp, 1 * kSystemPointerSize));
  __ ret(1 * kSystemPointerSize);  // Remove eax.
}

// static
void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax     : argc
  //  -- esp[0]  : return address
  //  -- esp[1]  : receiver
  //  -- esp[2]  : thisArg
  //  -- esp[3]  : argArray
  // -----------------------------------

  // 1. Load receiver into xmm0, argArray into edx (if present), remove all
  // arguments from the stack (including the receiver), and push thisArg (if
  // present) instead.
  {
    Label no_arg_array, no_this_arg;
    StackArgumentsAccessor args(eax);
    // Spill receiver to allow the usage of edi as a scratch register.
    __ movd(xmm0, args.GetReceiverOperand());

    __ LoadRoot(edx, RootIndex::kUndefinedValue);
    __ mov(edi, edx);
    __ cmp(eax, Immediate(JSParameterCount(0)));
    __ j(equal, &no_this_arg, Label::kNear);
    {
      __ mov(edi, args[1]);
      __ cmp(eax, Immediate(JSParameterCount(1)));
      __ j(equal, &no_arg_array, Label::kNear);
      __ mov(edx, args[2]);
      __ bind(&no_arg_array);
    }
    __ bind(&no_this_arg);
    __ DropArgumentsAndPushNewReceiver(eax, edi, ecx);

    // Restore receiver to edi.
    __ movd(edi, xmm0);
  }

  // ----------- S t a t e -------------
  //  -- edx    : argArray
  //  -- edi    : receiver
  //  -- esp[0] : return address
  //  -- esp[4] : thisArg
  // -----------------------------------

  // 2. We don't need to check explicitly for callable receiver here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Tail call with no arguments if argArray is null or undefined.
  Label no_arguments;
  __ JumpIfRoot(edx, RootIndex::kNullValue, &no_arguments, Label::kNear);
  __ JumpIfRoot(edx, RootIndex::kUndefinedValue, &no_arguments, Label::kNear);

  // 4a. Apply the receiver to the given argArray.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);

  // 4b. The argArray is either null or undefined, so we tail call without any
  // arguments to the receiver.
  __ bind(&no_arguments);
  {
    __ Move(eax, JSParameterCount(0));
    __ TailCallBuiltin(Builtins::Call());
  }
}

// static
void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
  // Stack Layout:
  // esp[0]           : Return address
  // esp[8]           : Argument 0 (receiver: callable to call)
  // esp[16]          : Argument 1
  //  ...
  // esp[8 * n]       : Argument n-1
  // esp[8 * (n + 1)] : Argument n
  // eax contains the number of arguments, n.

  // 1. Get the callable to call (passed as receiver) from the stack.
  {
    StackArgumentsAccessor args(eax);
    __ mov(edi, args.GetReceiverOperand());
  }

  // 2. Save the return address and drop the callable.
  __ PopReturnAddressTo(edx);
  __ Pop(ecx);

  // 3. Make sure we have at least one argument.
  {
    Label done;
    __ cmp(eax, Immediate(JSParameterCount(0)));
    __ j(greater, &done, Label::kNear);
    __ PushRoot(RootIndex::kUndefinedValue);
    __ inc(eax);
    __ bind(&done);
  }

  // 4. Push back the return address one slot down on the stack (overwriting the
  // original callable), making the original first argument the new receiver.
  __ PushReturnAddressFrom(edx);
  __ dec(eax);  // One fewer argument (first argument is new receiver).

  // 5. Call the callable.
  __ TailCallBuiltin(Builtins::Call());
}

void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax     : argc
  //  -- esp[0]  : return address
  //  -- esp[4]  : receiver
  //  -- esp[8]  : target         (if argc >= 1)
  //  -- esp[12] : thisArgument   (if argc >= 2)
  //  -- esp[16] : argumentsList  (if argc == 3)
  // -----------------------------------

  // 1. Load target into edi (if present), argumentsList into edx (if present),
  // remove all arguments from the stack (including the receiver), and push
  // thisArgument (if present) instead.
  {
    Label done;
    StackArgumentsAccessor args(eax);
    __ LoadRoot(edi, RootIndex::kUndefinedValue);
    __ mov(edx, edi);
    __ mov(ecx, edi);
    __ cmp(eax, Immediate(JSParameterCount(1)));
    __ j(below, &done, Label::kNear);
    __ mov(edi, args[1]);  // target
    __ j(equal, &done, Label::kNear);
    __ mov(ecx, args[2]);  // thisArgument
    __ cmp(eax, Immediate(JSParameterCount(3)));
    __ j(below, &done, Label::kNear);
    __ mov(edx, args[3]);  // argumentsList
    __ bind(&done);

    // Spill argumentsList to use edx as a scratch register.
    __ movd(xmm0, edx);

    __ DropArgumentsAndPushNewReceiver(eax, ecx, edx);

    // Restore argumentsList.
    __ movd(edx, xmm0);
  }

  // ----------- S t a t e -------------
  //  -- edx    : argumentsList
  //  -- edi    : target
  //  -- esp[0] : return address
  //  -- esp[4] : thisArgument
  // -----------------------------------

  // 2. We don't need to check explicitly for callable target here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Apply the target to the given argumentsList.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);
}

void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax     : argc
  //  -- esp[0]  : return address
  //  -- esp[4]  : receiver
  //  -- esp[8]  : target
  //  -- esp[12] : argumentsList
  //  -- esp[16] : new.target (optional)
  // -----------------------------------

  // 1. Load target into edi (if present), argumentsList into ecx (if present),
  // new.target into edx (if present, otherwise use target), remove all
  // arguments from the stack (including the receiver), and push thisArgument
  // (if present) instead.
  {
    Label done;
    StackArgumentsAccessor args(eax);
    __ LoadRoot(edi, RootIndex::kUndefinedValue);
    __ mov(edx, edi);
    __ mov(ecx, edi);
    __ cmp(eax, Immediate(JSParameterCount(1)));
    __ j(below, &done, Label::kNear);
    __ mov(edi, args[1]);  // target
    __ mov(edx, edi);
    __ j(equal, &done, Label::kNear);
    __ mov(ecx, args[2]);  // argumentsList
    __ cmp(eax, Immediate(JSParameterCount(3)));
    __ j(below, &done, Label::kNear);
    __ mov(edx, args[3]);  // new.target
    __ bind(&done);

    // Spill argumentsList to use ecx as a scratch register.
    __ movd(xmm0, ecx);

    __ DropArgumentsAndPushNewReceiver(
        eax, masm->RootAsOperand(RootIndex::kUndefinedValue), ecx);

    // Restore argumentsList.
    __ movd(ecx, xmm0);
  }

  // ----------- S t a t e -------------
  //  -- ecx    : argumentsList
  //  -- edx    : new.target
  //  -- edi    : target
  //  -- esp[0] : return address
  //  -- esp[4] : receiver (undefined)
  // -----------------------------------

  // 2. We don't need to check explicitly for constructor target here,
  // since that's the first thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 3. We don't need to check explicitly for constructor new.target here,
  // since that's the second thing the Construct/ConstructWithArrayLike
  // builtins will do.

  // 4. Construct the target with the given new.target and argumentsList.
  __ TailCallBuiltin(Builtin::kConstructWithArrayLike);
}

namespace {

// Allocate new stack space for |count| arguments and shift all existing
// arguments already on the stack. |pointer_to_new_space_out| points to the
// first free slot on the stack to copy additional arguments to and
// |argc_in_out| is updated to include |count|.
void Generate_AllocateSpaceAndShiftExistingArguments(
    MacroAssembler* masm, Register count, Register argc_in_out,
    Register pointer_to_new_space_out, Register scratch1, Register scratch2) {
  DCHECK(!AreAliased(count, argc_in_out, pointer_to_new_space_out, scratch1,
                     scratch2));
  // Use pointer_to_new_space_out as scratch until we set it to the correct
  // value at the end.
  Register old_esp = pointer_to_new_space_out;
  Register new_space = scratch1;
  __ mov(old_esp, esp);

  __ lea(new_space, Operand(count, times_system_pointer_size, 0));
  __ AllocateStackSpace(new_space);

  Register current = scratch1;
  Register value = scratch2;

  Label loop, entry;
  __ mov(current, 0);
  __ jmp(&entry);
  __ bind(&loop);
  __ mov(value, Operand(old_esp, current, times_system_pointer_size, 0));
  __ mov(Operand(esp, current, times_system_pointer_size, 0), value);
  __ inc(current);
  __ bind(&entry);
  __ cmp(current, argc_in_out);
  __ j(less_equal, &loop, Label::kNear);

  // Point to the next free slot above the shifted arguments (argc + 1 slot for
  // the return address).
  __ lea(
      pointer_to_new_space_out,
      Operand(esp, argc_in_out, times_system_pointer_size, kSystemPointerSize));
  // Update the total number of arguments.
  __ add(argc_in_out, count);
}

}  // namespace

// static
// TODO(v8:11615): Observe Code::kMaxArguments in CallOrConstructVarargs
void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                               Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- edi    : target
  //  -- esi    : context for the Call / Construct builtin
  //  -- eax    : number of parameters on the stack
  //  -- ecx    : len (number of elements to from args)
  //  -- edx    : new.target (checked to be constructor or undefined)
  //  -- esp[4] : arguments list (a FixedArray)
  //  -- esp[0] : return address.
  // -----------------------------------

  __ movd(xmm0, edx);  // Spill new.target.
  __ movd(xmm1, edi);  // Spill target.
  __ movd(xmm3, esi);  // Spill the context.

  const Register kArgumentsList = esi;
  const Register kArgumentsLength = ecx;

  __ PopReturnAddressTo(edx);
  __ pop(kArgumentsList);
  __ PushReturnAddressFrom(edx);

  if (v8_flags.debug_code) {
    // Allow kArgumentsList to be a FixedArray, or a FixedDoubleArray if
    // kArgumentsLength == 0.
    Label ok, fail;
    __ AssertNotSmi(kArgumentsList);
    __ mov(edx, FieldOperand(kArgumentsList, HeapObject::kMapOffset));
    __ CmpInstanceType(edx, FIXED_ARRAY_TYPE);
    __ j(equal, &ok);
    __ CmpInstanceType(edx, FIXED_DOUBLE_ARRAY_TYPE);
    __ j(not_equal, &fail);
    __ cmp(kArgumentsLength, 0);
    __ j(equal, &ok);
    // Fall through.
    __ bind(&fail);
    __ Abort(AbortReason::kOperandIsNotAFixedArray);

    __ bind(&ok);
  }

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
  __ StackOverflowCheck(kArgumentsLength, edx, &stack_overflow);

  __ movd(xmm4, kArgumentsList);  // Spill the arguments list.
  // Move the arguments already in the stack,
  // including the receiver and the return address.
  // kArgumentsLength (ecx): Number of arguments to make room for.
  // eax: Number of arguments already on the stack.
  // edx: Points to first free slot on the stack after arguments were shifted.
  Generate_AllocateSpaceAndShiftExistingArguments(masm, kArgumentsLength, eax,
                                                  edx, edi, esi);
  __ movd(kArgumentsList, xmm4);  // Recover arguments list.
  __ movd(xmm2, eax);             // Spill argument count.

  // Push additional arguments onto the stack.
  {
    __ Move(eax, Immediate(0));
    Label done, push, loop;
    __ bind(&loop);
    __ cmp(eax, kArgumentsLength);
    __ j(equal, &done, Label::kNear);
    // Turn the hole into undefined as we go.
    __ mov(edi, FieldOperand(kArgumentsList, eax, times_tagged_size,
                             FixedArray::kHeaderSize));
    __ CompareRoot(edi, RootIndex::kTheHoleValue);
    __ j(not_equal, &push, Label::kNear);
    __ LoadRoot(edi, RootIndex::kUndefinedValue);
    __ bind(&push);
    __ mov(Operand(edx, 0), edi);
    __ add(edx, Immediate(kSystemPointerSize));
    __ inc(eax);
    __ jmp(&loop);
    __ bind(&done);
  }

  // Restore eax, edi and edx.
  __ movd(esi, xmm3);  // Restore the context.
  __ movd(eax, xmm2);  // Restore argument count.
  __ movd(edi, xmm1);  // Restore target.
  __ movd(edx, xmm0);  // Restore new.target.

  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ movd(esi, xmm3);  // Restore the context.
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                      CallOrConstructMode mode,
                                                      Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edi : the target to call (can be any Object)
  //  -- esi : context for the Call / Construct builtin
  //  -- edx : the new target (for [[Construct]] calls)
  //  -- ecx : start index (to support rest parameters)
  // -----------------------------------

  __ movd(xmm0, esi);  // Spill the context.

  Register scratch = esi;

  // Check if new.target has a [[Construct]] internal method.
  if (mode == CallOrConstructMode::kConstruct) {
    Label new_target_constructor, new_target_not_constructor;
    __ JumpIfSmi(edx, &new_target_not_constructor, Label::kNear);
    __ mov(scratch, FieldOperand(edx, HeapObject::kMapOffset));
    __ test_b(FieldOperand(scratch, Map::kBitFieldOffset),
              Immediate(Map::Bits1::IsConstructorBit::kMask));
    __ j(not_zero, &new_target_constructor, Label::kNear);
    __ bind(&new_target_not_constructor);
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ Push(edx);
      __ movd(esi, xmm0);  // Restore the context.
      __ CallRuntime(Runtime::kThrowNotConstructor);
    }
    __ bind(&new_target_constructor);
  }

  __ movd(xmm1, edx);  // Preserve new.target (in case of [[Construct]]).

  Label stack_done, stack_overflow;
  __ mov(edx, Operand(ebp, StandardFrameConstants::kArgCOffset));
  __ dec(edx);  // Exclude receiver.
  __ sub(edx, ecx);
  __ j(less_equal, &stack_done);
  {
    // ----------- S t a t e -------------
    //  -- eax : the number of arguments already in the stack
    //  -- ecx : start index (to support rest parameters)
    //  -- edx : number of arguments to copy, i.e. arguments count - start index
    //  -- edi : the target to call (can be any Object)
    //  -- ebp : point to the caller stack frame
    //  -- xmm0 : context for the Call / Construct builtin
    //  -- xmm1 : the new target (for [[Construct]] calls)
    // -----------------------------------

    // Forward the arguments from the caller frame.
    __ movd(xmm2, edi);  // Preserve the target to call.
    __ StackOverflowCheck(edx, edi, &stack_overflow);
    __ movd(xmm3, ebx);  // Preserve root register.

    Register scratch = ebx;

    // Move the arguments already in the stack,
    // including the receiver and the return address.
    // edx: Number of arguments to make room for.
    // eax: Number of arguments already on the stack.
    // esi: Points to first free slot on the stack after arguments were shifted.
    Generate_AllocateSpaceAndShiftExistingArguments(masm, edx, eax, esi, ebx,
                                                    edi);

    // Point to the first argument to copy (skipping receiver).
    __ lea(ecx, Operand(ecx, times_system_pointer_size,
                        CommonFrameConstants::kFixedFrameSizeAboveFp +
                            kSystemPointerSize));
    __ add(ecx, ebp);

    // Copy the additional caller arguments onto the stack.
    // TODO(victorgomes): Consider using forward order as potentially more cache
    // friendly.
    {
      Register src = ecx, dest = esi, num = edx;
      Label loop;
      __ bind(&loop);
      __ dec(num);
      __ mov(scratch, Operand(src, num, times_system_pointer_size, 0));
      __ mov(Operand(dest, num, times_system_pointer_size, 0), scratch);
      __ j(not_zero, &loop);
    }

    __ movd(ebx, xmm3);  // Restore root register.
    __ movd(edi, xmm2);  // Restore the target to call.
  }
  __ bind(&stack_done);

  __ movd(edx, xmm1);  // Restore new.target (in case of [[Construct]]).
  __ movd(esi, xmm0);  // Restore the context.

  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ movd(edi, xmm2);  // Restore the target to call.
  __ movd(esi, xmm0);  // Restore the context.
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallFunction(MacroAssembler* masm,
                                     ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edi : the function to call (checked to be a JSFunction)
  // -----------------------------------
  StackArgumentsAccessor args(eax);
  __ AssertCallableFunction(edi, edx);

  __ mov(edx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));

  // Enter the context of the function; ToObject has to run in the function
  // context, and we also need to take the global proxy from the function
  // context in case of conversion.
  __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));
  // We need to convert the receiver for non-native sloppy mode functions.
  Label done_convert;
  __ test(FieldOperand(edx, SharedFunctionInfo::kFlagsOffset),
          Immediate(SharedFunctionInfo::IsNativeBit::kMask |
                    SharedFunctionInfo::IsStrictBit::kMask));
  __ j(not_zero, &done_convert);
  {
    // ----------- S t a t e -------------
    //  -- eax : the number of arguments
    //  -- edx : the shared function info.
    //  -- edi : the function to call (checked to be a JSFunction)
    //  -- esi : the function context.
    // -----------------------------------

    if (mode == ConvertReceiverMode::kNullOrUndefined) {
      // Patch receiver to global proxy.
      __ LoadGlobalProxy(ecx);
    } else {
      Label convert_to_object, convert_receiver;
      __ mov(ecx, args.GetReceiverOperand());
      __ JumpIfSmi(ecx, &convert_to_object, Label::kNear);
      static_assert(LAST_JS_RECEIVER_TYPE == LAST_TYPE);
      __ CmpObjectType(ecx, FIRST_JS_RECEIVER_TYPE, ecx);  // Clobbers ecx.
      __ j(above_equal, &done_convert);
      // Reload the receiver (it was clobbered by CmpObjectType).
      __ mov(ecx, args.GetReceiverOperand());
      if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
        Label convert_global_proxy;
        __ JumpIfRoot(ecx, RootIndex::kUndefinedValue, &convert_global_proxy,
                      Label::kNear);
        __ JumpIfNotRoot(ecx, RootIndex::kNullValue, &convert_to_object,
                         Label::kNear);
        __ bind(&convert_global_proxy);
        {
          // Patch receiver to global proxy.
          __ LoadGlobalProxy(ecx);
        }
        __ jmp(&convert_receiver);
      }
      __ bind(&convert_to_object);
      {
        // Convert receiver using ToObject.
        // TODO(bmeurer): Inline the allocation here to avoid building the frame
        // in the fast case? (fall back to AllocateInNewSpace?)
        FrameScope scope(masm, StackFrame::INTERNAL);
        __ SmiTag(eax);
        __ Push(eax);
        __ Push(edi);
        __ mov(eax, ecx);
        __ Push(esi);
        __ CallBuiltin(Builtin::kToObject);
        __ Pop(esi);
        __ mov(ecx, eax);
        __ Pop(edi);
        __ Pop(eax);
        __ SmiUntag(eax);
      }
      __ mov(edx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
      __ bind(&convert_receiver);
    }
    __ mov(args.GetReceiverOperand(), ecx);
  }
  __ bind(&done_convert);

  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edx : the shared function info.
  //  -- edi : the function to call (checked to be a JSFunction)
  //  -- esi : the function context.
  // -----------------------------------

  __ movzx_w(
      ecx, FieldOperand(edx, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(edi, no_reg, ecx, eax, InvokeType::kJump);
}

namespace {

void Generate_PushBoundArguments(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edx : new.target (only in case of [[Construct]])
  //  -- edi : target (checked to be a JSBoundFunction)
  // -----------------------------------
  __ movd(xmm0, edx);  // Spill edx.

  // Load [[BoundArguments]] into ecx and length of that into edx.
  Label no_bound_arguments;
  __ mov(ecx, FieldOperand(edi, JSBoundFunction::kBoundArgumentsOffset));
  __ mov(edx, FieldOperand(ecx, FixedArray::kLengthOffset));
  __ SmiUntag(edx);
  __ test(edx, edx);
  __ j(zero, &no_bound_arguments);
  {
    // ----------- S t a t e -------------
    //  -- eax  : the number of arguments
    //  -- xmm0 : new.target (only in case of [[Construct]])
    //  -- edi  : target (checked to be a JSBoundFunction)
    //  -- ecx  : the [[BoundArguments]] (implemented as FixedArray)
    //  -- edx  : the number of [[BoundArguments]]
    // -----------------------------------

    // Check the stack for overflow.
    {
      Label done, stack_overflow;
      __ StackOverflowCheck(edx, ecx, &stack_overflow);
      __ jmp(&done);
      __ bind(&stack_overflow);
      {
        FrameScope frame(masm, StackFrame::MANUAL);
        __ EnterFrame(StackFrame::INTERNAL);
        __ CallRuntime(Runtime::kThrowStackOverflow);
        __ int3();
      }
      __ bind(&done);
    }

    // Spill context.
    __ movd(xmm3, esi);

    // Save Return Adress and Receiver into registers.
    __ pop(esi);
    __ movd(xmm1, esi);
    __ pop(esi);
    __ movd(xmm2, esi);

    // Push [[BoundArguments]] to the stack.
    {
      Label loop;
      __ mov(ecx, FieldOperand(edi, JSBoundFunction::kBoundArgumentsOffset));
      __ mov(edx, FieldOperand(ecx, FixedArray::kLengthOffset));
      __ SmiUntag(edx);
      // Adjust effective number of arguments (eax contains the number of
      // arguments from the call not including receiver plus the number of
      // [[BoundArguments]]).
      __ add(eax, edx);
      __ bind(&loop);
      __ dec(edx);
      __ mov(esi, FieldOperand(ecx, edx, times_tagged_size,
                               FixedArray::kHeaderSize));
      __ push(esi);
      __ j(greater, &loop);
    }

    // Restore Receiver and Return Address.
    __ movd(esi, xmm2);
    __ push(esi);
    __ movd(esi, xmm1);
    __ push(esi);

    // Restore context.
    __ movd(esi, xmm3);
  }

  __ bind(&no_bound_arguments);
  __ movd(edx, xmm0);  // Reload edx.
}

}  // namespace

// static
void Builtins::Generate_CallBoundFunctionImpl(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edi : the function to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertBoundFunction(edi);

  // Patch the receiver to [[BoundThis]].
  StackArgumentsAccessor args(eax);
  __ mov(ecx, FieldOperand(edi, JSBoundFunction::kBoundThisOffset));
  __ mov(args.GetReceiverOperand(), ecx);

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Call the [[BoundTargetFunction]] via the Call builtin.
  __ mov(edi, FieldOperand(edi, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtins::Call());
}

// static
void Builtins::Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edi : the target to call (can be any Object).
  // -----------------------------------
  Register argc = eax;
  Register target = edi;
  Register map = ecx;
  Register instance_type = edx;
  DCHECK(!AreAliased(argc, target, map, instance_type));

  StackArgumentsAccessor args(argc);

  Label non_callable, non_smi, non_callable_jsfunction, non_jsboundfunction,
      non_proxy, non_wrapped_function, class_constructor;
  __ JumpIfSmi(target, &non_callable);
  __ bind(&non_smi);
  __ LoadMap(map, target);
  __ CmpInstanceTypeRange(map, instance_type, map,
                          FIRST_CALLABLE_JS_FUNCTION_TYPE,
                          LAST_CALLABLE_JS_FUNCTION_TYPE);
  __ j(above, &non_callable_jsfunction);
  __ TailCallBuiltin(Builtins::CallFunction(mode));

  __ bind(&non_callable_jsfunction);
  __ cmpw(instance_type, Immediate(JS_BOUND_FUNCTION_TYPE));
  __ j(not_equal, &non_jsboundfunction);
  __ TailCallBuiltin(Builtin::kCallBoundFunction);

  // Check if target is a proxy and call CallProxy external builtin
  __ bind(&non_jsboundfunction);
  __ LoadMap(map, target);
  __ test_b(FieldOperand(map, Map::kBitFieldOffset),
            Immediate(Map::Bits1::IsCallableBit::kMask));
  __ j(zero, &non_callable);

  // Call CallProxy external builtin
  __ cmpw(instance_type, Immediate(JS_PROXY_TYPE));
  __ j(not_equal, &non_proxy);
  __ TailCallBuiltin(Builtin::kCallProxy);

  // Check if target is a wrapped function and call CallWrappedFunction external
  // builtin
  __ bind(&non_proxy);
  __ cmpw(instance_type, Immediate(JS_WRAPPED_FUNCTION_TYPE));
  __ j(not_equal, &non_wrapped_function);
  __ TailCallBuiltin(Builtin::kCallWrappedFunction);

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  // Check that the function is not a "classConstructor".
  __ bind(&non_wrapped_function);
  __ cmpw(instance_type, Immediate(JS_CLASS_CONSTRUCTOR_TYPE));
  __ j(equal, &class_constructor);

  // 2. Call to something else, which might have a [[Call]] internal method (if
  // not we raise an exception).
  // Overwrite the original receiver with the (original) target.
  __ mov(args.GetReceiverOperand(), target);
  // Let the "call_as_function_delegate" take care of the rest.
  __ LoadNativeContextSlot(target, Context::CALL_AS_FUNCTION_DELEGATE_INDEX);
  __ TailCallBuiltin(
      Builtins::CallFunction(ConvertReceiverMode::kNotNullOrUndefined));

  // 3. Call to something that is not callable.
  __ bind(&non_callable);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowCalledNonCallable);
    __ Trap();  // Unreachable.
  }

  // 4. The function is a "classConstructor", need to raise an exception.
  __ bind(&class_constructor);
  {
    FrameScope frame(masm, StackFrame::INTERNAL);
    __ Push(target);
    __ CallRuntime(Runtime::kThrowConstructorNonCallableError);
    __ Trap();  // Unreachable.
  }
}

// static
void Builtins::Generate_ConstructFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edx : the new target (checked to be a constructor)
  //  -- edi : the constructor to call (checked to be a JSFunction)
  // -----------------------------------
  __ AssertConstructor(edi);
  __ AssertFunction(edi, ecx);

  Label call_generic_stub;

  // Jump to JSBuiltinsConstructStub or JSConstructStubGeneric.
  __ mov(ecx, FieldOperand(edi, JSFunction::kSharedFunctionInfoOffset));
  __ test(FieldOperand(ecx, SharedFunctionInfo::kFlagsOffset),
          Immediate(SharedFunctionInfo::ConstructAsBuiltinBit::kMask));
  __ j(zero, &call_generic_stub, Label::kNear);

  // Calling convention for function specific ConstructStubs require
  // ecx to contain either an AllocationSite or undefined.
  __ LoadRoot(ecx, RootIndex::kUndefinedValue);
  __ TailCallBuiltin(Builtin::kJSBuiltinsConstructStub);

  __ bind(&call_generic_stub);
  // Calling convention for function specific ConstructStubs require
  // ecx to contain either an AllocationSite or undefined.
  __ LoadRoot(ecx, RootIndex::kUndefinedValue);
  __ TailCallBuiltin(Builtin::kJSConstructStubGeneric);
}

// static
void Builtins::Generate_ConstructBoundFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edx : the new target (checked to be a constructor)
  //  -- edi : the constructor to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertConstructor(edi);
  __ AssertBoundFunction(edi);

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Patch new.target to [[BoundTargetFunction]] if new.target equals target.
  {
    Label done;
    __ cmp(edi, edx);
    __ j(not_equal, &done, Label::kNear);
    __ mov(edx, FieldOperand(edi, JSBoundFunction::kBoundTargetFunctionOffset));
    __ bind(&done);
  }

  // Construct the [[BoundTargetFunction]] via the Construct builtin.
  __ mov(edi, FieldOperand(edi, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtin::kConstruct);
}

// static
void Builtins::Generate_Construct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- eax : the number of arguments
  //  -- edx : the new target (either the same as the constructor or
  //           the JSFunction on which new was invoked initially)
  //  -- edi : the constructor to call (can be any Object)
  // -----------------------------------
  Register argc = eax;
  Register target = edi;
  Register map = ecx;
  DCHECK(!AreAliased(argc, target, map));

  StackArgumentsAccessor args(argc);

  // Check if target is a Smi.
  Label non_constructor, non_proxy, non_jsfunction, non_jsboundfunction;
  __ JumpIfSmi(target, &non_constructor);

  // Check if target has a [[Construct]] internal method.
  __ mov(map, FieldOperand(target, HeapObject::kMapOffset));
  __ test_b(FieldOperand(map, Map::kBitFieldOffset),
            Immediate(Map::Bits1::IsConstructorBit::kMask));
  __ j(zero, &non_constructor);

  // Dispatch based on instance type.
  __ CmpInstanceTypeRange(map, map, map, FIRST_JS_FUNCTION_TYPE,
                          LAST_JS_FUNCTION_TYPE);
  __ j(above, &non_jsfunction);
  __ TailCallBuiltin(Builtin::kConstructFunction);

  // Only dispatch to bound functions after checking whether they are
  // constructors.
  __ bind(&non_jsfunction);
  __ mov(map, FieldOperand(target, HeapObject::kMapOffset));
  __ CmpInstanceType(map, JS_BOUND_FUNCTION_TYPE);
  __ j(not_equal, &non_jsboundfunction);
  __ TailCallBuiltin(Builtin::kConstructBoundFunction);

  // Only dispatch to proxies after checking whether they are constructors.
  __ bind(&non_jsboundfunction);
  __ CmpInstanceType(map, JS_PROXY_TYPE);
  __ j(not_equal, &non_proxy);
  __ TailCallBuiltin(Builtin::kConstructProxy);

  // Called Construct on an exotic Object with a [[Construct]] internal method.
  __ bind(&non_proxy);
  {
    // Overwrite the original receiver with the (original) target.
    __ mov(args.GetReceiverOperand(), target);
    // Let the "call_as_constructor_delegate" take care of the rest.
    __ LoadNativeContextSlot(target,
                             Context::CALL_AS_CONSTRUCTOR_DELEGATE_INDEX);
    __ TailCallBuiltin(Builtins::CallFunction());
  }

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);
}

namespace {

void Generate_OSREntry(MacroAssembler* masm, Register entry_address) {
  ASM_CODE_COMMENT(masm);
  // Overwrite the return address on the stack.
  __ mov(Operand(esp, 0), entry_address);

  // And "return" to the OSR entry point of the function.
  __ ret(0);
}

enum class OsrSourceTier {
  kInterpreter,
  kBaseline,
};

void OnStackReplacement(MacroAssembler* masm, OsrSourceTier source,
                        Register maybe_target_code) {
  Label jump_to_optimized_code;
  {
    // If maybe_target_code is not null, no need to call into runtime. A
    // precondition here is: if maybe_target_code is a InstructionStream object,
    // it must NOT be marked_for_deoptimization (callers must ensure this).
    __ cmp(maybe_target_code, Immediate(0));
    __ j(not_equal, &jump_to_optimized_code, Label::kNear);
  }

  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kCompileOptimizedOSR);
  }

  // If the code object is null, just return to the caller.
  __ cmp(eax, Immediate(0));
  __ j(not_equal, &jump_to_optimized_code, Label::kNear);
  __ ret(0);

  __ bind(&jump_to_optimized_code);
  DCHECK_EQ(maybe_target_code, eax);  // Already in the right spot.

  // OSR entry tracing.
  {
    Label next;
    __ cmpb(__ ExternalReferenceAsOperand(
                ExternalReference::address_of_log_or_trace_osr(), ecx),
            Immediate(0));
    __ j(equal, &next, Label::kNear);

    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(eax);  // Preserve the code object.
      __ CallRuntime(Runtime::kLogOrTraceOptimizedOSREntry, 0);
      __ Pop(eax);
    }

    __ bind(&next);
  }

  if (source == OsrSourceTier::kInterpreter) {
    // Drop the handler frame that is be sitting on top of the actual
    // JavaScript frame. This is the case then OSR is triggered from bytecode.
    __ leave();
  }

  // Load deoptimization data from the code object.
  __ mov(ecx, Operand(eax, Code::kDeoptimizationDataOrInterpreterDataOffset -
                               kHeapObjectTag));

  // Load the OSR entrypoint offset from the deoptimization data.
  __ mov(ecx, Operand(ecx, FixedArray::OffsetOfElementAt(
                               DeoptimizationData::kOsrPcOffsetIndex) -
                               kHeapObjectTag));
  __ SmiUntag(ecx);

  __ LoadCodeInstructionStart(eax, eax);

  // Compute the target address = code_entry + osr_offset
  __ add(eax, ecx);

  Generate_OSREntry(masm, eax);
}

}  // namespace

void Builtins::Generate_InterpreterOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);
  OnStackReplacement(masm, OsrSourceTier::kInterpreter,
                     D::MaybeTargetCodeRegister());
}

void Builtins::Generate_BaselineOnStackReplacement(MacroAssembler* masm) {
  using D = OnStackReplacementDescriptor;
  static_assert(D::kParameterCount == 1);

  __ mov(kContextRegister,
         MemOperand(ebp, BaselineFrameConstants::kContextOffset));
  OnStackReplacement(masm, OsrSourceTier::kBaseline,
                     D::MaybeTargetCodeRegister());
}

#if V8_ENABLE_WEBASSEMBLY

// Returns the offset beyond the last saved FP register.
int SaveWasmParams(MacroAssembler* masm) {
  // Save all parameter registers (see wasm-linkage.h). They might be
  // overwritten in the subsequent runtime call. We don't have any callee-saved
  // registers in wasm, so no need to store anything else.
  static_assert(WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs + 1 ==
                    arraysize(wasm::kGpParamRegisters),
                "frame size mismatch");
  for (Register reg : wasm::kGpParamRegisters) {
    __ Push(reg);
  }
  static_assert(WasmLiftoffSetupFrameConstants::kNumberOfSavedFpParamRegs ==
                    arraysize(wasm::kFpParamRegisters),
                "frame size mismatch");
  __ AllocateStackSpace(kSimd128Size * arraysize(wasm::kFpParamRegisters));
  int offset = 0;
  for (DoubleRegister reg : wasm::kFpParamRegisters) {
    __ movdqu(Operand(esp, offset), reg);
    offset += kSimd128Size;
  }
  return offset;
}

// Consumes the offset beyond the last saved FP register (as returned by
// {SaveWasmParams}).
void RestoreWasmParams(MacroAssembler* masm, int offset) {
  for (DoubleRegister reg : base::Reversed(wasm::kFpParamRegisters)) {
    offset -= kSimd128Size;
    __ movdqu(reg, Operand(esp, offset));
  }
  DCHECK_EQ(0, offset);
  __ add(esp, Immediate(kSimd128Size * arraysize(wasm::kFpParamRegisters)));
  for (Register reg : base::Reversed(wasm::kGpParamRegisters)) {
    __ Pop(reg);
  }
}

// When this builtin is called, the topmost stack entry is the calling pc.
// This is replaced with the following:
//
// [    calling pc     ]  <-- esp; popped by {ret}.
// [  feedback vector  ]
// [   Wasm instance   ]
// [ WASM frame marker ]
// [    saved ebp      ]  <-- ebp; this is where "calling pc" used to be.
void Builtins::Generate_WasmLiftoffFrameSetup(MacroAssembler* masm) {
  constexpr Register func_index = wasm::kLiftoffFrameSetupFunctionReg;

  // We have zero free registers at this point. Free up a temp. Its value
  // could be tagged, but we're only storing it on the stack for a short
  // while, and no GC or stack walk can happen during this time.
  Register tmp = eax;  // Arbitrarily chosen.
  __ Push(tmp);        // This is the "marker" slot.
  {
    Operand saved_ebp_slot = Operand(esp, kSystemPointerSize);
    __ mov(tmp, saved_ebp_slot);  // tmp now holds the "calling pc".
    __ mov(saved_ebp_slot, ebp);
    __ lea(ebp, Operand(esp, kSystemPointerSize));
  }
  __ Push(tmp);  // This is the "instance" slot.

  // Stack layout is now:
  // [calling pc]  <-- instance_slot  <-- esp
  // [saved tmp]   <-- marker_slot
  // [saved ebp]
  Operand marker_slot = Operand(ebp, WasmFrameConstants::kFrameTypeOffset);
  Operand instance_slot = Operand(ebp, WasmFrameConstants::kWasmInstanceOffset);

  // Load the feedback vector.
  __ mov(tmp, FieldOperand(kWasmInstanceRegister,
                           WasmTrustedInstanceData::kFeedbackVectorsOffset));
  __ mov(tmp, FieldOperand(tmp, func_index, times_tagged_size,
                           FixedArray::kHeaderSize));
  Label allocate_vector;
  __ JumpIfSmi(tmp, &allocate_vector);

  // Vector exists. Finish setting up the stack frame.
  __ Push(tmp);                // Feedback vector.
  __ mov(tmp, instance_slot);  // Calling PC.
  __ Push(tmp);
  __ mov(instance_slot, kWasmInstanceRegister);
  __ mov(tmp, marker_slot);
  __ mov(marker_slot, Immediate(StackFrame::TypeToMarker(StackFrame::WASM)));
  __ ret(0);

  __ bind(&allocate_vector);
  // Feedback vector doesn't exist yet. Call the runtime to allocate it.
  // We temporarily change the frame type for this, because we need special
  // handling by the stack walker in case of GC.
  // For the runtime call, we create the following stack layout:
  //
  // [ reserved slot for NativeModule ]  <-- arg[2]
  // [  ("declared") function index   ]  <-- arg[1] for runtime func.
  // [         Wasm instance          ]  <-- arg[0]
  // [ ...spilled Wasm parameters...  ]
  // [           calling pc           ]  <-- already in place
  // [   WASM_LIFTOFF_SETUP marker    ]
  // [           saved ebp            ]  <-- already in place

  __ mov(tmp, marker_slot);
  __ mov(marker_slot,
         Immediate(StackFrame::TypeToMarker(StackFrame::WASM_LIFTOFF_SETUP)));

  int offset = SaveWasmParams(masm);

  // Arguments to the runtime function: instance, func_index.
  __ Push(kWasmInstanceRegister);
  __ SmiTag(func_index);
  __ Push(func_index);
  // Allocate a stack slot where the runtime function can spill a pointer
  // to the NativeModule.
  __ Push(esp);
  __ Move(kContextRegister, Smi::zero());
  __ CallRuntime(Runtime::kWasmAllocateFeedbackVector, 3);
  tmp = func_index;
  __ mov(tmp, kReturnRegister0);

  RestoreWasmParams(masm, offset);

  // Finish setting up the stack frame:
  //                                    [   calling pc    ]
  //                     (tmp reg) ---> [ feedback vector ]
  // [        calling pc         ]  =>  [  Wasm instance  ]  <-- instance_slot
  // [ WASM_LIFTOFF_SETUP marker ]      [   WASM marker   ]  <-- marker_slot
  // [         saved ebp         ]      [    saved ebp    ]
  __ mov(marker_slot, Immediate(StackFrame::TypeToMarker(StackFrame::WASM)));
  __ Push(tmp);                // Feedback vector.
  __ mov(tmp, instance_slot);  // Calling PC.
  __ Push(tmp);
  __ mov(instance_slot, kWasmInstanceRegister);
  __ ret(0);
}

void Builtins::Generate_WasmCompileLazy(MacroAssembler* masm) {
  // The function index was put in edi by the jump table trampoline.
  // Convert to Smi for the runtime call.
  __ SmiTag(kWasmCompileLazyFuncIndexRegister);
  {
    HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
    FrameScope scope(masm, StackFrame::INTERNAL);
    int offset = SaveWasmParams(masm);

    // Push arguments for the runtime function.
    __ Push(kWasmInstanceRegister);
    __ Push(kWasmCompileLazyFuncIndexRegister);
    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kWasmCompileLazy, 2);
    // The runtime function returns the jump table slot offset as a Smi. Use
    // that to compute the jump target in edi.
    __ SmiUntag(kReturnRegister0);
    __ mov(edi, kReturnRegister0);

    RestoreWasmParams(masm, offset);

    // After the instance register has been restored, we can add the jump table
    // start to the jump table offset already stored in edi.
    __ add(edi, MemOperand(kWasmInstanceRegister,
                           WasmTrustedInstanceData::kJumpTableStartOffset -
                               kHeapObjectTag));
  }

  // Finally, jump to the jump table slot for the function.
  __ jmp(edi);
}

void Builtins::Generate_WasmDebugBreak(MacroAssembler* masm) {
  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  {
    FrameScope scope(masm, StackFrame::WASM_DEBUG_BREAK);

    // Save all parameter registers. They might hold live values, we restore
    // them after the runtime call.
    for (Register reg :
         base::Reversed(WasmDebugBreakFrameConstants::kPushedGpRegs)) {
      __ Push(reg);
    }

    constexpr int kFpStackSize =
        kSimd128Size * WasmDebugBreakFrameConstants::kNumPushedFpRegisters;
    __ AllocateStackSpace(kFpStackSize);
    int offset = kFpStackSize;
    for (DoubleRegister reg :
         base::Reversed(WasmDebugBreakFrameConstants::kPushedFpRegs)) {
      offset -= kSimd128Size;
      __ movdqu(Operand(esp, offset), reg);
    }

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kWasmDebugBreak, 0);

    // Restore registers.
    for (DoubleRegister reg : WasmDebugBreakFrameConstants::kPushedFpRegs) {
      __ movdqu(reg, Operand(esp, offset));
      offset += kSimd128Size;
    }
    __ add(esp, Immediate(kFpStackSize));
    for (Register reg : WasmDebugBreakFrameConstants::kPushedGpRegs) {
      __ Pop(reg);
    }
  }

  __ ret(0);
}

namespace {
// Check that the stack was in the old state (if generated code assertions are
// enabled), and switch to the new state.
void SwitchStackState(MacroAssembler* masm, Register jmpbuf,
                      wasm::JumpBuffer::StackState old_state,
                      wasm::JumpBuffer::StackState new_state) {
  if (v8_flags.debug_code) {
    __ cmp(MemOperand(jmpbuf, wasm::kJmpBufStateOffset), Immediate(old_state));
    Label ok;
    __ j(equal, &ok, Label::kNear);
    __ Trap();
    __ bind(&ok);
  }
  __ mov(MemOperand(jmpbuf, wasm::kJmpBufStateOffset), Immediate(new_state));
}

void FillJumpBuffer(MacroAssembler* masm, Register jmpbuf, Register scratch,
                    Label* pc) {
  DCHECK(!AreAliased(scratch, jmpbuf));

  __ mov(MemOperand(jmpbuf, wasm::kJmpBufSpOffset), esp);
  __ mov(MemOperand(jmpbuf, wasm::kJmpBufFpOffset), ebp);
  __ mov(scratch, __ StackLimitAsOperand(StackLimitKind::kRealStackLimit));
  __ mov(MemOperand(jmpbuf, wasm::kJmpBufStackLimitOffset), scratch);
  __ LoadLabelAddress(scratch, pc);
  __ mov(MemOperand(jmpbuf, wasm::kJmpBufPcOffset), scratch);
}

void LoadJumpBuffer(MacroAssembler* masm, Register jmpbuf, bool load_pc) {
  __ mov(esp, MemOperand(jmpbuf, wasm::kJmpBufSpOffset));
  __ mov(ebp, MemOperand(jmpbuf, wasm::kJmpBufFpOffset));
  SwitchStackState(masm, jmpbuf, wasm::JumpBuffer::Inactive,
                   wasm::JumpBuffer::Active);
  if (load_pc) {
    __ jmp(MemOperand(jmpbuf, wasm::kJmpBufPcOffset));
  }
  // The stack limit is set separately under the ExecutionAccess lock.
}

void SaveState(MacroAssembler* masm, Register active_continuation, Register tmp,
               Register tmp2, Label* suspend) {
  DCHECK(!AreAliased(active_continuation, tmp));
  Register jmpbuf = tmp;
  __ mov(jmpbuf, FieldOperand(active_continuation,
                              WasmContinuationObject::kJmpbufOffset));
  FillJumpBuffer(masm, jmpbuf, tmp2, suspend);
}

void LoadTargetJumpBuffer(MacroAssembler* masm, Register target_continuation) {
  Register target_jmpbuf = target_continuation;
  __ mov(target_jmpbuf, FieldOperand(target_continuation,
                                     WasmContinuationObject::kJmpbufOffset));
  MemOperand GCScanSlotPlace =
      MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Move(GCScanSlotPlace, Immediate(0));
  // Switch stack!
  LoadJumpBuffer(masm, target_jmpbuf, false);
}

// Updates the stack limit to match the new active stack.
// Pass the {finished_continuation} argument to indicate that the stack that we
// are switching from has returned, and in this case return its memory to the
// stack pool.
void SwitchStacks(MacroAssembler* masm, Register finished_continuation,
                  const Register& keep1, const Register& keep2 = no_reg,
                  const Register& keep3 = no_reg) {
  using ER = ExternalReference;
  __ Push(keep1);
  if (keep2 != no_reg) {
    __ Push(keep2);
  }
  if (keep3 != no_reg) {
    __ Push(keep3);
  }
  if (finished_continuation != no_reg) {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(2, eax);
    __ Move(Operand(esp, 0 * kSystemPointerSize),
            Immediate(ER::isolate_address(masm->isolate())));
    __ mov(Operand(esp, 1 * kSystemPointerSize), finished_continuation);
    __ CallCFunction(ER::wasm_return_switch(), 2);
  } else {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(1, eax);
    __ Move(Operand(esp, 0 * kSystemPointerSize),
            Immediate(ER::isolate_address()));
    __ CallCFunction(ER::wasm_sync_stack_limit(), 1);
  }
  if (keep3 != no_reg) {
    __ Pop(keep3);
  }
  if (keep2 != no_reg) {
    __ Pop(keep2);
  }
  __ Pop(keep1);
}

void ReloadParentContinuation(MacroAssembler* masm, Register promise,
                              Register return_value, Register context,
                              Register tmp, Register tmp2) {
  Register active_continuation = tmp;
  __ LoadRoot(active_continuation, RootIndex::kActiveContinuation);

  DCHECK(!AreAliased(promise, return_value, context, tmp));

  __ Push(promise);

  // We don't need to save the full register state since we are switching out of
  // this stack for the last time. Mark the stack as retired.
  Register jmpbuf = promise;
  __ mov(jmpbuf, FieldOperand(active_continuation,
                              WasmContinuationObject::kJmpbufOffset));
  SwitchStackState(masm, jmpbuf, wasm::JumpBuffer::Active,
                   wasm::JumpBuffer::Retired);

  Register parent = tmp2;
  __ mov(parent, FieldOperand(active_continuation,
                              WasmContinuationObject::kParentOffset));

  // Update active continuation root.
  __ mov(masm->RootAsOperand(RootIndex::kActiveContinuation), parent);
  jmpbuf = parent;
  __ mov(jmpbuf, FieldOperand(parent, WasmContinuationObject::kJmpbufOffset));

  __ Pop(promise);
  // Switch stack!
  LoadJumpBuffer(masm, jmpbuf, false);
  SwitchStacks(masm, active_continuation, promise, return_value, context);
}

// Loads the context field of the WasmTrustedInstanceData or WasmImportData
// depending on the ref's type, and places the result in the input register.
void GetContextFromRef(MacroAssembler* masm, Register ref, Register scratch) {
  __ Move(scratch, FieldOperand(ref, HeapObject::kMapOffset));
  __ CmpInstanceType(scratch, WASM_TRUSTED_INSTANCE_DATA_TYPE);
  Label instance;
  Label end;
  __ j(equal, &instance);
  __ Move(ref, FieldOperand(ref, WasmImportData::kNativeContextOffset));
  __ jmp(&end);
  __ bind(&instance);
  __ Move(ref,
          FieldOperand(ref, WasmTrustedInstanceData::kNativeContextOffset));
  __ bind(&end);
}

void RestoreParentSuspender(MacroAssembler* masm, Register tmp1,
                            Register tmp2) {
  Register suspender = tmp1;
  __ LoadRoot(suspender, RootIndex::kActiveSuspender);
  __ mov(FieldOperand(suspender, WasmSuspenderObject::kStateOffset),
         Immediate(Smi::FromInt(WasmSuspenderObject::kInactive)));
  __ Move(suspender,
          FieldOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ CompareRoot(suspender, RootIndex::kUndefinedValue);
  Label undefined;
  __ j(equal, &undefined, Label::kNear);
#ifdef DEBUG
  // Check that the parent suspender is active.
  Label parent_inactive;
  Register state = tmp2;
  __ Move(state, FieldOperand(suspender, WasmSuspenderObject::kStateOffset));
  __ SmiCompare(state, Smi::FromInt(WasmSuspenderObject::kActive));
  __ j(equal, &parent_inactive, Label::kNear);
  __ Trap();
  __ bind(&parent_inactive);
#endif
  __ Move(FieldOperand(suspender, WasmSuspenderObject::kStateOffset),
          Immediate(Smi::FromInt(WasmSuspenderObject::kActive)));
  __ bind(&undefined);
  __ mov(masm->RootAsOperand(RootIndex::kActiveSuspender), suspender);
}

void ResetStackSwitchFrameStackSlots(MacroAssembler* masm) {
  __ mov(MemOperand(ebp, StackSwitchFrameConstants::kRefOffset), Immediate(0));
  __ mov(MemOperand(ebp, StackSwitchFrameConstants::kResultArrayOffset),
         Immediate(0));
}

void SwitchToAllocatedStack(MacroAssembler* masm, Register wrapper_buffer,
                            Register original_fp, Register new_wrapper_buffer,
                            Register scratch, Register scratch2,
                            Label* suspend) {
  ResetStackSwitchFrameStackSlots(masm);
  Register parent_continuation = new_wrapper_buffer;
  __ LoadRoot(parent_continuation, RootIndex::kActiveContinuation);
  __ Move(
      parent_continuation,
      FieldOperand(parent_continuation, WasmContinuationObject::kParentOffset));
  SaveState(masm, parent_continuation, scratch, scratch2, suspend);
  SwitchStacks(masm, no_reg, wrapper_buffer);
  parent_continuation = no_reg;
  Register target_continuation = scratch;
  __ LoadRoot(target_continuation, RootIndex::kActiveContinuation);
  // Save the old stack's ebp, and use it to access the parameters in
  // the parent frame.
  __ mov(original_fp, ebp);
  LoadTargetJumpBuffer(masm, target_continuation);
  // Push the loaded ebp. We know it is null, because there is no frame yet,
  // so we could also push 0 directly. In any case we need to push it, because
  // this marks the base of the stack segment for the stack frame iterator.
  __ EnterFrame(StackFrame::STACK_SWITCH);
  int stack_space =
      StackSwitchFrameConstants::kNumSpillSlots * kSystemPointerSize +
      JSToWasmWrapperFrameConstants::kWrapperBufferSize;
  __ AllocateStackSpace(stack_space);
  __ AlignStackPointer();
  __ mov(new_wrapper_buffer, esp);
  // Copy data needed for return handling from old wrapper buffer to new one.
  __ mov(scratch,
         MemOperand(wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount));
  __ mov(MemOperand(new_wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount),
         scratch);
  __ mov(
      scratch,
      MemOperand(wrapper_buffer,
                 JSToWasmWrapperFrameConstants::kWrapperBufferRefReturnCount));
  __ mov(
      MemOperand(new_wrapper_buffer,
                 JSToWasmWrapperFrameConstants::kWrapperBufferRefReturnCount),
      scratch);
  __ mov(
      scratch,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray));
  __ mov(
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray),
      scratch);
  __ mov(
      scratch,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray +
              4));
  __ mov(
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray +
              4),
      scratch);
}

void SwitchBackAndReturnPromise(MacroAssembler* masm, Register tmp,
                                Register tmp2, Label* return_promise) {
  // The return value of the wasm function becomes the parameter of the
  // FulfillPromise builtin, and the promise is the return value of this
  // wrapper.

  static const Builtin_FulfillPromise_InterfaceDescriptor desc;
  static_assert(kReturnRegister0 == desc.GetRegisterParameter(0));

  Register promise = desc.GetRegisterParameter(0);
  Register return_value = desc.GetRegisterParameter(1);

  __ mov(return_value, kReturnRegister0);

  __ LoadRoot(promise, RootIndex::kActiveSuspender);
  __ Move(promise, FieldOperand(promise, WasmSuspenderObject::kPromiseOffset));
  __ mov(kContextRegister,
         MemOperand(ebp, StackSwitchFrameConstants::kRefOffset));
  GetContextFromRef(masm, kContextRegister, tmp);

  ReloadParentContinuation(masm, promise, return_value, kContextRegister, tmp,
                           tmp2);
  __ Push(promise);
  RestoreParentSuspender(masm, promise, tmp);
  __ Pop(promise);

  __ Move(MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
          Immediate(1));
  __ Push(promise);
  __ CallBuiltin(Builtin::kFulfillPromise);
  __ Pop(promise);

  __ bind(return_promise);
}

void GenerateExceptionHandlingLandingPad(MacroAssembler* masm,
                                         Label* return_promise) {
  int catch_handler = __ pc_offset();

  // Restore esp to free the reserved stack slots for the sections.
  __ lea(esp, MemOperand(ebp, StackSwitchFrameConstants::kLastSpillOffset));

  // Unset thread_in_wasm_flag.
  Register thread_in_wasm_flag_addr = ecx;
  __ mov(
      thread_in_wasm_flag_addr,
      MemOperand(kRootRegister, Isolate::thread_in_wasm_flag_address_offset()));
  __ mov(MemOperand(thread_in_wasm_flag_addr, 0), Immediate(0));
  thread_in_wasm_flag_addr = no_reg;

  // The exception becomes the parameter of the RejectPromise builtin, and the
  // promise is the return value of this wrapper.
  static const Builtin_RejectPromise_InterfaceDescriptor desc;
  constexpr Register promise = desc.GetRegisterParameter(0);
  constexpr Register reason = desc.GetRegisterParameter(1);
  DCHECK(kReturnRegister0 == promise);

  __ mov(reason, kReturnRegister0);

  __ LoadRoot(promise, RootIndex::kActiveSuspender);
  __ Move(promise, FieldOperand(promise, WasmSuspenderObject::kPromiseOffset));

  __ mov(kContextRegister,
         MemOperand(ebp, StackSwitchFrameConstants::kRefOffset));
  constexpr Register tmp1 = edi;
  static_assert(tmp1 != promise && tmp1 != reason && tmp1 != kContextRegister);
  constexpr Register tmp2 = edx;
  static_assert(tmp2 != promise && tmp2 != reason && tmp2 != kContextRegister);
  GetContextFromRef(masm, kContextRegister, tmp1);
  ReloadParentContinuation(masm, promise, reason, kContextRegister, tmp1, tmp2);
  __ Push(promise);
  RestoreParentSuspender(masm, promise, edi);
  __ Pop(promise);

  __ Move(MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
          Immediate(1));
  __ Push(promise);

  Register debug_event = desc.GetRegisterParameter(2);
  __ LoadRoot(debug_event, RootIndex::kTrueValue);
  __ CallBuiltin(Builtin::kRejectPromise);
  __ Pop(promise);

  // Run the rest of the wrapper normally (switch to the old stack,
  // deconstruct the frame, ...).
  __ jmp(return_promise);

  masm->isolate()->builtins()->SetJSPIPromptHandlerOffset(catch_handler);
}

void JSToWasmWrapperHelper(MacroAssembler* masm, bool stack_switch) {
  __ EnterFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);

  constexpr int kNumSpillSlots = StackSwitchFrameConstants::kNumSpillSlots;
  __ sub(esp, Immediate(kNumSpillSlots * kSystemPointerSize));

  ResetStackSwitchFrameStackSlots(masm);

  Register wrapper_buffer =
      WasmJSToWasmWrapperDescriptor::WrapperBufferRegister();

  Register original_fp = stack_switch ? esi : ebp;
  Register new_wrapper_buffer = stack_switch ? ecx : wrapper_buffer;

  Label suspend;
  if (stack_switch) {
    SwitchToAllocatedStack(masm, wrapper_buffer, original_fp,
                           new_wrapper_buffer, eax, edx, &suspend);
  }
  __ mov(MemOperand(ebp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset),
         new_wrapper_buffer);
  if (stack_switch) {
    // Preserve wasm_instance across the switch.
    __ mov(eax, MemOperand(original_fp,
                           JSToWasmWrapperFrameConstants::kRefParamOffset));
    __ mov(MemOperand(ebp, StackSwitchFrameConstants::kRefOffset), eax);

    Register result_array = eax;
    __ mov(result_array,
           MemOperand(original_fp,
                      JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
    __ mov(MemOperand(ebp, StackSwitchFrameConstants::kResultArrayOffset),
           result_array);
  }

  Register result_size = eax;
  original_fp = no_reg;

  MemOperand GCScanSlotPlace =
      MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Move(GCScanSlotPlace, Immediate(0));

  __ mov(
      result_size,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferStackReturnBufferSize));
  __ shl(result_size, kSystemPointerSizeLog2);
  __ sub(esp, result_size);
  __ mov(
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferStackReturnBufferStart),
      esp);

  result_size = no_reg;
  new_wrapper_buffer = no_reg;

  // param_start should not alias with any parameter registers.
  Register params_start = eax;
  __ mov(params_start,
         MemOperand(wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferParamStart));
  Register params_end = esi;
  __ mov(params_end,
         MemOperand(wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferParamEnd));

  Register last_stack_param = ecx;

  // The first GP parameter is the instance, which we handle specially.
  int stack_params_offset =
      (arraysize(wasm::kGpParamRegisters) - 1) * kSystemPointerSize +
      arraysize(wasm::kFpParamRegisters) * kDoubleSize;

  int param_padding = stack_params_offset & kSystemPointerSize;
  stack_params_offset += param_padding;
  __ lea(last_stack_param, MemOperand(params_start, stack_params_offset));

  Label loop_start;
  __ bind(&loop_start);

  Label finish_stack_params;
  __ cmp(last_stack_param, params_end);
  __ j(greater_equal, &finish_stack_params);

  // Push parameter
  __ sub(params_end, Immediate(kSystemPointerSize));
  __ push(MemOperand(params_end, 0));
  __ jmp(&loop_start);

  __ bind(&finish_stack_params);

  int next_offset = stack_params_offset;
  for (size_t i = arraysize(wasm::kFpParamRegisters) - 1;
       i < arraysize(wasm::kFpParamRegisters); --i) {
    next_offset -= kDoubleSize;
    __ Movsd(wasm::kFpParamRegisters[i], MemOperand(params_start, next_offset));
  }

  // Set the flag-in-wasm flag before loading the parameter registers. There are
  // not so many registers, so we use one of the parameter registers before it
  // is blocked.
  Register thread_in_wasm_flag_addr = ecx;
  __ mov(
      thread_in_wasm_flag_addr,
      MemOperand(kRootRegister, Isolate::thread_in_wasm_flag_address_offset()));
  __ mov(MemOperand(thread_in_wasm_flag_addr, 0), Immediate(1));

  next_offset -= param_padding;
  for (size_t i = arraysize(wasm::kGpParamRegisters) - 1; i > 0; --i) {
    next_offset -= kSystemPointerSize;
    __ mov(wasm::kGpParamRegisters[i], MemOperand(params_start, next_offset));
  }
  DCHECK_EQ(next_offset, 0);
  // Since there are so few registers, {params_start} overlaps with one of the
  // parameter registers. Make sure it overlaps with the last one we fill.
  DCHECK_EQ(params_start, wasm::kGpParamRegisters[1]);

  // Pick up the instance from frame.
  if (stack_switch) {
    __ mov(kWasmInstanceRegister,
           MemOperand(ebp, StackSwitchFrameConstants::kRefOffset));
  } else {
    __ mov(kWasmInstanceRegister,
           MemOperand(ebp, JSToWasmWrapperFrameConstants::kRefParamOffset));
  }

  Register call_target = edi;
  __ mov(call_target,
         MemOperand(wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferCallTarget));
  if (stack_switch) {
    __ Move(MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
            Immediate(0));
  }
  __ call(call_target);

  __ mov(
      thread_in_wasm_flag_addr,
      MemOperand(kRootRegister, Isolate::thread_in_wasm_flag_address_offset()));
  __ mov(MemOperand(thread_in_wasm_flag_addr, 0), Immediate(0));
  thread_in_wasm_flag_addr = no_reg;

  wrapper_buffer = esi;
  __ mov(wrapper_buffer,
         MemOperand(ebp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset));

  __ Movsd(MemOperand(
               wrapper_buffer,
               JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister1),
           wasm::kFpReturnRegisters[0]);
  __ Movsd(MemOperand(
               wrapper_buffer,
               JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister2),
           wasm::kFpReturnRegisters[1]);
  __ mov(MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister1),
         wasm::kGpReturnRegisters[0]);
  __ mov(MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister2),
         wasm::kGpReturnRegisters[1]);

  // Call the return value builtin with
  // eax: wasm instance.
  // ecx: the result JSArray for multi-return.
  // edx: pointer to the byte buffer which contains all parameters.
  if (stack_switch) {
    __ mov(eax, MemOperand(ebp, StackSwitchFrameConstants::kRefOffset));
    __ mov(ecx, MemOperand(ebp, StackSwitchFrameConstants::kResultArrayOffset));
  } else {
    __ mov(eax,
           MemOperand(ebp, JSToWasmWrapperFrameConstants::kRefParamOffset));
    __ mov(ecx,
           MemOperand(ebp,
                      JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
  }
  Register scratch = edx;
  GetContextFromRef(masm, eax, scratch);
  __ mov(edx, wrapper_buffer);
  __ CallBuiltin(Builtin::kJSToWasmHandleReturns);

  Label return_promise;

  if (stack_switch) {
    SwitchBackAndReturnPromise(masm, edx, edi, &return_promise);
  }
  __ bind(&suspend);

  __ LeaveFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);
  __ ret(0);

  // Catch handler for the stack-switching wrapper: reject the promise with the
  // thrown exception.
  if (stack_switch) {
    GenerateExceptionHandlingLandingPad(masm, &return_promise);
  }
}
}  // namespace

void Builtins::Generate_JSToWasmWrapperAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, false);
}

void Builtins::Generate_WasmReturnPromiseOnSuspendAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, true);
}

void Builtins::Generate_WasmToJsWrapperAsm(MacroAssembler* masm) {
  // Pop the return address into a scratch register and push it later again. The
  // return address has to be on top of the stack after all registers have been
  // pushed, so that the return instruction can find it.
  Register scratch = edi;
  __ pop(scratch);

  int required_stack_space = arraysize(wasm::kFpParamRegisters) * kDoubleSize;
  __ sub(esp, Immediate(required_stack_space));
  for (int i = 0; i < static_cast<int>(arraysize(wasm::kFpParamRegisters));
       ++i) {
    __ Movsd(MemOperand(esp, i * kDoubleSize), wasm::kFpParamRegisters[i]);
  }
  // eax is pushed for alignment, so that the pushed register parameters and
  // stack parameters look the same as the layout produced by the js-to-wasm
  // wrapper for out-going parameters. Having the same layout allows to share
  // code in Torque, especially the `LocationAllocator`. eax has been picked
  // arbitrarily.
  __ push(eax);
  // Push the GP registers in reverse order so that they are on the stack like
  // in an array, with the first item being at the lowest address.
  for (size_t i = arraysize(wasm::kGpParamRegisters) - 1; i > 0; --i) {
    __ push(wasm::kGpParamRegisters[i]);
  }
  // Reserve fixed slots for the CSA wrapper.
  // Two slots for stack-switching (central stack pointer and secondary stack
  // limit):
  __ push(Immediate(0));
  __ push(Immediate(0));
  // One slot for the signature:
  __ push(eax);
  // Push the return address again.
  __ push(scratch);
  __ TailCallBuiltin(Builtin::kWasmToJsWrapperCSA);
}

void Builtins::Generate_WasmTrapHandlerLandingPad(MacroAssembler* masm) {
  __ Trap();
}

void Builtins::Generate_WasmSuspend(MacroAssembler* masm) {
  // Set up the stackframe.
  __ EnterFrame(StackFrame::STACK_SWITCH);

  Register suspender = eax;

  __ AllocateStackSpace(StackSwitchFrameConstants::kNumSpillSlots *
                        kSystemPointerSize);
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  // -------------------------------------------
  // Save current state in active jump buffer.
  // -------------------------------------------
  Label resume;
  Register continuation = ecx;
  __ LoadRoot(continuation, RootIndex::kActiveContinuation);
  Register jmpbuf = edi;
  __ Move(jmpbuf,
          FieldOperand(continuation, WasmContinuationObject::kJmpbufOffset));
  FillJumpBuffer(masm, jmpbuf, edx, &resume);
  SwitchStackState(masm, jmpbuf, wasm::JumpBuffer::Active,
                   wasm::JumpBuffer::Inactive);
  __ Move(FieldOperand(suspender, WasmSuspenderObject::kStateOffset),
          Immediate(Smi::FromInt(WasmSuspenderObject::kSuspended)));
  jmpbuf = no_reg;

  Register suspender_continuation = edi;
  __ Move(suspender_continuation,
          FieldOperand(suspender, WasmSuspenderObject::kContinuationOffset));
#ifdef DEBUG
  // -------------------------------------------
  // Check that the suspender's continuation is the active continuation.
  // -------------------------------------------
  // TODO(thibaudm): Once we add core stack-switching instructions, this check
  // will not hold anymore: it's possible that the active continuation changed
  // (due to an internal switch), so we have to update the suspender.
  __ cmp(suspender_continuation, continuation);
  Label ok;
  __ j(equal, &ok);
  __ Trap();
  __ bind(&ok);
#endif

  // -------------------------------------------
  // Update roots.
  // -------------------------------------------
  Register caller = ecx;
  __ Move(caller, FieldOperand(suspender_continuation,
                               WasmContinuationObject::kParentOffset));
  __ mov(masm->RootAsOperand(RootIndex::kActiveContinuation), caller);
  Register parent = edi;
  __ Move(parent, FieldOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ mov(masm->RootAsOperand(RootIndex::kActiveSuspender), parent);
  parent = no_reg;

  // -------------------------------------------
  // Load jump buffer.
  // -------------------------------------------
  SwitchStacks(masm, no_reg, caller, suspender);
  jmpbuf = caller;
  __ Move(jmpbuf, FieldOperand(caller, WasmContinuationObject::kJmpbufOffset));
  caller = no_reg;
  __ Move(kReturnRegister0,
          FieldOperand(suspender, WasmSuspenderObject::kPromiseOffset));
  MemOperand GCScanSlotPlace =
      MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Move(GCScanSlotPlace, Immediate(0));
  LoadJumpBuffer(masm, jmpbuf, true);
  __ Trap();
  __ bind(&resume);
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  __ ret(0);
}

namespace {
// Resume the suspender stored in the closure. We generate two variants of this
// builtin: the onFulfilled variant resumes execution at the saved PC and
// forwards the value, the onRejected variant throws the value.

void Generate_WasmResumeHelper(MacroAssembler* masm, wasm::OnResume on_resume) {
  __ EnterFrame(StackFrame::STACK_SWITCH);

  Register closure = kJSFunctionRegister;  // edi

  __ AllocateStackSpace(StackSwitchFrameConstants::kNumSpillSlots *
                        kSystemPointerSize);
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  // -------------------------------------------
  // Load suspender from closure.
  // -------------------------------------------
  Register sfi = closure;
  __ Move(
      sfi,
      MemOperand(
          closure,
          wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction()));
  Register function_data = sfi;
  __ Move(function_data,
          FieldOperand(sfi, SharedFunctionInfo::kUntrustedFunctionDataOffset));
  // The write barrier uses a fixed register for the host object (edi). The next
  // barrier is on the suspender, so load it in edi directly.
  Register suspender = edi;
  __ Move(suspender,
          FieldOperand(function_data, WasmResumeData::kSuspenderOffset));
  // Check the suspender state.
  Label suspender_is_suspended;
  Register state = edx;
  __ Move(state, FieldOperand(suspender, WasmSuspenderObject::kStateOffset));
  __ SmiCompare(state, Smi::FromInt(WasmSuspenderObject::kSuspended));
  __ j(equal, &suspender_is_suspended);
  __ Trap();
  closure = no_reg;
  sfi = no_reg;

  __ bind(&suspender_is_suspended);
  // -------------------------------------------
  // Save current state.
  // -------------------------------------------

  Label suspend;
  Register active_continuation = edx;
  __ LoadRoot(active_continuation, RootIndex::kActiveContinuation);
  Register current_jmpbuf = eax;
  __ Move(current_jmpbuf, FieldOperand(active_continuation,
                                       WasmContinuationObject::kJmpbufOffset));
  active_continuation = no_reg;  // We reload this later.
  FillJumpBuffer(masm, current_jmpbuf, edx, &suspend);
  SwitchStackState(masm, current_jmpbuf, wasm::JumpBuffer::Active,
                   wasm::JumpBuffer::Inactive);
  current_jmpbuf = no_reg;

  // -------------------------------------------
  // Set the suspender and continuation parents and update the roots.
  // -------------------------------------------
  Register active_suspender = edx;
  Register slot_address = WriteBarrierDescriptor::SlotAddressRegister();
  // Check that the fixed register isn't one that is already in use.
  DCHECK(!AreAliased(slot_address, suspender, active_suspender));

  __ LoadRoot(active_suspender, RootIndex::kActiveSuspender);
  __ mov(FieldOperand(suspender, WasmSuspenderObject::kParentOffset),
         active_suspender);
  __ RecordWriteField(suspender, WasmSuspenderObject::kParentOffset,
                      active_suspender, slot_address, SaveFPRegsMode::kIgnore);
  __ Move(FieldOperand(suspender, WasmSuspenderObject::kStateOffset),
          Immediate(Smi::FromInt(WasmSuspenderObject::kActive)));
  __ mov(masm->RootAsOperand(RootIndex::kActiveSuspender), suspender);

  active_suspender = no_reg;

  Register target_continuation = suspender;
  __ Move(target_continuation,
          FieldOperand(suspender, WasmSuspenderObject::kContinuationOffset));
  suspender = no_reg;
  active_continuation = edx;
  __ LoadRoot(active_continuation, RootIndex::kActiveContinuation);
  __ mov(
      FieldOperand(target_continuation, WasmContinuationObject::kParentOffset),
      active_continuation);
  __ RecordWriteField(
      target_continuation, WasmContinuationObject::kParentOffset,
      active_continuation, slot_address, SaveFPRegsMode::kIgnore);
  active_continuation = no_reg;
  __ mov(masm->RootAsOperand(RootIndex::kActiveContinuation),
         target_continuation);

  SwitchStacks(masm, no_reg, target_continuation);

  // -------------------------------------------
  // Load state from target jmpbuf (longjmp).
  // -------------------------------------------
  Register target_jmpbuf = edi;
  __ Move(target_jmpbuf, FieldOperand(target_continuation,
                                      WasmContinuationObject::kJmpbufOffset));
  // Move resolved value to return register.
  __ mov(kReturnRegister0, Operand(ebp, 3 * kSystemPointerSize));
  __ Move(MemOperand(ebp, StackSwitchFrameConstants::kGCScanSlotCountOffset),
          Immediate(0));
  if (on_resume == wasm::OnResume::kThrow) {
    // Switch to the continuation's stack without restoring the PC.
    LoadJumpBuffer(masm, target_jmpbuf, false);
    // Pop this frame now. The unwinder expects that the first STACK_SWITCH
    // frame is the outermost one.
    __ LeaveFrame(StackFrame::STACK_SWITCH);
    // Forward the onRejected value to kThrow.
    __ push(kReturnRegister0);
    __ Move(kContextRegister, Smi::zero());
    __ CallRuntime(Runtime::kThrow);
  } else {
    // Resume the continuation normally.
    LoadJumpBuffer(masm, target_jmpbuf, true);
  }
  __ Trap();
  __ bind(&suspend);
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  // Pop receiver + parameter.
  __ ret(2 * kSystemPointerSize);
}
}  // namespace

void Builtins::Generate_WasmResume(MacroAssembler* masm) {
  Generate_WasmResumeHelper(masm, wasm::OnResume::kContinue);
}

void Builtins::Generate_WasmReject(MacroAssembler* masm) {
  Generate_WasmResumeHelper(masm, wasm::OnResume::kThrow);
}

void Builtins::Generate_WasmOnStackReplace(MacroAssembler* masm) {
  // Only needed on x64.
  __ Trap();
}

namespace {
static constexpr Register kOldSPRegister = esi;

void SwitchToTheCentralStackIfNeeded(MacroAssembler* masm, int edi_slot_index) {
  using ER = ExternalReference;

  // Preserve edi on the stack as a local.
  __ mov(ExitFrameStackSlotOperand(edi_slot_index * kSystemPointerSize), edi);

  // kOldSPRegister used as a switch flag, if it is zero - no switch performed
  // if it is not zero, it contains old sp value.
  __ Move(kOldSPRegister, 0);

  DCHECK(!AreAliased(kOldSPRegister, ecx, ebx));

  ER on_central_stack_flag = ER::Create(
      IsolateAddressId::kIsOnCentralStackFlagAddress, masm->isolate());

  Label do_not_need_to_switch;
  __ cmpb(__ ExternalReferenceAsOperand(on_central_stack_flag, ecx),
          Immediate(0));
  __ j(not_zero, &do_not_need_to_switch);

  // Perform switching to the central stack.
  __ mov(kOldSPRegister, esp);

  Register argc_input = eax;
  Register central_stack_sp = edi;
  DCHECK(!AreAliased(central_stack_sp, argc_input));
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ push(argc_input);
    __ push(kRuntimeCallFunctionRegister);

    __ PrepareCallCFunction(2, ecx);

    __ Move(Operand(esp, 0 * kSystemPointerSize),
            Immediate(ER::isolate_address()));
    __ mov(Operand(esp, 1 * kSystemPointerSize), kOldSPRegister);

    __ CallCFunction(ER::wasm_switch_to_the_central_stack(), 2,
                     SetIsolateDataSlots::kNo);
    __ mov(central_stack_sp, kReturnRegister0);

    __ pop(kRuntimeCallFunctionRegister);
    __ pop(argc_input);
  }

  static constexpr int kReturnAddressSlotOffset = 4 * kSystemPointerSize;
  __ sub(central_stack_sp, Immediate(kReturnAddressSlotOffset));
  __ mov(esp, central_stack_sp);

  // esp should be aligned by 16 bytes,
  // but it is not guaranteed for stored SP.
  __ AlignStackPointer();

  // Update the sp saved in the frame.
  // It will be used to calculate the callee pc during GC.
  // The pc is going to be on the new stack segment, so rewrite it here.
  __ mov(Operand(ebp, ExitFrameConstants::kSPOffset), esp);

  Label exitLabel;
  // Restore bashed edi, so we can make the CCall properly.
  __ mov(edi, Operand(kOldSPRegister, edi_slot_index * kSystemPointerSize));
  __ jmp(&exitLabel);
  __ bind(&do_not_need_to_switch);
  __ mov(edi, ExitFrameStackSlotOperand(edi_slot_index * kSystemPointerSize));

  __ bind(&exitLabel);
}

void SwitchFromTheCentralStackIfNeeded(MacroAssembler* masm) {
  using ER = ExternalReference;

  Label no_stack_change;
  __ cmp(kOldSPRegister, Immediate(0));
  __ j(equal, &no_stack_change);
  __ mov(esp, kOldSPRegister);

  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ push(kReturnRegister0);
    __ push(kReturnRegister1);

    __ PrepareCallCFunction(1, ecx);
    __ Move(Operand(esp, 0 * kSystemPointerSize),
            Immediate(ER::isolate_address()));
    __ CallCFunction(ER::wasm_switch_from_the_central_stack(), 1,
                     SetIsolateDataSlots::kNo);

    __ pop(kReturnRegister1);
    __ pop(kReturnRegister0);
  }

  __ bind(&no_stack_change);
}

}  // namespace

void Builtins::Generate_WasmToOnHeapWasmToJsTrampoline(MacroAssembler* masm) {
  // Load the code pointer from the WasmImportData and tail-call there.
  Register import_data = wasm::kGpParamRegisters[0];
  Register code_object = edi;  // Not part of kGpParamRegisters.
  __ mov(code_object, FieldOperand(import_data, WasmImportData::kCodeOffset));
  __ jmp(FieldOperand(code_object, Code::kInstructionStartOffset));
}

#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_CEntry(MacroAssembler* masm, int result_size,
                               ArgvMode argv_mode, bool builtin_exit_frame,
                               bool switch_to_central_stack) {
  CHECK(result_size == 1 || result_size == 2);

  using ER = ExternalReference;

  // eax: number of arguments including receiver
  // edx: pointer to C function
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // esi: current context (C callee-saved)
  // edi: JS function of the caller (C callee-saved)
  //
  // If argv_mode == ArgvMode::kRegister:
  // ecx: pointer to the first argument

  static_assert(eax == kRuntimeCallArgCountRegister);
  static_assert(ecx == kRuntimeCallArgvRegister);
  static_assert(edx == kRuntimeCallFunctionRegister);
  static_assert(esi == kContextRegister);
  static_assert(edi == kJSFunctionRegister);

  DCHECK(!AreAliased(kRuntimeCallArgCountRegister, kRuntimeCallArgvRegister,
                     kRuntimeCallFunctionRegister, kContextRegister,
                     kJSFunctionRegister, kRootRegister));

  const int kSwitchToTheCentralStackSlots = switch_to_central_stack ? 1 : 0;
  const int kReservedStackSlots = 3 + kSwitchToTheCentralStackSlots;

#if V8_ENABLE_WEBASSEMBLY
  const int kEdiSlot = kReservedStackSlots - 1;
#endif  // V8_ENABLE_WEBASSEMBLY

  __ EnterExitFrame(
      kReservedStackSlots,
      builtin_exit_frame ? StackFrame::BUILTIN_EXIT : StackFrame::EXIT, edi);

  // Set up argv in a callee-saved register. It is reused below so it must be
  // retained across the C call.
  static constexpr Register kArgvRegister = edi;
  if (argv_mode == ArgvMode::kRegister) {
    __ mov(kArgvRegister, ecx);
  } else {
    int offset =
        StandardFrameConstants::kFixedFrameSizeAboveFp - kReceiverOnStackSize;
    __ lea(kArgvRegister, Operand(ebp, eax, times_system_pointer_size, offset));
  }

  // edx: pointer to C function
  // ebp: frame pointer  (restored after C call)
  // esp: stack pointer  (restored after C call)
  // eax: number of arguments including receiver
  // edi: pointer to the first argument (C callee-saved)

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchToTheCentralStackIfNeeded(masm, kEdiSlot);
  }
#endif  // V8_ENABLE_WEBASSEMBLY
  // Result returned in eax, or eax+edx if result size is 2.

  // Check stack alignment.
  if (v8_flags.debug_code) {
    __ CheckStackAlignment();
  }
  // Call C function.
  __ mov(Operand(esp, 0 * kSystemPointerSize), eax);            // argc.
  __ mov(Operand(esp, 1 * kSystemPointerSize), kArgvRegister);  // argv.
  __ Move(ecx, Immediate(ER::isolate_address()));
  __ mov(Operand(esp, 2 * kSystemPointerSize), ecx);
  __ call(kRuntimeCallFunctionRegister);

  // Result is in eax or edx:eax - do not destroy these registers!

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchFromTheCentralStackIfNeeded(masm);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Check result for exception sentinel.
  Label exception_returned;
  __ CompareRoot(eax, RootIndex::kException);
  __ j(equal, &exception_returned);

  // Check that there is no exception, otherwise we
  // should have returned the exception sentinel.
  if (v8_flags.debug_code) {
    __ push(edx);
    __ LoadRoot(edx, RootIndex::kTheHoleValue);
    Label okay;
    ER exception_address =
        ER::Create(IsolateAddressId::kExceptionAddress, masm->isolate());
    __ cmp(edx, __ ExternalReferenceAsOperand(exception_address, ecx));
    // Cannot use check here as it attempts to generate call into runtime.
    __ j(equal, &okay, Label::kNear);
    __ int3();
    __ bind(&okay);
    __ pop(edx);
  }

  __ LeaveExitFrame(esi);
  if (argv_mode == ArgvMode::kStack) {
    // Drop arguments and the receiver from the caller stack.
    DCHECK(!AreAliased(esi, kArgvRegister));
    __ PopReturnAddressTo(ecx);
    __ lea(esp, Operand(kArgvRegister, kReceiverOnStackSize));
    __ PushReturnAddressFrom(ecx);
  }
  __ ret(0);

  // Handling of exception.
  __ bind(&exception_returned);

  ER pending_handler_context_address = ER::Create(
      IsolateAddressId::kPendingHandlerContextAddress, masm->isolate());
  ER pending_handler_entrypoint_address = ER::Create(
      IsolateAddressId::kPendingHandlerEntrypointAddress, masm->isolate());
  ER pending_handler_fp_address =
      ER::Create(IsolateAddressId::kPendingHandlerFPAddress, masm->isolate());
  ER pending_handler_sp_address =
      ER::Create(IsolateAddressId::kPendingHandlerSPAddress, masm->isolate());

  // Ask the runtime for help to determine the handler. This will set eax to
  // contain the current exception, don't clobber it.
  ER find_handler = ER::Create(Runtime::kUnwindAndFindExceptionHandler);
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ PrepareCallCFunction(3, eax);
    __ mov(Operand(esp, 0 * kSystemPointerSize), Immediate(0));  // argc.
    __ mov(Operand(esp, 1 * kSystemPointerSize), Immediate(0));  // argv.
    __ Move(esi, Immediate(ER::isolate_address()));
    __ mov(Operand(esp, 2 * kSystemPointerSize), esi);
    __ CallCFunction(find_handler, 3, SetIsolateDataSlots::kNo);
  }

  // Retrieve the handler context, SP and FP.
  __ mov(esp, __ ExternalReferenceAsOperand(pending_handler_sp_address, esi));
  __ mov(ebp, __ ExternalReferenceAsOperand(pending_handler_fp_address, esi));
  __ mov(esi,
         __ ExternalReferenceAsOperand(pending_handler_context_address, esi));

  // If the handler is a JS frame, restore the context to the frame. Note that
  // the context will be set to (esi == 0) for non-JS frames.
  Label skip;
  __ test(esi, esi);
  __ j(zero, &skip, Label::kNear);
  __ mov(Operand(ebp, StandardFrameConstants::kContextOffset), esi);
  __ bind(&skip);

  // Clear c_entry_fp, like we do in `LeaveExitFrame`.
  ER c_entry_fp_address =
      ER::Create(IsolateAddressId::kCEntryFPAddress, masm->isolate());
  __ mov(__ ExternalReferenceAsOperand(c_entry_fp_address, esi), Immediate(0));

  // Compute the handler entry address and jump to it.
  __ mov(edi, __ ExternalReferenceAsOperand(pending_handler_entrypoint_address,
                                            edi));
  __ jmp(edi);
}

void Builtins::Generate_DoubleToI(MacroAssembler* masm) {
  Label check_negative, process_64_bits, done;

  // Account for return address and saved regs.
  const int kArgumentOffset = 4 * kSystemPointerSize;

  MemOperand mantissa_operand(MemOperand(esp, kArgumentOffset));
  MemOperand exponent_operand(
      MemOperand(esp, kArgumentOffset + kDoubleSize / 2));

  // The result is returned on the stack.
  MemOperand return_operand = mantissa_operand;

  Register scratch1 = ebx;

  // Since we must use ecx for shifts below, use some other register (eax)
  // to calculate the result.
  Register result_reg = eax;
  // Save ecx if it isn't the return register and therefore volatile, or if it
  // is the return register, then save the temp register we use in its stead for
  // the result.
  Register save_reg = eax;
  __ push(ecx);
  __ push(scratch1);
  __ push(save_reg);

  __ mov(scratch1, mantissa_operand);
  if (CpuFeatures::IsSupported(SSE3)) {
    CpuFeatureScope scope(masm, SSE3);
    // Load x87 register with heap number.
    __ fld_d(mantissa_operand);
  }
  __ mov(ecx, exponent_operand);

  __ and_(ecx, HeapNumber::kExponentMask);
  __ shr(ecx, HeapNumber::kExponentShift);
  __ lea(result_reg, MemOperand(ecx, -HeapNumber::kExponentBias));
  __ cmp(result_reg, Immediate(HeapNumber::kMantissaBits));
  __ j(below, &process_64_bits);

  // Result is entirely in lower 32-bits of mantissa
  int delta =
      HeapNumber::kExponentBias + base::Double::kPhysicalSignificandSize;
  if (CpuFeatures::IsSupported(SSE3)) {
    __ fstp(0);
  }
  __ sub(ecx, Immediate(delta));
  __ xor_(result_reg, result_reg);
  __ cmp(ecx, Immediate(31));
  __ j(above, &done);
  __ shl_cl(scratch1);
  __ jmp(&check_negative);

  __ bind(&process_64_bits);
  if (CpuFeatures::IsSupported(SSE3)) {
    CpuFeatureScope scope(masm, SSE3);
    // Reserve space for 64 bit answer.
    __ AllocateStackSpace(kDoubleSize);  // Nolint.
    // Do conversion, which cannot fail because we checked the exponent.
    __ fisttp_d(Operand(esp, 0));
    __ mov(result_reg, Operand(esp, 0));  // Load low word of answer as result
    __ add(esp, Immediate(kDoubleSize));
    __ jmp(&done);
  } else {
    // Result must be extracted from shifted 32-bit mantissa
    __ sub(ecx, Immediate(delta));
    __ neg(ecx);
    __ mov(result_reg, exponent_operand);
    __ and_(
        result_reg,
        Immediate(static_cast<uint32_t>(base::Double::kSignificandMask >> 32)));
    __ add(result_reg,
           Immediate(static_cast<uint32_t>(base::Double::kHiddenBit >> 32)));
    __ shrd_cl(scratch1, result_reg);
    __ shr_cl(result_reg);
    __ test(ecx, Immediate(32));
    __ cmov(not_equal, scratch1, result_reg);
  }

  // If the double was negative, negate the integer result.
  __ bind(&check_negative);
  __ mov(result_reg, scratch1);
  __ neg(result_reg);
  __ cmp(exponent_operand, Immediate(0));
  __ cmov(greater, result_reg, scratch1);

  // Restore registers
  __ bind(&done);
  __ mov(return_operand, result_reg);
  __ pop(save_reg);
  __ pop(scratch1);
  __ pop(ecx);
  __ ret(0);
}

void Builtins::Generate_CallApiCallbackImpl(MacroAssembler* masm,
                                            CallApiCallbackMode mode) {
  // ----------- S t a t e -------------
  // CallApiCallbackMode::kOptimizedNoProfiling/kOptimized modes:
  //  -- eax                 : api function address
  // Both modes:
  //  -- ecx                 : arguments count (not including the receiver)
  //  -- edx                 : FunctionTemplateInfo
  //  -- edi                 : holder
  //  -- esi                 : context
  //  -- esp[0]              : return address
  //  -- esp[8]              : argument 0 (receiver)
  //  -- esp[16]             : argument 1
  //  -- ...
  //  -- esp[argc * 8]       : argument (argc - 1)
  //  -- esp[(argc + 1) * 8] : argument argc
  // -----------------------------------

  Register api_function_address = no_reg;
  Register argc = no_reg;
  Register func_templ = no_reg;
  Register holder = no_reg;
  Register topmost_script_having_context = no_reg;

  switch (mode) {
    case CallApiCallbackMode::kGeneric:
      argc = CallApiCallbackGenericDescriptor::ActualArgumentsCountRegister();
      topmost_script_having_context = CallApiCallbackGenericDescriptor::
          TopmostScriptHavingContextRegister();
      func_templ =
          CallApiCallbackGenericDescriptor::FunctionTemplateInfoRegister();
      holder = CallApiCallbackGenericDescriptor::HolderRegister();
      break;

    case CallApiCallbackMode::kOptimizedNoProfiling:
    case CallApiCallbackMode::kOptimized:
      // Caller context is always equal to current context because we don't
      // inline Api calls cross-context.
      topmost_script_having_context = kContextRegister;
      api_function_address =
          CallApiCallbackOptimizedDescriptor::ApiFunctionAddressRegister();
      argc = CallApiCallbackOptimizedDescriptor::ActualArgumentsCountRegister();
      func_templ =
          CallApiCallbackOptimizedDescriptor::FunctionTemplateInfoRegister();
      holder = CallApiCallbackOptimizedDescriptor::HolderRegister();
      break;
  }
  DCHECK(!AreAliased(api_function_address, topmost_script_having_context, argc,
                     func_templ, holder));

  using FCA = FunctionCallbackArguments;
  using ER = ExternalReference;
  using FC = ApiCallbackExitFrameConstants;

  static_assert(FCA::kArgsLength == 6);
  static_assert(FCA::kNewTargetIndex == 5);
  static_assert(FCA::kTargetIndex == 4);
  static_assert(FCA::kReturnValueIndex == 3);
  static_assert(FCA::kContextIndex == 2);
  static_assert(FCA::kIsolateIndex == 1);
  static_assert(FCA::kHolderIndex == 0);

  // Set up FunctionCallbackInfo's implicit_args on the stack as follows:
  //
  // Current state:
  //   esp[0]: return address
  //
  // Target state:
  //   esp[0 * kSystemPointerSize]: return address
  //   esp[1 * kSystemPointerSize]: kHolder   <= implicit_args_
  //   esp[2 * kSystemPointerSize]: kIsolate
  //   esp[3 * kSystemPointerSize]: kContext
  //   esp[4 * kSystemPointerSize]: undefined (kReturnValue)
  //   esp[5 * kSystemPointerSize]: kTarget
  //   esp[6 * kSystemPointerSize]: undefined (kNewTarget)
  // Existing state:
  //   esp[7 * kSystemPointerSize]:          <= FCA:::values_

  __ StoreRootRelative(IsolateData::topmost_script_having_context_offset(),
                       topmost_script_having_context);

  if (mode == CallApiCallbackMode::kGeneric) {
    api_function_address = ReassignRegister(topmost_script_having_context);
  }

  // Park argc in xmm0.
  __ movd(xmm0, argc);

  __ PopReturnAddressTo(argc);
  __ PushRoot(RootIndex::kUndefinedValue);  // kNewTarget
  __ Push(func_templ);                      // kTarget
  __ PushRoot(RootIndex::kUndefinedValue);  // kReturnValue
  __ Push(kContextRegister);                // kContext

  // TODO(ishell): Consider using LoadAddress+push approach here.
  __ Push(Immediate(ER::isolate_address()));
  __ Push(holder);

  Register scratch = ReassignRegister(holder);

  // The API function takes v8::FunctionCallbackInfo reference, allocate it
  // in non-GCed space of the exit frame.
  static constexpr int kApiArgc = 1;
  static constexpr int kApiArg0Offset = 0 * kSystemPointerSize;

  if (mode == CallApiCallbackMode::kGeneric) {
    __ mov(api_function_address,
           FieldOperand(func_templ,
                        FunctionTemplateInfo::kMaybeRedirectedCallbackOffset));
  }

  __ PushReturnAddressFrom(argc);

  // The ApiCallbackExitFrame must be big enough to store the outgoing
  // parameters for C function on the stack.
  constexpr int extra_slots =
      FC::getExtraSlotsCountFrom<ExitFrameConstants>() + kApiArgc;
  __ EnterExitFrame(extra_slots, StackFrame::API_CALLBACK_EXIT,
                    api_function_address);

  if (v8_flags.debug_code) {
    __ mov(esi, Immediate(base::bit_cast<int32_t>(kZapValue)));
  }

  // Reload argc from xmm0.
  __ movd(argc, xmm0);

  Operand argc_operand = Operand(ebp, FC::kFCIArgcOffset);
  {
    ASM_CODE_COMMENT_STRING(masm, "Initialize v8::FunctionCallbackInfo");
    // FunctionCallbackInfo::length_.
    // TODO(ishell): pass JSParameterCount(argc) to simplify things on the
    // caller end.
    __ mov(argc_operand, argc);

    // FunctionCallbackInfo::implicit_args_.
    __ lea(scratch, Operand(ebp, FC::kImplicitArgsArrayOffset));
    __ mov(Operand(ebp, FC::kFCIImplicitArgsOffset), scratch);

    // FunctionCallbackInfo::values_ (points at JS arguments on the stack).
    __ lea(scratch, Operand(ebp, FC::kFirstArgumentOffset));
    __ mov(Operand(ebp, FC::kFCIValuesOffset), scratch);
  }

  __ RecordComment("v8::FunctionCallback's argument.");
  __ lea(scratch, Operand(ebp, FC::kFunctionCallbackInfoOffset));
  __ mov(ExitFrameStackSlotOperand(kApiArg0Offset), scratch);

  ExternalReference thunk_ref = ER::invoke_function_callback(mode);
  Register no_thunk_arg = no_reg;

  Operand return_value_operand = Operand(ebp, FC::kReturnValueOffset);
  static constexpr int kSlotsToDropOnReturn =
      FC::kFunctionCallbackInfoArgsLength + kJSArgcReceiverSlots;

  const bool with_profiling =
      mode != CallApiCallbackMode::kOptimizedNoProfiling;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, no_thunk_arg, kSlotsToDropOnReturn,
                           &argc_operand, return_value_operand);
}

void Builtins::Generate_CallApiGetter(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- esi                 : context
  //  -- edx                 : receiver
  //  -- ecx                 : holder
  //  -- eax                 : accessor info
  //  -- esp[0]              : return address
  // -----------------------------------

  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
  Register scratch = edi;
  DCHECK(!AreAliased(receiver, holder, callback, scratch));

  // Build v8::PropertyCallbackInfo::args_ array on the stack and push property
  // name below the exit frame to make GC aware of them.
  using PCA = PropertyCallbackArguments;
  using ER = ExternalReference;
  using FC = ApiAccessorExitFrameConstants;

  static_assert(PCA::kPropertyKeyIndex == 0);
  static_assert(PCA::kShouldThrowOnErrorIndex == 1);
  static_assert(PCA::kHolderIndex == 2);
  static_assert(PCA::kIsolateIndex == 3);
  static_assert(PCA::kHolderV2Index == 4);
  static_assert(PCA::kReturnValueIndex == 5);
  static_assert(PCA::kDataIndex == 6);
  static_assert(PCA::kThisIndex == 7);
  static_assert(PCA::kArgsLength == 8);

  // Set up v8::PropertyCallbackInfo's (PCI) args_ on the stack as follows:
  // Current state:
  //   esp[0]: return address
  //
  // Target state:
  //   esp[0 * kSystemPointerSize]: return address
  //   esp[1 * kSystemPointerSize]: name                      <= PCI::args_
  //   esp[2 * kSystemPointerSize]: kShouldThrowOnErrorIndex
  //   esp[3 * kSystemPointerSize]: kHolderIndex
  //   esp[4 * kSystemPointerSize]: kIsolateIndex
  //   esp[5 * kSystemPointerSize]: kHolderV2Index
  //   esp[6 * kSystemPointerSize]: kReturnValueIndex
  //   esp[7 * kSystemPointerSize]: kDataIndex
  //   esp[8 * kSystemPointerSize]: kThisIndex / receiver

  __ PopReturnAddressTo(scratch);
  __ push(receiver);
  __ push(FieldOperand(callback, AccessorInfo::kDataOffset));
  __ PushRoot(RootIndex::kUndefinedValue);  // kReturnValue
  __ Push(Smi::zero());                     // kHolderV2
  Register isolate_reg = ReassignRegister(receiver);
  __ LoadAddress(isolate_reg, ER::isolate_address());
  __ push(isolate_reg);
  __ push(holder);
  __ Push(Smi::FromInt(kDontThrow));  // should_throw_on_error -> kDontThrow

  Register name = ReassignRegister(holder);
  __ mov(name, FieldOperand(callback, AccessorInfo::kNameOffset));
  __ push(name);
  __ PushReturnAddressFrom(scratch);

  // The API function takes a name local handle and v8::PropertyCallbackInfo
  // reference, allocate them in non-GCed space of the exit frame.
  static constexpr int kApiArgc = 2;
  static constexpr int kApiArg0Offset = 0 * kSystemPointerSize;
  static constexpr int kApiArg1Offset = 1 * kSystemPointerSize;

  Register api_function_address = ReassignRegister(isolate_reg);
  __ RecordComment("Load function_address");
  __ mov(api_function_address,
         FieldOperand(callback, AccessorInfo::kMaybeRedirectedGetterOffset));

  __ EnterExitFrame(FC::getExtraSlotsCountFrom<ExitFrameConstants>() + kApiArgc,
                    StackFrame::API_ACCESSOR_EXIT, api_function_address);
  if (v8_flags.debug_code) {
    __ mov(esi, Immediate(base::bit_cast<int32_t>(kZapValue)));
  }

  __ RecordComment("Create v8::PropertyCallbackInfo object on the stack.");
  // property_callback_info_arg = v8::PropertyCallbackInfo&
  Register property_callback_info_arg = ReassignRegister(scratch);
  __ lea(property_callback_info_arg, Operand(ebp, FC::kArgsArrayOffset));

  DCHECK(!AreAliased(api_function_address, property_callback_info_arg, name,
                     callback));

  __ RecordComment("Local<Name>");
#ifdef V8_ENABLE_DIRECT_HANDLE
  // name_arg = Local<Name>(name), name value was pushed to GC-ed stack space.
  __ mov(ExitFrameStackSlotOperand(kApiArg0Offset), name);
#else
  // name_arg = Local<Name>(&name), which is &args_array[kPropertyKeyIndex].
  static_assert(PCA::kPropertyKeyIndex == 0);
  __ mov(ExitFrameStackSlotOperand(kApiArg0Offset), property_callback_info_arg);
#endif

  __ RecordComment("v8::PropertyCallbackInfo<T>&");
  __ mov(ExitFrameStackSlotOperand(kApiArg1Offset), property_callback_info_arg);

  ExternalReference thunk_ref = ER::invoke_accessor_getter_callback();
  // Pass AccessorInfo to thunk wrapper in case profiler or side-effect
  // checking is enabled.
  Register thunk_arg = callback;

  Operand return_value_operand = Operand(ebp, FC::kReturnValueOffset);
  static constexpr int kSlotsToDropOnReturn =
      FC::kPropertyCallbackInfoArgsLength;
  Operand* const kUseStackSpaceConstant = nullptr;

  const bool with_profiling = true;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, thunk_arg, kSlotsToDropOnReturn,
                           kUseStackSpaceConstant, return_value_operand);
}

void Builtins::Generate_DirectCEntry(MacroAssembler* masm) {
  __ int3();  // Unused on this architecture.
}

namespace {

enum Direction { FORWARD, BACKWARD };
enum Alignment { MOVE_ALIGNED, MOVE_UNALIGNED };

// Expects registers:
// esi - source, aligned if alignment == ALIGNED
// edi - destination, always aligned
// ecx - count (copy size in bytes)
// edx - loop count (number of 64 byte chunks)
void MemMoveEmitMainLoop(MacroAssembler* masm, Label* move_last_15,
                         Direction direction, Alignment alignment) {
  ASM_CODE_COMMENT(masm);
  Register src = esi;
  Register dst = edi;
  Register count = ecx;
  Register loop_count = edx;
  Label loop, move_last_31, move_last_63;
  __ cmp(loop_count, 0);
  __ j(equal, &move_last_63);
  __ bind(&loop);
  // Main loop. Copy in 64 byte chunks.
  if (direction == BACKWARD) __ sub(src, Immediate(0x40));
  __ movdq(alignment == MOVE_ALIGNED, xmm0, Operand(src, 0x00));
  __ movdq(alignment == MOVE_ALIGNED, xmm1, Operand(src, 0x10));
  __ movdq(alignment == MOVE_ALIGNED, xmm2, Operand(src, 0x20));
  __ movdq(alignment == MOVE_ALIGNED, xmm3, Operand(src, 0x30));
  if (direction == FORWARD) __ add(src, Immediate(0x40));
  if (direction == BACKWARD) __ sub(dst, Immediate(0x40));
  __ movdqa(Operand(dst, 0x00), xmm0);
  __ movdqa(Operand(dst, 0x10), xmm1);
  __ movdqa(Operand(dst, 0x20), xmm2);
  __ movdqa(Operand(dst, 0x30), xmm3);
  if (direction == FORWARD) __ add(dst, Immediate(0x40));
  __ dec(loop_count);
  __ j(not_zero, &loop);
  // At most 63 bytes left to copy.
  __ bind(&move_last_63);
  __ test(count, Immediate(0x20));
  __ j(zero, &move_last_31);
  if (direction == BACKWARD) __ sub(src, Immediate(0x20));
  __ movdq(alignment == MOVE_ALIGNED, xmm0, Operand(src, 0x00));
  __ movdq(alignment == MOVE_ALIGNED, xmm1, Operand(src, 0x10));
  if (direction == FORWARD) __ add(src, Immediate(0x20));
  if (direction == BACKWARD) __ sub(dst, Immediate(0x20));
  __ movdqa(Operand(dst, 0x00), xmm0);
  __ movdqa(Operand(dst, 0x10), xmm1);
  if (direction == FORWARD) __ add(dst, Immediate(0x20));
  // At most 31 bytes left to copy.
  __ bind(&move_last_31);
  __ test(count, Immediate(0x10));
  __ j(zero, move_last_15);
  if (direction == BACKWARD) __ sub(src, Immediate(0x10));
  __ movdq(alignment == MOVE_ALIGNED, xmm0, Operand(src, 0));
  if (direction == FORWARD) __ add(src, Immediate(0x10));
  if (direction == BACKWARD) __ sub(dst, Immediate(0x10));
  __ movdqa(Operand(dst, 0), xmm0);
  if (direction == FORWARD) __ add(dst, Immediate(0x10));
}

void MemMoveEmitPopAndReturn(MacroAssembler* masm) {
  __ pop(esi);
  __ pop(edi);
  __ ret(0);
}

}  // namespace

void Builtins::Generate_MemMove(MacroAssembler* masm) {
  // Generated code is put into a fixed, unmovable buffer, and not into
  // the V8 heap. We can't, and don't, refer to any relocatable addresses
  // (e.g. the JavaScript nan-object).

  // 32-bit C declaration function calls pass arguments on stack.

  // Stack layout:
  // esp[12]: Third argument, size.
  // esp[8]: Second argument, source pointer.
  // esp[4]: First argument, destination pointer.
  // esp[0]: return address

  const int kDestinationOffset = 1 * kSystemPointerSize;
  const int kSourceOffset = 2 * kSystemPointerSize;
  const int kSizeOffset = 3 * kSystemPointerSize;

  // When copying up to this many bytes, use special "small" handlers.
  const size_t kSmallCopySize = 8;
  // When copying up to this many bytes, use special "medium" handlers.
  const size_t kMediumCopySize = 63;
  // When non-overlapping region of src and dst is less than this,
  // use a more careful implementation (slightly slower).
  const size_t kMinMoveDistance = 16;
  // Note that these values are dictated by the implementation below,
  // do not just change them and hope things will work!

  int stack_offset = 0;  // Update if we change the stack height.

  Label backward, backward_much_overlap;
  Label forward_much_overlap, small_size, medium_size, pop_and_return;
  __ push(edi);
  __ push(esi);
  stack_offset += 2 * kSystemPointerSize;
  Register dst = edi;
  Register src = esi;
  Register count = ecx;
  Register loop_count = edx;
  __ mov(dst, Operand(esp, stack_offset + kDestinationOffset));
  __ mov(src, Operand(esp, stack_offset + kSourceOffset));
  __ mov(count, Operand(esp, stack_offset + kSizeOffset));

  __ cmp(dst, src);
  __ j(equal, &pop_and_return);

  __ prefetch(Operand(src, 0), 1);
  __ cmp(count, kSmallCopySize);
  __ j(below_equal, &small_size);
  __ cmp(count, kMediumCopySize);
  __ j(below_equal, &medium_size);
  __ cmp(dst, src);
  __ j(above, &backward);

  {
    // |dst| is a lower address than |src|. Copy front-to-back.
    Label unaligned_source, move_last_15, skip_last_move;
    __ mov(eax, src);
    __ sub(eax, dst);
    __ cmp(eax, kMinMoveDistance);
    __ j(below, &forward_much_overlap);
    // Copy first 16 bytes.
    __ movdqu(xmm0, Operand(src, 0));
    __ movdqu(Operand(dst, 0), xmm0);
    // Determine distance to alignment: 16 - (dst & 0xF).
    __ mov(edx, dst);
    __ and_(edx, 0xF);
    __ neg(edx);
    __ add(edx, Immediate(16));
    __ add(dst, edx);
    __ add(src, edx);
    __ sub(count, edx);
    // dst is now aligned. Main copy loop.
    __ mov(loop_count, count);
    __ shr(loop_count, 6);
    // Check if src is also aligned.
    __ test(src, Immediate(0xF));
    __ j(not_zero, &unaligned_source);
    // Copy loop for aligned source and destination.
    MemMoveEmitMainLoop(masm, &move_last_15, FORWARD, MOVE_ALIGNED);
    // At most 15 bytes to copy. Copy 16 bytes at end of string.
    __ bind(&move_last_15);
    __ and_(count, 0xF);
    __ j(zero, &skip_last_move, Label::kNear);
    __ movdqu(xmm0, Operand(src, count, times_1, -0x10));
    __ movdqu(Operand(dst, count, times_1, -0x10), xmm0);
    __ bind(&skip_last_move);
    MemMoveEmitPopAndReturn(masm);

    // Copy loop for unaligned source and aligned destination.
    __ bind(&unaligned_source);
    MemMoveEmitMainLoop(masm, &move_last_15, FORWARD, MOVE_UNALIGNED);
    __ jmp(&move_last_15);

    // Less than kMinMoveDistance offset between dst and src.
    Label loop_until_aligned, last_15_much_overlap;
    __ bind(&loop_until_aligned);
    __ mov_b(eax, Operand(src, 0));
    __ inc(src);
    __ mov_b(Operand(dst, 0), eax);
    __ inc(dst);
    __ dec(count);
    __ bind(&forward_much_overlap);  // Entry point into this block.
    __ test(dst, Immediate(0xF));
    __ j(not_zero, &loop_until_aligned);
    // dst is now aligned, src can't be. Main copy loop.
    __ mov(loop_count, count);
    __ shr(loop_count, 6);
    MemMoveEmitMainLoop(masm, &last_15_much_overlap, FORWARD, MOVE_UNALIGNED);
    __ bind(&last_15_much_overlap);
    __ and_(count, 0xF);
    __ j(zero, &pop_and_return);
    __ cmp(count, kSmallCopySize);
    __ j(below_equal, &small_size);
    __ jmp(&medium_size);
  }

  {
    // |dst| is a higher address than |src|. Copy backwards.
    Label unaligned_source, move_first_15, skip_last_move;
    __ bind(&backward);
    // |dst| and |src| always point to the end of what's left to copy.
    __ add(dst, count);
    __ add(src, count);
    __ mov(eax, dst);
    __ sub(eax, src);
    __ cmp(eax, kMinMoveDistance);
    __ j(below, &backward_much_overlap);
    // Copy last 16 bytes.
    __ movdqu(xmm0, Operand(src, -0x10));
    __ movdqu(Operand(dst, -0x10), xmm0);
    // Find distance to alignment: dst & 0xF
    __ mov(edx, dst);
    __ and_(edx, 0xF);
    __ sub(dst, edx);
    __ sub(src, edx);
    __ sub(count, edx);
    // dst is now aligned. Main copy loop.
    __ mov(loop_count, count);
    __ shr(loop_count, 6);
    // Check if src is also aligned.
    __ test(src, Immediate(0xF));
    __ j(not_zero, &unaligned_source);
    // Copy loop for aligned source and destination.
    MemMoveEmitMainLoop(masm, &move_first_15, BACKWARD, MOVE_ALIGNED);
    // At most 15 bytes to copy. Copy 16 bytes at beginning of string.
    __ bind(&move_first_15);
    __ and_(count, 0xF);
    __ j(zero, &skip_last_move, Label::kNear);
    __ sub(src, count);
    __ sub(dst, count);
    __ movdqu(xmm0, Operand(src, 0));
    __ movdqu(Operand(dst, 0), xmm0);
    __ bind(&skip_last_move);
    MemMoveEmitPopAndReturn(masm);

    // Copy loop for unaligned source and aligned destination.
    __ bind(&unaligned_source);
    MemMoveEmitMainLoop(masm, &move_first_15, BACKWARD, MOVE_UNALIGNED);
    __ jmp(&move_first_15);

    // Less than kMinMoveDistance offset between dst and src.
    Label loop_until_aligned, first_15_much_overlap;
    __ bind(&loop_until_aligned);
    __ dec(src);
    __ dec(dst);
    __ mov_b(eax, Operand(src, 0));
    __ mov_b(Operand(dst, 0), eax);
    __ dec(count);
    __ bind(&backward_much_overlap);  // Entry point into this block.
    __ test(dst, Immediate(0xF));
    __ j(not_zero, &loop_until_aligned);
    // dst is now aligned, src can't be. Main copy loop.
    __ mov(loop_count, count);
    __ shr(loop_count, 6);
    MemMoveEmitMainLoop(masm, &first_15_much_overlap, BACKWARD, MOVE_UNALIGNED);
    __ bind(&first_15_much_overlap);
    __ and_(count, 0xF);
    __ j(zero, &pop_and_return);
    // Small/medium handlers expect dst/src to point to the beginning.
    __ sub(dst, count);
    __ sub(src, count);
    __ cmp(count, kSmallCopySize);
    __ j(below_equal, &small_size);
    __ jmp(&medium_size);
  }
  {
    // Special handlers for 9 <= copy_size < 64. No assumptions about
    // alignment or move distance, so all reads must be unaligned and
    // must happen before any writes.
    Label f9_16, f17_32, f33_48, f49_63;

    __ bind(&f9_16);
    __ movsd(xmm0, Operand(src, 0));
    __ movsd(xmm1, Operand(src, count, times_1, -8));
    __ movsd(Operand(dst, 0), xmm0);
    __ movsd(Operand(dst, count, times_1, -8), xmm1);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f17_32);
    __ movdqu(xmm0, Operand(src, 0));
    __ movdqu(xmm1, Operand(src, count, times_1, -0x10));
    __ movdqu(Operand(dst, 0x00), xmm0);
    __ movdqu(Operand(dst, count, times_1, -0x10), xmm1);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f33_48);
    __ movdqu(xmm0, Operand(src, 0x00));
    __ movdqu(xmm1, Operand(src, 0x10));
    __ movdqu(xmm2, Operand(src, count, times_1, -0x10));
    __ movdqu(Operand(dst, 0x00), xmm0);
    __ movdqu(Operand(dst, 0x10), xmm1);
    __ movdqu(Operand(dst, count, times_1, -0x10), xmm2);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f49_63);
    __ movdqu(xmm0, Operand(src, 0x00));
    __ movdqu(xmm1, Operand(src, 0x10));
    __ movdqu(xmm2, Operand(src, 0x20));
    __ movdqu(xmm3, Operand(src, count, times_1, -0x10));
    __ movdqu(Operand(dst, 0x00), xmm0);
    __ movdqu(Operand(dst, 0x10), xmm1);
    __ movdqu(Operand(dst, 0x20), xmm2);
    __ movdqu(Operand(dst, count, times_1, -0x10), xmm3);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&medium_size);  // Entry point into this block.
    __ mov(eax, count);
    __ dec(eax);
    __ shr(eax, 4);
    if (v8_flags.debug_code) {
      Label ok;
      __ cmp(eax, 3);
      __ j(below_equal, &ok);
      __ int3();
      __ bind(&ok);
    }

    // Dispatch to handlers.
    Label eax_is_2_or_3;

    __ cmp(eax, 1);
    __ j(greater, &eax_is_2_or_3);
    __ j(less, &f9_16);  // eax == 0.
    __ jmp(&f17_32);     // eax == 1.

    __ bind(&eax_is_2_or_3);
    __ cmp(eax, 3);
    __ j(less, &f33_48);  // eax == 2.
    __ jmp(&f49_63);      // eax == 3.
  }
  {
    // Specialized copiers for copy_size <= 8 bytes.
    Label f0, f1, f2, f3, f4, f5_8;
    __ bind(&f0);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f1);
    __ mov_b(eax, Operand(src, 0));
    __ mov_b(Operand(dst, 0), eax);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f2);
    __ mov_w(eax, Operand(src, 0));
    __ mov_w(Operand(dst, 0), eax);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f3);
    __ mov_w(eax, Operand(src, 0));
    __ mov_b(edx, Operand(src, 2));
    __ mov_w(Operand(dst, 0), eax);
    __ mov_b(Operand(dst, 2), edx);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f4);
    __ mov(eax, Operand(src, 0));
    __ mov(Operand(dst, 0), eax);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&f5_8);
    __ mov(eax, Operand(src, 0));
    __ mov(edx, Operand(src, count, times_1, -4));
    __ mov(Operand(dst, 0), eax);
    __ mov(Operand(dst, count, times_1, -4), edx);
    MemMoveEmitPopAndReturn(masm);

    __ bind(&small_size);  // Entry point into this block.
    if (v8_flags.debug_code) {
      Label ok;
      __ cmp(count, 8);
      __ j(below_equal, &ok);
      __ int3();
      __ bind(&ok);
    }

    // Dispatch to handlers.
    Label count_is_above_3, count_is_2_or_3;

    __ cmp(count, 3);
    __ j(greater, &count_is_above_3);

    __ cmp(count, 1);
    __ j(greater, &count_is_2_or_3);
    __ j(less, &f0);  // count == 0.
    __ jmp(&f1);      // count == 1.

    __ bind(&count_is_2_or_3);
    __ cmp(count, 3);
    __ j(less, &f2);  // count == 2.
    __ jmp(&f3);      // count == 3.

    __ bind(&count_is_above_3);
    __ cmp(count, 5);
    __ j(less, &f4);  // count == 4.
    __ jmp(&f5_8);    // count in [5, 8[.
  }

  __ bind(&pop_and_return);
  MemMoveEmitPopAndReturn(masm);
}

namespace {

void Generate_DeoptimizationEntry(MacroAssembler* masm,
                                  DeoptimizeKind deopt_kind) {
  Isolate* isolate = masm->isolate();

  // Save all general purpose registers before messing with them.
  const int kNumberOfRegisters = Register::kNumRegisters;

  const int kXmmRegsSize = kSimd128Size * XMMRegister::kNumRegisters;
  __ AllocateStackSpace(kXmmRegsSize);
  const RegisterConfiguration* config = RegisterConfiguration::Default();
  DCHECK_GE(XMMRegister::kNumRegisters,
            config->num_allocatable_simd128_registers());
  DCHECK_EQ(config->num_allocatable_simd128_registers(),
            config->num_allocatable_double_registers());
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int offset = code * kSimd128Size;
    __ movdqu(Operand(esp, offset), xmm_reg);
  }

  __ pushad();

  ExternalReference c_entry_fp_address =
      ExternalReference::Create(IsolateAddressId::kCEntryFPAddress, isolate);
  __ mov(masm->ExternalReferenceAsOperand(c_entry_fp_address, esi), ebp);

  const int kSavedRegistersAreaSize =
      kNumberOfRegisters * kSystemPointerSize + kXmmRegsSize;

  // Get the address of the location in the code object
  // and compute the fp-to-sp delta in register edx.
  __ mov(ecx, Operand(esp, kSavedRegistersAreaSize));
  __ lea(edx, Operand(esp, kSavedRegistersAreaSize + 1 * kSystemPointerSize));

  __ sub(edx, ebp);
  __ neg(edx);

  // Allocate a new deoptimizer object.
  __ PrepareCallCFunction(5, eax);
  __ mov(eax, Immediate(0));
  Label context_check;
  __ mov(edi, Operand(ebp, CommonFrameConstants::kContextOrFrameTypeOffset));
  __ JumpIfSmi(edi, &context_check);
  __ mov(eax, Operand(ebp, StandardFrameConstants::kFunctionOffset));
  __ bind(&context_check);
  __ mov(Operand(esp, 0 * kSystemPointerSize), eax);  // Function.
  __ mov(Operand(esp, 1 * kSystemPointerSize),
         Immediate(static_cast<int>(deopt_kind)));
  __ mov(Operand(esp, 2 * kSystemPointerSize),
         ecx);  // InstructionStream address or 0.
  __ mov(Operand(esp, 3 * kSystemPointerSize), edx);  // Fp-to-sp delta.
  __ Move(Operand(esp, 4 * kSystemPointerSize),
          Immediate(ExternalReference::isolate_address()));
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 5);
  }

  // Preserve deoptimizer object in register eax and get the input
  // frame descriptor pointer.
  __ mov(esi, Operand(eax, Deoptimizer::input_offset()));

  // Fill in the input registers.
  for (int i = kNumberOfRegisters - 1; i >= 0; i--) {
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    __ pop(Operand(esi, offset));
  }

  int simd128_regs_offset = FrameDescription::simd128_registers_offset();
  // Fill in the xmm (simd128 / double) input registers.
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    int dst_offset = code * kSimd128Size + simd128_regs_offset;
    int src_offset = code * kSimd128Size;
    __ movdqu(xmm0, Operand(esp, src_offset));
    __ movdqu(Operand(esi, dst_offset), xmm0);
  }

  // Clear FPU all exceptions.
  // TODO(ulan): Find out why the TOP register is not zero here in some cases,
  // and check that the generated code never deoptimizes with unbalanced stack.
  __ fnclex();

  // Mark the stack as not iterable for the CPU profiler which won't be able to
  // walk the stack without the return address.
  __ mov_b(__ ExternalReferenceAsOperand(IsolateFieldId::kStackIsIterable),
           Immediate(0));

  // Remove the return address and the xmm registers.
  __ add(esp, Immediate(kXmmRegsSize + 1 * kSystemPointerSize));

  // Compute a pointer to the unwinding limit in register ecx; that is
  // the first stack slot not part of the input frame.
  __ mov(ecx, Operand(esi, FrameDescription::frame_size_offset()));
  __ add(ecx, esp);

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ lea(edx, Operand(esi, FrameDescription::frame_content_offset()));
  Label pop_loop_header;
  __ jmp(&pop_loop_header);
  Label pop_loop;
  __ bind(&pop_loop);
  __ pop(Operand(edx, 0));
  __ add(edx, Immediate(sizeof(uint32_t)));
  __ bind(&pop_loop_header);
  __ cmp(ecx, esp);
  __ j(not_equal, &pop_loop);

  // Compute the output frame in the deoptimizer.
  __ push(eax);
  __ PrepareCallCFunction(1, esi);
  __ mov(Operand(esp, 0 * kSystemPointerSize), eax);
  {
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 1);
  }
  __ pop(eax);

  __ mov(esp, Operand(eax, Deoptimizer::caller_frame_top_offset()));

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, inner_push_loop, outer_loop_header, inner_loop_header;
  // Outer loop state: eax = current FrameDescription**, edx = one
  // past the last FrameDescription**.
  __ mov(edx, Operand(eax, Deoptimizer::output_count_offset()));
  __ mov(eax, Operand(eax, Deoptimizer::output_offset()));
  __ lea(edx, Operand(eax, edx, times_system_pointer_size, 0));
  __ jmp(&outer_loop_header);
  __ bind(&outer_push_loop);
  // Inner loop state: esi = current FrameDescription*, ecx = loop
  // index.
  __ mov(esi, Operand(eax, 0));
  __ mov(ecx, Operand(esi, FrameDescription::frame_size_offset()));
  __ jmp(&inner_loop_header);
  __ bind(&inner_push_loop);
  __ sub(ecx, Immediate(sizeof(uint32_t)));
  __ push(Operand(esi, ecx, times_1, FrameDescription::frame_content_offset()));
  __ bind(&inner_loop_header);
  __ test(ecx, ecx);
  __ j(not_zero, &inner_push_loop);
  __ add(eax, Immediate(kSystemPointerSize));
  __ bind(&outer_loop_header);
  __ cmp(eax, edx);
  __ j(below, &outer_push_loop);

  // In case of a failed STUB, we have to restore the XMM registers.
  for (int i = 0; i < config->num_allocatable_simd128_registers(); ++i) {
    int code = config->GetAllocatableSimd128Code(i);
    XMMRegister xmm_reg = XMMRegister::from_code(code);
    int src_offset = code * kSimd128Size + simd128_regs_offset;
    __ movdqu(xmm_reg, Operand(esi, src_offset));
  }

  // Push pc and continuation from the last output frame.
  __ push(Operand(esi, FrameDescription::pc_offset()));
  __ mov(eax, Operand(esi, FrameDescription::continuation_offset()));
  // Skip pushing the continuation if it is zero. This is used as a marker for
  // wasm deopts that do not use a builtin call to finish the deopt.
  Label push_registers;
  __ test(eax, eax);
  __ j(zero, &push_registers);
  __ push(eax);
  __ bind(&push_registers);

  // Push the registers from the last output frame.
  for (int i = 0; i < kNumberOfRegisters; i++) {
    int offset =
        (i * kSystemPointerSize) + FrameDescription::registers_offset();
    __ push(Operand(esi, offset));
  }

  __ mov_b(__ ExternalReferenceAsOperand(IsolateFieldId::kStackIsIterable),
           Immediate(1));

  // Restore the registers from the stack.
  __ popad();

  __ InitializeRootRegister();

  // Return to the continuation point.
  __ ret(0);
}

}  // namespace

void Builtins::Generate_DeoptimizationEntry_Eager(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kEager);
}

void Builtins::Generate_DeoptimizationEntry_Lazy(MacroAssembler* masm) {
  Generate_DeoptimizationEntry(masm, DeoptimizeKind::kLazy);
}

namespace {

// Restarts execution either at the current or next (in execution order)
// bytecode. If there is baseline code on the shared function info, converts an
// interpreter frame into a baseline frame and continues execution in baseline
// code. Otherwise execution continues with bytecode.
void Generate_BaselineOrInterpreterEntry(MacroAssembler* masm,
                                         bool next_bytecode,
                                         bool is_osr = false) {
  Label start;
  __ bind(&start);

  // Spill the accumulator register; note that we're not within a frame, so we
  // have to make sure to pop it before doing any GC-visible calls.
  __ push(kInterpreterAccumulatorRegister);

  // Get function from the frame.
  Register closure = eax;
  __ mov(closure, MemOperand(ebp, StandardFrameConstants::kFunctionOffset));

  // Get the InstructionStream object from the shared function info.
  Register code_obj = esi;
  __ mov(code_obj,
         FieldOperand(closure, JSFunction::kSharedFunctionInfoOffset));
  __ mov(
      code_obj,
      FieldOperand(code_obj, SharedFunctionInfo::kTrustedFunctionDataOffset));

  // Check if we have baseline code. For OSR entry it is safe to assume we
  // always have baseline code.
  if (!is_osr) {
    Label start_with_baseline;
    __ CmpObjectType(code_obj, CODE_TYPE, kInterpreterBytecodeOffsetRegister);
    __ j(equal, &start_with_baseline);

    // Start with bytecode as there is no baseline code.
    __ pop(kInterpreterAccumulatorRegister);
    Builtin builtin = next_bytecode ? Builtin::kInterpreterEnterAtNextBytecode
                                    : Builtin::kInterpreterEnterAtBytecode;
    __ TailCallBuiltin(builtin);

    __ bind(&start_with_baseline);
  } else if (v8_flags.debug_code) {
    __ CmpObjectType(code_obj, CODE_TYPE, kInterpreterBytecodeOffsetRegister);
    __ Assert(equal, AbortReason::kExpectedBaselineData);
  }

  if (v8_flags.debug_code) {
    AssertCodeIsBaseline(masm, code_obj, ecx);
  }

  // Load the feedback cell and vector.
  Register feedback_cell = eax;
  Register feedback_vector = ecx;
  __ mov(feedback_cell, FieldOperand(closure, JSFunction::kFeedbackCellOffset));
  closure = no_reg;
  __ mov(feedback_vector,
         FieldOperand(feedback_cell, FeedbackCell::kValueOffset));

  Label install_baseline_code;
  // Check if feedback vector is valid. If not, call prepare for baseline to
  // allocate it.
  __ CmpObjectType(feedback_vector, FEEDBACK_VECTOR_TYPE,
                   kInterpreterBytecodeOffsetRegister);
  __ j(not_equal, &install_baseline_code);

  // Save BytecodeOffset from the stack frame.
  __ mov(kInterpreterBytecodeOffsetRegister,
         MemOperand(ebp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister);
  // Replace bytecode offset with feedback cell.
  static_assert(InterpreterFrameConstants::kBytecodeOffsetFromFp ==
                BaselineFrameConstants::kFeedbackCellFromFp);
  __ mov(MemOperand(ebp, BaselineFrameConstants::kFeedbackCellFromFp),
         feedback_cell);
  feedback_cell = no_reg;
  // Update feedback vector cache.
  static_assert(InterpreterFrameConstants::kFeedbackVectorFromFp ==
                BaselineFrameConstants::kFeedbackVectorFromFp);
  __ mov(MemOperand(ebp, InterpreterFrameConstants::kFeedbackVectorFromFp),
         feedback_vector);
  feedback_vector = no_reg;

  // Compute baseline pc for bytecode offset.
  ExternalReference get_baseline_pc_extref;
  if (next_bytecode || is_osr) {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_next_executed_bytecode();
  } else {
    get_baseline_pc_extref =
        ExternalReference::baseline_pc_for_bytecode_offset();
  }
  Register get_baseline_pc = ecx;
  __ LoadAddress(get_baseline_pc, get_baseline_pc_extref);

  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset.
  // TODO(pthier): Investigate if it is feasible to handle this special case
  // in TurboFan instead of here.
  Label valid_bytecode_offset, function_entry_bytecode;
  if (!is_osr) {
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag +
                     kFunctionEntryBytecodeOffset));
    __ j(equal, &function_entry_bytecode);
  }

  __ sub(kInterpreterBytecodeOffsetRegister,
         Immediate(BytecodeArray::kHeaderSize - kHeapObjectTag));

  __ bind(&valid_bytecode_offset);
  // Get bytecode array from the stack frame.
  __ mov(kInterpreterBytecodeArrayRegister,
         MemOperand(ebp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ PrepareCallCFunction(3, eax);
    __ mov(Operand(esp, 0 * kSystemPointerSize), code_obj);
    __ mov(Operand(esp, 1 * kSystemPointerSize),
           kInterpreterBytecodeOffsetRegister);
    __ mov(Operand(esp, 2 * kSystemPointerSize),
           kInterpreterBytecodeArrayRegister);
    __ CallCFunction(get_baseline_pc, 3);
  }
  __ LoadCodeInstructionStart(code_obj, code_obj);
  __ add(code_obj, kReturnRegister0);
  __ pop(kInterpreterAccumulatorRegister);

  if (is_osr) {
    DCHECK_EQ(feedback_cell, no_reg);
    closure = ecx;
    __ mov(closure, MemOperand(ebp, StandardFrameConstants::kFunctionOffset));
    ResetJSFunctionAge(masm, closure, closure);
    Generate_OSREntry(masm, code_obj);
  } else {
    __ jmp(code_obj);
  }
  __ Trap();  // Unreachable.

  if (!is_osr) {
    __ bind(&function_entry_bytecode);
    // If the bytecode offset is kFunctionEntryOffset, get the start address of
    // the first bytecode.
    __ mov(kInterpreterBytecodeOffsetRegister, Immediate(0));
    if (next_bytecode) {
      __ LoadAddress(get_baseline_pc,
                     ExternalReference::baseline_pc_for_bytecode_offset());
    }
    __ jmp(&valid_bytecode_offset);
  }

  __ bind(&install_baseline_code);
  // Pop/re-push the accumulator so that it's spilled within the below frame
  // scope, to keep the stack valid.
  __ pop(kInterpreterAccumulatorRegister);
  // Restore the clobbered context register.
  __ mov(kContextRegister,
         Operand(ebp, StandardFrameConstants::kContextOffset));
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(kInterpreterAccumulatorRegister);
    // Reload closure.
    closure = eax;
    __ mov(closure, MemOperand(ebp, StandardFrameConstants::kFunctionOffset));
    __ Push(closure);
    __ CallRuntime(Runtime::kInstallBaselineCode, 1);
    __ Pop(kInterpreterAccumulatorRegister);
  }
  // Retry from the start after installing baseline code.
  __ jmp(&start);
}

}  // namespace

void Builtins::Generate_BaselineOrInterpreterEnterAtBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false);
}

void Builtins::Generate_BaselineOrInterpreterEnterAtNextBytecode(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, true);
}

void Builtins::Generate_InterpreterOnStackReplacement_ToBaseline(
    MacroAssembler* masm) {
  Generate_BaselineOrInterpreterEntry(masm, false, true);
}

void Builtins::Generate_RestartFrameTrampoline(MacroAssembler* masm) {
  // Frame is being dropped:
  // - Look up current function on the frame.
  // - Leave the frame.
  // - Restart the frame by calling the function.

  __ mov(edi, Operand(ebp, StandardFrameConstants::kFunctionOffset));
  __ mov(eax, Operand(ebp, StandardFrameConstants::kArgCOffset));

  __ LeaveFrame(StackFrame::INTERPRETED);

  // The arguments are already in the stack (including any necessary padding),
  // we should not try to massage the arguments again.
  __ mov(ecx, Immediate(kDontAdaptArgumentsSentinel));
  __ mov(esi, FieldOperand(edi, JSFunction::kContextOffset));
  __ InvokeFunctionCode(edi, no_reg, ecx, eax, InvokeType::kJump);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_IA32
