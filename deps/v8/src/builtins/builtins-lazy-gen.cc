// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-lazy-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/objects/code-inl.h"
#include "src/objects/feedback-vector.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

void LazyBuiltinsAssembler::GenerateTailCallToJSCode(
    TNode<Code> code, TNode<JSFunction> function) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  auto context = Parameter<Context>(Descriptor::kContext);
  auto new_target = Parameter<Object>(Descriptor::kNewTarget);
  TailCallJSCode(code, context, function, new_target, argc);
}

void LazyBuiltinsAssembler::GenerateTailCallToReturnedCode(
    Runtime::FunctionId function_id, TNode<JSFunction> function) {
  auto context = Parameter<Context>(Descriptor::kContext);
  TNode<Code> code = CAST(CallRuntime(function_id, context, function));
  GenerateTailCallToJSCode(code, function);
}

void LazyBuiltinsAssembler::MaybeTailCallOptimizedCodeSlot(
    TNode<JSFunction> function, TNode<FeedbackVector> feedback_vector) {
  Label fallthrough(this), may_have_optimized_code(this),
      maybe_needs_logging(this);

  TNode<Uint16T> flags =
      LoadObjectField<Uint16T>(feedback_vector, FeedbackVector::kFlagsOffset);

  // Fall through if no optimization trigger or optimized code.
  GotoIfNot(
      IsSetWord32(flags, FeedbackVector::kFlagsHasAnyOptimizedCode |
                             FeedbackVector::kFlagsTieringStateIsAnyRequested |
                             FeedbackVector::kFlagsLogNextExecution),
      &fallthrough);

  GotoIfNot(
      IsSetWord32(flags, FeedbackVector::kFlagsTieringStateIsAnyRequested),
      &maybe_needs_logging);
  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized, function);

  BIND(&maybe_needs_logging);
  {
    GotoIfNot(IsSetWord32(flags, FeedbackVector::kFlagsLogNextExecution),
              &may_have_optimized_code);
    GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution,
                                   function);
  }

  BIND(&may_have_optimized_code);
  {
    Label heal_optimized_code_slot(this);
    TNode<MaybeObject> maybe_optimized_code_entry = LoadMaybeWeakObjectField(
        feedback_vector, FeedbackVector::kMaybeOptimizedCodeOffset);

    // Optimized code slot is a weak reference to Code object.
    TNode<CodeWrapper> code_wrapper = CAST(GetHeapObjectAssumeWeak(
        maybe_optimized_code_entry, &heal_optimized_code_slot));
    TNode<Code> optimized_code =
        LoadCodePointerFromObject(code_wrapper, CodeWrapper::kCodeOffset);

    // Check if the optimized code is marked for deopt. If it is, call the
    // runtime to clear it.
    GotoIf(IsMarkedForDeoptimization(optimized_code),
           &heal_optimized_code_slot);

    // Optimized code is good, get it into the closure and link the closure into
    // the optimized functions list, then tail call the optimized code.
    StoreCodePointerField(function, JSFunction::kCodeOffset, optimized_code);
    Comment("MaybeTailCallOptimizedCodeSlot:: GenerateTailCallToJSCode");
    GenerateTailCallToJSCode(optimized_code, function);

    // Optimized code slot contains deoptimized code, or the code is cleared
    // and tiering state hasn't yet been updated. Evict the code, update the
    // state and re-enter the closure's code.
    BIND(&heal_optimized_code_slot);
    GenerateTailCallToReturnedCode(Runtime::kHealOptimizedCodeSlot, function);
  }

  // Fall-through if the optimized code cell is clear and the tiering state is
  // kNone.
  BIND(&fallthrough);
}

void LazyBuiltinsAssembler::CompileLazy(TNode<JSFunction> function) {
  // First lookup code, maybe we don't need to compile!
  Label compile_function(this, Label::kDeferred);

  // Check the code object for the SFI. If SFI's code entry points to
  // CompileLazy, then we need to lazy compile regardless of the function or
  // tiering state.
  TNode<SharedFunctionInfo> shared =
      CAST(LoadObjectField(function, JSFunction::kSharedFunctionInfoOffset));
  TVARIABLE(Uint16T, sfi_data_type);
  TNode<Code> sfi_code =
      GetSharedFunctionInfoCode(shared, &sfi_data_type, &compile_function);

  TNode<HeapObject> feedback_cell_value = LoadFeedbackCellValue(function);

  // If feedback cell isn't initialized, compile function
  GotoIf(IsUndefined(feedback_cell_value), &compile_function);

  CSA_DCHECK(this, TaggedNotEqual(sfi_code, HeapConstantNoHole(BUILTIN_CODE(
                                                isolate(), CompileLazy))));
  StoreCodePointerField(function, JSFunction::kCodeOffset, sfi_code);

  Label maybe_use_sfi_code(this);
  // If there is no feedback, don't check for optimized code.
  GotoIf(HasInstanceType(feedback_cell_value, CLOSURE_FEEDBACK_CELL_ARRAY_TYPE),
         &maybe_use_sfi_code);

  // If it isn't undefined or fixed array it must be a feedback vector.
  CSA_DCHECK(this, IsFeedbackVector(feedback_cell_value));

  // Is there a tiering state or optimized code in the feedback vector?
  MaybeTailCallOptimizedCodeSlot(function, CAST(feedback_cell_value));
  Goto(&maybe_use_sfi_code);

  // At this point we have a candidate InstructionStream object. It's *not* a
  // cached optimized InstructionStream object (we'd have tail-called it above).
  // A usual case would be the InterpreterEntryTrampoline to start executing
  // existing bytecode.
  BIND(&maybe_use_sfi_code);
  Label tailcall_code(this), baseline(this);
  TVARIABLE(Code, code);

  // Check if we have baseline code.
  GotoIf(InstanceTypeEqual(sfi_data_type.value(), CODE_TYPE), &baseline);

  code = sfi_code;
  Goto(&tailcall_code);

  BIND(&baseline);
  // Ensure we have a feedback vector.
  code = Select<Code>(
      IsFeedbackVector(feedback_cell_value), [=]() { return sfi_code; },
      [=]() {
        return CAST(CallRuntime(Runtime::kInstallBaselineCode,
                                Parameter<Context>(Descriptor::kContext),
                                function));
      });
  Goto(&tailcall_code);

  BIND(&tailcall_code);
  GenerateTailCallToJSCode(code.value(), function);

  BIND(&compile_function);
  GenerateTailCallToReturnedCode(Runtime::kCompileLazy, function);
}

TF_BUILTIN(CompileLazy, LazyBuiltinsAssembler) {
  auto function = Parameter<JSFunction>(Descriptor::kTarget);

  CompileLazy(function);
}

TF_BUILTIN(CompileLazyDeoptimizedCode, LazyBuiltinsAssembler) {
  auto function = Parameter<JSFunction>(Descriptor::kTarget);

  TNode<Code> code = HeapConstantNoHole(BUILTIN_CODE(isolate(), CompileLazy));
  // Set the code slot inside the JSFunction to CompileLazy.
  StoreCodePointerField(function, JSFunction::kCodeOffset, code);
  GenerateTailCallToJSCode(code, function);
}

}  // namespace internal
}  // namespace v8
