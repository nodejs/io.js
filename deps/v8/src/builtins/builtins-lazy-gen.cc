// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-lazy-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/common/globals.h"
#include "src/objects/code-inl.h"
#include "src/objects/feedback-vector-inl.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

#include "src/codegen/define-code-stub-assembler-macros.inc"

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
  constexpr uint32_t kFlagMask =
      FeedbackVector::FlagMaskForNeedsProcessingCheckFrom(
          CodeKind::INTERPRETED_FUNCTION);
  GotoIfNot(IsSetWord32(flags, kFlagMask), &fallthrough);

  GotoIfNot(
      IsSetWord32(flags, FeedbackVector::kFlagsTieringStateIsAnyRequested),
      &maybe_needs_logging);
  GenerateTailCallToReturnedCode(Runtime::kCompileOptimized, function);

  BIND(&maybe_needs_logging);
  {
#ifdef V8_ENABLE_LEAPTIERING
    // In the leaptiering case, we don't tier up to optimized code through the
    // feedback vector (but instead through the dispatch table), so we can only
    // get here if kFlagsLogNextExecution is set.
    CSA_DCHECK(this,
               IsSetWord32(flags, FeedbackVector::kFlagsLogNextExecution));
#else
    GotoIfNot(IsSetWord32(flags, FeedbackVector::kFlagsLogNextExecution),
              &may_have_optimized_code);
#endif
    GenerateTailCallToReturnedCode(Runtime::kFunctionLogNextExecution,
                                   function);
  }

#ifndef V8_ENABLE_LEAPTIERING
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
#endif  // V8_ENABLE_LEAPTIERING

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
  USE(sfi_code);
#ifndef V8_ENABLE_LEAPTIERING
  // In the leaptiering case, the code is installed below, through the
  // InstallSFICode runtime function.
  StoreCodePointerField(function, JSFunction::kCodeOffset, sfi_code);
#endif  // V8_ENABLE_LEAPTIERING

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
#ifdef V8_ENABLE_LEAPTIERING
  // In the leaptiering case, we now simply install the code of the SFI on the
  // function's dispatch table entry and call it. Installing the code is
  // necessary as the dispatch table entry may still contain the CompileLazy
  // builtin at this point (we can only update dispatch table code from C++).
  GenerateTailCallToReturnedCode(Runtime::kInstallSFICode, function);
#else
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
      [=, this]() {
        return CAST(CallRuntime(Runtime::kInstallBaselineCode,
                                Parameter<Context>(Descriptor::kContext),
                                function));
      });
  Goto(&tailcall_code);

  BIND(&tailcall_code);
  GenerateTailCallToJSCode(code.value(), function);
#endif  // V8_ENABLE_LEAPTIERING

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
#ifndef V8_ENABLE_LEAPTIERING
  // Set the code slot inside the JSFunction to CompileLazy.
  StoreCodePointerField(function, JSFunction::kCodeOffset, code);
#endif  // V8_ENABLE_LEAPTIERING
  GenerateTailCallToJSCode(code, function);
}

#include "src/codegen/undef-code-stub-assembler-macros.inc"

}  // namespace internal
}  // namespace v8
