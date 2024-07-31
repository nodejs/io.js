// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-proxy-gen.h"

#include "src/builtins/builtins-inl.h"
#include "src/builtins/builtins-utils-gen.h"
#include "src/builtins/builtins-utils.h"
#include "src/codegen/code-stub-assembler-inl.h"
#include "src/common/globals.h"
#include "src/logging/counters.h"
#include "src/objects/js-proxy.h"
#include "src/objects/objects-inl.h"
#include "torque-generated/exported-macros-assembler.h"

namespace v8 {
namespace internal {

TNode<JSProxy> ProxiesCodeStubAssembler::AllocateProxy(
    TNode<Context> context, TNode<JSReceiver> target,
    TNode<JSReceiver> handler) {
  TVARIABLE(Map, map);

  Label callable_target(this), constructor_target(this), none_target(this),
      create_proxy(this);

  TNode<NativeContext> nativeContext = LoadNativeContext(context);

  Branch(IsCallable(target), &callable_target, &none_target);

  BIND(&callable_target);
  {
    // Every object that is a constructor is implicitly callable
    // so it's okay to nest this check here
    GotoIf(IsConstructor(target), &constructor_target);
    map = CAST(
        LoadContextElement(nativeContext, Context::PROXY_CALLABLE_MAP_INDEX));
    Goto(&create_proxy);
  }
  BIND(&constructor_target);
  {
    map = CAST(LoadContextElement(nativeContext,
                                  Context::PROXY_CONSTRUCTOR_MAP_INDEX));
    Goto(&create_proxy);
  }
  BIND(&none_target);
  {
    map = CAST(LoadContextElement(nativeContext, Context::PROXY_MAP_INDEX));
    Goto(&create_proxy);
  }

  BIND(&create_proxy);
  TNode<HeapObject> proxy = Allocate(JSProxy::kSize);
  StoreMapNoWriteBarrier(proxy, map.value());
  RootIndex empty_dict = V8_ENABLE_SWISS_NAME_DICTIONARY_BOOL
                             ? RootIndex::kEmptySwissPropertyDictionary
                             : RootIndex::kEmptyPropertyDictionary;
  StoreObjectFieldRoot(proxy, JSProxy::kPropertiesOrHashOffset, empty_dict);
  StoreObjectFieldNoWriteBarrier(proxy, JSProxy::kTargetOffset, target);
  StoreObjectFieldNoWriteBarrier(proxy, JSProxy::kHandlerOffset, handler);

  return CAST(proxy);
}

TNode<Context> ProxiesCodeStubAssembler::CreateProxyRevokeFunctionContext(
    TNode<JSProxy> proxy, TNode<NativeContext> native_context) {
  const TNode<Context> context = AllocateSyntheticFunctionContext(
      native_context, ProxyRevokeFunctionContextSlot::kProxyContextLength);
  StoreContextElementNoWriteBarrier(
      context, ProxyRevokeFunctionContextSlot::kProxySlot, proxy);
  return context;
}

TNode<JSFunction> ProxiesCodeStubAssembler::AllocateProxyRevokeFunction(
    TNode<Context> context, TNode<JSProxy> proxy) {
  const TNode<NativeContext> native_context = LoadNativeContext(context);

  const TNode<Context> proxy_context =
      CreateProxyRevokeFunctionContext(proxy, native_context);
  const TNode<Map> revoke_map = CAST(LoadContextElement(
      native_context, Context::STRICT_FUNCTION_WITHOUT_PROTOTYPE_MAP_INDEX));
  const TNode<SharedFunctionInfo> revoke_info = ProxyRevokeSharedFunConstant();

  return AllocateFunctionWithMapAndContext(revoke_map, revoke_info,
                                           proxy_context);
}

TF_BUILTIN(CallProxy, ProxiesCodeStubAssembler) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  TNode<IntPtrT> argc_ptr = ChangeInt32ToIntPtr(argc);
  auto proxy = Parameter<JSProxy>(Descriptor::kFunction);
  auto context = Parameter<Context>(Descriptor::kContext);

  CSA_DCHECK(this, IsCallable(proxy));

  PerformStackCheck(context);

  Label throw_proxy_handler_revoked(this, Label::kDeferred),
      trap_undefined(this);

  // 1. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  TNode<HeapObject> handler =
      CAST(LoadObjectField(proxy, JSProxy::kHandlerOffset));

  // 2. If handler is null, throw a TypeError exception.
  CSA_DCHECK(this, IsNullOrJSReceiver(handler));
  GotoIfNot(JSAnyIsNotPrimitive(handler), &throw_proxy_handler_revoked);

