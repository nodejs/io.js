// Copyright 2014 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/builtins/accessors.h"
#include "src/common/message-template.h"
#include "src/deoptimizer/deoptimizer.h"
#include "src/execution/arguments-inl.h"
#include "src/execution/frames-inl.h"
#include "src/execution/isolate-inl.h"
#include "src/execution/isolate.h"
#include "src/handles/handles.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/interpreter/bytecode-flags-and-tokens.h"
#include "src/objects/arguments-inl.h"
#include "src/objects/fixed-array.h"
#include "src/objects/heap-object.h"
#include "src/objects/js-disposable-stack-inl.h"
#include "src/objects/js-disposable-stack.h"
#include "src/objects/objects.h"
#include "src/objects/oddball.h"
#include "src/objects/smi.h"
#include "src/objects/tagged.h"
#include "src/runtime/runtime-utils.h"

namespace v8 {
namespace internal {

RUNTIME_FUNCTION(Runtime_ThrowConstAssignError) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(MessageTemplate::kConstAssign));
}

RUNTIME_FUNCTION(Runtime_ThrowUsingAssignError) {
  HandleScope scope(isolate);
  THROW_NEW_ERROR_RETURN_FAILURE(isolate,
                                 NewTypeError(MessageTemplate::kUsingAssign));
}

namespace {

enum class RedeclarationType { kSyntaxError = 0, kTypeError = 1 };

Tagged<Object> ThrowRedeclarationError(Isolate* isolate, Handle<String> name,
                                       RedeclarationType redeclaration_type) {
  HandleScope scope(isolate);
  if (redeclaration_type == RedeclarationType::kSyntaxError) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewSyntaxError(MessageTemplate::kVarRedeclaration, name));
  } else {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kVarRedeclaration, name));
  }
}

// May throw a RedeclarationError.
Tagged<Object> DeclareGlobal(Isolate* isolate, Handle<JSGlobalObject> global,
                             Handle<String> name, Handle<Object> value,
                             PropertyAttributes attr, bool is_var,
                             RedeclarationType redeclaration_type) {
  DirectHandle<ScriptContextTable> script_contexts(
      global->native_context()->script_context_table(), isolate);
  VariableLookupResult lookup;
  if (script_contexts->Lookup(name, &lookup) &&
      IsLexicalVariableMode(lookup.mode)) {
    // ES#sec-globaldeclarationinstantiation 6.a:
    // If envRec.HasLexicalDeclaration(name) is true, throw a SyntaxError
    // exception.
    return ThrowRedeclarationError(isolate, name,
                                   RedeclarationType::kSyntaxError);
  }

  // Do the lookup own properties only, see ES5 erratum.
  LookupIterator::Configuration lookup_config(
      LookupIterator::Configuration::OWN_SKIP_INTERCEPTOR);
  if (!is_var) {
    // For function declarations, use the interceptor on the declaration. For
    // non-functions, use it only on initialization.
    lookup_config = LookupIterator::Configuration::OWN;
  }
  LookupIterator it(isolate, global, name, global, lookup_config);
  Maybe<PropertyAttributes> maybe = JSReceiver::GetPropertyAttributes(&it);
  if (maybe.IsNothing()) return ReadOnlyRoots(isolate).exception();

  if (it.IsFound()) {
    PropertyAttributes old_attributes = maybe.FromJust();
    // The name was declared before; check for conflicting re-declarations.

    // Skip var re-declarations.
    if (is_var) return ReadOnlyRoots(isolate).undefined_value();

    if ((old_attributes & DONT_DELETE) != 0) {
      // Only allow reconfiguring globals to functions in user code (no
      // natives, which are marked as read-only).
      DCHECK_EQ(attr & READ_ONLY, 0);

      // Check whether we can reconfigure the existing property into a
      // function.
      if (old_attributes & READ_ONLY || old_attributes & DONT_ENUM ||
          (it.state() == LookupIterator::ACCESSOR)) {
        // ECMA-262 section 15.1.11 GlobalDeclarationInstantiation 5.d:
        // If hasRestrictedGlobal is true, throw a SyntaxError exception.
        // ECMA-262 section 18.2.1.3 EvalDeclarationInstantiation 8.a.iv.1.b:
        // If fnDefinable is false, throw a TypeError exception.
        return ThrowRedeclarationError(isolate, name, redeclaration_type);
      }
      // If the existing property is not configurable, keep its attributes. Do
      attr = old_attributes;
    }

    // If the current state is ACCESSOR, this could mean it's an AccessorInfo
    // type property. We are not allowed to call into such setters during global
    // function declaration since this would break e.g., onload. Meaning
    // 'function onload() {}' would invalidly register that function as the
    // onload callback. To avoid this situation, we first delete the property
    // before readding it as a regular data property below.
    if (it.state() == LookupIterator::ACCESSOR) it.Delete();
  }

  if (!is_var) it.Restart();

  // Define or redefine own property.
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, JSObject::DefineOwnPropertyIgnoreAttributes(&it, value, attr));

  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace

RUNTIME_FUNCTION(Runtime_DeclareModuleExports) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  DirectHandle<FixedArray> declarations = args.at<FixedArray>(0);
  DirectHandle<JSFunction> closure = args.at<JSFunction>(1);

  DirectHandle<ClosureFeedbackCellArray> closure_feedback_cell_array(
      closure->has_feedback_vector()
          ? closure->feedback_vector()->closure_feedback_cell_array()
          : closure->closure_feedback_cell_array(),
      isolate);

  Handle<Context> context(isolate->context(), isolate);
  DCHECK(context->IsModuleContext());
  DirectHandle<FixedArray> exports(
      Cast<SourceTextModule>(context->extension())->regular_exports(), isolate);

  int length = declarations->length();
  FOR_WITH_HANDLE_SCOPE(isolate, int, i = 0, i, i < length, i++, {
    Tagged<Object> decl = declarations->get(i);
    int index;
    Tagged<Object> value;
    if (IsSmi(decl)) {
      index = Smi::ToInt(decl);
      value = ReadOnlyRoots(isolate).the_hole_value();
    } else {
      Handle<SharedFunctionInfo> sfi(
          Cast<SharedFunctionInfo>(declarations->get(i)), isolate);
      int feedback_index = Smi::ToInt(declarations->get(++i));
      index = Smi::ToInt(declarations->get(++i));
      Handle<FeedbackCell> feedback_cell(
          closure_feedback_cell_array->get(feedback_index), isolate);
      value = *Factory::JSFunctionBuilder(isolate, sfi, context)
                   .set_feedback_cell(feedback_cell)
                   .Build();
    }

    Cast<Cell>(exports->get(index - 1))->set_value(value);
  });

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_DeclareGlobals) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  DirectHandle<FixedArray> declarations = args.at<FixedArray>(0);
  DirectHandle<JSFunction> closure = args.at<JSFunction>(1);

  Handle<JSGlobalObject> global(isolate->global_object());
  Handle<Context> context(isolate->context(), isolate);

  DirectHandle<ClosureFeedbackCellArray> closure_feedback_cell_array(
      closure->has_feedback_vector()
          ? closure->feedback_vector()->closure_feedback_cell_array()
          : closure->closure_feedback_cell_array(),
      isolate);

  // Traverse the name/value pairs and set the properties.
  int length = declarations->length();
  FOR_WITH_HANDLE_SCOPE(isolate, int, i = 0, i, i < length, i++, {
    Handle<Object> decl(declarations->get(i), isolate);
    Handle<String> name;
    Handle<Object> value;
    bool is_var = IsString(*decl);

    if (is_var) {
      name = Cast<String>(decl);
      value = isolate->factory()->undefined_value();
    } else {
      Handle<SharedFunctionInfo> sfi = Cast<SharedFunctionInfo>(decl);
      name = handle(sfi->Name(), isolate);
      int index = Smi::ToInt(declarations->get(++i));
      Handle<FeedbackCell> feedback_cell(
          closure_feedback_cell_array->get(index), isolate);
      value = Factory::JSFunctionBuilder(isolate, sfi, context)
                  .set_feedback_cell(feedback_cell)
                  .Build();
    }

    // Compute the property attributes. According to ECMA-262,
    // the property must be non-configurable except in eval.
    Tagged<Script> script = Cast<Script>(closure->shared()->script());
    PropertyAttributes attr =
        script->compilation_type() == Script::CompilationType::kEval
            ? NONE
            : DONT_DELETE;

    // ES#sec-globaldeclarationinstantiation 5.d:
    // If hasRestrictedGlobal is true, throw a SyntaxError exception.
    Tagged<Object> result =
        DeclareGlobal(isolate, global, name, value, attr, is_var,
                      RedeclarationType::kSyntaxError);
    if (IsException(result)) return result;
  });

  return ReadOnlyRoots(isolate).undefined_value();
}

RUNTIME_FUNCTION(Runtime_InitializeDisposableStack) {
  HandleScope scope(isolate);
  DCHECK_EQ(0, args.length());

  DirectHandle<JSDisposableStackBase> disposable_stack =
      isolate->factory()->NewJSDisposableStackBase();
  JSDisposableStackBase::InitializeJSDisposableStackBase(isolate,
                                                         disposable_stack);
  return *disposable_stack;
}

