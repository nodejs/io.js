// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/api/api-inl.h"
#include "src/api/api.h"
#include "src/builtins/builtins.h"
#include "src/common/message-template.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/messages.h"
#include "src/execution/tiering-manager.h"
#include "src/handles/maybe-handles.h"
#include "src/logging/counters.h"
#include "src/numbers/conversions.h"
#include "src/objects/template-objects-inl.h"
#include "src/utils/ostreams.h"

#if V8_ENABLE_WEBASSEMBLY
// TODO(chromium:1236668): Drop this when the "SaveAndClearThreadInWasmFlag"
// approach is no longer needed.
#include "src/trap-handler/trap-handler.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_AccessCheck) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSObject> object = args.at<JSObject>(0);
  if (!isolate->MayAccess(isolate->native_context(), object)) {
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                isolate->ReportFailedAccessCheck(object));
    UNREACHABLE();
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_FatalProcessOutOfMemoryInAllocateRaw) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  isolate->heap()->FatalProcessOutOfMemory("CodeStubAssembler::AllocateRaw");
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_FatalProcessOutOfMemoryInvalidArrayLength) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  isolate->heap()->FatalProcessOutOfMemory("invalid array length");
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_FatalInvalidSize) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  FATAL("Invalid size");
  UNREACHABLE();
}

RUNTIME_FUNCTION(Runtime_Throw) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->Throw(args[0]);
}

RUNTIME_FUNCTION(Runtime_ReThrow) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->ReThrow(args[0]);
}

RUNTIME_FUNCTION(Runtime_ReThrowWithMessage) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  return isolate->ReThrow(args[0], args[1]);
}

RUNTIME_FUNCTION(Runtime_ThrowStackOverflow) {
  SealHandleScope shs(isolate);
  DCHECK_LE(0, args.length());
  return isolate->StackOverflow();
}

RUNTIME_FUNCTION(Runtime_ThrowSymbolAsyncIteratorInvalid) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kSymbolAsyncIteratorInvalid));
}

RUNTIME_FUNCTION(Runtime_TerminateExecution) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  return isolate->TerminateExecution();
}

namespace {

Tagged<Object> NewError(Isolate* isolate, RuntimeArguments args,
                        Handle<JSFunction> (Isolate::*constructor_fn)()) {
  HandleScope scope(isolate);
  DCHECK_LE(1, args.length());
  int message_id_smi = args.smi_value_at(0);

  constexpr int kMaxMessageArgs = 3;
  Handle<Object> message_args[kMaxMessageArgs];
  int num_message_args = 0;
  while (num_message_args < kMaxMessageArgs &&
         args.length() > num_message_args + 1) {
    message_args[num_message_args] = args.at(num_message_args + 1);
    ++num_message_args;
  }

  MessageTemplate message_id = MessageTemplateFromInt(message_id_smi);

  return *isolate->factory()->NewError(
      (isolate->*constructor_fn)(), message_id,
      base::VectorOf(message_args, num_message_args));
}

Tagged<Object> ThrowError(Isolate* isolate, RuntimeArguments args,
                          Handle<JSFunction> (Isolate::*constructor_fn)()) {
  return isolate->Throw(NewError(isolate, args, constructor_fn));
}

}  // namespace

RUNTIME_FUNCTION(Runtime_ThrowRangeError) {
  if (v8_flags.correctness_fuzzer_suppressions) {
    DCHECK_LE(1, args.length());
    int message_id_smi = args.smi_value_at(0);

    // If the result of a BigInt computation is truncated to 64 bit, Turbofan
    // can sometimes truncate intermediate results already, which can prevent
    // those from exceeding the maximum length, effectively preventing a
    // RangeError from being thrown. As this is a performance optimization, this
    // behavior is accepted. To prevent the correctness fuzzer from detecting
    // this difference, we crash the program.
    if (MessageTemplateFromInt(message_id_smi) ==
        MessageTemplate::kBigIntTooBig) {
      FATAL("Aborting on invalid BigInt length");
    }
  }

  return ThrowError(isolate, args, &Isolate::range_error_function);
}

RUNTIME_FUNCTION(Runtime_ThrowTypeError) {
  return ThrowError(isolate, args, &Isolate::type_error_function);
}

