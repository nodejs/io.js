// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins-utils-inl.h"
#include "src/builtins/builtins.h"
#include "src/common/message-template.h"
#include "src/execution/isolate.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/objects/keys.h"
#include "src/objects/lookup.h"
#include "src/objects/objects-inl.h"
#include "src/objects/property-descriptor.h"

namespace v8 {
namespace internal {

// -----------------------------------------------------------------------------
// ES6 section 19.1 Object Objects

// ES6 section 19.1.3.4 Object.prototype.propertyIsEnumerable ( V )
BUILTIN(ObjectPrototypePropertyIsEnumerable) {
  HandleScope scope(isolate);
  DirectHandle<JSReceiver> object;
  DirectHandle<Name> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, name, Object::ToName(isolate, args.atOrUndefined(isolate, 1)));
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, object, Object::ToObject(isolate, args.receiver()));
  Maybe<PropertyAttributes> maybe =
      JSReceiver::GetOwnPropertyAttributes(isolate, object, name);
  if (maybe.IsNothing()) return ReadOnlyRoots(isolate).exception();
  if (maybe.FromJust() == ABSENT) return ReadOnlyRoots(isolate).false_value();
  return isolate->heap()->ToBoolean((maybe.FromJust() & DONT_ENUM) == 0);
}

// ES6 section 19.1.2.3 Object.defineProperties
BUILTIN(ObjectDefineProperties) {
  HandleScope scope(isolate);
  DCHECK_LE(3, args.length());
  DirectHandle<Object> target = args.at(1);
  DirectHandle<Object> properties = args.at(2);

  RETURN_RESULT_OR_FAILURE(
      isolate, JSReceiver::DefineProperties(isolate, target, properties));
}

// ES6 section 19.1.2.4 Object.defineProperty
BUILTIN(ObjectDefineProperty) {
  HandleScope scope(isolate);
  DCHECK_LE(4, args.length());
  DirectHandle<Object> target = args.at(1);
  DirectHandle<Object> key = args.at(2);
  Handle<Object> attributes = args.at(3);

  return JSReceiver::DefineProperty(isolate, target, key, attributes);
}

namespace {

template <AccessorComponent which_accessor>
Tagged<Object> ObjectDefineAccessor(Isolate* isolate,
                                    DirectHandle<JSAny> object,
                                    DirectHandle<Object> name,
                                    DirectHandle<Object> accessor) {
  // 1. Let O be ? ToObject(this value).
  DirectHandle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  // 2. If IsCallable(getter) is false, throw a TypeError exception.
  if (!IsCallable(*accessor)) {
    MessageTemplate message =
        which_accessor == ACCESSOR_GETTER
            ? MessageTemplate::kObjectGetterExpectingFunction
            : MessageTemplate::kObjectSetterExpectingFunction;
    THROW_NEW_ERROR_RETURN_FAILURE(isolate, NewTypeError(message));
  }
  // 3. Let desc be PropertyDescriptor{[[Get]]: getter, [[Enumerable]]: true,
  //                                   [[Configurable]]: true}.
  PropertyDescriptor desc;
  if (which_accessor == ACCESSOR_GETTER) {
    desc.set_get(Cast<JSAny>(accessor));
  } else {
    DCHECK(which_accessor == ACCESSOR_SETTER);
    desc.set_set(Cast<JSAny>(accessor));
  }
  desc.set_enumerable(true);
  desc.set_configurable(true);
  // 4. Let key be ? ToPropertyKey(P).
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, name,
                                     Object::ToPropertyKey(isolate, name));
  // 5. Perform ? DefinePropertyOrThrow(O, key, desc).
  // To preserve legacy behavior, we ignore errors silently rather than
  // throwing an exception.
  Maybe<bool> success = JSReceiver::DefineOwnProperty(
      isolate, receiver, name, &desc, Just(kThrowOnError));
  MAYBE_RETURN(success, ReadOnlyRoots(isolate).exception());
  if (!success.FromJust()) {
    isolate->CountUsage(v8::Isolate::kDefineGetterOrSetterWouldThrow);
  }
  // 6. Return undefined.
  return ReadOnlyRoots(isolate).undefined_value();
}