Tagged<Object> AddToDisposableStack(Isolate* isolate,
                                    DirectHandle<JSDisposableStackBase> stack,
                                    Handle<Object> value,
                                    DisposeMethodCallType type,
                                    DisposeMethodHint hint) {
  // a. If V is either null or undefined and hint is sync-dispose, return
  // unused.
  if (IsNullOrUndefined(*value)) {
    return *value;
  }

  Handle<Object> method;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, method,
      JSDisposableStackBase::CheckValueAndGetDisposeMethod(isolate, value,
                                                           hint));

  // Return the DisposableResource Record { [[ResourceValue]]: V, [[Hint]]:
  // hint, [[DisposeMethod]]: method }.
  JSDisposableStackBase::Add(isolate, stack, value, method, type, hint);
  return *value;
}

RUNTIME_FUNCTION(Runtime_AddDisposableValue) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  DirectHandle<JSDisposableStackBase> stack = args.at<JSDisposableStackBase>(0);
  Handle<Object> value = args.at<Object>(1);

  // Return the DisposableResource Record { [[ResourceValue]]: V, [[Hint]]:
  // hint, [[DisposeMethod]]: method }.
  return AddToDisposableStack(isolate, stack, value,
                              DisposeMethodCallType::kValueIsReceiver,
                              DisposeMethodHint::kSyncDispose);
}

RUNTIME_FUNCTION(Runtime_AddAsyncDisposableValue) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());

  DirectHandle<JSDisposableStackBase> stack = args.at<JSDisposableStackBase>(0);
  Handle<Object> value = args.at<Object>(1);

  // Return the DisposableResource Record { [[ResourceValue]]: V, [[Hint]]:
  // hint, [[DisposeMethod]]: method }.
  return AddToDisposableStack(isolate, stack, value,
                              DisposeMethodCallType::kValueIsReceiver,
                              DisposeMethodHint::kAsyncDispose);
}

RUNTIME_FUNCTION(Runtime_DisposeDisposableStack) {
  HandleScope scope(isolate);
  DCHECK_EQ(4, args.length());

  DirectHandle<JSDisposableStackBase> disposable_stack =
      args.at<JSDisposableStackBase>(0);
  DirectHandle<Smi> continuation_token = args.at<Smi>(1);
  Handle<Object> continuation_error = args.at<Object>(2);
  DirectHandle<Smi> has_await_using = args.at<Smi>(3);

  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result,
      JSDisposableStackBase::DisposeResources(
          isolate, disposable_stack,
          (*continuation_token !=
           Smi::FromInt(static_cast<int>(
               interpreter::TryFinallyContinuationToken::kRethrowToken)))
              ? MaybeHandle<Object>()
              : continuation_error,
          static_cast<DisposableStackResourcesType>(
              Smi::ToInt(*has_await_using))));
  return *result;
}

namespace {

Tagged<Object> DeclareEvalHelper(Isolate* isolate, Handle<String> name,
                                 Handle<Object> value) {
  // Declarations are always made in a function, native, eval, or script
  // context, or a declaration block scope. Since this is called from eval, the
  // context passed is the context of the caller, which may be some nested
  // context and not the declaration context.
  Handle<Context> context(isolate->context()->declaration_context(), isolate);

  // For debug-evaluate we always use sloppy eval, in which case context could
  // also be a module context. As module contexts re-use the extension slot
  // we need to check for this.
  const bool is_debug_evaluate_in_module =
      isolate->context()->IsDebugEvaluateContext() &&
      context->IsModuleContext();

  DCHECK(context->IsFunctionContext() || IsNativeContext(*context) ||
         context->IsScriptContext() || context->IsEvalContext() ||
         (context->IsBlockContext() &&
          context->scope_info()->is_declaration_scope()) ||
         is_debug_evaluate_in_module);

  bool is_var = IsUndefined(*value, isolate);
  DCHECK_IMPLIES(!is_var, IsJSFunction(*value));

  int index;
  PropertyAttributes attributes;
  InitializationFlag init_flag;
  VariableMode mode;

  Handle<Object> holder =
      Context::Lookup(context, name, DONT_FOLLOW_CHAINS, &index, &attributes,
                      &init_flag, &mode);
  DCHECK(holder.is_null() || !IsSourceTextModule(*holder));
  DCHECK(!isolate->has_exception());

  Handle<JSObject> object;

  if (attributes != ABSENT && IsJSGlobalObject(*holder)) {
    // ES#sec-evaldeclarationinstantiation 8.a.iv.1.b:
    // If fnDefinable is false, throw a TypeError exception.
    return DeclareGlobal(isolate, Cast<JSGlobalObject>(holder), name, value,
                         NONE, is_var, RedeclarationType::kTypeError);
  }
  if (context->has_extension() && IsJSGlobalObject(context->extension())) {
    Handle<JSGlobalObject> global(Cast<JSGlobalObject>(context->extension()),
                                  isolate);
    return DeclareGlobal(isolate, global, name, value, NONE, is_var,
                         RedeclarationType::kTypeError);
  } else if (context->IsScriptContext()) {
    DCHECK(IsJSGlobalObject(context->global_object()));
    Handle<JSGlobalObject> global(
        Cast<JSGlobalObject>(context->global_object()), isolate);
    return DeclareGlobal(isolate, global, name, value, NONE, is_var,
                         RedeclarationType::kTypeError);
  }

  if (attributes != ABSENT) {
    DCHECK_EQ(NONE, attributes);

    // Skip var re-declarations.
    if (is_var) return ReadOnlyRoots(isolate).undefined_value();

    if (index != Context::kNotFound) {
      DCHECK(holder.is_identical_to(context));
      context->set(index, *value);
      return ReadOnlyRoots(isolate).undefined_value();
    }

    object = Cast<JSObject>(holder);

  } else if (context->has_extension() && !is_debug_evaluate_in_module) {
    object = handle(context->extension_object(), isolate);
    DCHECK(IsJSContextExtensionObject(*object));
  } else if (context->scope_info()->HasContextExtensionSlot() &&
             !is_debug_evaluate_in_module) {
    // Sloppy varblock and function contexts might not have an extension object
    // yet. Sloppy eval will never have an extension object, as vars are hoisted
    // out, and lets are known statically.
    DCHECK((context->IsBlockContext() &&
            context->scope_info()->is_declaration_scope()) ||
           context->IsFunctionContext());
    object =
        isolate->factory()->NewJSObject(isolate->context_extension_function());

    context->set_extension(*object);
  } else {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate,
        NewEvalError(MessageTemplate::kVarNotAllowedInEvalScope, name));
  }

