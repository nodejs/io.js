// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <optional>

#include "src/builtins/builtins-inl.h"
#include "src/builtins/data-view-ops.h"
#include "src/common/assert-scope.h"
#include "src/common/message-template.h"
#include "src/compiler/wasm-compiler.h"
#include "src/debug/debug.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/frames.h"
#include "src/heap/factory.h"
#include "src/numbers/conversions.h"
#include "src/objects/objects-inl.h"
#include "src/strings/unicode-inl.h"
#include "src/trap-handler/trap-handler.h"
#include "src/wasm/compilation-environment-inl.h"
#include "src/wasm/module-compiler.h"
#include "src/wasm/serialized-signature-inl.h"
#include "src/wasm/value-type.h"
#include "src/wasm/wasm-code-manager.h"
#include "src/wasm/wasm-constants.h"
#include "src/wasm/wasm-debug.h"
#include "src/wasm/wasm-engine.h"
#include "src/wasm/wasm-objects.h"
#include "src/wasm/wasm-opcodes-inl.h"
#include "src/wasm/wasm-subtyping.h"
#include "src/wasm/wasm-value.h"

#if V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE
#include "src/wasm/interpreter/wasm-interpreter.h"
#endif  // V8_ENABLE_WEBASSEMBLY && V8_ENABLE_DRUMBRAKE

namespace v8::internal {

// TODO(13036): See if we can find a way to have the stack walker visit
// tagged values being passed from Wasm to runtime functions. In the meantime,
// disallow access to safe-looking-but-actually-unsafe stack-backed handles
// and thereby force manual creation of safe handles (backed by HandleScope).
class RuntimeArgumentsWithoutHandles : public RuntimeArguments {
 public:
  RuntimeArgumentsWithoutHandles(int length, Address* arguments)
      : RuntimeArguments(length, arguments) {}

 private:
  // Disallowing the superclass method.
  template <class S = Object>
  V8_INLINE Handle<S> at(int index) const;
};

#define RuntimeArguments RuntimeArgumentsWithoutHandles

// (End of TODO(13036)-related hackery.)

namespace {

template <typename FrameType>
class FrameFinder {
 public:
  explicit FrameFinder(Isolate* isolate,
                       std::initializer_list<StackFrame::Type>
                           skipped_frame_types = {StackFrame::EXIT})
      : frame_iterator_(isolate, isolate->thread_local_top()) {
    // We skip at least one frame.
    DCHECK_LT(0, skipped_frame_types.size());

    for (auto type : skipped_frame_types) {
      DCHECK_EQ(type, frame_iterator_.frame()->type());
      USE(type);
      frame_iterator_.Advance();
    }
    // Type check the frame where the iterator stopped now.
    DCHECK_NOT_NULL(frame());
  }

  FrameType* frame() { return FrameType::cast(frame_iterator_.frame()); }

 private:
  StackFrameIterator frame_iterator_;
};

Tagged<WasmTrustedInstanceData> GetWasmInstanceDataOnStackTop(
    Isolate* isolate) {
  Address fp = Isolate::c_entry_fp(isolate->thread_local_top());
  fp = Memory<Address>(fp + ExitFrameConstants::kCallerFPOffset);
#ifdef DEBUG
  intptr_t marker =
      Memory<intptr_t>(fp + CommonFrameConstants::kContextOrFrameTypeOffset);
  DCHECK(StackFrame::MarkerToType(marker) == StackFrame::WASM);
#endif
  const int offset = WasmFrameConstants::kWasmInstanceOffset;
  Tagged<Object> trusted_instance_data(Memory<Address>(fp + offset));
  return Cast<WasmTrustedInstanceData>(trusted_instance_data);
}

Tagged<Context> GetNativeContextFromWasmInstanceOnStackTop(Isolate* isolate) {
  return GetWasmInstanceDataOnStackTop(isolate)->native_context();
}

class V8_NODISCARD ClearThreadInWasmScope {
 public:
  explicit ClearThreadInWasmScope(Isolate* isolate)
      : isolate_(isolate), is_thread_in_wasm_(trap_handler::IsThreadInWasm()) {
    // In some cases we call this from Wasm code inlined into JavaScript
    // so the flag might not be set.
    if (is_thread_in_wasm_) {
      trap_handler::ClearThreadInWasm();
    }

#if V8_ENABLE_DRUMBRAKE
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      isolate->wasm_execution_timer()->Stop();
    }
#endif  // V8_ENABLE_DRUMBRAKE
  }
  ~ClearThreadInWasmScope() {
    DCHECK_IMPLIES(trap_handler::IsTrapHandlerEnabled(),
                   !trap_handler::IsThreadInWasm());
    if (!isolate_->has_exception() && is_thread_in_wasm_) {
      trap_handler::SetThreadInWasm();

#if V8_ENABLE_DRUMBRAKE
      if (v8_flags.wasm_enable_exec_time_histograms &&
          v8_flags.slow_histograms && !v8_flags.wasm_jitless) {
        isolate_->wasm_execution_timer()->Start();
      }
#endif  // V8_ENABLE_DRUMBRAKE
    }
    // Otherwise we only want to set the flag if the exception is caught in
    // wasm. This is handled by the unwinder.
  }

 private:
  Isolate* isolate_;
  const bool is_thread_in_wasm_;
};

Tagged<Object> ThrowWasmError(
    Isolate* isolate, MessageTemplate message,
    std::initializer_list<DirectHandle<Object>> args = {}) {
#if V8_ENABLE_DRUMBRAKE
  if (v8_flags.wasm_jitless) {
    // Store the trap reason to be retrieved later when the interpreter will
    // trap while detecting the thrown exception.
    wasm::WasmInterpreterThread::SetRuntimeLastWasmError(isolate, message);
  }
#endif  // V8_ENABLE_DRUMBRAKE

  Handle<JSObject> error_obj =
      isolate->factory()->NewWasmRuntimeError(message, base::VectorOf(args));
  JSObject::AddProperty(isolate, error_obj,
                        isolate->factory()->wasm_uncatchable_symbol(),
                        isolate->factory()->true_value(), NONE);
  return isolate->Throw(*error_obj);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmGenericWasmToJSObject) {
  SealHandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Tagged<Object> value = args[0];
  if (IsWasmFuncRef(value)) {
    Tagged<WasmInternalFunction> internal =
        Cast<WasmFuncRef>(value)->internal(isolate);
    Tagged<JSFunction> external;
    if (internal->try_get_external(&external)) return external;
    // Slow path:
    HandleScope scope(isolate);
    return *WasmInternalFunction::GetOrCreateExternal(
        handle(internal, isolate));
  }
  if (IsWasmNull(value)) return ReadOnlyRoots(isolate).null_value();
  return value;
}

// Takes a JS object and a wasm type as Smi. Type checks the object against the
// type; if the check succeeds, returns the object in its wasm representation;
// otherwise throws a type error.
RUNTIME_FUNCTION(Runtime_WasmGenericJSToWasmObject) {
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> value(args[1], isolate);
  // Make sure ValueType fits properly in a Smi.
  static_assert(wasm::ValueType::kLastUsedBit + 1 <= kSmiValueSize);
  int raw_type = args.smi_value_at(2);

  wasm::ValueType type = wasm::ValueType::FromRawBitField(raw_type);
  uint32_t canonical_index = wasm::kInvalidCanonicalIndex;
  if (type.has_index()) {
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
        Cast<WasmTrustedInstanceData>(args[0]), isolate);
    const wasm::WasmModule* module = trusted_instance_data->module();
    DCHECK_NOT_NULL(module);
    canonical_index = module->isorecursive_canonical_type_ids[type.ref_index()];
  }
  const char* error_message;
  Handle<Object> result;
  if (!JSToWasmObject(isolate, value, type, canonical_index, &error_message)
           .ToHandle(&result)) {
    return isolate->Throw(*isolate->factory()->NewTypeError(
        MessageTemplate::kWasmTrapJSTypeError));
  }
  return *result;
}

// Parameters:
// args[0]: the object, any JS value.
// args[1]: the expected ValueType, Smi-tagged.
// args[2]: the expected canonical type index, Smi-tagged, if the type is
//          an indexed reftype.
// Type checks the object against the type; if the check succeeds, returns the
// object in its wasm representation; otherwise throws a type error.
RUNTIME_FUNCTION(Runtime_WasmJSToWasmObject) {
  // TODO(manoskouk): Use {SaveAndClearThreadInWasmFlag} in runtime-internal.cc
  // and runtime-strings.cc.
  bool thread_in_wasm = trap_handler::IsThreadInWasm();
  if (thread_in_wasm) trap_handler::ClearThreadInWasm();
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Handle<Object> value(args[0], isolate);
  // Make sure ValueType fits properly in a Smi.
  static_assert(wasm::ValueType::kLastUsedBit + 1 <= kSmiValueSize);
  int raw_type = args.smi_value_at(1);
  int canonical_index = args.smi_value_at(2);

  wasm::ValueType expected = wasm::ValueType::FromRawBitField(raw_type);
  const char* error_message;
  Handle<Object> result;
  bool success =
      JSToWasmObject(isolate, value, expected, canonical_index, &error_message)
          .ToHandle(&result);
  Tagged<Object> ret = success
                           ? *result
                           : isolate->Throw(*isolate->factory()->NewTypeError(
                                 MessageTemplate::kWasmTrapJSTypeError));
  if (thread_in_wasm && !isolate->has_exception()) {
    trap_handler::SetThreadInWasm();
  }
  return ret;
}

