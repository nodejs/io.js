// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/api/api-inl.h"
#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/codegen/code-factory.h"
#include "src/codegen/compiler.h"
#include "src/logging/counters.h"
#include "src/numbers/conversions.h"
#include "src/objects/api-callbacks.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/strings/string-builder-inl.h"

namespace v8 {
namespace internal {

namespace {

// ES6 section 19.2.1.1.1 CreateDynamicFunction
MaybeHandle<Object> CreateDynamicFunction(Isolate* isolate,
                                          BuiltinArguments args,
                                          const char* token) {
  // Compute number of arguments, ignoring the receiver.
  DCHECK_LE(1, args.length());
  int const argc = args.length() - 1;

  Handle<JSFunction> target = args.target();
  Handle<JSObject> target_global_proxy(target->global_proxy(), isolate);

  if (!Builtins::AllowDynamicFunction(isolate, target, target_global_proxy)) {
    isolate->CountUsage(v8::Isolate::kFunctionConstructorReturnedUndefined);
    // TODO(verwaest): We would like to throw using the calling context instead
    // of the entered context but we don't currently have access to that.
    HandleScopeImplementer* impl = isolate->handle_scope_implementer();
    SaveAndSwitchContext save(isolate,
                              impl->LastEnteredContext()->native_context());
    THROW_NEW_ERROR(isolate, NewTypeError(MessageTemplate::kNoAccess), Object);
  }

  // Build the source string.
  Handle<String> source;
  int parameters_end_pos = kNoSourcePosition;
  {
    IncrementalStringBuilder builder(isolate);
    builder.AppendCharacter('(');
    builder.AppendCString(token);
    builder.AppendCStringLiteral(" anonymous(");
    if (argc > 1) {
      for (int i = 1; i < argc; ++i) {
        if (i > 1) builder.AppendCharacter(',');
        Handle<String> param;
        ASSIGN_RETURN_ON_EXCEPTION(
            isolate, param, Object::ToString(isolate, args.at(i)), Object);
        param = String::Flatten(isolate, param);
        builder.AppendString(param);
      }
    }
    builder.AppendCharacter('\n');
    parameters_end_pos = builder.Length();
    builder.AppendCStringLiteral(") {\n");
    if (argc > 0) {
      Handle<String> body;
      ASSIGN_RETURN_ON_EXCEPTION(
          isolate, body, Object::ToString(isolate, args.at(argc)), Object);
      builder.AppendString(body);
    }
    builder.AppendCStringLiteral("\n})");
    ASSIGN_RETURN_ON_EXCEPTION(isolate, source, builder.Finish(), Object);
  }

  bool is_code_like = true;
  for (int i = 0; i < argc; ++i) {
    if (!Object::IsCodeLike(*args.at(i + 1), isolate)) {
      is_code_like = false;
      break;
    }
  }

  // Compile the string in the constructor and not a helper so that errors to
  // come from here.
  Handle<JSFunction> function;
  {
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, function,
        Compiler::GetFunctionFromString(
            handle(target->native_context(), isolate), source,
            ONLY_SINGLE_FUNCTION_LITERAL, parameters_end_pos, is_code_like),
        Object);
    Handle<Object> result;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, result,
        Execution::Call(isolate, function, target_global_proxy, 0, nullptr),
        Object);
    function = Handle<JSFunction>::cast(result);
    function->shared()->set_name_should_print_as_anonymous(true);
  }

  // If new.target is equal to target then the function created
  // is already correctly setup and nothing else should be done
  // here. But if new.target is not equal to target then we are
  // have a Function builtin subclassing case and therefore the
  // function has wrong initial map. To fix that we create a new
  // function object with correct initial map.
  Handle<Object> unchecked_new_target = args.new_target();
  if (!IsUndefined(*unchecked_new_target, isolate) &&
      !unchecked_new_target.is_identical_to(target)) {
    Handle<JSReceiver> new_target =
        Handle<JSReceiver>::cast(unchecked_new_target);
    Handle<Map> initial_map;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, initial_map,
        JSFunction::GetDerivedMap(isolate, target, new_target), Object);

    Handle<SharedFunctionInfo> shared_info(function->shared(), isolate);
    Handle<Map> map = Map::AsLanguageMode(isolate, initial_map, shared_info);

    Handle<Context> context(function->context(), isolate);
    function = Factory::JSFunctionBuilder{isolate, shared_info, context}
                   .set_map(map)
                   .set_allocation_type(AllocationType::kYoung)
                   .Build();
  }
  return function;
}

}  // namespace

// ES6 section 19.2.1.1 Function ( p1, p2, ... , pn, body )
BUILTIN(FunctionConstructor) {
  HandleScope scope(isolate);
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result, CreateDynamicFunction(isolate, args, "function"));
  return *result;
}

// ES6 section 25.2.1.1 GeneratorFunction (p1, p2, ... , pn, body)
BUILTIN(GeneratorFunctionConstructor) {
  HandleScope scope(isolate);
  RETURN_RESULT_OR_FAILURE(isolate,
                           CreateDynamicFunction(isolate, args, "function*"));
}

