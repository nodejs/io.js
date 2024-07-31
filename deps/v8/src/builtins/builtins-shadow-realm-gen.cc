// Copyright 2022 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/objects/descriptor-array.h"
#include "src/objects/js-shadow-realm.h"
#include "src/objects/module.h"

namespace v8 {
namespace internal {

class ShadowRealmBuiltinsAssembler : public CodeStubAssembler {
 public:
  explicit ShadowRealmBuiltinsAssembler(compiler::CodeAssemblerState* state)
      : CodeStubAssembler(state) {}

  enum ImportValueFulfilledFunctionContextSlot {
    kEvalContextSlot = Context::MIN_CONTEXT_SLOTS,
    kSpecifierSlot,
    kExportNameSlot,
    kContextLength,
  };

 protected:
  TNode<JSObject> AllocateJSWrappedFunction(TNode<Context> context,
                                            TNode<Object> target);
  void CheckAccessor(TNode<DescriptorArray> array, TNode<IntPtrT> index,
                     TNode<Name> name, Label* bailout);
  TNode<Object> ImportValue(TNode<NativeContext> caller_context,
                            TNode<NativeContext> eval_context,
                            TNode<String> specifier, TNode<String> export_name);
  TNode<Context> CreateImportValueFulfilledFunctionContext(
      TNode<NativeContext> caller_context, TNode<NativeContext> eval_context,
      TNode<String> specifier, TNode<String> export_name);
  TNode<JSFunction> AllocateImportValueFulfilledFunction(
      TNode<NativeContext> caller_context, TNode<NativeContext> eval_context,
      TNode<String> specifier, TNode<String> export_name);
  void ShadowRealmThrow(TNode<Context> context,
                        MessageTemplate fallback_message,
                        TNode<Object> exception);
};

TNode<JSObject> ShadowRealmBuiltinsAssembler::AllocateJSWrappedFunction(
    TNode<Context> context, TNode<Object> target) {
  TNode<NativeContext> native_context = LoadNativeContext(context);
  TNode<Map> map = CAST(
      LoadContextElement(native_context, Context::WRAPPED_FUNCTION_MAP_INDEX));
  TNode<JSObject> wrapped = AllocateJSObjectFromMap(map);
  StoreObjectFieldNoWriteBarrier(
      wrapped, JSWrappedFunction::kWrappedTargetFunctionOffset, target);
  StoreObjectFieldNoWriteBarrier(wrapped, JSWrappedFunction::kContextOffset,
                                 context);
  return wrapped;
}

TNode<Context>
ShadowRealmBuiltinsAssembler::CreateImportValueFulfilledFunctionContext(
    TNode<NativeContext> caller_context, TNode<NativeContext> eval_context,
    TNode<String> specifier, TNode<String> export_name) {
  const TNode<Context> context = AllocateSyntheticFunctionContext(
      caller_context, ImportValueFulfilledFunctionContextSlot::kContextLength);
  StoreContextElementNoWriteBarrier(
      context, ImportValueFulfilledFunctionContextSlot::kEvalContextSlot,
      eval_context);
  StoreContextElementNoWriteBarrier(
      context, ImportValueFulfilledFunctionContextSlot::kSpecifierSlot,
      specifier);
  StoreContextElementNoWriteBarrier(
      context, ImportValueFulfilledFunctionContextSlot::kExportNameSlot,
      export_name);
  return context;
}

TNode<JSFunction>
ShadowRealmBuiltinsAssembler::AllocateImportValueFulfilledFunction(
    TNode<NativeContext> caller_context, TNode<NativeContext> eval_context,
    TNode<String> specifier, TNode<String> export_name) {
  const TNode<Context> function_context =
      CreateImportValueFulfilledFunctionContext(caller_context, eval_context,
                                                specifier, export_name);
  const TNode<Map> function_map = CAST(LoadContextElement(
      caller_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX));
  const TNode<SharedFunctionInfo> info =
      ShadowRealmImportValueFulfilledSFIConstant();

  return AllocateFunctionWithMapAndContext(function_map, info,
                                           function_context);
}

void ShadowRealmBuiltinsAssembler::CheckAccessor(TNode<DescriptorArray> array,
                                                 TNode<IntPtrT> index,
                                                 TNode<Name> name,
                                                 Label* bailout) {
  TNode<Name> key = LoadKeyByDescriptorEntry(array, index);
  GotoIfNot(TaggedEqual(key, name), bailout);
  TNode<Object> value = LoadValueByDescriptorEntry(array, index);
  GotoIfNot(IsAccessorInfo(CAST(value)), bailout);
}

void ShadowRealmBuiltinsAssembler::ShadowRealmThrow(
    TNode<Context> context, MessageTemplate fallback_message,
    TNode<Object> exception) {
  TNode<Smi> template_index = SmiConstant(static_cast<int>(fallback_message));
  CallRuntime(Runtime::kShadowRealmThrow, context, template_index, exception);
  Unreachable();
}

// https://tc39.es/proposal-shadowrealm/#sec-getwrappedvalue
TF_BUILTIN(ShadowRealmGetWrappedValue, ShadowRealmBuiltinsAssembler) {
  auto context = Parameter<Context>(Descriptor::kContext);
  auto creation_context = Parameter<Context>(Descriptor::kCreationContext);
  auto target_context = Parameter<Context>(Descriptor::kTargetContext);
  auto value = Parameter<Object>(Descriptor::kValue);

  Label if_primitive(this), if_callable(this), unwrap(this), wrap(this),
      slow_wrap(this, Label::kDeferred), bailout(this, Label::kDeferred);

  // 2. Return value.
  GotoIf(TaggedIsSmi(value), &if_primitive);
  GotoIfNot(JSAnyIsNotPrimitive(CAST(value)), &if_primitive);

  // 1. If Type(value) is Object, then
  // 1a. If IsCallable(value) is false, throw a TypeError exception.
  // 1b. Return ? WrappedFunctionCreate(callerRealm, value).
  Branch(IsCallable(CAST(value)), &if_callable, &bailout);

  BIND(&if_primitive);
  Return(value);

  BIND(&if_callable);
  TVARIABLE(Object, target);
  target = value;
  // WrappedFunctionCreate
  // https://tc39.es/proposal-shadowrealm/#sec-wrappedfunctioncreate
  Branch(IsJSWrappedFunction(CAST(value)), &unwrap, &wrap);

  BIND(&unwrap);
  // The intermediate wrapped functions are not user-visible. And calling a
  // wrapped function won't cause a side effect in the creation realm.
  // Unwrap here to avoid nested unwrapping at the call site.
  TNode<JSWrappedFunction> target_wrapped_function = CAST(value);
  target = LoadObjectField(target_wrapped_function,
                           JSWrappedFunction::kWrappedTargetFunctionOffset);
  Goto(&wrap);

  BIND(&wrap);
  // Disallow wrapping of slow-mode functions. We need to figure out
  // whether the length and name property are in the original state.
  TNode<Map> map = LoadMap(CAST(target.value()));
  GotoIf(IsDictionaryMap(map), &slow_wrap);

  // Check whether the length and name properties are still present as
  // AccessorInfo objects. If so, their value can be recomputed even if
  // the actual value on the object changes.
  TNode<Uint32T> bit_field3 = LoadMapBitField3(map);
  TNode<IntPtrT> number_of_own_descriptors = Signed(
      DecodeWordFromWord32<Map::Bits3::NumberOfOwnDescriptorsBits>(bit_field3));
  GotoIf(IntPtrLessThan(
             number_of_own_descriptors,
             IntPtrConstant(JSFunction::kMinDescriptorsForFastBindAndWrap)),
         &slow_wrap);

  // We don't need to check the exact accessor here because the only case
  // custom accessor arise is with function templates via API, and in that
  // case the object is in dictionary mode
  TNode<DescriptorArray> descriptors = LoadMapInstanceDescriptors(map);
  CheckAccessor(
      descriptors,
      IntPtrConstant(
          JSFunctionOrBoundFunctionOrWrappedFunction::kLengthDescriptorIndex),
      LengthStringConstant(), &slow_wrap);
  CheckAccessor(
      descriptors,
      IntPtrConstant(
          JSFunctionOrBoundFunctionOrWrappedFunction::kNameDescriptorIndex),
      NameStringConstant(), &slow_wrap);

  // Verify that prototype matches the function prototype of the target
  // context.
  TNode<Object> prototype = LoadMapPrototype(map);
  TNode<Object> function_map =
      LoadContextElement(target_context, Context::WRAPPED_FUNCTION_MAP_INDEX);
  TNode<Object> function_prototype = LoadMapPrototype(CAST(function_map));
  GotoIf(TaggedNotEqual(prototype, function_prototype), &slow_wrap);

  // 1. Let internalSlotsList be the internal slots listed in Table 2, plus
  // [[Prototype]] and [[Extensible]].
  // 2. Let wrapped be ! MakeBasicObject(internalSlotsList).
  // 3. Set wrapped.[[Prototype]] to
  // callerRealm.[[Intrinsics]].[[%Function.prototype%]].
  // 4. Set wrapped.[[Call]] as described in 2.1.
  // 5. Set wrapped.[[WrappedTargetFunction]] to Target.
  // 6. Set wrapped.[[Realm]] to callerRealm.
  // 7. Let result be CopyNameAndLength(wrapped, Target, "wrapped").
  // 8. If result is an Abrupt Completion, throw a TypeError exception.
  // Installed with default accessors.
  TNode<JSObject> wrapped =
      AllocateJSWrappedFunction(creation_context, target.value());

  // 9. Return wrapped.
  Return(wrapped);

  BIND(&slow_wrap);
  {
    Return(CallRuntime(Runtime::kShadowRealmWrappedFunctionCreate, context,
                       creation_context, target.value()));
  }

  BIND(&bailout);
  ThrowTypeError(context, MessageTemplate::kNotCallable, value);
}

// https://tc39.es/proposal-shadowrealm/#sec-wrapped-function-exotic-objects-call-thisargument-argumentslist
TF_BUILTIN(CallWrappedFunction, ShadowRealmBuiltinsAssembler) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  TNode<IntPtrT> argc_ptr = ChangeInt32ToIntPtr(argc);
  auto wrapped_function = Parameter<JSWrappedFunction>(Descriptor::kFunction);
  auto context = Parameter<Context>(Descriptor::kContext);