RUNTIME_FUNCTION(Runtime_WasmMemoryGrow) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  // {memory_index} and {delta_pages} are checked to be positive Smis in the
  // WasmMemoryGrow builtin which calls this runtime function.
  uint32_t memory_index = args.positive_smi_value_at(1);
  uint32_t delta_pages = args.positive_smi_value_at(2);

  Handle<WasmMemoryObject> memory_object{
      trusted_instance_data->memory_object(memory_index), isolate};
  int ret = WasmMemoryObject::Grow(isolate, memory_object, delta_pages);
  // The WasmMemoryGrow builtin which calls this runtime function expects us to
  // always return a Smi.
  DCHECK(!isolate->has_exception());
  return Smi::FromInt(ret);
}

RUNTIME_FUNCTION(Runtime_TrapHandlerThrowWasmError) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  std::vector<FrameSummary> summary;
  FrameFinder<WasmFrame> frame_finder(isolate, {StackFrame::EXIT});
  WasmFrame* frame = frame_finder.frame();
  // TODO(ahaas): We cannot use frame->position() here because for inlined
  // function it does not return the correct source position. We should remove
  // frame->position() to avoid problems in the future.
  frame->Summarize(&summary);
  DCHECK(summary.back().IsWasm());
  int pos = summary.back().AsWasm().SourcePosition();

  wasm::WasmCodeRefScope code_ref_scope;
  auto wire_bytes = frame->wasm_code()->native_module()->wire_bytes();
  wasm::WasmOpcode op = static_cast<wasm::WasmOpcode>(wire_bytes.at(pos));
  MessageTemplate message = MessageTemplate::kWasmTrapMemOutOfBounds;
  if (op == wasm::kGCPrefix || op == wasm::kExprRefAsNonNull ||
      op == wasm::kExprCallRef || op == wasm::kExprReturnCallRef ||
      // Calling imported string function with null can trigger a signal.
      op == wasm::kExprCallFunction || op == wasm::kExprReturnCall) {
    message = MessageTemplate::kWasmTrapNullDereference;
#if DEBUG
  } else {
    if (wasm::WasmOpcodes::IsPrefixOpcode(op)) {
      op = wasm::Decoder{wire_bytes}
               .read_prefixed_opcode<wasm::Decoder::NoValidationTag>(
                   &wire_bytes.begin()[pos])
               .first;
    }
#endif  // DEBUG
  }
  return ThrowWasmError(isolate, message);
}

RUNTIME_FUNCTION(Runtime_ThrowWasmError) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  int message_id = args.smi_value_at(0);
  return ThrowWasmError(isolate, MessageTemplateFromInt(message_id));
}

RUNTIME_FUNCTION(Runtime_ThrowWasmStackOverflow) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  SealHandleScope shs(isolate);
  DCHECK_LE(0, args.length());
  return isolate->StackOverflow();
}

RUNTIME_FUNCTION(Runtime_WasmThrowJSTypeError) {
  // The caller may be wasm or JS. Only clear the thread_in_wasm flag if the
  // caller is wasm, and let the unwinder set it back depending on the handler.
  if (trap_handler::IsTrapHandlerEnabled() && trap_handler::IsThreadInWasm()) {
#if V8_ENABLE_DRUMBRAKE
    // Transitioning from Wasm To JS.
    if (v8_flags.wasm_enable_exec_time_histograms && v8_flags.slow_histograms &&
        !v8_flags.wasm_jitless) {
      // Stop measuring the time spent running jitted Wasm.
      isolate->wasm_execution_timer()->Stop();
    }
#endif  // V8_ENABLE_DRUMBRAKE

    trap_handler::ClearThreadInWasm();
  }
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kWasmTrapJSTypeError));
}

// This error is thrown from a wasm-to-JS wrapper, so unlike
// Runtime_ThrowWasmError, this function does not check or unset the
// thread-in-wasm flag.
RUNTIME_FUNCTION(Runtime_ThrowBadSuspenderError) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  return ThrowWasmError(isolate, MessageTemplate::kWasmTrapBadSuspender);
}

RUNTIME_FUNCTION(Runtime_WasmThrowRangeError) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewRangeError(message_id));
}

RUNTIME_FUNCTION(Runtime_WasmThrowDataViewTypeError) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  DataViewOp op = static_cast<DataViewOp>(isolate->error_message_param());
  Handle<String> op_name =
      isolate->factory()->NewStringFromAsciiChecked(ToString(op));
  Handle<Object> value(args[1], isolate);

  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(message_id, op_name, value));
}

RUNTIME_FUNCTION(Runtime_WasmThrowDataViewDetachedError) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  DataViewOp op = static_cast<DataViewOp>(isolate->error_message_param());
  Handle<String> op_name =
      isolate->factory()->NewStringFromAsciiChecked(ToString(op));

  THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id, op_name));
}

RUNTIME_FUNCTION(Runtime_WasmThrowTypeError) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  MessageTemplate message_id = MessageTemplateFromInt(args.smi_value_at(0));
  Handle<Object> arg(args[1], isolate);
  if (IsSmi(*arg)) {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id));
  } else {
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message_id, arg));
  }
}

RUNTIME_FUNCTION(Runtime_WasmThrow) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<Context> context = GetNativeContextFromWasmInstanceOnStackTop(isolate);
  isolate->set_context(context);
  DirectHandle<WasmExceptionTag> tag(Cast<WasmExceptionTag>(args[0]), isolate);
  DirectHandle<FixedArray> values(Cast<FixedArray>(args[1]), isolate);
  auto js_tag = Cast<WasmTagObject>(context->wasm_js_tag());
  if (*tag == js_tag->tag()) {
    return isolate->Throw(values->get(0));
  } else {
    DirectHandle<WasmExceptionPackage> exception =
        WasmExceptionPackage::New(isolate, tag, values);
    return isolate->Throw(*exception);
  }
}

RUNTIME_FUNCTION(Runtime_WasmReThrow) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  return isolate->ReThrow(args[0]);
}

RUNTIME_FUNCTION(Runtime_WasmStackGuard) {
  ClearThreadInWasmScope wasm_flag(isolate);
  SealHandleScope shs(isolate);
  DCHECK_EQ(0, args.length());

  // Check if this is a real stack overflow.
  StackLimitCheck check(isolate);
  if (check.WasmHasOverflowed()) return isolate->StackOverflow();

  return isolate->stack_guard()->HandleInterrupts(
      StackGuard::InterruptLevel::kAnyEffect);
}

