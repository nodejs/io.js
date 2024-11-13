// Copyright 2013 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#if V8_TARGET_ARCH_ARM64

#include "src/api/api-arguments.h"
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
#include "src/objects/instance-type.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"
#include "src/objects/smi.h"
#include "src/runtime/runtime.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/wasm/baseline/liftoff-assembler-defs.h"
#include "src/wasm/object-access.h"
#include "src/wasm/stacks.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-linkage.h"
#include "src/wasm/wasm-objects.h"
#endif  // V8_ENABLE_WEBASSEMBLY

#if defined(V8_OS_WIN)
#include "src/diagnostics/unwinding-info-win64.h"
#endif  // V8_OS_WIN

namespace v8 {
namespace internal {

#define __ ACCESS_MASM(masm)

namespace {
constexpr int kReceiverOnStackSize = kSystemPointerSize;
}  // namespace

void Builtins::Generate_Adaptor(MacroAssembler* masm, Address address) {
  __ CodeEntry();

  __ Mov(kJavaScriptCallExtraArg1Register, ExternalReference::Create(address));
  __ TailCallBuiltin(Builtin::kAdaptorWithBuiltinExitFrame);
}

namespace {

void Generate_JSBuiltinsConstructStubHelper(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : number of arguments
  //  -- x1     : constructor function
  //  -- x3     : new target
  //  -- cp     : context
  //  -- lr     : return address
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_JSConstructStubHelper");
  Label stack_overflow;

  __ StackOverflowCheck(x0, &stack_overflow);

  // Enter a construct frame.
  {
    FrameScope scope(masm, StackFrame::CONSTRUCT);
    Label already_aligned;
    Register argc = x0;

    if (v8_flags.debug_code) {
      // Check that FrameScope pushed the context on to the stack already.
      __ Peek(x2, 0);
      __ Cmp(x2, cp);
      __ Check(eq, AbortReason::kUnexpectedValue);
    }

    // Push number of arguments.
    __ Push(argc, padreg);

    // Round up to maintain alignment.
    Register slot_count = x2;
    Register slot_count_without_rounding = x12;
    __ Add(slot_count_without_rounding, argc, 1);
    __ Bic(slot_count, slot_count_without_rounding, 1);
    __ Claim(slot_count);

    // Preserve the incoming parameters on the stack.
    __ LoadRoot(x4, RootIndex::kTheHoleValue);

    // Compute a pointer to the slot immediately above the location on the
    // stack to which arguments will be later copied.
    __ SlotAddress(x2, argc);

    // Store padding, if needed.
    __ Tbnz(slot_count_without_rounding, 0, &already_aligned);
    __ Str(padreg, MemOperand(x2));
    __ Bind(&already_aligned);

    // TODO(victorgomes): When the arguments adaptor is completely removed, we
    // should get the formal parameter count and copy the arguments in its
    // correct position (including any undefined), instead of delaying this to
    // InvokeFunction.

    // Copy arguments to the expression stack.
    {
      Register count = x2;
      Register dst = x10;
      Register src = x11;
      __ SlotAddress(dst, 0);
      // Poke the hole (receiver).
      __ Str(x4, MemOperand(dst));
      __ Add(dst, dst, kSystemPointerSize);  // Skip receiver.
      __ Add(src, fp,
             StandardFrameConstants::kCallerSPOffset +
                 kSystemPointerSize);  // Skip receiver.
      __ Sub(count, argc, kJSArgcReceiverSlots);
      __ CopyDoubleWords(dst, src, count);
    }

    // ----------- S t a t e -------------
    //  --                           x0: number of arguments (untagged)
    //  --                           x1: constructor function
    //  --                           x3: new target
    // If argc is odd:
    //  --     sp[0*kSystemPointerSize]: the hole (receiver)
    //  --     sp[1*kSystemPointerSize]: argument 1
    //  --             ...
    //  -- sp[(n-1)*kSystemPointerSize]: argument (n - 1)
    //  -- sp[(n+0)*kSystemPointerSize]: argument n
    //  -- sp[(n+1)*kSystemPointerSize]: padding
    //  -- sp[(n+2)*kSystemPointerSize]: padding
    //  -- sp[(n+3)*kSystemPointerSize]: number of arguments
    //  -- sp[(n+4)*kSystemPointerSize]: context (pushed by FrameScope)
    // If argc is even:
    //  --     sp[0*kSystemPointerSize]: the hole (receiver)
    //  --     sp[1*kSystemPointerSize]: argument 1
    //  --             ...
    //  -- sp[(n-1)*kSystemPointerSize]: argument (n - 1)
    //  -- sp[(n+0)*kSystemPointerSize]: argument n
    //  -- sp[(n+1)*kSystemPointerSize]: padding
    //  -- sp[(n+2)*kSystemPointerSize]: number of arguments
    //  -- sp[(n+3)*kSystemPointerSize]: context (pushed by FrameScope)
    // -----------------------------------

    // Call the function.
    __ InvokeFunctionWithNewTarget(x1, x3, argc, InvokeType::kCall);

    // Restore the context from the frame.
    __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
    // Restore arguments count from the frame. Use fp relative
    // addressing to avoid the circular dependency between padding existence and
    // argc parity.
    __ Ldr(x1, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
    // Leave construct frame.
  }

  // Remove caller arguments from the stack and return.
  __ DropArguments(x1);
  __ Ret();

  __ Bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();
  }
}

}  // namespace

// The construct stub for ES5 constructor functions and ES6 class constructors.
void Builtins::Generate_JSConstructStubGeneric(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : number of arguments
  //  -- x1     : constructor function
  //  -- x3     : new target
  //  -- lr     : return address
  //  -- cp     : context pointer
  //  -- sp[...]: constructor arguments
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_JSConstructStubGeneric");

  FrameScope scope(masm, StackFrame::MANUAL);
  // Enter a construct frame.
  __ EnterFrame(StackFrame::CONSTRUCT);
  Label post_instantiation_deopt_entry, not_create_implicit_receiver;

  if (v8_flags.debug_code) {
    // Check that FrameScope pushed the context on to the stack already.
    __ Peek(x2, 0);
    __ Cmp(x2, cp);
    __ Check(eq, AbortReason::kUnexpectedValue);
  }

  // Preserve the incoming parameters on the stack.
  __ Push(x0, x1, padreg, x3);

  // ----------- S t a t e -------------
  //  --        sp[0*kSystemPointerSize]: new target
  //  --        sp[1*kSystemPointerSize]: padding
  //  -- x1 and sp[2*kSystemPointerSize]: constructor function
  //  --        sp[3*kSystemPointerSize]: number of arguments
  //  --        sp[4*kSystemPointerSize]: context (pushed by FrameScope)
  // -----------------------------------

  __ LoadTaggedField(
      x4, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(x4, SharedFunctionInfo::kFlagsOffset));
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(w4);
  __ JumpIfIsInRange(
      w4, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver);

  // If not derived class constructor: Allocate the new receiver object.
  __ CallBuiltin(Builtin::kFastNewObject);

  __ B(&post_instantiation_deopt_entry);

  // Else: use TheHoleValue as receiver for constructor call
  __ Bind(&not_create_implicit_receiver);
  __ LoadRoot(x0, RootIndex::kTheHoleValue);

  // ----------- S t a t e -------------
  //  --                                x0: receiver
  //  -- Slot 4 / sp[0*kSystemPointerSize]: new target
  //  -- Slot 3 / sp[1*kSystemPointerSize]: padding
  //  -- Slot 2 / sp[2*kSystemPointerSize]: constructor function
  //  -- Slot 1 / sp[3*kSystemPointerSize]: number of arguments
  //  -- Slot 0 / sp[4*kSystemPointerSize]: context
  // -----------------------------------
  // Deoptimizer enters here.
  masm->isolate()->heap()->SetConstructStubCreateDeoptPCOffset(
      masm->pc_offset());

  __ Bind(&post_instantiation_deopt_entry);

  // Restore new target from the top of the stack.
  __ Peek(x3, 0 * kSystemPointerSize);

  // Restore constructor function and argument count.
  __ Ldr(x1, MemOperand(fp, ConstructFrameConstants::kConstructorOffset));
  __ Ldr(x12, MemOperand(fp, ConstructFrameConstants::kLengthOffset));

  // Copy arguments to the expression stack. The called function pops the
  // receiver along with its arguments, so we need an extra receiver on the
  // stack, in case we have to return it later.

  // Overwrite the new target with a receiver.
  __ Poke(x0, 0);

  // Push two further copies of the receiver. One will be popped by the called
  // function. The second acts as padding if the number of arguments plus
  // receiver is odd - pushing receiver twice avoids branching. It also means
  // that we don't have to handle the even and odd cases specially on
  // InvokeFunction's return, as top of stack will be the receiver in either
  // case.
  __ Push(x0, x0);

  // ----------- S t a t e -------------
  //  --                              x3: new target
  //  --                             x12: number of arguments (untagged)
  //  --        sp[0*kSystemPointerSize]: implicit receiver (overwrite if argc
  //  odd)
  //  --        sp[1*kSystemPointerSize]: implicit receiver
  //  --        sp[2*kSystemPointerSize]: implicit receiver
  //  --        sp[3*kSystemPointerSize]: padding
  //  -- x1 and sp[4*kSystemPointerSize]: constructor function
  //  --        sp[5*kSystemPointerSize]: number of arguments
  //  --        sp[6*kSystemPointerSize]: context
  // -----------------------------------

  // Round the number of arguments down to the next even number, and claim
  // slots for the arguments. If the number of arguments was odd, the last
  // argument will overwrite one of the receivers pushed above.
  Register argc_without_receiver = x11;
  __ Sub(argc_without_receiver, x12, kJSArgcReceiverSlots);
  __ Bic(x10, x12, 1);

  // Check if we have enough stack space to push all arguments.
  Label stack_overflow;
  __ StackOverflowCheck(x10, &stack_overflow);
  __ Claim(x10);

  // TODO(victorgomes): When the arguments adaptor is completely removed, we
  // should get the formal parameter count and copy the arguments in its
  // correct position (including any undefined), instead of delaying this to
  // InvokeFunction.

  // Copy the arguments.
  {
    Register count = x2;
    Register dst = x10;
    Register src = x11;
    __ Mov(count, argc_without_receiver);
    __ Poke(x0, 0);          // Add the receiver.
    __ SlotAddress(dst, 1);  // Skip receiver.
    __ Add(src, fp,
           StandardFrameConstants::kCallerSPOffset + kSystemPointerSize);
    __ CopyDoubleWords(dst, src, count);
  }

  // Call the function.
  __ Mov(x0, x12);
  __ InvokeFunctionWithNewTarget(x1, x3, x0, InvokeType::kCall);

  // If the result is an object (in the ECMA sense), we should get rid
  // of the receiver and use the result; see ECMA-262 section 13.2.2-7
  // on page 74.
  Label use_receiver, do_throw, leave_and_return, check_receiver;

  // If the result is undefined, we jump out to using the implicit receiver.
  __ CompareRoot(x0, RootIndex::kUndefinedValue);
  __ B(ne, &check_receiver);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ Bind(&use_receiver);
  __ Peek(x0, 0 * kSystemPointerSize);
  __ CompareRoot(x0, RootIndex::kTheHoleValue);
  __ B(eq, &do_throw);

  __ Bind(&leave_and_return);
  // Restore arguments count from the frame.
  __ Ldr(x1, MemOperand(fp, ConstructFrameConstants::kLengthOffset));
  // Leave construct frame.
  __ LeaveFrame(StackFrame::CONSTRUCT);
  // Remove caller arguments from the stack and return.
  __ DropArguments(x1);
  __ Ret();

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_receiver);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(x0, &use_receiver);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(x0, x4, &leave_and_return);
  __ B(&use_receiver);

  __ Bind(&do_throw);
  // Restore the context from the frame.
  __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  __ Unreachable();

