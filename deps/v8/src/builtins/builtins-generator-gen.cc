// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/execution/isolate.h"
#include "src/objects/js-generator.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

class GeneratorBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit GeneratorBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

 protected:
  // Currently, AsyncModules in V8 are built on top of JSAsyncFunctionObjects
  // with an initial yield. Thus, we need some way to 'resume' the
  // underlying JSAsyncFunctionObject owned by an AsyncModule. To support this
  // the body of resume is factored out below, and shared by JSGeneratorObject
  // prototype methods as well as AsyncModuleEvaluate. The only difference
  // between AsyncModuleEvaluate and JSGeneratorObject::PrototypeNext is
  // the expected receiver.
  void InnerResume(CodeStubArguments* args, TNode<JSGeneratorObject> receiver,
                   TNode<Object> value, TNode<Context> context,
                   JSGeneratorObject::ResumeMode resume_mode,
                   char const* const method_name);
  void GeneratorPrototypeResume(CodeStubArguments* args, TNode<Object> receiver,
                                TNode<Object> value, TNode<Context> context,
                                JSGeneratorObject::ResumeMode resume_mode,
                                char const* const method_name);
};

void GeneratorBuiltinsAssembler::InnerResume(
    CodeStubArguments* args, TNode<JSGeneratorObject> receiver,
    TNode<Object> value, TNode<Context> context,
    JSGeneratorObject::ResumeMode resume_mode, char const* const method_name) {
  // Check if the {receiver} is running or already closed.
  TNode<Smi> receiver_continuation =
      LoadObjectField<Smi>(receiver, JSGeneratorObject::kContinuationOffset);
  Label if_receiverisclosed(this, Label::kDeferred),
      if_receiverisrunning(this, Label::kDeferred);
  TNode<Smi> closed = SmiConstant(JSGeneratorObject::kGeneratorClosed);
  GotoIf(SmiEqual(receiver_continuation, closed), &if_receiverisclosed);
  DCHECK_LT(JSGeneratorObject::kGeneratorExecuting,
            JSGeneratorObject::kGeneratorClosed);
  GotoIf(SmiLessThan(receiver_continuation, closed), &if_receiverisrunning);

  // Remember the {resume_mode} for the {receiver}.
  StoreObjectFieldNoWriteBarrier(receiver, JSGeneratorObject::kResumeModeOffset,
                                 SmiConstant(resume_mode));

  // Resume the {receiver} using our trampoline.
  // Close the generator if there was an exception.
  TVARIABLE(Object, var_exception);
  Label if_exception(this, Label::kDeferred), if_final_return(this);
  TNode<Object> result;
  {
    compiler::ScopedExceptionHandler handler(this, &if_exception,
                                             &var_exception);
    result = CallBuiltin(Builtin::kResumeGeneratorTrampoline, context, value,
                         receiver);
  }

  // If the generator is not suspended (i.e., its state is 'executing'),
  // close it and wrap the return value in IteratorResult.
  TNode<Smi> result_continuation =
      LoadObjectField<Smi>(receiver, JSGeneratorObject::kContinuationOffset);

  // The generator function should not close the generator by itself, let's
  // check it is indeed not closed yet.
  CSA_DCHECK(this, SmiNotEqual(result_continuation, closed));

  TNode<Smi> executing = SmiConstant(JSGeneratorObject::kGeneratorExecuting);
  GotoIf(SmiEqual(result_continuation, executing), &if_final_return);

  args->PopAndReturn(result);

  BIND(&if_final_return);
  {
    // Close the generator.
    StoreObjectFieldNoWriteBarrier(
        receiver, JSGeneratorObject::kContinuationOffset, closed);
    // Return the wrapped result.
    args->PopAndReturn(CallBuiltin(Builtin::kCreateIterResultObject, context,
                                   result, TrueConstant()));
  }

  BIND(&if_receiverisclosed);
  {
    // The {receiver} is closed already.
    TNode<Object> builtin_result;
    switch (resume_mode) {
      case JSGeneratorObject::kNext:
        builtin_result = CallBuiltin(Builtin::kCreateIterResultObject, context,
                                     UndefinedConstant(), TrueConstant());
        break;
      case JSGeneratorObject::kReturn:
        builtin_result = CallBuiltin(Builtin::kCreateIterResultObject, context,
                                     value, TrueConstant());
        break;
      case JSGeneratorObject::kThrow:
        builtin_result = CallRuntime(Runtime::kThrow, context, value);
        break;
      case JSGeneratorObject::kRethrow:
        // Currently only async generators use this mode.
        UNREACHABLE();
    }
    args->PopAndReturn(builtin_result);
  }

  BIND(&if_receiverisrunning);
  { ThrowTypeError(context, MessageTemplate::kGeneratorRunning); }

  BIND(&if_exception);
  {
    StoreObjectFieldNoWriteBarrier(
        receiver, JSGeneratorObject::kContinuationOffset, closed);
    CallRuntime(Runtime::kReThrow, context, var_exception.value());
    Unreachable();
  }
}