RUNTIME_FUNCTION(Runtime_WasmCompileLazy) {
  ClearThreadInWasmScope wasm_flag(isolate);
  DCHECK_EQ(2, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int func_index = args.smi_value_at(1);

  TRACE_EVENT1("v8.wasm", "wasm.CompileLazy", "func_index", func_index);
  DisallowHeapAllocation no_gc;
  SealHandleScope scope(isolate);

  DCHECK(isolate->context().is_null());
  isolate->set_context(trusted_instance_data->native_context());
  bool success = wasm::CompileLazy(isolate, trusted_instance_data, func_index);
  if (!success) {
    DCHECK(v8_flags.wasm_lazy_validation);
    AllowHeapAllocation throwing_unwinds_the_stack;
    wasm::ThrowLazyCompilationError(
        isolate, trusted_instance_data->native_module(), func_index);
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots{isolate}.exception();
  }

  return Smi::FromInt(
      wasm::JumpTableOffset(trusted_instance_data->module(), func_index));
}

namespace {
Tagged<FixedArray> AllocateFeedbackVector(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    int declared_func_index) {
  DCHECK(isolate->context().is_null());
  isolate->set_context(trusted_instance_data->native_context());
  const wasm::WasmModule* module =
      trusted_instance_data->native_module()->module();

  int func_index = declared_func_index + module->num_imported_functions;
  int num_slots = NumFeedbackSlots(module, func_index);
  DirectHandle<FixedArray> vector =
      isolate->factory()->NewFixedArrayWithZeroes(num_slots);
  DCHECK_EQ(trusted_instance_data->feedback_vectors()->get(declared_func_index),
            Smi::zero());
  trusted_instance_data->feedback_vectors()->set(declared_func_index, *vector);
  isolate->set_context(Tagged<Context>());
  return *vector;
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmAllocateFeedbackVector) {
  ClearThreadInWasmScope wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  int declared_func_index = args.smi_value_at(1);
  wasm::NativeModule** native_module_stack_slot =
      reinterpret_cast<wasm::NativeModule**>(args.address_of_arg_at(2));
  wasm::NativeModule* native_module = trusted_instance_data->native_module();
  DCHECK(native_module->enabled_features().has_inlining() ||
         native_module->module()->is_wasm_gc);
  // We have to save the native_module on the stack, in case the allocation
  // triggers a GC and we need the module to scan LiftoffSetupFrame stack frame.
  *native_module_stack_slot = native_module;
  return AllocateFeedbackVector(isolate, trusted_instance_data,
                                declared_func_index);
}

RUNTIME_FUNCTION(Runtime_WasmLiftoffDeoptFinish) {
  ClearThreadInWasmScope wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  // Destroy the Deoptimizer object stored on the isolate.
  size_t deopt_frame_count = Deoptimizer::DeleteForWasm(isolate);
  size_t i = 0;

  // For each liftoff frame, check if the feedback vector is already present.
  // If it is not, allocate a new feedback vector for it.
  for (StackFrameIterator it(isolate); !it.done(); it.Advance()) {
    StackFrame* frame = it.frame();
    if (frame->is_wasm() && WasmFrame::cast(frame)->wasm_code()->is_liftoff()) {
      Address vector_address =
          frame->fp() - WasmLiftoffFrameConstants::kFeedbackVectorOffset;
      Tagged<Object> vector_or_smi(Memory<intptr_t>(vector_address));
      if (vector_or_smi.IsSmi()) {
        int declared_func_index = Cast<Smi>(vector_or_smi).value();
        Tagged<Object> vector =
            trusted_instance_data->feedback_vectors()->get(declared_func_index);
        // The vector can already exist if the same function appears multiple
        // times in the deopted frames (i.e. it was inlined recursively).
        if (vector == Smi::zero()) {
          vector = AllocateFeedbackVector(isolate, trusted_instance_data,
                                          declared_func_index);
        }
        memcpy(reinterpret_cast<void*>(vector_address), &vector,
               sizeof(intptr_t));
      }
      if (++i == deopt_frame_count) {
        break;  // All deopt frames have been visited.
      }
    }
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {
void ReplaceWrapper(Isolate* isolate,
                    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
                    int function_index, DirectHandle<Code> wrapper_code) {
  Tagged<WasmFuncRef> func_ref;
  CHECK(trusted_instance_data->try_get_func_ref(function_index, &func_ref));
  Tagged<JSFunction> external_function;
  CHECK(func_ref->internal(isolate)->try_get_external(&external_function));
  if (external_function->shared()->HasWasmJSFunctionData()) return;
  CHECK(external_function->shared()->HasWasmExportedFunctionData());
  external_function->set_code(*wrapper_code);
  Tagged<WasmExportedFunctionData> function_data =
      external_function->shared()->wasm_exported_function_data();
  function_data->set_wrapper_code(*wrapper_code);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmCompileWrapper) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<WasmExportedFunctionData> function_data(
      Cast<WasmExportedFunctionData>(args[0]), isolate);
  DirectHandle<WasmTrustedInstanceData> trusted_data{
      function_data->instance_data(), isolate};
  DCHECK(isolate->context().is_null());
  isolate->set_context(trusted_data->native_context());

  const wasm::WasmModule* module = trusted_data->module();
  const int function_index = function_data->function_index();
  const wasm::WasmFunction& function = module->functions[function_index];
  const wasm::FunctionSig* sig = function.sig;
  const uint32_t canonical_sig_index =
      module->isorecursive_canonical_type_ids[function.sig_index];

  // The start function is not guaranteed to be registered as
  // an exported function (although it is called as one).
  // If there is no entry for the start function, the tier-up is abandoned.
  Tagged<WasmFuncRef> func_ref;
  if (!trusted_data->try_get_func_ref(function_index, &func_ref)) {
    DCHECK_EQ(function_index, module->start_function_index);
    return ReadOnlyRoots(isolate).undefined_value();
  }

  DirectHandle<Code> wrapper_code =
      wasm::JSToWasmWrapperCompilationUnit::CompileJSToWasmWrapper(
          isolate, sig, canonical_sig_index, module);

  // Replace the wrapper for the function that triggered the tier-up.
  // This is to ensure that the wrapper is replaced, even if the function
  // is implicitly exported and is not part of the export_table.
  ReplaceWrapper(isolate, trusted_data, function_index, wrapper_code);

  // Iterate over all exports to replace eagerly the wrapper for all functions
  // that share the signature of the function that tiered up.
  for (wasm::WasmExport exp : module->export_table) {
    if (exp.kind != wasm::kExternalFunction) {
      continue;
    }
    int index = static_cast<int>(exp.index);
    const wasm::WasmFunction& exp_function = module->functions[index];
    if (exp_function.sig == sig && index != function_index) {
      ReplaceWrapper(isolate, trusted_data, index, wrapper_code);
    }
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_IsWasmExternalFunction) {
  DCHECK_EQ(1, args.length());
  return isolate->heap()->ToBoolean(
      WasmExternalFunction::IsWasmExternalFunction(args[0]));
}

RUNTIME_FUNCTION(Runtime_TierUpWasmToJSWrapper) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<WasmImportData> import_data(Cast<WasmImportData>(args[0]),
                                           isolate);

  DCHECK(isolate->context().is_null());
  isolate->set_context(import_data->native_context());

  std::unique_ptr<wasm::ValueType[]> reps;
  wasm::FunctionSig sig = wasm::SerializedSignatureHelper::DeserializeSignature(
      import_data->sig(), &reps);
  DirectHandle<Object> origin(import_data->call_origin(), isolate);

  if (IsWasmFuncRef(*origin)) {
    // The tierup for `WasmInternalFunction is special, as there may not be an
    // instance.
    size_t expected_arity = sig.parameter_count();
    wasm::ImportCallKind kind = wasm::kDefaultImportCallKind;
    if (IsJSFunction(import_data->callable())) {
      Tagged<SharedFunctionInfo> shared =
          Cast<JSFunction>(import_data->callable())->shared();
      expected_arity =
          shared->internal_formal_parameter_count_without_receiver();
      if (expected_arity != sig.parameter_count()) {
        kind = wasm::ImportCallKind::kJSFunctionArityMismatch;
      }
    }
    const wasm::WasmModule* module =
        import_data->has_instance_data()
            ? import_data->instance_data()->module()
            : nullptr;

    DirectHandle<Code> wasm_to_js_wrapper_code =
        compiler::CompileWasmToJSWrapper(
            isolate, module, &sig, kind, static_cast<int>(expected_arity),
            static_cast<wasm::Suspend>(import_data->suspend()))
            .ToHandleChecked();

    // We have to install the optimized wrapper as `code`, as the generated
    // code may move. `call_target` would become stale then.
    DirectHandle<WasmInternalFunction> internal_function{
        Cast<WasmFuncRef>(*origin)->internal(isolate), isolate};
    import_data->set_code(*wasm_to_js_wrapper_code);
    internal_function->set_call_target(
        Builtins::EntryOf(Builtin::kWasmToOnHeapWasmToJsTrampoline, isolate));
    return ReadOnlyRoots(isolate).undefined_value();
  }

  Handle<WasmTrustedInstanceData> trusted_data(import_data->instance_data(),
                                               isolate);
  if (IsTuple2(*origin)) {
    auto tuple = Cast<Tuple2>(origin);
    trusted_data =
        handle(Cast<WasmInstanceObject>(tuple->value1())->trusted_data(isolate),
               isolate);
    origin = direct_handle(tuple->value2(), isolate);
  }
  const wasm::WasmModule* module = trusted_data->module();

  // Get the function's canonical signature index.
  uint32_t canonical_sig_index = std::numeric_limits<uint32_t>::max();
  if (WasmImportData::CallOriginIsImportIndex(origin)) {
    int func_index = WasmImportData::CallOriginAsIndex(origin);
    canonical_sig_index =
        module->isorecursive_canonical_type_ids[module->functions[func_index]
                                                    .sig_index];
  } else {
    // Indirect function table index.
    int entry_index = WasmImportData::CallOriginAsIndex(origin);
    int table_count = trusted_data->dispatch_tables()->length();
    // We have to find the table which contains the correct entry.
    for (int table_index = 0; table_index < table_count; ++table_index) {
      bool table_is_shared = module->tables[table_index].shared;
      DirectHandle<WasmTrustedInstanceData> maybe_shared_data =
          table_is_shared ? direct_handle(trusted_data->shared_part(), isolate)
                          : trusted_data;
      if (!maybe_shared_data->has_dispatch_table(table_index)) continue;
      Tagged<WasmDispatchTable> table =
          maybe_shared_data->dispatch_table(table_index);
      if (entry_index < table->length() &&
          table->implicit_arg(entry_index) == *import_data) {
        canonical_sig_index = table->sig(entry_index);
        break;
      }
    }
  }
  DCHECK_NE(canonical_sig_index, std::numeric_limits<uint32_t>::max());

  // Compile a wrapper for the target callable.
  Handle<JSReceiver> callable(Cast<JSReceiver>(import_data->callable()),
                              isolate);
  wasm::Suspend suspend = static_cast<wasm::Suspend>(import_data->suspend());
  wasm::WasmCodeRefScope code_ref_scope;

  wasm::NativeModule* native_module = trusted_data->native_module();

  wasm::ResolvedWasmImport resolved({}, -1, callable, &sig, canonical_sig_index,
                                    wasm::WellKnownImport::kUninstantiated);
  wasm::ImportCallKind kind = resolved.kind();
  callable = resolved.callable();  // Update to ultimate target.
  DCHECK_NE(wasm::ImportCallKind::kLinkError, kind);
  wasm::CompilationEnv env = wasm::CompilationEnv::ForModule(native_module);
  // {expected_arity} should only be used if kind != kJSFunctionArityMismatch.
  int expected_arity = static_cast<int>(sig.parameter_count());
  if (kind == wasm::ImportCallKind ::kJSFunctionArityMismatch) {
    expected_arity = Cast<JSFunction>(callable)
                         ->shared()
                         ->internal_formal_parameter_count_without_receiver();
  }

  wasm::WasmImportWrapperCache* cache = native_module->import_wrapper_cache();
  wasm::WasmCode* wasm_code =
      cache->MaybeGet(kind, canonical_sig_index, expected_arity, suspend);
  if (!wasm_code) {
    wasm::WasmCompilationResult result = compiler::CompileWasmImportCallWrapper(
        &env, kind, &sig, false, expected_arity, suspend);
    std::unique_ptr<wasm::WasmCode> compiled_code = native_module->AddCode(
        result.func_index, result.code_desc, result.frame_slot_count,
        result.ool_spill_count, result.tagged_parameter_slots,
        result.protected_instructions_data.as_vector(),
        result.source_positions.as_vector(),
        result.inlining_positions.as_vector(), result.deopt_data.as_vector(),
        GetCodeKind(result), wasm::ExecutionTier::kNone,
        wasm::kNotForDebugging);
    wasm_code = native_module->PublishCode(std::move(compiled_code));
    isolate->counters()->wasm_generated_code_size()->Increment(
        wasm_code->instructions().length());
    isolate->counters()->wasm_reloc_size()->Increment(
        wasm_code->reloc_info().length());
    if (V8_UNLIKELY(native_module->log_code())) {
      wasm::GetWasmEngine()->LogCode(base::VectorOf(&wasm_code, 1));
      // Log the code immediately in the current isolate.
      wasm::GetWasmEngine()->LogOutstandingCodesForIsolate(isolate);
    }

    wasm::WasmImportWrapperCache::ModificationScope cache_scope(cache);
    wasm::WasmImportWrapperCache::CacheKey key(kind, canonical_sig_index,
                                               expected_arity, suspend);
    cache_scope[key] = wasm_code;
  }

  if (WasmImportData::CallOriginIsImportIndex(origin)) {
    int func_index = WasmImportData::CallOriginAsIndex(origin);
    ImportedFunctionEntry entry(trusted_data, func_index);
    entry.set_target(wasm_code->instruction_start());
  } else {
    // Indirect function table index.
    int entry_index = WasmImportData::CallOriginAsIndex(origin);
    int table_count = trusted_data->dispatch_tables()->length();
    // We have to find the table which contains the correct entry.
    for (int table_index = 0; table_index < table_count; ++table_index) {
      if (!trusted_data->has_dispatch_table(table_index)) continue;
      Tagged<WasmDispatchTable> table =
          trusted_data->dispatch_table(table_index);
      if (entry_index < table->length() &&
          table->implicit_arg(entry_index) == *import_data) {
        table->SetTarget(entry_index, wasm_code->instruction_start());
        // {ref} is used in at most one table.
        break;
      }
    }
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTriggerTierUp) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  SealHandleScope shs(isolate);

  {
    DisallowGarbageCollection no_gc;
    DCHECK_EQ(1, args.length());
    Tagged<WasmTrustedInstanceData> trusted_data =
        Cast<WasmTrustedInstanceData>(args[0]);

    FrameFinder<WasmFrame> frame_finder(isolate);
    int func_index = frame_finder.frame()->function_index();
    DCHECK_EQ(trusted_data, frame_finder.frame()->trusted_instance_data());

    if (V8_UNLIKELY(v8_flags.wasm_sync_tier_up)) {
      if (!trusted_data->native_module()->HasCodeWithTier(
              func_index, wasm::ExecutionTier::kTurbofan)) {
        wasm::TierUpNowForTesting(isolate, trusted_data, func_index);
      }
      // We call this function when the tiering budget runs out, so reset that
      // budget to appropriately delay the next call.
      int array_index =
          wasm::declared_function_index(trusted_data->module(), func_index);
      trusted_data->tiering_budget_array()[array_index].store(
          v8_flags.wasm_tiering_budget, std::memory_order_relaxed);
    } else {
      wasm::TriggerTierUp(isolate, trusted_data, func_index);
    }
  }

  // We're reusing this interrupt mechanism to interrupt long-running loops.
  StackLimitCheck check(isolate);
  // We don't need to handle stack overflows here, because the function that
  // performed this runtime call did its own stack check at its beginning.
  // However, we can't DCHECK(!check.JsHasOverflowed()) here, because the
  // additional stack space used by the CEntryStub and this runtime function
  // itself might have pushed us above the limit where a stack check would
  // fail.
  if (check.InterruptRequested()) {
    // Note: This might trigger a GC, which invalidates the {args} object (see
    // https://crbug.com/v8/13036#2).
    Tagged<Object> result = isolate->stack_guard()->HandleInterrupts();
    if (IsException(result)) return result;
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmI32AtomicWait) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int memory_index = args.smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  int32_t expected_value = NumberToInt32(args[3]);
  Tagged<BigInt> timeout_ns = Cast<BigInt>(args[4]);

  Handle<JSArrayBuffer> array_buffer{
      trusted_instance_data->memory_object(memory_index)->array_buffer(),
      isolate};
  // Should have trapped if address was OOB.
  DCHECK_LT(offset, array_buffer->byte_length());

  // Trap if memory is not shared, or wait is not allowed on the isolate
  if (!array_buffer->is_shared() || !isolate->allow_atomics_wait()) {
    return ThrowWasmError(
        isolate, MessageTemplate::kAtomicsOperationNotAllowed,
        {isolate->factory()->NewStringFromAsciiChecked("Atomics.wait")});
  }
  return FutexEmulation::WaitWasm32(isolate, array_buffer, offset,
                                    expected_value, timeout_ns->AsInt64());
}

RUNTIME_FUNCTION(Runtime_WasmI64AtomicWait) {
  ClearThreadInWasmScope clear_wasm_flag(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  int memory_index = args.smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  Tagged<BigInt> expected_value = Cast<BigInt>(args[3]);
  Tagged<BigInt> timeout_ns = Cast<BigInt>(args[4]);

  Handle<JSArrayBuffer> array_buffer{
      trusted_instance_data->memory_object(memory_index)->array_buffer(),
      isolate};
  // Should have trapped if address was OOB.
  DCHECK_LT(offset, array_buffer->byte_length());

  // Trap if memory is not shared, or if wait is not allowed on the isolate
  if (!array_buffer->is_shared() || !isolate->allow_atomics_wait()) {
    return ThrowWasmError(
        isolate, MessageTemplate::kAtomicsOperationNotAllowed,
        {isolate->factory()->NewStringFromAsciiChecked("Atomics.wait")});
  }
  return FutexEmulation::WaitWasm64(isolate, array_buffer, offset,
                                    expected_value->AsInt64(),
                                    timeout_ns->AsInt64());
}

namespace {
Tagged<Object> ThrowTableOutOfBounds(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data) {
  // Handle out-of-bounds access here in the runtime call, rather
  // than having the lower-level layers deal with JS exceptions.
  if (isolate->context().is_null()) {
    isolate->set_context(trusted_instance_data->native_context());
  }
  return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmRefFunc) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t function_index = args.positive_smi_value_at(1);

  return *WasmTrustedInstanceData::GetOrCreateFuncRef(
      isolate, trusted_instance_data, function_index);
}

RUNTIME_FUNCTION(Runtime_WasmInternalFunctionCreateExternal) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  // TODO(14564): Pass WasmFuncRef here instead of WasmInternalFunction.
  DirectHandle<WasmInternalFunction> internal(
      Cast<WasmInternalFunction>(args[0]), isolate);
  return *WasmInternalFunction::GetOrCreateExternal(internal);
}

RUNTIME_FUNCTION(Runtime_WasmFunctionTableGet) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(3, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t entry_index = args.positive_smi_value_at(2);
  DCHECK_LT(table_index, trusted_instance_data->tables()->length());
  auto table = handle(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  // We only use the runtime call for lazily initialized function references.
  DCHECK(
      !table->has_trusted_data()
          ? table->type() == wasm::kWasmFuncRef
          : (IsSubtypeOf(table->type(), wasm::kWasmFuncRef,
                         table->trusted_data(isolate)->module()) ||
             IsSubtypeOf(table->type(),
                         wasm::ValueType::RefNull(wasm::HeapType::kFuncShared),
                         table->trusted_data(isolate)->module())));

  if (!table->is_in_bounds(entry_index)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
  }

  return *WasmTableObject::Get(isolate, table, entry_index);
}

RUNTIME_FUNCTION(Runtime_WasmFunctionTableSet) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t entry_index = args.positive_smi_value_at(2);
  DirectHandle<Object> element(args[3], isolate);
  DCHECK_LT(table_index, trusted_instance_data->tables()->length());
  auto table = handle(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  // We only use the runtime call for lazily initialized function references.
  DCHECK(
      !table->has_trusted_data()
          ? table->type() == wasm::kWasmFuncRef
          : (IsSubtypeOf(table->type(), wasm::kWasmFuncRef,
                         table->trusted_data(isolate)->module()) ||
             IsSubtypeOf(table->type(),
                         wasm::ValueType::RefNull(wasm::HeapType::kFuncShared),
                         table->trusted_data(isolate)->module())));

  if (!table->is_in_bounds(entry_index)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapTableOutOfBounds);
  }
  WasmTableObject::Set(isolate, table, entry_index, element);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableInit) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  Handle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t elem_segment_index = args.positive_smi_value_at(2);
  static_assert(
      wasm::kV8MaxWasmTableSize < kSmiMaxValue,
      "Make sure clamping to Smi range doesn't make an invalid call valid");
  uint32_t dst = args.positive_smi_value_at(3);
  uint32_t src = args.positive_smi_value_at(4);
  uint32_t count = args.positive_smi_value_at(5);

  DCHECK(!isolate->context().is_null());

  // TODO(14616): Pass the correct instance data.
  std::optional<MessageTemplate> opt_error =
      WasmTrustedInstanceData::InitTableEntries(
          isolate, trusted_instance_data, trusted_instance_data, table_index,
          elem_segment_index, dst, src, count);
  if (opt_error.has_value()) {
    return ThrowWasmError(isolate, opt_error.value());
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableCopy) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_dst_index = args.positive_smi_value_at(1);
  uint32_t table_src_index = args.positive_smi_value_at(2);
  static_assert(
      wasm::kV8MaxWasmTableSize < kSmiMaxValue,
      "Make sure clamping to Smi range doesn't make an invalid call valid");
  uint32_t dst = args.positive_smi_value_at(3);
  uint32_t src = args.positive_smi_value_at(4);
  uint32_t count = args.positive_smi_value_at(5);

  DCHECK(!isolate->context().is_null());

  bool oob = !WasmTrustedInstanceData::CopyTableEntries(
      isolate, trusted_instance_data, table_dst_index, table_src_index, dst,
      src, count);
  if (oob) return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTableGrow) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK(isolate->IsOnCentralStack());
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t table_index = args.positive_smi_value_at(1);
  DirectHandle<Object> value(args[2], isolate);
  uint32_t delta = args.positive_smi_value_at(3);

  DirectHandle<WasmTableObject> table(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);
  int result = WasmTableObject::Grow(isolate, table, delta, value);

  return Smi::FromInt(result);
}

RUNTIME_FUNCTION(Runtime_WasmTableFill) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t table_index = args.positive_smi_value_at(1);
  uint32_t start = args.positive_smi_value_at(2);
  DirectHandle<Object> value(args[3], isolate);
  uint32_t count = args.positive_smi_value_at(4);

  DirectHandle<WasmTableObject> table(
      Cast<WasmTableObject>(trusted_instance_data->tables()->get(table_index)),
      isolate);

  uint32_t table_size = table->current_length();

  if (start > table_size) {
    return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  }

  // Even when table.fill goes out-of-bounds, as many entries as possible are
  // put into the table. Only afterwards we trap.
  uint32_t fill_count = std::min(count, table_size - start);
  if (fill_count < count) {
    return ThrowTableOutOfBounds(isolate, trusted_instance_data);
  }
  WasmTableObject::Fill(isolate, table, start, value, fill_count);

  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {
// Returns true if any breakpoint was hit, false otherwise.
bool ExecuteWasmDebugBreaks(
    Isolate* isolate,
    DirectHandle<WasmTrustedInstanceData> trusted_instance_data,
    WasmFrame* frame) {
  DirectHandle<Script> script{trusted_instance_data->module_object()->script(),
                              isolate};
  auto* debug_info = trusted_instance_data->native_module()->GetDebugInfo();

  // Enter the debugger.
  DebugScope debug_scope(isolate->debug());

  // Check for instrumentation breakpoints first, but still execute regular
  // breakpoints afterwards.
  bool paused_on_instrumentation = false;
  DCHECK_EQ(script->break_on_entry(),
            !!trusted_instance_data->break_on_entry());
  if (script->break_on_entry()) {
    MaybeHandle<FixedArray> maybe_on_entry_breakpoints =
        WasmScript::CheckBreakPoints(isolate, script,
                                     WasmScript::kOnEntryBreakpointPosition,
                                     frame->id());
    script->set_break_on_entry(false);
    // Update the "break_on_entry" flag on all live instances.
    i::Tagged<i::WeakArrayList> weak_instance_list =
        script->wasm_weak_instance_list();
    for (int i = 0; i < weak_instance_list->length(); ++i) {
      if (weak_instance_list->Get(i).IsCleared()) continue;
      i::Cast<i::WasmInstanceObject>(weak_instance_list->Get(i).GetHeapObject())
          ->trusted_data(isolate)
          ->set_break_on_entry(false);
    }
    DCHECK(!trusted_instance_data->break_on_entry());
    if (!maybe_on_entry_breakpoints.is_null()) {
      isolate->debug()->OnInstrumentationBreak();
      paused_on_instrumentation = true;
    }
  }

  if (debug_info->IsStepping(frame)) {
    debug_info->ClearStepping(isolate);
    StepAction step_action = isolate->debug()->last_step_action();
    isolate->debug()->ClearStepping();
    isolate->debug()->OnDebugBreak(isolate->factory()->empty_fixed_array(),
                                   step_action);
    return true;
  }

  // Check whether we hit a breakpoint.
  Handle<FixedArray> breakpoints;
  if (WasmScript::CheckBreakPoints(isolate, script, frame->position(),
                                   frame->id())
          .ToHandle(&breakpoints)) {
    debug_info->ClearStepping(isolate);
    StepAction step_action = isolate->debug()->last_step_action();
    isolate->debug()->ClearStepping();
    if (isolate->debug()->break_points_active()) {
      // We hit one or several breakpoints. Notify the debug listeners.
      isolate->debug()->OnDebugBreak(breakpoints, step_action);
    }
    return true;
  }

  return paused_on_instrumentation;
}
}  // namespace

RUNTIME_FUNCTION(Runtime_WasmDebugBreak) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());
  FrameFinder<WasmFrame> frame_finder(
      isolate, {StackFrame::EXIT, StackFrame::WASM_DEBUG_BREAK});
  WasmFrame* frame = frame_finder.frame();
  DirectHandle<WasmTrustedInstanceData> trusted_data{
      frame->trusted_instance_data(), isolate};
  isolate->set_context(trusted_data->native_context());

  if (!ExecuteWasmDebugBreaks(isolate, trusted_data, frame)) {
    // We did not hit a breakpoint. If we are in stepping code, but the user did
    // not request stepping, clear this (to save further calls into this runtime
    // function).
    auto* debug_info = trusted_data->native_module()->GetDebugInfo();
    debug_info->ClearStepping(frame);
  }

  // Execute a stack check before leaving this function. This is to handle any
  // interrupts set by the debugger (e.g. termination), but also to execute Wasm
  // code GC to get rid of temporarily created Wasm code.
  StackLimitCheck check(isolate);
  if (check.InterruptRequested()) {
    Tagged<Object> interrupt_object =
        isolate->stack_guard()->HandleInterrupts();
    // Interrupt handling can create an exception, including the
    // termination exception.
    if (IsException(interrupt_object, isolate)) return interrupt_object;
    DCHECK(IsUndefined(interrupt_object, isolate));
  }

  return ReadOnlyRoots(isolate).undefined_value();
}

