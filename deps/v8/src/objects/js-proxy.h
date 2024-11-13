// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_OBJECTS_JS_PROXY_H_
#define V8_OBJECTS_JS_PROXY_H_

#include "src/objects/js-objects.h"
#include "torque-generated/builtin-definitions.h"

// Has to be the last include (doesn't have include guards):
#include "src/objects/object-macros.h"

namespace v8 {
namespace internal {

class KeyAccumulator;

#include "torque-generated/src/objects/js-proxy-tq.inc"

// The JSProxy describes EcmaScript Harmony proxies
class JSProxy : public TorqueGeneratedJSProxy<JSProxy, JSReceiver> {
 public:
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSProxy> New(Isolate* isolate,
                                                        Handle<Object>,
                                                        Handle<Object>);

  V8_INLINE bool IsRevoked() const;
  static void Revoke(DirectHandle<JSProxy> proxy);

  // ES6 9.5.1
  static MaybeHandle<HeapObject> GetPrototype(DirectHandle<JSProxy> receiver);

  // ES6 9.5.2
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetPrototype(
      Isolate* isolate, DirectHandle<JSProxy> proxy, Handle<Object> value,
      bool from_javascript, ShouldThrow should_throw);
  // ES6 9.5.3
  V8_WARN_UNUSED_RESULT static Maybe<bool> IsExtensible(
      DirectHandle<JSProxy> proxy);

  // ES6, #sec-isarray.  NOT to be confused with %_IsArray.
  V8_WARN_UNUSED_RESULT static Maybe<bool> IsArray(Handle<JSProxy> proxy);

  // ES6 9.5.4 (when passed kDontThrow)
  V8_WARN_UNUSED_RESULT static Maybe<bool> PreventExtensions(
      DirectHandle<JSProxy> proxy, ShouldThrow should_throw);

  // ES6 9.5.5
  V8_WARN_UNUSED_RESULT static Maybe<bool> GetOwnPropertyDescriptor(
      Isolate* isolate, DirectHandle<JSProxy> proxy, Handle<Name> name,
      PropertyDescriptor* desc);

  // ES6 9.5.6
  V8_WARN_UNUSED_RESULT static Maybe<bool> DefineOwnProperty(
      Isolate* isolate, Handle<JSProxy> object, Handle<Object> key,
      PropertyDescriptor* desc, Maybe<ShouldThrow> should_throw);

  // ES6 9.5.7
  V8_WARN_UNUSED_RESULT static Maybe<bool> HasProperty(
      Isolate* isolate, DirectHandle<JSProxy> proxy, Handle<Name> name);

  // This function never returns false.
  // It returns either true or throws.
  V8_WARN_UNUSED_RESULT static Maybe<bool> CheckHasTrap(
      Isolate* isolate, Handle<Name> name, Handle<JSReceiver> target);

  // ES6 9.5.10
  V8_WARN_UNUSED_RESULT static Maybe<bool> CheckDeleteTrap(
      Isolate* isolate, Handle<Name> name, Handle<JSReceiver> target);

  // ES6 9.5.8
  V8_WARN_UNUSED_RESULT static MaybeHandle<JSAny> GetProperty(
      Isolate* isolate, DirectHandle<JSProxy> proxy, Handle<Name> name,
      Handle<Object> receiver, bool* was_found);

  enum AccessKind { kGet, kSet };

  static MaybeHandle<JSAny> CheckGetSetTrapResult(Isolate* isolate,
                                                  Handle<Name> name,
                                                  Handle<JSReceiver> target,
                                                  Handle<Object> trap_result,
                                                  AccessKind access_kind);

  // ES6 9.5.9
  V8_WARN_UNUSED_RESULT static Maybe<bool> SetProperty(
      DirectHandle<JSProxy> proxy, Handle<Name> name, Handle<Object> value,
      Handle<Object> receiver, Maybe<ShouldThrow> should_throw);

  // ES6 9.5.10 (when passed LanguageMode::kSloppy)
  V8_WARN_UNUSED_RESULT static Maybe<bool> DeletePropertyOrElement(
      DirectHandle<JSProxy> proxy, Handle<Name> name,
      LanguageMode language_mode);

  // ES6 9.5.12
  V8_WARN_UNUSED_RESULT static Maybe<bool> OwnPropertyKeys(
      Isolate* isolate, Handle<JSReceiver> receiver, Handle<JSProxy> proxy,
      PropertyFilter filter, KeyAccumulator* accumulator);

  V8_WARN_UNUSED_RESULT static Maybe<PropertyAttributes> GetPropertyAttributes(
      LookupIterator* it);

  // Dispatched behavior.
  DECL_VERIFIER(JSProxy)

  static const int kMaxIterationLimit = 100 * 1024;

  // kTargetOffset aliases with the elements of JSObject. The fact that
  // JSProxy::target is a Javascript value which cannot be confused with an
  // elements backing store is exploited by loading from this offset from an
  // unknown JSReceiver.
  static_assert(static_cast<int>(JSObject::kElementsOffset) ==
                static_cast<int>(JSProxy::kTargetOffset));

  using BodyDescriptor =
      FixedBodyDescriptor<JSReceiver::kPropertiesOrHashOffset, kSize, kSize>;

  static Maybe<bool> SetPrivateSymbol(Isolate* isolate, Handle<JSProxy> proxy,
                                      Handle<Symbol> private_name,
                                      PropertyDescriptor* desc,
                                      Maybe<ShouldThrow> should_throw);

  TQ_OBJECT_CONSTRUCTORS(JSProxy)
};

// JSProxyRevocableResult is just a JSObject with a specific initial map.
// This initial map adds in-object properties for "proxy" and "revoke".
// See https://tc39.github.io/ecma262/#sec-proxy.revocable
class JSProxyRevocableResult
    : public TorqueGeneratedJSProxyRevocableResult<JSProxyRevocableResult,
                                                   JSObject> {
 public:
  // Indices of in-object properties.
  static const int kProxyIndex = 0;
  static const int kRevokeIndex = 1;

 private:
  DISALLOW_IMPLICIT_CONSTRUCTORS(JSProxyRevocableResult);
};

}  // namespace internal
}  // namespace v8

#include "src/objects/object-macros-undef.h"

#endif  // V8_OBJECTS_JS_PROXY_H_