void GeneratorBuiltinsAssembler::GeneratorPrototypeResume(
    CodeStubArguments* args, TNode<Object> receiver, TNode<Object> value,
    TNode<Context> context, JSGeneratorObject::ResumeMode resume_mode,
    char const* const method_name) {
  // Check if the {receiver} is actually a JSGeneratorObject.
  ThrowIfNotInstanceType(context, receiver, JS_GENERATOR_OBJECT_TYPE,
                         method_name);
  TNode<JSGeneratorObject> generator = CAST(receiver);
  InnerResume(args, generator, value, context, resume_mode, method_name);
}

TF_BUILTIN(AsyncModuleEvaluate, GeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  // AsyncModules act like JSAsyncFunctions. Thus we check here
  // that the {receiver} is a JSAsyncFunction.
  char const* const method_name = "[AsyncModule].evaluate";
  ThrowIfNotInstanceType(context, receiver, JS_ASYNC_FUNCTION_OBJECT_TYPE,
                         method_name);
  TNode<JSAsyncFunctionObject> async_function = CAST(receiver);
  InnerResume(&args, async_function, value, context, JSGeneratorObject::kNext,
              method_name);
}

// ES6 #sec-generator.prototype.next
TF_BUILTIN(GeneratorPrototypeNext, GeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  GeneratorPrototypeResume(&args, receiver, value, context,
                           JSGeneratorObject::kNext,
                           "[Generator].prototype.next");
}