// Assumes copy ranges are in-bounds and copy length > 0.
// TODO(manoskouk): Unify part of this with the implementation in
// wasm-extern-refs.cc
RUNTIME_FUNCTION(Runtime_WasmArrayCopy) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DisallowGarbageCollection no_gc;
  DCHECK_EQ(5, args.length());
  Tagged<WasmArray> dst_array = Cast<WasmArray>(args[0]);
  uint32_t dst_index = args.positive_smi_value_at(1);
  Tagged<WasmArray> src_array = Cast<WasmArray>(args[2]);
  uint32_t src_index = args.positive_smi_value_at(3);
  uint32_t length = args.positive_smi_value_at(4);
  DCHECK_GT(length, 0);
  bool overlapping_ranges =
      dst_array.ptr() == src_array.ptr() &&
      (dst_index < src_index ? dst_index + length > src_index
                             : src_index + length > dst_index);
  wasm::ValueType element_type = src_array->type()->element_type();
  if (element_type.is_reference()) {
    ObjectSlot dst_slot = dst_array->ElementSlot(dst_index);
    ObjectSlot src_slot = src_array->ElementSlot(src_index);
    if (overlapping_ranges) {
      isolate->heap()->MoveRange(dst_array, dst_slot, src_slot, length,
                                 UPDATE_WRITE_BARRIER);
    } else {
      isolate->heap()->CopyRange(dst_array, dst_slot, src_slot, length,
                                 UPDATE_WRITE_BARRIER);
    }
  } else {
    void* dst = reinterpret_cast<void*>(dst_array->ElementAddress(dst_index));
    void* src = reinterpret_cast<void*>(src_array->ElementAddress(src_index));
    size_t copy_size = length * element_type.value_kind_size();
    if (overlapping_ranges) {
      MemMove(dst, src, copy_size);
    } else {
      MemCopy(dst, src, copy_size);
    }
  }
  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmArrayNewSegment) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(5, args.length());
  Handle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  uint32_t offset = args.positive_smi_value_at(2);
  uint32_t length = args.positive_smi_value_at(3);
  DirectHandle<Map> rtt(Cast<Map>(args[4]), isolate);

  wasm::ArrayType* type =
      reinterpret_cast<wasm::ArrayType*>(rtt->wasm_type_info()->native_type());

  uint32_t element_size = type->element_type().value_kind_size();
  // This check also implies no overflow.
  if (length > static_cast<uint32_t>(WasmArray::MaxLength(element_size))) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapArrayTooLarge);
  }

  if (type->element_type().is_numeric()) {
    // No chance of overflow due to the check above.
    uint32_t length_in_bytes = length * element_size;

    if (!base::IsInBounds<uint32_t>(
            offset, length_in_bytes,
            trusted_instance_data->data_segment_sizes()->get(segment_index))) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
    }

    Address source =
        trusted_instance_data->data_segment_starts()->get(segment_index) +
        offset;
    return *isolate->factory()->NewWasmArrayFromMemory(length, rtt, source);
  } else {
    Handle<Object> elem_segment_raw = handle(
        trusted_instance_data->element_segments()->get(segment_index), isolate);
    const wasm::WasmElemSegment* module_elem_segment =
        &trusted_instance_data->module()->elem_segments[segment_index];
    // If the segment is initialized in the instance, we have to get its length
    // from there, as it might have been dropped. If the segment is
    // uninitialized, we need to fetch its length from the module.
    int segment_length = IsFixedArray(*elem_segment_raw)
                             ? Cast<FixedArray>(elem_segment_raw)->length()
                             : module_elem_segment->element_count;
    if (!base::IsInBounds<size_t>(offset, length, segment_length)) {
      return ThrowWasmError(
          isolate, MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
    }
    // TODO(14616): Pass the correct instance data.
    DirectHandle<Object> result =
        isolate->factory()->NewWasmArrayFromElementSegment(
            trusted_instance_data, trusted_instance_data, segment_index, offset,
            length, rtt);
    if (IsSmi(*result)) {
      return ThrowWasmError(
          isolate, static_cast<MessageTemplate>(Cast<Smi>(*result).value()));
    } else {
      return *result;
    }
  }
}