  __ Bind(&stack_overflow);
  // Restore the context from the frame.
  __ Ldr(cp, MemOperand(fp, ConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ Unreachable();
}
void Builtins::Generate_JSBuiltinsConstructStub(MacroAssembler* masm) {
  Generate_JSBuiltinsConstructStubHelper(masm);
}

void Builtins::Generate_ConstructedNonConstructable(MacroAssembler* masm) {
  FrameScope scope(masm, StackFrame::INTERNAL);
  __ PushArgument(x1);
  __ CallRuntime(Runtime::kThrowConstructedNonConstructable);
  __ Unreachable();
}

static void AssertCodeIsBaselineAllowClobber(MacroAssembler* masm,
                                             Register code, Register scratch) {
  // Verify that the code kind is baseline code via the CodeKind.
  __ Ldr(scratch, FieldMemOperand(code, Code::kFlagsOffset));
  __ DecodeField<Code::KindField>(scratch);
  __ Cmp(scratch, Operand(static_cast<int>(CodeKind::BASELINE)));
  __ Assert(eq, AbortReason::kExpectedBaselineData);
}

static void AssertCodeIsBaseline(MacroAssembler* masm, Register code,
                                 Register scratch) {
  DCHECK(!AreAliased(code, scratch));
  return AssertCodeIsBaselineAllowClobber(masm, code, scratch);
}

static void CheckSharedFunctionInfoBytecodeOrBaseline(MacroAssembler* masm,
                                                      Register data,
                                                      Register scratch,
                                                      Label* is_baseline,
                                                      Label* is_bytecode) {
#if V8_STATIC_ROOTS_BOOL
  __ IsObjectTypeFast(data, scratch, CODE_TYPE);
#else
  __ CompareObjectType(data, scratch, scratch, CODE_TYPE);
#endif  // V8_STATIC_ROOTS_BOOL
  if (v8_flags.debug_code) {
    Label not_baseline;
    __ B(ne, &not_baseline);
    AssertCodeIsBaseline(masm, data, scratch);
    __ B(eq, is_baseline);
    __ Bind(&not_baseline);
  } else {
    __ B(eq, is_baseline);
  }

#if V8_STATIC_ROOTS_BOOL
  // scratch already contains the compressed map.
  __ CompareInstanceTypeWithUniqueCompressedMap(scratch, Register::no_reg(),
                                                INTERPRETER_DATA_TYPE);
#else
  // scratch already contains the instance type.
  __ Cmp(scratch, INTERPRETER_DATA_TYPE);
#endif  // V8_STATIC_ROOTS_BOOL
  __ B(ne, is_bytecode);
}

// TODO(v8:11429): Add a path for "not_compiled" and unify the two uses under
// the more general dispatch.
static void GetSharedFunctionInfoBytecodeOrBaseline(
    MacroAssembler* masm, Register sfi, Register bytecode, Register scratch1,
    Label* is_baseline, Label* is_unavailable) {
  DCHECK(!AreAliased(bytecode, scratch1));
  ASM_CODE_COMMENT(masm);
  Label done;

  Register data = bytecode;
  __ LoadTrustedPointerField(
      data,
      FieldMemOperand(sfi, SharedFunctionInfo::kTrustedFunctionDataOffset),
      kUnknownIndirectPointerTag);

  if (V8_JITLESS_BOOL) {
    __ IsObjectType(data, scratch1, scratch1, INTERPRETER_DATA_TYPE);
    __ B(ne, &done);
  } else {
    CheckSharedFunctionInfoBytecodeOrBaseline(masm, data, scratch1, is_baseline,
                                              &done);
  }

  __ LoadProtectedPointerField(
      bytecode, FieldMemOperand(data, InterpreterData::kBytecodeArrayOffset));

  __ Bind(&done);
  __ IsObjectType(bytecode, scratch1, scratch1, BYTECODE_ARRAY_TYPE);
  __ B(ne, is_unavailable);
}

// static
void Builtins::Generate_ResumeGeneratorTrampoline(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the value to pass to the generator
  //  -- x1 : the JSGeneratorObject to resume
  //  -- lr : return address
  // -----------------------------------

  // Store input value into generator object.
  __ StoreTaggedField(
      x0, FieldMemOperand(x1, JSGeneratorObject::kInputOrDebugPosOffset));
  __ RecordWriteField(x1, JSGeneratorObject::kInputOrDebugPosOffset, x0,
                      kLRHasNotBeenSaved, SaveFPRegsMode::kIgnore);
  // Check that x1 is still valid, RecordWrite might have clobbered it.
  __ AssertGeneratorObject(x1);

  // Load suspended function and context.
  __ LoadTaggedField(x4,
                     FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
  __ LoadTaggedField(cp, FieldMemOperand(x4, JSFunction::kContextOffset));

  // Flood function if we are stepping.
  Label prepare_step_in_if_stepping, prepare_step_in_suspended_generator;
  Label stepping_prepared;
  ExternalReference debug_hook =
      ExternalReference::debug_hook_on_function_call_address(masm->isolate());
  __ Mov(x10, debug_hook);
  __ Ldrsb(x10, MemOperand(x10));
  __ CompareAndBranch(x10, Operand(0), ne, &prepare_step_in_if_stepping);

  // Flood function if we need to continue stepping in the suspended generator.
  ExternalReference debug_suspended_generator =
      ExternalReference::debug_suspended_generator_address(masm->isolate());
  __ Mov(x10, debug_suspended_generator);
  __ Ldr(x10, MemOperand(x10));
  __ CompareAndBranch(x10, Operand(x1), eq,
                      &prepare_step_in_suspended_generator);
  __ Bind(&stepping_prepared);

  // Check the stack for overflow. We are not trying to catch interruptions
  // (i.e. debug break and preemption) here, so check the "real stack limit".
  Label stack_overflow;
  __ LoadStackLimit(x10, StackLimitKind::kRealStackLimit);
  __ Cmp(sp, x10);
  __ B(lo, &stack_overflow);

  // Get number of arguments for generator function.
  __ LoadTaggedField(
      x10, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
  __ Ldrh(w10, FieldMemOperand(
                   x10, SharedFunctionInfo::kFormalParameterCountOffset));

  __ Sub(x10, x10, kJSArgcReceiverSlots);
  // Claim slots for arguments and receiver (rounded up to a multiple of two).
  __ Add(x11, x10, 2);
  __ Bic(x11, x11, 1);
  __ Claim(x11);

  // Store padding (which might be replaced by the last argument).
  __ Sub(x11, x11, 1);
  __ Poke(padreg, Operand(x11, LSL, kSystemPointerSizeLog2));

  // Poke receiver into highest claimed slot.
  __ LoadTaggedField(x5,
                     FieldMemOperand(x1, JSGeneratorObject::kReceiverOffset));
  __ Poke(x5, __ ReceiverOperand());

  // ----------- S t a t e -------------
  //  -- x1                       : the JSGeneratorObject to resume
  //  -- x4                       : generator function
  //  -- x10                      : argument count
  //  -- cp                       : generator context
  //  -- lr                       : return address
  //  -- sp[0 .. arg count]       : claimed for receiver and args
  // -----------------------------------

  // Copy the function arguments from the generator object's register file.
  __ LoadTaggedField(
      x5,
      FieldMemOperand(x1, JSGeneratorObject::kParametersAndRegistersOffset));
  {
    Label loop, done;
    __ Cbz(x10, &done);
    __ SlotAddress(x12, x10);
    __ Add(x5, x5, Operand(x10, LSL, kTaggedSizeLog2));
    __ Add(x5, x5, Operand(FixedArray::kHeaderSize - kHeapObjectTag));
    __ Bind(&loop);
    __ Sub(x10, x10, 1);
    __ LoadTaggedField(x11, MemOperand(x5, -kTaggedSize, PreIndex));
    __ Str(x11, MemOperand(x12, -kSystemPointerSize, PostIndex));
    __ Cbnz(x10, &loop);
    __ Bind(&done);
  }

  // Underlying function needs to have bytecode available.
  if (v8_flags.debug_code) {
    Label ok, is_baseline, is_unavailable;
    Register sfi = x3;
    Register bytecode = x3;
    __ LoadTaggedField(
        sfi, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
    GetSharedFunctionInfoBytecodeOrBaseline(masm, sfi, bytecode, x0,
                                            &is_baseline, &is_unavailable);
    __ B(&ok);

    __ Bind(&is_unavailable);
    __ Abort(AbortReason::kMissingBytecodeArray);

    __ Bind(&is_baseline);
    __ IsObjectType(bytecode, x0, x0, CODE_TYPE);
    __ Assert(eq, AbortReason::kMissingBytecodeArray);

    __ Bind(&ok);
  }

  // Resume (Ignition/TurboFan) generator object.
  {
    __ LoadTaggedField(
        x0, FieldMemOperand(x4, JSFunction::kSharedFunctionInfoOffset));
    __ Ldrh(w0, FieldMemOperand(
                    x0, SharedFunctionInfo::kFormalParameterCountOffset));
    // We abuse new.target both to indicate that this is a resume call and to
    // pass in the generator object.  In ordinary calls, new.target is always
    // undefined because generator functions are non-constructable.
    __ Mov(x3, x1);
    __ Mov(x1, x4);
    __ JumpJSFunction(x1);
  }

  __ Bind(&prepare_step_in_if_stepping);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Push hole as receiver since we do not use it for stepping.
    __ LoadRoot(x5, RootIndex::kTheHoleValue);
    __ Push(x1, padreg, x4, x5);
    __ CallRuntime(Runtime::kDebugOnFunctionCall);
    __ Pop(padreg, x1);
    __ LoadTaggedField(x4,
                       FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
  }
  __ B(&stepping_prepared);

  __ Bind(&prepare_step_in_suspended_generator);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(x1, padreg);
    __ CallRuntime(Runtime::kDebugPrepareStepInSuspendedGenerator);
    __ Pop(padreg, x1);
    __ LoadTaggedField(x4,
                       FieldMemOperand(x1, JSGeneratorObject::kFunctionOffset));
  }
  __ B(&stepping_prepared);

  __ bind(&stack_overflow);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();  // This should be unreachable.
  }
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
//
// Input is either:
//   x0: root_register_value.
//   x1: new_target.
//   x2: target.
//   x3: receiver.
//   x4: argc.
//   x5: argv.
// or
//   x0: root_register_value.
//   x1: microtask_queue.
// Output:
//   x0: result.
void Generate_JSEntryVariant(MacroAssembler* masm, StackFrame::Type type,
                             Builtin entry_trampoline) {
  Label invoke, handler_entry, exit;

  {
    NoRootArrayScope no_root_array(masm);

#if defined(V8_OS_WIN)
    // In order to allow Windows debugging tools to reconstruct a call stack, we
    // must generate information describing how to recover at least fp, sp, and
    // pc for the calling frame. Here, JSEntry registers offsets to
    // xdata_encoder which then emits the offset values as part of the unwind
    // data accordingly.
    win64_unwindinfo::XdataEncoder* xdata_encoder = masm->GetXdataEncoder();
    if (xdata_encoder) {
      xdata_encoder->onFramePointerAdjustment(
          EntryFrameConstants::kDirectCallerFPOffset,
          EntryFrameConstants::kDirectCallerSPOffset);
    }
#endif

    __ PushCalleeSavedRegisters();

    // Set up the reserved register for 0.0.
    __ Fmov(fp_zero, 0.0);

    // Initialize the root register.
    // C calling convention. The first argument is passed in x0.
    __ Mov(kRootRegister, x0);

#ifdef V8_COMPRESS_POINTERS
    // Initialize the pointer cage base register.
    __ LoadRootRelative(kPtrComprCageBaseRegister,
                        IsolateData::cage_base_offset());
#endif
  }

  // Set up fp. It points to the {fp, lr} pair pushed as the last step in
  // PushCalleeSavedRegisters.
  static_assert(
      EntryFrameConstants::kCalleeSavedRegisterBytesPushedAfterFpLrPair == 0);
  static_assert(EntryFrameConstants::kOffsetToCalleeSavedRegisters == 0);
  __ Mov(fp, sp);

  // Build an entry frame (see layout below).

  // Push frame type markers.
  __ Mov(x12, StackFrame::TypeToMarker(type));
  __ Push(x12, xzr);

  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                        masm->isolate()));
  __ Ldr(x10, MemOperand(x11));  // x10 = C entry FP.

  // Clear c_entry_fp, now we've loaded its value to be pushed on the stack.
  // If the c_entry_fp is not already zero and we don't clear it, the
  // StackFrameIteratorForProfiler will assume we are executing C++ and miss the
  // JS frames on top.
  __ Str(xzr, MemOperand(x11));

  // Set js_entry_sp if this is the outermost JS call.
  Label done;
  ExternalReference js_entry_sp = ExternalReference::Create(
      IsolateAddressId::kJSEntrySPAddress, masm->isolate());
  __ Mov(x12, js_entry_sp);
  __ Ldr(x11, MemOperand(x12));  // x11 = previous JS entry SP.

  // Select between the inner and outermost frame marker, based on the JS entry
  // sp. We assert that the inner marker is zero, so we can use xzr to save a
  // move instruction.
  DCHECK_EQ(StackFrame::INNER_JSENTRY_FRAME, 0);
  __ Cmp(x11, 0);  // If x11 is zero, this is the outermost frame.
  // x11 = JS entry frame marker.
  __ Csel(x11, xzr, StackFrame::OUTERMOST_JSENTRY_FRAME, ne);
  __ B(ne, &done);
  __ Str(fp, MemOperand(x12));

  __ Bind(&done);

  __ LoadIsolateField(x9, IsolateFieldId::kFastCCallCallerFP);
  __ Ldr(x7, MemOperand(x9));
  __ Str(xzr, MemOperand(x9));
  __ LoadIsolateField(x9, IsolateFieldId::kFastCCallCallerPC);
  __ Ldr(x8, MemOperand(x9));
  __ Str(xzr, MemOperand(x9));
  __ Push(x10, x11, x7, x8);

  // The frame set up looks like this:
  // sp[0] : fast api call pc.
  // sp[1] : fast api call fp.
  // sp[2] : JS entry frame marker.
  // sp[3] : C entry FP.
  // sp[4] : stack frame marker (0).
  // sp[5] : stack frame marker (type).
  // sp[6] : saved fp   <- fp points here.
  // sp[7] : saved lr
  // sp[8,26) : other saved registers

  // Jump to a faked try block that does the invoke, with a faked catch
  // block that sets the exception.
  __ B(&invoke);

  // Prevent the constant pool from being emitted between the record of the
  // handler_entry position and the first instruction of the sequence here.
  // There is no risk because Assembler::Emit() emits the instruction before
  // checking for constant pool emission, but we do not want to depend on
  // that.
  {
    Assembler::BlockPoolsScope block_pools(masm);

    // Store the current pc as the handler offset. It's used later to create the
    // handler table.
    __ BindExceptionHandler(&handler_entry);
    masm->isolate()->builtins()->SetJSEntryHandlerOffset(handler_entry.pos());

    // Caught exception: Store result (exception) in the exception
    // field in the JSEnv and return a failure sentinel. Coming in here the
    // fp will be invalid because UnwindAndFindHandler sets it to 0 to
    // signal the existence of the JSEntry frame.
    __ Mov(x10, ExternalReference::Create(IsolateAddressId::kExceptionAddress,
                                          masm->isolate()));
  }
  __ Str(x0, MemOperand(x10));
  __ LoadRoot(x0, RootIndex::kException);
  __ B(&exit);

  // Invoke: Link this frame into the handler chain.
  __ Bind(&invoke);

  // Push new stack handler.
  static_assert(StackHandlerConstants::kSize == 2 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kSize");
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");

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

  // If an exception not caught by another handler occurs, this handler
  // returns control to the code after the B(&invoke) above, which
  // restores all callee-saved registers (including cp and fp) to their
  // saved values before returning a failure to C.
  //
  // Invoke the function by calling through JS entry trampoline builtin and
  // pop the faked function when we return.
  __ CallBuiltin(entry_trampoline);

  // Pop the stack handler and unlink this frame from the handler chain.
  static_assert(StackHandlerConstants::kNextOffset == 0 * kSystemPointerSize,
                "Unexpected offset for StackHandlerConstants::kNextOffset");
  __ Pop(x10, padreg);
  __ Mov(x11, ExternalReference::Create(IsolateAddressId::kHandlerAddress,
                                        masm->isolate()));
  __ Drop(StackHandlerConstants::kSlotCount - 2);
  __ Str(x10, MemOperand(x11));

  __ Bind(&exit);
  // x0 holds the result.
  // The stack pointer points to the top of the entry frame pushed on entry from
  // C++ (at the beginning of this stub):
  // sp[0] : fast api call pc.
  // sp[1] : fast api call fp.
  // sp[2] : JS entry frame marker.
  // sp[3] : C entry FP.
  // sp[4] : stack frame marker (0).
  // sp[5] : stack frame marker (type).
  // sp[6] : saved fp   <- fp points here.
  // sp[7] : saved lr
  // sp[8,26) : other saved registers

  __ Pop(x10, x11);
  __ LoadIsolateField(x8, IsolateFieldId::kFastCCallCallerPC);
  __ Str(x10, MemOperand(x8));
  __ LoadIsolateField(x9, IsolateFieldId::kFastCCallCallerFP);
  __ Str(x11, MemOperand(x9));

  // Check if the current stack frame is marked as the outermost JS frame.
  Label non_outermost_js_2;
  {
    Register c_entry_fp = x11;
    __ PeekPair(x10, c_entry_fp, 0);
    __ Cmp(x10, StackFrame::OUTERMOST_JSENTRY_FRAME);
    __ B(ne, &non_outermost_js_2);
    __ Mov(x12, js_entry_sp);
    __ Str(xzr, MemOperand(x12));
    __ Bind(&non_outermost_js_2);

    // Restore the top frame descriptors from the stack.
    __ Mov(x12, ExternalReference::Create(IsolateAddressId::kCEntryFPAddress,
                                          masm->isolate()));
    __ Str(c_entry_fp, MemOperand(x12));
  }

  // Reset the stack to the callee saved registers.
  static_assert(
      EntryFrameConstants::kFixedFrameSize % (2 * kSystemPointerSize) == 0,
      "Size of entry frame is not a multiple of 16 bytes");
  // fast_c_call_caller_fp and fast_c_call_caller_pc have already been popped.
  int drop_count =
      (EntryFrameConstants::kFixedFrameSize / kSystemPointerSize) - 2;
  __ Drop(drop_count);
  // Restore the callee-saved registers and return.
  __ PopCalleeSavedRegisters();
  __ Ret();
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

// Input:
//   x1: new.target.
//   x2: function.
//   x3: receiver.
//   x4: argc.
//   x5: argv.
// Output:
//   x0: result.
static void Generate_JSEntryTrampolineHelper(MacroAssembler* masm,
                                             bool is_construct) {
  Register new_target = x1;
  Register function = x2;
  Register receiver = x3;
  Register argc = x4;
  Register argv = x5;
  Register scratch = x10;
  Register slots_to_claim = x11;

  {
    // Enter an internal frame.
    FrameScope scope(masm, StackFrame::INTERNAL);

    // Setup the context (we need to use the caller context from the isolate).
    __ Mov(scratch, ExternalReference::Create(IsolateAddressId::kContextAddress,
                                              masm->isolate()));
    __ Ldr(cp, MemOperand(scratch));

    // Claim enough space for the arguments and the function, including an
    // optional slot of padding.
    constexpr int additional_slots = 2;
    __ Add(slots_to_claim, argc, additional_slots);
    __ Bic(slots_to_claim, slots_to_claim, 1);

    // Check if we have enough stack space to push all arguments.
    Label enough_stack_space, stack_overflow;
    __ StackOverflowCheck(slots_to_claim, &stack_overflow);
    __ B(&enough_stack_space);

    __ Bind(&stack_overflow);
    __ CallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();

    __ Bind(&enough_stack_space);
    __ Claim(slots_to_claim);

    // Store padding (which might be overwritten).
    __ SlotAddress(scratch, slots_to_claim);
    __ Str(padreg, MemOperand(scratch, -kSystemPointerSize));

    // Store receiver on the stack.
    __ Poke(receiver, 0);
    // Store function on the stack.
    __ SlotAddress(scratch, argc);
    __ Str(function, MemOperand(scratch));

    // Copy arguments to the stack in a loop, in reverse order.
    // x4: argc.
    // x5: argv.
    Label loop, done;

    // Skip the argument set up if we have no arguments.
    __ Cmp(argc, JSParameterCount(0));
    __ B(eq, &done);

    // scratch has been set to point to the location of the function, which
    // marks the end of the argument copy.
    __ SlotAddress(x0, 1);  // Skips receiver.
    __ Bind(&loop);
    // Load the handle.
    __ Ldr(x11, MemOperand(argv, kSystemPointerSize, PostIndex));
    // Dereference the handle.
    __ Ldr(x11, MemOperand(x11));
    // Poke the result into the stack.
    __ Str(x11, MemOperand(x0, kSystemPointerSize, PostIndex));
    // Loop if we've not reached the end of copy marker.
    __ Cmp(x0, scratch);
    __ B(lt, &loop);

    __ Bind(&done);

    __ Mov(x0, argc);
    __ Mov(x3, new_target);
    __ Mov(x1, function);
    // x0: argc.
    // x1: function.
    // x3: new.target.

    // Initialize all JavaScript callee-saved registers, since they will be seen
    // by the garbage collector as part of handlers.
    // The original values have been saved in JSEntry.
    __ LoadRoot(x19, RootIndex::kUndefinedValue);
    __ Mov(x20, x19);
    __ Mov(x21, x19);
    __ Mov(x22, x19);
    __ Mov(x23, x19);
    __ Mov(x24, x19);
    __ Mov(x25, x19);
#ifndef V8_COMPRESS_POINTERS
    __ Mov(x28, x19);
#endif
    // Don't initialize the reserved registers.
    // x26 : root register (kRootRegister).
    // x27 : context pointer (cp).
    // x28 : pointer cage base register (kPtrComprCageBaseRegister).
    // x29 : frame pointer (fp).

    Builtin builtin = is_construct ? Builtin::kConstruct : Builtins::Call();
    __ CallBuiltin(builtin);

    // Exit the JS internal frame and remove the parameters (except function),
    // and return.
  }

  // Result is in x0. Return.
  __ Ret();
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
  //   x0: root_register_value
  //   x1: microtask_queue

  __ Mov(RunMicrotasksDescriptor::MicrotaskQueueRegister(), x1);
  __ TailCallBuiltin(Builtin::kRunMicrotasks);
}

static void LeaveInterpreterFrame(MacroAssembler* masm, Register scratch1,
                                  Register scratch2) {
  ASM_CODE_COMMENT(masm);
  Register params_size = scratch1;
  // Get the size of the formal parameters + receiver (in bytes).
  __ Ldr(params_size,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Ldrh(params_size.W(),
          FieldMemOperand(params_size, BytecodeArray::kParameterSizeOffset));

  Register actual_params_size = scratch2;
  // Compute the size of the actual parameters + receiver (in bytes).
  __ Ldr(actual_params_size,
         MemOperand(fp, StandardFrameConstants::kArgCOffset));

  // If actual is bigger than formal, then we should use it to free up the stack
  // arguments.
  __ Cmp(params_size, actual_params_size);
  __ Csel(params_size, actual_params_size, params_size, kLessThan);

  // Leave the frame (also dropping the register file).
  __ LeaveFrame(StackFrame::INTERPRETED);

  // Drop receiver + arguments.
  __ DropArguments(params_size);
}

// Advance the current bytecode offset. This simulates what all bytecode
// handlers do upon completion of the underlying operation. Will bail out to a
// label if the bytecode (without prefix) is a return bytecode. Will not advance
// the bytecode offset if the current bytecode is a JumpLoop, instead just
// re-executing the JumpLoop to jump to the correct bytecode.
static void AdvanceBytecodeOffsetOrReturn(MacroAssembler* masm,
                                          Register bytecode_array,
                                          Register bytecode_offset,
                                          Register bytecode, Register scratch1,
                                          Register scratch2, Label* if_return) {
  ASM_CODE_COMMENT(masm);
  Register bytecode_size_table = scratch1;

  // The bytecode offset value will be increased by one in wide and extra wide
  // cases. In the case of having a wide or extra wide JumpLoop bytecode, we
  // will restore the original bytecode. In order to simplify the code, we have
  // a backup of it.
  Register original_bytecode_offset = scratch2;
  DCHECK(!AreAliased(bytecode_array, bytecode_offset, bytecode_size_table,
                     bytecode, original_bytecode_offset));

  __ Mov(bytecode_size_table, ExternalReference::bytecode_size_table_address());
  __ Mov(original_bytecode_offset, bytecode_offset);

  // Check if the bytecode is a Wide or ExtraWide prefix bytecode.
  Label process_bytecode, extra_wide;
  static_assert(0 == static_cast<int>(interpreter::Bytecode::kWide));
  static_assert(1 == static_cast<int>(interpreter::Bytecode::kExtraWide));
  static_assert(2 == static_cast<int>(interpreter::Bytecode::kDebugBreakWide));
  static_assert(3 ==
                static_cast<int>(interpreter::Bytecode::kDebugBreakExtraWide));
  __ Cmp(bytecode, Operand(0x3));
  __ B(hi, &process_bytecode);
  __ Tst(bytecode, Operand(0x1));
  // The code to load the next bytecode is common to both wide and extra wide.
  // We can hoist them up here since they do not modify the flags after Tst.
  __ Add(bytecode_offset, bytecode_offset, Operand(1));
  __ Ldrb(bytecode, MemOperand(bytecode_array, bytecode_offset));
  __ B(ne, &extra_wide);

  // Update table to the wide scaled table.
  __ Add(bytecode_size_table, bytecode_size_table,
         Operand(kByteSize * interpreter::Bytecodes::kBytecodeCount));
  __ B(&process_bytecode);

  __ Bind(&extra_wide);
  // Update table to the extra wide scaled table.
  __ Add(bytecode_size_table, bytecode_size_table,
         Operand(2 * kByteSize * interpreter::Bytecodes::kBytecodeCount));

  __ Bind(&process_bytecode);

// Bailout to the return label if this is a return bytecode.
#define JUMP_IF_EQUAL(NAME)                                              \
  __ Cmp(x1, Operand(static_cast<int>(interpreter::Bytecode::k##NAME))); \
  __ B(if_return, eq);
  RETURN_BYTECODE_LIST(JUMP_IF_EQUAL)
#undef JUMP_IF_EQUAL

  // If this is a JumpLoop, re-execute it to perform the jump to the beginning
  // of the loop.
  Label end, not_jump_loop;
  __ Cmp(bytecode, Operand(static_cast<int>(interpreter::Bytecode::kJumpLoop)));
  __ B(ne, &not_jump_loop);
  // We need to restore the original bytecode_offset since we might have
  // increased it to skip the wide / extra-wide prefix bytecode.
  __ Mov(bytecode_offset, original_bytecode_offset);
  __ B(&end);

  __ bind(&not_jump_loop);
  // Otherwise, load the size of the current bytecode and advance the offset.
  __ Ldrb(scratch1.W(), MemOperand(bytecode_size_table, bytecode));
  __ Add(bytecode_offset, bytecode_offset, scratch1);

  __ Bind(&end);
}

namespace {

void ResetSharedFunctionInfoAge(MacroAssembler* masm, Register sfi) {
  __ Strh(wzr, FieldMemOperand(sfi, SharedFunctionInfo::kAgeOffset));
}

void ResetJSFunctionAge(MacroAssembler* masm, Register js_function,
                        Register scratch) {
  const Register shared_function_info(scratch);
  __ LoadTaggedField(
      shared_function_info,
      FieldMemOperand(js_function, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, shared_function_info);
}

void ResetFeedbackVectorOsrUrgency(MacroAssembler* masm,
                                   Register feedback_vector, Register scratch) {
  DCHECK(!AreAliased(feedback_vector, scratch));
  __ Ldrb(scratch,
          FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
  __ And(scratch, scratch, Operand(~FeedbackVector::OsrUrgencyBits::kMask));
  __ Strb(scratch,
          FieldMemOperand(feedback_vector, FeedbackVector::kOsrStateOffset));
}

}  // namespace

// static
void Builtins::Generate_BaselineOutOfLinePrologue(MacroAssembler* masm) {
  UseScratchRegisterScope temps(masm);
  // Need a few extra registers
  temps.Include(CPURegList(kXRegSizeInBits, {x14, x15, x22}));

  auto descriptor =
      Builtins::CallInterfaceDescriptorFor(Builtin::kBaselineOutOfLinePrologue);
  Register closure = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kClosure);
  // Load the feedback cell and vector from the closure.
  Register feedback_cell = temps.AcquireX();
  Register feedback_vector = temps.AcquireX();
  __ LoadTaggedField(feedback_cell,
                     FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(
      feedback_vector,
      FieldMemOperand(feedback_cell, FeedbackCell::kValueOffset));
  __ AssertFeedbackVector(feedback_vector, x4);

  // Check the tiering state.
  Label flags_need_processing;
  Register flags = temps.AcquireW();
  __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      flags, feedback_vector, CodeKind::BASELINE, &flags_need_processing);

  {
    UseScratchRegisterScope temps(masm);
    ResetFeedbackVectorOsrUrgency(masm, feedback_vector, temps.AcquireW());
  }

  // Increment invocation count for the function.
  {
    UseScratchRegisterScope temps(masm);
    Register invocation_count = temps.AcquireW();
    __ Ldr(invocation_count,
           FieldMemOperand(feedback_vector,
                           FeedbackVector::kInvocationCountOffset));
    __ Add(invocation_count, invocation_count, Operand(1));
    __ Str(invocation_count,
           FieldMemOperand(feedback_vector,
                           FeedbackVector::kInvocationCountOffset));
  }

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  {
    ASM_CODE_COMMENT_STRING(masm, "Frame Setup");
    // Normally the first thing we'd do here is Push(lr, fp), but we already
    // entered the frame in BaselineCompiler::Prologue, as we had to use the
    // value lr before the call to this BaselineOutOfLinePrologue builtin.

    Register callee_context = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kCalleeContext);
    Register callee_js_function = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kClosure);
    {
      UseScratchRegisterScope temps(masm);
      ResetJSFunctionAge(masm, callee_js_function, temps.AcquireX());
    }
    __ Push(callee_context, callee_js_function);
    DCHECK_EQ(callee_js_function, kJavaScriptCallTargetRegister);
    DCHECK_EQ(callee_js_function, kJSFunctionRegister);

    Register argc = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kJavaScriptCallArgCount);
    // We'll use the bytecode for both code age/OSR resetting, and pushing onto
    // the frame, so load it into a register.
    Register bytecode_array = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kInterpreterBytecodeArray);
    __ Push(argc, bytecode_array, feedback_cell, feedback_vector);
    __ AssertFeedbackVector(feedback_vector, x4);
  }

  Label call_stack_guard;
  Register frame_size = descriptor.GetRegisterParameter(
      BaselineOutOfLinePrologueDescriptor::kStackFrameSize);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt check");
    // Stack check. This folds the checks for both the interrupt stack limit
    // check and the real stack limit into one by just checking for the
    // interrupt limit. The interrupt limit is either equal to the real stack
    // limit or tighter. By ensuring we have space until that limit after
    // building the frame we can quickly precheck both at once.
    UseScratchRegisterScope temps(masm);

    Register sp_minus_frame_size = temps.AcquireX();
    __ Sub(sp_minus_frame_size, sp, frame_size);
    Register interrupt_limit = temps.AcquireX();
    __ LoadStackLimit(interrupt_limit, StackLimitKind::kInterruptStackLimit);
    __ Cmp(sp_minus_frame_size, interrupt_limit);
    __ B(lo, &call_stack_guard);
  }

  // Do "fast" return to the caller pc in lr.
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ Ret();

  __ bind(&flags_need_processing);
  {
    ASM_CODE_COMMENT_STRING(masm, "Optimized marker check");
    // Drop the frame created by the baseline call.
    __ Pop<MacroAssembler::kAuthLR>(fp, lr);
    __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);
    __ Trap();
  }

  __ bind(&call_stack_guard);
  {
    ASM_CODE_COMMENT_STRING(masm, "Stack/interrupt call");
    Register new_target = descriptor.GetRegisterParameter(
        BaselineOutOfLinePrologueDescriptor::kJavaScriptCallNewTarget);

    FrameScope frame_scope(masm, StackFrame::INTERNAL);
    // Save incoming new target or generator
    __ Push(padreg, new_target);
    __ SmiTag(frame_size);
    __ PushArgument(frame_size);
    __ CallRuntime(Runtime::kStackGuardWithGap);
    __ Pop(new_target, padreg);
  }
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
  __ Ret();
}

// static
void Builtins::Generate_BaselineOutOfLinePrologueDeopt(MacroAssembler* masm) {
  // We're here because we got deopted during BaselineOutOfLinePrologue's stack
  // check. Undo all its frame creation and call into the interpreter instead.

  // Drop the feedback vector and the bytecode offset (was the feedback vector
  // but got replaced during deopt).
  __ Drop(2);

  // Bytecode array, argc, Closure, Context.
  __ Pop(padreg, kJavaScriptCallArgCountRegister, kJavaScriptCallTargetRegister,
         kContextRegister);

  // Drop frame pointer
  __ LeaveFrame(StackFrame::BASELINE);

  // Enter the interpreter.
  __ TailCallBuiltin(Builtin::kInterpreterEntryTrampoline);
}

// Generate code for entering a JS function with the interpreter.
// On entry to the function the receiver and arguments have been pushed on the
// stack left to right.
//
// The live registers are:
//   - x0: actual argument count
//   - x1: the JS function object being called.
//   - x3: the incoming new target or generator object
//   - cp: our context.
//   - fp: our caller's frame pointer.
//   - lr: return address.
//
// The function builds an interpreter frame. See InterpreterFrameConstants in
// frame-constants.h for its layout.
void Builtins::Generate_InterpreterEntryTrampoline(
    MacroAssembler* masm, InterpreterEntryTrampolineMode mode) {
  Register closure = x1;

  // Get the bytecode array from the function object and load it into
  // kInterpreterBytecodeArrayRegister.
  Register sfi = x4;
  __ LoadTaggedField(
      sfi, FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));
  ResetSharedFunctionInfoAge(masm, sfi);

  // The bytecode array could have been flushed from the shared function info,
  // if so, call into CompileLazy.
  Label is_baseline, compile_lazy;
  GetSharedFunctionInfoBytecodeOrBaseline(masm, sfi,
                                          kInterpreterBytecodeArrayRegister,
                                          x11, &is_baseline, &compile_lazy);

  Label push_stack_frame;
  Register feedback_vector = x2;
  __ LoadFeedbackVector(feedback_vector, closure, x7, &push_stack_frame);

#ifndef V8_JITLESS
  // If feedback vector is valid, check for optimized code and update invocation
  // count.
  Label flags_need_processing;
  Register flags = w7;
  __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
      flags, feedback_vector, CodeKind::INTERPRETED_FUNCTION,
      &flags_need_processing);

  ResetFeedbackVectorOsrUrgency(masm, feedback_vector, w7);

  // Increment invocation count for the function.
  __ Ldr(w10, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));
  __ Add(w10, w10, Operand(1));
  __ Str(w10, FieldMemOperand(feedback_vector,
                              FeedbackVector::kInvocationCountOffset));

  // Open a frame scope to indicate that there is a frame on the stack.  The
  // MANUAL indicates that the scope shouldn't actually generate code to set up
  // the frame (that is done below).
#else
  // Note: By omitting the above code in jitless mode we also disable:
  // - kFlagsLogNextExecution: only used for logging/profiling; and
  // - kInvocationCountOffset: only used for tiering heuristics and code
  //   coverage.
#endif  // !V8_JITLESS

  __ Bind(&push_stack_frame);
  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ Push<MacroAssembler::kSignLR>(lr, fp);
  __ mov(fp, sp);
  __ Push(cp, closure);

  // Load the initial bytecode offset.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));

  // Push actual argument count, bytecode array, Smi tagged bytecode array
  // offset and the feedback vector.
  __ SmiTag(x6, kInterpreterBytecodeOffsetRegister);
  __ Push(kJavaScriptCallArgCountRegister, kInterpreterBytecodeArrayRegister);
  __ Push(x6, feedback_vector);

  // Allocate the local and temporary register file on the stack.
  Label stack_overflow;
  {
    // Load frame size from the BytecodeArray object.
    __ Ldr(w11, FieldMemOperand(kInterpreterBytecodeArrayRegister,
                                BytecodeArray::kFrameSizeOffset));
    __ Ldrh(w12, FieldMemOperand(kInterpreterBytecodeArrayRegister,
                                 BytecodeArray::kMaxArgumentsOffset));
    __ Add(w12, w11, Operand(w12, LSL, kSystemPointerSizeLog2));

    // Do a stack check to ensure we don't go over the limit.
    __ Sub(x10, sp, Operand(x12));
    {
      UseScratchRegisterScope temps(masm);
      Register scratch = temps.AcquireX();
      __ LoadStackLimit(scratch, StackLimitKind::kRealStackLimit);
      __ Cmp(x10, scratch);
    }
    __ B(lo, &stack_overflow);

    // If ok, push undefined as the initial value for all register file entries.
    // Note: there should always be at least one stack slot for the return
    // register in the register file.
    Label loop_header;
    __ Lsr(x11, x11, kSystemPointerSizeLog2);
    // Round up the number of registers to a multiple of 2, to align the stack
    // to 16 bytes.
    __ Add(x11, x11, 1);
    __ Bic(x11, x11, 1);
    __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);
    __ PushMultipleTimes(kInterpreterAccumulatorRegister, x11);
    __ Bind(&loop_header);
  }