RUNTIME_FUNCTION(Runtime_ThrowTypeErrorIfStrict) {
  if (GetShouldThrow(isolate, Nothing<ShouldThrow>()) ==
      ShouldThrow::kDontThrow) {
    return ReadOnlyRoots(isolate).undefined_value();
  }
  return ThrowError(isolate, args, &Isolate::type_error_function);
}

namespace {

const char* ElementsKindToType(ElementsKind fixed_elements_kind) {
  switch (fixed_elements_kind) {
#define ELEMENTS_KIND_CASE(Type, type, TYPE, ctype) \
  case TYPE##_ELEMENTS:                             \
    return #Type "Array";

    TYPED_ARRAYS(ELEMENTS_KIND_CASE)
    RAB_GSAB_TYPED_ARRAYS_WITH_TYPED_ARRAY_TYPE(ELEMENTS_KIND_CASE)
#undef ELEMENTS_KIND_CASE

    default:
      UNREACHABLE();
  }
}

}  // namespace

RUNTIME_FUNCTION(Runtime_ThrowInvalidTypedArrayAlignment) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Map> map = args.at<Map>(0);
  Handle<String> problem_string = args.at<String>(1);

  ElementsKind kind = map->elements_kind();

  Handle<String> type =
      isolate->factory()->NewStringFromAsciiChecked(ElementsKindToType(kind));

  ExternalArrayType external_type;
  size_t size;
  Factory::TypeAndSizeForElementsKind(kind, &external_type, &size);
  Handle<Object> element_size =
      handle(Smi::FromInt(static_cast<int>(size)), isolate);

  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewRangeError(MessageTemplate::kInvalidTypedArrayAlignment,
                             problem_string, type, element_size));
}

RUNTIME_FUNCTION(Runtime_UnwindAndFindExceptionHandler) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  return isolate->UnwindAndFindHandler();
}

RUNTIME_FUNCTION(Runtime_PropagateException) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  DCHECK(isolate->has_exception());
  return ReadOnlyRoots(isolate).exception();
}

RUNTIME_FUNCTION(Runtime_ThrowReferenceError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> name = args.at(0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewReferenceError(MessageTemplate::kNotDefined, name));
}

RUNTIME_FUNCTION(Runtime_ThrowAccessedUninitializedVariable) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> name = args.at(0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewReferenceError(MessageTemplate::kAccessedUninitializedVariable, name));
}

RUNTIME_FUNCTION(Runtime_NewError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  int template_index = args.smi_value_at(0);
  Handle<Object> arg0 = args.at(1);
  MessageTemplate message_template = MessageTemplateFromInt(template_index);
  return *isolate->factory()->NewError(message_template, arg0);
}

RUNTIME_FUNCTION(Runtime_NewForeign) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  return *isolate->factory()->NewForeign(kNullAddress);
}

RUNTIME_FUNCTION(Runtime_NewTypeError) {
  return NewError(isolate, args, &Isolate::type_error_function);
}

RUNTIME_FUNCTION(Runtime_NewReferenceError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  int template_index = args.smi_value_at(0);
  Handle<Object> arg0 = args.at(1);
  MessageTemplate message_template = MessageTemplateFromInt(template_index);
  return *isolate->factory()->NewReferenceError(message_template, arg0);
}

RUNTIME_FUNCTION(Runtime_NewSyntaxError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  int template_index = args.smi_value_at(0);
  Handle<Object> arg0 = args.at(1);
  MessageTemplate message_template = MessageTemplateFromInt(template_index);
  return *isolate->factory()->NewSyntaxError(message_template, arg0);
}

RUNTIME_FUNCTION(Runtime_ThrowInvalidStringLength) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewInvalidStringLengthError());
}

RUNTIME_FUNCTION(Runtime_ThrowIteratorResultNotAnObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> value = args.at(0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kIteratorResultNotAnObject, value));
}

RUNTIME_FUNCTION(Runtime_ThrowThrowMethodMissing) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kThrowMethodMissing));
}

RUNTIME_FUNCTION(Runtime_ThrowSymbolIteratorInvalid) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kSymbolIteratorInvalid));
}