  // 3. Assert: Type(handler) is Object.
  CSA_DCHECK(this, IsJSReceiver(handler));

  // 4. Let target be the value of the [[ProxyTarget]] internal slot of O.
  TNode<Object> target = LoadObjectField(proxy, JSProxy::kTargetOffset);

  // 5. Let trap be ? GetMethod(handler, "apply").
  // 6. If trap is undefined, then
  Handle<Name> trap_name = factory()->apply_string();
  TNode<Object> trap = GetMethod(context, handler, trap_name, &trap_undefined);

  CodeStubArguments args(this, argc_ptr);
  TNode<Object> receiver = args.GetReceiver();

  // 7. Let argArray be CreateArrayFromList(argumentsList).
  TNode<JSArray> array = EmitFastNewAllArguments(
      UncheckedCast<Context>(context),
      UncheckedCast<RawPtrT>(LoadFramePointer()),
      UncheckedCast<IntPtrT>(args.GetLengthWithoutReceiver()));

  // 8. Return Call(trap, handler, «target, thisArgument, argArray»).
  TNode<Object> result = Call(context, trap, handler, target, receiver, array);
  args.PopAndReturn(result);

  BIND(&trap_undefined);
  {
    // 6.a. Return Call(target, thisArgument, argumentsList).
    TailCallBuiltin(Builtins::Call(), context, target, argc);
  }