  // If the bytecode array has a valid incoming new target or generator object
  // register, initialize it with incoming value which was passed in x3.
  Label no_incoming_new_target_or_generator_register;
  __ Ldrsw(x10,
           FieldMemOperand(
               kInterpreterBytecodeArrayRegister,
               BytecodeArray::kIncomingNewTargetOrGeneratorRegisterOffset));
  __ Cbz(x10, &no_incoming_new_target_or_generator_register);
  __ Str(x3, MemOperand(fp, x10, LSL, kSystemPointerSizeLog2));
  __ Bind(&no_incoming_new_target_or_generator_register);

  // Perform interrupt stack check.
  // TODO(solanes): Merge with the real stack limit check above.
  Label stack_check_interrupt, after_stack_check_interrupt;
  __ LoadStackLimit(x10, StackLimitKind::kInterruptStackLimit);
  __ Cmp(sp, x10);
  __ B(lo, &stack_check_interrupt);
  __ Bind(&after_stack_check_interrupt);

  // The accumulator is already loaded with undefined.

  // Load the dispatch table into a register and dispatch to the bytecode
  // handler at the current bytecode offset.
  Label do_dispatch;
  __ bind(&do_dispatch);
  __ Mov(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));
  __ Ldrb(x23, MemOperand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister));
  __ Mov(x1, Operand(x23, LSL, kSystemPointerSizeLog2));
  __ Ldr(kJavaScriptCallCodeStartRegister,
         MemOperand(kInterpreterDispatchTableRegister, x1));
  __ Call(kJavaScriptCallCodeStartRegister);

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

  __ JumpTarget();

  // Get bytecode array and bytecode offset from the stack frame.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  // Either return, or advance to the next bytecode and dispatch.
  Label do_return;
  __ Ldrb(x1, MemOperand(kInterpreterBytecodeArrayRegister,
                         kInterpreterBytecodeOffsetRegister));
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, x1, x2, x3,
                                &do_return);
  __ B(&do_dispatch);

  __ bind(&do_return);
  // The return value is in x0.
  LeaveInterpreterFrame(masm, x2, x4);
  __ Ret();

  __ bind(&stack_check_interrupt);
  // Modify the bytecode offset in the stack to be kFunctionEntryBytecodeOffset
  // for the call to the StackGuard.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(Smi::FromInt(BytecodeArray::kHeaderSize - kHeapObjectTag +
                              kFunctionEntryBytecodeOffset)));
  __ Str(kInterpreterBytecodeOffsetRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  __ CallRuntime(Runtime::kStackGuard);

  // After the call, restore the bytecode array, bytecode offset and accumulator
  // registers again. Also, restore the bytecode offset in the stack to its
  // previous value.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ LoadRoot(kInterpreterAccumulatorRegister, RootIndex::kUndefinedValue);

  __ SmiTag(x10, kInterpreterBytecodeOffsetRegister);
  __ Str(x10, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  __ jmp(&after_stack_check_interrupt);

#ifndef V8_JITLESS
  __ bind(&flags_need_processing);
  __ OptimizeCodeOrTailCallOptimizedCodeSlot(flags, feedback_vector);

  __ bind(&is_baseline);
  {
    // Load the feedback vector from the closure.
    __ LoadTaggedField(
        feedback_vector,
        FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
    __ LoadTaggedField(
        feedback_vector,
        FieldMemOperand(feedback_vector, FeedbackCell::kValueOffset));

    Label install_baseline_code;
    // Check if feedback vector is valid. If not, call prepare for baseline to
    // allocate it.
    __ LoadTaggedField(
        x7, FieldMemOperand(feedback_vector, HeapObject::kMapOffset));
    __ Ldrh(x7, FieldMemOperand(x7, Map::kInstanceTypeOffset));
    __ Cmp(x7, FEEDBACK_VECTOR_TYPE);
    __ B(ne, &install_baseline_code);

    // Check the tiering state.
    __ LoadFeedbackVectorFlagsAndJumpIfNeedsProcessing(
        flags, feedback_vector, CodeKind::BASELINE, &flags_need_processing);

    // Load the baseline code into the closure.
    __ Move(x2, kInterpreterBytecodeArrayRegister);
    static_assert(kJavaScriptCallCodeStartRegister == x2, "ABI mismatch");
    __ ReplaceClosureCodeWithOptimizedCode(x2, closure);
    __ JumpCodeObject(x2, kJSEntrypointTag);

    __ bind(&install_baseline_code);
    __ GenerateTailCallToReturnedCode(Runtime::kInstallBaselineCode);
  }
#endif  // !V8_JITLESS

  __ bind(&compile_lazy);
  __ GenerateTailCallToReturnedCode(Runtime::kCompileLazy);
  __ Unreachable();  // Should not return.

  __ bind(&stack_overflow);
  __ CallRuntime(Runtime::kThrowStackOverflow);
  __ Unreachable();  // Should not return.
}

static void GenerateInterpreterPushArgs(MacroAssembler* masm, Register num_args,
                                        Register first_arg_index,
                                        Register spread_arg_out,
                                        ConvertReceiverMode receiver_mode,
                                        InterpreterPushArgsMode mode) {
  ASM_CODE_COMMENT(masm);
  Register last_arg_addr = x10;
  Register stack_addr = x11;
  Register slots_to_claim = x12;
  Register slots_to_copy = x13;

  DCHECK(!AreAliased(num_args, first_arg_index, last_arg_addr, stack_addr,
                     slots_to_claim, slots_to_copy));
  // spread_arg_out may alias with the first_arg_index input.
  DCHECK(!AreAliased(spread_arg_out, last_arg_addr, stack_addr, slots_to_claim,
                     slots_to_copy));

  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Exclude final spread from slots to claim and the number of arguments.
    __ Sub(num_args, num_args, 1);
  }

  // Round up to an even number of slots.
  __ Add(slots_to_claim, num_args, 1);
  __ Bic(slots_to_claim, slots_to_claim, 1);

  __ Claim(slots_to_claim);
  {
    // Store padding, which may be overwritten.
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Sub(scratch, slots_to_claim, 1);
    __ Poke(padreg, Operand(scratch, LSL, kSystemPointerSizeLog2));
  }

  const bool skip_receiver =
      receiver_mode == ConvertReceiverMode::kNullOrUndefined;
  if (skip_receiver) {
    __ Sub(slots_to_copy, num_args, kJSArgcReceiverSlots);
  } else {
    __ Mov(slots_to_copy, num_args);
  }
  __ SlotAddress(stack_addr, skip_receiver ? 1 : 0);

  __ Sub(last_arg_addr, first_arg_index,
         Operand(slots_to_copy, LSL, kSystemPointerSizeLog2));
  __ Add(last_arg_addr, last_arg_addr, kSystemPointerSize);

  // Load the final spread argument into spread_arg_out, if necessary.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ Ldr(spread_arg_out, MemOperand(last_arg_addr, -kSystemPointerSize));
  }

  __ CopyDoubleWords(stack_addr, last_arg_addr, slots_to_copy,
                     MacroAssembler::kDstLessThanSrcAndReverse);

  if (receiver_mode == ConvertReceiverMode::kNullOrUndefined) {
    // Store "undefined" as the receiver arg if we need to.
    Register receiver = x14;
    __ LoadRoot(receiver, RootIndex::kUndefinedValue);
    __ Poke(receiver, 0);
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenCallImpl(
    MacroAssembler* masm, ConvertReceiverMode receiver_mode,
    InterpreterPushArgsMode mode) {
  DCHECK(mode != InterpreterPushArgsMode::kArrayFunction);
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x2 : the address of the first argument to be pushed. Subsequent
  //          arguments should be consecutive above this, in the same order as
  //          they are to be pushed onto the stack.
  //  -- x1 : the target to call (can be any Object).
  // -----------------------------------

  // Push the arguments. num_args may be updated according to mode.
  // spread_arg_out will be updated to contain the last spread argument, when
  // mode == InterpreterPushArgsMode::kWithFinalSpread.
  Register num_args = x0;
  Register first_arg_index = x2;
  Register spread_arg_out =
      (mode == InterpreterPushArgsMode::kWithFinalSpread) ? x2 : no_reg;
  GenerateInterpreterPushArgs(masm, num_args, first_arg_index, spread_arg_out,
                              receiver_mode, mode);

  // Call the target.
  if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    __ TailCallBuiltin(Builtin::kCallWithSpread);
  } else {
    __ TailCallBuiltin(Builtins::Call(receiver_mode));
  }
}

// static
void Builtins::Generate_InterpreterPushArgsThenConstructImpl(
    MacroAssembler* masm, InterpreterPushArgsMode mode) {
  // ----------- S t a t e -------------
  // -- x0 : argument count
  // -- x3 : new target
  // -- x1 : constructor to call
  // -- x2 : allocation site feedback if available, undefined otherwise
  // -- x4 : address of the first argument
  // -----------------------------------
  __ AssertUndefinedOrAllocationSite(x2);

  // Push the arguments. num_args may be updated according to mode.
  // spread_arg_out will be updated to contain the last spread argument, when
  // mode == InterpreterPushArgsMode::kWithFinalSpread.
  Register num_args = x0;
  Register first_arg_index = x4;
  Register spread_arg_out =
      (mode == InterpreterPushArgsMode::kWithFinalSpread) ? x2 : no_reg;
  GenerateInterpreterPushArgs(masm, num_args, first_arg_index, spread_arg_out,
                              ConvertReceiverMode::kNullOrUndefined, mode);

  if (mode == InterpreterPushArgsMode::kArrayFunction) {
    __ AssertFunction(x1);

    // Tail call to the array construct stub (still in the caller
    // context at this point).
    __ TailCallBuiltin(Builtin::kArrayConstructorImpl);
  } else if (mode == InterpreterPushArgsMode::kWithFinalSpread) {
    // Call the constructor with x0, x1, and x3 unmodified.
    __ TailCallBuiltin(Builtin::kConstructWithSpread);
  } else {
    DCHECK_EQ(InterpreterPushArgsMode::kOther, mode);
    // Call the constructor with x0, x1, and x3 unmodified.
    __ TailCallBuiltin(Builtin::kConstruct);
  }
}

// static
void Builtins::Generate_ConstructForwardAllArgsImpl(
    MacroAssembler* masm, ForwardWhichFrame which_frame) {
  // ----------- S t a t e -------------
  // -- x3 : new target
  // -- x1 : constructor to call
  // -----------------------------------
  Label stack_overflow;

  // Load the frame pointer into x4.
  switch (which_frame) {
    case ForwardWhichFrame::kCurrentFrame:
      __ Move(x4, fp);
      break;
    case ForwardWhichFrame::kParentFrame:
      __ Ldr(x4, MemOperand(fp, StandardFrameConstants::kCallerFPOffset));
      break;
  }

  // Load the argument count into x0.
  __ Ldr(x0, MemOperand(x4, StandardFrameConstants::kArgCOffset));

  // Point x4 to the base of the argument list to forward, excluding the
  // receiver.
  __ Add(x4, x4,
         Operand((StandardFrameConstants::kFixedSlotCountAboveFp + 1) *
                 kSystemPointerSize));

  Register stack_addr = x11;
  Register slots_to_claim = x12;
  Register argc_without_receiver = x13;

  // Round up to even number of slots.
  __ Add(slots_to_claim, x0, 1);
  __ Bic(slots_to_claim, slots_to_claim, 1);

  __ StackOverflowCheck(slots_to_claim, &stack_overflow);

  // Adjust the stack pointer.
  __ Claim(slots_to_claim);
  {
    // Store padding, which may be overwritten.
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Sub(scratch, slots_to_claim, 1);
    __ Poke(padreg, Operand(scratch, LSL, kSystemPointerSizeLog2));
  }

  // Copy the arguments.
  __ Sub(argc_without_receiver, x0, kJSArgcReceiverSlots);
  __ SlotAddress(stack_addr, 1);
  __ CopyDoubleWords(stack_addr, x4, argc_without_receiver);

  // Push a slot for the receiver to be constructed.
  __ Mov(x14, Operand(0));
  __ Poke(x14, 0);

  // Call the constructor with x0, x1, and x3 unmodified.
  __ TailCallBuiltin(Builtin::kConstruct);

  __ Bind(&stack_overflow);
  {
    __ TailCallRuntime(Runtime::kThrowStackOverflow);
    __ Unreachable();
  }
}

namespace {

void NewImplicitReceiver(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  // -- x0 : the number of arguments
  // -- x1 : constructor to call (checked to be a JSFunction)
  // -- x3 : new target
  //
  //  Stack:
  //  -- Implicit Receiver
  //  -- [arguments without receiver]
  //  -- Implicit Receiver
  //  -- Context
  //  -- FastConstructMarker
  //  -- FramePointer
  // -----------------------------------
  Register implicit_receiver = x4;

  // Save live registers.
  __ SmiTag(x0);
  __ Push(x0, x1, x3, padreg);
  __ CallBuiltin(Builtin::kFastNewObject);
  // Save result.
  __ Mov(implicit_receiver, x0);
  // Restore live registers.
  __ Pop(padreg, x3, x1, x0);
  __ SmiUntag(x0);

  // Patch implicit receiver (in arguments)
  __ Poke(implicit_receiver, 0 * kSystemPointerSize);
  // Patch second implicit (in construct frame)
  __ Str(implicit_receiver,
         MemOperand(fp, FastConstructFrameConstants::kImplicitReceiverOffset));

  // Restore context.
  __ Ldr(cp, MemOperand(fp, FastConstructFrameConstants::kContextOffset));
}

}  // namespace

// static
void Builtins::Generate_InterpreterPushArgsThenFastConstructFunction(
    MacroAssembler* masm) {
  // ----------- S t a t e -------------
  // -- x0 : argument count
  // -- x1 : constructor to call (checked to be a JSFunction)
  // -- x3 : new target
  // -- x4 : address of the first argument
  // -- cp : context pointer
  // -----------------------------------
  __ AssertFunction(x1);

  // Check if target has a [[Construct]] internal method.
  Label non_constructor;
  __ LoadMap(x2, x1);
  __ Ldrb(x2, FieldMemOperand(x2, Map::kBitFieldOffset));
  __ TestAndBranchIfAllClear(x2, Map::Bits1::IsConstructorBit::kMask,
                             &non_constructor);

  // Enter a construct frame.
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterFrame(StackFrame::FAST_CONSTRUCT);

  if (v8_flags.debug_code) {
    // Check that FrameScope pushed the context on to the stack already.
    __ Peek(x2, 0);
    __ Cmp(x2, cp);
    __ Check(eq, AbortReason::kUnexpectedValue);
  }

  // Implicit receiver stored in the construct frame.
  __ LoadRoot(x2, RootIndex::kTheHoleValue);
  __ Push(x2, padreg);

  // Push arguments + implicit receiver.
  GenerateInterpreterPushArgs(masm, x0, x4, Register::no_reg(),
                              ConvertReceiverMode::kNullOrUndefined,
                              InterpreterPushArgsMode::kOther);
  __ Poke(x2, 0 * kSystemPointerSize);

  // Check if it is a builtin call.
  Label builtin_call;
  __ LoadTaggedField(
      x2, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w2, FieldMemOperand(x2, SharedFunctionInfo::kFlagsOffset));
  __ TestAndBranchIfAnySet(w2, SharedFunctionInfo::ConstructAsBuiltinBit::kMask,
                           &builtin_call);

  // Check if we need to create an implicit receiver.
  Label not_create_implicit_receiver;
  __ DecodeField<SharedFunctionInfo::FunctionKindBits>(w2);
  __ JumpIfIsInRange(
      w2, static_cast<uint32_t>(FunctionKind::kDefaultDerivedConstructor),
      static_cast<uint32_t>(FunctionKind::kDerivedConstructor),
      &not_create_implicit_receiver);
  NewImplicitReceiver(masm);
  __ bind(&not_create_implicit_receiver);

  // Call the function.
  __ InvokeFunctionWithNewTarget(x1, x3, x0, InvokeType::kCall);

  // ----------- S t a t e -------------
  //  -- x0     constructor result
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
  Label use_receiver, do_throw, leave_and_return, check_receiver;

  // If the result is undefined, we jump out to using the implicit receiver.
  __ CompareRoot(x0, RootIndex::kUndefinedValue);
  __ B(ne, &check_receiver);

  // Throw away the result of the constructor invocation and use the
  // on-stack receiver as the result.
  __ Bind(&use_receiver);
  __ Ldr(x0,
         MemOperand(fp, FastConstructFrameConstants::kImplicitReceiverOffset));
  __ CompareRoot(x0, RootIndex::kTheHoleValue);
  __ B(eq, &do_throw);

  __ Bind(&leave_and_return);
  // Leave construct frame.
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ Ret();

  // Otherwise we do a smi check and fall through to check if the return value
  // is a valid receiver.
  __ bind(&check_receiver);

  // If the result is a smi, it is *not* an object in the ECMA sense.
  __ JumpIfSmi(x0, &use_receiver);

  // Check if the type of the result is not an object in the ECMA sense.
  __ JumpIfJSAnyIsNotPrimitive(x0, x4, &leave_and_return);
  __ B(&use_receiver);

  __ bind(&builtin_call);
  // TODO(victorgomes): Check the possibility to turn this into a tailcall.
  __ InvokeFunctionWithNewTarget(x1, x3, x0, InvokeType::kCall);
  __ LeaveFrame(StackFrame::FAST_CONSTRUCT);
  __ Ret();

  __ Bind(&do_throw);
  // Restore the context from the frame.
  __ Ldr(cp, MemOperand(fp, FastConstructFrameConstants::kContextOffset));
  __ CallRuntime(Runtime::kThrowConstructorReturnedNonObject);
  __ Unreachable();

  // Called Construct on an Object that doesn't have a [[Construct]] internal
  // method.
  __ bind(&non_constructor);
  __ TailCallBuiltin(Builtin::kConstructedNonConstructable);
}

static void Generate_InterpreterEnterBytecode(MacroAssembler* masm) {
  // Initialize the dispatch table register.
  __ Mov(
      kInterpreterDispatchTableRegister,
      ExternalReference::interpreter_dispatch_table_address(masm->isolate()));

  // Get the bytecode array pointer from the frame.
  __ Ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));

  if (v8_flags.debug_code) {
    // Check function data field is actually a BytecodeArray object.
    __ AssertNotSmi(
        kInterpreterBytecodeArrayRegister,
        AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
    __ IsObjectType(kInterpreterBytecodeArrayRegister, x1, x1,
                    BYTECODE_ARRAY_TYPE);
    __ Assert(
        eq, AbortReason::kFunctionDataShouldBeBytecodeArrayOnInterpreterEntry);
  }

  // Get the target bytecode offset from the frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  if (v8_flags.debug_code) {
    Label okay;
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
    __ B(ge, &okay);
    __ Unreachable();
    __ bind(&okay);
  }

  // Dispatch to the target bytecode.
  __ Ldrb(x23, MemOperand(kInterpreterBytecodeArrayRegister,
                          kInterpreterBytecodeOffsetRegister));
  __ Mov(x1, Operand(x23, LSL, kSystemPointerSizeLog2));
  __ Ldr(kJavaScriptCallCodeStartRegister,
         MemOperand(kInterpreterDispatchTableRegister, x1));

  {
    UseScratchRegisterScope temps(masm);
    temps.Exclude(x17);
    __ Mov(x17, kJavaScriptCallCodeStartRegister);
    __ Call(x17);
  }

  // We return here after having executed the function in the interpreter.
  // Now jump to the correct point in the interpreter entry trampoline.
  Label builtin_trampoline, trampoline_loaded;
  Tagged<Smi> interpreter_entry_return_pc_offset(
      masm->isolate()->heap()->interpreter_entry_return_pc_offset());
  DCHECK_NE(interpreter_entry_return_pc_offset, Smi::zero());

  // If the SFI function_data is an InterpreterData, the function will have a
  // custom copy of the interpreter entry trampoline for profiling. If so,
  // get the custom trampoline, otherwise grab the entry address of the global
  // trampoline.
  __ Ldr(x1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ LoadTaggedField(
      x1, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ LoadTrustedPointerField(
      x1, FieldMemOperand(x1, SharedFunctionInfo::kTrustedFunctionDataOffset),
      kUnknownIndirectPointerTag);
  __ IsObjectType(x1, kInterpreterDispatchTableRegister,
                  kInterpreterDispatchTableRegister, INTERPRETER_DATA_TYPE);
  __ B(ne, &builtin_trampoline);

  __ LoadProtectedPointerField(
      x1, FieldMemOperand(x1, InterpreterData::kInterpreterTrampolineOffset));
  __ LoadCodeInstructionStart(x1, x1, kJSEntrypointTag);
  __ B(&trampoline_loaded);

  __ Bind(&builtin_trampoline);
  __ Mov(x1, ExternalReference::
                 address_of_interpreter_entry_trampoline_instruction_start(
                     masm->isolate()));
  __ Ldr(x1, MemOperand(x1));

  __ Bind(&trampoline_loaded);

  {
    UseScratchRegisterScope temps(masm);
    temps.Exclude(x17);
    __ Add(x17, x1, Operand(interpreter_entry_return_pc_offset.value()));
    __ Br(x17);
  }
}

void Builtins::Generate_InterpreterEnterAtNextBytecode(MacroAssembler* masm) {
  // Get bytecode array and bytecode offset from the stack frame.
  __ ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Label enter_bytecode, function_entry_bytecode;
  __ cmp(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                 kFunctionEntryBytecodeOffset));
  __ B(eq, &function_entry_bytecode);

  // Load the current bytecode.
  __ Ldrb(x1, MemOperand(kInterpreterBytecodeArrayRegister,
                         kInterpreterBytecodeOffsetRegister));

  // Advance to the next bytecode.
  Label if_return;
  AdvanceBytecodeOffsetOrReturn(masm, kInterpreterBytecodeArrayRegister,
                                kInterpreterBytecodeOffsetRegister, x1, x2, x3,
                                &if_return);

  __ bind(&enter_bytecode);
  // Convert new bytecode offset to a Smi and save in the stackframe.
  __ SmiTag(x2, kInterpreterBytecodeOffsetRegister);
  __ Str(x2, MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));

  Generate_InterpreterEnterBytecode(masm);

  __ bind(&function_entry_bytecode);
  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset. Detect this case and advance to the first
  // actual bytecode.
  __ Mov(kInterpreterBytecodeOffsetRegister,
         Operand(BytecodeArray::kHeaderSize - kHeapObjectTag));
  __ B(&enter_bytecode);

  // We should never take the if_return path.
  __ bind(&if_return);
  __ Abort(AbortReason::kInvalidBytecodeAdvance);
}

void Builtins::Generate_InterpreterEnterAtBytecode(MacroAssembler* masm) {
  Generate_InterpreterEnterBytecode(masm);
}

namespace {
void Generate_ContinueToBuiltinHelper(MacroAssembler* masm,
                                      bool java_script_builtin,
                                      bool with_result) {
  const RegisterConfiguration* config(RegisterConfiguration::Default());
  int allocatable_register_count = config->num_allocatable_general_registers();
  int frame_size = BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp +
                   (allocatable_register_count +
                    BuiltinContinuationFrameConstants::PaddingSlotCount(
                        allocatable_register_count)) *
                       kSystemPointerSize;

  UseScratchRegisterScope temps(masm);
  Register scratch = temps.AcquireX();  // Temp register is not allocatable.

  // Set up frame pointer.
  __ Add(fp, sp, frame_size);

  if (with_result) {
    if (java_script_builtin) {
      __ mov(scratch, x0);
    } else {
      // Overwrite the hole inserted by the deoptimizer with the return value
      // from the LAZY deopt point.
      __ Str(x0, MemOperand(
                     fp, BuiltinContinuationFrameConstants::kCallerSPOffset));
    }
  }

  // Restore registers in pairs.
  int offset = -BuiltinContinuationFrameConstants::kFixedFrameSizeFromFp -
               allocatable_register_count * kSystemPointerSize;
  for (int i = allocatable_register_count - 1; i > 0; i -= 2) {
    int code1 = config->GetAllocatableGeneralCode(i);
    int code2 = config->GetAllocatableGeneralCode(i - 1);
    Register reg1 = Register::from_code(code1);
    Register reg2 = Register::from_code(code2);
    __ Ldp(reg1, reg2, MemOperand(fp, offset));
    offset += 2 * kSystemPointerSize;
  }

  // Restore first register separately, if number of registers is odd.
  if (allocatable_register_count % 2 != 0) {
    int code = config->GetAllocatableGeneralCode(0);
    __ Ldr(Register::from_code(code), MemOperand(fp, offset));
  }

  if (java_script_builtin) __ SmiUntag(kJavaScriptCallArgCountRegister);

  if (java_script_builtin && with_result) {
    // Overwrite the hole inserted by the deoptimizer with the return value from
    // the LAZY deopt point. x0 contains the arguments count, the return value
    // from LAZY is always the last argument.
    constexpr int return_offset =
        BuiltinContinuationFrameConstants::kCallerSPOffset /
            kSystemPointerSize -
        kJSArgcReceiverSlots;
    __ add(x0, x0, return_offset);
    __ Str(scratch, MemOperand(fp, x0, LSL, kSystemPointerSizeLog2));
    // Recover argument count.
    __ sub(x0, x0, return_offset);
  }

  // Load builtin index (stored as a Smi) and use it to get the builtin start
  // address from the builtins table.
  Register builtin = scratch;
  __ Ldr(
      builtin,
      MemOperand(fp, BuiltinContinuationFrameConstants::kBuiltinIndexOffset));

  // Restore fp, lr.
  __ Mov(sp, fp);
  __ Pop<MacroAssembler::kAuthLR>(fp, lr);

  __ LoadEntryFromBuiltinIndex(builtin, builtin);
  __ Jump(builtin);
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
  }