  PerformStackCheck(context);

  Label call_exception(this, Label::kDeferred),
      target_not_callable(this, Label::kDeferred);

  // 1. Let target be F.[[WrappedTargetFunction]].
  TNode<JSReceiver> target = CAST(LoadObjectField(
      wrapped_function, JSWrappedFunction::kWrappedTargetFunctionOffset));
  // 2. Assert: IsCallable(target) is true.
  CSA_DCHECK(this, IsCallable(target));

  // 4. Let callerRealm be ? GetFunctionRealm(F).
  TNode<Context> caller_context = LoadObjectField<Context>(
      wrapped_function, JSWrappedFunction::kContextOffset);
  // 3. Let targetRealm be ? GetFunctionRealm(target).
  TNode<Context> target_context =
      GetFunctionRealm(caller_context, target, &target_not_callable);
  // 5. NOTE: Any exception objects produced after this point are associated
  // with callerRealm.

  CodeStubArguments args(this, argc_ptr);
  TNode<Object> receiver = args.GetReceiver();

  // 6. Let wrappedArgs be a new empty List.
  TNode<FixedArray> wrapped_args =
      CAST(AllocateFixedArray(ElementsKind::PACKED_ELEMENTS, argc_ptr));
  // Fill the fixed array so that heap verifier doesn't complain about it.
  FillFixedArrayWithValue(ElementsKind::PACKED_ELEMENTS, wrapped_args,
                          IntPtrConstant(0), argc_ptr,
                          RootIndex::kUndefinedValue);