  BIND(&throw_proxy_handler_revoked);
  { ThrowTypeError(context, MessageTemplate::kProxyRevoked, "apply"); }
}

TF_BUILTIN(ConstructProxy, ProxiesCodeStubAssembler) {
  auto argc = UncheckedParameter<Int32T>(Descriptor::kActualArgumentsCount);
  TNode<IntPtrT> argc_ptr = ChangeInt32ToIntPtr(argc);
  auto proxy = Parameter<JSProxy>(Descriptor::kTarget);
  auto new_target = Parameter<Object>(Descriptor::kNewTarget);
  auto context = Parameter<Context>(Descriptor::kContext);

  CSA_DCHECK(this, IsCallable(proxy));

  PerformStackCheck(context);

  Label throw_proxy_handler_revoked(this, Label::kDeferred),
      trap_undefined(this), not_an_object(this, Label::kDeferred);

  // 1. Let handler be the value of the [[ProxyHandler]] internal slot of O.
  TNode<HeapObject> handler =
      CAST(LoadObjectField(proxy, JSProxy::kHandlerOffset));

  // 2. If handler is null, throw a TypeError exception.
  CSA_DCHECK(this, IsNullOrJSReceiver(handler));
  GotoIfNot(JSAnyIsNotPrimitive(handler), &throw_proxy_handler_revoked);

  // 3. Assert: Type(handler) is Object.
  CSA_DCHECK(this, IsJSReceiver(handler));

  // 4. Let target be the value of the [[ProxyTarget]] internal slot of O.
  TNode<Object> target = LoadObjectField(proxy, JSProxy::kTargetOffset);

  // 5. Let trap be ? GetMethod(handler, "construct").
  // 6. If trap is undefined, then
  Handle<Name> trap_name = factory()->construct_string();
  TNode<Object> trap = GetMethod(context, handler, trap_name, &trap_undefined);

  CodeStubArguments args(this, argc_ptr);

  // 7. Let argArray be CreateArrayFromList(argumentsList).
  TNode<JSArray> array = EmitFastNewAllArguments(
      UncheckedCast<Context>(context),
      UncheckedCast<RawPtrT>(LoadFramePointer()),
      UncheckedCast<IntPtrT>(args.GetLengthWithoutReceiver()));

  // 8. Let newObj be ? Call(trap, handler, « target, argArray, newTarget »).
  TNode<Object> new_obj =
      Call(context, trap, handler, target, array, new_target);

  // 9. If Type(newObj) is not Object, throw a TypeError exception.
  GotoIf(TaggedIsSmi(new_obj), &not_an_object);
  GotoIfNot(JSAnyIsNotPrimitive(CAST(new_obj)), &not_an_object);

  // 10. Return newObj.
  args.PopAndReturn(new_obj);

  BIND(&not_an_object);
  {
    ThrowTypeError(context, MessageTemplate::kProxyConstructNonObject, new_obj);
  }

  BIND(&trap_undefined);
  {
    // 6.a. Assert: target has a [[Construct]] internal method.
    CSA_DCHECK(this, IsConstructor(CAST(target)));

    // 6.b. Return ? Construct(target, argumentsList, newTarget).
    TailCallBuiltin(Builtin::kConstruct, context, target, new_target, argc);
  }

  BIND(&throw_proxy_handler_revoked);
  { ThrowTypeError(context, MessageTemplate::kProxyRevoked, "construct"); }
}

void ProxiesCodeStubAssembler::CheckGetSetTrapResult(
    TNode<Context> context, TNode<JSReceiver> target, TNode<JSProxy> proxy,
    TNode<Name> name, TNode<Object> trap_result,
    JSProxy::AccessKind access_kind) {
  // TODO(mslekova): Think of a better name for the trap_result param.
  TNode<Map> map = LoadMap(target);
  TVARIABLE(Object, var_value);
  TVARIABLE(Uint32T, var_details);
  TVARIABLE(Object, var_raw_value);

  Label if_found_value(this), check_in_runtime(this, Label::kDeferred),
      check_passed(this);

  GotoIfNot(IsUniqueNameNoIndex(name), &check_in_runtime);
  TNode<Uint16T> instance_type = LoadInstanceType(target);
  TryGetOwnProperty(context, target, target, map, instance_type, name,
                    &if_found_value, &var_value, &var_details, &var_raw_value,
                    &check_passed, &check_in_runtime, kReturnAccessorPair);

  BIND(&if_found_value);
  {
    Label throw_non_configurable_data(this, Label::kDeferred),
        throw_non_configurable_accessor(this, Label::kDeferred),
        check_accessor(this), check_data(this);

    // If targetDesc is not undefined and targetDesc.[[Configurable]] is
    // false, then:
    GotoIfNot(IsSetWord32(var_details.value(),
                          PropertyDetails::kAttributesDontDeleteMask),
              &check_passed);

    // If IsDataDescriptor(targetDesc) is true and
    // targetDesc.[[Writable]] is false, then:
    BranchIfAccessorPair(var_raw_value.value(), &check_accessor, &check_data);

    BIND(&check_data);
    {
      TNode<BoolT> read_only = IsSetWord32(
          var_details.value(), PropertyDetails::kAttributesReadOnlyMask);
      GotoIfNot(read_only, &check_passed);

      // If SameValue(trapResult, targetDesc.[[Value]]) is false,
      // throw a TypeError exception.
      BranchIfSameValue(trap_result, var_value.value(), &check_passed,
                        &throw_non_configurable_data);
    }

    BIND(&check_accessor);
    {
      TNode<HeapObject> accessor_pair = CAST(var_raw_value.value());

      if (access_kind == JSProxy::kGet) {
        Label continue_check(this, Label::kDeferred);
        // 10.b. If IsAccessorDescriptor(targetDesc) is true and
        // targetDesc.[[Get]] is undefined, then:
        TNode<Object> getter =
            LoadObjectField(accessor_pair, AccessorPair::kGetterOffset);
        // Here we check for null as well because if the getter was never
        // defined it's set as null.
        GotoIf(IsUndefined(getter), &continue_check);
        GotoIf(IsNull(getter), &continue_check);
        Goto(&check_passed);

        // 10.b.i. If trapResult is not undefined, throw a TypeError exception.
        BIND(&continue_check);
        GotoIfNot(IsUndefined(trap_result), &throw_non_configurable_accessor);
      } else {
        // 11.b.i. If targetDesc.[[Set]] is undefined, throw a TypeError
        // exception.
        TNode<Object> setter =
            LoadObjectField(accessor_pair, AccessorPair::kSetterOffset);
        GotoIf(IsUndefined(setter), &throw_non_configurable_accessor);
        GotoIf(IsNull(setter), &throw_non_configurable_accessor);
      }
      Goto(&check_passed);
    }

    BIND(&throw_non_configurable_data);
    {
      if (access_kind == JSProxy::kGet) {
        ThrowTypeError(context, MessageTemplate::kProxyGetNonConfigurableData,
                       name, var_value.value(), trap_result);
      } else {
        ThrowTypeError(context, MessageTemplate::kProxySetFrozenData, name);
      }
    }

    BIND(&throw_non_configurable_accessor);
    {
      if (access_kind == JSProxy::kGet) {
        ThrowTypeError(context,
                       MessageTemplate::kProxyGetNonConfigurableAccessor, name,
                       trap_result);
      } else {
        ThrowTypeError(context, MessageTemplate::kProxySetFrozenAccessor, name);
      }
    }

    BIND(&check_in_runtime);
    {
      CallRuntime(Runtime::kCheckProxyGetSetTrapResult, context, name, target,
                  trap_result, SmiConstant(access_kind));
      Goto(&check_passed);
    }

    BIND(&check_passed);
  }
}

void ProxiesCodeStubAssembler::CheckHasTrapResult(TNode<Context> context,
                                                  TNode<JSReceiver> target,
                                                  TNode<JSProxy> proxy,
                                                  TNode<Name> name) {
  TNode<Map> target_map = LoadMap(target);
  TVARIABLE(Object, var_value);
  TVARIABLE(Uint32T, var_details);
  TVARIABLE(Object, var_raw_value);

  Label if_found_value(this, Label::kDeferred),
      throw_non_configurable(this, Label::kDeferred),
      throw_non_extensible(this, Label::kDeferred), check_passed(this),
      check_in_runtime(this, Label::kDeferred);

  // 9.a. Let targetDesc be ? target.[[GetOwnProperty]](P).
  GotoIfNot(IsUniqueNameNoIndex(name), &check_in_runtime);
  TNode<Uint16T> instance_type = LoadInstanceType(target);
  TryGetOwnProperty(context, target, target, target_map, instance_type, name,
                    &if_found_value, &var_value, &var_details, &var_raw_value,
                    &check_passed, &check_in_runtime, kReturnAccessorPair);

  // 9.b. If targetDesc is not undefined, then (see 9.b.i. below).
  BIND(&if_found_value);
  {
    // 9.b.i. If targetDesc.[[Configurable]] is false, throw a TypeError
    // exception.
    TNode<BoolT> non_configurable = IsSetWord32(
        var_details.value(), PropertyDetails::kAttributesDontDeleteMask);
    GotoIf(non_configurable, &throw_non_configurable);

    // 9.b.ii. Let extensibleTarget be ? IsExtensible(target).
    TNode<BoolT> target_extensible = IsExtensibleMap(target_map);

    // 9.b.iii. If extensibleTarget is false, throw a TypeError exception.
    GotoIfNot(target_extensible, &throw_non_extensible);
    Goto(&check_passed);
  }

  BIND(&throw_non_configurable);
  { ThrowTypeError(context, MessageTemplate::kProxyHasNonConfigurable, name); }

  BIND(&throw_non_extensible);
  { ThrowTypeError(context, MessageTemplate::kProxyHasNonExtensible, name); }

  BIND(&check_in_runtime);
  {
    CallRuntime(Runtime::kCheckProxyHasTrapResult, context, name, target);
    Goto(&check_passed);
  }

  BIND(&check_passed);
}

void ProxiesCodeStubAssembler::CheckDeleteTrapResult(TNode<Context> context,
                                                     TNode<JSReceiver> target,
                                                     TNode<JSProxy> proxy,
                                                     TNode<Name> name) {
  TNode<Map> target_map = LoadMap(target);
  TVARIABLE(Object, var_value);
  TVARIABLE(Uint32T, var_details);
  TVARIABLE(Object, var_raw_value);

  Label if_found_value(this, Label::kDeferred),
      throw_non_configurable(this, Label::kDeferred),
      throw_non_extensible(this, Label::kDeferred), check_passed(this),
      check_in_runtime(this, Label::kDeferred);

  // 10. Let targetDesc be ? target.[[GetOwnProperty]](P).
  GotoIfNot(IsUniqueNameNoIndex(name), &check_in_runtime);
  TNode<Uint16T> instance_type = LoadInstanceType(target);
  TryGetOwnProperty(context, target, target, target_map, instance_type, name,
                    &if_found_value, &var_value, &var_details, &var_raw_value,
                    &check_passed, &check_in_runtime, kReturnAccessorPair);

  // 11. If targetDesc is undefined, return true.
  BIND(&if_found_value);
  {
    // 12. If targetDesc.[[Configurable]] is false, throw a TypeError exception.
    TNode<BoolT> non_configurable = IsSetWord32(
        var_details.value(), PropertyDetails::kAttributesDontDeleteMask);
    GotoIf(non_configurable, &throw_non_configurable);

    // 13. Let extensibleTarget be ? IsExtensible(target).
    TNode<BoolT> target_extensible = IsExtensibleMap(target_map);

    // 14. If extensibleTarget is false, throw a TypeError exception.
    GotoIfNot(target_extensible, &throw_non_extensible);
    Goto(&check_passed);
  }

  BIND(&throw_non_configurable);
  {
    ThrowTypeError(context,
                   MessageTemplate::kProxyDeletePropertyNonConfigurable, name);
  }

  BIND(&throw_non_extensible);
  {
    ThrowTypeError(context, MessageTemplate::kProxyDeletePropertyNonExtensible,
                   name);
  }

  BIND(&check_in_runtime);
  {
    CallRuntime(Runtime::kCheckProxyDeleteTrapResult, context, name, target);
    Goto(&check_passed);
  }

  BIND(&check_passed);
}

}  // namespace internal
}  // namespace v8