Tagged<Object> ObjectLookupAccessor(Isolate* isolate,
                                    DirectHandle<JSAny> object,
                                    DirectHandle<Object> key,
                                    AccessorComponent component) {
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, object,
                                     Object::ToObject(isolate, object));
  // TODO(jkummerow/verwaest): PropertyKey(..., bool*) performs a
  // functionally equivalent conversion, but handles element indices slightly
  // differently. Does one of the approaches have a performance advantage?
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, key,
                                     Object::ToPropertyKey(isolate, key));
  PropertyKey lookup_key(isolate, key);
  LookupIterator it(isolate, object, lookup_key,
                    LookupIterator::PROTOTYPE_CHAIN_SKIP_INTERCEPTOR);

  for (;; it.Next()) {
    switch (it.state()) {
      case LookupIterator::INTERCEPTOR:
      case LookupIterator::TRANSITION:
        UNREACHABLE();

      case LookupIterator::ACCESS_CHECK:
        if (it.HasAccess()) continue;
        RETURN_FAILURE_ON_EXCEPTION(isolate, isolate->ReportFailedAccessCheck(
                                                 it.GetHolder<JSObject>()));
        UNREACHABLE();

      case LookupIterator::JSPROXY: {
        PropertyDescriptor desc;
        Maybe<bool> found = JSProxy::GetOwnPropertyDescriptor(
            isolate, it.GetHolder<JSProxy>(), it.GetName(), &desc);
        MAYBE_RETURN(found, ReadOnlyRoots(isolate).exception());
        if (found.FromJust()) {
          if (component == ACCESSOR_GETTER && desc.has_get()) {
            return *desc.get();
          }
          if (component == ACCESSOR_SETTER && desc.has_set()) {
            return *desc.set();
          }
          return ReadOnlyRoots(isolate).undefined_value();
        }
        DirectHandle<JSPrototype> prototype;
        ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
            isolate, prototype, JSProxy::GetPrototype(it.GetHolder<JSProxy>()));
        if (IsNull(*prototype, isolate)) {
          return ReadOnlyRoots(isolate).undefined_value();
        }
        return ObjectLookupAccessor(isolate, prototype, key, component);
      }
      case LookupIterator::WASM_OBJECT:
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
      case LookupIterator::DATA:
      case LookupIterator::NOT_FOUND:
        return ReadOnlyRoots(isolate).undefined_value();

      case LookupIterator::ACCESSOR: {
        DirectHandle<Object> maybe_pair = it.GetAccessors();
        if (IsAccessorPair(*maybe_pair)) {
          DirectHandle<NativeContext> holder_realm(
              it.GetHolder<JSReceiver>()->GetCreationContext().value(),
              isolate);
          return *AccessorPair::GetComponent(
              isolate, holder_realm, Cast<AccessorPair>(maybe_pair), component);
        }
        continue;
      }
    }
    UNREACHABLE();
  }
}

}  // namespace

// ES6 B.2.2.2 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__defineGetter__
BUILTIN(ObjectDefineGetter) {
  HandleScope scope(isolate);
  DirectHandle<JSAny> object = args.at<JSAny>(0);  // Receiver.
  DirectHandle<Object> name = args.at(1);
  DirectHandle<Object> getter = args.at(2);
  return ObjectDefineAccessor<ACCESSOR_GETTER>(isolate, object, name, getter);
}

// ES6 B.2.2.3 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__defineSetter__
BUILTIN(ObjectDefineSetter) {
  HandleScope scope(isolate);
  DirectHandle<JSAny> object = args.at<JSAny>(0);  // Receiver.
  DirectHandle<Object> name = args.at(1);
  DirectHandle<Object> setter = args.at(2);
  return ObjectDefineAccessor<ACCESSOR_SETTER>(isolate, object, name, setter);
}

// ES6 B.2.2.4 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__lookupGetter__
BUILTIN(ObjectLookupGetter) {
  HandleScope scope(isolate);
  DirectHandle<JSAny> object = args.at<JSAny>(0);
  DirectHandle<Object> name = args.at(1);
  return ObjectLookupAccessor(isolate, object, name, ACCESSOR_GETTER);
}

// ES6 B.2.2.5 a.k.a.
// https://tc39.github.io/ecma262/#sec-object.prototype.__lookupSetter__
BUILTIN(ObjectLookupSetter) {
  HandleScope scope(isolate);
  DirectHandle<JSAny> object = args.at<JSAny>(0);
  DirectHandle<Object> name = args.at(1);
  return ObjectLookupAccessor(isolate, object, name, ACCESSOR_SETTER);
}

// ES6 section 19.1.2.5 Object.freeze ( O )
BUILTIN(ObjectFreeze) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);
  if (IsJSReceiver(*object)) {
    MAYBE_RETURN(JSReceiver::SetIntegrityLevel(
                     isolate, Cast<JSReceiver>(object), FROZEN, kThrowOnError),
                 ReadOnlyRoots(isolate).exception());
  }
  return *object;
}

// ES6 section B.2.2.1.1 get Object.prototype.__proto__
BUILTIN(ObjectPrototypeGetProto) {
  HandleScope scope(isolate);
  // 1. Let O be ? ToObject(this value).
  DirectHandle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, receiver, Object::ToObject(isolate, args.receiver()));

  // 2. Return ? O.[[GetPrototypeOf]]().
  RETURN_RESULT_OR_FAILURE(isolate,
                           JSReceiver::GetPrototype(isolate, receiver));
}