RUNTIME_FUNCTION(Runtime_WasmArrayInitSegment) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(6, args.length());
  Handle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[2]), isolate);
  uint32_t array_index = args.positive_smi_value_at(3);
  uint32_t segment_offset = args.positive_smi_value_at(4);
  uint32_t length = args.positive_smi_value_at(5);

  wasm::ArrayType* type = reinterpret_cast<wasm::ArrayType*>(
      array->map()->wasm_type_info()->native_type());

  uint32_t element_size = type->element_type().value_kind_size();

  if (type->element_type().is_numeric()) {
    if (!base::IsInBounds<uint32_t>(array_index, length, array->length())) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapArrayOutOfBounds);
    }

    // No chance of overflow, due to the check above and the limit in array
    // length.
    uint32_t length_in_bytes = length * element_size;

    if (!base::IsInBounds<uint32_t>(
            segment_offset, length_in_bytes,
            trusted_instance_data->data_segment_sizes()->get(segment_index))) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
    }

    Address source =
        trusted_instance_data->data_segment_starts()->get(segment_index) +
        segment_offset;
    Address dest = array->ElementAddress(array_index);
#if V8_TARGET_BIG_ENDIAN
    MemCopyAndSwitchEndianness(reinterpret_cast<void*>(dest),
                               reinterpret_cast<void*>(source), length,
                               element_size);