  // Pop TOS register and padding.
  DCHECK_EQ(kInterpreterAccumulatorRegister.code(), x0.code());
  __ Pop(x0, padreg);
  __ Ret();
}

namespace {

void Generate_OSREntry(MacroAssembler* masm, Register entry_address,
                       Operand offset = Operand(0)) {
  // Pop the return address to this function's caller from the return stack
  // buffer, since we'll never return to it.
  Label jump;
  __ Adr(lr, &jump);
  __ Ret();

  __ Bind(&jump);

  UseScratchRegisterScope temps(masm);
  temps.Exclude(x17);
  if (offset.IsZero()) {
    __ Mov(x17, entry_address);
  } else {
    __ Add(x17, entry_address, offset);
  }
  __ Br(x17);
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
    __ CompareTaggedAndBranch(x0, Smi::zero(), ne, &jump_to_optimized_code);
  }

  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallRuntime(Runtime::kCompileOptimizedOSR);
  }

  // If the code object is null, just return to the caller.
  __ CompareTaggedAndBranch(x0, Smi::zero(), ne, &jump_to_optimized_code);
  __ Ret();

  __ Bind(&jump_to_optimized_code);
  DCHECK_EQ(maybe_target_code, x0);  // Already in the right spot.

  // OSR entry tracing.
  {
    Label next;
    __ Mov(x1, ExternalReference::address_of_log_or_trace_osr());
    __ Ldrsb(x1, MemOperand(x1));
    __ Tst(x1, 0xFF);  // Mask to the LSB.
    __ B(eq, &next);

    {
      FrameScope scope(masm, StackFrame::INTERNAL);
      __ Push(x0, padreg);  // Preserve the code object.
      __ CallRuntime(Runtime::kLogOrTraceOptimizedOSREntry, 0);
      __ Pop(padreg, x0);
    }

    __ Bind(&next);
  }

  if (source == OsrSourceTier::kInterpreter) {
    // Drop the handler frame that is be sitting on top of the actual
    // JavaScript frame. This is the case then OSR is triggered from bytecode.
    __ LeaveFrame(StackFrame::STUB);
  }

  // Load deoptimization data from the code object.
  // <deopt_data> = <code>[#deoptimization_data_offset]
  __ LoadProtectedPointerField(
      x1,
      FieldMemOperand(x0, Code::kDeoptimizationDataOrInterpreterDataOffset));

  // Load the OSR entrypoint offset from the deoptimization data.
  // <osr_offset> = <deopt_data>[#header_size + #osr_pc_offset]
  __ SmiUntagField(
      x1, FieldMemOperand(x1, TrustedFixedArray::OffsetOfElementAt(
                                  DeoptimizationData::kOsrPcOffsetIndex)));

  __ LoadCodeInstructionStart(x0, x0, kJSEntrypointTag);

  // Compute the target address = code_entry + osr_offset
  // <entry_addr> = <code_entry> + <osr_offset>
  Generate_OSREntry(masm, x0, x1);
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

  __ ldr(kContextRegister,
         MemOperand(fp, BaselineFrameConstants::kContextOffset));
  OnStackReplacement(masm, OsrSourceTier::kBaseline,
                     D::MaybeTargetCodeRegister());
}

#ifdef V8_ENABLE_MAGLEV

// static
void Builtins::Generate_MaglevFunctionEntryStackCheck(MacroAssembler* masm,
                                                      bool save_new_target) {
  // Input (x0): Stack size (Smi).
  // This builtin can be invoked just after Maglev's prologue.
  // All registers are available, except (possibly) new.target.
  ASM_CODE_COMMENT(masm);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ AssertSmi(x0);
    if (save_new_target) {
      if (PointerCompressionIsEnabled()) {
        __ AssertSmiOrHeapObjectInMainCompressionCage(
            kJavaScriptCallNewTargetRegister);
      }
      __ Push(kJavaScriptCallNewTargetRegister, padreg);
    }
    __ PushArgument(x0);
    __ CallRuntime(Runtime::kStackGuardWithGap, 1);
    if (save_new_target) {
      __ Pop(padreg, kJavaScriptCallNewTargetRegister);
    }
  }
  __ Ret();
}

#endif  // V8_ENABLE_MAGLEV

// static
void Builtins::Generate_FunctionPrototypeApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0       : argc
  //  -- sp[0]    : receiver
  //  -- sp[8]    : thisArg  (if argc >= 1)
  //  -- sp[16]   : argArray (if argc == 2)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_FunctionPrototypeApply");

  Register argc = x0;
  Register receiver = x1;
  Register arg_array = x2;
  Register this_arg = x3;
  Register undefined_value = x4;
  Register null_value = x5;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);
  __ LoadRoot(null_value, RootIndex::kNullValue);

  // 1. Load receiver into x1, argArray into x2 (if present), remove all
  // arguments from the stack (including the receiver), and push thisArg (if
  // present) instead.
  {
    Label done;
    __ Mov(this_arg, undefined_value);
    __ Mov(arg_array, undefined_value);
    __ Peek(receiver, 0);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(this_arg, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(arg_array, 2 * kSystemPointerSize);
    __ bind(&done);
  }
  __ DropArguments(argc);
  __ PushArgument(this_arg);

  // ----------- S t a t e -------------
  //  -- x2      : argArray
  //  -- x1      : receiver
  //  -- sp[0]   : thisArg
  // -----------------------------------

  // 2. We don't need to check explicitly for callable receiver here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Tail call with no arguments if argArray is null or undefined.
  Label no_arguments;
  __ CmpTagged(arg_array, null_value);
  __ CcmpTagged(arg_array, undefined_value, ZFlag, ne);
  __ B(eq, &no_arguments);

  // 4a. Apply the receiver to the given argArray.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);

  // 4b. The argArray is either null or undefined, so we tail call without any
  // arguments to the receiver.
  __ Bind(&no_arguments);
  {
    __ Mov(x0, JSParameterCount(0));
    DCHECK_EQ(receiver, x1);
    __ TailCallBuiltin(Builtins::Call());
  }
}

// static
void Builtins::Generate_FunctionPrototypeCall(MacroAssembler* masm) {
  Register argc = x0;
  Register function = x1;

  ASM_LOCATION("Builtins::Generate_FunctionPrototypeCall");

  // 1. Get the callable to call (passed as receiver) from the stack.
  __ Peek(function, __ ReceiverOperand());

  // 2. Handle case with no arguments.
  {
    Label non_zero;
    Register scratch = x10;
    __ Cmp(argc, JSParameterCount(0));
    __ B(gt, &non_zero);
    __ LoadRoot(scratch, RootIndex::kUndefinedValue);
    // Overwrite receiver with undefined, which will be the new receiver.
    // We do not need to overwrite the padding slot above it with anything.
    __ Poke(scratch, 0);
    // Call function. The argument count is already zero.
    __ TailCallBuiltin(Builtins::Call());
    __ Bind(&non_zero);
  }

  Label arguments_ready;
  // 3. Shift arguments. It depends if the arguments is even or odd.
  // That is if padding exists or not.
  {
    Label even;
    Register copy_from = x10;
    Register copy_to = x11;
    Register count = x12;
    UseScratchRegisterScope temps(masm);
    Register argc_without_receiver = temps.AcquireX();
    __ Sub(argc_without_receiver, argc, kJSArgcReceiverSlots);

    // CopyDoubleWords changes the count argument.
    __ Mov(count, argc_without_receiver);
    __ Tbz(argc_without_receiver, 0, &even);

    // Shift arguments one slot down on the stack (overwriting the original
    // receiver).
    __ SlotAddress(copy_from, 1);
    __ Sub(copy_to, copy_from, kSystemPointerSize);
    __ CopyDoubleWords(copy_to, copy_from, count);
    // Overwrite the duplicated remaining last argument.
    __ Poke(padreg, Operand(argc_without_receiver, LSL, kXRegSizeLog2));
    __ B(&arguments_ready);

    // Copy arguments one slot higher in memory, overwriting the original
    // receiver and padding.
    __ Bind(&even);
    __ SlotAddress(copy_from, count);
    __ Add(copy_to, copy_from, kSystemPointerSize);
    __ CopyDoubleWords(copy_to, copy_from, count,
                       MacroAssembler::kSrcLessThanDst);
    __ Drop(2);
  }

  // 5. Adjust argument count to make the original first argument the new
  //    receiver and call the callable.
  __ Bind(&arguments_ready);
  __ Sub(argc, argc, 1);
  __ TailCallBuiltin(Builtins::Call());
}

void Builtins::Generate_ReflectApply(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0     : argc
  //  -- sp[0]  : receiver
  //  -- sp[8]  : target         (if argc >= 1)
  //  -- sp[16] : thisArgument   (if argc >= 2)
  //  -- sp[24] : argumentsList  (if argc == 3)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_ReflectApply");

  Register argc = x0;
  Register arguments_list = x2;
  Register target = x1;
  Register this_argument = x4;
  Register undefined_value = x3;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into x1 (if present), argumentsList into x2 (if present),
  // remove all arguments from the stack (including the receiver), and push
  // thisArgument (if present) instead.
  {
    Label done;
    __ Mov(target, undefined_value);
    __ Mov(this_argument, undefined_value);
    __ Mov(arguments_list, undefined_value);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(target, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(this_argument, 2 * kSystemPointerSize);
    __ Cmp(argc, Immediate(JSParameterCount(3)));
    __ B(lt, &done);
    __ Peek(arguments_list, 3 * kSystemPointerSize);
    __ bind(&done);
  }
  __ DropArguments(argc);
  __ PushArgument(this_argument);

  // ----------- S t a t e -------------
  //  -- x2      : argumentsList
  //  -- x1      : target
  //  -- sp[0]   : thisArgument
  // -----------------------------------

  // 2. We don't need to check explicitly for callable target here,
  // since that's the first thing the Call/CallWithArrayLike builtins
  // will do.

  // 3. Apply the target to the given argumentsList.
  __ TailCallBuiltin(Builtin::kCallWithArrayLike);
}

void Builtins::Generate_ReflectConstruct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0       : argc
  //  -- sp[0]   : receiver
  //  -- sp[8]   : target
  //  -- sp[16]  : argumentsList
  //  -- sp[24]  : new.target (optional)
  // -----------------------------------

  ASM_LOCATION("Builtins::Generate_ReflectConstruct");

  Register argc = x0;
  Register arguments_list = x2;
  Register target = x1;
  Register new_target = x3;
  Register undefined_value = x4;

  __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);

  // 1. Load target into x1 (if present), argumentsList into x2 (if present),
  // new.target into x3 (if present, otherwise use target), remove all
  // arguments from the stack (including the receiver), and push thisArgument
  // (if present) instead.
  {
    Label done;
    __ Mov(target, undefined_value);
    __ Mov(arguments_list, undefined_value);
    __ Mov(new_target, undefined_value);
    __ Cmp(argc, Immediate(JSParameterCount(1)));
    __ B(lt, &done);
    __ Peek(target, kSystemPointerSize);
    __ B(eq, &done);
    __ Peek(arguments_list, 2 * kSystemPointerSize);
    __ Mov(new_target, target);  // new.target defaults to target
    __ Cmp(argc, Immediate(JSParameterCount(3)));
    __ B(lt, &done);
    __ Peek(new_target, 3 * kSystemPointerSize);
    __ bind(&done);
  }

  __ DropArguments(argc);

  // Push receiver (undefined).
  __ PushArgument(undefined_value);

  // ----------- S t a t e -------------
  //  -- x2      : argumentsList
  //  -- x1      : target
  //  -- x3      : new.target
  //  -- sp[0]   : receiver (undefined)
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

// Prepares the stack for copying the varargs. First we claim the necessary
// slots, taking care of potential padding. Then we copy the existing arguments
// one slot up or one slot down, as needed.
void Generate_PrepareForCopyingVarargs(MacroAssembler* masm, Register argc,
                                       Register len) {
  Label exit, even, init;
  Register slots_to_copy = x10;
  Register slots_to_claim = x12;

  __ Mov(slots_to_copy, argc);
  __ Mov(slots_to_claim, len);
  __ Tbz(slots_to_claim, 0, &even);

  // Claim space we need. If argc (without receiver) is even, slots_to_claim =
  // len + 1, as we need one extra padding slot. If argc (without receiver) is
  // odd, we know that the original arguments will have a padding slot we can
  // reuse (since len is odd), so slots_to_claim = len - 1.
  {
    Register scratch = x11;
    __ Add(slots_to_claim, len, 1);
    __ And(scratch, argc, 1);
    __ Sub(slots_to_claim, slots_to_claim, Operand(scratch, LSL, 1));
  }

  __ Bind(&even);
  __ Cbz(slots_to_claim, &exit);
  __ Claim(slots_to_claim);
  // An alignment slot may have been allocated above. If the number of stack
  // parameters is 0, the we have to initialize the alignment slot.
  __ Cbz(slots_to_copy, &init);

  // Move the arguments already in the stack including the receiver.
  {
    Register src = x11;
    Register dst = x12;
    __ SlotAddress(src, slots_to_claim);
    __ SlotAddress(dst, 0);
    __ CopyDoubleWords(dst, src, slots_to_copy);
    __ jmp(&exit);
  }
  // Initialize the alignment slot with a meaningful value. This is only
  // necessary if slots_to_copy is 0, because otherwise the alignment slot
  // already contains a valid value. In case slots_to_copy is even, then the
  // alignment slot contains the last parameter passed over the stack. In case
  // slots_to_copy is odd, then the alignment slot is that alignment slot when
  // CallVarArgs (or similar) was called, and already got initialized for that
  // call.
  {
    __ Bind(&init);
    // This code here is only reached when the number of stack parameters is 0.
    // In that case we have to initialize the alignment slot if there is one.
    __ Tbz(len, 0, &exit);
    __ Str(xzr, MemOperand(sp, len, LSL, kSystemPointerSizeLog2));
  }
  __ Bind(&exit);
}

}  // namespace

// static
// TODO(v8:11615): Observe Code::kMaxArguments in CallOrConstructVarargs
void Builtins::Generate_CallOrConstructVarargs(MacroAssembler* masm,
                                               Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- x1 : target
  //  -- x0 : number of parameters on the stack
  //  -- x2 : arguments list (a FixedArray)
  //  -- x4 : len (number of elements to push from args)
  //  -- x3 : new.target (for [[Construct]])
  // -----------------------------------
  if (v8_flags.debug_code) {
    // Allow x2 to be a FixedArray, or a FixedDoubleArray if x4 == 0.
    Label ok, fail;
    __ AssertNotSmi(x2, AbortReason::kOperandIsNotAFixedArray);
    __ LoadTaggedField(x10, FieldMemOperand(x2, HeapObject::kMapOffset));
    __ Ldrh(x13, FieldMemOperand(x10, Map::kInstanceTypeOffset));
    __ Cmp(x13, FIXED_ARRAY_TYPE);
    __ B(eq, &ok);
    __ Cmp(x13, FIXED_DOUBLE_ARRAY_TYPE);
    __ B(ne, &fail);
    __ Cmp(x4, 0);
    __ B(eq, &ok);
    // Fall through.
    __ bind(&fail);
    __ Abort(AbortReason::kOperandIsNotAFixedArray);

    __ bind(&ok);
  }

  Register arguments_list = x2;
  Register argc = x0;
  Register len = x4;

  Label stack_overflow;
  __ StackOverflowCheck(len, &stack_overflow);

  // Skip argument setup if we don't need to push any varargs.
  Label done;
  __ Cbz(len, &done);

  Generate_PrepareForCopyingVarargs(masm, argc, len);

  // Push varargs.
  {
    Label loop;
    Register src = x10;
    Register undefined_value = x12;
    Register scratch = x13;
    __ Add(src, arguments_list, FixedArray::kHeaderSize - kHeapObjectTag);
#if !V8_STATIC_ROOTS_BOOL
    // We do not use the CompareRoot macro without static roots as it would do a
    // LoadRoot behind the scenes and we want to avoid that in a loop.
    Register the_hole_value = x11;
    __ LoadTaggedRoot(the_hole_value, RootIndex::kTheHoleValue);
#endif  // !V8_STATIC_ROOTS_BOOL
    __ LoadRoot(undefined_value, RootIndex::kUndefinedValue);
    // TODO(all): Consider using Ldp and Stp.
    Register dst = x16;
    __ SlotAddress(dst, argc);
    __ Add(argc, argc, len);  // Update new argc.
    __ Bind(&loop);
    __ Sub(len, len, 1);
    __ LoadTaggedField(scratch, MemOperand(src, kTaggedSize, PostIndex));
#if V8_STATIC_ROOTS_BOOL
    __ CompareRoot(scratch, RootIndex::kTheHoleValue);
#else
    __ CmpTagged(scratch, the_hole_value);
#endif
    __ Csel(scratch, scratch, undefined_value, ne);
    __ Str(scratch, MemOperand(dst, kSystemPointerSize, PostIndex));
    __ Cbnz(len, &loop);
  }
  __ Bind(&done);
  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallOrConstructForwardVarargs(MacroAssembler* masm,
                                                      CallOrConstructMode mode,
                                                      Builtin target_builtin) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x3 : the new.target (for [[Construct]] calls)
  //  -- x1 : the target to call (can be any Object)
  //  -- x2 : start index (to support rest parameters)
  // -----------------------------------

  Register argc = x0;
  Register start_index = x2;

  // Check if new.target has a [[Construct]] internal method.
  if (mode == CallOrConstructMode::kConstruct) {
    Label new_target_constructor, new_target_not_constructor;
    __ JumpIfSmi(x3, &new_target_not_constructor);
    __ LoadTaggedField(x5, FieldMemOperand(x3, HeapObject::kMapOffset));
    __ Ldrb(x5, FieldMemOperand(x5, Map::kBitFieldOffset));
    __ TestAndBranchIfAnySet(x5, Map::Bits1::IsConstructorBit::kMask,
                             &new_target_constructor);
    __ Bind(&new_target_not_constructor);
    {
      FrameScope scope(masm, StackFrame::MANUAL);
      __ EnterFrame(StackFrame::INTERNAL);
      __ PushArgument(x3);
      __ CallRuntime(Runtime::kThrowNotConstructor);
      __ Unreachable();
    }
    __ Bind(&new_target_constructor);
  }

  Register len = x6;
  Label stack_done, stack_overflow;
  __ Ldr(len, MemOperand(fp, StandardFrameConstants::kArgCOffset));
  __ Subs(len, len, kJSArgcReceiverSlots);
  __ Subs(len, len, start_index);
  __ B(le, &stack_done);
  // Check for stack overflow.
  __ StackOverflowCheck(len, &stack_overflow);

  Generate_PrepareForCopyingVarargs(masm, argc, len);

  // Push varargs.
  {
    Register args_fp = x5;
    Register dst = x13;
    // Point to the fist argument to copy from (skipping receiver).
    __ Add(args_fp, fp,
           CommonFrameConstants::kFixedFrameSizeAboveFp + kSystemPointerSize);
    __ lsl(start_index, start_index, kSystemPointerSizeLog2);
    __ Add(args_fp, args_fp, start_index);
    // Point to the position to copy to.
    __ SlotAddress(dst, argc);
    // Update total number of arguments.
    __ Add(argc, argc, len);
    __ CopyDoubleWords(dst, args_fp, len);
  }

  __ Bind(&stack_done);
  // Tail-call to the actual Call or Construct builtin.
  __ TailCallBuiltin(target_builtin);

  __ Bind(&stack_overflow);
  __ TailCallRuntime(Runtime::kThrowStackOverflow);
}

// static
void Builtins::Generate_CallFunction(MacroAssembler* masm,
                                     ConvertReceiverMode mode) {
  ASM_LOCATION("Builtins::Generate_CallFunction");
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSFunction)
  // -----------------------------------
  __ AssertCallableFunction(x1);

  __ LoadTaggedField(
      x2, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));

  // Enter the context of the function; ToObject has to run in the function
  // context, and we also need to take the global proxy from the function
  // context in case of conversion.
  __ LoadTaggedField(cp, FieldMemOperand(x1, JSFunction::kContextOffset));
  // We need to convert the receiver for non-native sloppy mode functions.
  Label done_convert;
  __ Ldr(w3, FieldMemOperand(x2, SharedFunctionInfo::kFlagsOffset));
  __ TestAndBranchIfAnySet(w3,
                           SharedFunctionInfo::IsNativeBit::kMask |
                               SharedFunctionInfo::IsStrictBit::kMask,
                           &done_convert);
  {
    // ----------- S t a t e -------------
    //  -- x0 : the number of arguments
    //  -- x1 : the function to call (checked to be a JSFunction)
    //  -- x2 : the shared function info.
    //  -- cp : the function context.
    // -----------------------------------

    if (mode == ConvertReceiverMode::kNullOrUndefined) {
      // Patch receiver to global proxy.
      __ LoadGlobalProxy(x3);
    } else {
      Label convert_to_object, convert_receiver;
      __ Peek(x3, __ ReceiverOperand());
      __ JumpIfSmi(x3, &convert_to_object);
      __ JumpIfJSAnyIsNotPrimitive(x3, x4, &done_convert);
      if (mode != ConvertReceiverMode::kNotNullOrUndefined) {
        Label convert_global_proxy;
        __ JumpIfRoot(x3, RootIndex::kUndefinedValue, &convert_global_proxy);
        __ JumpIfNotRoot(x3, RootIndex::kNullValue, &convert_to_object);
        __ Bind(&convert_global_proxy);
        {
          // Patch receiver to global proxy.
          __ LoadGlobalProxy(x3);
        }
        __ B(&convert_receiver);
      }
      __ Bind(&convert_to_object);
      {
        // Convert receiver using ToObject.
        // TODO(bmeurer): Inline the allocation here to avoid building the frame
        // in the fast case? (fall back to AllocateInNewSpace?)
        FrameScope scope(masm, StackFrame::INTERNAL);
        __ SmiTag(x0);
        __ Push(padreg, x0, x1, cp);
        __ Mov(x0, x3);
        __ CallBuiltin(Builtin::kToObject);
        __ Mov(x3, x0);
        __ Pop(cp, x1, x0, padreg);
        __ SmiUntag(x0);
      }
      __ LoadTaggedField(
          x2, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
      __ Bind(&convert_receiver);
    }
    __ Poke(x3, __ ReceiverOperand());
  }
  __ Bind(&done_convert);

  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSFunction)
  //  -- x2 : the shared function info.
  //  -- cp : the function context.
  // -----------------------------------

  __ Ldrh(x2,
          FieldMemOperand(x2, SharedFunctionInfo::kFormalParameterCountOffset));
  __ InvokeFunctionCode(x1, no_reg, x2, x0, InvokeType::kJump);
}