// ES6 section B.2.2.1.2 set Object.prototype.__proto__
BUILTIN(ObjectPrototypeSetProto) {
  HandleScope scope(isolate);
  // 1. Let O be ? RequireObjectCoercible(this value).
  DirectHandle<Object> object = args.receiver();
  if (IsNullOrUndefined(*object, isolate)) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kCalledOnNullOrUndefined,
                              isolate->factory()->NewStringFromAsciiChecked(
                                  "set Object.prototype.__proto__")));
  }

  // 2. If Type(proto) is neither Object nor Null, return undefined.
  DirectHandle<Object> proto = args.at(1);
  if (!IsNull(*proto, isolate) && !IsJSReceiver(*proto)) {
    return ReadOnlyRoots(isolate).undefined_value();
  }

  // 3. If Type(O) is not Object, return undefined.
  if (!IsJSReceiver(*object)) return ReadOnlyRoots(isolate).undefined_value();
  DirectHandle<JSReceiver> receiver = Cast<JSReceiver>(object);

  // 4. Let status be ? O.[[SetPrototypeOf]](proto).
  // 5. If status is false, throw a TypeError exception.
  MAYBE_RETURN(
      JSReceiver::SetPrototype(isolate, receiver, proto, true, kThrowOnError),
      ReadOnlyRoots(isolate).exception());

  // Return undefined.
  return ReadOnlyRoots(isolate).undefined_value();
}

namespace {

Tagged<Object> GetOwnPropertyKeys(Isolate* isolate, BuiltinArguments args,
                                  PropertyFilter filter) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);
  DirectHandle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));
  DirectHandle<FixedArray> keys;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, keys,
      KeyAccumulator::GetKeys(isolate, receiver, KeyCollectionMode::kOwnOnly,
                              filter, GetKeysConversion::kConvertToString));
  return *isolate->factory()->NewJSArrayWithElements(keys);
}

}  // namespace

// ES6 section 19.1.2.8 Object.getOwnPropertySymbols ( O )
BUILTIN(ObjectGetOwnPropertySymbols) {
  return GetOwnPropertyKeys(isolate, args, SKIP_STRINGS);
}

// ES6 section 19.1.2.12 Object.isFrozen ( O )
BUILTIN(ObjectIsFrozen) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);
  Maybe<bool> result = IsJSReceiver(*object)
                           ? JSReceiver::TestIntegrityLevel(
                                 isolate, Cast<JSReceiver>(object), FROZEN)
                           : Just(true);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

// ES6 section 19.1.2.13 Object.isSealed ( O )
BUILTIN(ObjectIsSealed) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);
  Maybe<bool> result = IsJSReceiver(*object)
                           ? JSReceiver::TestIntegrityLevel(
                                 isolate, Cast<JSReceiver>(object), SEALED)
                           : Just(true);
  MAYBE_RETURN(result, ReadOnlyRoots(isolate).exception());
  return isolate->heap()->ToBoolean(result.FromJust());
}

BUILTIN(ObjectGetOwnPropertyDescriptors) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);

  DirectHandle<JSReceiver> receiver;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, receiver,
                                     Object::ToObject(isolate, object));

  DirectHandle<FixedArray> keys;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, keys,
      KeyAccumulator::GetKeys(isolate, receiver, KeyCollectionMode::kOwnOnly,
                              ALL_PROPERTIES,
                              GetKeysConversion::kConvertToString));

  DirectHandle<JSObject> descriptors =
      isolate->factory()->NewJSObject(isolate->object_function());

  for (int i = 0; i < keys->length(); ++i) {
    DirectHandle<Name> key(Cast<Name>(keys->get(i)), isolate);
    PropertyDescriptor descriptor;
    Maybe<bool> did_get_descriptor = JSReceiver::GetOwnPropertyDescriptor(
        isolate, receiver, key, &descriptor);
    MAYBE_RETURN(did_get_descriptor, ReadOnlyRoots(isolate).exception());

    if (!did_get_descriptor.FromJust()) continue;
    DirectHandle<Object> from_descriptor = descriptor.ToObject(isolate);

    Maybe<bool> success = JSReceiver::CreateDataProperty(
        isolate, descriptors, key, from_descriptor, Just(kDontThrow));
    CHECK(success.FromJust());
  }

  return *descriptors;
}

// ES6 section 19.1.2.17 Object.seal ( O )
BUILTIN(ObjectSeal) {
  HandleScope scope(isolate);
  DirectHandle<Object> object = args.atOrUndefined(isolate, 1);
  if (IsJSReceiver(*object)) {
    MAYBE_RETURN(JSReceiver::SetIntegrityLevel(
                     isolate, Cast<JSReceiver>(object), SEALED, kThrowOnError),
                 ReadOnlyRoots(isolate).exception());
  }
  return *object;
}

}  // namespace internal
}  // namespace v8
