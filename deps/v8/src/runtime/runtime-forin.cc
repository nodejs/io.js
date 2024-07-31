// Copyright 2015 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/execution/isolate-inl.h"
#include "src/heap/factory.h"
#include "src/heap/heap-inl.h"  // For ToBoolean. TODO(jkummerow): Drop.
#include "src/objects/keys.h"
#include "src/objects/module.h"
#include "src/objects/objects-inl.h"

namespace v8 {
namespace internal {

namespace {

// Returns either a FixedArray or, if the given {receiver} has an enum cache
// that contains all enumerable properties of the {receiver} and its prototypes
// have none, the map of the {receiver}. This is used to speed up the check for
// deletions during a for-in.
MaybeHandle<HeapObject> Enumerate(Isolate* isolate,
                                  Handle<JSReceiver> receiver) {
  JSObject::MakePrototypesFast(receiver, kStartAtReceiver, isolate);
  FastKeyAccumulator accumulator(isolate, receiver,
                                 KeyCollectionMode::kIncludePrototypes,
                                 ENUMERABLE_STRINGS, true);
  // Test if we have an enum cache for {receiver}.
  if (!accumulator.is_receiver_simple_enum()) {
    Handle<FixedArray> keys;
    ASSIGN_RETURN_ON_EXCEPTION(
        isolate, keys,
        accumulator.GetKeys(accumulator.may_have_elements()
                                ? GetKeysConversion::kConvertToString
                                : GetKeysConversion::kNoNumbers),
        HeapObject);
    // Test again, since cache may have been built by GetKeys() calls above.
    if (!accumulator.is_receiver_simple_enum()) return keys;
  }
  DCHECK(!IsJSModuleNamespace(*receiver));
  return handle(receiver->map(), isolate);
}

// This is a slight modification of JSReceiver::HasProperty, dealing with
// the oddities of JSProxy and JSModuleNamespace in for-in filter.
MaybeHandle<Object> HasEnumerableProperty(Isolate* isolate,
                                          Handle<JSReceiver> receiver,
                                          Handle<Object> key) {
  bool success = false;
  Maybe<PropertyAttributes> result = Just(ABSENT);
  PropertyKey lookup_key(isolate, key, &success);
  if (!success) return isolate->factory()->undefined_value();
  LookupIterator it(isolate, receiver, lookup_key);
  for (;; it.Next()) {
    switch (it.state()) {
      case LookupIterator::TRANSITION:
        UNREACHABLE();
      case LookupIterator::JSPROXY: {
        // For proxies we have to invoke the [[GetOwnProperty]] trap.
        result = JSProxy::GetPropertyAttributes(&it);
        if (result.IsNothing()) return MaybeHandle<Object>();
        if (result.FromJust() == ABSENT) {
          // Continue lookup on the proxy's prototype.
          Handle<JSProxy> proxy = it.GetHolder<JSProxy>();
          Handle<Object> prototype;
          ASSIGN_RETURN_ON_EXCEPTION(isolate, prototype,
                                     JSProxy::GetPrototype(proxy), Object);
          if (IsNull(*prototype, isolate)) {
            return isolate->factory()->undefined_value();
          }
          // We already have a stack-check in JSProxy::GetPrototype.
          return HasEnumerableProperty(
              isolate, Handle<JSReceiver>::cast(prototype), key);
        } else if (result.FromJust() & DONT_ENUM) {
          return isolate->factory()->undefined_value();
        } else {
          return it.GetName();
        }
      }
      case LookupIterator::WASM_OBJECT:
        THROW_NEW_ERROR(isolate,
                        NewTypeError(MessageTemplate::kWasmObjectsAreOpaque),
                        Object);
      case LookupIterator::INTERCEPTOR: {
        result = JSObject::GetPropertyAttributesWithInterceptor(&it);
        if (result.IsNothing()) return MaybeHandle<Object>();
        if (result.FromJust() != ABSENT) return it.GetName();
        continue;
      }
      case LookupIterator::ACCESS_CHECK: {
        if (it.HasAccess()) continue;
        result = JSObject::GetPropertyAttributesWithFailedAccessCheck(&it);
        if (result.IsNothing()) return MaybeHandle<Object>();
        if (result.FromJust() != ABSENT) return it.GetName();
        return isolate->factory()->undefined_value();
      }
      case LookupIterator::TYPED_ARRAY_INDEX_NOT_FOUND:
        // TypedArray out-of-bounds access.
        return isolate->factory()->undefined_value();
      case LookupIterator::ACCESSOR: {
        if (IsJSModuleNamespace(*it.GetHolder<Object>())) {
          result = JSModuleNamespace::GetPropertyAttributes(&it);
          if (result.IsNothing()) return MaybeHandle<Object>();
          DCHECK_EQ(0, result.FromJust() & DONT_ENUM);
        }
        return it.GetName();
      }
      case LookupIterator::DATA:
        return it.GetName();
      case LookupIterator::NOT_FOUND:
        return isolate->factory()->undefined_value();
    }
    UNREACHABLE();
  }
}

}  // namespace


RUNTIME_FUNCTION(Runtime_ForInEnumerate) {
  HandleScope scope(isolate);
  DCHECK_EQ(1, args.length());
  Handle<JSReceiver> receiver = args.at<JSReceiver>(0);
  RETURN_RESULT_OR_FAILURE(isolate, Enumerate(isolate, receiver));
}


RUNTIME_FUNCTION(Runtime_ForInHasProperty) {
  HandleScope scope(isolate);
  DCHECK_EQ(2, args.length());
  Handle<JSReceiver> receiver = args.at<JSReceiver>(0);
  Handle<Object> key = args.at(1);
  Handle<Object> result;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, result, HasEnumerableProperty(isolate, receiver, key));
  return isolate->heap()->ToBoolean(!IsUndefined(*result, isolate));
}

}  // namespace internal
}  // namespace v8
