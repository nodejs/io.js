// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/execution.h"

#include "src/api/api-inl.h"
#include "src/debug/debug.h"
#include "src/execution/frames.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/vm-state-inl.h"
#include "src/logging/runtime-call-stats-scope.h"

#if V8_ENABLE_WEBASSEMBLY
#include "src/compiler/wasm-compiler.h"  // Only for static asserts.
#include "src/wasm/code-space-access.h"
#include "src/wasm/wasm-engine.h"
#endif  // V8_ENABLE_WEBASSEMBLY

namespace v8 {
namespace internal {

namespace {

Handle<Object> NormalizeReceiver(Isolate* isolate, Handle<Object> receiver) {
  // Convert calls on global objects to be calls on the global
  // receiver instead to avoid having a 'this' pointer which refers
  // directly to a global object.
  if (IsJSGlobalObject(*receiver)) {
    return handle(Cast<JSGlobalObject>(receiver)->global_proxy(), isolate);
  }
  return receiver;
}

struct InvokeParams {
  static InvokeParams SetUpForNew(Isolate* isolate, Handle<Object> constructor,
                                  Handle<Object> new_target, int argc,
                                  Handle<Object>* argv);

  static InvokeParams SetUpForCall(Isolate* isolate, Handle<Object> callable,
                                   Handle<Object> receiver, int argc,
                                   Handle<Object>* argv);

  static InvokeParams SetUpForTryCall(
      Isolate* isolate, Handle<Object> callable, Handle<Object> receiver,
      int argc, Handle<Object>* argv,
      Execution::MessageHandling message_handling,
      MaybeHandle<Object>* exception_out);

  static InvokeParams SetUpForRunMicrotasks(Isolate* isolate,
                                            MicrotaskQueue* microtask_queue);

  bool IsScript() const {
    if (!IsJSFunction(*target)) return false;
    auto function = Cast<JSFunction>(target);
    return function->shared()->is_script();
  }

  Handle<FixedArray> GetAndResetHostDefinedOptions() {
    DCHECK(IsScript());
    DCHECK_EQ(argc, 1);
    auto options = Cast<FixedArray>(argv[0]);
    argv = nullptr;
    argc = 0;
    return options;
  }

  Handle<Object> target;
  Handle<Object> receiver;
  int argc;
  Handle<Object>* argv;
  Handle<Object> new_target;

  MicrotaskQueue* microtask_queue;

  Execution::MessageHandling message_handling;
  MaybeHandle<Object>* exception_out;