#else
    MemCopy(reinterpret_cast<void*>(dest), reinterpret_cast<void*>(source),
            length_in_bytes);
#endif
    return *isolate->factory()->undefined_value();
  } else {
    Handle<Object> elem_segment_raw = handle(
        trusted_instance_data->element_segments()->get(segment_index), isolate);
    const wasm::WasmElemSegment* module_elem_segment =
        &trusted_instance_data->module()->elem_segments[segment_index];
    // If the segment is initialized in the instance, we have to get its length
    // from there, as it might have been dropped. If the segment is
    // uninitialized, we need to fetch its length from the module.
    int segment_length = IsFixedArray(*elem_segment_raw)
                             ? Cast<FixedArray>(elem_segment_raw)->length()
                             : module_elem_segment->element_count;
    if (!base::IsInBounds<size_t>(segment_offset, length, segment_length)) {
      return ThrowWasmError(
          isolate, MessageTemplate::kWasmTrapElementSegmentOutOfBounds);
    }
    if (!base::IsInBounds(array_index, length, array->length())) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapArrayOutOfBounds);
    }

    // If the element segment has not been initialized yet, lazily initialize it
    // now.
    AccountingAllocator allocator;
    Zone zone(&allocator, ZONE_NAME);
    // TODO(14616): Fix the instance data.
    std::optional<MessageTemplate> opt_error =
        wasm::InitializeElementSegment(&zone, isolate, trusted_instance_data,
                                       trusted_instance_data, segment_index);
    if (opt_error.has_value()) {
      return ThrowWasmError(isolate, opt_error.value());
    }

    auto elements = handle(
        Cast<FixedArray>(
            trusted_instance_data->element_segments()->get(segment_index)),
        isolate);
    if (length > 0) {
      isolate->heap()->CopyRange(*array, array->ElementSlot(array_index),
                                 elements->RawFieldOfElementAt(segment_offset),
                                 length, UPDATE_WRITE_BARRIER);
    }
    return *isolate->factory()->undefined_value();
  }
}

// Allocate a new suspender, and prepare for stack switching by updating the
// active continuation, active suspender and stack limit.
RUNTIME_FUNCTION(Runtime_WasmAllocateSuspender) {
  HandleScope scope(isolate);
  DirectHandle<WasmSuspenderObject> suspender =
      isolate->factory()->NewWasmSuspenderObject();

  // Update the continuation state.
  auto parent = handle(Cast<WasmContinuationObject>(
                           isolate->root(RootIndex::kActiveContinuation)),
                       isolate);
  std::unique_ptr<wasm::StackMemory> target_stack =
      isolate->stack_pool().GetOrAllocate();
  DirectHandle<WasmContinuationObject> target = WasmContinuationObject::New(
      isolate, target_stack.get(), wasm::JumpBuffer::Inactive, parent);
  target_stack->set_index(isolate->wasm_stacks().size());
  isolate->wasm_stacks().emplace_back(std::move(target_stack));
  for (size_t i = 0; i < isolate->wasm_stacks().size(); ++i) {
    SLOW_DCHECK(isolate->wasm_stacks()[i]->index() == i);
  }
  isolate->roots_table().slot(RootIndex::kActiveContinuation).store(*target);

  // Update the suspender state.
  FullObjectSlot active_suspender_slot =
      isolate->roots_table().slot(RootIndex::kActiveSuspender);
  suspender->set_parent(
      Cast<UnionOf<Undefined, WasmSuspenderObject>>(*active_suspender_slot));
  suspender->set_state(WasmSuspenderObject::kActive);
  suspender->set_continuation(*target);
  active_suspender_slot.store(*suspender);

  // Stack limit will be updated in WasmReturnPromiseOnSuspendAsm builtin.
  wasm::JumpBuffer* jmpbuf = reinterpret_cast<wasm::JumpBuffer*>(
      parent->ReadExternalPointerField<kWasmContinuationJmpbufTag>(
          WasmContinuationObject::kJmpbufOffset, isolate));
  DCHECK_EQ(jmpbuf->state, wasm::JumpBuffer::Active);
  jmpbuf->state = wasm::JumpBuffer::Inactive;
  return *suspender;
}

#define RETURN_RESULT_OR_TRAP(call)                                            \
  do {                                                                         \
    Handle<Object> result;                                                     \
    if (!(call).ToHandle(&result)) {                                           \
      DCHECK(isolate->has_exception());                                        \
      /* Mark any exception as uncatchable by Wasm. */                         \
      Handle<JSObject> exception(Cast<JSObject>(isolate->exception()),         \
                                 isolate);                                     \
      Handle<Name> uncatchable =                                               \
          isolate->factory()->wasm_uncatchable_symbol();                       \
      LookupIterator it(isolate, exception, uncatchable, LookupIterator::OWN); \
      if (!JSReceiver::HasProperty(&it).FromJust()) {                          \
        JSObject::AddProperty(isolate, exception, uncatchable,                 \
                              isolate->factory()->true_value(), NONE);         \
      }                                                                        \
      return ReadOnlyRoots(isolate).exception();                               \
    }                                                                          \
    DCHECK(!isolate->has_exception());                                         \
    return *result;                                                            \
  } while (false)

// "Special" because the type must be in a recgroup of its own.
// Used by "JS String Builtins".
RUNTIME_FUNCTION(Runtime_WasmCastToSpecialPrimitiveArray) {
  ClearThreadInWasmScope flag_scope(isolate);
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  int bits = args.smi_value_at(1);
  DCHECK(bits == 8 || bits == 16);

  if (args[0] == ReadOnlyRoots(isolate).null_value()) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapNullDereference);
  }
  MessageTemplate illegal_cast = MessageTemplate::kWasmTrapIllegalCast;
  if (!IsWasmArray(args[0])) return ThrowWasmError(isolate, illegal_cast);
  Tagged<WasmArray> obj = Cast<WasmArray>(args[0]);
  Tagged<WasmTypeInfo> wti = obj->map()->wasm_type_info();
  const wasm::WasmModule* module = wti->trusted_data(isolate)->module();
  DCHECK(module->has_array(wti->type_index()));
  uint32_t expected = bits == 8
                          ? wasm::TypeCanonicalizer::kPredefinedArrayI8Index
                          : wasm::TypeCanonicalizer::kPredefinedArrayI16Index;
  if (module->isorecursive_canonical_type_ids[wti->type_index()] != expected) {
    return ThrowWasmError(isolate, illegal_cast);
  }
  return obj;
}

// Returns the new string if the operation succeeds.  Otherwise throws an
// exception and returns an empty result.
RUNTIME_FUNCTION(Runtime_WasmStringNewWtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  uint32_t utf8_variant_value = args.positive_smi_value_at(2);
  double offset_double = args.number_value_at(3);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t size = NumberToUint32(args[4]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));

  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);

  uint64_t mem_size = trusted_instance_data->memory_size(memory);
  if (!base::IsInBounds<uint64_t>(offset, size, mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }

  const base::Vector<const uint8_t> bytes{
      trusted_instance_data->memory_base(memory) + offset, size};
  MaybeHandle<v8::internal::String> result_string =
      isolate->factory()->NewStringFromUtf8(bytes, utf8_variant);
  if (utf8_variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    DCHECK(!isolate->has_exception());
    if (result_string.is_null()) {
      return *isolate->factory()->wasm_null();
    }
    return *result_string.ToHandleChecked();
  }
  RETURN_RESULT_OR_TRAP(result_string);
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf8Array) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  uint32_t utf8_variant_value = args.positive_smi_value_at(0);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[1]), isolate);
  uint32_t start = NumberToUint32(args[2]);
  uint32_t end = NumberToUint32(args[3]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);

  MaybeHandle<v8::internal::String> result_string =
      isolate->factory()->NewStringFromUtf8(array, start, end, utf8_variant);
  if (utf8_variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    DCHECK(!isolate->has_exception());
    if (result_string.is_null()) {
      return *isolate->factory()->wasm_null();
    }
    return *result_string.ToHandleChecked();
  }
  RETURN_RESULT_OR_TRAP(result_string);
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf16) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  double offset_double = args.number_value_at(2);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t size_in_codeunits = NumberToUint32(args[3]);

  uint64_t mem_size = trusted_instance_data->memory_size(memory);
  if (size_in_codeunits > kMaxUInt32 / 2 ||
      !base::IsInBounds<uint64_t>(offset, size_in_codeunits * 2, mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }
  if (offset & 1) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapUnalignedAccess);
  }

  const uint8_t* bytes = trusted_instance_data->memory_base(memory) + offset;
  const base::uc16* codeunits = reinterpret_cast<const base::uc16*>(bytes);
  RETURN_RESULT_OR_TRAP(isolate->factory()->NewStringFromTwoByteLittleEndian(
      {codeunits, size_in_codeunits}));
}

RUNTIME_FUNCTION(Runtime_WasmStringNewWtf16Array) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  DirectHandle<WasmArray> array(Cast<WasmArray>(args[0]), isolate);
  uint32_t start = NumberToUint32(args[1]);
  uint32_t end = NumberToUint32(args[2]);

  RETURN_RESULT_OR_TRAP(
      isolate->factory()->NewStringFromUtf16(array, start, end));
}