  RETURN_FAILURE_ON_EXCEPTION(isolate, JSObject::SetOwnPropertyIgnoreAttributes(
                                           object, name, value, NONE));

  return ReadOnlyRoots(isolate).undefined_value();
}

}  // namespace

RUNTIME_FUNCTION(Runtime_DeclareEvalFunction) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  Handle<Object> value = args.at(1);
  return DeclareEvalHelper(isolate, name, value);
}

RUNTIME_FUNCTION(Runtime_DeclareEvalVar) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<String> name = args.at<String>(0);
  return DeclareEvalHelper(isolate, name,
                           isolate->factory()->undefined_value());
}

namespace {

// Find the arguments of the JavaScript function invocation that called
// into C++ code. Collect these in a newly allocated array of handles.
std::unique_ptr<Handle<Object>[]> GetCallerArguments(Isolate* isolate,
                                                     int* total_argc) {
  // Find frame containing arguments passed to the caller.
  JavaScriptStackFrameIterator it(isolate);
  JavaScriptFrame* frame = it.frame();
  std::vector<Tagged<SharedFunctionInfo>> functions;
  frame->GetFunctions(&functions);
  if (functions.size() > 1) {
    int inlined_jsframe_index = static_cast<int>(functions.size()) - 1;
    TranslatedState translated_values(frame);
    translated_values.Prepare(frame->fp());

    int argument_count = 0;
    TranslatedFrame* translated_frame =
        translated_values.GetArgumentsInfoFromJSFrameIndex(
            inlined_jsframe_index, &argument_count);
    TranslatedFrame::iterator iter = translated_frame->begin();

    // Skip the function.
    iter++;

    // Skip the receiver.
    iter++;
    argument_count--;

    *total_argc = argument_count;
    std::unique_ptr<Handle<Object>[]> param_data(
        NewArray<Handle<Object>>(*total_argc));
    bool should_deoptimize = false;
    for (int i = 0; i < argument_count; i++) {
      // If we materialize any object, we should deoptimize the frame because we
      // might alias an object that was eliminated by escape analysis.
      should_deoptimize = should_deoptimize || iter->IsMaterializedObject();
      Handle<Object> value = iter->GetValue();
      param_data[i] = value;
      iter++;
    }

    if (should_deoptimize) {
      translated_values.StoreMaterializedValuesAndDeopt(frame);
    }

    return param_data;
  } else {
    int args_count = frame->GetActualArgumentCount();
    *total_argc = args_count;
    std::unique_ptr<Handle<Object>[]> param_data(
        NewArray<Handle<Object>>(*total_argc));
    for (int i = 0; i < args_count; i++) {
      Handle<Object> val = Handle<Object>(frame->GetParameter(i), isolate);
      param_data[i] = val;
    }
    return param_data;
  }
}

template <typename T>
Handle<JSObject> NewSloppyArguments(Isolate* isolate, Handle<JSFunction> callee,
                                    T parameters, int argument_count) {
  CHECK(!IsDerivedConstructor(callee->shared()->kind()));
  DCHECK(callee->shared()->has_simple_parameters());
  Handle<JSObject> result =
      isolate->factory()->NewArgumentsObject(callee, argument_count);

  // Allocate the elements if needed.
  int parameter_count =
      callee->shared()->internal_formal_parameter_count_without_receiver();
  if (argument_count > 0) {
    if (parameter_count > 0) {
      int mapped_count = std::min(argument_count, parameter_count);

      // Store the context and the arguments array at the beginning of the
      // parameter map.
      DirectHandle<Context> context(isolate->context(), isolate);
      DirectHandle<FixedArray> arguments = isolate->factory()->NewFixedArray(
          argument_count, AllocationType::kYoung);

      DirectHandle<SloppyArgumentsElements> parameter_map =
          isolate->factory()->NewSloppyArgumentsElements(
              mapped_count, context, arguments, AllocationType::kYoung);

      result->set_map(isolate->native_context()->fast_aliased_arguments_map());
      result->set_elements(*parameter_map);

      // Loop over the actual parameters backwards.
      int index = argument_count - 1;
      while (index >= mapped_count) {
        // These go directly in the arguments array and have no
        // corresponding slot in the parameter map.
        arguments->set(index, parameters[index]);
        --index;
      }

      DirectHandle<ScopeInfo> scope_info(callee->shared()->scope_info(),
                                         isolate);

      // First mark all mappable slots as unmapped and copy the values into the
      // arguments object.
      for (int i = 0; i < mapped_count; i++) {
        arguments->set(i, parameters[i]);
        parameter_map->set_mapped_entries(
            i, *isolate->factory()->the_hole_value());
      }

      // Walk all context slots to find context allocated parameters. Mark each
      // found parameter as mapped.
      ReadOnlyRoots roots{isolate};
      for (int i = 0; i < scope_info->ContextLocalCount(); i++) {
        if (!scope_info->ContextLocalIsParameter(i)) continue;
        int parameter = scope_info->ContextLocalParameterNumber(i);
        if (parameter >= mapped_count) continue;
        arguments->set_the_hole(roots, parameter);
        Tagged<Smi> slot = Smi::FromInt(scope_info->ContextHeaderLength() + i);
        parameter_map->set_mapped_entries(parameter, slot);
      }
    } else {
      // If there is no aliasing, the arguments object elements are not
      // special in any way.
      DirectHandle<FixedArray> elements = isolate->factory()->NewFixedArray(
          argument_count, AllocationType::kYoung);
      result->set_elements(*elements);
      for (int i = 0; i < argument_count; ++i) {
        elements->set(i, parameters[i]);
      }
    }
  }
  return result;
}

class HandleArguments {
 public:
  explicit HandleArguments(Handle<Object>* array) : array_(array) {}
  Tagged<Object> operator[](int index) { return *array_[index]; }