RUNTIME_FUNCTION(Runtime_ThrowNoAccess) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());

  // TODO(verwaest): We would like to throw using the calling context instead
  // of the entered context but we don't currently have access to that.
  HandleScopeImplementer* impl = isolate->handle_scope_implementer();
  SaveAndSwitchContext save(isolate,
                            impl->LastEnteredContext()->native_context());
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(MessageTemplate::kNoAccess));
}

RUNTIME_FUNCTION(Runtime_ThrowNotConstructor) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotConstructor, object));
}

RUNTIME_FUNCTION(Runtime_ThrowApplyNonFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  Handle<String> type = Object::TypeOf(isolate, object);
  Handle<String> msg;
  if (IsNull(*object)) {
    // "which is null"
    msg = isolate->factory()->NewStringFromAsciiChecked("null");
  } else if (isolate->factory()->object_string()->Equals(*type)) {
    // "which is an object"
    msg = isolate->factory()->NewStringFromAsciiChecked("an object");
  } else {
    // "which is a typeof arg"
    msg = isolate->factory()
              ->NewConsString(
                  isolate->factory()->NewStringFromAsciiChecked("a "), type)
              .ToHandleChecked();
  }
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kApplyNonFunction, object, msg));
}

RUNTIME_FUNCTION(Runtime_StackGuard) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  TRACE_EVENT0("v8.execute", "V8.StackGuard");

  // First check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed()) {
    return isolate->StackOverflow();
  }

  return isolate->stack_guard()->HandleInterrupts(
      StackGuard::InterruptLevel::kAnyEffect);
}

RUNTIME_FUNCTION(Runtime_HandleNoHeapWritesInterrupts) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());
  TRACE_EVENT0("v8.execute", "V8.StackGuard");

  // First check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed()) {
    return isolate->StackOverflow();
  }

  return isolate->stack_guard()->HandleInterrupts(
      StackGuard::InterruptLevel::kNoHeapWrites);
}

RUNTIME_FUNCTION(Runtime_StackGuardWithGap) {
  SealHandleScope shs(isolate);
  DCHECK_EQ(args.length(), 1);
  uint32_t gap = args.positive_smi_value_at(0);
  TRACE_EVENT0("v8.execute", "V8.StackGuard");

  // First check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed(gap)) {
    return isolate->StackOverflow();
  }

  return isolate->stack_guard()->HandleInterrupts(
      StackGuard::InterruptLevel::kAnyEffect);
}

namespace {

Tagged<Object> BytecodeBudgetInterruptWithStackCheck(Isolate* isolate,
                                                     RuntimeArguments& args,
                                                     CodeKind code_kind) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> function = args.at<JSFunction>(0);
  TRACE_EVENT0("v8.execute", "V8.BytecodeBudgetInterruptWithStackCheck");

  // Check for stack interrupts here so that we can fold the interrupt check
  // into bytecode budget interrupts.
  StackLimitCheck check(isolate);
  if (check.JsHasOverflowed()) {
    // We ideally wouldn't actually get StackOverflows here, since we stack
    // check on bytecode entry, but it's possible that this check fires due to
    // the runtime function call being what overflows the stack.
    return isolate->StackOverflow();
  } else if (check.InterruptRequested()) {
    Tagged<Object> return_value = isolate->stack_guard()->HandleInterrupts();
    if (!IsUndefined(return_value, isolate)) {
      return return_value;
    }
  }

  isolate->tiering_manager()->OnInterruptTick(function, code_kind);
  return ReadOnlyRoots(isolate).undefined_value();
}

Tagged<Object> BytecodeBudgetInterrupt(Isolate* isolate, RuntimeArguments& args,
                                       CodeKind code_kind) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> function = args.at<JSFunction>(0);
  TRACE_EVENT0("v8.execute", "V8.BytecodeBudgetInterrupt");

  isolate->tiering_manager()->OnInterruptTick(function, code_kind);
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterruptWithStackCheck_Ignition) {
  return BytecodeBudgetInterruptWithStackCheck(isolate, args,
                                               CodeKind::INTERPRETED_FUNCTION);
}

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterrupt_Ignition) {
  return BytecodeBudgetInterrupt(isolate, args, CodeKind::INTERPRETED_FUNCTION);
}

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterruptWithStackCheck_Sparkplug) {
  return BytecodeBudgetInterruptWithStackCheck(isolate, args,
                                               CodeKind::BASELINE);
}

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterrupt_Sparkplug) {
  return BytecodeBudgetInterrupt(isolate, args, CodeKind::BASELINE);
}

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterrupt_Maglev) {
  return BytecodeBudgetInterrupt(isolate, args, CodeKind::MAGLEV);
}