RUNTIME_FUNCTION(Runtime_WasmSubstring) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  Handle<String> string(Cast<String>(args[0]), isolate);
  int start = args.positive_smi_value_at(1);
  int length = args.positive_smi_value_at(2);

  string = String::Flatten(isolate, string);
  return *isolate->factory()->NewCopiedSubstring(string, start, length);
}

// Returns the new string if the operation succeeds.  Otherwise traps.
RUNTIME_FUNCTION(Runtime_WasmStringConst) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(2, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  static_assert(
      base::IsInRange(wasm::kV8MaxWasmStringLiterals, 0, Smi::kMaxValue));
  uint32_t index = args.positive_smi_value_at(1);

  DCHECK_LT(index, trusted_instance_data->module()->stringref_literals.size());

  const wasm::WasmStringRefLiteral& literal =
      trusted_instance_data->module()->stringref_literals[index];
  const base::Vector<const uint8_t> module_bytes =
      trusted_instance_data->native_module()->wire_bytes();
  const base::Vector<const uint8_t> string_bytes = module_bytes.SubVector(
      literal.source.offset(), literal.source.end_offset());
  // TODO(12868): No need to re-validate WTF-8.  Also, result should be cached.
  return *isolate->factory()
              ->NewStringFromUtf8(string_bytes, unibrow::Utf8Variant::kWtf8)
              .ToHandleChecked();
}

RUNTIME_FUNCTION(Runtime_WasmStringNewSegmentWtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  DirectHandle<WasmTrustedInstanceData> trusted_instance_data(
      Cast<WasmTrustedInstanceData>(args[0]), isolate);
  uint32_t segment_index = args.positive_smi_value_at(1);
  uint32_t offset = args.positive_smi_value_at(2);
  uint32_t length = args.positive_smi_value_at(3);
  unibrow::Utf8Variant variant =
      static_cast<unibrow::Utf8Variant>(args.positive_smi_value_at(4));

  if (!base::IsInBounds<uint32_t>(
          offset, length,
          trusted_instance_data->data_segment_sizes()->get(segment_index))) {
    return ThrowWasmError(isolate,
                          MessageTemplate::kWasmTrapDataSegmentOutOfBounds);
  }

  Address source =
      trusted_instance_data->data_segment_starts()->get(segment_index) + offset;
  MaybeHandle<String> result = isolate->factory()->NewStringFromUtf8(
      {reinterpret_cast<const uint8_t*>(source), length}, variant);
  if (variant == unibrow::Utf8Variant::kUtf8NoTrap) {
    DCHECK(!isolate->has_exception());
    // Only instructions from the stringref proposal can set variant
    // kUtf8NoTrap, so WasmNull is appropriate here.
    if (result.is_null()) return *isolate->factory()->wasm_null();
    return *result.ToHandleChecked();
  }
  RETURN_RESULT_OR_FAILURE(isolate, result);
}

namespace {
// TODO(12868): Consider unifying with api.cc:String::Utf8Length.
template <typename T>
int MeasureWtf8(base::Vector<const T> wtf16) {
  int previous = unibrow::Utf16::kNoPreviousCharacter;
  int length = 0;
  DCHECK(wtf16.size() <= String::kMaxLength);
  static_assert(String::kMaxLength <=
                (kMaxInt / unibrow::Utf8::kMaxEncodedSize));
  for (size_t i = 0; i < wtf16.size(); i++) {
    int current = wtf16[i];
    length += unibrow::Utf8::Length(current, previous);
    previous = current;
  }
  return length;
}
int MeasureWtf8(Isolate* isolate, Handle<String> string) {
  string = String::Flatten(isolate, string);
  DisallowGarbageCollection no_gc;
  String::FlatContent content = string->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  return content.IsOneByte() ? MeasureWtf8(content.ToOneByteVector())
                             : MeasureWtf8(content.ToUC16Vector());
}
size_t MaxEncodedSize(base::Vector<const uint8_t> wtf16) {
  DCHECK(wtf16.size() < std::numeric_limits<size_t>::max() /
                            unibrow::Utf8::kMax8BitCodeUnitSize);
  return wtf16.size() * unibrow::Utf8::kMax8BitCodeUnitSize;
}
size_t MaxEncodedSize(base::Vector<const base::uc16> wtf16) {
  DCHECK(wtf16.size() < std::numeric_limits<size_t>::max() /
                            unibrow::Utf8::kMax16BitCodeUnitSize);
  return wtf16.size() * unibrow::Utf8::kMax16BitCodeUnitSize;
}
bool HasUnpairedSurrogate(base::Vector<const uint8_t> wtf16) { return false; }
bool HasUnpairedSurrogate(base::Vector<const base::uc16> wtf16) {
  return unibrow::Utf16::HasUnpairedSurrogate(wtf16.begin(), wtf16.size());
}
// TODO(12868): Consider unifying with api.cc:String::WriteUtf8.
template <typename T>
int EncodeWtf8(base::Vector<char> bytes, size_t offset,
               base::Vector<const T> wtf16, unibrow::Utf8Variant variant,
               MessageTemplate* message, MessageTemplate out_of_bounds) {
  // The first check is a quick estimate to decide whether the second check
  // is worth the computation.
  if (!base::IsInBounds<size_t>(offset, MaxEncodedSize(wtf16), bytes.size()) &&
      !base::IsInBounds<size_t>(offset, MeasureWtf8(wtf16), bytes.size())) {
    *message = out_of_bounds;
    return -1;
  }

  bool replace_invalid = false;
  switch (variant) {
    case unibrow::Utf8Variant::kWtf8:
      break;
    case unibrow::Utf8Variant::kUtf8:
      if (HasUnpairedSurrogate(wtf16)) {
        *message = MessageTemplate::kWasmTrapStringIsolatedSurrogate;
        return -1;
      }
      break;
    case unibrow::Utf8Variant::kLossyUtf8:
      replace_invalid = true;
      break;
    default:
      UNREACHABLE();
  }

  char* dst_start = bytes.begin() + offset;
  char* dst = dst_start;
  int previous = unibrow::Utf16::kNoPreviousCharacter;
  for (auto code_unit : wtf16) {
    dst += unibrow::Utf8::Encode(dst, code_unit, previous, replace_invalid);
    previous = code_unit;
  }
  DCHECK_LE(dst - dst_start, static_cast<ptrdiff_t>(kMaxInt));
  return static_cast<int>(dst - dst_start);
}
template <typename GetWritableBytes>
Tagged<Object> EncodeWtf8(Isolate* isolate, unibrow::Utf8Variant variant,
                          Handle<String> string,
                          GetWritableBytes get_writable_bytes, size_t offset,
                          MessageTemplate out_of_bounds_message) {
  string = String::Flatten(isolate, string);
  MessageTemplate message;
  int written;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent content = string->GetFlatContent(no_gc);
    base::Vector<char> dst = get_writable_bytes(no_gc);
    written = content.IsOneByte()
                  ? EncodeWtf8(dst, offset, content.ToOneByteVector(), variant,
                               &message, out_of_bounds_message)
                  : EncodeWtf8(dst, offset, content.ToUC16Vector(), variant,
                               &message, out_of_bounds_message);
  }
  if (written < 0) {
    DCHECK_NE(message, MessageTemplate::kNone);
    return ThrowWasmError(isolate, message);
  }
  return *isolate->factory()->NewNumberFromInt(written);
}
}  // namespace

// Used for storing the name of a string-constants imports module off the heap.
// Defined here to be able to make use of the helper functions above.
void ToUtf8Lossy(Isolate* isolate, Handle<String> string, std::string& out) {
  int utf8_length = MeasureWtf8(isolate, string);
  DisallowGarbageCollection no_gc;
  out.resize(utf8_length);
  String::FlatContent content = string->GetFlatContent(no_gc);
  DCHECK(content.IsFlat());
  static constexpr unibrow::Utf8Variant variant =
      unibrow::Utf8Variant::kLossyUtf8;
  MessageTemplate* error_cant_happen = nullptr;
  MessageTemplate oob_cant_happen = MessageTemplate::kInvalid;
  if (content.IsOneByte()) {
    EncodeWtf8({out.data(), out.size()}, 0, content.ToOneByteVector(), variant,
               error_cant_happen, oob_cant_happen);
  } else {
    EncodeWtf8({out.data(), out.size()}, 0, content.ToUC16Vector(), variant,
               error_cant_happen, oob_cant_happen);
  }
}

RUNTIME_FUNCTION(Runtime_WasmStringMeasureUtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  Handle<String> string(Cast<String>(args[0]), isolate);

  string = String::Flatten(isolate, string);
  int length;
  {
    DisallowGarbageCollection no_gc;
    String::FlatContent content = string->GetFlatContent(no_gc);
    DCHECK(content.IsFlat());
    if (content.IsOneByte()) {
      length = MeasureWtf8(content.ToOneByteVector());
    } else {
      base::Vector<const base::uc16> code_units = content.ToUC16Vector();
      if (unibrow::Utf16::HasUnpairedSurrogate(code_units.begin(),
                                               code_units.size())) {
        length = -1;
      } else {
        length = MeasureWtf8(code_units);
      }
    }
  }
  return *isolate->factory()->NewNumberFromInt(length);
}