// ES6 #sec-generator.prototype.return
TF_BUILTIN(GeneratorPrototypeReturn, GeneratorBuiltinsAssembler) {
  const int kValueArg = 0;

  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> value = args.GetOptionalArgumentValue(kValueArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  GeneratorPrototypeResume(&args, receiver, value, context,
                           JSGeneratorObject::kReturn,
                           "[Generator].prototype.return");
}

// ES6 #sec-generator.prototype.throw
TF_BUILTIN(GeneratorPrototypeThrow, GeneratorBuiltinsAssembler) {
  const int kExceptionArg = 0;

  auto argc = UncheckedParameter<Int32T>(Descriptor::kJSActualArgumentsCount);
  CodeStubArguments args(this, argc);

  TNode<Object> receiver = args.GetReceiver();
  TNode<Object> exception = args.GetOptionalArgumentValue(kExceptionArg);
  auto context = Parameter<Context>(Descriptor::kContext);

  GeneratorPrototypeResume(&args, receiver, exception, context,
                           JSGeneratorObject::kThrow,
                           "[Generator].prototype.throw");
}

// TODO(cbruni): Merge with corresponding bytecode handler.
TF_BUILTIN(SuspendGeneratorBaseline, GeneratorBuiltinsAssembler) {
  auto generator = Parameter<JSGeneratorObject>(Descriptor::kGeneratorObject);
  auto context = LoadContextFromBaseline();
  StoreJSGeneratorObjectContext(generator, context);
  auto suspend_id = SmiTag(UncheckedParameter<IntPtrT>(Descriptor::kSuspendId));
  StoreJSGeneratorObjectContinuation(generator, suspend_id);
  // Store the bytecode offset in the [input_or_debug_pos] field, to be used by
  // the inspector.
  auto bytecode_offset =
      SmiTag(UncheckedParameter<IntPtrT>(Descriptor::kBytecodeOffset));
  // Avoid the write barrier by using the generic helper.
  StoreObjectFieldNoWriteBarrier(
      generator, JSGeneratorObject::kInputOrDebugPosOffset, bytecode_offset);

  TNode<JSFunction> closure = LoadJSGeneratorObjectFunction(generator);
  auto sfi = LoadJSFunctionSharedFunctionInfo(closure);
  CSA_DCHECK(this,
             Word32BinaryNot(IsSharedFunctionInfoDontAdaptArguments(sfi)));
  TNode<IntPtrT> formal_parameter_count = Signed(ChangeUint32ToWord(
      LoadSharedFunctionInfoFormalParameterCountWithoutReceiver(sfi)));

  TNode<FixedArray> parameters_and_registers =
      LoadJSGeneratorObjectParametersAndRegisters(generator);
  auto parameters_and_registers_length =
      LoadAndUntagFixedArrayBaseLength(parameters_and_registers);

  // Copy over the function parameters
  auto parameter_base_index = IntPtrConstant(
      interpreter::Register::FromParameterIndex(0).ToOperand() + 1);
  CSA_CHECK(this, UintPtrLessThan(formal_parameter_count,
                                  parameters_and_registers_length));
  auto parent_frame_pointer = LoadParentFramePointer();
  BuildFastLoop<IntPtrT>(
      IntPtrConstant(0), formal_parameter_count,
      [=, this](TNode<IntPtrT> index) {
        auto reg_index = IntPtrAdd(parameter_base_index, index);
        TNode<Object> value = LoadFullTagged(parent_frame_pointer,
                                             TimesSystemPointerSize(reg_index));
        UnsafeStoreFixedArrayElement(parameters_and_registers, index, value);
      },
      1, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

  // Iterate over register file and write values into array.
  // The mapping of register to array index must match that used in
  // BytecodeGraphBuilder::VisitResumeGenerator.
  auto register_base_index =
      IntPtrAdd(formal_parameter_count,
                IntPtrConstant(interpreter::Register(0).ToOperand()));
  auto register_count = UncheckedParameter<IntPtrT>(Descriptor::kRegisterCount);
  auto end_index = IntPtrAdd(formal_parameter_count, register_count);
  CSA_CHECK(this, UintPtrLessThan(end_index, parameters_and_registers_length));
  BuildFastLoop<IntPtrT>(
      formal_parameter_count, end_index,
      [=, this](TNode<IntPtrT> index) {
        auto reg_index = IntPtrSub(register_base_index, index);
        TNode<Object> value = LoadFullTagged(parent_frame_pointer,
                                             TimesSystemPointerSize(reg_index));
        UnsafeStoreFixedArrayElement(parameters_and_registers, index, value);
      },
      1, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

  // The return value is unused, defaulting to undefined.
  Return(UndefinedConstant());
}

// TODO(cbruni): Merge with corresponding bytecode handler.
TF_BUILTIN(ResumeGeneratorBaseline, GeneratorBuiltinsAssembler) {
  auto generator = Parameter<JSGeneratorObject>(Descriptor::kGeneratorObject);
  TNode<JSFunction> closure = LoadJSGeneratorObjectFunction(generator);
  auto sfi = LoadJSFunctionSharedFunctionInfo(closure);
  CSA_DCHECK(this,
             Word32BinaryNot(IsSharedFunctionInfoDontAdaptArguments(sfi)));
  TNode<IntPtrT> formal_parameter_count = Signed(ChangeUint32ToWord(
      LoadSharedFunctionInfoFormalParameterCountWithoutReceiver(sfi)));

  TNode<FixedArray> parameters_and_registers =
      LoadJSGeneratorObjectParametersAndRegisters(generator);

  // Iterate over array and write values into register file.  Also erase the
  // array contents to not keep them alive artificially.
  auto register_base_index =
      IntPtrAdd(formal_parameter_count,
                IntPtrConstant(interpreter::Register(0).ToOperand()));
  auto register_count = UncheckedParameter<IntPtrT>(Descriptor::kRegisterCount);
  auto end_index = IntPtrAdd(formal_parameter_count, register_count);
  auto parameters_and_registers_length =
      LoadAndUntagFixedArrayBaseLength(parameters_and_registers);
  CSA_CHECK(this, UintPtrLessThan(end_index, parameters_and_registers_length));
  auto parent_frame_pointer = LoadParentFramePointer();
  BuildFastLoop<IntPtrT>(
      formal_parameter_count, end_index,
      [=, this](TNode<IntPtrT> index) {
        TNode<Object> value =
            UnsafeLoadFixedArrayElement(parameters_and_registers, index);
        auto reg_index = IntPtrSub(register_base_index, index);
        StoreFullTaggedNoWriteBarrier(parent_frame_pointer,
                                      TimesSystemPointerSize(reg_index), value);
        UnsafeStoreFixedArrayElement(parameters_and_registers, index,
                                     StaleRegisterConstant(),
                                     SKIP_WRITE_BARRIER);
      },
      1, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

  Return(LoadJSGeneratorObjectInputOrDebugPos(generator));
}

}  // namespace internal
}  // namespace v8