 private:
  Handle<Object>* array_;
};

class ParameterArguments {
 public:
  explicit ParameterArguments(Address parameters) : parameters_(parameters) {}
  Tagged<Object> operator[](int index) {
    return *FullObjectSlot(parameters_ - (index + 1) * kSystemPointerSize);
  }

 private:
  Address parameters_;
};

}  // namespace

RUNTIME_FUNCTION(Runtime_NewSloppyArguments) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> callee = args.at<JSFunction>(0);
  // This generic runtime function can also be used when the caller has been
  // inlined, we use the slow but accurate {GetCallerArguments}.
  int argument_count = 0;
  std::unique_ptr<Handle<Object>[]> arguments =
      GetCallerArguments(isolate, &argument_count);
  HandleArguments argument_getter(arguments.get());
  return *NewSloppyArguments(isolate, callee, argument_getter, argument_count);
}

RUNTIME_FUNCTION(Runtime_NewStrictArguments) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSFunction> callee = args.at<JSFunction>(0);
  // This generic runtime function can also be used when the caller has been
  // inlined, we use the slow but accurate {GetCallerArguments}.
  int argument_count = 0;
  std::unique_ptr<Handle<Object>[]> arguments =
      GetCallerArguments(isolate, &argument_count);
  DirectHandle<JSObject> result =
      isolate->factory()->NewArgumentsObject(callee, argument_count);
  if (argument_count) {
    DirectHandle<FixedArray> array =
        isolate->factory()->NewFixedArray(argument_count);
    DisallowGarbageCollection no_gc;
    WriteBarrierMode mode = array->GetWriteBarrierMode(no_gc);
    for (int i = 0; i < argument_count; i++) {
      array->set(i, *arguments[i], mode);
    }
    result->set_elements(*array);
  }
  return *result;
}