BUILTIN(AsyncFunctionConstructor) {
  HandleScope scope(isolate);
  Handle<Object> maybe_func;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, maybe_func,
      CreateDynamicFunction(isolate, args, "async function"));
  if (!IsJSFunction(*maybe_func)) return *maybe_func;

  // Do not lazily compute eval position for AsyncFunction, as they may not be
  // determined after the function is resumed.
  Handle<JSFunction> func = Handle<JSFunction>::cast(maybe_func);
  Handle<Script> script =
      handle(Script::cast(func->shared()->script()), isolate);
  int position = Script::GetEvalPosition(isolate, script);
  USE(position);

  return *func;
}

BUILTIN(AsyncGeneratorFunctionConstructor) {
  HandleScope scope(isolate);
  Handle<Object> maybe_func;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, maybe_func,
      CreateDynamicFunction(isolate, args, "async function*"));
  if (!IsJSFunction(*maybe_func)) return *maybe_func;

  // Do not lazily compute eval position for AsyncFunction, as they may not be
  // determined after the function is resumed.
  Handle<JSFunction> func = Handle<JSFunction>::cast(maybe_func);
  Handle<Script> script =
      handle(Script::cast(func->shared()->script()), isolate);
  int position = Script::GetEvalPosition(isolate, script);
  USE(position);

  return *func;
}

namespace {

enum class ProtoSource {
  kNormalFunction,
  kUseTargetPrototype,
};

Tagged<Object> DoFunctionBind(Isolate* isolate, BuiltinArguments args,
                              ProtoSource proto_source) {
  HandleScope scope(isolate);
  DCHECK_LE(1, args.length());
  if (!IsCallable(*args.receiver())) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kFunctionBind));
  }

  // Allocate the bound function with the given {this_arg} and {args}.
  Handle<JSReceiver> target = args.at<JSReceiver>(0);
  Handle<Object> this_arg = isolate->factory()->undefined_value();
  base::ScopedVector<Handle<Object>> argv(std::max(0, args.length() - 2));
  if (args.length() > 1) {
    this_arg = args.at(1);
    for (int i = 2; i < args.length(); ++i) {
      argv[i - 2] = args.at(i);
    }
  }

  Handle<HeapObject> proto;
  if (proto_source == ProtoSource::kUseTargetPrototype) {
    // Determine the prototype of the {target_function}.
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
        isolate, proto, JSReceiver::GetPrototype(isolate, target));
  } else if (proto_source == ProtoSource::kNormalFunction) {
    Handle<NativeContext> native_context(
        isolate->global_object()->native_context(), isolate);
    auto function_proto = native_context->function_function()->prototype();
    proto = handle(HeapObject::cast(function_proto), isolate);
  } else {
    UNREACHABLE();
  }

  Handle<JSBoundFunction> function;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, function,
      isolate->factory()->NewJSBoundFunction(target, this_arg, argv, proto));
  Maybe<bool> result =
      JSFunctionOrBoundFunctionOrWrappedFunction::CopyNameAndLength(
          isolate, function, target, isolate->factory()->bound__string(),
          argv.length());
  if (result.IsNothing()) {
    DCHECK(isolate->has_exception());
    return ReadOnlyRoots(isolate).exception();
  }
  return *function;
}

}  // namespace

// ES6 section 19.2.3.2 Function.prototype.bind ( thisArg, ...args )
BUILTIN(FunctionPrototypeBind) {
  return DoFunctionBind(isolate, args, ProtoSource::kUseTargetPrototype);
}

#if V8_ENABLE_WEBASSEMBLY
BUILTIN(WebAssemblyFunctionPrototypeBind) {
  return DoFunctionBind(isolate, args, ProtoSource::kNormalFunction);
}
#endif  // V8_ENABLE_WEBASSEMBLY

// ES6 section 19.2.3.5 Function.prototype.toString ( )
BUILTIN(FunctionPrototypeToString) {
  HandleScope scope(isolate);
  Handle<Object> receiver = args.receiver();
  if (IsJSBoundFunction(*receiver)) {
    return *JSBoundFunction::ToString(Handle<JSBoundFunction>::cast(receiver));
  }
  if (IsJSFunction(*receiver)) {
    return *JSFunction::ToString(Handle<JSFunction>::cast(receiver));
  }
  // With the revised toString behavior, all callable objects are valid
  // receivers for this method.
  if (IsJSReceiver(*receiver) &&
      JSReceiver::cast(*receiver)->map()->is_callable()) {
    return ReadOnlyRoots(isolate).function_native_code_string();
  }
  THROW_NEW_ERROR_RETURN_FAILURE(
      isolate, NewTypeError(MessageTemplate::kNotGeneric,
                            isolate->factory()->NewStringFromAsciiChecked(
                                "Function.prototype.toString"),
                            isolate->factory()->Function_string()));
}

}  // namespace internal
}  // namespace v8
