// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/codegen/compiler.h"
#include "src/logging/counters.h"
#include "src/objects/js-shadow-realm-inl.h"

namespace v8 {
namespace internal {

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm-constructor
BUILTIN(ShadowRealmConstructor) {
  HandleScope scope(isolate);
  // 1. If NewTarget is undefined, throw a TypeError exception.
  if (IsUndefined(*args.new_target(), isolate)) {  // [[Call]]
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kConstructorNotFunction,
                              isolate->factory()->ShadowRealm_string()));
  }
  // [[Construct]]
  DirectHandle<JSFunction> target = args.target();
  DirectHandle<JSReceiver> new_target = Cast<JSReceiver>(args.new_target());

  // 3. Let realmRec be CreateRealm().
  // 5. Let context be a new execution context.
  // 6. Set the Function of context to null.
  // 7. Set the Realm of context to realmRec.
  // 8. Set the ScriptOrModule of context to null.
  // 10. Perform ? SetRealmGlobalObject(realmRec, undefined, undefined).
  // 11. Perform ? SetDefaultGlobalBindings(O.[[ShadowRealm]]).
  // 12. Perform ? HostInitializeShadowRealm(O.[[ShadowRealm]]).
  // These steps are combined in
  // Isolate::RunHostCreateShadowRealmContextCallback and Context::New.
  // The host operation is hoisted for not creating a half-initialized
  // ShadowRealm object, which can fail the heap verification.
  DirectHandle<NativeContext> native_context;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, native_context,
      isolate->RunHostCreateShadowRealmContextCallback());

  // 2. Let O be ? OrdinaryCreateFromConstructor(NewTarget,
  // "%ShadowRealm.prototype%", « [[ShadowRealm]], [[ExecutionContext]] »).
  DirectHandle<JSObject> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, result,
                                     JSObject::New(target, new_target, {}));
  auto O = Cast<JSShadowRealm>(result);

  // 4. Set O.[[ShadowRealm]] to realmRec.
  // 9. Set O.[[ExecutionContext]] to context.
  O->set_native_context(*native_context);

  // 13. Return O.
  return *O;
}