RUNTIME_FUNCTION(Runtime_BytecodeBudgetInterruptWithStackCheck_Maglev) {
  return BytecodeBudgetInterruptWithStackCheck(isolate, args, CodeKind::MAGLEV);
}

namespace {

#if V8_ENABLE_WEBASSEMBLY
class V8_NODISCARD SaveAndClearThreadInWasmFlag {
 public:
  SaveAndClearThreadInWasmFlag() {
    if (trap_handler::IsTrapHandlerEnabled()) {
      if (trap_handler::IsThreadInWasm()) {
        thread_was_in_wasm_ = true;
        trap_handler::ClearThreadInWasm();
      }
    }
  }
  ~SaveAndClearThreadInWasmFlag() {
    if (thread_was_in_wasm_) {
      trap_handler::SetThreadInWasm();
    }
  }

 private:
  bool thread_was_in_wasm_{false};
};
#else
class SaveAndClearThreadInWasmFlag {};
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace

RUNTIME_FUNCTION(Runtime_AllocateInYoungGeneration) {
  HandleScope scope(isolate);
  DCHECK(isolate->IsOnCentralStack());
  DCHECK_EQ(2, args.length());
  // TODO(v8:13070): Align allocations in the builtins that call this.
  int size = ALIGN_TO_ALLOCATION_ALIGNMENT(args.smi_value_at(0));
  int flags = args.smi_value_at(1);
  AllocationAlignment alignment =
      AllocateDoubleAlignFlag::decode(flags) ? kDoubleAligned : kTaggedAligned;
  CHECK(IsAligned(size, kTaggedSize));
  CHECK_GT(size, 0);

#if V8_ENABLE_WEBASSEMBLY
  // When this is called from WasmGC code, clear the "thread in wasm" flag,
  // which is important in case any GC needs to happen.
  // TODO(chromium:1236668): Find a better fix, likely by replacing the global
  // flag.
  SaveAndClearThreadInWasmFlag clear_wasm_flag;
#endif  // V8_ENABLE_WEBASSEMBLY

  // TODO(v8:9472): Until double-aligned allocation is fixed for new-space
  // allocations, don't request it.
  alignment = kTaggedAligned;

  return *isolate->factory()->NewFillerObject(size, alignment,
                                              AllocationType::kYoung,
                                              AllocationOrigin::kGeneratedCode);
}

RUNTIME_FUNCTION(Runtime_AllocateInOldGeneration) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  // TODO(v8:13070): Align allocations in the builtins that call this.
  int size = ALIGN_TO_ALLOCATION_ALIGNMENT(args.smi_value_at(0));
  int flags = args.smi_value_at(1);
  AllocationAlignment alignment =
      AllocateDoubleAlignFlag::decode(flags) ? kDoubleAligned : kTaggedAligned;
  CHECK(IsAligned(size, kTaggedSize));
  CHECK_GT(size, 0);
  return *isolate->factory()->NewFillerObject(
      size, alignment, AllocationType::kOld, AllocationOrigin::kGeneratedCode);
}

RUNTIME_FUNCTION(Runtime_AllocateByteArray) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int length = args.smi_value_at(0);
  DCHECK_LT(0, length);
  return *isolate->factory()->NewByteArray(length);
}

RUNTIME_FUNCTION(Runtime_AllocateSeqOneByteString) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int length = args.smi_value_at(0);
  if (length == 0) return ReadOnlyRoots(isolate).empty_string();
  Handle<SeqOneByteString> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result, isolate->factory()->NewRawOneByteString(length));
  return *result;
}

RUNTIME_FUNCTION(Runtime_AllocateSeqTwoByteString) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int length = args.smi_value_at(0);
  if (length == 0) return ReadOnlyRoots(isolate).empty_string();
  Handle<SeqTwoByteString> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result, isolate->factory()->NewRawTwoByteString(length));
  return *result;
}

RUNTIME_FUNCTION(Runtime_ThrowIteratorError) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  return isolate->Throw(*ErrorUtils::NewIteratorError(isolate, object));
}