namespace {

void Generate_PushBoundArguments(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : target (checked to be a JSBoundFunction)
  //  -- x3 : new.target (only in case of [[Construct]])
  // -----------------------------------

  Register bound_argc = x4;
  Register bound_argv = x2;

  // Load [[BoundArguments]] into x2 and length of that into x4.
  Label no_bound_arguments;
  __ LoadTaggedField(
      bound_argv, FieldMemOperand(x1, JSBoundFunction::kBoundArgumentsOffset));
  __ SmiUntagField(bound_argc,
                   FieldMemOperand(bound_argv, FixedArray::kLengthOffset));
  __ Cbz(bound_argc, &no_bound_arguments);
  {
    // ----------- S t a t e -------------
    //  -- x0 : the number of arguments
    //  -- x1 : target (checked to be a JSBoundFunction)
    //  -- x2 : the [[BoundArguments]] (implemented as FixedArray)
    //  -- x3 : new.target (only in case of [[Construct]])
    //  -- x4 : the number of [[BoundArguments]]
    // -----------------------------------

    Register argc = x0;

    // Check for stack overflow.
    {
      // Check the stack for overflow. We are not trying to catch interruptions
      // (i.e. debug break and preemption) here, so check the "real stack
      // limit".
      Label done;
      __ LoadStackLimit(x10, StackLimitKind::kRealStackLimit);
      // Make x10 the space we have left. The stack might already be overflowed
      // here which will cause x10 to become negative.
      __ Sub(x10, sp, x10);
      // Check if the arguments will overflow the stack.
      __ Cmp(x10, Operand(bound_argc, LSL, kSystemPointerSizeLog2));
      __ B(gt, &done);
      __ TailCallRuntime(Runtime::kThrowStackOverflow);
      __ Bind(&done);
    }

    Label copy_bound_args;
    Register total_argc = x15;
    Register slots_to_claim = x12;
    Register scratch = x10;
    Register receiver = x14;

    __ Sub(argc, argc, kJSArgcReceiverSlots);
    __ Add(total_argc, argc, bound_argc);
    __ Peek(receiver, 0);

    // Round up slots_to_claim to an even number if it is odd.
    __ Add(slots_to_claim, bound_argc, 1);
    __ Bic(slots_to_claim, slots_to_claim, 1);
    __ Claim(slots_to_claim, kSystemPointerSize);

    __ Tbz(bound_argc, 0, &copy_bound_args);
    {
      Label argc_even;
      __ Tbz(argc, 0, &argc_even);
      // Arguments count is odd (with the receiver it's even), so there's no
      // alignment padding above the arguments and we have to "add" it. We
      // claimed bound_argc + 1, since it is odd and it was rounded up. +1 here
      // is for stack alignment padding.
      // 1. Shift args one slot down.
      {
        Register copy_from = x11;
        Register copy_to = x12;
        __ SlotAddress(copy_to, slots_to_claim);
        __ Add(copy_from, copy_to, kSystemPointerSize);
        __ CopyDoubleWords(copy_to, copy_from, argc);
      }
      // 2. Write a padding in the last slot.
      __ Add(scratch, total_argc, 1);
      __ Str(padreg, MemOperand(sp, scratch, LSL, kSystemPointerSizeLog2));
      __ B(&copy_bound_args);

      __ Bind(&argc_even);
      // Arguments count is even (with the receiver it's odd), so there's an
      // alignment padding above the arguments and we can reuse it. We need to
      // claim bound_argc - 1, but we claimed bound_argc + 1, since it is odd
      // and it was rounded up.
      // 1. Drop 2.
      __ Drop(2);
      // 2. Shift args one slot up.
      {
        Register copy_from = x11;
        Register copy_to = x12;
        __ SlotAddress(copy_to, total_argc);
        __ Sub(copy_from, copy_to, kSystemPointerSize);
        __ CopyDoubleWords(copy_to, copy_from, argc,
                           MacroAssembler::kSrcLessThanDst);
      }
    }

    // If bound_argc is even, there is no alignment massage to do, and we have
    // already claimed the correct number of slots (bound_argc).
    __ Bind(&copy_bound_args);

    // Copy the receiver back.
    __ Poke(receiver, 0);
    // Copy [[BoundArguments]] to the stack (below the receiver).
    {
      Label loop;
      Register counter = bound_argc;
      Register copy_to = x12;
      __ Add(bound_argv, bound_argv, FixedArray::kHeaderSize - kHeapObjectTag);
      __ SlotAddress(copy_to, 1);
      __ Bind(&loop);
      __ Sub(counter, counter, 1);
      __ LoadTaggedField(scratch,
                         MemOperand(bound_argv, kTaggedSize, PostIndex));
      __ Str(scratch, MemOperand(copy_to, kSystemPointerSize, PostIndex));
      __ Cbnz(counter, &loop);
    }
    // Update argc.
    __ Add(argc, total_argc, kJSArgcReceiverSlots);
  }
  __ Bind(&no_bound_arguments);
}

}  // namespace

// static
void Builtins::Generate_CallBoundFunctionImpl(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSBoundFunction)
  // -----------------------------------
  __ AssertBoundFunction(x1);

  // Patch the receiver to [[BoundThis]].
  __ LoadTaggedField(x10,
                     FieldMemOperand(x1, JSBoundFunction::kBoundThisOffset));
  __ Poke(x10, __ ReceiverOperand());

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Call the [[BoundTargetFunction]] via the Call builtin.
  __ LoadTaggedField(
      x1, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtins::Call());
}

// static
void Builtins::Generate_Call(MacroAssembler* masm, ConvertReceiverMode mode) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the target to call (can be any Object).
  // -----------------------------------
  Register target = x1;
  Register map = x4;
  Register instance_type = x5;
  DCHECK(!AreAliased(x0, target, map, instance_type));

  Label non_callable, class_constructor;
  __ JumpIfSmi(target, &non_callable);
  __ LoadMap(map, target);
  __ CompareInstanceTypeRange(map, instance_type,
                              FIRST_CALLABLE_JS_FUNCTION_TYPE,
                              LAST_CALLABLE_JS_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtins::CallFunction(mode), ls);
  __ Cmp(instance_type, JS_BOUND_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtin::kCallBoundFunction, eq);

  // Check if target has a [[Call]] internal method.
  {
    Register flags = x4;
    __ Ldrb(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    map = no_reg;
    __ TestAndBranchIfAllClear(flags, Map::Bits1::IsCallableBit::kMask,
                               &non_callable);
  }

  // Check if target is a proxy and call CallProxy external builtin
  __ Cmp(instance_type, JS_PROXY_TYPE);
  __ TailCallBuiltin(Builtin::kCallProxy, eq);

  // Check if target is a wrapped function and call CallWrappedFunction external
  // builtin
  __ Cmp(instance_type, JS_WRAPPED_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtin::kCallWrappedFunction, eq);

  // ES6 section 9.2.1 [[Call]] ( thisArgument, argumentsList)
  // Check that the function is not a "classConstructor".
  __ Cmp(instance_type, JS_CLASS_CONSTRUCTOR_TYPE);
  __ B(eq, &class_constructor);

  // 2. Call to something else, which might have a [[Call]] internal method (if
  // not we raise an exception).
  // Overwrite the original receiver with the (original) target.
  __ Poke(target, __ ReceiverOperand());

  // Let the "call_as_function_delegate" take care of the rest.
  __ LoadNativeContextSlot(target, Context::CALL_AS_FUNCTION_DELEGATE_INDEX);
  __ TailCallBuiltin(
      Builtins::CallFunction(ConvertReceiverMode::kNotNullOrUndefined));

  // 3. Call to something that is not callable.
  __ bind(&non_callable);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ PushArgument(target);
    __ CallRuntime(Runtime::kThrowCalledNonCallable);
    __ Unreachable();
  }

  // 4. The function is a "classConstructor", need to raise an exception.
  __ bind(&class_constructor);
  {
    FrameScope frame(masm, StackFrame::INTERNAL);
    __ PushArgument(target);
    __ CallRuntime(Runtime::kThrowConstructorNonCallableError);
    __ Unreachable();
  }
}

// static
void Builtins::Generate_ConstructFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the constructor to call (checked to be a JSFunction)
  //  -- x3 : the new target (checked to be a constructor)
  // -----------------------------------
  __ AssertConstructor(x1);
  __ AssertFunction(x1);

  // Calling convention for function specific ConstructStubs require
  // x2 to contain either an AllocationSite or undefined.
  __ LoadRoot(x2, RootIndex::kUndefinedValue);

  Label call_generic_stub;

  // Jump to JSBuiltinsConstructStub or JSConstructStubGeneric.
  __ LoadTaggedField(
      x4, FieldMemOperand(x1, JSFunction::kSharedFunctionInfoOffset));
  __ Ldr(w4, FieldMemOperand(x4, SharedFunctionInfo::kFlagsOffset));
  __ TestAndBranchIfAllClear(
      w4, SharedFunctionInfo::ConstructAsBuiltinBit::kMask, &call_generic_stub);

  __ TailCallBuiltin(Builtin::kJSBuiltinsConstructStub);

  __ bind(&call_generic_stub);
  __ TailCallBuiltin(Builtin::kJSConstructStubGeneric);
}