RUNTIME_FUNCTION(Runtime_NewRestParameter) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<JSFunction> callee = args.at<JSFunction>(0);
  int start_index =
      callee->shared()->internal_formal_parameter_count_without_receiver();
  // This generic runtime function can also be used when the caller has been
  // inlined, we use the slow but accurate {GetCallerArguments}.
  int argument_count = 0;
  std::unique_ptr<Handle<Object>[]> arguments =
      GetCallerArguments(isolate, &argument_count);
  int num_elements = std::max(0, argument_count - start_index);
  DirectHandle<JSObject> result = isolate->factory()->NewJSArray(
      PACKED_ELEMENTS, num_elements, num_elements,
      ArrayStorageAllocationMode::DONT_INITIALIZE_ARRAY_ELEMENTS);
  if (num_elements == 0) return *result;
  {
    DisallowGarbageCollection no_gc;
    Tagged<FixedArray> elements = Cast<FixedArray>(result->elements());
    WriteBarrierMode mode = elements->GetWriteBarrierMode(no_gc);
    for (int i = 0; i < num_elements; i++) {
      elements->set(i, *arguments[i + start_index], mode);
    }
  }
  return *result;
}

RUNTIME_FUNCTION(Runtime_NewClosure) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<SharedFunctionInfo> shared = args.at<SharedFunctionInfo>(0);
  Handle<FeedbackCell> feedback_cell = args.at<FeedbackCell>(1);
  Handle<Context> context(isolate->context(), isolate);
  return *Factory::JSFunctionBuilder{isolate, shared, context}
              .set_feedback_cell(feedback_cell)
              .set_allocation_type(AllocationType::kYoung)
              .Build();
}

RUNTIME_FUNCTION(Runtime_NewClosure_Tenured) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<SharedFunctionInfo> shared = args.at<SharedFunctionInfo>(0);
  Handle<FeedbackCell> feedback_cell = args.at<FeedbackCell>(1);
  Handle<Context> context(isolate->context(), isolate);
  // The caller ensures that we pretenure closures that are assigned
  // directly to properties.
  return *Factory::JSFunctionBuilder{isolate, shared, context}
              .set_feedback_cell(feedback_cell)
              .set_allocation_type(AllocationType::kOld)
              .Build();
}

RUNTIME_FUNCTION(Runtime_NewFunctionContext) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());

  DirectHandle<ScopeInfo> scope_info = args.at<ScopeInfo>(0);

  DirectHandle<Context> outer(isolate->context(), isolate);
  return *isolate->factory()->NewFunctionContext(outer, scope_info);
}

// TODO(jgruber): Rename these functions to 'New...Context'.
RUNTIME_FUNCTION(Runtime_PushWithContext) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<JSReceiver> extension_object = args.at<JSReceiver>(0);
  DirectHandle<ScopeInfo> scope_info = args.at<ScopeInfo>(1);
  DirectHandle<Context> current(isolate->context(), isolate);
  return *isolate->factory()->NewWithContext(current, scope_info,
                                             extension_object);
}

// TODO(jgruber): Rename these functions to 'New...Context'.
RUNTIME_FUNCTION(Runtime_PushCatchContext) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  DirectHandle<Object> thrown_object = args.at(0);
  DirectHandle<ScopeInfo> scope_info = args.at<ScopeInfo>(1);
  DirectHandle<Context> current(isolate->context(), isolate);
  return *isolate->factory()->NewCatchContext(current, scope_info,
                                              thrown_object);
}

// TODO(jgruber): Rename these functions to 'New...Context'.
RUNTIME_FUNCTION(Runtime_PushBlockContext) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  DirectHandle<ScopeInfo> scope_info = args.at<ScopeInfo>(0);
  DirectHandle<Context> current(isolate->context(), isolate);
  return *isolate->factory()->NewBlockContext(current, scope_info);
}


RUNTIME_FUNCTION(Runtime_DeleteLookupSlot) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<String> name = args.at<String>(0);

  int index;
  PropertyAttributes attributes;
  InitializationFlag flag;
  VariableMode mode;
  Handle<Context> context(isolate->context(), isolate);
  Handle<Object> holder = Context::Lookup(context, name, FOLLOW_CHAINS, &index,
                                          &attributes, &flag, &mode);

  // If the slot was not found the result is true.
  if (holder.is_null()) {
    // In case of JSProxy, an exception might have been thrown.
    if (isolate->has_exception()) return ReadOnlyRoots(isolate).exception();
    return ReadOnlyRoots(isolate).true_value();
  }

  // If the slot was found in a context or in module imports and exports it
  // should be DONT_DELETE.
  if (IsContext(*holder) || IsSourceTextModule(*holder)) {
    return ReadOnlyRoots(isolate).false_value();
  }

  // The slot was found in a JSReceiver, either a context extension object,
  // the global object, or the subject of a with.  Try to delete it
  // (respecting DONT_DELETE).
  Handle<JSReceiver> object = Cast<JSReceiver>(holder);
  Maybe<bool> result = JSReceiver::DeleteProperty(object, name);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}