  // 8. Let wrappedThisArgument to ? GetWrappedValue(targetRealm, thisArgument).
  // Create wrapped value in the target realm.
  TNode<Object> wrapped_receiver =
      CallBuiltin(Builtin::kShadowRealmGetWrappedValue, caller_context,
                  target_context, caller_context, receiver);
  StoreFixedArrayElement(wrapped_args, 0, wrapped_receiver);
  // 7. For each element arg of argumentsList, do
  BuildFastLoop<IntPtrT>(
      IntPtrConstant(0), args.GetLengthWithoutReceiver(),
      [&](TNode<IntPtrT> index) {
        // 7a. Let wrappedValue be ? GetWrappedValue(targetRealm, arg).
        // Create wrapped value in the target realm.
        TNode<Object> wrapped_value =
            CallBuiltin(Builtin::kShadowRealmGetWrappedValue, caller_context,
                        target_context, caller_context, args.AtIndex(index));
        // 7b. Append wrappedValue to wrappedArgs.
        StoreFixedArrayElement(
            wrapped_args, IntPtrAdd(index, IntPtrConstant(1)), wrapped_value);
      },
      1, LoopUnrollingMode::kNo, IndexAdvanceMode::kPost);

  TVARIABLE(Object, var_exception);
  TNode<Object> result;
  {
    compiler::ScopedExceptionHandler handler(this, &call_exception,
                                             &var_exception);
    TNode<Int32T> args_count = Int32Constant(0);  // args already on the stack

    // 9. Let result be the Completion Record of Call(target,
    // wrappedThisArgument, wrappedArgs).
    result = CallBuiltin(Builtin::kCallVarargs, target_context, target,
                         args_count, argc, wrapped_args);
  }