// static
void Builtins::Generate_ConstructBoundFunction(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the function to call (checked to be a JSBoundFunction)
  //  -- x3 : the new target (checked to be a constructor)
  // -----------------------------------
  __ AssertConstructor(x1);
  __ AssertBoundFunction(x1);

  // Push the [[BoundArguments]] onto the stack.
  Generate_PushBoundArguments(masm);

  // Patch new.target to [[BoundTargetFunction]] if new.target equals target.
  {
    Label done;
    __ CmpTagged(x1, x3);
    __ B(ne, &done);
    __ LoadTaggedField(
        x3, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
    __ Bind(&done);
  }

  // Construct the [[BoundTargetFunction]] via the Construct builtin.
  __ LoadTaggedField(
      x1, FieldMemOperand(x1, JSBoundFunction::kBoundTargetFunctionOffset));
  __ TailCallBuiltin(Builtin::kConstruct);
}

// static
void Builtins::Generate_Construct(MacroAssembler* masm) {
  // ----------- S t a t e -------------
  //  -- x0 : the number of arguments
  //  -- x1 : the constructor to call (can be any Object)
  //  -- x3 : the new target (either the same as the constructor or
  //          the JSFunction on which new was invoked initially)
  // -----------------------------------
  Register target = x1;
  Register map = x4;
  Register instance_type = x5;
  DCHECK(!AreAliased(x0, target, map, instance_type));

  // Check if target is a Smi.
  Label non_constructor, non_proxy;
  __ JumpIfSmi(target, &non_constructor);

  // Check if target has a [[Construct]] internal method.
  __ LoadTaggedField(map, FieldMemOperand(target, HeapObject::kMapOffset));
  {
    Register flags = x2;
    DCHECK(!AreAliased(x0, target, map, instance_type, flags));
    __ Ldrb(flags, FieldMemOperand(map, Map::kBitFieldOffset));
    __ TestAndBranchIfAllClear(flags, Map::Bits1::IsConstructorBit::kMask,
                               &non_constructor);
  }

  // Dispatch based on instance type.
  __ CompareInstanceTypeRange(map, instance_type, FIRST_JS_FUNCTION_TYPE,
                              LAST_JS_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtin::kConstructFunction, ls);

  // Only dispatch to bound functions after checking whether they are
  // constructors.
  __ Cmp(instance_type, JS_BOUND_FUNCTION_TYPE);
  __ TailCallBuiltin(Builtin::kConstructBoundFunction, eq);

  // Only dispatch to proxies after checking whether they are constructors.
  __ Cmp(instance_type, JS_PROXY_TYPE);
  __ B(ne, &non_proxy);
  __ TailCallBuiltin(Builtin::kConstructProxy);

  // Called Construct on an exotic Object with a [[Construct]] internal method.
  __ bind(&non_proxy);
  {
    // Overwrite the original receiver with the (original) target.
    __ Poke(target, __ ReceiverOperand());

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

#if V8_ENABLE_WEBASSEMBLY
// Compute register lists for parameters to be saved. We save all parameter
// registers (see wasm-linkage.h). They might be overwritten in runtime
// calls. We don't have any callee-saved registers in wasm, so no need to
// store anything else.
constexpr RegList kSavedGpRegs = ([]() constexpr {
  RegList saved_gp_regs;
  for (Register gp_param_reg : wasm::kGpParamRegisters) {
    saved_gp_regs.set(gp_param_reg);
  }
  // The instance has already been stored in the fixed part of the frame.
  saved_gp_regs.clear(kWasmInstanceRegister);
  // All set registers were unique. The instance is skipped.
  CHECK_EQ(saved_gp_regs.Count(), arraysize(wasm::kGpParamRegisters) - 1);
  // We push a multiple of 16 bytes.
  CHECK_EQ(0, saved_gp_regs.Count() % 2);
  CHECK_EQ(WasmLiftoffSetupFrameConstants::kNumberOfSavedGpParamRegs,
           saved_gp_regs.Count());
  return saved_gp_regs;
})();

constexpr DoubleRegList kSavedFpRegs = ([]() constexpr {
  DoubleRegList saved_fp_regs;
  for (DoubleRegister fp_param_reg : wasm::kFpParamRegisters) {
    saved_fp_regs.set(fp_param_reg);
  }

  CHECK_EQ(saved_fp_regs.Count(), arraysize(wasm::kFpParamRegisters));
  CHECK_EQ(WasmLiftoffSetupFrameConstants::kNumberOfSavedFpParamRegs,
           saved_fp_regs.Count());
  return saved_fp_regs;
})();

// When entering this builtin, we have just created a Wasm stack frame:
//
// [   Wasm instance   ]  <-- sp
// [ WASM frame marker ]
// [     saved fp      ]  <-- fp
//
// Due to stack alignment restrictions, this builtin adds the feedback vector
// plus a filler to the stack. The stack pointer will be
// moved an appropriate distance by {PatchPrepareStackFrame}.
//
// [     (unused)      ]  <-- sp
// [  feedback vector  ]
// [   Wasm instance   ]
// [ WASM frame marker ]
// [     saved fp      ]  <-- fp
void Builtins::Generate_WasmLiftoffFrameSetup(MacroAssembler* masm) {
  Register func_index = wasm::kLiftoffFrameSetupFunctionReg;
  Register vector = x9;
  Register scratch = x10;
  Label allocate_vector, done;

  __ LoadTaggedField(
      vector, FieldMemOperand(kWasmInstanceRegister,
                              WasmTrustedInstanceData::kFeedbackVectorsOffset));
  __ Add(vector, vector, Operand(func_index, LSL, kTaggedSizeLog2));
  __ LoadTaggedField(vector, FieldMemOperand(vector, FixedArray::kHeaderSize));
  __ JumpIfSmi(vector, &allocate_vector);
  __ bind(&done);
  __ Push(vector, xzr);
  __ Ret();

  __ bind(&allocate_vector);
  // Feedback vector doesn't exist yet. Call the runtime to allocate it.
  // We temporarily change the frame type for this, because we need special
  // handling by the stack walker in case of GC.
  __ Mov(scratch, StackFrame::TypeToMarker(StackFrame::WASM_LIFTOFF_SETUP));
  __ Str(scratch, MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));
  // Save registers.
  __ PushXRegList(kSavedGpRegs);
  __ PushQRegList(kSavedFpRegs);
  __ Push<MacroAssembler::kSignLR>(lr, xzr);  // xzr is for alignment.

  // Arguments to the runtime function: instance, func_index, and an
  // additional stack slot for the NativeModule. The first pushed register
  // is for alignment. {x0} and {x1} are picked arbitrarily.
  __ SmiTag(func_index);
  __ Push(x0, kWasmInstanceRegister, func_index, x1);
  __ Mov(cp, Smi::zero());
  __ CallRuntime(Runtime::kWasmAllocateFeedbackVector, 3);
  __ Mov(vector, kReturnRegister0);

  // Restore registers and frame type.
  __ Pop<MacroAssembler::kAuthLR>(xzr, lr);
  __ PopQRegList(kSavedFpRegs);
  __ PopXRegList(kSavedGpRegs);
  // Restore the instance from the frame.
  __ Ldr(kWasmInstanceRegister,
         MemOperand(fp, WasmFrameConstants::kWasmInstanceOffset));
  __ Mov(scratch, StackFrame::TypeToMarker(StackFrame::WASM));
  __ Str(scratch, MemOperand(fp, TypedFrameConstants::kFrameTypeOffset));
  __ B(&done);
}

void Builtins::Generate_WasmCompileLazy(MacroAssembler* masm) {
  // The function index was put in w8 by the jump table trampoline.
  // Sign extend and convert to Smi for the runtime call.
  __ sxtw(kWasmCompileLazyFuncIndexRegister,
          kWasmCompileLazyFuncIndexRegister.W());
  __ SmiTag(kWasmCompileLazyFuncIndexRegister);

  UseScratchRegisterScope temps(masm);
  temps.Exclude(x17);
  {
    HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
    FrameScope scope(masm, StackFrame::INTERNAL);
    // Manually save the instance (which kSavedGpRegs skips because its
    // other use puts it into the fixed frame anyway). The stack slot is valid
    // because the {FrameScope} (via {EnterFrame}) always reserves it (for stack
    // alignment reasons). The instance is needed because once this builtin is
    // done, we'll call a regular Wasm function.
    __ Str(kWasmInstanceRegister,
           MemOperand(fp, WasmFrameConstants::kWasmInstanceOffset));

    // Save registers that we need to keep alive across the runtime call.
    __ PushXRegList(kSavedGpRegs);
    __ PushQRegList(kSavedFpRegs);

    __ Push(kWasmInstanceRegister, kWasmCompileLazyFuncIndexRegister);
    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Mov(cp, Smi::zero());
    __ CallRuntime(Runtime::kWasmCompileLazy, 2);

    // Untag the returned Smi into into x17 (ip1), for later use.
    static_assert(!kSavedGpRegs.has(x17));
    __ SmiUntag(x17, kReturnRegister0);

    // Restore registers.
    __ PopQRegList(kSavedFpRegs);
    __ PopXRegList(kSavedGpRegs);
    // Restore the instance from the frame.
    __ Ldr(kWasmInstanceRegister,
           MemOperand(fp, WasmFrameConstants::kWasmInstanceOffset));
  }

  // The runtime function returned the jump table slot offset as a Smi (now in
  // x17). Use that to compute the jump target. Use x17 (ip1) for the branch
  // target, to be compliant with CFI.
  constexpr Register temp = x8;
  static_assert(!kSavedGpRegs.has(temp));
  __ ldr(temp, FieldMemOperand(kWasmInstanceRegister,
                               WasmTrustedInstanceData::kJumpTableStartOffset));
  __ add(x17, temp, Operand(x17));
  // Finally, jump to the jump table slot for the function.
  __ Jump(x17);
}

void Builtins::Generate_WasmDebugBreak(MacroAssembler* masm) {
  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  {
    FrameScope scope(masm, StackFrame::WASM_DEBUG_BREAK);

    // Save all parameter registers. They might hold live values, we restore
    // them after the runtime call.
    __ PushXRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
    __ PushQRegList(WasmDebugBreakFrameConstants::kPushedFpRegs);

    // Initialize the JavaScript context with 0. CEntry will use it to
    // set the current context on the isolate.
    __ Move(cp, Smi::zero());
    __ CallRuntime(Runtime::kWasmDebugBreak, 0);

    // Restore registers.
    __ PopQRegList(WasmDebugBreakFrameConstants::kPushedFpRegs);
    __ PopXRegList(WasmDebugBreakFrameConstants::kPushedGpRegs);
  }
  __ Ret();
}

namespace {
// Check that the stack was in the old state (if generated code assertions are
// enabled), and switch to the new state.
void SwitchStackState(MacroAssembler* masm, Register jmpbuf,
                      Register tmp,
                      wasm::JumpBuffer::StackState old_state,
                      wasm::JumpBuffer::StackState new_state) {
  if (v8_flags.debug_code) {
    __ Ldr(tmp.W(), MemOperand(jmpbuf, wasm::kJmpBufStateOffset));
    __ Cmp(tmp.W(), old_state);
    Label ok;
    __ B(&ok, eq);
    __ Trap();
    __ bind(&ok);
  }
  __ Mov(tmp.W(), new_state);
  __ Str(tmp.W(), MemOperand(jmpbuf, wasm::kJmpBufStateOffset));
}

// Switch the stack pointer. Also switch the simulator's stack limit when
// running on the simulator. This needs to be done as close as possible to
// changing the stack pointer, as a mismatch between the stack pointer and the
// simulator's stack limit can cause stack access check failures.
void SwitchStackPointerAndSimulatorStackLimit(MacroAssembler* masm,
                                              Register jmpbuf, Register tmp) {
  if (masm->options().enable_simulator_code) {
    UseScratchRegisterScope temps(masm);
    temps.Exclude(x16);
    __ Ldr(tmp, MemOperand(jmpbuf, wasm::kJmpBufSpOffset));
    __ Ldr(x16, MemOperand(jmpbuf, wasm::kJmpBufStackLimitOffset));
    __ Mov(sp, tmp);
    __ hlt(kImmExceptionIsSwitchStackLimit);
  } else {
    __ Ldr(tmp, MemOperand(jmpbuf, wasm::kJmpBufSpOffset));
    __ Mov(sp, tmp);
  }
}

void FillJumpBuffer(MacroAssembler* masm, Register jmpbuf, Label* pc,
                    Register tmp) {
  __ Mov(tmp, sp);
  __ Str(tmp, MemOperand(jmpbuf, wasm::kJmpBufSpOffset));
  __ Str(fp, MemOperand(jmpbuf, wasm::kJmpBufFpOffset));
  __ LoadStackLimit(tmp, StackLimitKind::kRealStackLimit);
  __ Str(tmp, MemOperand(jmpbuf, wasm::kJmpBufStackLimitOffset));
  __ Adr(tmp, pc);
  __ Str(tmp, MemOperand(jmpbuf, wasm::kJmpBufPcOffset));
}

void LoadJumpBuffer(MacroAssembler* masm, Register jmpbuf, bool load_pc,
                    Register tmp) {
  SwitchStackPointerAndSimulatorStackLimit(masm, jmpbuf, tmp);
  __ Ldr(fp, MemOperand(jmpbuf, wasm::kJmpBufFpOffset));
  SwitchStackState(masm, jmpbuf, tmp, wasm::JumpBuffer::Inactive,
                   wasm::JumpBuffer::Active);
  if (load_pc) {
    __ Ldr(tmp, MemOperand(jmpbuf, wasm::kJmpBufPcOffset));
    __ Br(tmp);
  }
  // The stack limit in StackGuard is set separately under the ExecutionAccess
  // lock.
}

void SaveState(MacroAssembler* masm, Register active_continuation,
               Register tmp, Label* suspend) {
  Register jmpbuf = tmp;
  __ LoadExternalPointerField(
      jmpbuf,
      FieldMemOperand(active_continuation,
                      WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  UseScratchRegisterScope temps(masm);
  Register scratch = temps.AcquireX();
  FillJumpBuffer(masm, jmpbuf, suspend, scratch);
}

void LoadTargetJumpBuffer(MacroAssembler* masm, Register target_continuation,
                          Register tmp) {
  Register target_jmpbuf = target_continuation;
  __ LoadExternalPointerField(
      target_jmpbuf,
      FieldMemOperand(target_continuation,
                      WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  __ Str(xzr,
         MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset));
  // Switch stack!
  LoadJumpBuffer(masm, target_jmpbuf, false, tmp);
}

// Updates the stack limit to match the new active stack.
// Pass the {finished_continuation} argument to indicate that the stack that we
// are switching from has returned, and in this case return its memory to the
// stack pool.
void SwitchStacks(MacroAssembler* masm, Register finished_continuation,
                  const CPURegister& keep1 = NoReg,
                  const CPURegister& keep2 = padreg,
                  const CPURegister& keep3 = NoReg) {
  using ER = ExternalReference;
  __ Push(keep1, keep2);
  if (keep3 != NoReg) {
    __ Push(keep3, padreg);
  }
  if (finished_continuation != no_reg) {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ Mov(kCArgRegs[0], ExternalReference::isolate_address(masm->isolate()));
    __ Mov(kCArgRegs[1], finished_continuation);
    __ CallCFunction(ER::wasm_return_switch(), 2);
  } else {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ Mov(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::wasm_sync_stack_limit(), 1);
  }
  if (keep3 != NoReg) {
    __ Pop(padreg, keep3);
  }
  __ Pop(keep2, keep1);
}

void ReloadParentContinuation(MacroAssembler* masm, Register return_reg,
                              Register return_value, Register context,
                              Register tmp1, Register tmp2, Register tmp3) {
  Register active_continuation = tmp1;
  __ LoadRoot(active_continuation, RootIndex::kActiveContinuation);

  // Set a null pointer in the jump buffer's SP slot to indicate to the stack
  // frame iterator that this stack is empty.
  Register jmpbuf = tmp2;
  __ LoadExternalPointerField(
      jmpbuf,
      FieldMemOperand(active_continuation,
                      WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  __ Str(xzr, MemOperand(jmpbuf, wasm::kJmpBufSpOffset));
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    SwitchStackState(masm, jmpbuf, scratch, wasm::JumpBuffer::Active,
                     wasm::JumpBuffer::Retired);
  }
  Register parent = tmp2;
  __ LoadTaggedField(parent,
                     FieldMemOperand(active_continuation,
                                     WasmContinuationObject::kParentOffset));

  // Update active continuation root.
  int32_t active_continuation_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveContinuation);
  __ Str(parent, MemOperand(kRootRegister, active_continuation_offset));
  jmpbuf = parent;
  __ LoadExternalPointerField(
      jmpbuf, FieldMemOperand(parent, WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);

  // Switch stack!
  LoadJumpBuffer(masm, jmpbuf, false, tmp3);

  SwitchStacks(masm, active_continuation, return_reg, return_value, context);
}

void RestoreParentSuspender(MacroAssembler* masm, Register tmp1,
                            Register tmp2) {
  Register suspender = tmp1;
  __ LoadRoot(suspender, RootIndex::kActiveSuspender);
  MemOperand state_loc =
    FieldMemOperand(suspender, WasmSuspenderObject::kStateOffset);
  __ Move(tmp2, Smi::FromInt(WasmSuspenderObject::kInactive));
  __ StoreTaggedField(tmp2, state_loc);
  __ LoadTaggedField(
      suspender,
      FieldMemOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ CompareRoot(suspender, RootIndex::kUndefinedValue);
  Label undefined;
  __ B(&undefined, eq);
  if (v8_flags.debug_code) {
    // Check that the parent suspender is active.
    Label parent_inactive;
    Register state = tmp2;
    __ SmiUntag(state, state_loc);
    __ cmp(state, WasmSuspenderObject::kActive);
    __ B(&parent_inactive, eq);
    __ Trap();
    __ bind(&parent_inactive);
  }
  __ Move(tmp2, Smi::FromInt(WasmSuspenderObject::kActive));
  __ StoreTaggedField(tmp2, state_loc);
  __ bind(&undefined);
  int32_t active_suspender_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveSuspender);
  __ Str(suspender, MemOperand(kRootRegister, active_suspender_offset));
}

void ResetStackSwitchFrameStackSlots(MacroAssembler* masm) {
  __ Str(xzr, MemOperand(fp, StackSwitchFrameConstants::kResultArrayOffset));
  __ Str(xzr, MemOperand(fp, StackSwitchFrameConstants::kRefOffset));
}

// TODO(irezvov): Consolidate with arm RegisterAllocator.
class RegisterAllocator {
 public:
  class Scoped {
   public:
    Scoped(RegisterAllocator* allocator, Register* reg):
      allocator_(allocator), reg_(reg) {}
    ~Scoped() { allocator_->Free(reg_); }
   private:
    RegisterAllocator* allocator_;
    Register* reg_;
  };

  explicit RegisterAllocator(const CPURegList& registers)
      : initial_(registers),
        available_(registers) {}
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

  void Reserve(const Register& reg1,
               const Register& reg2,
               const Register& reg3 = NoReg,
               const Register& reg4 = NoReg,
               const Register& reg5 = NoReg,
               const Register& reg6 = NoReg) {
    Reserve(reg1);
    Reserve(reg2);
    Reserve(reg3);
    Reserve(reg4);
    Reserve(reg5);
    Reserve(reg6);
  }

  bool IsUsed(const Register& reg) {
    return initial_.IncludesAliasOf(reg)
      && !available_.IncludesAliasOf(reg);
  }

  void ResetExcept(const Register& reg1 = NoReg,
                   const Register& reg2 = NoReg,
                   const Register& reg3 = NoReg,
                   const Register& reg4 = NoReg,
                   const Register& reg5 = NoReg,
                   const Register& reg6 = NoReg) {
    available_ = initial_;
    if (reg1 != NoReg) {
      available_.Remove(reg1, reg2, reg3, reg4);
    }
    if (reg5 != NoReg) {
      available_.Remove(reg5, reg6);
    }
    auto it = allocated_registers_.begin();
    while (it != allocated_registers_.end()) {
      if (available_.IncludesAliasOf(**it)) {
        **it = no_reg;
        it = allocated_registers_.erase(it);
      } else {
        it++;
      }
    }
  }

  static RegisterAllocator WithAllocatableGeneralRegisters() {
    CPURegList list(kXRegSizeInBits, RegList());
    const RegisterConfiguration* config(RegisterConfiguration::Default());
    list.set_bits(config->allocatable_general_codes_mask());
    return RegisterAllocator(list);
  }

 private:
  std::vector<Register*> allocated_registers_;
  const CPURegList initial_;
  CPURegList available_;
};

#define DEFINE_REG(Name) \
  Register Name = no_reg; \
  regs.Ask(&Name);

#define DEFINE_REG_W(Name) \
  DEFINE_REG(Name); \
  Name = Name.W();

#define ASSIGN_REG(Name) \
  regs.Ask(&Name);

#define ASSIGN_REG_W(Name) \
  ASSIGN_REG(Name); \
  Name = Name.W();

#define DEFINE_PINNED(Name, Reg) \
  Register Name = no_reg; \
  regs.Pinned(Reg, &Name);

#define ASSIGN_PINNED(Name, Reg) regs.Pinned(Reg, &Name);

#define DEFINE_SCOPED(Name) \
  DEFINE_REG(Name) \
  RegisterAllocator::Scoped scope_##Name(&regs, &Name);

#define FREE_REG(Name) regs.Free(&Name);

// Loads the context field of the WasmTrustedInstanceData or WasmImportData
// depending on the ref's type, and places the result in the input register.
void GetContextFromRef(MacroAssembler* masm, Register ref, Register scratch) {
  __ LoadTaggedField(scratch, FieldMemOperand(ref, HeapObject::kMapOffset));
  __ CompareInstanceType(scratch, scratch, WASM_TRUSTED_INSTANCE_DATA_TYPE);
  Label instance;
  Label end;
  __ B(eq, &instance);
  __ LoadTaggedField(
      ref, FieldMemOperand(ref, WasmImportData::kNativeContextOffset));
  __ jmp(&end);
  __ bind(&instance);
  __ LoadTaggedField(
      ref, FieldMemOperand(ref, WasmTrustedInstanceData::kNativeContextOffset));
  __ bind(&end);
}

}  // namespace

void Builtins::Generate_WasmToJsWrapperAsm(MacroAssembler* masm) {
  // Push registers in reverse order so that they are on the stack like
  // in an array, with the first item being at the lowest address.
  __ Push(wasm::kFpParamRegisters[7], wasm::kFpParamRegisters[6],
          wasm::kFpParamRegisters[5], wasm::kFpParamRegisters[4]);
  __ Push(wasm::kFpParamRegisters[3], wasm::kFpParamRegisters[2],
          wasm::kFpParamRegisters[1], wasm::kFpParamRegisters[0]);

  __ Push(wasm::kGpParamRegisters[6], wasm::kGpParamRegisters[5],
          wasm::kGpParamRegisters[4], wasm::kGpParamRegisters[3]);
  __ Push(wasm::kGpParamRegisters[2], wasm::kGpParamRegisters[1]);
  // Push four more slots that will be used as fixed spill slots in the torque
  // wrapper. Two slots for stack-switching (central stack pointer and secondary
  // stack limit), one for the signature, and one for stack alignment.
  __ Push(xzr, xzr, xzr, xzr);
  __ TailCallBuiltin(Builtin::kWasmToJsWrapperCSA);
}

void Builtins::Generate_WasmTrapHandlerLandingPad(MacroAssembler* masm) {
  __ Add(lr, kWasmTrapHandlerFaultAddressRegister,
         WasmFrameConstants::kProtectedInstructionReturnAddressOffset);
  __ TailCallBuiltin(Builtin::kWasmTrapHandlerThrowTrap);
}

void Builtins::Generate_WasmSuspend(MacroAssembler* masm) {
  auto regs = RegisterAllocator::WithAllocatableGeneralRegisters();
  // Set up the stackframe.
  __ EnterFrame(StackFrame::STACK_SWITCH);

  DEFINE_PINNED(suspender, x0);
  DEFINE_PINNED(context, kContextRegister);

  __ Sub(sp, sp,
         Immediate(StackSwitchFrameConstants::kNumSpillSlots *
                   kSystemPointerSize));
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  // -------------------------------------------
  // Save current state in active jump buffer.
  // -------------------------------------------
  Label resume;
  DEFINE_REG(continuation);
  __ LoadRoot(continuation, RootIndex::kActiveContinuation);
  DEFINE_REG(jmpbuf);
  DEFINE_REG(scratch);
  __ LoadExternalPointerField(
      jmpbuf,
      FieldMemOperand(continuation, WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  FillJumpBuffer(masm, jmpbuf, &resume, scratch);
  SwitchStackState(masm, jmpbuf, scratch, wasm::JumpBuffer::Active,
                   wasm::JumpBuffer::Inactive);
  __ Move(scratch, Smi::FromInt(WasmSuspenderObject::kSuspended));
  __ StoreTaggedField(
      scratch,
      FieldMemOperand(suspender, WasmSuspenderObject::kStateOffset));
  regs.ResetExcept(suspender, continuation);

  DEFINE_REG(suspender_continuation);
  __ LoadTaggedField(
      suspender_continuation,
      FieldMemOperand(suspender, WasmSuspenderObject::kContinuationOffset));
  if (v8_flags.debug_code) {
    // -------------------------------------------
    // Check that the suspender's continuation is the active continuation.
    // -------------------------------------------
    // TODO(thibaudm): Once we add core stack-switching instructions, this
    // check will not hold anymore: it's possible that the active continuation
    // changed (due to an internal switch), so we have to update the suspender.
    __ cmp(suspender_continuation, continuation);
    Label ok;
    __ B(&ok, eq);
    __ Trap();
    __ bind(&ok);
  }
  FREE_REG(continuation);
  // -------------------------------------------
  // Update roots.
  // -------------------------------------------
  DEFINE_REG(caller);
  __ LoadTaggedField(caller,
                     FieldMemOperand(suspender_continuation,
                                     WasmContinuationObject::kParentOffset));
  int32_t active_continuation_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveContinuation);
  __ Str(caller, MemOperand(kRootRegister, active_continuation_offset));
  DEFINE_REG(parent);
  __ LoadTaggedField(
      parent, FieldMemOperand(suspender, WasmSuspenderObject::kParentOffset));
  int32_t active_suspender_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveSuspender);
  __ Str(parent, MemOperand(kRootRegister, active_suspender_offset));
  regs.ResetExcept(suspender, caller);

  // -------------------------------------------
  // Load jump buffer.
  // -------------------------------------------
  SwitchStacks(masm, no_reg, caller, suspender);
  ASSIGN_REG(jmpbuf);
  __ LoadExternalPointerField(
      jmpbuf, FieldMemOperand(caller, WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  __ LoadTaggedField(
      kReturnRegister0,
      FieldMemOperand(suspender, WasmSuspenderObject::kPromiseOffset));
  MemOperand GCScanSlotPlace =
      MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Str(xzr, GCScanSlotPlace);
  ASSIGN_REG(scratch)
  LoadJumpBuffer(masm, jmpbuf, true, scratch);
  __ Trap();
  __ Bind(&resume, BranchTargetIdentifier::kBtiJump);
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  __ Ret(lr);
}

namespace {
// Resume the suspender stored in the closure. We generate two variants of this
// builtin: the onFulfilled variant resumes execution at the saved PC and
// forwards the value, the onRejected variant throws the value.

void Generate_WasmResumeHelper(MacroAssembler* masm, wasm::OnResume on_resume) {
  auto regs = RegisterAllocator::WithAllocatableGeneralRegisters();
  __ EnterFrame(StackFrame::STACK_SWITCH);

  DEFINE_PINNED(closure, kJSFunctionRegister);  // x1

  __ Sub(sp, sp,
         Immediate(StackSwitchFrameConstants::kNumSpillSlots *
                   kSystemPointerSize));
  // Set a sentinel value for the spill slots visited by the GC.
  ResetStackSwitchFrameStackSlots(masm);

  regs.ResetExcept(closure);

  // -------------------------------------------
  // Load suspender from closure.
  // -------------------------------------------
  DEFINE_REG(sfi);
  __ LoadTaggedField(
      sfi,
      MemOperand(
          closure,
          wasm::ObjectAccess::SharedFunctionInfoOffsetInTaggedJSFunction()));
  FREE_REG(closure);
  // Suspender should be ObjectRegister register to be used in
  // RecordWriteField calls later.
  DEFINE_PINNED(suspender, WriteBarrierDescriptor::ObjectRegister());
  DEFINE_REG(resume_data);
  __ LoadTaggedField(
      resume_data,
      FieldMemOperand(sfi, SharedFunctionInfo::kUntrustedFunctionDataOffset));
  // The write barrier uses a fixed register for the host object (rdi). The next
  // barrier is on the suspender, so load it in rdi directly.
  __ LoadTaggedField(
      suspender,
      FieldMemOperand(resume_data, WasmResumeData::kSuspenderOffset));
  // Check the suspender state.
  Label suspender_is_suspended;
  DEFINE_REG(state);
  __ SmiUntag(state,
              FieldMemOperand(suspender, WasmSuspenderObject::kStateOffset));
  __ cmp(state, WasmSuspenderObject::kSuspended);
  __ B(&suspender_is_suspended, eq);
  __ Trap();

  regs.ResetExcept(suspender);

  __ bind(&suspender_is_suspended);
  // -------------------------------------------
  // Save current state.
  // -------------------------------------------
  Label suspend;
  DEFINE_REG(active_continuation);
  __ LoadRoot(active_continuation, RootIndex::kActiveContinuation);
  DEFINE_REG(current_jmpbuf);
  DEFINE_REG(scratch);
  __ LoadExternalPointerField(
      current_jmpbuf,
      FieldMemOperand(active_continuation,
                      WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  FillJumpBuffer(masm, current_jmpbuf, &suspend, scratch);
  SwitchStackState(masm, current_jmpbuf, scratch, wasm::JumpBuffer::Active,
                   wasm::JumpBuffer::Inactive);
  FREE_REG(current_jmpbuf);

  // -------------------------------------------
  // Set the suspender and continuation parents and update the roots
  // -------------------------------------------
  DEFINE_REG(active_suspender);
  __ LoadRoot(active_suspender, RootIndex::kActiveSuspender);
  __ StoreTaggedField(
      active_suspender,
      FieldMemOperand(suspender, WasmSuspenderObject::kParentOffset));
  __ RecordWriteField(suspender, WasmSuspenderObject::kParentOffset,
                      active_suspender, kLRHasBeenSaved,
                      SaveFPRegsMode::kIgnore);
  __ Move(scratch, Smi::FromInt(WasmSuspenderObject::kActive));
  __ StoreTaggedField(
      scratch,
      FieldMemOperand(suspender, WasmSuspenderObject::kStateOffset));
  int32_t active_suspender_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveSuspender);
  __ Str(suspender, MemOperand(kRootRegister, active_suspender_offset));

  // Next line we are going to load a field from suspender, but we have to use
  // the same register for target_continuation to use it in RecordWriteField.
  // So, free suspender here to use pinned reg, but load from it next line.
  FREE_REG(suspender);
  DEFINE_PINNED(target_continuation, WriteBarrierDescriptor::ObjectRegister());
  suspender = target_continuation;
  __ LoadTaggedField(
      target_continuation,
      FieldMemOperand(suspender, WasmSuspenderObject::kContinuationOffset));
  suspender = no_reg;

  __ StoreTaggedField(
      active_continuation,
      FieldMemOperand(target_continuation,
                      WasmContinuationObject::kParentOffset));
  __ RecordWriteField(
      target_continuation, WasmContinuationObject::kParentOffset,
      active_continuation, kLRHasBeenSaved, SaveFPRegsMode::kIgnore);
  FREE_REG(active_continuation);
  int32_t active_continuation_offset =
      MacroAssembler::RootRegisterOffsetForRootIndex(
          RootIndex::kActiveContinuation);
  __ Str(target_continuation,
         MemOperand(kRootRegister, active_continuation_offset));

  SwitchStacks(masm, no_reg, target_continuation);

  regs.ResetExcept(target_continuation);

  // -------------------------------------------
  // Load state from target jmpbuf (longjmp).
  // -------------------------------------------
  regs.Reserve(kReturnRegister0);
  DEFINE_REG(target_jmpbuf);
  ASSIGN_REG(scratch);
  __ LoadExternalPointerField(
      target_jmpbuf,
      FieldMemOperand(target_continuation,
                      WasmContinuationObject::kJmpbufOffset),
      kWasmContinuationJmpbufTag);
  // Move resolved value to return register.
  __ Ldr(kReturnRegister0, MemOperand(fp, 3 * kSystemPointerSize));
  MemOperand GCScanSlotPlace =
      MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset);
  __ Str(xzr, GCScanSlotPlace);
  if (on_resume == wasm::OnResume::kThrow) {
    // Switch to the continuation's stack without restoring the PC.
    LoadJumpBuffer(masm, target_jmpbuf, false, scratch);
    // Pop this frame now. The unwinder expects that the first STACK_SWITCH
    // frame is the outermost one.
    __ LeaveFrame(StackFrame::STACK_SWITCH);
    // Forward the onRejected value to kThrow.
    __ Push(xzr, kReturnRegister0);
    __ CallRuntime(Runtime::kThrow);
  } else {
    // Resume the continuation normally.
    LoadJumpBuffer(masm, target_jmpbuf, true, scratch);
  }
  __ Trap();
  __ Bind(&suspend, BranchTargetIdentifier::kBtiJump);
  __ LeaveFrame(StackFrame::STACK_SWITCH);
  // Pop receiver + parameter.
  __ DropArguments(2);
  __ Ret(lr);
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
void SwitchToAllocatedStack(MacroAssembler* masm, RegisterAllocator& regs,
                            Register wasm_instance, Register wrapper_buffer,
                            Register& original_fp, Register& new_wrapper_buffer,
                            Label* suspend) {
  ResetStackSwitchFrameStackSlots(masm);
  DEFINE_SCOPED(scratch)
  DEFINE_REG(target_continuation)
  __ LoadRoot(target_continuation, RootIndex::kActiveContinuation);
  DEFINE_REG(parent_continuation)
  __ LoadTaggedField(parent_continuation,
                     FieldMemOperand(target_continuation,
                                     WasmContinuationObject::kParentOffset));
  SaveState(masm, parent_continuation, scratch, suspend);
  SwitchStacks(masm, no_reg, wasm_instance, wrapper_buffer);
  FREE_REG(parent_continuation);
  // Save the old stack's fp in x9, and use it to access the parameters in
  // the parent frame.
  regs.Pinned(x9, &original_fp);
  __ Mov(original_fp, fp);
  __ LoadRoot(target_continuation, RootIndex::kActiveContinuation);
  LoadTargetJumpBuffer(masm, target_continuation, scratch);
  FREE_REG(target_continuation);
  // Push the loaded fp. We know it is null, because there is no frame yet,
  // so we could also push 0 directly. In any case we need to push it,
  // because this marks the base of the stack segment for
  // the stack frame iterator.
  __ EnterFrame(StackFrame::STACK_SWITCH);
  int stack_space =
      RoundUp(StackSwitchFrameConstants::kNumSpillSlots * kSystemPointerSize +
                  JSToWasmWrapperFrameConstants::kWrapperBufferSize,
              16);
  __ Sub(sp, sp, Immediate(stack_space));
  ASSIGN_REG(new_wrapper_buffer)
  __ Mov(new_wrapper_buffer, sp);
  // Copy data needed for return handling from old wrapper buffer to new one.
  // kWrapperBufferRefReturnCount will be copied too, because 8 bytes are copied
  // at the same time.
  static_assert(JSToWasmWrapperFrameConstants::kWrapperBufferRefReturnCount ==
                JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount + 4);
  __ Ldr(scratch,
         MemOperand(wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount));
  __ Str(scratch,
         MemOperand(new_wrapper_buffer,
                    JSToWasmWrapperFrameConstants::kWrapperBufferReturnCount));
  __ Ldr(
      scratch,
      MemOperand(
          wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray));
  __ Str(
      scratch,
      MemOperand(
          new_wrapper_buffer,
          JSToWasmWrapperFrameConstants::kWrapperBufferSigRepresentationArray));
}

void SwitchBackAndReturnPromise(MacroAssembler* masm, RegisterAllocator& regs,
                                Label* return_promise) {
  regs.ResetExcept();
  // The return value of the wasm function becomes the parameter of the
  // FulfillPromise builtin, and the promise is the return value of this
  // wrapper.
  static const Builtin_FulfillPromise_InterfaceDescriptor desc;
  DEFINE_PINNED(promise, desc.GetRegisterParameter(0));
  DEFINE_PINNED(return_value, desc.GetRegisterParameter(1));
  __ Move(return_value, kReturnRegister0);
  DEFINE_SCOPED(tmp);
  DEFINE_SCOPED(tmp2);
  DEFINE_SCOPED(tmp3);
  __ LoadRoot(promise, RootIndex::kActiveSuspender);
  __ LoadTaggedField(
      promise, FieldMemOperand(promise, WasmSuspenderObject::kPromiseOffset));

  __ Ldr(kContextRegister,
         MemOperand(fp, StackSwitchFrameConstants::kRefOffset));
  GetContextFromRef(masm, kContextRegister, tmp);

  ReloadParentContinuation(masm, promise, return_value, kContextRegister, tmp,
                           tmp2, tmp3);
  RestoreParentSuspender(masm, tmp, tmp2);

  __ Mov(tmp, 1);
  __ Str(tmp,
         MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset));
  __ Push(padreg, promise);
  __ CallBuiltin(Builtin::kFulfillPromise);
  __ Pop(promise, padreg);
  FREE_REG(return_value);
  FREE_REG(promise);

  __ bind(return_promise);
}

void GenerateExceptionHandlingLandingPad(MacroAssembler* masm,
                                         RegisterAllocator& regs,
                                         Label* return_promise) {
  regs.ResetExcept();
  static const Builtin_RejectPromise_InterfaceDescriptor desc;
  DEFINE_PINNED(promise, desc.GetRegisterParameter(0));
  DEFINE_PINNED(reason, desc.GetRegisterParameter(1));
  DEFINE_PINNED(debug_event, desc.GetRegisterParameter(2));
  int catch_handler = __ pc_offset();
  __ JumpTarget();

  DEFINE_SCOPED(thread_in_wasm_flag_addr);
  thread_in_wasm_flag_addr = x2;
  // Unset thread_in_wasm_flag.
  __ Ldr(
      thread_in_wasm_flag_addr,
      MemOperand(kRootRegister, Isolate::thread_in_wasm_flag_address_offset()));
  __ Str(wzr, MemOperand(thread_in_wasm_flag_addr, 0));

  // The exception becomes the parameter of the RejectPromise builtin, and the
  // promise is the return value of this wrapper.
  __ Move(reason, kReturnRegister0);
  __ LoadRoot(promise, RootIndex::kActiveSuspender);
  __ LoadTaggedField(
      promise, FieldMemOperand(promise, WasmSuspenderObject::kPromiseOffset));

  __ Ldr(kContextRegister,
         MemOperand(fp, StackSwitchFrameConstants::kRefOffset));

  DEFINE_SCOPED(tmp);
  DEFINE_SCOPED(tmp2);
  DEFINE_SCOPED(tmp3);
  GetContextFromRef(masm, kContextRegister, tmp);
  ReloadParentContinuation(masm, promise, reason, kContextRegister, tmp, tmp2,
                           tmp3);
  RestoreParentSuspender(masm, tmp, tmp2);

  __ Mov(tmp, 1);
  __ Str(tmp,
         MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset));
  __ Push(padreg, promise);
  __ LoadRoot(debug_event, RootIndex::kTrueValue);
  __ CallBuiltin(Builtin::kRejectPromise);
  __ Pop(promise, padreg);

  // Run the rest of the wrapper normally (deconstruct the frame, ...).
  __ jmp(return_promise);

  masm->isolate()->builtins()->SetJSPIPromptHandlerOffset(catch_handler);
}

void JSToWasmWrapperHelper(MacroAssembler* masm, bool stack_switch) {
  auto regs = RegisterAllocator::WithAllocatableGeneralRegisters();

  __ EnterFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);

  __ Sub(sp, sp,
         Immediate(StackSwitchFrameConstants::kNumSpillSlots *
                   kSystemPointerSize));

  DEFINE_PINNED(ref, kWasmInstanceRegister);
  __ Ldr(ref, MemOperand(fp, JSToWasmWrapperFrameConstants::kRefParamOffset));

  DEFINE_PINNED(wrapper_buffer,
                WasmJSToWasmWrapperDescriptor::WrapperBufferRegister());

  Label suspend;
  Register original_fp = no_reg;
  Register new_wrapper_buffer = no_reg;
  if (stack_switch) {
    SwitchToAllocatedStack(masm, regs, ref, wrapper_buffer, original_fp,
                           new_wrapper_buffer, &suspend);
  } else {
    original_fp = fp;
    new_wrapper_buffer = wrapper_buffer;
  }

  regs.ResetExcept(original_fp, wrapper_buffer, ref, new_wrapper_buffer);

  {
    __ Str(new_wrapper_buffer,
           MemOperand(fp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset));
    if (stack_switch) {
      __ Str(ref, MemOperand(fp, StackSwitchFrameConstants::kRefOffset));
      DEFINE_SCOPED(scratch)
      __ Ldr(
          scratch,
          MemOperand(original_fp,
                     JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
      __ Str(scratch,
             MemOperand(fp, StackSwitchFrameConstants::kResultArrayOffset));
    }
  }
  {
    DEFINE_SCOPED(result_size);
    __ Ldr(result_size,
           MemOperand(wrapper_buffer, JSToWasmWrapperFrameConstants::
                                          kWrapperBufferStackReturnBufferSize));
    // The `result_size` is the number of slots needed on the stack to store the
    // return values of the wasm function. If `result_size` is an odd number, we
    // have to add `1` to preserve stack pointer alignment.
    __ Add(result_size, result_size, 1);
    __ Bic(result_size, result_size, 1);
    __ Sub(sp, sp, Operand(result_size, LSL, kSystemPointerSizeLog2));
  }
  {
    DEFINE_SCOPED(scratch);
    __ Mov(scratch, sp);
    __ Str(scratch, MemOperand(new_wrapper_buffer,
                               JSToWasmWrapperFrameConstants::
                                   kWrapperBufferStackReturnBufferStart));
  }
  if (stack_switch) {
    FREE_REG(new_wrapper_buffer)
  }
  FREE_REG(ref)
  for (auto reg : wasm::kGpParamRegisters) {
    regs.Reserve(reg);
  }

  // The first GP parameter is the instance, which we handle specially.
  int stack_params_offset =
      (arraysize(wasm::kGpParamRegisters) - 1) * kSystemPointerSize +
      arraysize(wasm::kFpParamRegisters) * kDoubleSize;

  {
    DEFINE_SCOPED(params_start);
    __ Ldr(params_start,
           MemOperand(wrapper_buffer,
                      JSToWasmWrapperFrameConstants::kWrapperBufferParamStart));
    {
      // Push stack parameters on the stack.
      DEFINE_SCOPED(params_end);
      __ Ldr(params_end,
             MemOperand(wrapper_buffer,
                        JSToWasmWrapperFrameConstants::kWrapperBufferParamEnd));
      DEFINE_SCOPED(last_stack_param);

      __ Add(last_stack_param, params_start, Immediate(stack_params_offset));
      Label loop_start;
      {
        DEFINE_SCOPED(scratch);
        // Check if there is an even number of parameters, so no alignment
        // needed.
        __ Sub(scratch, params_end, last_stack_param);
        __ TestAndBranchIfAllClear(scratch, 0x8, &loop_start);

        // Push the first parameter with alignment.
        __ Ldr(scratch, MemOperand(params_end, -kSystemPointerSize, PreIndex));
        __ Push(xzr, scratch);
      }
      __ bind(&loop_start);

      Label finish_stack_params;
      __ Cmp(last_stack_param, params_end);
      __ B(ge, &finish_stack_params);

      // Push parameter
      {
        DEFINE_SCOPED(scratch1);
        DEFINE_SCOPED(scratch2);
        __ Ldp(scratch2, scratch1,
               MemOperand(params_end, -2 * kSystemPointerSize, PreIndex));
        __ Push(scratch1, scratch2);
      }
      __ jmp(&loop_start);

      __ bind(&finish_stack_params);
    }

    size_t next_offset = 0;
    for (size_t i = 1; i < arraysize(wasm::kGpParamRegisters); i += 2) {
      // Check that {params_start} does not overlap with any of the parameter
      // registers, so that we don't overwrite it by accident with the loads
      // below.
      DCHECK_NE(params_start, wasm::kGpParamRegisters[i]);
      DCHECK_NE(params_start, wasm::kGpParamRegisters[i + 1]);
      __ Ldp(wasm::kGpParamRegisters[i], wasm::kGpParamRegisters[i + 1],
             MemOperand(params_start, next_offset));
      next_offset += 2 * kSystemPointerSize;
    }

    for (size_t i = 0; i < arraysize(wasm::kFpParamRegisters); i += 2) {
      __ Ldp(wasm::kFpParamRegisters[i], wasm::kFpParamRegisters[i + 1],
             MemOperand(params_start, next_offset));
      next_offset += 2 * kDoubleSize;
    }
    DCHECK_EQ(next_offset, stack_params_offset);
  }

  {
    DEFINE_SCOPED(thread_in_wasm_flag_addr);
    __ Ldr(thread_in_wasm_flag_addr,
           MemOperand(kRootRegister,
                      Isolate::thread_in_wasm_flag_address_offset()));
    DEFINE_SCOPED(scratch);
    __ Mov(scratch, 1);
    __ Str(scratch.W(), MemOperand(thread_in_wasm_flag_addr, 0));
  }
  __ Str(xzr,
         MemOperand(fp, StackSwitchFrameConstants::kGCScanSlotCountOffset));
  {
    DEFINE_SCOPED(call_target);
    __ Ldr(call_target,
           MemOperand(wrapper_buffer,
                      JSToWasmWrapperFrameConstants::kWrapperBufferCallTarget));
    __ Call(call_target);
  }
  regs.ResetExcept();
  // The wrapper_buffer has to be in x2 as the correct parameter register.
  regs.Reserve(kReturnRegister0, kReturnRegister1);
  ASSIGN_PINNED(wrapper_buffer, x2);
  {
    DEFINE_SCOPED(thread_in_wasm_flag_addr);
    __ Ldr(thread_in_wasm_flag_addr,
           MemOperand(kRootRegister,
                      Isolate::thread_in_wasm_flag_address_offset()));
    __ Str(wzr, MemOperand(thread_in_wasm_flag_addr, 0));
  }

  __ Ldr(wrapper_buffer,
         MemOperand(fp, JSToWasmWrapperFrameConstants::kWrapperBufferOffset));

  __ Str(wasm::kFpReturnRegisters[0],
         MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister1));
  __ Str(wasm::kFpReturnRegisters[1],
         MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferFPReturnRegister2));
  __ Str(wasm::kGpReturnRegisters[0],
         MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister1));
  __ Str(wasm::kGpReturnRegisters[1],
         MemOperand(
             wrapper_buffer,
             JSToWasmWrapperFrameConstants::kWrapperBufferGPReturnRegister2));
  // Call the return value builtin with
  // x0: wasm instance.
  // x1: the result JSArray for multi-return.
  // x2: pointer to the byte buffer which contains all parameters.
  if (stack_switch) {
    __ Ldr(x1, MemOperand(fp, StackSwitchFrameConstants::kResultArrayOffset));
    __ Ldr(x0, MemOperand(fp, StackSwitchFrameConstants::kRefOffset));
  } else {
    __ Ldr(x1, MemOperand(
                   fp, JSToWasmWrapperFrameConstants::kResultArrayParamOffset));
    __ Ldr(x0, MemOperand(fp, JSToWasmWrapperFrameConstants::kRefParamOffset));
  }
  Register scratch = x3;
  GetContextFromRef(masm, x0, scratch);
  __ CallBuiltin(Builtin::kJSToWasmHandleReturns);

  Label return_promise;
  if (stack_switch) {
    SwitchBackAndReturnPromise(masm, regs, &return_promise);
  }
  __ Bind(&suspend, BranchTargetIdentifier::kBtiJump);

  __ LeaveFrame(stack_switch ? StackFrame::STACK_SWITCH
                             : StackFrame::JS_TO_WASM);
  // Despite returning to the different location for regular and stack switching
  // versions, incoming argument count matches both cases:
  // instance and result array without suspend or
  // or promise resolve/reject params for callback.
  constexpr int64_t stack_arguments_in = 2;
  __ DropArguments(stack_arguments_in);
  __ Ret();

  // Catch handler for the stack-switching wrapper: reject the promise with the
  // thrown exception.
  if (stack_switch) {
    GenerateExceptionHandlingLandingPad(masm, regs, &return_promise);
  }
}
}  // namespace

void Builtins::Generate_JSToWasmWrapperAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, false);
}

void Builtins::Generate_WasmReturnPromiseOnSuspendAsm(MacroAssembler* masm) {
  JSToWasmWrapperHelper(masm, true);
}

namespace {
void SwitchSimulatorStackLimit(MacroAssembler* masm) {
  if (masm->options().enable_simulator_code) {
    UseScratchRegisterScope temps(masm);
    temps.Exclude(x16);
    __ LoadStackLimit(x16, StackLimitKind::kRealStackLimit);
    __ hlt(kImmExceptionIsSwitchStackLimit);
  }
}

static constexpr Register kOldSPRegister = x23;
static constexpr Register kSwitchFlagRegister = x24;

void SwitchToTheCentralStackIfNeeded(MacroAssembler* masm, Register argc_input,
                                     Register target_input,
                                     Register argv_input) {
  using ER = ExternalReference;

  __ Mov(kSwitchFlagRegister, 0);
  __ Mov(kOldSPRegister, sp);

  // Using x2-x4 as temporary registers, because they will be rewritten
  // before exiting to native code anyway.

  ER on_central_stack_flag_loc = ER::Create(
      IsolateAddressId::kIsOnCentralStackFlagAddress, masm->isolate());
  const Register& on_central_stack_flag = x2;
  __ Mov(on_central_stack_flag, on_central_stack_flag_loc);
  __ Ldrb(on_central_stack_flag, MemOperand(on_central_stack_flag));

  Label do_not_need_to_switch;
  __ Cbnz(on_central_stack_flag, &do_not_need_to_switch);
  // Switch to central stack.

  static constexpr Register central_stack_sp = x4;
  DCHECK(!AreAliased(central_stack_sp, argc_input, argv_input, target_input));
  {
    __ Push(argc_input, target_input, argv_input, padreg);
    __ Mov(kCArgRegs[0], ER::isolate_address());
    __ Mov(kCArgRegs[1], kOldSPRegister);
    __ CallCFunction(ER::wasm_switch_to_the_central_stack(), 2,
                     SetIsolateDataSlots::kNo);
    __ Mov(central_stack_sp, kReturnRegister0);
    __ Pop(padreg, argv_input, target_input, argc_input);
  }

  SwitchSimulatorStackLimit(masm);

  static constexpr int kReturnAddressSlotOffset = 1 * kSystemPointerSize;
  static constexpr int kPadding = 1 * kSystemPointerSize;
  __ Sub(sp, central_stack_sp, kReturnAddressSlotOffset + kPadding);
  __ Mov(kSwitchFlagRegister, 1);

  // Update the sp saved in the frame.
  // It will be used to calculate the callee pc during GC.
  // The pc is going to be on the new stack segment, so rewrite it here.
  __ Add(central_stack_sp, sp, kSystemPointerSize);
  __ Str(central_stack_sp, MemOperand(fp, ExitFrameConstants::kSPOffset));

  __ bind(&do_not_need_to_switch);
}

void SwitchFromTheCentralStackIfNeeded(MacroAssembler* masm) {
  using ER = ExternalReference;

  Label no_stack_change;
  __ Cbz(kSwitchFlagRegister, &no_stack_change);

  {
    __ Push(kReturnRegister0, kReturnRegister1);
    __ Mov(kCArgRegs[0], ER::isolate_address());
    __ CallCFunction(ER::wasm_switch_from_the_central_stack(), 1,
                     SetIsolateDataSlots::kNo);
    __ Pop(kReturnRegister1, kReturnRegister0);
  }

  SwitchSimulatorStackLimit(masm);

  __ Mov(sp, kOldSPRegister);

  __ bind(&no_stack_change);
}

}  // namespace

void Builtins::Generate_WasmToOnHeapWasmToJsTrampoline(MacroAssembler* masm) {
  // Load the code pointer from the WasmImportData and tail-call there.
  Register import_data = wasm::kGpParamRegisters[0];
  // Use x17 which is not in kGpParamRegisters and allows to jump to a "bti c"
  // marker.
  Register call_target = x17;
  UseScratchRegisterScope temps{masm};
  temps.Exclude(call_target);
#ifdef V8_ENABLE_SANDBOX
  __ LoadCodeEntrypointViaCodePointer(
      call_target, FieldMemOperand(import_data, WasmImportData::kCodeOffset),
      kWasmEntrypointTag);
#else
  Register code = call_target;
  __ Ldr(code, FieldMemOperand(import_data, WasmImportData::kCodeOffset));
  __ Ldr(call_target, FieldMemOperand(code, Code::kInstructionStartOffset));
#endif
  __ Jump(call_target);
}

#endif  // V8_ENABLE_WEBASSEMBLY

void Builtins::Generate_CEntry(MacroAssembler* masm, int result_size,
                               ArgvMode argv_mode, bool builtin_exit_frame,
                               bool switch_to_central_stack) {
  ASM_LOCATION("CEntry::Generate entry");

  using ER = ExternalReference;

  // Register parameters:
  //    x0: argc (including receiver, untagged)
  //    x1: target
  // If argv_mode == ArgvMode::kRegister:
  //    x11: argv (pointer to first argument)
  //
  // The stack on entry holds the arguments and the receiver, with the receiver
  // at the highest address:
  //
  //    sp[argc-1]: receiver
  //    sp[argc-2]: arg[argc-2]
  //    ...           ...
  //    sp[1]:      arg[1]
  //    sp[0]:      arg[0]
  //
  // The arguments are in reverse order, so that arg[argc-2] is actually the
  // first argument to the target function and arg[0] is the last.
  static constexpr Register argc_input = x0;
  static constexpr Register target_input = x1;
  // Initialized below if ArgvMode::kStack.
  static constexpr Register argv_input = x11;

  if (argv_mode == ArgvMode::kStack) {
    // Derive argv from the stack pointer so that it points to the first
    // argument.
    __ SlotAddress(argv_input, argc_input);
    __ Sub(argv_input, argv_input, kReceiverOnStackSize);
  }

  // If ArgvMode::kStack, argc is reused below and must be retained across the
  // call in a callee-saved register.
  static constexpr Register argc = x22;

  // Enter the exit frame.
  const int kNoExtraSpace = 0;
  FrameScope scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(
      x10, kNoExtraSpace,
      builtin_exit_frame ? StackFrame::BUILTIN_EXIT : StackFrame::EXIT);

  if (argv_mode == ArgvMode::kStack) {
    __ Mov(argc, argc_input);
  }

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchToTheCentralStackIfNeeded(masm, argc_input, target_input, argv_input);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // x21 : argv
  // x22 : argc
  // x23 : call target
  //
  // The stack (on entry) holds the arguments and the receiver, with the
  // receiver at the highest address:
  //
  //         argv[8]:     receiver
  // argv -> argv[0]:     arg[argc-2]
  //         ...          ...
  //         argv[...]:   arg[1]
  //         argv[...]:   arg[0]
  //
  // Immediately below (after) this is the exit frame, as constructed by
  // EnterExitFrame:
  //         fp[8]:    CallerPC (lr)
  //   fp -> fp[0]:    CallerFP (old fp)
  //         fp[-8]:   Space reserved for SPOffset.
  //         fp[-16]:  CodeObject()
  //         sp[...]:  Saved doubles, if saved_doubles is true.
  //         sp[16]:   Alignment padding, if necessary.
  //         sp[8]:   Preserved x22 (used for argc).
  //   sp -> sp[0]:    Space reserved for the return address.

  // TODO(jgruber): Swap these registers in the calling convention instead.
  static_assert(target_input == x1);
  static_assert(argv_input == x11);
  __ Swap(target_input, argv_input);
  static constexpr Register target = x11;
  static constexpr Register argv = x1;
  static_assert(!AreAliased(argc_input, argc, target, argv));

  // Prepare AAPCS64 arguments to pass to the builtin.
  static_assert(argc_input == x0);  // Already in the right spot.
  static_assert(argv == x1);        // Already in the right spot.
  __ Mov(x2, ER::isolate_address());

  __ StoreReturnAddressAndCall(target);

  // Result returned in x0 or x1:x0 - do not destroy these registers!

  //  x0    result0      The return code from the call.
  //  x1    result1      For calls which return ObjectPair.
  //  x22   argc         .. only if ArgvMode::kStack.
  const Register& result = x0;

#if V8_ENABLE_WEBASSEMBLY
  if (switch_to_central_stack) {
    SwitchFromTheCentralStackIfNeeded(masm);
  }
#endif  // V8_ENABLE_WEBASSEMBLY

  // Check result for exception sentinel.
  Label exception_returned;
  // The returned value may be a trusted object, living outside of the main
  // pointer compression cage, so we need to use full pointer comparison here.
  __ CompareRoot(result, RootIndex::kException, ComparisonMode::kFullPointer);
  __ B(eq, &exception_returned);

  // The call succeeded, so unwind the stack and return.
  if (argv_mode == ArgvMode::kStack) {
    __ Mov(x11, argc);  // x11 used as scratch, just til DropArguments below.
    __ LeaveExitFrame(x10, x9);
    __ DropArguments(x11);
  } else {
    __ LeaveExitFrame(x10, x9);
  }

  __ AssertFPCRState();
  __ Ret();

  // Handling of exception.
  __ Bind(&exception_returned);

  // Ask the runtime for help to determine the handler. This will set x0 to
  // contain the current exception, don't clobber it.
  {
    FrameScope scope(masm, StackFrame::MANUAL);
    __ Mov(x0, 0);  // argc.
    __ Mov(x1, 0);  // argv.
    __ Mov(x2, ER::isolate_address());
    __ CallCFunction(ER::Create(Runtime::kUnwindAndFindExceptionHandler), 3,
                     SetIsolateDataSlots::kNo);
  }

  // Retrieve the handler context, SP and FP.
  __ Mov(cp, ER::Create(IsolateAddressId::kPendingHandlerContextAddress,
                        masm->isolate()));
  __ Ldr(cp, MemOperand(cp));
  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Mov(scratch, ER::Create(IsolateAddressId::kPendingHandlerSPAddress,
                               masm->isolate()));
    __ Ldr(scratch, MemOperand(scratch));
    __ Mov(sp, scratch);
  }
  __ Mov(fp, ER::Create(IsolateAddressId::kPendingHandlerFPAddress,
                        masm->isolate()));
  __ Ldr(fp, MemOperand(fp));

  // If the handler is a JS frame, restore the context to the frame. Note that
  // the context will be set to (cp == 0) for non-JS frames.
  Label not_js_frame;
  __ Cbz(cp, &not_js_frame);
  __ Str(cp, MemOperand(fp, StandardFrameConstants::kContextOffset));
  __ Bind(&not_js_frame);

  {
    // Clear c_entry_fp, like we do in `LeaveExitFrame`.
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Mov(scratch,
           ER::Create(IsolateAddressId::kCEntryFPAddress, masm->isolate()));
    __ Str(xzr, MemOperand(scratch));
  }

  // Compute the handler entry address and jump to it. We use x17 here for the
  // jump target, as this jump can occasionally end up at the start of
  // InterpreterEnterAtBytecode, which when CFI is enabled starts with
  // a "BTI c".
  UseScratchRegisterScope temps(masm);
  temps.Exclude(x17);
  __ Mov(x17, ER::Create(IsolateAddressId::kPendingHandlerEntrypointAddress,
                         masm->isolate()));
  __ Ldr(x17, MemOperand(x17));
  __ Br(x17);
}

void Builtins::Generate_DoubleToI(MacroAssembler* masm) {
  Label done;
  Register result = x7;

  DCHECK(result.Is64Bits());

  HardAbortScope hard_abort(masm);  // Avoid calls to Abort.
  UseScratchRegisterScope temps(masm);
  Register scratch1 = temps.AcquireX();
  Register scratch2 = temps.AcquireX();
  DoubleRegister double_scratch = temps.AcquireD();

  // Account for saved regs.
  const int kArgumentOffset = 2 * kSystemPointerSize;

  __ Push(result, scratch1);  // scratch1 is also pushed to preserve alignment.
  __ Peek(double_scratch, kArgumentOffset);

  // Try to convert with a FPU convert instruction.  This handles all
  // non-saturating cases.
  __ TryConvertDoubleToInt64(result, double_scratch, &done);
  __ Fmov(result, double_scratch);

  // If we reach here we need to manually convert the input to an int32.

  // Extract the exponent.
  Register exponent = scratch1;
  __ Ubfx(exponent, result, HeapNumber::kMantissaBits,
          HeapNumber::kExponentBits);

  // It the exponent is >= 84 (kMantissaBits + 32), the result is always 0 since
  // the mantissa gets shifted completely out of the int32_t result.
  __ Cmp(exponent, HeapNumber::kExponentBias + HeapNumber::kMantissaBits + 32);
  __ CzeroX(result, ge);
  __ B(ge, &done);

  // The Fcvtzs sequence handles all cases except where the conversion causes
  // signed overflow in the int64_t target. Since we've already handled
  // exponents >= 84, we can guarantee that 63 <= exponent < 84.

  if (v8_flags.debug_code) {
    __ Cmp(exponent, HeapNumber::kExponentBias + 63);
    // Exponents less than this should have been handled by the Fcvt case.
    __ Check(ge, AbortReason::kUnexpectedValue);
  }

  // Isolate the mantissa bits, and set the implicit '1'.
  Register mantissa = scratch2;
  __ Ubfx(mantissa, result, 0, HeapNumber::kMantissaBits);
  __ Orr(mantissa, mantissa, 1ULL << HeapNumber::kMantissaBits);

  // Negate the mantissa if necessary.
  __ Tst(result, kXSignMask);
  __ Cneg(mantissa, mantissa, ne);

  // Shift the mantissa bits in the correct place. We know that we have to shift
  // it left here, because exponent >= 63 >= kMantissaBits.
  __ Sub(exponent, exponent,
         HeapNumber::kExponentBias + HeapNumber::kMantissaBits);
  __ Lsl(result, mantissa, exponent);

  __ Bind(&done);
  __ Poke(result, kArgumentOffset);
  __ Pop(scratch1, result);
  __ Ret();
}

void Builtins::Generate_CallApiCallbackImpl(MacroAssembler* masm,
                                            CallApiCallbackMode mode) {
  // ----------- S t a t e -------------
  // CallApiCallbackMode::kOptimizedNoProfiling/kOptimized modes:
  //  -- x1                  : api function address
  // Both modes:
  //  -- x2                  : arguments count (not including the receiver)
  //  -- x3                  : FunctionTemplateInfo
  //  -- x0                  : holder
  //  -- cp                  : context
  //  -- sp[0]               : receiver
  //  -- sp[8]               : first argument
  //  -- ...
  //  -- sp[(argc) * 8]      : last argument
  // -----------------------------------

  Register function_callback_info_arg = kCArgRegs[0];

  Register api_function_address = no_reg;
  Register argc = no_reg;
  Register func_templ = no_reg;
  Register holder = no_reg;
  Register topmost_script_having_context = no_reg;
  Register scratch = x4;

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
                     holder, func_templ, scratch));

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
  // Target state:
  //   sp[0 * kSystemPointerSize]: kHolder   <= FCA::implicit_args_
  //   sp[1 * kSystemPointerSize]: kIsolate
  //   sp[2 * kSystemPointerSize]: kContext
  //   sp[3 * kSystemPointerSize]: undefined (kReturnValue)
  //   sp[4 * kSystemPointerSize]: kData
  //   sp[5 * kSystemPointerSize]: undefined (kNewTarget)
  // Existing state:
  //   sp[6 * kSystemPointerSize]:            <= FCA:::values_

  __ StoreRootRelative(IsolateData::topmost_script_having_context_offset(),
                       topmost_script_having_context);

  if (mode == CallApiCallbackMode::kGeneric) {
    api_function_address = ReassignRegister(topmost_script_having_context);
  }

  // Reserve space on the stack.
  static constexpr int kStackSize = FCA::kArgsLength;
  static_assert(kStackSize % 2 == 0);
  __ Claim(kStackSize, kSystemPointerSize);

  // kHolder.
  __ Str(holder, MemOperand(sp, FCA::kHolderIndex * kSystemPointerSize));

  // kIsolate.
  __ Mov(scratch, ER::isolate_address());
  __ Str(scratch, MemOperand(sp, FCA::kIsolateIndex * kSystemPointerSize));

  // kContext.
  __ Str(cp, MemOperand(sp, FCA::kContextIndex * kSystemPointerSize));

  // kReturnValue.
  __ LoadRoot(scratch, RootIndex::kUndefinedValue);
  __ Str(scratch, MemOperand(sp, FCA::kReturnValueIndex * kSystemPointerSize));

  // kTarget.
  __ Str(func_templ, MemOperand(sp, FCA::kTargetIndex * kSystemPointerSize));

  // kNewTarget.
  __ Str(scratch, MemOperand(sp, FCA::kNewTargetIndex * kSystemPointerSize));

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  if (mode == CallApiCallbackMode::kGeneric) {
    __ LoadExternalPointerField(
        api_function_address,
        FieldMemOperand(func_templ,
                        FunctionTemplateInfo::kMaybeRedirectedCallbackOffset),
        kFunctionTemplateInfoCallbackTag);
  }

  __ EnterExitFrame(scratch, FC::getExtraSlotsCountFrom<ExitFrameConstants>(),
                    StackFrame::API_CALLBACK_EXIT);

  MemOperand argc_operand = MemOperand(fp, FC::kFCIArgcOffset);
  {
    ASM_CODE_COMMENT_STRING(masm, "Initialize v8::FunctionCallbackInfo");
    // FunctionCallbackInfo::length_.
    // TODO(ishell): pass JSParameterCount(argc) to simplify things on the
    // caller end.
    __ Str(argc, argc_operand);

    // FunctionCallbackInfo::implicit_args_.
    __ Add(scratch, fp, Operand(FC::kImplicitArgsArrayOffset));
    __ Str(scratch, MemOperand(fp, FC::kFCIImplicitArgsOffset));

    // FunctionCallbackInfo::values_ (points at JS arguments on the stack).
    __ Add(scratch, fp, Operand(FC::kFirstArgumentOffset));
    __ Str(scratch, MemOperand(fp, FC::kFCIValuesOffset));
  }

  __ RecordComment("v8::FunctionCallback's argument.");
  // function_callback_info_arg = v8::FunctionCallbackInfo&
  __ Add(function_callback_info_arg, fp,
         Operand(FC::kFunctionCallbackInfoOffset));

  DCHECK(!AreAliased(api_function_address, function_callback_info_arg));

  ExternalReference thunk_ref = ER::invoke_function_callback(mode);
  Register no_thunk_arg = no_reg;

  MemOperand return_value_operand = MemOperand(fp, FC::kReturnValueOffset);
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
  //  -- cp                  : context
  //  -- x1                  : receiver
  //  -- x3                  : accessor info
  //  -- x0                  : holder
  // -----------------------------------

  Register name_arg = kCArgRegs[0];
  Register property_callback_info_arg = kCArgRegs[1];

  Register api_function_address = x2;
  Register receiver = ApiGetterDescriptor::ReceiverRegister();
  Register holder = ApiGetterDescriptor::HolderRegister();
  Register callback = ApiGetterDescriptor::CallbackRegister();
  Register scratch = x4;
  Register undef = x5;
  Register scratch2 = x6;

  DCHECK(!AreAliased(receiver, holder, callback, scratch, undef, scratch2));

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
  // Target state:
  //   sp[0 * kSystemPointerSize]: name                      <= PCI::args_
  //   sp[1 * kSystemPointerSize]: kShouldThrowOnErrorIndex
  //   sp[2 * kSystemPointerSize]: kHolderIndex
  //   sp[3 * kSystemPointerSize]: kIsolateIndex
  //   sp[4 * kSystemPointerSize]: kHolderV2Index
  //   sp[5 * kSystemPointerSize]: kReturnValueIndex
  //   sp[6 * kSystemPointerSize]: kDataIndex
  //   sp[7 * kSystemPointerSize]: kThisIndex / receiver

  __ LoadTaggedField(scratch,
                     FieldMemOperand(callback, AccessorInfo::kDataOffset));
  __ LoadRoot(undef, RootIndex::kUndefinedValue);
  __ Mov(scratch2, ER::isolate_address());
  Register holderV2 = xzr;
  __ Push(receiver, scratch,  // kThisIndex, kDataIndex
          undef, holderV2,    // kReturnValueIndex, kHolderV2Index
          scratch2, holder);  // kIsolateIndex, kHolderIndex

  // |name_arg| clashes with |holder|, so we need to push holder first.
  __ LoadTaggedField(name_arg,
                     FieldMemOperand(callback, AccessorInfo::kNameOffset));
  static_assert(kDontThrow == 0);
  Register should_throw_on_error = xzr;  // should_throw_on_error -> kDontThrow
  __ Push(should_throw_on_error, name_arg);

  __ RecordComment("Load api_function_address");
  __ LoadExternalPointerField(
      api_function_address,
      FieldMemOperand(callback, AccessorInfo::kMaybeRedirectedGetterOffset),
      kAccessorInfoGetterTag);

  FrameScope frame_scope(masm, StackFrame::MANUAL);
  __ EnterExitFrame(scratch, FC::getExtraSlotsCountFrom<ExitFrameConstants>(),
                    StackFrame::API_ACCESSOR_EXIT);

  __ RecordComment("Create v8::PropertyCallbackInfo object on the stack.");
  // property_callback_info_arg = v8::PropertyCallbackInfo&
  __ Add(property_callback_info_arg, fp, Operand(FC::kArgsArrayOffset));

  DCHECK(!AreAliased(api_function_address, property_callback_info_arg, name_arg,
                     callback, scratch, scratch2));

#ifdef V8_ENABLE_DIRECT_HANDLE
  // name_arg = Local<Name>(name), name value was pushed to GC-ed stack space.
  // |name_arg| is already initialized above.
#else
  // name_arg = Local<Name>(&name), which is &args_array[kPropertyKeyIndex].
  static_assert(PCA::kPropertyKeyIndex == 0);
  __ mov(name_arg, property_callback_info_arg);
#endif

  ExternalReference thunk_ref = ER::invoke_accessor_getter_callback();
  // Pass AccessorInfo to thunk wrapper in case profiler or side-effect
  // checking is enabled.
  Register thunk_arg = callback;

  MemOperand return_value_operand = MemOperand(fp, FC::kReturnValueOffset);
  static constexpr int kSlotsToDropOnReturn =
      FC::kPropertyCallbackInfoArgsLength;
  MemOperand* const kUseStackSpaceConstant = nullptr;

  const bool with_profiling = true;
  CallApiFunctionAndReturn(masm, with_profiling, api_function_address,
                           thunk_ref, thunk_arg, kSlotsToDropOnReturn,
                           kUseStackSpaceConstant, return_value_operand);
}