namespace {

MaybeHandle<Object> LoadLookupSlot(Isolate* isolate, Handle<String> name,
                                   ShouldThrow should_throw,
                                   Handle<Object>* receiver_return = nullptr) {
  int index;
  PropertyAttributes attributes;
  InitializationFlag flag;
  VariableMode mode;
  Handle<Context> context(isolate->context(), isolate);
  Handle<Object> holder = Context::Lookup(context, name, FOLLOW_CHAINS, &index,
                                          &attributes, &flag, &mode);
  if (isolate->has_exception()) return MaybeHandle<Object>();

  if (!holder.is_null() && IsSourceTextModule(*holder)) {
    Handle<Object> receiver = isolate->factory()->undefined_value();
    if (receiver_return) *receiver_return = receiver;
    return SourceTextModule::LoadVariable(
        isolate, Cast<SourceTextModule>(holder), index);
  }
  if (index != Context::kNotFound) {
    DCHECK(IsContext(*holder));
    // If the "property" we were looking for is a local variable, the
    // receiver is the global object; see ECMA-262, 3rd., 10.1.6 and 10.2.3.
    Handle<Object> receiver = isolate->factory()->undefined_value();
    Handle<Object> value = handle(Cast<Context>(*holder)->get(index), isolate);
    // Check for uninitialized bindings.
    if (flag == kNeedsInitialization && IsTheHole(*value, isolate)) {
      THROW_NEW_ERROR(isolate,
                      NewReferenceError(MessageTemplate::kNotDefined, name));
    }
    DCHECK(!IsTheHole(*value, isolate));
    if (receiver_return) *receiver_return = receiver;
    return value;
  }

  // Otherwise, if the slot was found the holder is a context extension
  // object, subject of a with, or a global object.  We read the named
  // property from it.
  if (!holder.is_null()) {
    // No need to unhole the value here.  This is taken care of by the
    // GetProperty function.
    Handle<Object> value;
    ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                               Object::GetProperty(isolate, holder, name));
    if (receiver_return) {
      *receiver_return =
          (IsJSGlobalObject(*holder) || IsJSContextExtensionObject(*holder))
              ? Cast<Object>(isolate->factory()->undefined_value())
              : holder;
    }
    return value;
  }

  if (should_throw == kThrowOnError) {
    // The property doesn't exist - throw exception.
    THROW_NEW_ERROR(isolate,
                    NewReferenceError(MessageTemplate::kNotDefined, name));
  }

  // The property doesn't exist - return undefined.
  if (receiver_return) *receiver_return = isolate->factory()->undefined_value();
  return isolate->factory()->undefined_value();
}

}  // namespace


RUNTIME_FUNCTION(Runtime_LoadLookupSlot) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<String> name = args.at<String>(0);
  RETURN_RESULT_OR_FAILURE(isolate,
                           LoadLookupSlot(isolate, name, kThrowOnError));
}


RUNTIME_FUNCTION(Runtime_LoadLookupSlotInsideTypeof) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<String> name = args.at<String>(0);
  RETURN_RESULT_OR_FAILURE(isolate, LoadLookupSlot(isolate, name, kDontThrow));
}


RUNTIME_FUNCTION_RETURN_PAIR(Runtime_LoadLookupSlotForCall) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<String> name = args.at<String>(0);
  Handle<Object> value;
  Handle<Object> receiver;
  ASSIGN_RETURN_ON_EXCEPTION_VALUE(
      isolate, value, LoadLookupSlot(isolate, name, kThrowOnError, &receiver),
      MakePair(ReadOnlyRoots(isolate).exception(), Tagged<Object>()));
  return MakePair(*value, *receiver);
}