RUNTIME_FUNCTION(Runtime_WasmStringMeasureWtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  Handle<String> string(Cast<String>(args[0]), isolate);

  int length = MeasureWtf8(isolate, string);
  return *isolate->factory()->NewNumberFromInt(length);
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(5, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  uint32_t utf8_variant_value = args.positive_smi_value_at(2);
  Handle<String> string(Cast<String>(args[3]), isolate);
  double offset_double = args.number_value_at(4);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));

  char* memory_start =
      reinterpret_cast<char*>(trusted_instance_data->memory_base(memory));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {memory_start, trusted_instance_data->memory_size(memory)};
  };
  return EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, offset,
                    MessageTemplate::kWasmTrapMemOutOfBounds);
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf8Array) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(4, args.length());
  HandleScope scope(isolate);
  uint32_t utf8_variant_value = args.positive_smi_value_at(0);
  Handle<String> string(Cast<String>(args[1]), isolate);
  Handle<WasmArray> array(Cast<WasmArray>(args[2]), isolate);
  uint32_t start = NumberToUint32(args[3]);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->ElementAddress(0)), array->length()};
  };
  return EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, start,
                    MessageTemplate::kWasmTrapArrayOutOfBounds);
}

RUNTIME_FUNCTION(Runtime_WasmStringToUtf8Array) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  Handle<String> string(Cast<String>(args[0]), isolate);
  uint32_t length = MeasureWtf8(isolate, string);
  wasm::WasmValue initial_value(int8_t{0});
  Tagged<WeakArrayList> rtts = isolate->heap()->wasm_canonical_rtts();
  // This function can only get called from Wasm code, so we can safely assume
  // that the canonical RTT is still around.
  DirectHandle<Map> map(
      Cast<Map>(rtts->Get(wasm::TypeCanonicalizer::kPredefinedArrayI8Index)
                    .GetHeapObject()),
      isolate);
  Handle<WasmArray> array = isolate->factory()->NewWasmArray(
      wasm::kWasmI8, length, initial_value, map);
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->ElementAddress(0)), length};
  };
  Tagged<Object> write_result =
      EncodeWtf8(isolate, unibrow::Utf8Variant::kLossyUtf8, string,
                 get_writable_bytes, 0, MessageTemplate::kNone);
  DCHECK(IsNumber(write_result) && Object::NumberValue(write_result) == length);
  USE(write_result);
  return *array;
}

RUNTIME_FUNCTION(Runtime_WasmStringEncodeWtf16) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(6, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t memory = args.positive_smi_value_at(1);
  Tagged<String> string = Cast<String>(args[2]);
  double offset_double = args.number_value_at(3);
  uintptr_t offset = static_cast<uintptr_t>(offset_double);
  uint32_t start = args.positive_smi_value_at(4);
  uint32_t length = args.positive_smi_value_at(5);

  DCHECK(base::IsInBounds<uint32_t>(start, length, string->length()));

  size_t mem_size = trusted_instance_data->memory_size(memory);
  static_assert(String::kMaxLength <=
                (std::numeric_limits<size_t>::max() / sizeof(base::uc16)));
  if (!base::IsInBounds<size_t>(offset, length * sizeof(base::uc16),
                                mem_size)) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }
  if (offset & 1) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapUnalignedAccess);
  }

#if defined(V8_TARGET_LITTLE_ENDIAN)
  uint16_t* dst = reinterpret_cast<uint16_t*>(
      trusted_instance_data->memory_base(memory) + offset);
  String::WriteToFlat(string, dst, start, length);
#elif defined(V8_TARGET_BIG_ENDIAN)
  // TODO(12868): The host is big-endian but we need to write the string
  // contents as little-endian.
  USE(string);
  USE(start);
  UNIMPLEMENTED();
#else
#error Unknown endianness
#endif

  return Smi::zero();  // Unused.
}

RUNTIME_FUNCTION(Runtime_WasmStringAsWtf8) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);
  Handle<String> string(Cast<String>(args[0]), isolate);
  int wtf8_length = MeasureWtf8(isolate, string);
  Handle<ByteArray> array = isolate->factory()->NewByteArray(wtf8_length);

  auto utf8_variant = unibrow::Utf8Variant::kWtf8;
  auto get_writable_bytes =
      [&](const DisallowGarbageCollection&) -> base::Vector<char> {
    return {reinterpret_cast<char*>(array->begin()),
            static_cast<size_t>(wtf8_length)};
  };
  EncodeWtf8(isolate, utf8_variant, string, get_writable_bytes, 0,
             MessageTemplate::kWasmTrapArrayOutOfBounds);
  return *array;
}

RUNTIME_FUNCTION(Runtime_WasmStringViewWtf8Encode) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(7, args.length());
  HandleScope scope(isolate);
  Tagged<WasmTrustedInstanceData> trusted_instance_data =
      Cast<WasmTrustedInstanceData>(args[0]);
  uint32_t utf8_variant_value = args.positive_smi_value_at(1);
  DirectHandle<ByteArray> array(Cast<ByteArray>(args[2]), isolate);
  double addr_double = args.number_value_at(3);
  uintptr_t addr = static_cast<uintptr_t>(addr_double);
  uint32_t start = NumberToUint32(args[4]);
  uint32_t end = NumberToUint32(args[5]);
  uint32_t memory = args.positive_smi_value_at(6);

  DCHECK(utf8_variant_value <=
         static_cast<uint32_t>(unibrow::Utf8Variant::kLastUtf8Variant));
  DCHECK_LE(start, end);
  DCHECK(base::IsInBounds<size_t>(start, end - start, array->length()));

  auto utf8_variant = static_cast<unibrow::Utf8Variant>(utf8_variant_value);
  size_t length = end - start;

  if (!base::IsInBounds<size_t>(addr, length,
                                trusted_instance_data->memory_size(memory))) {
    return ThrowWasmError(isolate, MessageTemplate::kWasmTrapMemOutOfBounds);
  }

  uint8_t* memory_start = trusted_instance_data->memory_base(memory);
  const uint8_t* src = reinterpret_cast<const uint8_t*>(array->begin() + start);
  uint8_t* dst = memory_start + addr;

  std::vector<size_t> surrogates;
  if (utf8_variant != unibrow::Utf8Variant::kWtf8) {
    unibrow::Wtf8::ScanForSurrogates({src, length}, &surrogates);
    if (utf8_variant == unibrow::Utf8Variant::kUtf8 && !surrogates.empty()) {
      return ThrowWasmError(isolate,
                            MessageTemplate::kWasmTrapStringIsolatedSurrogate);
    }
  }

  MemCopy(dst, src, length);

  for (size_t surrogate : surrogates) {
    DCHECK_LT(surrogate, length);
    DCHECK_EQ(utf8_variant, unibrow::Utf8Variant::kLossyUtf8);
    unibrow::Utf8::Encode(reinterpret_cast<char*>(dst + surrogate),
                          unibrow::Utf8::kBadChar, 0, false);
  }

  // Unused.
  return Tagged<Smi>(0);
}

RUNTIME_FUNCTION(Runtime_WasmStringViewWtf8Slice) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(3, args.length());
  HandleScope scope(isolate);
  DirectHandle<ByteArray> array(Cast<ByteArray>(args[0]), isolate);
  uint32_t start = NumberToUint32(args[1]);
  uint32_t end = NumberToUint32(args[2]);

  DCHECK_LT(start, end);
  DCHECK(base::IsInBounds<size_t>(start, end - start, array->length()));

  // This can't throw because the result can't be too long if the input wasn't,
  // and encoding failures are ruled out too because {start}/{end} are aligned.
  return *isolate->factory()
              ->NewStringFromUtf8(array, start, end,
                                  unibrow::Utf8Variant::kWtf8)
              .ToHandleChecked();
}

#ifdef V8_ENABLE_DRUMBRAKE
RUNTIME_FUNCTION(Runtime_WasmTraceBeginExecution) {
  DCHECK(v8_flags.slow_histograms && !v8_flags.wasm_jitless &&
         v8_flags.wasm_enable_exec_time_histograms);
  DCHECK_EQ(0, args.length());
  HandleScope scope(isolate);

  wasm::WasmExecutionTimer* timer = isolate->wasm_execution_timer();
  timer->Start();

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_WasmTraceEndExecution) {
  DCHECK(v8_flags.slow_histograms && !v8_flags.wasm_jitless &&
         v8_flags.wasm_enable_exec_time_histograms);
  DCHECK_EQ(0, args.length());
  HandleScope scope(isolate);

  wasm::WasmExecutionTimer* timer = isolate->wasm_execution_timer();
  timer->Stop();

  return ReadOnlyRoots(isolate).undefined_value();
}
#endif  // V8_ENABLE_DRUMBRAKE

RUNTIME_FUNCTION(Runtime_WasmStringFromCodePoint) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  HandleScope scope(isolate);

  uint32_t code_point = NumberToUint32(args[0]);
  if (code_point <= unibrow::Utf16::kMaxNonSurrogateCharCode) {
    return *isolate->factory()->LookupSingleCharacterStringFromCode(code_point);
  }
  if (code_point > 0x10FFFF) {
    // Allocate a new number to preserve the to-uint conversion (e.g. if
    // args[0] == -1, we want the error message to report 4294967295).
    return ThrowWasmError(isolate, MessageTemplate::kInvalidCodePoint,
                          {isolate->factory()->NewNumberFromUint(code_point)});
  }

  base::uc16 char_buffer[] = {
      unibrow::Utf16::LeadSurrogate(code_point),
      unibrow::Utf16::TrailSurrogate(code_point),
  };
  DirectHandle<SeqTwoByteString> result =
      isolate->factory()
          ->NewRawTwoByteString(arraysize(char_buffer))
          .ToHandleChecked();
  DisallowGarbageCollection no_gc;
  CopyChars(result->GetChars(no_gc), char_buffer, arraysize(char_buffer));
  return *result;
}

RUNTIME_FUNCTION(Runtime_WasmStringHash) {
  ClearThreadInWasmScope flag_scope(isolate);
  DCHECK_EQ(1, args.length());
  Tagged<String> string(Cast<String>(args[0]));
  uint32_t hash = string->EnsureHash();
  return Smi::FromInt(static_cast<int>(hash));
}

}  // namespace v8::internal