void Builtins::Generate_DirectCEntry(MacroAssembler* masm) {
  // The sole purpose of DirectCEntry is for movable callers (e.g. any general
  // purpose InstructionStream object) to be able to call into C functions that
  // may trigger GC and thus move the caller.
  //
  // DirectCEntry places the return address on the stack (updated by the GC),
  // making the call GC safe. The irregexp backend relies on this.

  __ Poke<MacroAssembler::kSignLR>(lr, 0);  // Store the return address.
  __ Blr(x10);                              // Call the C++ function.
  __ Peek<MacroAssembler::kAuthLR>(lr, 0);  // Return to calling code.
  __ AssertFPCRState();
  __ Ret();
}

namespace {

template <typename RegisterT>
void CopyRegListToFrame(MacroAssembler* masm, const Register& dst,
                        int dst_offset, const CPURegList& reg_list,
                        const RegisterT& temp0, const RegisterT& temp1,
                        int src_offset = 0) {
  ASM_CODE_COMMENT(masm);
  DCHECK_EQ(reg_list.Count() % 2, 0);
  UseScratchRegisterScope temps(masm);
  CPURegList copy_to_input = reg_list;
  int reg_size = reg_list.RegisterSizeInBytes();
  DCHECK_EQ(temp0.SizeInBytes(), reg_size);
  DCHECK_EQ(temp1.SizeInBytes(), reg_size);

  // Compute some temporary addresses to avoid having the macro assembler set
  // up a temp with an offset for accesses out of the range of the addressing
  // mode.
  Register src = temps.AcquireX();
  masm->Add(src, sp, src_offset);
  masm->Add(dst, dst, dst_offset);

  // Write reg_list into the frame pointed to by dst.
  for (int i = 0; i < reg_list.Count(); i += 2) {
    masm->Ldp(temp0, temp1, MemOperand(src, i * reg_size));

    CPURegister reg0 = copy_to_input.PopLowestIndex();
    CPURegister reg1 = copy_to_input.PopLowestIndex();
    int offset0 = reg0.code() * reg_size;
    int offset1 = reg1.code() * reg_size;

    // Pair up adjacent stores, otherwise write them separately.
    if (offset1 == offset0 + reg_size) {
      masm->Stp(temp0, temp1, MemOperand(dst, offset0));
    } else {
      masm->Str(temp0, MemOperand(dst, offset0));
      masm->Str(temp1, MemOperand(dst, offset1));
    }
  }
  masm->Sub(dst, dst, dst_offset);
}

void RestoreRegList(MacroAssembler* masm, const CPURegList& reg_list,
                    const Register& src_base, int src_offset) {
  ASM_CODE_COMMENT(masm);
  DCHECK_EQ(reg_list.Count() % 2, 0);
  UseScratchRegisterScope temps(masm);
  CPURegList restore_list = reg_list;
  int reg_size = restore_list.RegisterSizeInBytes();

  // Compute a temporary addresses to avoid having the macro assembler set
  // up a temp with an offset for accesses out of the range of the addressing
  // mode.
  Register src = temps.AcquireX();
  masm->Add(src, src_base, src_offset);

  // No need to restore padreg.
  restore_list.Remove(padreg);

  // Restore every register in restore_list from src.
  while (!restore_list.IsEmpty()) {
    CPURegister reg0 = restore_list.PopLowestIndex();
    CPURegister reg1 = restore_list.PopLowestIndex();
    int offset0 = reg0.code() * reg_size;

    if (reg1 == NoCPUReg) {
      masm->Ldr(reg0, MemOperand(src, offset0));
      break;
    }

    int offset1 = reg1.code() * reg_size;

    // Pair up adjacent loads, otherwise read them separately.
    if (offset1 == offset0 + reg_size) {
      masm->Ldp(reg0, reg1, MemOperand(src, offset0));
    } else {
      masm->Ldr(reg0, MemOperand(src, offset0));
      masm->Ldr(reg1, MemOperand(src, offset1));
    }
  }
}

void Generate_DeoptimizationEntry(MacroAssembler* masm,
                                  DeoptimizeKind deopt_kind) {
  Isolate* isolate = masm->isolate();

  // TODO(all): This code needs to be revisited. We probably only need to save
  // caller-saved registers here. Callee-saved registers can be stored directly
  // in the input frame.

  // Save all allocatable simd128 / double registers.
  CPURegList saved_simd128_registers(
      kQRegSizeInBits,
      DoubleRegList::FromBits(
          RegisterConfiguration::Default()->allocatable_simd128_codes_mask()));
  DCHECK_EQ(saved_simd128_registers.Count() % 2, 0);
  __ PushCPURegList(saved_simd128_registers);

  // We save all the registers except sp, lr, platform register (x18) and the
  // masm scratches.
  CPURegList saved_registers(CPURegister::kRegister, kXRegSizeInBits, 0, 28);
  saved_registers.Remove(ip0);
  saved_registers.Remove(ip1);
  saved_registers.Remove(x18);
  saved_registers.Combine(fp);
  saved_registers.Align();
  DCHECK_EQ(saved_registers.Count() % 2, 0);
  __ PushCPURegList(saved_registers);

  __ Mov(x3, Operand(ExternalReference::Create(
                 IsolateAddressId::kCEntryFPAddress, isolate)));
  __ Str(fp, MemOperand(x3));

  const int kSavedRegistersAreaSize =
      (saved_registers.Count() * kXRegSize) +
      (saved_simd128_registers.Count() * kQRegSize);

  // Floating point registers are saved on the stack above core registers.
  const int kSimd128RegistersOffset = saved_registers.Count() * kXRegSize;

  Register code_object = x2;
  Register fp_to_sp = x3;
  // Get the address of the location in the code object. This is the return
  // address for lazy deoptimization.
  __ Mov(code_object, lr);
  // Compute the fp-to-sp delta.
  __ Add(fp_to_sp, sp, kSavedRegistersAreaSize);
  __ Sub(fp_to_sp, fp, fp_to_sp);

  // Allocate a new deoptimizer object.
  __ Ldr(x1, MemOperand(fp, CommonFrameConstants::kContextOrFrameTypeOffset));

  // Ensure we can safely load from below fp.
  DCHECK_GT(kSavedRegistersAreaSize, -StandardFrameConstants::kFunctionOffset);
  __ Ldr(x0, MemOperand(fp, StandardFrameConstants::kFunctionOffset));

  // If x1 is a smi, zero x0.
  __ Tst(x1, kSmiTagMask);
  __ CzeroX(x0, eq);

  __ Mov(x1, static_cast<int>(deopt_kind));
  // Following arguments are already loaded:
  //  - x2: code object address
  //  - x3: fp-to-sp delta
  __ Mov(x4, ExternalReference::isolate_address());

  {
    // Call Deoptimizer::New().
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::new_deoptimizer_function(), 5);
  }