namespace {

// https://tc39.es/proposal-shadowrealm/#sec-getwrappedvalue
MaybeDirectHandle<Object> GetWrappedValue(
    Isolate* isolate, DirectHandle<NativeContext> creation_context,
    Handle<Object> value) {
  // 1. If Type(value) is Object, then
  if (!IsJSReceiver(*value)) {
    // 2. Return value.
    return value;
  }
  // 1a. If IsCallable(value) is false, throw a TypeError exception.
  if (!IsCallable(*value)) {
    // The TypeError thrown is created with creation Realm's TypeError
    // constructor instead of the executing Realm's.
    THROW_NEW_ERROR_RETURN_VALUE(
        isolate,
        NewError(
            direct_handle(creation_context->type_error_function(), isolate),
            MessageTemplate::kNotCallable, value),
        {});
  }
  // 1b. Return ? WrappedFunctionCreate(callerRealm, value).
  return JSWrappedFunction::Create(isolate, creation_context,
                                   Cast<JSReceiver>(value));
}

}  // namespace

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm.prototype.evaluate
BUILTIN(ShadowRealmPrototypeEvaluate) {
  HandleScope scope(isolate);

  Handle<Object> source_text = args.atOrUndefined(isolate, 1);
  // 1. Let O be this value.
  DirectHandle<Object> receiver = args.receiver();

  Factory* factory = isolate->factory();

  // 2. Perform ? ValidateShadowRealmObject(O).
  if (!IsJSShadowRealm(*receiver)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kIncompatibleMethodReceiver));
  }
  auto shadow_realm = Cast<JSShadowRealm>(receiver);

  // 3. If Type(sourceText) is not String, throw a TypeError exception.
  if (!IsString(*source_text)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kInvalidShadowRealmEvaluateSourceText));
  }

  // 4. Let callerRealm be the current Realm Record.
  DirectHandle<NativeContext> caller_context = isolate->native_context();

  // 5. Let evalRealm be O.[[ShadowRealm]].
  DirectHandle<NativeContext> eval_context(shadow_realm->native_context(),
                                           isolate);
  // 6. Return ? PerformShadowRealmEval(sourceText, callerRealm, evalRealm).

  // PerformShadowRealmEval
  // https://tc39.es/proposal-shadowrealm/#sec-performshadowrealmeval
  // 1. Perform ? HostEnsureCanCompileStrings(callerRealm, evalRealm).
  // Run embedder pre-checks before executing the source code.
  MaybeDirectHandle<String> validated_source;
  bool unhandled_object;
  std::tie(validated_source, unhandled_object) =
      Compiler::ValidateDynamicCompilationSource(isolate, eval_context,
                                                 source_text);
  if (unhandled_object) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewTypeError(MessageTemplate::kInvalidShadowRealmEvaluateSourceText));
  }

  DirectHandle<JSObject> eval_global_proxy(eval_context->global_proxy(),
                                           isolate);
  MaybeHandle<Object> result;
  bool is_parse_failed = false;
  {
    // 8. If runningContext is not already suspended, suspend runningContext.
    // 9. Let evalContext be a new ECMAScript code execution context.
    // 10. Set evalContext's Function to null.
    // 11. Set evalContext's Realm to evalRealm.
    // 12. Set evalContext's ScriptOrModule to null.
    // 13. Set evalContext's VariableEnvironment to varEnv.
    // 14. Set evalContext's LexicalEnvironment to lexEnv.
    // 15. Set evalContext's PrivateEnvironment to null.
    // 16. Push evalContext onto the execution context stack; evalContext is now
    // the running execution context.
    SaveAndSwitchContext save(isolate, *eval_context);

    // 2. Perform the following substeps in an implementation-defined order,
    // possibly interleaving parsing and error detection:
    // 2a. Let script be ParseText(! StringToCodePoints(sourceText), Script).
    // 2b. If script is a List of errors, throw a SyntaxError exception.
    // 2c. If script Contains ScriptBody is false, return undefined.
    // 2d. Let body be the ScriptBody of script.
    // 2e. If body Contains NewTarget is true, throw a SyntaxError
    // exception.
    // 2f. If body Contains SuperProperty is true, throw a SyntaxError
    // exception.
    // 2g. If body Contains SuperCall is true, throw a SyntaxError exception.
    // 3. Let strictEval be IsStrict of script.
    // 4. Let runningContext be the running execution context.
    // 5. Let lexEnv be NewDeclarativeEnvironment(evalRealm.[[GlobalEnv]]).
    // 6. Let varEnv be evalRealm.[[GlobalEnv]].
    // 7. If strictEval is true, set varEnv to lexEnv.
    DirectHandle<JSFunction> function;
    MaybeDirectHandle<JSFunction> maybe_function =
        Compiler::GetFunctionFromValidatedString(eval_context, validated_source,
                                                 NO_PARSE_RESTRICTION,
                                                 kNoSourcePosition);
    if (maybe_function.is_null()) {
      is_parse_failed = true;
    } else {
      function = maybe_function.ToHandleChecked();

      // 17. Let result be EvalDeclarationInstantiation(body, varEnv,
      // lexEnv, null, strictEval).
      // 18. If result.[[Type]] is normal, then
      // 18a. a. Set result to Completion(Evaluation of body).
      // 19. If result.[[Type]] is normal and result.[[Value]] is empty, then
      // 19a. Set result to NormalCompletion(undefined).
      result = Execution::Call(isolate, function, eval_global_proxy, {});

      // 20. Suspend evalContext and remove it from the execution context stack.
      // 21. Resume the context that is now on the top of the execution context
      // stack as the running execution context. Done by the scope.
    }
  }

  if (result.is_null()) {
    DCHECK(isolate->has_exception());
    Handle<Object> exception(isolate->exception(), isolate);
    isolate->clear_internal_exception();
    if (is_parse_failed) {
      auto error_object = Cast<JSObject>(exception);
      auto message = Cast<String>(JSReceiver::GetDataProperty(
          isolate, error_object, factory->message_string()));

      return isolate->ReThrow(
          *factory->NewError(isolate->syntax_error_function(), message));
    }
    // 22. If result.[[Type]] is not NORMAL, then
    // 22a. Let copiedError be CreateTypeErrorCopy(callerRealm,
    // result.[[Value]]). 22b. Return ThrowCompletion(copiedError).
    DirectHandle<String> string =
        Object::NoSideEffectsToString(isolate, exception);
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        ShadowRealmNewTypeErrorCopy(
            exception, MessageTemplate::kCallShadowRealmEvaluateThrew, string));
  }
  // 23. Return ? GetWrappedValue(callerRealm, result.[[Value]]).
  DirectHandle<Object> wrapped_result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, wrapped_result,
      GetWrappedValue(isolate, caller_context, result.ToHandleChecked()));
  return *wrapped_result;
}

}  // namespace internal
}  // namespace v8