  bool is_construct;
  Execution::Target execution_target;
};

// static
InvokeParams InvokeParams::SetUpForNew(Isolate* isolate,
                                       Handle<Object> constructor,
                                       Handle<Object> new_target, int argc,
                                       Handle<Object>* argv) {
  InvokeParams params;
  params.target = constructor;
  params.receiver = isolate->factory()->undefined_value();
  DCHECK(!params.IsScript());
  params.argc = argc;
  params.argv = argv;
  params.new_target = new_target;
  params.microtask_queue = nullptr;
  params.message_handling = Execution::MessageHandling::kReport;
  params.exception_out = nullptr;
  params.is_construct = true;
  params.execution_target = Execution::Target::kCallable;
  return params;
}

// static
InvokeParams InvokeParams::SetUpForCall(Isolate* isolate,
                                        Handle<Object> callable,
                                        Handle<Object> receiver, int argc,
                                        Handle<Object>* argv) {
  InvokeParams params;
  params.target = callable;
  params.receiver = NormalizeReceiver(isolate, receiver);
  // Check for host-defined options argument for scripts.
  DCHECK_IMPLIES(params.IsScript(), argc == 1);
  DCHECK_IMPLIES(params.IsScript(), IsFixedArray(*argv[0]));
  params.argc = argc;
  params.argv = argv;
  params.new_target = isolate->factory()->undefined_value();
  params.microtask_queue = nullptr;
  params.message_handling = Execution::MessageHandling::kReport;
  params.exception_out = nullptr;
  params.is_construct = false;
  params.execution_target = Execution::Target::kCallable;
  return params;
}

// static
InvokeParams InvokeParams::SetUpForTryCall(
    Isolate* isolate, Handle<Object> callable, Handle<Object> receiver,
    int argc, Handle<Object>* argv, Execution::MessageHandling message_handling,
    MaybeHandle<Object>* exception_out) {
  InvokeParams params;
  params.target = callable;
  params.receiver = NormalizeReceiver(isolate, receiver);
  // Check for host-defined options argument for scripts.
  DCHECK_IMPLIES(params.IsScript(), argc == 1);
  DCHECK_IMPLIES(params.IsScript(), IsFixedArray(*argv[0]));
  params.argc = argc;
  params.argv = argv;
  params.new_target = isolate->factory()->undefined_value();
  params.microtask_queue = nullptr;
  params.message_handling = message_handling;
  params.exception_out = exception_out;
  params.is_construct = false;
  params.execution_target = Execution::Target::kCallable;
  return params;
}

// static
InvokeParams InvokeParams::SetUpForRunMicrotasks(
    Isolate* isolate, MicrotaskQueue* microtask_queue) {
  auto undefined = isolate->factory()->undefined_value();
  InvokeParams params;
  params.target = undefined;
  params.receiver = undefined;
  params.argc = 0;
  params.argv = nullptr;
  params.new_target = undefined;
  params.microtask_queue = microtask_queue;
  params.message_handling = Execution::MessageHandling::kReport;
  params.exception_out = nullptr;
  params.is_construct = false;
  params.execution_target = Execution::Target::kRunMicrotasks;
  return params;
}

Handle<Code> JSEntry(Isolate* isolate, Execution::Target execution_target,
                     bool is_construct) {
  if (is_construct) {
    DCHECK_EQ(Execution::Target::kCallable, execution_target);
    return BUILTIN_CODE(isolate, JSConstructEntry);
  } else if (execution_target == Execution::Target::kCallable) {
    DCHECK(!is_construct);
    return BUILTIN_CODE(isolate, JSEntry);
  } else if (execution_target == Execution::Target::kRunMicrotasks) {
    DCHECK(!is_construct);
    return BUILTIN_CODE(isolate, JSRunMicrotasksEntry);
  }
  UNREACHABLE();
}

MaybeHandle<Context> NewScriptContext(
    Isolate* isolate, DirectHandle<JSFunction> function,
    DirectHandle<FixedArray> host_defined_options) {
  // TODO(cbruni, 1244145): Use passed in host_defined_options.
  // Creating a script context is a side effect, so abort if that's not
  // allowed.
  if (isolate->should_check_side_effects()) {
    isolate->Throw(*isolate->factory()->NewEvalError(
        MessageTemplate::kNoSideEffectDebugEvaluate));
    return MaybeHandle<Context>();
  }
  SaveAndSwitchContext save(isolate, function->context());
  Tagged<SharedFunctionInfo> sfi = function->shared();
  Handle<Script> script(Cast<Script>(sfi->script()), isolate);
  Handle<ScopeInfo> scope_info(sfi->scope_info(), isolate);
  DirectHandle<NativeContext> native_context(
      Cast<NativeContext>(function->context()), isolate);
  Handle<JSGlobalObject> global_object(native_context->global_object(),
                                       isolate);
  Handle<ScriptContextTable> script_context(
      native_context->script_context_table(), isolate);

  // Find name clashes.
  for (auto it : ScopeInfo::IterateLocalNames(scope_info)) {
    Handle<String> name(it->name(), isolate);
    VariableMode mode = scope_info->ContextLocalMode(it->index());
    VariableLookupResult lookup;
    if (script_context->Lookup(name, &lookup)) {
      if (IsLexicalVariableMode(mode) || IsLexicalVariableMode(lookup.mode)) {
        DirectHandle<Context> context(script_context->get(lookup.context_index),
                                      isolate);
        // If we are trying to re-declare a REPL-mode let as a let or REPL-mode
        // const as a const, allow it.
        // TODO(rezvan): Add check and related tests for VariableMode::kUsing.
        if (!(((mode == VariableMode::kLet &&
                lookup.mode == VariableMode::kLet) ||
               (mode == VariableMode::kConst &&
                lookup.mode == VariableMode::kConst)) &&
              scope_info->IsReplModeScope() &&
              context->scope_info()->IsReplModeScope())) {
          // ES#sec-globaldeclarationinstantiation 5.b:
          // If envRec.HasLexicalDeclaration(name) is true, throw a SyntaxError
          // exception.
          MessageLocation location(script, 0, 1);
          isolate->ThrowAt(isolate->factory()->NewSyntaxError(
                               MessageTemplate::kVarRedeclaration, name),
                           &location);
          return MaybeHandle<Context>();
        }
      }
    }

    if (IsLexicalVariableMode(mode)) {
      LookupIterator it(isolate, global_object, name, global_object,
                        LookupIterator::OWN_SKIP_INTERCEPTOR);
      Maybe<PropertyAttributes> maybe = JSReceiver::GetPropertyAttributes(&it);
      // Can't fail since the we looking up own properties on the global object
      // skipping interceptors.
      CHECK(!maybe.IsNothing());
      if ((maybe.FromJust() & DONT_DELETE) != 0) {
        // ES#sec-globaldeclarationinstantiation 5.a:
        // If envRec.HasVarDeclaration(name) is true, throw a SyntaxError
        // exception.
        // ES#sec-globaldeclarationinstantiation 5.d:
        // If hasRestrictedGlobal is true, throw a SyntaxError exception.
        MessageLocation location(script, 0, 1);
        isolate->ThrowAt(isolate->factory()->NewSyntaxError(
                             MessageTemplate::kVarRedeclaration, name),
                         &location);
        return MaybeHandle<Context>();
      }

      JSGlobalObject::InvalidatePropertyCell(global_object, name);
    }
  }

  Handle<Context> result =
      isolate->factory()->NewScriptContext(native_context, scope_info);

  result->Initialize(isolate);
  // In REPL mode, we are allowed to add/modify let/const variables.
  // We use the previous defined script context for those.
  const bool ignore_duplicates = scope_info->IsReplModeScope();
  DirectHandle<ScriptContextTable> new_script_context_table =
      ScriptContextTable::Add(isolate, script_context, result,
                              ignore_duplicates);
  native_context->synchronized_set_script_context_table(
      *new_script_context_table);
  return result;
}

V8_WARN_UNUSED_RESULT MaybeHandle<Object> Invoke(Isolate* isolate,
                                                 const InvokeParams& params) {
  RCS_SCOPE(isolate, RuntimeCallCounterId::kInvoke);
  DCHECK(!IsJSGlobalObject(*params.receiver));
  DCHECK_LE(params.argc, FixedArray::kMaxLength);
  DCHECK(!isolate->has_exception());

#if V8_ENABLE_WEBASSEMBLY
  // If we have PKU support for Wasm, ensure that code is currently write
  // protected for this thread.
  DCHECK_IMPLIES(wasm::GetWasmCodeManager()->HasMemoryProtectionKeySupport(),
                 !wasm::GetWasmCodeManager()->MemoryProtectionKeyWritable());
#endif  // V8_ENABLE_WEBASSEMBLY

#ifdef USE_SIMULATOR
  // Simulators use separate stacks for C++ and JS. JS stack overflow checks
  // are performed whenever a JS function is called. However, it can be the case
  // that the C++ stack grows faster than the JS stack, resulting in an overflow
  // there. Add a check here to make that less likely.
  StackLimitCheck check(isolate);
  if (check.HasOverflowed()) {
    isolate->StackOverflow();
    isolate->ReportPendingMessages(params.message_handling ==
                                   Execution::MessageHandling::kReport);
    return MaybeHandle<Object>();
  }
#endif

  // api callbacks can be called directly, unless we want to take the detour
  // through JS to set up a frame for break-at-entry.
  if (IsJSFunction(*params.target)) {
    auto function = Cast<JSFunction>(params.target);
    if ((!params.is_construct || IsConstructor(*function)) &&
        function->shared()->IsApiFunction() &&
        !function->shared()->BreakAtEntry(isolate)) {
      SaveAndSwitchContext save(isolate, function->context());
      DCHECK(IsJSGlobalObject(function->context()->global_object()));

      Handle<Object> receiver = params.is_construct
                                    ? isolate->factory()->the_hole_value()
                                    : params.receiver;
      Handle<FunctionTemplateInfo> fun_data(function->shared()->api_func_data(),
                                            isolate);
      auto value = Builtins::InvokeApiFunction(
          isolate, params.is_construct, fun_data, receiver, params.argc,
          params.argv, Cast<HeapObject>(params.new_target));
      bool has_exception = value.is_null();
      DCHECK_EQ(has_exception, isolate->has_exception());
      if (has_exception) {
        isolate->ReportPendingMessages(params.message_handling ==
                                       Execution::MessageHandling::kReport);
        return MaybeHandle<Object>();
      } else {
        isolate->clear_pending_message();
      }
      return value;
    }
#ifdef DEBUG
    if (function->shared()->is_script()) {
      DCHECK(params.IsScript());
      DCHECK(IsJSGlobalProxy(*params.receiver));
      DCHECK_EQ(params.argc, 1);
      DCHECK(IsFixedArray(*params.argv[0]));
    } else {
      DCHECK(!params.IsScript());
    }
#endif
    // Set up a ScriptContext when running scripts that need it.
    if (function->shared()->needs_script_context()) {
      Handle<Context> context;
      DirectHandle<FixedArray> host_defined_options =
          const_cast<InvokeParams&>(params).GetAndResetHostDefinedOptions();
      if (!NewScriptContext(isolate, function, host_defined_options)
               .ToHandle(&context)) {
        isolate->ReportPendingMessages(params.message_handling ==
                                       Execution::MessageHandling::kReport);
        return MaybeHandle<Object>();
      }

      // We mutate the context if we allocate a script context. This is
      // guaranteed to only happen once in a native context since scripts will
      // always produce name clashes with themselves.
      function->set_context(*context);
    }
  }

  // Entering JavaScript.
  VMState<JS> state(isolate);
  if (!AllowJavascriptExecution::IsAllowed(isolate)) {
    GRACEFUL_FATAL("Invoke in DisallowJavascriptExecutionScope");
  }
  if (!ThrowOnJavascriptExecution::IsAllowed(isolate)) {
    isolate->ThrowIllegalOperation();
    isolate->ReportPendingMessages(params.message_handling ==
                                   Execution::MessageHandling::kReport);
    return MaybeHandle<Object>();
  }
  if (!DumpOnJavascriptExecution::IsAllowed(isolate)) {
    V8::GetCurrentPlatform()->DumpWithoutCrashing();
    return isolate->factory()->undefined_value();
  }
  isolate->IncrementJavascriptExecutionCounter();

  if (params.execution_target == Execution::Target::kCallable) {
    Handle<NativeContext> context = isolate->native_context();
    if (!IsUndefined(context->script_execution_callback(), isolate)) {
      v8::Context::AbortScriptExecutionCallback callback =
          v8::ToCData<v8::Context::AbortScriptExecutionCallback,
                      kApiAbortScriptExecutionCallbackTag>(
              isolate, context->script_execution_callback());
      v8::Isolate* api_isolate = reinterpret_cast<v8::Isolate*>(isolate);
      v8::Local<v8::Context> api_context = v8::Utils::ToLocal(context);
      callback(api_isolate, api_context);
      DCHECK(!isolate->has_exception());
      // Always throw an exception to abort execution, if callback exists.
      isolate->ThrowIllegalOperation();
      return MaybeHandle<Object>();
    }
  }

  // Placeholder for return value.
  Tagged<Object> value;
  DirectHandle<Code> code =
      JSEntry(isolate, params.execution_target, params.is_construct);
  {
    // Save and restore context around invocation and block the
    // allocation of handles without explicit handle scopes.
    SaveContext save(isolate);
    SealHandleScope shs(isolate);

    if (v8_flags.clear_exceptions_on_js_entry) isolate->clear_exception();

    if (params.execution_target == Execution::Target::kCallable) {
      // clang-format off
      // {new_target}, {target}, {receiver}, return value: tagged pointers
      // {argv}: pointer to array of tagged pointers
      using JSEntryFunction = GeneratedCode<Address(
          Address root_register_value, Address new_target, Address target,
          Address receiver, intptr_t argc, Address** argv)>;
      // clang-format on
      JSEntryFunction stub_entry =
          JSEntryFunction::FromAddress(isolate, code->instruction_start());

      Address orig_func = (*params.new_target).ptr();
      Address func = (*params.target).ptr();
      Address recv = (*params.receiver).ptr();
      Address** argv = reinterpret_cast<Address**>(params.argv);
      RCS_SCOPE(isolate, RuntimeCallCounterId::kJS_Execution);
      value = Tagged<Object>(
          stub_entry.Call(isolate->isolate_data()->isolate_root(), orig_func,
                          func, recv, JSParameterCount(params.argc), argv));
    } else {
      DCHECK_EQ(Execution::Target::kRunMicrotasks, params.execution_target);

      // clang-format off
      // return value: tagged pointers
      // {microtask_queue}: pointer to a C++ object
      using JSEntryFunction = GeneratedCode<Address(
          Address root_register_value, MicrotaskQueue* microtask_queue)>;
      // clang-format on
      JSEntryFunction stub_entry =
          JSEntryFunction::FromAddress(isolate, code->instruction_start());

      RCS_SCOPE(isolate, RuntimeCallCounterId::kJS_Execution);
      value = Tagged<Object>(stub_entry.Call(
          isolate->isolate_data()->isolate_root(), params.microtask_queue));
    }
  }

#ifdef VERIFY_HEAP
  if (v8_flags.verify_heap) {
    Object::ObjectVerify(value, isolate);
  }
#endif

  // Update the pending exception flag and return the value.
  bool has_exception = IsException(value, isolate);
  DCHECK_EQ(has_exception, isolate->has_exception());
  if (has_exception) {
    isolate->ReportPendingMessages(params.message_handling ==
                                   Execution::MessageHandling::kReport);
    return MaybeHandle<Object>();
  } else {
    isolate->clear_pending_message();
  }

  return Handle<Object>(value, isolate);
}

MaybeHandle<Object> InvokeWithTryCatch(Isolate* isolate,
                                       const InvokeParams& params) {
  DCHECK_IMPLIES(v8_flags.strict_termination_checks,
                 !isolate->is_execution_terminating());
  MaybeHandle<Object> maybe_result;
  if (params.exception_out != nullptr) {
    *params.exception_out = {};
  }

  // Enter a try-block while executing the JavaScript code. To avoid
  // duplicate error printing it must be non-verbose.  Also, to avoid
  // creating message objects during stack overflow we shouldn't
  // capture messages.
  v8::TryCatch catcher(reinterpret_cast<v8::Isolate*>(isolate));
  catcher.SetVerbose(false);
  catcher.SetCaptureMessage(false);

  maybe_result = Invoke(isolate, params);

  if (V8_LIKELY(!maybe_result.is_null())) {
    DCHECK(!isolate->has_exception());
    return maybe_result;
  }

  DCHECK(isolate->has_exception());
  if (isolate->is_execution_terminating()) {
    return maybe_result;
  }

  if (params.exception_out != nullptr) {
    DCHECK(catcher.HasCaught());
    *params.exception_out = v8::Utils::OpenHandle(*catcher.Exception());
  }

  return maybe_result;
}

}  // namespace

// static
MaybeHandle<Object> Execution::Call(Isolate* isolate, Handle<Object> callable,
                                    Handle<Object> receiver, int argc,
                                    Handle<Object> argv[]) {
  // Use Execution::CallScript instead for scripts:
  DCHECK_IMPLIES(IsJSFunction(*callable),
                 !Cast<JSFunction>(*callable)->shared()->is_script());
  return Invoke(isolate, InvokeParams::SetUpForCall(isolate, callable, receiver,
                                                    argc, argv));
}

// static
MaybeHandle<Object> Execution::CallScript(Isolate* isolate,
                                          Handle<JSFunction> script_function,
                                          Handle<Object> receiver,
                                          Handle<Object> host_defined_options) {
  DCHECK(script_function->shared()->is_script());
  DCHECK(IsJSGlobalProxy(*receiver) || IsJSGlobalObject(*receiver));
  return Invoke(
      isolate, InvokeParams::SetUpForCall(isolate, script_function, receiver, 1,
                                          &host_defined_options));
}

MaybeHandle<Object> Execution::CallBuiltin(Isolate* isolate,
                                           Handle<JSFunction> builtin,
                                           Handle<Object> receiver, int argc,
                                           Handle<Object> argv[]) {
  DCHECK(builtin->code(isolate)->is_builtin());
  DisableBreak no_break(isolate->debug());
  return Invoke(isolate, InvokeParams::SetUpForCall(isolate, builtin, receiver,
                                                    argc, argv));
}

// static
MaybeHandle<Object> Execution::New(Isolate* isolate, Handle<Object> constructor,
                                   int argc, Handle<Object> argv[]) {
  return New(isolate, constructor, constructor, argc, argv);
}

// static
MaybeHandle<Object> Execution::New(Isolate* isolate, Handle<Object> constructor,
                                   Handle<Object> new_target, int argc,
                                   Handle<Object> argv[]) {
  return Invoke(isolate, InvokeParams::SetUpForNew(isolate, constructor,
                                                   new_target, argc, argv));
}

// static
MaybeHandle<Object> Execution::TryCallScript(
    Isolate* isolate, Handle<JSFunction> script_function,
    Handle<Object> receiver, Handle<FixedArray> host_defined_options) {
  DCHECK(script_function->shared()->is_script());
  DCHECK(IsJSGlobalProxy(*receiver) || IsJSGlobalObject(*receiver));
  Handle<Object> argument = host_defined_options;
  return InvokeWithTryCatch(
      isolate, InvokeParams::SetUpForTryCall(
                   isolate, script_function, receiver, 1, &argument,
                   MessageHandling::kKeepPending, nullptr));
}

// static
MaybeHandle<Object> Execution::TryCall(Isolate* isolate,
                                       Handle<Object> callable,
                                       Handle<Object> receiver, int argc,
                                       Handle<Object> argv[],
                                       MessageHandling message_handling,
                                       MaybeHandle<Object>* exception_out) {
  // Use Execution::TryCallScript instead for scripts:
  DCHECK_IMPLIES(IsJSFunction(*callable),
                 !Cast<JSFunction>(*callable)->shared()->is_script());
  return InvokeWithTryCatch(
      isolate,
      InvokeParams::SetUpForTryCall(isolate, callable, receiver, argc, argv,
                                    message_handling, exception_out));
}

// static
MaybeHandle<Object> Execution::TryRunMicrotasks(
    Isolate* isolate, MicrotaskQueue* microtask_queue) {
  return InvokeWithTryCatch(
      isolate, InvokeParams::SetUpForRunMicrotasks(isolate, microtask_queue));
}

struct StackHandlerMarker {
  Address next;
  Address padding;
};
static_assert(offsetof(StackHandlerMarker, next) ==
              StackHandlerConstants::kNextOffset);
static_assert(offsetof(StackHandlerMarker, padding) ==
              StackHandlerConstants::kPaddingOffset);
static_assert(sizeof(StackHandlerMarker) == StackHandlerConstants::kSize);

#if V8_ENABLE_WEBASSEMBLY
void Execution::CallWasm(Isolate* isolate, DirectHandle<Code> wrapper_code,
                         Address wasm_call_target,
                         DirectHandle<Object> object_ref, Address packed_args) {
  using WasmEntryStub = GeneratedCode<Address(
      Address target, Address object_ref, Address argv, Address c_entry_fp)>;
  WasmEntryStub stub_entry =
      WasmEntryStub::FromAddress(isolate, wrapper_code->instruction_start());

  // Save and restore context around invocation and block the
  // allocation of handles without explicit handle scopes.
  SaveContext save(isolate);
  SealHandleScope shs(isolate);

  Address saved_c_entry_fp = *isolate->c_entry_fp_address();
  Address saved_js_entry_sp = *isolate->js_entry_sp_address();
  if (saved_js_entry_sp == kNullAddress) {
    *isolate->js_entry_sp_address() = GetCurrentStackPosition();
  }
  StackHandlerMarker stack_handler;
  stack_handler.next = isolate->thread_local_top()->handler_;
#ifdef V8_USE_ADDRESS_SANITIZER
  stack_handler.padding = GetCurrentStackPosition();
#else
  stack_handler.padding = 0;
#endif
  isolate->thread_local_top()->handler_ =
      reinterpret_cast<Address>(&stack_handler);
  trap_handler::SetThreadInWasm();

  {
    RCS_SCOPE(isolate, RuntimeCallCounterId::kJS_Execution);
    static_assert(compiler::CWasmEntryParameters::kCodeEntry == 0);
    static_assert(compiler::CWasmEntryParameters::kObjectRef == 1);
    static_assert(compiler::CWasmEntryParameters::kArgumentsBuffer == 2);
    static_assert(compiler::CWasmEntryParameters::kCEntryFp == 3);
    Address result = stub_entry.Call(wasm_call_target, (*object_ref).ptr(),
                                     packed_args, saved_c_entry_fp);
    if (result != kNullAddress) isolate->set_exception(Tagged<Object>(result));
  }

  // If there was an exception, then the thread-in-wasm flag is cleared
  // already.
  if (trap_handler::IsThreadInWasm()) {
    trap_handler::ClearThreadInWasm();
  }
  isolate->thread_local_top()->handler_ = stack_handler.next;
  if (saved_js_entry_sp == kNullAddress) {
    *isolate->js_entry_sp_address() = saved_js_entry_sp;
  }
  *isolate->c_entry_fp_address() = saved_c_entry_fp;
}
#endif  // V8_ENABLE_WEBASSEMBLY

}  // namespace internal
}  // namespace v8