  // Preserve "deoptimizer" object in register x0.
  Register deoptimizer = x0;

  // Get the input frame descriptor pointer.
  __ Ldr(x1, MemOperand(deoptimizer, Deoptimizer::input_offset()));

  // Copy core registers into the input frame.
  CopyRegListToFrame(masm, x1, FrameDescription::registers_offset(),
                     saved_registers, x2, x3);

  // Copy simd128 / double registers to the input frame.
  CopyRegListToFrame(masm, x1, FrameDescription::simd128_registers_offset(),
                     saved_simd128_registers, q2, q3, kSimd128RegistersOffset);

  // Mark the stack as not iterable for the CPU profiler which won't be able to
  // walk the stack without the return address.
  {
    UseScratchRegisterScope temps(masm);
    Register is_iterable = temps.AcquireX();
    __ LoadIsolateField(is_iterable, IsolateFieldId::kStackIsIterable);
    __ strb(xzr, MemOperand(is_iterable));
  }

  // Remove the saved registers from the stack.
  DCHECK_EQ(kSavedRegistersAreaSize % kXRegSize, 0);
  __ Drop(kSavedRegistersAreaSize / kXRegSize);

  // Compute a pointer to the unwinding limit in register x2; that is
  // the first stack slot not part of the input frame.
  Register unwind_limit = x2;
  __ Ldr(unwind_limit, MemOperand(x1, FrameDescription::frame_size_offset()));

  // Unwind the stack down to - but not including - the unwinding
  // limit and copy the contents of the activation frame to the input
  // frame description.
  __ Add(x3, x1, FrameDescription::frame_content_offset());
  __ SlotAddress(x1, 0);
  __ Lsr(unwind_limit, unwind_limit, kSystemPointerSizeLog2);
  __ Mov(x5, unwind_limit);
  __ CopyDoubleWords(x3, x1, x5);
  // Since {unwind_limit} is the frame size up to the parameter count, we might
  // end up with a unaligned stack pointer. This is later recovered when
  // setting the stack pointer to {caller_frame_top_offset}.
  __ Bic(unwind_limit, unwind_limit, 1);
  __ Drop(unwind_limit);

  // Compute the output frame in the deoptimizer.
  __ Push(padreg, x0);  // Preserve deoptimizer object across call.
  {
    // Call Deoptimizer::ComputeOutputFrames().
    AllowExternalCallThatCantCauseGC scope(masm);
    __ CallCFunction(ExternalReference::compute_output_frames_function(), 1);
  }
  __ Pop(x4, padreg);  // Restore deoptimizer object (class Deoptimizer).

  {
    UseScratchRegisterScope temps(masm);
    Register scratch = temps.AcquireX();
    __ Ldr(scratch, MemOperand(x4, Deoptimizer::caller_frame_top_offset()));
    __ Mov(sp, scratch);
  }

  // Replace the current (input) frame with the output frames.
  Label outer_push_loop, outer_loop_header;
  __ Ldrsw(x1, MemOperand(x4, Deoptimizer::output_count_offset()));
  __ Ldr(x0, MemOperand(x4, Deoptimizer::output_offset()));
  __ Add(x1, x0, Operand(x1, LSL, kSystemPointerSizeLog2));
  __ B(&outer_loop_header);

  __ Bind(&outer_push_loop);
  Register current_frame = x2;
  Register frame_size = x3;
  __ Ldr(current_frame, MemOperand(x0, kSystemPointerSize, PostIndex));
  __ Ldr(x3, MemOperand(current_frame, FrameDescription::frame_size_offset()));
  __ Lsr(frame_size, x3, kSystemPointerSizeLog2);
  __ Claim(frame_size, kXRegSize, /*assume_sp_aligned=*/false);

  __ Add(x7, current_frame, FrameDescription::frame_content_offset());
  __ SlotAddress(x6, 0);
  __ CopyDoubleWords(x6, x7, frame_size);

  __ Bind(&outer_loop_header);
  __ Cmp(x0, x1);
  __ B(lt, &outer_push_loop);

  RestoreRegList(masm, saved_simd128_registers, current_frame,
                 FrameDescription::simd128_registers_offset());

  {
    UseScratchRegisterScope temps(masm);
    Register is_iterable = temps.AcquireX();
    Register one = x4;
    __ LoadIsolateField(is_iterable, IsolateFieldId::kStackIsIterable);
    __ Mov(one, Operand(1));
    __ strb(one, MemOperand(is_iterable));
  }

  // TODO(all): ARM copies a lot (if not all) of the last output frame onto the
  // stack, then pops it all into registers. Here, we try to load it directly
  // into the relevant registers. Is this correct? If so, we should improve the
  // ARM code.

  // Restore registers from the last output frame.
  // Note that lr is not in the list of saved_registers and will be restored
  // later. We can use it to hold the address of last output frame while
  // reloading the other registers.
  DCHECK(!saved_registers.IncludesAliasOf(lr));
  Register last_output_frame = lr;
  __ Mov(last_output_frame, current_frame);

  RestoreRegList(masm, saved_registers, last_output_frame,
                 FrameDescription::registers_offset());

  UseScratchRegisterScope temps(masm);
  temps.Exclude(x17);
  Register continuation = x17;
  __ Ldr(continuation, MemOperand(last_output_frame,
                                  FrameDescription::continuation_offset()));
  __ Ldr(lr, MemOperand(last_output_frame, FrameDescription::pc_offset()));
#ifdef V8_ENABLE_CONTROL_FLOW_INTEGRITY
  __ Autibsp();
#endif
  // If the continuation is non-zero (JavaScript), branch to the continuation.
  // For Wasm just return to the pc from the last output frame in the lr
  // register.
  Label end;
  __ CompareAndBranch(continuation, 0, eq, &end);
  __ Br(continuation);
  __ Bind(&end);
  __ Ret();
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

  // Get function from the frame.
  Register closure = x1;
  __ Ldr(closure, MemOperand(fp, StandardFrameConstants::kFunctionOffset));

  // Get the InstructionStream object from the shared function info.
  Register code_obj = x22;
  __ LoadTaggedField(
      code_obj,
      FieldMemOperand(closure, JSFunction::kSharedFunctionInfoOffset));

  if (is_osr) {
    ResetSharedFunctionInfoAge(masm, code_obj);
  }

  __ LoadTrustedPointerField(
      code_obj,
      FieldMemOperand(code_obj, SharedFunctionInfo::kTrustedFunctionDataOffset),
      kUnknownIndirectPointerTag);

  // Check if we have baseline code. For OSR entry it is safe to assume we
  // always have baseline code.
  if (!is_osr) {
    Label start_with_baseline;
    __ IsObjectType(code_obj, x3, x3, CODE_TYPE);
    __ B(eq, &start_with_baseline);

    // Start with bytecode as there is no baseline code.
    Builtin builtin = next_bytecode ? Builtin::kInterpreterEnterAtNextBytecode
                                    : Builtin::kInterpreterEnterAtBytecode;
    __ TailCallBuiltin(builtin);

    // Start with baseline code.
    __ bind(&start_with_baseline);
  } else if (v8_flags.debug_code) {
    __ IsObjectType(code_obj, x3, x3, CODE_TYPE);
    __ Assert(eq, AbortReason::kExpectedBaselineData);
  }

  if (v8_flags.debug_code) {
    AssertCodeIsBaseline(masm, code_obj, x3);
  }

  // Load the feedback cell and vector.
  Register feedback_cell = x2;
  Register feedback_vector = x15;
  __ LoadTaggedField(feedback_cell,
                     FieldMemOperand(closure, JSFunction::kFeedbackCellOffset));
  __ LoadTaggedField(
      feedback_vector,
      FieldMemOperand(feedback_cell, FeedbackCell::kValueOffset));

  Label install_baseline_code;
  // Check if feedback vector is valid. If not, call prepare for baseline to
  // allocate it.
  __ IsObjectType(feedback_vector, x3, x3, FEEDBACK_VECTOR_TYPE);
  __ B(ne, &install_baseline_code);

  // Save BytecodeOffset from the stack frame.
  __ SmiUntag(kInterpreterBytecodeOffsetRegister,
              MemOperand(fp, InterpreterFrameConstants::kBytecodeOffsetFromFp));
  // Replace bytecode offset with feedback cell.
  static_assert(InterpreterFrameConstants::kBytecodeOffsetFromFp ==
                BaselineFrameConstants::kFeedbackCellFromFp);
  __ Str(feedback_cell,
         MemOperand(fp, BaselineFrameConstants::kFeedbackCellFromFp));
  feedback_cell = no_reg;
  // Update feedback vector cache.
  static_assert(InterpreterFrameConstants::kFeedbackVectorFromFp ==
                BaselineFrameConstants::kFeedbackVectorFromFp);
  __ Str(feedback_vector,
         MemOperand(fp, InterpreterFrameConstants::kFeedbackVectorFromFp));
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
  Register get_baseline_pc = x3;
  __ Mov(get_baseline_pc, get_baseline_pc_extref);

  // If the code deoptimizes during the implicit function entry stack interrupt
  // check, it will have a bailout ID of kFunctionEntryBytecodeOffset, which is
  // not a valid bytecode offset.
  // TODO(pthier): Investigate if it is feasible to handle this special case
  // in TurboFan instead of here.
  Label valid_bytecode_offset, function_entry_bytecode;
  if (!is_osr) {
    __ cmp(kInterpreterBytecodeOffsetRegister,
           Operand(BytecodeArray::kHeaderSize - kHeapObjectTag +
                   kFunctionEntryBytecodeOffset));
    __ B(eq, &function_entry_bytecode);
  }

  __ Sub(kInterpreterBytecodeOffsetRegister, kInterpreterBytecodeOffsetRegister,
         (BytecodeArray::kHeaderSize - kHeapObjectTag));

  __ bind(&valid_bytecode_offset);
  // Get bytecode array from the stack frame.
  __ ldr(kInterpreterBytecodeArrayRegister,
         MemOperand(fp, InterpreterFrameConstants::kBytecodeArrayFromFp));
  // Save the accumulator register, since it's clobbered by the below call.
  __ Push(padreg, kInterpreterAccumulatorRegister);
  {
    __ Mov(kCArgRegs[0], code_obj);
    __ Mov(kCArgRegs[1], kInterpreterBytecodeOffsetRegister);
    __ Mov(kCArgRegs[2], kInterpreterBytecodeArrayRegister);
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ CallCFunction(get_baseline_pc, 3, 0);
  }
  __ LoadCodeInstructionStart(code_obj, code_obj, kJSEntrypointTag);
  __ Add(code_obj, code_obj, kReturnRegister0);
  __ Pop(kInterpreterAccumulatorRegister, padreg);

  if (is_osr) {
    Generate_OSREntry(masm, code_obj);
  } else {
    __ Jump(code_obj);
  }
  __ Trap();  // Unreachable.

  if (!is_osr) {
    __ bind(&function_entry_bytecode);
    // If the bytecode offset is kFunctionEntryOffset, get the start address of
    // the first bytecode.
    __ Mov(kInterpreterBytecodeOffsetRegister, Operand(0));
    if (next_bytecode) {
      __ Mov(get_baseline_pc,
             ExternalReference::baseline_pc_for_bytecode_offset());
    }
    __ B(&valid_bytecode_offset);
  }

  __ bind(&install_baseline_code);
  {
    FrameScope scope(masm, StackFrame::INTERNAL);
    __ Push(padreg, kInterpreterAccumulatorRegister);
    __ PushArgument(closure);
    __ CallRuntime(Runtime::kInstallBaselineCode, 1);
    __ Pop(kInterpreterAccumulatorRegister, padreg);
  }
  // Retry from the start after installing baseline code.
  __ B(&start);
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

  __ Ldr(x1, MemOperand(fp, StandardFrameConstants::kFunctionOffset));
  __ ldr(x0, MemOperand(fp, StandardFrameConstants::kArgCOffset));

  __ LeaveFrame(StackFrame::INTERPRETED);

  // The arguments are already in the stack (including any necessary padding),
  // we should not try to massage the arguments again.
  __ Mov(x2, kDontAdaptArgumentsSentinel);
  __ InvokeFunction(x1, x2, x0, InvokeType::kJump);
}

#undef __

}  // namespace internal
}  // namespace v8

#endif  // V8_TARGET_ARCH_ARM