  // 10. If result.[[Type]] is normal or result.[[Type]] is return, then
  // 10a. Return ? GetWrappedValue(callerRealm, result.[[Value]]).
  TNode<Object> wrapped_result =
      CallBuiltin(Builtin::kShadowRealmGetWrappedValue, caller_context,
                  caller_context, target_context, result);
  args.PopAndReturn(wrapped_result);

  // 11. Else,
  BIND(&call_exception);
  // 11a. Throw a TypeError exception.
  ShadowRealmThrow(context, MessageTemplate::kCallWrappedFunctionThrew,
                   var_exception.value());

  BIND(&target_not_callable);
  // A wrapped value should not be non-callable.
  Unreachable();
}

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealm.prototype.importvalue
TF_BUILTIN(ShadowRealmPrototypeImportValue, ShadowRealmBuiltinsAssembler) {
  const char* const kMethodName = "ShadowRealm.prototype.importValue";
  TNode<Context> context = Parameter<Context>(Descriptor::kContext);
  // 1. Let O be this value.
  TNode<Object> O = Parameter<Object>(Descriptor::kReceiver);
  // 2. Perform ? ValidateShadowRealmObject(O).
  ThrowIfNotInstanceType(context, O, JS_SHADOW_REALM_TYPE, kMethodName);

  // 3. Let specifierString be ? ToString(specifier).
  TNode<Object> specifier = Parameter<Object>(Descriptor::kSpecifier);
  TNode<String> specifier_string = ToString_Inline(context, specifier);
  // 4. Let exportNameString be ? ToString(exportName).
  TNode<Object> export_name = Parameter<Object>(Descriptor::kExportName);
  TNode<String> export_name_string = ToString_Inline(context, export_name);
  // 5. Let callerRealm be the current Realm Record.
  TNode<NativeContext> caller_context = LoadNativeContext(context);
  // 6. Let evalRealm be O.[[ShadowRealm]].
  // 7. Let evalContext be O.[[ExecutionContext]].
  TNode<NativeContext> eval_context =
      CAST(LoadObjectField(CAST(O), JSShadowRealm::kNativeContextOffset));
  // 8. Return ? ShadowRealmImportValue(specifierString, exportNameString,
  // callerRealm, evalRealm, evalContext).
  TNode<Object> result = ImportValue(caller_context, eval_context,
                                     specifier_string, export_name_string);
  Return(result);
}

// https://tc39.es/proposal-shadowrealm/#sec-shadowrealmimportvalue
TNode<Object> ShadowRealmBuiltinsAssembler::ImportValue(
    TNode<NativeContext> caller_context, TNode<NativeContext> eval_context,
    TNode<String> specifier, TNode<String> export_name) {
  // 1. Assert: evalContext is an execution context associated to a ShadowRealm
  // instance's [[ExecutionContext]].
  // 2. Let innerCapability be ! NewPromiseCapability(%Promise%).
  // 3. Let runningContext be the running execution context.
  // 4. If runningContext is not already suspended, suspend runningContext.
  // 5. Push evalContext onto the execution context stack; evalContext is now
  // the running execution context.
  // 6. Perform ! HostImportModuleDynamically(null, specifierString,
  // innerCapability).
  // 7. Suspend evalContext and remove it from the execution context stack.
  // 8. Resume the context that is now on the top of the execution context stack
  // as the running execution context.
  TNode<Object> inner_capability =
      CallRuntime(Runtime::kShadowRealmImportValue, eval_context, specifier);

  // 9. Let steps be the steps of an ExportGetter function as described below.
  // 10. Let onFulfilled be ! CreateBuiltinFunction(steps, 1, "", «
  // [[ExportNameString]] », callerRealm).
  // 11. Set onFulfilled.[[ExportNameString]] to exportNameString.
  TNode<JSFunction> on_fulfilled = AllocateImportValueFulfilledFunction(
      caller_context, eval_context, specifier, export_name);

  TNode<JSFunction> on_rejected = CAST(LoadContextElement(
      caller_context, Context::SHADOW_REALM_IMPORT_VALUE_REJECTED_INDEX));
  // 12. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  TNode<JSPromise> promise = NewJSPromise(caller_context);
  // 13. Return ! PerformPromiseThen(innerCapability.[[Promise]], onFulfilled,
  // callerRealm.[[Intrinsics]].[[%ThrowTypeError%]], promiseCapability).
  return CallBuiltin(Builtin::kPerformPromiseThen, caller_context,
                     inner_capability, on_fulfilled, on_rejected, promise);
}

// ExportGetter of
// https://tc39.es/proposal-shadowrealm/#sec-shadowrealmimportvalue
TF_BUILTIN(ShadowRealmImportValueFulfilled, ShadowRealmBuiltinsAssembler) {
  // An ExportGetter function is an anonymous built-in function with a
  // [[ExportNameString]] internal slot. When an ExportGetter function is called
  // with argument exports, it performs the following steps:
  // 8. Let realm be f.[[Realm]].
  TNode<Context> context = Parameter<Context>(Descriptor::kContext);
  TNode<Context> eval_context = CAST(LoadContextElement(
      context, ImportValueFulfilledFunctionContextSlot::kEvalContextSlot));

  Label get_export_exception(this, Label::kDeferred);

  // 2. Let f be the active function object.
  // 3. Let string be f.[[ExportNameString]].
  // 4. Assert: Type(string) is String.
  TNode<String> export_name_string = CAST(LoadContextElement(
      context, ImportValueFulfilledFunctionContextSlot::kExportNameSlot));

  // 1. Assert: exports is a module namespace exotic object.
  TNode<JSModuleNamespace> exports =
      Parameter<JSModuleNamespace>(Descriptor::kExports);

  // 5. Let hasOwn be ? HasOwnProperty(exports, string).
  // 6. If hasOwn is false, throw a TypeError exception.
  // 7. Let value be ? Get(exports, string).

  // The only exceptions thrown by Runtime::kGetModuleNamespaceExport are
  // either the export is not found or the module is not initialized.
  TVARIABLE(Object, var_exception);
  TNode<Object> value;
  {
    compiler::ScopedExceptionHandler handler(this, &get_export_exception,
                                             &var_exception);
    value = CallRuntime(Runtime::kGetModuleNamespaceExport, eval_context,
                        exports, export_name_string);
  }

  // 9. Return ? GetWrappedValue(realm, value).
  TNode<NativeContext> caller_context = LoadNativeContext(context);
  TNode<Object> wrapped_result =
      CallBuiltin(Builtin::kShadowRealmGetWrappedValue, caller_context,
                  caller_context, eval_context, value);
  Return(wrapped_result);

  BIND(&get_export_exception);
  {
    TNode<String> specifier_string = CAST(LoadContextElement(
        context, ImportValueFulfilledFunctionContextSlot::kSpecifierSlot));
    ThrowTypeError(context, MessageTemplate::kUnresolvableExport,
                   specifier_string, export_name_string);
  }
}

TF_BUILTIN(ShadowRealmImportValueRejected, ShadowRealmBuiltinsAssembler) {
  TNode<Context> context = Parameter<Context>(Descriptor::kContext);
  TNode<Object> exception = Parameter<Object>(Descriptor::kException);
  ShadowRealmThrow(context, MessageTemplate::kImportShadowRealmRejected,
                   exception);
}

}  // namespace internal
}  // namespace v8