RUNTIME_FUNCTION(Runtime_ThrowSpreadArgError) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  int message_id_smi = args.smi_value_at(0);
  MessageTemplate message_id = MessageTemplateFromInt(message_id_smi);
  Handle<Object> object = args.at(1);
  return ErrorUtils::ThrowSpreadArgError(isolate, message_id, object);
}

RUNTIME_FUNCTION(Runtime_ThrowCalledNonCallable) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  return isolate->Throw(
      *ErrorUtils::NewCalledNonCallableError(isolate, object));
}

RUNTIME_FUNCTION(Runtime_ThrowConstructedNonConstructable) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  return isolate->Throw(
      *ErrorUtils::NewConstructedNonConstructable(isolate, object));
}

RUNTIME_FUNCTION(Runtime_ThrowPatternAssignmentNonCoercible) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  return ErrorUtils::ThrowLoadFromNullOrUndefined(isolate, object,
                                                  MaybeHandle<Object>());
}

RUNTIME_FUNCTION(Runtime_ThrowConstructorReturnedNonObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());

  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate,
      NewTypeError(MessageTemplate::kDerivedConstructorReturnedNonObject));
}

// ES6 section 7.3.17 CreateListFromArrayLike (obj)
RUNTIME_FUNCTION(Runtime_CreateListFromArrayLike) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  RETURN_RESULT_OR_FAILURE(isolate, Object::CreateListFromArrayLike(
                                        isolate, object, ElementTypes::kAll));
}

RUNTIME_FUNCTION(Runtime_IncrementUseCounter) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int counter = args.smi_value_at(0);
  isolate->CountUsage(static_cast<v8::Isolate::UseCounterFeature>(counter));
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_GetAndResetTurboProfilingData) {
  HandleScope scope(isolate);
  DCHECK_LE(args.length(), 2);
  if (!BasicBlockProfiler::Get()->HasData(isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(
            MessageTemplate::kInvalid,
            isolate->factory()->NewStringFromAsciiChecked("Runtime Call"),
            isolate->factory()->NewStringFromAsciiChecked(
                "V8 was not built with v8_enable_builtins_profiling=true")));
  }

  std::stringstream stats_stream;
  BasicBlockProfiler::Get()->Log(isolate, stats_stream);
  Handle<String> result =
      isolate->factory()->NewStringFromAsciiChecked(stats_stream.str().c_str());
  BasicBlockProfiler::Get()->ResetCounts(isolate);
  return *result;
}

RUNTIME_FUNCTION(Runtime_GetAndResetRuntimeCallStats) {
  HandleScope scope(isolate);
  DCHECK_LE(args.length(), 2);
#ifdef V8_RUNTIME_CALL_STATS
  if (!v8_flags.runtime_call_stats) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalid,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "Runtime Call"),
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "--runtime-call-stats is not set")));
  }
  // Append any worker thread runtime call stats to the main table before
  // printing.
  isolate->counters()->worker_thread_runtime_call_stats()->AddToMainTable(
      isolate->counters()->runtime_call_stats());

  if (args.length() == 0) {
    // Without arguments, the result is returned as a string.
    std::stringstream stats_stream;
    isolate->counters()->runtime_call_stats()->Print(stats_stream);
    Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(
        stats_stream.str().c_str());
    isolate->counters()->runtime_call_stats()->Reset();
    return *result;
  }

  std::FILE* f;
  if (IsString(args[0])) {
    // With a string argument, the results are appended to that file.
    Handle<String> filename = args.at<String>(0);
    f = std::fopen(filename->ToCString().get(), "a");
    DCHECK_NOT_NULL(f);
  } else {
    // With an integer argument, the results are written to stdout/stderr.
    int fd = args.smi_value_at(0);
    DCHECK(fd == 1 || fd == 2);
    f = fd == 1 ? stdout : stderr;
  }
  // The second argument (if any) is a message header to be printed.
  if (args.length() >= 2) {
    Handle<String> message = args.at<String>(1);
    message->PrintOn(f);
    std::fputc('\n', f);
    std::fflush(f);
  }
  OFStream stats_stream(f);
  isolate->counters()->runtime_call_stats()->Print(stats_stream);
  isolate->counters()->runtime_call_stats()->Reset();
  if (IsString(args[0])) {
    std::fclose(f);
  } else {
    std::fflush(f);
  }
  return ReadOnlyRoots(isolate).undefined_value();
