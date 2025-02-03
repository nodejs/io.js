// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-async-gen.h"

#include "src/builtins/builtins-utils-gen.h"
#include "src/heap/factory-inl.h"
#include "src/objects/js-generator.h"
#include "src/objects/js-promise.h"
#include "src/objects/shared-function-info.h"

namespace v8 {
namespace internal {

#include "src/codegen/define-code-stub-assembler-macros.inc"

namespace {
// Describe fields of Context associated with the AsyncIterator unwrap closure.
class ValueUnwrapContext {
 public:
  enum Fields { kDoneSlot = Context::MIN_CONTEXT_SLOTS, kLength };
};

}  // namespace

TNode<Object> AsyncBuiltinsAssembler::Await(TNode<Context> context,
                                            TNode<JSGeneratorObject> generator,
                                            TNode<Object> value,
                                            TNode<JSPromise> outer_promise,
                                            RootIndex on_resolve_sfi,
                                            RootIndex on_reject_sfi) {
  return Await(
      context, generator, value, outer_promise,
      [&](TNode<Context> context, TNode<NativeContext> native_context) {
        auto on_resolve = AllocateRootFunctionWithContext(
            on_resolve_sfi, context, native_context);
        auto on_reject = AllocateRootFunctionWithContext(on_reject_sfi, context,
                                                         native_context);
        return std::make_pair(on_resolve, on_reject);
      });
}

TNode<Object> AsyncBuiltinsAssembler::Await(
    TNode<Context> context, TNode<JSGeneratorObject> generator,
    TNode<Object> value, TNode<JSPromise> outer_promise,
    const CreateClosures& CreateClosures) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);

  // We do the `PromiseResolve(%Promise%,value)` avoiding to unnecessarily
  // create wrapper promises. Now if {value} is already a promise with the
  // intrinsics %Promise% constructor as its "constructor", we don't need
  // to allocate the wrapper promise.
  {
    TVARIABLE(Object, var_value, value);
    Label if_slow_path(this, Label::kDeferred), if_done(this),
        if_slow_constructor(this, Label::kDeferred);
    GotoIf(TaggedIsSmi(value), &if_slow_path);
    TNode<HeapObject> value_object = CAST(value);
    const TNode<Map> value_map = LoadMap(value_object);
    GotoIfNot(IsJSPromiseMap(value_map), &if_slow_path);
    // We can skip the "constructor" lookup on {value} if it's [[Prototype]]
    // is the (initial) Promise.prototype and the @@species protector is
    // intact, as that guards the lookup path for "constructor" on
    // JSPromise instances which have the (initial) Promise.prototype.
    const TNode<Object> promise_prototype =
        LoadContextElement(native_context, Context::PROMISE_PROTOTYPE_INDEX);
    GotoIfNot(TaggedEqual(LoadMapPrototype(value_map), promise_prototype),
              &if_slow_constructor);
    Branch(IsPromiseSpeciesProtectorCellInvalid(), &if_slow_constructor,
           &if_done);

    // At this point, {value} doesn't have the initial promise prototype or
    // the promise @@species protector was invalidated, but {value} could still
    // have the %Promise% as its "constructor", so we need to check that as
    // well.
    BIND(&if_slow_constructor);
    {
      const TNode<Object> value_constructor = GetProperty(
          context, value, isolate()->factory()->constructor_string());
      const TNode<Object> promise_function =
          LoadContextElement(native_context, Context::PROMISE_FUNCTION_INDEX);
      Branch(TaggedEqual(value_constructor, promise_function), &if_done,
             &if_slow_path);
    }

    BIND(&if_slow_path);
    {
      // We need to mark the {value} wrapper as having {outer_promise}
      // as its parent, which is why we need to inline a good chunk of
      // logic from the `PromiseResolve` builtin here.
      var_value = NewJSPromise(native_context, outer_promise);
      CallBuiltin(Builtin::kResolvePromise, native_context, var_value.value(),
                  value);
      Goto(&if_done);
    }

    BIND(&if_done);
    value = var_value.value();
  }