RUNTIME_FUNCTION(Runtime_LoadLookupSlotForCall_Baseline) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  // Output pair is returned into two consecutive stack slots.
  FullObjectSlot value_ret = args.slot_from_address_at(1, 0);
  FullObjectSlot receiver_ret = args.slot_from_address_at(1, -1);
  Handle<Object> receiver;
  Handle<Object> value;
  if (!LoadLookupSlot(isolate, name, kThrowOnError, &receiver)
           .ToHandle(&value)) {
    DCHECK((isolate)->has_exception());
    value_ret.store(ReadOnlyRoots(isolate).exception());
    receiver_ret.store(Tagged<Object>());
    return ReadOnlyRoots(isolate).exception();
  }
  value_ret.store(*value);
  receiver_ret.store(*receiver);
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {

MaybeHandle<Object> StoreLookupSlot(
    Isolate* isolate, Handle<Context> context, Handle<String> name,
    Handle<Object> value, LanguageMode language_mode,
    ContextLookupFlags context_lookup_flags = FOLLOW_CHAINS) {
  int index;
  PropertyAttributes attributes;
  InitializationFlag flag;
  VariableMode mode;
  bool is_sloppy_function_name;
  Handle<Object> holder =
      Context::Lookup(context, name, context_lookup_flags, &index, &attributes,
                      &flag, &mode, &is_sloppy_function_name);
  if (holder.is_null()) {
    // In case of JSProxy, an exception might have been thrown.
    if (isolate->has_exception()) return MaybeHandle<Object>();
  } else if (IsSourceTextModule(*holder)) {
    if ((attributes & READ_ONLY) == 0) {
      SourceTextModule::StoreVariable(Cast<SourceTextModule>(holder), index,
                                      value);
    } else {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kConstAssign, name));
    }
    return value;
  }
  // The property was found in a context slot.
  if (index != Context::kNotFound) {
    auto holder_context = Cast<Context>(holder);
    if (flag == kNeedsInitialization &&
        IsTheHole(holder_context->get(index), isolate)) {
      THROW_NEW_ERROR(isolate,
                      NewReferenceError(MessageTemplate::kNotDefined, name));
    }
    if ((attributes & READ_ONLY) == 0) {
      if (v8_flags.const_tracking_let && holder_context->IsScriptContext()) {
        Context::UpdateConstTrackingLetSideData(holder_context, index, value,
                                                isolate);
      }
      Cast<Context>(holder)->set(index, *value);
    } else if (!is_sloppy_function_name || is_strict(language_mode)) {
      THROW_NEW_ERROR(isolate,
                      NewTypeError(MessageTemplate::kConstAssign, name));
    }
    return value;
  }

  // Slow case: The property is not in a context slot.  It is either in a
  // context extension object, a property of the subject of a with, or a
  // property of the global object.
  Handle<JSReceiver> object;
  if (attributes != ABSENT) {
    // The property exists on the holder.
    object = Cast<JSReceiver>(holder);
  } else if (is_strict(language_mode)) {
    // If absent in strict mode: throw.
    THROW_NEW_ERROR(isolate,
                    NewReferenceError(MessageTemplate::kNotDefined, name));
  } else {
    // If absent in sloppy mode: add the property to the global object.
    object = handle(context->global_object(), isolate);
  }

  ASSIGN_RETURN_ON_EXCEPTION(isolate, value,
                             Object::SetProperty(isolate, object, name, value));
  return value;
}

}  // namespace


RUNTIME_FUNCTION(Runtime_StoreLookupSlot_Sloppy) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  Handle<Object> value = args.at(1);
  Handle<Context> context(isolate->context(), isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate,
      StoreLookupSlot(isolate, context, name, value, LanguageMode::kSloppy));
}

RUNTIME_FUNCTION(Runtime_StoreLookupSlot_Strict) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  Handle<Object> value = args.at(1);
  Handle<Context> context(isolate->context(), isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate,
      StoreLookupSlot(isolate, context, name, value, LanguageMode::kStrict));
}

// Store into a dynamic declaration context for sloppy-mode block-scoped
// function hoisting which leaks out of an eval.
RUNTIME_FUNCTION(Runtime_StoreLookupSlot_SloppyHoisting) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  Handle<Object> value = args.at(1);
  const ContextLookupFlags lookup_flags =
      static_cast<ContextLookupFlags>(DONT_FOLLOW_CHAINS);
  Handle<Context> declaration_context(isolate->context()->declaration_context(),
                                      isolate);
  RETURN_RESULT_OR_FAILURE(
      isolate, StoreLookupSlot(isolate, declaration_context, name, value,
                               LanguageMode::kSloppy, lookup_flags));
}

RUNTIME_FUNCTION(Runtime_StoreGlobalNoHoleCheckForReplLetOrConst) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<String> name = args.at<String>(0);
  DirectHandle<Object> value = args.at(1);

  DirectHandle<Context> native_context = isolate->native_context();
  DirectHandle<ScriptContextTable> script_contexts(
      native_context->script_context_table(), isolate);

  VariableLookupResult lookup_result;
  bool found = script_contexts->Lookup(name, &lookup_result);
  CHECK(found);
  DirectHandle<Context> script_context(
      script_contexts->get(lookup_result.context_index), isolate);
  // We need to initialize the side data also for variables declared with
  // VariableMode::kConst. This is because such variables can be accessed
  // by functions using the LdaContextSlot bytecode, and such accesses are not
  // regarded as "immutable" when optimizing.
  if (v8_flags.const_tracking_let) {
    Context::UpdateConstTrackingLetSideData(
        script_context, lookup_result.slot_index, value, isolate);
  }

  script_context->set(lookup_result.slot_index, *value);
  return *value;
}

}  // namespace internal
}  // namespace v8