#else   // V8_RUNTIME_CALL_STATS
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kInvalid,
                            isolate->factory()->NewStringFromAsciiChecked(
                                "Runtime Call"),
                            isolate->factory()->NewStringFromAsciiChecked(
                                "RCS was disabled at compile-time")));
#endif  // V8_RUNTIME_CALL_STATS
}

RUNTIME_FUNCTION(Runtime_OrdinaryHasInstance) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<Object> callable = args.at(0);
  Handle<Object> object = args.at(1);
  RETURN_RESULT_OR_FAILURE(
      isolate, Object::OrdinaryHasInstance(isolate, callable, object));
}

RUNTIME_FUNCTION(Runtime_Typeof) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<Object> object = args.at(0);
  return *Object::TypeOf(isolate, object);
}

RUNTIME_FUNCTION(Runtime_AllowDynamicFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> target = args.at<JSFunction>(0);
  Handle<JSObject> global_proxy(target->global_proxy(), isolate);
  return *isolate->factory()->ToBoolean(
      Builtins::AllowDynamicFunction(isolate, target, global_proxy));
}

RUNTIME_FUNCTION(Runtime_CreateAsyncFromSyncIterator) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  Handle<Object> sync_iterator = args.at(0);

  if (!IsJSReceiver(*sync_iterator)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kSymbolIteratorInvalid));
  }

  Handle<Object> next;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, next,
      Object::GetProperty(isolate, sync_iterator,
                          isolate->factory()->next_string()));

  return *isolate->factory()->NewJSAsyncFromSyncIterator(
      Handle<JSReceiver>::cast(sync_iterator), next);
}

RUNTIME_FUNCTION(Runtime_GetTemplateObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<TemplateObjectDescription> description =
      args.at<TemplateObjectDescription>(0);
  Handle<SharedFunctionInfo> shared_info = args.at<SharedFunctionInfo>(1);
  int slot_id = args.smi_value_at(2);

  Handle<NativeContext> native_context(isolate->context()->native_context(),
                                       isolate);
  return *TemplateObjectDescription::GetTemplateObject(
      isolate, native_context, description, shared_info, slot_id);
}

RUNTIME_FUNCTION(Runtime_ReportMessageFromMicrotask) {
  // Helper to report messages and continue JS execution. This is intended to
  // behave similarly to reporting exceptions which reach the top-level, but
  // allow the JS code to continue.
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  Handle<Object> exception = args.at(0);

  DCHECK(!isolate->has_exception());
  isolate->set_exception(*exception);
  MessageLocation* no_location = nullptr;
  Handle<JSMessageObject> message =
      isolate->CreateMessageOrAbort(exception, no_location);
  MessageHandler::ReportMessage(isolate, no_location, message);
  isolate->clear_exception();
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_GetInitializerFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  Handle<JSReceiver> constructor = args.at<JSReceiver>(0);
  Handle<Symbol> key = isolate->factory()->class_fields_symbol();
  Handle<Object> initializer =
      JSReceiver::GetDataProperty(isolate, constructor, key);
  return *initializer;
}

RUNTIME_FUNCTION(Runtime_DoubleToStringWithRadix) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  double number = args.number_value_at(0);
  int32_t radix = 0;
  CHECK(Object::ToInt32(args[1], &radix));

  char* const str = DoubleToRadixCString(number, radix);
  Handle<String> result = isolate->factory()->NewStringFromAsciiChecked(str);
  DeleteArray(str);
  return *result;
}

RUNTIME_FUNCTION(Runtime_SharedValueBarrierSlow) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<HeapObject> value = args.at<HeapObject>(0);
  Handle<Object> shared_value;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, shared_value, Object::ShareSlow(isolate, value, kThrowOnError));
  return *shared_value;
}

RUNTIME_FUNCTION(Runtime_InvalidateDependentCodeForConstTrackingLet) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<ConstTrackingLetCell> const_tracking_let_cell =
      Handle<ConstTrackingLetCell>::cast(args.at<HeapObject>(0));
  DependentCode::DeoptimizeDependencyGroups(
      isolate, *const_tracking_let_cell,
      DependentCode::kConstTrackingLetChangedGroup);
  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace internal
}  // namespace v8