  static const int kClosureContextSize =
      FixedArray::SizeFor(Context::MIN_CONTEXT_EXTENDED_SLOTS);
  TNode<Context> closure_context =
      UncheckedCast<Context>(AllocateInNewSpace(kClosureContextSize));
  {
    // Initialize the await context, storing the {generator} as extension.
    TNode<Map> map = CAST(
        LoadContextElement(native_context, Context::AWAIT_CONTEXT_MAP_INDEX));
    StoreMapNoWriteBarrier(closure_context, map);
    StoreObjectFieldNoWriteBarrier(
        closure_context, Context::kLengthOffset,
        SmiConstant(Context::MIN_CONTEXT_EXTENDED_SLOTS));
    const TNode<Object> empty_scope_info =
        LoadContextElement(native_context, Context::SCOPE_INFO_INDEX);
    StoreContextElementNoWriteBarrier(
        closure_context, Context::SCOPE_INFO_INDEX, empty_scope_info);
    StoreContextElementNoWriteBarrier(closure_context, Context::PREVIOUS_INDEX,
                                      native_context);
    StoreContextElementNoWriteBarrier(closure_context, Context::EXTENSION_INDEX,
                                      generator);
  }

  // Allocate and initialize resolve and reject handlers
  auto [on_resolve, on_reject] =
      CreateClosures(closure_context, native_context);

  // Deal with PromiseHooks and debug support in the runtime. This
  // also allocates the throwaway promise, which is only needed in
  // case of PromiseHooks or debugging.
  TVARIABLE(Object, var_throwaway, UndefinedConstant());
  Label if_instrumentation(this, Label::kDeferred),
      if_instrumentation_done(this);
  TNode<Uint32T> promiseHookFlags = PromiseHookFlags();
  GotoIf(IsIsolatePromiseHookEnabledOrDebugIsActiveOrHasAsyncEventDelegate(
             promiseHookFlags),
         &if_instrumentation);
#ifdef V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  // This call to NewJSPromise is to keep behaviour parity with what happens
  // in Runtime::kDebugAsyncFunctionSuspended below if native hooks are set.
  // It creates a throwaway promise that will trigger an init event and get
  // passed into Builtin::kPerformPromiseThen below.
  GotoIfNot(IsContextPromiseHookEnabled(promiseHookFlags),
            &if_instrumentation_done);
  var_throwaway = NewJSPromise(context, value);
#endif  // V8_ENABLE_JAVASCRIPT_PROMISE_HOOKS
  Goto(&if_instrumentation_done);
  BIND(&if_instrumentation);
  {
    var_throwaway =
        CallRuntime(Runtime::kDebugAsyncFunctionSuspended, native_context,
                    value, outer_promise, on_reject, generator);
    Goto(&if_instrumentation_done);
  }
  BIND(&if_instrumentation_done);

  return CallBuiltin(Builtin::kPerformPromiseThen, native_context, value,
                     on_resolve, on_reject, var_throwaway.value());
}

TNode<JSFunction> AsyncBuiltinsAssembler::CreateUnwrapClosure(
    TNode<NativeContext> native_context, TNode<Boolean> done) {
  const TNode<Context> closure_context =
      AllocateAsyncIteratorValueUnwrapContext(native_context, done);
  return AllocateRootFunctionWithContext(
      RootIndex::kAsyncIteratorValueUnwrapSharedFun, closure_context,
      native_context);
}

TNode<Context> AsyncBuiltinsAssembler::AllocateAsyncIteratorValueUnwrapContext(
    TNode<NativeContext> native_context, TNode<Boolean> done) {
  CSA_DCHECK(this, IsBoolean(done));

  TNode<Context> context = AllocateSyntheticFunctionContext(
      native_context, ValueUnwrapContext::kLength);
  StoreContextElementNoWriteBarrier(context, ValueUnwrapContext::kDoneSlot,
                                    done);
  return context;
}

TF_BUILTIN(AsyncIteratorValueUnwrap, AsyncBuiltinsAssembler) {
  auto value = Parameter<Object>(Descriptor::kValue);
  auto context = Parameter<Context>(Descriptor::kContext);

  const TNode<Object> done =
      LoadContextElement(context, ValueUnwrapContext::kDoneSlot);
  CSA_DCHECK(this, IsBoolean(CAST(done)));

  const TNode<Object> unwrapped_value =
      CallBuiltin(Builtin::kCreateIterResultObject, context, value, done);

  Return(unwrapped_value);
}

#include "src/codegen/undef-code-stub-assembler-macros.inc"

}  // namespace internal
}  // namespace v8
